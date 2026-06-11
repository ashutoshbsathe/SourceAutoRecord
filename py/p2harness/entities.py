"""Percept parsing: the AgentLoop delta-snapshot stream -> LLM-facing marks.

The harness streams a FULL EntitySnapshot only on the first step, then deltas:
each later GameState carries only entities that changed (and only their changed
fields) plus `deleted` markers. WorldView merges that stream into a running view
of the marked world; observe() returns the complete marked-entity list each
step -- one dict per mark with a class-projected semantic `state`.

parse_snapshot() is the one-shot form for a single FULL snapshot (e.g. a
post-reset initial_state); on a delta it would see only what changed, so use a
WorldView for a live stream.
"""

import math

# m_nCubeType -> semantic cube type (see status_field_recon.md).
_CUBE_TYPES = {0: 'standard', 2: 'reflective'}


def _field_value(field):
    """Read the populated arm of an EntityField's value oneof (None if unset)."""
    which = field.WhichOneof('value')
    return getattr(field, which) if which else None


def _project_state(class_name, fields):
    """Class-projected semantic state -- the LLM-facing view, not the raw dump.

    Only category-A puzzle elements carry status; everything else gets {}. Field
    mappings are per status_field_recon.md. The door has no reliable server
    open-state field, so `open` is None (read it from the frame).
    """
    if class_name == 'prop_weighted_cube':
        return {
            'cube_type': _CUBE_TYPES.get(fields.get('m_nCubeType'), 'standard'),
            'on_button': bool(fields.get('m_bActivated', False)),
        }
    if class_name == 'prop_button':  # pedestal: pressed == anim sequence 3
        return {'pressed': fields.get('m_nSequence') == 3}
    if 'button' in class_name:  # floor / weight buttons: a clean networked bool
        return {'pressed': bool(fields.get('m_bButtonState', False))}
    if 'door' in class_name:  # no server open-state field -- visual only
        return {'open': None}
    return {}


def _dist_bearing(player, eye_yaw, pos):
    """Horizontal distance + bearing from the player to a world point.

    Both live in the ground plane (z dropped -- height stays readable in `pos`):
    dist is the 2D distance, matching go_to's reach metric so the model can
    calibrate; bearing is signed degrees off the player's facing, + = left (CCW),
    - = right (CW), in [-180, 180]. Python-side conveniences over world coords.
    """
    dx, dy = pos[0] - player[0], pos[1] - player[1]
    dist = math.sqrt(dx * dx + dy * dy)
    bearing = (math.degrees(math.atan2(dy, dx)) - eye_yaw + 180.0) % 360.0 - 180.0
    return dist, bearing


def _mark_dict(rec, player, eye_yaw):
    """Project one merged entity record into the LLM-facing dict."""
    dist, bearing = _dist_bearing(player, eye_yaw, rec['pos'])
    return {
        'mark': rec['mark'],
        'class': rec['class'],
        'name': rec['name'],
        'pos': [round(c, 1) for c in rec['pos']],
        'dist': round(dist, 1),
        'bearing': round(bearing, 1),
        'state': _project_state(rec['class'], rec['fields']),
    }


class WorldView:
    """Running merged view of the marked world over the delta-snapshot stream.

    Feed every streamed GameState to observe(); it merges full/delta snapshots
    and returns the complete marked-entity list each step (not just what changed
    this step). One instance per episode.
    """

    def __init__(self):
        # entity_index -> {serial, mark, class, name, pos, fields}. Keyed by index
        # (the slot); serial guards against a recycled slot becoming a new entity.
        self._ents = {}

    def observe(self, state):
        """Merge one GameState (full or delta) and return the marked percept.

        The list is sorted by mark; dist/bearing are relative to the player in
        `state`.
        """
        snap = state.entity_snapshot
        if snap.is_full_snapshot:
            self._ents.clear()
        for e in snap.entities:
            if e.deleted:  # tombstone carries the old serial -- match before dropping
                rec = self._ents.get(e.entity_index)
                if rec is not None and rec['serial'] == e.serial_number:
                    del self._ents[e.entity_index]
                continue
            rec = self._ents.get(e.entity_index)
            if rec is None or rec['serial'] != e.serial_number:
                rec = {'serial': e.serial_number, 'fields': {}}
                self._ents[e.entity_index] = rec
            rec['mark'] = e.mark
            rec['class'] = e.class_name
            rec['name'] = e.target_name
            rec['pos'] = (e.position.x, e.position.y, e.position.z)
            for f in e.fields:  # a delta carries only changed fields -- merge them in
                rec['fields'][f.name] = _field_value(f)

        player = (state.position.x, state.position.y, state.position.z)
        eye_yaw = state.camera.y
        marks = [
            _mark_dict(rec, player, eye_yaw)
            for rec in self._ents.values()
            if rec.get('mark', 0) > 0
        ]
        marks.sort(key=lambda d: d['mark'])
        return marks


def parse_snapshot(state):
    """Marked entities from a single FULL GameState (one-shot; see module doc)."""
    return WorldView().observe(state)
