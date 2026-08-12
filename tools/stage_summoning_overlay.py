#!/usr/bin/env python3
"""Stage the client half of the feature-flagged Summoning overlay.

The ordinary content tree remains the flag-off input.  Client-visible Summoning
files live below ``ported/scape2009_summoning`` (or in the matching asset
subtrees) where the ordinary cachepack config walk cannot see them.  This tool
builds the small, explicit second-pass tree used only by the flag-on bake.

It also provides ``--compare-cache`` as the byte-identity harness.  The command
compares every regular file in two cache directories and refuses a vacuous
zero-file pass.
"""

from __future__ import annotations

import argparse
import filecmp
import hashlib
import json
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TREE = REPO_ROOT / "OSRS-Content" / "osrs239-content"
DEFAULT_BOUNDARY = REPO_ROOT / "docs" / "summoning_port" / "roster_boundary_530.json"
LANE = Path("ported") / "scape2009_summoning"
ASSET_ROOTS = (
    "models",
    "animsets",
    "framemaps",
    "sprites",
    "synth",
    "interfaces",
    "scripts",
)
CONFIG_SUFFIXES = {
    ".enum",
    ".npc",
    ".obj",
    ".loc",
    ".inv",
    ".seq",
    ".spotanim",
    ".varbit",
    ".varp",
    ".constant",
}
INTERFACE_OVERLAYS = "interface_overlays"
CONFIG_OVERLAYS = "config_overlays"
# Existing cache scripts that need a Summoning-only replacement. Keep their
# source in the ordinary script tree so it remains name-resolved and
# round-trippable, but copy it into the disposable feature-on stage explicitly;
# otherwise cachepack silently retains the base-cache bytecode.
CLIENTSCRIPT_OVERLAYS = ("script_1904.cs2",)
ADMISSION_TEXT_SUFFIXES = {
    ".alloc",
    ".client",
    ".compack",
    ".constant",
    ".cs2",
    ".enum",
    ".if",
    ".inv",
    ".loc",
    ".npc",
    ".obj",
    ".pack",
    ".seq",
    ".spotanim",
    ".varbit",
    ".varp",
}
# Only these files are line-oriented membership/allocation ledgers. A generated
# cohort can be held out row-by-row here; configs, interfaces and scripts must
# instead be isolated in a cohort-named file so their record structure cannot
# be accidentally truncated.
REVIEW_FILTER_SUFFIXES = {".alloc", ".client", ".pack"}
GENERATED_COHORT_TOKEN = re.compile(r"(?<![A-Za-z0-9_])(summoning_(?:roster|cohort)_[a-z0-9_]+)")
SYNTH_SOURCE_TOKEN = re.compile(r"(?:^|_)synth_(\d+)(?:$|_)")
DIRECT_PET_RECORD_TOKEN = re.compile(r"(?<![A-Za-z0-9_])(summoning_pet_[a-z0-9_]+)")
NPC_SOUNDS_YES = re.compile(r"(?mi)^\s*npc_sounds\s*=\s*yes\s*$")

# The original Spirit wolf import predates the generated Phase-5 cohort
# admission ledger.  Its five config filenames now follow the cohort naming
# convention, but its canonical records deliberately retain their older
# ``summoning_*`` names.  Admit only these exact legacy paths; a similarly
# prefixed file or a generated-cohort token inside their contents must still
# pass the ordinary boundary checks below.
BASE_COHORT_CONFIG_TOKEN = "summoning_cohort_spirit_wolf"
BASE_COHORT_CONFIG_PATHS = frozenset(
    (LANE / "configs" / f"{BASE_COHORT_CONFIG_TOKEN}{suffix}").as_posix()
    for suffix in (".loc", ".npc", ".obj", ".seq", ".spotanim")
)


@dataclass(frozen=True)
class RosterAdmission:
    admitted_cohorts: tuple[str, ...]
    review_only_cohorts: tuple[str, ...]
    safe_synths: frozenset[int]
    admitted_pet_prefixes: tuple[str, ...]
    admitted_review_references: frozenset[str]


def fail(message: str) -> ValueError:
    return ValueError(f"stage_summoning_overlay: {message}")


def load_roster_boundary(path: Path) -> RosterAdmission:
    """Load the narrow Phase-5 roster admission contract.

    The seven-column import ledger remains cachepack-compatible.  This separate
    document answers a different question: which generated familiar cohorts are
    allowed to enter the feature-on staging tree at all.
    """
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise fail(f"cannot read roster boundary {path}: {exc}") from exc
    if (not isinstance(data, dict) or data.get("schema") != 1 or
            data.get("phase") not in {"5a", "5b", "5c"}):
        raise fail(f"roster boundary {path} must be schema 1 for Phase 5a, 5b, or 5c")
    cohorts = data.get("admitted_cohorts")
    review_only = data.get("review_only_cohorts")
    synths = data.get("safe_synth_sources")
    if not isinstance(cohorts, list) or not all(
        isinstance(prefix, str) and prefix.startswith("summoning_") for prefix in cohorts
    ) or len(set(cohorts)) != len(cohorts):
        raise fail("roster boundary admitted_cohorts must be summoning_ prefixes")
    if not isinstance(synths, list):
        raise fail("roster boundary safe_synth_sources must be an array")
    try:
        safe_synths = frozenset(int(source_id) for source_id in synths)
    except (TypeError, ValueError) as exc:
        raise fail("roster boundary safe_synth_sources must contain integers") from exc
    if safe_synths != {188}:
        raise fail("Phase 5a permits exactly safe synth source 188")
    if not isinstance(review_only, list) or not review_only:
        raise fail("roster boundary review_only_cohorts must be a non-empty array")
    review_prefixes: list[str] = []
    for index, cohort in enumerate(review_only, start=1):
        if not isinstance(cohort, dict):
            raise fail(f"review_only_cohorts[{index}] must be an object")
        prefix = cohort.get("prefix")
        if (not isinstance(prefix, str) or not prefix.startswith("summoning_") or
                cohort.get("status") != "preserved_experiment"):
            raise fail(f"review_only_cohorts[{index}] must be a preserved summoning_ experiment")
        review_prefixes.append(prefix)
    if len(set(review_prefixes)) != len(review_prefixes):
        raise fail("roster boundary review_only_cohorts contains duplicate prefixes")
    if set(cohorts) & set(review_prefixes):
        raise fail("roster boundary cannot admit and review-hold the same cohort")
    pet_prefixes = data.get("admitted_pet_prefixes", [])
    if (not isinstance(pet_prefixes, list) or not all(
        isinstance(prefix, str) and prefix.startswith("summoning_pet_") for prefix in pet_prefixes
    ) or len(set(pet_prefixes)) != len(pet_prefixes)):
        raise fail("roster boundary admitted_pet_prefixes must be distinct summoning_pet_ prefixes")
    references = data.get("admitted_review_references", [])
    if (not isinstance(references, list) or not all(
        isinstance(reference, str) and reference.startswith("summoning_roster_530_")
        for reference in references
    ) or len(set(references)) != len(references)):
        raise fail("roster boundary admitted_review_references must be distinct review-only record names")
    return RosterAdmission(
        tuple(cohorts), tuple(review_prefixes), safe_synths,
        tuple(pet_prefixes), frozenset(references)
    )


def cohort_matches(token: str, prefix: str) -> bool:
    return token == prefix or token.startswith(f"{prefix}_")


def cohort_is_admitted(token: str, admission: RosterAdmission) -> bool:
    return any(cohort_matches(token, prefix) for prefix in admission.admitted_cohorts)


def cohort_is_review_only(token: str, admission: RosterAdmission) -> bool:
    return any(cohort_matches(token, prefix) for prefix in admission.review_only_cohorts)


def review_only_tokens(value: str, admission: RosterAdmission) -> set[str]:
    return {
        token for token in GENERATED_COHORT_TOKEN.findall(value)
        if cohort_is_review_only(token, admission) and token not in admission.admitted_review_references
    }


def pet_is_admitted(token: str, admission: RosterAdmission) -> bool:
    return any(cohort_matches(token, prefix) for prefix in admission.admitted_pet_prefixes)


def audit_roster_admission(tree: Path, lane: Path, boundary_path: Path) -> int:
    """Fail closed on unknown generated work; hold known experiments out of stage."""
    admission = load_roster_boundary(boundary_path)
    errors: list[str] = []
    checked = 0
    review_only_hits = 0
    roots = [lane]
    roots.extend(tree / root_name / LANE for root_name in ASSET_ROOTS)
    for root in roots:
        if not root.exists():
            continue
        for path in ensure_plain_tree(root, "Summoning admission input"):
            checked += 1
            relative = path.relative_to(tree).as_posix()
            text = ""
            if path.suffix in ADMISSION_TEXT_SUFFIXES:
                text = path.read_text(encoding="utf-8", errors="replace")
            path_tokens = set(GENERATED_COHORT_TOKEN.findall(relative))
            text_tokens = set(GENERATED_COHORT_TOKEN.findall(text))
            tokens = path_tokens | text_tokens
            for token in sorted(tokens):
                checked += 1
                review_only = cohort_is_review_only(token, admission)
                base_cohort_path = (
                    token == BASE_COHORT_CONFIG_TOKEN
                    and relative in BASE_COHORT_CONFIG_PATHS
                    and token in path_tokens
                    and token not in text_tokens
                )
                if review_only:
                    review_only_hits += 1
                elif not base_cohort_path and not cohort_is_admitted(token, admission):
                    errors.append(f"unadmitted generated cohort {token!r} reaches {relative}")
                if re.search(r"(?:^|_)pet(?:_|$)", token) and not review_only:
                    errors.append(f"pet cohort {token!r} is deferred to Phase 7 ({relative})")

            # The existing skill-guide taxonomy legitimately says “pets”; only
            # a direct `summoning_pet_*` record or a generated roster cohort
            # constitutes out-of-scope pet content here.
            for token in sorted(set(DIRECT_PET_RECORD_TOKEN.findall(relative))):
                checked += 1
                if not pet_is_admitted(token, admission) and not review_only_tokens(relative, admission):
                    errors.append(f"pet record {token!r} is deferred to Phase 7 ({relative})")
            for line in text.splitlines():
                for token in sorted(set(DIRECT_PET_RECORD_TOKEN.findall(line))):
                    checked += 1
                    if not pet_is_admitted(token, admission) and not review_only_tokens(line, admission):
                        errors.append(f"pet record {token!r} is deferred to Phase 7 ({relative})")

            if NPC_SOUNDS_YES.search(text):
                checked += 1
                errors.append(f"npc_sounds=yes opens an unreviewed synth closure ({relative})")

            path_synth_sources = {int(source_id) for source_id in SYNTH_SOURCE_TOKEN.findall(relative)}
            for source_id in sorted(path_synth_sources):
                checked += 1
                if source_id not in admission.safe_synths and not review_only_tokens(relative, admission):
                    errors.append(f"unsafe synth source {source_id} reaches {relative}")
            for line in text.splitlines():
                for source_id_text in SYNTH_SOURCE_TOKEN.findall(line):
                    source_id = int(source_id_text)
                    checked += 1
                    if source_id not in admission.safe_synths and not review_only_tokens(line, admission):
                        errors.append(f"unsafe synth source {source_id} reaches {relative}")

    if checked == 0:
        errors.append("roster admission executed zero checks")
    if errors:
        joined = "\n".join(f"  - {error}" for error in errors[:20])
        suffix = "\n  - ..." if len(errors) > 20 else ""
        raise fail(f"roster admission rejected {len(errors)} artifact(s):\n{joined}{suffix}")
    print(
        f"stage_summoning_overlay: roster admission {checked} checks, 0 errors; "
        f"{review_only_hits} review-only references held out"
    )
    return checked


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
    files = ensure_plain_tree(source, "Summoning source")
    for source_file in files:
        copy_file(source_file, destination / source_file.relative_to(source))
    return len(files)


def copy_admitted_tree(
    source: Path, destination: Path, admission: RosterAdmission
) -> tuple[int, int]:
    """Copy an input subtree while holding named review-only work out of stage.

    Generated config/assets use cohort names in their filenames, but allocation
    and member-pack files mix accepted records with review-only records.  The
    latter are filtered line-by-line into the disposable stage; the originals
    remain untouched in the content tree for later Phase-5b review.
    """
    if not source.exists():
        return 0, 0
    copied = 0
    withheld = 0
    for source_file in ensure_plain_tree(source, "Summoning source"):
        relative = source_file.relative_to(source)
        if review_only_tokens(relative.as_posix(), admission):
            withheld += 1
            continue
        if source_file.suffix in REVIEW_FILTER_SUFFIXES:
            raw = source_file.read_bytes()
            lines = raw.splitlines(keepends=True)
            held_lines = [
                line for line in lines
                if review_only_tokens(line.decode("latin-1"), admission)
            ]
            if held_lines:
                retained = b"".join(line for line in lines if line not in held_lines)
                withheld += len(held_lines)
                if not retained:
                    continue
                destination_file = destination / relative
                destination_file.parent.mkdir(parents=True, exist_ok=True)
                destination_file.write_bytes(retained)
                copied += 1
                continue
        elif source_file.suffix in ADMISSION_TEXT_SUFFIXES:
            text = source_file.read_bytes().decode("latin-1")
            if review_only_tokens(text, admission):
                raise fail(
                    f"review-only cohort must live in an isolated file or line-oriented pack: {source_file}"
                )
        copy_file(source_file, destination / relative)
        copied += 1
    return copied, withheld


def assert_review_only_absent(root: Path, admission: RosterAdmission) -> int:
    """Prove that a preserved experiment did not leak into the staged tree."""
    errors: list[str] = []
    checked = 0
    for path in ensure_plain_tree(root, "staged Summoning output"):
        checked += 1
        relative = path.relative_to(root).as_posix()
        tokens = review_only_tokens(relative, admission)
        if path.suffix in ADMISSION_TEXT_SUFFIXES:
            tokens.update(review_only_tokens(path.read_bytes().decode("latin-1"), admission))
        if tokens:
            errors.append(f"review-only cohort {sorted(tokens)!r} leaked to {relative}")
    if checked == 0:
        errors.append("staged roster exclusion executed zero checks")
    if errors:
        joined = "\n".join(f"  - {error}" for error in errors[:20])
        suffix = "\n  - ..." if len(errors) > 20 else ""
        raise fail(f"review-only roster exclusion rejected {len(errors)} artifact(s):\n{joined}{suffix}")
    print(f"stage_summoning_overlay: review-only exclusion {checked} checks, 0 errors")
    return checked


def apply_interface_overlays(tree: Path, lane: Path, out: Path) -> int:
    """Append marked component fragments to pristine interface sources.

    An interface archive is encoded as one complete ``.if``/``.compack`` pair,
    so a feature lane cannot express three appended toplevel components as a
    second cache record.  Keep the authored delta in the lane and compose the
    complete source only in the disposable feature-on stage.
    """
    # Keep the ordinary ``interfaces/`` shape below this root so the
    # ServerScript compiler can consume the same component fragments through
    # an additional --component-root without teaching it a staging convention.
    overlay_root = lane / INTERFACE_OVERLAYS / "interfaces"
    if not overlay_root.exists():
        return 0
    fragments = ensure_plain_tree(overlay_root, "Summoning interface overlays")
    copied = 0
    for fragment in sorted(fragments):
        relative = fragment.relative_to(overlay_root)
        if relative.suffix not in {".if", ".compack"}:
            raise fail(f"interface overlay must be .if or .compack: {fragment}")
        source = tree / "interfaces" / relative
        destination = out / "interfaces" / relative
        if not source.is_file() or source.is_symlink():
            raise fail(f"interface overlay has no plain base source: {source}")
        copy_file(source, destination)
        base = destination.read_bytes()
        addition = fragment.read_bytes()
        separator = b"" if not base or base.endswith(b"\n") else b"\n"
        destination.write_bytes(base + separator + addition)
        copied += 1
    return copied


def split_config_records(path: Path) -> dict[str, list[str]]:
    records: dict[str, list[str]] = {}
    current: str | None = None
    # The committed cache text is byte-preserving Latin-1; all.enum contains
    # an intentional 0xa0 that is not valid standalone UTF-8.
    for line in path.read_text(encoding="latin-1").splitlines():
        if line.startswith("[") and line.endswith("]"):
            current = line
            if current in records:
                raise fail(f"duplicate config record {current} in {path}")
            records[current] = [line]
        elif current is not None:
            records[current].append(line)
    return records


def apply_config_overlays(tree: Path, lane: Path, out: Path) -> int:
    """Expand append-only record fragments against the pristine config tree."""
    overlay_root = lane / CONFIG_OVERLAYS
    if not overlay_root.exists():
        return 0
    fragments = ensure_plain_tree(overlay_root, "Summoning config overlays")
    copied = 0
    for fragment in sorted(fragments):
        if fragment.suffix not in CONFIG_SUFFIXES:
            raise fail(f"config overlay has unsupported suffix: {fragment}")
        base = tree / "configs" / f"all{fragment.suffix}"
        if not base.is_file() or base.is_symlink():
            raise fail(f"config overlay has no plain base source: {base}")
        base_records = split_config_records(base)
        additions = split_config_records(fragment)
        composed: list[str] = []
        for header, extra_lines in additions.items():
            if header not in base_records:
                raise fail(f"config overlay record is absent from {base}: {header}")
            body = list(base_records[header])
            while body and body[-1] == "":
                body.pop()
            body.extend(extra_lines[1:])
            composed.extend(body)
            composed.append("")
        destination = out / "configs" / LANE / fragment.name
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text("\n".join(composed), encoding="latin-1")
        copied += 1
    return copied


def reset_output(tree: Path, out: Path) -> None:
    tree = tree.resolve()
    out = out.resolve()
    if out == tree or tree in out.parents:
        raise fail("output must not be the content tree or a child of it")
    if out == REPO_ROOT or out == REPO_ROOT.parent or out == Path(out.anchor):
        raise fail(f"refusing broad output path: {out}")
    if out.exists():
        if out.is_symlink() or not out.is_dir():
            raise fail(f"existing output is not a plain directory: {out}")
        shutil.rmtree(out)
    out.mkdir(parents=True)


def stage(tree: Path, out: Path, boundary_path: Path) -> int:
    tree = tree.resolve()
    out = out.resolve()
    lane = tree / LANE
    if not (lane / "PROVENANCE.md").is_file():
        raise fail(f"missing lane marker {lane / 'PROVENANCE.md'}")
    ensure_plain_tree(lane, "Summoning lane")
    admission = load_roster_boundary(boundary_path.resolve())
    audit_roster_admission(tree, lane, boundary_path.resolve())
    reset_output(tree, out)

    copied = 0
    withheld = 0
    for required in (tree / "meta.ini", tree / "content.ini"):
        if not required.is_file() or required.is_symlink():
            raise fail(f"missing plain support file: {required}")
        copy_file(required, out / required.name)
        copied += 1
    copied += copy_tree(tree / "fields", out / "fields")

    # Full member indexes are lookup context only.  They allow imported blocks
    # to name existing records without placing those base records in this tree.
    compacks = sorted((tree / "configs").glob("*.compack"))
    if not compacks:
        raise fail(f"no config member indexes under {tree / 'configs'}")
    for compack in compacks:
        if compack.is_symlink():
            raise fail(f"config member index is a symlink: {compack}")
        copy_file(compack, out / "configs" / compack.name)
        copied += 1

    # Client dbrows and DB_FIND indexes are one derived unit. The feature tree
    # adds rows to cache tables 212/213, so the stage needs the base row/schema
    # text and index templates as lookup context before gen_dbindex appends the
    # authored rows. These copies exist only in the disposable flag-on stage.
    for name in ("all.dbrow", "all.dbtable"):
        source = tree / "configs" / name
        if not source.is_file() or source.is_symlink():
            raise fail(f"missing plain database source: {source}")
        copy_file(source, out / "configs" / name)
        copied += 1
    copied += copy_tree(tree / "dbindex", out / "dbindex")

    dbrow_alloc = tree / "pack" / "dbrow.alloc"
    if not dbrow_alloc.is_file() or dbrow_alloc.is_symlink():
        raise fail(f"missing plain dbrow allocation ledger: {dbrow_alloc}")
    copy_file(dbrow_alloc, out / "pack" / "dbrow.alloc")
    copied += 1
    dbindex_pack = tree / "pack" / "21_dbtableindex.pack"
    if not dbindex_pack.is_file() or dbindex_pack.is_symlink():
        raise fail(f"missing plain dbindex archive pack: {dbindex_pack}")
    copy_file(dbindex_pack, out / "pack" / "21_dbtableindex.pack")
    copied += 1

    # Lane directories mirror their destination in the staged root.  Config
    # records may also be placed directly under the lane; keep those in a
    # provenance-named subdirectory of the config walk.
    for child in sorted(lane.iterdir()):
        if child.name == "PROVENANCE.md":
            continue
        if child.is_dir() and child.name in {INTERFACE_OVERLAYS, CONFIG_OVERLAYS}:
            continue
        if child.is_dir() and child.name in {"configs", "interfaces", "scripts", "pack", *ASSET_ROOTS}:
            copied_here, withheld_here = copy_admitted_tree(child, out / child.name, admission)
            copied += copied_here
            withheld += withheld_here
        elif child.is_file() and child.suffix in CONFIG_SUFFIXES:
            if review_only_tokens(child.name, admission):
                withheld += 1
            else:
                copy_file(child, out / "configs" / LANE / child.name)
                copied += 1
        else:
            raise fail(f"unclassified lane entry: {child}")

    for name in CLIENTSCRIPT_OVERLAYS:
        source = tree / "scripts" / name
        if not source.is_file() or source.is_symlink():
            raise fail(f"missing plain clientscript overlay source: {source}")
        copy_file(source, out / "scripts" / name)
        copied += 1

    copied += apply_interface_overlays(tree, lane, out)
    copied += apply_config_overlays(tree, lane, out)

    # Large binary assets stay in their native roots in the source tree, but
    # only the provenance-marked subtree is copied into the second-pass view.
    for root_name in ASSET_ROOTS:
        source = tree / root_name / LANE
        copied_here, withheld_here = copy_admitted_tree(source, out / root_name / LANE, admission)
        copied += copied_here
        withheld += withheld_here

    if admission.review_only_cohorts and withheld == 0:
        raise fail("review-only cohort is declared but stage withheld zero artifacts")

    generated = subprocess.run(
        [sys.executable, str(REPO_ROOT / "tools/gen_dbindex.py"),
         "--content", str(out), "--write"],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if generated.returncode != 0:
        raise fail(f"dbindex regeneration failed:\n{generated.stdout}")
    print(generated.stdout, end="")
    assert_review_only_absent(out, admission)

    if copied == 0:
        raise fail("staged zero files")
    print(
        f"stage_summoning_overlay: staged {copied} files in {out}; "
        f"withheld {withheld} review-only artifacts/rows"
    )
    return 0


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def cache_files(root: Path) -> dict[Path, Path]:
    files = ensure_plain_tree(root.resolve(), "cache")
    return {path.relative_to(root.resolve()): path for path in files}


def compare_cache(before: Path, after: Path) -> int:
    left = cache_files(before)
    right = cache_files(after)
    errors: list[str] = []
    left_names = set(left)
    right_names = set(right)
    for missing in sorted(left_names - right_names):
        errors.append(f"missing after: {missing}")
    for added in sorted(right_names - left_names):
        errors.append(f"added after: {added}")
    checked = 0
    for relative in sorted(left_names & right_names):
        checked += 1
        if not filecmp.cmp(left[relative], right[relative], shallow=False):
            errors.append(
                f"bytes differ: {relative} "
                f"({digest(left[relative])} != {digest(right[relative])})"
            )
    if checked == 0:
        errors.append("cache comparison executed zero file checks")
    for error in errors:
        print(f"stage_summoning_overlay: error: {error}", file=sys.stderr)
    print(f"stage_summoning_overlay: compared {checked} cache files, {len(errors)} errors")
    return 1 if errors else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, default=DEFAULT_TREE)
    parser.add_argument("--boundary", type=Path, default=DEFAULT_BOUNDARY)
    parser.add_argument("--out", type=Path, help="directory to replace with the staged overlay")
    parser.add_argument(
        "--compare-cache",
        nargs=2,
        metavar=("BEFORE", "AFTER"),
        type=Path,
        help="assert two cache directories are byte-identical",
    )
    args = parser.parse_args()
    try:
        if args.compare_cache:
            if args.out is not None:
                parser.error("--out and --compare-cache are mutually exclusive")
            return compare_cache(*args.compare_cache)
        if args.out is None:
            parser.error("--out is required when staging")
        return stage(args.tree, args.out, args.boundary)
    except ValueError as exc:
        print(exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
