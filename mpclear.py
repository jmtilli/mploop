#!/usr/bin/env python3
import os
import fcntl
import time
import io
import sys
import subprocess
import libmp
from pathlib import Path

libmp.touch()

if len(sys.argv) != 1:
    print("Usage: mpclear")
    sys.exit(1)

with libmp.DbLock() as lck:
    contents = []
    with open(libmp.dbexpanded, "w") as f:
        if contents != []:
            f.write('\n'.join(contents) + '\n')
