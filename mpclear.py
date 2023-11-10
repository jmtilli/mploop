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

if len(sys.argv) != 1:
    print("Usage: mpclear")
    sys.exit(1)

with libmploop.DbLock() as lck:
    contents = []
    with open(libmploop.dbexpanded, "w") as f:
        if contents != []:
            f.write('\n'.join(contents) + '\n')
