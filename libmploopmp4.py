from __future__ import print_function
from __future__ import division
import struct
import io

def f_skip(f, siz):
    while siz >= 16384:
        if len(f.read(16384)) != 16384:
            return False
        siz -= 16384
    if len(f.read(siz)) != siz:
        return False
    return True

def mp4_tags(fn):
    comments = []
    try:
        with open(fn, "rb") as f:
            ftypsizbuf = f.read(4)
            if len(ftypsizbuf) != 4:
                return None
            ftypsiz = struct.unpack(">I", ftypsizbuf)[0]
            ftyptkn = f.read(4)
            if ftypsiz == 1:
                return None
            elif ftypsiz < 8:
                return None
            if ftyptkn != b"ftyp":
                return None
            ftypbuf = f.read(ftypsiz-8)
            if len(ftypbuf) != ftypsiz-8:
                return None
            while True:
                moovsizbuf = f.read(4)
                if len(moovsizbuf) != 4:
                    return None
                moovsiz = struct.unpack(">I", moovsizbuf)[0]
                moovtkn = f.read(4)
                if moovsiz == 1:
                    moovsizbuf = f.read(8)
                    moovsiz = struct.unpack(">Q", moovsizbuf)-8
                    if moovsiz < 8:
                        return None
                elif moovsiz < 8:
                    return None
                if moovtkn != b"moov":
                    if not f_skip(f, moovsiz-8):
                        return None
                    continue
                if moovsiz > 32*1024*1024: # arbitrary limit
                    return None
                moovbuf = f.read(moovsiz-8)
                if len(moovbuf) != moovsiz-8:
                    return None
                fmoov = io.BytesIO(moovbuf)
                while True:
                    udtasizbuf = fmoov.read(4)
                    if len(udtasizbuf) == 0:
                        break
                    if len(udtasizbuf) != 4:
                        return None
                    udtasiz = struct.unpack(">I", udtasizbuf)[0]
                    udtatkn = fmoov.read(4)
                    if udtasiz == 1:
                        return None
                    elif udtasiz < 8:
                        return None
                    udtabuf = fmoov.read(udtasiz-8)
                    if len(udtabuf) != udtasiz-8:
                        return None
                    if udtatkn != b"udta":
                        continue
                    fudta = io.BytesIO(udtabuf)
                    while True:
                        metasizbuf = fudta.read(4)
                        if len(metasizbuf) == 0:
                            break
                        if len(metasizbuf) != 4:
                            return None
                        metasiz = struct.unpack(">I", metasizbuf)[0]
                        metatkn = fudta.read(4)
                        if metasiz == 1:
                            return None
                        elif metasiz < 8:
                            return None
                        metabuf = fudta.read(metasiz-8)
                        if len(metabuf) != metasiz-8:
                            return None
                        if metatkn != b"meta":
                            continue
                        fmeta = io.BytesIO(metabuf[4:])
                        while True:
                            ilstsizbuf = fmeta.read(4)
                            if len(ilstsizbuf) == 0:
                                break
                            if len(ilstsizbuf) != 4:
                                return None
                            ilstsiz = struct.unpack(">I", ilstsizbuf)[0]
                            ilsttkn = fmeta.read(4)
                            if ilstsiz == 1:
                                return None
                            elif ilstsiz < 8:
                                return None
                            ilstbuf = fmeta.read(ilstsiz-8)
                            if len(ilstbuf) != ilstsiz-8:
                                return None
                            if ilsttkn != b"ilst":
                                continue
                            filst = io.BytesIO(ilstbuf)
                            while True:
                                tagsizbuf = filst.read(4)
                                if len(tagsizbuf) == 0:
                                    break
                                if len(tagsizbuf) != 4:
                                    return None
                                tagsiz = struct.unpack(">I", tagsizbuf)[0]
                                tagtkn = filst.read(4)
                                if tagsiz == 1:
                                    return None
                                elif tagsiz < 8:
                                    return None
                                tagbuf = filst.read(tagsiz-8)
                                if len(tagbuf) != tagsiz-8:
                                    return None
                                ftag = io.BytesIO(tagbuf)
                                rawdata = None
                                data = None
                                mean = None
                                name = None
                                while True:
                                    kvsizbuf = ftag.read(4)
                                    if len(kvsizbuf) == 0:
                                        break
                                    if len(kvsizbuf) != 4:
                                        return None
                                    kvsiz = struct.unpack(">I", kvsizbuf)[0]
                                    kvtkn = ftag.read(4)
                                    if kvsiz == 1:
                                        return None
                                    elif kvsiz < 8:
                                        return None
                                    kvbuf = ftag.read(kvsiz-8)
                                    if len(kvbuf) != kvsiz-8:
                                        return None
                                    if kvtkn == b'data':
                                        if kvbuf[0:8] == b'\x00\x00\x00\x01\x00\x00\x00\x00': # text
                                            data = kvbuf[8:].decode("utf-8")
                                            rawdata = None
                                        elif kvbuf[0:8] == b'\x00\x00\x00\x00\x00\x00\x00\x00': # uint8
                                            data = None
                                            rawdata = kvbuf[8:]
                                    elif kvtkn == b'mean':
                                        mean = kvbuf
                                    elif kvtkn == b'name':
                                        name = kvbuf
                                tagtkns = {
                                        b'\xa9alb': 'ALBUM',
                                        b'\xa9ART': 'ARTIST',
                                        b'aART': 'ALBUMARTIST',
                                        b'\xa9cmt': 'COMMENT',
                                        b'\xa9day': 'YEAR',
                                        b'\xa9nam': 'TITLE',
                                        b'\xa9gen': 'GENRE',
                                        b'\xa9wrt': 'COMPOSER',
                                        b'\xa9too': 'ENCODER',
                                        b'cprt': 'COPYRIGHT',
                                        b'desc': 'DESCRIPTION',
                                        b'\xa9enc': 'ENCODED-BY',
                                }
                                if tagtkn == b'trkn' and rawdata:
                                    if rawdata[0:3] == b'\x00\x00\x00' and rawdata[4:5] == b'\x00' and rawdata[6:8] == b'\x00\x00':
                                        comments.append("TRACKNUMBER=" + str(struct.unpack("B", rawdata[3:4])[0]) + "/" + str(struct.unpack("B", rawdata[5:6])[0]))
                                    pass
                                elif tagtkn == b'disk' and rawdata:
                                    if rawdata[0:3] == b'\x00\x00\x00' and rawdata[4:5] == b'\x00':
                                        comments.append("DISCNUMBER=" + str(struct.unpack("B", rawdata[3:4])[0]) + "/" + str(struct.unpack("B", rawdata[5:6])[0]))
                                    pass
                                elif tagtkn == b'----':
                                    if mean == b'\0\0\0\0com.apple.iTunes':
                                        if name == b'\0\0\0\0replaygain_track_gain':
                                            comments.append("REPLAYGAIN_TRACK_GAIN=" + data + " dB")
                                        elif name == b'\0\0\0\0replaygain_album_gain':
                                            comments.append("REPLAYGAIN_ALBUM_GAIN=" + data + " dB")
                                elif tagtkn in tagtkns:
                                    comments.append(tagtkns[tagtkn]+'='+data)
                return comments
    except:
        return None
