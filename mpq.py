#!/usr/bin/env python3
import os
import fcntl
import time
import io
import sys
import subprocess
import random
import getopt
from pathlib import Path

shuffle = False

opts, args = getopt.getopt(sys.argv[1:], "s")
for o, a in opts:
    if o == '-s':
        shuffle = True
    else:
        assert False, "unhandled option"

def escape(x):
    return x.replace("\\", "\\\\").replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")

if len(args) == 0:
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

aps = []

for fl in args:
    ap = os.path.abspath(fl)
    if not Path(ap).is_file():
        sys.exit(1)
    aps.append(escape(ap))

if shuffle:
    random.shuffle(aps)

lck = os.open(os.path.expanduser('~') + '/.mploop/db.txt', os.O_RDWR | os.O_CREAT, 0o777)
with open(os.path.expanduser('~') + '/.mploop/db.txt', "a") as f:
    if aps != []:
        f.write('\n'.join(aps) + '\n')
os.close(lck)
