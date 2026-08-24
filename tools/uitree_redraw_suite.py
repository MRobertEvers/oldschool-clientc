#!/usr/bin/env python3
"""Deterministic correctness and performance A/B suite for UITree redraws.

The runner deliberately uses only the Python standard library.  It builds the
same C harness twice (against a detached baseline worktree and the candidate
tree), exercises full/forced/retained redraw modes, performs decoded-pixel BMP
comparisons, and reports paired-process benchmark statistics.
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import hashlib
import html
import json
import math
import os
from pathlib import Path
import platform
import random
import shlex
import shutil
import statistics
import struct
import subprocess
import sys
import tempfile
import time
import traceback
from typing import Any, Mapping, Sequence
import xml.etree.ElementTree as ET


SUITE_VERSION = 2
FRAME_PARITY_FIELDS = (
    "frame",
    "scenario",
    "checkpoint",
    "pixel_hash",
    "emit_hash",
    "emit_count",
)
BENCH_FIELDS = (
    "scenario",
    "elapsed_ns",
    "operations",
    "ns_per_op",
    "full_walks",
    "retained_frames",
    "emit_nodes",
)
EXPECTED_BENCH_SCENARIOS = (
    "steady",
    "camera",
    "overlay_position",
    "scroll",
    "hover",
    "content",
    "topology",
    "aggregate",
)
VISUAL_BASELINE_STALE_SCENARIOS = frozenset(("host-camera", "host-input"))
EXPECTED_VISUAL_TRACE = (
    ("initial", "initial"),
    ("steady", "steady"),
    ("hover", "hover_on"),
    ("hover", "hover_off"),
    ("scroll", "scroll_a"),
    ("scroll", "scroll_b"),
    ("content", "content_mutated"),
    ("transparency", "alpha_changed"),
    ("topology", "topology_replace"),
    ("host-camera", "camera_host_only"),
    ("projection", "fish_orbit_a"),
    ("projection", "fish_orbit_b"),
    ("projection", "fish_viewport_clipped"),
    ("projection", "fish_near_plane_hidden"),
    ("projection", "fish_revealed"),
    ("shape", "arc_sprite_rotated"),
    ("host-input", "cs1_active_on"),
    ("reachability", "hidden_subtree_revealed"),
    ("volatile", "overlay_zero_to_one"),
    ("volatile", "overlay_one_to_two"),
    ("volatile", "overlay_two_to_zero"),
    ("host-input", "cs1_active_off"),
    ("reachability", "subtree_hidden_again"),
    ("reset", "reset"),
)
BENCH_CAMERA_SCENARIOS = frozenset(("camera", "host-camera", "host-input"))
CORRECT_AGGREGATE_SCENARIOS = frozenset(
    ("steady", "overlay_position", "scroll", "hover", "content", "topology")
)
GUARD_SCENARIOS = frozenset(("overlay_position", "scroll", "hover", "content", "topology"))

BENCH_VARIANT_ORDERS = (
    ("baseline-retained", "candidate-retained", "candidate-forced"),
    ("candidate-retained", "candidate-forced", "baseline-retained"),
    ("candidate-forced", "baseline-retained", "candidate-retained"),
    ("candidate-forced", "candidate-retained", "baseline-retained"),
    ("baseline-retained", "candidate-forced", "candidate-retained"),
    ("candidate-retained", "baseline-retained", "candidate-forced"),
)

PROFILE_DEFAULTS = {
    "quick": {
        "visual_frames": 256,
        "bench_frames": 4000,
        "trials": 6,
        "bootstrap_samples": 5000,
    },
    "full": {
        "visual_frames": 2048,
        "bench_frames": 30000,
        "trials": 18,
        "bootstrap_samples": 20000,
    },
}


class SuiteError(RuntimeError):
    """A suite setup, execution, or input-contract failure."""


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds")


def json_dump(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def safe_rel(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return str(path)


def capture(argv: Sequence[str], cwd: Path | None = None) -> str:
    try:
        proc = subprocess.run(
            list(argv),
            cwd=str(cwd) if cwd else None,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
    except OSError as exc:
        return f"unavailable: {exc}"
    return proc.stdout.strip()


def git_value(root: Path, *args: str) -> str:
    try:
        proc = subprocess.run(
            ("git", "-C", str(root), *args),
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
    except OSError:
        return ""
    output = proc.stdout.strip()
    return output.splitlines()[0] if proc.returncode == 0 and output else ""


def stable_seed(text: str, base: int) -> int:
    raw = hashlib.sha256(text.encode("utf-8")).digest()
    return base ^ int.from_bytes(raw[:8], "big")


def percentile(sorted_values: Sequence[float], fraction: float) -> float:
    if not sorted_values:
        raise SuiteError("cannot calculate a percentile of an empty sample")
    if len(sorted_values) == 1:
        return sorted_values[0]
    position = fraction * (len(sorted_values) - 1)
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return sorted_values[lower]
    weight = position - lower
    return sorted_values[lower] * (1.0 - weight) + sorted_values[upper] * weight


def paired_bootstrap_ratio(
    baseline: Sequence[float],
    candidate: Sequence[float],
    samples: int,
    seed: int,
) -> tuple[float, float, float, list[float]]:
    if len(baseline) != len(candidate) or not baseline:
        raise SuiteError("paired bootstrap requires equal non-empty samples")
    ratios: list[float] = []
    for before, after in zip(baseline, candidate):
        if before <= 0.0 or after < 0.0:
            raise SuiteError("benchmark time values must be positive")
        ratios.append(after / before)
    estimate = statistics.median(ratios)
    generator = random.Random(seed)
    boot: list[float] = []
    count = len(ratios)
    for _ in range(samples):
        draw = [ratios[generator.randrange(count)] for _ in range(count)]
        boot.append(statistics.median(draw))
    boot.sort()
    return estimate, percentile(boot, 0.025), percentile(boot, 0.975), ratios


class BMP:
    """A decoded BMP in top-to-bottom, packed RGB byte order."""

    __slots__ = ("width", "height", "rgb")

    def __init__(self, width: int, height: int, rgb: bytes):
        self.width = width
        self.height = height
        self.rgb = rgb


def _masked_channel(pixel: int, mask: int) -> int:
    if mask == 0:
        return 0
    shift = (mask & -mask).bit_length() - 1
    raw = (pixel & mask) >> shift
    maximum = mask >> shift
    return (raw * 255 + maximum // 2) // maximum


def decode_bmp(path: Path) -> BMP:
    """Decode an uncompressed/bitfield 32-bit BMP, ignoring alpha and padding."""
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        raise SuiteError(f"{path}: not a BMP file")
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]
    if dib_size < 40 or 14 + dib_size > len(data):
        raise SuiteError(f"{path}: unsupported/truncated DIB header ({dib_size})")
    width, signed_height = struct.unpack_from("<ii", data, 18)
    planes, bits = struct.unpack_from("<HH", data, 26)
    compression = struct.unpack_from("<I", data, 30)[0]
    if width <= 0 or signed_height == 0 or planes != 1 or bits != 32:
        raise SuiteError(
            f"{path}: expected a non-empty 32-bit BMP "
            f"(got {width}x{signed_height}, planes={planes}, bits={bits})"
        )
    if compression not in (0, 3, 6):
        raise SuiteError(f"{path}: unsupported BMP compression {compression}")
    height = abs(signed_height)
    row_size = width * 4
    required = pixel_offset + row_size * height
    if required > len(data):
        raise SuiteError(f"{path}: truncated pixel array")

    red_mask, green_mask, blue_mask = 0x00FF0000, 0x0000FF00, 0x000000FF
    if compression in (3, 6):
        mask_offset = 14 + 40 if dib_size == 40 else 14 + 40
        if mask_offset + 12 > len(data) or mask_offset + 12 > pixel_offset:
            raise SuiteError(f"{path}: missing RGB bitfield masks")
        red_mask, green_mask, blue_mask = struct.unpack_from("<III", data, mask_offset)

    decoded = bytearray(width * height * 3)
    for output_y in range(height):
        source_y = output_y if signed_height < 0 else height - output_y - 1
        source = pixel_offset + source_y * row_size
        target = output_y * width * 3
        if compression == 0:
            # Slice assignment performs the common BGRX -> RGB shuffle in C.
            # The full profile decodes thousands of screenshots, so avoiding a
            # Python iteration per pixel materially shortens the oracle itself.
            row = data[source : source + row_size]
            end = target + width * 3
            decoded[target:end:3] = row[2::4]
            decoded[target + 1 : end : 3] = row[1::4]
            decoded[target + 2 : end : 3] = row[0::4]
            continue
        for x in range(width):
            pixel = struct.unpack_from("<I", data, source + x * 4)[0]
            red = _masked_channel(pixel, red_mask)
            green = _masked_channel(pixel, green_mask)
            blue = _masked_channel(pixel, blue_mask)
            offset = target + x * 3
            decoded[offset : offset + 3] = bytes((red, green, blue))
    return BMP(width, height, bytes(decoded))


def write_bmp(path: Path, image: BMP) -> None:
    """Write a deterministic top-down 32-bit BGRX BMP."""
    path.parent.mkdir(parents=True, exist_ok=True)
    row_size = image.width * 4
    pixel_size = row_size * image.height
    pixel_offset = 14 + 40
    header = struct.pack("<2sIHHI", b"BM", pixel_offset + pixel_size, 0, 0, pixel_offset)
    dib = struct.pack(
        "<IiiHHIIiiII",
        40,
        image.width,
        -image.height,
        1,
        32,
        0,
        pixel_size,
        2835,
        2835,
        0,
        0,
    )
    expected_rgb = image.width * image.height * 3
    if len(image.rgb) != expected_rgb:
        raise SuiteError(
            f"cannot write {path}: expected {expected_rgb} RGB bytes, got {len(image.rgb)}"
        )
    pixels = bytearray(b"\xff") * pixel_size
    pixels[0::4] = image.rgb[2::3]
    pixels[1::4] = image.rgb[1::3]
    pixels[2::4] = image.rgb[0::3]
    with path.open("wb") as stream:
        stream.write(header)
        stream.write(dib)
        stream.write(pixels)


def compare_bmps(before_path: Path, after_path: Path, diff_path: Path) -> dict[str, Any]:
    before = decode_bmp(before_path)
    after = decode_bmp(after_path)
    result: dict[str, Any] = {
        "before": str(before_path),
        "after": str(after_path),
        "before_sha256": hashlib.sha256(before.rgb).hexdigest(),
        "after_sha256": hashlib.sha256(after.rgb).hexdigest(),
        "dimensions_before": [before.width, before.height],
        "dimensions_after": [after.width, after.height],
        "different_pixels": None,
        "max_channel_delta": None,
        "bbox": None,
        "exact": False,
        "diff": str(diff_path),
    }
    if (before.width, before.height) != (after.width, after.height):
        # A valid diff still makes the dimensional failure easy to find in the gallery.
        width = max(before.width, after.width)
        height = max(before.height, after.height)
        write_bmp(diff_path, BMP(width, height, bytes((255, 0, 255)) * width * height))
        result["reason"] = "dimension mismatch"
        return result

    if before.rgb == after.rgb:
        write_bmp(
            diff_path,
            BMP(before.width, before.height, bytes(before.width * before.height * 3)),
        )
        result.update(
            {
                "different_pixels": 0,
                "max_channel_delta": 0,
                "bbox": None,
                "exact": True,
            }
        )
        return result

    different = 0
    maximum = 0
    min_x, min_y = before.width, before.height
    max_x = max_y = -1
    diff = bytearray(len(before.rgb))
    for index in range(before.width * before.height):
        offset = index * 3
        channels = tuple(
            abs(before.rgb[offset + channel] - after.rgb[offset + channel])
            for channel in range(3)
        )
        delta = max(channels)
        if delta:
            different += 1
            maximum = max(maximum, delta)
            x, y = index % before.width, index // before.width
            min_x, min_y = min(min_x, x), min(min_y, y)
            max_x, max_y = max(max_x, x), max(max_y, y)
            # Brighten small deltas so one-bit changes remain visible.
            diff[offset : offset + 3] = bytes(max(48, value) for value in channels)
    write_bmp(diff_path, BMP(before.width, before.height, bytes(diff)))
    result.update(
        {
            "different_pixels": different,
            "max_channel_delta": maximum,
            "bbox": [min_x, min_y, max_x, max_y] if different else None,
            "exact": different == 0,
        }
    )
    return result


class Suite:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.candidate_root = args.candidate_root.resolve()
        # Capture source state before creating an explicitly selected artifact
        # directory. This avoids treating the runner's own new --out directory
        # as pre-existing candidate dirt while preserving the actual start state.
        candidate_status_at_start = capture(
            (
                "git",
                "-C",
                str(self.candidate_root),
                "status",
                "--short",
                "--untracked-files=all",
            )
        )
        self.output = args.out.resolve()
        self.output.mkdir(parents=True, exist_ok=False)
        self.logs = self.output / "logs"
        self.logs.mkdir()
        self.started_at = utc_now()
        self.commands: list[dict[str, Any]] = []
        self.gates: list[dict[str, Any]] = []
        self.visual_results: list[dict[str, Any]] = []
        self.chrome_results: list[dict[str, Any]] = []
        self.frame_results: dict[str, Any] = {}
        self.benchmark_results: list[dict[str, Any]] = []
        self.raw_benchmarks: list[dict[str, Any]] = []
        self.fatal_error: str | None = None
        self.baseline_root: Path | None = None
        self.baseline_temp_parent: Path | None = None
        self.baseline_is_temporary = False
        self.baseline_cleanup = "not needed"
        self.baseline_commit = ""
        self.candidate_commit = ""
        self.baseline_tree = ""
        self.candidate_tree = ""
        self.baseline_status = ""
        self.candidate_status = candidate_status_at_start
        self.baseline_submodules = ""
        self.candidate_submodules = ""
        self.reproducibility_reasons: list[str] = []
        self.baseline_harness: Path | None = None
        self.candidate_harness: Path | None = None
        self.native_build_root: Path | None = None
        self.gallery_rows: list[dict[str, Any]] = []

    def add_gate(
        self,
        name: str,
        passed: bool,
        detail: str,
        category: str,
        **fields: Any,
    ) -> None:
        gate = {"name": name, "passed": bool(passed), "detail": detail, "category": category}
        gate.update(fields)
        self.gates.append(gate)

    def run_command(
        self,
        argv: Sequence[str | Path],
        *,
        cwd: Path,
        log_name: str,
        env: Mapping[str, str] | None = None,
    ) -> None:
        command = [str(value) for value in argv]
        log_path = self.logs / log_name
        merged_env = os.environ.copy()
        if env:
            merged_env.update(env)
        print(f"[uitree-redraw] {' '.join(command)}", flush=True)
        started = time.monotonic()
        try:
            with log_path.open("w", encoding="utf-8", newline="\n") as stream:
                stream.write("command: " + " ".join(command) + "\n")
                stream.write(f"cwd: {cwd}\n\n")
                stream.flush()
                proc = subprocess.run(
                    command,
                    cwd=str(cwd),
                    env=merged_env,
                    stdout=stream,
                    stderr=subprocess.STDOUT,
                    check=False,
                )
        except OSError as exc:
            elapsed = time.monotonic() - started
            self.commands.append(
                {
                    "argv": command,
                    "cwd": str(cwd),
                    "log": safe_rel(log_path, self.output),
                    "returncode": None,
                    "elapsed_seconds": elapsed,
                    "error": str(exc),
                }
            )
            raise SuiteError(f"could not execute {command[0]}: {exc}") from exc
        elapsed = time.monotonic() - started
        self.commands.append(
            {
                "argv": command,
                "cwd": str(cwd),
                "log": safe_rel(log_path, self.output),
                "returncode": proc.returncode,
                "elapsed_seconds": elapsed,
            }
        )
        if proc.returncode:
            raise SuiteError(
                f"command failed with exit {proc.returncode}: {' '.join(command)} "
                f"(see {log_path})"
            )

    def prepare_worktrees(self) -> None:
        if not (self.candidate_root / ".git").exists():
            # A linked worktree has a .git file, while a primary checkout has a directory.
            if not (self.candidate_root / ".git").is_file():
                raise SuiteError(f"candidate root is not a Git worktree: {self.candidate_root}")
        self.candidate_commit = git_value(self.candidate_root, "rev-parse", "HEAD")
        if not self.candidate_commit:
            raise SuiteError("could not resolve candidate HEAD")
        self.candidate_tree = git_value(self.candidate_root, "rev-parse", "HEAD^{tree}")
        resolved = git_value(
            self.candidate_root, "rev-parse", "--verify", f"{self.args.baseline_ref}^{{commit}}"
        )
        if not resolved:
            raise SuiteError(f"could not resolve baseline ref {self.args.baseline_ref!r}")
        self.baseline_commit = resolved

        if self.args.baseline_root:
            self.baseline_root = self.args.baseline_root.resolve()
            actual = git_value(self.baseline_root, "rev-parse", "HEAD")
            if not actual:
                raise SuiteError(f"baseline root is not a Git worktree: {self.baseline_root}")
            if not self.args.allow_baseline_mismatch and actual != self.baseline_commit:
                raise SuiteError(
                    f"--baseline-root is {actual}, but {self.args.baseline_ref} resolves to "
                    f"{self.baseline_commit}; use --allow-baseline-mismatch to make this explicit"
                )
            self.baseline_commit = actual
            self.baseline_tree = git_value(self.baseline_root, "rev-parse", "HEAD^{tree}")
            self.baseline_status = capture(
                ("git", "-C", str(self.baseline_root), "status", "--short", "--untracked-files=all")
            )
            self.validate_source_cleanliness()
            return

        self.baseline_temp_parent = Path(tempfile.mkdtemp(prefix="torirs-uitree-redraw-"))
        self.baseline_root = self.baseline_temp_parent / "baseline"
        self.run_command(
            (
                "git",
                "-C",
                self.candidate_root,
                "worktree",
                "add",
                "--detach",
                self.baseline_root,
                self.baseline_commit,
            ),
            cwd=self.candidate_root,
            log_name="baseline-worktree-add.log",
        )
        self.baseline_is_temporary = True
        self.baseline_tree = git_value(self.baseline_root, "rev-parse", "HEAD^{tree}")
        self.baseline_status = capture(
            ("git", "-C", str(self.baseline_root), "status", "--short", "--untracked-files=all")
        )
        self.validate_source_cleanliness()

    def validate_source_cleanliness(self) -> None:
        assert self.baseline_root is not None
        self.baseline_submodules = capture(
            ("git", "-C", str(self.baseline_root), "submodule", "status", "--recursive")
        )
        self.candidate_submodules = capture(
            ("git", "-C", str(self.candidate_root), "submodule", "status", "--recursive")
        )
        if self.baseline_status:
            raise SuiteError(
                "baseline worktree is dirty; a baseline override must be an exact committed tree:\n"
                + self.baseline_status
            )
        if self.candidate_status:
            self.reproducibility_reasons.append("candidate worktree was dirty")
            if not self.args.allow_dirty:
                raise SuiteError(
                    "candidate worktree is dirty; commit the candidate before collecting evidence "
                    "or use --allow-dirty for a development-only run:\n"
                    + self.candidate_status
                )
        if self.args.allow_dirty:
            self.reproducibility_reasons.append("--allow-dirty was supplied")
        if self.args.skip_build:
            self.reproducibility_reasons.append("externally built harnesses were supplied")
        if self.args.allow_baseline_mismatch:
            self.reproducibility_reasons.append("baseline mismatch override was allowed")

    def clean_worktree(self) -> None:
        if not self.baseline_is_temporary or self.args.keep_baseline_worktree:
            if self.baseline_is_temporary:
                self.baseline_cleanup = f"preserved at {self.baseline_root}"
            return
        assert self.baseline_root is not None
        assert self.baseline_temp_parent is not None
        # The generated parent and exact child make the destructive target bounded.
        if self.baseline_root.parent != self.baseline_temp_parent:
            self.baseline_cleanup = "refused unsafe cleanup path"
            return
        command = (
            "git",
            "-C",
            str(self.candidate_root),
            "worktree",
            "remove",
            "--force",
            str(self.baseline_root),
        )
        output = capture(command, cwd=self.candidate_root)
        if self.baseline_root.exists():
            self.baseline_cleanup = f"git cleanup failed; preserved at {self.baseline_root}: {output}"
            return
        try:
            self.baseline_temp_parent.rmdir()
        except OSError:
            pass
        self.baseline_cleanup = "temporary detached worktree removed"

    def build_harnesses(self) -> None:
        assert self.baseline_root is not None
        build_root = self.output / "build" / "harness"
        baseline_build = build_root / "baseline"
        candidate_build = build_root / "candidate"
        harness_dir = self.candidate_root / "test" / "uitree_redraw"
        if not (harness_dir / "Makefile").is_file():
            raise SuiteError(f"UITree redraw harness Makefile is missing: {harness_dir / 'Makefile'}")

        if not self.args.skip_build:
            baseline_build.mkdir(parents=True, exist_ok=True)
            candidate_build.mkdir(parents=True, exist_ok=True)
            self.run_command(
                (
                    self.args.make,
                    "-C",
                    harness_dir,
                    f"SOURCE_ROOT={self.baseline_root}",
                    f"BUILD_DIR={baseline_build}",
                    "CANDIDATE=0",
                ),
                cwd=self.candidate_root,
                log_name="build-harness-baseline.log",
            )
            self.run_command(
                (
                    self.args.make,
                    "-C",
                    harness_dir,
                    f"SOURCE_ROOT={self.candidate_root}",
                    f"BUILD_DIR={candidate_build}",
                    "CANDIDATE=1",
                ),
                cwd=self.candidate_root,
                log_name="build-harness-candidate.log",
            )

        self.baseline_harness = (
            self.args.baseline_harness.resolve()
            if self.args.baseline_harness
            else baseline_build / "uitree_redraw_harness"
        )
        self.candidate_harness = (
            self.args.candidate_harness.resolve()
            if self.args.candidate_harness
            else candidate_build / "uitree_redraw_harness"
        )
        for label, executable in (
            ("baseline", self.baseline_harness),
            ("candidate", self.candidate_harness),
        ):
            if not executable.is_file():
                raise SuiteError(f"{label} harness executable is missing: {executable}")
            if os.name != "nt" and not os.access(executable, os.X_OK):
                raise SuiteError(f"{label} harness is not executable: {executable}")

    def run_native_checks(self) -> None:
        """Archive the candidate's focused native tests and optimized link."""
        source_dir = self.candidate_root / "src"
        self.native_build_root = (
            self.candidate_root
            / "build"
            / "uitree-redraw-native"
            / f"{self.candidate_commit[:12]}-{os.getpid()}"
        )
        self.native_build_root.mkdir(parents=True, exist_ok=True)
        object_base = self.native_build_root / "objects"
        # The Makefile prefixes test executable invocations with "./". Keep
        # this relative to src/ so an absolute base cannot become .//abs/path.
        relative_object_base = os.path.relpath(object_base, source_dir)
        targets = (
            ("test-uitree", "all UITree unit and integration cases"),
            ("test-client-trigger", "client-trigger and scripted-overlay store cases"),
            ("test-cs2-frame-settle", "CS2 publication-fence cases"),
            ("all", "optimized native client compile and link"),
        )
        for target, detail in targets:
            self.run_command(
                (
                    self.args.make,
                    "-C",
                    source_dir,
                    "OPT=1",
                    f"PLATFORM_OBJ_BASE={relative_object_base}",
                    target,
                ),
                cwd=self.candidate_root,
                log_name=f"native-{target}.log",
            )
            self.add_gate(
                f"native check: {target}",
                True,
                detail,
                "native",
            )

    def run_harness(
        self,
        executable: Path,
        mode: str,
        retention: str,
        output: Path,
        frames: int,
        seed: int,
        log_name: str,
    ) -> None:
        output.mkdir(parents=True, exist_ok=False)
        self.run_command(
            (
                executable,
                "--mode",
                mode,
                "--out",
                output,
                "--retention",
                retention,
                "--frames",
                str(frames),
                "--seed",
                str(seed),
            ),
            cwd=self.candidate_root,
            log_name=log_name,
            env={"LC_ALL": "C", "LANG": "C", "TZ": "UTC"},
        )

    @staticmethod
    def bmp_map(directory: Path) -> dict[str, Path]:
        return {
            path.relative_to(directory).as_posix(): path
            for path in sorted(directory.rglob("*.bmp"))
            if path.is_file()
        }

    def compare_visual_image_sets(
        self,
        baseline_full_dir: Path,
        baseline_retained_dir: Path,
        candidate_forced_dir: Path,
        candidate_retained_dir: Path,
        scenario_by_checkpoint: Mapping[str, str],
    ) -> None:
        variants = {
            "baseline-full": self.bmp_map(baseline_full_dir),
            "baseline-retained": self.bmp_map(baseline_retained_dir),
            "candidate-forced": self.bmp_map(candidate_forced_dir),
            "candidate-retained": self.bmp_map(candidate_retained_dir),
        }
        key_sets = {name: set(images) for name, images in variants.items()}
        all_keys = set().union(*key_sets.values())
        sets_equal = bool(all_keys) and all(keys == all_keys for keys in key_sets.values())
        self.add_gate(
            "UITree harness: four-way checkpoint set",
            sets_equal,
            "; ".join(f"{name}={len(images)}" for name, images in variants.items()),
            "visual",
        )

        baseline_stale_images = 0
        unexpected_baseline_images: list[str] = []
        diff_root = self.output / "visual" / "diff"
        for relative in sorted(all_keys):
            row: dict[str, Any] = {
                "suite": "UITree harness",
                "checkpoint": relative,
                "scenario": scenario_by_checkpoint.get(relative),
            }
            if any(relative not in images for images in variants.values()):
                missing = [name for name, images in variants.items() if relative not in images]
                row.update({"exact": False, "reason": f"missing from {missing}"})
                self.visual_results.append(row)
                continue

            safe_name = Path(relative)
            before_diff = diff_root / "baseline-retained-vs-oracle" / safe_name
            forced_diff = diff_root / "baseline-full-vs-candidate-forced" / safe_name
            after_diff = diff_root / "candidate-retained-vs-oracle" / safe_name
            before_cmp = compare_bmps(
                variants["baseline-retained"][relative],
                variants["candidate-forced"][relative],
                before_diff,
            )
            forced_cmp = compare_bmps(
                variants["baseline-full"][relative],
                variants["candidate-forced"][relative],
                forced_diff,
            )
            after_cmp = compare_bmps(
                variants["candidate-retained"][relative],
                variants["candidate-forced"][relative],
                after_diff,
            )
            scenario = scenario_by_checkpoint.get(relative, "")
            baseline_difference_allowed = scenario in VISUAL_BASELINE_STALE_SCENARIOS
            if not before_cmp["exact"]:
                if baseline_difference_allowed:
                    baseline_stale_images += 1
                else:
                    unexpected_baseline_images.append(relative)

            row.update(
                {
                    "before": safe_rel(variants["baseline-retained"][relative], self.output),
                    "baseline_full": safe_rel(variants["baseline-full"][relative], self.output),
                    "oracle": safe_rel(variants["candidate-forced"][relative], self.output),
                    "after": safe_rel(variants["candidate-retained"][relative], self.output),
                    "before_diff": safe_rel(before_diff, self.output),
                    "forced_diff": safe_rel(forced_diff, self.output),
                    "after_diff": safe_rel(after_diff, self.output),
                    "before_vs_oracle": before_cmp,
                    "forced_parity": forced_cmp,
                    "after_vs_oracle": after_cmp,
                    "baseline_difference_allowed": baseline_difference_allowed,
                    "exact": bool(forced_cmp["exact"] and after_cmp["exact"]),
                }
            )
            self.visual_results.append(row)
            self.gallery_rows.append(row)
            self.add_gate(
                f"UITree harness: {relative}: baseline full == forced oracle",
                bool(forced_cmp["exact"]),
                f"different_pixels={forced_cmp['different_pixels']}, "
                f"max_channel_delta={forced_cmp['max_channel_delta']}, "
                f"bbox={forced_cmp['bbox']}",
                "visual-forced-parity",
            )
            self.add_gate(
                f"UITree harness: {relative}: candidate retained == forced oracle",
                bool(after_cmp["exact"]),
                f"different_pixels={after_cmp['different_pixels']}, "
                f"max_channel_delta={after_cmp['max_channel_delta']}, "
                f"bbox={after_cmp['bbox']}",
                "visual-candidate-oracle",
            )
            self.add_gate(
                f"UITree harness: {relative}: baseline retained difference scoped",
                bool(before_cmp["exact"] or baseline_difference_allowed),
                f"scenario={scenario!r}, exact={before_cmp['exact']}, "
                f"different_pixels={before_cmp['different_pixels']}",
                "visual-baseline-evidence",
            )

        self.add_gate(
            "baseline retained visual difference is host-input scoped",
            not unexpected_baseline_images,
            f"unexpected checkpoints={unexpected_baseline_images[:20]}",
            "visual-baseline-evidence",
        )
        self.add_gate(
            "baseline retained demonstrates stale host input",
            baseline_stale_images > 0,
            f"allowed differing host-camera/host-input checkpoints={baseline_stale_images}",
            "visual-baseline-evidence",
        )

    @staticmethod
    def read_frames_csv(path: Path) -> list[dict[str, str]]:
        if not path.is_file():
            raise SuiteError(f"visual harness did not write {path}")
        with path.open("r", encoding="utf-8", newline="") as stream:
            reader = csv.DictReader(stream)
            fields = tuple(reader.fieldnames or ())
            missing = [field for field in FRAME_PARITY_FIELDS if field not in fields]
            for field in ("full_walks", "retained_frames"):
                if field not in fields:
                    missing.append(field)
            if missing:
                raise SuiteError(f"{path}: frames.csv missing fields: {', '.join(missing)}")
            rows = list(reader)
        if not rows:
            raise SuiteError(f"{path}: frames.csv has no data rows")
        return rows

    def validate_visual_trace_contract(self, rows: list[dict[str, str]]) -> None:
        mismatches: list[dict[str, Any]] = []
        observed = set()
        for index, row in enumerate(rows):
            expected_scenario, expected_checkpoint = EXPECTED_VISUAL_TRACE[
                index % len(EXPECTED_VISUAL_TRACE)
            ]
            observed.add((row["scenario"], row["checkpoint"]))
            if (
                row["frame"] != str(index)
                or row["scenario"] != expected_scenario
                or row["checkpoint"] != expected_checkpoint
            ):
                mismatches.append(
                    {
                        "row": index + 2,
                        "expected": {
                            "frame": str(index),
                            "scenario": expected_scenario,
                            "checkpoint": expected_checkpoint,
                        },
                        "actual": {
                            "frame": row["frame"],
                            "scenario": row["scenario"],
                            "checkpoint": row["checkpoint"],
                        },
                    }
                )
                if len(mismatches) >= 100:
                    break
        missing = sorted(set(EXPECTED_VISUAL_TRACE) - observed)
        complete_cycle = len(rows) >= len(EXPECTED_VISUAL_TRACE)
        self.add_gate(
            "frames.csv: required 24-step visual trace coverage",
            complete_cycle and not missing and not mismatches,
            f"rows={len(rows)}, complete_cycle={complete_cycle}, "
            f"missing={missing}, first_mismatches={mismatches[:5]}",
            "frame-coverage",
        )

    def compare_frame_rows(
        self,
        label: str,
        reference: list[dict[str, str]],
        actual: list[dict[str, str]],
    ) -> dict[str, Any]:
        differences: list[dict[str, Any]] = []
        count = max(len(reference), len(actual))
        for index in range(count):
            if index >= len(reference) or index >= len(actual):
                differences.append(
                    {
                        "row": index + 2,
                        "field": "row",
                        "expected": reference[index] if index < len(reference) else None,
                        "actual": actual[index] if index < len(actual) else None,
                    }
                )
                continue
            for field in FRAME_PARITY_FIELDS:
                if reference[index][field] != actual[index][field]:
                    differences.append(
                        {
                            "row": index + 2,
                            "field": field,
                            "expected": reference[index][field],
                            "actual": actual[index][field],
                        }
                    )
                    if len(differences) >= 100:
                        break
            if len(differences) >= 100:
                break
        exact = len(reference) == len(actual) and not differences
        result = {
            "comparison": label,
            "reference_rows": len(reference),
            "actual_rows": len(actual),
            "strict_fields": list(FRAME_PARITY_FIELDS),
            "exact": exact,
            "differences": differences,
            "differences_truncated": len(differences) >= 100,
        }
        self.add_gate(
            f"frames.csv: {label}",
            exact,
            f"reference_rows={len(reference)}, actual_rows={len(actual)}, "
            f"first_differences={differences[:3]}",
            "frame-oracle",
        )
        return result

    def compare_baseline_retained_rows(
        self,
        oracle: list[dict[str, str]],
        baseline_retained: list[dict[str, str]],
    ) -> dict[str, Any]:
        allowed: list[dict[str, Any]] = []
        unexpected: list[dict[str, Any]] = []
        count = max(len(oracle), len(baseline_retained))
        for index in range(count):
            if index >= len(oracle) or index >= len(baseline_retained):
                unexpected.append(
                    {
                        "row": index + 2,
                        "field": "row",
                        "expected": oracle[index] if index < len(oracle) else None,
                        "actual": (
                            baseline_retained[index] if index < len(baseline_retained) else None
                        ),
                    }
                )
                continue
            reference = oracle[index]
            actual = baseline_retained[index]
            scenario = reference["scenario"]
            for field in FRAME_PARITY_FIELDS:
                if reference[field] == actual[field]:
                    continue
                difference = {
                    "row": index + 2,
                    "frame": reference["frame"],
                    "scenario": scenario,
                    "field": field,
                    "expected": reference[field],
                    "actual": actual[field],
                }
                # Frame identity is never permitted to drift. Only rendered
                # content fields may expose the known legacy host-input bug.
                if (
                    field not in ("frame", "scenario", "checkpoint")
                    and scenario in VISUAL_BASELINE_STALE_SCENARIOS
                ):
                    allowed.append(difference)
                else:
                    unexpected.append(difference)

        scoped = len(oracle) == len(baseline_retained) and not unexpected
        demonstrated = bool(allowed)
        self.add_gate(
            "frames.csv: baseline retained differences are host-input scoped",
            scoped,
            f"allowed_differences={len(allowed)}, unexpected={unexpected[:10]}",
            "frame-baseline-evidence",
        )
        self.add_gate(
            "frames.csv: baseline retained demonstrates stale host input",
            demonstrated,
            f"allowed host-camera/host-input differences={len(allowed)}",
            "frame-baseline-evidence",
        )
        return {
            "comparison": "baseline retained vs forced oracle",
            "reference_rows": len(oracle),
            "actual_rows": len(baseline_retained),
            "allowed_scenarios": sorted(VISUAL_BASELINE_STALE_SCENARIOS),
            "allowed_differences": allowed[:500],
            "allowed_differences_truncated": len(allowed) > 500,
            "unexpected_differences": unexpected[:500],
            "unexpected_differences_truncated": len(unexpected) > 500,
            "scoped": scoped,
            "demonstrated": demonstrated,
        }

    def run_visual_suite(self) -> None:
        assert self.baseline_harness is not None
        assert self.candidate_harness is not None
        root = self.output / "visual" / "harness"
        baseline_full_dir = root / "baseline-full"
        baseline_retained_dir = root / "baseline-retained"
        candidate_forced_dir = root / "candidate-forced"
        candidate_retained_dir = root / "candidate-retained"
        self.run_harness(
            self.baseline_harness,
            "visual",
            "0",
            baseline_full_dir,
            self.args.visual_frames,
            self.args.seed,
            "visual-baseline-full.log",
        )
        self.run_harness(
            self.baseline_harness,
            "visual",
            "1",
            baseline_retained_dir,
            self.args.visual_frames,
            self.args.seed,
            "visual-baseline-retained.log",
        )
        self.run_harness(
            self.candidate_harness,
            "visual",
            "0",
            candidate_forced_dir,
            self.args.visual_frames,
            self.args.seed,
            "visual-candidate-forced.log",
        )
        self.run_harness(
            self.candidate_harness,
            "visual",
            "1",
            candidate_retained_dir,
            self.args.visual_frames,
            self.args.seed,
            "visual-candidate-retained.log",
        )

        baseline_full_rows = self.read_frames_csv(baseline_full_dir / "frames.csv")
        baseline_retained_rows = self.read_frames_csv(baseline_retained_dir / "frames.csv")
        candidate_forced_rows = self.read_frames_csv(candidate_forced_dir / "frames.csv")
        candidate_retained_rows = self.read_frames_csv(candidate_retained_dir / "frames.csv")
        self.validate_visual_trace_contract(candidate_forced_rows)
        self.add_gate(
            "frames.csv: requested frame count",
            len(baseline_full_rows)
            == len(baseline_retained_rows)
            == len(candidate_forced_rows)
            == len(candidate_retained_rows)
            == self.args.visual_frames,
            f"requested={self.args.visual_frames}, baseline_full={len(baseline_full_rows)}, "
            f"baseline_retained={len(baseline_retained_rows)}, "
            f"candidate_forced={len(candidate_forced_rows)}, "
            f"candidate_retained={len(candidate_retained_rows)}",
            "frame-oracle",
        )
        self.frame_results = {
            "baseline_full_vs_candidate_forced": self.compare_frame_rows(
                "baseline full == candidate forced", candidate_forced_rows, baseline_full_rows
            ),
            "candidate_retained_vs_forced": self.compare_frame_rows(
                "candidate retained == candidate forced oracle",
                candidate_forced_rows,
                candidate_retained_rows,
            ),
            "baseline_retained_evidence": self.compare_baseline_retained_rows(
                candidate_forced_rows, baseline_retained_rows
            ),
        }
        scenario_by_checkpoint = {
            f"frame_{int(row['frame'], 0):04d}_{row['checkpoint']}.bmp": row["scenario"]
            for row in candidate_forced_rows
            if row["checkpoint"]
        }
        self.compare_visual_image_sets(
            baseline_full_dir,
            baseline_retained_dir,
            candidate_forced_dir,
            candidate_retained_dir,
            scenario_by_checkpoint,
        )

        try:
            baseline_full_walks = int(baseline_full_rows[-1]["full_walks"], 0)
            baseline_full_retained = int(baseline_full_rows[-1]["retained_frames"], 0)
            baseline_production_walks = int(baseline_retained_rows[-1]["full_walks"], 0)
            baseline_production_retained = int(
                baseline_retained_rows[-1]["retained_frames"], 0
            )
            candidate_forced_walks = int(candidate_forced_rows[-1]["full_walks"], 0)
            candidate_forced_retained = int(candidate_forced_rows[-1]["retained_frames"], 0)
            candidate_production_walks = int(candidate_retained_rows[-1]["full_walks"], 0)
            candidate_production_retained = int(
                candidate_retained_rows[-1]["retained_frames"], 0
            )
        except ValueError as exc:
            raise SuiteError(f"frames.csv has a non-integer structural counter: {exc}") from exc
        self.frame_results["structural_totals"] = {
            "baseline_full_walks": baseline_full_walks,
            "baseline_full_retained_frames": baseline_full_retained,
            "baseline_production_full_walks": baseline_production_walks,
            "baseline_production_retained_frames": baseline_production_retained,
            "candidate_forced_full_walks": candidate_forced_walks,
            "candidate_forced_retained_frames": candidate_forced_retained,
            "candidate_production_full_walks": candidate_production_walks,
            "candidate_production_retained_frames": candidate_production_retained,
        }
        self.add_gate(
            "full-walk modes exercised",
            baseline_full_walks == self.args.visual_frames
            and candidate_forced_walks == self.args.visual_frames
            and baseline_full_retained == 0
            and candidate_forced_retained == 0,
            f"frames={self.args.visual_frames}, baseline full walks/retained="
            f"{baseline_full_walks}/{baseline_full_retained}, candidate forced="
            f"{candidate_forced_walks}/{candidate_forced_retained}",
            "structure",
        )
        self.add_gate(
            "baseline production retention exercised",
            baseline_production_retained > 0
            and baseline_production_walks < baseline_full_walks
            and baseline_production_retained + baseline_production_walks
            == self.args.visual_frames,
            f"retained={baseline_production_retained}, full_walks={baseline_production_walks}",
            "structure",
        )
        self.add_gate(
            "candidate production retention exercised",
            candidate_production_retained > 0
            and candidate_production_walks < candidate_forced_walks
            and candidate_production_retained + candidate_production_walks
            == self.args.visual_frames,
            f"retained={candidate_production_retained}, full_walks={candidate_production_walks}",
            "structure",
        )

    def run_chrome_visual(self) -> None:
        assert self.baseline_root is not None
        chrome_build = self.output / "build" / "chrome"
        before_build = chrome_build / "baseline"
        after_build = chrome_build / "candidate"
        raw_before = self.output / "visual" / "chrome" / "raw-baseline"
        raw_after = self.output / "visual" / "chrome" / "raw-candidate"
        before_build.mkdir(parents=True, exist_ok=True)
        after_build.mkdir(parents=True, exist_ok=True)
        harness_dir = self.candidate_root / "test" / "uitree_redraw"
        self.run_command(
            (
                self.args.make,
                "-C",
                harness_dir,
                "chrome-visual",
                f"SOURCE_ROOT={self.baseline_root}",
                f"BUILD_DIR={before_build}",
                "CANDIDATE=0",
                f"CHROME_OUT_DIR={raw_before}",
            ),
            cwd=self.candidate_root,
            log_name="chrome-baseline.log",
        )
        self.run_command(
            (
                self.args.make,
                "-C",
                harness_dir,
                "chrome-visual",
                f"SOURCE_ROOT={self.candidate_root}",
                f"BUILD_DIR={after_build}",
                "CANDIDATE=1",
                f"CHROME_OUT_DIR={raw_after}",
            ),
            cwd=self.candidate_root,
            log_name="chrome-candidate.log",
        )
        before_sources = sorted((raw_before / "build").glob("debug_overlay_*.bmp"))
        after_sources = sorted((raw_after / "build").glob("debug_overlay_*.bmp"))
        before_dir = self.output / "visual" / "chrome" / "before"
        after_dir = self.output / "visual" / "chrome" / "after"
        diff_dir = self.output / "visual" / "chrome" / "diff"
        before_dir.mkdir(parents=True, exist_ok=True)
        after_dir.mkdir(parents=True, exist_ok=True)
        for path in before_sources:
            shutil.copy2(path, before_dir / path.name)
        for path in after_sources:
            shutil.copy2(path, after_dir / path.name)
        before = self.bmp_map(before_dir)
        after = self.bmp_map(after_dir)
        keys_equal = set(before) == set(after) and bool(before)
        self.add_gate(
            "Soft3D chrome: checkpoint set",
            keys_equal,
            f"before={len(before)}, after={len(after)}, "
            f"before_only={sorted(set(before) - set(after))}, "
            f"after_only={sorted(set(after) - set(before))}",
            "chrome-visual",
        )
        for relative in sorted(set(before) | set(after)):
            row: dict[str, Any] = {"suite": "Soft3D chrome", "checkpoint": relative}
            if relative not in before or relative not in after:
                row.update({"exact": False, "reason": "missing checkpoint"})
                self.chrome_results.append(row)
                continue
            diff_path = diff_dir / relative
            comparison = compare_bmps(before[relative], after[relative], diff_path)
            row.update(
                {
                    "before": safe_rel(before[relative], self.output),
                    "oracle": safe_rel(before[relative], self.output),
                    "after": safe_rel(after[relative], self.output),
                    "before_diff": safe_rel(diff_path, self.output),
                    "after_diff": safe_rel(diff_path, self.output),
                    "before_vs_oracle": comparison,
                    "after_vs_oracle": comparison,
                    "exact": comparison["exact"],
                }
            )
            self.chrome_results.append(row)
            self.gallery_rows.append(row)
            self.add_gate(
                f"Soft3D chrome: {relative}: before == after",
                bool(comparison["exact"]),
                f"different_pixels={comparison['different_pixels']}, "
                f"max_channel_delta={comparison['max_channel_delta']}, "
                f"bbox={comparison['bbox']}",
                "chrome-visual",
            )

    @staticmethod
    def read_bench_csv(path: Path) -> dict[str, dict[str, Any]]:
        if not path.is_file():
            raise SuiteError(f"benchmark harness did not write {path}")
        rows: dict[str, dict[str, Any]] = {}
        with path.open("r", encoding="utf-8", newline="") as stream:
            reader = csv.DictReader(stream)
            fields = tuple(reader.fieldnames or ())
            missing = [field for field in BENCH_FIELDS if field not in fields]
            if missing:
                raise SuiteError(f"{path}: metrics.csv missing fields: {', '.join(missing)}")
            for line, raw in enumerate(reader, 2):
                scenario = raw["scenario"].strip()
                if not scenario:
                    raise SuiteError(f"{path}:{line}: empty scenario")
                if scenario in rows:
                    raise SuiteError(f"{path}:{line}: duplicate scenario {scenario!r}")
                try:
                    elapsed = int(raw["elapsed_ns"], 0)
                    operations = int(raw["operations"], 0)
                    ns_per_op = float(raw["ns_per_op"])
                    full_walks = int(raw["full_walks"], 0)
                    retained_frames = int(raw["retained_frames"], 0)
                    emit_nodes = int(raw["emit_nodes"], 0)
                except ValueError as exc:
                    raise SuiteError(f"{path}:{line}: invalid numeric value: {exc}") from exc
                if elapsed <= 0 or operations <= 0 or ns_per_op <= 0.0:
                    raise SuiteError(f"{path}:{line}: timing and operations must be positive")
                if full_walks < 0 or retained_frames < 0 or emit_nodes < 0:
                    raise SuiteError(f"{path}:{line}: structural counters must be non-negative")
                calculated = elapsed / operations
                # The harness prints three decimals, so only its final rounding
                # unit is permitted; reject stale or mislabelled metrics.
                if not math.isclose(calculated, ns_per_op, rel_tol=0.0, abs_tol=0.00051):
                    raise SuiteError(
                        f"{path}:{line}: ns_per_op={ns_per_op} disagrees with "
                        f"elapsed_ns/operations={calculated}"
                    )
                rows[scenario] = {
                    "elapsed_ns": elapsed,
                    "operations": operations,
                    "ns_per_op": ns_per_op,
                    "full_walks": full_walks,
                    "retained_frames": retained_frames,
                    "emit_nodes": emit_nodes,
                }
        missing_scenarios = sorted(set(EXPECTED_BENCH_SCENARIOS) - set(rows))
        if missing_scenarios:
            raise SuiteError(f"{path}: missing benchmark scenarios: {', '.join(missing_scenarios)}")
        unexpected_scenarios = sorted(set(rows) - set(EXPECTED_BENCH_SCENARIOS))
        if unexpected_scenarios:
            raise SuiteError(
                f"{path}: unexpected benchmark scenarios: {', '.join(unexpected_scenarios)}"
            )
        return rows

    def validate_benchmark_trial_contract(
        self,
        trial: int,
        metrics_by_variant: Mapping[str, Mapping[str, Mapping[str, Any]]],
    ) -> None:
        individual = tuple(
            scenario for scenario in EXPECTED_BENCH_SCENARIOS if scenario != "aggregate"
        )
        expected_operations = {
            scenario: self.args.bench_frames for scenario in individual
        }
        expected_operations["aggregate"] = self.args.bench_frames * len(individual)

        operation_errors: list[str] = []
        accounting_errors: list[str] = []
        aggregate_errors: list[str] = []
        for variant, metrics in metrics_by_variant.items():
            for scenario in EXPECTED_BENCH_SCENARIOS:
                record = metrics[scenario]
                if record["operations"] != expected_operations[scenario]:
                    operation_errors.append(
                        f"{variant}/{scenario}: operations={record['operations']} "
                        f"expected={expected_operations[scenario]}"
                    )
                if record["full_walks"] + record["retained_frames"] != record["operations"]:
                    accounting_errors.append(
                        f"{variant}/{scenario}: walks={record['full_walks']} + "
                        f"retained={record['retained_frames']} != operations={record['operations']}"
                    )

            aggregate = metrics["aggregate"]
            for field in (
                "elapsed_ns",
                "operations",
                "full_walks",
                "retained_frames",
                "emit_nodes",
            ):
                component_total = sum(metrics[scenario][field] for scenario in individual)
                if aggregate[field] != component_total:
                    aggregate_errors.append(
                        f"{variant}/{field}: aggregate={aggregate[field]} "
                        f"components={component_total}"
                    )

        operation_vectors = {
            scenario: tuple(
                metrics_by_variant[variant][scenario]["operations"]
                for variant in (
                    "baseline-retained",
                    "candidate-retained",
                    "candidate-forced",
                )
            )
            for scenario in EXPECTED_BENCH_SCENARIOS
        }
        paired_operation_errors = {
            scenario: values
            for scenario, values in operation_vectors.items()
            if len(set(values)) != 1
        }
        self.add_gate(
            f"benchmark trial {trial}: operation contract",
            not operation_errors and not paired_operation_errors,
            f"per-record errors={operation_errors}, paired errors={paired_operation_errors}",
            "benchmark-contract",
        )
        self.add_gate(
            f"benchmark trial {trial}: exact counter accounting",
            not accounting_errors,
            f"errors={accounting_errors}",
            "benchmark-contract",
        )
        self.add_gate(
            f"benchmark trial {trial}: aggregate is exact component sum",
            not aggregate_errors,
            f"errors={aggregate_errors}",
            "benchmark-contract",
        )

        forced_errors: list[str] = []
        for scenario, record in metrics_by_variant["candidate-forced"].items():
            if record["full_walks"] != record["operations"] or record["retained_frames"] != 0:
                forced_errors.append(
                    f"{scenario}: operations={record['operations']}, "
                    f"walks={record['full_walks']}, retained={record['retained_frames']}"
                )
        self.add_gate(
            f"benchmark trial {trial}: candidate forced performs only full walks",
            not forced_errors,
            f"errors={forced_errors}",
            "benchmark-structure",
        )

        production_errors: list[str] = []
        for variant in ("baseline-retained", "candidate-retained"):
            aggregate = metrics_by_variant[variant]["aggregate"]
            if not (
                aggregate["retained_frames"] > 0
                and aggregate["full_walks"] < aggregate["operations"]
            ):
                production_errors.append(
                    f"{variant}: operations={aggregate['operations']}, "
                    f"walks={aggregate['full_walks']}, retained={aggregate['retained_frames']}"
                )
        self.add_gate(
            f"benchmark trial {trial}: production retention exercised",
            not production_errors,
            f"errors={production_errors}",
            "benchmark-structure",
        )

        candidate_emit_errors: list[str] = []
        for scenario in individual:
            forced_nodes = metrics_by_variant["candidate-forced"][scenario]["emit_nodes"]
            retained_nodes = metrics_by_variant["candidate-retained"][scenario]["emit_nodes"]
            if forced_nodes != retained_nodes:
                candidate_emit_errors.append(
                    f"{scenario}: forced={forced_nodes}, retained={retained_nodes}"
                )
        production_emit_errors: list[str] = []
        for scenario in sorted(CORRECT_AGGREGATE_SCENARIOS):
            baseline_nodes = metrics_by_variant["baseline-retained"][scenario]["emit_nodes"]
            candidate_nodes = metrics_by_variant["candidate-retained"][scenario]["emit_nodes"]
            if baseline_nodes != candidate_nodes:
                production_emit_errors.append(
                    f"{scenario}: baseline={baseline_nodes}, candidate={candidate_nodes}"
                )
        self.add_gate(
            f"benchmark trial {trial}: equal-work emit-node controls",
            not candidate_emit_errors and not production_emit_errors,
            f"candidate forced/retained errors={candidate_emit_errors}; "
            f"production correct-scenario errors={production_emit_errors}",
            "benchmark-contract",
        )

    def record_benchmark_comparison(
        self,
        *,
        comparison: str,
        scenario: str,
        before_variant: str,
        after_variant: str,
        before_values: list[float],
        after_values: list[float],
        gate_kind: str,
        before_records: list[dict[str, Any]] | None = None,
        after_records: list[dict[str, Any]] | None = None,
    ) -> None:
        ratio, lower, upper, ratios = paired_bootstrap_ratio(
            before_values,
            after_values,
            self.args.bootstrap_samples,
            stable_seed(f"{comparison}:{scenario}", self.args.seed),
        )
        before_median = statistics.median(before_values)
        after_median = statistics.median(after_values)
        absolute_delta = after_median - before_median
        if gate_kind in ("primary", "effectiveness"):
            passed = upper < 1.0
            criterion = "paired bootstrap 95% CI upper < 1.0"
        elif gate_kind == "guard":
            ratio_ok = upper <= 1.0 + self.args.regression_tolerance
            floor_ok = absolute_delta <= self.args.practical_floor_ns
            passed = ratio_ok or floor_ok
            criterion = (
                f"CI upper <= {1.0 + self.args.regression_tolerance:.4f} or "
                f"median delta <= {self.args.practical_floor_ns:.3f} ns/op"
            )
        elif gate_kind == "informational":
            passed = True
            criterion = "informational only; known stale baseline makes a relative gate invalid"
        else:
            raise SuiteError(f"unknown benchmark gate kind {gate_kind!r}")

        result: dict[str, Any] = {
            "comparison": comparison,
            "scenario": scenario,
            "gate_kind": gate_kind,
            "primary": gate_kind == "primary",
            "before_variant": before_variant,
            "after_variant": after_variant,
            "trials": self.args.trials,
            "baseline_ns_per_op": before_values,
            "candidate_ns_per_op": after_values,
            "paired_ratios": ratios,
            "baseline_median_ns_per_op": before_median,
            "candidate_median_ns_per_op": after_median,
            "median_ratio": ratio,
            "ci95_lower": lower,
            "ci95_upper": upper,
            "median_delta_ns_per_op": absolute_delta,
            "criterion": criterion,
            "passed": passed,
        }
        if before_records is not None and after_records is not None:
            keys = ("operations", "full_walks", "retained_frames", "emit_nodes")
            result["baseline_counter_medians"] = {
                key: statistics.median(record[key] for record in before_records) for key in keys
            }
            result["candidate_counter_medians"] = {
                key: statistics.median(record[key] for record in after_records) for key in keys
            }
        self.benchmark_results.append(result)
        self.add_gate(
            f"benchmark: {comparison}: {scenario}",
            passed,
            f"{before_variant}={before_median:.3f} ns/op, "
            f"{after_variant}={after_median:.3f} ns/op, paired median ratio={ratio:.5f}, "
            f"CI95=[{lower:.5f}, {upper:.5f}], delta={absolute_delta:.3f} ns/op; "
            f"{criterion}",
            f"benchmark-{gate_kind}",
            scenario=scenario,
            comparison=comparison,
        )

    def run_benchmarks(self) -> None:
        assert self.baseline_harness is not None
        assert self.candidate_harness is not None
        raw_root = self.output / "perf" / "raw"
        paired: dict[int, dict[str, dict[str, dict[str, Any]]]] = {}
        variants = {
            "baseline-retained": (self.baseline_harness, "1"),
            "candidate-retained": (self.candidate_harness, "1"),
            "candidate-forced": (self.candidate_harness, "0"),
        }
        order_index = 0
        for trial in range(self.args.trials):
            trial_seed = (self.args.seed + trial * 104729) & 0xFFFFFFFF
            order = BENCH_VARIANT_ORDERS[trial % len(BENCH_VARIANT_ORDERS)]
            paired[trial] = {}
            for variant in order:
                executable, retention = variants[variant]
                output = raw_root / f"trial-{trial + 1:03d}" / variant
                self.run_harness(
                    executable,
                    "bench",
                    retention,
                    output,
                    self.args.bench_frames,
                    trial_seed,
                    f"bench-{trial + 1:03d}-{variant}.log",
                )
                metrics = self.read_bench_csv(output / "metrics.csv")
                paired[trial][variant] = metrics
                self.raw_benchmarks.append(
                    {
                        "trial": trial + 1,
                        "seed": trial_seed,
                        "order": order_index,
                        "trial_order": list(order),
                        "variant": variant,
                        "retention": retention,
                        "metrics_csv": safe_rel(output / "metrics.csv", self.output),
                        "metrics": metrics,
                    }
                )
                order_index += 1

        for trial, trial_variants in paired.items():
            sets = {variant: set(metrics) for variant, metrics in trial_variants.items()}
            first = sets["baseline-retained"]
            if any(scenarios != first for scenarios in sets.values()):
                raise SuiteError(
                    f"benchmark trial {trial + 1}: scenario sets differ: "
                    + "; ".join(f"{name}={sorted(value)}" for name, value in sets.items())
                )
            self.validate_benchmark_trial_contract(trial + 1, trial_variants)

        all_scenarios = sorted(
            set.intersection(
                *(
                    set(trial_variants["baseline-retained"])
                    & set(trial_variants["candidate-retained"])
                    & set(trial_variants["candidate-forced"])
                    for trial_variants in paired.values()
                )
            )
        )
        for scenario in all_scenarios:
            before_records = [
                paired[index]["baseline-retained"][scenario] for index in paired
            ]
            after_records = [
                paired[index]["candidate-retained"][scenario] for index in paired
            ]
            if scenario == "steady":
                gate_kind = "primary"
            elif scenario in BENCH_CAMERA_SCENARIOS or scenario == "aggregate":
                gate_kind = "informational"
            elif scenario in GUARD_SCENARIOS:
                gate_kind = "guard"
            else:
                gate_kind = "informational"
            self.record_benchmark_comparison(
                comparison="production-ab",
                scenario=scenario,
                before_variant="baseline-retained",
                after_variant="candidate-retained",
                before_values=[record["ns_per_op"] for record in before_records],
                after_values=[record["ns_per_op"] for record in after_records],
                gate_kind=gate_kind,
                before_records=before_records,
                after_records=after_records,
            )

        missing_correct = sorted(CORRECT_AGGREGATE_SCENARIOS - set(all_scenarios))
        if missing_correct:
            raise SuiteError(
                "cannot calculate correctness-qualified aggregate; missing scenarios: "
                + ", ".join(missing_correct)
            )
        correct_before: list[float] = []
        correct_after: list[float] = []
        for index in paired:
            before_elapsed = sum(
                paired[index]["baseline-retained"][scenario]["elapsed_ns"]
                for scenario in CORRECT_AGGREGATE_SCENARIOS
            )
            before_operations = sum(
                paired[index]["baseline-retained"][scenario]["operations"]
                for scenario in CORRECT_AGGREGATE_SCENARIOS
            )
            after_elapsed = sum(
                paired[index]["candidate-retained"][scenario]["elapsed_ns"]
                for scenario in CORRECT_AGGREGATE_SCENARIOS
            )
            after_operations = sum(
                paired[index]["candidate-retained"][scenario]["operations"]
                for scenario in CORRECT_AGGREGATE_SCENARIOS
            )
            correct_before.append(before_elapsed / before_operations)
            correct_after.append(after_elapsed / after_operations)
        self.record_benchmark_comparison(
            comparison="production-ab",
            scenario="correct-aggregate",
            before_variant="baseline-retained",
            after_variant="candidate-retained",
            before_values=correct_before,
            after_values=correct_after,
            gate_kind="primary",
        )

        candidate_retained_steady = [
            paired[index]["candidate-retained"]["steady"] for index in paired
        ]
        candidate_forced_steady = [
            paired[index]["candidate-forced"]["steady"] for index in paired
        ]
        self.record_benchmark_comparison(
            comparison="candidate-retention-effectiveness",
            scenario="steady",
            before_variant="candidate-forced",
            after_variant="candidate-retained",
            before_values=[record["ns_per_op"] for record in candidate_forced_steady],
            after_values=[record["ns_per_op"] for record in candidate_retained_steady],
            gate_kind="effectiveness",
            before_records=candidate_forced_steady,
            after_records=candidate_retained_steady,
        )
        for scenario in sorted(set(all_scenarios) & BENCH_CAMERA_SCENARIOS):
            forced_records = [paired[index]["candidate-forced"][scenario] for index in paired]
            retained_records = [
                paired[index]["candidate-retained"][scenario] for index in paired
            ]
            self.record_benchmark_comparison(
                comparison="candidate-correctness-cost",
                scenario=scenario,
                before_variant="candidate-forced",
                after_variant="candidate-retained",
                before_values=[record["ns_per_op"] for record in forced_records],
                after_values=[record["ns_per_op"] for record in retained_records],
                gate_kind="guard",
                before_records=forced_records,
                after_records=retained_records,
            )

    def execute(self) -> None:
        self.prepare_worktrees()
        if not self.args.skip_native:
            self.run_native_checks()
        self.build_harnesses()
        for label, skipped in (
            ("native tests and optimized link", self.args.skip_native),
            ("harness visual", self.args.skip_visual),
            ("Soft3D chrome", self.args.skip_chrome),
            ("performance", self.args.skip_bench),
        ):
            self.add_gate(
                f"complete evidence lane: {label}",
                not skipped,
                "run" if not skipped else "skipped by diagnostic command-line option",
                "completeness",
            )
        if not self.args.skip_visual:
            self.run_visual_suite()
        if not self.args.skip_chrome:
            self.run_chrome_visual()
        if not self.args.skip_bench:
            self.run_benchmarks()

    def evidence_complete(self) -> bool:
        return not (
            self.args.skip_native
            or self.args.skip_visual
            or self.args.skip_chrome
            or self.args.skip_bench
        )

    def passed(self) -> bool:
        return (
            self.fatal_error is None
            and self.evidence_complete()
            and bool(self.gates)
            and all(gate["passed"] for gate in self.gates)
        )

    def reproduction_command(self) -> str:
        command = [
            "python3",
            "tools/uitree_redraw_suite.py",
            "--baseline-ref",
            self.baseline_commit or self.args.baseline_ref,
            "--profile",
            self.args.profile,
            "--seed",
            str(self.args.seed),
            "--visual-frames",
            str(self.args.visual_frames),
            "--bench-frames",
            str(self.args.bench_frames),
            "--trials",
            str(self.args.trials),
            "--bootstrap-samples",
            str(self.args.bootstrap_samples),
            "--regression-tolerance",
            str(self.args.regression_tolerance),
            "--practical-floor-ns",
            str(self.args.practical_floor_ns),
            "--make",
            self.args.make,
            "--out",
            "build/uitree-redraw/reproduction",
        ]
        if self.args.skip_visual:
            command.append("--skip-visual")
        if self.args.skip_chrome:
            command.append("--skip-chrome")
        if self.args.skip_bench:
            command.append("--skip-bench")
        if self.args.skip_native:
            command.append("--skip-native")
        return shlex.join(command)

    def manifest(self) -> dict[str, Any]:
        baseline_binary = self.baseline_harness if self.baseline_harness and self.baseline_harness.is_file() else None
        candidate_binary = self.candidate_harness if self.candidate_harness and self.candidate_harness.is_file() else None
        contract_dir = self.candidate_root / "test" / "uitree_redraw"
        identity_files = {
            "runner": Path(__file__).resolve(),
            "harness_makefile": contract_dir / "Makefile",
            "harness_source": contract_dir / "uitree_redraw_harness.c",
            "harness_readme": contract_dir / "README.md",
            "world_projection_header": self.candidate_root
            / "src"
            / "render"
            / "torirs_world_projection.h",
        }
        return {
            "schema_version": SUITE_VERSION,
            "suite": "uitree-redraw-ab",
            "started_at": self.started_at,
            "finished_at": utc_now(),
            "status": "passed" if self.passed() else "failed",
            "evidence_complete": self.evidence_complete(),
            "evidence_reproducible": not self.reproducibility_reasons,
            "reproducibility_reasons": self.reproducibility_reasons,
            "reproduction": {
                "checkout_candidate": f"git checkout {self.candidate_commit}",
                "command": self.reproduction_command(),
            },
            "profile": self.args.profile,
            "baseline": {
                "ref": self.args.baseline_ref,
                "commit": self.baseline_commit,
                "tree": self.baseline_tree,
                "root": str(self.baseline_root) if self.baseline_root else None,
                "temporary": self.baseline_is_temporary,
                "cleanup": self.baseline_cleanup,
                "status_at_start": self.baseline_status,
                "harness": str(baseline_binary) if baseline_binary else None,
                "harness_sha256": sha256_file(baseline_binary) if baseline_binary else None,
            },
            "candidate": {
                "commit": self.candidate_commit,
                "tree": self.candidate_tree,
                "root": str(self.candidate_root),
                "status_at_start": self.candidate_status,
                "harness": str(candidate_binary) if candidate_binary else None,
                "harness_sha256": sha256_file(candidate_binary) if candidate_binary else None,
            },
            "configuration": {
                "seed": self.args.seed,
                "visual_frames": self.args.visual_frames,
                "bench_frames": self.args.bench_frames,
                "trials": self.args.trials,
                "bootstrap_samples": self.args.bootstrap_samples,
                "regression_tolerance": self.args.regression_tolerance,
                "practical_floor_ns": self.args.practical_floor_ns,
                "skip_build": self.args.skip_build,
                "skip_native": self.args.skip_native,
                "skip_visual": self.args.skip_visual,
                "skip_chrome": self.args.skip_chrome,
                "skip_bench": self.args.skip_bench,
                "allow_dirty": self.args.allow_dirty,
                "benchmark_order_cycle": [list(order) for order in BENCH_VARIANT_ORDERS],
                "native_build_root": str(self.native_build_root)
                if self.native_build_root
                else None,
            },
            "host": {
                "platform": platform.platform(),
                "system": platform.system(),
                "release": platform.release(),
                "machine": platform.machine(),
                "processor": platform.processor(),
                "cpu_count": os.cpu_count(),
                "python": sys.version,
                "python_executable": sys.executable,
                "git_version": capture(("git", "--version")),
                "make_version": capture((self.args.make, "--version")).splitlines()[:2],
                "cc_version": capture((os.environ.get("CC", "cc"), "--version")).splitlines()[:3],
            },
            "build_environment": {
                name: os.environ.get(name)
                for name in (
                    "MAKE",
                    "CC",
                    "CPPFLAGS",
                    "CFLAGS",
                    "LDFLAGS",
                    "LDLIBS",
                    "SDKROOT",
                    "MACOSX_DEPLOYMENT_TARGET",
                )
            },
            "submodules": {
                "baseline": self.baseline_submodules,
                "candidate": self.candidate_submodules,
            },
            "suite_file_sha256": {
                name: sha256_file(path) if path.is_file() else None
                for name, path in identity_files.items()
            },
            "commands": self.commands,
            "fatal_error": self.fatal_error,
        }

    def summary(self) -> dict[str, Any]:
        failed = [gate for gate in self.gates if not gate["passed"]]
        return {
            "schema_version": SUITE_VERSION,
            "status": "passed" if self.passed() else "failed",
            "evidence_complete": self.evidence_complete(),
            "evidence_reproducible": not self.reproducibility_reasons,
            "reproducibility_reasons": self.reproducibility_reasons,
            "fatal_error": self.fatal_error,
            "gate_count": len(self.gates),
            "failed_gate_count": len(failed),
            "failed_gates": failed,
            "gates": self.gates,
            "visual": {
                "harness": self.visual_results,
                "frames_csv": self.frame_results,
                "chrome": self.chrome_results,
            },
            "performance": {
                "results": self.benchmark_results,
                "raw_trials": self.raw_benchmarks,
            },
        }

    def write_summary_csv(self) -> None:
        path = self.output / "summary.csv"
        fields = (
            "section",
            "name",
            "comparison",
            "gate_kind",
            "before_variant",
            "after_variant",
            "primary",
            "baseline_median_ns_per_op",
            "candidate_median_ns_per_op",
            "median_ratio",
            "ci95_lower",
            "ci95_upper",
            "passed",
            "detail",
        )
        with path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader()
            for result in self.benchmark_results:
                writer.writerow(
                    {
                        "section": "benchmark",
                        "name": result["scenario"],
                        "comparison": result["comparison"],
                        "gate_kind": result["gate_kind"],
                        "before_variant": result["before_variant"],
                        "after_variant": result["after_variant"],
                        "primary": int(result["primary"]),
                        "baseline_median_ns_per_op": f"{result['baseline_median_ns_per_op']:.9f}",
                        "candidate_median_ns_per_op": f"{result['candidate_median_ns_per_op']:.9f}",
                        "median_ratio": f"{result['median_ratio']:.9f}",
                        "ci95_lower": f"{result['ci95_lower']:.9f}",
                        "ci95_upper": f"{result['ci95_upper']:.9f}",
                        "passed": int(result["passed"]),
                        "detail": result["criterion"],
                    }
                )
            for gate in self.gates:
                if gate["category"].startswith("benchmark"):
                    continue
                writer.writerow(
                    {
                        "section": gate["category"],
                        "name": gate["name"],
                        "passed": int(gate["passed"]),
                        "detail": gate["detail"],
                    }
                )

    def write_gallery(self) -> None:
        path = self.output / "gallery.html"
        rows: list[str] = []
        for item in self.gallery_rows:
            if "before" not in item:
                continue
            before_stats = item.get("before_vs_oracle", {})
            after_stats = item.get("after_vs_oracle", {})
            oracle_title = (
                "Baseline reference" if item["suite"] == "Soft3D chrome" else "Forced oracle"
            )
            before_title = (
                "Before" if item["suite"] == "Soft3D chrome" else "Baseline retained (before)"
            )
            after_title = (
                "After" if item["suite"] == "Soft3D chrome" else "Candidate retained (after)"
            )
            cells = []
            for title, key in (
                (before_title, "before"),
                (oracle_title, "oracle"),
                (after_title, "after"),
                ("After diff", "after_diff"),
            ):
                source = html.escape(item[key], quote=True)
                cells.append(
                    f'<td><div class="label">{html.escape(title)}</div>'
                    f'<a href="{source}"><img loading="lazy" src="{source}" alt="{html.escape(title)}"></a></td>'
                )
            detail = (
                f"scenario={item.get('scenario')}, "
                f"before diff pixels={before_stats.get('different_pixels')}, "
                f"after diff pixels={after_stats.get('different_pixels')}, "
                f"after max delta={after_stats.get('max_channel_delta')}, "
                f"after bbox={after_stats.get('bbox')}"
            )
            rows.append(
                "<section><h2>"
                + html.escape(f"{item['suite']} — {item['checkpoint']}")
                + "</h2><table><tr>"
                + "".join(cells)
                + "</tr></table><p>"
                + html.escape(detail)
                + "</p></section>"
            )
        status = "PASS" if self.passed() else "FAIL"
        document = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>UITree redraw visual parity</title>
<style>
body {{ font: 14px system-ui, sans-serif; margin: 1.5rem; background: #17191d; color: #eee; }}
h1 {{ color: {'#67d391' if self.passed() else '#ff6b6b'}; }}
section {{ border-top: 1px solid #454950; padding: 1rem 0; }}
table {{ width: 100%; table-layout: fixed; }} td {{ padding: .4rem; vertical-align: top; }}
img {{ max-width: 100%; image-rendering: pixelated; background: #000; border: 1px solid #555; }}
.label {{ font-weight: 700; margin-bottom: .35rem; }} a {{ color: #8fc9ff; }}
</style></head><body>
<h1>{status}: UITree redraw visual parity</h1>
<p>Comparisons use decoded 32-bit BMP RGB values; alpha and BMP row orientation are normalized.</p>
{''.join(rows) if rows else '<p>No visual rows were produced.</p>'}
</body></html>
"""
        path.write_text(document, encoding="utf-8", newline="\n")

    def write_report(self) -> None:
        status = "PASS" if self.passed() else "FAIL"
        failed = [gate for gate in self.gates if not gate["passed"]]
        lines = [
            f"# UITree redraw A/B report — {status}",
            "",
            f"- Baseline: `{self.args.baseline_ref}` (`{self.baseline_commit or 'unresolved'}`)",
            f"- Candidate: `{self.candidate_commit or 'unresolved'}`",
            f"- Candidate tree: `{self.candidate_tree or 'unresolved'}`",
            f"- Evidence complete: `{'yes' if self.evidence_complete() else 'no'}`",
            f"- Evidence reproducible: `{'yes' if not self.reproducibility_reasons else 'no'}`",
            f"- Profile: `{self.args.profile}`; seed `{self.args.seed}`",
            f"- Visual frames: {self.args.visual_frames}; benchmark frames: {self.args.bench_frames}",
            f"- Independent paired trials: {self.args.trials}; bootstrap samples: {self.args.bootstrap_samples}",
            f"- Lanes: native={'skipped' if self.args.skip_native else 'run'}, "
            f"harness visual={'skipped' if self.args.skip_visual else 'run'}, "
            f"Soft3D chrome={'skipped' if self.args.skip_chrome else 'run'}, "
            f"benchmark={'skipped' if self.args.skip_bench else 'run'}",
            f"- Gates: {len(self.gates) - len(failed)} passed, {len(failed)} failed",
            f"- [Visual gallery](gallery.html)",
            "",
            "## Correctness",
            "",
            "Four independent harness runs separate the actual baseline-retained picture from the "
            "forced-full correctness oracle. Baseline full must equal candidate forced, and candidate "
            "retained must equal candidate forced, at every frame and decoded-RGB checkpoint. Differences "
            "from baseline retained are accepted only in `host-camera`/`host-input` scenarios and at least "
            "one such difference is required. The Soft3D chrome lane remains exact baseline/candidate parity.",
            "",
            f"- Harness BMP checkpoints: {len(self.visual_results)}",
            f"- Soft3D chrome BMP checkpoints: {len(self.chrome_results)}",
            "",
            "## Performance",
            "",
            "| Comparison | Scenario | Kind | Before ns/op | After ns/op | Median ratio | 95% CI | Result |",
            "|---|---|---:|---:|---:|---:|---:|---:|",
        ]
        for result in self.benchmark_results:
            lines.append(
                f"| {result['comparison']} | {result['scenario']} | {result['gate_kind']} | "
                f"{result['baseline_median_ns_per_op']:.3f} | "
                f"{result['candidate_median_ns_per_op']:.3f} | {result['median_ratio']:.5f} | "
                f"[{result['ci95_lower']:.5f}, {result['ci95_upper']:.5f}] | "
                f"{'PASS' if result['passed'] else 'FAIL'} |"
            )
        lines.extend(
            [
                "",
                "Production `steady` and the correctness-qualified aggregate (which excludes camera) "
                "require the upper paired-bootstrap confidence bound below 1.0. The all-scenario aggregate "
                "and production camera comparison are informational because the baseline camera result is "
                "known stale. Candidate camera cost is guarded against candidate forced-full instead.",
            ]
        )
        if self.reproducibility_reasons:
            lines.extend(
                [
                    "",
                    "## Reproducibility warning",
                    "",
                    *[f"- {reason}" for reason in self.reproducibility_reasons],
                ]
            )
        lines.extend(
            [
                "",
                "## Reproduce from committed sources",
                "",
                f"1. `git checkout {self.candidate_commit or '<candidate-sha>'}`",
                f"2. `{self.reproduction_command()}`",
            ]
        )
        if failed or self.fatal_error:
            lines.extend(["", "## Failures", ""])
            if self.fatal_error:
                lines.append(f"- Fatal: {self.fatal_error}")
            lines.extend(f"- `{gate['name']}`: {gate['detail']}" for gate in failed)
        lines.extend(
            [
                "",
                "## Artifacts",
                "",
                "- `manifest.json`: source/build/host identity and executed commands.",
                "- `summary.json` and `summary.csv`: machine-readable gates and statistics.",
                "- `visual/`: before/oracle/after BMPs and decoded-RGB diff BMPs.",
                "- `perf/raw/`: one independent process result per variant and trial.",
                "- `junit.xml`: CI test results.",
                "- `SHA256SUMS`: artifact integrity hashes.",
                "",
            ]
        )
        (self.output / "report.md").write_text("\n".join(lines), encoding="utf-8", newline="\n")

    def write_junit(self) -> None:
        failures = sum(not gate["passed"] for gate in self.gates) + int(self.fatal_error is not None)
        suite = ET.Element(
            "testsuite",
            {
                "name": "uitree-redraw-ab",
                "tests": str(len(self.gates) + int(self.fatal_error is not None)),
                "failures": str(failures),
                "errors": "0",
                "timestamp": self.started_at,
            },
        )
        if self.fatal_error:
            case = ET.SubElement(suite, "testcase", {"name": "suite execution", "classname": "fatal"})
            failure = ET.SubElement(case, "failure", {"message": self.fatal_error})
            failure.text = self.fatal_error
        for gate in self.gates:
            case = ET.SubElement(
                suite,
                "testcase",
                {"name": gate["name"], "classname": gate["category"]},
            )
            ET.SubElement(case, "system-out").text = gate["detail"]
            if not gate["passed"]:
                failure = ET.SubElement(case, "failure", {"message": gate["detail"][:500]})
                failure.text = gate["detail"]
        tree = ET.ElementTree(suite)
        ET.indent(tree, space="  ")
        tree.write(self.output / "junit.xml", encoding="utf-8", xml_declaration=True)

    def write_checksums(self) -> None:
        checksum_path = self.output / "SHA256SUMS"
        files = [
            path
            for path in sorted(self.output.rglob("*"))
            if path.is_file() and path != checksum_path
        ]
        with checksum_path.open("w", encoding="utf-8", newline="\n") as stream:
            for path in files:
                stream.write(f"{sha256_file(path)}  {path.relative_to(self.output).as_posix()}\n")

    def finalize(self) -> None:
        json_dump(self.output / "manifest.json", self.manifest())
        json_dump(self.output / "summary.json", self.summary())
        self.write_summary_csv()
        self.write_gallery()
        self.write_report()
        self.write_junit()
        self.write_checksums()


def positive_int(value: str) -> int:
    parsed = int(value, 0)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def uint32_int(value: str) -> int:
    parsed = int(value, 0)
    if parsed < 0 or parsed > 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("must be an unsigned 32-bit integer")
    return parsed


def nonnegative_float(value: str) -> float:
    parsed = float(value)
    if parsed < 0.0 or not math.isfinite(parsed):
        raise argparse.ArgumentTypeError("must be a finite non-negative number")
    return parsed


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    inferred_root = Path(__file__).resolve().parents[1]
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    # build/ is already ignored; full evidence (especially BMPs and object files)
    # must never turn into an accidental multi-gigabyte Git add.
    parser = argparse.ArgumentParser(
        description=(
            "Compare baseline/candidate production retention against independent forced-full "
            "correctness oracles, including exact BMP parity and paired-process performance CIs."
        )
    )
    parser.add_argument("--candidate-root", type=Path, default=inferred_root)
    parser.add_argument("--baseline-ref", default="origin/v3")
    parser.add_argument(
        "--baseline-root",
        type=Path,
        help="use an existing baseline worktree instead of creating a temporary detached worktree",
    )
    parser.add_argument(
        "--allow-baseline-mismatch",
        action="store_true",
        help="allow --baseline-root HEAD to differ from --baseline-ref (recorded in manifest)",
    )
    parser.add_argument(
        "--out",
        type=Path,
        help="new artifact directory (default: CANDIDATE_ROOT/build/uitree-redraw/<timestamp>)",
    )
    parser.add_argument("--profile", choices=sorted(PROFILE_DEFAULTS), default="quick")
    parser.add_argument("--seed", type=uint32_int, default=0x5EED239)
    parser.add_argument("--visual-frames", type=positive_int)
    parser.add_argument("--bench-frames", type=positive_int)
    parser.add_argument("--trials", type=positive_int)
    parser.add_argument("--bootstrap-samples", type=positive_int)
    parser.add_argument(
        "--regression-tolerance",
        type=nonnegative_float,
        default=0.03,
        help="maximum non-primary CI ratio regression (default: 0.03)",
    )
    parser.add_argument(
        "--practical-floor-ns",
        type=nonnegative_float,
        default=100.0,
        help="waive non-primary ratio noise below this median ns/op increase (default: 100)",
    )
    parser.add_argument("--make", default=os.environ.get("MAKE", "make"))
    parser.add_argument(
        "--allow-dirty",
        action="store_true",
        help="permit a dirty candidate for development only; final evidence is marked non-reproducible",
    )
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--baseline-harness", type=Path)
    parser.add_argument("--candidate-harness", type=Path)
    parser.add_argument(
        "--skip-native",
        action="store_true",
        help="skip native unit checks and optimized client link (diagnostic; evidence fails)",
    )
    parser.add_argument("--skip-visual", action="store_true")
    parser.add_argument("--skip-chrome", action="store_true")
    parser.add_argument("--skip-bench", action="store_true")
    parser.add_argument(
        "--keep-baseline-worktree",
        action="store_true",
        help="retain the automatically created detached worktree for debugging",
    )
    args = parser.parse_args(argv)
    if args.out is None:
        args.out = args.candidate_root / "build" / "uitree-redraw" / f"{stamp}-{os.getpid()}"
    defaults = PROFILE_DEFAULTS[args.profile]
    for name in ("visual_frames", "bench_frames", "trials", "bootstrap_samples"):
        if getattr(args, name) is None:
            setattr(args, name, defaults[name])
    if args.trials < len(BENCH_VARIANT_ORDERS) and not args.skip_bench:
        parser.error("--trials must be at least 6 when benchmarks are enabled")
    if args.trials % len(BENCH_VARIANT_ORDERS) and not args.skip_bench:
        parser.error("--trials must be a multiple of 6 for the balanced variant-order schedule")
    if args.bootstrap_samples < 1000 and not args.skip_bench:
        parser.error("--bootstrap-samples must be at least 1000 when benchmarks are enabled")
    if args.skip_build and not (args.baseline_harness and args.candidate_harness):
        parser.error("--skip-build requires --baseline-harness and --candidate-harness")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        suite = Suite(args)
    except (OSError, SuiteError) as exc:
        print(f"uitree_redraw_suite: setup failed: {exc}", file=sys.stderr)
        return 2

    try:
        suite.execute()
    except KeyboardInterrupt:
        suite.fatal_error = "interrupted"
    except Exception as exc:  # Preserve evidence and a traceback for all execution failures.
        suite.fatal_error = f"{type(exc).__name__}: {exc}"
        traceback_text = traceback.format_exc()
        (suite.logs / "fatal.log").write_text(traceback_text, encoding="utf-8", newline="\n")
        print(f"uitree_redraw_suite: {suite.fatal_error}", file=sys.stderr)
    finally:
        try:
            suite.clean_worktree()
        except Exception as cleanup_exc:
            suite.baseline_cleanup = f"cleanup error: {cleanup_exc}"
            if suite.fatal_error is None:
                suite.fatal_error = f"baseline cleanup failed: {cleanup_exc}"
        try:
            suite.finalize()
        except Exception as finalize_exc:
            print(f"uitree_redraw_suite: could not finalize artifacts: {finalize_exc}", file=sys.stderr)
            traceback.print_exc()
            return 2

    print(
        f"uitree_redraw_suite: {'PASS' if suite.passed() else 'FAIL'}; artifacts: {suite.output}",
        flush=True,
    )
    return 0 if suite.passed() else 1


if __name__ == "__main__":
    raise SystemExit(main())
