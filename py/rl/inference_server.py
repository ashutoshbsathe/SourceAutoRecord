import queue
import threading
import time
from typing import Callable, List, Tuple, Dict, Any
import numpy as np

class InferenceServer:
    """
    A dynamic batching inference server.
    Independent worker threads push their observations to a central queue
    and block on a Future. The server thread pulls up to `max_batch_size` 
    requests, pads them to keep JAX shapes static, executes a single JAX 
    forward pass, and resolves the Futures.
    """
    def __init__(self, compute_action_fn: Callable, vision_encoder: Callable, max_batch_size: int):
        self.compute_action_fn = compute_action_fn
        self.vision_encoder = vision_encoder
        self.max_batch_size = max_batch_size
        self.request_queue = queue.Queue()
        
        # Updated atomically by the Learner thread
        self.params = None
        self.rng = None
        
        self.stop_event = threading.Event()
        self.server_thread = threading.Thread(target=self._server_loop, daemon=True)

    def start(self, initial_params: Any, initial_rng: Any):
        self.params = initial_params
        self.rng = initial_rng
        self.server_thread.start()

    def stop(self):
        self.stop_event.set()
        self.server_thread.join()

    def update_params(self, new_params: Any, new_rng: Any):
        """Thread-safe update of the latest model params from the Learner."""
        self.params = new_params
        self.rng = new_rng

    def get_action(self, obs: Dict[str, np.ndarray]) -> Dict[str, Any]:
        """Called by workers to get an action synchronously."""
        # Using a simple Event + result bucket instead of concurrent.futures 
        # to minimise threading overhead.
        event = threading.Event()
        result_bucket = {}
        self.request_queue.put((obs, event, result_bucket))
        event.wait()
        
        if "error" in result_bucket:
            raise RuntimeError(result_bucket["error"])
        return result_bucket["result"]

    def _server_loop(self):
        # Create dummy observations for padding
        # We assume the first request gives us the shape/dtype
        dummy_image = None
        dummy_pos = None

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
                first_obs = requests[0][0]
                dummy_image = np.zeros_like(first_obs["image"])
                dummy_pos = np.zeros_like(first_obs["position"])
                
            # Pad batch to max_batch_size to avoid JAX recompilation
            images = []
            positions = []
            for r in requests:
                images.append(r[0]["image"])
                positions.append(r[0]["position"])
                
            while len(images) < self.max_batch_size:
                images.append(dummy_image)
                positions.append(dummy_pos)
                
            batch_images = np.stack(images)
            batch_positions = np.stack(positions)
            
            try:
                # Capture current params and rng safely
                params = self.params
                rng = self.rng
                
                # Execute JAX forward pass
                image_embeds = self.vision_encoder(batch_images)
                
                actions, log_probs, values = self.compute_action_fn(
                    params, image_embeds, batch_positions, rng
                )
                
                # Resolve futures
                for i in range(actual_size):
                    _, event, result_bucket = requests[i]
                    
                    # Convert JAX arrays to NumPy scalars/arrays for the worker
                    worker_actions = {k: np.array(v[i]) for k, v in actions.items()}
                    worker_log_prob = np.array(log_probs[i])
                    worker_value = np.array(values[i])
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
                for r in requests:
                    _, event, result_bucket = r
                    result_bucket["error"] = str(e)
                    event.set()
