#!/usr/bin/env python3
import os
import re
import fcntl
import time
import io
import sys
import subprocess
import tempfile
from pathlib import Path

if len(sys.argv) != 1:
    print("Usage: vimp")
    sys.exit(1)

sio = io.StringIO()
path = tempfile.mkstemp()[1]
editor = 'vi'
if os.getenv('VISUAL', '') != '':
    editor = os.getenv('VISUAL', '')
elif os.getenv('EDITOR', '') != '':
    editor = os.getenv('EDITOR', '')

lck = os.open(os.path.expanduser('~') + '/.mploop/db.txt', os.O_RDWR | os.O_CREAT, 0o777)
with open(os.path.expanduser('~') + '/.mploop/db.txt', "r") as f:
    idx = 0
    for a in f.readlines():
        if a and a[-1] == '\n':
            a = a[:-1]
        sio.write("[" + str(idx) + "] " + a + '\n')
        idx += 1
with open(path, "w") as f:
    f.write(sio.getvalue())
subprocess.run([editor, "--", path])
contents = []
with open(path, "r") as f:
    idx = 0
    for a in f.readlines():
        if a and a[-1] == '\n':
            a = a[:-1]
        if not re.match('^\\[[0-9]+\\] ', a):
            assert False
        a = re.sub('^\\[[0-9]+\\] ', '', a)
        contents.append(a)
        idx += 1
with open(os.path.expanduser('~') + '/.mploop/db.txt', "w") as f:
    if contents != []:
        f.write('\n'.join(contents) + '\n')

os.close(lck)
os.unlink(path)
sys.exit(0)
