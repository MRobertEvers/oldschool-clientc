#!/usr/bin/env python3
"""tools/quest_gate/gate.py over the raid suite, test/raids/ (tools/raid_gate/README.md).

Same arguments, same output, same exit code: this sets TORIRS_QUEST_TESTS_DIR
to test/raids and TORIRS_QUEST_PUBLISH_DIR to the raid publish root
(selftest/minigames/<raid>/<room>/play/), then execs the quest gate's gate.py.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import suite  # noqa: E402

if __name__ == "__main__":
    # The quest gate runs as a child so that raid coverage (the encounter spec
    # table against the tick ledger, tools/raid_gate/raid_coverage.py) can be
    # graded afterwards for every test id named on the command line; a FULL
    # coverage is part of a raid room's green (docs/RAID_ORCHESTRATOR.md 5).
    code = suite.run_quest_gate("gate.py")
    ids = [a for a in sys.argv[1:] if not a.startswith("-")]
    if ids and "--probe" not in sys.argv and "--cutscene-as" not in sys.argv:
        import raid_coverage
        cov = raid_coverage.main(["raid_coverage"] + ids)
        if cov and not code:
            code = cov
    sys.exit(code)
