import socket 
import struct
from collections import OrderedDict
import gym 
from gym import spaces 
import numpy as np 

class TestChamber(gym.Env):
    # Using quantized player and camera movements for now
    # TODO: Allow configurable width and height
    def __init__(self, host='', port=6555, max_steps=64):
        super().__init__()
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.connect((host, port))
        self.socket.sendall(bytearray([0, 1, 2, 3, 4])) # Just send some random data as a hello
        recv = self.socket.recv(1024)
        print('Received response: ', recv)
        self.max_steps = max_steps
        self.num_steps = 0
        self.action_space = spaces.MultiDiscrete(
            # m.x  m.y  v.x  v.y  j  d  u  z  b  o
            [ 256, 256, 256, 256, 2, 2, 2, 2, 2, 2 ]
        )
        self.observation_space = spaces.Dict(dict(
            img=spaces.Box(
                low=0, high=256, dtype=np.float64, shape=(180, 180, 3)
            ),
            vel=spaces.Box(low=np.asarray([-np.inf]*3), high=np.asarray([np.inf]*3), shape=(3,), dtype=np.float64),
            pos=spaces.Box(low=np.asarray([-np.inf]*3), high=np.asarray([np.inf]*3), shape=(3,), dtype=np.float64),
            ang=spaces.Box(low=np.asarray([-np.inf]*3), high=np.asarray([np.inf]*3), shape=(3,), dtype=np.float64),
        ))

        # Chamber specific things 
        self.dest = np.asarray([200, 775, 450])
        self.prev_dist = np.inf
        self.best_dist = np.inf
        self.prev_vel = 0 
    
    def _observe(self):
        code = self.socket.recv(1)[0]
        assert code == 4
        # We're now sure that server will send exactly 97200 bytes
        all_pixels = bytearray(97200)
        for idx in range(97200):
            all_pixels[idx] = self.socket.recv(1)[0]
        # This is BGR for now, allow for this customization too ?
        # On some distros, BGR may work better than RGB. 
        b_channel = np.asarray([all_pixels[idx] for idx in range(0, 97200, 3)]).reshape(180, 180, 1)
        g_channel = np.asarray([all_pixels[idx] for idx in range(1, 97200, 3)]).reshape(180, 180, 1)
        r_channel = np.asarray([all_pixels[idx] for idx in range(2, 97200, 3)]).reshape(180, 180, 1)
        img = np.dstack((r_channel, g_channel, b_channel)).astype(np.uint8)
        assert img.min() >= 0 and img.max() <= 255
        assert img.shape == (180, 180, 3)
        vel_pos_ang = bytearray(37) # TODO: Send more useful information in the first byte 
        for idx in range(37):
            vel_pos_ang[idx] = self.socket.recv(1)[0]
        vel_pos_ang = np.asarray(struct.unpack('fffffffff', vel_pos_ang[-36:]))
        return img.astype(np.float32), vel_pos_ang[:3], vel_pos_ang[3:6], vel_pos_ang[6:]

    def step(self, action):
        assert self.action_space.contains(action), 'Invalid action'
        action = action.astype(np.uint8)
        movement = action[0].tobytes() + action[1].tobytes()
        view = action[2].tobytes() + action[3].tobytes()
        buttons = 0
        for bit in action[4:]:
            buttons = (buttons << 1) | bit
        if isinstance(buttons, np.integer):
            buttons = buttons.astype(np.uint8).tobytes()
        else:
            buttons = buttons.to_bytes(1, 'big')
        packet = buttons + movement + view 
        assert len(packet) == 5
        self.socket.sendall(bytearray(packet))
        img, vel, pos, ang = self._observe()
        
        self.num_steps += 1 

        new_dist = np.linalg.norm(self.dest - pos)
        new_vel = np.linalg.norm(vel)
        reward = (1 if new_dist < self.best_dist else (0 if new_dist < self.prev_dist else -1)) #+ (1 if new_vel > self.prev_vel else -1)
        done = self.num_steps == self.max_steps or new_dist < 100 
        
        if new_dist < self.best_dist:
            self.best_dist = new_dist
        self.prev_dist = new_dist
        self.prev_vel = new_vel

        return dict(img=img, vel=vel, pos=pos, ang=ang), reward, done, {}

    def reset(self):
        # This is fairly expensive but can't do anything for the time being 
        # A hacky way would be to simply teleport the player back at the start 
        # but this doesn't guarantee puzzle resets so `restart_level` it is 
        # I guess Krzyhau's speedrun mod can turn down load times 
        restart = bytes([128])
        self.socket.sendall(bytearray(restart))
        img, vel, pos, ang = self._observe()
        assert img.shape == (180, 180, 3)
        self.num_steps = 0
        print('Reset complete')

        # Chamber specific things
        self.dest = np.asarray([970, 1200, 450])
        self.prev_dist = np.inf
        self.best_dist = np.inf
        self.prev_vel = 0
        return dict(img=img, vel=vel, pos=pos, ang=ang)

if __name__ == '__main__':
    env = TestChamber()
    state = env.reset()
    obs_space = env.observation_space
    print(len(obs_space.spaces), len(state))
    if not isinstance(state, dict):
        print('State is not dict')
    for k, space in obs_space.spaces.items():
        if k not in state:
            print(k, ' not in state lol')
        if not space.contains(state[k]):
            print(k, ' space is out of bounds')
            print(state[k])
            print(state[k].dtype, space.dtype, np.can_cast(state[k].dtype, space.dtype))
            print(state[k].shape, space.shape)
            print(state[k] >= np.asarray([-np.inf, -np.inf, -np.inf]))
            print(state[k] <= np.asarray([np.inf, np.inf, np.inf]))
    print(env.observation_space.contains(state))
    import time 
    from PIL import Image
    avg_time = 0
    for episode in range(10):
        start_time = time.time()
        obs = env.reset()
        #Image.fromarray(obs['img']).save(f'./episodes/episode={episode:04d},step=00.png')
        done = False 
        total_reward = 0
        step = 1
        while not done:
            ac = env.action_space.sample()
            obs, reward, done, _ = env.step(ac)
            print(reward)
            total_reward += reward 
            #Image.fromarray(obs['img']).save(f'./episodes/episode={episode:04d},step={step:02d}.png')
            step += 1
        end_time = time.time()
        episode_time = end_time - start_time
        print('Episode time (including reset) =', episode_time)
        print('Total episode reward =', total_reward)
        print(64*'-')
        avg_time += episode_time
    print('Average episode time =', avg_time / 10)

