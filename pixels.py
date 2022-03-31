import socket 
import random 
import struct 
import time 
import numpy as np 
from PIL import Image

# Command line options for steam 
# `-condebug` can be theoretically ommitted
# -vulkan -sw -w 180 -h 180 -condebug +map rl_challenge_1 +plugin_load sar +sar_tas_server 1 +sar_tas_debug 1 +sar_tas_playback_rate 100 +hud_quickinfo 0 +sar_quickhud_mode 1 +sar_quickhud_size 4

host = '' #socket.gethostname()
port = 6555

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect((host, port))

len_array = random.randint(1, 25)
data = [random.randint(0, 25) for _ in range(len_array)]

s.sendall(bytearray(data))
x = s.recv(1024)
print(x)

def observe(s, fname):
    x = s.recv(1)
    print(x)
    assert x == bytes([4])
    first = s.recv(97200//3)
    print(len(first))
    second = s.recv(97200//3)
    print(len(second))
    third = s.recv(97200//3)
    print(len(third))
    all_pixels = first + second + third
    print(len(all_pixels))
    r_channel = np.asarray([all_pixels[idx] for idx in range(0, 97200, 3)]).reshape(180, 180, 1)
    g_channel = np.asarray([all_pixels[idx] for idx in range(1, 97200, 3)]).reshape(180, 180, 1)
    b_channel = np.asarray([all_pixels[idx] for idx in range(2, 97200, 3)]).reshape(180, 180, 1)
    img = np.dstack((r_channel, g_channel, b_channel)).astype(np.uint8)
    print(img.shape, img.min(), img.max())
    img = Image.fromarray(img)
    if fname:
        img.save(fname)
    x = s.recv(37)
    print(len(x), x[0], struct.unpack('fffffffff', x[-36:]))
    print(32*'-')
    return img, x[0], struct.unpack('fffffffff', x[-36:])


start_time = time.time()
for i in range(10):
    print(i)
    num_steps = random.randint(10, 20)
    restart = bytes([128])
    s.sendall(bytearray(restart))
    observe(s, f'./restart_tests/restart_{i:03d}_start.png')
    for j in range(num_steps):
        print(f'i = {i}, j = {j}')
        buttons = random.randint(0, 127).to_bytes(1, 'little')
        movement = random.randint(0, 511).to_bytes(2, 'little')
        view = random.randint(0, 511).to_bytes(2, 'little')
        data = buttons + movement + view
        s.sendall(bytearray(data))
        observe(s, f'./restart_tests/restart_{i:03d}_observation_{j:03d}.png')
    print(64*'-')
end_time = time.time()

print(end_time - start_time)
