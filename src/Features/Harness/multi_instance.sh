#!/bin/bash

# env STEAM_COMPAT_DATA_PATH=~/p2prefix$i DXVK_STATE_CACHE_PATH=~/p2cache$i \

for i in {1..4}; do
    gamescope -w 640 -h 480 -W 640 -H 480 -b -- \
        ~/.steam/root/ubuntu12_32/steam-runtime/run.sh \
        "~/HDD/SteamLibrary/steamapps/common/Portal 2/portal2.sh" -game portal2 -nosteam -novid -vulkan -sw +engine_no_focus_sleep 0 -nomousegrab &
done
