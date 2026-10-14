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
import pokesprite as ps  # noqa: E402
import locs  # noqa: E402
from store import Store  # noqa: E402


def parse(keys):
    npcs, layer_seqs, loc_keys, obj_keys, spot_keys = {}, {}, set(), set(), set()
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
        elif parts[0] == "spot" and len(parts) == 2:
            spot_keys.add(int(parts[1]))
        elif parts[0] == "loc" and len(parts) == 4:
            # An early client asked for a static loc as seq 0; it is -1.
            seq = int(parts[3])
            loc_keys.add((int(parts[1]), int(parts[2]), -1 if seq == 0 else seq))
        else:
            print("skip %s (no generator for it yet)" % key)
    return npcs, layer_seqs, loc_keys, obj_keys, spot_keys


CONTENT = os.path.join(os.path.dirname(os.path.dirname(HERE)), "OSRS-Content/osrs239-content")
# Walls (0 straight, 1 diagonal corner, 3 post, 9 diagonal; 2, the L corner,
# is drawn from its straight wall at two rotations), centrepieces, ground decor.
DRAWN_LOC_SHAPES = (0, 1, 2, 3, 9, 10, 11, 22)
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


_LOC = {}


def loc_config(loc):
    """The loc's all.loc block, "" when it has none."""
    if not _LOC:
        import pokesprite as ps
        _LOC["names"] = ps.compack("loc")
        _LOC["ids"] = {v: k for k, v in _LOC["names"].items()}
        _LOC["txt"] = open(os.path.join(CONTENT, "configs/all.loc"), "rb").read().decode(
            "utf8", "replace")
    name = _LOC["names"].get(loc)
    if not name:
        return ""
    txt = _LOC["txt"]
    i = txt.find("\n[%s]\n" % name)
    return txt[i:txt.find("\n[", i + 2)] if i >= 0 else ""


def multiloc_children(loc):
    """The locs a multiloc can resolve to; [] for an ordinary loc."""
    kids = []
    for row in loc_config(loc).splitlines():
        if row.startswith("multiloc") and "=" in row:
            kid = _LOC["ids"].get(row.split("=", 1)[1].strip())
            if kid is not None and kid not in kids:
                kids.append(kid)
    return kids


def region_locs(sqs):
    """(loc, shape, seq) for every drawn-shape loc the squares place (.jl2)."""
    import pokesprite as ps
    seq_ids = {v: k for k, v in ps.compack("seq").items()}
    anims, children = {}, {}
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
            if shape == 2:
                shape = 0
            # A multiloc has no models: the client draws the child its varbit
            # picks, which only the live game knows -- so render every child.
            for drawn in children.setdefault(loc, multiloc_children(loc)) or [loc]:
                if drawn not in anims:
                    anims[drawn] = -1
                    for row in loc_config(drawn).splitlines():
                        if row.startswith("anim=") and row[5:].strip() in seq_ids:
                            anims[drawn] = seq_ids[row[5:].strip()]
                out.add((drawn, shape, anims[drawn]))
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


def settle_empty(store, key, err, raw=None):
    if raw:
        ps.drop_raw(raw)
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
    # A failed subject's renders too: its sheet never stored, so nothing else
    # drops them (POKESPRITE_KEEP_RAW=1 keeps them to debug the failure).
    ps.drop_raw(os.path.join(work, "npc_%d" % npc, "_raw"))
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
                    a.store, "loc/%d/%d/%d" % (loc, shape, seq), e,
                    os.path.join(a.work, "loc_%d_%d" % (loc, shape)))), flush=True)
        for gender in (("m", "f") if a.only in (None, "players") else ()):
            todo = {"kit:%d" % k: PLAYER_SEQS for k in default_kits(gender)}
            n = layers.generate(todo, os.path.join(a.work, "layers_raw"), a.store, gender)
            print("default %s body: %d layer sheets" % (gender, n), flush=True)
        return

    st = Store(a.store)
    missing = st.missing()
    st.close()
    npcs, layer_seqs, loc_keys, obj_keys, spot_keys = parse(k for k, _ in missing)
    # parse() rewrites an old client's seq 0 to -1, which missing() could not
    # see was already stored.
    st = Store(a.store)
    # A multiloc parent has no models -- an old client asked for one; the
    # current one asks for the child its varbit picks. Render the children.
    loc_keys = {(kid, shape, seq) for loc, shape, seq in loc_keys
                for kid in (multiloc_children(loc) or [loc])}
    loc_keys = {k for k in loc_keys if not st.has("loc/%d/%d/%d" % k)}
    st.close()
    print("missing: %d keys -> %d npcs, %d player layers, %d locs" % (
        len(missing), len(npcs), sum(len(v) for v in layer_seqs.values()), len(loc_keys)))
    done = 0
    for npc, seqs in sorted(npcs.items()):
        if a.limit and done >= a.limit:
            break
        print("npc %d seqs %s: %s" % (npc, sorted(seqs), run_npc(npc, seqs, a.work, a.store)))
        done += 1
    for spot in sorted(spot_keys):
        try:
            locs.generate_spot(spot, a.work, a.store)
            print("spot %d: ok" % spot)
        except (SystemExit, Exception) as e:
            print("spot %d: %s" % (spot, settle_empty(a.store, "spot/%d" % spot, e,
                                                            os.path.join(a.work, "spot_%d_z%d" % (
                                                                spot, locs.SPOT_ZOOM)))))
    for obj in sorted(obj_keys):
        try:
            locs.generate_obj(obj, a.work, a.store)
            print("obj %d: ok" % obj)
        except (SystemExit, Exception) as e:
            print("obj %d: %s" % (obj, settle_empty(a.store, "obj/%d" % obj, e,
                                                          os.path.join(a.work, "obj_%d" % obj))))
    for loc, shape, seq in sorted(loc_keys):
        try:
            locs.generate(loc, shape, seq, a.work, a.store)
            print("loc %d shape %d seq %d: ok" % (loc, shape, seq))
        except (SystemExit, Exception) as e:
            print("loc %d shape %d seq %d: %s" % (loc, shape, seq, settle_empty(
                a.store, "loc/%d/%d/%d" % (loc, shape, seq), e,
                os.path.join(a.work, "loc_%d_%d" % (loc, shape)))))
    for gender, by_layer in sorted(layer_seqs.items()):
        todo = {L: sorted(s) for L, s in sorted(by_layer.items())}
        n = layers.generate(todo, os.path.join(a.work, "layers_raw"), a.store, gender)
        print("player layers (%s): %d sheets stored" % (gender, n))


if __name__ == "__main__":
    main()
