#!/usr/bin/env python3
"""Generate the character-design panel's kit tables from the cache.

Reads   OSRS-Content/osrs239-content/configs/all.idk
        OSRS-Content/osrs239-content/configs/all.idk.compack
Writes  OSRS-Content/osrs239-content/server/scripts/player/configs/player_design.enum

The design panel offers a list of identity kits per body part, and that list is
the cache's own: `bodypart` groups them (0..6 male, 7..13 male's female twin)
and `notselectable` marks the six the panel must not show -- three of them are
the bald/clean-shaven "nothing here" records the game uses for a full helm, and
offering those as a hairstyle is how a player ends up unable to get their hair
back.

Two enums rather than fourteen, because RuneScript cannot build an enum name
from a variable and a fourteen-arm switch in content would be a table written
twice:

    design_kit      key = part * 1000 + index   ->  idk id
    design_kit_count  key = part                ->  how many that part offers

1000 and not 100: the male hair list is 71 long today and a cache that doubled
it would silently start indexing into jaws.

  python3 tools/gen_player_design.py [--check]
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(ROOT, "OSRS-Content", "osrs239-content")
OUT = os.path.join(
    CONTENT, "server", "scripts", "player", "configs", "player_design.enum"
)
STRIDE = 1000
PART_NAMES = ["hair", "jaw", "torso", "arms", "hands", "legs", "feet"]


def read_names():
    """id -> section name, from the compack."""
    names = {}
    path = os.path.join(CONTENT, "configs", "all.idk.compack")
    with open(path, encoding="utf8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if "=" not in line or line.startswith("//"):
                continue
            key, name = line.split("=", 1)
            try:
                names[name] = int(key)
            except ValueError:
                pass
    return names


def read_kits(names):
    """part -> [idk id], in id order, selectable only."""
    path = os.path.join(CONTENT, "configs", "all.idk")
    with open(path, encoding="cp1252", errors="replace") as f:
        text = f.read()
    parts = {}
    for m in re.finditer(r"^\[([^\]]+)\]\n(.*?)(?=^\[|\Z)", text, re.S | re.M):
        name, body = m.group(1), m.group(2)
        if name not in names:
            continue
        if re.search(r"^notselectable=", body, re.M):
            continue
        bp = re.search(r"^bodypart=(\d+)", body, re.M)
        if not bp:
            continue
        parts.setdefault(int(bp.group(1)), []).append(names[name])
    for ids in parts.values():
        ids.sort()
    return parts


def render(parts):
    out = []
    out.append("// The identity kits the character-design panel offers, per body part.")
    out.append("//")
    out.append("// DERIVED -- regenerate with `python3 tools/gen_player_design.py`.")
    out.append("// Source: the cache's own `configs/all.idk`, whose `bodypart` groups the")
    out.append("// kits and whose `notselectable` marks the ones the panel must not show.")
    out.append("//")
    out.append("// Body parts are 0..6 on the male body and 7..13 the same seven on the")
    out.append("// female one: hair, jaw, torso, arms, hands, legs, feet. The two halves are")
    out.append("// NOT the same length (male hair has %d, female %d), which is why"
               % (len(parts.get(0, [])), len(parts.get(7, []))))
    out.append("// `~design_kit_index` clamps rather than assuming a pairing.")
    out.append("//")
    out.append("// `design_kit`'s key is `part * %d + index`. A thousand and not a hundred:" % STRIDE)
    out.append("// the longest list is %d today and a cache that doubled it would start"
               % max(len(v) for v in parts.values()))
    out.append("// indexing quietly into the next part.")
    out.append("")
    out.append("[design_kit]")
    out.append("inputtype=int")
    out.append("outputtype=int")
    out.append("default=-1")
    for part in sorted(parts):
        label = PART_NAMES[part % 7]
        body = "female" if part >= 7 else "male"
        out.append("// %s %s -- %d kit(s)" % (body, label, len(parts[part])))
        for index, idk in enumerate(parts[part]):
            out.append("val=%d,%d" % (part * STRIDE + index, idk))
        out.append("")
    out.append("// How many each part offers, so the arrows know where to wrap.")
    out.append("[design_kit_count]")
    out.append("inputtype=int")
    out.append("outputtype=int")
    out.append("default=0")
    for part in sorted(parts):
        out.append("val=%d,%d" % (part, len(parts[part])))
    out.append("")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    names = read_names()
    parts = read_kits(names)
    missing = [p for p in range(14) if not parts.get(p)]
    if missing:
        print("gen_player_design: no selectable kits for body part(s) %s" % missing,
              file=sys.stderr)
        return 1
    text = render(parts)

    if args.check:
        have = open(OUT, encoding="utf8").read() if os.path.exists(OUT) else ""
        if have != text:
            print("gen_player_design: %s is stale -- re-run tools/gen_player_design.py" % OUT,
                  file=sys.stderr)
            return 1
        print("gen_player_design: %s is up to date" % os.path.relpath(OUT, ROOT))
        return 0

    with open(OUT, "w", encoding="utf8") as f:
        f.write(text)
    print("gen_player_design: %d body parts, %d selectable kit(s) -> %s"
          % (len(parts), sum(len(v) for v in parts.values()), os.path.relpath(OUT, ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
