#!/usr/bin/bash
#
# Build SAR (sar.so) and install it into the Portal 2 game folder.
#
# Reads STEAM_ROOT from .env (if present), else the environment, else falls
# back to ~/.steam/root. The plugin is copied to:
#   $STEAM_ROOT/steamapps/common/Portal 2/portal2/sar.so
#
# By default only sar.so is installed; load it from the in-game developer
# console with:  plugin_load sar
#
# Pass --with-autoexec (-a) to ALSO install the harness autoexec.cfg
# (src/Features/Harness/autoexec.cfg) into the game's portal2/cfg/ folder.
# Source auto-runs cfg/autoexec.cfg on launch, and that cfg does plugin_load
# sar itself, so the plugin loads without the manual console command.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# --- args ---------------------------------------------------------------
WITH_AUTOEXEC=0
for arg in "$@"; do
    case "$arg" in
        -a|--with-autoexec) WITH_AUTOEXEC=1 ;;
        -h|--help)
            echo "Usage: $0 [--with-autoexec]"
            echo "  -a, --with-autoexec   also install src/Features/Harness/autoexec.cfg"
            echo "                        into <Portal 2>/portal2/cfg/ (NOT default)"
            exit 0
            ;;
        *)
            echo "error: unknown argument: $arg" >&2
            echo "Usage: $0 [--with-autoexec]" >&2
            exit 1
            ;;
    esac
done

# Source .env for STEAM_ROOT (and friends) if present.
if [ -f .env ]; then
    set -a
    . ./.env
    set +a
fi

STEAM_ROOT="${STEAM_ROOT:-$HOME/.steam/root}"
PORTAL2_DIR="$STEAM_ROOT/steamapps/common/Portal 2"
SAR_SO_LOCATION="$PORTAL2_DIR/portal2/"
AUTOEXEC_CFG_LOCATION="$PORTAL2_DIR/portal2/cfg/"
AUTOEXEC_SRC="$SCRIPT_DIR/src/Features/Harness/autoexec.cfg"

if [ ! -d "$PORTAL2_DIR" ]; then
    echo "error: Portal 2 directory not found:" >&2
    echo "  $PORTAL2_DIR" >&2
    echo "Set STEAM_ROOT in .env (see .env.example) to point at your Steam root." >&2
    exit 1
fi

echo "==> Building sar.so (RELEASE_BUILD=1)"
RELEASE_BUILD=1 make -j"$(nproc --all)"

echo "==> Installing sar.so -> $SAR_SO_LOCATION"
cp sar.so "$SAR_SO_LOCATION"

if [ "$WITH_AUTOEXEC" -eq 1 ]; then
    if [ ! -f "$AUTOEXEC_SRC" ]; then
        echo "error: autoexec.cfg not found at $AUTOEXEC_SRC" >&2
        exit 1
    fi
    echo "==> Installing autoexec.cfg -> $AUTOEXEC_CFG_LOCATION"
    mkdir -p "$AUTOEXEC_CFG_LOCATION"
    cp "$AUTOEXEC_SRC" "$AUTOEXEC_CFG_LOCATION"
fi

echo
if [ "$WITH_AUTOEXEC" -eq 1 ]; then
    echo "Installed sar.so + autoexec.cfg. The autoexec runs on launch and does"
    echo "plugin_load sar itself, so just start the game."
else
    echo "Installed sar.so. In the Portal 2 developer console, run:  plugin_load sar"
    echo "(Re-run with --with-autoexec to also install the harness autoexec.cfg.)"
fi
