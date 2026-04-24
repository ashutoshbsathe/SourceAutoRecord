import gymnasium as gym
from gymnasium import spaces
import numpy as np
import time
import cv2

from p2harness import P2Harness, harness_pb2

IMAGE_SIZE = 84

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
    ):

        super().__init__()
        self.map_name = map_name
        self.target_pos = np.array(target_pos, dtype=np.float32)
        self.address = address
        self.render_mode = render_mode
        self.num_ticks = num_ticks_per_step
        self.max_steps = max_steps
        self.episode_steps = 0

        self.harness = P2Harness(self.address)

        # Connect and Handshake
        resp = self.harness.handshake()
        print(f"[Portal2Env] Connected to {resp.game_version}, map: {resp.map_name}")

        self.cv2 = cv2

        # ----------------------------------------------------
        # Action Space Definition
        # ----------------------------------------------------
        # Shape: (13,)
        # 0: key_forward
        # 1: key_left
        # 2: key_backward
        # 3: key_right
        # 4: key_use
        # 5: key_zoomin
        # 6: key_zoomout
        # 7: key_crouch
        # 8: portal_primary
        # 9: portal_secondary
        # 10: key_jump
        # 11: mouse_dx (float)
        # 12: mouse_dy (float)
        self.action_space = spaces.Box(
            low=-1.0, high=1.0, shape=(13,), dtype=np.float32
        )

        # ----------------------------------------------------
        # Observation Space Definition
        # ----------------------------------------------------
        # We output only an unnormalized IMAGE_SIZE x IMAGE_SIZE RGB image
        # RLlib's default VisionNetwork will automatically handle this setup efficiently
        self.observation_space = spaces.Box(
            low=0,
            high=255,
            shape=(IMAGE_SIZE, IMAGE_SIZE, 3),
            dtype=np.float32,
        )

    def _get_obs(self, env_msg: harness_pb2.EnvironmentMessage):
        """Parse EnvironmentMessage into Gym observation."""
        pixels = self.harness.get_shm_pixels()
        resized = self.cv2.resize(pixels, (IMAGE_SIZE, IMAGE_SIZE))
        return resized.astype(np.float32)

    def _check_terminated(self, dist: float) -> bool:
        """Utility to determine if the episode should end due to death."""
        return bool(abs(dist)<= 10)

    def reset(self, seed=None, options=None):
        """Restarts the level/map, re-establishes the streaming loop, and returns initial obs."""
        super().reset(seed=seed)
        self.episode_steps = 0

        print(f"[Portal2Env] Resetting to map: {self.map_name}")
        reset_resp = self.harness.reset(self.map_name)
        if not reset_resp.success:
            raise RuntimeError(f"Reset failed: {reset_resp.error_message}")

        # The agent loop must be restarted after a reset
        self.harness.start_agent_loop()

        # We need an initial observation.
        action_req = harness_pb2.ActionRequest(num_ticks=1)
        agent_msg = harness_pb2.AgentMessage(
            action=action_req, copy_pixels_to_shm=True
        )

        env_msg = self.harness.step_agent_loop(agent_msg)
        obs = self._get_obs(env_msg)

        return obs, {}

    def step(self, action):
        """Execute action, retrieve observation, and compute reward/termination."""

        # 1. Translate Box to ActionRequest
        action_req = harness_pb2.ActionRequest(
            num_ticks=self.num_ticks,
            key_forward=bool(action[0] > 0.0),
            key_left=bool(action[1] > 0.0),
            key_backward=bool(action[2] > 0.0),
            key_right=bool(action[3] > 0.0),
            key_use=bool(action[4] > 0.0),
            key_zoomin=bool(action[5] > 0.0),
            key_zoomout=bool(action[6] > 0.0),
            key_crouch=bool(action[7] > 0.0),
            portal_primary=bool(action[8] > 0.0),
            portal_secondary=bool(action[9] > 0.0),
            key_jump=bool(action[10] > 0.0),
            mouse_dx=float(action[11]),
            mouse_dy=float(action[12]),
        )

        agent_msg = harness_pb2.AgentMessage(
            action=action_req, copy_pixels_to_shm=True
        )

        # 2. Push to stream and get response
        env_msg = self.harness.step_agent_loop(agent_msg)
        if not env_msg.success:
            raise RuntimeError(f"AgentLoop step failed: {env_msg.error_message}")

        obs = self._get_obs(env_msg)
        state = env_msg.state
        pos = np.array([state.position.x, state.position.y, state.position.z], dtype=np.float32)
        dist = np.linalg.norm(pos - self.target_pos)

        # 3. Handle Termination explicitly
        self.episode_steps += 1
        terminated = self._check_terminated(dist)
        truncated = bool(self.episode_steps >= self.max_steps)

        # # 4. Calculate Sparse Reward (-1 * Euclidean distance) at the end
        # if terminated or truncated:
        #     reward = -1.0 * dist
        # else:
        #     reward = 0.0
        reward = -1.0 * dist

        return obs, reward, terminated, truncated, {}

    def render(self):
        """Gym render method."""
        if self.render_mode == "rgb_array":
            return self.harness.get_shm_pixels()
        elif self.render_mode == "human":
            pixels = self.harness.get_shm_pixels()
            bgr_frame = self.cv2.cvtColor(pixels, self.cv2.COLOR_RGB2BGR)
            self.cv2.namedWindow("Portal 2 RL Demo", self.cv2.WINDOW_NORMAL)
            self.cv2.resizeWindow(
                "Portal 2 RL Demo", self.harness.shm_width, self.harness.shm_height
            )
            self.cv2.imshow("Portal 2 RL Demo", bgr_frame)
            self.cv2.waitKey(1)

    def close(self):
        """Cleanup environment resources."""
        if self.render_mode == "human":
            self.cv2.destroyAllWindows()
        self.harness.close()
