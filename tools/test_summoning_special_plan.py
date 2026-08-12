#!/usr/bin/env python3
"""Keep the human Summoning coverage matrix aligned with the live registry."""

from __future__ import annotations

import re
import sys
from pathlib import Path

from summoning_special_registry import IMPLEMENTED, records


REPO = Path(__file__).resolve().parents[1]
PLAN = REPO / "SUMMONING_SPECIALS.md"


def familiar_key(name: str) -> str:
    key = name.upper().replace(" ", "_").replace("-", "_")
    return {
        "ABYSSAL_LURKER": "ABYSSAL_LUKRER",  # retained source roster typo
        "FORGE_REGENT": "FORGE_REGENT_BEAST",
        "KARAMTHULHU_OVERLORD": "KARAMTHULHU",
    }.get(key, key)


def main() -> int:
    try:
        rows = re.findall(
            r"^\| \[([ x])\] \| (\d+) \| ([^|]+?) — [^|]+ \|",
            PLAN.read_text(encoding="utf-8"), re.MULTILINE,
        )
        assert len(rows) == 78, f"expected 78 coverage rows, got {len(rows)}"
        by_key = {record.familiar.removesuffix("_POUCH"): record for record in records()}
        seen: set[int] = set()
        documented_done: set[int] = set()
        for mark, documented_type, familiar in rows:
            record = by_key.get(familiar_key(familiar))
            assert record is not None, f"coverage row has no registry familiar: {familiar}"
            assert int(documented_type) == record.type, (
                f"coverage row type {documented_type} does not match registry type "
                f"{record.type}: {familiar}"
            )
            assert record.type not in seen, f"coverage row duplicates type {record.type}: {familiar}"
            seen.add(record.type)
            if mark == "x":
                documented_done.add(record.type)
        assert seen == set(range(1, 79)), "coverage matrix is not the registry's 78-familiar roster"
        assert documented_done == IMPLEMENTED, (
            "coverage-matrix done marks diverged from the enabled registry rows"
        )
    except (AssertionError, OSError, ValueError, KeyError) as exc:
        print(f"test_summoning_special_plan: error: {exc}", file=sys.stderr)
        return 1

    print(
        "test_summoning_special_plan: "
        f"78 registry-mapped coverage rows, {len(IMPLEMENTED)} enabled, 0 errors"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
