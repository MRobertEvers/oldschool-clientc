#!/usr/bin/env python3
"""tools/quest_gate/run.py over the raid suite, test/raids/ (tools/raid_gate/README.md).

Same arguments, same output, same exit code: this sets TORIRS_QUEST_TESTS_DIR
to test/raids and TORIRS_QUEST_PUBLISH_DIR to the raid publish root
(selftest/minigames/<raid>/<room>/play/), then execs the quest gate's run.py.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import suite  # noqa: E402

if __name__ == "__main__":
    sys.exit(suite.exec_quest_gate("run.py"))
