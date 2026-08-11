#!/usr/bin/env python3
"""Download free/open 3D models to serve as the high-poly counter-class
(Class 2) for the style classifier.

Sources, all permissively licensed and fetchable without auth:

1. alecjacobson/common-3d-test-models (github) — the classic graphics-research
   scan/test set (Stanford bunny, armadillo, nefertiti, ...) as single OBJs.
2. KhronosGroup/glTF-Sample-Models (github) — a hand-picked set of textured
   PBR showcase assets (DamagedHelmet, Duck, ...) as self-contained .glb.
3. KhronosGroup/glTF-Sample-Assets (github) — the FULL current Khronos sample
   index (~80 assets); every model that ships a glTF-Binary variant.
4. ModelNet10 (Princeton) — 4,899 category-labeled CAD meshes (chairs, desks,
   toilets, ...). Downloaded as one zip and converted OFF -> OBJ here, since
   Blender has no OFF importer. This is the bulk-diversity source.

Files land in highpoly_src/ (gitignored). Standard library only — no pip deps.

Usage:
    python fetch_highpoly.py                     # everything
    python fetch_highpoly.py --modelnet-limit 0  # skip ModelNet
"""

import argparse
import io
import json
import os
import random
import sys
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(HERE, "highpoly_src")

CJ = ("https://github.com/alecjacobson/common-3d-test-models"
      "/raw/master/data/{name}")
KHR = ("https://github.com/KhronosGroup/glTF-Sample-Models"
       "/raw/main/2.0/{name}/glTF-Binary/{name}.glb")

# Classic research meshes — mostly untextured, genuinely high-poly scans.
COMMON_OBJS = [
    "alligator.obj", "armadillo.obj", "beast.obj", "beetle.obj", "bunny.obj",
    "cheburashka.obj", "cow.obj", "fandisk.obj", "happy.obj", "homer.obj",
    "horse.obj", "igea.obj", "lucy.obj", "max-planck.obj", "nefertiti.obj",
    "ogre.obj", "rocker-arm.obj", "spot.obj", "stanford-bunny.obj",
    "suzanne.obj", "teapot.obj", "xyzrgb_dragon.obj",
]

# Textured PBR showcase assets — the "modern game asset" look.
KHRONOS_GLBS = [
    "DamagedHelmet", "Duck", "Avocado", "BoomBox", "Lantern", "WaterBottle",
    "BarramundiFish", "CesiumMan", "Corset", "Fox",
]


def fetch(url: str, dest: str) -> bool:
    """Download url -> dest. Returns False (and keeps going) on any failure —
    a couple of missing models is fine, the corpus just gets smaller."""
    if os.path.isfile(dest) and os.path.getsize(dest) > 0:
        print(f"  cached  {os.path.basename(dest)}")
        return True
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "osrs-stylizer"})
        with urllib.request.urlopen(req, timeout=120) as resp, open(dest, "wb") as fh:
            fh.write(resp.read())
        print(f"  fetched {os.path.basename(dest)} "
              f"({os.path.getsize(dest) // 1024} KB)")
        return True
    except Exception as exc:
        print(f"  FAILED  {url}: {exc}", file=sys.stderr)
        try:
            os.remove(dest)
        except OSError:
            pass
        return False


SAMPLE_ASSETS_INDEX = ("https://raw.githubusercontent.com/KhronosGroup/"
                       "glTF-Sample-Assets/main/Models/model-index.json")
SAMPLE_ASSETS_FILE = ("https://raw.githubusercontent.com/KhronosGroup/"
                      "glTF-Sample-Assets/main/Models/{name}/glTF-Binary/{file}")

MODELNET_MIRRORS = [
    "https://modelnet.cs.princeton.edu/ModelNet10.zip",
    "http://3dvision.princeton.edu/projects/2014/3DShapeNets/ModelNet10.zip",
]


def fetch_khronos_index() -> tuple[int, int]:
    """Download every asset in the current Khronos sample index that ships a
    self-contained glTF-Binary variant. Returns (ok, attempted)."""
    print("glTF-Sample-Assets (full index):")
    try:
        req = urllib.request.Request(SAMPLE_ASSETS_INDEX,
                                     headers={"User-Agent": "osrs-stylizer"})
        with urllib.request.urlopen(req, timeout=60) as resp:
            index = json.load(resp)
    except Exception as exc:
        print(f"  FAILED to fetch index: {exc}", file=sys.stderr)
        return 0, 0

    ok = attempted = 0
    for entry in index:
        variants = entry.get("variants", {})
        glb = variants.get("glTF-Binary")
        if not glb:
            continue  # multi-file variants only — not worth the complexity
        attempted += 1
        url = SAMPLE_ASSETS_FILE.format(name=entry["name"], file=glb)
        ok += fetch(url, os.path.join(OUT_DIR, glb))
    return ok, attempted


def _off_to_obj(off_bytes: bytes) -> str | None:
    """Convert an OFF mesh to OBJ text. Handles ModelNet's infamous malformed
    headers where the counts are glued to the magic ('OFF490 322 0')."""
    try:
        text = off_bytes.decode("ascii", errors="ignore")
        tokens: list[str] = []
        for line in text.splitlines():
            line = line.split("#", 1)[0].strip()
            if line:
                tokens.extend(line.split())
        if not tokens:
            return None
        if tokens[0] == "OFF":
            tokens = tokens[1:]
        elif tokens[0].startswith("OFF"):
            tokens[0] = tokens[0][3:]      # 'OFF490' -> '490'
        n_verts, n_faces = int(tokens[0]), int(tokens[1])
        pos = 3                             # skip n_edges
        out = []
        for _ in range(n_verts):
            x, y, z = tokens[pos], tokens[pos + 1], tokens[pos + 2]
            out.append(f"v {x} {y} {z}")
            pos += 3
        for _ in range(n_faces):
            k = int(tokens[pos])
            idx = tokens[pos + 1:pos + 1 + k]
            # OBJ indices are 1-based; OFF are 0-based. N-gons are fine.
            out.append("f " + " ".join(str(int(i) + 1) for i in idx))
            pos += 1 + k
        return "\n".join(out) + "\n"
    except (ValueError, IndexError):
        return None


def fetch_modelnet(limit: int, seed: int) -> tuple[int, int]:
    """Download ModelNet10 (~451 MB zip, cached) and convert a deterministic
    sample of `limit` meshes to OBJ under highpoly_src/modelnet/.
    Returns (converted, attempted)."""
    print(f"ModelNet10 (bulk meshes, limit={limit}):")
    zip_path = os.path.join(OUT_DIR, "ModelNet10.zip")
    if not (os.path.isfile(zip_path) and os.path.getsize(zip_path) > 100_000_000):
        for url in MODELNET_MIRRORS:
            print(f"  downloading {url} (~451 MB)...")
            if fetch(url, zip_path):
                break
        else:
            print("  FAILED: no ModelNet mirror reachable", file=sys.stderr)
            return 0, 0
    else:
        print("  cached  ModelNet10.zip")

    out_dir = os.path.join(OUT_DIR, "modelnet")
    os.makedirs(out_dir, exist_ok=True)
    with zipfile.ZipFile(zip_path) as zf:
        members = [m for m in zf.namelist()
                   if m.endswith(".off") and "__MACOSX" not in m]
        members.sort()
        if limit and limit < len(members):
            members = random.Random(seed).sample(members, limit)
        converted = 0
        for i, member in enumerate(members, 1):
            # ModelNet10/chair/train/chair_0001.off -> chair_train_chair_0001.obj
            parts = member.split("/")
            stem = "_".join(parts[1:]).replace(".off", "")
            dest = os.path.join(out_dir, f"{stem}.obj")
            if os.path.isfile(dest):
                converted += 1
                continue
            obj_text = _off_to_obj(zf.read(member))
            if obj_text is None:
                print(f"  ! unparsable OFF: {member}", file=sys.stderr)
                continue
            with open(dest, "w", encoding="ascii") as fh:
                fh.write(obj_text)
            converted += 1
            if i % 500 == 0 or i == len(members):
                print(f"  {i}/{len(members)} converted")
    return converted, len(members)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelnet-limit", type=int, default=2500,
                    help="how many ModelNet meshes to convert (0 = skip)")
    ap.add_argument("--seed", type=int, default=1337)
    args = ap.parse_args()

    os.makedirs(OUT_DIR, exist_ok=True)
    ok = 0
    total = 0

    print("common-3d-test-models (OBJ):")
    for name in COMMON_OBJS:
        total += 1
        ok += fetch(CJ.format(name=name), os.path.join(OUT_DIR, name))

    print("glTF-Sample-Models (GLB):")
    for name in KHRONOS_GLBS:
        total += 1
        ok += fetch(KHR.format(name=name), os.path.join(OUT_DIR, f"{name}.glb"))

    k_ok, k_total = fetch_khronos_index()
    ok += k_ok
    total += k_total

    if args.modelnet_limit:
        m_ok, m_total = fetch_modelnet(args.modelnet_limit, args.seed)
        ok += m_ok
        total += m_total

    print(f"\n{ok}/{total} models ready under {OUT_DIR}")
    if ok == 0:
        raise SystemExit("nothing downloaded — check network access")


if __name__ == "__main__":
    main()
