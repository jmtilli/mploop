#!/usr/bin/env python3
import os
import re
import fcntl
import time
import io
import sys
import subprocess
import random
import tempfile
from pathlib import Path

os.makedirs(os.path.expanduser('~') + '/.mploop', exist_ok = True)
Path(os.path.expanduser('~') + '/.mploop/db.txt').touch()

if len(sys.argv) != 1:
    print("Usage: mpshuffle")
    sys.exit(1)

sio = io.StringIO()
path = tempfile.mkstemp()[1]
editor = 'vi'
if os.getenv('VISUAL', '') != '':
    editor = os.getenv('VISUAL', '')
elif os.getenv('EDITOR', '') != '':
    editor = os.getenv('EDITOR', '')

contents = []
lck = os.open(os.path.expanduser('~') + '/.mploop/db.txt', os.O_RDWR | os.O_CREAT, 0o777)
fcntl.flock(lck, fcntl.LOCK_EX)
with open(os.path.expanduser('~') + '/.mploop/db.txt', "r") as f:
    idx = 0
    for a in f.readlines():
        if a and a[-1] == '\n':
            a = a[:-1]
        contents.append(a)
        idx += 1
random.shuffle(contents)
with open(os.path.expanduser('~') + '/.mploop/db.txt', "w") as f:
    if contents != []:
        f.write('\n'.join(contents) + '\n')

os.close(lck)
os.unlink(path)
sys.exit(0)
