import queue
import threading
import time
from typing import Callable, List, Tuple, Dict, Any
import numpy as np
import jax


class InferenceServer:
    """
    A dynamic batching inference server with persistent Sequence Caching.
    Independent worker threads push their observations to a central queue
    and block on a Future. The server thread maintains a rolling window
    of observations for each worker, runs the ViT on the new frames,
    and evaluates the Transformer ActorCritic.
    """

    def __init__(
        self,
        compute_action_fn: Callable,
        vision_encoder: Callable,
        max_batch_size: int,
        max_seq_len: int = 128,
    ):
        self.compute_action_fn = compute_action_fn
        self.vision_encoder = vision_encoder
        self.max_batch_size = max_batch_size
        self.max_seq_len = max_seq_len
        self.request_queue = queue.Queue()

        # Persistent Cache (KV-cache equivalent for observation sequences)
        self.cache_images = None
        self.cache_kinematics = None
        self.cache_lens = None

        # Updated atomically by the Learner thread
        self.params = None
        self.rng = None

        self.stop_event = threading.Event()
        self.server_thread = threading.Thread(target=self._server_loop, daemon=True)

    def start(self, initial_params: Any, initial_rng: Any, embed_dim: int = 768):
        self.params = initial_params
        self.rng = initial_rng
        self.cache_images = np.zeros(
            (self.max_batch_size, self.max_seq_len, embed_dim), dtype=np.float32
        )
        self.cache_kinematics = np.zeros(
            (self.max_batch_size, self.max_seq_len, 6), dtype=np.float32
        )
        self.cache_lens = np.zeros(self.max_batch_size, dtype=np.int32)
        self.server_thread.start()

    def stop(self):
        self.stop_event.set()
        self.server_thread.join()

    def update_params(self, new_params: Any, new_rng: Any):
        """Thread-safe update of the latest model params from the Learner."""
        self.params = new_params
        self.rng = new_rng

    def get_action(
        self, worker_id: int, obs: Dict[str, np.ndarray], is_first: bool = False
    ) -> Dict[str, Any]:
        """Called by workers to get an action synchronously."""
        event = threading.Event()
        result_bucket = {}
        self.request_queue.put((worker_id, obs, is_first, event, result_bucket))
        event.wait()

        if "error" in result_bucket:
            raise RuntimeError(result_bucket["error"])
        return result_bucket["result"]

    def _server_loop(self):
        try:
            # Create dummy observations for padding
            # We assume the first request gives us the shape/dtype
            dummy_image = None
            dummy_kinematics = None

            while not self.stop_event.is_set():
                try:
                    # Block until at least one request arrives
                    req = self.request_queue.get(timeout=0.1)
                except queue.Empty:
                    continue

                requests = [req]

                # Drain up to max_batch_size
                while len(requests) < self.max_batch_size:
                    try:
                        requests.append(self.request_queue.get_nowait())
                    except queue.Empty:
                        break

                actual_size = len(requests)

                if dummy_image is None:
                    first_obs = requests[0][1]
                    dummy_image = np.zeros_like(first_obs["image"])
                    dummy_kinematics = np.zeros_like(first_obs["kinematics"])

                # We must batch images for the vision_encoder
                images = []
                for r in requests:
                    images.append(r[1]["image"])
                while len(images) < self.max_batch_size:
                    images.append(dummy_image)
                batch_images = np.stack(images)

                try:
                    # 1. Vision Encoder on current frames
                    image_embeds = self.vision_encoder(batch_images)

                    # 2. Update persistent cache for actual requests
                    for i in range(actual_size):
                        worker_id, obs, is_first, _, _ = requests[i]
                        if is_first:
                            self.cache_lens[worker_id] = 0
                            self.cache_images[worker_id].fill(0)
                            self.cache_kinematics[worker_id].fill(0)

                        curr_len = self.cache_lens[worker_id]
                        if curr_len < self.max_seq_len:
                            self.cache_images[worker_id, curr_len] = image_embeds[i]
                            self.cache_kinematics[worker_id, curr_len] = obs["kinematics"]
                            self.cache_lens[worker_id] += 1
                        else:
                            # Shift left (sliding window)
                            self.cache_images[worker_id, :-1] = self.cache_images[
                                worker_id, 1:
                            ]
                            self.cache_images[worker_id, -1] = image_embeds[i]
                            self.cache_kinematics[worker_id, :-1] = self.cache_kinematics[
                                worker_id, 1:
                            ]
                            self.cache_kinematics[worker_id, -1] = obs["kinematics"]

                    # 3. Create JAX batch from persistent cache
                    # We always run the full batch of max_batch_size environments
                    # to avoid JAX recompilation. The cache holds sequences of max_seq_len.
                    params = self.params
                    rng, step_rng = jax.random.split(self.rng)
                    self.rng = rng

                    actions, log_probs, values = self.compute_action_fn(
                        params, self.cache_images, self.cache_kinematics, step_rng
                    )

                    # 4. Resolve futures
                    for i in range(actual_size):
                        worker_id, _, _, event, result_bucket = requests[i]
                        # The worker's current valid step is at cache_lens[worker_id] - 1
                        idx = self.cache_lens[worker_id] - 1

                        # Convert JAX arrays to NumPy scalars/arrays for the worker
                        # Extract the outputs only at the current sequence index
                        worker_actions = {
                            k: np.array(v[worker_id, idx]) for k, v in actions.items()
                        }
                        worker_log_prob = np.array(log_probs[worker_id, idx])
                        worker_value = np.array(values[worker_id, idx])
                        worker_embed = np.array(image_embeds[i])

                        result_bucket["result"] = {
                            "actions": worker_actions,
                            "log_prob": worker_log_prob,
                            "value": worker_value,
                            "image_embed": worker_embed,
                        }
                        event.set()

                except Exception as e:
                    # If inference fails, propagate error to workers
                    import traceback

                    traceback.print_exc()
                    for r in requests:
                        _, _, _, event, result_bucket = r
                        result_bucket["error"] = str(e)
                        event.set()

        except Exception as e:
            print(f"[InferenceServer] FATAL THREAD CRASH: {e}")
            import traceback

            traceback.print_exc()
