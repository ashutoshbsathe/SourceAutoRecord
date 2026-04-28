import ray
import gymnasium as gym
from gymnasium import spaces
import numpy as np
import cv2

from p2harness import P2Harness, harness_pb2

IMAGE_SIZE = 224


class Portal2Env(gym.Env):
    """
    Gymnasium environment for Portal 2 RL, powered by the AgentLoop gRPC harness.
    Customized for RL Challenge with vision + position observations and sparse rewards.

    Game process lifecycle is managed externally by Portal2GameInstanceManager (Ray actor).
    Workers call self.restart_game() to recover from crashes; the manager handles stop_all()
    at the end of training.
    """

    metadata = {"render_modes": ["human", "rgb_array"], "render_fps": 60}

    def __init__(
        self,
        map_name: str = "sp_a2_laser_chaining",
        target_pos: tuple = (0.0, 0.0, 0.0),
        instance_id: int = 0,
        render_mode: str = None,
        num_ticks_per_step: int = 1,
        max_steps: int = 300,
        manager=None,  # Portal2GameInstanceManager Ray actor handle
    ):
        super().__init__()
        self.map_name = map_name
        self.target_pos = np.array(target_pos, dtype=np.float32)
        self.instance_id = instance_id
        self.render_mode = render_mode
        self.num_ticks = num_ticks_per_step
        self.max_steps = max_steps
        self.episode_steps = 0
        self.global_steps = 0
        self.prev_dist = 0.0
        self.manager = manager  # Ray actor handle; None in single-process usage

        address = f"localhost:{50000 + instance_id}"
        self.harness = P2Harness(address)

        resp = self._try_with_restarts(lambda : self.harness.handshake())
        print(
            f"[Portal2Env/{instance_id}] Connected to {resp.game_version}, "
            f"map: {resp.map_name}, shm: {resp.shm_name}"
        )

        # Action space: individual discrete axes + continuous mouse
        self.action_space = spaces.Dict({
            "move_fb": spaces.Discrete(3),   # 0: none, 1: forward, 2: backward
            "move_lr": spaces.Discrete(3),   # 0: none, 1: left,    2: right
            "zoom":    spaces.Discrete(3),   # 0: none, 1: in,      2: out
            "portal":  spaces.Discrete(3),   # 0: none, 1: primary, 2: secondary
            "use":     spaces.Discrete(2),   # 0: none, 1: use
            "crouch":  spaces.Discrete(2),   # 0: none, 1: crouch
            "jump":    spaces.Discrete(2),   # 0: none, 1: jump
            "mouse":   spaces.Box(low=-1.0, high=1.0, shape=(2,), dtype=np.float32),
        })

        # Observation: resized RGB frame + world-space position
        self.observation_space = spaces.Dict({
            "image": spaces.Box(
                low=0.0, high=1.0,
                shape=(IMAGE_SIZE, IMAGE_SIZE, 3),
                dtype=np.float32,
            ),
            "position": spaces.Box(
                low=-10000.0, high=10000.0,
                shape=(3,),
                dtype=np.float32,
            ),
        })

    def _get_obs(self, env_msg: harness_pb2.EnvironmentMessage) -> dict:
        """Build a Gym observation dict from an EnvironmentMessage."""
        pixels = self._try_with_restarts(lambda : self.harness.get_shm_pixels())
        resized = cv2.resize(pixels, (IMAGE_SIZE, IMAGE_SIZE))
        state = env_msg.state
        pos = np.array([state.position.x, state.position.y, state.position.z], dtype=np.float32)
        return {
            "image": resized.astype(np.float32) / 255.0,
            "position": pos * np.array([1.0, 1.0, 0.1], dtype=np.float32),
        }

    def _check_terminated(self, dist: float) -> bool:
        """Return True when the agent is within 100 units of the target (goal reached)."""
        return bool(abs(dist) <= 100)
    
    def _try_with_restarts(self, func, max_retries=3):
        for attempt in range(max_retries):
            try:
                return func()
            except Exception as e:
                print(f"[Portal2Env/{self.instance_id}] Attempt {attempt + 1} failed for function {func.__name__}: {e}")
                if attempt == max_retries - 1:
                    raise e
                self.restart_game()

    def restart_game(self):
        """
        Ask the manager to restart this instance's game process.
        Call this when a crash is detected (e.g. harness RPC fails).
        Blocks until the restart completes.
        """
        if self.manager is None:
            print(f"[Portal2Env/{self.instance_id}] No manager set; cannot restart game.")
            return
        print(f"[Portal2Env/{self.instance_id}] Requesting game restart from manager...")
        ray.get(self.manager.restart_instance.remote(self.instance_id))
        print(f"[Portal2Env/{self.instance_id}] Game restarted.")

    def reset(self, seed=None, options=None):
        """Restart the map, re-establish the streaming loop, and return the initial observation."""
        super().reset(seed=seed)
        self.episode_steps = 0
        self.prev_dist = 0.0

        print(f"[Portal2Env/{self.instance_id}] Resetting to map: {self.map_name}")
        reset_resp = self._try_with_restarts(lambda : self.harness.reset(self.map_name))
        if not reset_resp.success:
            raise RuntimeError(f"Reset failed: {reset_resp.error_message}")

        self.harness.start_agent_loop()

        # Short forward-walk burst so the first observation has a meaningful frame.
        action_req = harness_pb2.ActionRequest(num_ticks=192, key_forward=True)
        agent_msg = harness_pb2.AgentMessage(action=action_req, copy_pixels_to_shm=True)
        env_msg = self._try_with_restarts(lambda : self.harness.step_agent_loop(agent_msg))
        obs = self._try_with_restarts(lambda : self._get_obs(env_msg))

        return obs, {}

    def step(self, action):
        """Execute one action, return (obs, reward, terminated, truncated, info)."""
        self.global_steps += 1

        action_req = harness_pb2.ActionRequest(
            num_ticks=self.num_ticks,
            key_forward=bool(action["move_fb"] == 1),
            key_backward=bool(action["move_fb"] == 2),
            key_left=bool(action["move_lr"] == 1),
            key_right=bool(action["move_lr"] == 2),
            key_use=bool(action["use"] == 1),
            key_zoomin=bool(action["zoom"] == 1),
            key_zoomout=bool(action["zoom"] == 2),
            key_crouch=bool(action["crouch"] == 1),
            portal_primary=bool(action["portal"] == 1),
            portal_secondary=bool(action["portal"] == 2),
            key_jump=bool(action["jump"] == 1),
            mouse_dx=float(action["mouse"][0]),
            mouse_dy=float(action["mouse"][1]),
        )
        agent_msg = harness_pb2.AgentMessage(action=action_req, copy_pixels_to_shm=True)

        env_msg = self._try_with_restarts(lambda : self.harness.step_agent_loop(agent_msg))
        if not env_msg.success:
            raise RuntimeError(
                f"AgentLoop step failed: {env_msg.error_message} "
                f"(ep_steps={self.episode_steps}, global_steps={self.global_steps})"
            )

        obs = self._try_with_restarts(lambda : self._get_obs(env_msg))
        state = env_msg.state
        pos = np.array([state.position.x, state.position.y, state.position.z], dtype=np.float32)
        scale = np.array([1.0, 1.0, 0.1], dtype=np.float32)
        dist = np.linalg.norm(pos * scale - self.target_pos * scale)

        self.episode_steps += 1
        terminated = self._check_terminated(dist)
        truncated = bool(self.episode_steps >= self.max_steps)

        dist_improvement = self.prev_dist - dist
        reward = dist_improvement if dist_improvement != 0 else -100.0
        reward += 10000.0 if terminated else 0.0
        reward -= 1.0  # per-step penalty
        self.prev_dist = dist

        print(
            f"[Portal2Env/{self.instance_id}] reward={reward:.2f} dist={dist:.1f} "
            f"ep={self.episode_steps}/{self.max_steps} global={self.global_steps}"
        )

        return obs, reward, terminated, truncated, {}

    def render(self):
        if self.render_mode == "rgb_array":
            return self.harness.get_shm_pixels()
        elif self.render_mode == "human":
            pixels = self.harness.get_shm_pixels()
            bgr = cv2.cvtColor(pixels, cv2.COLOR_RGB2BGR)
            cv2.namedWindow("Portal 2 RL", cv2.WINDOW_NORMAL)
            cv2.resizeWindow("Portal 2 RL", self.harness.shm_width, self.harness.shm_height)
            cv2.imshow("Portal 2 RL", bgr)
            cv2.waitKey(1)

    def close(self):
        """Close the gRPC harness connection. Game lifecycle is managed by the Ray actor manager."""
        if self.render_mode == "human":
            cv2.destroyAllWindows()
        self.harness.close()
