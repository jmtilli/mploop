#!/usr/bin/env python3
from __future__ import print_function
from __future__ import division
import os
import fcntl
import time
import io
import sys
import subprocess
import random
import getopt
import libmploop
import libplaylist

libmploop.touch()

shuffle = False
insert = False
urlmode = False
count_to_add = None
skipplaylist = False

opts, args = getopt.getopt(sys.argv[1:], "siuPc:")
for o, a in opts:
    if o == '-s':
        shuffle = True
    elif o == '-i':
        insert = True
    elif o == '-u':
        urlmode = True
    elif o == '-P':
        skipplaylist = True
    elif o == '-c':
        count_to_add = int(a)
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
    url = False
    if urlmode:
        flit = fl
        lowfl = flit.lower()
        # TODO gopher, gophers, rist
        changed = True
        while changed:
            changed = False
            if lowfl.startswith("hls+"):
                flit = flit[4:]
                url = True
                changed = True
            if lowfl.startswith("crypto+"):
                flit = flit[7:]
                url = True
                changed = True
            if lowfl.startswith("crypto:"):
                flit = flit[7:]
                url = True
                changed = True
            if lowfl.startswith("cache:"):
                flit = flit[6:]
                url = True
                changed = True
            if lowfl.startswith("async:"):
                flit = flit[6:]
                url = True
                changed = True
            if lowfl.startswith("subfile,,"):
                flit = flit[9:]
                url = True
                changed = True
            lowfl = flit.lower()
        if lowfl.startswith("http://"):
            url = True
        elif lowfl.startswith("https://"):
            url = True
        elif lowfl.startswith("smb://"):
            url = True
        elif lowfl.startswith("amqp://"):
            url = True
        elif lowfl.startswith("async:"):
            url = True
        elif lowfl.startswith("concat:"):
            url = True
        elif lowfl.startswith("icecast://"):
            url = True
        elif lowfl.startswith("ipfs://"):
            url = True
        elif lowfl.startswith("mmst://"):
            url = True
        elif lowfl.startswith("mmsh://"):
            url = True
        elif lowfl.startswith("rtmp://"):
            url = True
        elif lowfl.startswith("rtmpe://"):
            url = True
        elif lowfl.startswith("rtmps://"):
            url = True
        elif lowfl.startswith("rtmpt://"):
            url = True
        elif lowfl.startswith("rtmpte://"):
            url = True
        elif lowfl.startswith("rtmpts://"):
            url = True
        elif lowfl.startswith("sftp://"):
            url = True
        elif lowfl.startswith("rtp://"):
            url = True
        elif lowfl.startswith("rtsp://"):
            url = True
        elif lowfl.startswith("sap://"):
            url = True
        elif lowfl.startswith("sctp://"):
            url = True
        elif lowfl.startswith("srt://"):
            url = True
        elif lowfl.startswith("srtp://"):
            url = True
        elif lowfl.startswith("tcp://"):
            url = True
        elif lowfl.startswith("tls://"):
            url = True
        elif lowfl.startswith("udp://"):
            url = True
        elif lowfl.startswith("unix://"):
            url = True
        elif lowfl.startswith("zmq:tcp://"):
            url = True
        elif lowfl.startswith("file:"):
            if not url:
                fl = flit[5:]
    if url:
        ap = fl
    else:
        ap = os.path.abspath(fl)
    if not url and not os.path.isfile(ap):
        print(fl + " is not file")
        sys.exit(1)
    if not url and not skipplaylist:
        pl = libplaylist.get_playlist(ap)
        if pl is not None:
            aps += [libmploop.escape(ap2) for ap2 in pl]
            continue
    aps.append(libmploop.escape(ap))

if shuffle:
    random.shuffle(aps)

if count_to_add is not None:
    aps = aps[0:count_to_add]

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
