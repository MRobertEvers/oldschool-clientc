#!/usr/bin/env python3
"""Discover quest test files under test/quests/.

A quest is test/quests/<name>.lua whose name does not start with `_` -- the
underscore prefix is reserved for the verb-conformance harness
(_conformance.lua) and any future non-quest helper. See test/quests/README.md:
"tools/quest_gate/run.py --all runs quests, this runs verbs."

Shared by run.py and gate.py so the two can never disagree about which
quests exist: a quest whose process crashed before it could even create its
own build/quest_gate/<name>/ directory must still show up as "no ledger" in
gate.py, which means gate.py has to know the name was expected without
looking at what happened to exist on disk.

TORIRS_QUEST_TESTS_DIR moves the whole suite (tests and fixtures/) to another
directory -- test/raids/, through tools/raid_gate/ -- see TESTS_DIR_ENV below.
"""

import os
import re

# The per-quest virtual-clock budget. A quest file may declare
# `max_frames = <n>,` beside its `fixture = "..."` field; run.py applies it to
# TORIRS_MAX_FRAMES for that run (and scales the wall-clock --timeout by the
# same ratio), lint_quest.py rejects one above the ceiling. Kept here so the
# runner and the lint can never disagree about the number.
DEFAULT_MAX_FRAMES = 60000
# 8x (480000 frames, ~16000 server ticks): Regicide walked honestly -- both Underground Pass
# walks, the maze bridges, the forests -- needs ~8000+ ticks and hit the old 4x ceiling at
# tick 7981 on its last leg (matthew-mbp-m4-b48 round 8). The wall-clock timeout still scales
# with max_frames, and render skip keeps a frame cheap, so a long budget costs nothing unless
# it is used.
MAX_FRAMES_CEILING = 8 * DEFAULT_MAX_FRAMES
MAX_FRAMES_RE = re.compile(r"(?m)^[ \t]*max_frames\s*=\s*(\d+)\s*,")


# A second suite run by the same tools (the raid room tests, test/raids/,
# docs/RAID_ORCHESTRATOR.md section 6) must never be discovered by the quest
# loop's `run.py --all`, `gate.py --all` or `make test-quests`, so it does not
# live under test/quests/. tools/raid_gate/run.py and gate.py set these two
# variables and exec the scripts here unchanged. Unset (or empty), nothing
# changes: the tests are test/quests/*.lua, their fixtures
# test/quests/fixtures/, and a passing run publishes to the caller's default.
# A relative value is taken from the repository root.
TESTS_DIR_ENV = "TORIRS_QUEST_TESTS_DIR"
PUBLISH_DIR_ENV = "TORIRS_QUEST_PUBLISH_DIR"


def _repo_relative(repo_root, value):
    assert repo_root
    assert value
    if os.path.isabs(value):
        return value
    return os.path.join(repo_root, value)


def quests_dir(repo_root):
    assert repo_root
    override = os.environ.get(TESTS_DIR_ENV)
    if override:
        return _repo_relative(repo_root, override)
    return os.path.join(repo_root, "test", "quests")


def fixtures_dir(repo_root):
    """Where a test's `fixture = "<name>.ini"` is read from: the suite's own
    fixtures/ directory, so a raid test never depends on a quest fixture."""
    return os.path.join(quests_dir(repo_root), "fixtures")


def publish_dir_override(repo_root):
    """TORIRS_QUEST_PUBLISH_DIR resolved, or None when it is unset -- the
    caller keeps its own default (run.py: selftest/quests)."""
    override = os.environ.get(PUBLISH_DIR_ENV)
    if not override:
        return None
    return _repo_relative(repo_root, override)


def suite_publish_subdir(test_id):
    """The publish subdirectory of a test in an overridden suite, which has no
    QUEUE.tsv row to name it: `<raid>_<room>` -> `<raid>/<room>`
    (tob_maiden -> tob/maiden, toa_150 -> toa/150), an id with no `_` as
    itself."""
    assert test_id
    head, sep, rest = test_id.partition("_")
    if not sep or not head or not rest:
        return test_id
    return os.path.join(head, rest)


def discover(repo_root):
    """Every quest name, sorted. Empty (not an error) when none exist yet --
    test/quests/ currently holds only the conformance harness; Phase D adds
    the first real quest files."""
    directory = quests_dir(repo_root)
    if not os.path.isdir(directory):
        return []
    names = []
    for entry in sorted(os.listdir(directory)):
        if entry.endswith(".lua") and not entry.startswith("_"):
            names.append(entry[:-len(".lua")])
    return names


def quest_path(repo_root, name):
    assert repo_root
    assert name
    return os.path.join(quests_dir(repo_root), "%s.lua" % name)
