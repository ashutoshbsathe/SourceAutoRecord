#!/usr/bin/bash
set -euo pipefail

# Define the target directory where demos will be saved.
# Note: Corrected username to 'absathe' based on the local system path.
TARGET_DIR="/home/absathe/MachineLearning/p2leaderboard"

# The rsync address provided by the leaderboard maintainers.
# Standard rsync URL format typically omits the extra colon after the hostname
# if no port is specified, or uses the double-colon module syntax.
RSYNC_SOURCE="rsync://board.portal2.sr/demos"
# Alternative syntax if the above does not connect:
# RSYNC_SOURCE="board.portal2.sr::demos"

echo "===================================================================="
echo "Preparing to download Portal 2 Leaderboard Demos"
echo "Target Directory: ${TARGET_DIR}"
echo "Source Address:   ${RSYNC_SOURCE}"
echo "===================================================================="

# Ensure the target directory exists
mkdir -p "${TARGET_DIR}"

# Flags used:
#  -a, --archive    : Archive mode; enables recursion and preserves permissions/timestamps.
#  --partial        : Keeps partially transferred files if the transfer is interrupted, enabling quick resume.
#  --info=progress2 : Outputs a clean, global progress bar and summary statistics for the entire batch transfer.

echo "Starting rsync transfer..."
echo "Tip: You can safely cancel (Ctrl+C) and restart this script anytime; rsync will resume where it left off."
echo ""

# Using --info=progress2 displays a clean global progress bar for the entire transfer.
# rsync operates single-threaded per transfer session by default, which usually saturates bandwidth.
rsync -a --partial --info=progress2 "${RSYNC_SOURCE}" "${TARGET_DIR}/"

echo ""
echo "===================================================================="
echo "Transfer complete or up-to-date!"
echo "Demos are available in: ${TARGET_DIR}"
echo "===================================================================="
