#!/usr/bin/env python3
"""The waves suite's location, shared by tools/waves_gate/run.py and gate.py.

Copied from the raid loop's tools/raid_gate/suite.py at 94f55b306
(docs/minigames/waves_loop/FORKED_FROM.md) and changed to the waves layout.

A wave test is a quest-driver test (docs/WAVES_ORCHESTRATOR.md section 6) run
by the quest gate's own run.py and gate.py, but it lives under test/waves/ so
the quest loop -- `tools/quest_gate/run.py --all`, `gate.py --all`,
`make test-quests` -- never discovers it (section 2: the waves loop is
independent of the quest loop). The two wrappers set TORIRS_QUEST_TESTS_DIR
and TORIRS_QUEST_PUBLISH_DIR (tools/quest_gate/quest_list.py) and run the quest
gate script with the same argv.

Test ids are `<game>_<unit>`: `inferno_nibblers`, `inferno_zuk`,
`colosseum_sol_heredit`. The publish subdirectory is the id split at its first
`_` (quest_list.suite_publish_subdir), so a PASS lands under
selftest/minigames/<game>/<unit>/play/.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
QUEST_GATE_DIR = os.path.join(REPO_ROOT, "tools", "quest_gate")
WAVES_TESTS_DIR = os.path.join(REPO_ROOT, "test", "waves")
QUEST_TESTS_DIR = os.path.join(REPO_ROOT, "test", "quests")
QUEUE_TSV_PATH = os.path.join(QUEST_TESTS_DIR, "QUEUE.tsv")
# A passing unit's ledger and shots: selftest/minigames/<game>/<unit>/play/
# (quest_list.suite_publish_subdir: inferno_nibblers -> inferno/nibblers).
WAVES_PUBLISH_DIR = os.path.join(REPO_ROOT, "OSRS-Content", "osrs239-content", "server", "scripts",
                                 "selftest", "minigames")
# The games a wave id may name (docs/WAVES_ORCHESTRATOR.md section 1).
GAMES = ("inferno", "colosseum")


def wave_names():
    names = []
    if not os.path.isdir(WAVES_TESTS_DIR):
        return names
    for entry in sorted(os.listdir(WAVES_TESTS_DIR)):
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
    """Wave test ids that are also quest ids. build/quest_gate/<id>/, the
    session lock and the checkpoints are keyed by id alone, so such a name
    would overwrite (or be refused by) a quest's run."""
    taken = quest_ids()
    return [name for name in wave_names() if name in taken]


def misnamed():
    """Wave test ids that do not start with a known game (`<game>_<unit>`):
    the publish path and the coverage grader both read the game from the id."""
    return [name for name in wave_names()
            if name.partition("_")[0] not in GAMES or not name.partition("_")[2]]


def refusal():
    clash = collisions()
    if clash:
        return ("waves_gate: REFUSING: test/waves/%s.lua shares its id with the quest loop "
                "(test/quests/ or QUEUE.tsv); build/quest_gate/<id>/ and the session lock are "
                "keyed by id -- rename the wave test" % ", ".join(clash))
    bad = misnamed()
    if bad:
        return ("waves_gate: REFUSING: test/waves/%s.lua is not named <game>_<unit> with <game> "
                "one of %s" % (", ".join(bad), ", ".join(GAMES)))
    return None


def suite_env():
    return dict(os.environ, TORIRS_QUEST_TESTS_DIR=WAVES_TESTS_DIR,
                TORIRS_QUEST_PUBLISH_DIR=WAVES_PUBLISH_DIR)


def run_quest_gate(script):
    """exec_quest_gate as a child process: the exit code comes back so the
    caller can grade more after it."""
    import subprocess
    why = refusal()
    if why:
        print(why, file=sys.stderr)
        return 2
    sys.stdout.flush()
    return subprocess.call([sys.executable, os.path.join(QUEST_GATE_DIR, script)] + sys.argv[1:],
                           env=suite_env())


def exec_quest_gate(script):
    why = refusal()
    if why:
        print(why, file=sys.stderr)
        return 2
    os.environ["TORIRS_QUEST_TESTS_DIR"] = WAVES_TESTS_DIR
    os.environ["TORIRS_QUEST_PUBLISH_DIR"] = WAVES_PUBLISH_DIR
    target = os.path.join(QUEST_GATE_DIR, script)
    sys.stdout.flush()
    os.execv(sys.executable, [sys.executable, target] + sys.argv[1:])
    return 1  # not reached
