#!/usr/bin/env python3
"""Fill in npc and loc examine text from the dat1 cache, matched by name.

Why this exists
---------------

`cache.osrs239` states examine text for **items only**. Verified by enumerating
every config group in it: group 10 (obj) carries 13,759 examine strings, group 9
(npc, 16,292 records) carries none, and group 6 (loc, 62,194 records) carries
none. The reference client agrees -- `ItemComposition` is the one type that
decodes the string (opcode 3, exposed to CS2 as `oc_desc`, opcode 4218), and the
four Examine menu rows (1002 npc, 1003 item, 1004 ground obj, 1013 loc) each send
a packet and print whatever the server answers. Jagex retired the npc/loc opcode
in 2006 and moved the text server-side.

This client resolves Examine locally off the cache record, so an npc or a loc had
nothing to print and fell back to "It's a <name>.". The text has to come from
somewhere, and the most defensible source in this tree is the dat1 cache it
already ships: `cache254.lostcity` states 1,161 npc and 2,217 loc examines in
Jagex's own wording, under the same opcode 3 this pipeline now carries through to
the client.

Ids are useless across that gap -- dat1 npc 1 is "Man", osrs239 npc 1 is
"Molanisk" -- so the join is the **name**, and only where it is unambiguous: a
name whose dat1 records disagree about the examine text is skipped rather than
guessed at.

Output
------

`configs/examine.npc` and `configs/examine.loc`, rank-0 overlay files stating
nothing but `desc=`. Deliberately not appended to `configs/all.npc`: that file is
the machine export and a re-unpack owns it (see the exporter-owns-generated-
configs rule). Deliberately not written under `server/scripts` either -- a `.npc`
block there is a *server* overlay and would hand the server content it has no
business owning, for a field only the client reads.

Both files are rewritten whole on every run, so re-running after a cache refresh
is the update path.

Usage:
  tools/import_examine.py [--dat1 cache254.lostcity] [--tree OSRS-Content/osrs239-content]
  tools/import_examine.py --report          # coverage only, writes nothing
"""

import argparse
import bz2
import os
import sys

SECTOR = 520


# --------------------------------------------------------------------------
# dat1 container
# --------------------------------------------------------------------------


class Dat1Cache:
    def __init__(self, path):
        self.path = path
        with open(os.path.join(path, "main_file_cache.dat"), "rb") as f:
            self.dat = f.read()

    def read(self, index, group):
        idx = os.path.join(self.path, "main_file_cache.idx%d" % index)
        with open(idx, "rb") as f:
            f.seek(group * 6)
            entry = f.read(6)
        if len(entry) < 6:
            return None
        length = int.from_bytes(entry[0:3], "big")
        sector = int.from_bytes(entry[3:6], "big")
        if length == 0 or sector == 0:
            return None
        out = bytearray()
        remaining = length
        while remaining > 0:
            off = sector * SECTOR
            header = self.dat[off : off + 8]
            sector = int.from_bytes(header[4:7], "big")
            take = min(SECTOR - 8, remaining)
            out += self.dat[off + 8 : off + 8 + take]
            remaining -= take
        return bytes(out)


def jag_archive(blob):
    """A dat1 .jag archive -> {name hash: entry bytes}."""
    unpacked = int.from_bytes(blob[0:3], "big")
    packed = int.from_bytes(blob[3:6], "big")
    data = blob[6 : 6 + packed]
    whole = unpacked != packed
    if whole:
        data = bz2.decompress(b"BZh1" + data)
    count = int.from_bytes(data[0:2], "big")
    pos = 2
    entries = []
    for _ in range(count):
        name_hash = int.from_bytes(data[pos : pos + 4], "big")
        entry_unpacked = int.from_bytes(data[pos + 4 : pos + 7], "big")
        entry_packed = int.from_bytes(data[pos + 7 : pos + 10], "big")
        pos += 10
        entries.append((name_hash, entry_unpacked, entry_packed))
    out = {}
    for name_hash, _entry_unpacked, entry_packed in entries:
        chunk = data[pos : pos + entry_packed]
        pos += entry_packed
        if not whole:
            chunk = bz2.decompress(b"BZh1" + chunk)
        out[name_hash] = chunk
    return out


def jag_hash(name):
    value = 0
    for ch in name.upper():
        value = (value * 61 + ord(ch) - 32) & 0xFFFFFFFF
    return value


class Reader:
    def __init__(self, data):
        self.d = data
        self.p = 0

    def g1(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def g2(self):
        v = int.from_bytes(self.d[self.p : self.p + 2], "big")
        self.p += 2
        return v

    def gstr(self):
        end = self.d.index(10, self.p)
        v = self.d[self.p : end].decode("latin-1")
        self.p = end + 1
        return v


def record_offsets(idx_data):
    """dat1's `<type>.idx`: a record count, then each record's size in order."""
    r = Reader(idx_data)
    count = r.g2()
    offsets = []
    pos = 2
    for _ in range(count):
        size = r.g2()
        offsets.append(pos)
        pos += size
    return offsets


def _skip_npc(r, code):
    """One dat1 npc opcode, transcribed from Client-TS/src/config/NpcType.ts."""
    if code == 1:
        for _ in range(r.g1()):
            r.g2()
    elif code == 12:
        r.g1()
    elif code in (13, 14, 90, 91, 92, 95, 97, 98, 102, 103):
        r.g2()
    elif code == 17:
        for _ in range(4):
            r.g2()
    elif 30 <= code < 40:
        r.gstr()
    elif code == 40:
        for _ in range(r.g1()):
            r.g2()
            r.g2()
    elif code == 60:
        for _ in range(r.g1()):
            r.g2()
    elif code in (93, 99):
        pass
    elif code in (100, 101):
        r.g1()
    else:
        return False
    return True


def _skip_loc(r, code):
    """One dat1 loc opcode, transcribed from Client-TS/src/config/LocType.ts."""
    if code == 1:
        for _ in range(r.g1()):
            r.g2()
            r.g1()
    elif code == 5:
        for _ in range(r.g1()):
            r.g2()
    elif code in (14, 15, 19, 28, 29, 39, 69, 75):
        r.g1()
    elif code in (24, 60, 65, 66, 67, 68, 70, 71, 72):
        r.g2()
    elif code in (17, 18, 21, 22, 23, 62, 64, 73, 74):
        pass
    elif 30 <= code < 39:
        r.gstr()
    elif code == 40:
        for _ in range(r.g1()):
            r.g2()
            r.g2()
    else:
        return False
    return True


def parse_dat1(archive, kind):
    """(name, desc) per record of `kind`, straight off the dat1 config archive.

    The whole opcode table is walked, not just the two strings this tool wants:
    a dat1 record states its opcodes in *cache* order, not in the order they are
    listed, and real records lead with opcode 95 (combat level) well before the
    name. Skipping an opcode by the wrong width lands the next `gstr` in the
    middle of a payload, so the widths are transcribed from the in-tree
    reference client rather than guessed. An opcode neither table names ends
    that record's walk -- its payload width is unknown, so nothing after it can
    be trusted -- and whatever strings were already read still count.
    """
    dat = archive[jag_hash(kind + ".dat")]
    offsets = record_offsets(archive[jag_hash(kind + ".idx")])
    step = _skip_npc if kind == "npc" else _skip_loc
    out = {}
    for record_id, offset in enumerate(offsets):
        r = Reader(dat)
        r.p = offset
        name = desc = None
        while True:
            try:
                code = r.g1()
                if code == 0:
                    break
                if code == 2:
                    name = r.gstr()
                elif code == 3:
                    desc = r.gstr()
                elif not step(r, code):
                    break
            except (IndexError, ValueError):
                break
        if name and desc:
            out[record_id] = (name, desc)
    return out


def by_name(records):
    """name -> examine, dropping every name its records disagree about.

    "Man" is stated three times with one wording and is safe; a name whose
    records carry two different sentences cannot be resolved by name alone, and
    guessing which one an osrs239 record of that name wants is exactly the kind
    of plausible-but-wrong data this is meant to avoid.
    """
    seen = {}
    for name, desc in records.values():
        seen.setdefault(name.strip().lower(), {}).setdefault(desc, 0)
        seen[name.strip().lower()][desc] += 1
    resolved = {}
    ambiguous = 0
    for key, descs in seen.items():
        if len(descs) == 1:
            resolved[key] = next(iter(descs))
        else:
            ambiguous += 1
    return resolved, ambiguous


# --------------------------------------------------------------------------
# the wiki corpus
# --------------------------------------------------------------------------


WIKI_MARKUP = (b"{{", b"<br", b"[[", b"''", b"</", b"|")


def wiki_examines(directory):
    """name -> examine, from the `|examine =` line of the monster infoboxes.

    Second source, and for an npc the better one where both speak: the corpus is
    current OldSchool wording while dat1's is 2004's. Held to the same standard
    as the dat1 join -- a page whose infobox states several different examines
    (the `examine1`/`examine2` variant forms) is skipped, and so is any value
    carrying wiki markup, because a `{{*}}` or a `<br/>` in a chat line is not
    text the client can print.

    Disambiguated titles ("Warrior _Rellekka_") are skipped outright: the whole
    reason they exist is that the bare name belongs to several npcs, which is
    the case this cannot resolve.
    """
    if not os.path.isdir(directory):
        return {}, 0
    found = {}
    skipped = 0
    for entry in sorted(os.listdir(directory)):
        if not entry.endswith(".wikitext"):
            continue
        title = entry[: -len(".wikitext")]
        if "_" in title:
            skipped += 1
            continue
        with open(os.path.join(directory, entry), "rb") as f:
            text = f.read()
        values = set()
        for line in text.split(b"\n"):
            stripped = line.strip()
            if not stripped.startswith(b"|examine"):
                continue
            _, sep, value = stripped.partition(b"=")
            if sep:
                values.add(value.strip())
        if len(values) != 1:
            skipped += 1
            continue
        value = values.pop()
        if any(m in value for m in WIKI_MARKUP) or not value:
            skipped += 1
            continue
        found[title.strip().lower()] = value.decode("latin-1")
    return found, skipped


# --------------------------------------------------------------------------
# the content tree
# --------------------------------------------------------------------------


def read_blocks(path):
    """[(symbol, {key: value})] for a cachepack config text file."""
    blocks = []
    symbol = None
    fields = {}
    with open(path, "rb") as f:
        for raw in f:
            line = raw.decode("latin-1").rstrip("\r\n")
            if line.startswith("[") and line.endswith("]"):
                if symbol is not None:
                    blocks.append((symbol, fields))
                symbol = line[1:-1]
                fields = {}
            elif symbol is not None and "=" in line and not line.startswith("//"):
                key, _, value = line.partition("=")
                if key not in fields:
                    fields[key] = value
    if symbol is not None:
        blocks.append((symbol, fields))
    return blocks


HEADER = """// Examine text, imported by tools/import_examine.py -- REGENERATED WHOLE.
//
// `cache.osrs239` states examine text for items and for nothing else: Jagex
// retired the npc/loc opcode in 2006 and OldSchool sends those two from the
// server. This client answers Examine off the cache record, so the text is
// carried here instead, under the same config opcode 3 the field has always
// had, sourced from %s and joined by name.
//
// %d of %d %s records matched. Edit tools/import_examine.py, not this file.
"""


def escape(value):
    """cachepack's text escaping, the write half of `cp_unescape`."""
    return value.replace("\\", "\\\\").replace("\n", "\\n").replace("\r", "\\r")


def emit(path, header, rows):
    with open(path, "wb") as f:
        f.write(header.encode("latin-1"))
        for symbol, desc in rows:
            f.write(("\n[%s]\ndesc=%s\n" % (symbol, escape(desc))).encode("latin-1"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dat1", default="cache254.lostcity")
    ap.add_argument("--tree", default="OSRS-Content/osrs239-content")
    ap.add_argument("--wiki", default=None, help="monster wikitext dir (default: <tree>/wiki/monsters)")
    ap.add_argument("--report", action="store_true", help="print coverage, write nothing")
    args = ap.parse_args()

    archive = jag_archive(Dat1Cache(args.dat1).read(0, 2))
    wiki_dir = args.wiki or os.path.join(args.tree, "wiki", "monsters")
    wiki, wiki_skipped = wiki_examines(wiki_dir)
    print("wiki: %d usable examines in %s (%d pages skipped)" % (len(wiki), wiki_dir, wiki_skipped))

    total_written = 0
    for kind in ("npc", "loc"):
        source = parse_dat1(archive, kind)
        lookup, ambiguous = by_name(source)
        sources = "`%s`" % args.dat1
        if kind == "npc":
            sources = "`%s` and the wiki corpus" % args.dat1

        target = os.path.join(args.tree, "configs", "all.%s" % kind)
        blocks = read_blocks(target)

        rows = []
        already = 0
        from_wiki = 0
        non_ascii = 0
        for symbol, fields in blocks:
            if fields.get("desc"):
                already += 1
                continue
            name = fields.get("name", "").strip().lower()
            if not name or name == "null":
                continue
            # The wiki first for an npc: same field, later wording.
            desc = wiki.get(name) if kind == "npc" else None
            if desc:
                from_wiki += 1
            else:
                desc = lookup.get(name)
            if desc and not desc.isascii():
                non_ascii += 1
                desc = None
            if desc:
                rows.append((symbol, desc))

        print(
            "%s: %d dat1 examines over %d names (%d ambiguous, skipped); "
            "%d of %d records matched (%d from the wiki), %d already stated one, "
            "%d dropped as non-ASCII"
            % (
                kind,
                len(source),
                len(lookup) + ambiguous,
                ambiguous,
                len(rows),
                len(blocks),
                from_wiki,
                already,
                non_ascii,
            )
        )
        if args.report:
            continue
        out = os.path.join(args.tree, "configs", "examine.%s" % kind)
        emit(out, HEADER % (sources, len(rows), len(blocks), kind), rows)
        print("  wrote %s" % out)
        total_written += len(rows)

    if not args.report:
        print("%d records now state an examine that did not before" % total_written)
    return 0


if __name__ == "__main__":
    sys.exit(main())
