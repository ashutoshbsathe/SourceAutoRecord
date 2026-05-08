import time
from game_launcher import (
    GameInstance,
    DEFAULT_GAMESCOPE_ARGS,
    DEFAULT_GAME_ARGS,
    DEFAULT_STEAM_RUNTIME_SH,
    DEFAULT_PORTAL2_SH,
    DEFAULT_STAGGER_DELAY,
    get_instance_specific_args,
)
from p2harness import P2Harness, harness_pb2
from concurrent.futures import ThreadPoolExecutor, as_completed

instances = []
NUM_INSTANCES = 8

for i in range(NUM_INSTANCES):
    instances.append(
        GameInstance(
            instance_id=i,
            gamescope_args=DEFAULT_GAMESCOPE_ARGS.copy(),
            game_args=DEFAULT_GAME_ARGS.copy() + get_instance_specific_args(i),
            steam_runtime_sh=DEFAULT_STEAM_RUNTIME_SH,
            portal2_sh=DEFAULT_PORTAL2_SH,
        )
    )

for instance in instances:
    instance.start()
    time.sleep(DEFAULT_STAGGER_DELAY)

print("Booting...")
time.sleep(10)

harnesses = []
for i in range(NUM_INSTANCES):
    harnesses.append(P2Harness(f"localhost:{50000 + i}"))

print("Handshaking...")
for harness in harnesses:
    print(harness.handshake())
    harness.reset("sp_a2_laser_chaining")
    harness.start_agent_loop()

print("Stepping in parallel...")


def step_env(harness, i, step):
    action_req = harness_pb2.ActionRequest(num_ticks=8, key_forward=True)
    agent_msg = harness_pb2.AgentMessage(action=action_req, copy_pixels_to_shm=True)
    resp = harness.step_agent_loop(agent_msg)
    return i, step


with ThreadPoolExecutor(max_workers=NUM_INSTANCES) as pool:
    for step in range(10):
        futures = []
        for i, harness in enumerate(harnesses):
            futures.append(pool.submit(step_env, harness, i, step))
        for future in as_completed(futures):
            future.result()
        print(f"Global step {step} completed")

for harness in harnesses:
    harness.close()
for instance in instances:
    instance.stop()
