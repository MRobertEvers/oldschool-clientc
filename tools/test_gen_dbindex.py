#!/usr/bin/env python3
"""Mutation test for tools/gen_dbindex.py."""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
SOURCE = REPO / "OSRS-Content/osrs239-content"
GENERATOR = REPO / "tools/gen_dbindex.py"


def run(content: Path, mode: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(GENERATOR), "--content", str(content), mode],
        cwd=REPO,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


def main() -> int:
    checks = 0
    with tempfile.TemporaryDirectory(prefix="summoning-dbindex-") as raw_tmp:
        content = Path(raw_tmp) / "content"
        configs = content / "configs"
        configs.mkdir(parents=True)
        for name in (
            "all.dbrow",
            "all.dbrow.compack",
            "all.dbtable",
            "all.dbtable.compack",
        ):
            shutil.copy2(SOURCE / "configs" / name, configs / name)
        shutil.copytree(SOURCE / "dbindex", content / "dbindex")

        clean = run(content, "--check")
        checks += 1
        if clean.returncode != 0 or "verified 147 index files; stale=0" not in clean.stdout:
            print(clean.stdout, end="")
            print("test_gen_dbindex: pristine regeneration did not match", file=sys.stderr)
            return 1

        target = content / "dbindex/dbindex_212.dbi"
        text = target.read_text(encoding="utf-8")
        needle = "index=0:0:9656,9657,9658"
        checks += 1
        if needle not in text:
            print("test_gen_dbindex: mutation fixture disappeared", file=sys.stderr)
            return 1
        target.write_text(text.replace(needle, "index=0:0:9658,9657", 1), encoding="utf-8")

        stale = run(content, "--check")
        checks += 1
        if stale.returncode == 0 or "1 stale of 147 index files" not in stale.stdout:
            print(stale.stdout, end="")
            print("test_gen_dbindex: omitted/misordered rows were not rejected", file=sys.stderr)
            return 1

        repaired = run(content, "--write")
        checks += 1
        if repaired.returncode != 0 or "rewrote 147 index files; stale=1" not in repaired.stdout:
            print(repaired.stdout, end="")
            print("test_gen_dbindex: repair failed", file=sys.stderr)
            return 1

        source_files = sorted((SOURCE / "dbindex").glob("*.dbi"))
        checks += len(source_files)
        for source in source_files:
            candidate = content / "dbindex" / source.name
            if source.read_bytes() != candidate.read_bytes():
                print(f"test_gen_dbindex: repair not byte-identical: {source.name}", file=sys.stderr)
                return 1

        final = run(content, "--check")
        checks += 1
        if final.returncode != 0:
            print(final.stdout, end="")
            print("test_gen_dbindex: repaired tree is still stale", file=sys.stderr)
            return 1

        # Authored client rows live outside configs/all.dbrow, receive stable
        # ids from the allocation ledger, and must become visible to DB_FIND.
        pack = content / "pack"
        pack.mkdir()
        (pack / "dbrow.alloc").write_text(
            "70000=summoning_test_subsection\n", encoding="utf-8"
        )
        authored = configs / "ported/test.dbrow"
        authored.parent.mkdir(parents=True)
        authored.write_text(
            "[summoning_test_subsection]\n"
            "columns=4\n"
            "table=skill_guide_subsections\n"
            "columndef=0:skill,int\n"
            "values=0:0:25\n"
            "columndef=1:id,int\n"
            "values=1:0:1\n"
            "columndef=2:header,string\n"
            "values=2:0:Familiars\n"
            "columndef=3:membersonly,boolean\n"
            "values=3:0:1\n",
            encoding="utf-8",
        )
        overlay_stale = run(content, "--check")
        checks += 1
        if overlay_stale.returncode == 0 or "1 stale of 147 index files" not in overlay_stale.stdout:
            print(overlay_stale.stdout, end="")
            print("test_gen_dbindex: authored row did not stale its index", file=sys.stderr)
            return 1
        overlay_write = run(content, "--write")
        checks += 1
        if overlay_write.returncode != 0:
            print(overlay_write.stdout, end="")
            print("test_gen_dbindex: authored row regeneration failed", file=sys.stderr)
            return 1
        guide_index = (content / "dbindex/dbindex_212.dbi").read_text(encoding="utf-8")
        checks += 2
        if "index=0:0:" not in guide_index or "70000" not in guide_index:
            print("test_gen_dbindex: authored row missing from master index", file=sys.stderr)
            return 1
        if "index=0:25:70000" not in guide_index:
            print("test_gen_dbindex: authored skill key missing from column index", file=sys.stderr)
            return 1

    if checks == 0:
        print("test_gen_dbindex: zero checks", file=sys.stderr)
        return 1
    print(f"test_gen_dbindex: {checks} checks, 0 errors")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
