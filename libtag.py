from __future__ import print_function
from __future__ import division
import re
import struct
import io

ID3_GENRE_LIST = {
    0: u"Blues",
    1: u"Classic Rock",
    2: u"Country",
    3: u"Dance",
    4: u"Disco",
    5: u"Funk",
    6: u"Grunge",
    7: u"Hip-Hop",
    8: u"Jazz",
    9: u"Metal",
    10: u"New Age",
    11: u"Oldies",
    12: u"Other",
    13: u"Pop",
    14: u"R&B",
    15: u"Rap",
    16: u"Reggae",
    17: u"Rock",
    18: u"Techno",
    19: u"Industrial",
    20: u"Alternative",
    21: u"Ska",
    22: u"Death Metal",
    23: u"Pranks",
    24: u"Soundtrack",
    25: u"Euro-Techno",
    26: u"Ambient",
    27: u"Trip-Hop",
    28: u"Vocal",
    29: u"Jazz+Funk",
    30: u"Fusion",
    31: u"Trance",
    32: u"Classical",
    33: u"Instrumental",
    34: u"Acid",
    35: u"House",
    36: u"Game",
    37: u"Sound Clip",
    38: u"Gospel",
    39: u"Noise",
    40: u"Alternative Rock",
    41: u"Bass",
    42: u"Soul",
    43: u"Punk",
    44: u"Space",
    45: u"Meditative",
    46: u"Instrumental Pop",
    47: u"Instrumental Rock",
    48: u"Ethnic",
    49: u"Gothic",
    50: u"Darkwave",
    51: u"Techno-Industrial",
    52: u"Electronic",
    53: u"Pop-Folk",
    54: u"Eurodance",
    55: u"Dream",
    56: u"Southern Rock",
    57: u"Comedy",
    58: u"Cult",
    59: u"Gangsta",
    60: u"Top 40",
    61: u"Christian Rap",
    62: u"Pop/Funk",
    63: u"Jungle",
    64: u"Native US",
    65: u"Cabaret",
    66: u"New Wave",
    67: u"Psychadelic",
    68: u"Rave",
    69: u"Showtunes",
    70: u"Trailer",
    71: u"Lo-Fi",
    72: u"Tribal",
    73: u"Acid Punk",
    74: u"Acid Jazz",
    75: u"Polka",
    76: u"Retro",
    77: u"Musical",
    78: u"Rock & Roll",
    79: u"Hard Rock",
    80: u"Folk",
    81: u"Folk-Rock",
    82: u"National Folk",
    83: u"Swing",
    84: u"Fast Fusion",
    85: u"Bebob",
    86: u"Latin",
    87: u"Revival",
    88: u"Celtic",
    89: u"Bluegrass",
    90: u"Avantgarde",
    91: u"Gothic Rock",
    92: u"Progressive Rock",
    93: u"Psychedelic Rock",
    94: u"Symphonic Rock",
    95: u"Slow Rock",
    96: u"Big Band",
    97: u"Chorus",
    98: u"Easy Listening",
    99: u"Acoustic",
    100: u"Humour",
    101: u"Speech",
    102: u"Chanson",
    103: u"Opera",
    104: u"Chamber Music",
    105: u"Sonata",
    106: u"Symphony",
    107: u"Booty Bass",
    108: u"Primus",
    109: u"Porn Groove",
    110: u"Satire",
    111: u"Slow Jam",
    112: u"Club",
    113: u"Tango",
    114: u"Samba",
    115: u"Folklore",
    116: u"Ballad",
    117: u"Power Ballad",
    118: u"Rhythmic Soul",
    119: u"Freestyle",
    120: u"Duet",
    121: u"Punk Rock",
    122: u"Drum Solo",
    123: u"Acapella",
    124: u"Euro-House",
    125: u"Dance Hall",
    126: u"Goa",
    127: u"Drum & Bass",
    128: u"Club - House",
    129: u"Hardcore",
    130: u"Terror",
    131: u"Indie",
    132: u"BritPop",
    133: u"Negerpunk",
    134: u"Polsk Punk",
    135: u"Beat",
    136: u"Christian Gangsta Rap",
    137: u"Heavy Metal",
    138: u"Black Metal",
    139: u"Crossover",
    140: u"Contemporary Christian",
    141: u"Christian Rock",
    142: u"Merengue",
    143: u"Salsa",
    144: u"Thrash Metal",
    145: u"Anime",
    146: u"JPop",
    147: u"Synthpop",
    148: u"Unknown",
}

def mangle_genre(x):
    if not re.match("^(\\(([0-9]|[1-9][0-9]|1[0-3][0-9]|14[0-8])\\))+$", x):
        return x
    genres = []
    while x != "":
        res = re.match("^\\(([0-9]+)\\)", x)
        if not res:
            break
        genres.append(ID3_GENRE_LIST[int(res.group(1))])
        x = x[len(res.group(0)):]
    return u', '.join(genres)

def decode_syncsafe(x):
    sizebytes = struct.unpack("BBBB", x)
    if sizebytes[0]>>7:
        return None
    if sizebytes[1]>>7:
        return None
    if sizebytes[2]>>7:
        return None
    if sizebytes[3]>>7:
        return None
    return (sizebytes[0]<<21)|(sizebytes[1]<<14)|(sizebytes[2]<<7)|(sizebytes[3])

def unsync_read_opt(f, sz):
    res = b""
    last_ff = False
    last_ff_old = False
    while len(res) < sz:
        tmpblock = f.read(sz - len(res))
        if tmpblock == b"":
            return res
        last_ff = (tmpblock[-1:] == b"\xff")
        tmpblock = tmpblock.replace(b"\xff\x00", b"\xff")
        if tmpblock[0:1] == b"\x00" and last_ff_old:
            tmpblock = tmpblock[1:]
        res += tmpblock
        last_ff_old = last_ff
    return res

def unsync_read(f, sz):
    res = b""
    unsync = False
    while len(res) < sz:
        ch_read = f.read(1)
        if len(ch_read) != 1:
            return res
        res += ch_read
        if not unsync and len(res) >= 2 and res[-2:] == b"\xff\x00":
            res = res[:-1]
            unsync = True
        else:
            unsync = False
    return res

def maybe_unsync_read(f, unsync, sz):
    if unsync:
        return unsync_read_opt(f, sz)
    else:
        return f.read(sz)

def test():
    for n in range(1,20):
        actual = io.BytesIO(n*b"\xff\x00\x00")
        expected = n*b"\xff\x00"
        assert unsync_read(actual, len(expected)) == expected
    for n in range(1,20):
        actual = io.BytesIO(n*b"\xff\x00")
        expected = n*b"\xff"
        assert unsync_read(actual, len(expected)) == expected
    for n in range(1,20):
        actual = io.BytesIO(n*b"\xff\x00\x00")
        expected = n*b"\xff\x00"
        gotten = unsync_read_opt(actual, len(expected))
        assert gotten == expected
    for n in range(1,20):
        actual = io.BytesIO(n*b"\xff\x00")
        expected = n*b"\xff"
        gotten = unsync_read_opt(actual, len(expected))
        assert gotten == expected

def get_id3v2_4(fname):
    with open(fname, "rb") as f:
        id3header = f.read(10)
        if len(id3header) != 10:
            return None, None
        if id3header[0:3] != b"ID3":
            return None, None
        version = struct.unpack("B", id3header[3:4])[0]
        if version == 0xFF:
            return None, None
        if version != 4:
            return None, None
        revision = struct.unpack("B", id3header[4:5])[0]
        if revision == 0xFF:
            return None, None
        flags = struct.unpack("B", id3header[5:6])[0]
        size = decode_syncsafe(id3header[6:10])
        if size is None:
            return None, None
        unsync_all = (((flags>>7)&1) == 1)
        exthdr = (((flags>>6)&1) == 1)
        experimental = (((flags>>5)&1) == 1)
        footerpresent = (((flags>>4)&1) == 1)
        if flags&15:
            return None, None
        #if flags&128:
        #    return None, None # XXX this fails with tags created by mp3gain
        if exthdr:
            exthdr1 = f.read(4)
            if len(exthdr1) != 4:
                return None, None
            esize = decode_syncsafe(exthdr1)
            if esize is None:
                return None, None
            exthdr = f.read(esize)
            if len(exthdr) != esize:
                return None, None
        gain = {}
        res = []
        while True:
            if f.tell() > size+10:
                return gain, res
            framehdr = f.read(10)
            if len(framehdr) != 10:
                return gain, res
            if f.tell() > size+10:
                return gain, res
            framesz = decode_syncsafe(framehdr[4:8])
            if framesz is None:
                print("ret-None")
                return None, None
            flags = struct.unpack(">H", framehdr[8:10])[0]
            grouping_identity = (flags >> 6)&1
            compression = (flags >> 3)&1
            encryption = (flags >> 2)&1
            frame_unsync = (flags >> 1)&1
            data_length = (flags >> 0)&1
            if data_length:
                dlen = f.read(4) # probably should decode from syncsafe
                if len(dlen) != 4:
                    return gain, res
                framesz -= 4
            framecontents = maybe_unsync_read(f, unsync_all or frame_unsync, framesz)
            if len(framecontents) != framesz:
                return gain, res
            if f.tell() > size+10:
                return gain, res
            if grouping_identity:
                continue # don't know how to handle this
            if compression:
                continue
            if encryption:
                continue
            if data_length:
                continue # probably compressed or encrypted
            #print(repr(framehdr[0:4]))
            keys = {
                    #b"COMM": "COMMENT", # Complex to support
                    b"TXXX": "DESCRIPTION",
                    b"TCOM": "COMPOSER",
                    b"TCOP": "COPYRIGHT",
                    #b"WCOP": "COPYRIGHT", # Not sure if includes encoding byte
                    b"TENC": "ENCODED-BY",
                    b"TEXT": "LYRICIST",
                    b"TIT1": "TITLE",
                    b"TIT2": "TITLE",
                    b"TIT3": "SUBTITLE",
                    b"TPE1": "ARTIST",
                    b"TPE2": "ARTIST",
                    b"TPE3": "CONDUCTOR",
                    b"TPE4": "REMIXER",
                    b"TPUB": "PUBLISHER",
                    b"TSRC": "ISRC",
                    b"TALB": "ALBUM",
                    b"TRCK": "TRACKNUMBER",
                    b"TDAT": "DATE",
                    b"TYER": "YEAR",
                    b"TCON": "GENRE",
            }
            if framehdr[0:4] not in keys:
                continue
            key = keys[framehdr[0:4]]
            encoding = struct.unpack("B", framecontents[0:1])[0]
            if encoding == 0:
                if framecontents[-1:] == b"\x00":
                    framecontents = framecontents[:-1]
                val = framecontents[1:].decode("iso-8859-1")
                #if framecontents[-1:] != b"\x00":
                #    return None, None
            elif encoding == 1:
                if framecontents[-2:] == b"\x00\x00":
                    framecontents = framecontents[:-2]
                val = framecontents[1:].decode("utf-16")
                #if framecontents[-2:] != b"\x00\x00":
                #    return None, None
            elif encoding == 2:
                if framecontents[-2:] == b"\x00\x00":
                    framecontents = framecontents[:-2]
                val = (b"\xfe\xff"+framecontents[1:]).decode("utf-16")
                #if framecontents[-2:] != b"\x00\x00":
                #    return None, None
            elif encoding == 3:
                if framecontents[-1:] == b"\x00":
                    framecontents = framecontents[:-1]
                val = framecontents[1:].decode("utf-8")
                #if framecontents[-1:] != b"\x00":
                #    return None, None
            if key == 'GENRE':
                val = mangle_genre(val)
            if framehdr[0:4] == b"TXXX":
                split_val = val.split(u"\x00", 1)
                if len(split_val) == 2 and split_val[0] == "replaygain_reference_loudness":
                    try:
                        gain["REF"] = float(re.sub(" dB$", "", split_val[1]))
                    except:
                        pass
                if len(split_val) == 2 and split_val[0] == "replaygain_track_gain":
                    try:
                        gain["TRACK"] = float(re.sub(" dB$", "", split_val[1]))
                    except:
                        pass
                if len(split_val) == 2 and split_val[0] == "replaygain_album_gain":
                    try:
                        gain["ALBUM"] = float(re.sub(" dB$", "", split_val[1]))
                    except:
                        pass
                continue
            else:
                res.append((key, val))

def get_id3v2_3(fname):
    with open(fname, "rb") as f:
        id3header = f.read(10)
        if len(id3header) != 10:
            return None, None
        if id3header[0:3] != b"ID3":
            return None, None
        version = struct.unpack("B", id3header[3:4])[0]
        if version == 0xFF:
            return None, None
        if version != 3:
            return None, None
        revision = struct.unpack("B", id3header[4:5])[0]
        if revision == 0xFF:
            return None, None
        flags = struct.unpack("B", id3header[5:6])[0]
        size = decode_syncsafe(id3header[6:10])
        if size is None:
            return None, None
        unsync = (((flags>>7)&1) == 1)
        exthdr = (((flags>>6)&1) == 1)
        experimental = (((flags>>5)&1) == 1)
        if flags&31:
            return None, None
        if flags&128:
            return None, None
        if exthdr:
            exthdr1 = maybe_unsync_read(f, unsync, 4)
            if len(exthdr1) != 4:
                return None, None
            exthdrsize = struct.unpack(">I", exthdr1)[0]
            if exthdrsize != 6 and exthdrsize != 10:
                return None, None
            exthdr = maybe_unsync_read(f, unsync, exthdrsize)
            if len(exthdr) != exthdrsize:
                return None, None
        gain = {}
        res = []
        while True:
            if f.tell() > size+10:
                return gain, res
            framehdr = maybe_unsync_read(f, unsync, 10)
            if len(framehdr) != 10:
                return gain, res
            if f.tell() > size+10:
                return gain, res
            framesz = struct.unpack(">I", framehdr[4:8])[0]
            flags = struct.unpack(">H", framehdr[8:10])[0]
            framecontents = maybe_unsync_read(f, unsync, framesz)
            if len(framecontents) != framesz:
                return gain, res
            if f.tell() > size+10:
                return gain, res
            #print(repr(framehdr[0:4]))
            keys = {
                    #b"COMM": "COMMENT", # Complex to support
                    b"TXXX": "DESCRIPTION",
                    b"TCOM": "COMPOSER",
                    b"TCOP": "COPYRIGHT",
                    #b"WCOP": "COPYRIGHT", # Not sure if includes encoding byte
                    b"TENC": "ENCODED-BY",
                    b"TEXT": "LYRICIST",
                    b"TIT1": "TITLE",
                    b"TIT2": "TITLE",
                    b"TIT3": "SUBTITLE",
                    b"TPE1": "ARTIST",
                    b"TPE2": "ARTIST",
                    b"TPE3": "CONDUCTOR",
                    b"TPE4": "REMIXER",
                    b"TPUB": "PUBLISHER",
                    b"TSRC": "ISRC",
                    b"TALB": "ALBUM",
                    b"TRCK": "TRACKNUMBER",
                    b"TDAT": "DATE",
                    b"TYER": "YEAR",
                    b"TCON": "GENRE",
            }
            if framehdr[0:4] not in keys:
                continue
            key = keys[framehdr[0:4]]
            encoding = struct.unpack("B", framecontents[0:1])[0]
            # If the textstring is followed by a termination ($00 (00)) all the following information should be ignored and not be displayed. 
            if encoding == 0:
                val = framecontents[1:].decode("iso-8859-1")
            elif encoding == 1:
                val = framecontents[1:].decode("utf-16")
            #elif encoding == 2:
            #    val = (b"\xfe\xff"+framecontents[1:]).decode("utf-16")
            if key == 'GENRE':
                val = mangle_genre(val)
            if framehdr[0:4] == b"TXXX":
                split_val = val.split(u"\x00", 1)
                if len(split_val) == 2 and split_val[0] == "replaygain_reference_loudness":
                    try:
                        gain["REF"] = float(re.sub(" dB$", "", split_val[1]))
                    except:
                        pass
                if len(split_val) == 2 and split_val[0] == "replaygain_track_gain":
                    try:
                        gain["TRACK"] = float(re.sub(" dB$", "", split_val[1]))
                    except:
                        pass
                if len(split_val) == 2 and split_val[0] == "replaygain_album_gain":
                    try:
                        gain["ALBUM"] = float(re.sub(" dB$", "", split_val[1]))
                    except:
                        pass
                continue
            else:
                res.append((key, val))

def get_id3v2_2(fname):
    with open(fname, "rb") as f:
        id3header = f.read(10)
        if len(id3header) != 10:
            return None, None
        if id3header[0:3] != b"ID3":
            return None, None
        version = struct.unpack("B", id3header[3:4])[0]
        if version == 0xFF:
            return None, None
        if version != 2:
            return None, None
        revision = struct.unpack("B", id3header[4:5])[0]
        if revision == 0xFF:
            return None, None
        flags = struct.unpack("B", id3header[5:6])[0]
        size = decode_syncsafe(id3header[6:10])
        if size is None:
            return None, None
        unsync = (((flags>>7)&1) == 1)
        compression = (((flags>>6)&1) == 1)
        if compression:
            return None, None
        if flags&63:
            return None, None
        if flags&128:
            return None, None
        gain = {}
        res = []
        while True:
            if f.tell() > size+10:
                return gain, res
            framehdr = maybe_unsync_read(f, unsync, 6)
            if len(framehdr) != 6:
                return gain, res
            if f.tell() > size+10:
                return gain, res
            framesz = struct.unpack(">I", b"\x00"+framehdr[3:6])[0]
            framecontents = maybe_unsync_read(f, unsync, framesz)
            if len(framecontents) != framesz:
                return gain, res
            if f.tell() > size+10:
                return gain, res
            #print(repr(framehdr[0:3]))
            keys = {
                    #b"COM": "COMMENT", # complex to support
                    b"TXX": "DESCRIPTION",
                    b"TCM": "COMPOSER",
                    b"TCR": "COPYRIGHT",
                    #b"WCP": "COPYRIGHT", # complex to support
                    b"TEN": "ENCODED-BY",
                    b"TXT": "LYRICIST",
                    b"TT1": "TITLE",
                    b"TT2": "TITLE",
                    b"TT3": "SUBTITLE",
                    b"TP1": "ARTIST",
                    b"TP2": "ARTIST",
                    b"TP3": "CONDUCTOR",
                    b"TP4": "REMIXER",
                    b"TPB": "PUBLISHER",
                    b"TRC": "ISRC",
                    b"TAL": "ALBUM",
                    b"TRK": "TRACKNUMBER",
                    b"TDA": "DATE",
                    b"TYE": "YEAR",
                    b"TCO": "GENRE", # (51)(39) refers to genres 51 and 39
            }
            if framehdr[0:3] not in keys:
                continue
            key = keys[framehdr[0:3]]
            # If the textstring is followed by a termination ($00 (00)) all the following information should be ignored and not be displayed.
            encoding = struct.unpack("B", framecontents[0:1])[0]
            if encoding == 0:
                val = framecontents[1:].decode("iso-8859-1")
            elif encoding == 1:
                val = framecontents[1:].decode("utf-16")
            #elif encoding == 2:
            #    val = (b"\xfe\xff"+framecontents[1:]).decode("utf-16")
            if key == 'GENRE':
                val = mangle_genre(val)
            if framehdr[0:3] == b"TXX":
                split_val = val.split(u"\x00", 1)
                if len(split_val) == 2 and split_val[0] == "replaygain_reference_loudness":
                    try:
                        gain["REF"] = float(re.sub(" dB$", "", split_val[1]))
                    except:
                        pass
                if len(split_val) == 2 and split_val[0] == "replaygain_track_gain":
                    try:
                        gain["TRACK"] = float(re.sub(" dB$", "", split_val[1]))
                    except:
                        pass
                if len(split_val) == 2 and split_val[0] == "replaygain_album_gain":
                    try:
                        gain["ALBUM"] = float(re.sub(" dB$", "", split_val[1]))
                    except:
                        pass
                continue
            else:
                res.append((key, val))

def get_id3v2(fname):
    with open(fname, "rb") as f:
        id3header = f.read(10)
        if len(id3header) != 10:
            return None, None
        if id3header[0:3] != b"ID3":
            return None, None
        version = struct.unpack("B", id3header[3:4])[0]
        if version == 0xFF:
            return None, None
        revision = struct.unpack("B", id3header[4:5])[0]
        if revision == 0xFF:
            return None, None
        flags = struct.unpack("B", id3header[5:6])[0]
        sizebytes = struct.unpack("BBBB", id3header[6:10])
        if sizebytes[0]>>7:
            return None, None
        if sizebytes[1]>>7:
            return None, None
        if sizebytes[2]>>7:
            return None, None
        if sizebytes[3]>>7:
            return None, None
        size = (sizebytes[0]<<21)|(sizebytes[1]<<14)|(sizebytes[2]<<7)|(sizebytes[3])
        if version == 4:
            return get_id3v2_4(fname)
        if version == 3:
            return get_id3v2_3(fname)
        if version == 2:
            return get_id3v2_2(fname)

def get_ape(fname):
    with open(fname, "rb") as f:
        f.seek(0, 2)
        file_len = f.tell()
        f.seek(-32, 2)
        apefooter = f.read(32)
        if len(apefooter) != 32:
            return None, None
        if apefooter[0:8] != b"APETAGEX":
            f.seek(-128, 2)
            id3v1tag = f.read(128)
            if len(id3v1tag) == 128:
                has_id3 = (id3v1tag[0:3] == b'TAG')
                if has_id3:
                    f.seek(-128-32, 2)
                    apefooter = f.read(32)
        if len(apefooter) != 32:
            return None, None
        if apefooter[0:8] != b"APETAGEX":
            return None, None
        apeversion = apefooter[8:12]
        if struct.unpack("<I", apeversion)[0] != 2000:
            return None, None
        apelen = struct.unpack("<I", apefooter[12:16])[0]
        apeitems = struct.unpack("<I", apefooter[16:20])[0]
        apeflags = struct.unpack("<I", apefooter[20:24])[0]
        apereserved = apefooter[24:32]
        if file_len < apelen:
            return None, None
        if file_len >= (apelen + (has_id3 and (128+32) or 32)):
            f.seek(-apelen-(has_id3 and (128+32) or 32), 2)
            apeheader = f.read(32)
            if len(apeheader) != 32:
                return None, None
            apeversion2 = apeheader[8:12]
            if struct.unpack("<I", apeversion2)[0] != 2000:
                return None, None
            apelen2 = struct.unpack("<I", apeheader[12:16])[0]
            apeitems2 = struct.unpack("<I", apeheader[16:20])[0]
            apeflags2 = struct.unpack("<I", apeheader[20:24])[0]
            apereserved2 = apeheader[24:32]
            if apelen2 != apelen or apeitems2 != apeitems:
                return None, None
            gain = {}
            res = []
            for itemid in range(apeitems2):
                tagheader = f.read(8)
                if len(tagheader) != 8:
                    return None, None
                taglen = struct.unpack("<I", tagheader[0:4])[0]
                tagflags = struct.unpack("<I", tagheader[4:8])[0]
                key = b''
                while True:
                    byte = f.read(1)
                    if len(byte) != 1:
                        break
                    if byte != b'\x00':
                        key += byte
                    else:
                        break
                if str != bytes:
                    try:
                        key = key.decode("us-ascii")
                    except:
                        return None, None
                val = f.read(taglen)
                if len(val) != taglen:
                    return None, None
                if (tagflags&0x6) == 0:
                    if key == 'REPLAYGAIN_REFERENCE_LOUDNESS':
                        try:
                            gain["REF"] = float(re.sub(" dB$", "", val.decode("utf-8")))
                        except:
                            pass
                    elif key == 'REPLAYGAIN_TRACK_GAIN':
                        try:
                            gain["TRACK"] = float(re.sub(" dB$", "", val.decode("utf-8")))
                        except:
                            pass
                    elif key == 'REPLAYGAIN_ALBUM_GAIN':
                        try:
                            gain["ALBUM"] = float(re.sub(" dB$", "", val.decode("utf-8")))
                        except:
                            pass
                    else:
                        res.append((key, val.decode("utf-8")))
            return gain, res

def get_id3v1(fname):
    with open(fname, "rb") as f:
        f.seek(-128, 2)
        gain = {}
        res = []
        id3v1tag = f.read(128)
        if len(id3v1tag) != 128:
            return None, None
        if id3v1tag[0:3] == b'TAG':
            title = id3v1tag[3:33]
            while title and title[-1:] == b'\x00':
                title = title[:-1]
            try:
                title = title.decode("utf-8")
            except:
                title = title.decode("iso-8859-1")
            artist = id3v1tag[33:63]
            while artist and artist[-1:] == b'\x00':
                artist = artist[:-1]
            try:
                artist = artist.decode("utf-8")
            except:
                artist = artist.decode("iso-8859-1")
            album = id3v1tag[63:93]
            while album and album[-1:] == b'\x00':
                album = album[:-1]
            try:
                album = album.decode("utf-8")
            except:
                album = album.decode("iso-8859-1")
            year = id3v1tag[93:97]
            while year and year[-1:] == b'\x00':
                year = year[:-1]
            year = year.decode("iso-8859-1")
            comment = id3v1tag[97:125]
            while comment and comment[-1:] == b'\x00':
                comment = comment[:-1]
            try:
                comment = comment.decode("utf-8")
            except:
                comment = comment.decode("iso-8859-1")
            tracknumber = struct.unpack(">H", id3v1tag[125:127])[0]
            genre = struct.unpack("B", id3v1tag[127:128])[0]
            res = []
            res.append(("TITLE", title))
            res.append(("ARTIST", artist))
            res.append(("ALBUM", album))
            res.append(("YEAR", year))
            res.append(("COMMENT", comment))
            try:
                res.append(("TRACKNUMBER", unicode(tracknumber)))
            except NameError:
                res.append(("TRACKNUMBER", str(tracknumber)))
            try:
                res.append(("GENRE", ID3_GENRE_LIST[genre]))
            except KeyError:
                pass
            return gain, res
        else:
            return None, None

if __name__ == '__main__':
    test()
