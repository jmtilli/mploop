#!/usr/bin/env python3
import os
import fcntl
import libmploop

with libmploop.DbLock() as lck:
    with open(libmploop.npexpanded, 'r') as f:
        np = (f.read() != '')
    with open(libmploop.pastexpanded, 'r') as f:
        past = f.readlines()
        if np:
            toput = [past[1], past[0]]
            pastremain = past[2:]
        else:
            toput = [past[0]]
            pastremain = past[1:]
    with open(libmploop.pastexpanded, 'w') as f:
        f.write(''.join(pastremain))
    with open(libmploop.dbexpanded, 'r') as f:
        queue = f.readlines()
    with open(libmploop.dbexpanded, 'w') as f:
        f.write(''.join(toput + queue))
    if np:
        libmploop.send_mploop_command("q")
