#!/usr/bin/env python3
"""content_asset_inventory.py --names REGEX [--names REGEX ...] : what the cache
has for a piece of content that our scripts never use.

Written after Romeo & Juliet (2026-10-24). The quest was ported from LostCity's
2004 scripts and passed its quest gate for two months while the rev-241 cache
carried OSRS's 2007 rewrite in plain sight: jingles `cadava_potion_romeo_juliet`
and `romeo_falls_in_love_romeo_juliet`, seqs `human_drink_from_vial_cadava` and
`human_apothecary_create_potion`, the crypt's `romeo_juliet_*` locs, the sleeping
congregation npcs, `varb1838_romjul_juliet_visible` (Juliet's multinpc switch)
and `fai_varrock_cadavabush_tailored` (a per-player bush on `varb6530`). Nothing
referenced any of them; each one was a scene, a mechanic or a state the port did
not have.

For every config type it lists the cache symbols whose NAME matches one of the
regexes, and for each whether any file under the scripts tree mentions it:

    USED    the symbol appears in a .rs2 / .npc / .loc / .obj / .spawn / .varp /
            .varbit / .constant / .enum / .dbrow file under --scripts
    PLACED  a loc no script names but the map places (static scenery: fine,
            unless it is a multiloc carrier, below)
    CACHE   another cache config names it (an npc's readyanim, a spotanim's
            seq, an obj's model): the client uses it without us
    UNUSED  the cache has it and nothing of ours does -- read it: a missing
            scene, sound, state or npc, or say why it is not content

Multiloc and multinpc carriers (`multivarbit=` / `multivarp=`) are checked
again: the switch var must be WRITTEN somewhere (`%<var> =`), or the player
always sees the default form. That is how the tailored bush and Juliet's
balcony went unnoticed: both carriers are placed by the map, so a name search
alone calls them used.

    python3 tools/rsprox/content_asset_inventory.py --names 'romeo|juliet|romjul|cadava|lawrence|leptoc|phillipa|apothecary'
    python3 tools/rsprox/content_asset_inventory.py --names '^mistmyst_|misthalin' --unused

Name matching is a starting point, not a proof: Jagex names assets after the
quest, the npc or the place (`fai_varrock_*` is "Varrock's New Look", 2007), so
pass the npcs' and the area's names too.
"""
import argparse
import collections
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
CONTENT = os.path.join(REPO, "OSRS-Content", "osrs-content")

# type -> the names file (id=name per line)
SOURCES = {
    "npc": "configs/all.npc.compack",
    "loc": "configs/all.loc.compack",
    "obj": "configs/all.obj.compack",
    "seq": "configs/all.seq.compack",
    "spotanim": "configs/all.spotanim.compack",
    "varbit": "configs/all.varbit.compack",
    "varp": "configs/all.varp.compack",
    "enum": "configs/all.enum.compack",
    "struct": "configs/all.struct.compack",
    "dbrow": "configs/all.dbrow.compack",
    "inv": "configs/all.inv.compack",
    "jingle": "pack/11_musicjingles.pack",
    "song": "pack/6_musictracks.pack",
    "synth": "pack/4_soundeffects.pack",
}
REFERENCE_EXT = (".rs2", ".npc", ".loc", ".obj", ".spawn", ".varp", ".varbit", ".constant",
                 ".enum", ".dbrow", ".dbtable", ".inv", ".param", ".struct", ".seq")


def read_names(path):
    out = []
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            key, sep, value = line.strip().partition("=")
            if sep and key.isdigit() and value:
                out.append((int(key), value.split()[0]))
    return out


def scan_scripts(root):
    """Every identifier token in the content's own files, and every var written."""
    tokens = collections.Counter()
    written = set()
    for dirpath, _dirs, files in os.walk(root):
        if os.sep + "build" in dirpath:
            continue
        for name in files:
            if not name.endswith(REFERENCE_EXT):
                continue
            path = os.path.join(dirpath, name)
            if ".generated." in name or "_explicit" in path:
                # generated blocks name every symbol; they are not content using it
                continue
            with open(path, encoding="utf-8", errors="replace") as handle:
                text = handle.read()
            for tok in re.findall(r"[A-Za-z_][A-Za-z0-9_]*", text):
                tokens[tok] += 1
            for var in re.findall(r"%([A-Za-z0-9_]+)\s*=(?!=)", text):
                written.add(var)
    return tokens, written


def cache_tokens(content):
    """Identifiers the cache's own config dumps use as VALUES (key=value)."""
    tokens = collections.Counter()
    for rel in ("configs/all.npc", "configs/all.loc", "configs/all.obj", "configs/all.spotanim",
                "configs/all.seq", "configs/all.idk"):
        path = os.path.join(content, rel)
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8", errors="replace") as handle:
            for line in handle:
                _key, sep, value = line.partition("=")
                if sep:
                    for tok in re.findall(r"[A-Za-z_][A-Za-z0-9_]*", value):
                        tokens[tok] += 1
    return tokens


def placed_loc_ids(maps_dir, wanted):
    placed = set()
    if not wanted or not os.path.isdir(maps_dir):
        return placed
    for name in os.listdir(maps_dir):
        if not name.endswith(".jl2"):
            continue
        with open(os.path.join(maps_dir, name), encoding="utf-8", errors="ignore") as handle:
            for line in handle:
                _pos, sep, rest = line.partition(":")
                if not sep:
                    continue
                parts = rest.split()
                if parts and parts[0].isdigit() and int(parts[0]) in wanted:
                    placed.add(int(parts[0]))
    return placed


def carriers(config_path, names):
    """symbol -> (switch var, forms) for multiloc / multinpc carriers among `names`."""
    out = {}
    if not os.path.exists(config_path):
        return out
    current = None
    var = None
    forms = []
    with open(config_path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if line.startswith("[") and line.endswith("]"):
                if current in names and var:
                    out[current] = (var, forms)
                current, var, forms = line[1:-1], None, []
                continue
            if current not in names:
                continue
            key, _, value = line.partition("=")
            if key in ("multivarbit", "multivarp") and value not in ("default", "-1", ""):
                var = value
            elif re.match(r"multi(loc|npc)\d+$", key):
                forms.append(value)
    if current in names and var:
        out[current] = (var, forms)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--names", action="append", required=True, help="regex on the cache symbol (repeatable)")
    ap.add_argument("--content", default=CONTENT)
    ap.add_argument("--scripts", default=None, help="default <content>/server/scripts")
    ap.add_argument("--unused", action="store_true", help="print only UNUSED rows and unwritten switches")
    args = ap.parse_args()
    pattern = re.compile("|".join("(?:%s)" % n for n in args.names), re.I)
    scripts = args.scripts or os.path.join(args.content, "server", "scripts")
    tokens, written = scan_scripts(scripts)
    in_cache = cache_tokens(args.content)

    matched = {}
    for kind, rel in SOURCES.items():
        path = os.path.join(args.content, rel)
        if not os.path.exists(path):
            continue
        matched[kind] = [(i, n) for i, n in read_names(path) if pattern.search(n)]

    loc_ids = {i for i, _ in matched.get("loc", [])}
    placed = placed_loc_ids(os.path.join(args.content, "maps"), loc_ids)
    unused_total = 0
    for kind, rows in matched.items():
        if not rows:
            continue
        lines = []
        for ident, name in rows:
            if tokens[name] > 0:
                verdict = "USED"
            elif kind == "loc" and ident in placed:
                verdict = "PLACED"
            elif in_cache[name] > 0:
                verdict = "CACHE"
            else:
                verdict = "UNUSED"
                unused_total += 1
            if args.unused and verdict != "UNUSED":
                continue
            lines.append("  %-7s %6d %s" % (verdict, ident, name))
        if lines:
            print("== %s (%d matched)" % (kind, len(rows)))
            print("\n".join(lines))

    unwritten = 0
    for kind, conf in (("loc", "configs/all.loc"), ("npc", "configs/all.npc")):
        names = {n for _, n in matched.get(kind, [])}
        for symbol, (var, forms) in sorted(carriers(os.path.join(args.content, conf), names).items()):
            ok = var in written
            if not ok:
                unwritten += 1
            if args.unused and ok:
                continue
            print("== %s carrier %s: switch %s %s -> %s" % (
                kind, symbol, var, "WRITTEN" if ok else "NEVER WRITTEN", ", ".join(forms)))
    print("%d unused symbol(s), %d carrier switch(es) never written" % (unused_total, unwritten))
    return 0


if __name__ == "__main__":
    sys.exit(main())
