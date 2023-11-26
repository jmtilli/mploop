from __future__ import print_function
from __future__ import division
import struct

def f_skip(f, siz):
    while siz >= 16384:
        if len(f.read(16384)) != 16384:
            return False
        siz -= 16384
    if len(f.read(siz)) != siz:
        return False
    return True

class Atom(object):
    def __init__(self, parent, f, siz):
        self.parent = parent
        self.siz = siz
        self.f = f
    def remaining(self):
        if self.parent:
            return min(self.parent.remaining(), self.siz)
        else:
            return self.siz
    def consume(self, siz):
        assert siz <= self.remaining()
        self.siz -= siz
        if self.parent:
            self.parent.consume(siz)
    def read(self, siz):
        if siz > self.remaining():
            siz = self.remaining()
        self.consume(siz)
        if siz == 0:
            return b''
        return self.f.read(siz)
    def skip_all(self):
        res = f_skip(self.f, self.remaining())
        self.consume(self.remaining())
        return res
    def skip(self, siz):
        if siz > self.remaining():
            siz = self.remaining()
        self.consume(siz)
        return f_skip(self.f, siz)


def process_meta(meta, f, comments):
    vflags = meta.read(4) # version, 1 byte, flags, 3 bytes
    while True:
        if vflags != b"\x00\x00\x00\x00" and vflags is not None:
            ilstsizbuf = vflags
            ilsttkn = meta.read(4)
            proposed_siz1 = struct.unpack(">I", vflags)[0]
            proposed_siz2 = struct.unpack(">I", ilsttkn)[0]
            if proposed_siz1 < 8 and proposed_siz2-8 <= meta.remaining():
                if ilsttkn != b"ilst" and ilsttkn != b"hdlr" and ilsttkn != b"free":
                    second_way = True
            if proposed_siz1-8 > meta.remaining() and proposed_siz2-8 <= meta.remaining():
                if ilsttkn != b"ilst" and ilsttkn != b"hdlr" and ilsttkn != b"free":
                    second_way = True
            if second_way:
                ilstsizbuf = ilsttkn
                ilsttkn = meta.read(4)
        else:
            ilstsizbuf = meta.read(4)
            ilsttkn = meta.read(4)
        vflags = None
        if len(ilstsizbuf) == 0:
            break
        if len(ilstsizbuf) != 4:
            return None
        ilstsiz = struct.unpack(">I", ilstsizbuf)[0]
        if ilstsiz == 1:
            ilstsizbuf = meta.read(8)
            ilstsiz = struct.unpack(">Q", ilstsizbuf)-8
            if ilstsiz < 8:
                return None
        elif ilstsiz < 8:
            return None
        ilst = Atom(meta, f, ilstsiz-8)
        if ilsttkn != b"ilst":
            ilst.skip_all()
            continue
        while True:
            tagsizbuf = ilst.read(4)
            if len(tagsizbuf) == 0:
                break
            if len(tagsizbuf) != 4:
                return None
            tagsiz = struct.unpack(">I", tagsizbuf)[0]
            tagtkn = ilst.read(4)
            if tagsiz == 1:
                tagsizbuf = ilst.read(8)
                tagsiz = struct.unpack(">Q", tagsizbuf)-8
                if tagsiz < 8:
                    return None
            elif tagsiz < 8:
                return None
            tag = Atom(ilst, f, tagsiz-8)
            rawdata = None
            data = None
            mean = None
            name = None
            while True:
                kvsizbuf = tag.read(4)
                if len(kvsizbuf) == 0:
                    break
                if len(kvsizbuf) != 4:
                    return None
                kvsiz = struct.unpack(">I", kvsizbuf)[0]
                kvtkn = tag.read(4)
                if kvsiz == 1:
                    return None
                elif kvsiz < 8:
                    return None
                kvbuf = tag.read(kvsiz-8)
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
            if not f_skip(f, ftypsiz-8):
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
                moov = Atom(None, f, moovsiz-8)
                while True:
                    udtasizbuf = moov.read(4)
                    if len(udtasizbuf) == 0:
                        break
                    if len(udtasizbuf) != 4:
                        return None
                    udtasiz = struct.unpack(">I", udtasizbuf)[0]
                    udtatkn = moov.read(4)
                    if udtasiz == 1:
                        udtasizbuf = moov.read(8)
                        udtasiz = struct.unpack(">Q", udtasizbuf)-8
                        if udtasiz < 8:
                            return None
                    elif udtasiz < 8:
                        return None
                    udta = Atom(moov, f, udtasiz-8)
                    if udtatkn == b"meta":
                        if process_meta(udta, f, comments) is None:
                            return None
                        continue
                    if udtatkn != b"udta":
                        udta.skip_all()
                        continue
                    while True:
                        metasizbuf = udta.read(4)
                        if len(metasizbuf) == 0:
                            break
                        if len(metasizbuf) != 4:
                            return None
                        metasiz = struct.unpack(">I", metasizbuf)[0]
                        metatkn = udta.read(4)
                        if metasiz == 1:
                            metasizbuf = udta.read(8)
                            metasiz = struct.unpack(">Q", metasizbuf)-8
                            if metasiz < 8:
                                return None
                        elif metasiz < 8:
                            return None
                        meta = Atom(udta, f, metasiz-8)
                        if metatkn != b"meta":
                            meta.skip_all()
                            continue
                        if process_meta(meta, f, comments) is None:
                            return None
                return comments
    except:
        return None
