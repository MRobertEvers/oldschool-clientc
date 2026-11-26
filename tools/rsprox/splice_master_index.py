"""splice_master_index.py <src.bin> <donor.bin> <out.bin>

A copy of an RSProx recording with another recording's JS5 master index in its
header. RSProx names ids from the cache that master index identifies (OpenRS2);
a revision OpenRS2 has not archived yet (rev 243 on 2026-11-25) transcribes with
bare ids. Splicing in an older session's master index (index 4677, rev 242)
names everything from that cache. The packet decoders still follow the
recording's own revision. Ids added in the newer revision print unnamed or with
the older name, so check anything new by id.

    python3 tools/rsprox/splice_master_index.py idx4679.rev243mi.bin idx4677.bin idx4679.bin

The header layout is proxy/src/main/kotlin/net/rsprox/proxy/binary/BinaryHeader.kt
(rsprox-c).
"""
import struct
import sys


def jstr_end(b, o):
    return b.index(0, o) + 1


def master_index_at(b):
    o = 4 + 4 + 4 + 1 + 1 + 8 + 2 + 4 + 1   # version, revision, sub, client, platform, time, world, flags, location
    o = jstr_end(b, o)                        # world host
    o = jstr_end(b, o)                        # world activity
    o += 2                                    # local player index
    o += 2 + struct.unpack(">H", b[o:o + 2])[0]  # account hash
    o = jstr_end(b, o)                        # client name
    return o, struct.unpack(">I", b[o:o + 4])[0]


def main():
    src = open(sys.argv[1], "rb").read()
    donor = open(sys.argv[2], "rb").read()
    sa, sn = master_index_at(src)
    da, dn = master_index_at(donor)
    print("src rev %d master index %d bytes; donor rev %d, %d bytes" % (
        struct.unpack(">I", src[4:8])[0], sn, struct.unpack(">I", donor[4:8])[0], dn))
    with open(sys.argv[3], "wb") as out:
        out.write(src[:sa] + donor[da:da + 4 + dn] + src[sa + 4 + sn:])


if __name__ == "__main__":
    main()
