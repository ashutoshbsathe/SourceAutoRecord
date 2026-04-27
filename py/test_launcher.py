from game_launcher import Portal2GameInstanceManager
import time

manager = Portal2GameInstanceManager(0)
game_instance = manager.start_instance()
print("Game instance started, is_alive:", game_instance.is_alive())
time.sleep(5)
print("Stopping game instance...")
manager.stop_instance()
print("Done.")
