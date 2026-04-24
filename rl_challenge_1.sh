#!/bin/bash

readonly MAP_NAME="workshop/1858300862251329775/1644417521"
readonly TARGET_POS="951.94,1137.39,449.03"

uv run python py/test_env.py \
    --map_name "$MAP_NAME" \
    --target_pos "$TARGET_POS" \
    --reset_prob 0.01 \
    --max_steps 300