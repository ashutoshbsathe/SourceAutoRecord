import subprocess
import os
import time
import signal
import logging

DEFAULT_STEAM_RUNTIME_SH = "~/.steam/root/ubuntu12_32/steam-runtime/run.sh"
DEFAULT_PORTAL2_SH = "~/HDD/SteamLibrary/steamapps/common/Portal 2/portal2.sh"

DEFAULT_GAMESCOPE_ARGS = [
    "-w", "640", "-h", "480",
    "-W", "640", "-H", "480",
    "-b"
]

DEFAULT_GAME_ARGS = [
    "-game", "portal2",
    "-nosteam", "-novid", "-vulkan", "-sw",
    "-nomousegrab"
    "+engine_no_focus_sleep", "0",
]

DEFAULT_BOOT_WAIT_TIME = 10

DEFAULT_INSTANCE_WIDTH = 640
DEFAULT_INSTANCE_HEIGHT = 480

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

class GameInstance:
    """
    A robust wrapper around the Portal 2 game process.
    Handles starting, stopping, restarting, and monitoring the game instance.
    """
    def __init__(
        self, 
        instance_id: int,
        gamescope_args: list[str], 
        game_args: list[str], 
        address: str,
        steam_runtime_sh: str = DEFAULT_STEAM_RUNTIME_SH,
        portal2_sh: str = DEFAULT_PORTAL2_SH
    ):
        self.instance_id = instance_id
        self.gamescope_args = gamescope_args
        self.game_args = game_args
        self.address = address
        self.steam_runtime_sh = steam_runtime_sh
        self.portal2_sh = portal2_sh
        self.process: subprocess.Popen = None
        self.log_file_path = f"portal2_instance_{self.instance_id}.log"

    def start(self):
        """Builds the command and spawns the subprocess."""
        if self.process is not None and self.is_alive():
            logger.warning(f"[Instance {self.instance_id}] Game is already running.")
            return

        # Combine gamescope and game args
        # Ensure we expand ~ to the actual home directory
        steam_runtime_sh = os.path.expanduser(self.steam_runtime_sh)
        portal2_sh = os.path.expanduser(self.portal2_sh)
        
        command = [
            "gamescope"
        ] + self.gamescope_args + [
            "--",
            steam_runtime_sh,
            portal2_sh
        ] + self.game_args

        logger.info(f"[Instance {self.instance_id}] Starting game: {' '.join(command)}")
        
        self.log_file = open(self.log_file_path, "w")
        self.process = subprocess.Popen(
            command,
            stdout=self.log_file,
            stderr=subprocess.STDOUT,
            preexec_fn=os.setsid  # Create a new session so we can kill the process group
        )
        logger.info(f"[Instance {self.instance_id}] Process started with PID: {self.process.pid}")

    def stop(self):
        """Gracefully terminates or kills the process."""
        if self.process is None:
            return
            
        logger.info(f"[Instance {self.instance_id}] Stopping process...")
        try:
            # Kill the entire process group
            os.killpg(os.getpgid(self.process.pid), signal.SIGTERM)
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            logger.warning(f"[Instance {self.instance_id}] Process did not terminate, sending SIGKILL.")
            os.killpg(os.getpgid(self.process.pid), signal.SIGKILL)
            self.process.wait()
        except Exception as e:
            logger.error(f"[Instance {self.instance_id}] Error stopping process: {e}")
            
        self.process = None
        if hasattr(self, 'log_file') and not self.log_file.closed:
            self.log_file.close()
        logger.info(f"[Instance {self.instance_id}] Process stopped.")

    def restart(self):
        """Kills and restarts the process."""
        logger.info(f"[Instance {self.instance_id}] Restarting...")
        self.stop()
        # Sleep briefly to ensure ports/shm are released
        time.sleep(2)
        self.start()

    def is_alive(self) -> bool:
        """Checks if the process is still running."""
        if self.process is None:
            return False
        
        retcode = self.process.poll()
        if retcode is not None:
            logger.error(f"[Instance {self.instance_id}] Process died unexpectedly with code {retcode}.")
            self.process = None
            return False
        return True


class Portal2GameInstanceManager:
    """
    Factory for managing a single Portal 2 game instance for a worker.
    """
    def __init__(
        self,
        num_instances: int = 1,
        base_address: str = "localhost",
        base_port: int = 50051,
        gamescope_args: list[str] = None,
        game_args: list[str] = None,
        steam_runtime_sh: str = DEFAULT_STEAM_RUNTIME_SH,
        portal2_sh: str = DEFAULT_PORTAL2_SH,
        boot_wait_time: int = DEFAULT_BOOT_WAIT_TIME,
    ):
        self.num_instances = num_instances
        self.base_address = base_address
        self.base_port = base_port
        self.gamescope_args = gamescope_args if gamescope_args is not None else DEFAULT_GAMESCOPE_ARGS.copy()
        self.game_args = game_args if game_args is not None else DEFAULT_GAME_ARGS.copy()
        self.steam_runtime_sh = steam_runtime_sh
        self.portal2_sh = portal2_sh
        self.boot_wait_time = boot_wait_time
        self.game_instances = [None for _ in range(num_instances)]

    def get_instance_address(self, instance_id: int) -> str:
        return f"{self.base_address}:{self.base_port + instance_id}" 

    def get_instance(self, instance_id: int) -> GameInstance:
        return self.game_instances[instance_id]
    
    def start_instance(self, instance_id: int) -> GameInstance:
        """Configures and starts the game instance."""
        assert 0 <= instance_id < self.num_instances, "Instance ID out of range."
        if self.game_instances[instance_id] is not None and self.game_instances[instance_id].is_alive():
            return self.game_instances[instance_id]

        instance_gamescope_args = self.gamescope_args.copy()
        
        instance_game_args = self.game_args.copy() + [
            "+sar_harness_instance", str(instance_id)
        ]        
        game_instance = GameInstance(
            instance_id=instance_id,
            gamescope_args=instance_gamescope_args,
            game_args=instance_game_args,
            address=self.get_instance_address(instance_id),
            steam_runtime_sh=self.steam_runtime_sh,
            portal2_sh=self.portal2_sh
        )
        game_instance.start()
        
        # Wait a bit for the game to actually launch before returning
        time.sleep(self.boot_wait_time)
        self.game_instances[instance_id] = game_instance
        return game_instance

    def stop_instance(self, instance_id: int):
        """Stops the game instance."""
        assert 0 <= instance_id < self.num_instances, "Instance ID out of range."
        game_instance = self.game_instances[instance_id]
        if game_instance is None:
            logger.warning(f"[Manager] No instance to stop.")
            return
        game_instance.stop()
        self.game_instances[instance_id] = None

    def restart_instance(self, instance_id: int):
        """Restarts the game instance."""
        assert 0 <= instance_id < self.num_instances, "Instance ID out of range."
        game_instance = self.game_instances[instance_id]
        if game_instance is None:
            logger.warning(f"[Manager] No instance to restart.")
            return
        game_instance.restart()
        self.game_instances[instance_id] = game_instance
