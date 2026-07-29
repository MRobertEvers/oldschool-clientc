#!/usr/bin/env python3
"""
Import OpenRune's gameval name table into LostCity-style .pack files.

OpenRune-Server ships `data/cfg/gamevals-binary/gamevals.dat`: every id in its
cache with the symbolic name its content refers to it by (`npcs.goblin`,
`items.bronze_scimitar`, `objects.door`). That table is what makes it possible
to write content against names instead of against magic numbers, and it is the
one thing this repo has no way to derive — a cache carries an npc's *display*
name, not the unique symbol a server needs.

    tools/gameval_import.py --search npcs goblin        # discovery
    tools/gameval_import.py --names tools/gameval_import.names \
        --out OSRS-Content/osrs239-content/names

`--names` is the checked-in request list: one `namespace:name` per line, so the
generated packs stay a *subset* rather than 3 MB of text nobody reads. A name
the table does not have is an error, never a silent omission.

**`--out` is the `names/` directory, never `pack/`.** An imported alias is
layer 1 — a name a human chose, from a foreign revision, that the cache does not
state. `pack/` is layer 0 and is regenerated wholesale by `cachepack unpack`, so
an alias written there is reverted by the next unpack or, worse, kept and made
indistinguishable from a name the cache actually carries. The script refuses a
`pack/` output rather than trusting the caller, and merges into whatever is
already in the target file instead of truncating it. Both of those were real:
the previously documented command truncated `pack/npc.pack` from 16,292 lines to
39. See docs/CONTENT_ARCHITECTURE.md §4.1.

OpenRune's cache is revision 235.10 and this repo's mock targets 230, so an id
imported here is a *claim* that has to be checked. `mock230_pack` does that
against cache.osrs230 (name match, id in range) and is the thing that fails
when the two revisions have drifted — this script only reads what OpenRune
says.

Format of gamevals.dat, which is Java DataOutputStream:

    u32 group_count
    per group: u16 name_len, name, u32 entry_count,
               entry_count * (u16 len, "<name>=<id>")
"""

import argparse
import os
import struct
import sys

DEFAULT_GAMEVALS = os.path.expanduser(
    "~/Documents/git_repos/OpenRune-Server/data/cfg/gamevals-binary/gamevals.dat"
)

# gameval namespace -> the .pack basename the mock's content uses. LostCity
# names its packs after the config type, singular; OpenRune names its groups
# after the plural table.
PACK_FOR_NAMESPACE = {
    "npcs": "npc",
    "items": "obj",
    "objects": "loc",
    "sequences": "seq",
    "spotanims": "spotanim",
    "inv": "inv",
    "varp": "varp",
    "varbits": "varbit",
    "interfaces": "interface",
    "components": "component",
    "varcs": "varc",
}


def read_gamevals(path):
    """{namespace: {name: id}} for every group in the table."""
    with open(path, "rb") as handle:
        blob = handle.read()

    offset = 0

    def u16():
        nonlocal offset
        value = struct.unpack_from(">H", blob, offset)[0]
        offset += 2
        return value

    def u32():
        nonlocal offset
        value = struct.unpack_from(">I", blob, offset)[0]
        offset += 4
        return value

    def utf():
        nonlocal offset
        length = u16()
        text = blob[offset : offset + length].decode("utf-8", "replace")
        offset += length
        return text

    groups = {}
    for _ in range(u32()):
        name = utf()
        entries = {}
        for _ in range(u32()):
            record = utf()
            # Split from the right: a symbol may contain '=' (component names
            # are "<interface>:<child>=<id>" and sprite names carry ':').
            symbol, _, value = record.rpartition("=")
            entries[symbol] = int(value)
        groups[name] = entries

    if offset != len(blob):
        raise SystemExit(
            f"gamevals.dat: consumed {offset} of {len(blob)} bytes — format drift"
        )
    return groups


def cmd_search(groups, namespace, needle):
    entries = groups.get(namespace)
    if entries is None:
        raise SystemExit(
            f"no such namespace {namespace!r}; have {', '.join(sorted(groups))}"
        )
    for name, value in sorted(entries.items(), key=lambda kv: kv[1]):
        if needle.lower() in name.lower():
            print(f"{value}\t{namespace}.{name}")


def read_requests(path):
    """[(namespace, name, alias)] from the request list, comments stripped.

    A line is `namespace:name` or `namespace:name as alias` — the alias is the
    symbol content writes, for when OpenRune's is unwieldy
    (`sos_war_goblin_armed as goblin_warrior`).
    """
    requests = []
    with open(path, "r", encoding="utf-8") as handle:
        for lineno, raw in enumerate(handle, 1):
            line = raw.split("//", 1)[0].strip()
            if not line:
                continue
            if ":" not in line:
                raise SystemExit(f"{path}:{lineno}: expected `namespace:name`")
            namespace, _, rest = line.partition(":")
            if " as " in rest:
                name, _, alias = rest.partition(" as ")
                requests.append((namespace.strip(), name.strip(), alias.strip()))
            else:
                requests.append((namespace.strip(), rest.strip(), rest.strip()))
    return requests


# The tool owns exactly what sits between these markers. Everything else in the
# file — comments, hand-authored aliases, the prose justifying them — is copied
# through untouched. See docs/CONTENT_ARCHITECTURE.md §3.2: `cachepack unpack`
# already destroys authored prose in the packs it owns, and this tool used to do
# the same thing far more violently.
BEGIN_MARKER = "// >>> gameval_import: generated, do not edit below this line"
END_MARKER = "// <<< gameval_import: end generated"


def split_managed(text):
    """(before, after) around the tool's own block, which is discarded.

    A file with no markers is all `before`, so the block gets appended.
    """
    if BEGIN_MARKER not in text:
        return text, ""
    before, _, rest = text.partition(BEGIN_MARKER)
    if END_MARKER not in rest:
        # A truncated previous run. Everything from the marker on is ours and
        # unparseable, so drop it rather than guess where it ended.
        return before, ""
    _, _, after = rest.partition(END_MARKER)
    return before, after.lstrip("\n")


def cmd_generate(groups, requests, out_dir, source):
    """Merge the requested aliases into the layer-1 packs under `out_dir`.

    Two things this deliberately does NOT do, both of which it used to:

    - **Write layer 0.** `pack/<ns>.pack` is regenerated wholesale by
      `cachepack unpack` from the cache's own gameval table. Writing an alias
      there means the next unpack reverts it, or — worse — keeps it and leaves
      it indistinguishable from a name the cache actually stated. Aliases belong
      in `names/`, which is layer 1. See docs/CONTENT_ARCHITECTURE.md §4.1.

    - **Truncate.** Every output was opened `"w"` and rewritten with only the
      requested symbols, so running the command documented in
      docs/mock230_content.md §5 took `pack/npc.pack` from 16,292 lines to 39
      and `pack/varp.pack` from 5,705 to 11. A following `cachepack unpack`
      refilled them from the cache and silently reverted every alias the import
      had just made. Neither ordering was correct.
    """
    if os.path.basename(os.path.normpath(out_dir)) == "pack":
        raise SystemExit(
            f"refusing to write {out_dir}: that is layer 0, which cachepack\n"
            f"regenerates from the cache's gameval table. Aliases go in the\n"
            f"sibling `names/` directory — see docs/CONTENT_ARCHITECTURE.md §4.1."
        )

    packs = {}
    missing = []
    for namespace, name, alias in requests:
        pack = PACK_FOR_NAMESPACE.get(namespace)
        if pack is None:
            raise SystemExit(f"no pack mapping for namespace {namespace!r}")
        entries = groups.get(namespace, {})
        if name not in entries:
            missing.append(f"{namespace}:{name}")
            continue
        packs.setdefault(pack, {})[alias] = entries[name]

    if missing:
        raise SystemExit(
            "gamevals.dat has no entry for:\n  " + "\n  ".join(missing)
        )

    os.makedirs(out_dir, exist_ok=True)
    for pack, entries in sorted(packs.items()):
        path = os.path.join(out_dir, f"{pack}.pack")
        existing = ""
        if os.path.exists(path):
            with open(path, "r", encoding="utf-8") as handle:
                existing = handle.read()
        before, after = split_managed(existing)

        block = [
            BEGIN_MARKER,
            f"// Imported by tools/gameval_import.py from {source}.",
            "// Add symbols to tools/gameval_import.names and re-run; hand-authored",
            "// lines outside this block are preserved. Ids are OpenRune's (cache rev",
            "// 235.10) and are re-validated against the cache by mock230_pack.",
        ]
        for alias, value in sorted(entries.items(), key=lambda kv: kv[1]):
            block.append(f"{value}={alias}")
        block.append(END_MARKER)

        if before and not before.endswith("\n"):
            before += "\n"
        text = before + "\n".join(block) + "\n"
        if after.strip():
            text += "\n" + after.lstrip("\n")

        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)
        kept = len([ln for ln in before.splitlines() if "=" in ln and not ln.strip().startswith("//")])
        print(f"wrote {path} ({len(entries)} imported, {kept} hand-authored preserved)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gamevals", default=DEFAULT_GAMEVALS)
    parser.add_argument("--search", nargs=2, metavar=("NAMESPACE", "SUBSTRING"))
    parser.add_argument("--names", help="request list to generate packs from")
    parser.add_argument(
        "--out",
        help="the tree's `names/` directory (layer 1). Writing `pack/` is refused.",
    )
    parser.add_argument(
        "--namespaces", action="store_true", help="list the table's groups"
    )
    args = parser.parse_args()

    if not os.path.exists(args.gamevals):
        raise SystemExit(
            f"{args.gamevals} not found — point --gamevals at OpenRune-Server's "
            "data/cfg/gamevals-binary/gamevals.dat"
        )
    groups = read_gamevals(args.gamevals)

    if args.namespaces:
        for name, entries in groups.items():
            print(f"{name}\t{len(entries)}")
        return 0
    if args.search:
        cmd_search(groups, args.search[0], args.search[1])
        return 0
    if args.names:
        if not args.out:
            raise SystemExit("--names needs --out")
        cmd_generate(groups, read_requests(args.names), args.out, args.gamevals)
        return 0

    parser.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
