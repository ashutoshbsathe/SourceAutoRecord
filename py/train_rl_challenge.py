import os

import numpy as np
import ray
import torch
import torch.nn as nn
import torchvision
from absl import app, flags
from gymnasium import spaces
from ray import tune
from ray.rllib.algorithms.ppo import PPOConfig
from ray.rllib.algorithms.ppo.torch.default_ppo_torch_rl_module import DefaultPPOTorchRLModule
from ray.rllib.core.models.catalog import Catalog
from ray.rllib.core.rl_module.rl_module import RLModuleSpec
from torchvision.models import Swin_T_Weights

from game_launcher import Portal2GameInstanceManager
from rl_challenge_env import Portal2Env

FLAGS = flags.FLAGS

flags.DEFINE_string("map_name", "sp_a2_triple_laser", "Map to load for the environment.")
flags.DEFINE_list(
    "target_pos", ["0.0", "0.0", "0.0"],
    "Target position as comma-separated x,y,z floats.",
)
flags.DEFINE_integer("num_instances", 1, "Number of parallel Portal 2 game instances (= Ray env runners).")
flags.DEFINE_integer("num_iterations", 100, "Number of training iterations.")
flags.DEFINE_integer("checkpoint_freq", 10, "Checkpoint frequency in iterations.")
flags.DEFINE_integer("max_steps", 96, "Max steps per episode.")


# ---------------------------------------------------------------------------
# Action-dimension helper (used by PortalRLModule to size the actor head)
# ---------------------------------------------------------------------------

def get_action_dim(space) -> int:
    if isinstance(space, spaces.Discrete):
        return space.n
    elif isinstance(space, spaces.Box):
        return int(np.prod(space.shape)) * 2
    elif isinstance(space, spaces.Dict):
        return sum(get_action_dim(s) for s in space.values())
    elif isinstance(space, spaces.Tuple):
        return sum(get_action_dim(s) for s in space)
    else:
        raise ValueError(f"Unsupported space: {space}")


# ---------------------------------------------------------------------------
# Custom RLlib module: frozen Swin-T image encoder + position MLP
# ---------------------------------------------------------------------------

class PortalRLModule(DefaultPPOTorchRLModule):
    def setup(self):
        # Frozen Swin-T backbone; only the projection head is trained.
        self.encoder = torchvision.models.swin_t(weights=Swin_T_Weights.DEFAULT)
        for p in self.encoder.parameters():
            p.requires_grad = False
        self.encoder.head = nn.Sequential(nn.Linear(768, 384), nn.LayerNorm([384]))

        # Small MLP to embed the 3-D position into the same latent space.
        self.position_features = nn.Sequential(nn.Linear(3, 384), nn.LayerNorm([384]))

        # PPO heads operate on the 768-D concatenated latent.
        action_dim = get_action_dim(self.action_space)
        self.actor_head = nn.Linear(768, action_dim)
        self.critic_head = nn.Linear(768, 1)

        self.action_dist_cls = Catalog._get_dist_cls_from_action_space(
            self.action_space, framework="torch"
        )

    def _encode(self, batch) -> torch.Tensor:
        image_obs = batch["obs"]["image"].permute(0, 3, 1, 2)
        img_latents = self.encoder(image_obs)
        pos_latents = self.position_features(batch["obs"]["position"])
        return torch.cat([img_latents, pos_latents], dim=1)

    def _forward(self, batch, **kwargs):
        return {"action_dist_inputs": self.actor_head(self._encode(batch))}

    def _forward_train(self, batch, **kwargs):
        latents = self._encode(batch)
        return {
            "action_dist_inputs": self.actor_head(latents),
            "embeddings": latents,
        }

    def compute_values(self, batch, embeddings=None, **kwargs):
        latents = embeddings if embeddings is not None else self._encode(batch)
        return self.critic_head(latents).squeeze(-1)


# ---------------------------------------------------------------------------
# Environment factory (called once per Ray env-runner worker)
# ---------------------------------------------------------------------------

def env_creator(env_config):
    """
    RLlib calls this in each remote worker process.

    worker_index is 1-based for remote workers (0 for the local worker when
    num_env_runners=0).  We subtract 1 so instance IDs are 0-based and map
    cleanly to sar_harness_instance / port 50000+N.

    The manager is retrieved by name from the Ray cluster — it was created
    once in main() before training started and owns all game processes.
    """
    instance_id = (env_config.worker_index - 1) if hasattr(env_config, "worker_index") else 0
    manager = ray.get_actor("portal2_manager")

    return Portal2Env(
        map_name=env_config["map_name"],
        target_pos=env_config["target_pos"],
        instance_id=instance_id,
        max_steps=env_config.get("max_steps", 300),
        num_ticks_per_step=8,
        render_mode=None,
        manager=manager,
    )


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main(argv):
    del argv

    num_instances = FLAGS.num_instances
    target_pos = tuple(float(x) for x in FLAGS.target_pos)

    ray.init()

    # Create the manager once as a named Ray actor so env_creator can retrieve
    # it by name from any worker process without serialising the handle.
    manager = Portal2GameInstanceManager.options(
        name="portal2_manager",
        lifetime="detached",
    ).remote(num_instances=num_instances)

    print(f"[main] Starting {num_instances} Portal 2 instance(s)...")
    ray.get(manager.start_all.remote())
    print("[main] All instances ready. Starting training...")

    tune.register_env("Portal2Challenge-v0", env_creator)

    # train_batch_size scales linearly with workers so each worker contributes
    # a fixed number of steps per training iteration.
    train_batch_size = 64 * num_instances

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
        .env_runners(
            # Each remote worker owns exactly one Portal 2 instance.
            # worker_index is 1-based, so instance IDs are worker_index-1 = 0..N-1.
            num_env_runners=num_instances,
            num_envs_per_env_runner=1,
        )
        # Evaluation is disabled: enabling it spawns an extra Portal2Env that
        # would try to connect to an already-claimed gRPC port.
        # Run evaluation separately using saved checkpoints.
        .evaluation(evaluation_interval=None)
        .training(
            train_batch_size=train_batch_size,
            minibatch_size=8,
            lr=1e-2,
            grad_clip=2,
            grad_clip_by="global_norm",
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

    try:
        tuner = tune.Tuner(
            "PPO",
            param_space=config.to_dict(),
            run_config=run_config,
        )
        tuner.fit()
        print("Training finished.")
    finally:
        print("[main] Stopping all Portal 2 instances...")
        ray.get(manager.stop_all.remote())
        print("[main] Done.")


if __name__ == "__main__":
    app.run(main)
