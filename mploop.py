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

# Mandatory tools to run:
# - mplayer
# - file
# Recommended tools to run:
# - id3v2
# - id3tool
# - mp3gain
# - metaflac
# - vorbiscomment

def clear_stdin():
    old_settings = termios.tcgetattr(sys.stdin)
    new_settings = termios.tcgetattr(sys.stdin)
    new_settings[3] = new_settings[3] & ~termios.ICANON & ~termios.ECHO
    termios.tcsetattr(sys.stdin, termios.TCSANOW, new_settings)
    old_flags = fcntl.fcntl(sys.stdin, fcntl.F_GETFL)
    fcntl.fcntl(sys.stdin, fcntl.F_SETFL, old_flags | os.O_NONBLOCK)
    y = select.poll()
    y.register(sys.stdin, select.POLLIN)
    seen_input = False
    while True:
        if y.poll(1):
            try:
                while True:
                    s = sys.stdin.read(4096)
                    if s == '':
                        termios.tcsetattr(sys.stdin, termios.TCSANOW, old_settings)
                        fcntl.fcntl(sys.stdin, fcntl.F_SETFL, old_flags)
                        break
                    else:
                        seen_input = True
            except OSError as e:
                if e.args[0] == errno.EAGAIN or e.args[0] == errno.EWOULDBLOCK:
                    # Just in case stdin and stdout are both the same fd
                    termios.tcsetattr(sys.stdin, termios.TCSANOW, old_settings)
                    fcntl.fcntl(sys.stdin, fcntl.F_SETFL, old_flags)
                    break
                else:
                    raise e
        else:
            break
    termios.tcsetattr(sys.stdin, termios.TCSANOW, old_settings)
    fcntl.fcntl(sys.stdin, fcntl.F_SETFL, old_flags)
    return seen_input

offset = 6.0
offset2 = 6.0

def get_mp3_gain(ln):
    mimetype=subprocess.run(["file", "-b", "--mime-type", "--", ln], capture_output=True).stdout.decode("us-ascii")
    trackgain_db = 0.0
    albumgain_db = None
    comments = []
    if mimetype != "" and mimetype[-1] == "\n":
        mimetype = mimetype[:-1]
    if mimetype == "audio/mpeg":
        try:
            ln2 = ln
            if ln2 and ln2[0] == '-':
                ln2 = './' + ln2
            newlinecnt = ln2.count('\n')
            out = subprocess.run(["id3v2", "-l", ln2], capture_output=True).stdout.decode("utf-8").split("\n")
            firstprocess = False
            newlineseen = 0
            secondprocess = False
            for line in out[1:]:
                if firstprocess and newlineseen < newlinecnt:
                    newlineseen += 1
                    if newlineseen < newlinecnt:
                        continue
                elif not firstprocess:
                    rem = re.match("^id3v2 tag info for (.*)$", line)
                    if rem:
                        firstprocess = True
                if firstprocess and newlineseen == newlinecnt:
                    rem = re.match("^(.*):$", line)
                    if rem:
                        secondprocess = True
                if not secondprocess:
                    continue
                rem = re.match("^COMM \\([^:]*\\): (.*)$", line)
                if rem:
                    comments.append(("COMMENT", rem.group(1)))
                rem = re.match("^TXXX \\([^:]*\\): (.*)$", line)
                if rem:
                    comments.append(("DESCRIPTION", rem.group(1)))
                rem = re.match("^TCOM \\([^:]*\\): (.*)$", line)
                if rem:
                    comments.append(("COMPOSER", rem.group(1)))
                rem = re.match("^TCOP \\([^:]*\\): (.*)$", line)
                if rem:
                    comments.append(("COPYRIGHT", rem.group(1)))
                rem = re.match("^WCOP \\([^:]*\\): (.*)$", line)
                if rem:
                    comments.append(("COPYRIGHT", rem.group(1)))
                rem = re.match("^TENC \\([^:]*\\): (.*)$", line)
                if rem:
                    comments.append(("ENCODED-BY", rem.group(1)))
                rem = re.match("^TEXT \\([^:]*\\): (.*)$", line)
                if rem:
                    comments.append(("LYRICIST", rem.group(1)))
                rem = re.match("^TIT1 \\([^:]*\\): (.*)$", line)
                if rem:
                    comments.append(("TITLE", rem.group(1)))
                rem = re.match("^TIT2 \\([^:]*\\): (.*)$", line)
                if rem:
                    comments.append(("TITLE", rem.group(1)))
                rem = re.match("^TIT3 \\([^:]*\\): (.*)$", line)
                if rem:
                    comments.append(("SUBTITLE", rem.group(1)))
                rem = re.match("^TPE1 \\([^:]*\\): (.*)$", line)
                if rem:
                    comments.append(("ARTIST", rem.group(1)))
                rem = re.match("^TPE2 \\([^:]*\\): (.*)$", line)
                if rem:
                    comments.append(("ARTIST", rem.group(1)))
                rem = re.match("^TPE3 \\([^:]*\\): (.*)$", line)
                if rem:
                    comments.append(("CONDUCTOR", rem.group(1)))
                rem = re.match("^TPE4 \\([^:]*\\): (.*)$", line)
                if rem:
                    comments.append(("REMIXER", rem.group(1)))
                rem = re.match("^TPUB \\([^:]*\\): (.*)$", line)
                if rem:
                    comments.append(("PUBLISHER", rem.group(1)))
                rem = re.match("^TSRC \\([^:]*\\): (.*)$", line)
                if rem:
                    comments.append(("ISRC", rem.group(1)))
                rem = re.match("^TALB \\([^:]*\\): (.*)$", line)
                if rem:
                    comments.append(("ALBUM", rem.group(1)))
                rem = re.match("^TRCK \\([^:]*\\): (.*)$", line)
                if rem:
                    comments.append(("TRACKNUMBER", rem.group(1)))
                rem = re.match("^TDAT \\([^:]*\\): (.*)$", line)
                if rem:
                    comments.append(("DATE", rem.group(1)))
                rem = re.match("^TYER \\([^:]*\\): (.*)$", line)
                if rem:
                    comments.append(("YEAR", rem.group(1)))
                rem = re.match("^TCON \\([^:]*\\): (.*)$", line)
                if rem:
                    comments.append(("GENRE", rem.group(1)))
        except FileNotFoundError:
            pass
        if comments == []:
            try:
                out = subprocess.run(["id3tool", "--", ln], capture_output=True).stdout.decode("utf-8").split("\n")
                for line in out[1:]:
                    rem = re.match("^Song Title:\t(.*)$", line)
                    if rem:
                        comments.append(("TITLE", rem.group(1)))
                    rem = re.match("^Artist:\t\t(.*)$", line)
                    if rem:
                        comments.append(("ARTIST", rem.group(1)))
                    rem = re.match("^Album:\t\t(.*)$", line)
                    if rem:
                        comments.append(("ALBUM", rem.group(1)))
                    rem = re.match("^Track:\t\t(.*)$", line)
                    if rem:
                        comments.append(("TRACKNUMBER", rem.group(1)))
                    rem = re.match("^Year:\t\t(.*)$", line)
                    if rem:
                        comments.append(("DATE", rem.group(1)))
                    rem = re.match("^Genre:\t\t(.*)$", line)
                    if rem:
                        comments.append(("GENRE", rem.group(1)))
            except FileNotFoundError:
                pass
        try:
            out = subprocess.run(["mp3gain", "-s", "c", "--", ln], capture_output=True).stdout.decode("utf-8").split("\n")
        except FileNotFoundError:
            return (0.0, comments)
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
        return (albumgain_db, comments)
    return (trackgain_db, comments)

def get_flac_gain(ln):
    mimetype=subprocess.run(["file", "-b", "--mime-type", "--", ln], capture_output=True).stdout.decode("us-ascii")
    magic_ref = 89.0
    ref = 89.0
    trackgain_db = 0.0
    albumgain_db = None
    comments = []
    if mimetype != "" and mimetype[-1] == "\n":
        mimetype = mimetype[:-1]
    if mimetype == "audio/flac":
        try:
            out = subprocess.run(["metaflac", "--list", "--block-type=VORBIS_COMMENT", "--", ln], capture_output=True).stdout.decode("utf-8").split("\n")
        except FileNotFoundError:
            return (0.0, [])
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
                elif k == 'REPLAYGAIN_ALBUM_PEAK' or k == 'REPLAYGAIN_TRACK_PEAK':
                    pass
                else:
                    comments.append((k,v))
    if albumgain_db != None:
        return (albumgain_db + (magic_ref - ref), comments)
    return (trackgain_db + (magic_ref - ref), comments)

def get_gain(ln):
    mimetype=subprocess.run(["file", "-b", "--mime-type", "--", ln], capture_output=True).stdout.decode("us-ascii")
    magic_ref = 89.0
    ref = 89.0
    trackgain_db = 0.0
    albumgain_db = None
    comments = []
    if mimetype != "" and mimetype[-1] == "\n":
        mimetype = mimetype[:-1]
    if mimetype == "audio/flac":
        return get_flac_gain(ln)
    elif mimetype == "audio/mpeg":
        return get_mp3_gain(ln)
    elif mimetype == "audio/ogg":
        try:
            out = subprocess.run(["vorbiscomment", "--", ln], capture_output=True).stdout.decode("utf-8").split("\n")
        except FileNotFoundError:
            return (0.0, [])
        for out1 in out:
            if out1 == '':
                continue
            if "=" not in out1:
                continue
            k,v = out1.split("=", 1)
            if k == "REPLAYGAIN_REFERENCE_LOUDNESS":
                if v[-3:] == " dB":
                    try:
                        ref = float(v[:-3])
                    except:
                        pass
            elif k == "REPLAYGAIN_TRACK_GAIN":
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
            elif k == 'REPLAYGAIN_ALBUM_PEAK' or k == 'REPLAYGAIN_TRACK_PEAK':
                pass
            else:
                comments.append((k,v))
    if albumgain_db != None:
        return (albumgain_db + (magic_ref - ref), comments)
    return (trackgain_db + (magic_ref - ref), comments)

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
            escape = False
        elif ch == '\\':
            escape = True
            continue
        else:
            res.write(ch)
    return res.getvalue()

mainlck = os.open(os.path.expanduser('~') + '/.mploop/.mainlock', os.O_RDWR | os.O_CREAT, 0o777)
fcntl.flock(mainlck, fcntl.LOCK_EX | fcntl.LOCK_NB)

last_seen = time.monotonic()

expanded = os.path.expanduser('~') + '/.mploop/db.txt'

while True:
    if os.stat(expanded).st_size == 0:
        # Remove console input
        #subprocess.run(["bash", "-c", 'while read -t 0.1 -N 100 a; do true; done'])
        now_monotonic = time.monotonic()
        if now_monotonic - last_seen < 60:
            time.sleep(0.3)
        elif now_monotonic - last_seen < 600:
            time.sleep(1)
        else:
            time.sleep(2)
        continue
    expanded = os.path.expanduser('~') + '/.mploop/db.txt'
    lck =  os.open(expanded, os.O_RDWR | os.O_CREAT, 0o777)
    fcntl.flock(lck, fcntl.LOCK_EX)
    with open(expanded, 'r') as f:
        ln = f.readline()
        if ln == '':
            os.close(lck)
            # Remove console input
            #subprocess.run(["bash", "-c", 'while read -t 0.1 -N 100 a; do true; done'])
            now_monotonic = time.monotonic()
            if now_monotonic - last_seen < 60:
                time.sleep(0.3)
            elif now_monotonic - last_seen < 600:
                time.sleep(1)
            else:
                time.sleep(2)
            continue
        last_seen = time.monotonic()
        if ln and ln[-1] == '\n':
            ln = ln[:-1]
        ln = unescape(ln)
        rest = ''.join(f.readlines())
    with open(expanded, "w") as f:
        f.write(rest)
    os.close(lck)
    gain,comments = get_gain(ln)
    if clear_stdin():
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
    print(80*"-")
    subprocess.run(["mplayer", "-nolirc", "-msglevel", "all=0:statusline=5", "-af", "volume=" + str(gain-offset2) + ":1", "--", ln])
    print("")
