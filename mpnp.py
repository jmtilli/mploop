#!/usr/bin/env python3
from __future__ import print_function
from __future__ import division
import libmploop

libmploop.touch()

with libmploop.DbLock() as lck:
    with open(libmploop.npexpanded, "r") as f:
        comments = []
        for a in f.readlines():
            if a and a[-1] == '\n':
                a = a[:-1]
            comments.append(a.split("=", 1))
        for comment in comments:
            k = comment[0]
            pretty = k
            if k == 'TRACKNUMBER':
                pretty = 'Track number:'
            elif k == 'COPYRIGHT':
                pretty = 'Copyright'
            elif k == '':
                pretty = 'Comment:'
            else:
                pretty = k[0:1].upper() + k[1:].lower() + ':'
            v = comment[1]
            print(pretty + ' ' + v)
