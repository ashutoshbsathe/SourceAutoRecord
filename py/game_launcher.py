import logging
import os
import signal
import subprocess
import time

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

