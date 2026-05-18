"""
Rollout buffer and multi-environment trajectory collection.

The buffer eagerly encodes images through the frozen ViT during collection,
storing only the 768-dim embeddings to save ~200× memory vs raw pixels.

Environment stepping is parallelised with a ThreadPoolExecutor so that
one slow/crashed env doesn't block or cascade-fail the others.
"""

from concurrent.futures import ThreadPoolExecutor, as_completed
from typing import Callable, Dict, List, Tuple

import numpy as np
import jax
import jax.numpy as jnp


class RolloutBuffer:
    """Fixed-size buffer for one rollout of (num_steps x num_envs) transitions.

    All storage is NumPy; data is converted to JAX arrays when consumed.
    """

    def __init__(self, num_steps: int, num_envs: int, embed_dim: int = 768):
        self.num_steps = num_steps
        self.num_envs = num_envs

        # Observations (ViT embeddings, not raw pixels)
        self.image_embeds = np.zeros((num_steps, num_envs, embed_dim), dtype=np.float32)
        self.positions = np.zeros((num_steps, num_envs, 3), dtype=np.float32)

        # Actions (stored per component)
        self.actions: Dict[str, np.ndarray] = {
            'move_fb': np.zeros((num_steps, num_envs), dtype=np.int32),
            'move_lr': np.zeros((num_steps, num_envs), dtype=np.int32),
            'zoom': np.zeros((num_steps, num_envs), dtype=np.int32),
            'portal': np.zeros((num_steps, num_envs), dtype=np.int32),
            'buttons': np.zeros((num_steps, num_envs, 3), dtype=np.int32),
            'mouse': np.zeros((num_steps, num_envs, 2), dtype=np.float32),
        }

        self.log_probs = np.zeros((num_steps, num_envs), dtype=np.float32)
        self.values = np.zeros((num_steps, num_envs), dtype=np.float32)
        self.rewards = np.zeros((num_steps, num_envs), dtype=np.float32)
        self.dones = np.zeros((num_steps, num_envs), dtype=np.float32)

    def store(
        self,
        step: int,
        image_embeds: np.ndarray,
        positions: np.ndarray,
        actions: Dict[str, np.ndarray],
        log_probs: np.ndarray,
        values: np.ndarray,
    ):
        """Write one timestep of data into the buffer."""
        self.image_embeds[step] = image_embeds
        self.positions[step] = positions
        for k in self.actions:
            self.actions[k][step] = actions[k]
        self.log_probs[step] = log_probs
        self.values[step] = values

    def flatten(self) -> Dict[str, jnp.ndarray]:
        """Flatten (T, N, ...) -> (T*N, ...) and convert to JAX arrays."""
        B = self.num_steps * self.num_envs
        return {
            'image_embeds': jnp.array(self.image_embeds.reshape(B, -1)),
            'positions': jnp.array(self.positions.reshape(B, -1)),
            'actions': {
                k: jnp.array(v.reshape(B, *v.shape[2:]))
                for k, v in self.actions.items()
            },
            'log_probs': jnp.array(self.log_probs.reshape(B)),
            'values': jnp.array(self.values.reshape(B)),
        }


# ──────────────── Episode statistics tracker ─────────────────────────────── #


class EpisodeStats:
    """Track per-environment running episode return and length."""

    def __init__(self, num_envs: int):
        self.num_envs = num_envs
        self.ep_return = np.zeros(num_envs, dtype=np.float64)
        self.ep_length = np.zeros(num_envs, dtype=np.int64)
        # Completed episode statistics
        self.completed_returns: List[float] = []
        self.completed_lengths: List[int] = []

    def update(self, rewards: np.ndarray, dones: np.ndarray):
        """Update with one timestep of (num_envs,) rewards and dones."""
        self.ep_return += rewards
        self.ep_length += 1
        for i in range(self.num_envs):
            if dones[i]:
                self.completed_returns.append(float(self.ep_return[i]))
                self.completed_lengths.append(int(self.ep_length[i]))
                self.ep_return[i] = 0.0
                self.ep_length[i] = 0

    def flush(self) -> Tuple[List[float], List[int]]:
        """Return and clear completed episode stats."""
        rets = self.completed_returns.copy()
        lens = self.completed_lengths.copy()
        self.completed_returns.clear()
        self.completed_lengths.clear()
        return rets, lens


# ──────────────── Per-env step (runs in thread pool) ─────────────────────── #


def _step_single_env(env, action_dict, prev_obs):
    """Step a single environment. Returns (obs, reward, done, error_flag).

    This function runs in a thread.  It catches any exception (stream timeout,
    gRPC drop, etc.) and auto-recovers by restarting the instance.
    """
    try:
        obs, reward, terminated, truncated, _info = env.step(action_dict)
        done = terminated or truncated

        if done:
            obs, _ = env.reset()

        return obs, float(reward), done, False

    except Exception as e:
        env_id = getattr(env, 'instance', None)
        env_id = env_id.instance_id if env_id else '?'
        print(
            f'[Rollout] Env {env_id} error: {e.__class__.__name__}: {e}\n'
            f'         Restarting instance and resetting...'
        )
        try:
            env.restart_instance()
            obs, _ = env.reset()
        except Exception as e2:
            print(f'[Rollout] Env {env_id} restart also failed: {e2}')
            obs = prev_obs  # fallback to previous observation
        return obs, 0.0, True, True  # done=True so GAE doesn't bootstrap


# ──────────────── Rollout collection ─────────────────────────────────────── #


def collect_rollouts(
    envs,
    compute_action_fn: Callable,
    params,
    vision_encoder,
    current_obs: List[dict],
    num_steps: int,
    rng: jnp.ndarray,
    ep_stats: EpisodeStats,
) -> Tuple['RolloutBuffer', List[dict], jnp.ndarray]:
    """Collect a rollout of transitions from multiple environments.

    Environment steps are parallelised with a thread pool so one slow or
    crashed env doesn't block the others.

    Args:
        envs:              list of Portal2Env instances.
        compute_action_fn: JIT-compiled (params, img_embed, pos, rng) -> (actions, lp, vals).
        params:            trainable model parameters.
        vision_encoder:    VisionEncoder instance (frozen ViT).
        current_obs:       list of current observations (one per env).
        num_steps:         number of timesteps to collect.
        rng:               JAX PRNG key.
        ep_stats:          EpisodeStats tracker.

    Returns:
        buffer:      filled RolloutBuffer.
        current_obs: updated observations after rollout.
        rng:         consumed PRNG key.
    """
    num_envs = len(envs)
    embed_dim = vision_encoder.hidden_size
    buffer = RolloutBuffer(num_steps, num_envs, embed_dim)

    # Reusable thread pool — one thread per env
    with ThreadPoolExecutor(max_workers=num_envs) as pool:
        for step in range(num_steps):
            rng, step_rng = jax.random.split(rng)

            # ── Batch observations ──
            images = jnp.array(np.stack([obs['image'] for obs in current_obs]))
            positions = jnp.array(np.stack([obs['position'] for obs in current_obs]))

            # ── Encode images (frozen ViT) ──
            image_embeds = vision_encoder(images)  # (N, 768)

            # ── Model forward pass + sample ──
            actions, log_probs, values = compute_action_fn(
                params, image_embeds, positions, step_rng
            )

            # ── Store in buffer ──
            buffer.store(
                step,
                np.array(image_embeds),
                np.array(positions),
                {k: np.array(v) for k, v in actions.items()},
                np.array(log_probs),
                np.array(values),
            )

            # ── Step all environments in parallel ──
            futures = {}
            for i, env in enumerate(envs):
                action_dict = {
                    'move_fb': int(actions['move_fb'][i]),
                    'move_lr': int(actions['move_lr'][i]),
                    'zoom': int(actions['zoom'][i]),
                    'portal': int(actions['portal'][i]),
                    'buttons': np.array(actions['buttons'][i]),
                    'mouse': np.array(actions['mouse'][i]),
                }
                futures[
                    pool.submit(_step_single_env, env, action_dict, current_obs[i])
                ] = i

            # ── Collect results ──
            rewards = np.zeros(num_envs, dtype=np.float32)
            dones = np.zeros(num_envs, dtype=np.float32)
            next_obs = [None] * num_envs

            for future in as_completed(futures):
                i = futures[future]
                obs, reward, done, _errored = future.result()
                rewards[i] = reward
                dones[i] = float(done)
                next_obs[i] = obs

            buffer.rewards[step] = rewards
            buffer.dones[step] = dones
            ep_stats.update(rewards, dones)

            current_obs = next_obs

    return buffer, current_obs, rng
