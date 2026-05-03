import logging
import os
import signal
import subprocess
import time

import ray

DEFAULT_STEAM_RUNTIME_SH = "~/.steam/root/ubuntu12_32/steam-runtime/run.sh"
DEFAULT_PORTAL2_SH = "~/.steam/root/steamapps/common/Portal 2/portal2.sh"

DEFAULT_GAMESCOPE_ARGS = [
    "-w", "640", "-h", "480",
    "-W", "640", "-H", "480",
    "-b",
]

DEFAULT_GAME_ARGS = [
    "-game", "portal2",
    "-nosteam", "-novid", "-vulkan", "-sw",
    "-nomousegrab",
    "+engine_no_focus_sleep", "0",
]

ENABLE_HARNESS_CMD = ["+sar_harness", "1"]

# Seconds to wait after the *last* instance launches before returning from start_all().
DEFAULT_BOOT_WAIT_TIME = 10
# Seconds between consecutive instance launches (avoids VPK file-lock contention).
DEFAULT_STAGGER_DELAY = DEFAULT_BOOT_WAIT_TIME

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


class GameInstance:
    """
    Wrapper around a single Portal 2 game process.
    Handles start, stop, restart, and liveness checks.
    Lives inside the Portal2GameInstanceManager Ray actor process.
    """

    def __init__(
        self,
        instance_id: int,
        gamescope_args: list[str],
        game_args: list[str],
        steam_runtime_sh: str = DEFAULT_STEAM_RUNTIME_SH,
        portal2_sh: str = DEFAULT_PORTAL2_SH,
    ):
        self.instance_id = instance_id
        self.gamescope_args = gamescope_args
        self.game_args = game_args
        self.steam_runtime_sh = steam_runtime_sh
        self.portal2_sh = portal2_sh
        self.process: subprocess.Popen | None = None
        self.log_file_path = f"portal2_instance_{self.instance_id}.log"

    def start(self):
        """Build the command and spawn the subprocess."""
        if self.process is not None and self.is_alive():
            logger.warning(f"[Instance {self.instance_id}] Already running.")
            return

        steam_runtime_sh = os.path.expanduser(self.steam_runtime_sh)
        portal2_sh = os.path.expanduser(self.portal2_sh)

        command = (
            ["gamescope"]
            + self.gamescope_args
            + ["--", steam_runtime_sh, portal2_sh]
            + self.game_args
        )

        logger.info(f"[Instance {self.instance_id}] Starting: {' '.join(command)}")
        self.log_file = open(self.log_file_path, "w")
        self.process = subprocess.Popen(
            command,
            stdout=self.log_file,
            stderr=subprocess.STDOUT,
            preexec_fn=os.setsid,  # new session so we can kill the whole process group
        )
        logger.info(f"[Instance {self.instance_id}] PID: {self.process.pid}")

    def stop(self):
        """Gracefully terminate (SIGTERM → SIGKILL fallback)."""
        if self.process is None:
            return
        logger.info(f"[Instance {self.instance_id}] Stopping...")
        try:
            os.killpg(os.getpgid(self.process.pid), signal.SIGTERM)
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            logger.warning(f"[Instance {self.instance_id}] SIGTERM timed out, sending SIGKILL.")
            os.killpg(os.getpgid(self.process.pid), signal.SIGKILL)
            self.process.wait()
        except Exception as e:
            logger.error(f"[Instance {self.instance_id}] Error stopping: {e}")
        finally:
            self.process = None
            if hasattr(self, "log_file") and not self.log_file.closed:
                self.log_file.close()
        logger.info(f"[Instance {self.instance_id}] Stopped.")

    def restart(self):
        """Stop and restart the process."""
        logger.info(f"[Instance {self.instance_id}] Restarting...")
        self.stop()
        time.sleep(2)  # brief pause so OS releases port / SHM
        self.start()

    def is_alive(self) -> bool:
        if self.process is None:
            return False
        retcode = self.process.poll()
        if retcode is not None:
            logger.error(f"[Instance {self.instance_id}] Died with code {retcode}.")
            self.process = None
            return False
        return True


@ray.remote
class Portal2GameInstanceManager:
    """
    Ray named actor that owns all Portal 2 game processes for a training run.

    Lifecycle:
        manager = Portal2GameInstanceManager.options(name="portal2_manager").remote(...)
        ray.get(manager.start_all.remote())
        # ... training ...
        ray.get(manager.stop_all.remote())

    Workers obtain their instance ID from worker_index (passed via env_config) and
    call manager.restart_instance.remote(instance_id) if the game crashes.
    """

    def __init__(
        self,
        num_instances: int = 1,
        gamescope_args: list[str] | None = None,
        game_args: list[str] | None = None,
        steam_runtime_sh: str = DEFAULT_STEAM_RUNTIME_SH,
        portal2_sh: str = DEFAULT_PORTAL2_SH,
        boot_wait_time: int = DEFAULT_BOOT_WAIT_TIME,
        stagger_delay: float = DEFAULT_STAGGER_DELAY,
    ):
        self.num_instances = num_instances
        self.gamescope_args = gamescope_args if gamescope_args is not None else DEFAULT_GAMESCOPE_ARGS.copy()
        self.game_args = game_args if game_args is not None else DEFAULT_GAME_ARGS.copy()
        self.steam_runtime_sh = steam_runtime_sh
        self.portal2_sh = portal2_sh
        self.boot_wait_time = boot_wait_time
        self.stagger_delay = stagger_delay
        self.game_instances: list[GameInstance | None] = [None] * num_instances

    def _build_instance(self, instance_id: int) -> GameInstance:
        """Construct a GameInstance with per-instance args (no start)."""
        game_args = self.game_args.copy() + ["+sar_harness_instance", str(instance_id)] + ENABLE_HARNESS_CMD
        return GameInstance(
            instance_id=instance_id,
            gamescope_args=self.gamescope_args.copy(),
            game_args=game_args,
            steam_runtime_sh=self.steam_runtime_sh,
            portal2_sh=self.portal2_sh,
        )

    def start_all(self):
        """
        Launch all instances with a stagger delay between each, then wait
        boot_wait_time seconds after the last launch for the games to finish
        initialising before workers start connecting.
        """
        for i in range(self.num_instances):
            instance = self._build_instance(i)
            instance.start()
            self.game_instances[i] = instance
            if i < self.num_instances - 1:
                time.sleep(self.stagger_delay)
        logger.info(
            f"[Manager] All {self.num_instances} instance(s) launched. "
            f"Waiting {self.boot_wait_time}s for boot..."
        )
        time.sleep(self.boot_wait_time)
        logger.info("[Manager] Boot wait done. Ready.")

    def stop_all(self):
        """Stop all running instances. Called at the end of training."""
        for i, instance in enumerate(self.game_instances):
            if instance is not None:
                instance.stop()
                self.game_instances[i] = None
        logger.info("[Manager] All instances stopped.")

    def restart_instance(self, instance_id: int):
        """Restart a specific instance. Called by workers on detected crashes."""
        assert 0 <= instance_id < self.num_instances, f"Invalid instance_id={instance_id}"
        instance = self.game_instances[instance_id]
        if instance is None:
            logger.warning(f"[Manager] restart_instance({instance_id}): no instance registered.")
            return
        instance.restart()
        logger.info(f"[Manager] Instance {instance_id} restarted.")

    def is_alive(self, instance_id: int) -> bool:
        """Return True if the game process for instance_id is still running."""
        instance = self.game_instances[instance_id]
        return instance is not None and instance.is_alive()
