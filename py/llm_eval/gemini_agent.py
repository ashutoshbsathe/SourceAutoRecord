"""The frozen-VLM agent + eval loop: Gemini drives a chamber to a `.trajectory`.

Gemini is the verb source -- the REPL loop with the model in place of stdin. Each
step sends the annotated frame + telemetry to one stateful chat, gets back a
fenced ```json {reasoning, verb} block (verb = a plain command string like the
REPL types), validates it against the live percept, re-prompts in-session on
rejection, then steps + records. Stops on the engine's chamber-complete signal,
`done`, or the budget.
"""

import json
import os
import re
from dataclasses import dataclass

from google import genai
from google.genai import types
from p2harness import harness_pb2
from p2harness import macro_grammar

from . import trajectory_pb2
from .trajectory_io import TrajectoryWriter
from .trajectory_io import encode_png
from .trajectory_io import make_call
from .trajectory_io import make_step

MODEL = 'gemini-3.5-flash'

# Retry 429s (rate limits) and transient 5xx with exponential backoff -- free-tier
# throttling is the norm here. Recovers per-minute throttling; the daily quota won't.
_RETRY = types.HttpRetryOptions(
    attempts=6,
    initial_delay=4.0,
    max_delay=90.0,
    exp_base=2.0,
    jitter=1.0,
    http_status_codes=[429, 500, 503],
)

# TODO: exit elevator should be potentially a target the model can go_to? also
# agent should probably spawn in entrance airlock? `move forward 100` as the
# "default" first step?. also maybe telling the model that `go_to` only works
# with the "tagged" entities? the model seems to be using `go_to` for the exit
# elevator as well which feels stupid?

_SYSTEM = """You are an agent solving a Portal 2 test chamber. Goal: explore the
chamber and reach its exit. You are not told where the exit is -- look around,
move through the chamber, and use the marked entities to find your way out.

Each turn you get an annotated screenshot and telemetry: your position, `holding`
(the mark you carry, or nothing), the result of your last action, and a list of
marked entities -- each with an integer `mark` (also drawn on the frame), class,
`name` (its in-game targetname, e.g. `@exit_door`, `@entrance_door`,
`@exit_airlock_door` -- read this to tell otherwise-identical entities apart and
to find the way OUT), position, distance, bearing, and state.

Verbs:
{verbs}

One valid example of every verb:
{examples}

Notes:
- `mark` is an entity's integer label. Distance is 2D ground distance in game
  units -- the metric `go_to` drives to, so when a distance is near 0 you have
  arrived. Bearing is degrees off your facing (+ = left).
- {caveat}
- `last_result` is feedback. SUCCESS/COMPLETED = the verb did what you asked.
  REACHED_PROJECTION = go_to walked to the nearest reachable spot (beside a solid
  target, or short of an out-of-reach ledge; `moved` units shows how far) -- you
  are at a NEW position now, so re-read the percept and re-plan; do NOT just
  repeat the same move. A failure like STUCK/NO_ROUTE/WALL/EDGE/BAD_MARK means you
  did NOT reach it -- try a different approach (e.g. `look` for an opening, then
  `move`, or pick a nearer mark).
  SEATED = a held cube is now resting on the button you released onto (success).
  NOT_FAIR/NOT_SEATED = the cube was only dropped, not placed -- get within reach
  of the button (and clear of walls/fizzlers) and release again.
- Lasers (interpose / redirect_to): to redirect a beam, `pick_up` a reflector
  cube, `interpose` it onto an emitter's beam, then `redirect_to` aim it at a
  laser target to power it (or pass `interpose` a target mark to do both at
  once). ON_BEAM = the cube is on the beam (now `redirect_to` a target). POWERED
  = the target is lit (success). NOT_INTERCEPTING = the cube missed the beam (try
  a different percent along it). NO_FLOOR/NOT_REACHABLE = no floor or no walk path
  at that beam point (pick another percent). NOT_SEATED (from `redirect_to`) = the
  cube is not on a beam (interpose it first). NOT_POWERED = on the beam but the
  aim misses the target. OUT_OF_REACH = you are not next to the cube -- `go_to` it
  first, then `redirect_to`.
- Portals (place_portal / pass_through / jump_into): portalable WALL panels are
  labeled `S1`, `S2`, ... `place_portal blue S3` drops a blue portal on panel S3;
  append `@u,v` (fractions 0-1) to aim at a point on the panel, e.g. `S3@0.5,0.9`
  near its top edge. Read the axes off the panel's color-coded grid: it is WHITE
  at (0,0), reddens along +u (toward u=1) and blues along +v (toward v=1), and is
  magenta at (1,1) -- so a redder cell means higher u, a bluer cell higher v.
  Place BOTH a blue and an orange portal -- the pair auto-links
  into one doorway (re-placing a color moves that portal). Once a linked pair is
  down (`Pb` = blue, `Po` = orange in the marks), `pass_through Pb` walks you in
  the blue portal and out the orange. If a portal is on the FLOOR, `jump_into Pb`
  instead flings you: you fall in and shoot out the linked portal, freezing
  mid-air (`wait` to resume) -- stand on a ledge ABOVE a floor portal first for a
  big fling. Use a portal pair to cross a gap, reach a high ledge, or get somewhere
  walking can't. PLACED = portal down; NOT_PORTALABLE / CANT_FIT / OVERLAP / NO_LOS
  = that panel/point won't take it, pick another. NOT_GROUND (from jump_into) = the
  portal is on a wall, use pass_through.
- A fizzler (`trigger_portal_cleanser`) with state `active: true` is ON -- it
  destroys you, cubes, and portals passing through it; `active: false` is OFF and
  safe to cross (its box is hidden on the frame when off). Fizzlers toggle with
  their linked button/laser, so powering one off can open a path.
- The environment decides when the chamber is solved and ends the run for you --
  you do NOT judge success yourself. Just keep making progress toward the exit.
  The exit is usually an elevator that carries you out over a few seconds, so if
  you believe you've reached it but the run hasn't ended, `wait 250` near the
  exit (200-300 ticks) to let the elevator finish. 
- A SINGLE `wait 250` is sufficient to trigger the exit, if it doesn't, make sure your 
  position is appropriate and try again after applying a few micro corrections. Ensure you
  are in the correct exit environment (walk towards the CYLINDRICAL exit elevator using the 
  stairs, wait for it to open, walk in and ONLY THEN use `wait 250`).
- Use `done` only to stop when you are truly stuck with no action left to try;
  it is a give-up, not a win -- the environment, not `done`, marks a real solve.
- Your output should be STRICTLY in the following format:
<begin expected format, yes 3 backticks with json is the expected starting of the response>
```json
{{
    "reasoning": "foo bar therefore let me try baz",
    "verb": "move forward 10"
}}
```
<end expected format, ends with 3 backticks>
You MAY choose to add additional things in the response as well HOWEVER this JSON block must be in PRISTINE condition.

If you give up -- stuck, with no action left to try -- the output block must be:
```json
{{
    "reasoning": "stuck, no action left to try",
    "verb": "done"
}}
```
"""


def _percept_text(obs):
    """Format the player + marked-entity telemetry the model reads."""
    px, py, pz = obs.player
    lines = [
        f'player=({px:.0f},{py:.0f},{pz:.0f}) holding={obs.held_mark or "nothing"}',
        f'last_result={obs.result.result_code} {obs.result.detail}'.strip(),
        'marks:',
    ]
    for m in obs.marks:
        x, y, z = m['pos']
        name = m['name']
        tag = f' "{name}"' if name else ''
        lines.append(
            f'  [{m["mark"]}] {m["class"]}{tag} pos=({x:.0f},{y:.0f},{z:.0f}) '
            f'dist={m["dist"]:.0f} bearing={m["bearing"]:.0f} state={m["state"]}'
        )
    return '\n'.join(lines)


def _usage(resp):
    """Map a Gemini response's token counts to a TokenUsage."""
    u = resp.usage_metadata
    if u is None:
        return trajectory_pb2.TokenUsage()
    return trajectory_pb2.TokenUsage(
        input=u.prompt_token_count or 0,
        output=(u.candidates_token_count or 0) + (u.thoughts_token_count or 0),
        cached=u.cached_content_token_count or 0,
        image=_img_tokens(u),
        thinking=u.thoughts_token_count or 0,
    )


def _img_tokens(u):
    """Image-modality input tokens from a usage_metadata, or 0."""
    details = getattr(u, 'prompt_tokens_details', None) if u is not None else None
    for d in details or []:
        if 'IMAGE' in str(d.modality).upper():
            return d.token_count or 0
    return 0


def _parts(resp):
    """Split a response into (answer_text, thinking_text)."""
    answer, thinking = [], []
    for cand in resp.candidates or []:
        for part in (cand.content.parts if cand.content else None) or []:
            if not part.text:
                continue
            (thinking if part.thought else answer).append(part.text)
    return ''.join(answer), '\n'.join(thinking)


def _token_line(usage):
    """Compact 'in N (img M) · out N (think M) · cached N' for one response."""
    line = f'in {usage.input}' + (f' (img {usage.image})' if usage.image else '')
    out = f'  ·  out {usage.output}' + (
        f' (think {usage.thinking})' if usage.thinking else ''
    )
    line += out
    if usage.cached:
        line += f'  ·  cached {usage.cached}'
    return line


def _indent(text, prefix='    '):
    """Indent every line of `text` for visual grouping under a header."""
    return '\n'.join(prefix + line for line in text.splitlines())


_ACTION_BLOCK = re.compile(r'```(?:json)?\s*(\{.*?\})\s*```', re.DOTALL)


def _extract_action(text):
    """Pull the {reasoning, verb} object from a fenced ```json block (or None)."""
    for block in reversed(_ACTION_BLOCK.findall(text)):
        try:
            obj = json.loads(block)
        except json.JSONDecodeError:
            continue
        if isinstance(obj, dict) and obj.get('verb'):
            return obj
    return None


@dataclass
class AgentAction:
    """One step's outcome: the accepted macro (None if gave up) + every LLM call."""

    macro: harness_pb2.MacroRequest | None
    calls: list  # the Call protos: rejected tries in order, then the accepted one


class GeminiAgent:
    """A Gemini chat that returns one validated macro per observation."""

    def __init__(self, max_retries=3):
        """Open one Gemini chat for this run (reads GEMINI_API_KEY)."""
        api_key = os.environ.get('GEMINI_API_KEY')
        if not api_key:
            raise RuntimeError('GEMINI_API_KEY not set (put it in the repo .env)')
        self.max_retries = max_retries
        self.tokens_in = 0
        self.tokens_out = 0
        self.system = _SYSTEM.format(
            verbs='\n'.join(macro_grammar.verb_signatures()),
            examples='\n'.join(macro_grammar.verb_examples()),
            caveat=macro_grammar.CAVEAT,
        )
        # Keep the client referenced -- a temporary would be GC'd, closing its
        # HTTP client and breaking the chat ("client has been closed").
        self.client = genai.Client(
            api_key=api_key,
            http_options=types.HttpOptions(retry_options=_RETRY),
        )
        self.chat = self.client.chats.create(
            model=MODEL,
            config=types.GenerateContentConfig(
                system_instruction=self.system,
                thinking_config=types.ThinkingConfig(
                    thinking_level=types.ThinkingLevel.MEDIUM,
                    include_thoughts=True,
                ),
            ),
        )

    def __call__(self, obs):
        """Return an AgentAction for `obs` (macro=None if every retry was rejected)."""
        percept = _percept_text(obs)
        print('  ▶ sent (frame + telemetry)', flush=True)
        print(_indent(percept), flush=True)
        message = [
            types.Part.from_bytes(data=encode_png(obs.frame), mime_type='image/png'),
            percept,
        ]
        sent = percept  # the exact text we send this call (a re-prompt on retries)
        calls = []
        for attempt in range(self.max_retries + 1):
            resp = self.chat.send_message(message=message)
            raw, thinking = _parts(resp)
            usage = _usage(resp)
            self.tokens_in += usage.input
            self.tokens_out += usage.output
            print(f'  ◀ gemini  (try {attempt + 1})  {_token_line(usage)}')
            print(_indent(raw), flush=True)
            action = _extract_action(raw)
            if action is None:
                print('    ↳ no parseable action block; re-prompting', flush=True)
                calls.append(
                    make_call(
                        sent,
                        thinking,
                        raw,
                        '',
                        usage,
                        rejection_reason='no parseable ```json action block',
                    )
                )
                sent = (
                    'No parseable ```json action block in your reply. Return one '
                    '```json {"reasoning": "...", "verb": "..."} ``` block.'
                )
                message = [sent]
                continue
            reasoning = action.get('reasoning', '')
            req = macro_grammar.validate(
                action.get('verb', ''), obs.marks, obs.held_mark
            )
            if isinstance(req, harness_pb2.MacroRequest):
                calls.append(
                    make_call(
                        sent, thinking, raw, reasoning, usage, accepted=True, macro=req
                    )
                )
                return AgentAction(req, calls)
            print(f'    ↳ rejected: {req}; re-prompting', flush=True)
            calls.append(
                make_call(
                    sent, thinking, raw, reasoning, usage, rejection_reason=str(req)
                )
            )
            sent = f'That action was rejected: {req}. Return a corrected action.'
            message = [sent]
        print('    ↳ gave up (max retries exhausted)', flush=True)
        return AgentAction(None, calls)


def _write(writer, index, obs, calls, terminal):
    """Serialize one (observation, calls) step to the trajectory."""
    writer.write_step(make_step(index, obs, calls, terminal))


def run_eval(session, agent, cfg, out_path, max_steps=30, corridor_ticks=0):
    """Drive the chamber with `agent`, recording each step. Returns the terminal.

    cfg holds the map name. The terminal (also on the last step) is
    SOLVED | DONE | BUDGET | GAVE_UP -- SOLVED comes from the engine's
    chamber-complete signal, not the agent. corridor_ticks > 0 walks the player
    out of the spawn airlock before step 0, so the agent doesn't burn turns on
    the entrance corridor.
    """
    if session.harness.shm is None:
        raise RuntimeError('frame capture needs a video-mode instance (no SHM mapped)')
    session.harness.execute_command('sar_harness_annotate 1')
    obs = session.reset(cfg['map'], capture_frame=True)
    if corridor_ticks > 0:
        obs = session.clear_entrance_corridor(corridor_ticks, capture_frame=True)

    header = trajectory_pb2.TrajectoryHeader(
        map=cfg['map'],
        model=MODEL,
        system_prompt=getattr(agent, 'system', ''),
        grammar='\n'.join(macro_grammar.verb_signatures()),
    )

    print(f'\n  recording -> {out_path}', flush=True)
    terminal = ''
    with TrajectoryWriter(out_path, header) as writer:
        for index in range(max_steps):
            print(f'\n{"═" * 16}  step {index}  {"═" * 16}', flush=True)
            seen = obs
            act = agent(seen)
            if act.macro is None:
                print(
                    '  ✗ no valid action after retries; recording GAVE_UP', flush=True
                )
                terminal = 'GAVE_UP'
                _write(writer, index, seen, act.calls, terminal)
                break
            obs = session.step(act.macro, capture_frame=True)
            mr = obs.result
            act.calls[-1].result = mr.SerializeToString()  # the accepted call's result
            sym = '✓' if mr.ok else '✗'
            print(f'  {sym} {act.macro.verb} → {mr.result_code} {mr.detail}'.rstrip())
            if obs.state.chamber_complete:
                terminal = 'SOLVED'
            elif act.macro.verb == 'done':
                terminal = 'DONE'
            elif index == max_steps - 1:
                terminal = 'BUDGET'
            # Write each step as it happens, so a rate-limit/crash mid-run still
            # leaves a complete trajectory up to the last finished step.
            _write(writer, index, seen, act.calls, terminal)
            if terminal:
                break
    total = (
        f'in {getattr(agent, "tokens_in", 0)} / out {getattr(agent, "tokens_out", 0)}'
    )
    print(f'\n  {terminal or "GAVE_UP"}  ·  tokens {total}  ·  {out_path}', flush=True)
    return terminal or 'GAVE_UP'
