#!/usr/bin/env python3
from __future__ import print_function
from __future__ import division
import sys
sys.dont_write_bytecode = True
import libmploop

if len(sys.argv) <= 1:
    cnt = 1
elif len(sys.argv) == 2:
    cnt = int(sys.argv[1])

libmploop.skip(cnt)
