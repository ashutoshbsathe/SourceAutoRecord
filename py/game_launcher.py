import logging
import os
import signal
import subprocess
import time

DEFAULT_STEAM_RUNTIME_SH = "~/.steam/root/ubuntu12_32/steam-runtime/run.sh"
DEFAULT_PORTAL2_SH = "~/.steam/root/steamapps/common/Portal 2/portal2.sh"

DEFAULT_GAMESCOPE_ARGS = [
    "-w",
    "640",
    "-h",
    "480",
    "-W",
    "640",
    "-H",
    "480",
    "-b",
]

DEFAULT_GAME_ARGS = [
    "-game",
    "portal2",
    "-nosteam",
    "-novid",
    "-vulkan",
    "-windowed",
    "-low",
    "-nomousegrab",
    "+engine_no_focus_sleep",
    "0",
]

ENABLE_HARNESS_CMD = ["+sar_harness", "1"]

def get_instance_specific_args(instance_id: int) -> list[str]:
    return [
        "+tv_port", str(47000 + instance_id),
        "+hostport", str(48000 + instance_id),
        "+clientport", str(49000 + instance_id),
        "+sar_harness_instance", str(instance_id),
    ] + ENABLE_HARNESS_CMD

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
        debug: bool = False,
    ):
        self.instance_id = instance_id
        self.gamescope_args = gamescope_args
        self.game_args = game_args
        self.steam_runtime_sh = steam_runtime_sh
        self.portal2_sh = portal2_sh
        self.debug = debug
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
            (
                ["gamescope"]
                + self.gamescope_args
                + ["--", steam_runtime_sh, portal2_sh]
                + self.game_args
            )
            if self.gamescope_args
            else [steam_runtime_sh, portal2_sh] + self.game_args
        )
        if self.debug:
            strace_log = f"strace_instance_{self.instance_id}.log"
            command = [
                "strace",
                "-f",              # follow forks (run.sh → portal2_linux)
                "-tt",             # microsecond timestamps
                "-o", strace_log,  # write to file (keeps game stdout clean)
                "-s", "256",       # longer string captures
                "--",
                *command,
            ]

        logger.info(f"[Instance {self.instance_id}] Starting: {' '.join(command)}")
        self.log_file = open(self.log_file_path, "w")
        self.process = subprocess.Popen(
            command,
            stdout=self.log_file,
            stderr=subprocess.STDOUT,
            start_new_session=True,  # same as preexec_fn=os.setsid but uses posix_spawn (thread-safe with JAX)
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
            logger.warning(
                f"[Instance {self.instance_id}] SIGTERM timed out, sending SIGKILL."
            )
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
