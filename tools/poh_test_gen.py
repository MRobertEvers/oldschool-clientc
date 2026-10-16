#!/usr/bin/env python3
"""Generate the per-room Construction scriptruns: test/raids/construction/<room>.lua.

Each file is tools/poh_test_template.lua with this room's data spliced in at
`--@DATA@`. The data is the content's own, never hand numbers:

  rooms      poh_room rows that carry an add-room button (configs/all.dbrow)
  hotspots   each [oploc5,<loc>] build handler in skill_construction/scripts,
             tied to the room by its poh_hotspot_runtime placements and to its
             poh_hotspot row by the names its body (and the procs it calls)
             mention -- restricted to that room's own poh_room:hotspot list
  tiers      every poh_hotspot:builddata row of that hotspot, in panel order:
             materials, levels, hidden / upgrade-source flags (all.dbrow),
             xp and built loc (poh_runtime_generated.dbrow)
  options    the built loc's op1..op4 (configs/all.loc)
  deep       tools/poh_test_deep.lua, spliced verbatim: what the furniture
             does (a teleport's landing, a tablet made, an item taken)

Usage: python3 tools/poh_test_gen.py [--check]
  --check   exit 1 if a generated file differs from what is on disk.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(ROOT, "OSRS-Content", "osrs239-content")
SCRIPTS = os.path.join(CONTENT, "server", "scripts", "skill_construction", "scripts")
RUNTIME = os.path.join(CONTENT, "server", "scripts", "skill_construction", "configs",
                       "poh_runtime_generated.dbrow")
TEMPLATE = os.path.join(ROOT, "tools", "poh_test_template.lua")
DEEP = os.path.join(ROOT, "tools", "poh_test_deep.lua")
OUT_DIR = os.path.join(ROOT, "test", "raids", "construction")

# The add-room button's own row; the a-variants are the same room with a
# staircase landing and are never offered by the panel.
SKIP_ROWS = {"poh_dummy_null", "poh_dummy_hall1a", "poh_dummy_hall2a"}


def read_sections(path, keep):
    """[name] -> list of (key, value); data=col,val becomes (col, val)."""
    out = {}
    name = None
    with open(path, encoding="cp1252", errors="replace") as handle:
        for raw in handle:
            line = raw.strip()
            match = re.match(r"^\[(.+)\]$", line)
            if match:
                name = match.group(1)
                out[name] = [] if keep(name) else None
                continue
            if name is None or out.get(name) is None or "=" not in line or line.startswith("//"):
                continue
            key, value = line.split("=", 1)
            if key == "data" and "," in value:
                key, value = value.split(",", 1)
            out[name].append((key, value))
    return {k: v for k, v in out.items() if v is not None}


def field(rows, name, key):
    return [v for k, v in rows.get(name, []) if k == key]


def lua(value):
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, str):
        return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'
    if isinstance(value, list):
        return "{ " + ", ".join(lua(v) for v in value) + " }"
    if isinstance(value, dict):
        return "{ " + ", ".join(
            ("%s = %s" if re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", k) else "[%s] = %s")
            % (k if re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", k) else lua(k), lua(v))
            for k, v in value.items()) + " }"
    raise TypeError(value)


def main():
    check = "--check" in sys.argv[1:]
    db = read_sections(os.path.join(CONTENT, "configs", "all.dbrow"), lambda n: True)
    runtime_rows = read_sections(RUNTIME, lambda n: True)
    locs = read_sections(os.path.join(CONTENT, "configs", "all.loc"),
                         lambda n: n.startswith("poh_") or "poh" in n)

    def table(name):
        t = field(db, name, "table")
        return t[0] if t else None

    furniture_runtime = {}
    hotspot_rooms = {}
    for name, kv in runtime_rows.items():
        cols = {}
        for k, v in kv:
            cols.setdefault(k, []).append(v)
        if "furniture" in cols:
            furniture_runtime[cols["furniture"][0]] = cols
        if "unbuilt_loc" in cols:
            rooms = []
            for placement in cols.get("placement", []):
                parts = placement.split(",")
                rooms.append((parts[0], int(parts[2]), int(parts[3]), int(parts[5])))
            hotspot_rooms[cols["unbuilt_loc"][0]] = rooms

    # Every script in the construction tree, by header.
    bodies = {}
    for fname in sorted(os.listdir(SCRIPTS)):
        if not fname.endswith(".rs2"):
            continue
        with open(os.path.join(SCRIPTS, fname), encoding="utf-8") as handle:
            text = handle.read()
        parts = re.split(r"^(\[[a-z0-9_]+,[A-Za-z0-9_]+\][^\n]*)$", text, flags=re.M)
        for i in range(1, len(parts), 2):
            header = re.match(r"\[([a-z0-9_]+),([A-Za-z0-9_]+)\]", parts[i])
            key = (header.group(1), header.group(2))
            bodies.setdefault(key, "")
            bodies[key] += parts[i] + "\n" + parts[i + 1]

    hotspot_names = {n for n in db if table(n) == "poh_hotspot"}

    def mentioned_hotspots(text, depth):
        found = [w for w in re.findall(r"\b[a-z0-9_]+\b", text) if w in hotspot_names]
        if depth > 0:
            for proc in re.findall(r"~([a-z0-9_]+)", text):
                found += mentioned_hotspots(bodies.get(("proc", proc), ""), depth - 1)
        return found

    def menu_tiers(text, depth):
        """A chat-menu picker reachable from `text`: [(row text, furniture)]."""
        for proc in re.findall(r"~([a-z0-9_]+)", text):
            body = bodies.get(("proc", proc), "")
            choice = re.search(r"~p_choice\d\((.*?)\);", body, flags=re.S)
            if choice:
                texts = dict((int(v), t) for t, v in re.findall(r'"([^"]+)",\s*(\d+)', choice.group(1)))
                rows = re.findall(r"if \(\$opt = (\d+)\) return\(([a-z0-9_]+)\);", body)
                picked = [(texts[int(k)], r) for k, r in rows
                          if int(k) in texts and table(r) == "furniture"]
                if picked:
                    return picked
            if depth > 0:
                found = menu_tiers(body, depth - 1)
                if found:
                    return found
        return []

    def ops_of(loc):
        # A multiloc parent shows its child: with the selecting var at 0,
        # multiloc1 (a jewellery box's plain form, an occult altar's
        # standard-spellbook form). Its menu is that child's.
        child = field(locs, loc, "multiloc1")
        if child and child[0] not in ("-1", "null", "default") and child[0] != loc:
            return ops_of(child[0])
        out = {}
        for i in range(1, 5):
            v = field(locs, loc, "op%d" % i)
            if v and v[0] not in ("default", "hidden", ""):
                out["op%d" % i] = v[0]
        return out

    rooms = []
    for name in db:
        if table(name) != "poh_room" or name in SKIP_ROWS or not field(db, name, "button"):
            continue
        level = field(db, name, "level_requirement")
        floor = field(db, name, "floor_restriction")
        rooms.append({
            "row": name,
            "button": field(db, name, "button")[0].split(":")[1],
            "name": field(db, name, "name_uppercase")[0],
            "cost": int(field(db, name, "cost")[0]),
            "level": int(level[0].split(",")[1]) if level else 1,
            "floor": int(floor[0]) if floor else -1,
            "hotspots": field(db, name, "hotspot"),
        })

    unmatched = []
    written = 0
    stale = []
    with open(TEMPLATE, encoding="utf-8") as handle:
        template = handle.read()
    with open(DEEP, encoding="utf-8") as handle:
        template = template.replace("--@DEEP@", handle.read().rstrip("\n"))
    os.makedirs(OUT_DIR, exist_ok=True)
    for room in rooms:
        slots = []
        needs_crest = False
        for (trigger, loc), body in sorted(bodies.items()):
            if trigger != "oploc5":
                continue
            placements = [p for p in hotspot_rooms.get(loc, []) if p[0] == room["row"]]
            if not placements:
                continue
            candidates = [h for h in mentioned_hotspots(body, 2) if h in room["hotspots"]]
            if not candidates and menu_tiers(body, 2):
                candidates = ["(menu)"]
            if not candidates:
                unmatched.append((room["row"], loc))
                continue
            hotspot = candidates[0]
            tiers = []
            menu = menu_tiers(body, 2)
            rows = [r for _, r in menu] if menu else field(db, hotspot, "builddata")
            menu_text = dict((r, t) for t, r in menu)
            for index, furniture in enumerate(rows):
                if table(furniture) != "furniture":
                    continue
                rt = furniture_runtime.get(furniture, {})
                built = rt.get("built_loc", [""])[0]
                variants = [b for b in rt.get("built_loc", []) if b]
                mats = []
                for v in field(db, furniture, "material_cost"):
                    obj, qty = v.split(",")
                    mats.append([("nails" if obj == "any_nails" else obj), int(qty)])
                levels = []
                for v in field(db, furniture, "level_requirement"):
                    stat, lvl = v.split(",")
                    levels.append([stat, int(lvl)])
                tier = {
                    "row": index + 1,
                    "furniture": furniture,
                    "name": (field(db, furniture, "name") or [furniture])[0],
                    "mats": mats,
                    "levels": levels,
                    "xp10": int(rt.get("experience", ["0"])[0]),
                    "built": built,
                    "ops": ops_of(built) if built else {},
                }
                # Every built form the runtime knows (open/closed, per-crest,
                # lit/unlit): the piece standing after a build is one of them.
                if len(variants) > 1:
                    tier["variants"] = {v: ops_of(v) for v in variants}
                if furniture in menu_text:
                    tier["menu"] = menu_text[furniture]
                if field(db, furniture, "hidden_in_build_menu") == ["1"]:
                    tier["hidden"] = True
                rel = field(db, furniture, "upgrade_source_relative")
                if rel and index + 1 - int(rel[0]) >= 1:
                    tier["upgrade_from"] = index + 1 - int(rel[0])
                if field(db, furniture, "upgrade_source_absolute"):
                    tier["upgrade_absolute"] = True
                tiers.append(tier)
            if "family_crest" in body:
                needs_crest = True
            first = placements[0]
            slots.append({
                "hotspot": loc,
                "row": hotspot,
                "at": [first[1], first[2]],
                "angle": first[3],
                "tiers": tiers,
            })
        data = ["local ROOM = " + lua({k: room[k] for k in ("row", "button", "name", "cost", "level", "floor")}),
                "local SLOTS = {"]
        for slot in slots:
            data.append("    { hotspot = %s, row = %s, at = %s, angle = %d, tiers = {"
                        % (lua(slot["hotspot"]), lua(slot["row"]), lua(slot["at"]), slot["angle"]))
            for tier in slot["tiers"]:
                data.append("        " + lua(tier) + ",")
            data.append("    } },")
        data.append("}")
        data.append("local NEEDS_CREST = " + lua(needs_crest))
        text = template.replace("--@DATA@", "\n".join(data)).replace("@ROOM@", room["button"])
        path = os.path.join(OUT_DIR, room["button"] + ".lua")
        old = open(path, encoding="utf-8").read() if os.path.exists(path) else None
        if old != text:
            stale.append(path)
            if not check:
                with open(path, "w", encoding="utf-8") as handle:
                    handle.write(text)
        written += 1
        print("%-22s %2d hotspot(s), %3d tier(s)" % (
            room["button"], len(slots), sum(len(s["tiers"]) for s in slots)))
    for row, loc in unmatched:
        print("unmatched: %s %s (no poh_hotspot row of that room in its handler)" % (row, loc),
              file=sys.stderr)
    if check and stale:
        print("stale: " + ", ".join(os.path.relpath(p, ROOT) for p in stale), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
