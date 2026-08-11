#!/usr/bin/env python3
"""Structural and staging acceptance for the Phase-6a private BoB container.

This is deliberately a container *foundation*, not a shop or a Beast-of-Burden
UI.  It admits one cache-native, player-owned thirty-slot inventory into the
feature-on Summoning overlay, proves that its allocation/client membership stay
in that lane, and leaves shared scope, stock, restock, and transfer policy to a
later dedicated slice.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import io
import subprocess
import sys
import tempfile
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from types import ModuleType


REPO = Path(__file__).resolve().parents[1]
DEFAULT_TREE = REPO / "OSRS-Content/osrs239-content"
STAGER = REPO / "tools/stage_summoning_overlay.py"
ALLOCATOR = REPO / "tools/ss_allocate.py"

LANE_REL = Path("ported/scape2009_summoning")
BOB = "summoning_bob"
BOB_ID = 2001
BOB_SIZE = 30
REVIEW = "summoning_roster_530"


def load_module(name: str, path: Path) -> ModuleType:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def parse_sections(path: Path) -> dict[str, list[tuple[str, str]]]:
    """Read the small INI dialect used by fields and rank-1 configs."""
    sections: dict[str, list[tuple[str, str]]] = {}
    current: str | None = None
    for line_no, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        text = raw.strip()
        if not text or text.startswith((";", "#", "//")):
            continue
        if text.startswith("[") and text.endswith("]"):
            current = text[1:-1]
            if current in sections:
                raise ValueError(f"{path}:{line_no}: duplicate section {current!r}")
            sections[current] = []
            continue
        if current is None or "=" not in text:
            raise ValueError(f"{path}:{line_no}: expected section entry")
        key, value = (part.strip() for part in text.split("=", 1))
        sections[current].append((key, value))
    return sections


def values(entries: list[tuple[str, str]], label: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for key, value in entries:
        if key in result:
            raise ValueError(f"duplicate {label} key {key!r}")
        result[key] = value
    return result


def alloc_map(path: Path) -> dict[str, int]:
    result: dict[str, int] = {}
    ids: set[int] = set()
    for line_no, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        text = raw.strip()
        if not text or text.startswith("//"):
            continue
        if "=" not in text:
            raise ValueError(f"{path}:{line_no}: malformed allocation")
        left, right = (part.strip() for part in text.split("=", 1))
        if not left.isdecimal() or not right:
            raise ValueError(f"{path}:{line_no}: malformed allocation")
        ident = int(left)
        if ident in ids or right in result:
            raise ValueError(f"{path}:{line_no}: duplicate allocation")
        ids.add(ident)
        result[right] = ident
    return result


def membership(path: Path) -> list[str]:
    lines: list[str] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        text = raw.strip()
        if text and not text.startswith("//"):
            lines.append(text)
    return lines


def file_digest(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_bob_record(records: dict[str, list[tuple[str, str]]]) -> str | None:
    if set(records) != {BOB}:
        return f"expected only [{BOB}], got {sorted(records)}"
    if values(records[BOB], BOB) != {"size": str(BOB_SIZE)}:
        return f"[{BOB}] must state only size={BOB_SIZE}"
    return None


def validate_lane_membership(lines: list[str]) -> str | None:
    if lines != [BOB]:
        return f"inv.client must name only {BOB}, got {lines}"
    return None


def supports_inv_staging(config_suffixes: set[str], admission_suffixes: set[str]) -> bool:
    return ".inv" in config_suffixes and ".inv" in admission_suffixes


def marker_hits(root: Path, marker: str, text_suffixes: set[str]) -> list[str]:
    found: list[str] = []
    needle = marker.encode("utf-8")
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(root).as_posix()
        if marker in relative:
            found.append(relative)
            continue
        if path.suffix in text_suffixes and needle in path.read_bytes():
            found.append(relative)
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, default=DEFAULT_TREE)
    args = parser.parse_args()
    tree = args.tree.resolve()
    lane = tree / LANE_REL

    errors: list[str] = []
    checks = 0

    def expect(condition: bool, message: str) -> None:
        nonlocal checks
        checks += 1
        if not condition:
            errors.append(message)

    content = tree / "content.ini"
    fields = tree / "fields/inv.ini"
    config = lane / "configs/summoning_bob.inv"
    root_alloc = tree / "pack/inv.alloc"
    lane_alloc = lane / "pack/inv.alloc"
    lane_client = lane / "pack/inv.client"
    inputs = (content, fields, config, root_alloc, lane_alloc, lane_client)
    expect(all(path.is_file() for path in inputs), "Phase-6a source inputs are incomplete")
    if not all(path.is_file() for path in inputs):
        return finish(errors, checks)

    try:
        content_sections = parse_sections(content)
        expect(values(content_sections.get("namespace:inv", []), "namespace:inv") ==
               {"membership": "authored"},
               "content.ini must declare cache-named inv membership as authored only")

        field_sections = parse_sections(fields)
        expect(values(field_sections.get("inv.size", []), "inv.size") ==
               {"scope": "client", "client": "native"},
               "fields/inv.ini must expose only cache-native inv.size")
        expect(set(field_sections) == {"inv.size"},
               "fields/inv.ini widened into shared/shop semantics")

        record_error = validate_bob_record(parse_sections(config))
        expect(record_error is None, record_error or "invalid summoning_bob record")
        config_text = config.read_text(encoding="utf-8")
        expect("scope=" not in config_text.lower(),
               "summoning_bob must remain private by default, not declare scope")
        expect(not any(line.lstrip().startswith(";") for line in config_text.splitlines()),
               "cachepack config comments must use //, never INI-style ;")

        root = alloc_map(root_alloc)
        lane_mapping = alloc_map(lane_alloc)
        expect(root.get(BOB) == BOB_ID,
               f"root inv allocation is {root.get(BOB)!r}, expected {BOB_ID}")
        expect(lane_mapping == {BOB: BOB_ID},
               f"Summoning lane allocation changed: {lane_mapping}")
        expect(BOB_ID >= 2000 and BOB_ID not in {
            ident for name, ident in root.items() if name != BOB
        }, "summoning_bob allocation is not a distinct server-owned inv id")

        client_lines = membership(lane_client)
        membership_error = validate_lane_membership(client_lines)
        expect(membership_error is None, membership_error or "invalid inv.client")
        expect(not (lane / "pack/inv.server").exists(),
               "private BoB foundation must not invent an inv server-band roster")
    except (OSError, ValueError) as exc:
        errors.append(f"cannot parse Phase-6a source contract: {exc}")

    # Mutation controls hold the little validator helpers themselves to the
    # contract.  The normal checks above cover the committed files; these ensure
    # a missing client entry, altered capacity, or a lost staging suffix cannot
    # become a false green due to a weakened assertion.
    expect(validate_bob_record({BOB: [("size", "29")]}) is not None,
           "capacity mutation was accepted")
    expect(validate_lane_membership([]) is not None,
           "missing inv.client membership was accepted")
    try:
        stage_module = load_module("summoning_phase6_stager", STAGER)
    except (OSError, RuntimeError) as exc:
        errors.append(f"cannot load staging gate: {exc}")
    else:
        expect(supports_inv_staging(stage_module.CONFIG_SUFFIXES,
                                    stage_module.ADMISSION_TEXT_SUFFIXES),
               "stager does not classify and admission-audit .inv rank-1 configs")
        expect(".inv" not in stage_module.REVIEW_FILTER_SUFFIXES,
               "stager would line-filter a structured .inv config")
        expect(not supports_inv_staging(stage_module.CONFIG_SUFFIXES - {".inv"},
                                        stage_module.ADMISSION_TEXT_SUFFIXES),
               "lost .inv config suffix mutation was accepted")

        before = {path: file_digest(path) for path in inputs}
        with tempfile.TemporaryDirectory(prefix="summoning_phase6_stage_") as temporary:
            staged = Path(temporary) / "stage"
            output = io.StringIO()
            try:
                with redirect_stdout(output), redirect_stderr(output):
                    stage_result = stage_module.stage(tree, staged, REPO /
                                                      "docs/summoning_port/roster_boundary_530.json")
                    admission = stage_module.load_roster_boundary(
                        REPO / "docs/summoning_port/roster_boundary_530.json")
                    exclusion_checks = stage_module.assert_review_only_absent(staged, admission)
            except (OSError, ValueError) as exc:
                errors.append(f"feature-on staging failed: {exc}")
            else:
                expect(stage_result == 0 and exclusion_checks > 0,
                       "feature-on staging did not complete its admission audit")
                staged_config = staged / "configs/summoning_bob.inv"
                expect(staged_config.is_file(), "feature-on stage omitted summoning_bob.inv")
                if staged_config.is_file():
                    expect(validate_bob_record(parse_sections(staged_config)) is None,
                           "staged summoning_bob record changed")
                staged_alloc = staged / "pack/inv.alloc"
                staged_client = staged / "pack/inv.client"
                expect(staged_alloc.is_file() and alloc_map(staged_alloc) == {BOB: BOB_ID},
                       "feature-on stage omitted or changed the BoB allocation")
                expect(staged_client.is_file() and
                       validate_lane_membership(membership(staged_client)) is None,
                       "feature-on stage omitted or changed BoB client membership")
                expect((staged / "fields/inv.ini").is_file(),
                       "feature-on stage omitted the native inv field contract")
                review_hits = marker_hits(staged, REVIEW, stage_module.ADMISSION_TEXT_SUFFIXES)
                expect(not review_hits,
                       "feature-on stage leaked review-only roster data: " +
                       ", ".join(review_hits[:10]))
        after = {path: file_digest(path) for path in inputs}
        expect(after == before, "feature-on staging modified a Phase-6a source input")

    allocation = subprocess.run(
        [sys.executable, str(ALLOCATOR), "--tree", str(tree), "--namespace", "inv", "--check"],
        cwd=REPO,
        text=True,
        capture_output=True,
        check=False,
    )
    expect(allocation.returncode == 0,
           "ss_allocate --check rejected the inv allocation: " + allocation.stdout + allocation.stderr)

    return finish(errors, checks)


def finish(errors: list[str], checks: int) -> int:
    for error in errors:
        print(f"test_summoning_phase6: error: {error}", file=sys.stderr)
    print(f"test_summoning_phase6: {checks} checks, {len(errors)} errors")
    return 1 if errors or checks == 0 else 0


if __name__ == "__main__":
    raise SystemExit(main())
