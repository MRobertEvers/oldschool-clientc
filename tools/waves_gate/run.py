#!/usr/bin/env python3
"""tools/quest_gate/run.py over the waves suite, test/waves/ (tools/waves_gate/README.md).

Same arguments, same output, same exit code: this sets TORIRS_QUEST_TESTS_DIR
to test/waves and TORIRS_QUEST_PUBLISH_DIR to the minigame publish root
(selftest/minigames/<game>/<unit>/play/), then execs the quest gate's run.py.
Copied from tools/raid_gate/run.py at 94f55b306 (docs/minigames/waves_loop/FORKED_FROM.md).
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import suite  # noqa: E402

if __name__ == "__main__":
    sys.exit(suite.exec_quest_gate("run.py"))
