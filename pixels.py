import socket 
import random 
import struct 
import time 

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
    x = s.recv(97200//3)
    print(len(x))
    x = s.recv(97200//3)
    print(len(x))
    x = s.recv(97200//3)
    print(len(x))
    x = s.recv(36)
    print(struct.unpack('fffffffff', x[-36:]))
    print(64*'-')
end_time = time.time()

print(end_time - start_time)
