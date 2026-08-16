#!/usr/bin/env python3
"""Generate `pack/4_soundeffects.pack` — real names for the sound effects.

    tools/gen_sound_names.py --write
    tools/gen_sound_names.py --report        what changed, and what stayed numeric

## Why this exists

The sound-effect namespace was `synth_0`, `synth_1`, ... — the id spelled back
at itself. A namespace whose names carry no information is not a namespace, and
it cost this project two concrete things:

  - **The port's safety net.** `tools/port_weapon_fx.py` mediates every
    animation it carries from another server through a *name* that must resolve
    in this cache, because ids do not survive a tree change. Sounds had no names
    to mediate through, so weapon sound overlays were blocked outright
    (`is_sound_param`, "blocked on a `synth` pack kind") and no weapon in this
    tree stated an attack sound at all.
  - **LostCity's own rows.** LostCity states `param=slash_sound,hacksword_slash`
    — a *name*. With the numeric namespace there was nothing for that to resolve
    against; with this one it resolves to 2500, which is what LostCity means.

## Where the names come from

`docs/audio/osrs_wiki_sound_ids.wikitext`, a copy of the OSRS Wiki's
[[List of sound IDs]] taken so this generator does not need the network. That
page explains its own provenance, and it is the good kind: these are **Jagex's
own config names**, which the wiki says became knowable "due to Jagex
accidentally transmitting their names for the update Game Jam: POH Improvements"
(February 2025). They are not a community invention.

That they are Jagex's own names is checkable rather than merely claimed, and it
checks out three ways:

  - 2500 is `hacksword_slash` and 2501 `hacksword_stab` — the exact spellings
    LostCity uses in its `slash_sound` params, written years earlier from the
    2004 cache.
  - 2247 is `equip_staff` and 2248 `equip_sword`, which is what rsmod assigns to
    `equipment_sound` on staves and swords respectively.
  - 1352 is `crystal_bow2`, and rsmod assigns 1352 to the bow of faerdhinen —
    which is a crystal bow. That one line independently validates rsmod's whole
    numeric table, which nothing in this tree could check before.

## Name hygiene

Jagex's names are not all valid symbols here. Lowercased; anything outside
`[a-z0-9_]` becomes `_` (90 names: spaces, and the `#` in `charmflute_a#`); a
name that then collides with another gets its id appended (1 case, the three
rows the wiki spells "Sorting Salvage"). Ids the wiki does not name keep
`synth_<id>`, which is what every id was called before, so an unnamed sound is
still spellable and nothing that referenced one breaks.
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WIKITEXT = ROOT / "docs/audio/osrs_wiki_sound_ids.wikitext"
PACK = ROOT / "OSRS-Content/osrs239-content/pack/4_soundeffects.pack"


def parse_wiki(path):
    """id -> Jagex's config name, from the wiki's `|id||name||notes||file` rows."""
    names = {}
    text = path.read_text(encoding="utf-8", errors="replace")
    for m in re.finditer(r"^\|(\d+)\|\|([^|\n]*)\|\|", text, re.M):
        ident, name = int(m.group(1)), m.group(2).strip()
        if name:
            names.setdefault(ident, name)
    return names


def sanitise(names):
    """Jagex's names -> symbols this tree can spell, uniquely."""
    out = {}
    seen = {}
    for ident in sorted(names):
        sym = re.sub(r"[^a-z0-9_]", "_", names[ident].lower()).strip("_")
        if not sym:
            continue
        if sym in seen:
            # Deterministic and traceable: keep both, disambiguated by id. The
            # alternative (drop one) silently loses a sound.
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


def build(names, old):
    """The full pack: every id the old file had, renamed where the wiki names it."""
    out = {}
    for ident in sorted(old):
        out[ident] = names.get(ident, "synth_%d" % ident)
    # Ids the wiki names that the pack does not list are not invented here: the
    # pack's id set is this cache's, and a name for a sound this cache lacks
    # would resolve to nothing.
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true", help="rewrite the pack file")
    ap.add_argument("--report", action="store_true", help="counts only")
    args = ap.parse_args()

    names = sanitise(parse_wiki(WIKITEXT))
    old = existing_pack(PACK)
    if not old:
        raise SystemExit("no existing pack at %s" % PACK)
    new = build(names, old)

    named = sum(1 for i, n in new.items() if not n.startswith("synth_"))
    numeric = len(new) - named
    unused = sum(1 for i in names if i not in old)

    print("pack ids            %d" % len(new), file=sys.stderr)
    print("  named from wiki   %d" % named, file=sys.stderr)
    print("  still synth_<id>  %d" % numeric, file=sys.stderr)
    print("wiki names unused   %d  (ids this cache has no sound for)" % unused,
          file=sys.stderr)

    if args.write:
        PACK.write_text(
            "\n".join("%d=%s" % (i, new[i]) for i in sorted(new)) + "\n",
            encoding="utf-8",
        )
        print("wrote %s" % PACK, file=sys.stderr)


if __name__ == "__main__":
    main()
