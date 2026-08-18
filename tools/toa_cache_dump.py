#!/usr/bin/env python3
"""Extract every Tombs of Amascut record from the unpacked osrs239 cache.

    tools/…/toa_cache_dump.py OSRS-Content/osrs239-content docs/minigames/tombs_of_amascut/sources

Writes one file per config type: the numeric id, the cache's own symbol, and
(for npc/loc/obj) the whole decoded record.
"""
import re, sys, pathlib

CONTENT = pathlib.Path(sys.argv[1])
OUT = pathlib.Path(sys.argv[2])
OUT.mkdir(parents=True, exist_ok=True)

# Every ToA symbol in this cache starts with one of these. `warden` and
# `scarab` also match non-ToA records, so those are filtered by id range below.
PAT = re.compile(
    r"^(toa_|akkha|osb10_toa|kephri|wardens_p3|warden_pet|poh_warden_pet|"
    r"spotanim_zebak|zebak|baba|npc_akkha|npc_mandrill|npc_kephri|npc_scarab|"
    r"npc_wardens|npc_amascut|npc_apmeken|npc_crondis|npc_scabaras|npc_het|"
    r"npc_swarm01_scarab|npc_baboon|npc_zebak|npc_osmumten|crondis_|apmeken_|"
    r"scabaras_|het_|amascut_|osmumten_|tumeken|elidinis|masori|lightbearer|"
    r"keris_partisan|fx_obelisk_wardens|fx_wardens|fx_orb_dust|baboon|"
    r"warden_)", re.I)
EXTRA = re.compile(
    r"^(scarab_swarm|scarabs|osmumtens_fang|osmumtens_khopesh|thread_of_elidinis|"
    r"breach_of_the_scarab|eye_of_the_corruptor|jewel_of_the_sun|wardenpet_)", re.I)


def ids(kind):
    """symbol -> id, from all.<kind>.compack (`id=symbol` per line)."""
    out = {}
    p = CONTENT / "configs" / f"all.{kind}.compack"
    if not p.exists():
        return out
    for line in p.read_text(encoding="cp1252", errors="replace").splitlines():
        if "=" not in line:
            continue
        i, _, name = line.partition("=")
        try:
            out[name.strip()] = int(i)
        except ValueError:
            pass
    return out


def records(kind):
    """symbol -> whole `[symbol] ... ` block from all.<kind>."""
    p = CONTENT / "configs" / f"all.{kind}"
    if not p.exists():
        return {}
    text = p.read_text(encoding="cp1252", errors="replace")
    out, cur, buf = {}, None, []
    for line in text.splitlines():
        m = re.match(r"^\[([^\]]+)\]\s*$", line)
        if m:
            if cur:
                out[cur] = "\n".join(buf).rstrip()
            cur, buf = m.group(1), []
        elif cur is not None:
            buf.append(line)
    if cur:
        out[cur] = "\n".join(buf).rstrip()
    return out


def wanted(name):
    return bool(PAT.match(name) or EXTRA.match(name))


def dump(kind, full=True):
    idmap, recs = ids(kind), records(kind)
    rows = sorted(((i, n) for n, i in idmap.items() if wanted(n)))
    if not rows:
        return 0
    lines = [f"# {kind} — {len(rows)} Tombs of Amascut records from cache.osrs239",
             f"# id\tsymbol" + ("\t(record follows)" if full else "")]
    for i, n in rows:
        if full and n in recs:
            lines.append(f"\n[{i}] {n}")
            lines.append(recs[n])
        else:
            lines.append(f"{i}\t{n}")
    (OUT / f"cache_{kind}_toa.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return len(rows)


for kind, full in [("npc", True), ("loc", False), ("obj", True), ("seq", False),
                   ("spotanim", False), ("varbit", True), ("varp", False),
                   ("varc", False), ("inv", True), ("enum", False),
                   ("struct", False), ("param", False), ("dbtable", False),
                   ("dbrow", False), ("mapelement", False)]:
    n = dump(kind, full)
    if n:
        print(f"{kind}\t{n}")

# name-only packs: sounds, interfaces, models, sprites
for pack, label in [("4_soundeffects", "sound"), ("3_interfaces", "interface"),
                    ("0_animations", "anim_archive"), ("8_sprites", "sprite")]:
    p = CONTENT / "pack" / f"{pack}.pack"
    if not p.exists():
        continue
    rows = []
    for line in p.read_text(encoding="cp1252", errors="replace").splitlines():
        if "=" not in line:
            continue
        i, _, name = line.partition("=")
        name = name.strip()
        if wanted(name):
            rows.append((int(i), name))
    if rows:
        rows.sort()
        (OUT / f"cache_{label}_toa.txt").write_text(
            f"# {label} — {len(rows)} Tombs of Amascut entries from pack/{pack}.pack\n"
            + "\n".join(f"{i}\t{n}" for i, n in rows) + "\n", encoding="utf-8")
        print(f"{label}\t{len(rows)}")


# ---------------------------------------------------------------------------
# Invocations
# ---------------------------------------------------------------------------
# The invocation list is not a config type of its own: each invocation is a
# `struct` carrying param 1159 (its index, 1..46, which is also its bit position
# in the party's invocation varps) and param 1160 (its name). Selecting on
# "has both" finds all 46 and nothing else, so this needs no id list to maintain.
INVOCATION_PARAMS = [
    ("param_1159", "index"), ("param_1161", "category"),
    ("param_1162", "raid_level"), ("param_1299", "arg"),
    ("param_1346", "prereq_struct"), ("param_1297", "sprite_off"),
    ("param_1298", "sprite_on"), ("param_1262", "description"),
]


def dump_invocations():
    text = (CONTENT / "configs" / "all.struct").read_text(encoding="cp1252",
                                                          errors="replace")
    rows = []
    for block in re.split(r"\n(?=\[)", text):
        m = re.match(r"\[struct_(\d+)\]", block)
        if not m:
            continue
        params = dict(re.findall(r"^param=(param_\d+),(?:int|str),(.*)$",
                                 block, re.M))
        if "param_1159" not in params or "param_1160" not in params:
            continue
        rows.append([int(params["param_1159"]), int(m.group(1)),
                     params["param_1160"]]
                    + [params.get(k, "") for k, _ in INVOCATION_PARAMS[1:]])
    rows.sort()
    head = ["index", "struct", "name"] + [n for _, n in INVOCATION_PARAMS[1:]]
    (OUT / "cache_invocations_toa.tsv").write_text(
        "# Tombs of Amascut invocations, decoded from cache.osrs239 all.struct.\n"
        "# struct params: 1159 index, 1160 name, 1161 category, 1162 raid-level,\n"
        "# 1299 numeric argument, 1346 prerequisite struct, 1297/1298 sprites,\n"
        "# 1262 in-game description.\n"
        + "\t".join(head) + "\n"
        + "\n".join("\t".join(str(c) for c in r) for r in rows) + "\n",
        encoding="utf-8")
    return len(rows)


print(f"invocations\t{dump_invocations()}")
