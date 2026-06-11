"""No-game smoke test for the macro client: percept parsing + macro validation.

Unlike agentloop_smoke.py (which boots a real game), this exercises the *pure*
Python logic against synthetic protobufs, so it runs anywhere in <1s and is the
fast gate for the delta-merge / projection / validator code paths:

    uv run python py/percept_grammar_smoke.py

Exits non-zero on the first failed assertion.
"""

import sys

from p2harness import harness_pb2 as pb
from p2harness import macro_grammar as mg
from p2harness.entities import WorldView
from p2harness.entities import parse_snapshot


def ent(
    index, serial, cls='', name='', pos=(0, 0, 0), mark=0, fields=None, deleted=False
):
    """Build one EntityState, mirroring what the snapshotter emits."""
    e = pb.EntityState(entity_index=index, serial_number=serial, deleted=deleted)
    if deleted:  # a delete record carries only index + (old) serial
        return e
    e.class_name = cls
    e.target_name = name
    e.mark = mark
    e.position.x, e.position.y, e.position.z = pos
    for k, v in (fields or {}).items():
        f = e.fields.add()
        f.name = k
        if isinstance(v, bool):
            f.bool_val = v
        elif isinstance(v, int):
            f.int_val = v
        elif isinstance(v, float):
            f.float_val = v
        else:
            f.string_val = v
    return e


def gs(ents, player=(0.0, 0.0, 0.0), yaw=0.0, full=True):
    """Build a GameState wrapping one snapshot (full or delta)."""
    state = pb.GameState()
    state.position.x, state.position.y, state.position.z = player
    state.camera.y = yaw
    state.entity_snapshot.is_full_snapshot = full
    for e in ents:
        state.entity_snapshot.entities.append(e)
    return state


# A cube -> floor-button -> door chamber plus a pedestal button and an unmarked
# prop, as a full snapshot.
FULL = gs(
    [
        ent(
            2,
            1,
            'prop_weighted_cube',
            'box',
            (256, 0, 64),
            mark=1,
            fields={'m_nCubeType': 0, 'm_bActivated': True},
        ),
        ent(
            3,
            1,
            'prop_floor_button',
            'button_1',
            (256, 200, 64),
            mark=2,
            fields={'m_bButtonState': False},
        ),
        ent(4, 1, 'prop_testchamber_door', 'exit_door', (256, 400, 64), mark=3),
        ent(
            5,
            1,
            'prop_button',
            'pedestal',
            (0, 200, 64),
            mark=4,
            fields={'m_nSequence': 0},
        ),
        ent(6, 1, 'func_brush', '', (0, 0, 0), mark=0),  # unmarked -> dropped
    ]
)


def by_mark(marks):
    """Index a percept list by mark for terse assertions."""
    return {m['mark']: m for m in marks}


def check(cond, msg):
    """Raise AssertionError(msg) unless cond holds."""
    if not cond:
        raise AssertionError(msg)


def test_projection():
    """parse_snapshot projects per-class semantic state and drops unmarked ents."""
    m = by_mark(parse_snapshot(FULL))
    check(set(m) == {1, 2, 3, 4}, f'marks {sorted(m)} (unmarked leaked?)')
    check(m[1]['state'] == {'cube_type': 'standard', 'on_button': True}, m[1]['state'])
    check(m[2]['state'] == {'pressed': False}, m[2]['state'])
    check(m[3]['state'] == {'open': None}, m[3]['state'])
    check(m[4]['state'] == {'pressed': False}, m[4]['state'])  # m_nSequence 0 != 3
    # player at origin facing yaw 0; cube at +x 256 -> bearing ~0, horizontal dist 256.
    check(abs(m[1]['bearing']) < 0.1, f'cube bearing {m[1]["bearing"]}')
    check(round(m[1]['dist']) == 256, f'cube dist {m[1]["dist"]}')
    return 'cube/button/door/pedestal projected; unmarked dropped'


def test_delta_merge():
    """A delta updates changed entities/fields and leaves the rest intact."""
    wv = WorldView()
    wv.observe(FULL)
    # Delta: button pressed flips; cube moves + on_button flips. Crucially the
    # cube's m_nCubeType is NOT resent (unchanged), and door/pedestal are absent.
    delta = gs(
        [
            ent(
                3,
                1,
                'prop_floor_button',
                'button_1',
                (256, 200, 64),
                mark=2,
                fields={'m_bButtonState': True},
            ),
            ent(
                2,
                1,
                'prop_weighted_cube',
                'box',
                (256, 205, 64),
                mark=1,
                fields={'m_bActivated': True},
            ),
        ],
        full=False,
    )
    m = by_mark(wv.observe(delta))
    check(set(m) == {1, 2, 3, 4}, f'static entities lost across delta: {sorted(m)}')
    check(m[1]['state']['cube_type'] == 'standard', 'm_nCubeType did not persist')
    check(m[1]['state']['on_button'] is True, 'cube on_button not updated')
    check(m[1]['pos'] == [256.0, 205.0, 64.0], f'cube pos not updated: {m[1]["pos"]}')
    check(m[2]['state']['pressed'] is True, 'button press not updated')
    check(m[3]['state'] == {'open': None}, 'door dropped/changed across delta')
    return 'delta merged; static marks + unchanged fields preserved'


def test_delete_and_serial_reuse():
    """A delete drops the entity; a reused slot resets its merged fields."""
    wv = WorldView()
    wv.observe(FULL)
    wv.observe(gs([ent(2, 1, deleted=True)], full=False))  # cube removed
    check(1 not in by_mark(wv.observe(gs([], full=False))), 'deleted cube lingered')
    # Slot 2 reused by a new (serial 2) reflective cube that resends only its
    # type -- the old serial's m_bActivated must NOT leak through.
    reuse = gs(
        [
            ent(
                2,
                2,
                'prop_weighted_cube',
                'box2',
                (300, 0, 64),
                mark=1,
                fields={'m_nCubeType': 2},
            )
        ],
        full=False,
    )
    m = by_mark(wv.observe(reuse))
    check(m[1]['state']['cube_type'] == 'reflective', m[1]['state'])
    check(m[1]['state']['on_button'] is False, 'stale field leaked across serial reuse')
    return 'delete drops entity; serial reuse clears stale fields'


def test_validate_ok():
    """Well-formed actions validate into the right MacroRequest."""
    ents = parse_snapshot(FULL)
    r = mg.validate({'verb': 'go_to', 'mark': 2}, ents)
    check(isinstance(r, pb.MacroRequest) and r.verb == 'go_to' and r.mark == 2, r)
    r = mg.validate({'verb': 'pick_up', 'mark': 1}, ents)
    check(isinstance(r, pb.MacroRequest), f'cube pick_up rejected: {r}')
    r = mg.validate({'verb': 'release'}, ents, held_mark=1)
    check(isinstance(r, pb.MacroRequest) and r.mark == 0, r)
    r = mg.validate({'verb': 'move', 'dir': 'forward', 'ticks': 20}, ents)
    check(isinstance(r, pb.MacroRequest) and r.dir == 'forward' and r.ticks == 20, r)
    r = mg.validate({'verb': 'done'}, ents)
    check(isinstance(r, pb.MacroRequest) and r.verb == 'done', r)
    # look snaps to 15 deg and clamps pitch.
    r = mg.validate({'verb': 'look', 'yaw': 38, 'pitch': -100}, ents)
    check(
        isinstance(r, pb.MacroRequest) and r.yaw == 45 and r.pitch == -89,
        f'look snap/clamp: yaw={r.yaw} pitch={r.pitch}',
    )
    return 'go_to/pick_up/release/move/done/look validate + snap correctly'


def test_validate_reject():
    """Malformed or impossible actions reject locally with a structured string."""
    ents = parse_snapshot(FULL)

    def rejected(call, held=None):
        r = mg.validate(call, ents, held_mark=held)
        check(isinstance(r, str), f'expected rejection, got {r!r} for {call}')
        return r

    rejected({'verb': 'frobnicate'})
    rejected({'verb': 'go_to', 'mark': 99})  # absent mark
    rejected({'verb': 'aim_at'})  # missing mark
    rejected({'verb': 'pick_up', 'mark': 2})  # button isn't grabbable
    rejected({'verb': 'pick_up', 'mark': 1}, held=1)  # already holding
    rejected({'verb': 'release'}, held=None)  # not holding
    rejected({'verb': 'wait', 'ticks': 0})  # below range
    rejected({'verb': 'wait', 'ticks': 9999})  # above range
    rejected({'verb': 'wait', 'ticks': True})  # bool is not an int
    rejected({'verb': 'move', 'dir': 'sideways', 'ticks': 5})  # bad dir
    rejected('not a dict')
    return '11 malformed/impossible actions rejected locally'


def test_tool_schema():
    """The tool schema is single-sourced from VERB_SPECS and covers every verb."""
    schema = mg.tool_schema()
    enum = schema['properties']['verb']['enum']
    check(set(enum) == set(mg.VERB_SPECS), f'verb enum != VERB_SPECS: {enum}')
    check(len(mg.verb_signatures()) == len(mg.VERB_SPECS), 'signature count mismatch')
    return f'{len(enum)} verbs exposed; schema in sync with validate'


CHECKS = [
    ('projection', test_projection),
    ('delta_merge', test_delta_merge),
    ('delete_serial_reuse', test_delete_and_serial_reuse),
    ('validate_ok', test_validate_ok),
    ('validate_reject', test_validate_reject),
    ('tool_schema', test_tool_schema),
]


def main():
    """Run every check; print PASS/FAIL and exit non-zero on any failure."""
    print(f'=== percept/grammar smoke: {len(CHECKS)} checks ===')
    failures = 0
    for name, fn in CHECKS:
        try:
            detail = fn()
            print(f'  [PASS] {name} -- {detail}')
        except Exception as e:  # noqa: BLE001 -- report, don't abort the run
            failures += 1
            print(f'  [FAIL] {name} -- {type(e).__name__}: {e}')
    print(f'{len(CHECKS) - failures}/{len(CHECKS)} passed.')
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
