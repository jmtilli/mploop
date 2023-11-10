#!/usr/bin/env python3
from __future__ import print_function
from __future__ import division
import libmploop
import sys
import math

if len(sys.argv) != 2:
    print("Usage: " + (sys.argv and sys.argv[0] or "mpseek") + " seconds")

s = ""
seconds = float(sys.argv[1])
operations = int(math.floor(seconds/10.0))
while operations != 0:
    if operations >= 60:
        s += "\x1b[5~"
        operations -= 60
        continue
    if operations <= -60:
        s += "\x1b[6~"
        operations += 60
        continue
    if operations >= 6:
        s += "\x1b[A"
        operations -= 6
        continue
    if operations <= -6:
        s += "\x1b[B"
        operations += 6
        continue
    if operations > 0:
        s += "\x1b[C"
        operations -= 1
        continue
    if operations < 0:
        s += "\x1b[D"
        operations += 1
        continue

libmploop.send_mploop_command(s)
