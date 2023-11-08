#!/usr/bin/env python3
import subprocess
import os
import fcntl

expanded = os.path.expanduser('~') + '/.mploop/db.txt'
pastexpanded = os.path.expanduser('~') + '/.mploop/past.txt'
npexpanded = os.path.expanduser('~') + '/.mploop/np.txt'

lck = os.open(expanded, os.O_RDWR | os.O_CREAT, 0o777)
fcntl.flock(lck, fcntl.LOCK_EX)

with open(npexpanded, 'r') as f:
    np = (f.read() != '')
with open(pastexpanded, 'r') as f:
    past = f.readlines()
    if np:
        toput = [past[1], past[0]]
        pastremain = past[2:]
    else:
        toput = [past[0]]
        pastremain = past[1:]
with open(pastexpanded, 'w') as f:
    f.write(''.join(pastremain))
with open(expanded, 'r') as f:
    queue = f.readlines()
with open(expanded, 'w') as f:
    f.write(''.join(toput + queue))

if np:
    subprocess.run(["sh", "-c", "echo|socat - \"unix:$HOME/.mploop/sock\""])
