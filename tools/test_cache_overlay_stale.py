#!/usr/bin/env python3
"""Hermetic test for cache_overlay_stale.py — the launchers' bake predicate.

Every case here is a way a launcher could wrongly decide "up to date" and run a
client against a cache that does not contain the content the tree states.
"""

from __future__ import annotations

import importlib.util
import io
import os
import tempfile
from contextlib import redirect_stdout, redirect_stderr
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
TOOL = REPO / "tools/cache_overlay_stale.py"
LANE = "rs2012_qbd_td"


def load_tool():
    spec = importlib.util.spec_from_file_location("cache_overlay_stale", TOOL)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def put(path: Path, value: str = "x", mtime: float | None = None) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(value)
    if mtime is not None:
        os.utime(path, (mtime, mtime))
    return path


def build_fixture(root: Path, cache_time: float) -> tuple[Path, Path, Path]:
    """A content tree, a base cache, and a composed cache baked at cache_time."""

    tree = root / "content"
    put(tree / "ported" / LANE / "PROVENANCE.md", mtime=cache_time - 100)
    put(tree / "ported" / LANE / "pack/7_models.pack", "110000=m\n",
        mtime=cache_time - 100)
    put(tree / "models/ported" / LANE / "rs2012_model_70260.ob3",
        mtime=cache_time - 100)
    put(tree / "sprites/ported" / LANE / "mat/0.bmp", mtime=cache_time - 100)

    base = root / "cache.base"
    put(base / "main_file_cache.dat2", mtime=cache_time - 100)
    put(base / "main_file_cache.idx7", mtime=cache_time - 100)

    cache = root / "cache.lane"
    for name in ("main_file_cache.dat2", "main_file_cache.idx7",
                 "main_file_cache.idx255"):
        put(cache / name, mtime=cache_time)
    return tree, base, cache


def run(tool, tree: Path, cache: Path, base: Path, extra: list[Path] = []) -> int:
    argv = ["--cache", str(cache), "--lane", LANE, "--tree", str(tree),
            "--base", str(base)]
    for path in extra:
        argv += ["--input", str(path)]
    saved = os.sys.argv
    os.sys.argv = ["cache_overlay_stale.py"] + argv
    try:
        with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            return tool.main()
    finally:
        os.sys.argv = saved


def check(label: str, got: int, want: int, failures: list[str]) -> None:
    name = {0: "STALE", 1: "FRESH", 2: "ERROR"}
    if got != want:
        failures.append(f"{label}: got {name.get(got, got)}, want {name[want]}")
        print(f"  FAIL {label}: got {name.get(got, got)}, want {name[want]}")
    else:
        print(f"  ok   {label}: {name[want]}")


def main() -> int:
    tool = load_tool()
    failures: list[str] = []
    now = 1_000_000.0

    with tempfile.TemporaryDirectory(prefix="cache-stale-test-") as tmp:
        root = Path(tmp)
        tree, base, cache = build_fixture(root, now)

        check("untouched tree", run(tool, tree, cache, base),
              tool.FRESH, failures)

        # The case that started this: a model lands in an asset subtree after
        # the bake. The lane's own directory is untouched, so a check that only
        # watched ported/ would call this fresh.
        model = tree / "models/ported" / LANE / "rs2012_model_70260.ob3"
        os.utime(model, (now + 10, now + 10))
        check("newer asset payload", run(tool, tree, cache, base),
              tool.STALE, failures)
        os.utime(model, (now - 100, now - 100))
        check("asset payload restored", run(tool, tree, cache, base),
              tool.FRESH, failures)

        # A config/pack edit inside the lane itself.
        pack = tree / "ported" / LANE / "pack/7_models.pack"
        os.utime(pack, (now + 10, now + 10))
        check("newer lane pack", run(tool, tree, cache, base),
              tool.STALE, failures)
        os.utime(pack, (now - 100, now - 100))

        # A re-unpacked base cache changes every record the lane does not state.
        os.utime(base / "main_file_cache.idx7", (now + 10, now + 10))
        check("newer base cache", run(tool, tree, cache, base),
              tool.STALE, failures)
        os.utime(base / "main_file_cache.idx7", (now - 100, now - 100))

        # An edited stager or packer changes the output from identical sources.
        stager = put(root / "tools/stage.py", mtime=now + 10)
        check("newer stager", run(tool, tree, cache, base, [stager]),
              tool.STALE, failures)
        os.utime(stager, (now - 100, now - 100))
        check("older stager", run(tool, tree, cache, base, [stager]),
              tool.FRESH, failures)
        check("absent extra input",
              run(tool, tree, cache, base, [root / "tools/gone.py"]),
              tool.FRESH, failures)

        # A bake that died after the container and before the tables leaves a
        # cache whose NEWEST file is current. The stamp is the oldest for this
        # reason; without it this case reads as fresh.
        os.utime(cache / "main_file_cache.idx7", (now - 200, now - 200))
        check("half-written cache", run(tool, tree, cache, base),
              tool.STALE, failures)
        os.utime(cache / "main_file_cache.idx7", (now, now))

        # No cache at all: a fresh checkout must bake, not skip.
        (cache / "main_file_cache.dat2").unlink()
        check("no cache", run(tool, tree, cache, base), tool.STALE, failures)
        put(cache / "main_file_cache.dat2", mtime=now)

        # The force knob the launchers expose.
        os.environ["TORIRS_FORCE_CACHE_BAKE"] = "1"
        check("TORIRS_FORCE_CACHE_BAKE=1", run(tool, tree, cache, base),
              tool.STALE, failures)
        del os.environ["TORIRS_FORCE_CACHE_BAKE"]

        # A misspelled lane is not "nothing changed" — the caller must bake (or
        # fail) rather than silently skip forever.
        argv_saved = os.sys.argv
        os.sys.argv = ["cache_overlay_stale.py", "--cache", str(cache),
                       "--lane", "no_such_lane", "--tree", str(tree)]
        try:
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                got = tool.main()
        finally:
            os.sys.argv = argv_saved
        check("unknown lane", got, tool.ERROR, failures)

    if failures:
        print(f"test_cache_overlay_stale: FAIL ({len(failures)})")
        return 1
    print("test_cache_overlay_stale: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
