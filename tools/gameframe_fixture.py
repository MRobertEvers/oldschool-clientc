#!/usr/bin/env python3
"""Read-only provenance/freshness gate for the existing gameframe harness.

Uses the launcher's manifest parser and freshness predicates. Never rebuilds
shared content, touches timestamps, or accepts a freshness override as evidence.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import struct
import sys
import urllib.request
import zlib

from launcher.profiles import Manifest
from launcher.staleness import check_derived, coverage_gaps, _is_pristine


def digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def git_state(path):
    def run(*args):
        return subprocess.check_output(["git", "-C", str(path), *args], text=True).strip()
    changed = run("diff", "--name-only").splitlines()
    return {"path": str(path), "commit": run("rev-parse", "HEAD"),
            "status": run("status", "--short"), "modified_files": {
                name: digest(Path(path) / name) for name in changed if (Path(path) / name).is_file()}}


def inspect(repo, binary, manifest_path, revision):
    manifest = Manifest.load(str(manifest_path))
    report = {"revision": revision, "source": git_state(Path(__file__).resolve().parents[1]),
              "runtime": str(repo), "manifest": str(manifest_path),
              "manifest_sha256": digest(manifest_path), "binary": str(binary),
              "binary_sha256": digest(binary), "derived": [], "files": {}, "blockers": []}
    report["inputs"] = {key: os.environ.get(key) for key in (
        "GF_MATRIX_BASELINE", "GF_MATRIX_TAGS", "GF_MATRIX_MAX_FRAMES", "GF_MATRIX_LC_SAVE",
        "TORIRS_SIM_CMD", "TORIRS_SIM_CLICK_AT", "TORIRS_SIM_RESIZE", "TORIRS_SIM_HOVER",
        "TORIRS_CLIENTTYPE", "TORIRS_REVCONFIG_PLATFORM", "SDL_VIDEODRIVER")}
    expected = {"rs289lc": ("289", "lc289", "cs1"), "osrs239": ("239", "osrs239", "cs2")}
    actual = (manifest.revision, manifest.rev, manifest.ini.get("ui:boot", "logic"))
    if actual != expected[revision]:
        report["blockers"].append(f"revision/codec/logic mismatch: {actual}")
    for key in ("TORIRSSERVER_ALLOW_STALE_SCRIPTS", "TORIRS_SKIP_CHECKS"):
        if key in os.environ:
            report["blockers"].append(f"freshness override present: {key}")
    report["blockers"] += [str(gap) for gap in coverage_gaps(manifest)]
    if not list(manifest.derived()):
        report["blockers"].append("manifest has no derived-artifact declarations")
    for name, fields in manifest.derived():
        consumed = {"cache": manifest.cache_dir, "scripts": manifest.server_scripts}.get(name)
        if consumed and fields.get("out") and Path(manifest.resolve_path(fields["out"])).resolve() != Path(consumed).resolve():
            report["blockers"].append(f"{name}: checked out= differs from consumed artifact {consumed}")
        if _is_pristine(fields):
            report["derived"].append({"name": name, "state": "external/pristine", "note": fields.get("note")})
        else:
            stale, detail = check_derived(manifest, name, fields, str(repo))
            report["derived"].append({"name": name, "state": "stale" if stale else "fresh", "detail": detail})
            if stale:
                report["blockers"].append(f"{name}: {detail}")
    for directory in (manifest.cache_dir, manifest.server_scripts):
        if directory:
            path = Path(directory)
            if not path.is_dir():
                report["blockers"].append(f"missing artifact directory: {path}")
            else:
                for member in sorted(path.rglob("*")):
                    if member.is_file():
                        report["files"][str(member)] = digest(member)
    for key in ("revconfig_ui", "revconfig_cache"):
        value = manifest.ini.get("ui:boot", key)
        if value:
            path = Path(manifest.resolve_path(value))
            report["files"][str(path)] = digest(path)
    if revision == "rs289lc":
        if manifest.ini.get("cache:boot", "source") != "ondemand":
            report["blockers"].append("rs289lc baseline requires the server's on-demand cache")
        host = os.environ.get("TORIRS_WS_HOST", manifest.ini.get("net:boot", "ws_host") or manifest.net_host)
        port = os.environ.get("TORIRS_WS_PORT", manifest.ws_port or "80")
        with urllib.request.urlopen(f"http://{host}:{port}/crc", timeout=10) as response:
            crc = response.read()
        report["server_crc_hex"] = crc.hex()
        if len(crc) != 40:
            report["blockers"].append(f"invalid LostCity checksum response: {len(crc)} bytes")
        else:
            report["jag_archives"] = {}
            for index, name in enumerate(("title", "config", "interface", "media", "versionlist", "textures", "wordenc", "sounds"), 1):
                with urllib.request.urlopen(f"http://{host}:{port}/{name}", timeout=10) as response:
                    data = response.read()
                expected_crc = struct.unpack_from(">I", crc, index * 4)[0]
                actual_crc = zlib.crc32(data) & 0xffffffff
                report["jag_archives"][name] = {"sha256": hashlib.sha256(data).hexdigest(),
                    "expected_crc": expected_crc, "actual_crc": actual_crc}
                if expected_crc != actual_crc:
                    report["blockers"].append(f"{name}: downloaded archive does not match login CRC")
        server_root = os.environ.get("GF_MATRIX_LC_SERVER")
        if not server_root:
            report["blockers"].append("GF_MATRIX_LC_SERVER must name the matching LostCity_Server checkout")
        else:
            root = Path(server_root)
            report["server"] = git_state(root / "engine")
            report["content"] = git_state(root / "content")
            world = json.loads((root / "engine/data/config/world.json").read_text())
            if world.get("engine", {}).get("revision") != 289:
                report["blockers"].append("LostCity checkout is not configured for revision 289")
            for path in [root / "engine/data/config/world.json", root / "engine/package-lock.json"]:
                report["files"][str(path)] = digest(path)
            for path in sorted((root / "engine/data/pack").rglob("*")):
                if path.is_file():
                    report["files"][str(path)] = digest(path)
    report["accepted"] = not report["blockers"]
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--revision", choices=("rs289lc", "osrs239"), required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        report = inspect(args.repo.resolve(), args.binary.resolve(), args.manifest.resolve(), args.revision)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        report = {"accepted": False, "blockers": [str(error)]}
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    for blocker in report["blockers"]:
        print(f"FIXTURE BLOCKED: {blocker}")
    print(f"FIXTURE {'PASS' if report['accepted'] else 'BLOCKED'}: {args.out}")
    return 0 if report["accepted"] else 2


if __name__ == "__main__":
    sys.exit(main())
