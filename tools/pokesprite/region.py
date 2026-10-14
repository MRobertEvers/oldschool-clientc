#!/usr/bin/env python3
"""Fill the PokeSprite store with what players actually asked for.

    tools/pokesprite/region.py --store assets/pokesprite --from-missing
    tools/pokesprite/region.py --store assets/pokesprite --squares 49:51,49:51

io_server records every sheet key it was asked for and lacked in
<store>/missing.db. This renders exactly those -- npc sequences through
pokesprite.py, player layers through layers.generate -- into the store, so
the next session finds them. Keys are grouped by subject so one npc's
sequences share one render setup.
"""

import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import layers  # noqa: E402
import locs  # noqa: E402
from store import Store  # noqa: E402


def parse(keys):
    npcs, layer_seqs, loc_keys, obj_keys = {}, {}, set(), set()
    for key in keys:
        parts = key.split("/")
        if parts[0] == "npc" and len(parts) == 3:
            npc, seq = int(parts[1]), int(parts[2])
            if seq > 0:
                npcs.setdefault(npc, set()).add(seq)
        elif parts[0] == "layer" and len(parts) == 5 and parts[1] in ("m", "f"):
            seq = int(parts[4])
            if seq > 0:
                layer_seqs.setdefault(parts[1], {}).setdefault("%s:%s" % (parts[2], parts[3]),
                                                               set()).add(seq)
        elif parts[0] == "obj" and len(parts) == 2:
            obj_keys.add(int(parts[1]))
        elif parts[0] == "loc" and len(parts) == 4:
            # An early client asked for a static loc as seq 0; it is -1.
            seq = int(parts[3])
            loc_keys.add((int(parts[1]), int(parts[2]), -1 if seq == 0 else seq))
        else:
            print("skip %s (no generator for it yet)" % key)
    return npcs, layer_seqs, loc_keys, obj_keys


CONTENT = os.path.join(os.path.dirname(os.path.dirname(HERE)), "OSRS-Content/osrs239-content")
DRAWN_LOC_SHAPES = (10, 11, 22)
# Player sequences a fresh character uses: stance, walk (forward/back/left/
# right), run.
PLAYER_SEQS = [808, 819, 820, 821, 822, 824]


def squares(spec):
    xs, zs = spec.split(",")
    x0, x1 = (int(v) for v in xs.split(":"))
    z0, z1 = (int(v) for v in zs.split(":"))
    return [(x, z) for x in range(x0, x1 + 1) for z in range(z0, z1 + 1)]


def region_npcs(sqs):
    """npc type ids spawned in the squares (the content .spawn files)."""
    import pokesprite as ps
    by_name = {v: k for k, v in ps.compack("npc").items()}
    ids = set()
    spawn_dir = os.path.join(CONTENT, "server/scripts/areas/world/configs")
    for x, z in sqs:
        p = os.path.join(spawn_dir, "m%d_%d.spawn" % (x, z))
        if not os.path.exists(p):
            continue
        section = None
        for line in open(p, encoding="utf8", errors="replace"):
            if line.startswith("===="):
                section = line.strip("= \n")
                continue
            parts = line.split()
            if section == "NPC" and parts and not line.startswith("//") and parts[0] in by_name:
                ids.add(by_name[parts[0]])
    return sorted(ids)


def region_locs(sqs):
    """(loc, shape, seq) for every drawn-shape loc the squares place (.jl2)."""
    import pokesprite as ps
    loc_names = ps.compack("loc")
    seq_ids = {v: k for k, v in ps.compack("seq").items()}
    anims = {}
    txt = open(os.path.join(CONTENT, "configs/all.loc"), "rb").read().decode("utf8", "replace")
    out = set()
    for x, z in sqs:
        p = os.path.join(CONTENT, "maps", "m%d_%d.jl2" % (x, z))
        if not os.path.exists(p):
            continue
        for line in open(p, encoding="utf8", errors="replace"):
            head, sep, tail = line.partition(":")
            if not sep:
                continue
            parts = tail.split()
            if len(parts) < 2 or not parts[0].isdigit():
                continue
            loc, shape = int(parts[0]), int(parts[1])
            if shape not in DRAWN_LOC_SHAPES:
                continue
            if loc not in anims:
                name = loc_names.get(loc)
                seq = -1
                if name:
                    i = txt.find("\n[%s]\n" % name)
                    j = txt.find("\n[", i + 2)
                    for row in txt[i:j].splitlines():
                        if row.startswith("anim=") and row[5:].strip() in seq_ids:
                            seq = seq_ids[row[5:].strip()]
                anims[loc] = seq
            out.add((loc, shape, anims[loc]))
    return sorted(out)


def default_kits(gender):
    """The first selectable identity kit per body part: a fresh character."""
    first = {}
    for kit in range(400):
        try:
            bp = layers.idk_bodypart(kit)
        except (AssertionError, ValueError):
            continue
        if (bp >= 7) != (gender == "f") or bp % 7 in first:
            continue
        first[bp % 7] = kit
    return [first[k] for k in sorted(first)]


# What a subject with nothing to draw fails with: no faces at all, or faces
# that never reach the screen. Those get an empty sheet; anything else is a
# real failure and stays a miss.
NOTHING_TO_DRAW = ("no drawable faces", "nothing visible in any frame")


def settle_empty(store, key, err):
    text = str(err)
    if any(m in text for m in NOTHING_TO_DRAW):
        st = Store(store)
        st.write_empty(key)
        st.close()
        return "empty (nothing to draw) -- stored as such"
    return "FAILED " + text.strip()[-200:]


def run_npc(npc, seqs, work, store, anims=None):
    args = [sys.executable, os.path.join(HERE, "pokesprite.py"), "--npc", str(npc),
            "--out", os.path.join(work, "npc_%d" % npc), "--store", store]
    if seqs:
        args += ["--anims", ",".join("s%d=%d" % (s, s) for s in sorted(seqs))]
    r = subprocess.run(args, capture_output=True, text=True)
    return "ok" if r.returncode == 0 else "FAILED: " + (r.stderr.strip().splitlines() or ["?"])[-1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--store", required=True)
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--from-missing", action="store_true")
    mode.add_argument("--squares", help="X0:X1,Z0:Z1 map squares, e.g. 49:51,49:51 (Lumbridge)")
    ap.add_argument("--work", default="build/poke/gen")
    ap.add_argument("--limit", type=int, default=0, help="at most this many subjects")
    ap.add_argument("--only", choices=("npcs", "locs", "players"),
                    help="with --squares: just this part, so parts can run in parallel")
    a = ap.parse_args()

    if a.squares:
        sqs = squares(a.squares)
        npcs = region_npcs(sqs)
        locs_todo = region_locs(sqs)
        print("squares %s: %d npc types, %d locs" % (a.squares, len(npcs), len(locs_todo)))
        for npc in (npcs if a.only in (None, "npcs") else []):
            print("npc %d: %s" % (npc, run_npc(npc, None, a.work, a.store)), flush=True)
        for loc, shape, seq in (locs_todo if a.only in (None, "locs") else []):
            # One bad loc (an invisible one, a model that will not build) must
            # not stop a region: report it and go on.
            try:
                locs.generate(loc, shape, seq, a.work, a.store)
                print("loc %d shape %d seq %d: ok" % (loc, shape, seq), flush=True)
            except (SystemExit, Exception) as e:
                print("loc %d shape %d seq %d: %s" % (loc, shape, seq, settle_empty(
                    a.store, "loc/%d/%d/%d" % (loc, shape, seq), e)), flush=True)
        for gender in (("m", "f") if a.only in (None, "players") else ()):
            todo = {"kit:%d" % k: PLAYER_SEQS for k in default_kits(gender)}
            n = layers.generate(todo, os.path.join(a.work, "layers_raw"), a.store, gender)
            print("default %s body: %d layer sheets" % (gender, n), flush=True)
        return

    st = Store(a.store)
    missing = st.missing()
    st.close()
    npcs, layer_seqs, loc_keys, obj_keys = parse(k for k, _ in missing)
    print("missing: %d keys -> %d npcs, %d player layers, %d locs" % (
        len(missing), len(npcs), sum(len(v) for v in layer_seqs.values()), len(loc_keys)))
    done = 0
    for npc, seqs in sorted(npcs.items()):
        if a.limit and done >= a.limit:
            break
        print("npc %d seqs %s: %s" % (npc, sorted(seqs), run_npc(npc, seqs, a.work, a.store)))
        done += 1
    for obj in sorted(obj_keys):
        try:
            locs.generate_obj(obj, a.work, a.store)
            print("obj %d: ok" % obj)
        except (SystemExit, Exception) as e:
            print("obj %d: FAILED %s" % (obj, str(e).strip()[-200:]))
    for loc, shape, seq in sorted(loc_keys):
        try:
            locs.generate(loc, shape, seq, a.work, a.store)
            print("loc %d shape %d seq %d: ok" % (loc, shape, seq))
        except (SystemExit, Exception) as e:
            print("loc %d shape %d seq %d: %s" % (loc, shape, seq, settle_empty(
                a.store, "loc/%d/%d/%d" % (loc, shape, seq), e)))
    for gender, by_layer in sorted(layer_seqs.items()):
        todo = {L: sorted(s) for L, s in sorted(by_layer.items())}
        n = layers.generate(todo, os.path.join(a.work, "layers_raw"), a.store, gender)
        print("player layers (%s): %d sheets stored" % (gender, n))


if __name__ == "__main__":
    main()
