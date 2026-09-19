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
"""

import os


def quests_dir(repo_root):
    assert repo_root
    return os.path.join(repo_root, "test", "quests")


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
