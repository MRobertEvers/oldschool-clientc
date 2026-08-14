#!/usr/bin/env python3
"""Render the Ancient Curses asset evidence: spritesheets and human-readable tables.

This command creates *evidence only*. It never writes into a cache, a content
tree or a ledger, so it is safe to re-run at any point in the port.

Two sources are rendered side by side on purpose:

  rev558  cache.rs558_20091209_ancientcurses  — the authentic Dec-2009 curses
  osrs239 OSRS-Content/osrs239-content        — the Ruinous Powers set already
                                                shipping in the target cache

Sprites come out of `cachepack unpack --assets=sprites` as BMP plus a
`pack.meta` holding what a bitmap cannot: the group's frame count, the shared
palette, and each frame's `w,h,inner_w,inner_h,x,y` placement. The BMP alone is
therefore not the whole record, which is why the tables below read pack.meta
rather than inferring geometry from the image.

Usage:
  tools/build_curses_asset_review.py --recon build/rs558-recon
"""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path

try:
    from PIL import Image, ImageDraw
except ImportError:  # pragma: no cover
    raise SystemExit("Pillow is required: python3 -m pip install pillow")

ROOT = Path(__file__).resolve().parents[1]

# The two 26-icon blocks the rev558 cache carries for the curses book, found by
# pairing groups on identical frame geometry at a constant offset of 26.
RS558_ICONS_A = range(2321, 2347)
RS558_ICONS_B = range(2347, 2373)

# The osrs239 Ruinous Powers icons, already unpacked in the content tree.
OSRS239_SPRITES = ROOT / "OSRS-Content/osrs239-content/sprites"
OSRS239_ICON_SETS = ("icon_prayer_zaros01_30x30", "icon_prayer_zaros02_30x30")

# Curse spotanim block, contiguous in the rev558 cache with no gaps.
SPOTANIM_RANGE = range(2213, 2267)

# The curses book table, straight out of the rev558 client.
#
# Interface 271's onload chain reaches script 1237, which builds the curses
# half of the tab: it loops exactly 20 times and reads `enum 863`, whose 20
# entries (keys 0..19) map book position to struct 888..907. Each struct holds
# the whole client-side record for one curse, so this — not any wiki — is the
# primary source for the book's order, levels, icons and effect text.
CURSE_STRUCTS = range(888, 908)
CURSE_ENUM = 863
CURSE_COUNT = 20

# Param ids on those structs, named from their observed roles.
CURSE_PARAMS = {
    "param_737": "level",       # Prayer level required
    "param_735": "sprite_on",   # lit icon   (block A, 2321-2346)
    "param_736": "sprite_off",  # greyed icon(block B, 2347-2372)
    "param_734": "tooltip",     # "Level N<br>Name<br>Effect"
    "param_738": "denied",      # "You need a Prayer level of at least N..."
    "param_739": "members",
}

BG = (24, 22, 20)
FG = (226, 220, 208)
DIM = (128, 120, 110)
ACCENT = (196, 150, 60)


def read_pack_meta(path: Path) -> dict:
    """Parse a sprite group's pack.meta into {count, palette, frames{...}}."""
    meta = {"count": 0, "palette": 0, "frames": {}}
    if not path.is_file():
        return meta
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("//"):
            continue
        key, _, value = line.partition("=")
        if key == "count":
            meta["count"] = int(value)
        elif key == "palette":
            meta["palette"] = int(value)
        elif key.startswith("sprite"):
            idx = int(key[len("sprite"):])
            meta["frames"][idx] = [int(n) for n in value.split(",")]
    return meta


def load_frames(group_dir: Path) -> list[Image.Image]:
    frames = []
    for bmp in sorted(group_dir.glob("*.bmp"), key=lambda p: int(p.stem)):
        frames.append(Image.open(bmp).convert("RGB"))
    return frames


def contact_sheet(
    entries: list[tuple[str, Image.Image]],
    columns: int,
    cell: int,
    title: str,
    scale: int = 2,
) -> Image.Image:
    """Grid of labelled tiles. Each tile is the image, nearest-scaled, over BG."""
    pad = 6
    label_h = 12
    tile_w = cell * scale + pad * 2
    tile_h = cell * scale + pad + label_h + 4
    rows = (len(entries) + columns - 1) // columns
    head = 26

    sheet = Image.new("RGB", (columns * tile_w, head + rows * tile_h), BG)
    draw = ImageDraw.Draw(sheet)
    draw.text((pad, 8), title, fill=ACCENT)

    for i, (label, img) in enumerate(entries):
        col, row = i % columns, i // columns
        ox = col * tile_w
        oy = head + row * tile_h

        # Nearest-neighbour: these are pixel-art icons; smooth scaling lies
        # about the source pixels, which is the whole point of the review.
        big = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
        sheet.paste(big, (ox + pad + (cell * scale - big.width) // 2,
                          oy + pad + (cell * scale - big.height) // 2))
        draw.text((ox + pad, oy + pad + cell * scale + 2), label, fill=DIM)

    return sheet


def parse_config_block(path: Path, prefix: str, ids: range) -> dict[int, dict]:
    """Read `[<prefix>_<id>]` records out of an unpacked configs/all.<type> file."""
    if not path.is_file():
        return {}
    records: dict[int, dict] = {}
    current: int | None = None
    header = re.compile(rf"^\[{re.escape(prefix)}_(\d+)\]$")
    for line in path.read_text().splitlines():
        line = line.strip()
        m = header.match(line)
        if m:
            rid = int(m.group(1))
            current = rid if rid in ids else None
            if current is not None:
                records[current] = {}
            continue
        if current is None or not line or "=" not in line:
            continue
        key, _, value = line.partition("=")
        records[current][key] = value
    return records


def parse_structs(path: Path, ids: range) -> dict[int, dict[str, str]]:
    """Read `[struct_<id>]` param blocks, mapping known param ids to role names."""
    if not path.is_file():
        return {}
    out: dict[int, dict[str, str]] = {}
    current: int | None = None
    header = re.compile(r"^\[struct_(\d+)\]$")
    param = re.compile(r"^param=(\w+),(\w+),(.*)$")
    # errors="replace": the struct file carries a few non-UTF8 quote bytes in
    # unrelated clan-chat strings, which must not abort the whole parse.
    for line in path.read_text(errors="replace").splitlines():
        m = header.match(line.strip())
        if m:
            sid = int(m.group(1))
            current = sid if sid in ids else None
            if current is not None:
                out[current] = {}
            continue
        if current is None:
            continue
        p = param.match(line.strip())
        if p and p.group(1) in CURSE_PARAMS:
            out[current][CURSE_PARAMS[p.group(1)]] = p.group(3)
    return out


def curse_rows(recon: Path) -> list[dict]:
    """The 20 curses in book order, as the rev558 client states them."""
    structs = parse_structs(recon / "configs/all.struct", CURSE_STRUCTS)
    rows = []
    for key in range(CURSE_COUNT):
        sid = CURSE_STRUCTS.start + key
        rec = structs.get(sid, {})
        tip = rec.get("tooltip", "")
        # "Level N<br>Name<br>Effect..." — the effect itself may contain further
        # <br>s (Wrath and Soul Split both do), so take everything after the
        # name rather than just the third segment.
        parts = tip.split("<br>")
        rows.append({
            "bit": key,
            "struct": sid,
            "name": parts[1].strip() if len(parts) > 1 else "",
            "level": rec.get("level", ""),
            "effect": " ".join(p.strip() for p in parts[2:]).strip(),
            "sprite_on": rec.get("sprite_on", ""),
            "sprite_off": rec.get("sprite_off", ""),
            "members": rec.get("members", ""),
        })
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--recon", type=Path, default=ROOT / "build/rs558-recon",
                    help="tree produced by `cachepack unpack --assets=sprites`")
    ap.add_argument("--out", type=Path, default=ROOT / "docs/rs558_ancient_curses")
    args = ap.parse_args()

    sprites = args.recon / "sprites"
    if not sprites.is_dir():
        ap.error(f"no sprites in {args.recon}; run cachepack unpack --assets=sprites first")

    sheets = args.out / "sheets"
    tables = args.out / "tables"
    sheets.mkdir(parents=True, exist_ok=True)
    tables.mkdir(parents=True, exist_ok=True)

    # ---- spritesheets -----------------------------------------------------
    def gather_rs558(ids: range) -> list[tuple[str, Image.Image]]:
        out = []
        for sid in ids:
            d = sprites / f"sprite_{sid}"
            for i, img in enumerate(load_frames(d)):
                label = str(sid) if len(list(d.glob("*.bmp"))) == 1 else f"{sid}.{i}"
                out.append((label, img))
        return out

    made = []
    for name, ids, title in (
        ("rs558_curse_icons_a", RS558_ICONS_A,
         "rev558 curse icons, block A (sprite 2321-2346)"),
        ("rs558_curse_icons_b", RS558_ICONS_B,
         "rev558 curse icons, block B (sprite 2347-2372)"),
    ):
        entries = gather_rs558(ids)
        if not entries:
            continue
        sheet = contact_sheet(entries, columns=9, cell=30, title=title)
        path = sheets / f"{name}.png"
        sheet.save(path)
        made.append((path, len(entries)))

    # Both rev558 blocks stacked, so the on/off pairing is checkable by eye.
    pair_entries: list[tuple[str, Image.Image]] = []
    for a, b in zip(RS558_ICONS_A, RS558_ICONS_B):
        for sid in (a, b):
            for img in load_frames(sprites / f"sprite_{sid}"):
                pair_entries.append((str(sid), img))
    if pair_entries:
        sheet = contact_sheet(
            pair_entries, columns=10, cell=30,
            title="rev558 curse icons, A/B paired (offset 26) - each column pair is one curse")
        path = sheets / "rs558_curse_icons_paired.png"
        sheet.save(path)
        made.append((path, len(pair_entries)))

    # The book as the client actually orders it: 20 curses, on over off, each
    # labelled with its name. This is the sheet worth reading.
    curses = curse_rows(args.recon)
    book: list[tuple[str, Image.Image]] = []
    for row in curses:
        for state in ("sprite_on", "sprite_off"):
            sid = row[state]
            if not sid:
                continue
            for img in load_frames(sprites / f"sprite_{sid}"):
                tag = row["name"] if state == "sprite_on" else f"  {sid} off"
                book.append((tag[:16], img))
    if book:
        sheet = contact_sheet(
            book, columns=8, cell=30,
            title="Ancient Curses in book order (rev558 enum 863 -> struct 888-907); each pair is on, then off")
        path = sheets / "rs558_curses_book_order.png"
        sheet.save(path)
        made.append((path, len(book)))

    # osrs239 Ruinous Powers, for comparison against what already ships.
    if OSRS239_SPRITES.is_dir():
        entries = []
        for setname in OSRS239_ICON_SETS:
            for idx in range(24):
                d = OSRS239_SPRITES / f"{setname}_{idx}"
                for img in load_frames(d):
                    entries.append((f"{setname[-8:]}.{idx}", img))
        if entries:
            sheet = contact_sheet(
                entries, columns=12, cell=30,
                title="osrs239 Ruinous Powers icons (sprite 4842-4889) - the set already in the target cache")
            path = sheets / "osrs239_ruinous_icons.png"
            sheet.save(path)
            made.append((path, len(entries)))

    # ---- tables -----------------------------------------------------------
    if curses:
        with (tables / "curses_book.csv").open("w", newline="") as fh:
            w = csv.DictWriter(fh, fieldnames=list(curses[0].keys()))
            w.writeheader()
            w.writerows(curses)
        # Markdown too: this table is the spine of ANCIENT_CURSES.md and is
        # meant to be read, not just parsed.
        with (tables / "curses_book.md").open("w") as fh:
            fh.write("| bit | curse | lvl | icon on/off | effect (rev558 client text) |\n")
            fh.write("|----:|-------|----:|-------------|------------------------------|\n")
            for r in curses:
                fh.write(f"| {r['bit']} | {r['name']} | {r['level']} | "
                         f"{r['sprite_on']} / {r['sprite_off']} | {r['effect']} |\n")

    spot = parse_config_block(
        args.recon / "configs/all.spotanim", "spotanim", SPOTANIM_RANGE)
    if spot:
        with (tables / "rs558_curse_spotanims.csv").open("w", newline="") as fh:
            w = csv.writer(fh)
            w.writerow(["spotanim", "model", "anim", "ambient", "contrast",
                        "recol1s", "recol1d", "recol2s", "recol2d", "other"])
            for sid in SPOTANIM_RANGE:
                r = spot.get(sid)
                if r is None:
                    continue
                known = {"model", "anim", "ambient", "contrast",
                         "recol1s", "recol1d", "recol2s", "recol2d"}
                other = ";".join(f"{k}={v}" for k, v in r.items() if k not in known)
                w.writerow([sid, r.get("model", ""), r.get("anim", ""),
                            r.get("ambient", ""), r.get("contrast", ""),
                            r.get("recol1s", ""), r.get("recol1d", ""),
                            r.get("recol2s", ""), r.get("recol2d", ""), other])

    with (tables / "rs558_curse_sprites.csv").open("w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["sprite", "block", "frames", "palette",
                    "w", "h", "inner_w", "inner_h", "off_x", "off_y"])
        for block, ids in (("A", RS558_ICONS_A), ("B", RS558_ICONS_B)):
            for sid in ids:
                meta = read_pack_meta(sprites / f"sprite_{sid}" / "pack.meta")
                f0 = meta["frames"].get(0, [])
                w.writerow([sid, block, meta["count"], meta["palette"]] +
                           list(f0) + [""] * (6 - len(f0)))

    print(f"sheets -> {sheets}")
    for path, n in made:
        print(f"  {path.name:38s} {n:4d} tiles")
    print(f"tables -> {tables}")
    for t in sorted(tables.glob("*.csv")):
        print(f"  {t.name:38s} {sum(1 for _ in t.open()) - 1:4d} rows")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
