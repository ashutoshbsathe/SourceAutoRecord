"""
Portal 2 RL Challenge — PPO + ViT-B/16 Training Orchestrator.

Usage:
    python py/train_rl_challenge.py \\
        --map-name sp_a2_laser_chaining \\
        --target-pos 100,200,50 \\
        --num-envs 4

See `python py/train_rl_challenge.py --help` for all flags.
"""

import coolname
import os
import random
import signal
import sys
import time

import jax
import jax.numpy as jnp
import numpy as np
import optax

# ── project imports ──
from game_launcher import (
    GameInstance,
    DEFAULT_GAMESCOPE_ARGS,
    DEFAULT_GAME_ARGS,
    DEFAULT_STEAM_RUNTIME_SH,
    DEFAULT_PORTAL2_SH,
    DEFAULT_STAGGER_DELAY,
    get_instance_specific_args,
)
from rl_challenge_env import Portal2Env
from rl.config import PPOConfig, parse_args
from rl.model import VisionEncoder, ActorCritic
from rl.ppo import compute_gae, create_ppo_fns, ppo_update
from rl.checkpoint import CheckpointManager, TBLogger
from rl.inference_server import InferenceServer
from rl.async_worker import AsyncRolloutWorker
import queue

# ─────────────────────── Instance management ─────────────────────────────── #


def launch_instances(config: PPOConfig):
    """Start Portal 2 game instances and wrap them in Gym envs."""
    instances = []
    for i in range(config.num_envs):
        inst = GameInstance(
            instance_id=i,
            gamescope_args=DEFAULT_GAMESCOPE_ARGS.copy(),
            game_args=DEFAULT_GAME_ARGS.copy() + get_instance_specific_args(i),
            steam_runtime_sh=DEFAULT_STEAM_RUNTIME_SH,
            portal2_sh=DEFAULT_PORTAL2_SH,
            debug=False,
        )
        inst.start()
        instances.append(inst)
        time.sleep(DEFAULT_STAGGER_DELAY / 5)

    # Wait for the last instance to boot
    print(
        f"[Launcher] Waiting {config.num_envs * DEFAULT_STAGGER_DELAY / 2}s for instances to boot..."
    )
    time.sleep(config.num_envs * DEFAULT_STAGGER_DELAY / 2)

    envs = [
        Portal2Env(
            inst,
            map_name=config.map_name,
            target_pos=config.target_pos,
            max_steps=config.max_episode_steps,
            num_ticks_per_step=config.num_ticks_per_step,
        )
        for inst in instances
    ]
    return instances, envs


def cleanup(envs, instances, logger):
    """Graceful shutdown."""
    print("\n[Main] Cleaning up...")
    for env in envs:
        try:
            env.close()
        except Exception as e:
            print(f"  env.close() error: {e}")
    for inst in instances:
        try:
            inst.stop()
        except Exception as e:
            print(f"  inst.stop() error: {e}")
    if logger is not None:
        logger.close()
    print("[Main] Done.")


# ─────────────────────────── Main ────────────────────────────────────────── #


def main():
    config = parse_args()

    # ── Generate Run Name & Directories ──
    if config.resume:
        run_dir = os.path.abspath(config.resume)
        print(f"[Main] Resuming run in directory: {run_dir}")
    else:
        run_name = f"{coolname.generate_slug(2)}_{time.strftime('%Y%m%d_%H%M%S')}"
        run_dir = os.path.abspath(os.path.join(config.log_dir, run_name))
        os.makedirs(run_dir, exist_ok=True)
        print(f"[Main] New run directory: {run_dir}")

    config.log_dir = run_dir

    print(f"[Main] Config:\n{config}\n")

    # ── Verify JAX backend ──
    backend = jax.default_backend()
    devices = jax.devices()
    print(f"[Main] JAX backend: {backend}, devices: {devices}")
    if backend == "cpu":
        print(
            "[Main] ⚠ WARNING: JAX is running on CPU! "
            "Check your CUDA / jax[cuda] installation."
        )

    # ── Reproducibility ──
    rng = jax.random.PRNGKey(config.seed)
    np.random.seed(config.seed)

    # ── Launch game instances ──
    print(f"[Main] Launching {config.num_envs} game instances...")
    instances, envs = launch_instances(config)

    # ── Logging & checkpointing ──
    logger = TBLogger(config.log_dir)
    ckpt_mgr = CheckpointManager(config.log_dir)

    try:
        # ── Load frozen ViT ──
        print("[Main] Loading frozen ViT-B/16...")
        vision_encoder = VisionEncoder(config.vit_checkpoint)
        embed_dim = vision_encoder.hidden_size
        print(f"[Main] ViT hidden_size = {embed_dim}")

        # ── Initialise actor-critic ──
        model = ActorCritic(
            embed_dim=embed_dim,
            trunk_hidden=config.trunk_hidden,
            trunk_out=config.trunk_out,
        )
        rng, init_rng = jax.random.split(rng)
        dummy_img = jnp.zeros((1, embed_dim))
        dummy_kinematics = jnp.zeros((1, 6))
        params = model.init(init_rng, dummy_img, dummy_kinematics)
        print(
            f"[Main] Trainable param count: {sum(x.size for x in jax.tree.leaves(params)):,}"
        )

        # ── Optimizer ──
        if config.anneal_lr:
            lr_schedule = optax.linear_schedule(
                init_value=config.learning_rate,
                end_value=0.0,
                transition_steps=config.num_updates,
            )
        else:
            lr_schedule = config.learning_rate

        optimizer = optax.chain(
            optax.clip_by_global_norm(config.max_grad_norm),
            optax.adam(lr_schedule),
        )
        opt_state = optimizer.init(params)

        # ── Resume from checkpoint ──
        start_update = 0
        if config.resume:
            print(f"[Main] Resuming from {config.resume}")
            resume_mgr = CheckpointManager(config.resume, max_to_keep=1)
            target = {"params": params, "opt_state": opt_state, "step": 0}
            params, opt_state, start_update = resume_mgr.restore(target=target)
            start_update += 1  # continue from the next update
            print(f"[Main] Resumed at update {start_update}")

        # ── JIT-compiled PPO functions ──
        compute_action_fn, ppo_step_fn = create_ppo_fns(
            model, optimizer, config.clip_eps, config.vf_coef, config.ent_coef
        )

        # ── Start Async Workers ──
        print("[Main] Starting InferenceServer and Async Workers...")
        inference_server = InferenceServer(
            compute_action_fn,
            vision_encoder,
            max_batch_size=config.num_envs,
            max_seq_len=config.max_seq_len,
        )
        inference_server.start(params, rng, embed_dim=embed_dim)

        trajectory_queue = queue.Queue()
        workers = []
        for i, env in enumerate(envs):
            worker = AsyncRolloutWorker(
                worker_id=i,
                env=env,
                inference_server=inference_server,
                trajectory_queue=trajectory_queue,
                num_steps=config.num_steps,
                gamma=config.gamma,
                gae_lambda=config.gae_lambda,
            )
            workers.append(worker)
            worker.start()

        # ────────────────────── TRAINING LOOP ──────────────────────── #
        print(
            f"\n[Main] Starting training: {config.num_updates} updates, "
            f"{config.num_steps} steps/rollout, {config.num_envs} envs\n"
        )
        t_start = time.time()

        for update in range(start_update, config.num_updates):
            t_update = time.time()

            # ── 1. Collect rollouts asynchronously ──
            t_rollout = time.time()
            trajectories = []
            while len(trajectories) < config.num_envs:
                # Wait for trajectories from any worker
                traj = trajectory_queue.get()
                trajectories.append(traj)
            dt_rollout = time.time() - t_rollout

            # ── 2. Stack trajectories to preserve (num_envs, num_steps, ...) ──
            buffer_seq = {
                "image_embeds": jnp.array(
                    np.stack([t["image_embeds"] for t in trajectories])
                ),
                "kinematics": jnp.array(
                    np.stack([t["kinematics"] for t in trajectories])
                ),
                "actions": {
                    k: jnp.array(np.stack([t["actions"][k] for t in trajectories]))
                    for k in trajectories[0]["actions"].keys()
                },
                "log_probs": jnp.array(
                    np.stack([t["log_probs"] for t in trajectories])
                ),
                "values": jnp.array(np.stack([t["values"] for t in trajectories])),
            }
            seq_advantages = jnp.array(
                np.stack([t["advantages"] for t in trajectories])
            )
            seq_returns = jnp.array(np.stack([t["returns"] for t in trajectories]))

            # Combine rewards and dones for episode stats logging
            flat_rewards = np.concatenate([t["rewards"] for t in trajectories])
            flat_dones = np.concatenate([t["dones"] for t in trajectories])

            completed_returns = []
            completed_lengths = []
            for t in trajectories:
                completed_returns.extend(t["completed_returns"])
                completed_lengths.extend(t["completed_lengths"])

            # ── 3. PPO update (updating model params) ──
            t_ppo = time.time()

            rng, update_rng = jax.random.split(rng)
            params, opt_state, metrics = ppo_update(
                ppo_step_fn,
                params,
                opt_state,
                buffer_seq,
                seq_advantages,
                seq_returns,
                config.num_epochs,
                config.num_minibatches,
                config.max_seq_len,
                update_rng,
            )

            # Send latest params to InferenceServer
            rng, server_rng = jax.random.split(rng)
            inference_server.update_params(params, server_rng)

            dt_ppo = time.time() - t_ppo

            # ── 4. Logging ──
            logger.log_ppo_metrics(metrics, update)

            if completed_returns:
                logger.log_episode_stats(completed_returns, completed_lengths, update)

            if callable(lr_schedule):
                current_lr = float(lr_schedule(update))
            else:
                current_lr = float(lr_schedule)
            logger.log_learning_rate(current_lr, update)

            dt_total = time.time() - t_update
            fps = (config.num_steps * config.num_envs) / dt_total

            # Console summary
            ep_info = ""
            if completed_returns:
                ep_info = (
                    f" | ep_ret={np.mean(completed_returns):.1f}"
                    f" ({len(completed_returns)} eps)"
                )
            print(
                f"[Update {update:>5d}/{config.num_updates}] "
                f"loss={float(metrics.total_loss):.4f} "
                f"pg={float(metrics.policy_loss):.4f} "
                f"vf={float(metrics.value_loss):.4f} "
                f"ent={float(metrics.entropy):.3f} "
                f"kl={float(metrics.approx_kl):.4f} "
                f"clip={float(metrics.clip_fraction):.2f} "
                f"lr={current_lr:.2e} "
                f"fps={fps:.0f} "
                f"[rollout={dt_rollout:.1f}s ppo={dt_ppo:.1f}s total={dt_total:.1f}s]"
                f"{ep_info}"
            )

            # ── 5. Checkpoint ──
            if (update + 1) % config.checkpoint_freq == 0:
                t_ckpt = time.time()
                ckpt_mgr.save(update, params, opt_state)
                print(
                    f"[Checkpoint] Saved at update {update} ({time.time() - t_ckpt:.1f}s)"
                )

        # ── Final checkpoint ──
        ckpt_mgr.save(config.num_updates - 1, params, opt_state)

        elapsed = time.time() - t_start
        print(
            f"\n[Main] Training complete. {config.num_updates} updates in {elapsed:.1f}s"
        )

    finally:
        # Always clean up game instances, even on crash
        try:
            if "inference_server" in locals():
                inference_server.stop()
            if "workers" in locals():
                for w in workers:
                    w.stop()
        except:
            pass
        cleanup(envs, instances, logger)


if __name__ == "__main__":
    main()
