import grpc
import queue
import threading
from multiprocessing import shared_memory, resource_tracker
import numpy as np

from . import harness_pb2
from . import harness_pb2_grpc


class P2Harness:
    """
    A unified Pythonic interface for controlling Portal 2 via the SAR gRPC Harness.
    Wraps all RPCs and provides a high-performance Thread+Queue based AgentLoop.
    """

    def __init__(self, address: str = "localhost:50051"):
        self.address = address
        self.channel = grpc.insecure_channel(self.address)
        self.stub = harness_pb2_grpc.Portal2HarnessStub(self.channel)
        self.shm = None
        self.shm_width = 0
        self.shm_height = 0

        # AgentLoop threading components
        self._action_queue = None
        self._response_queue = None
        self._stream_thread = None
        self._stop_event = None

    def handshake(
        self, client_version: str = "P2Harness Python", client_id: str = "p2harness"
    ) -> harness_pb2.HandshakeResponse:
        """Perform InitialHandshake and map the shared memory framebuffer if available."""
        req = harness_pb2.HandshakeRequest(
            client_version=client_version, client_id=client_id
        )
        resp = self.stub.InitialHandshake(req)

        self.shm_width = resp.shm_width
        self.shm_height = resp.shm_height

        if resp.shm_size > 0:
            try:
                # If an old shm object is lying around, close it
                if self.shm is not None:
                    self.shm.close()
                # Server tells us the SHM name; fall back to legacy name for old builds
                shm_name = (
                    resp.shm_name if resp.shm_name else "portal2_harness_framebuffer"
                )
                self.shm = shared_memory.SharedMemory(name=shm_name)
                # Unregister so the python resource tracker doesn't complain about leaks
                # TODO(absathe): kinda suspect, wonder if this should be done during cleanup
                resource_tracker.unregister(self.shm._name, "shared_memory")
            except FileNotFoundError:
                print(
                    f"Warning: Server reported SHM size, but failed to open shared memory file '{shm_name}'."
                )
                self.shm = None

        return resp

    def observe(self) -> harness_pb2.GameState:
        """Call Observe RPC to get current state."""
        return self.stub.Observe(harness_pb2.Empty())

    def act(self, action: harness_pb2.ActionRequest) -> harness_pb2.ActionResponse:
        """Call Act RPC to send a synchronous action."""
        return self.stub.Act(action)

    def execute_command(self, command: str, timeout: float | None = None) -> harness_pb2.CommandResponse:
        """Call ExecuteCommand RPC to run a server console command."""
        return self.stub.ExecuteCommand(
            harness_pb2.CommandRequest(command=command), timeout=timeout
        )

    def render_demo(
        self,
        demo_path: str,
        output_path: str = "",
        capture_pixels: bool = True,
        timeout: float | None = None,
    ) -> harness_pb2.RenderDemoResponse:
        """Call RenderDemo RPC to convert a demo file to a rollout end-to-end synchronously."""
        req = harness_pb2.RenderDemoRequest(
            demo_path=demo_path,
            output_path=output_path,
            capture_pixels=capture_pixels,
        )
        return self.stub.RenderDemo(req, timeout=timeout)

    def reset(self, map_name: str = "") -> harness_pb2.ResetResponse:
        """Call Reset RPC. Restart level or change map."""
        # Stop existing stream if any, as server will drop it on reset anyway
        self.stop_agent_loop()
        req = harness_pb2.ResetRequest(map_name=map_name)
        return self.stub.Reset(req)

    def _private_action_generator(self):
        """Internal generator required by gRPC stream to consume our Action Queue."""
        while True:
            stop = self._stop_event
            if stop is None or stop.is_set():
                break
            try:
                # Use a timeout to periodically check the stop event
                action_msg = self._action_queue.get(timeout=0.1)
                yield action_msg
            except queue.Empty:
                continue

    def _stream_worker(self):
        """Background thread that manages the AgentLoop bidirectional stream."""
        try:
            responses = self.stub.AgentLoop(self._private_action_generator())
            for resp in responses:
                if self._stop_event.is_set():
                    responses.cancel()
                    break
                self._response_queue.put(resp)
        except grpc.RpcError as e:
            if e.code() != grpc.StatusCode.CANCELLED:
                print(f"AgentLoop RPC stream dropped: {e.code()} {e.details()}")
            self._response_queue.put(e)

    def start_agent_loop(self):
        """Start the background stream thread."""
        self.stop_agent_loop()

        self._stop_event = threading.Event()
        self._action_queue = queue.Queue(maxsize=1)
        self._response_queue = queue.Queue(maxsize=1)

        self._stream_thread = threading.Thread(target=self._stream_worker, daemon=True)
        self._stream_thread.start()

    def stop_agent_loop(self):
        """Stop the background stream thread if running."""
        if self._stop_event is not None:
            self._stop_event.set()
        if self._stream_thread is not None:
            self._stream_thread.join(timeout=1.0)
            self._stream_thread = None
        self._stop_event = None

    def step_agent_loop(
        self, action_msg: harness_pb2.AgentMessage, timeout: float = 5.0
    ) -> harness_pb2.EnvironmentMessage:
        """
        Push an action to the stream and block for the corresponding environment response.
        If copy_pixels_to_shm is True, the response implies pixels have been updated in SHM.
        """
        if self._stream_thread is None or not self._stream_thread.is_alive():
            raise RuntimeError(
                "AgentLoop stream is not running. Call start_agent_loop() first."
            )

        # We must ensure the queue is empty before sending, just in case
        while not self._response_queue.empty():
            self._response_queue.get_nowait()

        self._action_queue.put(action_msg)

        try:
            resp = self._response_queue.get(timeout=timeout)
            if isinstance(resp, Exception):
                raise resp
            return resp
        except queue.Empty:
            raise TimeoutError("Timed out waiting for response from AgentLoop stream.")

    def get_shm_pixels(self) -> np.ndarray:
        """Read the RGB pixels from shared memory."""
        if self.shm is None or self.shm_width == 0 or self.shm_height == 0:
            raise RuntimeError(
                "Shared memory not initialized. Did you call handshake() and did the server report SHM size?"
            )
        frame = np.ndarray(
            (self.shm_height, self.shm_width, 3), dtype=np.uint8, buffer=self.shm.buf
        )
        # We make a copy to prevent the numpy array holding onto the shm buffer directly
        # which can be overwritten by the game thread next tick.
        return frame.copy()

    def close(self):
        self.stop_agent_loop()
        if self.shm is not None:
            self.shm.close()
            self.shm = None
        self.channel.close()

    def __del__(self):
        self.close()
