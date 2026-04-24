#!/bin/bash

readonly MAP_NAME="workshop/1858300862251329775/1644417521"
readonly TARGET_POS="951.94,1137.39,449.03"

uv run python py/train_rl_challenge.py \
    --map_name "$MAP_NAME" \
    --target_pos "$TARGET_POS" \
    --num_iterations 100 \
    --checkpoint_freq 10