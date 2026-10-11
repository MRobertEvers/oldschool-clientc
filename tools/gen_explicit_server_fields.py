#!/usr/bin/env python3
"""
gen_explicit_server_fields — state, once, every server field a record left to a
default.

    cachepack pack --src OSRS-Content/osrs239-content --server-only \\
        --server-out <scratch> --defaults-report <scratch>/report.tsv
    TORIRS_DEFS_DUMP=<scratch>/engine.dump <a server built with the dump hook> --selftest
    tools/gen_explicit_server_fields.py --report <scratch>/report.tsv \\
        --dump <scratch>/engine.dump --write

Run ONCE (2026-10-11). Its output is authored data from then on and nothing
re-runs it: a value that is wrong is fixed in the generated file, by hand.

## What a default was

Three things answered for a record that said nothing (cachepack's
`--defaults` check names all three): the type's `[default]` block
(`general/configs/npc_default.npc`, now deleted), the engine's C seed for an
unstated band field (`npc_def_builtin`, `loc_def_add`, a varp or inv with no
band at all), and a tree param whose `default=` named a value. A Maiden blood
spawn dropped bones because `[default]` said `death_drop,bones` for every npc.

## Where each value comes from

From what the ENGINE resolved, not from a re-derivation of its rules: the
report says which fields a record left out, and the dump (`--dump`, one row per
record of what the running server answered for it) says what the server then
used. Writing that value down changes nothing the server does, which is the
point — the only deliberate changes are listed here:

  death_drop   the npc's own wiki page's `Always` remains line
               (`wiki_droptable.death_drop_choice`, the same decision
               gen_npc_stats.py states for the roster); `null` when there is no
               page, no version match, or the page states no remains. Owner,
               2026-10-11: no wiki answer is no drop, not bones.
  rune_type    `0` ("no single altar", runecraft.obj) on a loc that does not
               state one. The param default was `1`, which is ^air.
  nomove       written as the engine derived it, `moverestrict=nomove`; no
               record ever stated it, so the two always agree.

A client-native band field (dbtable `column`) is the cache record's own field
and its `=default` is the record's statement, so it is not touched.
"""
from __future__ import annotations

import argparse
import collections
import csv
import glob
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(REPO, "OSRS-Content", "osrs239-content")
OUT_DIR = os.path.join(CONTENT, "server", "scripts", "_explicit", "configs")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wiki_droptable as wd  # noqa: E402
import wiki_infobox as wi  # noqa: E402

csv.field_size_limit(1 << 20)

WORDS = {
    ("npc", "blockwalk"): {0: "none", 1: "npc", 2: "all", 3: "player"},
    ("npc", "retaliate"): {0: "no", 1: "yes"},
    ("npc", "hitsplat"): {0: "no", 1: "yes"},
    ("npc", "forcemulti"): {0: "no", 1: "yes"},
    ("npc", "givechase"): {0: "no", 1: "yes"},
    ("npc", "huntmode"): {0: "none", 1: "aggressive", 2: "aggressive_melee"},
    ("npc", "defaultmode"): {0: "none", 1: "wander", 2: "patrol"},
    ("npc", "moverestrict"): {0: "normal", 1: "blocked", 2: "blocked+normal", 3: "indoors",
                              4: "outdoors", 5: "nomove", 6: "passthru"},
    ("npc", "facing"): {0: "northwest", 1: "north", 2: "northeast", 3: "west", 4: "east",
                        5: "southwest", 6: "south", 7: "southeast"},
    ("varp", "scope"): {0: "temp", 1: "perm"},
    ("varp", "transmit"): {0: "no", 1: "yes"},
    ("varp", "protect"): {0: "no", 1: "yes"},
    ("varp", "wholewrite"): {0: "deny", 1: "allow"},
    ("varp", "wholeread"): {0: "deny", 1: "allow"},
    ("inv", "restock"): {0: "no", 1: "yes"},
    ("inv", "allstock"): {0: "no", 1: "yes"},
    ("inv", "stackall"): {0: "no", 1: "yes"},
}

# npc band field -> the dump's key for the value the engine used.
NPC_DUMP_KEY = {
    "hitpoints": "hp", "attack": "att", "strength": "str", "defence": "def",
    "ranged": "rng", "magic": "mag", "attackrate": "rate", "attack_anim": "aa",
    "defend_anim": "da", "death_anim": "dea", "death_delay": "ddelay",
    "blockwalk": "bw", "blocksight": "bs", "attack_sound": "as", "defend_sound": "ds",
    "death_sound": "des", "retaliate": "ret", "healthbar": "hb", "hitsplat": "hs",
    "forcemulti": "fm", "wanderrange": "wr", "maxrange": "mr", "huntrange": "hr",
    "timer": "timer", "respawnrate": "resp", "moverestrict": "mvr", "huntmode": "hunt",
    "givechase": "chase", "nomove": "nomove", "defaultmode": "mode", "facing": "face", "damagetype": "dmg",
    "attackrange": "arange",
}
# Fields whose value names a record of another type.
NPC_REF = {"attack_anim": "seq", "defend_anim": "seq", "death_anim": "seq",
           "healthbar": "healthbar"}
NPC_PARAM_SPELLED = {"attackrate", "death_drop", "attack_anim", "defend_anim", "death_anim",
                     "attack_sound", "defend_sound", "death_sound", "huntrange", "damagetype",
                     "attackrange"}
LOC_REF = {"next_loc_stage": "loc", "prev_loc_stage": "loc", "funeral_pyre_struct": "struct"}


def load_names(ns: str) -> dict[int, str]:
    """id -> name from configs/all.<ns>.compack."""
    out: dict[int, str] = {}
    path = os.path.join(CONTENT, "configs", "all.%s.compack" % ns)
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = re.match(r"^(\d+)=(\S+)", line)
            if m:
                out[int(m.group(1))] = m.group(2)
    return out


def load_categories() -> dict[str, int]:
    out: dict[str, int] = {}
    with open(os.path.join(CONTENT, "pack", "category.pack"), encoding="utf-8") as f:
        for line in f:
            m = re.match(r"^(\d+)=(\S+)", line)
            if m:
                out[m.group(2)] = int(m.group(1))
    return out


def load_stat_names() -> dict[int, str]:
    out: dict[int, str] = {}
    with open(os.path.join(CONTENT, "pack", "stat.pack"), encoding="utf-8") as f:
        for line in f:
            m = re.match(r"^(\d+)=(\S+)", line)
            if m:
                out[int(m.group(1))] = m.group(2)
    return out


def read_report(path: str):
    """{type: {record: {field: (stated, value)}}}, field order kept per type."""
    records: dict[str, dict[str, dict[str, tuple[bool, str]]]] = collections.defaultdict(dict)
    order: dict[str, list[str]] = collections.defaultdict(list)
    with open(path, encoding="utf-8") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            t, r, fld = row["type"], row["record"], row["field"]
            rec = records[t].setdefault(r, {})
            if fld not in rec and fld not in order[t]:
                order[t].append(fld)
            rec[fld] = (row["stated"] == "1", row["value"])
    return records, order


def read_dump(path: str):
    """The engine's answers, by kind and record name."""
    npc: dict[str, dict[str, str]] = {}
    npc_id: dict[str, int] = {}
    loc_cat: dict[str, int] = {}
    varp: dict[str, list[int]] = {}
    inv: dict[str, dict[str, str]] = {}
    loc_id: dict[str, int] = {}
    obj: dict[str, str] = {}
    loc_param: dict[int, dict[str, str]] = collections.defaultdict(dict)
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            kind = line[:2]
            if kind == "P\t":
                _, what, rid, name, value = line.rstrip("\n").split("\t", 4)
                if what == "loc":
                    loc_param[int(rid)][name] = value
                continue
            parts = line.rstrip("\n").split("\t")
            if kind == "N\t":
                fields = {}
                for tok in parts[3].split(" "):
                    if "=" in tok:
                        k, v = tok.split("=", 1)
                        fields[k] = v
                if parts[2] != "-":
                    npc[parts[2]] = fields
                    npc_id[parts[2]] = int(parts[1])
            elif kind == "V\t":
                if parts[2] != "-":
                    varp[parts[2]] = [int(x) for x in parts[3].split(" ")]
            elif kind == "I\t":
                if parts[2] != "-":
                    fields = {}
                    for tok in parts[3].split(" "):
                        if "=" in tok:
                            k, v = tok.split("=", 1)
                            fields[k] = v
                    inv[parts[2]] = fields
            elif kind == "L\t":
                if parts[2] != "-":
                    loc_id[parts[2]] = int(parts[1])
                    loc_cat[parts[2]] = int(parts[3].split(" ")[0][len("cat="):])
            elif kind == "O\t":
                if parts[2] != "-":
                    obj[parts[2]] = parts[3][len("req="):]
    return npc, npc_id, varp, inv, loc_id, loc_cat, obj, loc_param


_DD_CACHE: dict[str, tuple[str, str]] = {}


_ID_INDEX: dict[int, str] | None = None


def wiki_title_for_id(npc_id: int) -> str | None:
    """The cached monster page whose infobox names this npc id. By id, never by
    name (docs/NPC_WIKI_DROPTABLES_PLAN.md §1): no id is on two pages."""
    global _ID_INDEX
    if _ID_INDEX is None:
        _ID_INDEX = {}
        for path in sorted(glob.glob(os.path.join(wi.MONSTERS_DIR, "*.wikitext"))):
            title = os.path.basename(path)[: -len(".wikitext")]
            with open(path, encoding="utf-8") as f:
                for version in wi.parse_page(f.read()):
                    for i in version["ids"]:
                        _ID_INDEX.setdefault(i, title)
    return _ID_INDEX.get(npc_id)


def wiki_death_drop(gameval: str, npc_id: int) -> tuple[str, str]:
    """(value, source note). `null` whenever the wiki gives no answer."""
    if gameval in _DD_CACHE:
        return _DD_CACHE[gameval]
    answer = ("null", "no wiki page names npc %d" % npc_id)
    res = wd.resolved_title(gameval)
    if res is None and wiki_title_for_id(npc_id):
        res = (wiki_title_for_id(npc_id), [npc_id])
    if res is not None:
        title, ids = res
        try:
            text = wi.load_page(title)
        except OSError:
            text = None
            answer = ("null", "no cached page '%s'" % title)
        if text is not None:
            versions = wi.parse_page(text)
            stats_id = next(iter(wd.find_stats_id_candidates(gameval, ids)), None)
            match = next((v for v in versions if stats_id in v["ids"]), None)
            if match is None and len(ids) == 1:
                match = next((v for v in versions if ids[0] in v["ids"]), None)
            dropversion = match["fields"].get("dropversion") if match else None
            blocks = [b for b in wd.parse_drop_blocks(text) if not b["tertiary"]]
            main = wd.select_blocks(blocks, dropversion)
            if main:
                choice = wd.death_drop_choice([ln for b in main for ln in b["lines"]])
            else:
                # The same weaker question gen_npc_stats.death_drop_for_page asks:
                # one answer across every main block is still an answer.
                found = {c["gameval"] for c in (wd.death_drop_choice(b["lines"]) for b in blocks) if c}
                choice = {"gameval": found.pop()} if len(found) == 1 else None
            if choice:
                answer = (choice["gameval"], "wiki '%s'" % title)
            else:
                answer = ("null", "wiki '%s' states no remains" % title)
    _DD_CACHE[gameval] = answer
    return answer


def ref_name(names: dict[int, str], value: str) -> str:
    n = int(value)
    if n < 0:
        return "null"
    return names.get(n, str(n))


class Values:
    """The explicit spelling of one (type, record, field) the engine answered
    for: what the dump says it used, in the field's own words and names."""

    def __init__(self, dump_path: str):
        (self.npc, self.npc_ids, self.varp, self.inv, self.loc_ids, self.loc_cats,
         self.obj, self.loc_param) = read_dump(dump_path)
        categories = load_categories()
        self.rune_readers = {categories[n] for n in ("rc_altar", "rc_ruins", "rc_exit_portal")}
        self.names = {ns: load_names(ns) for ns in ("seq", "obj", "healthbar", "loc", "struct")}
        self.stats = load_stat_names()
        self.counts: collections.Counter = collections.Counter()
        self.rc_relying: list[str] = []

    def known(self, t: str, rec: str) -> bool:
        table = {"npc": self.npc, "loc": self.loc_ids, "varp": self.varp, "inv": self.inv,
                 "obj": self.obj}[t]
        return rec in table

    def lines(self, t: str, rec: str, fld: str) -> list[str]:
        return getattr(self, "_" + t)(rec, fld)

    def _npc(self, rec: str, fld: str) -> list[str]:
        engine = self.npc[rec]
        if fld == "death_drop":
            value, why = wiki_death_drop(rec, self.npc_ids[rec])
            self.counts["npc death_drop " + ("null" if value == "null" else "wiki")] += 1
            return ["param=death_drop,%s // %s" % (value, why)]
        if fld == "patrol":
            if engine.get("patrol", "0") != "0":
                raise SystemExit("%s: an unstated patrol with waypoints" % rec)
            return ["patrol=empty"]
        raw = engine[NPC_DUMP_KEY[fld]]
        if ("npc", fld) in WORDS:
            value = WORDS[("npc", fld)][int(raw)]
        elif fld in NPC_REF:
            value = ref_name(self.names[NPC_REF[fld]], raw)
        elif fld.endswith("_sound"):
            value = "null" if int(raw) < 0 else raw
        else:
            value = raw
        return [("param=%s,%s" if fld in NPC_PARAM_SPELLED else "%s=%s") % (fld, value)]

    def _loc(self, rec: str, fld: str) -> list[str]:
        raw = self.loc_param[self.loc_ids[rec]][fld]
        if fld == "rune_type":
            if raw != "1":
                raise SystemExit("%s: unstated rune_type answered %s, not the default" % (rec, raw))
            # Read only by the runecraft triggers on these three categories. One
            # of those that stated nothing really did act as ^air, so it says
            # so; every other loc has no altar.
            if self.loc_cats.get(rec, -1) in self.rune_readers:
                self.rc_relying.append(rec)
                return ["param=rune_type,1 // ^air: stated none, and the runecraft trigger read the default"]
            return ["param=rune_type,0"]
        if fld in LOC_REF:
            return ["param=%s,%s" % (fld, ref_name(self.names[LOC_REF[fld]], raw))]
        return ["param=%s,%s" % (fld, raw)]

    def _varp(self, rec: str, fld: str) -> list[str]:
        order = ["scope", "transmit", "protect", "wholewrite", "wholeread"]
        return ["%s=%s" % (fld, WORDS[("varp", fld)][self.varp[rec][order.index(fld)]])]

    def _inv(self, rec: str, fld: str) -> list[str]:
        engine = self.inv[rec]
        if fld == "scope":
            # Not shared, and every player container is saved whatever its scope
            # (torirs_server_save.c): perm is what the engine does.
            if engine["shared"] != "0":
                raise SystemExit("%s: shared with no stated scope" % rec)
            return ["scope=perm"]
        if fld == "stock":
            if engine["stock"]:
                raise SystemExit("%s: unstated stock with items" % rec)
            return ["stock=empty"]
        key = {"restock": "restock", "allstock": "all", "stackall": "stack"}[fld]
        return ["%s=%s" % (fld, WORDS[("inv", fld)][int(engine[key])])]

    def _obj(self, rec: str, fld: str) -> list[str]:
        pairs = [p for p in self.obj[rec].split(";") if p]
        if not pairs:
            return ["levelrequire=empty"]
        self.counts["obj levelrequire restated from the cache"] += 1
        return ["levelrequire%d=%s,%s" % (i + 1, self.stats[int(s)], lvl)
                for i, (s, lvl) in enumerate(p.split(":") for p in pairs)]


BAND_FIELDS = {
    "npc": set(NPC_DUMP_KEY) | {"death_drop", "patrol", "nomove"},
    "loc": {"next_loc_stage", "mining_rock_empty", "rune_type", "prev_loc_stage",
            "temple_wall_level", "funeral_pyre_struct"},
    "varp": {"scope", "transmit", "protect", "wholewrite", "wholeread"},
    "inv": {"scope", "restock", "allstock", "stackall", "stock"},
    "obj": {"levelrequire"},
}
DEFAULT_LINE_RE = re.compile(r"^(\s*)(?:param=)?([a-z_]+)[=,]default\s*(//.*)?$")
BLOCK_RE = re.compile(r"^\s*\[([^\]]+)\]")


def rewrite_authored(values: Values, records: dict, write: bool) -> set[tuple[str, str, str]]:
    """An authored `<field>=default` is a default too, and it outranks the
    generated layer (cp_merge_rank_for: authored 2, *.generated.* 1), so it
    would hide the value written there. State the value on that line instead.
    Only this build's records -- the report leaves out a lane's (cachepack
    counts those when the lane is built), and an authored overlay of a lane
    record that said `=default` defers to the lane's own value there."""
    done: set[tuple[str, str, str]] = set()
    root = os.path.join(CONTENT, "server", "scripts")
    for t, fields in BAND_FIELDS.items():
        for dirpath, _, files in os.walk(root):
            for name in files:
                if not name.endswith("." + t) or ".generated." in name:
                    continue
                path = os.path.join(dirpath, name)
                with open(path, encoding="utf-8") as f:
                    text = f.read()
                out, block, changed = [], None, 0
                for line in text.split("\n"):
                    m = BLOCK_RE.match(line)
                    if m:
                        block = m.group(1)
                    m = DEFAULT_LINE_RE.match(line)
                    if (m and block and m.group(2) in fields and block != "default"
                            and block in records.get(t, {}) and values.known(t, block)):
                        new = values.lines(t, block, m.group(2))
                        out.extend(m.group(1) + n for n in new)
                        done.add((t, block, m.group(2)))
                        changed += 1
                        continue
                    out.append(line)
                if changed:
                    values.counts["authored %s =default lines stated" % t] += changed
                    if write:
                        with open(path, "w", encoding="utf-8") as f:
                            f.write("\n".join(out))
    return done


# ---- the second pass: params whose default= named a value (2026-10-11) ------
#
# Every tree param that declared a value default (cachepack names them) now
# declares its type's absence, and every record a reader can reach states the
# value it used to inherit. Which records: every record of the type the
# reader asks (`oc_param` on any wielded or used obj, `npc_param` on any npc),
# except where the reader is bound by trigger to named records.
PARAM_READERS = {
    "obj": ["ready_baseanim", "turnonspot_baseanim", "walk_f_baseanim", "walk_b_baseanim",
            "walk_l_baseanim", "walk_r_baseanim", "running_baseanim", "stabattack_anim",
            "slashattack_anim", "crushattack_anim", "rangeattack_anim", "attack_anim_stance1",
            "attack_anim_stance2", "attack_anim_stance3", "attack_anim_stance4", "defend_anim",
            "sa_energy", "mining_rate", "mining_animation", "rune_type"],
    "npc": ["combat_xp_multiplier"],
}
# A deliberate value rather than the inherited one. rune_type's default was 1,
# ^air: every obj used on the air ruins opened them (~talisman_opens).
PARAM_VALUE = {("obj", "rune_type"): "0"}
# Readers bound to named records. The string default was never read (an absent
# string param answers ""), so dropping a god cape printed an empty message.
PARAM_NAMED = {("obj", "lose_cape_message"): (
    ["saradomin_cape", "zamorak_cape", "guthix_cape"],
    "The gods reclaim the game as it touches the ground.")}
ABSENCE = {"int": "0", "boolean": "no", "string": "null"}


def tree_params() -> dict[str, tuple[str, str, str]]:
    """name -> (file, type, default) for every tree-declared param. A param the
    cache's own table declares (configs/all.param, which `combat.param` restates
    for attackrate) keeps the cache's default: that is the cache's data."""
    with open(os.path.join(CONTENT, "configs", "all.param"), encoding="utf-8") as f:
        cache = set(re.findall(r"^\[([^\]]+)\]", f.read(), re.M))
    out = {}
    root = os.path.join(CONTENT, "server", "scripts")
    for dirpath, _, files in os.walk(root):
        for name in files:
            if not name.endswith(".param"):
                continue
            path = os.path.join(dirpath, name)
            cur, typ = None, None
            with open(path, encoding="utf-8") as f:
                for line in f:
                    line = line.strip()
                    m = BLOCK_RE.match(line)
                    if m:
                        cur = m.group(1)
                        continue
                    if cur and line.startswith("type="):
                        typ = line[5:].strip()
                    if cur and line.startswith("default=") and cur not in cache:
                        out[cur] = (path, typ, line[8:].strip())
    return out


def stated_params(t: str) -> dict[str, set[str]]:
    """record -> the params some layer of this build states for it."""
    out: dict[str, set[str]] = collections.defaultdict(set)
    paths = [os.path.join(CONTENT, "configs", "all.%s" % t)]
    for dirpath, _, files in os.walk(os.path.join(CONTENT, "server", "scripts")):
        paths += [os.path.join(dirpath, n) for n in files if n.endswith("." + t)]
    for path in paths:
        cur = None
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                m = BLOCK_RE.match(line)
                if m:
                    cur = m.group(1)
                    continue
                m = re.match(r"^\s*param=([a-z0-9_]+),(.*)$", line)
                if cur and m and not m.group(2).strip().startswith("default"):
                    out[cur].add(m.group(1))
    return out


def add_lines(path: str, additions: dict[str, list[str]]) -> int:
    """Append lines to each named block of a generated file, adding the blocks
    it lacks at its end."""
    with open(path, encoding="utf-8") as f:
        text = f.read()
    parts = re.split(r"(?m)^(?=\[)", text)
    seen = set()
    for i, part in enumerate(parts):
        m = re.match(r"^\[([^\]]+)\]", part)
        if not m or m.group(1) not in additions:
            continue
        seen.add(m.group(1))
        body = part.rstrip("\n")
        parts[i] = body + "\n" + "\n".join(additions[m.group(1)]) + "\n\n"
    tail = ["[%s]\n%s\n" % (n, "\n".join(additions[n])) for n in sorted(additions) if n not in seen]
    with open(path, "w", encoding="utf-8") as f:
        f.write("".join(parts).rstrip("\n") + "\n\n" + "\n".join(tail))
    return len(additions)


def params_pass(records: dict, write: bool) -> int:
    declared = tree_params()
    for t, names in PARAM_READERS.items():
        stated = stated_params(t)
        additions: dict[str, list[str]] = collections.defaultdict(list)
        for p in names:
            value = PARAM_VALUE.get((t, p), declared[p][2])
            for rec in records.get(t, {}):
                if rec != "default" and p not in stated.get(rec, ()):
                    additions[rec].append("param=%s,%s" % (p, value))
            print("%s %-24s stated on %d more record(s) as %s" % (
                t, p, sum(1 for r in additions if any(l.startswith("param=%s," % p) for l in additions[r])), value))
        for (tt, p), (recs, text) in PARAM_NAMED.items():
            if tt == t:
                for rec in recs:
                    if p not in stated.get(rec, ()):
                        additions[rec].append("param=%s,%s" % (p, text))
        if write:
            add_lines(os.path.join(OUT_DIR, "server_fields.generated.%s" % t), additions)
    # The declarations: every value default becomes its type's absence.
    for p, (path, typ, default) in sorted(declared.items()):
        absent = ABSENCE.get(typ, "null")
        if default in ("", "default", "null", "empty", "-1", "0", "no"):
            continue
        print("param %-24s default=%s -> %s" % (p, default, absent))
        if write:
            with open(path, encoding="utf-8") as f:
                lines = f.read().split("\n")
            cur = None
            for i, line in enumerate(lines):
                m = BLOCK_RE.match(line)
                if m:
                    cur = m.group(1)
                elif cur == p and line.strip().startswith("default="):
                    lines[i] = "default=" + absent
            with open(path, "w", encoding="utf-8") as f:
                f.write("\n".join(lines))
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--report", required=True, help="cachepack --defaults-report TSV")
    ap.add_argument("--dump", help="TORIRS_DEFS_DUMP output (the band pass)")
    ap.add_argument("--params", action="store_true",
                    help="the second pass: state every value-defaulted tree param")
    ap.add_argument("--write", action="store_true", help="write the files (else: counts only)")
    args = ap.parse_args()
    if args.params:
        return params_pass(read_report(args.report)[0], args.write)

    records, order = read_report(args.report)
    values = Values(args.dump)
    rewritten = rewrite_authored(values, records, args.write)

    out: dict[str, list[str]] = collections.defaultdict(list)
    members: dict[str, list[str]] = collections.defaultdict(list)
    for t in ("npc", "loc", "varp", "inv", "obj"):
        for rec in sorted(records.get(t, {})):
            if rec == "default":
                continue
            if not values.known(t, rec):
                values.counts["%s: no id in this build (a lane's record), left alone" % t] += 1
                continue
            lines = []
            for fld in order[t]:
                stated, _ = records[t][rec].get(fld, (False, ""))
                if stated or (t, rec, fld) in rewritten or fld not in BAND_FIELDS[t]:
                    continue
                lines.extend(values.lines(t, rec, fld))
            if lines:
                out[t].append("[%s]\n%s\n" % (rec, "\n".join(lines)))
                members[t].append(rec)
                values.counts["%s records" % t] += 1

    for k in sorted(values.counts):
        print("%-60s %d" % (k, values.counts[k]))
    if values.rc_relying:
        print("loc rune_type: %d runecraft locs relied on the ^air default and now state it: %s"
              % (len(values.rc_relying), ", ".join(values.rc_relying)))
    if not args.write:
        return 0

    os.makedirs(OUT_DIR, exist_ok=True)
    header = HEADER.format(script=os.path.relpath(__file__, REPO))
    for t, blocks in out.items():
        path = os.path.join(OUT_DIR, "server_fields.generated.%s" % t)
        with open(path, "w", encoding="utf-8") as f:
            f.write(header)
            f.write("\n".join(blocks))
        print("wrote %s (%d blocks)" % (os.path.relpath(path, REPO), len(blocks)))
    for t in ("npc", "loc", "varp"):
        add_members(t, members[t])
    return 0


HEADER = """\
// Every server field this tree's records had left to a default, stated.
//
// Generated ONCE, on 2026-10-11, by {script}, from what the
// running server resolved for each record that said nothing: the type's
// `[default]` block (deleted the same day), the engine's C seed for an unstated
// field, or a param's value default. Nothing re-runs it. It is authored data now:
// a wrong value is corrected here, by hand.
//
// A record states every server field of its type (cachepack --defaults=error).
// This is the generated layer (a `.generated.` file merges below authored ones,
// cp_merge_rank_for), so a value stated in a hand-written block wins over the
// one here; delete the line here when that happens. Another `.generated.` file
// stating the same key is a merge error naming both.
//
// The deliberate changes (the rest is what the engine already did):
//   death_drop   the npc's wiki page's `Always` remains line, or `null` when the
//                wiki gives none -- never the old bones fallthrough.
//   rune_type    0 (no single altar) where a loc stated none; the param's
//                default was 1, which is ^air.

"""


def add_members(ns: str, added: list[str]) -> None:
    """Name every record that now has a server half in pack/<ns>.server: a
    section appended after the authored list, which is left as it was."""
    path = os.path.join(CONTENT, "pack", "%s.server" % ns)
    with open(path, encoding="utf-8") as f:
        text = f.read()
    if MEMBERS_HEADER in text:
        text = text[: text.index(MEMBERS_HEADER)]
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
    have = {l.strip() for l in text.splitlines() if l.strip() and not l.startswith("//")}
    new = sorted(n for n in set(added) if n not in have)
    with open(path, "a", encoding="utf-8") as f:
        f.write(MEMBERS_HEADER)
        f.write("\n".join(new) + "\n")
    print("pack/%s.server: %d names added to %d" % (ns, len(new), len(have)))


MEMBERS_HEADER = """
// ---------------------------------------------------------------------------
// Every other record (2026-10-11). A record states every server field of its
// type -- no default answers for it (cachepack --defaults=error) -- so every
// record has a server half, and this file is now the whole roster rather than
// the subset the header above describes. Its fields are in
// server/scripts/_explicit/configs/server_fields.generated.<type>.
// ---------------------------------------------------------------------------
"""


if __name__ == "__main__":
    sys.exit(main())
