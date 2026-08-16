#!/usr/bin/env python3
"""Compute a LostCity server's 9 login-block jag CRCs from its client cache.

Mirrors engine/src/cache/CrcTable.ts makeCrcs(): CRC32 of each idx0 archive as
stored, in index order, signed. Same numbers `curl http://<host>/crc` serves,
without needing the server up.
"""
import sys, zlib

def read_archive(dat, idx, file_id):
    off = file_id * 6
    size = int.from_bytes(idx[off:off+3], 'big')
    sector = int.from_bytes(idx[off+3:off+6], 'big')
    if size == 0 or size > 2_000_000 or sector <= 0:
        return None
    out = bytearray()
    part = 0
    while len(out) < size and sector:
        base = sector * 520
        avail = min(512, size - len(out))
        head = dat[base:base+8]
        if len(head) < 8:
            return None
        # sector header: file id u16, part u16, next sector u24, index id u8
        nxt = int.from_bytes(head[4:7], 'big')
        out += dat[base+8:base+8+avail]
        sector = nxt
        part += 1
    return bytes(out)

def main(cache_dir):
    dat = open(f'{cache_dir}/main_file_cache.dat','rb').read()
    idx = open(f'{cache_dir}/main_file_cache.idx0','rb').read()
    crcs = []
    for i in range(len(idx)//6):
        a = read_archive(dat, idx, i)
        if a is None:
            crcs.append(0)
            continue
        crc = zlib.crc32(a)
        crcs.append(crc - (1 << 32) if crc >= (1 << 31) else crc)
    print(','.join(str(c) for c in crcs))

main(sys.argv[1])
