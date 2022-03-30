import socket 
import random 
import struct 
import time 
import numpy as np 
from PIL import Image

host = socket.gethostname()
port = 6555

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect((host, port))

len_array = random.randint(1, 25)
data = [random.randint(0, 25) for _ in range(len_array)]

s.sendall(bytearray(data))
x = s.recv(1024)
print(x)
start_time = time.time()
for i in range(10):
    print(i)
    s.sendall(bytearray(data))
    x = s.recv(1)
    print(x)
    first = s.recv(97200//3)
    second = s.recv(97200//3)
    third = s.recv(97200//3)
    all_pixels = first + second + third 
    r_channel = np.asarray([all_pixels[idx] for idx in range(0, 97200, 3)]).reshape(180, 180, 1)
    g_channel = np.asarray([all_pixels[idx] for idx in range(1, 97200, 3)]).reshape(180, 180, 1)
    b_channel = np.asarray([all_pixels[idx] for idx in range(2, 97200, 3)]).reshape(180, 180, 1)
    img = np.dstack((r_channel, g_channel, b_channel)).astype(np.uint8)
    print(img.shape, img.min(), img.max())
    Image.fromarray(img).save(f'./saved_imgs/idx={i:03d}.png')
    x = s.recv(36)
    print(struct.unpack('fffffffff', x[-36:]))
    print(64*'-')
end_time = time.time()

print(end_time - start_time)
