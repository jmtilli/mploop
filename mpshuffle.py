#!/usr/bin/env python3
from __future__ import print_function
from __future__ import division
import os
import re
import fcntl
import time
import io
import sys
import subprocess
import random
import tempfile
import libmploop

libmploop.touch()

if len(sys.argv) != 1:
    print("Usage: mpshuffle")
    sys.exit(1)

contents = []
with libmploop.DbLock() as lck:
    with open(libmploop.dbexpanded, "r") as f:
        idx = 0
        for a in f.readlines():
            if a and a[-1] == '\n':
                a = a[:-1]
            contents.append(a)
            idx += 1
    random.shuffle(contents)
    with open(libmploop.dbexpanded, "w") as f:
        if contents != []:
            f.write('\n'.join(contents) + '\n')

sys.exit(0)
