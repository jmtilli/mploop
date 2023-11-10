#!/usr/bin/env python3
from __future__ import print_function
from __future__ import division
import os
import fcntl
import time
import io
import sys
import subprocess
import libmploop

libmploop.touch()

if len(sys.argv) < 2:
    print("Usage: mprm idx1 idx2 idx3 ...")
    sys.exit(1)

s = set([])
for idx in sys.argv[1:]:
    s.add(int(idx))

with libmploop.DbLock() as lck:
    contents = []
    with open(libmploop.dbexpanded, "r") as f:
        idx = 0
        for a in f.readlines():
            if a and a[-1] == '\n':
                a = a[:-1]
            if idx not in s:
                contents.append(a)
            idx += 1
    with open(libmploop.dbexpanded, "w") as f:
        if contents != []:
            f.write('\n'.join(contents) + '\n')
