#!/usr/bin/env python3
"""Diff the interface/var/camera/sound packets of a recording window against ours.

The tick log (ticklog_tells.py) carries entity tells: anims, spotanims, npc
info, sounds. It has no interface, clientscript, message, var or camera rows.
This tool covers those, from both sides:

  ours  TORIRS_SCRIPTRUN_WIRELOG=<file> on a scriptrun (src/scriptrun/
        scriptrun_core.c core_wirelog): `<tick>\\t<kind>\\t<fields>`, ids only.
  R     an RSProx transcript (rsprox_index.py get <id>).

Both are reduced to the same numeric lines (components as group:child, scripts
by id, varp writes expanded to the varbits whose value changed, coordinates
absolute) and printed per tick relative to an anchor tick on each side:

  python3 tools/rsprox/wirelog_tells.py --rec idx4293.txt --rfrom 170 --rto 330 \\
      --ours wirelog.tsv --ofrom 105 --anchor-r 171 --anchor-o 106 [--xlate DX,DZ]

--xlate is subtracted from our coordinates (an instance played where the
transcript prints template tiles). Lines only one side has are marked R-only /
O-only; equal lines on the same relative tick are `=`.
"""
import argparse
import collections
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONTENT = ROOT / "OSRS-Content" / "osrs-content"

# Kinds the noise filter drops on both sides: the clock varps and the like.
NOISE_VARBITS = {8354, 12391, 12392}  # clock, date_milliseconds_past_minute, the seconds clock
NOISE_VARPS = {3078, 3079, 6405, 102, 456, 3883}  # date_minutes, map_clock, playtime, poison, disease, music_current_track
NOISE_SCRIPTS = {4517, 4518, 4721, 3954, 3932, 828, 5224}  # stat/spec/regen timers, music text, playermember, pvp level

TYPE_NAMES = {"overlay": 1, "modal": 0}


def load_varbits():
    """varp id -> [(varbit id, lo, hi)] from configs/all.varbit."""
    by_varp = collections.defaultdict(list)
    cur = None
    base = lo = hi = None
    for line in (CONTENT / "configs" / "all.varbit").read_text(errors="replace").splitlines():
        m = re.match(r"\[varb(\d+)_", line)
        if m:
            if cur is not None and base is not None:
                by_varp[base].append((cur, lo, hi))
            cur, base, lo, hi = int(m.group(1)), None, None, None
            continue
        if line.startswith("basevar=varp"):
            base = int(re.match(r"basevar=varp(\d+)", line).group(1))
        elif line.startswith("startbit="):
            lo = int(line.split("=")[1])
        elif line.startswith("endbit="):
            hi = int(line.split("=")[1])
    if cur is not None and base is not None:
        by_varp[base].append((cur, lo, hi))
    return by_varp


def comp_of(text):
    m = re.search(r"\((\d+):(\d+)\)", text)
    return f"{m.group(1)}:{m.group(2)}" if m else "?"


def parse_rec(path, t0, t1):
    out = collections.defaultdict(list)
    tick = None
    block = None
    for line in Path(path).read_text(errors="replace").splitlines():
        m = re.match(r"^\[(\d+)\]$", line)
        if m:
            tick = int(m.group(1))
            continue
        if tick is None or tick < t0 or tick > t1:
            continue
        s = line.strip()
        # The recorder's own player-info lines: facing and exactmove (the
        # transcript shows only the recorder unless --all-players).
        if line.startswith("    [") and not line.startswith("     "):
            block = s.split("]")[0][1:]
        elif block == "player_info":
            fm = re.match(r"\[facing\] (.*)$", s)
            if fm:
                body = fm.group(1)
                cm = re.match(r"coord=\((\d+), (\d+), \d+\)", body)
                am = re.match(r"angle=(\d+)", body)
                if cm:
                    out[tick].append(f"face_coord {cm.group(1)},{cm.group(2)}")
                elif am:
                    out[tick].append(f"face_angle {am.group(1)}")
                elif body.startswith("type=reset"):
                    out[tick].append("face_reset")
                continue
            em = re.match(r"\[exactmove\] to1=\((\d+), (\d+), \d+\), delay1=(\d+), "
                          r"to2=\((\d+), (\d+), \d+\), delay2=(\d+), angle=(\d+)", s)
            if em:
                g = em.groups()
                out[tick].append(f"exactmove {g[0]},{g[1]} {g[3]},{g[4]} {g[2]} {g[5]} {g[6]}")
                continue
        m = re.match(r"\[([a-z_0-9]+)\]\s*(.*)$", s)
        if not m:
            continue
        kind, rest = m.group(1), m.group(2)
        ev = rec_event(kind, rest)
        if ev:
            out[tick].append(ev)
    return out


def rec_event(kind, rest):
    def field(name):
        m = re.search(name + r"=([^,]*(?:\([^)]*\))?)", rest)
        return m.group(1).strip() if m else ""

    if kind == "if_opensub":
        iface = re.search(r"id=[^(]*\((\d+)\)", rest).group(1)
        return f"if_opensub {comp_of(rest.split(', id=')[0])} {iface} {TYPE_NAMES.get(field('type'), field('type'))}"
    if kind == "if_closesub":
        return f"if_closesub {comp_of(rest)}"
    if kind == "if_settext":
        text = re.search(r'text="(.*)"$', rest).group(1)
        return f"if_settext {comp_of(rest)} {text}"
    if kind.startswith("if_setevents"):
        return f"if_setevents {comp_of(rest)} {field('start')} {field('end')} {re.search(r'events=(.*)$', rest).group(1)}"
    if kind == "if_sethide":
        return f"if_sethide {comp_of(rest)} {1 if field('hide') == 'true' else 0}"
    if kind == "if_setanim":
        return f"if_setanim {comp_of(rest)} {re.search(r'id=[^(]*\((\d+)\)$', rest).group(1)}"
    if kind == "if_setnpchead":
        return f"if_setnpchead {comp_of(rest)} {re.search(r'id=[^(]*\((\d+)\)$', rest).group(1)}"
    if kind == "if_setplayerhead":
        return f"if_setplayerhead {comp_of(rest)}"
    if kind == "runclientscript":
        sid = re.search(r"id=[^(]*\((\d+)\)", rest).group(1)
        if int(sid) in NOISE_SCRIPTS:
            return None
        vals = re.search(r"values=\[(.*)\]$", rest)
        args = []
        if vals:
            for v in split_values(vals.group(1)):
                cm = re.search(r"\((\d+):(\d+)\)$", v)
                if cm:
                    args.append(str((int(cm.group(1)) << 16) | int(cm.group(2))))
                elif v in ("true", "false"):
                    args.append("1" if v == "true" else "0")
                else:
                    args.append(v)
        return f"runclientscript {sid} {','.join(args)}"
    if kind == "message_game":
        return f"message_game {field('type')} {re.search(r'message=\"(.*)\"$', rest).group(1)}"
    if kind == "varbit":
        vid = int(re.search(r"\((\d+)\)", rest).group(1))
        if vid in NOISE_VARBITS:
            return None
        return f"varbit {vid} {field('new')}"
    if kind in ("varp_small", "varp_large"):
        vid = int(re.search(r"\((\d+)\)", rest).group(1))
        if vid in NOISE_VARPS:
            return None
        return f"varp {vid} {field('new')}"
    if kind == "minimap_toggle":
        return f"minimap_toggle {field('state')}"
    if kind in ("hidenpcops", "hidelocops", "hideobjops"):
        return f"{kind} {1 if field('hide') == 'true' else 0}"
    if kind == "synth_sound":
        loops = field("loops") or "1"
        return f"synth_sound {field('id')} {loops} {field('delay') or '0'}"
    if kind.startswith("midi_song_withsecondary"):
        return f"midi_song_withsecondary {field('primaryid')} {field('secondaryid')}"
    if kind.startswith("midi_song"):
        return f"midi_song {field('id')}"
    if kind == "cam_reset":
        return "cam_reset"
    if kind == "cam_shake":
        return f"cam_shake {field('axis')} {field('random')} {field('amplitude')} {field('rate')}"
    if kind.startswith("cam_moveto_cycles"):
        x, z = coord(rest)
        ign = 1 if field("ignoreterrain") == "true" else 0
        return f"cam_moveto_cycles {x},{z} {field('height')} {field('cycles')} {ign} {field('easing')}"
    if kind.startswith("cam_rotateto_coordinate"):
        x, z = coord(rest)
        return f"cam_rotateto_coordinate {x},{z} {field('height')} {field('cycles')} {field('easing')}"
    if kind.startswith("cam_moveto") or kind.startswith("cam_lookat"):
        x, z = coord(rest)
        name = "cam_moveto" if kind.startswith("cam_moveto") else "cam_lookat"
        return f"{name} {x},{z} {field('height')} {field('rate')} {field('rate2')}"
    if kind == "cam_unlock":
        return f"cam_unlock {field('unlock')}"
    return None


def split_values(s):
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return [v.strip('"') for v in out]


def coord(rest):
    m = re.search(r"coord=\((\d+), (\d+), \d+\)", rest)
    return int(m.group(1)), int(m.group(2))


EASINGS = {0: "linear", 1: "ease_in_sine", 2: "ease_out_sine"}


def parse_ours(path, t0, t1, xlate, by_varp):
    out = collections.defaultdict(list)
    varps = {}
    dx, dz = xlate
    for line in Path(path).read_text(errors="replace").splitlines():
        f = line.split("\t")
        if len(f) < 2:
            continue
        tick, kind, a = int(f[0]), f[1], f[2:]
        ev = None
        if kind == "varp":
            vid, new = int(a[0]), int(a[1])
            old = varps.get(vid, 0)
            varps[vid] = new
            if not (t0 <= tick <= t1):
                continue
            if vid in by_varp:
                for vb, lo, hi in by_varp[vid]:
                    mask = (1 << (hi - lo + 1)) - 1
                    o, n = (old >> lo) & mask, (new >> lo) & mask
                    if o != n and vb not in NOISE_VARBITS:
                        out[tick].append(f"varbit {vb} {n}")
            elif vid not in NOISE_VARPS:
                out[tick].append(f"varp {vid} {new}")
            continue
        if not (t0 <= tick <= t1):
            continue
        if kind == "if_opensub":
            ev = f"if_opensub {a[0]} {a[1]} {a[2]}"
        elif kind == "if_closesub":
            ev = f"if_closesub {a[0]}"
        elif kind == "if_settext":
            ev = f"if_settext {a[0]} {a[1] if len(a) > 1 else ''}"
        elif kind == "if_setevents":
            ev = f"if_setevents {a[0]} {a[1]} {a[2]} 0x{int(a[3]) & 0xffffffff:x}"
        elif kind in ("if_sethide", "if_setanim", "if_setnpchead"):
            ev = f"{kind} {a[0]} {a[1]}"
        elif kind == "if_setplayerhead":
            ev = f"if_setplayerhead {a[0]}"
        elif kind == "runclientscript":
            if int(a[0]) in NOISE_SCRIPTS:
                continue
            ev = f"runclientscript {a[0]} {(a[1] if len(a) > 1 else '').replace(chr(34), '')}"
        elif kind == "message_game":
            ev = f"message_game {a[0]} {a[1] if len(a) > 1 else ''}"
        elif kind in ("minimap_toggle", "hidenpcops", "hidelocops", "hideobjops", "midi_song"):
            ev = f"{kind} {a[0]}"
        elif kind == "midi_song_withsecondary":
            ev = f"midi_song_withsecondary {a[0]} {a[1]}"
        elif kind == "synth_sound":
            ev = f"synth_sound {a[0]} {a[1]} {a[2]}"
        elif kind == "cam_reset":
            ev = "cam_reset"
        elif kind == "cam_shake":
            ev = f"cam_shake {a[0]} {a[1]} {a[2]} {a[3]}"
        elif kind == "cam_moveto_cycles":
            ev = (f"cam_moveto_cycles {int(a[0]) - dx},{int(a[1]) - dz} {a[2]} {a[3]} {a[4]} "
                  f"{EASINGS.get(int(a[5]), a[5])}")
        elif kind == "cam_rotateto_coordinate":
            ev = f"cam_rotateto_coordinate {int(a[0]) - dx},{int(a[1]) - dz} {a[2]} {a[3]} {EASINGS.get(int(a[4]), a[4])}"
        elif kind in ("cam_moveto", "cam_lookat"):
            ev = f"{kind} {int(a[0]) - dx},{int(a[1]) - dz} {a[2]} {a[3]} {a[4]}"
        elif kind == "face_coord":
            ev = f"face_coord {int(a[0]) - dx},{int(a[1]) - dz}"
        elif kind == "face_angle":
            ev = f"face_angle {a[0]}"
        elif kind == "face_entity":
            ev = f"face_entity {a[0]}"
        elif kind == "exactmove":
            (sx, sz), (ex, ez) = (map(int, a[0].split(",")), map(int, a[1].split(",")))
            ev = f"exactmove {sx - dx},{sz - dz} {ex - dx},{ez - dz} {a[2]} {a[3]} {a[4]}"
        if ev:
            out[tick].append(ev)
    return out


def norm(ev):
    """The comparable part: if_setevents' event set prints as names on one side
    and bits on the other, so it compares on the component and range only."""
    if ev.startswith("if_setevents"):
        return " ".join(ev.split()[:4])
    if ev.startswith("message_game"):
        p = ev.split(" ", 2)
        return "message_game " + (p[2] if len(p) > 2 else "")
    return ev


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rec", required=True)
    ap.add_argument("--rfrom", type=int, required=True)
    ap.add_argument("--rto", type=int, required=True)
    ap.add_argument("--ours", required=True)
    ap.add_argument("--ofrom", type=int, required=True)
    ap.add_argument("--anchor-r", type=int, required=True)
    ap.add_argument("--anchor-o", type=int, required=True)
    ap.add_argument("--xlate", default="0,0")
    ap.add_argument("--only-diff", action="store_true", help="print only ticks with a mismatch")
    ap.add_argument("--subst", action="append", default=[],
                    help="OURS=RECORDED text substitution on our lines (the player's name), repeatable")
    ap.add_argument("--ignore", action="append", default=[],
                    help="regex; matching lines are dropped from both sides, repeatable")
    args = ap.parse_args()
    xlate = tuple(int(v) for v in args.xlate.split(","))
    by_varp = load_varbits()
    rec = parse_rec(args.rec, args.rfrom, args.rto)
    oto = args.ofrom + (args.rto - args.rfrom) + (args.anchor_o - args.ofrom) - (args.anchor_r - args.rfrom)
    ours = parse_ours(args.ours, args.ofrom, oto, xlate, by_varp)
    subst = [tuple(x.split("=", 1)) for x in args.subst]
    ignore = [re.compile(x) for x in args.ignore]
    for side, table in (("o", ours), ("r", rec)):
        for t in list(table):
            kept = []
            for e in table[t]:
                if side == "o":
                    for a, b in subst:
                        e = e.replace(a, b)
                if not any(rx.search(e) for rx in ignore):
                    kept.append(e)
            table[t] = kept
    rel = sorted({t - args.anchor_r for t in rec} | {t - args.anchor_o for t in ours})
    mism = 0
    for r in rel:
        rt, ot = args.anchor_r + r, args.anchor_o + r
        rl = collections.Counter(norm(e) for e in rec.get(rt, []))
        ol = collections.Counter(norm(e) for e in ours.get(ot, []))
        both = rl & ol
        ronly = rl - ol
        oonly = ol - rl
        if (ronly or oonly) or not args.only_diff:
            print(f"--- A{r:+d}  R[{rt}]  O[{ot}]")
        if not args.only_diff:
            for e in both.elements():
                print(f"   =       {e}")
        for e in ronly.elements():
            print(f"   R-only  {e}")
        for e in oonly.elements():
            print(f"   O-only  {e}")
        mism += sum(ronly.values()) + sum(oonly.values())
    print(f"# {mism} mismatched line(s)", file=sys.stderr)


if __name__ == "__main__":
    main()
