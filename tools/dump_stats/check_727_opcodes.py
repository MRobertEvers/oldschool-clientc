#!/usr/bin/env python3
"""
Check the rev-727 npc/obj opcode table against every record in the cache.

`3rd/rscache` had no profile for rev 727, and its npc/obj codecs stop at the
first opcode they do not know, because an unknown opcode has an unknown payload
length and reading past it fills real fields with payload bytes. Deriving the
missing lengths by fitting them to the data does not work — with all 256 opcodes
free the parser can "explain" almost any byte stream, and a fit that scores 96%
still rewrites opcode 13 from a u16 animation id into a single byte. Length
alone is not identification.

So the table below is transcribed from a source, not fitted:
`~/Documents/git_repos/rsmv/src/opcodes/{npcs,items}.jsonc`, which states each
opcode's payload with `buildnr` gates across the whole RS2/RS3 lineage. The
gates that bite at build 727 are:

  - npc opcode 0x01 (models) and 0x3C (head models) hold **varuint** ids from
    build 669, not u16. A varuint is 2 bytes when the top bit of the first is
    clear and 4 when it is set, so a model list reads at the right length only
    by accident, and every field after it lands wrong. This is what made most
    727 npc records stop mid-record under the 643 codec.
  - obj model fields (`item_modelid`) are varuint from build 670, same story.
  - obj opcodes 0x17/0x19 lost their trailing type byte at build 502.

What makes this checkable rather than plausible: every config record ends with
opcode 0 at exactly its file length, so a wrong length anywhere makes a record
miss its terminator. The table is right only if 100% of records land exactly.
That is the number this script prints.

Run:
  python3 check_727_opcodes.py <raw_dir> npc
  python3 check_727_opcodes.py <raw_dir> obj

`<raw_dir>` is what `dump_stats --raw-dir` wrote.
"""

import sys
import os
from collections import defaultdict

BUILD = 727


# ---- primitive readers -----------------------------------------------------
#
# Each returns the new position, or -1 when the payload does not fit. Only
# lengths matter here; this file validates layout, not meaning.


def _u(n):
    def f(d, p, e):
        return p + n if p + n <= e else -1
    return f


def _cstr(d, p, e):
    i = d.find(0, p, e)
    return i + 1 if i >= 0 else -1


def _varushort(d, p, e):
    if p >= e:
        return -1
    return p + (1 if (d[p] & 0x80) == 0 else 2)


def _varuint(d, p, e):
    if p + 2 > e:
        return -1
    return p + (2 if (d[p] & 0x80) == 0 else 4)


def _seq(*parts):
    def f(d, p, e):
        for part in parts:
            p = part(d, p, e)
            if p < 0:
                return -1
        return p
    return f


def _array(elem, count=_varushort):
    """`["array", elem]` defaults its count to a varushort in rsmv; an explicit
    count type is passed for the few that state one."""
    def f(d, p, e):
        start = p
        p = count(d, p, e)
        if p < 0:
            return -1
        n = _value(d, start, count)
        for _ in range(n):
            p = elem(d, p, e)
            if p < 0:
                return -1
        return p
    return f


def _value(d, p, kind):
    """The numeric value of a count field, needed to know how many elements
    follow."""
    if kind is _varushort:
        return d[p] if (d[p] & 0x80) == 0 else (((d[p] & 0x7F) << 8) | d[p + 1])
    return d[p]  # ubyte counts


def _ubyte_count(d, p, e):
    return p + 1 if p < e else -1


def _extrasmap(d, p, e):
    """Opcode 0xF9. `["array","ubyte",[struct type:ubyte, prop:utribyte,
    int when type==0, string when type==1]]`."""
    if p >= e:
        return -1
    n = d[p]
    p += 1
    for _ in range(n):
        if p + 4 > e:
            return -1
        kind = d[p]
        p += 4                      # type byte + 3-byte property id
        if kind == 1:
            p = _cstr(d, p, e)
            if p < 0:
                return -1
        else:
            p += 4
            if p > e:
                return -1
    return p


NONE = _u(0)
U8 = _u(1)
U16 = _u(2)
U24 = _u(3)
U32 = _u(4)
VARUSHORT = _varushort
VARUINT = _varuint
VARSHORT = _varushort          # same width rule, different sign handling
VARNULLINT = _varuint
STR = _cstr

# Build-gated aliases, resolved once for BUILD.
MODELID = VARUINT if BUILD >= 669 else U16      # npc models / head models
ITEM_MODELID = VARUINT if BUILD >= 670 else U16  # every obj model field


# ---- npc, from rsmv src/opcodes/npcs.jsonc ---------------------------------

NPC = {
    0x01: _array(MODELID),                      # models
    0x02: STR,                                  # name
    0x03: STR,                                  # examine (pre-2006)
    0x08: U8,
    0x0B: U8,
    0x0C: U8,                                   # boundSize
    0x0D: U16,
    0x0E: U16,
    0x11: _seq(U16, U16, U16, U16),
    0x1E: STR, 0x1F: STR, 0x20: STR, 0x21: STR, 0x22: STR,   # actions
    0x28: _array(_seq(U16, U16)),               # colour replacements
    0x29: _array(_seq(U16, U16)),               # material replacements
    0x2A: _array(U8),                           # recolour palette
    0x2C: U16,
    0x2D: U16,
    0x3C: _array(MODELID),                      # head (chathead) models
    0x5D: NONE,                                 # drawMapDot = false
    0x5F: U16,                                  # combat level
    0x61: U16,                                  # scaleXZ
    0x62: U16,                                  # scaleY
    0x63: NONE,
    0x64: U8,                                   # ambience
    0x65: U8,                                   # model contrast
    0x66: U16,                                  # head icon
    0x67: U16,
    0x6A: _seq(U32, _array(U16, _ubyte_count), U32),         # morphs 1
    0x6B: NONE,
    0x6D: NONE,                                 # slow walk
    0x6F: NONE,                                 # animate idle
    0x71: _seq(U16, U16),                       # shadow colours
    0x72: _seq(U8, U8),                         # shadow alpha
    0x73: _seq(U8, U8),
    0x76: _seq(U32, U16, _array(U16, _ubyte_count), U32),    # morphs 2
    0x77: U8,                                   # movement capabilities
    0x78: _seq(U16, U16, U16, U8),
    0x79: _array(U32),                          # per-model translations
    0x7A: U16,
    0x7B: U16,                                  # icon height
    0x7D: U8,                                   # respawn direction
    0x7F: U16,                                  # animation group (BasType)
    0x80: U8,                                   # movement type
    0x86: _seq(U16, U16, U16, U16, U8),         # ambient sound
    0x87: _seq(U8, U16),                        # cursor op + cursor
    0x88: _seq(U8, U16),
    0x89: U16,                                  # attack cursor
    0x8A: VARSHORT,                             # army icon
    0x8C: U8,
    0x8D: NONE,
    0x8E: U16,                                  # map function
    0x8F: NONE,
    0x96: STR, 0x97: STR, 0x98: STR, 0x99: STR, 0x9A: STR,   # members actions
    0x9B: _seq(U8, U8, U8, U8),
    0x9E: NONE,
    0x9F: NONE,
    0xA0: _array(U16),                          # quests
    0xA2: NONE,
    0xA3: U8,
    0xA4: _seq(U16, U16),
    0xA5: U8,
    0xA8: U8,
    0xA9: NONE,
    0xAA: U16, 0xAB: U16, 0xAC: U16, 0xAD: U16, 0xAE: U16, 0xAF: U16,
    0xB2: NONE,
    0xB3: _seq(VARSHORT, VARSHORT, VARSHORT, VARSHORT, VARSHORT, VARSHORT),
    0xB4: U8,
    0xB5: _seq(U16, U8),
    0xB6: NONE,
    0xB7: U8,
    0xB8: U8,
    0xB9: NONE,
    0xDB: U8,
    0xF9: _extrasmap,                           # params
    0xFD: U8,
}


# ---- obj, from rsmv src/opcodes/items.jsonc --------------------------------

OBJ = {
    0x01: ITEM_MODELID,                         # inventory model
    0x02: STR,                                  # name
    0x03: STR,                                  # buff effect
    0x04: U16,                                  # model zoom
    0x05: U16, 0x06: U16,                       # rotation x / y
    0x07: U16, 0x08: U16,                       # 2d translate
    0x0A: U16,
    0x0B: NONE,                                 # stackable
    0x0C: U32,                                  # value
    0x0D: U8,                                   # equip slot
    0x0E: U8,                                   # equip id
    0x0F: NONE,
    0x10: NONE,                                 # members
    0x12: U16,                                  # multi stack size
    0x17: ITEM_MODELID,                         # male model 0 (type byte gone at 502)
    0x18: ITEM_MODELID,                         # male model 1
    0x19: ITEM_MODELID,                         # female model 0
    0x1A: ITEM_MODELID,                         # female model 1
    0x1B: U8,
    0x1E: STR, 0x1F: STR, 0x20: STR, 0x21: STR, 0x22: STR,   # ground actions
    0x23: STR, 0x24: STR, 0x25: STR, 0x26: STR, 0x27: STR,   # widget actions
    0x28: _array(_seq(U16, U16)),               # colour replacements
    0x29: _array(_seq(U16, U16)),               # material replacements
    0x2A: _array(_seq(U8, U8)),                 # recolour palette
    0x2B: U32,                                  # name colour
    0x2C: U16,
    0x2D: U16,
    0x41: NONE,                                 # tradeable
    0x45: U32,                                  # buy limit
    0x4E: ITEM_MODELID, 0x4F: ITEM_MODELID,     # male/female model 2
    0x5A: ITEM_MODELID, 0x5B: ITEM_MODELID,     # head models
    0x5C: ITEM_MODELID, 0x5D: ITEM_MODELID,
    0x5E: U16,                                  # category
    0x5F: U16,                                  # rotation z
    0x60: U8,                                   # dummy item
    0x61: U16,                                  # noted id
    0x62: U16,                                  # noted template
    0x6E: U16, 0x6F: U16, 0x70: U16,            # resize x / y / z
    0x71: U8,                                   # ambient
    0x72: U8,                                   # contrast
    0x73: U8,                                   # team
    0x79: U16,                                  # loan id
    0x7A: U16,                                  # loan template
    0x7D: _seq(U8, U8, U8),                     # male translate
    0x7E: _seq(U8, U8, U8),                     # female translate
    0x7F: _seq(U8, U16), 0x80: _seq(U8, U16),
    0x81: _seq(U8, U16), 0x82: _seq(U8, U16),
    # rsmv states this as an array of (ubyte, ushort); at build 727 the element
    # is a bare ushort. obj 271 ends `84 01 00 0f 00` in a 59-byte record: the
    # trailing 0 is the terminator only if the one element is two bytes, and
    # without that the record has no terminator at all. 333 records say so.
    0x84: _array(U16),                          # quests
    0x86: U8,                                   # pick size shift
    0x8B: U16,                                  # bind link
    0x8C: U16,                                  # bind template
    0x8E: U16, 0x8F: U16, 0x90: U16, 0x91: U16, 0x92: U16,   # ground cursors
    0x96: U16, 0x97: U16, 0x98: U16, 0x99: U16, 0x9A: U16,   # widget cursors
    0x9C: NONE,                                 # dummy
    0x9D: NONE,                                 # randomize ground position
    0xA1: U16, 0xA2: U16, 0xA3: U16,            # combine info
    0xA4: STR,                                  # combine shard name
    0xA5: NONE,                                 # never stackable
    0xA7: NONE, 0xA8: NONE, 0xB2: NONE,
    0xB5: _seq(U32, U32),                       # big value
    0xF9: _extrasmap,                           # params
}

for _op in range(0x64, 0x6E):                   # stack info 0..9
    OBJ[_op] = _seq(U16, U16)


def load_records(raw_dir, what):
    blob = open(os.path.join(raw_dir, what + ".bin"), "rb").read()
    out = []
    for line in open(os.path.join(raw_dir, what + ".idx")):
        rid, off, size = (int(x) for x in line.split())
        if size > 0:
            out.append((rid, blob[off:off + size]))
    return out


def check(records, table):
    ok = 0
    fail = defaultdict(int)
    examples = defaultdict(list)
    used = defaultdict(int)

    for rid, data in records:
        n = len(data)
        pos = 0
        why = None
        while True:
            if pos >= n:
                why = ("no terminator", -1)
                break
            op = data[pos]
            pos += 1
            if op == 0:
                if pos != n:
                    why = ("terminator not at end", -2)
                break
            fn = table.get(op)
            if fn is None:
                why = ("unknown opcode", op)
                break
            nxt = fn(data, pos, n)
            if nxt < 0:
                why = ("payload overruns record", op)
                break
            used[op] += 1
            pos = nxt
        if why is None:
            ok += 1
        else:
            fail[why] += 1
            if len(examples[why]) < 6:
                examples[why].append(rid)

    return ok, fail, examples, used


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    raw_dir, family = sys.argv[1], sys.argv[2]
    table = NPC if family == "npc" else OBJ
    records = load_records(raw_dir, family)

    ok, fail, examples, used = check(records, table)
    total = len(records)
    print(f"{family}: {ok}/{total} records consume exactly "
          f"({100.0 * ok / total:.2f}%)")

    if fail:
        print("  failures:")
        for (why, op), n in sorted(fail.items(), key=lambda kv: -kv[1]):
            label = f"opcode 0x{op:02X} ({op})" if op >= 0 else ""
            print(f"    {n:6d}  {why} {label}  e.g. ids {examples[(why, op)]}")

    print(f"  opcodes seen: {len(used)}")
    unused = [op for op in sorted(table) if op not in used]
    if unused:
        print("  in the table but absent from this cache: "
              + " ".join(f"0x{o:02X}" for o in unused))
    return 0 if not fail else 2


if __name__ == "__main__":
    sys.exit(main())
