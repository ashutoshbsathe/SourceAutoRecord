#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────────────
# Portal 2 RL Challenge — Quick-start training script
#
# Launches 4 parallel game instances and trains a PPO agent with a frozen
# ViT-B/16 vision encoder.
#
# Usage:
#   chmod +x py/run_training.sh
#   ./py/run_training.sh
#
# Override any flag by passing it as an argument:
#   ./py/run_training.sh --num-envs 2 --learning-rate 1e-4
# ──────────────────────────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# JAX greedily pre-allocates 75% of GPU RAM by default, starving the game
# instances of VRAM.  Allocate on-demand and cap at 30% (~5 GB on 16 GB).
export XLA_PYTHON_CLIENT_PREALLOCATE=false
export XLA_PYTHON_CLIENT_MEM_FRACTION=0.30

python "$SCRIPT_DIR/train_rl_challenge.py" \
    --map-name "workshop/1858300862251329775/1644417521" \
    --target-pos "951.94,1137.39,449.03" \
    --num-envs 8 \
    --num-ticks-per-step 8 \
    --num-steps 128 \
    --num-updates 10000 \
    --num-epochs 4 \
    --num-minibatches 4 \
    --learning-rate 3e-4 \
    --gamma 0.99 \
    --gae-lambda 0.95 \
    --clip-eps 0.2 \
    --vf-coef 0.5 \
    --ent-coef 0.01 \
    --max-grad-norm 0.5 \
    --anneal-lr \
    --seed 42 \
    --checkpoint-dir checkpoints/ \
    --checkpoint-freq 50 \
    --log-dir runs/ \
    "$@"
