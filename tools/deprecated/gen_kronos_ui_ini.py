#!/usr/bin/env python3
"""Generate rev_kronos_ui.ini and rev_kronos_ui_cache.ini from cache interface dumps."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from gen_osrs_ui_common import DUMP_BIN, OUT_DIR, generate

KRONOS_COMPONENT_HEADER = """\
; Kronos sidebar tab mappings (DisplayHandler.sendDisplay on gameframe 165).
; tabno | Name              | iface | screen 165 child
; ------|-------------------|-------|-------------------
;     0 | Combat            |   593 | 8
;     1 | Stats             |   320 | 9
;     2 | Quest journal     |   720 | 10  (iface 399 is quest detail on frame 629)
;     3 | Inventory         |   149 | 11  (Interface.INVENTORY; INV grid in cache.kronos)
;     4 | Equipment         |   387 | 12
;     5 | Prayer            |   541 | 13
;     6 | Magic             |   218 | 14
;     7 | Clan chat         |     7 | 15
;     8 | Account           |   720 | 16
;     9 | Friends           |   429 | 17  (432 when ignore list active)
;    10 | Logout            |   182 | 18
;    11 | Options           |   261 | 19
;    12 | Emotes            |   216 | 20
;    13 | Music             |   239 | 21
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cache_dir", type=Path)
    parser.add_argument("--prefix", default="rev_kronos")
    parser.add_argument("--header", default="Kronos dat2 sprite cache (symbolic names + archive_id)")
    parser.add_argument("--out-dir", type=Path, default=OUT_DIR)
    parser.add_argument("--dump-bin", type=Path, default=DUMP_BIN)
    args = parser.parse_args()
    return generate(
        args.cache_dir.resolve(),
        args.dump_bin,
        args.out_dir,
        args.prefix,
        args.header,
        KRONOS_COMPONENT_HEADER,
    )


if __name__ == "__main__":
    raise SystemExit(main())
