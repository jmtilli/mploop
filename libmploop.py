import os
import sys
import fcntl
import termios
import time
import subprocess
import select
import socket
import io

mploopplayer = os.path.dirname(os.path.realpath(sys.argv[0])) + '/mploopplayer/mploopplayer'
if not os.access(mploopplayer, os.X_OK):
    mploopplayer = None

dbexpanded = os.path.expanduser('~') + '/.mploop/db.txt'
pastexpanded = os.path.expanduser('~') + '/.mploop/past.txt'
npexpanded = os.path.expanduser('~') + '/.mploop/np.txt'
mploopplayerpidexpanded = os.path.expanduser('~') + '/.mploop/mploopplayer.pid'
mplayerpidexpanded = os.path.expanduser('~') + '/.mploop/mplayer.pid'
sockexpanded = os.path.expanduser('~') + '/.mploop/sock'
mplayersockexpanded = os.path.expanduser('~') + '/.mploop/mplayer.sock'

def send_mploop_command(cmd, mplayercmd=None):
    if mploopplayer is None:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            sock.connect(mplayersockexpanded)
            if mplayercmd != None:
                sock.sendall(mplayercmd.encode())
            else:
                sock.sendall(cmd.encode())
        finally:
            sock.close()
    else:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            sock.connect(sockexpanded)
            sock.sendall(cmd.encode())
        finally:
            sock.close()

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

def escape(x):
    return x.replace("\\", "\\\\").replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")

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

def maybe_monotonic_time():
    if hasattr(time, "monotonic"):
        return time.monotonic()
    else:
        return time.time()

class AdaptiveSleep(object):
    def __init__(self):
        self.seen()
    def seen(self):
        self.last_seen = maybe_monotonic_time()
    def sleep(self):
        now_monotonic = maybe_monotonic_time()
        if now_monotonic - self.last_seen < 60:
            time.sleep(0.3)
        elif now_monotonic - self.last_seen < 600:
            time.sleep(1)
        else:
            time.sleep(2)
class MainLock(object):
    def __init__(self):
        self.mainlck = os.open(os.path.expanduser('~') + '/.mploop/.mainlock', os.O_RDWR | os.O_CREAT, 0o666)
        fcntl.flock(self.mainlck, fcntl.LOCK_EX | fcntl.LOCK_NB)
    def __enter__(self):
        pass
    def __exit__(self, *args):
        os.close(self.mainlck)
class DbLock(object):
    def __init__(self):
        self.mainlck = os.open(dbexpanded, os.O_RDWR | os.O_CREAT, 0o666)
        fcntl.flock(self.mainlck, fcntl.LOCK_EX)
    def __enter__(self):
        pass
    def __exit__(self, *args):
        os.close(self.mainlck)

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
    r128trackgain_db = None
    r128albumgain_db = None
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
                if k == "R128_TRACK_GAIN":
                    try:
                        r128trackgain_db = float(v)/256.0 + offset
                    except:
                        pass
                elif k == "R128_ALBUM_GAIN":
                    try:
                        r128albumgain_db = float(v)/256.0 + offset
                    except:
                        pass
                elif k == "REPLAYGAIN_REFERENCE_LOUDNESS":
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
    if r128albumgain_db != None:
        return (r128albumgain_db, comments)
    if r128trackgain_db != None:
        return (r128trackgain_db, comments)
    if albumgain_db != None:
        return (albumgain_db + (magic_ref - ref), comments)
    return (trackgain_db + (magic_ref - ref), comments)

def get_opusinfo(ln):
    is_opus = False
    result = []
    found = False
    try:
        out = subprocess.run(["opusinfo", "--", ln], capture_output=True).stdout.decode("utf-8").split("\n")
        for out1 in out:
            if out1 == "User comments section follows...":
                found = True
                is_opus = True
                continue
            if out1[:12] == "Opus stream ":
                is_opus = True
            if not found:
                continue
            if out1[0] == "\t":
                result.append(out1[1:])
            else:
                found = False
        if result == []:
            return (is_opus, [""])
        return (is_opus, result)
    except FileNotFoundError:
        return (None, [""])

def touch():
    os.makedirs(os.path.expanduser('~') + '/.mploop', exist_ok = True)
    with open(os.path.expanduser('~') + '/.mploop/db.txt', 'a'):
        pass
    with open(os.path.expanduser('~') + '/.mploop/past.txt', 'a'):
        pass

def get_gain(ln):
    mimetype=subprocess.run(["file", "-b", "--mime-type", "--", ln], capture_output=True).stdout.decode("us-ascii")
    r128trackgain_db = None
    r128albumgain_db = None
    magic_ref = 89.0
    ref = 89.0
    trackgain_db = 0.0
    albumgain_db = None
    comments = []
    is_opus = False
    if mimetype != "" and mimetype[-1] == "\n":
        mimetype = mimetype[:-1]
    if mimetype == "audio/flac":
        return get_flac_gain(ln)
    elif mimetype == "audio/mpeg":
        return get_mp3_gain(ln)
    elif mimetype == "audio/ogg":
        try:
            out = [""]
            is_opus, out = get_opusinfo(ln)
            try:
                if is_opus == None:
                    is_opus = False
                    out = subprocess.run(["opustags", "--", ln], capture_output=True).stdout.decode("utf-8").split("\n")
                    if out != [""]:
                        is_opus = True
            except FileNotFoundError:
                pass
            if out == [""]:
                out = subprocess.run(["vorbiscomment", "--", ln], capture_output=True).stdout.decode("utf-8").split("\n")
        except FileNotFoundError:
            return (0.0, [])
        for out1 in out:
            if out1 == '':
                continue
            if "=" not in out1:
                continue
            k,v = out1.split("=", 1)
            if k == "R128_TRACK_GAIN":
                try:
                    r128trackgain_db = float(v)/256.0 + offset
                except:
                    pass
            elif k == "R128_ALBUM_GAIN":
                try:
                    r128albumgain_db = float(v)/256.0 + offset
                except:
                    pass
            elif k == "REPLAYGAIN_REFERENCE_LOUDNESS":
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
    if r128albumgain_db != None:
        return (r128albumgain_db, comments)
    if r128trackgain_db != None:
        return (r128trackgain_db, comments)
    if albumgain_db != None:
        return (albumgain_db + (magic_ref - ref), comments)
    return (trackgain_db + (magic_ref - ref), comments)

def skip(cnt):
    if cnt == 0:
        pass
    elif cnt == 1:
        with open(npexpanded, 'r') as f:
            np = (f.read() != '')
        if not np:
            print("Not playing currently")
            sys.exit(1)
        send_mploop_command("q")
    elif cnt > 1:
        with DbLock() as lck:
            with open(npexpanded, 'r') as f:
                np = (f.read() != '')
            if not np:
                print("Not playing currently")
                sys.exit(1)
            with open(dbexpanded, 'r') as f:
                queue = f.readlines()
                toput = list(reversed(queue[0:(cnt-1)]))
                queueremain = queue[(cnt-1):]
            with open(pastexpanded, 'r') as f:
                past = f.readlines()
            with open(pastexpanded, 'w') as f:
                f.write(''.join(toput + past))
            with open(dbexpanded, 'w') as f:
                f.write(''.join(queueremain))
            send_mploop_command("q")
    else:
        acnt = abs(cnt)
        with DbLock() as lck:
            with open(npexpanded, 'r') as f:
                np = (f.read() != '')
            with open(pastexpanded, 'r') as f:
                past = f.readlines()
                if np:
                    toput = list(reversed(past[0:(1+acnt)]))
                    pastremain = past[(1+acnt):]
                else:
                    toput = list(reversed(past[0:acnt]))
                    pastremain = past[acnt:]
            with open(pastexpanded, 'w') as f:
                f.write(''.join(pastremain))
            with open(dbexpanded, 'r') as f:
                queue = f.readlines()
            with open(dbexpanded, 'w') as f:
                f.write(''.join(toput + queue))
            if np:
                send_mploop_command("q")
