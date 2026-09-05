#!/usr/bin/env python3
"""Pixel assertions for gameframe_matrix.sh (standard library only)."""
import argparse
import collections
import json
import re
import struct
from pathlib import Path


def read_bmp(path):
    data = Path(path).read_bytes()
    if data[:2] != b"BM":
        raise ValueError("capture is not a BMP")
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height, planes, bits, compression = struct.unpack_from("<iiHHI", data, 18)
    if width <= 0 or not height or planes != 1 or bits not in (24, 32) or compression:
        raise ValueError("unsupported BMP capture")
    stride = ((width * bits + 31) // 32) * 4
    if len(data) < offset + stride * abs(height):
        raise ValueError("truncated BMP capture")
    rows = []
    for y in range(abs(height)):
        sy = abs(height) - 1 - y if height > 0 else y
        start = offset + stride * sy
        rows.append([tuple(data[start + x * (bits // 8):start + x * (bits // 8) + 3])
                     for x in range(width)])
    return width, abs(height), rows


def check_live_surfaces(rows, log, frame, root, minimap_state, server_hide, failures):
    height, width = len(rows), len(rows[0])
    def report(name, valid, detail=""):
        print(f"PIXEL {name}={'PASS' if valid else 'FAIL'} {detail}")
        if not valid:
            failures.append(name)
    def pixels(box):
        x, y, w, h = box
        return [rows[yy][xx] for yy in range(max(y, 0), min(y+h, height))
                for xx in range(max(x, 0), min(x+w, width))]
    def inside(box):
        x, y, w, h = box
        return x >= 0 and y >= 0 and w > 0 and h > 0 and x+w <= width and y+h <= height
    parts = {name: tuple(map(int, box)) for name, *box in re.findall(
        r"UI_PART name=(\S+) visible=1 box=(-?\d+),(-?\d+) (\d+)x(\d+)", log)}
    emitted = [(int(kind), tuple(map(int, box))) for kind, *box in re.findall(
        r"EMIT_EXIT\[\d+\] kind=(\d+)[^\n]*? x=(-?\d+) y=(-?\d+) w=(\d+) h=(\d+)", log)]
    if minimap_state is not None:
        receipts = re.findall(r"frame_native: MINIMAP_TOGGLE state=(\d+)", log)
        report("native_minimap_packet_applied", bool(receipts) and int(receipts[-1]) == minimap_state)
        click_requested = "sim_click_at: frame=550" in log
        walks = len(re.findall(r"^minimap: click=", log, re.M))
        report("native_minimap_walk_permission", click_requested and walks == int(minimap_state in (0,3)),
               f"walks={walks} expected={int(minimap_state in (0,3))}")
    hidden_type = -1
    if server_hide:
        uid, hidden = map(int, server_hide.split(":"))
        report("server_interface_hide_applied", f"if_sethide: com={uid} hide={hidden} applied=1" in log)
        present = bool(re.search(rf"EMIT_EXIT[^\n]*com=0x{uid:08x} ", log))
        report("server_interface_hide_paint", present == (not hidden), f"painted={int(present)}")
        match = re.search(rf"BOUNDS com=0x{uid:08x} [^\n]*type=(\d+)", log)
        if hidden and match:
            hidden_type = int(match[1])
    state = minimap_state if minimap_state is not None else 0
    for kind, name, expected in ((11, "map_window_non_sky_ink", state in (0,1,3,4) and hidden_type != 2),
                                 (12, "compass_window_ink", state in (0,1,2) and hidden_type != 1)):
        boxes = [box for k, box in emitted if k == kind]
        valid = len(boxes) == int(expected)
        detail = f"emitted={len(boxes)} expected={int(expected)}"
        if expected and len(boxes) == 1:
            x,y,w,h = boxes[0]
            # Interior avoids the surrounding frame, which stays visible when
            # native content is hidden. Map tiles have repeated palette ink;
            # an overpainting 3D viewport has a much broader shaded palette.
            ps = pixels((x+w//4,y+h//4,w//2,h//2))
            counts = collections.Counter(ps)
            unique = len(counts)
            if kind == 11:
                valid &= 8 <= unique <= 256 and counts.most_common(1)[0][1] < len(ps)*0.9 and sum(n for _,n in counts.most_common(2)) > len(ps)*.4
            else:
                valid &= unique >= 8 and any(r > 100 and r > g*1.5 and r > b*1.5 for b,g,r in ps)
            detail += f" interior_colors={unique}"
        report(name, valid, detail)

    fixture = json.loads((Path(__file__).parent / "testdata/gameframe/orb-rim.json").read_text())
    discs = 0
    for name in ("hitpoints", "prayer", "run", "special"):
        box = parts.get("frame.orb."+name)
        if not box or not inside(box):
            continue
        x,y,_,_ = box
        matches = sum(rows[y+dy][x+dx] == tuple(reversed(rgb))
                      for (dx,dy),rgb in zip(fixture["points"], fixture["rgb"]))
        discs += matches >= 15
    report("orb_column_four_discs", discs == 4, f"discs={discs}")

    controls = []
    for child, hidden, *box in re.findall(
            r"BOUNDS[^\n]*\(162\|(5|8|12|16|20|24|28|32)\)[^\n]*hidden=(\d+)[^\n]*abs=(-?\d+),(-?\d+) (\d+)x(\d+)", log):
        if hidden == "0":
            controls.append((int(child),tuple(map(int,box))))
    report("controls_inside_canvas", len(controls) == (7 if root == 601 else 8)
           and all(inside(box) for _,box in controls))
    # Six filter cells contain green On/Off text. Count actual ink, so boxes
    # surviving beneath a world draw or an opaque overlay cannot pass.
    modes = sum(any(g > 150 and r < 100 and b < 100 for b,g,r in pixels(box))
                for child,box in controls if child not in (5,32))
    report("filter_modes_visible", modes == 6, f"green_cells={modes}")
    if frame in ("gameframe-layout/classic-fixed", "mobile-gameframe/stone-drawer"):
        fractions = []
        for name in ("frame.chat.backing", "frame.chat.bar"):
            ps = pixels(parts.get(name,(0,0,0,0)))
            fractions.append(sum(r>140 and g>100 and r>g>b for b,g,r in ps)/len(ps) if ps else -1)
        report("chat_backing_parchment_bar_rock", fractions[0] > .65 and 0 <= fractions[1] < .1,
               f"warm_backing={fractions[0]:.3f} warm_bar={fractions[1]:.3f}")


def check(path, frame, root, bounds_path=None, minimap_state=None, server_hide=None):
    width, height, rows = read_bmp(path)
    failures = []
    if frame == "gameframe-layout/classic-fixed":
        # The approved plain-rock band spans x=0..495, y=467..498.
        # Its 29-column source repeats without any of the four old recesses.
        # Checking all repeats also catches partially covered/late old art.
        # Mobile puts its message area below the filters, covering part of
        # this strip. Test the exposed rock, not the chat painted over it.
        backing = None
        if bounds_path:
            match = re.search(r"BOUNDS[^\n]*\(162\|37\)[^\n]*abs=(-?\d+),(-?\d+) (\d+)x(\d+)",
                              Path(bounds_path).read_text())
            if match:
                backing = tuple(map(int, match.groups()))
        def exposed(x, y):
            if backing is None:
                return True
            bx, by, bw, bh = backing
            return not (bx <= x < bx + bw and by <= y < by + bh)
        valid = width >= 496 and height >= 499 and (root != 601 or backing is not None)
        if valid:
            valid = all(rows[y][x] == rows[y][x % 29]
                        for y in range(467, 499) for x in range(29, 496)
                        if exposed(x, y) and exposed(x % 29, y))
            valid = valid and len({p for row in rows[467:499] for p in row[:29]}) > 3
        print(f"PIXEL no_captionless_2004_hollows={'PASS' if valid else 'FAIL'} root={root}")
        if not valid:
            failures.append("no_captionless_2004_hollows")
        if root != 601:
            # Approved running-client crop. The pre-fix 519-wide parchment
            # covered 40 columns of the old rail; its surviving right edge
            # does not match this complete re-cut border.
            fixture = Path(__file__).parent / "testdata/gameframe/classic-chat-rail.bmp"
            rw, rh, expected = read_bmp(fixture)
            valid = width >= 536 + rw and height >= 357 + rh
            if valid:
                valid = all(rows[357 + y][536:536 + rw] == expected[y] for y in range(rh))
            print(f"PIXEL chat_inside_complete_surround={'PASS' if valid else 'FAIL'} root={root}")
            if not valid:
                failures.append("chat_inside_complete_surround")
    if bounds_path:
        check_live_surfaces(rows, Path(bounds_path).read_text(), frame, root, minimap_state, server_hide, failures)
    return failures


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture")
    parser.add_argument("--frame", required=True)
    parser.add_argument("--root", required=True, type=int)
    parser.add_argument("--bounds", help="matching TORIRS_DUMP_BOUNDS log")
    parser.add_argument("--minimap-state", type=int, choices=range(6))
    parser.add_argument("--server-hide", help="expected native component uid:hide receipt")
    args = parser.parse_args()
    try:
        raise SystemExit(bool(check(args.capture, args.frame, args.root, args.bounds, args.minimap_state, args.server_hide)))
    except (OSError, ValueError, struct.error) as error:
        print(f"PIXEL capture=FAIL: {error}")
        raise SystemExit(1)
