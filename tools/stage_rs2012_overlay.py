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
BASE_PLACEMENTS = "BASE_PLACEMENTS.tsv"
# `ported/<lane>/lane.ini` is the lane descriptor the ServerScript compiler reads
# (src/serverscript/ssc_lane.c).  It names and gates the lane for script builds;
# cachepack has no use for it, so it is skipped rather than staged.
LANE_DESCRIPTOR = "lane.ini"
ASSET_ROOTS = (
    "models",
    "animsets",
    "framemaps",
    "sprites",
    "textures",
    "synth",
    "samples",
    "patches",
    "songs",
    "maps",
    "interfaces",
    "scripts",
)
CONFIG_SUFFIXES = {
    ".enum",
    ".inv",
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


def parse_loc_ledger(path: Path) -> dict[int, int]:
    """Read cachepack import's TSV authority for source->destination loc ids."""

    if not path.is_file() or path.is_symlink():
        raise fail(f"missing/non-plain import ledger: {path}")
    lines = path.read_text().splitlines()
    if not lines:
        raise fail(f"empty import ledger: {path}")
    header = lines[0].split("\t")
    required = {"kind", "source_id", "dest_id"}
    if not required <= set(header):
        raise fail(f"import ledger lacks {sorted(required)}: {path}")
    columns = {name: header.index(name) for name in required}
    result: dict[int, int] = {}
    destinations: set[int] = set()
    for line_number, raw in enumerate(lines[1:], 2):
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        fields = raw.split("\t")
        if max(columns.values()) >= len(fields) or fields[columns["kind"]] != "loc":
            continue
        try:
            source = int(fields[columns["source_id"]])
            destination = int(fields[columns["dest_id"]])
        except ValueError as exc:
            raise fail(f"{path}:{line_number}: non-numeric loc ledger id") from exc
        if source in result or destination in destinations:
            raise fail(f"{path}:{line_number}: duplicate loc ledger mapping")
        result[source] = destination
        destinations.add(destination)
    return result


def parse_base_placements(path: Path) -> list[tuple[str, int, int, int, int, int, int]]:
    """Read dynamic RS727 placements which have no source loc-map archive."""

    if not path.exists():
        return []
    if not path.is_file() or path.is_symlink():
        raise fail(f"base placement metadata is not a plain file: {path}")
    lines = path.read_text().splitlines()
    expected = ["square", "level", "x", "z", "source_loc", "shape", "angle"]
    if not lines or lines[0].split("\t")[: len(expected)] != expected:
        raise fail(f"base placement metadata has the wrong header: {path}")
    placements: list[tuple[str, int, int, int, int, int, int]] = []
    seen: set[tuple[str, int, int, int, int, int, int]] = set()
    for line_number, raw in enumerate(lines[1:], 2):
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        fields = raw.split("\t")
        if len(fields) < len(expected):
            raise fail(f"{path}:{line_number}: incomplete base placement")
        square = fields[0]
        if not re.fullmatch(r"\d+_\d+", square):
            raise fail(f"{path}:{line_number}: malformed square {square}")
        try:
            values = tuple(int(value) for value in fields[1:7])
        except ValueError as exc:
            raise fail(f"{path}:{line_number}: non-numeric base placement") from exc
        level, x, z, source_loc, shape, angle = values
        if not 0 <= level <= 3 or not 0 <= x <= 63 or not 0 <= z <= 63:
            raise fail(f"{path}:{line_number}: base placement coordinate out of range")
        if source_loc < 0 or not 0 <= shape <= 63 or not 0 <= angle <= 3:
            raise fail(f"{path}:{line_number}: base placement loc/shape/angle out of range")
        record = (square, level, x, z, source_loc, shape, angle)
        if record in seen:
            raise fail(f"{path}:{line_number}: duplicate base placement")
        seen.add(record)
        placements.append(record)
    return placements


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
    # Both destinations normally already exist because the sparse lane was
    # copied before this merge.  Count only newly materialized paths, not the
    # two in-place rewrites, so the reported total remains a physical-file
    # count rather than a write-operation count.
    created = sum(not path.exists() for path in (destination_text, destination_index))
    destination_text.parent.mkdir(parents=True, exist_ok=True)
    destination_text.write_text(
        base_text.read_text().rstrip() + "\n\n" + lane_text.read_text().lstrip()
    )
    merged = base_names | lane_names
    destination_index.write_text(
        "".join(f"{ident}={merged[ident]}\n" for ident in sorted(merged))
    )
    return created


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


def merge_base_placements(tree: Path, lane: Path, out: Path) -> int:
    """Overlay server-dynamic RS727 locs onto preserved OSRS239 squares.

    The Grotworm surface entrance was not encoded in revision 727's map index:
    its l46_50 archive/key is absent even though loc 70792 and the authoritative
    world coordinate survive.  This merge keeps the destination terrain and all
    auxiliary members byte-for-byte, while appending the ledger-remapped loc to
    a disposable staged copy of member 1.
    """

    placements = parse_base_placements(lane / BASE_PLACEMENTS)
    if not placements:
        return 0
    ledger = parse_loc_ledger(tree / "port/rs2012_qbd_td.map")
    map_pack_path = out / "pack/5_maps.pack"
    if not map_pack_path.is_file():
        raise fail(f"staged map pack is missing: {map_pack_path}")
    map_names = parse_name_pack(map_pack_path)

    copied = 0
    grouped: dict[str, list[tuple[int, int, int, int, int, int]]] = {}
    for square, level, x, z, source_loc, shape, angle in placements:
        grouped.setdefault(square, []).append((level, x, z, source_loc, shape, angle))

    for square, records in sorted(grouped.items()):
        map_x, map_z = (int(value) for value in square.split("_"))
        region = (map_x << 8) | map_z
        map_name = f"m{square}"
        if region in map_names or map_name in map_names.values():
            raise fail(f"base placement square collides with historical map pack: {square}")

        base_filepack = tree / f"maps/{map_name}.filepack"
        if not base_filepack.is_file() or base_filepack.is_symlink():
            raise fail(f"base placement filepack is missing/not plain: {base_filepack}")
        members = parse_member_pack(base_filepack)
        if not {0, 1} <= set(members):
            raise fail(f"base placement square lacks terrain/loc members: {base_filepack}")
        source_paths = {ident: tree / "maps" / member for ident, member in members.items()}
        for source in source_paths.values():
            if not source.is_file() or source.is_symlink():
                raise fail(f"base placement member is missing/not plain: {source}")
        before = {source: digest(source) for source in source_paths.values()}

        terrain_source = source_paths[0]
        loc_source = source_paths[1]
        terrain_dest = out / "maps" / members[0]
        loc_dest = out / "maps" / members[1]
        filepack_dest = out / f"maps/{map_name}.filepack"
        if terrain_dest.exists() or loc_dest.exists() or filepack_dest.exists():
            raise fail(f"base placement destination already exists: {square}")
        copy_file(terrain_source, terrain_dest)
        loc_text = loc_source.read_text()
        if "==== LOC ====" not in loc_text:
            raise fail(f"base placement loc member has no LOC marker: {loc_source}")
        appended: list[str] = []
        for level, x, z, source_loc, shape, angle in records:
            if source_loc not in ledger:
                raise fail(f"base placement loc {source_loc} is absent from import ledger")
            line = f"{level} {x} {z}: {ledger[source_loc]} {shape} {angle}"
            if line in loc_text.splitlines() or line in appended:
                raise fail(f"base placement already exists in {square}: {line}")
            appended.append(line)
        loc_dest.parent.mkdir(parents=True, exist_ok=True)
        loc_dest.write_text(loc_text.rstrip() + "\n" + "\n".join(appended) + "\n")
        copy_file(base_filepack, filepack_dest)
        copied += 3
        for ident, source in sorted(source_paths.items()):
            if ident < 2:
                continue
            copy_file(source, out / "maps" / members[ident])
            copied += 1
        if {source: digest(source) for source in source_paths.values()} != before:
            raise fail(f"base placement merge modified source bytes: {square}")
        map_names[region] = map_name

    map_pack_path.write_text(
        "".join(f"{ident}={map_names[ident]}\n" for ident in sorted(map_names))
    )
    return copied


def split_index(path: Path) -> tuple[list[str], list[str]]:
    """Split a pack index into its leading comment block and its data rows."""

    preamble: list[str] = []
    rows: list[str] = []
    for raw in path.read_text().splitlines():
        line = raw.split("//", 1)[0].split("#", 1)[0].split(";", 1)[0].strip()
        if not line:
            # Documentation only counts as a preamble until the first real row;
            # blank lines between rows are formatting and are not preserved.
            if not rows:
                preamble.append(raw)
            continue
        rows.append(line)
    return preamble, rows


def merge_index_rows(label: str, base_rows: list[str], lane_rows: list[str]) -> list[str]:
    """Union two pack indexes, refusing any row the two disagree about.

    Both `id=name` indexes and bare name lists occur. The lane allocates from
    reserved ranges (models 110000+, locs 63000+, sprites 8535+), so a genuine
    id or name conflict is an allocation bug rather than something to resolve
    here. What does legitimately repeat is a row the two state *identically* —
    the three TD route squares that already exist in OSRS239, `texture_0`, and a
    couple of ids the lane registered into base allocation tables — and those
    de-duplicate to one row.
    """

    keyed = [row for row in (*base_rows, *lane_rows) if "=" in row]
    if keyed and len(keyed) != len(base_rows) + len(lane_rows):
        raise fail(f"{label}: index mixes id=name rows with bare names")

    if not keyed:
        merged: list[str] = []
        for row in (*base_rows, *lane_rows):
            if row not in merged:
                merged.append(row)
        return merged

    names: dict[int, str] = {}
    reverse: dict[str, int] = {}
    for row in keyed:
        ident_text, name = row.split("=", 1)
        try:
            ident = int(ident_text)
        except ValueError as exc:
            raise fail(f"{label}: non-numeric member id in {row!r}") from exc
        name = name.strip()
        if names.get(ident, name) != name:
            raise fail(
                f"{label}: id {ident} is '{names[ident]}' in the base tree and "
                f"'{name}' in the lane"
            )
        if reverse.get(name, ident) != ident:
            raise fail(
                f"{label}: name '{name}' is id {reverse[name]} in the base tree "
                f"and id {ident} in the lane"
            )
        names[ident] = name
        reverse[name] = ident
    return [f"{ident}={names[ident]}" for ident in sorted(names)]


def merge_pack_indexes(tree: Path, out: Path) -> int:
    """Union the base pack indexes into the staged lane ones, for a full stage.

    Overlay staging leaves `out/pack` holding the lane's indexes alone, because
    `cachepack pack --base` resolves every archive the lane does not name from
    the base cache. A full stage has no base to fall back on, so each index has
    to list the base tree's members too or the cache loses everything the lane
    did not import.
    """

    base_pack = tree / "pack"
    if not base_pack.is_dir():
        raise fail(f"no base pack indexes under {base_pack}")
    created = 0
    for source in sorted(ensure_plain_tree(base_pack, "base pack index")):
        destination = out / "pack" / source.relative_to(base_pack)
        if not destination.exists():
            copy_file(source, destination)
            created += 1
            continue
        preamble, base_rows = split_index(source)
        _, lane_rows = split_index(destination)
        rows = merge_index_rows(source.name, base_rows, lane_rows)
        destination.write_text("\n".join((*preamble, *rows)).rstrip("\n") + "\n")
    return created


def copy_base_content(tree: Path, out: Path) -> int:
    """Add every base content path a full (no-base) stage must supply itself.

    Runs after the lane pass, and never overwrites: whatever the lane staged --
    including the merged texture archive, the retained map members and the
    rewritten map pack -- stays authoritative, and this only fills in what the
    base cache would otherwise have supplied.
    """

    copied = 0
    for source in sorted(ensure_plain_tree(tree, "content tree")):
        relative = source.relative_to(tree)
        # `ported` is every marked lane, staged deliberately or not at all --
        # this one by the pass above, the others because they are separate
        # features. `pack` is unioned by merge_pack_indexes rather than copied.
        if "ported" in relative.parts or relative.parts[0] == "pack":
            continue
        destination = out / relative
        if destination.exists():
            continue
        copy_file(source, destination)
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


def stage(tree: Path, out: Path, full: bool = False) -> int:
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
        if child.name == BASE_PLACEMENTS:
            # Evidence/merge metadata, consumed below rather than passed to
            # cachepack as an unknown content source.
            continue
        if child.name == LANE_DESCRIPTOR:
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

    # Add source-era server placements whose map archive does not exist. This
    # is deliberately last: it can reject any accidental collision with a
    # historical member generated above.
    copied += merge_base_placements(tree, lane, out)

    # Everything above is the lane and is identical in both modes. A full stage
    # then adds the base tree behind it, so the pack needs no --base cache.
    if full:
        copied += merge_pack_indexes(tree, out)
        copied += copy_base_content(tree, out)

    # Staging is an overlay into a disposable output. Hold that safety property
    # to bytes for every base path shadowed by the lane, especially the three TD
    # route squares also present in OSRS239.
    assert_fingerprints_unchanged(base_before)

    if copied == 0:
        raise fail("staged zero files")
    physical = len(ensure_plain_tree(out, "staged output"))
    if copied != physical:
        raise fail(
            f"file accounting mismatch: counted {copied}, materialized {physical}"
        )
    mode = "full" if full else "overlay"
    print(f"stage_rs2012_overlay: staged {copied} files in {out} ({mode})")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, default=DEFAULT_TREE)
    parser.add_argument("--out", type=Path, required=True,
                        help="directory to replace with the staged tree")
    parser.add_argument("--full", action="store_true",
                        help="also stage the base tree behind the lane, so the "
                             "result packs into a complete cache without a "
                             "--base cache to fall back on")
    args = parser.parse_args()
    try:
        return stage(args.tree, args.out, full=args.full)
    except ValueError as exc:
        print(exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
