#!/usr/bin/env python3
import os
import fcntl
import time
import io
import sys
import subprocess
import random
import getopt
import libmp
from pathlib import Path

libmploop.touch()

shuffle = False
insert = False

opts, args = getopt.getopt(sys.argv[1:], "si")
for o, a in opts:
    if o == '-s':
        shuffle = True
    elif o == '-i':
        insert = True
    else:
        assert False, "unhandled option"

if len(args) == 0:
    with libmploop.DbLock() as lck:
        with open(libmploop.dbexpanded, "r") as f:
            idx = 0
            for a in f.readlines():
                if a and a[-1] == '\n':
                    a = a[:-1]
                print("[" + str(idx) + "] " + a)
                idx += 1
    sys.exit(0)

aps = []

for fl in args:
    ap = os.path.abspath(fl)
    if not Path(ap).is_file():
        sys.exit(1)
    aps.append(libmploop.escape(ap))

if shuffle:
    random.shuffle(aps)

with libmploop.DbLock() as lck:
    if insert:
        contents = []
        with open(libmploop.dbexpanded, "r") as f:
            idx = 0
            for a in f.readlines():
                if a and a[-1] == '\n':
                    a = a[:-1]
                contents.append(a)
                idx += 1
        contents = aps + contents
        with open(libmploop.dbexpanded, "w") as f:
            if contents != []:
                f.write('\n'.join(contents) + '\n')
    else:
        with open(libmploop.dbexpanded, "a") as f:
            if aps != []:
                f.write('\n'.join(aps) + '\n')
