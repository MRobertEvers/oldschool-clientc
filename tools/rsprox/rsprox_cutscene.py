"""rsprox_cutscene.py <transcript> <from> <to> --origin X,Z [--name PREFIX]: a RuneScript draft of a recorded cutscene.

Reads one window of an `rsprox_index.py get` transcript (template-translated tiles) and prints the
statements that send the same packets on the same ticks, in the order the recording sent them:

  * actors: npc adds (with their spawn face), walks grouped into straight runs (one `npc_walk` per run,
    issued on the run's first step tick), teleports, seqs, spotanims, faces, says, transforms, dels;
  * the player: seqs, spotanims, faces, says, teleports (as `p_telejump` to an instance tile);
  * camera: cam_moveto / cam_lookat / cam_shake / cam_reset with every number as recorded;
  * the world: loc_add_change / loc_del / map_anim / sound_area / synth_sound / midi_song / jingles;
  * dialogue: every page as the chat proc that draws it (npc / player / mesbox / objbox), with its
    speaker, its chathead npc and its expression (the recorded chathead seq's base mood);
  * fades: script 948 as `~natt_fade(a, b, colour, alpha, cycles)`.

Tiles are printed as `~<name>_at(dx, dz, level)`: offsets from `--origin` (the SW tile of the copied
template square), so the draft drops into an instance copy unchanged. Every 239 symbol is looked up in
OSRS-Content/osrs-content (configs/all.*.compack, pack/4_soundeffects.pack, pack/6_musictracks.pack);
an id the 239 cache names differently is still printed by its 239 name, with the recorded name in a
comment -- check those (recorded ids are the session's revision).

Time: a page blocks until its click; the statements after it run on the click tick. Between action
ticks the draft emits `p_delay(gap - 1)` (p_delay(n) resumes on now + 1 + n). The draft is a starting
point: read it against the transcript and the analysis doc before it becomes content.
"""
import argparse
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "OSRS-Content", "osrs-content")


def table(path):
    out = {}
    try:
        for line in open(path, encoding="utf-8", errors="replace"):
            if "=" in line:
                k, v = line.strip().split("=", 1)
                out[k] = v.split(" ")[0]
    except OSError:
        pass
    return out


NPC = table(os.path.join(ROOT, "configs", "all.npc.compack"))
SEQ = table(os.path.join(ROOT, "configs", "all.seq.compack"))
SPOT = table(os.path.join(ROOT, "configs", "all.spotanim.compack"))
LOC = table(os.path.join(ROOT, "configs", "all.loc.compack"))
SYNTH = table(os.path.join(ROOT, "pack", "4_soundeffects.pack"))
MIDI = table(os.path.join(ROOT, "pack", "6_musictracks.pack"))
JINGLE = table(os.path.join(ROOT, "pack", "11_musicjingles.pack"))
SHAPES = {0: "wall_straight", 1: "wall_diagonalcorner", 2: "wall_l", 3: "wall_squarecorner",
          4: "walldecor_straight_nooffset", 9: "wall_diagonal", 10: "centrepiece_straight",
          11: "centrepiece_diagonal", 22: "grounddecor"}
MOOD = {"chatneu": "^chat_neutral", "chathap": "^chat_happy", "chatang": "^chat_angry", "chatsad": "^chat_sad",
        "chatcon": "^chat_confused", "chatshock": "^chat_shock", "chatquiz": "^chat_quiz",
        "chatscared": "^chat_scared", "chatshifty": "^chat_shifty", "chatbored": "^chat_bored",
        "chatlaugh": "^chat_laugh", "chatdrunk": "^chat_drunk", "chatgoblin": "^chat_goblin"}
DIRS = {"north": (0, 1), "south": (0, -1), "east": (1, 0), "west": (-1, 0), "north-east": (1, 1),
        "north-west": (-1, 1), "south-east": (1, -1), "south-west": (-1, -1)}

ap = argparse.ArgumentParser()
ap.add_argument("transcript")
ap.add_argument("lo", type=int)
ap.add_argument("hi", type=int)
ap.add_argument("--origin", required=True, help="X,Z of the copied square's SW tile (template coords)")
ap.add_argument("--name", default="natt", help="proc prefix: tiles print as ~<name>_at(dx, dz, level)")
ap.add_argument("--ignore", default="", help="regex of recorded npc names to leave out (the ambient crowd)")
args = ap.parse_args()
ox, oz = (int(v) for v in args.origin.split(","))

COORD = r"\((\d+), (\d+), (\d+)\)"


def at(x, z, lvl):
    return f"~{args.name}_at({int(x) - ox}, {int(z) - oz}, {lvl})"


def sym(tab, i, kind):
    i = str(i)
    if i == "-1":
        return "null"
    return tab.get(i, f"{kind}_{i}")


# ---- pass 1: per-tick raw lines --------------------------------------------------------------------
ticks = {}
tick = None
for raw in open(args.transcript, encoding="utf-8", errors="replace"):
    m = re.match(r"^\[(\d+)\]$", raw.strip())
    if m and raw.startswith("["):
        tick = int(m.group(1))
        continue
    if tick is None or tick < args.lo or tick > args.hi:
        continue
    ticks.setdefault(tick, []).append(raw.rstrip("\n"))

# ---- pass 2: events ------------------------------------------------------------------------------
# Each event: (tick, order, kind, payload). Walk steps are kept apart and grouped into runs later.
events = []
steps = {}            # npc index -> [(tick, x, z, lvl, dir)]
names = {}            # npc index -> (239 symbol, recorded name)
pages = []            # (tick, kind, speaker, head_npc, mood, text)
clicks = []           # ticks of resume_pausebutton
order = 0


def ev(t, kind, text):
    global order
    order += 1
    events.append((t, order, kind, text))


def flush_page(t, page):
    if page.get("iface") or page.get("kind"):
        iface = page.get("iface", "")
        text = page.get("text", page.get("box1508", ""))
        mood = page.get("seq", "")
        base = MOOD.get(re.sub(r"\d+$", "", mood), "^chat_neutral") if mood else ""
        if iface == "229" and not text:
            ev(t, "raw", "~natt_blank_chatbox; // messagebox, no text, no continue")
        elif page.get("kind") == "mesbox" or iface == "193":
            if page.get("obj"):
                ev(t, "page", f"~objbox(obj_{page['obj']}, \"{text}\");")
            else:
                ev(t, "page", f"~mesbox(\"{text}\");")
        elif page.get("kind") == "dialogue":
            speaker, _, body = text.partition("|")
            if page.get("player") or iface == "217":
                ev(t, "page", f"~chatplayer_anim({base or '^chat_neutral'}, \"{body}\");")
            else:
                head = sym(NPC, page.get("head", ""), "npc") if page.get("head") else "npc_type"
                ev(t, "page", f"~chatnpc_specific_anim(\"{speaker}\", {head}, {base or '^chat_neutral'}, \"{body}\");")
        elif iface == "219":
            ev(t, "raw", "// chatmenu opened here (see the analysis doc for its options)")


for t in sorted(ticks):
    who = None        # ("npc", index) | ("player",)
    page = {}
    for line in ticks[t]:
        s = line.strip()
        if s.startswith("[resume_pausebutton]"):
            clicks.append(t)
            continue
        m = re.match(r"\[(add|del|idle|walk|run|teleport|crawl)\] npc=\(index=(\d+), (?:id=\d+, multinpc=)?(.*?) \(id=(\d+)\), coord=" + COORD + r"\)\)?(.*)", s)
        if m:
            kind, idx, rname, nid, x, z, lvl, rest = m.groups()
            if args.ignore and re.search(args.ignore, rname):
                who = None
                continue
            who = ("npc", idx)
            names[idx] = (sym(NPC, nid, "npc"), rname)
            if kind == "add":
                ang = re.search(r"spawnangle=([\w-]+)", rest)
                ev(t, "add", (idx, nid, rname, x, z, lvl, ang.group(1) if ang else ""))
            elif kind == "del":
                ev(t, "del", (idx,))
            elif kind in ("walk", "run", "crawl"):
                n = re.search(r"newcoord=" + COORD, rest)
                d = re.search(r"step=([\w-]+)", rest)
                steps.setdefault(idx, []).append((t, int(n.group(1)), int(n.group(2)), int(n.group(3)),
                                                  d.group(1) if d else ""))
            elif kind == "teleport":
                n = re.search(r"newcoord=" + COORD, rest)
                ev(t, "npc_tele", (idx, n.group(1), n.group(2), n.group(3)))
            continue
        m = re.match(r"\[(idle|walk|run|teleport)\] player=\(index=\d+, name=[^,]*, coord=" + COORD + r"\)(.*)", s)
        if m:
            who = ("player",)
            if m.group(1) == "teleport":
                n = re.search(r"newcoord=" + COORD, m.group(5))
                ev(t, "p_tele", (n.group(1), n.group(2), n.group(3)))
            continue
        if who:
            m = re.match(r"\[sequence\] id=(-?\d+)(?:, delay=(\d+))?", s)
            if m:
                ev(t, "seq", (who, m.group(1), m.group(2) or "0"))
                continue
            m = re.match(r"\[spotanim\] id=(-?\d+)(.*)", s)
            if m:
                h = re.search(r"height=(\d+)", m.group(2))
                d = re.search(r"delay=(\d+)", m.group(2))
                ev(t, "spot", (who, m.group(1), h.group(1) if h else "0", d.group(1) if d else "0"))
                continue
            m = re.match(r"\[face_coord\] coord=" + COORD, s)
            if m:
                ev(t, "face", (who,) + m.groups())
                continue
            m = re.match(r"\[face_angle\] angle=(\d+)", s)
            if m:
                ev(t, "angle", (who, m.group(1)))
                continue
            m = re.match(r"\[face_pathingentity\] (.*)", s)
            if m:
                ev(t, "faceent", (who, m.group(1)))
                continue
            m = re.match(r"\[say\] text=\"(.*)\"", s)
            if m:
                ev(t, "say", (who, m.group(1)))
                continue
            m = re.match(r"\[transformation\] id=(\d+)", s)
            if m:
                ev(t, "transform", (who, m.group(1)))
                continue
            m = re.match(r"\[exactmove\] (.*)", s)
            if m:
                ev(t, "exactmove", (who, m.group(1)))
                continue
        m = re.match(r"\[cam_(moveto|lookat)_v\d\] coord=" + COORD + r", height=(\d+), rate=(\d+), rate2=(\d+)", s)
        if m:
            kind, x, z, lvl, h, r1, r2 = m.groups()
            ev(t, "cam", f"cam_{kind}({at(x, z, lvl)}, {h}, {r1}, {r2});")
            continue
        m = re.match(r"\[cam_shake\] (.*)", s)
        if m:
            ev(t, "raw", f"// cam_shake {m.group(1)}")
            continue
        if s.startswith("[cam_reset]"):
            ev(t, "raw", "cam_reset;")
            continue
        m = re.match(r"\[loc_add_change_v\d\] id=(\d+), coord=" + COORD + r", shape=(\d+), rotation=(\d+)", s)
        if m:
            lid, x, z, lvl, shape, rot = m.groups()
            ev(t, "raw", f"loc_add({at(x, z, lvl)}, {sym(LOC, lid, 'loc')}, {rot}, {SHAPES.get(int(shape), shape)}, -1);")
            continue
        m = re.match(r"\[loc_del\] coord=" + COORD + r", shape=(\d+), rotation=(\d+)", s)
        if m:
            x, z, lvl, shape, rot = m.groups()
            ev(t, "raw", f"// loc_del {at(x, z, lvl)} shape {shape} rot {rot}")
            continue
        m = re.match(r"\[loc_anim\] id=(\d+), coord=" + COORD + r"(.*)", s)
        if m:
            ev(t, "raw", f"// loc_anim {sym(SEQ, m.group(1), 'seq')} at {at(m.group(2), m.group(3), m.group(4))}{m.group(5)}")
            continue
        m = re.match(r"\[map_anim\] id=(\d+), coord=" + COORD + r"(.*)", s)
        if m:
            h = re.search(r"height=(\d+)", m.group(5))
            d = re.search(r"delay=(\d+)", m.group(5))
            ev(t, "raw", f"spotanim_map({sym(SPOT, m.group(1), 'spotanim')}, {at(*m.group(2, 3, 4))}, {h.group(1) if h else 0}, {d.group(1) if d else 0});")
            continue
        m = re.match(r"\[sound_area\] id=(\d+)(.*)", s)
        if m:
            ev(t, "raw", f"// sound_area {sym(SYNTH, m.group(1), 'synth')}{m.group(2)}")
            continue
        m = re.match(r"\[synth_sound\] id=(\d+)(.*)", s)
        if m:
            lo = re.search(r"loops=(\d+)", m.group(2))
            d = re.search(r"delay=(\d+)", m.group(2))
            ev(t, "raw", f"sound_synth({sym(SYNTH, m.group(1), 'synth')}, {lo.group(1) if lo else 1}, {d.group(1) if d else 0});")
            continue
        m = re.match(r"\[midi_song_v\d\] id=(\d+)", s)
        if m:
            ev(t, "raw", f"midi_song({sym(MIDI, m.group(1), 'midi')});")
            continue
        m = re.match(r"\[midi_jingle\] id=(\d+)", s)
        if m:
            ev(t, "raw", f"midi_jingle({sym(JINGLE, m.group(1), 'jingle')});")
            continue
        m = re.match(r"\[runclientscript\] id=948, .*values=\[(.*)\]", s)
        if m:
            ev(t, "raw", f"~{args.name}_fade({m.group(1)});")
            continue
        m = re.match(r"\[varbit\] id=(\d+), old=(-?\d+), new=(-?\d+)", s)
        if m and m.group(1) not in ("12391", "12392", "8354", "10670", "24", "5934", "25", "12393", "4605"):
            ev(t, "raw", f"// varbit {m.group(1)}: {m.group(2)} -> {m.group(3)}")
            continue
        if re.match(r"\[(hidenpcops|hidelocops|hideobjops|minimap_toggle)\]", s):
            ev(t, "raw", f"// {s}")
            continue
        # dialogue chrome: collect one page per tick
        m = re.match(r"\[message_game\] type=(dialogue|mesbox), message=\"(.*)\"$", s)
        if m:
            if page.get("kind"):
                flush_page(t, page)
                page = {}
            page["kind"] = m.group(1)
            page["text"] = m.group(2)
            continue
        m = re.match(r"\[if_setnpchead\] com=[^,]*, id=(?:\w+ \()?(\d+)", s)
        if m:
            page["head"] = m.group(1)
            continue
        if s.startswith("[if_setplayerhead]"):
            page["player"] = True
            continue
        m = re.match(r"\[if_setanim\] com=\S*(?:231|217):\d+\)?, id=(?:(\w+) \()?(\d+)", s)
        if m:
            page["seq"] = m.group(1) or SEQ.get(m.group(2), m.group(2))
            continue
        m = re.match(r"\[if_setobject\] com=\S*193:\d+\)?, id=(\d+)", s)
        if m:
            page["obj"] = m.group(1)
            continue
        m = re.match(r"\[if_opensub\] com=\S*162:565\)?, id=(?:\w+ \()?(\d+)", s)
        if m:
            page["iface"] = m.group(1)
            continue
        m = re.match(r"\[runclientscript\] id=1508, types=\[string\], values=\[\"(.*)\"\]", s)
        if m:
            page["box1508"] = m.group(1)
            continue
    flush_page(t, page)

# ---- npc walks: one npc_walk per straight run ------------------------------------------------------
for idx, ss in steps.items():
    run = []
    for st in ss + [None]:
        if run and (st is None or st[4] != run[-1][4] or st[0] != run[-1][0] + 1):
            first, last = run[0], run[-1]
            ev(first[0], "walk", (idx, last[1], last[2], last[3], len(run), first[4]))
            run = []
        if st is not None:
            run.append(st)

# ---- emit ------------------------------------------------------------------------------------------
events.sort(key=lambda e: (e[0], e[1]))
seen = {}
out = []
now = None


def handle(idx):
    return f"$a{idx}"


def select(w):
    if w[0] == "player":
        return ""
    return f"npc_finduid({handle(w[1])}); "


click_iter = iter(sorted(clicks))
for t, _, kind, p in events:
    if now is None:
        now = t
    if t > now:
        out.append(f"p_delay({t - now - 1});")
        now = t
    out.append(f"// [{t}]")
    if kind == "page":
        out.append(p)
        # the page blocks until the click; the script resumes on the click tick
        nxt = next((c for c in sorted(clicks) if c > t), None)
        if nxt is not None:
            out.append(f"// (clicked at [{nxt}])")
            now = nxt
        continue
    if kind == "raw" or kind == "cam":
        out.append(p)
        continue
    if kind == "add":
        idx, nid, rname, x, z, lvl, ang = p
        s239 = sym(NPC, nid, "npc")
        note = "" if rname.lower().replace(" ", "_") in s239 else f"  // recorded \"{rname}\""
        decl = "def_npc_uid " if idx not in seen else ""
        seen[idx] = True
        out.append(f"npc_add({at(x, z, lvl)}, {s239}, ^natt_actor_ticks);{note} // spawnangle {ang}")
        out.append(f"{decl}{handle(idx)} = npc_uid;")
        continue
    if kind == "del":
        if p[0] not in seen:
            continue
        out.append(f"npc_finduid({handle(p[0])}); npc_del;")
        continue
    if kind == "walk":
        idx, x, z, lvl, n, d = p
        out.append(f"npc_finduid({handle(idx)}); npc_walk({at(x, z, lvl)}); // {n} step(s) {d}")
        continue
    if kind == "npc_tele":
        idx, x, z, lvl = p
        out.append(f"npc_finduid({handle(idx)}); npc_tele({at(x, z, lvl)});")
        continue
    if kind == "p_tele":
        x, z, lvl = p
        out.append(f"p_telejump({at(x, z, lvl)});")
        continue
    who = p[0]
    if kind == "seq":
        seq = sym(SEQ, p[1], "seq")
        out.append(f"{select(who)}{'npc_anim' if who[0] == 'npc' else 'anim'}({seq}, {p[2]});")
    elif kind == "spot":
        sp = sym(SPOT, p[1], "spotanim")
        out.append(f"{select(who)}{'spotanim_npc' if who[0] == 'npc' else 'spotanim_pl'}({sp}, {p[2]}, {p[3]});")
    elif kind == "face":
        out.append(f"{select(who)}{'npc_facesquare' if who[0] == 'npc' else 'facesquare'}({at(*p[1:4])});")
    elif kind == "angle":
        out.append(f"// {'npc' if who[0] == 'npc' else 'player'} face_angle {p[1]}")
    elif kind == "faceent":
        out.append(f"// {select(who)}face entity: {p[1][:120]}")
    elif kind == "say":
        out.append(f"{select(who)}{'npc_say' if who[0] == 'npc' else 'say'}(\"{p[1]}\");")
    elif kind == "transform":
        out.append(f"{select(who)}npc_changetype({sym(NPC, p[1], 'npc')}, ^natt_actor_ticks);")
    elif kind == "exactmove":
        out.append(f"// exactmove {p[1]}")

print("// generated by tools/rsprox/rsprox_cutscene.py " + " ".join(sys.argv[1:]))
print("// actors: " + ", ".join(f"{handle(i)}={n[0]}" for i, n in names.items() if i in seen))
print("\n".join(out))
