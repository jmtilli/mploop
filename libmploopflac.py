from __future__ import print_function
from __future__ import division
import struct
import io

class FlacWith(object):
    def __init__(self, fn):
        self.f = open(fn, "rb")
        self.no_more = False
        hdr = self.f.read(4)
        if len(hdr) != 4:
            raise Exception("Not FLAC")
        if hdr[0:4] != b'fLaC':
            raise Exception("Not FLAC")
    def __enter__(self):
        return self
    def __exit__(self, exc_type, exc_val, exc_traceback):
        self.f.close()
    def get_metadata_block(self):
        if self.no_more:
            return None
        hdr = self.f.read(4)
        byte = struct.unpack("B", hdr[0:1])[0]
        if byte>>7:
            self.no_more = True
        mdlen = struct.unpack(">I", b"\x00" + hdr[1:4])[0]
        data = self.f.read(mdlen)
        if len(data) != mdlen:
            self.no_more = True
        return hdr + data

def meta_flac(fn):
    comments = []
    first = True
    try:
        with FlacWith(fn) as flac:
            while True:
                mdblock = flac.get_metadata_block()
                byte = struct.unpack("B", mdblock[0:1])[0]
                if first and (byte&127) != 0:
                    return None
                elif first:
                    first = False
                    continue
                if (byte&127) != 4:
                    continue
                cf = io.BytesIO(mdblock[4:])
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
