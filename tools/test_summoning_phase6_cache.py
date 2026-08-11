#!/usr/bin/env python3
"""Read back the cache-native Phase-6a BoB inventory from a feature cache."""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
DEFAULT_CACHE = REPO / "cache.osrs239.summoning"
DEFAULT_CACHEPACK = REPO / "3rd/rscache/tools/cachepack/cachepack"

BOB_ID = 2001
BOB_SIZE = "30"


def parse_sections(path: Path) -> dict[str, dict[str, str]]:
    sections: dict[str, dict[str, str]] = {}
    current: str | None = None
    for line_no, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        text = raw.strip()
        if not text or text.startswith("//"):
            continue
        if text.startswith("[") and text.endswith("]"):
            current = text[1:-1]
            if current in sections:
                raise ValueError(f"{path}:{line_no}: duplicate section {current!r}")
            sections[current] = {}
            continue
        if current is None or "=" not in text:
            raise ValueError(f"{path}:{line_no}: expected config property")
        key, value = (part.strip() for part in text.split("=", 1))
        if key in sections[current]:
            raise ValueError(f"{path}:{line_no}: duplicate key {key!r}")
        sections[current][key] = value
    return sections


def parse_compack(path: Path) -> dict[int, str]:
    result: dict[int, str] = {}
    for line_no, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        text = raw.strip()
        if not text or text.startswith("//"):
            continue
        if "=" not in text:
            raise ValueError(f"{path}:{line_no}: malformed compack line")
        left, right = (part.strip() for part in text.split("=", 1))
        if not left.isdecimal() or not right:
            raise ValueError(f"{path}:{line_no}: malformed compack line")
        ident = int(left)
        if ident in result:
            raise ValueError(f"{path}:{line_no}: duplicate id {ident}")
        result[ident] = right
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, default=DEFAULT_CACHE)
    parser.add_argument("--cachepack", type=Path, default=DEFAULT_CACHEPACK)
    args = parser.parse_args()
    cache = args.cache.resolve()
    cachepack = args.cachepack.resolve()

    errors: list[str] = []
    checks = 0

    def expect(condition: bool, message: str) -> None:
        nonlocal checks
        checks += 1
        if not condition:
            errors.append(message)

    expect((cache / "main_file_cache.dat2").is_file(), f"no feature cache at {cache}")
    expect(cachepack.is_file(), f"no cachepack executable at {cachepack}")
    if errors:
        return finish(checks, errors)

    with tempfile.TemporaryDirectory(prefix="summoning_phase6_unpack_") as temporary:
        unpack = Path(temporary) / "unpack"
        result = subprocess.run(
            [str(cachepack), "unpack", "--cache", str(cache), "--rev", "osrs239",
             "--src", str(unpack), "--types", "inv"],
            cwd=REPO,
            text=True,
            capture_output=True,
            check=False,
        )
        expect(result.returncode == 0,
               "cachepack could not unpack feature invs: " + result.stdout + result.stderr)
        invs = unpack / "configs/all.inv"
        names = unpack / "configs/all.inv.compack"
        expect(invs.is_file() and names.is_file(), "feature cache unpack omitted inv records")
        if invs.is_file() and names.is_file():
            try:
                records = parse_sections(invs)
                compack = parse_compack(names)
            except (OSError, ValueError) as exc:
                errors.append(f"cannot read unpacked BoB inv: {exc}")
            else:
                cache_name = f"inv_{BOB_ID}"
                expect(compack.get(BOB_ID) == cache_name,
                       f"feature cache does not contain inv {BOB_ID}")
                expect(records.get(cache_name) == {"size": BOB_SIZE},
                       f"feature cache has wrong BoB record: {records.get(cache_name)!r}")
                # pack/<ns>.alloc deliberately remains server authority and must
                # not leak into the client game's gameval table just to name one
                # private container.  Runtime resolves `summoning_bob` from the
                # root allocation ledger while the cache addresses it by id.
                expect("summoning_bob" not in records,
                       "server-owned BoB symbol leaked into cache gamevals")

    return finish(checks, errors)


def finish(checks: int, errors: list[str]) -> int:
    for error in errors:
        print(f"test_summoning_phase6_cache: error: {error}", file=sys.stderr)
    print(f"test_summoning_phase6_cache: {checks} checks, {len(errors)} errors")
    return 1 if errors or checks == 0 else 0


if __name__ == "__main__":
    raise SystemExit(main())
