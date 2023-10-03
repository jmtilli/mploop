#!/usr/bin/env python3
import os
import sys
import re
import fcntl
import time
import io
import subprocess

offset = 6.0
offset2 = 6.0

def get_mp3_gain(ln):
    mimetype=subprocess.run(["file", "-b", "--mime-type", "--", ln], capture_output=True).stdout.decode("us-ascii")
    trackgain_db = 0.0
    albumgain_db = None
    if mimetype != "" and mimetype[-1] == "\n":
        mimetype = mimetype[:-1]
    if mimetype == "audio/mpeg":
        try:
            out = subprocess.run(["mp3gain", "-s", "c", "--", ln], capture_output=True).stdout.decode("utf-8").split("\n")
        except FileNotFoundError:
            return 0.0
        for line in out[1:]:
            if re.match("^Recommended \"Track\" dB change: [-+]?[0-9]+\.[0-9]+$", line):
                numval = re.sub("^Recommended \"Track\" dB change: ", "", line)
                try:
                    trackgain_db = float(numval) + offset
                except:
                    pass
            elif re.match("^Recommended \"Album\" dB change: [-+]?[0-9]+\.[0-9]+$", line):
                numval = re.sub("^Recommended \"Album\" dB change: ", "", line)
                try:
                    albumgain_db = float(numval) + offset
                except:
                    pass
    if albumgain_db != None:
        return albumgain_db
    return trackgain_db

def get_flac_gain(ln):
    mimetype=subprocess.run(["file", "-b", "--mime-type", "--", ln], capture_output=True).stdout.decode("us-ascii")
    magic_ref = 89.0
    ref = 89.0
    trackgain_db = 0.0
    albumgain_db = None
    if mimetype != "" and mimetype[-1] == "\n":
        mimetype = mimetype[:-1]
    if mimetype == "audio/flac":
        out = subprocess.run(["metaflac", "--list", "--block-type=VORBIS_COMMENT", "--", ln], capture_output=True).stdout.decode("utf-8").split("\n")
        for out1 in out:
            if out1[:12] == "    comment[":
                cval = re.sub("^    comment\\[[0-9]+\\]: ", "", out1)
                if "=" not in cval:
                    continue
                k,v = cval.split("=", 1)
                if k == "REPLAYGAIN_REFERENCE_LOUDNESS":
                    if v[-3:] == " dB":
                        try:
                            ref = float(v[:-3])
                        except:
                            pass
                elif k == "REPLAYGAIN_ALBUM_GAIN":
                    if v[-3:] == " dB":
                        try:
                            albumgain_db = float(v[:-3]) + offset
                        except:
                            pass
                elif k == "REPLAYGAIN_TRACK_GAIN":
                    if v[-3:] == " dB":
                        try:
                            trackgain_db = float(v[:-3]) + offset
                        except:
                            pass
    if albumgain_db != None:
        return albumgain_db + (magic_ref - ref)
    return trackgain_db + (magic_ref - ref)

def get_gain(ln):
    mimetype=subprocess.run(["file", "-b", "--mime-type", "--", ln], capture_output=True).stdout.decode("us-ascii")
    trackgain_db = 0.0
    albumgain_db = None
    if mimetype != "" and mimetype[-1] == "\n":
        mimetype = mimetype[:-1]
    if mimetype == "audio/flac":
        return get_flac_gain(ln)
    elif mimetype == "audio/mpeg":
        return get_mp3_gain(ln)
    elif mimetype == "audio/ogg":
        out = subprocess.run(["vorbiscomment", "--", ln], capture_output=True).stdout.decode("utf-8").split("\n")
        for out1 in out:
            if out1 == '':
                continue
            if "=" not in out1:
                continue
            k,v = out1.split("=", 1)
            if k == "REPLAYGAIN_TRACK_GAIN":
                if v[-3:] == " dB":
                    try:
                        trackgain_db = float(v[:-3]) + offset
                    except:
                        pass
            elif k == "REPLAYGAIN_ALBUM_GAIN":
                if v[-3:] == " dB":
                    try:
                        albumgain_db = float(v[:-3]) + offset
                    except:
                        pass
    if albumgain_db != None:
        return albumgain_db
    return trackgain_db

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
    gain = get_gain(ln)
    print("GAIN", gain-offset2)
    subprocess.run(["mplayer", "-af", "volume=" + str(gain-offset2) + ":1", "--", ln])
