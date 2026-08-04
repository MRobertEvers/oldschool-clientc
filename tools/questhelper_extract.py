#!/usr/bin/env python3
"""Extract a Quest Helper guide into a name-resolved port worksheet.

The RuneLite Quest Helper under
`/Users/matthewevers/Documents/git_repos/quest-helper/.../helpers/quests/<dir>/`
defines how to *test* a quest (`steps.put(N, …)` = quest varbit values) and
names every subject via `net.runelite.api.gameval.*`. Those gameval constants
are the osrs239 cache's own names — lowercased they should resolve in
`OSRS-Content/osrs239-content/configs/all.*.compack`.

This tool is the lane's §4.1 "measure first" gate
(`docs/QUESTHELPER_CONTENT_PORT_QUEUE.md`, `docs/PORTING_GUIDE.md` §4.6).

Usage
-----
    python3 tools/questhelper_extract.py <helper-dir> [--content <tree>] [--check]
    python3 tools/questhelper_extract.py --all [--content <tree>] [--check]

`<helper-dir>` is a directory name under `helpers/quests/` (e.g. `xmarksthespot`)
or a path to that directory. `--check` exits non-zero if any gameval name fails
to resolve (or the `quest_*` dbrow is missing), so a slice cannot start blind.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from collections import defaultdict
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
DEFAULT_QH = Path(
    "/Users/matthewevers/Documents/git_repos/quest-helper"
    "/src/main/java/com/questhelper/helpers/quests"
)
DEFAULT_CONTENT = REPO / "OSRS-Content" / "osrs239-content"

# gameval kind → (compack file, optional alternate files)
KIND_COMPACK = {
    "NpcID": ("all.npc.compack",),
    "ObjectID": ("all.loc.compack",),
    "ItemID": ("all.obj.compack",),
    "VarbitID": ("all.varbit.compack",),
    "VarPlayerID": ("all.varp.compack",),
    "VarClientID": ("all.varc.compack",),
    "AnimationID": ("all.seq.compack",),
    "SpotanimID": ("all.spotanim.compack",),
}

GAMEVAL_RE = re.compile(
    r"\b(NpcID|ObjectID|ItemID|VarbitID|VarPlayerID|VarClientID|"
    r"AnimationID|SpotanimID)\.([A-Z0-9_]+)\b"
)
STEPS_PUT_RE = re.compile(r"steps\.put\(\s*(\d+)\s*,\s*([^)]+)\)")
WORLDPOINT_RE = re.compile(
    r"new\s+WorldPoint\(\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\)"
)
# Common quest-dbrow guesses from helper dir name
QUEST_DBROW_HINTS = {
    "xmarksthespot": "quest_xmarksthespot",
    "thecorsaircurse": "quest_corsaircurse",
    "clientofkourend": "quest_clientofkourend",
    "theribbitingtaleofalilypadlabourdispute": "quest_ribbitingtale",
    "pryingtimes": "quest_pryingtimes",
    "thequeenofthieves": "quest_queenofthieves",
    "thedepthsofdespair": "quest_depthsofdespair",
    "aporcineofinterest": "quest_porcineofinterest",
    "theascentofarceuus": "quest_ascentofarceuus",
    "ethicallyacquiredantiquities": "quest_ethicallyacquiredantiquities",
    "theidesofmilk": "quest_idesofmilk",
    "deserttreasureii": "quest_deserttreasure2",
    "dragonslayerii": "quest_dragonslayer2",
    "monkeymadnessii": "quest_monkeymadness2",
    "songoftheelves": "quest_songoftheelves",
    "sinsofthefather": "quest_sinsofthefather",
    "perilousmoon": "quest_perilousmoons",
    "akingdomdivided": "quest_kingdomdivided",
    "anightatthetheatre": "quest_nightatthetheatre",
    "atasteofhope": "quest_tasteofhope",
    "atfirstlight": "quest_atfirstlight",
    "bearyoursoul": "quest_bearyoursoul",
    "belowicemountain": "quest_belowicemountain",
    "beneathcursedsands": "quest_beneathcursedsands",
    "bonevoyage": "quest_bonevoyage",
    "childrenofthesun": "quest_childrenofthesun",
    "currentaffairs": "quest_currentaffairs",
    "deathontheisle": "quest_deathontheisle",
    "entertheabyss": "quest_entertheabyss",
    "gettingahead": "quest_gettingahead",
    "insearchofknowledge": "quest_insearchofknowledge",
    "makingfriendswithmyarm": "quest_makingfriendswithmyarm",
    "meatandgreet": "quest_meatandgreet",
    "misthalinmystery": "quest_misthalinmystery",
    "pandemonium": "quest_pandemonium",
    "scrambled": "quest_scrambled",
    "secretsofthenorth": "quest_secretsofthenorth",
    "shadowsofcustodia": "quest_shadowsofcustodia",
    "sleepinggiants": "quest_sleepinggiants",
    "taleoftherighteous": "quest_taleoftherighteous",
    "templeoftheeye": "quest_templeoftheeye",
    "thecurseofarrav": "quest_curseofarrav",
    "thefinaldawn": "quest_finaldawn",
    "theforsakentower": "quest_forsakentower",
    "thefremennikexiles": "quest_fremennikexiles",
    "thegardenofdeath": "quest_gardenofdeath",
    "theheartofdarkness": "quest_heartofdarkness",
    "theredreef": "quest_redreef",
    "troubledtortugans": "quest_troubledtortugans",
    "twilightspromise": "quest_twilightspromise",
}


def load_compack_names(path: Path) -> set[str]:
    names: set[str] = set()
    if not path.is_file():
        return names
    for line in path.read_text(errors="replace").splitlines():
        line = line.strip()
        if not line or line.startswith("//") or line.startswith("#"):
            continue
        # id=name  or  name alone
        if "=" in line:
            _, _, rhs = line.partition("=")
            names.add(rhs.strip().lower())
        else:
            names.add(line.lower())
    return names


def worldpoint_to_coord(x: int, z: int, plane: int) -> str:
    mx, mz = x >> 6, z >> 6
    lx, lz = x & 63, z & 63
    return f"{plane}_{mx}_{mz}_{lx}_{lz}"


def gather_java(helper_dir: Path) -> str:
    parts: list[str] = []
    for p in sorted(helper_dir.rglob("*.java")):
        parts.append(p.read_text(errors="replace"))
    return "\n".join(parts)


def normalize_gameval_name(kind: str, name: str) -> str:
    """Lowercase gameval; strip leading underscores (ItemID._4DOSESTAMINA → 4dosestamina)."""
    n = name.lower()
    if kind == "ItemID":
        n = n.lstrip("_")
    return n


def guess_quest_dbrow(helper_name: str, text: str, dbrow_names: set[str]) -> str | None:
    candidates: list[str] = []
    if helper_name in QUEST_DBROW_HINTS:
        candidates.append(QUEST_DBROW_HINTS[helper_name])
    candidates.append("quest_" + helper_name)
    candidates.append("miniquest_" + helper_name)
    # Quest.FOO in helper registration elsewhere
    m = re.search(r"Quest\.([A-Z0-9_]+)", text)
    if m:
        raw = m.group(1).lower().replace("__", "_")
        candidates.append("quest_" + raw.replace("_", ""))
        candidates.append("miniquest_" + raw.replace("_", ""))
    for c in candidates:
        if c in dbrow_names:
            return c
    return candidates[0] if candidates else None


def extract(helper_dir: Path, content: Path) -> dict:
    text = gather_java(helper_dir)
    helper_name = helper_dir.name

    steps: list[tuple[int, str]] = []
    for m in STEPS_PUT_RE.finditer(text):
        steps.append((int(m.group(1)), m.group(2).strip()))
    steps.sort(key=lambda t: t[0])

    refs: dict[str, set[str]] = defaultdict(set)
    for kind, name in GAMEVAL_RE.findall(text):
        refs[kind].add(normalize_gameval_name(kind, name))

    points: list[tuple[int, int, int, str]] = []
    seen_pts: set[tuple[int, int, int]] = set()
    for m in WORLDPOINT_RE.finditer(text):
        x, z, plane = int(m.group(1)), int(m.group(2)), int(m.group(3))
        key = (x, z, plane)
        if key in seen_pts:
            continue
        seen_pts.add(key)
        points.append((x, z, plane, worldpoint_to_coord(x, z, plane)))

    configs = content / "configs"
    dbrow_names = load_compack_names(configs / "all.dbrow.compack")
    resolved: dict[str, list[tuple[str, bool, str]]] = {}
    unresolved: list[tuple[str, str]] = []
    for kind, names in sorted(refs.items()):
        files = KIND_COMPACK.get(kind, ())
        pack_names: set[str] = set()
        for fname in files:
            pack_names |= load_compack_names(configs / fname)
        rows = []
        for n in sorted(names):
            ok = n in pack_names
            # Poison / dose gamevals sometimes keep a trailing '_' the pack drops.
            if not ok and kind == "ItemID" and n.rstrip("_") in pack_names:
                n = n.rstrip("_")
                ok = True
            src = files[0] if files else "?"
            rows.append((n, ok, src))
            if not ok:
                unresolved.append((kind, n))
        resolved[kind] = rows

    dbrow = guess_quest_dbrow(helper_name, text, dbrow_names)
    dbrow_ok = dbrow is not None and dbrow in dbrow_names
    if not dbrow_ok:
        unresolved.append(("dbrow", dbrow or helper_name))

    return {
        "helper": helper_name,
        "path": str(helper_dir),
        "steps": steps,
        "resolved": resolved,
        "unresolved": unresolved,
        "worldpoints": points,
        "quest_dbrow": dbrow,
        "quest_dbrow_ok": dbrow_ok,
    }


def format_report(data: dict) -> str:
    lines: list[str] = []
    lines.append(f"# Quest Helper extract: {data['helper']}")
    lines.append(f"path: {data['path']}")
    lines.append("")
    lines.append("## steps.put (varbit state machine)")
    if not data["steps"]:
        lines.append("(none found)")
    else:
        for n, expr in data["steps"]:
            lines.append(f"  {n:>4}  →  {expr}")
    lines.append("")
    lines.append(
        f"## quest dbrow: {data['quest_dbrow']}  "
        f"({'OK' if data['quest_dbrow_ok'] else 'MISSING'})"
    )
    lines.append("")
    lines.append("## WorldPoint → coord")
    for x, z, plane, coord in data["worldpoints"]:
        lines.append(f"  ({x}, {z}, {plane})  →  {coord}")
    lines.append("")
    lines.append("## gameval resolution")
    for kind, rows in data["resolved"].items():
        lines.append(f"### {kind}")
        for name, ok, src in rows:
            mark = "OK" if ok else "MISSING"
            lines.append(f"  [{mark:7}] {name}  ({src})")
    if data["unresolved"]:
        lines.append("")
        lines.append("## UNRESOLVED")
        for kind, name in data["unresolved"]:
            lines.append(f"  {kind}.{name}")
    return "\n".join(lines) + "\n"


def resolve_helper_dir(arg: str, qh_root: Path) -> Path:
    p = Path(arg)
    if p.is_dir():
        return p.resolve()
    cand = qh_root / arg
    if cand.is_dir():
        return cand.resolve()
    raise SystemExit(f"helper dir not found: {arg}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("helper", nargs="?", help="helpers/quests/<dir> name or path")
    ap.add_argument("--all", action="store_true", help="extract every helpers/quests/* dir")
    ap.add_argument("--content", type=Path, default=DEFAULT_CONTENT)
    ap.add_argument("--qh-root", type=Path, default=DEFAULT_QH)
    ap.add_argument(
        "--check",
        action="store_true",
        help="exit non-zero if any name fails to resolve",
    )
    ap.add_argument(
        "--only-unresolved",
        action="store_true",
        help="with --all, only print helpers that have unresolved names",
    )
    args = ap.parse_args()

    if not args.all and not args.helper:
        ap.error("helper dir required (or --all)")

    helpers: list[Path] = []
    if args.all:
        helpers = sorted(p for p in args.qh_root.iterdir() if p.is_dir())
    else:
        helpers = [resolve_helper_dir(args.helper, args.qh_root)]

    any_fail = False
    for h in helpers:
        data = extract(h, args.content)
        fail = bool(data["unresolved"]) or not data["quest_dbrow_ok"]
        if fail:
            any_fail = True
        if args.only_unresolved and not fail:
            continue
        sys.stdout.write(format_report(data))
        if len(helpers) > 1:
            sys.stdout.write("\n")

    if args.check and any_fail:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
