#!/usr/bin/env python3
"""An npc's whole NPC_INFO, recording against ours: every block, per type and
per life (the rsprox-content skill's wire audit, "NPC INFO, EVERY BLOCK").

    python3 tools/rsprox/npc_stream.py <transcript> <ticklog.tsv> --match 'regex on npc name'
        [--from T0 --to T1] [--align add|seq:<id>|say:<text>] [--lives N] [--summary]

Blocks compared: the add's spawn angle; [sequence] (id and delay);
[spotanim] (id, slot, delay, loop); [facing] (coord as an offset from the npc's own
tile, entity, reset, angle); [say]; [transformation]; [enabled_ops];
[level_change]; [transparency]; [exact_move]; [headbar] ids; the movement
kind and step length per tick; ticks from the add to the [del].

Ours comes from the tick log: npc_spawn / npc_anim / npc_spotanim / npc_face
(x -1 entity or reset, -2 angle) / npc_say / npc_retype / npc_tile /
npc_headbar / npc_free and the FILE-ONLY npc_info rows (spawn angle, ops,
level, transparency, exact move, timed bar).

Each life is aligned on its add (or on the first event --align names, on both
sides), lives paired in order of their start. The summary counts every block
value per life on both sides; the per-life diff lists the rows one side has
and the other lacks at the same relative tick.
"""
import argparse
import re
from collections import Counter, defaultdict

TICK = re.compile(r"^\[(\d+)\]")
ENTRY = re.compile(r"^ {8}\[(add|del|idle|walk|run|teleport|telejump|crawl)\] npc=\(index=(\d+), id=([a-z0-9_]+) \((\d+)\)(?:, multinpc=[^)]*\))?, coord=\((\d+), (\d+), (\d+)\)\)(.*)$")
OTHER = re.compile(r"^ {8}\[(add|del|idle|walk|run|teleport|telejump|crawl)\] (npc|player)=")
DIRS = {"south": 0, "south-west": 256, "west": 512, "north-west": 768, "north": 1024,
        "north-east": 1280, "east": 1536, "south-east": 1792}
OPS = {"0b00000": "none", "0b11111": "all"}
KIT_SPOTS = {1908, 2731, 2732, 1542, 753, 754, 369}


spawn_at = {}


def rec_events(path, want, t0, t1):
    """{(index, life#): [(tick, kind, value)]}, {key: name}, {key: id}."""
    lives = {}
    names = {}
    ids = {}
    cur = None
    life_of = {}
    tick = 0
    tile = {}
    for line in open(path, errors="replace"):
        m = TICK.match(line)
        if m:
            tick = int(m.group(1))
            cur = None
            continue
        if tick < t0 or tick > t1:
            continue
        m = ENTRY.match(line)
        if m:
            kind, idx, name, nid = m.group(1), m.group(2), m.group(3), int(m.group(4))
            if not want.search(name):
                cur = None
                continue
            if kind == "add" or idx not in life_of:
                life_of[idx] = (idx, tick)
            key = life_of[idx]
            cur = key
            names[key] = name
            ids[key] = nid
            ev = lives.setdefault(key, [])
            x, z = int(m.group(5)), int(m.group(6))
            if kind == "add":
                sa = re.search(r"spawnangle=([a-z-]+)", m.group(8))
                ev.append((tick, "add", str(DIRS.get(sa.group(1), "?") if sa else "-")))
                tile[key] = (x, z)
                spawn_at[key] = (x, z)
            elif kind == "del":
                ev.append((tick, "del", ""))
            elif kind != "idle":
                nm = re.search(r"newcoord=\((\d+), (\d+)", m.group(8))
                if nm:
                    nx, nz = int(nm.group(1)), int(nm.group(2))
                    ev.append((tick, "move", f"{kind} {max(abs(nx - x), abs(nz - z))}"))
                    tile[key] = (nx, nz)
            continue
        if OTHER.match(line) or (line.startswith("    [") and not line.startswith("        ")):
            cur = None
            continue
        if not cur or not line.startswith("            "):
            continue
        s = line.strip()
        ev = lives[cur]
        if s.startswith("[sequence]"):
            sid = re.search(r"\((-?\d+)\)", s) or re.search(r"id=(-1)", s)
            dl = re.search(r"delay=(\d+)", s)
            ev.append((tick, "seq", f"{sid.group(1) if sid else '?'} d{dl.group(1) if dl else 0}"))
        elif s.startswith("[spotanim]"):
            sid = re.search(r"\((\d+)\)", s)
            # The raiders' kit on it (a thrall's magic, the glaive, the
            # Sanguinesti, bolt procs, ice barrage) and the empty clears that
            # ride a thrall's hit.
            if (sid and int(sid.group(1)) in KIT_SPOTS) or (not sid and "delay=" in s):
                continue
            slot = re.search(r"slot=(\d+)", s)
            dl = re.search(r"delay=(\d+)", s)
            loop = " loop" if "loop=true" in s else ""
            ev.append((tick, "spot", f"{sid.group(1) if sid else -1} s{slot.group(1) if slot else 0} d{dl.group(1) if dl else 0}{loop}"))
        elif s.startswith("[facing]"):
            f = s[len("[facing] "):]
            if f.startswith("coord="):
                c = re.search(r"\((\d+), (\d+)", f)
                bx, bz = tile.get(cur, (0, 0))
                v = f"coord {int(c.group(1)) - bx},{int(c.group(2)) - bz}"
            elif f.startswith("player="):
                v = "entity player"
            elif f.startswith("npc="):
                v = "entity npc"
            elif f.startswith("type=reset"):
                v = "reset"
            elif f.startswith("angle="):
                v = "angle " + re.search(r"angle=(\d+)", f).group(1)
            else:
                v = f[:30]
            ev.append((tick, "face", v))
        elif s.startswith("[say]"):
            ev.append((tick, "say", re.search(r'text="([^"]*)"', s).group(1)))
        elif s.startswith("[transformation]"):
            ev.append((tick, "form", re.search(r"\((\d+)\)", s).group(1)))
        elif s.startswith("[enabled_ops]"):
            b = re.search(r"opflags=(0b[01]+)", s).group(1)
            ev.append((tick, "ops", OPS.get(b, b)))
        elif s.startswith("[level_change]"):
            ev.append((tick, "level", re.search(r"level=([0-9,]+)", s).group(1).replace(",", "")))
        elif s.startswith("[transparency]"):
            g = re.search(r"start=(-?\d+), end=(-?\d+), starttransparency=(\d+), endtransparency=(\d+)", s)
            ev.append((tick, "transp", f"{g.group(1)}..{g.group(2)} {g.group(3)}->{g.group(4)}" if g else s))
        elif s.startswith("[exact_move]") or s.startswith("[exactmove]"):
            ev.append((tick, "exact", "exact_move"))
        elif s.startswith("[headbar]"):
            hb = re.search(r"id=(\d+)", s)
            ev.append((tick, "headbar", hb.group(1) if hb else "?"))
    return lives, names, ids


def ours_events(path, want_ids, t0, t1):
    lives = {}
    slot_life = {}
    tiles = {}
    for line in open(path, errors="replace"):
        f = line.rstrip("\n").split("\t")
        if len(f) < 9 or not f[1].isdigit():
            continue
        tick, kind, a = int(f[1]), f[2], f[3:]
        if tick < t0 or tick > t1:
            continue
        if kind == "npc_spawn":
            if int(a[1]) in want_ids:
                key = (a[0], tick)
                slot_life[a[0]] = key
                packed = int(a[2])
                tiles[key] = ((packed >> 14) & 0x3FFF, packed & 0x3FFF)
                spawn_at[key] = tiles[key]
                lives[key] = [(tick, "add", a[3]), (tick, "id", a[1])]
            else:
                slot_life.pop(a[0], None)
            continue
        if kind == "npc_retype" and int(a[2]) in want_ids and a[0] not in slot_life:
            key = (a[0], tick)
            slot_life[a[0]] = key
            lives[key] = [(tick, "id", a[1])]
        key = slot_life.get(a[0])
        if not key:
            continue
        ev = lives[key]
        label = f[9] if len(f) > 9 else ""
        if kind == "npc_anim":
            ev.append((tick, "seq", f"{a[2]} d{a[3]}"))
        elif kind == "npc_spotanim":
            # g (f[10]): rev 241's loop byte
            loop = " loop" if len(f) > 10 and f[10] == "1" else ""
            ev.append((tick, "spot", f"{a[2]} s{a[5]} d{a[4]}{loop}"))
        elif kind == "npc_face":
            if a[2] == "-1":
                # An entity id from TORIRSSERVER_FACE_PLAYER_BASE (32768) up
                # is a player; below it, an npc's slot.
                ev.append((tick, "face", "reset" if a[3] == "-1" else
                           "entity player" if int(a[3]) >= 32768 else "entity npc"))
            elif a[2] == "-2":
                ev.append((tick, "face", f"angle {a[3]}"))
            else:
                bx, bz = tiles.get(key, (0, 0))
                ev.append((tick, "face", f"coord {int(a[2]) - bx},{int(a[3]) - bz}"))
        elif kind == "npc_say":
            ev.append((tick, "say", label))
        elif kind == "npc_retype":
            ev.append((tick, "form", a[2]))
        elif kind == "npc_tile":
            x, z = int(a[1]), int(a[2])
            px, pz = tiles.get(key, (x, z))
            tiles[key] = (x, z)
            ev.append((tick, "move", f"? {max(abs(x - px), abs(z - pz))}"))
        elif kind == "npc_headbar":
            ev.append((tick, "headbar", a[2]))
        elif kind == "npc_free":
            ev.append((tick, "del", ""))
            slot_life.pop(a[0], None)
        elif kind == "npc_info":
            code = a[2]
            if code == "1":
                ev[0] = (ev[0][0], "add", a[3]) if ev and ev[0][1] == "add" else ev[0]
            elif code == "2":
                # Ours logs the hidden mask; the recording prints the enabled one.
                bits = "0b" + format(~int(a[3]) & 31, "05b")
                ev.append((tick, "ops", OPS.get(bits, bits)))
            elif code == "3":
                ev.append((tick, "level", a[3]))
            elif code == "4":
                v = int(a[3])
                ev.append((tick, "transp", f"{v >> 16}..{v & 0xFFFF} {a[4]}->{a[5]}"))
            elif code == "5":
                ev.append((tick, "exact", "exact_move"))
            elif code == "6":
                ev.append((tick, "headbar", a[3]))
    return lives


def norm(k, v):
    if k == "move":
        return "move " + v.split()[-1]
    return f"{k} {v}"


def collapse(evs):
    """An entity face re-sent while it is still the face (Jagex re-sends it as
    the npc turns: a new fallback angle) is one face: keep the changes."""
    out = []
    last = None
    for t, k, v in evs:
        if k == "face":
            if v.startswith("entity") and v == last:
                continue
            last = v
        out.append((t, k, v))
    return out


def summary(evs):
    c = Counter()
    for t, k, v in evs:
        if k in ("id",):
            continue
        c[norm(k, v)] += 1
    return c


def anchor_of(evs, align):
    if align == "add":
        return evs[0][0]
    kind, _, val = align.partition(":")
    for t, k, v in evs:
        if k == kind and v.startswith(val):
            return t
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("transcript")
    ap.add_argument("ticklog")
    ap.add_argument("--match", required=True)
    ap.add_argument("--from", dest="t0", type=int, default=0)
    ap.add_argument("--to", dest="t1", type=int, default=10 ** 9)
    ap.add_argument("--ours-from", type=int, default=0)
    ap.add_argument("--ours-to", type=int, default=10 ** 9)
    ap.add_argument("--align", default="add")
    ap.add_argument("--lives", type=int, default=3)
    ap.add_argument("--summary", action="store_true")
    ap.add_argument("--family", action="append", default=[],
                    help="'regex=name': lives whose type matches count as one family (forms of one npc)")
    a = ap.parse_args()
    rl, names, ids = rec_events(a.transcript, re.compile(a.match), a.t0, a.t1)
    # Every id the transcript names, so ours can be named too.
    all_ids = {}
    for line in open(a.transcript, errors="replace"):
        for nm, nid in re.findall(r"id=([a-z0-9_]+) \((\d+)\)", line):
            all_ids[int(nid)] = nm
    want_re = re.compile(a.match)
    want = {i for i, n in all_ids.items() if want_re.search(n)}
    fams = [(re.compile(f.split("=", 1)[0]), f.split("=", 1)[1]) for f in a.family]

    def family(name):
        for rx, fam in fams:
            if rx.search(name):
                return fam
        return name
    names = {k: family(v) for k, v in names.items()}
    ol = ours_events(a.ticklog, want, a.ours_from, a.ours_to)
    def order(keys):
        # By start tick, then by spawn tile (rooms are copied whole, unturned,
        # so the two sides' tiles sort alike).
        return sorted(keys, key=lambda k: (k[1], spawn_at.get(k, (0, 0))))
    by_name = defaultdict(list)
    for key in order(rl):
        by_name[names[key]].append(key)
        rl[key] = collapse(rl[key])
    id_name = {i: family(n) for i, n in all_ids.items()}
    ours_by = defaultdict(list)
    for key in list(ol):
        ol[key] = collapse(ol[key])
    for key in order(ol):
        nid = int([v for t, k, v in ol[key] if k == "id"][0])
        ours_by[id_name.get(nid, str(nid))].append(key)
    for name in sorted(set(by_name) | set(ours_by)):
        rk, ok = by_name.get(name, []), ours_by.get(name, [])
        print(f"== {name}: lives recording {len(rk)}, ours {len(ok)}")
        rs, os_ = Counter(), Counter()
        for k in rk:
            rs.update(summary(rl[k]))
        for k in ok:
            os_.update(summary(ol[k]))
        nr, no = max(1, len(rk)), max(1, len(ok))
        for key in sorted(set(rs) | set(os_)):
            r, o = rs[key] / nr, os_[key] / no
            flag = "" if abs(r - o) < 0.5 or (r and o and 0.67 < o / r < 1.5) else "  <<"
            if flag or a.summary:
                print(f"   {key[:70]:70s} per life R {r:6.1f}  O {o:6.1f}{flag}")
        if a.summary:
            continue
        for i in range(min(a.lives, len(rk), len(ok))):
            re_, oe = rl[rk[i]], ol[ok[i]]
            ra, oa = anchor_of(re_, a.align), anchor_of(oe, a.align)
            if ra is None or oa is None:
                print(f"   life {i}: no anchor {a.align} on {'R' if ra is None else 'O'}")
                continue
            rset = {(t - ra, k, v) for t, k, v in re_ if k not in ("move", "id")}
            oset = {(t - oa, k, v) for t, k, v in oe if k not in ("move", "id")}
            both = rset & oset
            print(f"   life {i} (R [{ra}], O [{oa}]): matched {len(both)}, R-only {len(rset - oset)}, O-only {len(oset - rset)}")
            for t, side, k, v in sorted([(t, "R", k, v) for t, k, v in rset - oset] + [(t, "O", k, v) for t, k, v in oset - rset])[:40]:
                print(f"      +{t:4d} {side} {k:7s} {v}")


main()
