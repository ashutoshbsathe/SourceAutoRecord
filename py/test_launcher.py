import time

from game_launcher import (
    GameInstance,
    DEFAULT_GAMESCOPE_ARGS,
    DEFAULT_GAME_ARGS,
    DEFAULT_STEAM_RUNTIME_SH,
    DEFAULT_PORTAL2_SH,
    DEFAULT_STAGGER_DELAY,
)
from p2harness import P2Harness, harness_pb2

instances = []

NUM_INSTANCES = 6

for i in range(NUM_INSTANCES):
    instances.append(
        GameInstance(
            instance_id=i,
            gamescope_args=DEFAULT_GAMESCOPE_ARGS.copy(),
            game_args=DEFAULT_GAME_ARGS.copy()
            + [
                "-conclear",
                "-dev",
                "-condebug",
                # f"+sar_harness_instance {i}",
                # "+sar_harness 1",
            ],
            steam_runtime_sh=DEFAULT_STEAM_RUNTIME_SH,
            portal2_sh=DEFAULT_PORTAL2_SH,
        )
    )

for instance in instances:
    instance.start()
    time.sleep(DEFAULT_STAGGER_DELAY)

harnesses = []
for i in range(NUM_INSTANCES):
    harnesses.append(P2Harness(f"localhost:{50000 + i}"))

for harness in harnesses:
    print(harness.handshake())

for instance in instances:
    instance.stop()
