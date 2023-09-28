#!/usr/bin/env python3
import os
import fcntl
import time
import io
import sys
import subprocess
from pathlib import Path

def escape(x):
    return x.replace("\\", "\\\\").replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")

if len(sys.argv) < 2:
    print("Usage: mprm idx1 idx2 idx3 ...")
    sys.exit(1)

s = set([])
for idx in sys.argv[1:]:
    s.add(int(idx))

lck = os.open(os.path.expanduser('~') + '/.mploop/db.txt', os.O_RDWR | os.O_CREAT, 0o777)
contents = []
with open(os.path.expanduser('~') + '/.mploop/db.txt', "r") as f:
    idx = 0
    for a in f.readlines():
        if a and a[-1] == '\n':
            a = a[:-1]
        if idx not in s:
            contents.append(a)
        idx += 1
with open(os.path.expanduser('~') + '/.mploop/db.txt', "w") as f:
    if contents != []:
        f.write('\n'.join(contents) + '\n')
os.close(lck)
