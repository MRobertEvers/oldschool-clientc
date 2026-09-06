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


def check_live_surfaces(rows, log, frame, root, minimap_state, server_hide, failures, native_baseline=False, public_chat_mode="on"):
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
    if not native_baseline:
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
    report("filter_modes_visible", modes == (5 if public_chat_mode=="friends" else 6), f"green_cells={modes}")
    if frame in ("gameframe-layout/classic-fixed", "mobile-gameframe/stone-drawer"):
        fractions = []
        for name in ("frame.chat.backing", "frame.chat.bar"):
            ps = pixels(parts.get(name,(0,0,0,0)))
            fractions.append(sum(r>140 and g>100 and r>g>b for b,g,r in ps)/len(ps) if ps else -1)
        report("chat_backing_parchment_bar_rock", fractions[0] > .65 and 0 <= fractions[1] < .1,
               f"warm_backing={fractions[0]:.3f} warm_bar={fractions[1]:.3f}")


def check_rs289(rows, log, failures, scenario="baseline", frame="core/native", public_chat_mode="on"):
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
    report("rs289_four_chat_controls", len(boxes) == 4, f"controls={len(boxes)}")
    report("rs289_controls_inside_canvas", len(boxes) == 4 and all(
        x >= 0 and y >= 0 and w > 0 and h > 0 and x+w <= width and y+h <= height
        for x,y,w,h in boxes))
    modes = captions = 0
    for x,y,w,h in boxes:
        ps = [rows[yy][xx] for yy in range(max(0,y),min(height,y+h))
              for xx in range(max(0,x),min(width,x+w))]
        modes += sum(g > 150 and r < 100 and b < 100 for b,g,r in ps) >= 10
        captions += sum(min(b,g,r) > 180 and max(b,g,r)-min(b,g,r) < 30 for b,g,r in ps) >= 20
    report("rs289_live_chat_modes", modes == (2 if public_chat_mode=="friends" else 3), f"green_cells={modes}")
    report("rs289_chat_captions", captions == 4, f"captions={captions}")
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
          revision="osrs239", native_baseline=False, rs289_scenario="baseline", input_state=None, native_focus_hide=False, widget_demo=None, widget_moves=1, widget_rune_slot=0, owned_text=None, owned_count=1, widget_offset=12, plugin_id=None, plugin_enabled=1, plugin_lua=False, performance_metrics="fps,frame,effective,memory", performance_position="10,25", performance_color="FFFFFF", overlay_text=None, native_ground_labels=None, native_caption=None, ground_row_gap=None, public_chat_mode="on"):
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
        check_rs289(rows, Path(bounds_path).read_text(), failures, rs289_scenario, frame, public_chat_mode)
        return failures
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
        check_live_surfaces(rows, Path(bounds_path).read_text(), frame, root, minimap_state, server_hide, failures, native_baseline, public_chat_mode)
    return failures


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture")
    parser.add_argument("--frame", required=True)
    parser.add_argument("--root", required=True, type=int)
    parser.add_argument("--bounds", help="matching TORIRS_DUMP_BOUNDS log")
    parser.add_argument("--revision", choices=("osrs239", "rs289lc"), default="osrs239")
    parser.add_argument("--native-baseline", action="store_true", help="plugins disabled; no plugin orb assertion")
    parser.add_argument("--rs289-scenario", choices=("baseline", "stats", "skill-guide"), default="baseline")
    parser.add_argument("--minimap-state", type=int, choices=range(6))
    parser.add_argument("--server-hide", help="expected native component uid:hide receipt")
    parser.add_argument("--input-state", help="focused native field parent uid:expected text")
    parser.add_argument("--native-focus-hide", action="store_true", help="verify the native Hiscores typing/hide packet sequence")
    parser.add_argument("--widget-demo", choices=("c", "lua"))
    parser.add_argument("--widget-moves", type=int, default=1)
    parser.add_argument("--widget-rune-slot", type=int, choices=range(28), default=0)
    parser.add_argument("--owned-text")
    parser.add_argument("--owned-count",type=int,choices=(0,1),default=1)
    parser.add_argument("--widget-offset",type=int,default=12)
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
    args = parser.parse_args()
    try:
        raise SystemExit(bool(check(args.capture, args.frame, args.root, args.bounds, args.minimap_state,
                                    args.server_hide, args.revision, args.native_baseline, args.rs289_scenario, args.input_state, args.native_focus_hide, args.widget_demo, args.widget_moves, args.widget_rune_slot, args.owned_text, args.owned_count, args.widget_offset, args.plugin_id, args.plugin_enabled, args.plugin_lua, args.performance_metrics, args.performance_position, args.performance_color, args.overlay_text, args.native_ground_labels, args.native_caption, args.ground_row_gap, args.public_chat_mode)))
    except (OSError, ValueError, struct.error) as error:
        print(f"PIXEL capture=FAIL: {error}")
        raise SystemExit(1)
