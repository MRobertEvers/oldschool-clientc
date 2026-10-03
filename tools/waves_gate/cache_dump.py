#!/usr/bin/env python3
"""Dump every rev-239 cache record a wave minigame ships, quoted for citation.

    tools/waves_gate/cache_dump.py --game inferno \\
        --content OSRS-Content/osrs239-content \\
        --out docs/minigames/inferno/sources \\
        --index build/inventory_state/inferno.cache_index.json

Follows tools/toa_cache_dump.py (one file per kind, the cache's own symbol and
the whole decoded record), with three changes the waves loop needs:

  * every record carries the file and line it was quoted from
    (`configs/all.npc:210806`), and each dump line is stable, so a spec row can
    cite `sources/cache_npc.txt:<line>`;
  * selection is by four rules, each stated in LEDGER_cache.md and recorded per
    record as `why=`: by name, by binding (a record of an already selected
    record names it), by map (the cache map places it in the minigame's square
    or the entrance area), and by reference (the minigame's own server scripts
    name it, whether or not it looks like the minigame's);
  * sequences are summarised: frame count, per-frame lengths (run-length
    encoded), total length in client cycles (20 ms) and game ticks (30 cycles),
    and every frame sound with its name.

The --index JSON is the machine-readable form of the same selection (kind, id,
symbol, dump line, source line, bindings), read by the inventory builder.

The minigame is a table at the bottom (GAMES). The Fortis Colosseum reuses the
tool by adding its own entry; nothing above the table is Inferno-specific.

    tools/waves_gate/cache_dump.py --game colosseum \\
        --content OSRS-Content/osrs239-content \\
        --out docs/minigames/colosseum/sources \\
        --index build/inventory_state/colosseum.cache_index.json

The Colosseum entry turns on rules the Inferno's does not use (each one a key
of its GAMES entry, so the Inferno's dumps stay byte-identical): varcs and
inventories (`extra_kinds`), an id block (`id_blocks`), the cache's own sound
browser (`synth_menus`), enum/struct/param data binding (`data_bind`), the
clientscripts that read each var, interface, enum and inv (`cs2_scan`),
skeletal sequence lengths (`maya_lengths`), a loc's random sounds
(`loc_soundrandom`) and a whole-square map listing, cache_map.txt (`map_dump`).
"""
import argparse
import json
import pathlib
import re
import sys
from collections import defaultdict, OrderedDict

# ---------------------------------------------------------------------------
# Readers. Each returns line numbers so every record can be cited.
# ---------------------------------------------------------------------------


def read_text(p):
    return p.read_text(encoding="cp1252", errors="replace").splitlines()


def compack(content, kind):
    """id -> symbol and symbol -> id, from configs/all.<kind>.compack."""
    by_id, by_name = {}, {}
    p = content / "configs" / f"all.{kind}.compack"
    if not p.exists():
        return by_id, by_name
    for line in read_text(p):
        i, sep, name = line.partition("=")
        if not sep:
            continue
        try:
            i = int(i)
        except ValueError:
            continue
        name = name.strip()
        by_id[i] = name
        by_name.setdefault(name, i)
    return by_id, by_name


def records(content, kind):
    """symbol -> (header line number, [(line number, text), ...])."""
    p = content / "configs" / f"all.{kind}"
    out = {}
    if not p.exists():
        return out
    cur, start, buf = None, 0, []
    for n, line in enumerate(read_text(p), 1):
        m = re.match(r"^\[([^\]]+)\]\s*$", line)
        if m:
            if cur is not None:
                out[cur] = (start, buf)
            cur, start, buf = m.group(1), n, []
        elif cur is not None and line.strip() and not line.startswith("//"):
            buf.append((n, line))
    if cur is not None:
        out[cur] = (start, buf)
    return out


def pack(content, name):
    """id -> (symbol, pack line) from pack/<name>.pack (`id=symbol ...`)."""
    out = {}
    p = content / "pack" / f"{name}.pack"
    if not p.exists():
        return out
    for n, line in enumerate(read_text(p), 1):
        i, sep, rest = line.partition("=")
        if not sep:
            continue
        try:
            out[int(i)] = (rest.split()[0] if rest.split() else "", n)
        except ValueError:
            pass
    return out


def jl2(content, square):
    """[(level, x, z, loc id, shape, angle, line)] from maps/m<square>.jl2."""
    p = content / "maps" / f"m{square}.jl2"
    out = []
    if not p.exists():
        return out
    section = None
    for n, line in enumerate(read_text(p), 1):
        if line.startswith("===="):
            section = line.strip("= ").strip()
            continue
        if section != "LOC":
            continue
        m = re.match(r"^(\d+) (\d+) (\d+): (\d+) (\d+)(?: (\d+))?", line)
        if m:
            lv, x, z, loc, shape, ang = m.groups()
            out.append((int(lv), int(x), int(z), int(loc), int(shape),
                        int(ang or 0), n))
    return out


def fields(rec):
    """[(line, key, value)] for a record body."""
    out = []
    for n, line in rec[1]:
        k, sep, v = line.partition("=")
        if sep:
            out.append((n, k.strip(), v.strip()))
    return out


# ---------------------------------------------------------------------------
# Selection
# ---------------------------------------------------------------------------


class Selection:
    """kind -> id -> OrderedDict(why -> [detail, ...])."""

    def __init__(self):
        self.sel = defaultdict(OrderedDict)

    def add(self, kind, i, why, detail=""):
        d = self.sel[kind].setdefault(i, OrderedDict())
        lst = d.setdefault(why, [])
        if detail and detail not in lst:
            lst.append(detail)
        return d

    def has(self, kind, i):
        return i in self.sel[kind]


def strip_tags(s):
    return re.sub(r"<[^>]+>", "", s)


def name_match(game, sym):
    if not sym or game["exclude"].search(sym):
        return False
    toks = sym.lower().split("_")
    if any(t in game["name_tokens"] for t in toks):
        return True
    if any(game["jal_token"].match(t) for t in toks):
        return True
    return bool(game["name_substr"].search(sym))


def code_lines(path):
    """[(line, text)] of non-comment code (// and /* */ removed)."""
    out, in_block = [], False
    for n, line in enumerate(read_text(path), 1):
        s = line
        if in_block:
            if "*/" in s:
                s = s.split("*/", 1)[1]
                in_block = False
            else:
                continue
        while "/*" in s:
            a, b = s.split("/*", 1)
            if "*/" in b:
                s = a + b.split("*/", 1)[1]
            else:
                s = a
                in_block = True
        s = re.sub(r'"(?:[^"\\]|\\.)*"', '""', s)   # string literals
        s = s.split("//", 1)[0]
        if s.strip():
            out.append((n, s))
    return out


# context -> kind, for a token that names more than one kind of record
CONTEXT_KIND = [
    (r"\[oploc\d,\s*$|loc_(?:find|change|add|del)\([^)]*$|\bloc \$\w+\s*=\s*$", "loc"),
    (r"(?:npc_anim|loc_anim|\banim|seq \$\w+\s*=|_anim,)\s*\(?\s*$", "seq"),
    (r"spotanim_(?:npc|pl|map)\(\s*$|projectile\([^)]*$|projanim_\w+\([^)]*$|spotanim \$\w+\s*=\s*$", "spotanim"),
    (r"sound_synth\(\s*$", "sound"),
    (r"\[(?:ai|op)\w*,\s*$|npc_(?:add|find|type)[^)]*$|npc \$\w+\s*=\s*$", "npc"),
    (r"inv_\w+\([^)]*$|obj_add\([^)]*$", "obj"),
]


# param type letter (configs/all.param `type=`) -> record kind
PARAM_KIND = {"g": "enum", "J": "struct", "o": "obj", "O": "obj", "A": "seq",
              "t": "spotanim", "n": "npc", "l": "loc", "P": "sound"}
# the output type an enum() call names -> record kind
CS2_KIND = {"struct": "struct", "obj": "obj", "namedobj": "obj", "enum": "enum",
            "npc": "npc", "seq": "seq", "loc": "loc", "spotanim": "spotanim",
            "synth": "sound"}


def param_types(content):
    out, cur = {}, None
    for line in read_text(content / "configs" / "all.param"):
        m = re.match(r"^\[([^\]]+)\]", line)
        if m:
            cur = m.group(1)
        elif cur and line.startswith("type="):
            out[cur] = line[5:].strip()
    return out


def data_bind(game, content, S, ids, names, recs, cscripts, bound_structs, enum_types):
    """Rule 2b (LEDGER_cache.md). Enums carry no value type in this cache
    (configs/all.enum is lossy), so an enum's values are followed only when a
    clientscript reads them with a typed enum() call, either directly
    (`enum(int, struct, enum_5312, $i)`) or through the struct param the enum
    was loaded from (`$e = struct_param($s, param_690)` ... `enum(int,
    namedobj, $e, $i)`). A struct's params are followed by the param's
    declared type (configs/all.param). Returns the CS2 suite's lines."""
    ptype = param_types(content)
    texts, by_param, types = {}, defaultdict(set), defaultdict(set)
    for i, (sym, n) in cscripts.items():
        p = content / "scripts" / f"{sym}.cs2"
        if not p.exists():
            continue
        lines = read_text(p)
        texts[sym] = lines
        var_param = {}
        for t in lines:
            for m in re.finditer(r"(\$\w+)\s*=\s*struct_param\(.*?,\s*(param_\d+)\)", t):
                var_param[m.group(1)] = m.group(2)
            for m in re.finditer(r"\benum\(\s*\w+\s*,\s*(\w+)\s*,\s*(enum_\d+|\$\w+)", t):
                if m.group(2).startswith("enum_"):
                    types[int(m.group(2)[5:])].add(m.group(1))
                elif m.group(2) in var_param:
                    by_param[var_param[m.group(2)]].add(m.group(1))

    # seeds
    sel_varps = {ids["varp"][i] for i in S.sel["varp"]}
    for sym, rec in recs["varbit"].items():
        for n, k, v in fields(rec):
            if k == "basevar" and v in sel_varps and sym in names["varbit"]:
                S.add("varbit", names["varbit"][sym], "data", f"basevar {v} (configs/all.varbit:{n})")
    for sym, rec in recs["struct"].items():
        params = {v.split(",")[0]: v.split(",", 2)[-1] for _, k, v in fields(rec) if k == "param"}
        why = game["struct_select"](params)
        if why:
            bound_structs[sym] = why
    npc_ids = set(S.sel["npc"])
    for sym, rec in recs["enum"].items():
        vals = [v.split(",", 1)[1] for _, k, v in fields(rec) if k == "val"]
        if len(vals) >= 2 and all(x.lstrip("-").isdigit() and int(x) in npc_ids for x in vals) \
                and sym in names["enum"]:
            S.add("enum", names["enum"][sym], "data",
                  f"every value ({len(vals)}) is a selected npc id")
    for i, (sym, n) in cscripts.items():
        if not name_match(game, sym) or sym not in texts:
            continue
        for ln, t in enumerate(texts[sym], 1):
            for m in re.finditer(r"\b(enum|struct)_(\d+)\b", t):
                if m.group(1) == "enum" and int(m.group(2)) in ids["enum"]:
                    S.add("enum", int(m.group(2)), "data", f"named in scripts/{sym}.cs2:{ln}")
                elif m.group(1) == "struct" and m.group(0) in recs["struct"]:
                    bound_structs.setdefault(m.group(0), f"named in scripts/{sym}.cs2:{ln}")

    def take(kind, val, why):
        if kind == "struct":
            s2 = f"struct_{val}"
            if s2 in recs["struct"] and s2 not in bound_structs:
                bound_structs[s2] = why
                return True
            return False
        if val not in ids.get(kind, {}):
            return False
        new = not S.has(kind, val)
        S.add(kind, val, "data", why)
        return new

    changed = True
    while changed:
        changed = False
        for sym in list(bound_structs):
            for n, k, v in fields(recs["struct"][sym]):
                if k != "param":
                    continue
                pname, _, val = v.split(",", 2)
                kind = PARAM_KIND.get(ptype.get(pname, ""))
                if not kind or not val.lstrip("-").isdigit() or int(val) < 0 \
                        or pname in game.get("struct_param_skip", ()):
                    continue
                if kind == "enum" and pname in by_param:
                    types[int(val)].update(by_param[pname])
                changed |= take(kind, int(val), f"{pname} of {sym} (configs/all.struct:{n})")
        for i in list(S.sel["enum"]):
            outs = {CS2_KIND.get(t) for t in types.get(i, ())} - {None}
            if len(outs) != 1 or ids["enum"].get(i) not in recs["enum"]:
                continue
            kind = outs.pop()
            for n, k, v in fields(recs["enum"][ids["enum"][i]]):
                val = v.split(",", 1)[-1]
                if k != "val" or not val.lstrip("-").isdigit() or int(val) < 0:
                    continue
                changed |= take(kind, int(val), f"value of enum_{i} read as "
                                f"{'/'.join(sorted(types[i]))} (configs/all.enum:{n})")
    enum_types.update({i: sorted(t) for i, t in types.items()})
    return texts


def run(game_name, content, out, index_path):
    game = GAMES[game_name]
    kinds = ["npc", "seq", "spotanim", "loc", "obj", "varp", "varbit",
             "enum", "dbrow", "struct"] + game.get("extra_kinds", [])
    ids, names, recs = {}, {}, {}
    for k in kinds:
        ids[k], names[k] = compack(content, k)
        recs[k] = records(content, k)
    sounds = pack(content, "4_soundeffects")
    ifaces = pack(content, "3_interfaces")
    songs = pack(content, "6_musictracks")
    jingles = pack(content, "11_musicjingles")
    cscripts = pack(content, "12_clientscripts")
    names["sound"] = {v[0]: k for k, v in sounds.items()}
    names["interface"] = {v[0]: k for k, v in ifaces.items()}
    ids["sound"] = {k: v[0] for k, v in sounds.items()}
    ids["interface"] = {k: v[0] for k, v in ifaces.items()}

    S = Selection()

    # ---- rule 1: by name ------------------------------------------------
    for k in ["npc", "seq", "spotanim", "loc", "obj", "varp", "varbit",
              "enum", "dbrow"] + game.get("extra_kinds", []):
        for i, sym in ids[k].items():
            if name_match(game, sym):
                S.add(k, i, "name", sym)
    for i, (sym, _) in sounds.items():
        if name_match(game, sym):
            S.add("sound", i, "name", sym)
    for i, (sym, _) in ifaces.items():
        if name_match(game, sym):
            S.add("interface", i, "name", sym)
    # npc display names (the record's name=), for records whose symbol hides it
    for sym, rec in recs["npc"].items():
        if game["exclude"].search(sym) or game["npc_name_exclude"].search(sym):
            continue
        for n, key, v in fields(rec):
            if key == "name" and strip_tags(v) in game["npc_names"]:
                S.add("npc", names["npc"][sym], "name", f"name={strip_tags(v)}")
    # asset families joined by one Jagex name on both sides
    for k, rx in game["families"]:
        pool = sounds if k == "sound" else {i: (s, 0) for i, s in ids[k].items()}
        for i, (sym, _) in pool.items():
            if re.match(rx, sym):
                S.add(k, i, "family", rx)
    for k, i, why in game["named_ids"]:
        S.add(k, i, "doc", why)
    # a contiguous id block whose other records the name rule already chose
    for k, lo, hi, why in game.get("id_blocks", []):
        for i in range(lo, hi + 1):
            if i in ids[k] and not S.has(k, i):
                S.add(k, i, "block", why)
    # the cache's own sound browser: a `synth` dbrow menu lists sounds
    # (column 2, `name,id`) and sub-menus (column 1, dbrow ids)
    synth_menu_of = {}   # sound id -> menu symbol
    for top in game.get("synth_menus", []):
        todo = [(top, top)]
        while todo:
            sym, path = todo.pop(0)
            rec = recs["dbrow"].get(sym)
            if not rec:
                continue
            S.add("dbrow", names["dbrow"][sym], "synth", path)
            for n, key, v in fields(rec):
                m = re.match(r"1:\d+:(\d+)$", v) if key == "values" else None
                if m and int(m.group(1)) in ids["dbrow"]:
                    sub = ids["dbrow"][int(m.group(1))]
                    todo.append((sub, f"{path} > {sub}"))
                m = re.match(r"2:\d+:[^,]*,(\d+)$", v) if key == "values" else None
                if m:
                    sid = int(m.group(1))
                    synth_menu_of.setdefault(sid, sym)
                    S.add("sound", sid, "synth", f"{sym} (configs/all.dbrow:{n})")

    # ---- rule 3: by map -------------------------------------------------
    placements = defaultdict(list)
    for square, box, label in game["map"]:
        for lv, x, z, loc, shape, ang, n in jl2(content, square):
            if box and not (box[0] <= x <= box[2] and box[1] <= z <= box[3]):
                continue
            placements[loc].append((square, lv, x, z, shape, ang, n, label))
            S.add("loc", loc, "map", f"{label} m{square}")

    # ---- rule 4: by reference from the minigame's own content -----------
    refs = defaultdict(list)   # (kind, id) -> [file:line]
    files = []
    if game["content_dir"]:
        root = content / game["content_dir"]
        files = sorted(root.rglob("*"))
        files = [f for f in files if f.is_file()]
    for extra in game.get("extra_ref_files", []):
        files.extend(sorted(content.glob(extra)))
    kind_names = {k: names[k] for k in ["npc", "seq", "spotanim", "loc",
                                        "obj", "varp", "varbit", "enum",
                                        "dbrow", "sound", "interface"]}
    for f in files:
        rel = f.relative_to(content)
        for n, s in code_lines(f):
            for m in re.finditer(r"[A-Za-z_][A-Za-z0-9_]*", s):
                tok = m.group(0)
                if m.start() and s[m.start() - 1] in "$^~@":
                    continue   # a local, a constant, a proc or a label: never an asset
                hits = [k for k, nm in kind_names.items() if tok in nm]
                if not hits:
                    continue
                if len(hits) > 1:
                    pre = s[:m.start()]
                    pick = [k for rx, k in CONTEXT_KIND if k in hits and re.search(rx, pre)]
                    if pick:
                        hits = pick[:1]
                for k in hits:
                    i = kind_names[k][tok]
                    refs[(k, i)].append(f"{rel}:{n}")
            for m in re.finditer(r"param=(attack|defend|death)_sound,(\d+)", s):
                refs[("sound", int(m.group(2)))].append(f"{rel}:{n}")
    for (k, i), where in refs.items():
        if ids[k].get(i) in game.get("ref_exclude", ()):
            continue
        S.add(k, i, "ref", where[0] + (f" (+{len(where)-1})" if len(where) > 1 else ""))
    for k, i, why in game["ref_constants"]:
        S.add(k, i, "ref", why)

    # ---- rule 2b: data (enums, structs, params), games that ask for it ---
    bound_structs = OrderedDict()   # struct symbol -> why
    enum_types = {}                 # enum id -> output type read by a clientscript
    cs_texts = {}                   # script symbol -> lines (the whole CS2 suite)
    if game.get("data_bind"):
        cs_texts = data_bind(game, content, S, ids, names, recs, cscripts,
                             bound_structs, enum_types)

    # ---- rule 2: by binding, to a fixed point ---------------------------
    bindings = defaultdict(list)   # (kind, id) -> [(field, kind, id, line)]

    def bind(src_kind, src_id, field, dst_kind, dst_sym_or_id, line, select=True):
        if isinstance(dst_sym_or_id, str):
            if dst_sym_or_id in ("-1", "null", ""):
                return False
            dst = names.get(dst_kind, {}).get(dst_sym_or_id)
        else:
            dst = dst_sym_or_id
        if dst is None:
            return False
        bindings[(src_kind, src_id)].append((field, dst_kind, dst, line))
        if not select:
            if S.has(dst_kind, dst):
                S.add(dst_kind, dst, "bind", f"{field} of {src_kind} {ids[src_kind].get(src_id, src_id)}")
            return False
        new = not S.has(dst_kind, dst)
        S.add(dst_kind, dst, "bind", f"{field} of {src_kind} {ids[src_kind].get(src_id, src_id)}")
        return new

    changed = True
    done = set()
    while changed:
        changed = False
        for k in ["npc", "spotanim", "loc", "seq", "varbit"]:
            for i in list(S.sel[k].keys()):
                if (k, i) in done:
                    continue
                done.add((k, i))
                sym = ids[k].get(i)
                rec = recs[k].get(sym)
                if not rec:
                    continue
                for n, key, v in fields(rec):
                    if k == "npc" and "anim" in key:
                        changed |= bind(k, i, key, "seq", v, n)
                    elif k == "npc" and key.startswith("multinpc"):
                        changed |= bind(k, i, key, "npc", v, n)
                    elif k == "npc" and key in ("multivarbit", "multivarp"):
                        changed |= bind(k, i, key, "varbit" if key == "multivarbit" else "varp", v, n)
                    elif k in ("spotanim", "loc") and key == "anim":
                        changed |= bind(k, i, key, "seq", v, n)
                    elif k == "loc" and key.startswith("multiloc"):
                        changed |= bind(k, i, key, "loc", v, n)
                    elif k == "loc" and key in ("multivarbit", "multivarp"):
                        changed |= bind(k, i, key, "varbit" if key == "multivarbit" else "varp", v, n)
                    elif k == "loc" and re.match(r"soundrandom\d+$", key) and game.get("loc_soundrandom"):
                        changed |= bind(k, i, key, "sound", int(v), n)
                    elif k == "loc" and key == "soundid":
                        try:
                            changed |= bind(k, i, key, "sound", int(v), n)
                        except ValueError:
                            changed |= bind(k, i, key, "sound", v, n)
                    elif k == "seq" and key == "sound":
                        sid = int(v.split(",")[1])
                        changed |= bind(k, i, "frame sound", "sound", sid, n)
                    elif k == "varbit" and key == "basevar":
                        # recorded, not selected: a shared carrier varp (the
                        # pet menagerie, the collection log, the CA bitfields)
                        # is not the minigame's asset; one named for it is
                        # already selected by name
                        bind(k, i, key, "varp", v, n, select=False)

    # ---- the non-config kinds -------------------------------------------
    # clientscripts: name + the numeric references the content makes
    cs_sel = OrderedDict()
    for i, (sym, n) in cscripts.items():
        if name_match(game, sym):
            cs_sel[i] = ["name"]
    for i, why in game["clientscripts"]:
        cs_sel.setdefault(i, []).append(why)

    # clientscripts that read a selected var, interface, enum or inv (games
    # that ask for it): the whole CS2 suite is scanned for the literal
    read_by = defaultdict(list)    # (kind, id) -> [(script, line, text)]
    script_hits = defaultdict(set)  # script symbol -> matched lines
    iface_hooks = defaultdict(list)  # interface id -> [(line, event, script id)]
    if game.get("cs2_scan"):
        tok = {}
        for k in ("varp", "varbit", "varc"):
            for i in S.sel.get(k, {}):
                tok[f"%{ids[k][i]}"] = (k, i)
        for k in ("interface", "enum", "inv"):
            for i in S.sel.get(k, {}):
                tok[f"{k}_{i}"] = (k, i)
        cs_id = {sym: i for i, (sym, n) in cscripts.items()}
        rx = re.compile(r"%[A-Za-z0-9_]+|\b(?:interface|enum|inv)_\d+\b")
        for sym in sorted(cs_texts, key=lambda s: cs_id[s]):
            for ln, t in enumerate(cs_texts[sym], 1):
                for m in rx.finditer(t):
                    if m.group(0) in tok:
                        read_by[tok[m.group(0)]].append((sym, ln, t.strip()))
                        script_hits[sym].add(ln)
        for (k, i), hits in read_by.items():
            if k == "enum":
                continue   # a shared table: its readers are listed, not selected
            for sym, ln, t in hits:
                w = f"reads {k} {ids[k].get(i, i)}"
                lst = cs_sel.setdefault(cs_id[sym], [])
                if w not in lst:
                    lst.append(w)
        cs_sel = OrderedDict(sorted(cs_sel.items()))
        for i in S.sel["interface"]:
            p = content / "interfaces" / f"{ids['interface'][i]}.if"
            if p.exists():
                for ln, t in enumerate(read_text(p), 1):
                    m = re.match(r"^(on\w+)=i:(\d+)", t)
                    if m:
                        iface_hooks[i].append((ln, m.group(1), int(m.group(2))))

    def read_by_lines(kind, i):
        rb = read_by.get((kind, i), [])
        body = [f"  read_by: {s}  (scripts/{s}.cs2:{ln}): {t[:160]}" for s, ln, t in rb[:12]]
        if len(rb) > 12:
            body.append(f"  read_by: ... {len(rb) - 12} more lines in "
                        f"{len({s for s, _, _ in rb[12:]})} scripts")
        if game.get("cs2_scan") and kind in ("varp", "varbit", "varc", "interface", "enum", "inv") \
                and not rb:
            body.append("  read_by: no clientscript names it (scripts/*.cs2 scanned whole)")
        return body

    # ---- write ------------------------------------------------------------
    out.mkdir(parents=True, exist_ok=True)
    index = {"game": game_name, "records": []}

    def compress(details):
        """`multiloc1 of loc X, multiloc2 of loc X, ...` -> `multiloc1-2 (values 0-1) of loc X`."""
        out, groups = [], OrderedDict()
        for d in details:
            m = re.match(r"(multiloc|multinpc)(\d+) of (\w+) (\S+)$", d)
            if m:
                groups.setdefault((m.group(1), m.group(3), m.group(4)), []).append(int(m.group(2)))
            else:
                out.append(d)
        for (f, kind, sym), ns in groups.items():
            ns.sort()
            runs, a, b = [], ns[0], ns[0]
            for n in ns[1:] + [None]:
                if n is not None and n == b + 1:
                    b = n
                    continue
                runs.append(f"{a}" if a == b else f"{a}-{b}")
                if n is not None:
                    a = b = n
            vals = ",".join(r if "-" not in r else "-".join(str(int(x) - 1) for x in r.split("-"))
                            for r in runs)
            vals = ",".join(str(int(r) - 1) if r.isdigit() else r for r in vals.split(","))
            out.append(f"{f}{','.join(runs)} (value {vals}) of {kind} {sym}")
        return out

    def why_text(k, i):
        return "; ".join(f"{w}: {', '.join(compress(d))}" if d else w
                         for w, d in S.sel[k][i].items())

    def emit_config(kind, fname, title, summarise=None, src_file=None):
        lines = [f"# {title} -- {len(S.sel[kind])} records selected from cache.osrs239",
                 f"# source: OSRS-Content/osrs239-content/{src_file or 'configs/all.' + kind}"
                 " (record header line given per record)",
                 "# why= the selection rule(s) that chose the record (LEDGER_cache.md)", ""]
        for i in sorted(S.sel[kind]):
            sym = ids[kind].get(i, f"{kind}_{i}")
            rec = recs[kind].get(sym)
            src = f"configs/all.{kind}:{rec[0]}" if rec else "(no record body)"
            head = len(lines) + 1
            lines.append(f"[{i}] {sym}    ({src})")
            lines.append(f"  why= {why_text(kind, i)}")
            entry = {"kind": kind, "id": i, "symbol": sym, "line": head,
                     "source": src, "why": {w: d for w, d in S.sel[kind][i].items()},
                     "bindings": [(f, dk, di, ln) for f, dk, di, ln in bindings.get((kind, i), [])]}
            if rec:
                body = summarise(rec, entry) if summarise else \
                    [f"  {v}" for _, v in rec[1]]
                for b in body:
                    lines.append(b)
                entry["fields"] = [(n, kk, vv) for n, kk, vv in fields(rec)
                                   if not (kind == "seq" and kk == "frame")]
            elif summarise:
                summarise(None, entry)
            rb = read_by_lines(kind, i)
            if rb:
                entry["read_by"] = [(s, ln) for s, ln, _ in read_by.get((kind, i), [])]
                lines.extend(rb)
            lines.append("")
            index["records"].append(entry)
        (out / fname).write_text("\n".join(lines) + "\n", encoding="utf-8")
        return len(S.sel[kind])

    def seq_summary(rec, entry):
        if rec is None:
            entry["frames"], entry["cycles"], entry["frame_sounds"] = 0, 0, []
            return []
        frames, sounds_l, other = [], [], []
        for n, k, v in fields(rec):
            if k == "frame":
                parts = v.split(",")
                frames.append(int(parts[1]) if len(parts) > 1 else 0)
            elif k == "sound":
                p = v.split(",")
                sid = int(p[1])
                sounds_l.append((n, int(p[0]), sid, sounds.get(sid, ("?", 0))[0], v))
            else:
                other.append((n, k, v))
        out_l = []
        total = sum(frames)
        rle, prev, cnt = [], None, 0
        for f in frames + [None]:
            if f == prev:
                cnt += 1
                continue
            if prev is not None:
                rle.append(f"{prev}x{cnt}" if cnt > 1 else f"{prev}")
            prev, cnt = f, 1
        if frames:
            out_l.append(f"  frames={len(frames)}  lengths={' '.join(rle)}")
            out_l.append(f"  total={total} client cycles = {total * 20} ms = {total / 30:.2f} game ticks")
        else:
            out_l.append("  frames=0 (no frame list: skeletal or empty; length from maya fields if any)")
            mr = [v for _, k, v in other if k == "mayarange"]
            if game.get("maya_lengths") and mr:
                a, b = (int(x) for x in mr[0].split(",")[:2])
                total = b - a
                out_l.append(f"  maya length: mayarange {a}..{b} = {total} client cycles = "
                             f"{total * 20} ms = {total / 30:.2f} game ticks "
                             "(one skeletal keyframe per 20 ms client cycle)")
                entry["maya"] = True
        entry["frames"] = len(frames)
        entry["cycles"] = total
        entry["frame_sounds"] = [(fr, sid, nm, n) for n, fr, sid, nm, _ in sounds_l]
        for n, fr, sid, nm, v in sounds_l:
            out_l.append(f"  sound={v}    -> frame {fr} plays sound {sid} {nm}  (configs/all.seq:{n})")
        if not sounds_l:
            out_l.append("  (no frame sound)")
        for n, k, v in other:
            out_l.append(f"  {k}={v}")
        return out_l

    counts = OrderedDict()
    counts["npc"] = emit_config("npc", "cache_npc.txt", "npc records")
    counts["seq"] = emit_config("seq", "cache_seq.txt", "sequences", seq_summary)
    counts["spotanim"] = emit_config("spotanim", "cache_spotanim.txt",
                                     "spotanims (graphics and projectiles)")
    counts["obj"] = emit_config("obj", "cache_objs.txt", "obj records")

    def append_kind(kind, fname, title):
        """Emit an extra kind and append it to an existing dump file."""
        base = (out / fname).read_text().splitlines()
        n = emit_config(kind, fname + f".{kind}.tmp", title)
        for e in index["records"]:
            if e["kind"] == kind:
                e["line"] += len(base) + 1
        extra = (out / (fname + f".{kind}.tmp")).read_text().splitlines()
        (out / fname).write_text("\n".join(base + [""] + extra) + "\n", encoding="utf-8")
        (out / (fname + f".{kind}.tmp")).unlink()
        counts[kind] = n
    if "inv" in game.get("extra_kinds", []):
        append_kind("inv", "cache_objs.txt", "inventories (configs/all.inv)")

    # locs: the record plus where the map places it
    def loc_summary(rec, entry):
        pl = placements.get(entry["id"], [])
        entry["placements"] = pl
        if rec is None:
            return []
        body = [f"  {v}" for _, v in rec[1]]
        for square, lv, x, z, shape, ang, n, label in pl[:12]:
            body.append(f"  placed: {label} m{square} level {lv} local {x},{z} shape {shape} angle {ang}"
                        f"  (maps/m{square}.jl2:{n})")
        if len(pl) > 12:
            body.append(f"  placed: ... {len(pl) - 12} more placements")
        if not pl:
            body.append("  placed: nowhere in the selected map squares")
        return body
    counts["loc"] = emit_config("loc", "cache_locs.txt", "loc records", loc_summary)

    # vars: varps and varbits in one file
    n_vp = emit_config("varp", "cache_vars_varp.tmp", "varps")
    n_vb = emit_config("varbit", "cache_vars_varbit.tmp", "varbits")
    vp = (out / "cache_vars_varp.tmp").read_text().splitlines()
    vb = (out / "cache_vars_varbit.tmp").read_text().splitlines()
    for e in index["records"]:
        if e["kind"] == "varbit":
            e["line"] += len(vp) + 1
    (out / "cache_vars.txt").write_text("\n".join(vp + [""] + vb) + "\n", encoding="utf-8")
    (out / "cache_vars_varp.tmp").unlink()
    (out / "cache_vars_varbit.tmp").unlink()
    counts["varp"], counts["varbit"] = n_vp, n_vb
    if "varc" in game.get("extra_kinds", []):
        append_kind("varc", "cache_vars.txt", "varcs (client-only vars)")

    # sounds: name-only pack
    lines = [f"# sound effects -- {len(S.sel['sound'])} selected from "
             "OSRS-Content/osrs239-content/pack/4_soundeffects.pack (Jagex's own names)",
             "# why= the selection rule(s); `bind: frame sound of seq N` means the cache",
             "# itself plays it in-band; every other row is a server-side choice.", ""]
    for i in sorted(S.sel["sound"]):
        sym, n = sounds.get(i, ("?", 0))
        head = len(lines) + 1
        lines.append(f"{i}\t{sym}\t(pack/4_soundeffects.pack:{n})\twhy= {why_text('sound', i)}")
        index["records"].append({"kind": "sound", "id": i, "symbol": sym, "line": head,
                                 "source": f"pack/4_soundeffects.pack:{n}",
                                 "why": {w: d for w, d in S.sel['sound'][i].items()},
                                 "bindings": []})
    (out / "cache_sounds.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    counts["sound"] = len(S.sel["sound"])

    # interfaces: the whole .if for a selected interface, or only the
    # referenced components of a shared top level
    lines = ["# interfaces and clientscripts -- quoted from OSRS-Content/osrs239-content/"
             "interfaces/*.if and scripts/*.cs2 (the decompiled CS2 suite)", ""]
    for i in sorted(S.sel["interface"]):
        sym, n = ifaces[i]
        head = len(lines) + 1
        lines.append(f"[{i}] {sym}    (pack/3_interfaces.pack:{n})")
        lines.append(f"  why= {why_text('interface', i)}")
        p = content / "interfaces" / f"{sym}.if"
        comps = game["iface_components"].get(sym)
        if p.exists():
            body = read_text(p)
            keep = comps is None
            for ln, t in enumerate(body, 1):
                m = re.match(r"^\[([^\]]+)\]", t)
                if m and comps is not None:
                    keep = m.group(1) in comps
                if keep and t.strip():
                    lines.append(f"  {t}    (interfaces/{sym}.if:{ln})")
            if comps is not None:
                lines.append(f"  (only components {', '.join(comps)} quoted; the rest is the shared top level)")
        for ln, ev, sid in iface_hooks.get(i, []):
            lines.append(f"  hook: {ev} runs clientscript {sid} {cscripts.get(sid, ('?', 0))[0]}"
                         f"  (interfaces/{sym}.if:{ln})")
        lines.extend(read_by_lines("interface", i))
        lines.append("")
        index["records"].append({"kind": "interface", "id": i, "symbol": sym, "line": head,
                                 "source": f"pack/3_interfaces.pack:{n}",
                                 "why": {w: d for w, d in S.sel['interface'][i].items()},
                                 "bindings": []})
    lines.append("# clientscripts")
    lines.append("")
    for i, whys in cs_sel.items():
        sym, n = cscripts[i]
        head = len(lines) + 1
        lines.append(f"[clientscript {i}] {sym}    (pack/12_clientscripts.pack:{n})  why= {', '.join(whys)}")
        p = content / "scripts" / f"{sym}.cs2"
        # a script chosen only because it reads a selected record is quoted
        # at those lines; a script chosen by name or number is quoted whole
        only = None
        if game.get("cs2_scan") and all(w.startswith("reads ") for w in whys):
            only = script_hits.get(sym, set())
        if p.exists():
            for ln, t in enumerate(read_text(p), 1):
                if t.strip() and (only is None or ln in only):
                    lines.append(f"  {t}    (scripts/{sym}.cs2:{ln})")
            if only is not None:
                lines.append(f"  (only the lines that name a selected record are quoted)")
        lines.append("")
        index["records"].append({"kind": "clientscript", "id": i, "symbol": sym, "line": head,
                                 "source": f"pack/12_clientscripts.pack:{n}",
                                 "why": {w: [] for w in whys}, "bindings": []})
    (out / "cache_interfaces.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    counts["interface"] = len(S.sel["interface"])
    counts["clientscript"] = len(cs_sel)

    # enums, dbrows, structs
    lines = ["# enums, dbrows and structs that belong to the minigame -- quoted from "
             "OSRS-Content/osrs239-content/configs/all.{enum,dbrow,struct}", ""]
    struct_rows = []
    for sym, rec in recs["struct"].items():
        fl = fields(rec)
        params = {v.split(",")[0]: v.split(",", 2)[-1] for _, k, v in fl if k == "param"}
        why = game["struct_select"](params) or bound_structs.get(sym)
        if why:
            tail = sym.split("_")[-1]
            struct_rows.append((int(tail) if tail.isdigit() else -1, sym, rec, why))
    struct_rows.sort()
    for kind in ("dbrow", "enum"):
        for i in sorted(S.sel[kind]):
            sym = ids[kind].get(i)
            rec = recs[kind].get(sym)
            head = len(lines) + 1
            lines.append(f"[{kind} {i}] {sym}    (configs/all.{kind}:{rec[0] if rec else '?'})  why= {why_text(kind, i)}")
            if rec:
                for n, v in rec[1]:
                    lines.append(f"  {v}    (configs/all.{kind}:{n})")
            if kind == "enum" and enum_types.get(i):
                lines.append(f"  read_as: output type {'/'.join(enum_types[i])} "
                             "(the enum() calls of the CS2 suite that read it)")
            if kind == "enum":
                lines.extend(read_by_lines(kind, i))
            lines.append("")
            index["records"].append({"kind": kind, "id": i, "symbol": sym, "line": head,
                                     "source": f"configs/all.{kind}:{rec[0] if rec else '?'}",
                                     "why": {w: d for w, d in S.sel[kind][i].items()},
                                     "bindings": [],
                                     "fields": [(n, k, v) for n, k, v in fields(rec)] if rec else []})
    for sid, sym, rec, why in struct_rows:
        head = len(lines) + 1
        lines.append(f"[struct {sid}] {sym}    (configs/all.struct:{rec[0]})  why= {why}")
        for n, v in rec[1]:
            lines.append(f"  {v}    (configs/all.struct:{n})")
        lines.append("")
        index["records"].append({"kind": "struct", "id": sid, "symbol": sym, "line": head,
                                 "source": f"configs/all.struct:{rec[0]}",
                                 "why": {why: []}, "bindings": [],
                                 "fields": [(n, k, v) for n, k, v in fields(rec)]})
    (out / "cache_enums_dbrows.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    counts["dbrow"] = len(S.sel["dbrow"])
    counts["enum"] = len(S.sel["enum"])
    counts["struct"] = len(struct_rows)

    # music and jingles
    lines = ["# music tracks and jingles -- pack/6_musictracks.pack, pack/11_musicjingles.pack,",
             "# songs/*.jmid, and the tree's docs/audio/*.tsv rows (paths relative to the worktree)", ""]
    tsv_root = content.parent.parent / "docs" / "audio"
    mus = []
    for i, (sym, n) in songs.items():
        if sym in game["music"] or i in game["music_ids"] or name_match(game, sym):
            mus.append(("song", i, sym, f"pack/6_musictracks.pack:{n}", "songs"))
    for i, (sym, n) in jingles.items():
        if sym in game["jingles"] or name_match(game, sym):
            mus.append(("jingle", i, sym, f"pack/11_musicjingles.pack:{n}", "jingles"))
    for kind, i, sym, src, d in sorted(mus):
        head = len(lines) + 1
        lines.append(f"[{kind} {i}] {sym}    ({src})")
        f = content / d / f"{sym}.jmid"
        lines.append(f"  file: {d}/{sym}.jmid {'present' if f.exists() else 'ABSENT'}")
        for tsv in ("music_tracks_osrs239.tsv", "music_track_names.tsv", "music_regions.tsv",
                    "jingle_names.tsv", "osrs_wiki_jingle_ids.tsv"):
            p = tsv_root / tsv
            if not p.exists():
                continue
            for ln, t in enumerate(read_text(p), 1):
                if t.startswith("#"):
                    continue
                cols = t.split("\t")
                if sym in cols or (kind == "song" and len(cols) > 5 and cols[5] == str(i)) or \
                        (tsv == "music_regions.tsv" and str(i) == "502" and "mor_ul_rek" in cols):
                    lines.append(f"  docs/audio/{tsv}:{ln}: {t}")
        lines.append("")
        index["records"].append({"kind": "music" if kind == "song" else "jingle", "id": i,
                                 "symbol": sym, "line": head, "source": src,
                                 "why": {"name": []}, "bindings": []})
    if not any(k == "jingle" for k, *_ in mus):
        lines.append("# jingles: no jingle in pack/11_musicjingles.pack matches the name rule "
                     f"or the list {sorted(game['jingles'])}.")
    (out / "cache_music.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    counts["music"] = sum(1 for k, *_ in mus if k == "song")
    counts["jingle"] = sum(1 for k, *_ in mus if k == "jingle")

    # the map squares, whole: every loc each one places (games that ask)
    if game.get("map_dump"):
        lines = ["# map squares -- every loc the cache map places in the minigame's squares,",
                 "# from OSRS-Content/osrs239-content/maps/m<x>_<z>.jl2 (`==== LOC ====`: "
                 "`level x z: loc shape [angle]`).",
                 "# A square is 64x64 tiles: world x = 64*sqx + local x, world z = 64*sqz + local z.",
                 "# Per square: one line per distinct loc (count, levels, local bounding box, first",
                 "# placement line); then every placement of the locs the name rule selects.", ""]
        for square, box, label in game["map"]:
            pl = jl2(content, square)
            sx, sz = (int(v) for v in square.split("_"))
            byloc = defaultdict(list)
            for p in pl:
                byloc[p[3]].append(p)
            levels = sorted({p[0] for p in pl})
            lines.append(f"== {label}: maps/m{square}.jl2  (region {(sx << 8) | sz}, world x "
                         f"{64 * sx}..{64 * sx + 63}, z {64 * sz}..{64 * sz + 63}; levels {levels}; "
                         f"{len(pl)} placements of {len(byloc)} distinct locs)")
            for loc in sorted(byloc):
                ps = byloc[loc]
                xs, zs = [p[1] for p in ps], [p[2] for p in ps]
                lines.append(f"  {loc}\t{ids['loc'].get(loc, '?')}\tx{len(ps)}\tlevels "
                             f"{sorted({p[0] for p in ps})}\tlocal x {min(xs)}-{max(xs)} z {min(zs)}-{max(zs)}"
                             f"\t(maps/m{square}.jl2:{ps[0][6]})")
            named = [p for p in pl if name_match(game, ids["loc"].get(p[3], ""))]
            lines.append(f"  -- every placement of a loc the name rule selects ({len(named)}):")
            for lv, x, z, loc, shape, ang, n in named:
                lines.append(f"  placed {loc} {ids['loc'].get(loc)} level {lv} local {x},{z} "
                             f"(world {64 * sx + x},{64 * sz + z}) shape {shape} angle {ang}"
                             f"  (maps/m{square}.jl2:{n})")
            lines.append("")
        (out / "cache_map.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")

    index["counts"] = counts
    index["refs"] = {f"{k}:{i}": v for (k, i), v in refs.items()}
    if index_path:
        index_path.parent.mkdir(parents=True, exist_ok=True)
        index_path.write_text(json.dumps(index, indent=1), encoding="utf-8")
    for k, v in counts.items():
        print(f"{k}\t{v}")


# ---------------------------------------------------------------------------
# The minigames. One entry each; every rule here is restated in the game's
# LEDGER_cache.md.
# ---------------------------------------------------------------------------

def inferno_structs(params):
    # Combat Achievement tasks: param_1306 is the task index, param_1312 the
    # boss group; 60 is the group every Inferno task names ("Kill a Jal-Zek
    # within the Inferno", "Complete the Inferno 5 times", ...).
    if "param_1306" in params and params.get("param_1312") == "60":
        return "combat achievement, param_1312=60"
    for k, v in params.items():
        if v in ("The Inferno", "TzKal-Zuk", "Defeat TzKal-Zuk", "Defeat TzKal-Zuk.",
                 "Kill TzKal-Zuk"):
            return f"{k}={v}"
    return None


def colosseum_structs(params):
    # Combat Achievement tasks: param_1312=25 is the boss group of every
    # Colosseum task ("Defeat Sol Heredit once.", "Complete Wave 4 without ...")
    if "param_1306" in params and params.get("param_1312") == "25":
        return "combat achievement, param_1312=25"
    # the modifier records the modifier enum lists: index 1895, name 1896
    if "param_1895" in params and "param_1896" in params:
        return "modifier record (param_1895 + param_1896)"
    for k, v in params.items():
        s = v.strip()
        if s in ("Fortis Colosseum", "Sol Heredit", "Dizana's", "Dizana's (l)") or \
                s.startswith("Defeat Sol Heredit"):
            return f"{k}={s}"
    return None


GAMES = {
    "inferno": {
        "content_dir": "server/scripts/minigames/minigame_inferno",
        "extra_ref_files": ["npc_combat/i/inferno_*.combat"],
        # word parts of a Jagex symbol (split on `_`)
        "name_tokens": {"inferno", "infernopet", "zuk", "tzkal", "tzkalzuk",
                        "zukrek", "infernalcape"},
        "jal_token": re.compile(r"^jal(nib|mejrah|ak|akrek|tokjad|tok|imkot|xil|akxil|rek|mejjak|zek)?$"),
        "name_substr": re.compile(r"infernal_cape|moving_safe_spot|safe_spot_distructible"),
        "exclude": re.compile(r"^(colosseum_|placeholder_|cert_|br_|trailblazer|league_|"
                              r"deadman_|jad_challenge_|slayer_infernal|superior_infernal|"
                              r"slayerguide_)|infernal_mage|infernalmage"),
        "npc_name_exclude": re.compile(r"^tzhaar_fightcave_|^jad_challenge_"),
        "npc_names": {"Jal-Nib", "Jal-Nib-Rek", "Jal-MejRah", "Jal-Ak", "Jal-AkRek-Mej",
                      "Jal-AkRek-Xil", "Jal-AkRek-Ket", "Jal-ImKot", "Jal-Xil", "Jal-Zek",
                      "JalTok-Jad", "Yt-HurKot", "TzKal-Zuk", "Jal-MejJak", "Ancestral Glyph",
                      "Rocky support", "TzHaar-Ket-Keh", "TzRek-Zuk", "JalRek-Jad"},
        # INFERNO_SOUNDS.md sections 5 and 6: one Jagex asset name on both sides
        "families": [
            ("seq", r"^lizard_cleric_"), ("seq", r"^dagannoth_water_creature_"),
            ("sound", r"^lizard_cleric_"),
            # only the four members s5 joins; the rest of `dagganoth_*` is
            # Waterbirth scenery (pressure doors, plate bodies, eggs)
            ("sound", r"^dagganoth_(attack|hit|death|spines)$"),
            ("sound", r"^firebat_"), ("sound", r"^lavabeast_"), ("sound", r"^magmaquiris_"),
        ],
        # ids INFERNO_SOUNDS.md sections 2, 3, 5, 7 and 8 name for the Inferno
        "named_ids": [
            ("sound", 155, "INFERNO_SOUNDS.md s2/s8"), ("sound", 156, "INFERNO_SOUNDS.md s2/s8"),
            ("sound", 163, "INFERNO_SOUNDS.md s3/s8"), ("sound", 166, "INFERNO_SOUNDS.md s5/s8"),
            ("sound", 408, "INFERNO_SOUNDS.md s8"), ("sound", 409, "INFERNO_SOUNDS.md s8"),
            ("sound", 410, "INFERNO_SOUNDS.md s2/s8"), ("sound", 3528, "INFERNO_SOUNDS.md s2/s8"),
            ("sound", 2294, "INFERNO_SOUNDS.md s7"),
            # a source in the tree names it for the Inferno though no symbol of
            # ours does: Kronos's Jal-MejJak projectile, docs/BOSS_ASSETS.md:250
            ("spotanim", 660, "BOSS_ASSETS.md:250 (Kronos id: healer beam)"),
        ] + [("sound", i, "INFERNO_SOUNDS.md s7 cave block") for i in range(2039, 2047)],
        # the map: the template square whole, and a box around each placement
        # of the entrance loc (inferno_entrance 30352) on the live map
        "map": [
            ("35_83", None, "arena"),
            ("38_80", (55, 0, 63, 10), "entrance"),
            ("38_79", (50, 45, 62, 57), "entrance"),
        ],
        # scratch vars a selftest proc borrows; not an asset of the game
        "ref_exclude": {"varp7_mock_quest_progress"},
        # numeric references in the content that no symbol carries
        "ref_constants": [],
        "clientscripts": [(948, "ref: ^clientscript_fade_in inferno.constant:201"),
                          (951, "ref: ^clientscript_fade_out inferno.constant:200")],
        "iface_components": {"toplevel_osrs_stretch": ["overlay_hud", "overlay_atmosphere"]},
        "struct_select": inferno_structs,
        "music": {"inferno"},
        # Mor Ul Rek, the entrance area's track: archive 502 has no recovered
        # name in pack/6_musictracks.pack (song_502); docs/audio/
        # music_tracks_osrs239.tsv row 2922 and music_regions.tsv 38_79 name it
        "music_ids": {502},
        "jingles": set(),
    },
    # The Fortis Colosseum. Rules restated in
    # docs/minigames/colosseum/sources/LEDGER_cache.md.
    "colosseum": {
        # no Colosseum content exists in our server; only the generated
        # combat ledgers name its records
        "content_dir": None,
        "extra_ref_files": ["npc_combat/c/colosseum_*.combat"],
        "name_tokens": {"colosseum", "colosseeum", "colossi", "colossus", "solheredit",
                        "solhereditecho", "colosseumrewards", "npccolosseum",
                        "colosseummodifiers", "glaiveofralos", "tonalztics", "dizanas",
                        "manticore", "warband", "warbander", "glaive", "minimus", "gloria"},
        "jal_token": re.compile(r"(?!)"),
        "name_substr": re.compile(r"sol_heredit|sunfire_splinter|minotaur_boss|"
                                  r"jaguar_ranger|jaguar_human|jaguar_warrior|serpent_mager|"
                                  r"serpent_shaman|doom_scorpion|ralos01|(?:^|_)echo_crystal(?:$|_)"),
        # other game modes and other content that names the Colosseum: a
        # Leagues task, a Deadman finale, a quest's knight (vmq2_), the Meat
        # and Greet quest's records (mag_: quest_meatandgreet uses them), a
        # skill guide row, a clue helper row
        "exclude": re.compile(r"^(placeholder_|cert_|br_|trailblazer|leagues?_|deadman_|"
                              r"skill_feature_|cluehelper_|mag_)|deadman|vmq2"),
        "npc_name_exclude": re.compile(r"^(deadman_|leagues?_)"),
        "npc_names": {"Sol Heredit", "Minimus", "Gloria", "Smol Heredit"},
        "families": [],
        "named_ids": [],
        "id_blocks": [
            ("seq", 10798, 10923, "seq id block 10798-10923 (every other record in it "
                                  "is chosen by name)"),
            ("spotanim", 2666, 2734, "spotanim id block 2666-2734 (every other record in it "
                                     "is chosen by name)"),
        ],
        # the cache's own sound browser lists every Colosseum sound by monster
        "synth_menus": ["synth_npccolosseum", "synth_colosseumrewards"],
        "map": [
            ("28_48", None, "arena"),
            ("28_148", None, "lobby"),
        ],
        "ref_exclude": set(),
        "ref_constants": [],
        "clientscripts": [],
        "iface_components": {},
        "struct_select": lambda params: colosseum_structs(params),
        "music": set(),
        # "Are You Not Entertained?": dbrow music_fortis_colosseum names midi
        # 782, whose pack name is the unrecovered song_782
        "music_ids": {782},
        "jingles": set(),
        "extra_kinds": ["varc", "inv"],
        "data_bind": True,
        "cs2_scan": True,
        "maya_lengths": True,
        "map_dump": True,
        "loc_soundrandom": True,
        # a Combat Achievement task's category struct (struct_3594, "Combat
        # Achievements"), shared by every task in the game
        "struct_param_skip": {"param_1307"},
    },
}


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", required=True, choices=sorted(GAMES))
    ap.add_argument("--content", required=True, type=pathlib.Path)
    ap.add_argument("--out", required=True, type=pathlib.Path)
    ap.add_argument("--index", type=pathlib.Path)
    a = ap.parse_args()
    run(a.game, a.content.resolve(), a.out, a.index)


if __name__ == "__main__":
    sys.exit(main())
