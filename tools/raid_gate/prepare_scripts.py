#!/usr/bin/env python3
"""Write the scripts manifest a WATCHED client asks for: script/tests/tests.ini.

    python3 tools/raid_gate/prepare_scripts.py [--out script/tests/tests.ini]
                                               [--suite <name>=<directory> ...]

The Scripts tab (script/plugins/script_runner.lua) asks for this file through
the IO layer exactly the way the plugin host asks for plugins/plugins.ini
(src/plugin/task_plugin_io.c: one SCRIPT item, resolved under the script
directory; natively the filesystem, on the browser lane the served script
directory), and when Play is pressed it asks for the chosen test's source and
its fixture the same way. Nothing is pre-generated per test: the source is the
test file itself, read where it lives, again on every Play (hot reload).

Nobody runs this by hand. The launcher runs it on every `./launch run
osrs239-scripts` from the profile's `[derived:tests]` block
(tools/launcher/profiles.py, run_profile_derived), so the list is never older
than the launch. Run it yourself, then press Refresh in the tab, to list a test
file you created after the client started.

THE MANIFEST, one section per test, in TEST_SUITES order then by id:

    [test:<id>]
    suite=quest|raid
    title=<a human title>
    source=tests/quests/<id>.lua        ; resolved under the script directory
    fixture=tests/quests/fixtures/<fixture>.ini
    legs=<n>                            ; 0 for a run = function(t) file
    party=<n>                           ; 1 for a solo test
    max_frames=<n>                      ; run.py's budget; a watched run is not bounded by it
    available=1|0
    reason=<why not, when 0>

HOW A SOURCE IS READABLE AS A SCRIPT ITEM. The IO layer resolves a script path
under ONE root (script/, or TORIRS_SCRIPT_DIR), and io_server refuses a path
containing `..` (src/ioserver/io_server_main.c), so `../test/quests/x.lua` would
work natively and fail when the script directory is served. The tests
directories are therefore reached through two committed directory links,
script/tests/quests -> ../../test/quests and script/tests/raids ->
../../test/raids: a read through the link IS a read of the file in the tree
(no copy, nothing to go stale), and io_server's fopen follows it the same way.
A suite given with --suite and a directory outside script/ (a scratch tests
root for a proof) is named by its path relative to the script directory, which
only the native lane can read.

`_` files are harnesses, not tests (_conformance, _party_smoke, ...) and are
skipped. A test that declares `party = <n>` needs n clients in lock step (run.py
run_party): listed, available=0. A test whose fixture file is absent: listed,
available=0. Everything else is available: Play gives it a fresh account made
from its own fixture (src/plugin/torirs_plugin_drive.c, api.drive.play).

Nothing here changes what run.py does: it only imports run.py's readers.
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

SCRIPT_ROOT = os.path.join(REPO_ROOT, "script")
DEFAULT_OUT = os.path.join(SCRIPT_ROOT, "tests", "tests.ini")

# THE ONE TABLE OF TESTS DIRECTORIES. A new suite (the waves tests, when that
# branch merges) is one line here plus its link under script/tests/:
#   (suite name, directory in the tree, the same directory under the script root)
TEST_SUITES = [
    ("quest", "test/quests", "tests/quests"),
    ("raid", "test/raids", "tests/raids"),
]

RAID_NAMES = {
    "tob": "Theatre of Blood",
    "cox": "Chambers of Xeric",
    "toa": "Tombs of Amascut",
}
ENTER_RE = re.compile(
    r'raid\.enter\(\s*"(?P<raid>\w+)"\s*,\s*"(?P<room>\w+)"\s*,\s*\{\s*mode\s*=\s*"(?P<mode>\w+)"')
# The first line of a quest file is, by habit, `-- <Quest name>[: , -- ( .] ...`.
TITLE_LINE_RE = re.compile(r"^--\s*(?P<text>.+)$")
TITLE_CUT_RE = re.compile(r"\s+--\s+|[:.,(]|\s+-\s+")
TITLE_TAIL_RE = re.compile(r"\s+(quest test|quest|test)$", re.IGNORECASE)
TITLE_MAX = 40


def words(name):
    assert name
    return " ".join(part.capitalize() for part in name.split("_"))


def quest_title(test_id, source):
    """The quest's display name off the file's first comment line (`-- Cook's
    Assistant, end to end ...` -> `Cook's Assistant`), else the id with
    underscores as spaces."""
    first = source.lstrip("﻿").split("\n", 1)[0].strip()
    match = TITLE_LINE_RE.match(first)
    if match:
        text = TITLE_CUT_RE.split(match.group("text"), 1)[0].strip()
        text = TITLE_TAIL_RE.sub("", text).strip()
        if 2 <= len(text) <= TITLE_MAX:
            return text
    return test_id.replace("_", " ")


def raid_title(test_id, source, party):
    """`Theatre of Blood, Maiden, Entry solo` from the test's own first
    t.raid.enter(raid, room, {mode = ...}); the id with spaces when it has none."""
    match = ENTER_RE.search(source)
    if not match:
        return test_id.replace("_", " ")
    raid = RAID_NAMES.get(match.group("raid"), match.group("raid").upper())
    who = "solo" if party == 1 else ("trio" if party == 3 else "party of %d" % party)
    return "%s, %s, %s %s" % (raid, words(match.group("room")), words(match.group("mode")), who)


def clean(text):
    """One ini value: one line, no `;` (the reader would take it as a comment)."""
    return " ".join(str(text).replace(";", ",").split())


def script_path(directory_script_path, *parts):
    """A path under the script root, with forward slashes (io_server refuses a
    backslash)."""
    return "/".join([directory_script_path.rstrip("/")] + list(parts))


def describe(suite, directory, directory_script_path):
    """One manifest entry per test file in `directory`, sorted by id."""
    assert suite
    assert directory
    entries = []
    if not os.path.isdir(directory):
        return entries
    for name in sorted(os.listdir(directory)):
        # `_` files are harnesses and stay out of the list, except the play
        # library's own (`_play_*`: a room played through t.raid.play), which
        # the owner watches like any room (2026-10-05).
        if not name.endswith(".lua") or (name.startswith("_") and not name.startswith("_play_")):
            continue
        test_file = os.path.join(directory, name)
        test_id = name[:-len(".lua")]
        with open(test_file, "r", encoding="utf-8") as handle:
            source = handle.read()
        party = quest_run.read_party_size(test_file)
        fixture = quest_run.read_fixture_name(test_file)
        max_frames = quest_run.read_max_frames(test_file)
        _, layout = quest_run.legs_source(test_file)
        legs = len(layout["legs"]) if layout else 0
        title = raid_title(test_id, source, party) if suite == "raid" else quest_title(test_id, source)
        reason = ""
        if party > 1:
            reason = "needs %d clients (a party of %d plays in lock step under run.py)" % (party, party)
        elif not os.path.isfile(os.path.join(directory, "fixtures", fixture)):
            reason = "no fixture %s beside it" % fixture
        entries.append({
            "id": test_id,
            "suite": suite,
            "title": title,
            "source": script_path(directory_script_path, name),
            "fixture": script_path(directory_script_path, "fixtures", fixture),
            "legs": legs,
            # Raid seam25: a quest's fixture (fresh_lumbridge) applied IN PLACE
            # by ::resetcharacter gives it quest progress 0 without a relog;
            # a raid row's own setup clears and dresses, so the plain reset.
            "start": "reset" if suite == "quest" else "",
            "party": party,
            "max_frames": max_frames,
            "available": 0 if reason else 1,
            "reason": reason,
        })
    return entries


FIELDS = ("suite", "title", "source", "fixture", "legs", "party", "max_frames", "available", "reason", "start")


def write_manifest(out_path, suites):
    """Write the manifest for `suites` [(name, directory, script path)];
    returns the entries. Atomic: a client reading it mid-write sees the old
    one or the new one, never half."""
    assert out_path
    entries = []
    for suite, directory, directory_script_path in suites:
        entries.extend(describe(suite, directory, directory_script_path))
    lines = [
        "; GENERATED by tools/raid_gate/prepare_scripts.py (the osrs239-scripts profile's",
        "; [derived:tests] block runs it on every launch). Edits here are overwritten.",
        "; One [test:<id>] per test file; paths are under the script directory.",
        "",
    ]
    for entry in entries:
        lines.append("[test:%s]" % entry["id"])
        for field in FIELDS:
            lines.append("%s=%s" % (field, clean(entry[field])))
        lines.append("")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    temporary = out_path + ".tmp"
    with open(temporary, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))
    os.replace(temporary, out_path)
    return entries


def parse_suite(text):
    """`--suite raid=<directory>`: that suite read from <directory> instead,
    named by its path relative to the script root."""
    name, separator, directory = text.partition("=")
    if not separator or not name or not directory:
        raise argparse.ArgumentTypeError("--suite wants <name>=<directory>, got %r" % text)
    directory = os.path.abspath(directory)
    relative = os.path.relpath(directory, SCRIPT_ROOT).replace(os.sep, "/")
    return name, directory, relative


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--out", default=DEFAULT_OUT,
                        help="where the manifest goes (default %(default)s)")
    parser.add_argument("--suite", action="append", type=parse_suite, default=[],
                        help="<name>=<directory>: read that suite from another directory "
                             "(a scratch tests root); only the suites named are written")
    arguments = parser.parse_args()
    if arguments.suite:
        suites = arguments.suite
    else:
        suites = [(name, os.path.join(REPO_ROOT, directory), directory_script_path)
                  for name, directory, directory_script_path in TEST_SUITES]
    out_path = os.path.abspath(arguments.out)
    entries = write_manifest(out_path, suites)
    counts = []
    for suite, _, _ in suites:
        listed = [entry for entry in entries if entry["suite"] == suite]
        available = sum(1 for entry in listed if entry["available"])
        counts.append("%s %d listed, %d available" % (suite, len(listed), available))
    print("tests manifest: %s: %s" % (os.path.relpath(out_path, REPO_ROOT), "; ".join(counts)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
