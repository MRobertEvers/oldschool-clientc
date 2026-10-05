#!/usr/bin/env python3
"""Write the driver scripts a WATCHED client can play, and their index.

    python3 tools/raid_gate/prepare_scripts.py [--out build/quest_gate/_scripts]

A watched client (TORIRS_DRIVE_ON_DEMAND=1, profiles/osrs239-scripts.ini, the
Scripts tab) starts a script with api.drive.start(path, session_dir). The path
it is handed must be what run.py itself would run: the test file wrapped by
run.py's own write_wrapper_script, so the setup cheats, the login-grant wait,
the give/count checks and the legs harness are the ones a test run gets. This
writes one such file per test under test/raids/ and an index the tab reads:

    index.tsv   id  title  path  max_frames  fixture  available  reason

  - A file whose name starts with `_` is a harness, not a test, and is skipped
    (_party_smoke).
  - A test that declares `party = <n>` needs n clients in lock step
    (run.py run_party); one watched client cannot play it. It is listed with
    available=0 and that reason, and no script is written for it.
  - A test whose fixture is not fresh_lumbridge.ini needs a character the
    watch account does not have (the watch account is whoever logged in; a
    fixture is staged by run.py before login, never by the driver). It is
    listed with available=0 and the fixture named. Every raid room today is
    on fresh_lumbridge.ini and its setup is bring-alongs plus t.raid.enter,
    so a prepared room runs on any account at staff level 2.
  - Quests (test/quests/) are not listed yet: a quest needs its fixture at
    login (a later pass).

`max_frames` is the test's own frame budget as run.py reads it. A watched run
is not bounded by it (the client runs until its person closes it); the tab
may show it as the run's expected length.

Nothing here changes what run.py does: it only imports run.py's readers and
its wrapper writer.
"""

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
QUEST_GATE = os.path.join(REPO_ROOT, "tools", "quest_gate")

# tools/quest_gate/run.py, loaded by PATH under a name of its own: this
# directory has a run.py too (the raid gate's), and it is first on sys.path.
# quest_gate/run.py puts its own directory on the path for its imports.
import importlib.util  # noqa: E402

_spec = importlib.util.spec_from_file_location("quest_gate_run", os.path.join(QUEST_GATE, "run.py"))
quest_run = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(quest_run)

DEFAULT_OUT = os.path.join(REPO_ROOT, "build", "quest_gate", "_scripts")
TESTS_DIR = os.path.join(REPO_ROOT, "test", "raids")
INDEX_NAME = "index.tsv"
INDEX_HEADER = ("id", "title", "path", "max_frames", "fixture", "available", "reason")

RAID_NAMES = {
    "tob": "Theatre of Blood",
    "cox": "Chambers of Xeric",
    "toa": "Tombs of Amascut",
}
ENTER_RE = re.compile(
    r'raid\.enter\(\s*"(?P<raid>\w+)"\s*,\s*"(?P<room>\w+)"\s*,\s*\{\s*mode\s*=\s*"(?P<mode>\w+)"')


def words(name):
    assert name
    return " ".join(part.capitalize() for part in name.split("_"))


def title_for(test_id, source, party):
    """`Theatre of Blood, Maiden, Entry solo` from the test's own first
    t.raid.enter(raid, room, {mode = ...}); the id when it has none."""
    match = ENTER_RE.search(source)
    if not match:
        return test_id
    raid = RAID_NAMES.get(match.group("raid"), match.group("raid").upper())
    who = "solo" if party == 1 else ("trio" if party == 3 else "party of %d" % party)
    return "%s, %s, %s %s" % (raid, words(match.group("room")), words(match.group("mode")), who)


def clean(text):
    """One TSV cell: no tab, no newline."""
    return " ".join(str(text).split())


def prepare(out_dir, tests_dir):
    assert out_dir
    assert tests_dir
    os.makedirs(out_dir, exist_ok=True)
    rows = []
    for name in sorted(os.listdir(tests_dir)):
        if not name.endswith(".lua") or name.startswith("_"):
            continue
        test_file = os.path.join(tests_dir, name)
        test_id = name[:-len(".lua")]
        with open(test_file, "r", encoding="utf-8") as handle:
            source = handle.read()
        party = quest_run.read_party_size(test_file)
        fixture = quest_run.read_fixture_name(test_file)
        max_frames = quest_run.read_max_frames(test_file)
        reason = ""
        if party > 1:
            reason = ("party of %d: needs %d clients in lock step (run.py runs it); "
                      "one watched client cannot" % (party, party))
        elif fixture != quest_run.DEFAULT_FIXTURE:
            reason = ("needs fixture %s at login, which the watch account (whoever logged "
                      "in) does not have" % fixture)
        script_path = ""
        stale = os.path.join(out_dir, "%s.lua" % test_id)
        if not reason:
            script_path = stale
            quest_run.write_wrapper_script(test_file, script_path)
        elif os.path.exists(stale):
            # A test that WAS playable and no longer is must not leave a
            # script behind that the index no longer vouches for.
            os.unlink(stale)
        rows.append((test_id, title_for(test_id, source, party), script_path, max_frames,
                     fixture, 0 if reason else 1, reason))
    index_path = os.path.join(out_dir, INDEX_NAME)
    temporary = index_path + ".tmp"
    with open(temporary, "w", encoding="utf-8") as handle:
        handle.write("\t".join(INDEX_HEADER) + "\n")
        for row in rows:
            handle.write("\t".join(clean(cell) for cell in row) + "\n")
    os.replace(temporary, index_path)
    return index_path, rows


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--out", default=DEFAULT_OUT,
                        help="where the scripts and index.tsv go (default %(default)s)")
    parser.add_argument("--tests", default=TESTS_DIR,
                        help="the test directory to prepare (default %(default)s)")
    arguments = parser.parse_args()
    out_dir = os.path.abspath(arguments.out)
    index_path, rows = prepare(out_dir, os.path.abspath(arguments.tests))
    available = sum(1 for row in rows if row[5])
    print("prepare_scripts: %d script(s) playable, %d listed unavailable -> %s"
          % (available, len(rows) - available, index_path))
    for row in rows:
        print("  %-22s %s%s" % (row[0], "ok " if row[5] else "-- ", row[6] or row[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
