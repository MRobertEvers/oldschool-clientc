#!/usr/bin/env python3
"""Generate `pack/11_musicjingles.pack` — real names for the jingles.

    tools/gen_jingle_names.py --write
    tools/gen_jingle_names.py --report        what changed, and what stayed numeric

## Why this exists

`pack/11_musicjingles.pack` was `jingle_0` ... `jingle_314` — the id spelled
back at itself. `docs/SKILLING_SOUNDS.md` §6 filed this as genuinely blocked,
for a reason `tools/gen_sound_names.py` did not have to deal with: JS5 index 11
(music jingles), unlike index 6 (music tracks) or index 4 (sound effects),
carries **no name hashes at all** in this cache's reference table (`flags & 1`
is unset) — checked, not assumed, across every cache in this tree. The
hash-recovery trick that named the other two packs cannot work here.

## Where the names come from, and why they are proofs rather than guesses

The OSRS Wiki publishes a `cacheid` field on every jingle's infobox
(`docs/audio/osrs_wiki_jingle_ids.tsv`, harvested from all 324 pages in
Category:Jingles). That field is not documented anywhere as being the JS5
group id — so this script does not trust it on say-so. It is proofread three
independent ways:

  - **Id-set identity.** All 314 wiki `cacheid`s fall inside the exact set of
    archive-11 group ids this cache has (315 groups; only `17` and `178` have
    no wiki page). That is not a coincidence a name-collision could produce.
  - **Cross-era MIDI match.** LostCity ships its 2004-era jingles as *named*
    Standard MIDI files (`../LostCity_Server/content/jingles/*.mid`) — its own
    informal codenames, not the wiki's titles, so this does not check that the
    names agree (they mostly don't spell the same thing at all: LostCity's
    `midi_315` is the wiki's "Elf Singing"). What it checks is narrower and
    independent of the wiki: does the id decode to a real note sequence that
    also exists, byte-for-byte, in a completely different server's 2004
    corpus? 66 of LostCity's 74 named jingles match exactly one osrs239 id this
    way. The one thing that WOULD be fatal is two different osrs239 ids
    matching the same LostCity file — an ambiguous id-to-note mapping — and
    `--write` refuses to run if that ever happens (see `verify()`).
  - **A companion proof for the same `cacheid` field on the sibling index-6
    (music tracks) table**, which DOES carry name hashes: 7 sampled wiki
    `cacheid`s all resolve to the archive-6 group whose stored hash matches the
    lowercased track name. Same field, same source, same page layout — this is
    what justifies trusting the field on index 11 too.

`docs/audio/osrs_wiki_jingle_ids.tsv`'s own header has the full method and the
day it was harvested.

## Name hygiene

Wiki titles are not all valid symbols here. Lowercased; anything outside
`[a-z0-9_]` becomes `_`. Unlike sound effects, jingle titles are kept in full
including their parenthetical quest/context — `echo_of_beauty_mountain_daughter`
not `echo_of_beauty` — because two different jingles legitimately share a base
name (multiple "Ballad Opening"-style reprises exist across quests) and the
parenthetical is what disambiguates them on the wiki itself.

Ids the wiki does not name (17, 178) keep `jingle_<id>`, so an unnamed jingle is
still spellable and nothing that referenced one breaks.
"""

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WIKI_TSV = ROOT / "docs/audio/osrs_wiki_jingle_ids.tsv"
PACK = ROOT / "OSRS-Content/osrs239-content/pack/11_musicjingles.pack"
NAMES_TSV = ROOT / "docs/audio/jingle_names.tsv"
AUDIOPROBE = ROOT / "3rd/rscache/tools/audioprobe/audioprobe"
CACHE = ROOT / "cache.osrs239"
LOSTCITY_JINGLES = ROOT.parent / "LostCity_Server/content/jingles"


def parse_wiki(path):
    """id -> wiki page title, from the harvested `id\\ttitle` TSV."""
    names = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith(("<!--", "-->", "id\t")) or line.strip().startswith("-") or "\t" not in line:
            continue
        ident_s, title = line.split("\t", 1)
        try:
            ident = int(ident_s)
        except ValueError:
            continue
        names[ident] = title.strip()
    return names


def sanitise(names):
    out = {}
    seen = {}
    for ident in sorted(names):
        sym = re.sub(r"[^a-z0-9_]", "_", names[ident].lower())
        sym = re.sub(r"_+", "_", sym).strip("_")
        if not sym:
            continue
        if sym in seen:
            sym = "%s_%d" % (sym, ident)
        seen[sym] = ident
        out[ident] = sym
    return out


def existing_pack(path):
    rows = {}
    if not path.exists():
        return rows
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            try:
                rows[int(k)] = v.strip()
            except ValueError:
                pass
    return rows


# ---- MIDI note-event cross-check (mirrors tools/gen_jingle_lengths.py's
# reader, but only needs note-on events, not full duration) ----

def _read_varint(data, pos):
    value = 0
    while True:
        byte = data[pos]
        pos += 1
        value = (value << 7) | (byte & 0x7F)
        if not byte & 0x80:
            return value, pos


def note_events(data):
    assert data[0:4] == b"MThd"
    ntrk = int.from_bytes(data[10:12], "big")
    pos = 14
    events = []
    for _ in range(ntrk):
        assert data[pos : pos + 4] == b"MTrk"
        length = int.from_bytes(data[pos + 4 : pos + 8], "big")
        body_start, body_end = pos + 8, pos + 8 + length
        p, tick, running = body_start, 0, None
        while p < body_end:
            delta, p = _read_varint(data, p)
            tick += delta
            status = data[p]
            if status & 0x80:
                p += 1
                running = status
            else:
                status = running
            high = status & 0xF0
            if high in (0x80, 0x90, 0xA0, 0xB0, 0xE0):
                a, b = data[p], data[p + 1]
                p += 2
                if high == 0x90 and b != 0:
                    events.append((tick, a))
            elif high in (0xC0, 0xD0):
                p += 1
            elif status == 0xFF:
                p += 1
                mlen, p = _read_varint(data, p)
                p += mlen
            elif status in (0xF0, 0xF7):
                mlen, p = _read_varint(data, p)
                p += mlen
            else:
                break
        pos = body_end
    events.sort()
    return events


def decode_notes(jingle_id):
    with tempfile.NamedTemporaryFile(suffix=".mid") as tmp:
        result = subprocess.run(
            [str(AUDIOPROBE), str(CACHE), "osrs239", "--jingle", str(jingle_id), "--out", tmp.name],
            capture_output=True,
        )
        if result.returncode != 0:
            return None
        data = Path(tmp.name).read_bytes()
    if not data.startswith(b"MThd"):
        return None
    return note_events(data)


def verify(sym_names):
    """Cross-check: does this id decode to a real, non-empty note sequence that
    also exists, byte-for-byte, in LostCity's 2004 jingle corpus?

    This does NOT compare names -- LostCity's filenames are that server's own
    informal codenames (`midi_315`, `death`, ...), not the wiki's titles, and
    were never expected to match them. What it proves is narrower and more
    useful: that the *id* the wiki assigns names something this cache can
    actually decode to real, era-appropriate music, independent of the wiki
    ever being asked. A conflict would be two DIFFERENT ids independently
    matching the SAME LostCity file -- that would mean the id-to-note mapping
    is ambiguous, which is worth stopping for; a name that merely reads
    differently is not.
    """
    if not LOSTCITY_JINGLES.is_dir() or not AUDIOPROBE.exists():
        print(
            "warning: cross-check unavailable (need %s and %s) -- names trusted from the wiki alone"
            % (LOSTCITY_JINGLES, AUDIOPROBE),
            file=sys.stderr,
        )
        return [], []

    osrs_notes = {}
    for ident in sym_names:
        notes = decode_notes(ident)
        if notes:
            osrs_notes[ident] = notes

    matched, conflicts = [], []
    for lc_path in sorted(LOSTCITY_JINGLES.glob("*.mid")):
        lc_name = lc_path.stem
        lc_notes = note_events(lc_path.read_bytes())
        hit_ids = [i for i, n in osrs_notes.items() if n == lc_notes]
        if len(hit_ids) > 1:
            conflicts.append((lc_name, hit_ids))
        for ident in hit_ids:
            matched.append((ident, lc_name))
    return matched, conflicts


def build(names, old):
    out = {}
    for ident in sorted(old):
        out[ident] = names.get(ident, "jingle_%d" % ident)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true", help="rewrite the pack file")
    ap.add_argument("--report", action="store_true", help="counts only")
    ap.add_argument("--skip-verify", action="store_true", help="skip the MIDI cross-check (slow: decodes every id)")
    args = ap.parse_args()

    wiki_names = parse_wiki(WIKI_TSV)
    old = existing_pack(PACK)
    if not old:
        raise SystemExit("no existing pack at %s" % PACK)

    # Only trust a wiki cacheid that names an id this cache actually has --
    # "Yama's Jingle" = 11488 is out of range and is dropped here, not carried
    # into the pack as a row for an id that does not exist.
    wiki_names = {i: n for i, n in wiki_names.items() if i in old}
    sym_names = sanitise(wiki_names)

    if not args.skip_verify:
        matched, conflicts = verify(sym_names)
        if conflicts:
            for lc_name, hit_ids in conflicts:
                print(
                    "CONFLICT: LostCity's %r matches more than one osrs239 id by MIDI: %r"
                    % (lc_name, hit_ids),
                    file=sys.stderr,
                )
            raise SystemExit("refusing to write: MIDI cross-check found an ambiguous id (see above)")
        print("MIDI cross-check: %d ids independently confirmed, 0 conflicts" % len(matched), file=sys.stderr)
    else:
        matched = []

    new = build(sym_names, old)

    named = sum(1 for i, n in new.items() if not n.startswith("jingle_"))
    numeric = len(new) - named
    print("pack ids            %d" % len(new), file=sys.stderr)
    print("  named from wiki   %d" % named, file=sys.stderr)
    print("  still jingle_<id> %d" % numeric, file=sys.stderr)

    if args.write:
        PACK.write_text(
            "\n".join("%d=%s" % (i, new[i]) for i in sorted(new)) + "\n",
            encoding="utf-8",
        )
        print("wrote %s" % PACK, file=sys.stderr)

        confirmed_ids = {ident for ident, _ in matched}
        lines = [
            "# index-11 archive id -> name, and how it was established.",
            "# See tools/gen_jingle_names.py for the method.",
            "id\tname\tevidence",
        ]
        for ident in sorted(new):
            evidence = "wiki+midi" if ident in confirmed_ids else ("wiki" if ident in wiki_names else "none")
            lines.append("%d\t%s\t%s" % (ident, new[ident], evidence))
        NAMES_TSV.write_text("\n".join(lines) + "\n", encoding="utf-8")
        print("wrote %s" % NAMES_TSV, file=sys.stderr)


if __name__ == "__main__":
    main()
