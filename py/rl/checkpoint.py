"""
Checkpointing (flax serialization) and TensorBoard logging.

Uses flax.serialization (msgpack) instead of orbax to avoid the
orbax → grpcio-tools → protobuf<7 dependency conflict.
"""

import glob
import os
import time
from typing import Any, Dict, List, Optional, Tuple

import jax
import numpy as np
from flax import serialization
from tensorboardX import SummaryWriter


# ──────────────────────── Checkpointing ──────────────────────────────────── #

class CheckpointManager:
    """Save and restore trainable params + optimizer state.

    Checkpoints are stored as msgpack files via flax.serialization.
    File naming: ``ckpt_{step:08d}.msgpack``
    """

    def __init__(
        self,
        checkpoint_dir: str,
        max_to_keep: int = 5,
    ):
        self.checkpoint_dir = os.path.abspath(checkpoint_dir)
        self.max_to_keep = max_to_keep
        os.makedirs(self.checkpoint_dir, exist_ok=True)

    def _ckpt_path(self, step: int) -> str:
        return os.path.join(self.checkpoint_dir, f"ckpt_{step:08d}.msgpack")

    def _existing_steps(self) -> List[int]:
        """Return sorted list of checkpoint steps on disk."""
        pattern = os.path.join(self.checkpoint_dir, "ckpt_*.msgpack")
        paths = sorted(glob.glob(pattern))
        steps = []
        for p in paths:
            basename = os.path.basename(p)  # ckpt_00000042.msgpack
            try:
                s = int(basename.split("_")[1].split(".")[0])
                steps.append(s)
            except (IndexError, ValueError):
                continue
        return steps

    def _prune(self):
        """Remove old checkpoints beyond max_to_keep."""
        steps = self._existing_steps()
        while len(steps) > self.max_to_keep:
            oldest = steps.pop(0)
            path = self._ckpt_path(oldest)
            os.remove(path)

    def save(self, step: int, params, opt_state):
        """Persist a checkpoint at the given update step."""
        state = {"params": params, "opt_state": opt_state, "step": step}
        data = serialization.to_bytes(state)
        path = self._ckpt_path(step)
        # Atomic write: write to temp file then rename
        tmp_path = path + ".tmp"
        with open(tmp_path, "wb") as f:
            f.write(data)
        os.replace(tmp_path, path)
        self._prune()
        print(f"[Checkpoint] Saved step {step} → {path}")

    def restore(
        self,
        step: Optional[int] = None,
        target: Optional[dict] = None,
    ) -> Tuple[Any, Any, int]:
        """Load the latest (or specified) checkpoint.

        Args:
            step:   specific step to restore. None → latest.
            target: pytree template for deserialization (params + opt_state).
                    If None, restores as raw dicts.

        Returns:
            (params, opt_state, step)
        """
        if step is None:
            steps = self._existing_steps()
            if not steps:
                raise FileNotFoundError(
                    f"No checkpoints found in {self.checkpoint_dir}"
                )
            step = steps[-1]

        path = self._ckpt_path(step)
        with open(path, "rb") as f:
            data = f.read()

        if target is not None:
            state = serialization.from_bytes(target, data)
        else:
            state = serialization.from_bytes(None, data)

        print(f"[Checkpoint] Restored step {step} from {path}")
        return state["params"], state["opt_state"], state["step"]


# ──────────────────────── TensorBoard Logger ─────────────────────────────── #

class TBLogger:
    """Lightweight TensorBoard logger using tensorboardX."""

    def __init__(self, log_dir: str):
        os.makedirs(log_dir, exist_ok=True)
        self.writer = SummaryWriter(log_dir)
        self.log_dir = log_dir
        print(f"[TBLogger] Logging to {log_dir}")

    def log_scalar(self, tag: str, value: float, step: int):
        self.writer.add_scalar(tag, value, step)

    def log_ppo_metrics(self, metrics, step: int):
        """Log a PPOMetrics named-tuple."""
        for field_name in metrics._fields:
            val = getattr(metrics, field_name)
            self.writer.add_scalar(
                f"ppo/{field_name}", float(val), step
            )

    def log_episode_stats(
        self,
        returns: list,
        lengths: list,
        step: int,
    ):
        """Log completed episode statistics."""
        if returns:
            self.writer.add_scalar(
                "episode/mean_return", float(np.mean(returns)), step
            )
            self.writer.add_scalar(
                "episode/max_return", float(np.max(returns)), step
            )
            self.writer.add_scalar(
                "episode/min_return", float(np.min(returns)), step
            )
            self.writer.add_scalar(
                "episode/mean_length", float(np.mean(lengths)), step
            )
            self.writer.add_scalar(
                "episode/num_completed", len(returns), step
            )

    def log_learning_rate(self, lr: float, step: int):
        self.writer.add_scalar("train/learning_rate", lr, step)

    def close(self):
        self.writer.close()
