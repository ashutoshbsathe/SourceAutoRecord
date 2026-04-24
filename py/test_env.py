import time
import random
import numpy as np
from absl import app
from absl import flags
from portal2_env import Portal2Env

FLAGS = flags.FLAGS

flags.DEFINE_float("duration", 120.0, "Total duration to run the test in seconds.")
flags.DEFINE_string(
    "map_name", "sp_a2_triple_laser", "Map to load for the environment."
)
flags.DEFINE_list(
    "target_pos", ["0.0", "0.0", "0.0"], "Target position as a comma-separated list of x,y,z"
)
flags.DEFINE_float(
    "reset_prob", 0.001, "Probability of triggering a random reset per step."
)
flags.DEFINE_integer(
    "max_steps", 1000, "Maximum number of steps per episode before a forced reset."
)
flags.DEFINE_bool("render", True, "Whether to use human rendering mode.")
flags.DEFINE_bool("state_only", False, "Whether to run in state-only mode (no pixels).")


def main(argv):
    del argv  # Unused

    print(f"Initializing Portal2Env with map: {FLAGS.map_name}...")
    env = Portal2Env(
        map_name=FLAGS.map_name,
        target_pos=tuple(float(x) for x in FLAGS.target_pos),
        render_mode="human" if FLAGS.render else None,
        state_only=FLAGS.state_only,
    )

    print("Calling env.reset()...")
    obs, info = env.reset()

    print(f"Initial Obs Keys: {obs.keys()}")
    if "pixels" in obs:
        print(f"Pixels shape: {obs['pixels'].shape}")
    print(f"Position: {obs['position']}")

    print(f"\nStarting test for {FLAGS.duration} seconds...")
    start_t = time.perf_counter()

    total_steps = 0
    episodes = 1
    episode_steps = 0

    while True:
        elapsed = time.perf_counter() - start_t
        if elapsed >= FLAGS.duration:
            break

        # Check if we should reset
        force_reset = False
        if FLAGS.reset_prob > 0 and random.random() < FLAGS.reset_prob:
            print(f"Random reset triggered at step {episode_steps}!")
            force_reset = True
        elif episode_steps >= FLAGS.max_steps:
            print(f"Max steps ({FLAGS.max_steps}) reached!")
            force_reset = True

        if force_reset:
            print("Resetting environment...")
            obs, info = env.reset()
            episodes += 1
            episode_steps = 0
            continue

        # Sample action and step
        action = env.action_space.sample()
        obs, reward, terminated, truncated, info = env.step(action)

        if FLAGS.render:
            env.render()

        total_steps += 1
        episode_steps += 1

        if total_steps % 50 == 0:
            print(
                f"Global Step {total_steps} | Ep Step {episode_steps} | Reward: {reward:.1f} | Terminated: {terminated}"
            )

        if terminated:
            print(f"Agent died at step {episode_steps}! Episode terminated.")
            print("Resetting environment...")
            obs, info = env.reset()
            episodes += 1
            episode_steps = 0

    dur = time.perf_counter() - start_t
    print(f"\n{'=' * 30}")
    print("Benchmark Results:")
    print(f"Total time:  {dur:.2f}s")
    print(f"Total steps: {total_steps}")
    print(f"Episodes:    {episodes}")
    print(f"Steps/sec:   {total_steps / dur:.1f}")
    print(f"{'=' * 30}")

    env.close()


if __name__ == "__main__":
    app.run(main)
