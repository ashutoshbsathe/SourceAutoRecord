"""Training configuration and CLI argument parsing."""

from dataclasses import dataclass, field
import argparse
from typing import Tuple


@dataclass
class PPOConfig:
    """All hyperparameters for PPO + ViT training."""

    # --- Environment (mandatory, no defaults) ---
    map_name: str = ''
    target_pos: Tuple[float, float, float] = (0.0, 0.0, 0.0)

    # --- Environment (optional) ---
    num_envs: int = 4
    max_episode_steps: int = 300
    num_ticks_per_step: int = 8

    # --- Rollout ---
    num_steps: int = 128  # rollout length per env per update

    # --- PPO ---
    num_updates: int = 10000
    num_epochs: int = 4
    num_minibatches: int = 4
    learning_rate: float = 3e-4
    gamma: float = 0.99
    gae_lambda: float = 0.95
    clip_eps: float = 0.2
    vf_coef: float = 0.5
    ent_coef: float = 0.01
    max_grad_norm: float = 0.5
    anneal_lr: bool = True

    # --- Model ---
    vit_checkpoint: str = ''  # empty = auto-download from Google
    trunk_hidden: int = 512
    trunk_out: int = 256

    # --- Transformer / POMDP ---
    max_seq_len: int = 128
    transformer_blocks: int = 1
    transformer_heads: int = 8

    # --- Infrastructure ---
    seed: int = 42
    checkpoint_dir: str = 'checkpoints/'
    checkpoint_freq: int = 50
    log_dir: str = 'runs/'
    resume: str = ''


def parse_args() -> PPOConfig:
    """Parse CLI flags into a PPOConfig. --map-name and --target-pos are required."""
    p = argparse.ArgumentParser(description='PPO + ViT-B/16 Portal 2 RL Training')

    # Mandatory
    p.add_argument('--map-name', type=str, required=True, help='Portal 2 map name')
    p.add_argument(
        '--target-pos',
        type=str,
        required=True,
        help="Target position as comma-separated x,y,z (e.g. '100,200,50')",
    )

    # Environment
    p.add_argument('--num-envs', type=int, default=4)
    p.add_argument('--max-episode-steps', type=int, default=300)
    p.add_argument('--num-ticks-per-step', type=int, default=1)

    # Rollout
    p.add_argument('--num-steps', type=int, default=128)

    # PPO
    p.add_argument('--num-updates', type=int, default=10000)
    p.add_argument('--num-epochs', type=int, default=4)
    p.add_argument('--num-minibatches', type=int, default=4)
    p.add_argument('--learning-rate', type=float, default=3e-4)
    p.add_argument('--gamma', type=float, default=0.99)
    p.add_argument('--gae-lambda', type=float, default=0.95)
    p.add_argument('--clip-eps', type=float, default=0.2)
    p.add_argument('--vf-coef', type=float, default=0.5)
    p.add_argument('--ent-coef', type=float, default=0.01)
    p.add_argument('--max-grad-norm', type=float, default=0.5)
    p.add_argument('--anneal-lr', action='store_true', default=True)
    p.add_argument('--no-anneal-lr', dest='anneal_lr', action='store_false')

    # Model
    p.add_argument(
        '--vit-checkpoint',
        type=str,
        default='',
        help='Path to ViT .npz (empty=auto-download)',
    )
    p.add_argument('--trunk-hidden', type=int, default=512)
    p.add_argument('--trunk-out', type=int, default=256)

    # Transformer / POMDP
    p.add_argument('--max-seq-len', type=int, default=128)
    p.add_argument('--transformer-blocks', type=int, default=1)
    p.add_argument('--transformer-heads', type=int, default=8)

    # Infrastructure
    p.add_argument('--seed', type=int, default=42)
    p.add_argument('--checkpoint-freq', type=int, default=50)
    p.add_argument('--log-dir', type=str, default='runs/')
    p.add_argument('--resume', type=str, default='')

    args = p.parse_args()

    # Parse target_pos
    tx, ty, tz = [float(v) for v in args.target_pos.split(',')]

    return PPOConfig(
        map_name=args.map_name,
        target_pos=(tx, ty, tz),
        num_envs=args.num_envs,
        max_episode_steps=args.max_episode_steps,
        num_ticks_per_step=args.num_ticks_per_step,
        num_steps=args.num_steps,
        num_updates=args.num_updates,
        num_epochs=args.num_epochs,
        num_minibatches=args.num_minibatches,
        learning_rate=args.learning_rate,
        gamma=args.gamma,
        gae_lambda=args.gae_lambda,
        clip_eps=args.clip_eps,
        vf_coef=args.vf_coef,
        ent_coef=args.ent_coef,
        max_grad_norm=args.max_grad_norm,
        anneal_lr=args.anneal_lr,
        vit_checkpoint=args.vit_checkpoint,
        trunk_hidden=args.trunk_hidden,
        trunk_out=args.trunk_out,
        max_seq_len=args.max_seq_len,
        transformer_blocks=args.transformer_blocks,
        transformer_heads=args.transformer_heads,
        seed=args.seed,
        checkpoint_freq=args.checkpoint_freq,
        log_dir=args.log_dir,
        resume=args.resume,
    )
