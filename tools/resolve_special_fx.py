#!/usr/bin/env python3
"""
resolve_special_fx — translate a Kronos special-attack Java file's numeric
animate()/graphics()/projectile()/sound() ids into this tree's own gameval
names, the same way pvm_dragon_claws.rs2's header was built by hand.

    tools/resolve_special_fx.py --kronos <path/to/Weapon.java>
    tools/resolve_special_fx.py --seq 7514 --spotanim 1171 --sound 2537

Method (never hand-copy a Kronos integer past this):
  1. rsmod .data/symbols/{seq,spotanim,projanim,synth}.sym — id -> name,
     12,000+ rows, keyed by the same gameval names this tree uses.
  2. RuneLite runelite-api/.../gameval/{AnimationID,SpotanimID}.java — the
     independent second opinion on the same id. No public SoundEffectID
     gameval file exists in this RuneLite checkout, so sound ids are
     rsmod-only (still safe: pack/4_soundeffects.pack is N=synth_N for every
     id, so the *number* is the name and this only reports the rsmod label
     for a human-readable cross-check, per WEAPON_FX_PORT_QUEUE.md §0.3).
  3. Existence in this tree: configs/all.seq / all.spotanim / the synth
     range, so a name that resolves upstream but was never baked into this
     cache is caught here rather than at `ToriRSServer_Pack --check-only` time.
"""
from __future__ import annotations

import argparse
import os
import re
import sys

RSMOD = os.path.expanduser("~/Documents/git_repos/rsmod/.data/symbols")
RUNELITE_GAMEVAL = os.path.expanduser(
    "~/Documents/git_repos/runelite/runelite-api/src/main/java/net/runelite/api/gameval"
)
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(REPO, "OSRS-Content", "osrs239-content")

SYM_FILES = {
    "seq": os.path.join(RSMOD, "seq.sym"),
    "spotanim": os.path.join(RSMOD, "spotanim.sym"),
    "projanim": os.path.join(RSMOD, "projanim.sym"),
    "synth": os.path.join(RSMOD, "synth.sym"),
}

RUNELITE_FILES = {
    "seq": os.path.join(RUNELITE_GAMEVAL, "AnimationID.java"),
    "spotanim": os.path.join(RUNELITE_GAMEVAL, "SpotanimID.java"),
}

CACHE_FILES = {
    "seq": os.path.join(CONTENT, "configs", "all.seq"),
    "spotanim": os.path.join(CONTENT, "configs", "all.spotanim"),
}


def load_sym(kind: str) -> dict[int, str]:
    path = SYM_FILES[kind]
    out = {}
    if not os.path.exists(path):
        return out
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or "\t" not in line:
                continue
            id_s, name = line.split("\t", 1)
            try:
                out[int(id_s)] = name
            except ValueError:
                continue
    return out


def load_runelite(kind: str) -> dict[int, list[str]]:
    path = RUNELITE_FILES.get(kind)
    out: dict[int, list[str]] = {}
    if not path or not os.path.exists(path):
        return out
    pat = re.compile(r"public static final int (\w+) = (\d+);")
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = pat.search(line)
            if m:
                out.setdefault(int(m.group(2)), []).append(m.group(1))
    return out


def load_cache_names(kind: str) -> set[str]:
    path = CACHE_FILES.get(kind)
    out = set()
    if not path or not os.path.exists(path):
        return out
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("[") and line.endswith("]"):
                out.add(line[1:-1])
    return out


_SYM_CACHE: dict[str, dict[int, str]] = {}
_RL_CACHE: dict[str, dict[int, list[str]]] = {}
_CACHE_NAMES_CACHE: dict[str, set[str]] = {}


def resolve(kind: str, id_: int) -> dict:
    if kind not in _SYM_CACHE:
        _SYM_CACHE[kind] = load_sym(kind)
        _RL_CACHE[kind] = load_runelite(kind)
        _CACHE_NAMES_CACHE[kind] = load_cache_names(kind)

    sym_name = _SYM_CACHE[kind].get(id_)
    rl_names = _RL_CACHE[kind].get(id_, [])
    rl_name = None
    if rl_names:
        # RuneLite constants are SCREAMING_SNAKE; sym names are snake_case.
        # Agreement check is case/underscore-insensitive.
        rl_name = rl_names[0]

    agree = None
    if sym_name and rl_name:
        agree = sym_name.replace("_", "").lower() == rl_name.replace("_", "").lower()

    exists_here = None
    if kind in _CACHE_NAMES_CACHE and sym_name:
        names = _CACHE_NAMES_CACHE[kind]
        exists_here = sym_name in names if names else None

    return {
        "id": id_,
        "kind": kind,
        "sym_name": sym_name,
        "runelite_names": rl_names,
        "agree": agree,
        "exists_in_tree": exists_here,
    }


def load_local_synth_names() -> dict[int, str]:
    """`pack/4_soundeffects.pack` is THIS tree's own authority and takes
    priority over rsmod's synth.sym — most ids there are unnamed
    (`N=synth_N`), but a real minority carry actual names (e.g. `2537=
    puncture`), and missing that cost a wrong `synth_2537` substitution for
    six already-correctly-named sounds on 2026-08-13. Always check here
    first."""
    path = os.path.join(CONTENT, "pack", "4_soundeffects.pack")
    out: dict[int, str] = {}
    if not os.path.exists(path):
        return out
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if "=" not in line:
                continue
            id_s, name = line.split("=", 1)
            try:
                out[int(id_s)] = name
            except ValueError:
                continue
    return out


_LOCAL_SYNTH_CACHE: dict[int, str] | None = None


def resolve_synth(id_: int) -> dict:
    global _LOCAL_SYNTH_CACHE
    if _LOCAL_SYNTH_CACHE is None:
        _LOCAL_SYNTH_CACHE = load_local_synth_names()
    local_name = _LOCAL_SYNTH_CACHE.get(id_)
    if "synth" not in _SYM_CACHE:
        _SYM_CACHE["synth"] = load_sym("synth")
    sym_name = _SYM_CACHE["synth"].get(id_)
    # THIS tree's own pack/4_soundeffects.pack wins when it names the id
    # (whether that name is a real word or its own `synth_N` fallback);
    # rsmod's synth.sym is only a secondary cross-check, not the source.
    tree_name = local_name if local_name is not None else f"synth_{id_}"
    return {
        "id": id_,
        "kind": "synth",
        "sym_name": sym_name,
        "local_name": local_name,
        "tree_name": tree_name,
    }


KRONOS_CALL_RE = re.compile(
    r"\b(?:player|target|npc|c|caster)\.(animate|graphics|gfx|sound|publicSound|projectile)\s*\(([^)]*)\)"
)


def scan_kronos(path: str) -> list[tuple[str, list[str]]]:
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    calls = []
    for m in KRONOS_CALL_RE.finditer(text):
        method = m.group(1)
        args = [a.strip() for a in m.group(2).split(",")]
        calls.append((method, args))
    return calls


def cmd_kronos(path: str) -> None:
    print(f"== {path}")
    calls = scan_kronos(path)
    if not calls:
        print("  no animate()/graphics()/sound()/projectile() calls found (parser miss or purely numeric formula file)")
    for method, args in calls:
        first = args[0] if args else ""
        if not re.fullmatch(r"\d+", first):
            print(f"  {method}({', '.join(args)})  -- non-literal first arg, resolve by hand")
            continue
        id_ = int(first)
        if method == "animate":
            r = resolve("seq", id_)
            print(f"  animate({id_}) -> sym={r['sym_name']} runelite={r['runelite_names']} agree={r['agree']} in_tree={r['exists_in_tree']}")
        elif method in ("graphics", "gfx"):
            r = resolve("spotanim", id_)
            print(f"  {method}({', '.join(args)}) -> sym={r['sym_name']} runelite={r['runelite_names']} agree={r['agree']} in_tree={r['exists_in_tree']}")
        elif method in ("sound", "publicSound"):
            r = resolve_synth(id_)
            print(f"  {method}({', '.join(args)}) -> {r['tree_name']} (rsmod label: {r['sym_name']})")
        elif method == "projectile":
            r = resolve("projanim", id_) if False else None
            print(f"  projectile({', '.join(args)})  -- first arg {id_} is usually a gfx id, resolve as spotanim/projanim by hand")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--kronos", help="scan a Kronos special-attack .java file for animate/graphics/sound calls")
    ap.add_argument("--seq", type=int, help="resolve one seq id")
    ap.add_argument("--spotanim", type=int, help="resolve one spotanim id")
    ap.add_argument("--synth", type=int, help="resolve one synth id")
    args = ap.parse_args()

    if args.kronos:
        cmd_kronos(args.kronos)
    if args.seq is not None:
        print(resolve("seq", args.seq))
    if args.spotanim is not None:
        print(resolve("spotanim", args.spotanim))
    if args.synth is not None:
        print(resolve_synth(args.synth))
    if not (args.kronos or args.seq is not None or args.spotanim is not None or args.synth is not None):
        ap.print_help()


if __name__ == "__main__":
    main()
