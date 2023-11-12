#!/usr/bin/env python3
from __future__ import print_function
from __future__ import division
import os
import sys
import time
import socket
import subprocess
import termios
import select
import libmploop
import errno

libmploop.touch()

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

mainlck = libmploop.MainLock()

adaptive_sleep = libmploop.AdaptiveSleep()

toclear = True

try:
    while True:
        if os.stat(libmploop.dbexpanded).st_size == 0:
            if toclear:
                with open(libmploop.npexpanded, "w") as f:
                    f.write('')
                toclear = False
            adaptive_sleep.sleep()
            continue
        with libmploop.DbLock() as lck:
            with open(libmploop.dbexpanded, 'r') as f:
                ln = f.readline()
                if ln == '':
                    if toclear:
                        with open(libmploop.npexpanded, "w") as f:
                            f.write('')
                        toclear = False
                    adaptive_sleep.sleep()
                    continue
                adaptive_sleep.seen()
                if ln and ln[-1] == '\n':
                    ln = ln[:-1]
                rawln = ln
                ln = libmploop.unescape(ln)
                rest = ''.join(f.readlines())
            with open(libmploop.dbexpanded, "w") as f:
                f.write(rest)
            with open(libmploop.pastexpanded, 'r') as f:
                allpast = rawln + '\n' + ''.join(f.readlines())
            with open(libmploop.pastexpanded, 'w') as f:
                f.write(allpast)
        gain,comments = libmploop.get_gain(ln)
        if libmploop.clear_stdin():
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
            if hasattr(rawln, "decode"): # Python 2
                f.write(("FILE=" + rawln.decode("utf-8") + '\n' + '\n'.join(c[0] + "=" + c[1] for c in comments) + '\n').encode("utf-8"))
            else: # Python 3
                f.write("FILE=" + rawln + '\n' + '\n'.join(c[0] + "=" + c[1] for c in comments) + '\n')
        toclear=True
        print(80*"-")
        if libmploop.mploopplayer:
            proc = subprocess.Popen([libmploop.mploopplayer, "-s", libmploop.sockexpanded, "-g", str(gain-offset2-mploopplayer_extraoffset), "--", ln])
            with open(libmploop.mploopplayerpidexpanded, 'w') as f:
                f.write(str(proc.pid) + '\n')
            proc.wait()
        else:
            proc = subprocess.Popen(["mplayer", "-novideo", "-nolirc", "-msglevel", "all=0:statusline=5:cplayer=5", "-af", "volume=" + str(gain-offset2) + ":1", "--", ln], stdin=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
            with open(libmploop.mplayerpidexpanded, 'w') as f:
                f.write(str(proc.pid) + '\n')
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                os.unlink(libmploop.mplayersockexpanded)
            except OSError as e:
                if e.errno != errno.ENOENT:
                    raise
                pass
            sock.bind(libmploop.mplayersockexpanded)
            sock.listen(16)
            fds = set([])
            p = select.poll()
            p.register(sys.stdin, select.POLLIN)
            p.register(proc.stderr, select.POLLIN)
            p.register(sock, select.POLLIN)
            eof = False
            old_settings = termios.tcgetattr(sys.stdin)
            new_settings = termios.tcgetattr(sys.stdin)
            new_settings[3] = new_settings[3] & ~termios.ICANON & ~termios.ECHO
            termios.tcsetattr(sys.stdin, termios.TCSANOW, new_settings)
            socks = {}
            try:
                while not eof:
                    for fd, revent in p.poll():
                        if fd == sock.fileno() and (revent & select.POLLIN):
                            newfd, cliaddr = sock.accept()
                            fds.add(newfd.fileno())
                            p.register(newfd.fileno(), select.POLLIN)
                            socks[newfd.fileno()] = newfd
                        elif fd == sys.stdin.fileno() and (revent & select.POLLIN):
                            buf = os.read(sys.stdin.fileno(), 4096)
                            proc.stdin.write(buf)
                            proc.stdin.flush()
                        elif fd == proc.stderr.fileno() and (revent & (select.POLLIN | select.POLLHUP)):
                            buf = os.read(proc.stderr.fileno(), 4096)
                            if buf == b'':
                                eof = True
                                continue
                            sys.stderr.write(buf)
                            sys.stderr.flush()
                        elif fd != sock.fileno() and fd != sys.stdin.fileno() and fd != proc.stderr.fileno() and (revent & (select.POLLIN | select.POLLHUP)):
                            buf = os.read(fd, 4096)
                            if buf == b'':
                                p.unregister(fd)
                                socks[fd].close()
                                fds.remove(fd)
                                continue
                            proc.stdin.write(buf)
                            proc.stdin.flush()
            finally:
                termios.tcsetattr(sys.stdin, termios.TCSANOW, old_settings)
            for newfd in fds:
                socks[newfd].close()
            sock.close()
            proc.wait()
        print("")
except KeyboardInterrupt:
    with open(libmploop.npexpanded, "w") as f:
        f.write('')
    print("")
