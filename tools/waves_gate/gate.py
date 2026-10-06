#!/usr/bin/env python3
"""tools/quest_gate/gate.py over the waves suite, test/waves/ (tools/waves_gate/README.md).

Same arguments, same output, same exit code: this sets TORIRS_QUEST_TESTS_DIR
to test/waves and TORIRS_QUEST_PUBLISH_DIR to the minigame publish root
(selftest/minigames/<game>/<unit>/play/), then runs the quest gate's gate.py.
Copied from tools/raid_gate/gate.py at 94f55b306 (docs/minigames/waves_loop/FORKED_FROM.md).
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import suite  # noqa: E402

if __name__ == "__main__":
    # The quest gate runs as a child so that wave coverage (the encounter spec
    # table against the tick ledger, tools/waves_gate/waves_coverage.py) can be
    # graded afterwards for every test id named on the command line; a FULL
    # coverage is part of a unit's green (docs/WAVES_ORCHESTRATOR.md section 6).
    code = suite.run_quest_gate("gate.py")
    ids = [a for a in sys.argv[1:] if not a.startswith("-")]
    if ids and code != 2 and "--probe" not in sys.argv and "--cutscene-as" not in sys.argv:
        import waves_coverage
        cov = waves_coverage.main(["waves_coverage"] + ids)
        if cov and not code:
            code = cov
    sys.exit(code)
