"""Read and write the binary `.trajectory` format.

Length-delimited protobuf (a uint32 little-endian size prefix per message,
matching the `.rollout` framing): a TrajectoryHeader, then one Step per macro.
Frames are embedded as PNG bytes, so a trajectory is one self-contained file.
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


def make_step(
    index,
    obs,
    macro,
    result,
    reasoning='',
    raw_response='',
    usage=None,
    terminal='',
    thinking='',
    prompt_sent='',
    attempts=None,
):
    """Build a Step from the observation the agent acted on and what happened.

    `obs` supplies the frame/percept/player the agent saw; `macro` is the action
    it chose; `result` is that action's MacroResult.
    """
    px, py, pz = obs.player
    step = trajectory_pb2.Step(
        index=index,
        frame_png=encode_png(obs.frame),
        percept_json=json.dumps(obs.marks),
        player=trajectory_pb2.Vec3(x=px, y=py, z=pz),
        reasoning=reasoning,
        raw_response=raw_response,
        action=macro.SerializeToString(),
        result=result.SerializeToString(),
        held_mark=obs.held_mark or 0,
        terminal=terminal,
        thinking=thinking,
        prompt_sent=prompt_sent,
        eye_yaw=obs.state.camera.y,
    )
    if usage is not None:
        step.usage.CopyFrom(usage)
    if attempts:
        step.attempts.extend(attempts)
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
