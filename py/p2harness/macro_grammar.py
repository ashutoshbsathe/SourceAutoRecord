"""Macro grammar: the single source of truth for the act vocabulary.

One declarative spec (VERB_SPECS) drives both the LLM tool schema and local
validation, so the prompt and the gate can't drift. validate() checks a model
action against the grammar AND the live percept -- unknown verb, bad arg
type/range, a mark that doesn't exist or is the wrong kind for the verb,
holding-state -- and on failure returns a structured string fed straight back to
the model, with no gRPC round-trip and no game step (see
llm_percept_act_grammar.md).
"""

from dataclasses import dataclass

from . import harness_pb2

# Mirror the executor's clamps (MacroExecutor.cpp) so a bad arg is rejected here
# instead of silently clamped server-side. Keep this short.
WAIT_MAX_TICKS = 600  # kMaxWaitTicks
MOVE_MAX_TICKS = 400  # kGoToMaxTicks
LOOK_SNAP = 15  # degree step: keeps the model at reasoning, not aiming, altitude
PITCH_LIMIT = 89  # kPitchLimit

MOVE_DIRS = ('forward', 'back', 'left', 'right')

# Classes pick_up can carry (mirrors IsGrabbableClass in MacroExecutor.cpp).
GRABBABLE_CLASSES = frozenset(
    {'prop_weighted_cube', 'prop_monster_box', 'npc_portal_turret_floor'}
)


@dataclass(frozen=True)
class Verb:
    """One verb's argument spec, shared by validation and the tool schema."""

    doc: str
    mark: str | None = None  # 'required' | 'grabbable' | 'optional' | None
    ticks_max: int | None = None  # set => required `ticks` in [1, ticks_max]
    dirs: tuple | None = None  # set => required `dir` from this set
    look: bool = False  # set => required signed `yaw` + optional `pitch` (15deg)


VERB_SPECS = {
    'aim_at': Verb('Point the view at a mark.', mark='required'),
    'go_to': Verb('Walk in a straight line to a mark.', mark='required'),
    'interact': Verb(
        'Walk to a mark and press it (+use) -- a button or switch.', mark='required'
    ),
    'pick_up': Verb('Grab the cube/box/turret at a mark.', mark='grabbable'),
    'release': Verb(
        'Drop the carried object toward a mark, or at your feet if omitted.',
        mark='optional',
    ),
    'move': Verb(
        'Hold a movement direction for N ticks.',
        dirs=MOVE_DIRS,
        ticks_max=MOVE_MAX_TICKS,
    ),
    'look': Verb('Turn the view by signed degrees (snapped to 15).', look=True),
    'wait': Verb('Let the frozen world run for N ticks.', ticks_max=WAIT_MAX_TICKS),
    'done': Verb('Declare the task complete (success = exit proximity).'),
}

VERBS = frozenset(VERB_SPECS)


def build_macro(verb, args):
    """Map a verb + positional string args to a MacroRequest (raises on bad args).

    The imperative twin of validate(): no percept checks, just the arg shapes a
    typed or scripted command supplies.
    """
    m = harness_pb2.MacroRequest(verb=verb)
    if verb in ('aim_at', 'go_to', 'pick_up', 'interact'):
        m.mark = int(args[0])
    elif verb == 'release':
        m.mark = int(args[0]) if args else 0
    elif verb == 'move':
        m.dir = args[0]
        m.ticks = int(args[1])
    elif verb == 'look':
        m.yaw = int(args[0])
        m.pitch = int(args[1]) if len(args) > 1 else 0
    elif verb == 'wait':
        m.ticks = int(args[0])
    return m


def _is_int(v):
    """True for a real integer (JSON booleans are ints in Python -- exclude them)."""
    return isinstance(v, int) and not isinstance(v, bool)


def _snap(deg, step):
    """Round signed degrees to the nearest `step` (the coarse look lexer)."""
    return int(round(deg / step)) * step


def _check_mark(verb, spec, call, by_mark, req):
    """Validate the mark arg and set req.mark. Returns an error string or None."""
    mark = call.get('mark', 0)
    if spec.mark == 'optional' and not mark:
        return None  # release with no mark = drop at the player's feet
    if not _is_int(mark) or mark <= 0:
        return f'{verb}: requires a positive integer mark, got {mark!r}'
    ent = by_mark.get(mark)
    if ent is None:
        return f'{verb}: no entity with mark {mark}; marks present: {sorted(by_mark)}'
    if spec.mark == 'grabbable' and ent['class'] not in GRABBABLE_CLASSES:
        return f'{verb}: mark {mark} is a {ent["class"]}, not grabbable'
    req.mark = mark
    return None


def validate(call, entities, held_mark=None):
    """Check one model action against the grammar and the live percept.

    Args:
        call: the model's action object -- {'verb': str, plus per-verb 'mark' /
            'ticks' / 'dir' / 'yaw' / 'pitch'}.
        entities: parse_snapshot's marked-entity list, for mark resolution.
        held_mark: the driver-tracked carried mark, or None if empty-handed
            (there is no engine held-flag -- see the grammar doc).

    Returns:
        A ready-to-send MacroRequest, or a structured error string to feed back
        to the model (no gRPC, no game step).
    """
    if not isinstance(call, dict):
        return f'expected a JSON object, got {type(call).__name__}'
    verb = call.get('verb')
    spec = VERB_SPECS.get(verb)
    if spec is None:
        return f'unknown verb {verb!r}; valid: {", ".join(VERB_SPECS)}'

    req = harness_pb2.MacroRequest(verb=verb)
    by_mark = {e['mark']: e for e in entities}

    if spec.mark:
        err = _check_mark(verb, spec, call, by_mark, req)
        if err:
            return err
    if verb == 'pick_up' and held_mark is not None:
        return f'pick_up: already holding mark {held_mark}; release it first'
    if verb == 'release' and held_mark is None:
        return 'release: nothing is being held'
    if spec.ticks_max is not None:
        ticks = call.get('ticks')
        if not _is_int(ticks) or not 1 <= ticks <= spec.ticks_max:
            return (
                f'{verb}: ticks must be an integer in [1, {spec.ticks_max}], '
                f'got {ticks!r}'
            )
        req.ticks = ticks
    if spec.dirs is not None:
        d = call.get('dir')
        if d not in spec.dirs:
            return f'{verb}: dir must be one of {list(spec.dirs)}, got {d!r}'
        req.dir = d
    if spec.look:
        yaw, pitch = call.get('yaw'), call.get('pitch', 0)
        if not _is_int(yaw) or not _is_int(pitch):
            return (
                f'look: yaw and pitch must be integer degrees, '
                f'got yaw={yaw!r} pitch={pitch!r}'
            )
        req.yaw = _snap(yaw, LOOK_SNAP)
        req.pitch = max(-PITCH_LIMIT, min(PITCH_LIMIT, _snap(pitch, LOOK_SNAP)))
    return req


def _signature(verb, spec):
    """Human-readable 'verb(args)' for the prompt / tool description."""
    args = []
    if spec.mark == 'optional':
        args.append('[mark]')
    elif spec.mark:
        args.append('mark')
    if spec.dirs is not None:
        args.append('dir')
    if spec.ticks_max is not None:
        args.append('ticks')
    if spec.look:
        args += ['yaw', '[pitch]']
    return f'{verb}({", ".join(args)})'


def verb_signatures():
    """One 'verb(args): doc' line per verb for the system prompt (single source)."""
    return [f'{_signature(v, s)}: {s.doc}' for v, s in VERB_SPECS.items()]


def tool_schema():
    """JSON Schema for the single `act` tool the model calls each step.

    Derived from VERB_SPECS (the same source as validate). Per-verb arg
    requirements live in the verb description -- plain JSON Schema can't express
    them conditionally and validate() is the real gate -- so every arg is
    optional here.
    """
    return {
        'type': 'object',
        'properties': {
            'verb': {
                'type': 'string',
                'enum': list(VERB_SPECS),
                'description': 'the action. one of:\n' + '\n'.join(verb_signatures()),
            },
            'mark': {
                'type': 'integer',
                'description': 'target entity mark (anchored verbs)',
            },
            'ticks': {
                'type': 'integer',
                'description': 'duration in ticks (wait, move)',
            },
            'dir': {
                'type': 'string',
                'enum': list(MOVE_DIRS),
                'description': 'move direction',
            },
            'yaw': {
                'type': 'integer',
                'description': 'look: signed degrees, snapped to 15',
            },
            'pitch': {
                'type': 'integer',
                'description': 'look: signed degrees, snapped to 15',
            },
        },
        'required': ['verb'],
    }
