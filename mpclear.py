#!/usr/bin/env python3
import os
import fcntl
import time
import io
import sys
import subprocess
from pathlib import Path

if len(sys.argv) != 1:
    print("Usage: mpclear")
    sys.exit(1)

lck = os.open(os.path.expanduser('~') + '/.mploop/db.txt', os.O_RDWR | os.O_CREAT, 0o777)
fcntl.flock(lck, fcntl.LOCK_EX)
contents = []
with open(os.path.expanduser('~') + '/.mploop/db.txt', "w") as f:
    if contents != []:
        f.write('\n'.join(contents) + '\n')
os.close(lck)
