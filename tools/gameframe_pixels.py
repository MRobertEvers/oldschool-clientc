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


def read_asset_png(path):
    """Read the shipped noninterlaced RGBA8 frame art without a Pillow dependency."""
    import zlib
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("frame art is not PNG")
    width, height, depth, colour, compression, filtering, interlace = struct.unpack(">IIBBBBB", data[16:29])
    if (depth, colour, compression, filtering, interlace) != (8, 6, 0, 0, 0):
        raise ValueError("frame-art oracle requires noninterlaced RGBA8 PNG")
    packed = bytearray()
    at = 8
    while at < len(data):
        size = struct.unpack_from(">I", data, at)[0]
        if data[at+4:at+8] == b"IDAT":
            packed.extend(data[at+8:at+8+size])
        at += size + 12
    raw = zlib.decompress(packed)
    stride = width * 4
    if len(raw) != height * (stride + 1):
        raise ValueError("frame-art PNG has an unexpected decoded length")
    rows, previous = [], bytearray(stride)
    for y in range(height):
        method = raw[y*(stride+1)]
        row = bytearray(raw[y*(stride+1)+1:(y+1)*(stride+1)])
        for x in range(stride):
            left, above = row[x-4] if x >= 4 else 0, previous[x]
            corner = previous[x-4] if x >= 4 else 0
            if method == 0: predictor = 0
            elif method == 1: predictor = left
            elif method == 2: predictor = above
            elif method == 3: predictor = (left + above) // 2
            elif method == 4:
                prediction = left + above - corner
                distances = (abs(prediction-left), abs(prediction-above), abs(prediction-corner))
                predictor = (left, above, corner)[distances.index(min(distances))]
            else: raise ValueError("unknown PNG row filter")
            row[x] = (row[x] + predictor) & 255
        rows.append([tuple(row[x:x+4]) for x in range(0, stride, 4)])
        previous = row
    return width, height, rows


def check_classic_chat_pack(rows, log, failures):
    """Preserve native content and compare both full-height rails with authored art."""
    height, width = len(rows), len(rows[0])
    boxes = {int(child): tuple(map(int, box)) for child, *box in re.findall(
        r"BOUNDS[^\n]*\(162\|(\d+)\)[^\n]*hidden=0[^\n]*abs=(-?\d+),(-?\d+) (\d+)x(\d+)", log)}
    pack, input_line, scroll, bar = (boxes.get(i) for i in (0, 57, 58, 3))
    def inside(inner, outer):
        if not inner or not outer: return False
        x,y,w,h = inner; ox,oy,ow,oh = outer
        return w > 0 and h > 0 and x >= ox and y >= oy and x+w <= ox+ow and y+h <= oy+oh
    controls = [boxes.get(i) for i in (5,8,12,16,20,24,28,32)]
    # The authored desktop chat contains 142px of log/input plus its23px bar.
    # The scroll window must retain eight14px rows and its2px breathing room.
    content = (pack is not None and pack[2:] == (519,165) and
               inside(pack, (0,0,width,height)) and inside(input_line, pack) and
               input_line[3] == 16 and inside(scroll, pack) and scroll[3] >= 114 and
               inside(bar, pack) and bar[3] == 23 and
               input_line[1]+input_line[3] <= bar[1] and
               all(inside(control, bar) for control in controls))
    print(f"PIXEL chat_pack_native_content={'PASS' if content else 'FAIL'} pack={pack} scroll={scroll} input={input_line}")
    if not content: failures.append("chat_pack_native_content")
    valid, checked = bool(content), 0
    if valid:
        px,py,pw,ph = pack
        art_dir = Path(__file__).resolve().parent.parent / "script/plugins/assets/gameframe-layout"
        # The bottom-left sidebar stone/icon legitimately paints above the
        # right rail. Exclude only this frame owner's visible icon/face boxes,
        # and require at least three quarters of EACH rail to remain sampled.
        owner_match = re.search(
            rf"OWNED_WIDGET owner=(\d+) key=piece\.\d+ node=\d+ box={px+pw},{py},17,{ph}[^\n]*hidden=0", log)
        owner = owner_match[1] if owner_match else None
        covers = [tuple(map(int, box)) for found_owner, *box in re.findall(
            r"OWNED_WIDGET owner=(\d+) key=(?:icon|face)\.\d+ node=\d+ box=(-?\d+),(-?\d+),(\d+),(\d+)[^\n]*hidden=0", log)
            if found_owner == owner]
        valid = owner is not None
        for filename, x0 in (("classic_backleft2.png", px-17), ("classic_backvmid3.png", px+pw)):
            sw,sh,source = read_asset_png(art_dir/filename)
            if not inside((x0,py,17,ph),(0,0,width,height)):
                valid = False
                continue
            rail_checked = 0
            for y in range(ph):
                mirrored = y % (2*sh)
                sy = mirrored if mirrored < sh else 2*sh-1-mirrored
                for x in range(17):
                    r,g,b,a = source[sy][x*sw//17]
                    covered = any(cx <= x0+x < cx+cw and cy <= py+y < cy+ch
                                  for cx,cy,cw,ch in covers)
                    if a == 255 and not covered:
                        checked += 1
                        rail_checked += 1
                        if rows[py+y][x0+x] != (b,g,r): valid = False
            valid = valid and rail_checked >= 17*ph*3//4
    print(f"PIXEL chat_inside_complete_surround={'PASS' if valid else 'FAIL'} authored_pixels={checked}")
    if not valid: failures.append("chat_inside_complete_surround")


ORB_NAMES = ("hitpoints", "prayer", "run", "special")


def expected_orb_keys(spec=None, enabled=True):
    names = list(ORB_NAMES) if spec is None else ([name.strip() for name in spec.split(",")] if spec else [])
    if len(set(names)) != len(names) or any(name not in ORB_NAMES for name in names):
        raise ValueError("expected orbs must be a unique comma-separated subset of hitpoints,prayer,run,special")
    return {"orb_" + name for name in names} if enabled else set()


def check_expected_orb_set(actual, expected, failures):
    valid = set(actual) == set(expected)
    print(f"PIXEL orbs_expected_set={'PASS' if valid else 'FAIL'} expected={','.join(sorted(expected))} actual={','.join(sorted(actual))}")
    if not valid:
        failures.append("orbs_expected_set")
    return valid


def check_live_surfaces(rows, log, frame, root, minimap_state, server_hide, failures, native_baseline=False, public_chat_mode="on", expected_orbs=None):
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
    # What the tree reports at exit: every widget a plugin OWNS, by the key
    # the plugin created it under, and every semantic ROLE with the box of
    # the lane's own node it resolves to. A hidden one is not on screen.
    owned = {key: tuple(map(int, box)) for key, *box in re.findall(
        r"OWNED_WIDGET owner=\d+ key=(\S+) node=\d+ box=(-?\d+),(-?\d+),(\d+),(\d+)[^\n]* hidden=0", log)}
    roles = {name: tuple(map(int, box)) for name, *box in re.findall(
        r"ROLE_WIDGET role=(\S+) node=\d+ com=0x[0-9a-f]+ box=(-?\d+),(-?\d+),(\d+),(\d+) hidden=0", log)}
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
    # The exit draw list, in paint order: what covers a rim point is whatever
    # painted there AFTER the orb's own commands. The mobile toplevel (601)
    # seats the special orb's plate nine rows into the run orb's box, the
    # resizable toplevel at its floor (765 wide) puts its stone row over the
    # special orb, and the adviser sits on its right edge -- all natively
    # (gf-stone-osrs601-baseline/m31, gf-review-m11-baseline/m11 with every
    # plugin off show the same covers). A covered point is that later
    # command's pixel and is not sampled; whatever is left must still match
    # but one, from at least eight, which keeps the rule red for a disc that
    # moved or never drew (a box shifted three pixels reads discs=3).
    draw_list = [(int(i), tuple(map(int, box))) for i, *box in re.findall(
        r"EMIT_EXIT\[(\d+)\] kind=\d+ com=0x[0-9a-f]+[^\n]*? x=(-?\d+) y=(-?\d+) w=(\d+) h=(\d+)", log)]
    # The minimap-orbs plugin's own plates, the owned images it keys
    # orb_hitpoints .. orb_special (minimap_orbs.c ORB_PART); each sits in the
    # lane's orb layer at the plate's 57x34.
    expected = expected_orb_keys(expected_orbs)
    discs = 0
    for name in ORB_NAMES:
        if "orb_" + name not in expected:
            continue
        box = owned.get("orb_"+name)
        if not box or not inside(box):
            continue
        x,y,w,h = box
        own = [i for i,(ex,ey,ew,eh) in draw_list if ex >= x and ey >= y and ex+ew <= x+w and ey+eh <= y+h]
        last_own = max(own) if own else -1
        points = [((dx,dy),rgb) for (dx,dy),rgb in zip(fixture["points"], fixture["rgb"])
                  if not any(i > last_own and ex <= x+dx < ex+ew and ey <= y+dy < ey+eh
                             for i,(ex,ey,ew,eh) in draw_list)]
        matches = sum(rows[y+dy][x+dx] == tuple(reversed(rgb)) for (dx,dy),rgb in points)
        discs += len(points) >= 8 and matches >= len(points) - 1
    # The four discs are the minimap-orbs PLUGIN's owned pictures, so the rule
    # is a statement about that plugin. A capture that never ran it -- a
    # single-plugin run of something else, the widget demo, a manifest without
    # it -- has no discs to find and used to read `discs=0 FAIL`, which says
    # nothing about the frame under test. The run's own exit dump answers
    # whether the plugin was live: PLUGIN_STATE. Absent or not running, the
    # rule does not apply and says so; running, the explicitly requested set must
    # be present. The default remains all four; capability/config probes name
    # their expected set independently of whatever the plugin happened to draw.
    orbs_live = re.search(r"^PLUGIN_STATE id=minimap-orbs enabled=1 running=1", log, re.M)
    if not native_baseline:
        if orbs_live:
            actual = {key for key in owned if key.startswith("orb_")}
            check_expected_orb_set(actual, expected, failures)
            rule = "orb_column_four_discs" if expected_orbs is None else "orb_column_expected_discs"
            report(rule, discs == len(expected), f"discs={discs} expected={len(expected)}")
        else:
            print("PIXEL orb_column_four_discs=SKIP minimap-orbs not running")

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
    report("filter_modes_visible", modes == (5 if public_chat_mode=="friends" else 6), f"green_cells={modes}")
    if frame in ("gameframe-layout/classic-fixed", "mobile-gameframe/stone-drawer"):
        # The chat pack's own backing and bar, found by the roles the frame
        # plugin dressed them through (gameframe.c frame_chat_dress,
        # mobile_gameframe.c: set_image on the chat_backing / chat_bar role
        # nodes). Their boxes are the lane's; the pixels are the plugin's.
        fractions = []
        for name in ("chat_backing", "chat_bar"):
            ps = pixels(roles.get(name,(0,0,0,0)))
            fractions.append(sum(r>140 and g>100 and r>g>b for b,g,r in ps)/len(ps) if ps else -1)
        report("chat_backing_parchment_bar_rock", fractions[0] > .65 and 0 <= fractions[1] < .1,
               f"warm_backing={fractions[0]:.3f} warm_bar={fractions[1]:.3f}")


def check_owned_operation(log, language, failures):
    """The owned control was armed, the simulated click released inside its
    current canvas bounds, and its operation ran and invoked the native action.

    Containment is read from the client's own sim_click_at trace, not from the
    harness input, so a click that missed the control fails here even when the
    native filter happened to change for another reason."""
    if language == "c":
        bounds = re.findall(r"WIDGET_DEMO_OP_BOUNDS armed=(-?\d+) x=(-?\d+) y=(-?\d+) w=(\d+) h=(\d+)", log)
        armed = bool(bounds) and all(int(b[0]) == 0 for b in bounds)
        boxes = [tuple(map(int, b[1:])) for b in bounds]
        fired = re.findall(r"WIDGET_DEMO_OP result=(-?\d+) registration=(\d+)", log)
        ran = len(fired) >= 1 and all(int(r) == 0 and int(reg) > 0 for r, reg in fired)
    else:
        bounds = re.findall(r"LUA_WIDGET_DEMO_OP_BOUNDS (-?\d+) (-?\d+) (\d+) (\d+)", log)
        armed = bool(bounds)
        boxes = [tuple(map(int, b)) for b in bounds]
        fired = re.findall(r"LUA_WIDGET_DEMO_OP (\w+) (\w+)", log)
        ran = len(fired) >= 1 and all(ok == "true" and reason == "ok" for ok, reason in fired)
    releases = [tuple(map(int, m)) for m in re.findall(r"sim_click_at: released (-?\d+),(-?\d+)", log)]
    inside = bool(boxes) and bool(releases) and all(
        any(x <= rx < x + w and y <= ry < y + h for x, y, w, h in boxes) for rx, ry in releases)
    print(f"PIXEL owned_widget_armed={'PASS' if armed else 'FAIL'} language={language} bounds={len(bounds)}")
    if not armed: failures.append("owned_widget_armed")
    print(f"PIXEL owned_widget_click_inside={'PASS' if inside else 'FAIL'} releases={len(releases)}")
    if not inside: failures.append("owned_widget_click_inside")
    print(f"PIXEL owned_widget_operation_ran={'PASS' if ran else 'FAIL'} operations={len(fired)}")
    if not ran: failures.append("owned_widget_operation_ran")


def check_screenshot_saved(log, failures):
    """A plugin reported a capture and the file it named exists with PNG bytes.

    The path comes from the client's own log line, so a plugin that reported
    success without writing, or wrote somewhere else, fails here."""
    paths = re.findall(r"\] captured (\S+\.png)", log)
    saved = []
    for path in paths:
        p = Path(path)
        try:
            saved.append(p.is_file() and p.stat().st_size > 8 and p.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n")
        except OSError:
            saved.append(False)
    ok = bool(saved) and all(saved)
    print(f"PIXEL screenshot_saved={'PASS' if ok else 'FAIL'} reported={len(paths)} files={sum(saved)}")
    if not ok: failures.append("screenshot_saved")


def check_report_replaced(log, failures):
    """The plugin's report-slot camera lies inside a native control whose
    presentation the plugin hid: native paint and input off, native hide still
    zero (server state intact), while the camera itself is a painted owned
    graphic. Read from the final publication only."""
    final = log[log.rfind("NATIVE_ROOT id="):] if "NATIVE_ROOT id=" in log else log
    cams = [tuple(map(int, m)) for m in re.findall(r"SCREENSHOT_CAMERA camera_report (-?\d+) (-?\d+) (\d+) (\d+)", log)]
    cam = cams[-1] if cams else None
    nodes = re.findall(r"NATIVE_UI node=\d+[^\n]*com=(-?\d+) type=(\w+) hidden=(\d) native_paint=(\d) native_input=(\d) native_hide=(\d)[^\n]*box=(-?\d+),(-?\d+),(\d+),(\d+)", final)
    def contains(outer, inner):
        ox, oy, ow, oh = outer; ix, iy, iw, ih = inner
        return ox <= ix and oy <= iy and ix + iw <= ox + ow and iy + ih <= oy + oh
    hidden_hosts = [n for n in nodes if cam and n[2] == "0" and n[3] == "0" and n[4] == "0" and n[5] == "0"
                    and contains(tuple(map(int, n[6:])), cam) and n[1] in ("rs_graphic", "rs_layer", "chat_button")]
    camera_nodes = [n for n in nodes if cam and n[0] == "-1" and n[1] == "rs_graphic" and n[3] == "1"
                    and tuple(map(int, n[6:])) == cam]
    print(f"PIXEL report_control_plugin_hidden={'PASS' if hidden_hosts else 'FAIL'} hosts={len(hidden_hosts)} camera={cam}")
    if not hidden_hosts: failures.append("report_control_plugin_hidden")
    print(f"PIXEL report_camera_painted={'PASS' if camera_nodes else 'FAIL'} nodes={len(camera_nodes)}")
    if not camera_nodes: failures.append("report_camera_painted")


def check_highlight_color(rows, log, spec, failures):
    """A cache highlight group was recorded live by the engine AND its exact
    colour is painted. spec is RRGGBB[:min_pixels]. The NATIVE_HIGHLIGHT line is
    the engine's own record of the scripts' group, independent of the renderer;
    the pixel count is what the renderer actually put on the canvas."""
    color, _, minimum = spec.partition(":")
    rgb = int(color, 16); wanted = int(minimum) if minimum else 20
    bgr = (rgb & 255, (rgb >> 8) & 255, (rgb >> 16) & 255)
    groups = [m for m in re.findall(r"NATIVE_HIGHLIGHT kind=(\d+) group=(\d+) colour=([0-9a-f]{6}) outline=\d+ opacity=\d+ flags=\d+ members=(\d+)", log)
              if int(m[2], 16) == (rgb & 0xffffff) and int(m[3]) > 0]
    ink = sum(pixel == bgr for row in rows for pixel in row)
    print(f"PIXEL native_highlight_group={'PASS' if groups else 'FAIL'} colour={color} live_groups_with_members={len(groups)}")
    if not groups: failures.append("native_highlight_group")
    print(f"PIXEL highlight_painted={'PASS' if ink >= wanted else 'FAIL'} colour={color} pixels={ink} wanted={wanted}")
    if ink < wanted: failures.append("highlight_painted")


def check_panel_custom_ink(rows, log, spec, failures):
    """A plugin page's custom row was allotted a region (PLUGIN_PANEL_CUSTOM, the
    executor's own record) and the plugin painted into it: at least MIN distinct
    colours inside the region, so a blank plate or a single fill cannot pass.
    spec is ID[:MIN]."""
    row_id, _, minimum = spec.partition(":")
    wanted = int(minimum) if minimum else 12
    regions = re.findall(rf"PLUGIN_PANEL_CUSTOM id={re.escape(row_id)} region=(-?\d+),(-?\d+),(\d+),(\d+)", log)
    colours = set()
    if regions:
        x, y, w, h = map(int, regions[-1])
        for yy in range(max(0, y), min(len(rows), y + h)):
            for xx in range(max(0, x), min(len(rows[0]), x + w)):
                colours.add(rows[yy][xx])
    ok = bool(regions) and len(colours) >= wanted
    print(f"PIXEL panel_custom_ink={'PASS' if ok else 'FAIL'} id={row_id} regions={len(regions)} colours={len(colours)} wanted={wanted}")
    if not ok: failures.append("panel_custom_ink")


def check_rs289(rows, log, failures, scenario="baseline", frame="core/native", public_chat_mode="on", report_replaced=False):
    """Revconfig controls, plus evidence that actual mounted CS1 ran.

    The RS2 frame has three mode controls and Report. Do not borrow the
    eight-control IF3 rule or the six-state OSRS minimap protocol here.
    """
    height, width = len(rows), len(rows[0])
    def report(name, valid, detail=""):
        print(f"PIXEL {name}={'PASS' if valid else 'FAIL'} {detail}")
        if not valid:
            failures.append(name)
    boxes = [tuple(map(int, values)) for values in re.findall(
        r"NATIVE_UI[^\n]*type=chat_button hidden=0 native_paint=1[^\n]*box=(-?\d+),(-?\d+),(\d+),(\d+)", log)]
    painted = 3 if report_replaced else 4
    report("rs289_four_chat_controls", len(boxes) == painted, f"controls={len(boxes)} expected={painted}")
    report("rs289_controls_inside_canvas", len(boxes) == painted and all(
        x >= 0 and y >= 0 and w > 0 and h > 0 and x+w <= width and y+h <= height
        for x,y,w,h in boxes))
    modes = captions = 0
    for x,y,w,h in boxes:
        ps = [rows[yy][xx] for yy in range(max(0,y),min(height,y+h))
              for xx in range(max(0,x),min(width,x+w))]
        modes += sum(g > 150 and r < 100 and b < 100 for b,g,r in ps) >= 10
        captions += sum(min(b,g,r) > 180 and max(b,g,r)-min(b,g,r) < 30 for b,g,r in ps) >= 20
    report("rs289_live_chat_modes", modes == (2 if public_chat_mode=="friends" else 3), f"green_cells={modes}")
    report("rs289_chat_captions", captions == painted, f"captions={captions} expected={painted}")
    report("rs289_actual_cs1_values", bool(re.search(r"^NATIVE_CS1 com=\d+ incarnation=[1-9]\d* value\[0\]=[1-9]\d*", log, re.M)))
    report("rs289_mounted_cache_interfaces", bool(re.search(
        r"NATIVE_UI[^\n]*com=[1-9]\d* type=rs_\w+ hidden=0 native_paint=1", log)))
    report("rs289_revision", "cache profile epoch=dat1 game=rs2 revision=289" in log
           or "NATIVE_REVISION epoch=dat1 game=rs2 revision=289" in log)
    if frame not in ("core/native", "auto"):
        selections = re.findall(r"^frame_selection:.*?active=(\S+)", log.split("BOUNDS",1)[0], re.M)
        report("rs289_requested_frame_active", bool(selections) and selections[-1] == frame)
    if scenario == "stats":
        update = log.partition("sent ::setstat strength 20")[2]
        report("rs289_stat_packet_applied", bool(re.search(
            r"NATIVE_PACKET UPDATE_STAT applied stat=2 level=20 xp=\d+", update)))
        report("rs289_stat_cs1_readback", all(re.search(
            rf"NATIVE_CS1 com={uid} incarnation=\d+ value\[0\]=20", update)
            for uid in (4006, 4007)))
    if scenario == "skill-guide":
        packets = {int(uid): int(hidden) for uid,hidden in re.findall(
            r"NATIVE_PACKET IF_SETHIDE received com=(\d+) hide=([01])", log)}
        expected = {8844:1, 8813:0, 8825:1, 8828:1, 8838:1, 8841:1, 8850:1, 8860:1, 8863:1}
        report("rs289_skill_guide_packets", all(packets.get(uid) == hidden for uid,hidden in expected.items()))
        # The packet arrives before mounting. The final incarnation must carry
        # the latest server value and BOTH native availability consequences.
        report("rs289_skill_guide_hide_after_mount", all(re.search(
            rf"NATIVE_UI[^\n]*com={uid} type=rs_layer hidden={hidden} native_paint={1-hidden} native_input={1-hidden} native_hide={hidden} ", log)
            for uid,hidden in expected.items()))


def check_xp_orbs(rows, log, enabled, failures):
    """XP globes as owned image controls: the plugin's XP_ORBS_GLOBE lines give
    each live slot's last canvas box; every box is inside the canvas and its
    picture is painted (a globe is a coloured ring around an icon, so the box
    holds many distinct colours). A Flip press is proven by XP_ORBS_FLIP."""
    def report(name, valid, detail=""):
        print(f"PIXEL {name}={'PASS' if valid else 'FAIL'} {detail}")
        if not valid: failures.append(name)
    height, width = len(rows), len(rows[0])
    slots = {}
    for slot, skill, x, y, side in re.findall(r"XP_ORBS_GLOBE slot=(\d+) skill=(\d+) x=(-?\d+) y=(-?\d+) side=(\d+)", log):
        slots[int(slot)] = (int(skill), int(x), int(y), int(side))
    report("xp_orbs_globes", (len(slots) >= 1) == bool(enabled), f"slots={len(slots)}")
    if not enabled or not slots: return
    inside = all(0 <= x and 0 <= y and x + s <= width and y + s <= height for _, x, y, s in slots.values())
    report("xp_orbs_inside_canvas", inside)
    painted = 0
    for _, x, y, s in slots.values():
        colours = {rows[yy][xx] for yy in range(y, min(height, y + s)) for xx in range(x, min(width, x + s))}
        painted += len(colours) >= 8
    report("xp_orbs_painted", painted == len(slots), f"painted={painted}")


def check_minimap_orbs(rows, log, enabled, failures, expected_orbs=None):
    """Four owned orb controls, 57x34 each, painted with their meters.

    Read from the final publication: the plugin's own MINIMAP_ORBS_CONTROL lines
    give each control's canvas box; on a lane with interface 160 the control
    must cover the native orb root exactly (native=1), elsewhere it must not
    cover the minimap disc. The lower half of each disc must carry the fill
    colour (red hitpoints, gold run) and the top row of the hitpoints disc the
    dark cap, so a composed picture that never landed fails here."""
    def report(name, valid, detail=""):
        print(f"PIXEL {name}={'PASS' if valid else 'FAIL'} {detail}")
        if not valid: failures.append(name)
    final = log[log.rfind("NATIVE_ROOT id="):] if "NATIVE_ROOT id=" in log else log
    controls = {}
    for orb, native, armed, x, y, w, h in re.findall(r"MINIMAP_ORBS_CONTROL orb=(\w+) native=(\d) armed=(\d) box=(-?\d+),(-?\d+),(\d+),(\d+)", log):
        controls[orb] = (int(native), int(armed), int(x), int(y), int(w), int(h))
    # Boot publication can include controls subsequently removed by a config
    # write. The exit-owned widgets, not historical log lines, are the set
    # that still exists; each also needs a matching final placement receipt.
    live = {key: tuple(map(int, box)) for key, *box in re.findall(
        r"OWNED_WIDGET owner=\d+ key=(orb_\w+) node=\d+ box=(-?\d+),(-?\d+),(\d+),(\d+)[^\n]* hidden=0", log)}
    expected = expected_orb_keys(expected_orbs, bool(enabled))
    present = check_expected_orb_set(live, expected, failures)
    controls = {key: value for key, value in controls.items() if key in live}
    complete = set(controls) == expected and all(c[4] == 57 and c[5] == 34 and c[2:] == live[key] for key, c in controls.items())
    report("orbs_controls", present and complete, f"count={len(controls)} expected={len(expected)}")
    if not enabled or not present or not complete or not controls: return
    minimap = re.findall(r"NATIVE_UI[^\n]*type=minimap hidden=0 native_paint=1[^\n]*box=(-?\d+),(-?\d+),(\d+),(\d+)", final)
    natives = re.findall(r"NATIVE_UI[^\n]*com=(\d+) type=rs_layer[^\n]*box=(-?\d+),(-?\d+),57,34", final)
    native_boxes = {int(c): (int(x), int(y)) for c, x, y in natives}
    height, width = len(rows), len(rows[0])
    def covers_disc(map_box, box):
        mx, my, mw, mh = map_box; x, y, w, h = box
        cx, cy, r = 2 * mx + mw, 2 * my + mh, min(mw, mh)
        return any((2 * px + 1 - cx) ** 2 + (2 * py + 1 - cy) ** 2 < r * r for py in range(y, y + h) for px in range(x, x + w))
    if all(c[0] for c in controls.values()):
        roots = {"orb_hitpoints": 160 << 16 | 7, "orb_prayer": 160 << 16 | 18, "orb_run": 160 << 16 | 26, "orb_special": 160 << 16 | 34}
        placed = all(native_boxes.get(roots[k]) == (c[2], c[3]) for k, c in controls.items())
        report("orbs_cover_native_roots", placed, f"roots={len(native_boxes)}")
    else:
        report("orbs_clear_of_minimap", len(minimap) == 1 and all(not covers_disc(tuple(map(int, minimap[0])), c[2:]) for c in controls.values()), f"minimap={len(minimap)}")
    def disc_pixels(c, rows_from, rows_to):
        x, y = c[2] + 27, c[3] + 4
        return [rows[yy][xx] for yy in range(y + rows_from, y + rows_to) for xx in range(x + 6, x + 20) if 0 <= yy < height and 0 <= xx < width]
    values = {}
    for orb, value, filled, total, inactive in re.findall(r"MINIMAP_ORBS_VALUE orb=(\w+) value=(-?\d+) filled=(-?\d+) total=(\d+) inactive=(\d)", log):
        values[orb] = (int(value), int(filled), int(total), int(inactive))
    hp = controls.get("orb_hitpoints"); run = controls.get("orb_run")
    hp_low = disc_pixels(hp, 16, 24) if hp else []
    run_low = disc_pixels(run, 16, 24) if run else []
    if hp:
        report("orbs_hitpoints_red", sum(r > g + 40 and r > b + 40 for b, g, r in hp_low) >= len(hp_low) // 2, f"pixels={len(hp_low)}")
    run_value = values.get("orb_run")
    if run and run_value and run_value[3]:
        report("orbs_run_inactive_grey", sum(abs(r - g) < 30 and abs(g - b) < 30 and 40 < max(r, g, b) < 200 for b, g, r in run_low) >= len(run_low) // 3, f"pixels={len(run_low)} (walking)")
    elif run:
        report("orbs_run_gold", sum(r > 150 and g > 100 and b < 90 for b, g, r in run_low) >= len(run_low) // 3, f"pixels={len(run_low)}")
    hp_value = values.get("orb_hitpoints")
    if hp and hp_value and hp_value[2] > 0:
        hidden = 26 - (hp_value[1] * 26 + hp_value[2] - 1) // hp_value[2]
        top = disc_pixels(hp, 0, 2)
        if hidden >= 2:
            report("orbs_hitpoints_cap_dark", sum(max(b, g, r) < 90 for b, g, r in top) >= len(top) // 2, f"hidden_rows={hidden}")
        else:
            report("orbs_hitpoints_full_no_cap", sum(r > g + 40 and r > b + 40 for b, g, r in top) >= len(top) // 2, f"hidden_rows={hidden}")


def check_performance(rows, log, enabled, metrics, position, color, failures):
    def report(name, valid, detail=""):
        print(f"PIXEL {name}={'PASS' if valid else 'FAIL'} {detail}")
        if not valid: failures.append(name)
    entries=re.findall(r"OWNED_WIDGET owner=\d+ key=performance_(\w+) node=\d+ box=(-?\d+),(-?\d+),(\d+),(\d+) len=(\d+)",log)
    keys={entry[0] for entry in entries}
    report("performance_owned_lines",len(entries)==(4 if enabled else 0) and
        keys==({"fps","frame","effective","memory"} if enabled else set()),f"count={len(entries)}")
    if not enabled: return
    visible=metrics.split(",") if metrics else []
    report("performance_visible_lines",{e[0] for e in entries if int(e[5])>0}==set(visible),f"expected={len(visible)}")
    viewport=re.findall(r"NATIVE_UI[^\n]*type=world hidden=0 native_paint=1[^\n]*box=(-?\d+),(-?\d+),\d+,\d+",log)
    report("performance_viewport",len(viewport)==1)
    if len(viewport)!=1: return
    dx,dy=map(int,position.split(","));vx,vy=map(int,viewport[0])
    rgb=int(color.lstrip("#"),16);bgr=(rgb&255,(rgb>>8)&255,(rgb>>16)&255)
    for key,x,y,w,h,length in entries:
        if key not in visible: continue
        box=tuple(map(int,(x,y,w,h)))
        expected=(vx+dx,vy+dy+3+visible.index(key)*15,132,15)
        report("performance_"+key+"_position",box==expected,f"box={box}")
        x,y,w,h=box
        ink=sum(rows[yy][xx]==bgr for yy in range(max(0,y),min(len(rows),y+h))
            for xx in range(max(0,x),min(len(rows[0]),x+w)))
        report("performance_"+key+"_ink",ink>=50,f"pixels={ink}")


def check_overlay_text(rows, log, expected, failures):
    for text in expected:
        raw=text.encode();fingerprint=14695981039346656037
        for byte in raw: fingerprint=((fingerprint^byte)*1099511628211)&((1<<64)-1)
        entries=re.findall(rf"OVERLAY_TEXT x=(-?\d+) y=(-?\d+) color=([0-9a-f]+) len={len(raw)} hash={fingerprint:016x}",log)
        entries=[(int(x),int(y),int(color,16)) for x,y,color in entries if int(color,16)!=0]
        valid=len(entries)==1
        ink=0
        if valid:
            x,y,rgb=entries[0];bgr=(rgb&255,(rgb>>8)&255,(rgb>>16)&255)
            valid=0<=x<len(rows[0]) and 14<=y<len(rows)
            half=max(20,len(text)*4)
            ink=sum(rows[yy][xx]==bgr for yy in range(max(0,y-14),min(len(rows),y+2))
                for xx in range(max(0,x-half),min(len(rows[0]),x+half)))
            valid &= ink>=max(15,len(text)*2)
        print(f"PIXEL overlay_text={'PASS' if valid else 'FAIL'} text={text!r} copies={len(entries)} ink={ink}")
        if not valid: failures.append("overlay_text")


def check_prefs(prefs_path, contains, absent, failures):
    """The run's own plugin_prefs.ini, scored as text.

    Persistence is otherwise unpinnable from a capture: what a plugin says it
    saved, what the settings page showed, and what the host actually WROTE are
    three different facts, and only the third one survives the next launch.
    A missing file fails both kinds of rule -- "the host never wrote the file"
    is not evidence that a line is absent from it, it is a different failure.
    """
    text = None
    try:
        text = Path(prefs_path).read_text()
    except OSError as error:
        for pattern in list(contains) + list(absent):
            print(f"PIXEL prefs_file=FAIL {error}")
            failures.append(f"prefs_file:{pattern}")
        return
    for pattern in contains:
        found = re.search(pattern, text, re.M) is not None
        print(f"PIXEL prefs_contains={'PASS' if found else 'FAIL'} pattern={pattern!r}")
        if not found: failures.append(f"prefs_contains:{pattern}")
    for pattern in absent:
        gone = re.search(pattern, text, re.M) is None
        print(f"PIXEL prefs_absent={'PASS' if gone else 'FAIL'} pattern={pattern!r}")
        if not gone: failures.append(f"prefs_absent:{pattern}")


def check_log_counts(log, specs, failures):
    """<regex>:<n> -- the log must carry EXACTLY n matches of that regex.

    --expect-log cannot tell "issued once" from "re-issued every frame", which
    is the whole question behind a per-frame reissue defect; and it cannot say
    "exactly one orb bound" either. The count is the last colon-separated
    field, so a regex may itself contain colons.
    """
    for spec in specs:
        pattern, _, wanted = spec.rpartition(":")
        if not pattern or not wanted.isdigit():
            print(f"PIXEL expected_log_count=FAIL malformed spec={spec!r} (want '<regex>:<n>')")
            failures.append(f"expected_log_count:{spec}")
            continue
        seen = sum(1 for _ in re.finditer(pattern, log, re.M))
        valid = seen == int(wanted)
        print(f"PIXEL expected_log_count={'PASS' if valid else 'FAIL'} pattern={pattern!r} expected={wanted} observed={seen}")
        if not valid: failures.append(f"expected_log_count:{spec}")


def check_native_caption(rows,log,text,failures):
    raw=text.encode();fingerprint=14695981039346656037
    for byte in raw: fingerprint=((fingerprint^byte)*1099511628211)&((1<<64)-1)
    entries=re.findall(rf"NATIVE_GROUND_CAPTION root=(\d+) node=\d+ painted=[1-9]\d* box=(-?\d+),(-?\d+),(\d+),(\d+) color=([0-9a-f]+) len={len(raw)} hash={fingerprint:016x}",log)
    valid=len(entries)==1;ink=[];controls=[]
    if valid:
        root,x,y,w,h,color=entries[0];x,y,w,h=map(int,(x,y,w,h));rgb=int(color,16);bgr=(rgb&255,(rgb>>8)&255,(rgb>>16)&255)
        ink=[(xx,yy) for yy in range(max(0,y),min(len(rows),y+h)) for xx in range(max(0,x),min(len(rows[0]),x+w)) if rows[yy][xx]==bgr]
        valid=len(ink)>=len(text)*2
        controls=[tuple(map(int,box)) for box in re.findall(rf"NATIVE_GROUND_CONTROL root={root} node=\d+ live=1 box=(-?\d+),(-?\d+),(\d+),(\d+)",log)]
        for bx,by,bw,bh in controls:
            if y<by+bh and by<y+h and ink:
                valid &= max(xx for xx,yy in ink)<bx or min(xx for xx,yy in ink)>=bx+bw
    print(f"PIXEL native_caption_callback={'PASS' if valid else 'FAIL'} matches={len(entries)} ink={len(ink)} controls={len(controls)}")
    if not valid: failures.append("native_caption_callback")


def check(path, frame, root, bounds_path=None, minimap_state=None, server_hide=None,
          revision="osrs239", native_baseline=False, rs289_scenario="baseline", input_state=None, native_focus_hide=False, widget_demo=None, widget_moves=1, widget_rune_slot=0, owned_text=None, owned_count=1, widget_offset=12, plugin_id=None, plugin_enabled=1, plugin_lua=False, performance_metrics="fps,frame,effective,memory", performance_position="10,25", performance_color="FFFFFF", overlay_text=None, native_ground_labels=None, native_caption=None, ground_row_gap=None, public_chat_mode="on", widget_op=False, expect_log=None, screenshot_saved=False, report_replaced=False, forbid_log=None, highlight_color=None, panel_custom_ink=None, dest_tile=False, menu_row=None, overlay_text_absent=None, native_caption_absent=None, scene_objects=None, find_all_holes=None, prefs=None, prefs_contains=None, prefs_absent=None, expect_log_count=None, expected_orbs=None):
    width, height, rows = read_bmp(path)
    failures = []
    if public_chat_mode=="friends":
        log=Path(bounds_path).read_text() if bounds_path else ""
        valid=bool(re.search(r"NATIVE_CHAT_MODES public=1 private=0 trade=0",log))
        print(f"PIXEL public_chat_native_state={'PASS' if valid else 'FAIL'} expected=friends")
        if not valid: failures.append("public_chat_native_state")
    if native_caption:
        check_native_caption(rows,Path(bounds_path).read_text() if bounds_path else "",native_caption,failures)
    if ground_row_gap is not None:
        log=Path(bounds_path).read_text() if bounds_path else ""
        groups={}
        for root,y in re.findall(r"NATIVE_GROUND_CAPTION root=(\d+) node=\d+ painted=[1-9]\d* box=-?\d+,(-?\d+),\d+,\d+ color=[0-9a-f]+ len=[1-9]\d* hash=",log):
            groups.setdefault(root,[]).append(int(y))
        piles=[sorted(values) for values in groups.values() if len(values)>1]
        valid=bool(piles) and all(all(b-a==ground_row_gap for a,b in zip(ys,ys[1:])) for ys in piles)
        print(f"PIXEL native_ground_row_gap={'PASS' if valid else 'FAIL'} expected={ground_row_gap} piles={piles}")
        if not valid: failures.append("native_ground_row_gap")

    if overlay_text:
        check_overlay_text(rows,Path(bounds_path).read_text() if bounds_path else "",overlay_text,failures)
    if native_ground_labels:
        log=Path(bounds_path).read_text() if bounds_path else ""
        entries=re.findall(r"NATIVE_GROUND_OVERLAY root=\d+ widget_hide=(\d) native_hide=(\d) emitted=(\d+) coord=-?\d+ captions=(\d+) hidden_captions=(\d+) caption_emits=(\d+) buttons=(\d+) live_buttons=(\d+)",log)
        hidden=native_ground_labels=="hidden"
        parsed=[tuple(map(int,entry)) for entry in entries]
        valid=bool(parsed) and all(root_hide==0 and native_hide==0 and captions>0 and
            h==(captions if hidden else 0) and ((e==0) if hidden else e>=captions) and live==buttons
            for root_hide,native_hide,total,captions,h,e,buttons,live in parsed)
        print(f"PIXEL native_ground_labels={'PASS' if valid else 'FAIL'} expected={native_ground_labels} roots={len(entries)} captions={sum(e[3] for e in parsed)} caption_emits={sum(e[5] for e in parsed)} native_buttons={sum(e[7] for e in parsed)}")
        if not valid: failures.append("native_ground_labels")

    if bounds_path:
        log = Path(bounds_path).read_text()
        if "after_ready=1" in log and not re.search(r"^SIM_READY elapsed_ms=\d+ tree_generation=[1-9]\d*", log, re.M):
            print("PIXEL native_readiness=FAIL")
            failures.append("native_readiness")
        # A capture whose session died before the dump still scores every other
        # rule: the chrome is retained, the plugin's own widgets are retained,
        # and the world keeps its last frame under a "Connection lost" banner.
        # The tell is the local player: NATIVE_PLAYER is printed for every live
        # session and for none that has lost one. It caught TORIRS_SIM_HOVER
        # driving four more frames on a clock restarted at 20 ms after the loop
        # -- fixed since; the pointer is parked inside the loop now -- and the
        # rule stays because any knob that rewinds or stalls the clock ends the
        # session the same silent way. Only judged when the run dumped native
        # state at all, and never on a title-screen capture.
        if re.search(r"^NATIVE_ROOT ", log, re.M) and not re.search(r"^NATIVE_PLAYER ", log, re.M):
            print("PIXEL world_session_live=FAIL no NATIVE_PLAYER at exit: the client lost its session")
            failures.append("world_session_live")
    if plugin_id:
        log = Path(bounds_path).read_text() if bounds_path else ""
        states={name:(int(enabled),int(running),int(error)) for name,enabled,running,error in re.findall(
            r"PLUGIN_STATE id=(\S+) enabled=(\d) running=(\d) error=(\d)",log)}
        expected={plugin_id,"lua"} if plugin_lua else {plugin_id}
        valid=set(states)==expected and states.get(plugin_id)==(plugin_enabled,plugin_enabled,0)
        print(f"PIXEL selected_plugin_running={'PASS' if valid else 'FAIL'} id={plugin_id}")
        if not valid: failures.append("selected_plugin_running")
        if plugin_id in ("tile-indicator-c","tile-indicator-lua") and plugin_enabled:
            cyan=sum(pixel==(255,255,0) for row in rows for pixel in row)
            valid=cyan>=30
            print(f"PIXEL true_tile_marker={'PASS' if valid else 'FAIL'} cyan_pixels={cyan}")
            if not valid: failures.append("true_tile_marker")
        if plugin_id=="drawprobe" and plugin_enabled:
            # Interior of the probe's magenta rectangle, alpha 128, above the
            # native world. The original translator dropped opacity and made
            # every pixel pure magenta. Exclude its crossing green line by
            # allowing a small minority of unmatched pixels.
            blended=sum(128<=r<255 and 128<=b<255 and g<128
                for row in rows[12:20] for b,g,r in row[15:45])
            valid=blended>=200
            print(f"PIXEL drawprobe_rect_opacity={'PASS' if valid else 'FAIL'} blended={blended}")
            if not valid: failures.append("drawprobe_rect_opacity")
        if plugin_id in ("entity-highlighter","hull-probe") and plugin_enabled:
            colors=[("mesh_hull",(255,0,255))]
            if plugin_id=="hull-probe": colors.append(("bounds_hull",(255,255,0)))
            for name,color in colors:
                ink=sum(pixel==color for row in rows for pixel in row)
                valid=ink>=30
                print(f"PIXEL {name}={'PASS' if valid else 'FAIL'} pixels={ink}")
                if not valid: failures.append(name)
        if plugin_id=="performance-display":
            check_performance(rows,log,plugin_enabled,performance_metrics,performance_position,performance_color,failures)
        if plugin_id=="minimap-orbs":
            check_minimap_orbs(rows,log,plugin_enabled,failures,expected_orbs)
        if plugin_id=="xp-drop-orbs":
            check_xp_orbs(rows,log,plugin_enabled,failures)
    if widget_demo:
        log = Path(bounds_path).read_text() if bounds_path else ""
        if widget_demo == "c":
            matches = re.findall(r"WIDGET_DEMO api=3 before=(-?\d+),(-?\d+) \d+x\d+ after=(-?\d+),(-?\d+)", log)
        else:
            matches = re.findall(r"LUA_WIDGET_DEMO (-?\d+) (-?\d+) (-?\d+) (-?\d+)", log)
        valid = len(matches) == widget_moves and all(
            (int(m[2]),int(m[3])) == (int(m[0])-12,int(m[1])) for m in matches)
        print(f"PIXEL native_widget_api_move={'PASS' if valid else 'FAIL'} language={widget_demo} observations={len(matches)}")
        if not valid: failures.append("native_widget_api_move")
        if widget_op:
            check_owned_operation(log, widget_demo, failures)
        if revision == "osrs239":
            # A moved hidden tab or unused side-modal can satisfy API readback
            # while leaving the active inventory untouched. Check the actual
            # mounted inventory and its painted rune against a native sibling.
            entries = [(int(com),int(slot),int(member),tuple(map(int,box))) for com,slot,member,*box in re.findall(
                r"NATIVE_UI[^\n]*? com=(-?\d+)[^\n]*? slot=(\d+) member=(\d+)[^\n]*? box=(-?\d+),(-?\d+),(\d+),(\d+)",log)]
            modal = [box for com,slot,member,box in entries if com >> 16 == root and slot == 3 and member == 0]
            inventory = [box for com,slot,member,box in entries if com == 149 << 16]
            placed = len(modal)==1 and len(inventory)==1 and inventory[0] == (modal[0][0]-widget_offset,modal[0][1],190,261)
            print(f"PIXEL native_widget_visible_inventory={'PASS' if placed else 'FAIL'}")
            if not placed: failures.append("native_widget_visible_inventory")
            ink = json.loads((Path(__file__).parent/"testdata/gameframe/native-body-rune-ink.json").read_text())
            painted = False
            if len(modal)==1:
                x,y=modal[0][0]-widget_offset+16+(widget_rune_slot%4)*42,modal[0][1]+8+(widget_rune_slot//4)*36
                painted = all(0<=y+dy<height and 0<=x+dx<width and
                    rows[y+dy][x+dx][0]>rows[y+dy][x+dx][2]+ink["blue_over_red"] and
                    rows[y+dy][x+dx][0]>rows[y+dy][x+dx][1]+ink["blue_over_green"]
                    for dx,dy in ink["pixels"])
            print(f"PIXEL native_widget_moved_item_ink={'PASS' if painted else 'FAIL'}")
            if not painted: failures.append("native_widget_moved_item_ink")
    if expect_log or screenshot_saved or forbid_log:
        log = Path(bounds_path).read_text() if bounds_path else ""
        for pattern in expect_log or []:
            found = re.search(pattern, log) is not None
            print(f"PIXEL expected_log={'PASS' if found else 'FAIL'} pattern={pattern!r}")
            if not found: failures.append(f"expected_log:{pattern}")
        for pattern in forbid_log or []:
            absent = re.search(pattern, log) is None
            print(f"PIXEL forbidden_log={'PASS' if absent else 'FAIL'} pattern={pattern!r}")
            if not absent: failures.append(f"forbidden_log:{pattern}")
        if screenshot_saved:
            check_screenshot_saved(log, failures)
    if expect_log_count:
        check_log_counts(Path(bounds_path).read_text() if bounds_path else "", expect_log_count, failures)
    if prefs_contains or prefs_absent:
        # The prefs file is named by the caller because it is the HOST's file,
        # not the capture's: gameframe_matrix.sh points it at the
        # plugin_prefs.ini the run was given (TORIRS_PLUGIN_PREFS). Asking for
        # a prefs rule without saying which file is the caller's bug, and a
        # scorer that quietly passed it would score nothing at all.
        assert prefs, "prefs rules need the plugin_prefs.ini path (--prefs)"
        check_prefs(prefs, prefs_contains or [], prefs_absent or [], failures)
    for spec in find_all_holes or []:
        # ROLE:COUNT:MISSING against the LAST PLUGIN_FIND_ALL line for that role:
        # the numbering the bridge handed the frame plugin, holes included.
        log = Path(bounds_path).read_text() if bounds_path else ""
        role, count, missing = spec.split(":")
        answers = re.findall(rf"PLUGIN_FIND_ALL owner=\S+ role={re.escape(role)} count=(\d+) missing=(\S*)", log)
        observed = answers[-1] if answers else None
        valid = observed == (count, missing)
        print(f"PIXEL find_all_holes={'PASS' if valid else 'FAIL'} role={role} expected={count}:{missing} observed={observed}")
        if not valid: failures.append(f"find_all_holes:{spec}")
    if report_replaced:
        check_report_replaced(Path(bounds_path).read_text() if bounds_path else "", failures)
    if highlight_color:
        check_highlight_color(rows, Path(bounds_path).read_text() if bounds_path else "", highlight_color, failures)
    if panel_custom_ink:
        check_panel_custom_ink(rows, Path(bounds_path).read_text() if bounds_path else "", panel_custom_ink, failures)
    if dest_tile:
        log = Path(bounds_path).read_text() if bounds_path else ""
        yellow=sum(pixel==(0,255,255) for row in rows for pixel in row)
        tiles=re.findall(r"NATIVE_PLAYER true=(-?\d+),(-?\d+),(\d+) dest=(-?\d+),(-?\d+) flag=(-?\d+),(-?\d+)",log)
        walking=bool(tiles) and (tiles[-1][0],tiles[-1][1])!=(tiles[-1][3],tiles[-1][4])
        valid=yellow>=30 and walking
        print(f"PIXEL dest_tile_marker={'PASS' if valid else 'FAIL'} yellow_pixels={yellow} native_player={tiles[-1] if tiles else None}")
        if not valid: failures.append("dest_tile_marker")
    if menu_row:
        log = Path(bounds_path).read_text() if bounds_path else ""
        row_lines=[line for line in log.splitlines() if line.startswith("minimenu: row[")]
        for pattern in menu_row:
            hit=[line for line in row_lines if re.search(pattern,line)]
            valid=len(hit)>=1
            print(f"PIXEL menu_row={'PASS' if valid else 'FAIL'} pattern={pattern!r} rows={len(row_lines)} matched={hit[0] if hit else None}")
            if not valid: failures.append(f"menu_row:{pattern}")
    for text in overlay_text_absent or []:
        log = Path(bounds_path).read_text() if bounds_path else ""
        raw=text.encode();fingerprint=14695981039346656037
        for byte in raw: fingerprint=((fingerprint^byte)*1099511628211)&((1<<64)-1)
        entries=re.findall(rf"OVERLAY_TEXT x=-?\d+ y=-?\d+ color=[0-9a-f]+ len={len(raw)} hash={fingerprint:016x}",log)
        valid=not entries
        print(f"PIXEL overlay_text_absent={'PASS' if valid else 'FAIL'} text={text!r} copies={len(entries)}")
        if not valid: failures.append(f"overlay_text_absent:{text}")
    for text in native_caption_absent or []:
        log = Path(bounds_path).read_text() if bounds_path else ""
        raw=text.encode();fingerprint=14695981039346656037
        for byte in raw: fingerprint=((fingerprint^byte)*1099511628211)&((1<<64)-1)
        entries=re.findall(rf"NATIVE_GROUND_CAPTION root=\d+ node=\d+ painted=[1-9]\d* box=[-0-9,]+ color=[0-9a-f]+ len={len(raw)} hash={fingerprint:016x}",log)
        valid=not entries
        print(f"PIXEL native_caption_absent={'PASS' if valid else 'FAIL'} text={text!r} painted={len(entries)}")
        if not valid: failures.append(f"native_caption_absent:{text}")
    if scene_objects is not None:
        log = Path(bounds_path).read_text() if bounds_path else ""
        counts=re.findall(r"PLUGIN_SCENE_OBJECTS in_use=(\d+) active=(\d+) built=(\d+)",log)
        valid=bool(counts) and int(counts[-1][1])==scene_objects and int(counts[-1][2])>=min(scene_objects,int(counts[-1][1]))
        print(f"PIXEL scene_objects={'PASS' if valid else 'FAIL'} expected_active={scene_objects} counts={counts[-1] if counts else None}")
        if not valid: failures.append("scene_objects")
    if owned_text is not None or owned_count==0:
        log = Path(bounds_path).read_text() if bounds_path else ""
        records = re.findall(r"OWNED_WIDGET owner=\d+ key=strength node=\d+ box=(-?\d+),(-?\d+),(\d+),(\d+) len=(\d+) hash=([0-9a-f]+)",log)
        h=14695981039346656037
        for byte in (owned_text or "").encode(): h=((h^byte)*1099511628211)&((1<<64)-1)
        current=len(records)==owned_count and (owned_count==0 or records[0][4:]==(str(len(owned_text.encode())),f"{h:016x}"))
        print(f"PIXEL owned_widget_live_text={'PASS' if current else 'FAIL'} count={len(records)}")
        if not current: failures.append("owned_widget_live_text")
        painted=owned_count==0 and not records
        if owned_count==1 and len(records)==1:
            x,y,w,h=map(int,records[0][:4])
            emitted=bool(re.search(rf"EMIT_EXIT[^\n]*kind=2 com=0xffffffff[^\n]* x={x} y={y} w={w} h={h} ",log))
            ink=sum(1 for yy in range(max(0,y),min(height,y+h)) for xx in range(max(0,x),min(width,x+w))
                    if min(rows[yy][xx])>=240)
            painted=emitted and ink>=30
        print(f"PIXEL owned_widget_text_painted={'PASS' if painted else 'FAIL'}")
        if not painted: failures.append("owned_widget_text_painted")
    if revision == "rs289lc":
        if not bounds_path:
            raise ValueError("rs289lc requires its matching native trace")
        check_rs289(rows, Path(bounds_path).read_text(), failures, rs289_scenario, frame, public_chat_mode, report_replaced)
        return failures
    if frame == "gameframe-layout/classic-fixed":
        log = Path(bounds_path).read_text() if bounds_path else ""
        check_classic_chat_pack(rows, log, failures)
    if native_focus_hide:
        log = Path(bounds_path).read_text() if bounds_path else ""
        markers = ("sim_type: c97 at frame 700", "sim_type: c98 at frame 701",
                   "if_sethide: com=58589197 hide=1 applied=1", "sim_type: c120 at frame 800",
                   "if_sethide: com=58589197 hide=0 applied=1", "sim_type: c99 at frame 900")
        cursor = 0
        valid = True
        for marker in markers:
            at = log.find(marker, cursor)
            if at < 0:
                valid = False
                break
            cursor = at + len(marker)
        print(f"PIXEL native_focus_hide_sequence={'PASS' if valid else 'FAIL'}")
        if not valid:
            failures.append("native_focus_hide_sequence")
        if input_state is None:
            input_state = "58589197:abc"
    if input_state:
        parent, expected = input_state.split(":", 1)
        raw = expected.encode("utf-8")
        fingerprint = 14695981039346656037
        for byte in raw:
            fingerprint = ((fingerprint ^ byte) * 1099511628211) & ((1 << 64) - 1)
        log = Path(bounds_path).read_text() if bounds_path else ""
        matches = re.findall(rf"NATIVE_INPUT parent={int(parent)} com=\d+ focused=(\d) len=(\d+) hash=([0-9a-f]+)", log)
        valid = len(matches) == 1 and matches[0] == ("1", str(len(raw)), f"{fingerprint:016x}")
        print(f"PIXEL native_focused_input_state={'PASS' if valid else 'FAIL'} parent={parent}")
        if not valid:
            failures.append("native_focused_input_state")
    if bounds_path:
        check_live_surfaces(rows, Path(bounds_path).read_text(), frame, root, minimap_state, server_hide, failures, native_baseline, public_chat_mode, expected_orbs)
    return failures


def selftest():
    """Prove the log-count and prefs rules can BOTH pass and fail.

    A scoring rule that cannot go red is a rule that pins nothing, so every
    case below is asserted in both directions. Needs no client and no capture:
    run it after editing this file.
    """
    import tempfile

    expected = expected_orb_keys("hitpoints,run")
    for actual, should_pass in [({"orb_hitpoints", "orb_run"}, True),
                                ({"orb_hitpoints"}, False),
                                ({"orb_hitpoints", "orb_run", "orb_special"}, False)]:
        failures = []
        assert check_expected_orb_set(actual, expected, failures) == should_pass
        assert bool(failures) != should_pass
    assert expected_orb_keys() == {"orb_" + name for name in ORB_NAMES}
    assert expected_orb_keys("", False) == set()
    for invalid in ("hitpoints,hitpoints", "hitpoints,unknown"):
        try:
            expected_orb_keys(invalid)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid expected orb set accepted")

    log = ("sim_plugin_toggle: frame=500 id=gameframe-layout enabled=0 found=1 "
           "was=1/1 now=1/1 applied=0\n"
           "PLUGIN_STATE id=minimap-orbs enabled=1 running=1 error=0\n"
           "mobile_chat: set_image slot=0\n"
           "mobile_chat: set_image slot=1\n")

    failures = []
    check_log_counts(log, [r"mobile_chat: set_image:2", r"applied=0:1"], failures)
    assert failures == [], failures
    check_log_counts(log, [r"mobile_chat: set_image:1"], failures)
    assert failures == [r"expected_log_count:mobile_chat: set_image:1"], failures
    # A pattern that never matches is a failure, not a silent zero.
    failures = []
    check_log_counts(log, [r"never_emitted_line:1"], failures)
    assert len(failures) == 1, failures
    # ... and "exactly none" is a rule in its own right.
    failures = []
    check_log_counts(log, [r"never_emitted_line:0"], failures)
    assert failures == [], failures
    # A malformed spec must not read as a pass.
    failures = []
    check_log_counts(log, [r"missing the count"], failures)
    assert len(failures) == 1, failures

    with tempfile.TemporaryDirectory() as directory:
        prefs = Path(directory)/"plugin_prefs.ini"
        prefs.write_text("[plugin:minimap-orbs]\nenabled=1\nshow_spec=0\n")
        failures = []
        check_prefs(prefs, [r"^\[plugin:minimap-orbs\]$", r"^show_spec=0$"], [r"^show_spec=1$"], failures)
        assert failures == [], failures
        check_prefs(prefs, [r"^show_spec=1$"], [r"^enabled=1$"], failures)
        assert len(failures) == 2, failures
        # A prefs file the host never wrote fails both kinds of rule: "no file"
        # is not evidence that a line is absent from it.
        failures = []
        check_prefs(Path(directory)/"never_written.ini", [r"^enabled=1$"], [r"^show_spec=1$"], failures)
        assert len(failures) == 2, failures

    print("SELFTEST ok: expected_log_count and prefs rules each pass and fail")
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    # nargs="?" and the two required flags relaxed only so --selftest can run
    # with no capture at all; a scoring run without them still stops below.
    parser.add_argument("capture", nargs="?")
    parser.add_argument("--frame")
    parser.add_argument("--root", type=int)
    parser.add_argument("--bounds", help="matching TORIRS_DUMP_BOUNDS log")
    parser.add_argument("--revision", choices=("osrs239", "rs289lc"), default="osrs239")
    parser.add_argument("--native-baseline", action="store_true", help="plugins disabled; no plugin orb assertion")
    parser.add_argument("--rs289-scenario", choices=("baseline", "stats", "skill-guide"), default="baseline")
    parser.add_argument("--minimap-state", type=int, choices=range(6))
    parser.add_argument("--server-hide", help="expected native component uid:hide receipt")
    parser.add_argument("--input-state", help="focused native field parent uid:expected text")
    parser.add_argument("--native-focus-hide", action="store_true", help="verify the native Hiscores typing/hide packet sequence")
    parser.add_argument("--widget-demo", choices=("c", "lua"))
    parser.add_argument("--widget-op", action="store_true", help="the simulated click must press the demo's owned control")
    parser.add_argument("--expect-log", action="append", default=[], help="regex the client log must contain (repeatable)")
    parser.add_argument("--screenshot-saved", action="store_true", help="a plugin 'captured <path>' line names an existing PNG")
    parser.add_argument("--report-replaced", action="store_true", help="the native report control is plugin-hidden and the camera sits in its slot")
    parser.add_argument("--forbid-log", action="append", default=[], help="regex the client log must NOT contain (repeatable)")
    parser.add_argument("--highlight-color", help="RRGGBB[:min] a live cache highlight group of this colour has members and its exact colour is painted")
    parser.add_argument("--panel-custom-ink", help="ID[:min] a plugin page custom row has an allotted region with at least min distinct colours painted")
    parser.add_argument("--widget-moves", type=int, default=1)
    parser.add_argument("--widget-rune-slot", type=int, choices=range(28), default=0)
    parser.add_argument("--owned-text")
    parser.add_argument("--owned-count",type=int,choices=(0,1),default=1)
    parser.add_argument("--widget-offset",type=int,default=12)
    parser.add_argument("--expected-orbs", help="exact visible orb names, comma-separated; default hitpoints,prayer,run,special; oracle only")
    parser.add_argument("--plugin-id")
    parser.add_argument("--plugin-enabled",type=int,choices=(0,1),default=1)
    parser.add_argument("--plugin-lua",action="store_true")
    parser.add_argument("--performance-metrics",default="fps,frame,effective,memory")
    parser.add_argument("--performance-position",default="10,25")
    parser.add_argument("--performance-color",default="FFFFFF")
    parser.add_argument("--overlay-text",action="append",help="require a single non-shadow overlay label and matching ink")
    parser.add_argument("--native-ground-labels",choices=("hidden","shown"))
    parser.add_argument("--native-caption")
    parser.add_argument("--ground-row-gap",type=int)
    parser.add_argument("--public-chat-mode",choices=("on","friends"),default="on")
    parser.add_argument("--dest-tile", action="store_true", help="the tile indicator's yellow destination marker is painted while NATIVE_PLAYER says the walk has not ended")
    parser.add_argument("--menu-row", action="append", default=[], help="regex one 'minimenu: row[..]' line of the opened right-click menu must match (repeatable)")
    parser.add_argument("--overlay-text-absent", action="append", default=[], help="no overlay label with this exact text was drawn (repeatable)")
    parser.add_argument("--native-caption-absent", action="append", default=[], help="no painted native ground caption carries this exact text (repeatable)")
    parser.add_argument("--scene-objects", type=int, help="PLUGIN_SCENE_OBJECTS active count the engine must hold at exit")
    parser.add_argument("--find-all-holes", action="append", default=[], help="ROLE:COUNT:MISSING the last PLUGIN_FIND_ALL line for ROLE must report (repeatable)")
    parser.add_argument("--prefs", help="the plugin_prefs.ini the run was given (TORIRS_PLUGIN_PREFS), for the two rules below")
    parser.add_argument("--prefs-contains", action="append", default=[], help="regex that file must match after the run -- how a persistence claim is pinned (repeatable)")
    parser.add_argument("--prefs-absent", action="append", default=[], help="regex that file must NOT match after the run (repeatable)")
    parser.add_argument("--expect-log-count", action="append", default=[], help="<regex>:<n> the client log must carry EXACTLY n matches of (repeatable)")
    parser.add_argument("--selftest", action="store_true", help="prove the log-count and prefs rules can pass AND fail; no capture needed")
    args = parser.parse_args()
    if args.selftest:
        raise SystemExit(selftest())
    if not args.capture or args.frame is None or args.root is None:
        parser.error("a capture, --frame and --root are required (or --selftest)")
    try:
        raise SystemExit(bool(check(args.capture, args.frame, args.root, args.bounds, args.minimap_state,
                                    args.server_hide, args.revision, args.native_baseline, args.rs289_scenario, args.input_state, args.native_focus_hide, args.widget_demo, args.widget_moves, args.widget_rune_slot, args.owned_text, args.owned_count, args.widget_offset, args.plugin_id, args.plugin_enabled, args.plugin_lua, args.performance_metrics, args.performance_position, args.performance_color, args.overlay_text, args.native_ground_labels, args.native_caption, args.ground_row_gap, args.public_chat_mode, args.widget_op, args.expect_log, args.screenshot_saved, args.report_replaced, args.forbid_log, args.highlight_color, args.panel_custom_ink, args.dest_tile, args.menu_row, args.overlay_text_absent, args.native_caption_absent, args.scene_objects, args.find_all_holes,
                                    prefs=args.prefs, prefs_contains=args.prefs_contains,
                                    prefs_absent=args.prefs_absent, expect_log_count=args.expect_log_count, expected_orbs=args.expected_orbs)))
    except (OSError, ValueError, struct.error) as error:
        print(f"PIXEL capture=FAIL: {error}")
        raise SystemExit(1)
