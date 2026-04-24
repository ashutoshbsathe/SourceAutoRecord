import os
import ray
from ray import train, tune
from ray.rllib.algorithms.ppo import PPOConfig
from absl import app, flags

from rl_challenge_env import Portal2Env

FLAGS = flags.FLAGS

flags.DEFINE_string(
    "map_name", "sp_a2_triple_laser", "Map to load for the environment."
)
flags.DEFINE_list(
    "target_pos", ["0.0", "0.0", "0.0"], "Target position as a comma-separated list of x,y,z"
)
flags.DEFINE_integer("num_iterations", 100, "Number of training iterations.")
flags.DEFINE_integer("checkpoint_freq", 10, "Checkpoint frequency in iterations.")
flags.DEFINE_integer("max_steps", 300, "Max steps per episode.")

def env_creator(env_config):
    # Parse target_pos from string list to float tuple
    # Note: env_creator runs in a remote worker sometimes, so we pass it explicitly via env_config
    return Portal2Env(
        map_name=env_config["map_name"],
        target_pos=env_config["target_pos"],
        max_steps=env_config.get("max_steps", 300),
        render_mode=None,
    )


def main(argv):
    del argv  # Unused

    # Initialize Ray (using default /tmp to avoid AF_UNIX socket length limits)
    ray.init()

    # Register the environment
    tune.register_env("Portal2Challenge-v0", env_creator)

    # Convert flags to appropriate types
    target_pos = tuple(float(x) for x in FLAGS.target_pos)

    config = (
        PPOConfig()
        .environment(
            "Portal2Challenge-v0",
            env_config={
                "map_name": FLAGS.map_name,
                "target_pos": target_pos,
                "max_steps": FLAGS.max_steps,
            },
        )
        .resources(num_gpus=1)
        .framework("torch")
        # Ensure we only use 1 environment worker total so we don't try to open multiple game clients
        # 0 means training runs in the local worker alongside the env
        .env_runners(
            num_env_runners=0,
            num_envs_per_env_runner=1,
        )
        # We disable evaluation during training here. If we enable it, RLlib will instantiate a 
        # *second* Portal2Env object which will try to connect to the same gRPC game server and conflict.
        # It's better to evaluate separately offline using saved checkpoints.
        .evaluation(
            evaluation_interval=None,
        )
    )

    run_config = tune.RunConfig(
        name="ppo_portal2_challenge",
        storage_path=os.path.abspath(os.path.join(os.getcwd(), "ray_results")),
        checkpoint_config=tune.CheckpointConfig(
            checkpoint_frequency=FLAGS.checkpoint_freq,
            checkpoint_at_end=True,
        ),
        stop={"training_iteration": FLAGS.num_iterations},
    )

    tuner = tune.Tuner(
        "PPO",
        param_space=config.to_dict(),
        run_config=run_config,
    )

    results = tuner.fit()
    print("Training finished.")


if __name__ == "__main__":
    app.run(main)
