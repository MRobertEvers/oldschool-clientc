#!/usr/bin/env python3
"""The Great Olm with a PARTY: what Jagex's server does when more than one raider
is in the chamber, read from an RSProx transcript made with every player shown.

The archive's transcripts print the recorder alone (PLAYER_INFO_LOCAL_PLAYER_ONLY
in ~/.rsprox/settings). Re-transcribe with it off, into a private RSProx home so
the shared ~/.rsprox/binary/archive/<id>.txt is left alone:

    H=$SCRATCH/home; mkdir -p $H/.rsprox/binary/archive $H/.rsprox/settings
    ln -s ~/.rsprox/caches $H/.rsprox/caches
    cp ~/.rsprox/binary/archive/820.bin $H/.rsprox/binary/archive/
    sed 's/PLAYER_INFO_LOCAL_PLAYER_ONLY=true/PLAYER_INFO_LOCAL_PLAYER_ONLY=false/' \\
        ~/.rsprox/settings/settings-*.txt > $H/.rsprox/settings/settings-1.txt
    java -Duser.home=$H -cp "$(cat rsprox.cp)" net.rsprox.proxy.cli.TranscribeCommandKt -name archive/820

    python3 tools/cox/rsprox_olm_party.py $H/.rsprox/binary/archive/820.txt [section ...]

Sections (all by default): actions, autos, turns, spheres, burst, lightning,
teleport, powers, falls. Each prints its rows, then a verdict line per rule.

Positions: Olm acts in the npc phase, before the players move, so every rule
is judged on the tile each raider STARTED the tick on (a walk line's coord=,
not its newcoord=). The chamber frame is the transcript's own: local tile =
(x - 3216, z - 5704), the frame of cox_olm.constant.
"""
import re
import sys
from collections import Counter, defaultdict

TICK = re.compile(r"^\[(\d+)\]$")
ENTITY = re.compile(r"^ {8}\[([a-z_]+)\] (player|npc)=\(index=(\d+), (?:name=([^,]*)|id=([a-z0-9_]+) \(\d+\))"
                    r".*?coord=\((\d+), (\d+), (\d+)\)[^)]*\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?")
EXT = re.compile(r"^ {12}\[([a-z_]+)\] (.*)$")
PACKET = re.compile(r"^ {4}\[([a-z_0-9]+)\] ?(.*)$")
SUB = re.compile(r"^ {8}\[(source|target)\] coord=\((\d+), (\d+), (\d+)\)(?:, (player|npc)=\(index=\d+, "
                 r"(?:name=([^,]*)|id=([a-z0-9_]+) \(\d+\)))?")
COORD = re.compile(r"coord=\((\d+), (\d+), (\d+)\)")
ID = re.compile(r"id=([a-z0-9_]+) \((\d+)\)")
NUM = re.compile(r"([a-z]+)=(-?\d+)")

# the chamber (cox_olm.constant, local + (3216, 5704))
CH_X0, CH_X1, CH_Z0, CH_Z1 = 3227, 3237, 5730, 5748
SEP_Z, MID_Z0, MID_Z1 = 5740, 5735, 5745
HEAD_Z = 5738
WEST_X, EAST_X = 3220, 3238


def in_chamber(c):
    return c is not None and c[2] == 0 and CH_X0 <= c[0] <= CH_X1 and CH_Z0 <= c[1] <= CH_Z1


def in_view(d, c):
    if not in_chamber(c):
        return False
    z = c[1]
    if d == "S":
        return z <= SEP_Z
    if d == "N":
        return z >= SEP_Z
    return MID_Z0 <= z <= MID_Z1


def zone_counts(starts):
    s = m = n = 0
    for c in starts.values():
        if not in_chamber(c):
            continue
        if MID_Z0 <= c[1] <= MID_Z1:
            m += 1
        if c[1] > SEP_Z:
            n += 1
        if c[1] < SEP_Z:
            s += 1
    return s, m, n


def most_of(s, m, n, wall, extra_left=False, extra_right=False):
    """cox_olm.rs2 ~cox_olm_most_of: middle wins every tie; the wall decides N/S."""
    if wall == "W":
        n += 1 if extra_left else 0
        s += 1 if extra_right else 0
        if m >= s and m >= n:
            return "M"
        return "N" if n >= s else "S"
    s += 1 if extra_left else 0
    n += 1 if extra_right else 0
    if m >= s and m >= n:
        return "M"
    return "S" if s >= n else "N"


def side_to_dir(side, wall):
    """The head's own left/right/front -> the chamber's N/M/S (west wall: left is north)."""
    if side == "front":
        return "M"
    if wall == "W":
        return "N" if side == "left" else "S"
    return "S" if side == "left" else "N"


def head_anim(name):
    """('attack' | 'turn' | 'idle' | other, the side it ends facing)."""
    a = name.replace("olm_head_", "")
    if a.startswith("attack"):
        for side in ("left", "right", "front"):
            if side in a:
                return "attack", side
    if a.startswith("turn") or a.startswith("rotate"):
        a = a.replace("_enraged", "")
        if a.endswith("reverse") or a.endswith("reversed"):
            return "turn", "front"
        if a.endswith("left_right"):
            return "turn", "right"
        if a.endswith("right_left"):
            return "turn", "left"
        return "turn", "left" if "left" in a else "right"
    if a.startswith("idle"):
        for side in ("left", "right", "front"):
            if side in a:
                return "idle", side
    return a, None


class Tick:
    def __init__(self, t):
        self.t = t
        self.start = {}         # name -> coord the raider began the tick on
        self.end = {}           # name -> coord after its move
        self.moves = {}         # name -> walk | run | teleport | idle | ...
        self.pext = defaultdict(list)   # name -> [(kind, text)]
        self.next = defaultdict(list)   # npc id name -> [(kind, text)]
        self.packets = []       # (kind, text, [sub lines])
        self.messages = []


def parse(path):
    ticks = {}
    cur = None
    me = None
    section = None
    actor = None
    last_packet = None
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            line = raw.rstrip("\n")
            m = TICK.match(line)
            if m:
                t = int(m.group(1))
                cur = ticks.setdefault(t, Tick(t))
                section = None
                continue
            if cur is None:
                if "local player index" in line:
                    me = line.split(":")[-1].strip()
                continue
            m = PACKET.match(line)
            if m:
                kind = m.group(1)
                section = kind if kind.startswith("player_info") or kind.startswith("npc_info") else None
                actor = None
                last_packet = (kind, m.group(2), [])
                cur.packets.append(last_packet)
                if kind == "message_game":
                    mm = re.search(r'message="(.*)"$', m.group(2))
                    if mm:
                        cur.messages.append(mm.group(1))
                continue
            m = ENTITY.match(line)
            if m and section:
                kind, who = m.group(1), m.group(2)
                name = m.group(4) if who == "player" else m.group(5)
                c0 = (int(m.group(6)), int(m.group(7)), int(m.group(8)))
                c1 = (int(m.group(9)), int(m.group(10)), int(m.group(11))) if m.group(9) else c0
                if who == "player":
                    cur.start[name] = c0
                    cur.end[name] = c1
                    cur.moves[name] = kind
                actor = (who, name)
                continue
            m = SUB.match(line)
            if m and last_packet is not None and not section:
                last_packet[2].append(m.groups())
                continue
            m = EXT.match(line)
            if m and actor:
                if actor[0] == "player":
                    cur.pext[actor[1]].append((m.group(1), m.group(2)))
                else:
                    cur.next[actor[1]].append((m.group(1), m.group(2)))
    # carry the raiders' tiles across ticks with no line for them
    where = {}
    for t in sorted(ticks):
        tk = ticks[t]
        for name, c in where.items():
            tk.start.setdefault(name, c)
            tk.end.setdefault(name, c)
        where = dict(tk.end)
    return ticks, me


def fights(ticks):
    """[(first tick, last tick)] of each Olm fight: the first olm_head_spawning add to the collapse."""
    out = []
    begin = None
    for t in sorted(ticks):
        for kind, text, _ in ticks[t].packets:
            if begin is None and kind == "loc_add_change_v2" and "olm_head_spawning" in text:
                begin = t
        for msg in ticks[t].messages:
            if begin is not None and "As the Great Olm collapses" in msg:
                out.append((begin, t))
                begin = None
    return out


def head_events(ticks, t0, t1):
    """Per tick: (wall, kind, side) of the head loc's animation, the head's projectiles, the
    left claw's animation."""
    rows = {}
    for t in range(t0, t1 + 1):
        tk = ticks.get(t)
        if not tk:
            continue
        head = None
        claw = None
        shots = []
        for kind, text, subs in tk.packets:
            if kind == "loc_anim":
                c = COORD.search(text)
                a = re.search(r"anim=([a-z0-9_]+)", text)
                if not c or not a:
                    continue
                x, z = int(c.group(1)), int(c.group(2))
                wall = "W" if x == WEST_X else "E" if x == EAST_X else None
                if wall and z == HEAD_Z and a.group(1).startswith("olm_head_"):
                    k, side = head_anim(a.group(1))
                    if k in ("attack", "turn"):
                        head = (wall, k, side, a.group(1))
                elif wall and a.group(1).startswith("olm_hand_left_") and (
                        "cast" in a.group(1) or "gripping_attack" in a.group(1)):
                    claw = a.group(1).replace("olm_hand_left_", "")
            elif kind == "map_projanim_v2":
                i = ID.search(text)
                if i and i.group(1).startswith("olm_"):
                    src = [s for s in subs if s[0] == "source"]
                    tgt = [s for s in subs if s[0] == "target"]
                    shots.append((i.group(1), src[0][1:4] if src else None, tgt[0] if tgt else None, text))
        if head or claw or shots:
            rows[t] = (head, claw, shots)
    return rows


def raiders_in(tk):
    return {n: c for n, c in tk.start.items() if in_chamber(c)}


def fmt_zone(c):
    if not in_chamber(c):
        return "out"
    z = c[1]
    tags = []
    if z <= SEP_Z:
        tags.append("S")
    if MID_Z0 <= z <= MID_Z1:
        tags.append("M")
    if z >= SEP_Z:
        tags.append("N")
    return "".join(tags)


HEAD_SHOTS = ("olm_firebreath_travel", "olm_generic_range_proj")


def section_autos(ticks, fs):
    """Rule: an auto goes at EVERY raider in the faced view, and at nobody outside it."""
    agree = disagree = 0
    sizes = Counter()
    for t0, t1 in fs:
        ev = head_events(ticks, t0, t1)
        for t, (head, claw, shots) in sorted(ev.items()):
            autos = [s for s in shots if s[0] in HEAD_SHOTS]
            if not autos or not head or head[1] != "attack":
                continue
            d = side_to_dir(head[2], head[0])
            tk = ticks[t]
            inside = raiders_in(tk)
            want = {n for n, c in inside.items() if in_view(d, c)}
            got = {s[2][5] for s in autos if s[2] and s[2][5]}
            sizes[(len(inside), len(got))] += 1
            if want == got:
                agree += 1
            else:
                disagree += 1
                print("  t%d %s faces %s: shot %s; in view %s; tiles %s" % (
                    t, head[0], d, sorted(got), sorted(want),
                    {n: (c[0] - 3216, c[1] - 5704, fmt_zone(c)) for n, c in inside.items()}))
    print("autos: %d at exactly the raiders in the faced view, %d otherwise" % (agree, disagree))
    print("  (raiders in the chamber, raiders shot): %s" % dict(sorted(sizes.items())))


def section_turns(ticks, fs):
    """Rule (cox_olm.rs2 ~cox_olm_act): the head turns to the zone with the most raiders --
    middle wins a tie, the wall decides north/south -- with a hand hit since the last step
    adding one to that hand's side."""
    agree = disagree = 0
    steer_only = 0
    rows = []
    for t0, t1 in fs:
        ev = head_events(ticks, t0, t1)
        last_step = None
        for t, (head, claw, shots) in sorted(ev.items()):
            if not head or head[1] != "turn":
                continue
            wall = head[0]
            d = side_to_dir(head[2], wall)
            tk = ticks[t]
            s, m, n = zone_counts(tk.start)
            plain = most_of(s, m, n, wall)
            # hand hits in the 4 ticks before the turn
            lh = rh = False
            for u in range(t - 4, t):
                uk = ticks.get(u)
                if not uk:
                    continue
                for kind, text in uk.next.get("olm_hand_left", []):
                    if kind == "hit" and not text.endswith("value=0"):
                        lh = True
                for kind, text in uk.next.get("olm_hand_right", []):
                    if kind == "hit" and not text.endswith("value=0"):
                        rh = True
            steered = most_of(s, m, n, wall, lh, rh)
            ok = d == steered
            if ok:
                agree += 1
            else:
                disagree += 1
            if d == steered and d != plain:
                steer_only += 1
            rows.append((t, wall, d, (s, m, n), plain, steered, lh, rh, ok, claw))
    for r in rows:
        if not r[8]:
            print("  t%d %s turned %s: S/M/N %s, rule %s (steered %s; left hit %s right hit %s) claw %s" % (
                r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[9]))
    print("turns: %d follow the rule (%d of them only by the hand steer), %d do not" % (
        agree, steer_only, disagree))
    return rows


def section_actions(ticks, fs):
    """The head's action grid: every head attack/turn and claw cast, on a 4-tick clock."""
    for t0, t1 in fs:
        ev = head_events(ticks, t0, t1)
        grid = Counter(t % 4 for t, (h, c, s) in ev.items() if h or c)
        print("fight t%d..t%d: action ticks mod 4 %s" % (t0, t1, dict(grid)))


def section_spheres(ticks, fs):
    """Rule: a sphere at every raider in the faced view, three colours, each at most once."""
    for t0, t1 in fs:
        ev = head_events(ticks, t0, t1)
        for t, (head, claw, shots) in sorted(ev.items()):
            sph = [s for s in shots if s[0].startswith("olm_weak_")]
            if not sph:
                continue
            d = side_to_dir(head[2], head[0]) if head else "?"
            inside = raiders_in(ticks[t])
            want = sorted(n for n, c in inside.items() if d != "?" and in_view(d, c))
            got = [(s[2][5], s[0].replace("olm_weak_", "").replace("_proj", "")) for s in sph if s[2]]
            print("  t%d faces %s: spheres %s; in view %s; in chamber %d" % (t, d, got, want, len(inside)))


def section_burst(ticks, fs):
    """Rule: a seedling (olm_crystal_attack_small) under every raider in the chamber."""
    agree = disagree = 0
    for t0, t1 in fs:
        for t in range(t0, t1 + 1):
            tk = ticks.get(t)
            if not tk:
                continue
            seeds = []
            for kind, text, _ in tk.packets:
                if kind == "loc_add_change_v2" and "olm_crystal_attack_small" in text:
                    c = COORD.search(text)
                    seeds.append((int(c.group(1)), int(c.group(2)), int(c.group(3))))
            if not seeds:
                continue
            inside = raiders_in(tk)
            tiles = set(inside.values())
            ok = set(seeds) == tiles
            agree += ok
            disagree += not ok
            if not ok:
                print("  t%d seedlings %s; raiders %s" % (t, sorted(seeds), sorted(inside.items())))
    print("burst: %d casts with a seedling exactly under each raider's tile, %d otherwise" % (agree, disagree))


def section_lightning(ticks, fs):
    """Rule: the bolts' count, columns and directions; the party in the chamber."""
    counts = Counter()
    for t0, t1 in fs:
        ev = head_events(ticks, t0, t1)
        for t, (head, claw, shots) in sorted(ev.items()):
            if not claw or "earthshock" not in claw:
                continue
            tk = ticks[t]
            bolts = []
            for kind, text, _ in tk.packets:
                if kind == "map_anim" and "olm_shockwave" in text:
                    c = COORD.search(text)
                    bolts.append((int(c.group(1)), int(c.group(2))))
            # (a bolt may show two tiles of its column on the cast tick -- its start and
            # its first step: count one per column and end)
            south = sorted({b[0] - 3216 for b in bolts if b[1] < 5739})
            north = sorted({b[0] - 3216 for b in bolts if b[1] >= 5739})
            inside = raiders_in(tk)
            n = len(south) + len(north)
            both = sorted(set(south) & set(north))
            counts[(len(inside), n)] += 1
            print("  t%d %d raiders: %d bolts, from the south end %s, from the north end %s%s" % (
                t, len(inside), n, south, north, (", column(s) %s from both ends" % both) if both else ""))
    print("lightning (raiders, bolts on the cast tick): %s" % dict(sorted(counts.items())))


def section_teleport(ticks, fs):
    """Rule: raiders paired, each pair its colour (olm_playerswap_N), the swap and its hit."""
    for t0, t1 in fs:
        ev = head_events(ticks, t0, t1)
        for t, (head, claw, shots) in sorted(ev.items()):
            if not claw or "earthquake" not in claw:
                continue
            tk = ticks[t]
            inside = raiders_in(tk)
            colours = defaultdict(list)
            for u in range(t, t + 12):
                uk = ticks.get(u)
                if not uk:
                    continue
                for name, ext in uk.pext.items():
                    for kind, text in ext:
                        if kind == "spotanim" and "olm_playerswap" in text:
                            colours[name].append((u - t, ID.search(text).group(1)[-1]))
                for kind, text, _ in uk.packets:
                    if kind == "map_anim" and "olm_playerswap" in text:
                        c = COORD.search(text)
                        colours["map(%d,%d)" % (int(c.group(1)) - 3216, int(c.group(2)) - 5704)].append(
                            (u - t, ID.search(text).group(1)[-1]))
            swaps = []
            for u in range(t, t + 14):
                uk = ticks.get(u)
                if not uk:
                    continue
                for name, mv in uk.moves.items():
                    if mv == "teleport" and name in inside:
                        swaps.append((u - t, name, uk.start[name], uk.end[name]))
            hits = defaultdict(list)
            for u in range(t + 9, t + 13):
                uk = ticks.get(u)
                if not uk:
                    continue
                for name, ext in uk.pext.items():
                    for kind, text in ext:
                        if kind == "hit":
                            hits[name].append((u - t, int(NUM.findall(text)[1][1])))
            msgs = [(u - t, msg) for u in range(t, t + 14) if ticks.get(u)
                    for msg in ticks[u].messages if "pair" in msg or "teleport attack" in msg]
            print("  t%d %d raiders in the chamber:" % (t, len(inside)))
            for name in sorted(inside):
                c = inside[name]
                print("     %-12s at (%d,%d) colours %s" % (name, c[0] - 3216, c[1] - 5704,
                                                         " ".join("+%d:%s" % x for x in colours.get(name, []))))
            for k, v in colours.items():
                if k.startswith("map"):
                    print("     %s %s" % (k, " ".join("+%d:%s" % x for x in v)))
            for s in swaps:
                d = max(abs(s[2][0] - s[3][0]), abs(s[2][1] - s[3][1]))
                print("     +%d %s teleported (%d,%d) -> (%d,%d), %d tiles; hits after %s" % (
                    s[0], s[1], s[2][0] - 3216, s[2][1] - 5704, s[3][0] - 3216, s[3][1] - 5704, d,
                    [h for h in hits.get(s[1], []) if h[0] >= s[0]]))
            for m in msgs:
                print("     +%d recorder: %s" % m)


POWER_SHOTS = ("olm_acid_spit", "olm_burnwithme_travel", "olm_burnwithme_travel_pl", "olm_firewall_travel",
               "olm_crystal_bomb_travel", "olm_healme_spit")


def section_powers(ticks, fs):
    """Rule: who each phase attack picks, and how many it throws."""
    for t0, t1 in fs:
        ev = head_events(ticks, t0, t1)
        for t, (head, claw, shots) in sorted(ev.items()):
            if not head or head[1] != "attack":
                continue
            p = [s for s in shots if s[0] in POWER_SHOTS]
            if not p:
                continue
            d = side_to_dir(head[2], head[0])
            tk = ticks[t]
            inside = raiders_in(tk)
            view = sorted(n for n, c in inside.items() if in_view(d, c))
            kinds = Counter(s[0] for s in p)
            at_players = sorted(s[2][5] for s in p if s[2] and s[2][5])
            tiles = [(int(s[2][1]) - 3216, int(s[2][2]) - 5704) for s in p if s[2] and not s[2][5]]
            extra = ""
            # the crystal power's target: sp_attackglow_red on a raider this tick or the next
            for u in (t, t + 1):
                uk = ticks.get(u)
                for name, ext in (uk.pext.items() if uk else []):
                    for kind, text in ext:
                        if kind == "spotanim" and "attackglow_red" in text:
                            extra += " glow:%s(+%d)" % (name, u - t)
            rows = ", ".join("%s x%d" % (k.replace("olm_", ""), v) for k, v in kinds.items())
            print("  t%d faces %s, view %s: %s; at raiders %s; at %d tiles%s" % (
                t, d, view, rows, at_players, len(tiles), extra))
            if any(k == "olm_firewall_travel" for k in kinds):
                rows_z = sorted({tz for tx, tz in tiles})
                print("     wall rows %s; raiders' rows %s" % (
                    rows_z, {n: c[1] - 5704 for n, c in inside.items()}))


def section_burnspread(ticks, fs):
    """Rule: Burn With Me passes from a burning raider to the raiders beside them."""
    for t0, t1 in fs:
        for t in range(t0, t1 + 1):
            tk = ticks.get(t)
            if not tk:
                continue
            for kind, text, subs in tk.packets:
                if kind != "map_projanim_v2" or "olm_burnwithme_travel_pl" not in text:
                    continue
                src = [s for s in subs if s[0] == "source"]
                tgt = [s for s in subs if s[0] == "target"]
                sc = tuple(int(v) for v in src[0][1:4]) if src else None
                who = [n for n, c in tk.start.items() if c == sc]
                tc = tuple(int(v) for v in tgt[0][1:4]) if tgt else None
                d = max(abs(sc[0] - tc[0]), abs(sc[1] - tc[1])) if sc and tc else -1
                says = [n for n, ext in tk.pext.items() for k, x in ext if k == "say" or "Burn" in x]
                print("  t%d from %s (%s) to %s, %d tiles; %s; burning say: %s" % (
                    t, who, sc and (sc[0] - 3216, sc[1] - 5704), tgt[0][5] if tgt else None, d,
                    " ".join(re.findall(r"(?:starttime|endtime|angle|progress|startheight|endheight)=\d+", text)),
                    says))


def section_falls(ticks, fs):
    """Rule: the transitional falling crystals -- how many a tick, and how many at raiders."""
    for t0, t1 in fs:
        per = Counter()
        aimed = Counter()
        for t in range(t0, t1 + 1):
            tk = ticks.get(t)
            if not tk:
                continue
            tiles = set(tk.start.values())
            for kind, text, subs in tk.packets:
                if kind == "map_projanim_v2" and "olm_crystalrock_falling" in text:
                    tgt = [s for s in subs if s[0] == "target"]
                    tc = tuple(int(v) for v in tgt[0][1:4]) if tgt else None
                    per[t] += 1
                    if tc in tiles:
                        aimed[t] += 1
        spans = []
        for t in sorted(per):
            if spans and t - spans[-1][-1] <= 3:
                spans[-1].append(t)
            else:
                spans.append([t])
        for sp in spans:
            n = sum(per[t] for t in sp)
            a = sum(aimed[t] for t in sp)
            inside = len(raiders_in(ticks[sp[0]]))
            gaps = Counter(b - a_ for a_, b in zip([t for t in sp if aimed[t]], [t for t in sp if aimed[t]][1:]))
            print("  t%d..t%d (%d ticks) %d raiders: %d crystals, %d on a raider's tile, aimed-crystal gaps %s,"
                  " per tick %s" % (sp[0], sp[-1], sp[-1] - sp[0] + 1, inside, n, a, dict(gaps),
                                    dict(Counter(per[t] for t in sp))))


SECTIONS = {"actions": section_actions, "autos": section_autos, "turns": section_turns,
            "spheres": section_spheres, "burst": section_burst, "lightning": section_lightning,
            "teleport": section_teleport, "powers": section_powers, "burnspread": section_burnspread,
            "falls": section_falls}


def main(argv):
    assert argv, __doc__
    ticks, me = parse(argv[0])
    fs = fights(ticks)
    print("recorder %s; fights %s" % (me, fs))
    names = argv[1:] or list(SECTIONS)
    for name in names:
        print("== %s" % name)
        SECTIONS[name](ticks, fs)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
