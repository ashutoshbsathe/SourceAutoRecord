from torchvision.models import Swin_T_Weights
import os
import ray
from ray import tune
from ray.rllib.algorithms.ppo import PPOConfig
from absl import app, flags
import torch
import torch.nn as nn
import torchvision
from ray.rllib.algorithms.ppo.torch.default_ppo_torch_rl_module import DefaultPPOTorchRLModule
from ray.rllib.core.rl_module.rl_module import RLModuleSpec
from ray.rllib.core.models.catalog import Catalog

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
flags.DEFINE_integer("max_steps", 96, "Max steps per episode.")

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

class PortalRLModule(DefaultPPOTorchRLModule):
    def setup(self):
        # 1. Initialize the custom encoder
        # self.encoder = torchvision.models.vit_b_16(weights=torchvision.models.ViT_B_16_Weights.DEFAULT)
        # self.encoder.heads = nn.Identity()
        # for p in self.encoder.parameters():
        #     p.requires_grad = False
        self.encoder = torchvision.models.swin_t(weights=Swin_T_Weights.DEFAULT)
        for p in self.encoder.parameters():
            p.requires_grad = False
        self.encoder.head = nn.Sequential(nn.Linear(768, 384), nn.LayerNorm([384]))
        self.position_features = nn.Sequential(nn.Linear(3, 384), nn.LayerNorm([384]))
        # 2. Re-wire the PPO heads
        # Dynamically compute action_dim needed for RLlib's Action Distribution
        action_dim = get_action_dim(self.action_space)
        self.actor_head = nn.Linear(768, action_dim)
        self.critic_head = nn.Linear(768, 1)

        # 3. Explicitly set the action distribution class (since we don't call super().setup())
        self.action_dist_cls = Catalog._get_dist_cls_from_action_space(
            self.action_space, framework="torch"
        )

    def _forward(self, batch, **kwargs):
        image_obs = batch["obs"]["image"]
        image_obs = image_obs.permute(0, 3, 1, 2)
        image_latents = self.encoder(image_obs)
        pos_obs = batch["obs"]["position"]
        pos_latents = self.position_features(pos_obs)
        latents = torch.cat([image_latents, pos_latents], dim=1)
        return {
            "action_dist_inputs": self.actor_head(latents),
        }

    def _forward_train(self, batch, **kwargs):
        image_obs = batch["obs"]["image"]
        image_obs = image_obs.permute(0, 3, 1, 2)
        image_latents = self.encoder(image_obs)
        pos_obs = batch["obs"]["position"]
        pos_latents = self.position_features(pos_obs)
        latents = torch.cat([image_latents, pos_latents], dim=1)
        return {
            "action_dist_inputs": self.actor_head(latents),
            "embeddings": latents,
        }

    def compute_values(self, batch, embeddings=None, **kwargs):
        if embeddings is not None:
            latents = embeddings
        else:
            image_obs = batch["obs"]["image"]
            image_obs = image_obs.permute(0, 3, 1, 2)
            image_latents = self.encoder(image_obs)
            pos_obs = batch["obs"]["position"]
            pos_latents = self.position_features(pos_obs)
            latents = torch.cat([image_latents, pos_latents], dim=1)
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
            train_batch_size=64,
            minibatch_size=8,
            lr=1e-2,
            grad_clip=2,
            grad_clip_by='global_norm'
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
