#!/usr/bin/env python3
"""Turn a quest run's artefacts into a verdict.

UNIMPLEMENTED -- owner: tests (docs/ARCHITECT.md).

Red on, and only on, things that mean the test did not actually happen:

  * any FAIL row in ledger.tsv (header `quest-ledger-v1`; columns index, step,
    verdict, ticks, shots, detail, plus a trailing summary row),
  * a missing ledger, or one whose summary row disagrees with its rows,
  * a step that claims a shot that is not on disk,
  * a shot that is on disk but undersized (a capture that raced the renderer),
  * two shots with the same MD5 -- the failure that looks most like a pass: a
    screenshot per interaction, all of them identical, is a driver that never
    drove anything.

Exits non-zero until it is written.
"""

import sys

print("tools/quest_gate/gate.py: UNIMPLEMENTED (owner: tests)", file=sys.stderr)
sys.exit(2)
