#!/usr/bin/env python3
"""Patch the content tree to a newer cache, in place.

    tools/cache_upgrade.py --tree OSRS-Content/osrs-content \\
        --old-cache cache.osrs239 --old-rev osrs239 \\
        --new-cache cache.osrs241 --new-rev osrs241 \\
        --scratch <dir> --cachepack <bin>

There is ONE content tree. A new cache revision is patched into it by this
script; there is never a second copy per revision. The patch is built in a
scratch copy and written back file by file at the end, so sessions that keep
authoring in the tree while it runs lose nothing (step 11). What each step
protects, measured on 239 -> 241 (2026-10-20):

 1. snapshot       the tree -> <scratch>/snapshot and <scratch>/work (APFS
                   clones, mtimes kept).
 2. names          tools/cache_upgrade_names.py: a three-way merge per id of
                   every name table, base = raw old cache, theirs = raw new
                   cache. 868 names followed Jagex (518 of them `chatbox`, whose
                   children 565.. shifted one slot), 0 conflicts. Without it
                   every dialogue opened into `chatbox:567`, which 241 calls
                   `chatoverlay`.
 3. allocations    tools/alloc_rebase.py: server-allocated varps/params/enums
                   that the new cache's ids reach are moved above it, varp
                   names renamed with them, in the tree and in the repo's own
                   code (old->new: port/alloc_rebase_<rev>.map).
 4. lane ids       ported lanes' minted locs and underlays the new cache reaches
                   are re-minted above it, inside what the wire and map format
                   allow (port/lane_id_rebase_<rev>.map).
 5. unpack         cachepack unpack --cache <new> onto the work copy.
 6. CS2            the tree's scripts compile to the cache's exact bytecode
                   (9,725/9,725 in cache.osrs239.baked); what is authored in
                   them is their LOCAL NAMES (`$int2` -> `$dx_int2`). Unpack
                   rewrites every script it re-decompiles, so the names are
                   carried onto the fresh decompile by alignment against a raw
                   decompile of the old bytecode. A hand-kept `.cs2` where the
                   decompiler only manages `.cs2b` is kept when the bytecode did
                   not change (its names re-spelled by id); when it did, the
                   `.cs2b` stands and the old source is kept for review under
                   port/stale_cs2_<rev>/.
 7. removed        server fields of records the new cache deleted move to
                   port/cache_upgrade_removed_<rev>.<type>.
 8. var prefixes   the new vars get `varb<id>_` names, and the configs are
                   exported again so every value naming one uses the final name.
 9. authored data  config blocks and interface files that differ from a fresh
                   export of the OLD cache are authored (241: `type=string` on
                   varcs, server varbits 20414.., 11 interfaces). They are
                   carried when the new cache left the record alone; otherwise
                   logged as conflicts.
10. references     filler names the new cache named are followed in authored
                   files; membership lists gain the new records; authored full
                   records gain the new revision's keys.
11. apply          every file the patch changed is written into the tree where
                   the live file still matches the snapshot; one edited during
                   the run is kept and listed in port/cache_upgrade_kept_<rev>.tsv.

Every step logs into <tree>/port/cache_upgrade_<rev>.log.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TOK = re.compile(r"\$([A-Za-z_][A-Za-z0-9_]*)")
LOSSLESS = "// @rscache-lossless-v1 "
BLOCK = re.compile(r"^\[([^\]]+)\]\s*$")
PACK_LINE = re.compile(r"^(\d+)=(\S+)(.*)$")


class Log:
    def __init__(self) -> None:
        self.lines: list[str] = []

    def __call__(self, msg: str) -> None:
        print(msg, flush=True)
        self.lines.append(msg)


def run(cmd: list[str], log: Log, cwd: Path | None = None, out: Path | None = None) -> None:
    log("$ " + " ".join(str(c) for c in cmd))
    if out:
        with out.open("w") as fh:
            rc = subprocess.run([str(c) for c in cmd], cwd=cwd, stdout=fh, stderr=subprocess.STDOUT).returncode
    else:
        rc = subprocess.run([str(c) for c in cmd], cwd=cwd).returncode
    assert rc == 0, f"command failed ({rc}): {cmd}"


def raw_unpack(cachepack: Path, cache: Path, rev: str, dest: Path, log: Log) -> None:
    """A fresh unpack of one cache, cached in the scratch dir by the cache's size."""
    stamp = dest / ".source"
    key = f"{cache.resolve()} {(cache / 'main_file_cache.dat2').stat().st_size} {rev}"
    if stamp.exists() and stamp.read_text() == key:
        log(f"raw {rev}: reusing {dest}")
        return
    if dest.exists():
        shutil.rmtree(dest)
    run([cachepack, "unpack", "--cache", cache, "--rev", rev, "--src", dest, "--assets"], log,
        cwd=dest.parent, out=dest.parent / f"{dest.name}.log")
    stamp.write_text(key)


def read_pack(path: Path) -> dict[int, str]:
    out: dict[int, str] = {}
    if path.exists():
        for ln in path.read_text(encoding="utf-8", errors="replace").splitlines():
            m = PACK_LINE.match(ln)
            if m:
                out[int(m.group(1))] = m.group(2)
    return out


def strip_stamp(text: str) -> str:
    i = text.find(LOSSLESS)
    return text if i < 0 else text[:i]


def fnv1a64(data: bytes) -> int:
    h = 14695981039346656037
    for b in data:
        h ^= b
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def restamp(text: str) -> str:
    i = text.find(LOSSLESS)
    if i < 0:
        return text
    j = i + len(LOSSLESS)
    k = text.find(" ", j)
    return text[:j] + f"{fnv1a64(text[:i].encode()):016x}" + text[k:]


def local_map(curated: str, raw: str) -> dict[str, str] | None:
    """raw local -> curated local, paired by position.

    The two texts decompile the SAME bytecode, so their `$local` occurrences
    line up one for one; the text between them may differ only in how a
    global was spelled at the time (a name the tree has since given a model,
    say). A pairing that names one raw local two ways is refused."""
    c = TOK.findall(strip_stamp(curated))
    r = TOK.findall(strip_stamp(raw))
    if len(c) != len(r):
        return None
    m: dict[str, str] = {}
    for rn, cn in zip(r, c):
        if m.setdefault(rn, cn) != cn:
            return None
    if len(set(m.values())) != len(m):
        return None
    return {k: v for k, v in m.items() if k != v}


def apply_map(text: str, m: dict[str, str]) -> str | None:
    out = TOK.sub(lambda x: "$" + m.get(x.group(1), x.group(1)), text)
    names = set(TOK.findall(out))
    # Refuse a rename that would merge two distinct locals into one name.
    if len(names) != len(set(TOK.findall(text))):
        return None
    return out


def script_files(tree: Path) -> dict[int, Path]:
    """id -> source file (`.cs2`, else `.cs2b`) by the tree's clientscript pack."""
    names = read_pack(tree / "pack" / "12_clientscripts.pack")
    out = {}
    for ident, name in names.items():
        for ext in (".cs2", ".cs2b"):
            p = tree / "scripts" / f"{name}{ext}"
            if p.exists():
                out[ident] = p
                break
    return out


def same_bytes(a: Path, b: Path) -> bool:
    return a.exists() and b.exists() and a.read_bytes() == b.read_bytes()


def id_respell(x: Path, rev: str):
    """old spelling -> new, for every name the name merge changed, keyed the
    way a CS2 source writes it: `%varb4398_...` and `obj_33835` bare,
    `chatbox:chatmodal` qualified by its interface. One pass, so a chain
    (565->566->567) cannot cascade."""
    names_log = x / "port" / f"cache_upgrade_names_{rev}.tsv"
    ren: dict[str, str] = {}
    for ln in names_log.read_text(encoding="utf-8").splitlines()[2:]:
        f, _, old, new, _ = ln.split("\t")
        m = re.match(r"interfaces/(\w+)\.compack$", f)
        if m:
            ren[f"{m.group(1)}:{old}"] = f"{m.group(1)}:{new}"
        else:
            ren[old] = new
    if not ren:
        return lambda text: text
    rx = re.compile(r"(?<![A-Za-z0-9_])(" + "|".join(re.escape(k) for k in sorted(ren, key=len, reverse=True))
                    + r")(?![A-Za-z0-9_])")
    return lambda text: rx.sub(lambda m: ren[m.group(1)], text)


def step_cs2(old: Path, raw_old: Path, raw_new: Path, x: Path, rev: str, log: Log) -> None:
    # A hand-kept source is kept only where the bytecode did not change, so a
    # name in it means the same id as before; it is re-spelled with that id's
    # name now (241: `%varb4398_ge_newoffer_price` in three GE scripts).
    respell = id_respell(x, rev)
    # Unpack keeps a source whose bytecode did not change exactly as it was
    # (7,156 of 9,725 at 241), so its names are the OLD names; those, and only
    # those, are re-spelled by id. A fresh decompile already uses the new names,
    # and re-spelling one would cascade a shift (567 `chatmodal` -> 568).
    for src in (x / "scripts").glob("*.cs2"):
        before = old / "scripts" / src.name
        if before.exists() and same_bytes(src, before):
            t = src.read_bytes().decode("utf-8", errors="surrogateescape")
            nt = restamp(respell(t))
            if nt != t:
                src.write_bytes(nt.encode("utf-8", errors="surrogateescape"))
                log(f"cs2: {src.name} kept by unpack, names re-spelled by id")
    old_names = read_pack(old / "pack" / "12_clientscripts.pack")
    x_names = read_pack(x / "pack" / "12_clientscripts.pack")
    ro, rn = script_files(raw_old), script_files(raw_new)
    stale_dir = x / "port" / f"stale_cs2_{rev}"
    counts = {"renamed_exact": 0, "renamed_changed": 0, "no_locals": 0, "unaligned": 0,
              "kept_handwritten": 0, "stale_handwritten": 0, "gone": 0}
    review: list[str] = []
    for ident, name in sorted(old_names.items()):
        cur = old / "scripts" / f"{name}.cs2"
        if not cur.exists():
            continue
        xname = x_names.get(ident)
        if xname is None or ident not in rn:
            counts["gone"] += 1
            continue
        x_cs2, x_cs2b = x / "scripts" / f"{xname}.cs2", x / "scripts" / f"{xname}.cs2b"
        ro_file = ro.get(ident)
        if ro_file is None:
            continue
        # The decompiler cannot do this script; the tree carries a hand-kept source.
        if ro_file.suffix == ".cs2b":
            if same_bytes(ro_file, rn[ident]):
                if x_cs2b.exists():
                    x_cs2b.unlink()
                text = cur.read_bytes().decode("utf-8", errors="surrogateescape")
                x_cs2.write_bytes(restamp(respell(text)).encode("utf-8", errors="surrogateescape"))
                counts["kept_handwritten"] += 1
            else:
                stale_dir.mkdir(parents=True, exist_ok=True)
                shutil.copy2(cur, stale_dir / cur.name)
                if x_cs2.exists() and x_cs2b.exists():
                    x_cs2.unlink()
                counts["stale_handwritten"] += 1
                review.append(f"{ident}\t{name}\tbytecode changed; hand-kept source in {stale_dir.name}/")
            continue
        if not x_cs2.exists() or ident not in ro:
            continue
        m = local_map(cur.read_text(encoding="utf-8"), ro[ident].read_text(encoding="utf-8"))
        if m is None:
            counts["unaligned"] += 1
            review.append(f"{ident}\t{name}\tlocal names did not align; left as decompiled")
            continue
        if not m:
            counts["no_locals"] += 1
            continue
        text = x_cs2.read_text(encoding="utf-8")
        new = apply_map(text, m)
        if new is None:
            counts["unaligned"] += 1
            review.append(f"{ident}\t{name}\trename would merge locals; left as decompiled")
            continue
        x_cs2.write_text(restamp(new), encoding="utf-8")
        exact = same_bytes(ro[ident], rn[ident])
        counts["renamed_exact" if exact else "renamed_changed"] += 1
        if not exact:
            review.append(f"{ident}\t{name}\tbytecode changed; local names carried by slot, check them")
    log(f"cs2: {json.dumps(counts)}")
    (x / "port" / f"cache_upgrade_cs2_{rev}.tsv").write_text(
        "id\tscript\tnote\n" + "\n".join(review) + "\n", encoding="utf-8")


def blocks(path: Path) -> tuple[list[str], dict[str, str]]:
    order: list[str] = []
    out: dict[str, str] = {}
    name = None
    buf: list[str] = []
    if not path.exists():
        return order, out
    for ln in path.read_text(encoding="utf-8", errors="replace").splitlines(keepends=True):
        m = BLOCK.match(ln)
        if m:
            if name is not None:
                out[name] = "".join(buf)
            name = m.group(1)
            order.append(name)
            buf = []
        elif name is not None:
            buf.append(ln)
    if name is not None:
        out[name] = "".join(buf)
    return order, out


def step_authored_configs(old: Path, base: Path, x: Path, log: Log, conflicts: list[str]) -> None:
    """Carry authored config blocks: base = the OLD tree re-exported from the OLD cache."""
    for f in sorted((old / "configs").glob("all.*")):
        if f.suffix == ".compack":
            continue
        _, O = blocks(f)
        _, B = blocks(base / "configs" / f.name)
        xorder, X = blocks(x / "configs" / f.name)
        carried = added = 0
        for n, body in O.items():
            if n in B and B[n] == body:
                continue
            if n not in B:
                if n not in X:
                    X[n] = body
                    xorder.append(n)
                    added += 1
                continue
            if n in X and X[n] == B[n]:
                X[n] = body
                carried += 1
            elif n in X:
                conflicts.append(f"configs/{f.name}\t[{n}]\tauthored and changed by the new cache")
            else:
                conflicts.append(f"configs/{f.name}\t[{n}]\tauthored; the new cache renamed or removed it")
        if carried or added:
            text = "".join(f"[{n}]\n{X[n]}" for n in xorder)
            whole = (x / "configs" / f.name).read_text(encoding="utf-8")
            first = re.search(r"^\[", whole, re.M)
            head = whole[:first.start()] if first else whole
            (x / "configs" / f.name).write_text(head + text, encoding="utf-8")
            log(f"configs/{f.name}: {carried} authored blocks carried, {added} authored blocks added")


def step_authored_interfaces(old: Path, base: Path, raw_old: Path, raw_new: Path, x: Path,
                             renamed_ifaces: set[str], log: Log, conflicts: list[str]) -> None:
    def norm(s: str) -> str:
        return re.sub(r"\\ $", " ", s, flags=re.M)
    carried = 0
    for f in sorted((old / "interfaces").glob("*.if")):
        b = base / "interfaces" / f.name
        if not b.exists() or norm(f.read_text(encoding="utf-8", errors="replace")) == \
                norm(b.read_text(encoding="utf-8", errors="replace")):
            continue
        ro, rn = raw_old / "interfaces" / f.name, raw_new / "interfaces" / f.name
        if f.stem in renamed_ifaces or not same_bytes(ro, rn):
            conflicts.append(f"interfaces/{f.name}\tauthored and changed by the new cache")
            continue
        shutil.copy2(f, x / "interfaces" / f.name)
        carried += 1
    log(f"interfaces: {carried} authored files carried")


# What a ported lane mints in a CACHE-indexed namespace, per namespace: where a
# re-mint starts, the id ceiling the wire or map format allows, and how its
# map files spell the id.
LANE_IDS = {
    # Locs are u16 on the wire (LOC_ADD_CHANGE and friends); `.jl2` loc rows are
    # `x z level: id shape angle`.
    "loc": dict(cap=65535, map_glob="*.jl2", map_re=r"^(\S+ \S+ \S+: )(\d+)(?= )", map_offset=0),
    # A map tile's underlay is written as `id + 81` in a u16 opcode
    # (3rd/rscache/src/datatypes/maps.c); `.jm2` rows spell `u<id + 1>`.
    "underlay": dict(cap=65535 - 81, map_glob="*.jm2", map_re=r"( u)(\d+)(?=\b)", map_offset=1),
}


def step_lane_ids(x: Path, raw_new: Path, floors: dict[str, int], rev: str, log: Log) -> None:
    """Re-mint ported lanes' ids that the new cache now reaches, above it."""
    rows = []
    for ns, spec in LANE_IDS.items():
        cache_max = max(read_pack(raw_new / "configs" / f"all.{ns}.compack"), default=-1)
        nxt = floors[ns]
        assert nxt > cache_max, f"{ns}: lane floor {nxt} is not above the cache's {cache_max}"
        for lane in sorted((x / "ported").iterdir()):
            alloc = lane / "pack" / f"{ns}.alloc"
            if not alloc.exists():
                continue
            text = alloc.read_text(encoding="utf-8")
            ids = sorted(int(m.group(1)) for m in re.finditer(r"^(\d+)=", text, re.M))
            if not ids or min(ids) > cache_max:
                continue
            lo = min(ids)
            delta = nxt - lo
            assert max(ids) + delta <= spec["cap"], f"{lane.name}: {ns} ids would pass {spec['cap']}"
            mapping = {i: i + delta for i in ids}
            nxt = max(ids) + delta + 1
            alloc.write_text(re.sub(r"^(\d+)=", lambda m: f"{mapping[int(m.group(1))]}=", text, flags=re.M),
                             encoding="utf-8")
            off = spec["map_offset"]
            maps = (lane / "maps").glob(spec["map_glob"]) if (lane / "maps").is_dir() else []
            for mf in maps:
                t = mf.read_text(encoding="utf-8")
                t2 = re.sub(spec["map_re"],
                            lambda m: m.group(1) + str(mapping.get(int(m.group(2)) - off, int(m.group(2)) - off) + off),
                            t, flags=re.M)
                if t2 != t:
                    mf.write_text(t2, encoding="utf-8")
            # The import ledgers' destination column (kind, src id, src name, DEST id, ..., minted).
            for mp in (x / "port").glob("*.map"):
                t = mp.read_text(encoding="utf-8")
                t2 = re.sub(r"^(" + ns + r"\t[^\t]*\t[^\t]*\t)(\d+)(\t[^\t]*\tminted)",
                            lambda m: m.group(1) + str(mapping.get(int(m.group(2)), int(m.group(2)))) + m.group(3),
                            t, flags=re.M)
                if t2 != t:
                    mp.write_text(t2, encoding="utf-8")
            rows += [f"{lane.name}\t{ns}\t{o}\t{n}" for o, n in mapping.items()]
            log(f"lane {lane.name}: {ns} {lo}..{max(ids)} -> {lo + delta}..{max(ids) + delta}")
    (x / "port" / f"lane_id_rebase_{rev}.map").write_text(
        "lane\tkind\told_id\tnew_id\n" + "\n".join(rows) + "\n", encoding="utf-8")


def step_removed_records(x: Path, raw_old: Path, raw_new: Path, rev: str, log: Log) -> None:
    """Server fields stated for records the new cache deleted.

    241 deleted 51 Colosseum/Waves locs. The tree's authored server-field layer
    (`server/scripts/_explicit/configs/server_fields.generated.<type>`) still
    stated them, which cachepack reads as a record with no client half and
    refuses. Their blocks move out of the live file into
    port/cache_upgrade_removed_<rev>.<type>, kept for the record."""
    for gen in sorted((x / "server" / "scripts" / "_explicit" / "configs").glob("server_fields.generated.*")):
        kind = gen.suffix[1:]
        old_ids = read_pack(raw_old / "configs" / f"all.{kind}.compack")
        new_ids = read_pack(raw_new / "configs" / f"all.{kind}.compack")
        if not old_ids or not new_ids:
            continue
        tree_names = read_pack(x / "configs" / f"all.{kind}.compack")
        gone = {tree_names[i] for i in old_ids if i not in new_ids and i in tree_names}
        if not gone:
            continue
        text = gen.read_text(encoding="utf-8")
        parts = re.split(r"(?m)^(?=\[)", text)
        keep, moved = [], []
        for part in parts:
            m = BLOCK.match(part.split("\n", 1)[0])
            (moved if m and m.group(1) in gone else keep).append(part)
        if not moved:
            continue
        gen.write_text("".join(keep), encoding="utf-8")
        out = x / "port" / f"cache_upgrade_removed_{rev}.{kind}"
        out.write_text(f"// Server fields of {kind} records cache {rev} deleted, moved here from\n"
                       f"// {gen.relative_to(x)} by tools/cache_upgrade.py.\n\n" + "".join(moved),
                       encoding="utf-8")
        log(f"{gen.name}: {len(moved)} blocks for deleted records moved to {out.name}")


def step_membership(x: Path, raw_old: Path, raw_new: Path, rev: str, log: Log) -> None:
    """Keep `pack/<ns>.server` membership whole across the upgrade.

    In the old tree `loc.server`, `npc.server` and `varp.server` name every cache
    record of their type (62194/62194, 16292/16292, 5705/5705 at 239), so a
    record the new cache adds gets its line too -- cachepack refuses a record
    that states a server field its side does not receive (241: the new
    `mad_angel` npcs and their attackrate). A name the name merge changed
    (port/cache_upgrade_names_<rev>.tsv) is renamed on its line."""
    renames: dict[str, dict[str, str]] = {}
    log_file = x / "port" / f"cache_upgrade_names_{rev}.tsv"
    if log_file.exists():
        for ln in log_file.read_text(encoding="utf-8").splitlines()[2:]:
            f, _, old, new, _ = ln.split("\t")
            m = re.match(r"configs/all\.(\w+)\.compack$", f)
            if m:
                renames.setdefault(m.group(1), {})[old] = new
    for ns in ("loc", "npc", "varp"):
        member = x / "pack" / f"{ns}.server"
        if not member.exists():
            continue
        old_ids = read_pack(raw_old / "configs" / f"all.{ns}.compack")
        new_ids = read_pack(raw_new / "configs" / f"all.{ns}.compack")
        names = read_pack(x / "configs" / f"all.{ns}.compack")
        lines = member.read_text(encoding="utf-8").splitlines(keepends=True)
        ren = renames.get(ns, {})
        lines = [ren[l.strip()] + "\n" if l.strip() in ren else l for l in lines]
        listed = {l.strip() for l in lines}
        added = [names[i] for i in sorted(new_ids) if i not in old_ids and i in names and names[i] not in listed]
        if added:
            lines.append(f"\n// Records cache {rev} added (tools/cache_upgrade.py).\n")
            lines += [n + "\n" for n in added]
        member.write_text("".join(lines), encoding="utf-8")
        log(f"pack/{ns}.server: {len(added)} new records, {sum(1 for o in ren)} renamed")


FILLER = re.compile(r"^(?:(?:varb|varp|varc)\d+_)?[a-z0-9]+_\d+$")


def step_removed_scripts(x: Path, raw_old: Path, raw_new: Path, rev: str, log: Log) -> None:
    """Clientscripts the new cache deleted leave the live tree.

    Unpack does not delete a source whose id the new cache dropped, and the
    packer then writes the old script back into the new cache (241:
    `prayer_isavailable` 464, which calls into procs 241 changed, and fails the
    link). The source moves to port/removed_cs2_<rev>/ and its pack line to
    port/removed_cs2_<rev>.pack, kept for the record."""
    gone = set(read_pack(raw_old / "pack" / "12_clientscripts.pack")) - set(read_pack(raw_new / "pack" / "12_clientscripts.pack"))
    if not gone:
        return
    pack = x / "pack" / "12_clientscripts.pack"
    lines = pack.read_text(encoding="utf-8").splitlines(keepends=True)
    keep, moved = [], []
    for ln in lines:
        m = PACK_LINE.match(ln.rstrip("\n"))
        (moved if m and int(m.group(1)) in gone else keep).append(ln)
    dest = x / "port" / f"removed_cs2_{rev}"
    dest.mkdir(parents=True, exist_ok=True)
    for ln in moved:
        name = PACK_LINE.match(ln.rstrip("\n")).group(2)
        for ext in (".cs2", ".cs2b"):
            src = x / "scripts" / f"{name}{ext}"
            if src.exists():
                os.replace(src, dest / src.name)
    pack.write_text("".join(keep), encoding="utf-8")
    (x / "port" / f"removed_cs2_{rev}.pack").write_text(
        f"// Clientscripts cache {rev} deleted, moved out of pack/12_clientscripts.pack.\n" + "".join(moved),
        encoding="utf-8")
    log(f"clientscripts the new cache deleted: {len(moved)} moved to port/removed_cs2_{rev}/")


def step_register_floors(x: Path, shifts: list[str], rev: str, log: Log) -> None:
    """The moved allocations' floors go into the tree's content.ini, so the
    next server-allocated id starts above the new cache too (ss_allocate.py
    reads `base =` from here before the C defaults)."""
    path = x / "content.ini"
    text = path.read_text(encoding="utf-8")
    for spec in shifts:
        ns, floor = spec.split("=", 1)
        head = f"[namespace:{ns}]\n"
        i = text.find(head)
        if i < 0:
            text += f"\n{head}base       = {floor}\n"
            continue
        j = text.find("\n[", i + len(head))
        block = text[i:j if j >= 0 else len(text)]
        if re.search(r"(?m)^base\s*=", block):
            new_block = re.sub(r"(?m)^base\s*=.*$", f"base       = {floor}", block)
        else:
            new_block = block.rstrip("\n") + f"\nbase       = {floor}\n"
        text = text[:i] + new_block + text[i + len(block):]
    path.write_text(text, encoding="utf-8")
    log(f"content.ini floors: {', '.join(shifts)}")


def step_rename_refs(x: Path, rev: str, log: Log) -> None:
    """References to a FILLER name the new cache gave a real name follow the id.

    `varp_448` and `follower_obj` are one variable (241 named it); an authored
    block or membership line still saying `varp448_varp_448` is the same varp
    under its old spelling, and cachepack reads it as a second record. A real
    name that changed (`mouseover_text_enabled` -> `aerial_fishing_whole_fish_
    toggle`, a reused id) is NOT followed: a reference to it then fails to
    resolve, which is the loud outcome a reused id needs."""
    names_log = x / "port" / f"cache_upgrade_names_{rev}.tsv"
    ren = {}
    for ln in names_log.read_text(encoding="utf-8").splitlines()[2:]:
        f, _, old, new, _ = ln.split("\t")
        if f.startswith("configs/") and FILLER.match(old):
            ren[old] = new
    if not ren:
        return
    token = re.compile(r"\b(" + "|".join(re.escape(k) for k in sorted(ren, key=len, reverse=True)) + r")\b")
    touched = 0
    patterns = x / "port" / f".rename_refs_{rev}.tmp"
    patterns.write_text("\n".join(sorted(ren)) + "\n", encoding="utf-8")
    for root in ("server", "pack", "ported"):
        # Fixed strings as the cheap filter; the token regex below is exact.
        out = subprocess.run(["grep", "-rlIF", "-f", str(patterns), root,
                              "--exclude-dir=build", "--exclude-dir=wiki"],
                             cwd=x, capture_output=True, text=True, env={**os.environ, "LC_ALL": "C"}).stdout
        for rel in out.splitlines():
            f = x / rel
            text = f.read_bytes().decode("utf-8", errors="surrogateescape")
            new = token.sub(lambda m: ren[m.group(1)], text)
            if new != text:
                tmp = f.with_name(f.name + ".refs.tmp")
                tmp.write_bytes(new.encode("utf-8", errors="surrogateescape"))
                os.replace(tmp, f)
                touched += 1
    patterns.unlink()
    log(f"filler names the new cache named: {len(ren)}; references renamed in {touched} file(s)")


def new_keys(raw_old: Path, raw_new: Path, kind: str) -> list[str]:
    """Keys every record of `kind` states in the new export and none did in the
    old: what a revision added to the type (241 obj: flag251, seqs161)."""
    def keys(p: Path) -> list[str]:
        out: list[str] = []
        if not p.exists():
            return out
        with p.open(encoding="utf-8", errors="surrogateescape") as fh:
            started = False
            for ln in fh:
                if BLOCK.match(ln):
                    if started:
                        break
                    started = True
                    continue
                if started and "=" in ln:
                    out.append(ln.split("=", 1)[0])
        return out
    old = set(keys(raw_old / "configs" / f"all.{kind}"))
    return [k for k in keys(raw_new / "configs" / f"all.{kind}") if k not in old]


def step_authored_new_keys(x: Path, raw_old: Path, raw_new: Path, log: Log) -> None:
    """An authored FULL record (one the tree mints, like `herb_tar` 49000)
    states every key of its type, so it must state the keys the new revision
    added too -- cachepack refuses it otherwise. They go in as `key=default`,
    after the line the export writes them after."""
    for kind, anchor in (("obj", "getradeable"),):
        added = new_keys(raw_old, raw_new, kind)
        if not added:
            continue
        files = [p for root in (x / "server" / "scripts", x / "ported") for p in root.rglob(f"*.{kind}")]
        total = 0
        for f in files:
            raw = f.read_bytes()
            text = raw.decode("utf-8", errors="surrogateescape")
            out, n = [], 0
            for part in re.split(r"(?m)^(?=\[)", text):
                lines = part.splitlines(keepends=True)
                has = {ln.split("=", 1)[0] for ln in lines if "=" in ln}
                if f"{anchor}" in has and any(k not in has for k in added):
                    i = next(j for j, ln in enumerate(lines) if ln.startswith(f"{anchor}="))
                    lines[i + 1:i + 1] = [f"{k}=default\n" for k in added if k not in has]
                    n += 1
                out.append("".join(lines))
            if n:
                tmp = f.with_name(f.name + ".keys.tmp")
                tmp.write_bytes("".join(out).encode("utf-8", errors="surrogateescape"))
                os.replace(tmp, f)
                total += n
        log(f"{kind}: {total} authored full record(s) given {', '.join(added)}")


def step_var_prefix(x: Path, log: Log) -> None:
    sys.path.insert(0, str(REPO / "OSRS-Content" / "tools"))
    import var_prefix_names as v  # noqa: E402
    v.ROOT = x
    v.REPO = x.parent
    rel_root = x.name
    orig = v.Rewriter.table_for

    def table_for(self, rel):
        parts = rel.parts
        if len(parts) > 2 and parts[0] == rel_root and parts[1] == "ported":
            return self.lanes.get(parts[2], self.main)
        return orig(self, rel)
    v.Rewriter.table_for = table_for
    skip = ["models", "animsets", "synth", "samples", "sprites", "maps", "framemaps", "animayas",
            "songs", "jingles", "patches", "worldmap", "textures", "fonts", "binary", "defaults",
            "wiki", "build"]
    # LC_ALL=C: under a UTF-8 locale grep -I calls a file with cache-charset
    # bytes "binary" and drops it; only a NUL makes a file binary here.
    out = subprocess.run(["grep", "-rlI", "-E", ".", rel_root] + [f"--exclude-dir={d}" for d in skip],
                         cwd=x.parent, capture_output=True, text=True,
                         env={**os.environ, "LC_ALL": "C"}).stdout
    v.tracked_files = lambda: [Path(f) for f in out.splitlines()]
    saved = sys.argv
    sys.argv = ["var_prefix_names.py", "--write"]
    try:
        v.main()
    finally:
        sys.argv = saved
    # var_prefix_names.py reads strict UTF-8 and skips a file that is not --
    # and configs/all.npc carries cache-charset names (`Le-sabr\xe8`), so the
    # new cache's `multivarbit=` values there went unprefixed and the server
    # pack refused 142 npcs. Those files again, byte for byte: surrogateescape
    # round-trips every byte, the inserted names are ASCII, and the new text is
    # written whole to a temp file then moved over the old one.
    rw = v.Rewriter()
    fixed = 0
    for rel in v.tracked_files():
        path = x.parent / rel
        if path.suffix in v.SKIP_SUFFIXES or "wiki" in rel.parts:
            continue
        raw = path.read_bytes()
        try:
            raw.decode("utf-8")
            continue
        except UnicodeDecodeError:
            pass
        text = raw.decode("utf-8", errors="surrogateescape")
        new, n = rw.rewrite(rel, text)
        if n and new != text:
            tmp_path = path.with_name(path.name + ".prefix.tmp")
            tmp_path.write_bytes(new.encode("utf-8", errors="surrogateescape"))
            os.replace(tmp_path, path)
            fixed += 1
    log(f"var prefixes applied ({fixed} non-UTF-8 file(s) done byte-exact)")


def same_file(a: Path, b: Path) -> bool:
    sa, sb = a.stat(), b.stat()
    if sa.st_size != sb.st_size:
        return False
    if sa.st_mtime_ns == sb.st_mtime_ns:
        return True
    with a.open("rb") as fa, b.open("rb") as fb:
        while True:
            ca, cb = fa.read(1 << 20), fb.read(1 << 20)
            if ca != cb:
                return False
            if not ca:
                return True


def walk_files(root: Path) -> set[str]:
    out: set[str] = set()
    for d, dirs, files in os.walk(root):
        rel = os.path.relpath(d, root)
        # A script pack is build output, rebuilt from the sources.
        dirs[:] = [x for x in dirs if not (rel.startswith("server/scripts") and x.startswith("build"))]
        for f in files:
            out.add(os.path.normpath(os.path.join(rel, f)))
    return out


def apply_in_place(tree: Path, snap: Path, work: Path, rev: str, log: Log) -> None:
    """Write the patch into the live tree, file by file.

    Other sessions keep editing the tree while the patch is built. A file the
    patch changed is written only where the live file still matches the
    snapshot; one edited meanwhile is left as it is and listed in
    port/cache_upgrade_kept_<rev>.tsv, to be patched by hand or by a re-run."""
    changed = added = removed = 0
    kept: list[str] = []
    for rel in sorted(walk_files(snap) | walk_files(work)):
        s, w, t = snap / rel, work / rel, tree / rel
        if s.exists() and w.exists() and same_file(s, w):
            continue
        live_matches = (not s.exists() and not t.exists()) or (s.exists() and t.exists() and same_file(s, t))
        if not live_matches:
            kept.append(rel)
            continue
        if w.exists():
            t.parent.mkdir(parents=True, exist_ok=True)
            part = t.with_name(t.name + ".upgrade.tmp")
            shutil.copy2(w, part)
            os.replace(part, t)
            if s.exists():
                changed += 1
            else:
                added += 1
        else:
            t.unlink()
            removed += 1
    (work / "port" / f"cache_upgrade_kept_{rev}.tsv").write_text(
        "# Patched by the upgrade but edited in the tree while it ran; left as edited.\n"
        + "\n".join(kept) + "\n", encoding="utf-8")
    shutil.copy2(work / "port" / f"cache_upgrade_kept_{rev}.tsv", tree / "port" / f"cache_upgrade_kept_{rev}.tsv")
    log(f"applied in place: {changed} changed, {added} added, {removed} removed, {len(kept)} kept as edited")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tree", required=True, type=Path,
                    help="the content tree, patched in place (OSRS-Content/osrs-content)")
    ap.add_argument("--old-cache", required=True, type=Path)
    ap.add_argument("--old-rev", required=True)
    ap.add_argument("--new-cache", required=True, type=Path)
    ap.add_argument("--new-rev", required=True)
    ap.add_argument("--scratch", required=True, type=Path)
    ap.add_argument("--cachepack", required=True, type=Path)
    ap.add_argument("--shift", action="append", default=["varp=6400", "param=3000", "enum=7000"])
    ap.add_argument("--lane-floor", action="append", default=["varp=9000", "param=3500", "enum=7500"])
    ap.add_argument("--lane-loc-floor", type=int, default=64400)
    ap.add_argument("--lane-underlay-floor", type=int, default=1100)
    args = ap.parse_args()

    log = Log()
    args.old_cache = args.old_cache.resolve()
    args.new_cache = args.new_cache.resolve()
    tree = args.tree.resolve()
    scratch = args.scratch.resolve()
    scratch.mkdir(parents=True, exist_ok=True)
    cp = args.cachepack.resolve()
    rev = args.new_rev.replace("osrs", "")

    raw_old, raw_new, base = scratch / f"raw_{args.old_rev}", scratch / f"raw_{args.new_rev}", scratch / "base_old"
    raw_unpack(cp, args.old_cache, args.old_rev, raw_old, log)
    raw_unpack(cp, args.new_cache, args.new_rev, raw_new, log)

    # The tree as it stands (the old revision), the copy the patch is built in,
    # and the old tree re-exported from the old cache. `-p` keeps mtimes, so a
    # file nothing touched still matches its snapshot by stat alone.
    old, tmp = scratch / "snapshot", scratch / "work"
    for d in (old, tmp, base):
        if d.exists():
            shutil.rmtree(d)
    run(["cp", "-c", "-p", "-R", tree, old], log)
    run(["cp", "-c", "-p", "-R", old, tmp], log)
    run(["cp", "-c", "-R", old, base], log)
    # The base: the old tree re-exported from the old cache. A file that differs
    # from it in the old tree is authored.
    base_proc = subprocess.Popen([str(cp), "unpack", "--cache", str(args.old_cache.resolve()), "--rev",
                                  args.old_rev, "--src", str(base), "--assets"], cwd=scratch,
                                 stdout=open(scratch / "base_old.log", "w"), stderr=subprocess.STDOUT)

    names_log = tmp / "port" / f"cache_upgrade_names_{rev}.tsv"
    run([sys.executable, REPO / "tools" / "cache_upgrade_names.py", "--tree", tmp, "--base", raw_old,
         "--theirs", raw_new, "--log", names_log, "--write"], log)
    renamed_ifaces = set()
    for ln in names_log.read_text(encoding="utf-8").splitlines()[2:]:
        f = ln.split("\t")[0]
        if f.startswith("interfaces/"):
            renamed_ifaces.add(Path(f).stem)

    shift = []
    for s in args.shift:
        shift += ["--shift", s]
    for s in args.lane_floor:
        shift += ["--lane-floor", s]
    # The new cache's reach per server namespace. A varp's includes the varps
    # its varbits are packed into, which the varp group need not state.
    varp_ids = read_pack(raw_new / "configs" / "all.varp.compack")
    by_name = {v: k for k, v in varp_ids.items()}
    varp_max = max(varp_ids)
    for m in re.finditer(r"^basevar=(\S+)", (raw_new / "configs" / "all.varbit").read_text(
            encoding="utf-8", errors="replace"), re.M):
        name = m.group(1)
        filler = re.fullmatch(r"varp_(\d+)", name)
        ident = by_name.get(name, int(filler.group(1)) if filler else -1)
        varp_max = max(varp_max, ident)
    log(f"new cache reaches varp {varp_max}")
    shift += ["--cache-max", f"varp={varp_max}",
              "--cache-max", f"param={max(read_pack(raw_new / 'configs' / 'all.param.compack'))}",
              "--cache-max", f"enum={max(read_pack(raw_new / 'configs' / 'all.enum.compack'))}"]
    roots = []
    for r in [tmp] + [REPO / d for d in ("src", "script", "tools", "test")]:
        roots += ["--rename-root", r]
    run([sys.executable, REPO / "tools" / "alloc_rebase.py", "--tree", tmp, *shift, *roots,
         "--map", tmp / "port" / f"alloc_rebase_{rev}.map", "--write"], log)

    step_register_floors(tmp, args.shift, rev, log)
    step_lane_ids(tmp, raw_new, {"loc": args.lane_loc_floor, "underlay": args.lane_underlay_floor}, rev, log)

    run([cp, "unpack", "--cache", args.new_cache.resolve(), "--rev", args.new_rev, "--src", tmp, "--assets"],
        log, cwd=scratch, out=scratch / "unpack_new.log")

    step_cs2(old, raw_old, raw_new, tmp, rev, log)
    step_removed_records(tmp, raw_old, raw_new, rev, log)
    step_removed_scripts(tmp, raw_old, raw_new, rev, log)

    # The new vars get their `varb<id>_` prefix in the name tables, and the
    # configs are then exported AGAIN so every value that names one (a dbrow's
    # varp column, a multivarbit, a basevar) is written with the final name --
    # the first export wrote the raw spelling (241: `bossvarp,total_mad_angel_
    # kills`, an unknown varp to the packer). Configs only; the assets stand.
    step_var_prefix(tmp, log)
    run([cp, "unpack", "--cache", args.new_cache.resolve(), "--rev", args.new_rev, "--src", tmp],
        log, cwd=scratch, out=scratch / "unpack_new_configs.log")
    step_var_prefix(tmp, log)

    # After the last export: an export rewrites configs/all.* whole.
    rc = base_proc.wait()
    assert rc == 0, "re-export of the old tree failed"
    conflicts: list[str] = []
    step_authored_configs(old, base, tmp, log, conflicts)
    step_authored_interfaces(old, base, raw_old, raw_new, tmp, renamed_ifaces, log, conflicts)
    (tmp / "port" / f"cache_upgrade_conflicts_{rev}.tsv").write_text(
        "what\tdetail\n" + "\n".join(conflicts) + "\n", encoding="utf-8")
    log(f"conflicts: {len(conflicts)}")

    step_rename_refs(tmp, rev, log)
    step_membership(tmp, raw_old, raw_new, rev, log)
    step_authored_new_keys(tmp, raw_old, raw_new, log)

    apply_in_place(tree, old, tmp, rev, log)

    # Derived files that state the tree's own path or ids, regenerated for it.
    run([sys.executable, REPO / "tools" / "generate_godwars_combat_manifest.py"], log, cwd=REPO)
    (tree / "port" / f"cache_upgrade_{rev}.log").write_text("\n".join(log.lines) + "\n", encoding="utf-8")
    print(f"done: {tree}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
