import queue
import threading
import time
import numpy as np
from typing import Dict, Any, List

def compute_gae_numpy(
    rewards: np.ndarray,
    values: np.ndarray,
    dones: np.ndarray,
    last_value: float,
    gamma: float = 0.99,
    gae_lambda: float = 0.95,
):
    """
    Standard GAE computation in NumPy.
    All arrays should be shape (T,).
    Returns advantages and returns of shape (T,).
    """
    T = len(rewards)
    advantages = np.zeros(T, dtype=np.float32)
    last_gae = 0.0
    
    for t in reversed(range(T)):
        next_value = last_value if t == T - 1 else values[t + 1]
        next_non_terminal = 1.0 - dones[t]
        delta = rewards[t] + gamma * next_value * next_non_terminal - values[t]
        last_gae = delta + gamma * gae_lambda * next_non_terminal * last_gae
        advantages[t] = last_gae
        
    returns = advantages + values
    return advantages.astype(np.float32), returns.astype(np.float32)

class LocalBuffer:
    def __init__(self, num_steps: int):
        self.num_steps = num_steps
        self.step = 0
        self.images = []
        self.image_embeds = []
        self.positions = []
        self.actions = []
        self.rewards = np.zeros(num_steps, dtype=np.float32)
        self.dones = np.zeros(num_steps, dtype=np.float32)
        self.log_probs = np.zeros(num_steps, dtype=np.float32)
        self.values = np.zeros(num_steps, dtype=np.float32)

    def store(self, obs, action, reward, done, log_prob, value, image_embed):
        self.images.append(obs["image"])
        self.image_embeds.append(image_embed)
        self.positions.append(obs["position"])
        self.actions.append(action)
        self.rewards[self.step] = reward
        self.dones[self.step] = float(done)
        self.log_probs[self.step] = log_prob
        self.values[self.step] = value
        self.step += 1

    def is_full(self):
        return self.step >= self.num_steps

    def clear(self):
        self.step = 0
        self.images.clear()
        self.image_embeds.clear()
        self.positions.clear()
        self.actions.clear()
        self.rewards.fill(0)
        self.dones.fill(0)
        self.log_probs.fill(0)
        self.values.fill(0)

class AsyncRolloutWorker:
    def __init__(
        self,
        worker_id: int,
        env,
        inference_server,
        trajectory_queue: queue.Queue,
        num_steps: int,
        gamma: float,
        gae_lambda: float,
    ):
        self.worker_id = worker_id
        self.env = env
        self.inference_server = inference_server
        self.trajectory_queue = trajectory_queue
        self.num_steps = num_steps
        self.gamma = gamma
        self.gae_lambda = gae_lambda
        
        self.stop_event = threading.Event()
        self.worker_thread = threading.Thread(target=self._worker_loop, daemon=True)

    def start(self):
        self.worker_thread.start()

    def stop(self):
        self.stop_event.set()
        self.worker_thread.join()

    def _worker_loop(self):
        local_buffer = LocalBuffer(self.num_steps)
        obs, _ = self.env.reset()
        ep_ret = 0.0
        ep_len = 0
        
        while not self.stop_event.is_set():
            try:
                # 1. Get action from central InferenceServer
                res = self.inference_server.get_action(obs)
                actions = res["actions"]
                
                # 2. Convert actions for env
                action_dict = {
                    "move_fb": int(actions["move_fb"]),
                    "move_lr": int(actions["move_lr"]),
                    "zoom": int(actions["zoom"]),
                    "portal": int(actions["portal"]),
                    "buttons": np.array(actions["buttons"]),
                    "mouse": np.array(actions["mouse"]),
                }
                
                # 3. Step env
                next_obs, reward, terminated, truncated, _info = self.env.step(action_dict)
                done = terminated or truncated
                
                # 4. Store
                local_buffer.store(
                    obs, actions, float(reward), done, res["log_prob"], res["value"], res["image_embed"]
                )
                ep_ret += float(reward)
                ep_len += 1
                
                # 5. End of rollout or episode
                if local_buffer.is_full() or done:
                    # Bootstrap value
                    if not done:
                        final_res = self.inference_server.get_action(next_obs)
                        final_value = float(final_res["value"])
                    else:
                        final_value = 0.0
                        
                    # Calculate GAE
                    advantages, returns = compute_gae_numpy(
                        local_buffer.rewards[:local_buffer.step],
                        local_buffer.values[:local_buffer.step],
                        local_buffer.dones[:local_buffer.step],
                        final_value,
                        self.gamma,
                        self.gae_lambda
                    )
                    
                    # Pack actions into structure
                    n = local_buffer.step
                    packed_actions = {
                        k: np.stack([a[k] for a in local_buffer.actions]) 
                        for k in local_buffer.actions[0].keys()
                    }
                    
                    trajectory = {
                        "image_embeds": np.stack(local_buffer.image_embeds[:n]),
                        "positions": np.stack(local_buffer.positions[:n]),
                        "actions": packed_actions,
                        "log_probs": np.array(local_buffer.log_probs[:n]),
                        "values": np.array(local_buffer.values[:n]),
                        "rewards": np.array(local_buffer.rewards[:n]),
                        "dones": np.array(local_buffer.dones[:n]),
                        "advantages": advantages,
                        "returns": returns,
                        "ep_ret": ep_ret if done else None,
                        "ep_len": ep_len if done else None,
                    }
                    
                    self.trajectory_queue.put(trajectory)
                    local_buffer.clear()
                    
                if done:
                    obs, _ = self.env.reset()
                    ep_ret = 0.0
                    ep_len = 0
                else:
                    obs = next_obs
                    
            except Exception as e:
                print(f"[Worker {self.worker_id}] Error: {e.__class__.__name__}: {e}")
                print(f"[Worker {self.worker_id}] Restarting environment...")
                try:
                    self.env.restart_instance()
                    obs, _ = self.env.reset()
                except Exception as restart_err:
                    print(f"[Worker {self.worker_id}] Restart failed: {restart_err}")
                    time.sleep(5) # Prevent tight crash loop
                    
                local_buffer.clear()
                ep_ret = 0.0
                ep_len = 0
