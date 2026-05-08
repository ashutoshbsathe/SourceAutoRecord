import gymnasium as gym
from gymnasium import spaces
import numpy as np
import cv2
import time
import collections

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

IMAGE_SIZE = 224


class Portal2Env(gym.Env):
    """
    Gymnasium environment for Portal 2 RL, powered by the AgentLoop gRPC harness.
    Customized for RL Challenge with vision + position observations and sparse rewards.
    """

    metadata = {"render_modes": ["human", "rgb_array"], "render_fps": 60}

    def __init__(
        self,
        instance: GameInstance,
        map_name: str = "sp_a2_laser_chaining",
        target_pos: tuple = (0.0, 0.0, 0.0),
        render_mode: str = None,
        num_ticks_per_step: int = 1,
        max_steps: int = 256,
        progress_threshold: float = 50.0,
        progress_k: int = 8,
        camera_penalty_scale: float = 1e-1,
    ):
        super().__init__()
        self.instance = instance
        self.map_name = map_name
        self.target_pos = np.array(target_pos, dtype=np.float32)
        self.render_mode = render_mode
        self.num_ticks = num_ticks_per_step
        self.max_steps = max_steps
        self.progress_threshold = progress_threshold
        self.progress_k = progress_k
        self.camera_penalty_scale = camera_penalty_scale

        self.episode_steps = 0
        self.global_steps = 0
        self.prev_dist = 0.0
        self.dist_history = collections.deque(maxlen=self.progress_k)
        self.button_history = collections.deque(maxlen=self.progress_k)

        address = f"localhost:{50000 + self.instance.instance_id}"
        self.harness = P2Harness(address)

        # Retry handshake — later instances may need more time to boot
        max_retries = 10
        for attempt in range(max_retries):
            try:
                resp = self.harness.handshake()
                print(
                    f"[Portal2Env/{self.instance.instance_id}] Connected to {resp.game_version}, "
                    f"map: {resp.map_name}, shm: {resp.shm_name}"
                )
                break
            except Exception as e:
                if attempt == max_retries - 1:
                    raise RuntimeError(
                        f"[Portal2Env/{self.instance.instance_id}] Failed to connect "
                        f"after {max_retries} attempts: {e}"
                    ) from e
                # Check if the process actually died
                if not self.instance.is_alive():
                    print(
                        f"[Portal2Env/{self.instance.instance_id}] Instance died, "
                        f"restarting (attempt {attempt + 1}/{max_retries})..."
                    )
                    self.instance.restart()
                    time.sleep(DEFAULT_STAGGER_DELAY)
                    self.harness = P2Harness(address)
                else:
                    wait = min(5 * (attempt + 1), 30)
                    print(
                        f"[Portal2Env/{self.instance.instance_id}] Handshake failed, "
                        f"retrying in {wait}s (attempt {attempt + 1}/{max_retries})..."
                    )
                    time.sleep(wait)

        # Action space: individual discrete axes + continuous mouse
        self.action_space = spaces.Dict(
            {
                "move_fb": spaces.Discrete(3),  # 0: none, 1: forward, 2: backward
                "move_lr": spaces.Discrete(3),  # 0: none, 1: left,    2: right
                "zoom": spaces.Discrete(3),  # 0: none, 1: in,      2: out
                "portal": spaces.Discrete(3),  # 0: none, 1: primary, 2: secondary
                "buttons": spaces.MultiBinary(3),  # 0: use,  1: jump,    2: crouch
                "mouse": spaces.Box(low=-1.0, high=1.0, shape=(2,), dtype=np.float32),
            }
        )

        # Observation: resized RGB frame + world-space position
        self.observation_space = spaces.Dict(
            {
                "image": spaces.Box(
                    low=0.0,
                    high=1.0,
                    shape=(IMAGE_SIZE, IMAGE_SIZE, 3),
                    dtype=np.float32,
                ),
                "kinematics": spaces.Box(
                    low=-10000.0,
                    high=10000.0,
                    shape=(6,),
                    dtype=np.float32,
                ),
            }
        )

    def _get_obs(self, env_msg: harness_pb2.EnvironmentMessage) -> dict:
        """Build a Gym observation dict from an EnvironmentMessage."""
        pixels = self.harness.get_shm_pixels()
        resized = cv2.resize(pixels, (IMAGE_SIZE, IMAGE_SIZE))
        state = env_msg.state
        kinematics = np.array(
            [
                state.position.x,
                state.position.y,
                state.position.z,
                state.camera.x,
                state.camera.y,
                state.camera.z,
            ],
            dtype=np.float32,
        )
        return {
            "image": resized.astype(np.float32) / 255.0,
            "kinematics": kinematics,
        }

    def _check_terminated(self, dist: float) -> bool:
        """Return True when the agent is within 100 units of the target (goal reached)."""
        return bool(abs(dist) <= 100)

    def restart_instance(self):
        """Kill, relaunch, and fully reconnect the gRPC harness."""
        # Tear down old stream and channel
        self.harness.close()

        # Restart the OS process
        self.instance.restart()

        # Create a fresh gRPC connection (old channel is dead)
        address = f"localhost:{50000 + self.instance.instance_id}"
        self.harness = P2Harness(address)

        # Wait for the game to boot with retry (handshake probes gRPC server)
        max_retries = 10
        for attempt in range(max_retries):
            try:
                resp = self.harness.handshake()
                print(
                    f"[Portal2Env/{self.instance.instance_id}] Reconnected to "
                    f"{resp.game_version}, map: {resp.map_name}"
                )
                return
            except Exception:
                if attempt == max_retries - 1:
                    raise
                wait = min(5 * (attempt + 1), 30)
                time.sleep(wait)

    def reset(self, seed=None, options=None):
        """Restart the map, re-establish the streaming loop, and return the initial observation."""
        super().reset(seed=seed)
        if not self.instance.is_alive():
            print(
                f"[Portal2Env/{self.instance.instance_id}] Instance not alive, restarting..."
            )
            self.restart_instance()
        self.episode_steps = 0
        self.prev_dist = 0.0
        self.dist_history.clear()
        self.button_history.clear()

        print(
            f"[Portal2Env/{self.instance.instance_id}] Resetting to map: {self.map_name}"
        )
        reset_resp = self.harness.reset(self.map_name)
        if not reset_resp.success:
            raise RuntimeError(f"Reset failed: {reset_resp.error_message}")

        self.harness.start_agent_loop()

        # Short forward-walk burst so the first observation has a meaningful frame.
        # Use a longer timeout because map loading can take a while.
        action_req = harness_pb2.ActionRequest(num_ticks=192, key_forward=True)
        agent_msg = harness_pb2.AgentMessage(action=action_req, copy_pixels_to_shm=True)
        env_msg = self.harness.step_agent_loop(agent_msg, timeout=30.0)
        obs = self._get_obs(env_msg)

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
            key_use=bool(action["buttons"][0] == 1),
            key_zoomin=bool(action["zoom"] == 1),
            key_zoomout=bool(action["zoom"] == 2),
            key_crouch=bool(action["buttons"][2] == 1),
            portal_primary=bool(action["portal"] == 1),
            portal_secondary=bool(action["portal"] == 2),
            key_jump=bool(action["buttons"][1] == 1),
            mouse_dx=float(action["mouse"][0]) / 10, # desperate times, desperate measures
            mouse_dy=float(action["mouse"][1]) / 10,
        )
        agent_msg = harness_pb2.AgentMessage(action=action_req, copy_pixels_to_shm=True)

        env_msg = self.harness.step_agent_loop(agent_msg)

        obs = self._get_obs(env_msg)
        state = env_msg.state
        pos = np.array(
            [state.position.x, state.position.y, state.position.z], dtype=np.float32
        )
        scale = np.array([1.0, 1.0, 0.1], dtype=np.float32)
        dist = np.linalg.norm(pos * scale - self.target_pos * scale)

        self.episode_steps += 1
        terminated = self._check_terminated(dist)
        truncated = bool(self.episode_steps >= self.max_steps)

        self.dist_history.append(dist)
        button_state = (
            int(action["portal"]),
            int(action["zoom"]),
            int(action["buttons"][0]),
        )
        self.button_history.append(button_state)

        dist_improvement = self.prev_dist - dist
        reward = dist_improvement if dist_improvement != 0 else -100.0
        reward += 10000.0 if terminated else 0.0
        reward -= 1.0  # per-step penalty

        # Camera angle penalty
        camera_penalty = self.camera_penalty_scale * (state.camera.x**2 + state.camera.z**2)
        reward -= camera_penalty

        # Long term progress penalty/reward
        if len(self.dist_history) == self.progress_k:
            progress = self.dist_history[0] - dist
            penalty = progress - self.progress_threshold
            penalty = penalty - self.progress_threshold if abs(penalty) < self.progress_threshold else penalty * 2
        else:
            penalty = 0
        reward += penalty

        # Button spam penalty
        if len(self.button_history) == self.progress_k:
            hist = list(self.button_history)
            for i in range(3):
                states = [h[i] for h in hist]
                transitions = sum(
                    1 for j in range(1, len(states)) if states[j] != states[j - 1]
                )
                held_any_k = any(s != 0 for s in states)

                if held_any_k or transitions >= (self.progress_k / 4.0):
                    reward -= 5.0  # Flat penalty per spammy button

        self.prev_dist = dist

        if self.global_steps % 100 == 0:
            print(
                f"[Portal2Env/{self.instance.instance_id}] reward={reward:.2f} dist={dist:.1f} "
                f"cam={camera_penalty} ep={self.episode_steps}/{self.max_steps} global={self.global_steps}"
            )

        return obs, reward, terminated, truncated, {}

    def render(self):
        if self.render_mode == "rgb_array":
            return self.harness.get_shm_pixels()
        elif self.render_mode == "human":
            pixels = self.harness.get_shm_pixels()
            bgr = cv2.cvtColor(pixels, cv2.COLOR_RGB2BGR)
            cv2.namedWindow("Portal 2 RL", cv2.WINDOW_NORMAL)
            cv2.resizeWindow(
                "Portal 2 RL", self.harness.shm_width, self.harness.shm_height
            )
            cv2.imshow("Portal 2 RL", bgr)
            cv2.waitKey(1)

    def close(self):
        """Close the gRPC harness connection. Game lifecycle is managed by the Ray actor manager."""
        if self.render_mode == "human":
            cv2.destroyAllWindows()
        self.harness.close()
        self.instance.stop()


if __name__ == "__main__":
    instances = []
    for i in range(8):
        instances.append(
            GameInstance(
                instance_id=i,
                gamescope_args=DEFAULT_GAMESCOPE_ARGS.copy(),
                game_args=DEFAULT_GAME_ARGS.copy() + get_instance_specific_args(i),
                steam_runtime_sh=DEFAULT_STEAM_RUNTIME_SH,
                portal2_sh=DEFAULT_PORTAL2_SH,
                debug=False,
            )
        )
    for instance in instances:
        instance.start()
        time.sleep(DEFAULT_STAGGER_DELAY)
    envs = [Portal2Env(instance, render_mode="human") for instance in instances]
    for env in envs:
        env.reset()
    for _ in range(10):
        for env in envs:
            env.step(env.action_space.sample())
    for env in envs:
        env.close()
