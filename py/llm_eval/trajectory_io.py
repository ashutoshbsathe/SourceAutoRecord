"""Read and write the binary `.trajectory` format.

Length-delimited protobuf (a uint32 little-endian size prefix per message,
matching the `.rollout` framing): a TrajectoryHeader, then one Step per macro. A
Step is one Observation plus the ordered Calls the model made for it (the last is
accepted, or none is when the agent gave up). Frames are embedded as PNG bytes,
so a trajectory is one self-contained file.
"""

import json
import struct

import cv2

from . import trajectory_pb2

_SIZE = struct.Struct('<I')


def encode_png(frame):
    """RGB frame -> PNG bytes (empty bytes if there is no frame)."""
    if frame is None:
        return b''
    ok, buf = cv2.imencode('.png', frame[:, :, ::-1])  # cv2 expects BGR
    return buf.tobytes() if ok else b''


def make_call(
    prompt_sent,
    thinking,
    raw_response,
    reasoning,
    usage,
    accepted=False,
    rejection_reason='',
    macro=None,
    result=None,
):
    """Build one Call. `macro`/`result` are set only on the accepted call."""
    call = trajectory_pb2.Call(
        prompt_sent=prompt_sent,
        thinking=thinking,
        raw_response=raw_response,
        reasoning=reasoning,
        accepted=accepted,
        rejection_reason=rejection_reason,
    )
    if usage is not None:
        call.usage.CopyFrom(usage)
    if macro is not None:
        call.action = macro.SerializeToString()
    if result is not None:
        call.result = result.SerializeToString()
    return call


def make_step(index, obs, calls, terminal=''):
    """Build a Step from the observation the agent saw and the calls it made."""
    px, py, pz = obs.player
    step = trajectory_pb2.Step(index=index, terminal=terminal)
    step.obs.frame_png = encode_png(obs.frame)
    step.obs.percept_json = json.dumps(obs.marks)
    step.obs.player.x, step.obs.player.y, step.obs.player.z = px, py, pz
    step.obs.eye_yaw = obs.state.camera.y
    step.obs.held_mark = obs.held_mark or 0
    step.calls.extend(calls)
    return step


class TrajectoryWriter:
    """Stream a trajectory to disk: the header, then one Step at a time."""

    def __init__(self, path, header):
        """Open `path` and write the header immediately."""
        self._f = open(path, 'wb')
        self._put(header)

    def _put(self, msg):
        data = msg.SerializeToString()
        self._f.write(_SIZE.pack(len(data)))
        self._f.write(data)
        self._f.flush()  # keep a partial run readable if the process dies

    def write_step(self, step):
        """Append one length-delimited Step."""
        self._put(step)

    def close(self):
        """Close the underlying file."""
        self._f.close()

    def __enter__(self):
        """Enter the context manager."""
        return self

    def __exit__(self, *exc):
        """Close the file on context exit."""
        self.close()


def _read_one(f):
    """Read one length-delimited message body, or None at (or past) EOF.

    A short read -- a clean EOF, or a record left half-written by a killed
    process -- stops iteration, so a partial trajectory still loads up to its
    last complete step.
    """
    size_data = f.read(4)
    if len(size_data) < 4:
        return None
    (size,) = _SIZE.unpack(size_data)
    body = f.read(size)
    return body if len(body) == size else None


def read_trajectory(path):
    """Load a `.trajectory` file into (TrajectoryHeader, list[Step])."""
    with open(path, 'rb') as f:
        blob = _read_one(f)
        if blob is None:
            raise ValueError(f'{path}: empty or truncated trajectory')
        header = trajectory_pb2.TrajectoryHeader()
        header.ParseFromString(blob)
        steps = []
        while True:
            blob = _read_one(f)
            if blob is None:
                break
            step = trajectory_pb2.Step()
            step.ParseFromString(blob)
            steps.append(step)
    return header, steps
