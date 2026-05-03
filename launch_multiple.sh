#!/bin/bash 

for i in {0..3}
do
    gamescope -w 640 -h 480 -W 640 -H 480 -b -- \
        /home/absathe/.steam/root/ubuntu12_32/steam-runtime/run.sh \
        "/home/absathe/.steam/root/steamapps/common/Portal 2/portal2.sh" \
        -game portal2 -nosteam -novid -vulkan -sw -nomousegrab \
        +engine_no_focus_sleep 0 +sar_harness_instance $i +sar_harness 1
done