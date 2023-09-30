#!/usr/bin/env python3
import os
import fcntl
import time
import io
import subprocess

def unescape(x):
    res = io.StringIO()
    escape = False
    for ch in x:
        if escape:
            if ch == 't':
                res.write('\t')
            elif ch == 'n':
                res.write('\n')
            elif ch == 'r':
                res.write('\r')
            elif ch == '\\':
                res.write('\\')
            else:
                assert False
        elif ch == '\\':
            escape = True
            continue
        else:
            res.write(ch)
    return res.getvalue()

while True:
    lck =  os.open(os.path.expanduser('~') + '/.mploop/db.txt', os.O_RDWR | os.O_CREAT, 0o777)
    fcntl.flock(lck, fcntl.LOCK_EX)
    with open(os.path.expanduser('~') + '/.mploop/db.txt', 'r') as f:
        ln = f.readline()
        if ln == '':
            os.close(lck)
            time.sleep(1)
            continue
        if ln and ln[-1] == '\n':
            ln = ln[:-1]
        ln = unescape(ln)
        rest = ''.join(f.readlines())
    with open(os.path.expanduser('~') + '/.mploop/db.txt', "w") as f:
        f.write(rest)
    os.close(lck)
    subprocess.run(["mplayer", "--", ln])
