import os
import ray
from ray import train, tune
from ray.rllib.algorithms.ppo import PPOConfig
from absl import app, flags
import torch.nn as nn
from ray.rllib.algorithms.ppo.torch.default_ppo_torch_rl_module import DefaultPPOTorchRLModule
from ray.rllib.core.rl_module.rl_module import RLModuleSpec

import numpy as np
from gymnasium import spaces

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
flags.DEFINE_integer("max_steps", 384, "Max steps per episode.")

def get_action_dim(space):
    if isinstance(space, spaces.Discrete):
        return space.n
    elif isinstance(space, spaces.Box):
        return np.prod(space.shape) * 2
    elif isinstance(space, spaces.Dict):
        return sum(get_action_dim(s) for s in space.values())
    elif isinstance(space, spaces.Tuple):
        return sum(get_action_dim(s) for s in space)
    else:
        raise ValueError(f"Unsupported space: {space}")

class ResidualBlock(nn.Module):
    def __init__(self, channels):
        super().__init__()
        self.conv = nn.Sequential(
            nn.Conv2d(channels, channels, kernel_size=3, padding=1),
            nn.ReLU(),
            nn.Conv2d(channels, channels, kernel_size=3, padding=1),
        )
        self.relu = nn.ReLU()

    def forward(self, x):
        return self.relu(x + self.conv(x))

class PortalResNetEncoder(nn.Module):
    def __init__(self, input_shape, latent_dim=512):
        super().__init__()
        # input_shape is (240, 240, 3) -> Convert to (3, 240, 240) in forward
        self.net = nn.Sequential(
            # Stage 1: Fast spatial reduction (240 -> 60)
            nn.Conv2d(3, 32, kernel_size=7, stride=4, padding=3), 
            nn.ReLU(),
            # Stage 2: ResBlocks (60 -> 30)
            nn.Conv2d(32, 64, kernel_size=3, stride=2, padding=1),
            ResidualBlock(64),
            # Stage 3: ResBlocks (30 -> 15)
            nn.Conv2d(64, 128, kernel_size=3, stride=2, padding=1),
            ResidualBlock(128),
            # Stage 4: Global Pooling
            nn.AdaptiveAvgPool2d((1, 1)),
            nn.Flatten(),
            nn.Linear(128, latent_dim),
            nn.ReLU()
        )

    def forward(self, x):
        # Ray sends (B, H, W, C), Torch expects (B, C, H, W)
        x = x.permute(0, 3, 1, 2)
        return self.net(x)

class PortalRLModule(DefaultPPOTorchRLModule):
    def setup(self):
        # 1. Initialize the custom encoder
        self.encoder = PortalResNetEncoder(
            input_shape=self.observation_space.shape,
            latent_dim=256
        )
        
        # 2. Re-wire the PPO heads
        # Dynamically compute action_dim needed for RLlib's Action Distribution
        action_dim = get_action_dim(self.action_space)
        self.actor_head = nn.Linear(256, action_dim)
        self.critic_head = nn.Linear(256, 1)

        # 3. Explicitly set the action distribution class (since we don't call super().setup())
        from ray.rllib.core.models.catalog import Catalog
        self.action_dist_cls = Catalog._get_dist_cls_from_action_space(
            self.action_space, framework="torch"
        )

    def _forward(self, batch, **kwargs):
        obs = batch["obs"]
        latents = self.encoder(obs)
        return {
            "action_dist_inputs": self.actor_head(latents),
        }

    def _forward_train(self, batch, **kwargs):
        obs = batch["obs"]
        latents = self.encoder(obs)
        return {
            "action_dist_inputs": self.actor_head(latents),
            "embeddings": latents,
        }

    def compute_values(self, batch, embeddings=None, **kwargs):
        if embeddings is not None:
            latents = embeddings
        else:
            obs = batch["obs"]
            latents = self.encoder(obs)
        return self.critic_head(latents).squeeze(-1)

def env_creator(env_config):
    # Parse target_pos from string list to float tuple
    # Note: env_creator runs in a remote worker sometimes, so we pass it explicitly via env_config
    return Portal2Env(
        map_name=env_config["map_name"],
        target_pos=env_config["target_pos"],
        max_steps=env_config.get("max_steps", 300),
        render_mode=None,
        num_ticks_per_step=8,
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
        .rl_module(rl_module_spec=RLModuleSpec(module_class=PortalRLModule))
        .learners(num_learners=1, num_gpus_per_learner=1)
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
        .training(
            train_batch_size=512,
            minibatch_size=64,
            lr=1e-4,
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
