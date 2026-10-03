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


def run(game_name, content, out, index_path):
    game = GAMES[game_name]
    kinds = ["npc", "seq", "spotanim", "loc", "obj", "varp", "varbit",
             "enum", "dbrow", "struct"]
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
              "enum", "dbrow"]:
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
        if p.exists():
            for ln, t in enumerate(read_text(p), 1):
                if t.strip():
                    lines.append(f"  {t}    (scripts/{sym}.cs2:{ln})")
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
        why = game["struct_select"](params)
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
                if sym in cols or (len(cols) > 5 and cols[5] == str(i)) or \
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
