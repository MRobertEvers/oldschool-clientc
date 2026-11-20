#!/usr/bin/env python3
"""Which client triggers does a cache bind any clientscript to?

A client trigger (game/rs_client_trigger.h) names no table: a script is bound
to it by its GROUP NAME in the clientscript index (12), whose name hash is the
djb2 of the decimal string of

    subject * 256 + trigger            this npc / this loc type
    trigger - category * 256 - 0x300   any of this category
    trigger - 0x200                    any subject at all

This reads index 12's reference table straight out of a dat2 cache and tries
every form for each trigger asked about (subjects 0..69999, categories
0..4095), so "the client never fires trigger N" can be checked against what a
cache would run if it did.

    tools/client_trigger_scan.py cache.osrs242                 # 35 36 37 41 42
    tools/client_trigger_scan.py cache.osrs242 --expect-none 36 41 42

With --expect-none it exits 1 when any of the named triggers has a script:
the defines for NPC_DEL, PLAYER_ADD and PLAYER_DEL were deleted (fault E10,
2026-11-19) because neither cache.osrs239 nor cache.osrs242 binds one, and
this is how a cache upgrade finds out if that changes.
"""
import bz2
import gzip
import struct
import sys


def read_group(cache, index_file, group):
    dat = open(cache + '/main_file_cache.dat2', 'rb')
    idx = open(cache + '/main_file_cache.idx%d' % index_file, 'rb').read()
    entry = idx[group * 6:group * 6 + 6]
    size = int.from_bytes(entry[0:3], 'big')
    sector = int.from_bytes(entry[3:6], 'big')
    out = b''
    while len(out) < size:
        dat.seek(sector * 520)
        block = dat.read(520)
        out += block[8:8 + min(512, size - len(out))]
        sector = int.from_bytes(block[4:7], 'big')
    return out


def unpack(container):
    kind = container[0]
    clen = struct.unpack('>i', container[1:5])[0]
    if kind == 0:
        return container[5:5 + clen]
    data = container[9:9 + clen]
    if kind == 1:
        return bz2.decompress(b'BZh1' + data)
    return gzip.decompress(data)


def group_names(cache, index):
    ref = unpack(read_group(cache, 255, index))
    pos = [0]

    def u8():
        v = ref[pos[0]]
        pos[0] += 1
        return v

    def count(proto):
        if proto >= 7 and ref[pos[0]] & 0x80:
            v = struct.unpack('>I', ref[pos[0]:pos[0] + 4])[0] & 0x7fffffff
            pos[0] += 4
        else:
            v = struct.unpack('>H', ref[pos[0]:pos[0] + 2])[0]
            pos[0] += 2
        return v

    proto = u8()
    if proto >= 6:
        pos[0] += 4
    flags = u8()
    n = count(proto)
    ids, acc = [], 0
    for _ in range(n):
        acc += count(proto)
        ids.append(acc)
    if not flags & 1:
        return {}
    names = {}
    for gid in ids:
        names[struct.unpack('>i', ref[pos[0]:pos[0] + 4])[0]] = gid
        pos[0] += 4
    return names


def djb2(text):
    h = 0
    for ch in text.encode():
        h = (h * 31 + ch) & 0xffffffff
    return h - (1 << 32) if h & 0x80000000 else h


def bound(names, trigger):
    found = []
    h = djb2(str(trigger - 0x200))
    if h in names:
        found.append(('any', 0, names[h]))
    for cat in range(4096):
        h = djb2(str(trigger - cat * 256 - 0x300))
        if h in names:
            found.append(('category', cat, names[h]))
    for sub in range(70000):
        h = djb2(str(sub * 256 + trigger))
        if h in names:
            found.append(('subject', sub, names[h]))
    return found


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cache = argv[1]
    expect_none = '--expect-none' in argv
    triggers = [int(a) for a in argv[2:] if a != '--expect-none'] or [35, 36, 37, 41, 42]
    names = group_names(cache, 12)
    bad = 0
    for trigger in triggers:
        found = bound(names, trigger)
        print('trigger %d: %d script(s)%s' % (
            trigger, len(found), (' e.g. ' + ', '.join('%s %d -> %d' % f for f in found[:4])) if found else ''))
        if expect_none and found:
            bad += 1
    if expect_none:
        print('client_trigger_scan: %s' % ('FAIL' if bad else 'PASS (none bound)'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
