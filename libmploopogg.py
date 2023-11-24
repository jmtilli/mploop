from __future__ import print_function
from __future__ import division
import struct
import io

class OggWith(object):
    def __init__(self, fn):
        self.f = open(fn, "rb")
        hdr = self.f.read(28)
        if len(hdr) != 28:
            raise Exception("not ogg")
        if hdr[0:4] != b'OggS':
            raise Exception("not ogg")
        if hdr[4:5] != b'\x00':
            raise Exception("not correct ogg version")
        flags = struct.unpack("B", hdr[5:6])[0]
        if (flags >> 0)&1:
            raise Exception("continuation")
        if not ((flags >> 1)&1):
            raise Exception("not first")
        self.ssn = hdr[14:18]
        psn = hdr[18:22]
        checksum = hdr[22:26]
        pagesegments = struct.unpack("B", hdr[26:27])[0]
        hdr += self.f.read(pagesegments-1)
        self.segtbl = list(struct.unpack(pagesegments*"B", hdr[27:(27+pagesegments)]))
    def __enter__(self):
        return self
    def __exit__(self, exc_type, exc_val, exc_traceback):
        self.f.close()
    def get_page(self):
        hdr = self.f.read(28)
        if len(hdr) != 28:
            raise Exception("not ogg")
        if hdr[0:4] != b'OggS':
            raise Exception("not ogg")
        if hdr[4:5] != b'\x00':
            raise Exception("not correct ogg version")
        flags = struct.unpack("B", hdr[5:6])[0]
        if (flags >> 0)&1:
            raise Exception("continuation")
        if ((flags >> 1)&1):
            raise Exception("first")
        self.ssn = hdr[14:18]
        psn = hdr[18:22]
        checksum = hdr[22:26]
        pagesegments = struct.unpack("B", hdr[26:27])[0]
        hdr += self.f.read(pagesegments-1)
        self.segtbl = list(struct.unpack(pagesegments*"B", hdr[27:(27+pagesegments)]))
    def get_packet(self):
        if len(self.segtbl) == 0:
            self.get_page()
        idx = 0
        totcnt = 0
        for n in self.segtbl:
            totcnt += n
            idx += 1
            if n != 255:
                break
        del self.segtbl[0:idx]
        return self.ssn, self.f.read(totcnt)

def vorbis_comment(fn):
    ssnblacklist = set([])
    ssnwhitelist = set([])
    comments = []
    try:
        with OggWith(fn) as ogg:
            while True:
                ssn,pkt = ogg.get_packet()
                if len(pkt) == 0:
                    break
                if ssn in ssnblacklist:
                    continue
                if pkt[1:7] != b'vorbis':
                    ssnblacklist.add(ssn)
                    continue
                pkttype = struct.unpack("B", pkt[0:1])[0]
                if pkttype == 1 and len(pkt) >= 30 and pkt[7:11] == b"\x00\x00\x00\x00":
                    ssnwhitelist.add(ssn)
                    continue
                if pkttype == 3 and ssn in ssnwhitelist:
                    pass # comment
                else:
                    ssnblacklist.add(ssn)
                    continue
                cf = io.BytesIO(pkt[7:])
                vendor_len = struct.unpack("<I", cf.read(4))[0]
                vendor_str = cf.read(vendor_len)
                comment_list_len = struct.unpack("<I", cf.read(4))[0]
                for n in range(comment_list_len):
                    comment_len = struct.unpack("<I", cf.read(4))[0]
                    comment = cf.read(comment_len)
                    comments.append(comment.decode("utf-8"))
                return comments
    except:
        return None

def opus_info(fn):
    ssnblacklist = set([])
    ssnwhitelist = set([])
    comments = []
    try:
        with OggWith(fn) as ogg:
            while True:
                ssn,pkt = ogg.get_packet()
                if len(pkt) == 0:
                    break
                if ssn in ssnblacklist:
                    continue
                if pkt[0:4] != b'Opus':
                    ssnblacklist.add(ssn)
                    continue
                if pkt[4:8] == b'Head':
                    if pkt[8:9] != b'\x01':
                        ssnblacklist.add(ssn)
                        continue
                    ssnwhitelist.add(ssn)
                    continue
                if pkt[4:8] == b'Tags' and ssn in ssnwhitelist:
                    pass # comment
                else:
                    ssnblacklist.add(ssn)
                    continue
                cf = io.BytesIO(pkt[8:])
                vendor_len = struct.unpack("<I", cf.read(4))[0]
                vendor_str = cf.read(vendor_len)
                comment_list_len = struct.unpack("<I", cf.read(4))[0]
                for n in range(comment_list_len):
                    comment_len = struct.unpack("<I", cf.read(4))[0]
                    comment = cf.read(comment_len)
                    comments.append(comment.decode("utf-8"))
                return comments
    except:
        return None
