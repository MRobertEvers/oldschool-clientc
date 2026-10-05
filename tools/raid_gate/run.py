#!/usr/bin/env python3
"""tools/quest_gate/run.py over the raid suite, test/raids/ (tools/raid_gate/README.md).

Same arguments, same output, same exit code: this sets TORIRS_QUEST_TESTS_DIR
to test/raids and TORIRS_QUEST_PUBLISH_DIR to the raid publish root
(selftest/minigames/<raid>/<room>/play/), then execs the quest gate's run.py.

`<test id> --name X` (raid seam29): the quest gate honours --name only for
--script or a party test, and silently ran a solo test under its own id --
so two "scratch" runs of one room collided on build/quest_gate/<id>/ and the
name (which seeds the server's random numbers) never applied.  Here a solo
test id with --name is run as `--script test/raids/<id>.lua --name X
--fixture <its fixture>`: the same file, setup list and frame budget, under
the new name.  A renamed run is a scratch, so it needs --no-publish, and it
takes exactly one test id; anything else is refused loudly.
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import suite  # noqa: E402

PARTY_RE = re.compile(r"(?m)^\s{0,8}party\s*=\s*(\d+)\s*,")
FIXTURE_RE = re.compile(r'fixture\s*=\s*"([^"]+)"')  # tools/quest_gate/run.py's own


def option_value(argv, option):
    """The value of `--option V` or `--option=V`, else None."""
    for index, token in enumerate(argv):
        if token == option and index + 1 < len(argv):
            return argv[index + 1]
        if token.startswith(option + "="):
            return token.split("=", 1)[1]
    return None


def rename_solo_test(argv):
    """argv with a solo `<id> --name X` turned into a --script run; unchanged
    when there is no --name, a --script, or a party run (the quest gate
    renames a party itself).  Exits 2 with a reason when it cannot apply."""
    name = option_value(argv, "--name")
    if name is None or "--script" in argv or any(token.startswith("--script=") for token in argv):
        return argv
    ids = [token for token in argv if not token.startswith("-")
           and os.path.isfile(os.path.join(suite.RAID_TESTS_DIR, token + ".lua"))]
    if "--all" in argv or len(ids) != 1:
        sys.exit("tools/raid_gate/run.py: --name X renames ONE test id's run; got %s"
                 % (ids or "none") + (" with --all" if "--all" in argv else ""))
    test_file = os.path.join(suite.RAID_TESTS_DIR, ids[0] + ".lua")
    with open(test_file, "r", encoding="utf-8") as handle:
        text = handle.read()
    party = option_value(argv, "--party")
    declared = PARTY_RE.findall(text)
    if (party is not None and int(party) > 1) or (party is None and declared and int(declared[0]) > 1):
        return argv
    if "--no-publish" not in argv:
        sys.exit("tools/raid_gate/run.py: --name %s with test id %s needs --no-publish "
                 "(a renamed run is a scratch, never the test's evidence)" % (name, ids[0]))
    rewritten = [token for token in argv if token != ids[0]]
    rewritten = ["--script", test_file] + rewritten
    if option_value(argv, "--fixture") is None:
        match = FIXTURE_RE.search(text)
        assert match, "%s: no fixture = \"...\" field" % test_file
        rewritten += ["--fixture", match.group(1)]
    print("tools/raid_gate/run.py: %s --name %s -> --script %s (same file, setup and frame budget)"
          % (ids[0], name, os.path.relpath(test_file, suite.REPO_ROOT)), flush=True)
    return rewritten


if __name__ == "__main__":
    sys.argv[1:] = rename_solo_test(sys.argv[1:])
    sys.exit(suite.exec_quest_gate("run.py"))
