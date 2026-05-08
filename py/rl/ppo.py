"""
PPO algorithm: GAE computation, clipped surrogate loss, and update step.

All performance-critical functions are JIT-compiled.
"""

from typing import Any, Callable, Dict, NamedTuple, Tuple

import jax
import jax.numpy as jnp
import optax
import flax.linen as nn

from rl.model import ActorCritic, ActionDistParams, IndependentActionHead

# ─────────────────────────── GAE ─────────────────────────────────────────── #


def compute_gae(
    rewards: jnp.ndarray,
    values: jnp.ndarray,
    dones: jnp.ndarray,
    last_value: jnp.ndarray,
    gamma: float = 0.99,
    gae_lambda: float = 0.95,
) -> Tuple[jnp.ndarray, jnp.ndarray]:
    """Generalised Advantage Estimation (reverse-scan implementation).

    Args:
        rewards:    (T, N) rewards at each timestep.
        values:     (T, N) value estimates.
        dones:      (T, N) episode termination flags.
        last_value: (N,)   bootstrap value for the final step.
        gamma:      discount factor.
        gae_lambda: GAE lambda.

    Returns:
        advantages: (T, N)
        returns:    (T, N)
    """
    T, N = rewards.shape
    advantages = jnp.zeros_like(rewards)
    last_gae = jnp.zeros(N)

    def _step(carry, t):
        last_gae = carry
        # Reverse index
        idx = T - 1 - t
        next_value = jnp.where(
            idx == T - 1,
            last_value,
            values[idx + 1],
        )
        next_non_terminal = 1.0 - dones[idx]
        delta = rewards[idx] + gamma * next_value * next_non_terminal - values[idx]
        gae = delta + gamma * gae_lambda * next_non_terminal * last_gae
        return gae, (idx, gae)

    _, (indices, gaes) = jax.lax.scan(_step, last_gae, jnp.arange(T))

    # gaes come out in reverse order; sort back
    advantages = jnp.zeros_like(rewards).at[indices].set(gaes)
    returns = advantages + values
    return advantages, returns


# ────────────────────── PPO functions factory ────────────────────────────── #


class PPOMetrics(NamedTuple):
    total_loss: jnp.ndarray
    policy_loss: jnp.ndarray
    value_loss: jnp.ndarray
    entropy: jnp.ndarray
    approx_kl: jnp.ndarray
    clip_fraction: jnp.ndarray
    grad_norm: jnp.ndarray


def create_ppo_fns(
    model: ActorCritic,
    optimizer: optax.GradientTransformation,
    clip_eps: float = 0.2,
    vf_coef: float = 0.5,
    ent_coef: float = 0.01,
):
    """Build JIT-compiled PPO functions closed over the model and optimizer.

    Returns:
        compute_action_fn: (params, image_embeds, positions, rng) -> (actions, log_probs, values)
        ppo_step_fn:       single gradient step on a mini-batch.
    """

    # ── action inference (used during rollout) ────────────────────────── #
    @jax.jit
    def compute_action(params, image_embeds, positions, rng):
        dist_params, values = model.apply(params, image_embeds, positions)
        actions, log_probs = IndependentActionHead.sample(dist_params, rng)
        return actions, log_probs, values.squeeze(-1)

    # ── single PPO gradient step ──────────────────────────────────────── #
    @jax.jit
    def ppo_step(params, opt_state, batch):
        """One PPO gradient step on a mini-batch.

        batch is a dict with keys:
            image_embeds, positions, actions (dict), old_log_probs,
            advantages, returns, old_values
        """

        def loss_fn(params):
            dist_params, values = model.apply(
                params, batch["image_embeds"], batch["positions"]
            )
            values = values.squeeze(-1)

            new_log_probs = IndependentActionHead.log_prob(
                dist_params, batch["actions"]
            )
            ent = IndependentActionHead.entropy(dist_params)

            # ── policy loss (clipped surrogate) ──
            ratio = jnp.exp(new_log_probs - batch["old_log_probs"])
            pg_loss1 = -batch["advantages"] * ratio
            pg_loss2 = -batch["advantages"] * jnp.clip(
                ratio, 1.0 - clip_eps, 1.0 + clip_eps
            )
            pg_loss = jnp.maximum(pg_loss1, pg_loss2).mean()

            # ── value loss (clipped) ──
            v_unclipped = (values - batch["returns"]) ** 2
            v_clipped = batch["old_values"] + jnp.clip(
                values - batch["old_values"], -clip_eps, clip_eps
            )
            v_loss_clipped = (v_clipped - batch["returns"]) ** 2
            v_loss = 0.5 * jnp.maximum(v_unclipped, v_loss_clipped).mean()

            loss = pg_loss + vf_coef * v_loss - ent_coef * ent.mean()

            return loss, PPOMetrics(
                total_loss=loss,
                policy_loss=pg_loss,
                value_loss=v_loss,
                entropy=ent.mean(),
                approx_kl=((ratio - 1) - jnp.log(ratio)).mean(),
                clip_fraction=(jnp.abs(ratio - 1) > clip_eps)
                .astype(jnp.float32)
                .mean(),
                grad_norm=jnp.array(0.0),  # placeholder, filled below
            )

        (loss, metrics), grads = jax.value_and_grad(loss_fn, has_aux=True)(params)
        grad_norm = optax.global_norm(grads)
        metrics = metrics._replace(grad_norm=grad_norm)

        updates, opt_state = optimizer.update(grads, opt_state, params)
        params = optax.apply_updates(params, updates)
        return params, opt_state, metrics

    return compute_action, ppo_step


# ─────────────────────── Full PPO update pass ────────────────────────────── #


def ppo_update(
    ppo_step_fn: Callable,
    params,
    opt_state,
    buffer_seq: Dict[str, jnp.ndarray],
    advantages: jnp.ndarray,
    returns: jnp.ndarray,
    num_epochs: int,
    num_minibatches: int,
    max_seq_len: int,
    rng: jnp.ndarray,
):
    """Run multiple epochs of mini-batch PPO updates on sequence chunks.

    Args:
        ppo_step_fn: JIT-compiled single-step function from create_ppo_fns.
        params:      current trainable parameters.
        opt_state:   current optimizer state.
        buffer_seq:  sequence rollout data (num_envs, num_steps, ...).
        advantages:  (num_envs, num_steps)
        returns:     (num_envs, num_steps)
        num_epochs:  number of passes over the data.
        num_minibatches: number of mini-batches per epoch.
        max_seq_len: maximum context window for the Transformer.
        rng:         JAX PRNG key.

    Returns:
        params, opt_state, avg_metrics
    """
    num_envs, num_steps = advantages.shape

    # Chunk sequences into blocks of max_seq_len
    # If num_steps is not divisible, we truncate the end.
    n_chunks = num_steps // max_seq_len
    valid_steps = n_chunks * max_seq_len

    def _chunk(x):
        if x.ndim >= 2 and x.shape[0] == num_envs and x.shape[1] == num_steps:
            # (B, T, ...) -> (B, n_chunks, max_seq_len, ...) -> (B * n_chunks, max_seq_len, ...)
            reshaped = x[:, :valid_steps].reshape(
                num_envs * n_chunks, max_seq_len, *x.shape[2:]
            )
            return reshaped
        return x

    chunked_buffer = {
        "image_embeds": _chunk(buffer_seq["image_embeds"]),
        "positions": _chunk(buffer_seq["positions"]),
        "actions": {k: _chunk(v) for k, v in buffer_seq["actions"].items()},
        "log_probs": _chunk(buffer_seq["log_probs"]),
        "values": _chunk(buffer_seq["values"]),
    }
    chunked_adv = _chunk(advantages)
    chunked_ret = _chunk(returns)

    batch_size = num_envs * n_chunks
    minibatch_size = batch_size // num_minibatches

    # Normalise advantages (standard PPO practice)
    chunked_adv = (chunked_adv - jnp.mean(chunked_adv)) / (jnp.std(chunked_adv) + 1e-8)

    # Accumulate metrics for averaging
    metric_sums = None
    n_steps_opt = 0

    for _epoch in range(num_epochs):
        rng, shuffle_rng = jax.random.split(rng)
        perm = jax.random.permutation(shuffle_rng, batch_size)

        for mb_start in range(0, batch_size, minibatch_size):
            mb_idx = perm[mb_start : mb_start + minibatch_size]

            batch = {
                "image_embeds": chunked_buffer["image_embeds"][mb_idx],
                "positions": chunked_buffer["positions"][mb_idx],
                "actions": {k: v[mb_idx] for k, v in chunked_buffer["actions"].items()},
                "old_log_probs": chunked_buffer["log_probs"][mb_idx],
                "advantages": chunked_adv[mb_idx],
                "returns": chunked_ret[mb_idx],
                "old_values": chunked_buffer["values"][mb_idx],
            }

            params, opt_state, metrics = ppo_step_fn(params, opt_state, batch)

            if metric_sums is None:
                metric_sums = metrics
            else:
                metric_sums = PPOMetrics(*[a + b for a, b in zip(metric_sums, metrics)])
            n_steps_opt += 1

    avg_metrics = PPOMetrics(*[v / n_steps_opt for v in metric_sums])
    return params, opt_state, avg_metrics
