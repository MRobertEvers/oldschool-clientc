#!/usr/bin/env python3
"""Every splat and heal in a fight, each given ONE cause, and the recording's causes
diffed against our tick log's: a cause the recording has and ours never produces is a
missing mechanic (Ezekiel's melee under him, his star's second beat: 2026-10-22).

A splat at tick t is judged on the raider's tile at the end of t-1 (`prev`), the tile
a room or npc timer sees. Its cause, the first that fits:

  type:<splat>       not a damage/block splat (poison, burn, shield, ...): its own clock
  kit:<what>         the raider's own kit (a killerwatt shock's 10, ruby bolts (e))
  proj:<id>          a projectile aimed at the raider landing on t
                     (throw + (endtime + 15) // 30); `~1` one tick off
  spot:<id>          a spotanim put on the raider, landing (sent + delay // 30) on t or t-1
  sweep:<npc>@<k>    a room npc that jumped 2+ tiles k ticks before t (k 0..1: a teleport
                     or an exact move: Ezekiel's swords) across prev, footprint swept
  loc:<id>           a loc the room put on prev while its npcs lived (shapes 10, 11, 22: a
                     hazard tile -- Dreadnoth's blood, Bork's ragged fire) and has not removed
  tile:<id>@<d>      a map anim on prev (or the tile now) sent d ticks before t; of
                     several, the one landing nearest t. d separates beats: Ezekiel's star
                     explosion lands S+3 and hits at d=3 and again at d=5
  npc:<id>:<seq>@<k>:<where>
                     the room npc with a sequence k ticks before t (k 0..6, nearest
                     first), where = the raider's prev tile against its footprint:
                     under / adj / near (2..3) / far
  none               nothing above

On a room npc: heal:<id>:<ctx> (a heal splat on either side, with the npc's own seq
or spotanim of the last 4 ticks as ctx), and hit:<id>:content for damage no raider dealt
-- a sword, a cannonball, a battery's blast. The recording cannot name the dealer: a
splat with no projectile landing on it and no raider sequence within 3 tiles in t-2..t
(a raider's hit landing on the same tick hides one). Ours reads the HIT_NPC row's label
(trigger>script): a raider's attack applies through npc_default_damage. Ours' natural
regeneration (`regen`) and a test's cheats (a `[debugproc,..]` setting health) are not heals here;
the labels are trigger>script since 2026-10-22.

Keys use ids, so a recording (rev 241, names printed) and our tick log line up.

    rsprox_hit_causes.py --npc '<name regex>' <--all-players txt>... [--ours ticklog.tsv...]
        [--show KEY-REGEX] [--min 1]

The table: key, recorded n / sessions / values, ours n / values, and a flag:
REC-ONLY (a cause ours never produced: a missing mechanic, or a tile/stance our bots
never stood on -- decide which with a targeted test), OURS-ONLY (a cause Jagex never
showed). `--show` prints each splat of the matching keys with its tick and context.
"""
import argparse
import os
import re
import sys
from collections import Counter, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CONFIGS = os.path.join(ROOT, "OSRS-Content", "osrs-content", "configs")

HIT_TYPES = {16, 17, 12, 13, 43, 26, 28}      # damage / block, any viewer, ours generic
# The viewer's variants and ours' generic one, as one type each.
TYPE_CLASS = {65: "poison", 68: "poison", 66: "venom", 18: "shield", 19: "shield", 44: "shield"}
HEAL_TYPES = {6}
PRAYER = {-1: "none?", 0: "melee", 1: "missiles", 2: "magic", None: "?"}
LOC_SHAPES = {10, 11, 22}
RAIDER_SPOT = re.compile(r"impact|barrage|spell|thrall|sang|ancient|surge|blast|bolt")
# A test's cheat setting health (`::tfaboss` -> toa_boss_set_hp), or a cheat's own
# splat; content a cheat merely starts (`::tfadhbreak` -> tfa_dh_break) still counts.
CHEAT = re.compile(r"^regen$|debugproc,[^\]]*\]$|debugproc.*set_hp")
ROOM = 15                                     # tiles from a live room npc that count as its room
KIT_SPOTS = {560: "shock"}                    # skeleton_killerwatt_electricshock: a 10 on that raider
RUBY_SPOT = 754                               # xbows_blood_sacrifice_spot_anim: the shooter pays


def compack(kind):
    names = {}
    path = os.path.join(CONFIGS, "all.%s.compack" % kind)
    if os.path.exists(path):
        for line in open(path, errors="replace"):
            k, _, v = line.rstrip("\n").partition("=")
            if k.isdigit():
                names[int(k)] = v
    return names


def npc_sizes():
    sizes = {}
    name = None
    for line in open(os.path.join(CONFIGS, "all.npc"), errors="replace"):
        if line.startswith("["):
            name = line.strip()[1:-1]
        elif line.startswith("size=") and name and line[5:].strip().isdigit():
            sizes[name] = int(line[5:])
    return sizes


NPC_NAMES = compack("npc")
SEQ_NAMES = compack("seq")
SPOT_NAMES = compack("spotanim")
HIT_NAMES = compack("hitsplat")
LOC_NAMES = compack("loc")
SIZES = npc_sizes()


def where(tile, sw, size):
    if not tile or not sw:
        return "?"
    dx = max(0, sw[0] - tile[0], tile[0] - (sw[0] + size - 1))
    dz = max(0, sw[1] - tile[1], tile[1] - (sw[1] + size - 1))
    d = max(dx, dz)
    return "under" if d == 0 else "adj" if d == 1 else "near" if d <= 3 else "far"


class Fight:
    """One side's events, by tick."""

    def __init__(self, label):
        self.label = label
        self.ptile = defaultdict(dict)    # who -> tick -> tile (end of tick)
        self.ntile = defaultdict(dict)    # npc key -> tick -> sw tile
        self.ntype = {}                   # npc key -> type id (latest)
        self.nlive = defaultdict(list)    # npc key -> [(from, to)]
        self.nseq = defaultdict(list)     # tick -> [(npc key, seq)]
        self.nspot = defaultdict(list)    # tick -> [(npc key, spot)]
        self.pseq = defaultdict(set)      # tick -> {who}
        self.pspot = defaultdict(list)    # tick -> [(who, spot, land)]
        self.maps = defaultdict(list)     # tile -> [(sent, id, land)]
        self.locs = defaultdict(list)     # tile -> [(tick, loc id or -1)]  hazard shapes only
        self.proj = defaultdict(list)     # land tick -> [(target, id)]  target ("p", who) | ("n", key)
        self.phits = []                   # (t, who, type, value)
        self.phead = defaultdict(dict)    # who -> tick -> overhead (0 melee, 1 missiles, 2 magic, -1 none)
        self.nhits = []                   # (t, npc key, type, value, dealer)  dealer None: unknown
        self.heals = []                   # (t, npc key, value, source)

    def sweeps(self):
        if not hasattr(self, "_sweeps"):
            self._sweeps = defaultdict(list)
            for key, m in self.ntile.items():
                ticks = sorted(m)
                for a, b in zip(ticks, ticks[1:]):
                    p, q = m[a], m[b]
                    if max(abs(p[0] - q[0]), abs(p[1] - q[1])) >= 2:
                        self._sweeps[b].append((key, p, q))
        return self._sweeps

    def at(self, m, t):
        best = None
        for k in sorted(m):
            if k <= t:
                best = m[k]
            else:
                break
        return best


C = r"\((-?\d+), (-?\d+), \d\)"
TICK = re.compile(r"^\[(\d+)\]")
PL = re.compile(r"^        \[(\w+)\] player=\(index=\d+, name=([^,]+), coord=" + C + r"\)(?:, newcoord=" + C + ")?")
NP = re.compile(r"^        \[(\w+)\] npc=\(index=(\d+), id=(?:\S+ )?\(?(\d+)\)?, coord=" + C + r"\)(?:, newcoord=" + C + ")?")
ID = r"id=(?:\S+ \()?(-?\d+)\)?"


def read_rec(path):
    f = Fight(os.path.basename(path))
    t = 0
    cur = None          # ("p", who) | ("n", key)
    tiles = {}
    proj = None
    born = {}
    for line in open(path, errors="replace"):
        m = TICK.match(line)
        if m:
            for who, tl in tiles.items():
                f.ptile[who][t] = tl
            t = int(m.group(1))
            cur = None
            continue
        if line.startswith("    [") and not line.startswith("     "):
            cur = None
            proj = None
            m = re.match(r"^    \[map_anim\] " + ID + r", delay=(\d+)(?:, height=\d+)?, coord=" + C, line)
            if m:
                sid, d, x, z = int(m[1]), int(m[2]), int(m[3]), int(m[4])
                f.maps[(x, z)].append((t, sid, t + d // 30))
                continue
            m = re.match(r"^    \[loc_add_change\w*\] " + ID + r", coord=" + C + r", shape=(\d+)", line)
            if m and int(m[4]) in LOC_SHAPES:
                f.locs[(int(m[2]), int(m[3]))].append((t, int(m[1])))
                continue
            m = re.match(r"^    \[loc_del\w*\] coord=" + C + r", shape=(\d+)", line)
            if m and int(m[3]) in LOC_SHAPES:
                f.locs[(int(m[1]), int(m[2]))].append((t, -1))
                continue
            m = re.match(r"^    \[\w*projanim\w*\] " + ID + r", starttime=(\d+), endtime=(\d+)", line)
            if m:
                proj = (int(m[1]), int(m[3]))
            continue
        if proj and line.startswith("        [target]"):
            mp = re.search(r"player=\(index=\d+, name=([^,]+),", line)
            mn = re.search(r"npc=\(index=(\d+),", line)
            tgt = ("p", mp.group(1)) if mp else ("n", mn.group(1)) if mn else None
            if tgt:
                f.proj[t + (proj[1] + 15) // 30].append((tgt, proj[0]))
            continue
        m = PL.match(line)
        if m:
            who = m.group(2)
            x, z = (m.group(5), m.group(6)) if m.group(5) else (m.group(3), m.group(4))
            tiles[who] = (int(x), int(z))
            cur = ("p", who)
            continue
        m = NP.match(line)
        if m:
            kind, key, typ = m.group(1), m.group(2), int(m.group(3))
            x, z = (m.group(6), m.group(7)) if m.group(6) else (m.group(4), m.group(5))
            f.ntype[key] = typ
            f.ntile[key][t] = (int(x), int(z))
            if kind == "add" or key not in born:
                born[key] = t
            if kind == "del":
                f.nlive[key].append((born.pop(key), t))
            cur = ("n", key)
            continue
        if line.startswith("        [") and ("npc=" in line or "player=" in line):
            cur = None
            continue
        if not cur or not line.startswith("            ["):
            m = re.match(r"^                \[status\] overheadicon=(-?\d+)", line)
            if cur and cur[0] == "p" and m:
                f.phead[cur[1]][t] = int(m.group(1))
            continue
        s = line.strip()
        if cur[0] == "p" and s.startswith("[appearance]"):
            f.phead[cur[1]][t] = -1
            continue
        m = re.match(r"\[status\] overheadicon=(-?\d+)", line.strip())
        if m and cur[0] == "p":
            f.phead[cur[1]][t] = int(m[1])
            continue
        m = re.match(r"\[hit\] id=(?:\S+ \()?(\d+)\)?, value=(-?\d+)", s)
        if m:
            typ, v = int(m[1]), int(m[2])
            if cur[0] == "p":
                f.phits.append((t, cur[1], typ, v))
            elif typ in HEAL_TYPES:
                f.heals.append((t, cur[1], v, ""))
            else:
                f.nhits.append((t, cur[1], typ, v, None))
            continue
        m = re.match(r"\[sequence\] " + ID, s)
        if m and int(m[1]) >= 0:
            if cur[0] == "n":
                f.nseq[t].append((cur[1], int(m[1])))
            else:
                f.pseq[t].add(cur[1])
            continue
        m = re.match(r"\[spotanim\] (?:slot=\d+, )?" + ID + r"(?:, delay=(\d+))?", s)
        if m and int(m[1]) >= 0:
            sid, d = int(m[1]), int(m[2] or 0)
            if cur[0] == "p":
                f.pspot[t].append((cur[1], sid, t + d // 30))
            else:
                f.nspot[t].append((cur[1], sid))
    for who, tl in tiles.items():
        f.ptile[who][t] = tl
    for key, b in born.items():
        f.nlive[key].append((b, t))
    return f


def unpack(c):
    c = int(c)
    return ((c >> 14) & 16383, c & 16383)


def read_ours(path):
    f = Fight(os.path.basename(os.path.dirname(path)) or path)
    born = {}
    last = 0
    for line in open(path, errors="replace"):
        r = line.rstrip("\n").split("\t")
        if len(r) < 9 or not r[1].isdigit():
            continue
        t, k, v = int(r[1]), r[2], r[3:]
        last = t
        if k == "player_tile":
            f.ptile["pid" + v[0]][t] = (int(v[1]), int(v[2]))
        elif k == "npc_spawn":
            key = "s%s.%d" % (v[0], t)
            born[v[0]] = (key, t)
            f.ntype[key] = int(v[1])
            f.ntile[key][t] = unpack(v[2])
        elif k in ("npc_free", "npc_death") and v[0] in born:
            key, b = born[v[0]]
            if k == "npc_free":
                f.nlive[key].append((b, t))
                del born[v[0]]
        elif k == "npc_retype" and v[0] in born:
            f.ntype[born[v[0]][0]] = int(v[2])
        elif k == "npc_tile" and v[0] in born:
            f.ntile[born[v[0]][0]][t] = (int(v[1]), int(v[2]))
        elif k == "npc_anim" and v[0] in born and int(v[2]) >= 0:
            f.nseq[t].append((born[v[0]][0], int(v[2])))
        elif k == "npc_spotanim" and v[0] in born and int(v[2]) >= 0:
            f.nspot[t].append((born[v[0]][0], int(v[2])))
        elif k == "player_anim" and int(v[1]) >= 0:
            f.pseq[t].add("pid" + v[0])
        elif k == "player_spotanim" and int(v[1]) >= 0:
            f.pspot[t].append(("pid" + v[0], int(v[1]), t + int(v[3]) // 30))
        elif k == "loc_set" and int(v[2]) in LOC_SHAPES:
            f.locs[unpack(v[0])].append((t, int(v[1])))
        elif k == "map_spotanim":
            f.maps[unpack(v[0])].append((t, int(v[1]), t + int(v[3]) // 30))
        elif k == "projectile":
            tgt = int(v[2])
            end = int(v[5])
            if tgt < 0:
                target = ("p", "pid%d" % (-tgt - 1))
            elif tgt > 0 and str(tgt - 1) in born:
                target = ("n", born[str(tgt - 1)][0])
            else:
                continue
            f.proj[t + (end + 15) // 30].append((target, int(v[3])))
        elif k == "raider":
            m = re.search(r"head (-?\d+)", r[9] if len(r) > 9 else "")
            if m:
                # Ours is the appearance bitmask, 1 << overhead (0 melee, 1 missiles, 2 magic).
                bits = int(m.group(1))
                f.phead["pid" + v[0]][t] = {0: -1, 1: 0, 2: 1, 4: 2}.get(bits, 9)
        elif k == "heal_player":
            f.phits.append((t, "pid" + v[0], int(v[2]), int(v[1])))
        elif k == "hit_player":
            f.phits.append((t, "pid" + v[0], int(v[3]), int(v[2])))
        elif k == "hit_npc" and v[0] in born:
            # The row's label is the trigger>script that applied it (ticklog
            # HIT_NPC): a raider's attack lands through npc_default_damage; the
            # dealer pid cannot tell (a room script runs as some raider too).
            label = r[9] if len(r) > 9 else ""
            if int(v[3]) in HEAL_TYPES:
                f.heals.append((t, born[v[0]][0], int(v[2]), label))
            else:
                f.nhits.append((t, born[v[0]][0], int(v[3]), int(v[2]), label))
        # npc_heal rows are any rise in hitpoints, a form's bar reset among them;
        # the recording shows heal SPLATS, so ours' heals are its heal-splat
        # hit_npc rows (npc_hitmark(hitsplat_heal, ..) included).
    for key, b in born.values():
        f.nlive[key].append((b, last))
    return f


def room_npcs(f, rx):
    keys = {k for k, typ in f.ntype.items() if rx.search(NPC_NAMES.get(typ, str(typ)))}
    return keys


def live(f, key, t):
    return any(a <= t <= b for a, b in f.nlive.get(key, []))


def size_of(typ):
    return SIZES.get(NPC_NAMES.get(typ, ""), 1)


def player_cause(f, rkeys, t, who, typ, v):
    if typ not in HIT_TYPES:
        return "type:" + TYPE_CLASS.get(typ, HIT_NAMES.get(typ, str(typ)))
    prev = f.ptile[who].get(t - 1) or f.at(f.ptile[who], t - 1)
    now = f.ptile[who].get(t) or prev
    for w, sid, land in f.pspot.get(t, []):
        if w == who and sid in KIT_SPOTS:
            return "kit:" + KIT_SPOTS[sid]
    if who in f.pseq.get(t - 1, ()) and any(s == RUBY_SPOT for _, s in f.nspot.get(t - 1, [])):
        return "kit:rubybolt"
    best = None
    for off in (0, -1, 1):
        for tgt, pid in f.proj.get(t + off, []):
            if tgt == ("p", who):
                best = "proj:%d%s" % (pid, "" if off == 0 else "~1")
                break
        if best:
            return best
    # A spotanim with no delay sent as the raider swung is their weapon's (a launch).
    for dt in range(0, 7):
        for w, sid, land in f.pspot.get(t - dt, []):
            if land == t - dt and who in f.pseq.get(t - dt, ()):
                continue
            if w == who and land in (t, t - 1) and sid not in KIT_SPOTS:
                return "spot:%d" % sid
    for k in (0, 1):
        for key, p, q in f.sweeps().get(t - k, []):
            if key in rkeys and prev:
                n = size_of(f.ntype[key]) - 1
                if (min(p[0], q[0]) <= prev[0] <= max(p[0], q[0]) + n and
                        min(p[1], q[1]) <= prev[1] <= max(p[1], q[1]) + n):
                    return "sweep:%d@%d" % (f.ntype[key], k)
    cands = []
    for tile in {prev, now}:
        for sent, sid, land in f.maps.get(tile, []):
            if t - 8 <= sent <= t:
                cands.append((abs(land - t), t - sent, sid))
    if cands:
        cands.sort()
        _, d, sid = cands[0]
        return "tile:%d@%d" % (sid, d)
    if prev:
        cur_loc = None
        for tick, lid in f.locs.get(prev, []):
            if tick <= t - 1:
                cur_loc = (tick, lid)
        if cur_loc and cur_loc[1] >= 0 and not LOC_NAMES.get(cur_loc[1], "").startswith("invisible") and any(
                a <= cur_loc[0] <= b for k in rkeys for a, b in f.nlive.get(k, [])):
            return "loc:%d" % cur_loc[1]
    for k in range(0, 7):
        hits = []
        for key, seq in f.nseq.get(t - k, []):
            if key in rkeys:
                sw = f.at(f.ntile[key], t - 1)
                w = where(prev, sw, size_of(f.ntype[key]))
                hits.append(({"under": 0, "adj": 1, "near": 2, "far": 3, "?": 4}[w], key, seq, w))
        if hits:
            hits.sort()
            _, key, seq, w = hits[0]
            return "npc:%d:%d@%d:%s" % (f.ntype[key], seq, k, w)
    return "none"


def npc_ctx(f, key, t):
    for k in range(0, 5):
        for kk, seq in f.nseq.get(t - k, []):
            if kk == key:
                return "seq%d@%d" % (seq, k)
        for kk, sid in f.nspot.get(t - k, []):
            if kk == key:
                return "spot%d@%d" % (sid, k)
    return "-"


def npc_hit_cause(f, key, t, label):
    if label is not None:
        # A raider's attack applies through its target's [ai_queue2,..] and a
        # proc (npc_default_damage, or a room's own: Dagon'hai's shield).
        raider = not label or "npc_default_damage" in label or re.match(r"(hitmark )?\[ai_queue2,[^\]]*\]>", label)
        return None if raider else "content"
    for off in (0, -1, 1):
        if any(tgt == ("n", key) for tgt, _ in f.proj.get(t + off, [])):
            return None
    # A raider's spell on it (a barrage's splash sends no projectile to its
    # other targets) or a thrall's: an impact spotanim on the npc.
    for k in range(0, 5):
        for kk, sid in f.nspot.get(t - k, []):
            if kk == key and RAIDER_SPOT.search(SPOT_NAMES.get(sid, "")):
                return None
    sw = f.at(f.ntile[key], t)
    size = size_of(f.ntype[key])
    for k in range(0, 3):
        for who in f.pseq.get(t - k, ()):
            tl = f.at(f.ptile[who], t - k)
            if where(tl, sw, size) in ("under", "adj", "near") or any(
                    tgt == ("n", key) for d in range(0, 8) for tgt, _ in f.proj.get(t + d, [])):
                return None
    return "content"


def collect(f, rx, out, show):
    rkeys = room_npcs(f, rx)
    if not rkeys:
        return
    for t, who, typ, v in f.phits:
        alive = [k for k in rkeys if live(f, k, t)]
        if not alive:
            continue
        # In the room: within ROOM tiles of a live room npc (instances are
        # translated to their templates, and neighbouring rooms' templates touch).
        tl = f.at(f.ptile[who], t - 1)
        near = [f.at(f.ntile[k], t) for k in alive]
        if not tl or not any(sw and max(abs(tl[0] - sw[0]), abs(tl[1] - sw[1])) <= ROOM for sw in near):
            continue
        key = player_cause(f, rkeys, t, who, typ, v)
        head = f.at(f.phead[who], t) if f.phead.get(who) else None
        out[key].append((f.label, t, who, v, PRAYER.get(head, "?")))
    for t, key, v, src in f.heals:
        if key in rkeys and not CHEAT.search(src):
            out["heal:%d:%s" % (f.ntype[key], npc_ctx(f, key, t))].append((f.label, t, src or "-", v))
    for t, key, typ, v, dealer in f.nhits:  # dealer: ours' label, None on a recording
        if key in rkeys and typ not in HEAL_TYPES:
            if dealer and CHEAT.search(dealer):
                continue
            c = npc_hit_cause(f, key, t, dealer)
            if c:
                out["hit:%d:%s" % (f.ntype[key], c)].append(
                    (f.label, t, "%s %s" % (dealer or npc_ctx(f, key, t), HIT_NAMES.get(typ, str(typ))), v))


def name_key(key):
    def nm(m):
        kind, n = m.group(1), int(m.group(2))
        table = {"proj": SPOT_NAMES, "spot": SPOT_NAMES, "tile": SPOT_NAMES}.get(kind)
        return "%s:%s" % (kind, table.get(n, n) if table else n)
    s = re.sub(r"^(proj|spot|tile):(\d+)", nm, key)
    s = re.sub(r"^loc:(\d+)", lambda m: "loc:%s" % LOC_NAMES.get(int(m.group(1)), m.group(1)), s)
    s = re.sub(r"^sweep:(\d+)", lambda m: "sweep:%s" % NPC_NAMES.get(int(m.group(1)), m.group(1)), s)
    s = re.sub(r"^(npc|heal|hit):(\d+)", lambda m: "%s:%s" % (m.group(1), NPC_NAMES.get(int(m.group(2)), m.group(2))), s)
    s = re.sub(r"^(npc:[^:]+):(\d+)", lambda m: "%s:%s" % (m.group(1), SEQ_NAMES.get(int(m.group(2)), m.group(2))), s)
    s = re.sub(r"seq(\d+)@", lambda m: "%s@" % SEQ_NAMES.get(int(m.group(1)), m.group(1)), s)
    s = re.sub(r"spot(\d+)@", lambda m: "%s@" % SPOT_NAMES.get(int(m.group(1)), m.group(1)), s)
    return s


def span(rows):
    vs = sorted(r[3] for r in rows)
    return "%d..%d" % (vs[0], vs[-1]) if vs else ""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("txt", nargs="*")
    ap.add_argument("--npc", required=True, help="regex on the room's npc names (our cache's)")
    ap.add_argument("--ours", nargs="*", default=[])
    ap.add_argument("--show", help="regex on the keys whose splats to print")
    ap.add_argument("--min", type=int, default=1, help="hide keys with fewer recorded splats")
    ap.add_argument("--by-prayer", action="store_true",
                    help="under each raider key, its values per overhead prayer (a --bas transcript has every raider's)")
    a = ap.parse_args()
    rx = re.compile(a.npc)
    rec, ours = defaultdict(list), defaultdict(list)
    for p in a.txt:
        collect(read_rec(p), rx, rec, a.show)
    for p in a.ours:
        collect(read_ours(p), rx, ours, a.show)
    keys = sorted(set(rec) | set(ours), key=lambda k: (-len(rec.get(k, [])), k))
    print("%-78s %5s %4s %-9s %5s %-9s %s" % ("cause", "rec", "ses", "values", "ours", "values", "flag"))
    for k in keys:
        r, o = rec.get(k, []), ours.get(k, [])
        if len(r) < a.min and not o:
            continue
        flag = ""
        if a.ours:
            flag = "REC-ONLY" if r and not o else "OURS-ONLY" if o and not r else ""
        print("%-78s %5d %4d %-9s %5d %-9s %s" % (name_key(k)[:78], len(r), len({x[0] for x in r}), span(r), len(o), span(o), flag))
        if a.by_prayer and (r or o) and len((r or o)[0]) > 4:
            for side, rows in (("rec", r), ("ours", o)):
                by = defaultdict(list)
                for x in rows:
                    by[x[4]].append(x)
                if by:
                    print("      %-4s %s" % (side, "  ".join("%s %d: %s" % (pr, len(xs), span(xs)) for pr, xs in sorted(by.items()))))
    if a.show:
        sx = re.compile(a.show)
        for side, d in (("rec", rec), ("ours", ours)):
            for k in keys:
                if sx.search(k) or sx.search(name_key(k)):
                    for x in d.get(k, []):
                        print("  %s %s %s [%d] %s %s%s" % (side, name_key(k), x[0], x[1], x[2], x[3],
                                                          " (%s)" % x[4] if len(x) > 4 else ""))


if __name__ == "__main__":
    main()
