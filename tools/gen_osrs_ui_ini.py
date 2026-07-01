#!/usr/bin/env python3
"""Generate rev_osrs_ui.ini and rev_osrs_ui_cache.ini from cache interface dumps."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from gen_osrs_ui_common import DEFAULT_COMPONENT_HEADER, DUMP_BIN, OUT_DIR, generate


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cache_dir", type=Path, nargs="?", default=Path("cache"))
    parser.add_argument("--prefix", default="rev_osrs")
    parser.add_argument("--header", default="OSRS dat2 UI sprite cache (symbolic names + archive_id)")
    parser.add_argument("--out-dir", type=Path, default=OUT_DIR)
    parser.add_argument("--dump-bin", type=Path, default=DUMP_BIN)
    args = parser.parse_args()
    return generate(
        args.cache_dir.resolve(),
        args.dump_bin,
        args.out_dir,
        args.prefix,
        args.header,
        DEFAULT_COMPONENT_HEADER,
    )


if __name__ == "__main__":
    raise SystemExit(main())
