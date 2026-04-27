import gymnasium as gym
from gymnasium import spaces
import numpy as np
import time
import cv2

from p2harness import P2Harness, harness_pb2

IMAGE_SIZE = 224

class Portal2Env(gym.Env):
    """
    Gymnasium environment for Portal 2 RL, powered by the AgentLoop gRPC harness.
    Customized for RL Challenge with Vision-only inputs and sparse rewards.
    """

    metadata = {"render_modes": ["human", "rgb_array"], "render_fps": 60}

    def __init__(
        self,
        map_name: str = "sp_a2_laser_chaining",
        target_pos: tuple = (0.0, 0.0, 0.0),
        address: str = "localhost:50051",
        render_mode: str = None,
        num_ticks_per_step: int = 1,
        max_steps: int = 300,
        game_instance = None,
    ):

        super().__init__()
        self.map_name = map_name
        self.target_pos = np.array(target_pos, dtype=np.float32)
        self.address = address
        self.render_mode = render_mode
        self.num_ticks = num_ticks_per_step
        self.max_steps = max_steps
        self.episode_steps = 0
        self.global_steps = 0
        self.prev_dist = 0
        self.game_instance = game_instance

        self.harness = P2Harness(self.address)

        # Connect and Handshake
        resp = self.harness.handshake()
        print(f"[Portal2Env] Connected to {resp.game_version}, map: {resp.map_name}")

        # ----------------------------------------------------
        # Action Space Definition
        # ----------------------------------------------------
        self.action_space = spaces.Dict({
            'move_fb': spaces.Discrete(3),   # 0: None, 1: Forward, 2: Backward
            'move_lr': spaces.Discrete(3),   # 0: None, 1: Left, 2: Right
            'zoom': spaces.Discrete(3),      # 0: None, 1: In, 2: Out
            'portal': spaces.Discrete(3),    # 0: None, 1: Primary, 2: Secondary
            'use': spaces.Discrete(2),       # 0: None, 1: Use
            'crouch': spaces.Discrete(2),    # 0: None, 1: Crouch
            'jump': spaces.Discrete(2),      # 0: None, 1: Jump
            'mouse': spaces.Box(low=-1.0, high=1.0, shape=(2,), dtype=np.float32)
        })

        # ----------------------------------------------------
        # Observation Space Definition
        # ----------------------------------------------------
        # We output only an unnormalized IMAGE_SIZE x IMAGE_SIZE RGB image
        # RLlib's default VisionNetwork will automatically handle this setup efficiently
        self.observation_space = spaces.Dict({
            "image": spaces.Box(
                low=0.0,
                high=1.0,
                shape=(IMAGE_SIZE, IMAGE_SIZE, 3),
                dtype=np.float32,
            ),
            "position": spaces.Box(
                low=-10000.0,
                high=10000.0,
                shape=(3,),
                dtype=np.float32,
            )
        })

    def _get_obs(self, env_msg: harness_pb2.EnvironmentMessage):
        """Parse EnvironmentMessage into Gym observation."""
        pixels = self.harness.get_shm_pixels()
        resized = cv2.resize(pixels, (IMAGE_SIZE, IMAGE_SIZE))
        return {
            "image": resized.astype(np.float32) / 255.0,
            "position": np.array([env_msg.state.position.x, env_msg.state.position.y, env_msg.state.position.z], dtype=np.float32) * np.array([1.0, 1.0, 0.1])
        }

    def _check_terminated(self, dist: float) -> bool:
        """Utility to determine if the episode should end due to death."""
        return bool(abs(dist)<= 100)

    def reset(self, seed=None, options=None):
        """Restarts the level/map, re-establishes the streaming loop, and returns initial obs."""
        super().reset(seed=seed)
        self.episode_steps = 0
        self.prev_dist = 0

        print(f"[Portal2Env] Resetting to map: {self.map_name}")
        reset_resp = self.harness.reset(self.map_name)
        if not reset_resp.success:
            raise RuntimeError(f"Reset failed: {reset_resp.error_message}")

        # The agent loop must be restarted after a reset
        self.harness.start_agent_loop()

        # We need an initial observation.
        action_req = harness_pb2.ActionRequest(num_ticks=192, key_forward=True)
        agent_msg = harness_pb2.AgentMessage(
            action=action_req, copy_pixels_to_shm=True
        )

        env_msg = self.harness.step_agent_loop(agent_msg)
        obs = self._get_obs(env_msg)

        return obs, {}

    def step(self, action):
        """Execute action, retrieve observation, and compute reward/termination."""
        self.global_steps += 1

        # 1. Translate Dict to ActionRequest
        action_req = harness_pb2.ActionRequest(
            num_ticks=self.num_ticks,
            key_forward=bool(action['move_fb'] == 1),
            key_backward=bool(action['move_fb'] == 2),
            key_left=bool(action['move_lr'] == 1),
            key_right=bool(action['move_lr'] == 2),
            key_use=bool(action['use'] == 1),
            key_zoomin=bool(action['zoom'] == 1),
            key_zoomout=bool(action['zoom'] == 2),
            key_crouch=bool(action['crouch'] == 1),
            portal_primary=bool(action['portal'] == 1),
            portal_secondary=bool(action['portal'] == 2),
            key_jump=bool(action['jump'] == 1),
            mouse_dx=float(action['mouse'][0]),
            mouse_dy=float(action['mouse'][1]),
        )

        agent_msg = harness_pb2.AgentMessage(
            action=action_req, copy_pixels_to_shm=True
        )

        # 2. Push to stream and get response
        env_msg = self.harness.step_agent_loop(agent_msg)
        if not env_msg.success:
            raise RuntimeError(f"AgentLoop step failed: {env_msg.error_message}, Ep steps: {self.episode_steps}, Global steps: {self.global_steps}")

        obs = self._get_obs(env_msg)
        state = env_msg.state
        pos = np.array([state.position.x, state.position.y, state.position.z], dtype=np.float32)
        scale = np.array([1.0, 1.0, 0.1])
        dist = np.linalg.norm(pos * scale - self.target_pos * scale)

        # 3. Handle Termination explicitly
        self.episode_steps += 1
        terminated = self._check_terminated(dist)
        truncated = bool(self.episode_steps >= self.max_steps)

        dist_diff = self.prev_dist - dist
        reward = dist_diff if dist_diff != 0 else -100
        reward += 10000 if terminated else 0
        reward += -1 # step penalty
        self.prev_dist = dist
        print(f"Final Reward: {reward:.4f}, Dist: {dist:.4f}, Ep steps: {self.episode_steps}, Global steps: {self.global_steps}")

        return obs, reward, terminated, truncated, {}

    def render(self):
        """Gym render method."""
        if self.render_mode == "rgb_array":
            return self.harness.get_shm_pixels()
        elif self.render_mode == "human":
            pixels = self.harness.get_shm_pixels()
            bgr_frame = cv2.cvtColor(pixels, cv2.COLOR_RGB2BGR)
            cv2.namedWindow("Portal 2 RL Demo", cv2.WINDOW_NORMAL)
            cv2.resizeWindow(
                "Portal 2 RL Demo", self.harness.shm_width, self.harness.shm_height
            )
            cv2.imshow("Portal 2 RL Demo", bgr_frame)
            cv2.waitKey(1)

    def close(self):
        """Cleanup environment resources."""
        if self.render_mode == "human":
            cv2.destroyAllWindows()
        self.harness.close()
        if self.game_instance:
            self.game_instance.stop()
