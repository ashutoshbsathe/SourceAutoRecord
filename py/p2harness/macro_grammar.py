"""Macro grammar: the single source of truth for the act vocabulary.

One declarative spec (VERB_SPECS) drives both the LLM tool schema and local
validation, so the prompt and the gate can't drift. validate() checks a model
action against the grammar AND the live percept -- unknown verb, bad arg
type/range, a target that doesn't exist or is the wrong kind for the verb,
holding-state -- and on failure returns a structured string fed straight back to
the model, with no gRPC round-trip and no game step.

A target is a percept mark label -- the same token the annotation shows on
screen: "<n>" entity mark, "S<n>" surface panel, or "Pb"/"Po" portal.
"""

from dataclasses import dataclass

from . import harness_pb2

# Mirror the executor's clamps so a bad arg is rejected here
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


# The one warning that doesn't fit a single verb's doc: go_to/interact auto-march
# in a straight line and need a clear path. Surfaced verbatim in the prompt so the
# model stops blindly retrying a STUCK go_to (the bug that motivated this).
CAVEAT = (
    '`go_to`/`interact` route AROUND obstacles (cubes, buttons, walls) to reach a '
    'mark -- they fail BLOCKED only when NO walk path exists, or ADVANCED when a '
    'gap/edge stops them partway. On BLOCKED/ADVANCED you are at a NEW spot: '
    're-read the percept and try a nearer in-view mark, or `look`+`move` toward '
    "the opening -- don't blindly repeat the same command."
)


@dataclass(frozen=True)
class Verb:
    """One verb's argument spec, shared by validation and the tool schema."""

    doc: str
    example: str  # one valid command string that exercises this verb
    hint: str  # one-line teaching note paired with `example` in the prompt
    target: str | None = None  # target kind: 'any'|'entity'|'grabbable'|'optional'
    ticks_max: int | None = None  # set => required `ticks` in [1, ticks_max]
    dirs: tuple | None = None  # set => required `dir` from this set
    look: bool = False  # set => required signed `yaw` + optional `pitch` (15deg)


# doc = the signature line; example/hint = the per-verb teaching shown in the
# prompt. Examples are kept valid by percept_grammar_smoke.test_examples_validate.
VERB_SPECS = {
    'aim_at': Verb(
        'Point the view at a target -- an entity mark N, a wall panel Sn, or a '
        'placed portal Pb/Po.',
        'aim_at 9',
        'point the view at mark 9 (or a panel Sn / portal Pb|Po) to orient.',
        target='any',
    ),
    'go_to': Verb(
        'Walk to a target (mark N, panel Sn, or portal Pb/Po), routing AROUND '
        'obstacles (cubes, buttons, walls). Stops just BESIDE a cube/box it is '
        'sent to (it will not shove it). BLOCKED only if no walk path exists; '
        'ADVANCED if a gap/edge stopped it short.',
        'go_to 3',
        'walk to mark 3, routing around anything in the way.',
        target='any',
    ),
    'interact': Verb(
        'Walk to an entity mark (routing around obstacles, like go_to) and press '
        'it (+use) -- a button or switch. BLOCKED if no walk path exists.',
        'interact 5',
        'walk to mark 5 and press it.',
        target='entity',
    ),
    'pick_up': Verb(
        'Grab the cube/box/turret at a mark. Does NOT walk you there -- you must '
        "ALREADY be within arm's reach (~1 step, dist < ~96u) or it fails "
        'OUT_OF_REACH. go_to or move right up to the mark FIRST, then pick_up.',
        'pick_up 3',
        'grab grabbable mark 3 you are ALREADY standing next to (empty-handed); '
        'it does not move you -- go_to it first.',
        target='grabbable',
    ),
    'release': Verb(
        'Drop the held object. On a button mark you are in reach of, it places '
        'the cube ON the button and presses it (SEATED); out of reach it just '
        'drops (NOT_FAIR). A non-button mark, or none, drops toward the mark / '
        'at your feet.',
        'release 5',
        'on a button you are standing next to, release ON it to seat and press '
        'the cube (SEATED); otherwise it just drops.',
        target='optional',
    ),
    'interpose': Verb(
        'Seat the cube you are HOLDING onto a laser beam to block it. Give the '
        'emitter mark and how far along its beam to place the cube (percent 0-1 '
        'from the emitter). Optionally add an aim mark to also point the redirect '
        'at it. Fails NO_FLOOR (over a pit), NOT_REACHABLE (no walk path), '
        'NOT_INTERCEPTING (the cube misses the beam).',
        'interpose 4 0.5',
        'holding a cube, seat it halfway (0.5) along laser-emitter mark 4 to '
        'block the beam; add a third mark to redirect at a target.',
    ),
    'redirect_to': Verb(
        'Aim a cube ALREADY seated on a laser beam so it redirects the beam to a '
        'target and powers it. You must be standing next to the cube (re-aiming '
        'means re-placing it) -- else OUT_OF_REACH, so go_to the cube first. The '
        'cube must already be on a beam (interpose it first) -- else NOT_SEATED; '
        'NOT_POWERED if the aim cannot reach.',
        'redirect_to 6 9',
        'a reflector cube already on a beam (mark 6): aim it at laser-target mark '
        '9 to power it; interpose it onto the beam first.',
    ),
    'place_portal': Verb(
        'Place a portal of a color (blue|orange) on a portalable wall panel, '
        'named by its S-mark. The blue/orange pair auto-links; re-placing a '
        'color moves that portal. Fails NOT_PORTALABLE / CANT_FIT / OVERLAP / '
        'FIZZLED / NO_LOS.',
        'place_portal blue S1',
        'drop a blue portal on wall panel S1; place orange on another panel to '
        'link them.',
    ),
    'pass_through': Verb(
        'Walk through a placed portal, named by its percept label (Pb = blue, '
        'Po = orange). You emerge from the linked portal. To fling, build speed '
        'first -- the momentum you carry in comes out redirected. Fails '
        'NO_SUCH_PORTAL / UNLINKED / NOT_AT_MOUTH / BLOCKED.',
        'pass_through Pb',
        'walk into the blue portal (Pb); you come out the linked orange one.',
    ),
    'move': Verb(
        'Hold a movement direction for N ticks.',
        'move forward 10',
        'hold forward for 10 ticks (1-400) to step around an obstacle.',
        dirs=MOVE_DIRS,
        ticks_max=MOVE_MAX_TICKS,
    ),
    'look': Verb(
        'Turn the view by signed degrees, snapped to 15: +yaw turns LEFT, -yaw '
        'RIGHT; -pitch looks UP, +pitch looks DOWN.',
        'look 75 -30',
        'turn the view: yaw +75 (left), pitch -30 (look up); pitch optional.',
        look=True,
    ),
    'wait': Verb(
        'Let the frozen world run for N ticks.',
        'wait 50',
        'let the frozen world run 50 ticks (1-600).',
        ticks_max=WAIT_MAX_TICKS,
    ),
    'done': Verb(
        'Give up: stop when stuck with no action left (the environment, not '
        '`done`, detects a real solve).',
        'done',
        'give up when stuck; the environment marks a real solve, not `done`.',
    ),
}

VERBS = frozenset(VERB_SPECS)


def build_macro(verb, args):
    """Map a verb + positional string args to a MacroRequest (raises on bad args).

    The imperative twin of validate(): no percept checks, just the arg shapes a
    typed or scripted command supplies. Targets stay strings (the percept mark
    label); the executor resolves them.
    """
    m = harness_pb2.MacroRequest(verb=verb)
    if verb in ('aim_at', 'go_to', 'pick_up', 'interact'):
        m.target = args[0]
    elif verb == 'release':
        m.target = args[0] if args else ''
    elif verb == 'interpose':
        m.target = args[0]  # emitter
        m.percent = float(args[1])
        if len(args) > 2:
            m.aim = args[2]
    elif verb == 'redirect_to':
        m.target = args[0]  # the seated cube
        m.aim = args[1]  # the target to power
    elif verb == 'place_portal':
        m.color = args[0]
        m.target = args[1]  # wall panel Sn
    elif verb == 'pass_through':
        m.target = args[0]  # Pb/Po
    elif verb == 'move':
        m.dir = args[0]
        m.ticks = int(args[1])
    elif verb == 'look':
        m.yaw = int(args[0])
        m.pitch = int(args[1]) if len(args) > 1 else 0
    elif verb == 'wait':
        m.ticks = int(args[0])
    return m


def _snap(deg, step):
    """Round signed degrees to the nearest `step` (the coarse look lexer)."""
    return int(round(deg / step)) * step


def _target_kind(target):
    """The kind of a target label: 'entity' | 'panel' | 'portal' | None."""
    if target in ('Pb', 'Po'):
        return 'portal'
    if target[:1] == 'S' and target[1:].isdigit():
        return 'panel'
    if target.isdigit():
        return 'entity'
    return None


def _lookup(target, by_mark):
    """The percept entry for a target label, or None. by_mark keys entities by
    int mark and panels/portals by their string label (Sn/Pb/Po)."""
    if _target_kind(target) == 'entity':
        return by_mark.get(int(target))
    return by_mark.get(target)


def _present(by_mark):
    """The sorted target labels the percept currently carries (for error text)."""
    return sorted(str(k) for k in by_mark)


def _check_target(verb, spec, target, by_mark):
    """Check a target label against the verb's kind requirement + the percept.
    Error string or None."""
    if spec.target == 'optional' and not target:
        return None  # release with no target = drop at the player's feet
    kind = _target_kind(target)
    if kind is None:
        return f'{verb}: bad target {target!r}; expected mark N, panel Sn, or portal Pb/Po'
    if spec.target != 'any' and kind != 'entity':
        return f'{verb}: {target} is a {kind}; {verb} needs an entity mark'
    ent = _lookup(target, by_mark)
    if ent is None:
        return f'{verb}: no target {target}; present: {_present(by_mark)}'
    if spec.target == 'grabbable' and ent['class'] not in GRABBABLE_CLASSES:
        return f'{verb}: mark {target} is a {ent["class"]}, not grabbable'
    return None


def _check_interpose(req, held_mark, by_mark):
    """interpose checks: holding a cube, a valid laser-emitter target, percent in
    range, and a valid aim target if given. Error string or the ready req."""
    if held_mark is None:
        return 'interpose: nothing is being held; pick_up a cube first'
    em = _lookup(req.target, by_mark)
    if em is None:
        return f'interpose: no emitter {req.target}; present: {_present(by_mark)}'
    if em['class'] != 'env_portal_laser':
        return f'interpose: {req.target} is a {em["class"]}, not a laser emitter'
    if not 0.0 <= req.percent <= 1.0:
        return f'interpose: percent must be in [0, 1], got {req.percent}'
    if req.aim and _lookup(req.aim, by_mark) is None:
        return f'interpose: no aim target {req.aim}'
    return req


def _check_redirect(req, by_mark):
    """redirect_to checks: a placed reflector cube (target) + a laser-target
    (aim). Error string or the ready req."""
    cube = _lookup(req.target, by_mark)
    if cube is None:
        return f'redirect_to: no cube {req.target}; present: {_present(by_mark)}'
    if cube['class'] != 'prop_weighted_cube':
        return f'redirect_to: {req.target} is a {cube["class"]}, not a cube'
    tgt = _lookup(req.aim, by_mark)
    if tgt is None:
        return f'redirect_to: no laser target {req.aim}'
    if tgt['class'] != 'point_laser_target':
        return f'redirect_to: {req.aim} is a {tgt["class"]}, not a laser target'
    return req


def _check_place_portal(req, by_mark):
    """place_portal checks: a blue|orange color + a portalable wall-panel Sn
    target. Error string or the ready req."""
    if req.color not in ('blue', 'orange'):
        return f'place_portal: color must be blue|orange, got {req.color!r}'
    if _target_kind(req.target) != 'panel':
        return f'place_portal: target must be a wall panel Sn, got {req.target!r}'
    panel = by_mark.get(req.target)
    if panel is None:
        present = sorted(k for k in by_mark if isinstance(k, str) and k[:1] == 'S')
        return f'place_portal: no panel {req.target}; panels present: {present}'
    if panel['class'] != 'wall_panel':
        return f'place_portal: {req.target} is a {panel["class"]}, not a wall panel'
    return req


def _check_pass_through(req, by_mark):
    """pass_through checks: a Pb/Po target whose portal is actually placed."""
    if _target_kind(req.target) != 'portal':
        return f'pass_through: target must be a portal Pb/Po, got {req.target!r}'
    if req.target not in by_mark:
        present = sorted(k for k in by_mark if k in ('Pb', 'Po'))
        return f'pass_through: no {req.target} portal placed; present: {present}'
    return req


def validate(text, entities, held_mark=None):
    """Parse a command string ('go_to 7') and check it against grammar + percept.

    Same surface the REPL types: build_macro does the parsing, this adds the
    range / dir / look-snap / target / holding checks. Returns a ready-to-send
    MacroRequest, or a structured error string to feed back to the model (no
    gRPC, no game step).
    """
    tokens = text.split()
    if not tokens:
        return 'empty action'
    verb, args = tokens[0], tokens[1:]
    spec = VERB_SPECS.get(verb)
    if spec is None:
        return f'unknown verb {verb!r}; valid: {", ".join(VERB_SPECS)}'
    try:
        req = build_macro(verb, args)
    except Exception:  # any parse failure (bad int/float, missing arg) = bad args
        return f'{verb}: bad args {" ".join(args)!r}; expected {_signature(verb, spec)}'

    by_mark = {e['mark']: e for e in entities}
    if verb == 'interpose':
        return _check_interpose(req, held_mark, by_mark)
    if verb == 'redirect_to':
        return _check_redirect(req, by_mark)
    if verb == 'place_portal':
        return _check_place_portal(req, by_mark)
    if verb == 'pass_through':
        return _check_pass_through(req, by_mark)
    if spec.target:
        err = _check_target(verb, spec, req.target, by_mark)
        if err:
            return err
    if verb == 'pick_up' and held_mark is not None:
        return f'pick_up: already holding mark {held_mark}; release it first'
    if verb == 'release' and held_mark is None:
        return 'release: nothing is being held'
    if spec.ticks_max is not None and not 1 <= req.ticks <= spec.ticks_max:
        return f'{verb}: ticks must be in [1, {spec.ticks_max}], got {req.ticks}'
    if spec.dirs is not None and req.dir not in spec.dirs:
        return f'{verb}: dir must be one of {list(spec.dirs)}, got {req.dir!r}'
    if spec.look:
        req.yaw = _snap(req.yaw, LOOK_SNAP)
        req.pitch = max(-PITCH_LIMIT, min(PITCH_LIMIT, _snap(req.pitch, LOOK_SNAP)))
    return req


def _signature(verb, spec):
    """Plain-command form 'verb <arg>' -- the surface the model types."""
    if verb == 'interpose':
        return 'interpose <emitter> <percent 0-1> [aim]'
    if verb == 'redirect_to':
        return 'redirect_to <cube> <target>'
    if verb == 'place_portal':
        return 'place_portal <blue|orange> <Sn>'
    if verb == 'pass_through':
        return 'pass_through <Pb|Po>'
    args = []
    if spec.target == 'any':
        args.append('<target: N|Sn|Pb|Po>')
    elif spec.target == 'optional':
        args.append('[mark]')
    elif spec.target:
        args.append('<mark>')
    if spec.dirs is not None:
        args.append('<' + '|'.join(spec.dirs) + '>')
    if spec.ticks_max is not None:
        args.append(f'<ticks 1-{spec.ticks_max}>')
    if spec.look:
        args += ['<yaw>', '[pitch]']
    return f'{verb} {" ".join(args)}'.strip()


def verb_signatures():
    """One 'verb <args>: doc' line per verb for the system prompt (single source)."""
    return [f'{_signature(v, s)}: {s.doc}' for v, s in VERB_SPECS.items()]


def verb_examples():
    """One '<example>  # <hint>' line per verb: every verb shown once, valid."""
    return [f'{s.example}  # {s.hint}' for s in VERB_SPECS.values()]
