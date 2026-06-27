#!/usr/bin/env python3
# Tiny concurrent memcached client: sends a text command (accelsync/accelasync)
# request-reply over C connections for DUR seconds; prints requests/sec.
# usage: mc_bench.py PORT CMD C DUR
import socket, sys, threading, time
PORT, CMD, C, DUR = int(sys.argv[1]), sys.argv[2] + "\r\n", int(sys.argv[3]), float(sys.argv[4])
cnt = [0] * C
stop = time.time() + DUR
def worker(i):
    s = socket.create_connection(("127.0.0.1", PORT))
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    b = CMD.encode()
    while time.time() < stop:
        s.sendall(b)
        if not s.recv(4096): break
        cnt[i] += 1
    s.close()
ts = [threading.Thread(target=worker, args=(i,)) for i in range(C)]
for t in ts: t.start()
for t in ts: t.join()
print("%.2f" % (sum(cnt) / DUR))
