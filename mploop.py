#!/usr/bin/env python3
import os
import sys
import re
import fcntl
import time
import io
import subprocess
import termios
import select
import fcntl
import errno
import libmp
from pathlib import Path

libmp.touch()

# Mandatory tools to run:
# - mplayer
# - file
# Recommended tools to run:
# - id3v2
# - id3tool
# - mp3gain
# - metaflac
# - vorbiscomment
# - opusinfo or opustags

offset = 6.0
offset2 = 6.0
mploopplayer_extraoffset = 0.0

mainlck = libmp.MainLock()

adaptive_sleep = libmp.AdaptiveSleep()

toclear = True

try:
    while True:
        if os.stat(libmp.dbexpanded).st_size == 0:
            if toclear:
                with open(libmp.npexpanded, "w") as f:
                    f.write('')
                toclear = False
            adaptive_sleep.sleep()
            continue
        with libmp.DbLock() as lck:
            with open(libmp.dbexpanded, 'r') as f:
                ln = f.readline()
                if ln == '':
                    if toclear:
                        with open(libmp.npexpanded, "w") as f:
                            f.write('')
                        toclear = False
                    adaptive_sleep.sleep()
                    continue
                adaptive_sleep.seen()
                if ln and ln[-1] == '\n':
                    ln = ln[:-1]
                rawln = ln
                ln = libmp.unescape(ln)
                rest = ''.join(f.readlines())
            with open(libmp.dbexpanded, "w") as f:
                f.write(rest)
            with open(libmp.pastexpanded, 'r') as f:
                allpast = rawln + '\n' + ''.join(f.readlines())
            with open(libmp.pastexpanded, 'w') as f:
                f.write(allpast)
        gain,comments = libmp.get_gain(ln)
        if libmp.clear_stdin():
            print("")
        print(80*"=")
        print("Applying gain:", gain-offset2)
        print("File:", ln)
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
        with open(os.path.expanduser('~') + '/.mploop/np.txt', "w") as f:
            f.write("FILE=" + rawln + '\n' + '\n'.join(c[0] + "=" + c[1] for c in comments) + '\n')
        toclear=True
        print(80*"-")
        if libmp.mploopplayer:
            subprocess.run([libmp.mploopplayer, "-g", str(gain-offset2-mploopplayer_extraoffset), "--", ln])
        else:
            subprocess.run(["mplayer", "-novideo", "-nolirc", "-msglevel", "all=0:statusline=5:cplayer=5", "-af", "volume=" + str(gain-offset2) + ":1", "--", ln])
        print("")
except KeyboardInterrupt:
    with open(libmp.npexpanded, "w") as f:
        f.write('')
    print("")
