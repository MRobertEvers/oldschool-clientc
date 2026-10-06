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
        for name in ("all.dbrow", "all.dbtable"):
            shutil.copy2(SOURCE / "configs" / name, configs / name)
        # A row names what it references (`data=icon,invis_rod`), so the
        # generator reads every name source cachepack spells values from:
        # each config type's member index, the stat and category packs, and
        # the interface names a component value is spelled with.
        for compack in (SOURCE / "configs").glob("*.compack"):
            shutil.copy2(compack, configs / compack.name)
        (content / "pack").mkdir()
        for name in ("stat.pack", "category.pack", "3_interfaces.pack"):
            shutil.copy2(SOURCE / "pack" / name, content / "pack" / name)
        (content / "interfaces").mkdir()
        for compack in (SOURCE / "interfaces").glob("*.compack"):
            shutil.copy2(compack, content / "interfaces" / compack.name)
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
        (pack / "dbrow.alloc").write_text(
            "70000=summoning_test_subsection\n"
            "70001=summoning_test_empty\n", encoding="utf-8"
        )
        authored = configs / "ported/test.dbrow"
        authored.parent.mkdir(parents=True)
        authored.write_text(
            "[summoning_test_subsection]\n"
            "table=skill_guide_subsections\n"
            "data=skill,25\n"
            "data=id,1\n"
            "data=header,Familiars\n"
            "data=membersonly,true\n"
            # A row with no column data in the full-key config format: every
            # key is stated, `data=default` (the column block absent). It is a
            # member of its table and has no indexed value -- a marker must
            # never parse as a column value.
            "\n"
            "[summoning_test_empty]\n"
            "table=skill_guide_subsections\n"
            "data=default\n",
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
        # Anchored on whole lines: the file's header comment names `[master]`.
        master_block = guide_index.split("\n[master]\n", 1)[1].split("\n[", 1)[0]
        column_0_block = guide_index.split("\n[column_0]\n", 1)[1].split("\n[", 1)[0]
        checks += 2
        if "70001" not in master_block:
            print("test_gen_dbindex: full-key empty row missing from master index", file=sys.stderr)
            return 1
        if "70001" in column_0_block:
            print("test_gen_dbindex: full-key empty row's markers indexed as a value",
                  file=sys.stderr)
            return 1

    if checks == 0:
        print("test_gen_dbindex: zero checks", file=sys.stderr)
        return 1
    print(f"test_gen_dbindex: {checks} checks, 0 errors")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
