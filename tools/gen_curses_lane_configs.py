#!/usr/bin/env python3
"""Generate the Ancient Curses cache-side lane configs from the book table.

Everything here is derived from `docs/rs558_ancient_curses/tables/curses_book.csv`,
which is itself extracted from the rev558 cache (enum 863 -> structs 888-907).
Regenerating is therefore safe and idempotent: the cache stays the single source
of the levels, names, tooltips and icon ids.

What it writes, all under OSRS-Content/osrs239-content:

  ported/rs558_ancient_curses/configs/curses.obj        20 book entries
  ported/rs558_ancient_curses/configs/curses.enum       the book itself
  ported/rs558_ancient_curses/configs/curses.varp       the two mask carriers
  ported/rs558_ancient_curses/configs/curses.varbit     the two masks
  ported/rs558_ancient_curses/configs/all.varp.compack  base + the two varps
  ported/rs558_ancient_curses/configs/all.varbit.compack base + the two varbits
  ported/rs558_ancient_curses/pack/obj.alloc            48000..48019
  ported/rs558_ancient_curses/pack/enum.alloc           the book enum

Why the ids are what they are
-----------------------------
The client must be able to *read* all of this, and this project's content layer
cannot add a client-visible varp or varbit through the ordinary namespaces
(`content.ini`: varp `ids = server`, varbit `ids = cache`). The Summoning lane
established the way round it — a lane ships its own overriding `all.<ns>.compack`
and the packer writes the extra record into the flag-on cache only, leaving the
pristine `cache.osrs239` untouched.

  varp  5705/5706  sit in the gap between the cache high-water (5704) and the
                   server allocation floor (5725), so they are cache varps by
                   construction and cannot collide with `ss_allocate.py`.
  varbit 20412/20413  follow the cache high-water (20410) and Summoning's 20411.
  enum  6035       follows the base tree's allocated enums (5995..6034).
  obj   48000+     above the Summoning lane's obj high-water (47537).
"""

from __future__ import annotations

import csv
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TREE = ROOT / "OSRS-Content/osrs239-content"
LANE = TREE / "ported/rs558_ancient_curses"
BOOK = ROOT / "docs/rs558_ancient_curses/tables/curses_book.csv"

VARP_ACTIVE, VARP_QUICK = 5705, 5706
VARBIT_ACTIVE, VARBIT_QUICK = 20412, 20413
ENUM_BOOK = 6035
OBJ_BASE = 48000

# The prayer tab. Component N of interface 541 is `prayerN-8`, so the book's
# first entry lives at 541:9. Packed as (interface << 16) | component, which is
# what param_1751 carries — Thick Skin's 35454985 is (541<<16)|9.
IFACE_PRAYERBOOK = 541
FIRST_COMPONENT = 9

# Param ids, matching the standard book's records exactly so script 463
# (`prayer_updatebutton`) needs no change to read them.
P_BIT, P_COMPONENT, P_NAME, P_LEVEL, P_DESC = 630, 1751, 1752, 1753, 1754
P_SPRITE_OFF, P_SPRITE_ON = 1756, 1757


def slug(name: str) -> str:
    return name.lower().replace(" ", "_")


def main() -> int:
    rows = list(csv.DictReader(BOOK.open()))
    if len(rows) != 20:
        raise SystemExit(f"expected 20 curses in {BOOK}, found {len(rows)}")

    # param_1756/1757 are plain ints in the standard book's objs, so the icons
    # must be bound to the lane's real sprite ids, not to names. Read them back
    # out of the sprite pack the staging step wrote.
    sprite_pack = LANE / "pack/8_sprites.pack"
    if not sprite_pack.is_file():
        raise SystemExit(f"missing {sprite_pack}; stage the icon sprites first")
    sprite_id = {}
    for line in sprite_pack.read_text().splitlines():
        if not line.strip():
            continue
        sid, path = line.split("=", 1)
        sprite_id[path.rsplit("/", 1)[1]] = int(sid)

    (LANE / "configs").mkdir(parents=True, exist_ok=True)
    (LANE / "pack").mkdir(parents=True, exist_ok=True)

    # ---- objs: one book entry per curse ----------------------------------
    obj_lines, alloc_obj, enum_vals = [], [], []
    for i, r in enumerate(rows):
        bit = int(r["bit"])
        name = f"curses_book_{slug(r['name'])}"
        obj_id = OBJ_BASE + i
        component = (IFACE_PRAYERBOOK << 16) | (FIRST_COMPONENT + bit)
        # The lane's staged icon ids, not rev558's — the sprites were renumbered
        # into 21000+ when they were copied into the tree.
        try:
            on = sprite_id[f"curses_icon_{slug(r['name'])}_on"]
            off = sprite_id[f"curses_icon_{slug(r['name'])}_off"]
        except KeyError as exc:
            raise SystemExit(f"no staged icon for {r['name']}: {exc}") from exc
        obj_lines += [
            f"[{name}]",
            "// Book entry only: never in an inventory, never traded.",
            f"name={r['name']}",
            f"param=param_{P_BIT},int,{bit}",
            f"param=param_{P_COMPONENT},int,{component}",
            f"param=param_{P_NAME},str,{r['name']}",
            f"param=param_{P_LEVEL},int,{r['level']}",
            f"param=param_{P_DESC},str,{r['effect']}",
            f"param=param_{P_SPRITE_ON},int,{on}",
            f"param=param_{P_SPRITE_OFF},int,{off}",
            "",
        ]
        alloc_obj.append(f"{obj_id}={name}")
        enum_vals.append(f"val={bit},{name}")

    (LANE / "configs/curses.obj").write_text(
        "// Ancient Curses book entries, generated by tools/gen_curses_lane_configs.py\n"
        "// from the rev558 cache table. Do not hand-edit; edit the generator.\n"
        "//\n"
        "// One obj per curse, carrying the same params the standard book's objs use,\n"
        "// so `prayer_updatebutton` (script 463) reads them with no change.\n\n"
        + "\n".join(obj_lines))

    # ---- the book enum ----------------------------------------------------
    (LANE / "configs/curses.enum").write_text(
        "// The Ancient Curses book: position 0..19 -> the obj holding that curse's\n"
        "// client data. Returned by the `= 2` arm of script 7823, exactly as enum\n"
        "// 4959 is returned for Ruinous Powers.\n\n"
        "[curses_book]\n"
        "inputtype=int\n"
        "outputtype=obj\n"
        "default=null\n"
        + "\n".join(enum_vals) + "\n")

    # ---- carriers and masks ----------------------------------------------
    (LANE / "configs/curses.varp").write_text(
        "// Mask carriers for the third prayer book, mirroring the cache's own\n"
        "// prayer_ruinous_0 / prayer_ruinous_1 pair. A varp record carries no\n"
        "// fields; existence is the whole record.\n\n"
        "[prayer_curses_0]\n\n[prayer_curses_1]\n")

    (LANE / "configs/curses.varbit").write_text(
        "// The two 20-bit masks, one bit per curse in book order.\n"
        "// Shaped exactly like prayer_allactive_ruinous / quickprayer_selected_ruinous.\n\n"
        "[prayer_allactive_curses]\n"
        "basevar=prayer_curses_0\n"
        "startbit=0\n"
        "endbit=19\n\n"
        "[quickprayer_selected_curses]\n"
        "basevar=prayer_curses_1\n"
        "startbit=0\n"
        "endbit=19\n")

    # ---- compack overrides -------------------------------------------------
    # The lane ships a whole copy of the base index with its own ids appended.
    # cachepack pack writes the extra records into the flag-on cache only.
    # sscompile reads ONE staged `all.<ns>.compack` per namespace, so this file
    # has to be a superset of the Summoning lane's rather than a rival copy of
    # the base — two `--pack` dirs holding same-named files would redefine every
    # base id. Derive from Summoning's where it exists so its minted 20411 comes
    # along and cannot go stale behind us.
    summoning = TREE / "ported/scape2009_summoning/configs"
    for ns, extra in (
        ("varp", [(VARP_ACTIVE, "prayer_curses_0"), (VARP_QUICK, "prayer_curses_1")]),
        ("varbit", [(VARBIT_ACTIVE, "prayer_allactive_curses"),
                    (VARBIT_QUICK, "quickprayer_selected_curses")]),
    ):
        upstream = summoning / f"all.{ns}.compack"
        source = upstream if upstream.is_file() else TREE / f"configs/all.{ns}.compack"
        base = source.read_text().rstrip("\n").splitlines()
        taken = {int(l.split("=", 1)[0]) for l in base if l and l[0].isdigit()}
        for cid, cname in extra:
            if cid in taken:
                raise SystemExit(f"{ns} id {cid} already taken in the base tree")
            base.append(f"{cid}={cname}")
        (LANE / f"configs/all.{ns}.compack").write_text("\n".join(base) + "\n")

    (LANE / "pack/obj.alloc").write_text("\n".join(alloc_obj) + "\n")
    (LANE / "pack/enum.alloc").write_text(f"{ENUM_BOOK}=curses_book\n")

    print(f"objs      {OBJ_BASE}..{OBJ_BASE + 19}  (20)")
    print(f"enum      {ENUM_BOOK}=curses_book")
    print(f"varps     {VARP_ACTIVE}, {VARP_QUICK}")
    print(f"varbits   {VARBIT_ACTIVE}, {VARBIT_QUICK}")
    print(f"written under {LANE.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
