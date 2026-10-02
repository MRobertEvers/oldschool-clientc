#!/usr/bin/env python3
"""The raid suite's location, shared by tools/raid_gate/run.py and gate.py.

A raid room test is a quest-driver test (docs/RAID_ORCHESTRATOR.md section 6)
run by the quest gate's own run.py and gate.py, but it lives under test/raids/
so the quest loop -- `tools/quest_gate/run.py --all`, `gate.py --all`,
`make test-quests` -- never discovers it (the owner's rule of 2026-10-02: the
raid loop never interacts with the quest loop). The two wrappers set
TORIRS_QUEST_TESTS_DIR and TORIRS_QUEST_PUBLISH_DIR (tools/quest_gate/
quest_list.py) and exec the quest gate script with the same argv.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
QUEST_GATE_DIR = os.path.join(REPO_ROOT, "tools", "quest_gate")
RAID_TESTS_DIR = os.path.join(REPO_ROOT, "test", "raids")
QUEST_TESTS_DIR = os.path.join(REPO_ROOT, "test", "quests")
QUEUE_TSV_PATH = os.path.join(QUEST_TESTS_DIR, "QUEUE.tsv")
# A passing room's ledger and shots: selftest/minigames/<raid>/<room>/play/
# (quest_list.suite_publish_subdir: tob_maiden -> tob/maiden).
RAID_PUBLISH_DIR = os.path.join(REPO_ROOT, "OSRS-Content", "osrs239-content", "server", "scripts",
                                "selftest", "minigames")


def raid_names():
    names = []
    for entry in sorted(os.listdir(RAID_TESTS_DIR)):
        if entry.endswith(".lua") and not entry.startswith("_"):
            names.append(entry[:-len(".lua")])
    return names


def quest_ids():
    """Every id the quest loop owns: its files and its QUEUE.tsv test_id
    column (read only)."""
    ids = set()
    for entry in os.listdir(QUEST_TESTS_DIR):
        if entry.endswith(".lua"):
            ids.add(entry[:-len(".lua")])
    if os.path.isfile(QUEUE_TSV_PATH):
        with open(QUEUE_TSV_PATH, "r", encoding="utf-8") as handle:
            header = handle.readline().rstrip("\n").split("\t")
            column = header.index("test_id")
            for line in handle:
                fields = line.rstrip("\n").split("\t")
                if len(fields) > column:
                    ids.add(fields[column])
    return ids


def collisions():
    """Raid test ids that are also quest ids. build/quest_gate/<id>/, the
    session lock and the checkpoints are keyed by id alone, so such a name
    would overwrite (or be refused by) a quest's run."""
    taken = quest_ids()
    return [name for name in raid_names() if name in taken]


def exec_quest_gate(script):
    clash = collisions()
    if clash:
        print("raid_gate: REFUSING: test/raids/%s.lua shares its id with the quest loop "
              "(test/quests/ or QUEUE.tsv); build/quest_gate/<id>/ and the session lock are "
              "keyed by id -- rename the raid test" % ", ".join(clash), file=sys.stderr)
        return 2
    os.environ["TORIRS_QUEST_TESTS_DIR"] = RAID_TESTS_DIR
    os.environ["TORIRS_QUEST_PUBLISH_DIR"] = RAID_PUBLISH_DIR
    target = os.path.join(QUEST_GATE_DIR, script)
    sys.stdout.flush()
    os.execv(sys.executable, [sys.executable, target] + sys.argv[1:])
    return 1  # not reached
