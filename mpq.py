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

if len(sys.argv) == 1:
    lck = os.open(os.path.expanduser('~') + '/.mploop/db.txt', os.O_RDWR | os.O_CREAT, 0o777)
    with open(os.path.expanduser('~') + '/.mploop/db.txt', "r") as f:
        idx = 0
        for a in f.readlines():
            if a and a[-1] == '\n':
                a = a[:-1]
            print("[" + str(idx) + "] " + a)
            idx += 1
    os.close(lck)
    sys.exit(0)

if len(sys.argv) < 2:
    print("Usage: mpq a.ogg b.ogg c.ogg ...")
    sys.exit(1)

aps = []

for fl in sys.argv[1:]:
    ap = os.path.abspath(fl)
    if not Path(ap).is_file():
        sys.exit(1)
    aps.append(ap)

lck = os.open(os.path.expanduser('~') + '/.mploop/db.txt', os.O_RDWR | os.O_CREAT, 0o777)
with open(os.path.expanduser('~') + '/.mploop/db.txt', "a") as f:
    f.write('\n'.join(aps) + '\n')
os.close(lck)
