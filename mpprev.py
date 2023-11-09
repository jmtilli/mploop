#!/usr/bin/env python3
import libmploop
import sys

if len(sys.argv) <= 1:
    cnt = -1
elif len(sys.argv) == 2:
    cnt = -int(sys.argv[1])

libmploop.skip(cnt)
