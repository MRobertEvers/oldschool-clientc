#!/usr/bin/env python3
"""Capture and validate deterministic God Wars runtime evidence.

The focused native-server suite records every assertion as TSV. This wrapper
also proves that the retained run contains a launch trace for every row in the
classic combat manifest and the named multiplayer/Nex acceptance branches.
"""

from __future__ import annotations

import argparse
import csv
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = (
    ROOT
    / "OSRS-Content/osrs239-content/wiki/godwars_combat_manifest.csv"
)
DEFAULT_SERVER = Path("src/build_opt/mock230")
DEFAULT_SCRIPTS = Path(
    "OSRS-Content/osrs239-content/server/scripts/build"
)
DEFAULT_OUTPUT = Path("docs/evidence/godwars_runtime_trace.tsv")


def repo_path(path: Path) -> Path:
    return path if path.is_absolute() else ROOT / path


def runtime_path(path: Path) -> str:
    """Keep repository paths relative so checked evidence is machine-stable."""
    resolved = repo_path(path).resolve()
    try:
        return str(resolved.relative_to(ROOT.resolve()))
    except ValueError:
        return str(resolved)


def run_contract(command: list[str]) -> None:
    result = subprocess.run(command, cwd=ROOT, text=True, check=False)
    if result.returncode:
        raise SystemExit(f"contract command failed ({result.returncode}): {' '.join(command)}")


def tail(path: Path, count: int = 120) -> str:
    lines = path.read_text(errors="replace").splitlines()
    return "\n".join(lines[-count:])


def capture(server: Path, scripts: Path, destination: Path) -> None:
    server_abs = repo_path(server).resolve()
    scripts_env = runtime_path(scripts)
    if not server_abs.is_file():
        raise SystemExit(f"God Wars server binary does not exist: {server_abs}")
    if not (ROOT / scripts_env).is_dir() and not Path(scripts_env).is_dir():
        raise SystemExit(f"God Wars script pack does not exist: {scripts_env}")

    env = os.environ.copy()
    env.update(
        {
            "MOCK230_REV": "osrs239",
            "MOCK230_SCRIPTS": scripts_env,
            "MOCK230_SELFTEST_GWD_ONLY": "1",
            "MOCK230_SELFTEST_EVIDENCE": str(destination),
        }
    )
    with tempfile.NamedTemporaryFile(
        prefix="godwars_runtime_", suffix=".log", delete=False
    ) as stream:
        log = Path(stream.name)
        result = subprocess.run(
            [str(server_abs), "--selftest"],
            cwd=ROOT,
            env=env,
            stdout=stream,
            stderr=subprocess.STDOUT,
            check=False,
            timeout=180,
        )
    try:
        if result.returncode:
            raise SystemExit(
                f"God Wars focused runtime failed ({result.returncode})\n{tail(log)}"
            )
    finally:
        log.unlink(missing_ok=True)


def require_message(messages: set[str], needle: str) -> None:
    if not any(needle in message for message in messages):
        raise AssertionError(f"runtime evidence is missing: {needle}")


def validate(path: Path) -> tuple[int, int, int]:
    lines = path.read_text().splitlines()
    if len(lines) < 5 or lines[0] != "mock230-selftest-evidence-v1":
        raise AssertionError("not mock230 God Wars evidence version 1")
    if lines[1] != "wire\tosrs239":
        raise AssertionError(f"wrong runtime wire: {lines[1] if len(lines) > 1 else 'missing'}")
    if lines[2] != "index\tstatus\tmessage":
        raise AssertionError("evidence assertion header is missing")

    assertions: list[tuple[int, str, str]] = []
    for line in lines[3:-1]:
        fields = line.split("\t", 2)
        if len(fields) != 3:
            raise AssertionError(f"malformed evidence assertion: {line!r}")
        assertions.append((int(fields[0]), fields[1], fields[2]))
    summary = lines[-1].split("\t")
    if len(summary) != 4 or summary[:2] != ["summary", "godwars-focused"]:
        raise AssertionError(f"wrong evidence summary: {lines[-1]!r}")
    expected_count, failures = int(summary[2]), int(summary[3])
    if expected_count != len(assertions):
        raise AssertionError(
            f"summary says {expected_count} checks but contains {len(assertions)}"
        )
    if expected_count < 4000:
        raise AssertionError(f"focused suite unexpectedly shrank to {expected_count} checks")
    if failures or any(status != "PASS" for _, status, _ in assertions):
        failed = [message for _, status, message in assertions if status != "PASS"]
        raise AssertionError(f"runtime evidence contains failures: {failed[:10]}")
    for expected, (actual, _, _) in enumerate(assertions, 1):
        if actual != expected:
            raise AssertionError(f"non-contiguous assertion index {actual}, expected {expected}")

    messages = {message for _, _, message in assertions}
    with MANIFEST.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    npcs = {row["gameval"] for row in rows}
    if len(rows) != 126 or len(npcs) != 69:
        raise AssertionError(
            f"classic manifest has {len(npcs)} NPCs/{len(rows)} attacks, expected 69/126"
        )
    for row in rows:
        require_message(
            messages,
            f'{row["gameval"]}/{row["attack_name"]} trace launches animation ',
        )

    # Nex's autos and specials are outside the 69-NPC classic ledger.
    for phase in (1, 3, 4, 5):
        require_message(messages, f"[proc,nex_magic_auto] phase {phase} selects")
    for phase in (1, 5):
        require_message(messages, f"[proc,nex_melee_auto] phase {phase} selects")
    require_message(messages, "Nex Shadow auto selects nex_alternate_cast_attack")
    for special in ("smoke", "shadow", "blood", "ice"):
        for branch in (0, 1):
            require_message(
                messages, f"[proc,nex_special_{special}] branch {branch} executes"
            )
    for phase in (1, 3, 4, 5):
        require_message(messages, f"Nex phase {phase} landing deals")
    for marker in (
        "a salamander always breaks an Ice Prison wall",
        "Nex Ice Prison hits every player still in the armed 3x3",
        "Graardor queues all four in-room players and excludes the boundary outsider",
        "Kree'arra queues all four in-room players and excludes the boundary outsider",
        "godwars_saradomin_avatar magic queues only its current target",
        "godwars_zamorak_avatar magic queues only its current target",
        "Nex selects only the nearest of five players",
        "Nex mass barrage queues exactly one landing per participant",
        "private Nex room admits player 20 and rejects player 21",
        "Nex owner-loss soak completes 128 release/logout/death cycles",
        "two distinct Nex instance reservations coexist",
        "first Nex instance targets/projects only to its resident",
        "second Nex instance targets/projects only to its resident",
        "parallel Nex teardown releases both reservations",
    ):
        require_message(messages, marker)

    return len(npcs), len(rows), len(assertions)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", type=Path, default=DEFAULT_SERVER)
    parser.add_argument("--scripts", type=Path, default=DEFAULT_SCRIPTS)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--check",
        action="store_true",
        help="run again and require byte-for-byte agreement with the retained artifact",
    )
    parser.add_argument(
        "--skip-contracts",
        action="store_true",
        help="skip the manifest and source-contract preflight",
    )
    args = parser.parse_args()

    if not args.skip_contracts:
        run_contract(["python3", "tools/generate_godwars_combat_manifest.py", "--check"])
        run_contract(["python3", "tools/check_godwars_contract.py"])

    output = repo_path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        prefix="godwars_evidence_", suffix=".tsv", dir=output.parent, delete=False
    ) as stream:
        generated = Path(stream.name)
    try:
        capture(args.server, args.scripts, generated)
        npcs, attacks, assertions = validate(generated)
        if args.check:
            if not output.is_file():
                raise SystemExit(f"retained God Wars evidence is missing: {output}")
            validate(output)
            if generated.read_bytes() != output.read_bytes():
                raise SystemExit(
                    "God Wars runtime evidence drifted; rerun without --check to refresh it"
                )
        else:
            generated.replace(output)
        mode = "verified" if args.check else "captured"
        print(
            f"God Wars runtime evidence {mode}: "
            f"{npcs} NPCs, {attacks} attacks, {assertions} assertions"
        )
    finally:
        generated.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
