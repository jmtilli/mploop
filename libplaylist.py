from __future__ import print_function
from __future__ import division
import os
import re
import sys
import subprocess

MAX_LINE = 4097

def get_pls_playlist(x):
    tag_seen = False
    entries = {}
    number_of_entries = None
    with open(x, "r") as f:
        while True:
            l = f.readline(MAX_LINE)
            if len(l) == 0:
                res = []
                if number_of_entries < 1:
                    return None
                for k in range(1, number_of_entries+1):
                    if k not in entries:
                        return None
                    res.append(entries[k])
                return res
            if len(l) == MAX_LINE:
                return None
            if l[0] == ';':
                continue
            if re.match("^[ \t]*;", l):
                continue
            if l[-1] == '\n':
                l = l[:-1]
            if re.match("^[ \t]*$", l):
                continue
            if l == '[playlist]':
                tag_seen = True
                continue
            if not tag_seen:
                return None
            if l[0] == '[':
                return None
            if l[:4] == "File" and "=" in l:
                key,val = l.split("=", 1)
                try:
                    idx = int(key[4:])
                    entries[idx] = val
                except:
                    return None
            if l[:16] == "NumberOfEntries=":
                try:
                    number_of_entries = int(l[16:])
                except:
                    return None


def get_m3u_playlist(x):
    first_line = True
    is_ext = False
    res = []
    with open(x, "r") as f:
        while True:
            l = f.readline(MAX_LINE)
            if len(l) == 0:
                if res:
                    return res
                return None
            if len(l) == MAX_LINE:
                return None
            if first_line:
                first_line = False
                if l == "#EXTM3U\n":
                    is_ext = True
                    continue
            if l[0] == '#':
                continue
            if l[-1] == '\n':
                l = l[:-1]
            lowfl = l.lower()
            url = False
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
            if not url:
                l = os.path.join(os.path.dirname(x), l)
            if not url and not os.access(l, os.R_OK):
                if not is_ext:
                    return None
            if not url and not os.path.isfile(l) and not os.path.isdir(l):
                if not is_ext:
                    return None
            elif url or os.path.isfile(l):
                res.append(l)
            elif os.path.isdir(l):
                for key in os.listdir(l):
                    key = os.path.join(l, key)
                    if not os.path.isfile(key):
                        continue
                    if not os.access(key, os.R_OK):
                        continue
                    proc = subprocess.Popen(["file", "-b", "--mime-type", "--", key], stdout=subprocess.PIPE)
                    out,err = proc.communicate()
                    proc.wait()
                    mimetype = out.decode("us-ascii")
                    if mimetype != "" and mimetype[-1] == '\n':
                        mimetype = mimetype[:-1]
                    if mimetype[:6] == 'audio/' or mimetype[:6] == 'video/':
                        res.append(key)

def get_playlist(x):
    y = get_m3u_playlist(x)
    if y is not None:
        return y
    y = get_pls_playlist(x)
    if y is not None:
        return y
    return None
