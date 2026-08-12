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
import urllib.parse
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

MODELNET40_MIRRORS = [
    "https://modelnet.cs.princeton.edu/ModelNet40.zip",
    "http://modelnet.cs.princeton.edu/ModelNet40.zip",
]

# ModelNet10's ten categories are a curated subset of ModelNet40 — the same
# meshes under the same names. Excluding them from the 40-class conversion
# keeps the corpus free of near-duplicates (and saves the conversion time).
MODELNET10_CATEGORIES = {
    "bathtub", "bed", "chair", "desk", "dresser",
    "monitor", "night_stand", "sofa", "table", "toilet",
}

# Google Scanned Objects: 1,030 CC-BY textured household scans, served as
# per-model zips by the Gazebo Fuel REST API. The only bulk TEXTURED source —
# ModelNet is bare CAD, so without GSO the palette-bake stage mostly sees
# flat fallback gray.
GSO_LIST_URL = ("https://fuel.gazebosim.org/1.0/GoogleResearch/models"
                "?page={page}&per_page=100")
GSO_ZIP_URLS = [
    "https://fuel.gazebosim.org/1.0/GoogleResearch/models/{name}/tip/{name}.zip",
    "https://fuel.gazebosim.org/1.0/GoogleResearch/models/{name}/1/{name}.zip",
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


def _fetch_zip(zip_name: str, mirrors: list[str], min_bytes: int,
               size_hint: str) -> str | None:
    """Download a large corpus zip via the first reachable mirror, atomically
    (.part then rename) so a killed download can never masquerade as a cached
    complete file. Returns the zip path, or None if no mirror worked."""
    zip_path = os.path.join(OUT_DIR, zip_name)
    if os.path.isfile(zip_path) and os.path.getsize(zip_path) > min_bytes:
        print(f"  cached  {zip_name}")
        return zip_path
    part = zip_path + ".part"
    for url in mirrors:
        print(f"  downloading {url} ({size_hint})...", flush=True)
        if fetch(url, part) and os.path.getsize(part) > min_bytes:
            os.replace(part, zip_path)
            return zip_path
    print(f"  FAILED: no mirror reachable for {zip_name}", file=sys.stderr)
    return None


def _convert_modelnet(zip_path: str, subdir: str, limit: int, seed: int,
                      skip_categories: set[str] = frozenset()) -> tuple[int, int]:
    """Convert a deterministic sample of `limit` OFF meshes from a ModelNet
    zip to OBJ under highpoly_src/<subdir>/. Returns (converted, attempted)."""
    out_dir = os.path.join(OUT_DIR, subdir)
    os.makedirs(out_dir, exist_ok=True)
    with zipfile.ZipFile(zip_path) as zf:
        members = [m for m in zf.namelist()
                   if m.endswith(".off") and "__MACOSX" not in m
                   and m.split("/")[1] not in skip_categories]
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


def fetch_modelnet(limit: int, seed: int) -> tuple[int, int]:
    """ModelNet10: ~451 MB zip, 4,899 meshes -> highpoly_src/modelnet/."""
    print(f"ModelNet10 (bulk meshes, limit={limit}):")
    zip_path = _fetch_zip("ModelNet10.zip", MODELNET_MIRRORS,
                          100_000_000, "~451 MB")
    if zip_path is None:
        return 0, 0
    return _convert_modelnet(zip_path, "modelnet", limit, seed)


def fetch_modelnet40(limit: int, seed: int) -> tuple[int, int]:
    """ModelNet40 minus the ten ModelNet10 categories (those meshes are the
    same files): ~9,200 genuinely new meshes -> highpoly_src/modelnet40/."""
    print(f"ModelNet40 (bulk meshes, limit={limit}):")
    zip_path = _fetch_zip("ModelNet40.zip", MODELNET40_MIRRORS,
                          1_000_000_000, "~2 GB")
    if zip_path is None:
        return 0, 0
    return _convert_modelnet(zip_path, "modelnet40", limit, seed,
                             skip_categories=MODELNET10_CATEGORIES)


def _gso_model_names(limit: int) -> list[str]:
    """Page through the Fuel REST index until `limit` names are collected."""
    names: list[str] = []
    page = 1
    while len(names) < limit:
        url = GSO_LIST_URL.format(page=page)
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "osrs-stylizer"})
            with urllib.request.urlopen(req, timeout=60) as resp:
                batch = json.load(resp)
        except Exception as exc:
            print(f"  FAILED index page {page}: {exc}", file=sys.stderr)
            break
        if not batch:
            break                      # ran off the end of the collection
        names += [entry["name"] for entry in batch if "name" in entry]
        page += 1
    return names[:limit]


def fetch_gso(limit: int) -> tuple[int, int]:
    """Google Scanned Objects: textured OBJ scans, one zip per model, into
    highpoly_src/gso/<name>/ (the OBJ/MTL/texture trio must stay together, so
    each model keeps its own directory). Returns (ok, attempted)."""
    print(f"Google Scanned Objects (textured scans, limit={limit}):")
    names = _gso_model_names(limit)
    ok = 0
    for i, name in enumerate(names, 1):
        slug = name.replace(" ", "_")
        model_dir = os.path.join(OUT_DIR, "gso", slug)
        obj_path = os.path.join(model_dir, "meshes", "model.obj")
        if os.path.isfile(obj_path):
            ok += 1
            continue
        zip_dest = os.path.join(OUT_DIR, "gso", f"{slug}.zip")
        quoted = urllib.parse.quote(name)
        os.makedirs(os.path.dirname(zip_dest), exist_ok=True)
        if not any(fetch(u.format(name=quoted), zip_dest) for u in GSO_ZIP_URLS):
            continue
        try:
            with zipfile.ZipFile(zip_dest) as zf:
                for member in zf.namelist():
                    # Thumbnails are dead weight; everything else (meshes/,
                    # materials/textures/) is needed for the MTL to resolve.
                    if member.startswith("thumbnails/") or member.endswith("/"):
                        continue
                    zf.extract(member, model_dir)
        except zipfile.BadZipFile as exc:
            print(f"  FAILED {name}: {exc}", file=sys.stderr)
        finally:
            try:
                os.remove(zip_dest)    # keep only the extracted tree
            except OSError:
                pass
        if os.path.isfile(obj_path):
            ok += 1
        else:
            print(f"  ! no meshes/model.obj in {name}", file=sys.stderr)
        if i % 50 == 0 or i == len(names):
            print(f"  {i}/{len(names)} models ({ok} ok)", flush=True)
    return ok, len(names)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelnet-limit", type=int, default=2500,
                    help="how many ModelNet10 meshes to convert (0 = skip)")
    ap.add_argument("--modelnet40-limit", type=int, default=0,
                    help="how many ModelNet40 meshes to convert (0 = skip; "
                         "the ModelNet10 categories are always excluded)")
    ap.add_argument("--gso-limit", type=int, default=0,
                    help="how many Google Scanned Objects to download (0 = skip)")
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

    if args.modelnet40_limit:
        m_ok, m_total = fetch_modelnet40(args.modelnet40_limit, args.seed)
        ok += m_ok
        total += m_total

    if args.gso_limit:
        g_ok, g_total = fetch_gso(args.gso_limit)
        ok += g_ok
        total += g_total

    print(f"\n{ok}/{total} models ready under {OUT_DIR}")
    if ok == 0:
        raise SystemExit("nothing downloaded — check network access")


if __name__ == "__main__":
    main()
