#!/usr/bin/env python3
"""Stage the client assets/configs imported for the RS2012 QBD and TD port.

The ordinary osrs239 source tree remains the pristine-cache input.  Foreign
revision records live below ``ported/rs2012_qbd_td`` and their binary assets
live in identically named subdirectories of the normal asset roots.  This tool
creates the explicit second-pass tree consumed by ``cachepack pack --base``.

No directory is discovered by convention during the bake: this script copies
only the marked lane, its matching asset subtrees, the cache member indexes
needed to resolve existing symbols, and the shared field/register metadata.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TREE = REPO_ROOT / "OSRS-Content" / "osrs239-content"
LANE = Path("ported") / "rs2012_qbd_td"
ASSET_ROOTS = (
    "models",
    "animsets",
    "framemaps",
    "sprites",
    "textures",
    "synth",
    "samples",
    "patches",
    "maps",
    "interfaces",
    "scripts",
)
CONFIG_SUFFIXES = {
    ".enum",
    ".npc",
    ".obj",
    ".loc",
    ".seq",
    ".spotanim",
    ".varbit",
    ".varp",
    ".constant",
}


def fail(message: str) -> ValueError:
    return ValueError(f"stage_rs2012_overlay: {message}")


def ensure_plain_tree(root: Path, label: str) -> list[Path]:
    if not root.is_dir():
        raise fail(f"{label} directory does not exist: {root}")
    files: list[Path] = []
    for path in root.rglob("*"):
        if path.is_symlink():
            raise fail(f"{label} contains a symlink: {path}")
        if path.is_file():
            files.append(path)
    return files


def copy_file(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def copy_tree(source: Path, destination: Path) -> int:
    if not source.exists():
        return 0
    files = ensure_plain_tree(source, "RS2012 source")
    for source_file in files:
        copy_file(source_file, destination / source_file.relative_to(source))
    return len(files)


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def parse_member_pack(path: Path) -> dict[int, Path]:
    """Read an id=relative-path filepack, refusing ambiguous/unsafe rows."""

    members: dict[int, Path] = {}
    for line_number, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.split("#", 1)[0].split(";", 1)[0].strip()
        if not line:
            continue
        if "=" not in line:
            raise fail(f"{path}:{line_number}: malformed filepack row")
        ident_text, member_text = line.split("=", 1)
        try:
            ident = int(ident_text)
        except ValueError as exc:
            raise fail(f"{path}:{line_number}: non-numeric member id") from exc
        member = Path(member_text.strip())
        if member.is_absolute() or ".." in member.parts or not member.parts:
            raise fail(f"{path}:{line_number}: unsafe member path {member}")
        if ident in members:
            raise fail(f"{path}:{line_number}: duplicate member id {ident}")
        members[ident] = member
    return members


def parse_name_pack(path: Path) -> dict[int, str]:
    """Read an id=name compack and reject duplicate ids or names."""

    names: dict[int, str] = {}
    reverse: set[str] = set()
    for line_number, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.split("#", 1)[0].split(";", 1)[0].strip()
        if not line:
            continue
        if "=" not in line:
            raise fail(f"{path}:{line_number}: malformed compack row")
        ident_text, name = line.split("=", 1)
        try:
            ident = int(ident_text)
        except ValueError as exc:
            raise fail(f"{path}:{line_number}: non-numeric member id") from exc
        name = name.strip()
        if ident in names or name in reverse:
            raise fail(f"{path}:{line_number}: duplicate texture id/name")
        names[ident] = name
        reverse.add(name)
    return names


def config_block_names(path: Path) -> list[str]:
    return re.findall(r"^\[([^]]+)]\s*$", path.read_text(), re.MULTILINE)


def merge_texture_archive_zero(tree: Path, lane: Path, out: Path) -> int:
    """Merge imported materials into OSRS texture archive 0.

    Texture archive 0 is multi-member. A lane containing only its imported
    blocks cannot replace the source file: that would rebuild the archive with
    every stock OSRS material missing. Keep both text layers and merge their
    member indexes by id, refusing an allocation/name collision.
    """

    lane_text = lane / "textures/texture_0.texture"
    lane_index = lane / "textures/texture_0.compack"
    if not lane_text.exists() and not lane_index.exists():
        return 0
    if not lane_text.is_file() or lane_text.is_symlink():
        raise fail(f"missing/non-plain lane texture source: {lane_text}")
    if not lane_index.is_file() or lane_index.is_symlink():
        raise fail(f"missing/non-plain lane texture index: {lane_index}")

    base_text = tree / "textures/texture_0.texture"
    base_index = tree / "textures/texture_0.compack"
    for source in (base_text, base_index):
        if not source.is_file() or source.is_symlink():
            raise fail(f"missing/non-plain base texture source: {source}")

    base_names = parse_name_pack(base_index)
    lane_names = parse_name_pack(lane_index)
    id_overlap = set(base_names) & set(lane_names)
    name_overlap = set(base_names.values()) & set(lane_names.values())
    if id_overlap or name_overlap:
        raise fail(
            "texture archive 0 collision: "
            f"ids={sorted(id_overlap)} names={sorted(name_overlap)}"
        )

    base_blocks = config_block_names(base_text)
    lane_blocks = config_block_names(lane_text)
    block_overlap = set(base_blocks) & set(lane_blocks)
    if block_overlap:
        raise fail(f"texture block collision: {sorted(block_overlap)}")
    if len(base_blocks) != len(set(base_blocks)) or len(lane_blocks) != len(set(lane_blocks)):
        raise fail("texture source contains duplicate block names")
    all_blocks = set(base_blocks) | set(lane_blocks)
    indexed_blocks = set(base_names.values()) | set(lane_names.values())
    if all_blocks != indexed_blocks:
        raise fail(
            "texture source/index disagree: "
            f"unindexed={sorted(all_blocks - indexed_blocks)} "
            f"missing={sorted(indexed_blocks - all_blocks)}"
        )

    destination_text = out / "textures/texture_0.texture"
    destination_index = out / "textures/texture_0.compack"
    destination_text.parent.mkdir(parents=True, exist_ok=True)
    destination_text.write_text(
        base_text.read_text().rstrip() + "\n\n" + lane_text.read_text().lstrip()
    )
    merged = base_names | lane_names
    destination_index.write_text(
        "".join(f"{ident}={merged[ident]}\n" for ident in sorted(merged))
    )
    return 2


def copy_base_map_members(tree: Path, lane: Path, out: Path) -> int:
    """Supply auxiliary OSRS map members retained by a historical overlay.

    RS727 contributes terrain and locs (members 0/1). Existing OSRS239 squares
    can also carry members 2..N. map_port preserves those rows in the lane's
    filepack; this copies the payloads into the sparse stage so cachepack can
    rebuild the archive without dropping them.
    """

    copied = 0
    lane_maps = lane / "maps"
    if not lane_maps.exists():
        return copied
    for filepack in sorted(lane_maps.glob("m*.filepack")):
        members = parse_member_pack(filepack)
        if set(members).isdisjoint({0, 1}):
            raise fail(f"historical map filepack has neither terrain nor locs: {filepack}")
        for ident, member in sorted(members.items()):
            if ident < 2:
                continue
            source = tree / "maps" / member
            try:
                source.resolve().relative_to((tree / "maps").resolve())
            except ValueError as exc:
                raise fail(f"map member escapes base maps tree: {member}") from exc
            if not source.is_file() or source.is_symlink():
                raise fail(f"retained base map member is missing/not plain: {source}")
            copy_file(source, out / "maps" / member)
            copied += 1
    return copied


def base_conflict_fingerprints(tree: Path, lane: Path) -> dict[Path, str]:
    """Hash base files whose staged destination has the same relative path."""

    fingerprints: dict[Path, str] = {}
    for child in lane.iterdir():
        if not child.is_dir() or child.name not in {"configs", "pack", *ASSET_ROOTS}:
            continue
        for lane_file in ensure_plain_tree(child, "RS2012 lane"):
            base = tree / child.name / lane_file.relative_to(child)
            # The lane itself can appear below an asset root; it is not a base
            # collision and hashing it would only prove the source copied itself.
            if base.is_file() and lane not in base.parents:
                if base.is_symlink():
                    raise fail(f"base collision is a symlink: {base}")
                fingerprints[base] = digest(base)
    return fingerprints


def assert_fingerprints_unchanged(fingerprints: dict[Path, str]) -> None:
    for path, before in fingerprints.items():
        if not path.is_file() or path.is_symlink() or digest(path) != before:
            raise fail(f"staging modified base source file: {path}")


def reset_output(tree: Path, out: Path) -> None:
    tree = tree.resolve()
    out = out.resolve()
    if out == tree or tree in out.parents:
        raise fail("output must not be the content tree or a child of it")
    if out in {REPO_ROOT, REPO_ROOT.parent, Path(out.anchor)}:
        raise fail(f"refusing broad output path: {out}")
    if out.exists():
        if out.is_symlink() or not out.is_dir():
            raise fail(f"existing output is not a plain directory: {out}")
        shutil.rmtree(out)
    out.mkdir(parents=True)


def stage(tree: Path, out: Path) -> int:
    tree = tree.resolve()
    out = out.resolve()
    lane = tree / LANE
    marker = lane / "PROVENANCE.md"
    if not marker.is_file() or marker.is_symlink():
        raise fail(f"missing plain lane marker {marker}")
    ensure_plain_tree(lane, "RS2012 lane")
    base_before = base_conflict_fingerprints(tree, lane)
    reset_output(tree, out)

    copied = 0
    for required in (tree / "meta.ini", tree / "content.ini"):
        if not required.is_file() or required.is_symlink():
            raise fail(f"missing plain support file: {required}")
        copy_file(required, out / required.name)
        copied += 1
    copied += copy_tree(tree / "fields", out / "fields")

    # Imported records can refer to symbols already present in osrs239.  The
    # full compacks are lookup context; the imported config blocks remain the
    # only records this sparse stage asks cachepack to write.
    compacks = sorted((tree / "configs").glob("*.compack"))
    if not compacks:
        raise fail(f"no config member indexes under {tree / 'configs'}")
    for compack in compacks:
        if compack.is_symlink():
            raise fail(f"config member index is a symlink: {compack}")
        copy_file(compack, out / "configs" / compack.name)
        copied += 1

    # Lane directories mirror their staged destination.  The marker itself is
    # evidence, not cache input, so it is intentionally not copied.
    allowed_lane_dirs = {"configs", "pack", *ASSET_ROOTS}
    for child in sorted(lane.iterdir()):
        if child.name == marker.name:
            continue
        if child.is_dir() and child.name in allowed_lane_dirs:
            copied += copy_tree(child, out / child.name)
        elif child.is_file() and child.suffix in CONFIG_SUFFIXES:
            copy_file(child, out / "configs" / LANE / child.name)
            copied += 1
        else:
            raise fail(f"unclassified lane entry: {child}")

    # Binary payloads stay under the normal content roots so cachepack can use
    # its ordinary asset grammar.  Only the provenance-matched subtree enters
    # this feature stage.
    for root_name in ASSET_ROOTS:
        copied += copy_tree(tree / root_name / LANE, out / root_name / LANE)

    # The lane copy above puts its archive-0 texture sources in the right path;
    # now expand that sparse layer against the base archive rather than letting
    # it replace all stock OSRS material members.
    copied += merge_texture_archive_zero(tree, lane, out)

    # The lane owns historical terrain/locs but not modern auxiliary members.
    # Copy exactly the >=2 payloads its merge-aware filepacks retained.
    copied += copy_base_map_members(tree, lane, out)

    # Staging is an overlay into a disposable output. Hold that safety property
    # to bytes for every base path shadowed by the lane, especially the three TD
    # route squares also present in OSRS239.
    assert_fingerprints_unchanged(base_before)

    if copied == 0:
        raise fail("staged zero files")
    print(f"stage_rs2012_overlay: staged {copied} files in {out}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, default=DEFAULT_TREE)
    parser.add_argument("--out", type=Path, required=True,
                        help="directory to replace with the sparse overlay")
    args = parser.parse_args()
    try:
        return stage(args.tree, args.out)
    except ValueError as exc:
        print(exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
