#!/usr/bin/env python3
"""Run one quest test per client process, headless, and collect its artefacts.

One process per quest, never two quests in one process: a quest test leaves
server saves, varps and a scene behind it, and the next quest starting from
that would have unattributable failures (docs/QUEST_DRIVER_DESIGN.md).

For each quest named test/quests/<quest>.lua (the shape is in
test/quests/README.md: `{ id, fixture, setup = {cheats}, run = function(t)
... end }`), this:

  * builds ONE shared binary into its own objdir (OPT=1 EMBED_SERVER=1,
    PLATFORM_OBJ_BASE=build_questtest, PLATFORM_TARGET=torirs_questtest --
    never another build's objdir, several sessions build from this checkout
    at once) and rebuilds the server script pack -- which the embedded server
    refuses to boot on when stale -- when its inputs changed (a stat
    fingerprint over the script tree, pack_fingerprint.py);
  * rewrites manifests/manifest_osrs239.ini to transport=embed against this
    checkout's cache.osrs239, into manifests/.questtest.ini (the manifest's
    OWN directory is load-bearing -- run from a session dir instead and
    several content openers silently mount nothing, see conformance.py);
  * for each quest: makes a fresh, private build/quest_gate/<quest>/
    directory (never reused between runs -- the server writes on exit),
    copies the quest's fixture into <dir>/saves/<quest>.ini (a fixture that
    is not physically copied in means a fresh character stuck on the
    Character Creator modal, which reads exactly like a broken verb),
    generates a driver script that runs the quest's own `setup` cheats and
    then `run(t)` (see write_wrapper_script), and launches ONE client
    process against it, bounded by a wall-clock ceiling so a hung run fails
    the quest instead of hanging the whole suite.

Artefacts land directly under build/quest_gate/<quest>/ (ledger.tsv,
shots/NNN-name.png, client.log) because that IS the TORIRS_CONTENT_TEST
session directory -- see src/plugin/torirs_plugin_drive.c's ledger writer
and torirs_plugin_drive_ui.c's shot writer, both of which resolve paths
under it directly.

This script's own exit code answers one question only -- did every
requested quest's client process actually run to completion with a ledger
behind it? -- and says nothing about whether a quest PASSED. That verdict
belongs to gate.py alone (docs/QUEST_DRIVER_DESIGN.md); test-quests runs
both in sequence.

Usage:
  tools/quest_gate/run.py --all [--jobs N] [--timeout SECONDS]
  tools/quest_gate/run.py <quest> [--timeout SECONDS] [--rebuild-scripts]
      (the script pack -- `make -C src torirsserver-scripts`: the content
      contract checks, then sscompile -- is rebuilt only when a stat
      fingerprint of what it is built from changed, else one line
      "scripts: pack current (fingerprint XXXXXXXX)"; --rebuild-scripts
      forces the rebuild (and so the contract checks),
      TORIRS_QUEST_ALWAYS_BUILD=1 rebuilds on every run as before; a server
      that still refuses the pack as STALE gets one forced rebuild and a
      relaunch -- tools/quest_gate/pack_fingerprint.py)
  tools/quest_gate/run.py <quest> --from-leg K | --only-leg K
      (a legs file only: resume from checkpoint K-1 under build/quest_gate/<quest>.leg<K>/;
      exit 2 when it is missing or stale; never published, never graded --
      docs/quest_authoring/relay.md "Checkpoints")
  TORIRS_QUEST_NO_PUBLISH=1 tools/quest_gate/run.py <quest> ...
      (as --no-publish on every run.py the environment reaches -- a seam
      pass or any private-binary run must never replace the evidence
      published under OSRS-Content selftest/quests/<quest_dir>/play/)
  tools/quest_gate/run.py <quest> --no-build --no-publish --detach   (then:)
  tools/quest_gate/run.py --wait <quest> [--timeout 540]
      (a run longer than a 10-minute shell call: --detach starts it in the
      background and returns; --wait blocks up to --timeout seconds and exits
      with the run's own exit (0/1) / 2 no such run / 3 still running;
      docs/quest_authoring/relay.md "Runs longer than the shell cap")
  tools/quest_gate/run.py <quest> --render-every-frame
      (every client starts with TORIRS_RENDER_SKIP=1 -- a frame is drawn only
      for a screenshot, a click or a pickset read; this flag draws every frame,
      the A/B -- docs/quest_authoring/running.md "Render skip")
  tools/quest_gate/run.py --script test/quests/_conformance.lua --name proof
      (advanced: drives an arbitrary standalone driver script -- no quest
      table; a non-empty `setup` list is run by the same wrapper loop, an
      empty or absent one is not wrapped) -- through this same build/env/launch path.
      This is how the verb-conformance harness (test/quests/_conformance.lua)
      can be run THROUGH this runner as a cross-check against `make
      test-quest-conformance`'s own answer, rather than only ever through
      conformance.py's separate attempt loop.)
"""

# This directory holds queue.py (the QUEUE.tsv tool). Python puts a script's
# own directory FIRST on sys.path, so from here `import queue` -- which the
# standard library's concurrent.futures does -- found ours, and run.py
# --jobs N died on ThreadPoolExecutor ("module 'queue' has no attribute
# 'SimpleQueue'", 2026-09-20; the full-suite re-run never ran a quest). The
# scrub below must run before ANY other import; the directory goes back on
# the END of the path so quest_list/build_support/ledger still resolve.
import os as _os
import sys as _sys
_HERE = _os.path.dirname(_os.path.abspath(__file__))
_sys.path[:] = [_p for _p in _sys.path if _os.path.abspath(_p or ".") != _HERE]
_sys.path.append(_HERE)

import argparse
import atexit
import hashlib
import os
import re
import shutil
import signal
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))

import build_support  # noqa: E402
import ledger  # noqa: E402
import pack_fingerprint  # noqa: E402
import lint_quest  # noqa: E402  (legs_layout: where a relay file's legs are)
import quest_list  # noqa: E402

# tools/quest_gate/queue.py, NOT `import queue` -- that name is the stdlib
# module ThreadPoolExecutor needs (see the sys.path scrub above), so this
# file's own queue.py is loaded by path under a name that cannot collide
# with it, rather than by a plain `import queue` that would shadow it right
# back.
import importlib.util as _importlib_util

_queue_tsv_spec = _importlib_util.spec_from_file_location(
    "quest_gate_queue_tsv", os.path.join(HERE, "queue.py"))
quest_queue_tsv = _importlib_util.module_from_spec(_queue_tsv_spec)
_queue_tsv_spec.loader.exec_module(quest_queue_tsv)

OBJ_BASE = "build_questtest"
TARGET = "torirs_questtest"
DEFAULT_MAX_FRAMES = str(quest_list.DEFAULT_MAX_FRAMES)
# 400, not 180: a 100-shot quest run takes 60-120 s of wall time on this
# machine, and romeojuliet's legitimate imp-fight retry loop was cut off at 180
# by a reviewer running the default. A hang still fails in under seven
# minutes; the virtual clock (TORIRS_MAX_FRAMES) is the tighter bound.
# Measured 2026-09-30 over the 81 committed tests' last runs (seam32,
# build/seam_state/seam32/p95_wall.json, many run in parallel): p50 40 s, p90
# 116 s, p95 149 s; legends 572 s under its own max_frames scaling.
DEFAULT_TIMEOUT = 400
# seam32 stall_detector_heartbeat. The ceiling above is a BACKSTOP; the kill
# that matters is the stall detector in launch_client. The driver rewrites
# <session>/heartbeat every 25 server ticks from its per-frame pump
# (src/plugin/torirs_plugin_drive.c drive_heartbeat); once the file exists a
# heartbeat older than TORIRS_QUEST_STALL_SECONDS (default 90) kills the
# process group, and before it exists the client gets STALL_BOOT_GRACE_DEFAULT
# seconds (TORIRS_QUEST_STALL_BOOT_SECONDS) to boot, log in and start the
# script -- boot is ~12 s, a cold pack load longer. A long await keeps ticking
# and keeps beating; only a frozen frame loop or a clock that stopped does not.
# TORIRS_QUEST_STALL_SECONDS=0 turns the detector off. A binary built before
# the heartbeat (no HEARTBEAT_MARKER in it) is never judged by it.
STALL_SECONDS_DEFAULT = 90
STALL_BOOT_GRACE_DEFAULT = 120
STALL_POLL_SECONDS = 5
HEARTBEAT_FILE = "heartbeat"
HEARTBEAT_MARKER = b"quest-driver: heartbeat"
# A quest whose real guide does not fit the default virtual clock declares
# its own budget as a `max_frames = <n>,` field beside `fixture = "..."` in
# its quest table (Sheep Herder's four-sheep herd plus the feed/incinerate
# leg overran 60000, 2026-09-24). Read by regex like the fixture, applied to
# TORIRS_MAX_FRAMES for that run only; the wall-clock --timeout is scaled by
# the same ratio. The ceiling is a hard stop: lint_quest.py rejects a larger
# declaration and read_max_frames asserts on one.
MAX_FRAMES_CEILING = quest_list.MAX_FRAMES_CEILING
DEFAULT_FIXTURE = "fresh_lumbridge.ini"

# Where a PASSING quest's evidence is kept. build/quest_gate/<quest>/ is
# deleted on every run, so a screenshot there lives exactly until the next
# run; this directory is inside the OSRS-Content submodule, under
# selftest/quests/<quest_dir>/play/, BESIDE selftest/quests/<quest_dir>/scenes/
# -- the per-quest Gate D BMP sets the content audits already commit
# (server/scripts/selftest/quests/quest_cook/scenes/01_talk_cook.bmp, ...) --
# so the shots a quest test took are versioned with the content they
# photograph, and everything about one quest's evidence sits under one
# directory (2026-09-23, selftest/quests/README.md).
PUBLISH_DIR = os.path.join(REPO_ROOT, "OSRS-Content", "osrs239-content", "server", "scripts",
                           "selftest", "quests")
# tools/raid_gate/run.py moves it (TORIRS_QUEST_PUBLISH_DIR, quest_list.py):
# a raid room's evidence is kept under selftest/minigames/<raid>/<room>/play/.
PUBLISH_DIR_OVERRIDDEN = quest_list.publish_dir_override(REPO_ROOT) is not None
if PUBLISH_DIR_OVERRIDDEN:
    PUBLISH_DIR = quest_list.publish_dir_override(REPO_ROOT)

QUEUE_TSV_PATH = os.path.join(REPO_ROOT, "test", "quests", "QUEUE.tsv")


def quest_dir_for(test_id):
    """test_id -> QUEUE.tsv's quest_dir column for that row, or
    `quest_<test_id>` when the id has no row at all (hans, which predates
    QUEUE.tsv and is not in it) -- the same fallback
    docs/quests/selftest layout uses."""
    assert test_id
    if PUBLISH_DIR_OVERRIDDEN:
        # Not a quest (a raid room): QUEUE.tsv is the quest loop's and has no
        # row for it, by the owner's rule never will.
        return quest_list.suite_publish_subdir(test_id)
    rows = quest_queue_tsv.load_rows(QUEUE_TSV_PATH)
    row = quest_queue_tsv.find_row(rows, test_id)
    if row and row.get("quest_dir"):
        return row["quest_dir"]
    return "quest_%s" % test_id


def play_dir_for(test_id):
    """The folder under PUBLISH_DIR/<quest_dir>/ that holds this test's
    evidence: `play`, or `play-<test_id>` for a second test of the same
    quest (misc_astrid beside misc on quest_misc -- b59's sampler found
    misc's 410 shots replaced by misc_astrid's 422 because both published
    to one `play/`). The test whose id the quest_dir is named for keeps
    `play`."""
    quest_dir = quest_dir_for(test_id)
    if quest_dir == "quest_%s" % test_id:
        return "play"
    rows = quest_queue_tsv.load_rows(QUEUE_TSV_PATH)
    shared = [r["test_id"] for r in rows if r.get("quest_dir") == quest_dir and r["test_id"] != test_id]
    return "play-%s" % test_id if shared else "play"

FIXTURE_RE = re.compile(r'fixture\s*=\s*"([^"]+)"')
MAX_FRAMES_RE = quest_list.MAX_FRAMES_RE
NAME_LINE_RE = re.compile(r"(?m)^name\s*=.*$")


def run(command, **kwargs):
    print("+ " + " ".join(command), flush=True)
    return subprocess.call(command, **kwargs)


def build(warm_from):
    return build_support.build(REPO_ROOT, OBJ_BASE, TARGET, warm_from=warm_from, run=run)


def ensure_scripts(force=False):
    """The embedded server refuses to boot on a stale script pack, and this
    tree routinely carries uncommitted OSRS-Content edits. It used to be
    rebuilt unconditionally, every run (46 of cooks_assistant's 58 s); now
    it is rebuilt when a stat fingerprint of its inputs changed, under a
    lock shared with every other runner (pack_fingerprint.py; conformance.py
    does the same). `force` is --rebuild-scripts."""
    return pack_fingerprint.ensure_pack(run, label="scripts", force=force)


def launch_with_stale_retry(prepare_and_launch):
    """Run `prepare_and_launch()` (-> the launch_and_report dict) and, if the
    embedded server refused the pack as STALE -- a fingerprint that said
    "current" and was wrong -- forget the fingerprint, rebuild, and run it
    once more. The server's refusal stays the last word; this only keeps a
    false "current" from stranding the run."""
    result = prepare_and_launch()
    if not pack_fingerprint.stale_pack_refused(os.path.join(result["directory"], "client.log")):
        return result
    code = pack_fingerprint.rebuild_after_refusal(run)
    if code != 0:
        print("run.py: the script pack did not rebuild after the STALE refusal", file=sys.stderr,
              flush=True)
        return result
    return prepare_and_launch()


def write_manifest():
    """See tools/quest_gate/conformance.py's manifest() -- same rewrite,
    same reason the output has to live under manifests/ and not a session
    dir (measured A/B there: 50/78 from manifests/, 46/78 from a session
    dir)."""
    source = os.path.join(REPO_ROOT, "manifests", "manifest_osrs239.ini")
    out = os.path.join(REPO_ROOT, "manifests", ".questtest.ini")
    lines = []
    with open(source, "r", encoding="utf-8") as handle:
        for line in handle:
            if line.startswith("dir="):
                line = "dir=%s\n" % os.path.join(REPO_ROOT, "cache.osrs239")
            elif line.startswith("transport="):
                line = "transport=embed\n"
            lines.append(line)
    with open(out, "w", encoding="utf-8") as handle:
        handle.writelines(lines)
    return out


def read_fixture_name(quest_file):
    """The `fixture = "<name>.ini"` field a quest file declares, read by
    regex rather than a Lua interpreter -- consistent with how
    tools/quest_gate/verb_list.py already reads the driver's own Lua
    sources, rather than this tooling embedding a second Lua runtime."""
    with open(quest_file, "r", encoding="utf-8") as handle:
        text = handle.read()
    match = FIXTURE_RE.search(text)
    assert match, "%s: no fixture = \"...\" field found" % quest_file
    return match.group(1)


def read_max_frames(script_file):
    """The `max_frames = <n>,` field a quest file (or a --script file) may
    declare, as an int; DEFAULT_MAX_FRAMES when it declares none. A value
    above MAX_FRAMES_CEILING is a contract violation, not a clamp -- a
    silently-clamped budget would fail the run somewhere unrelated."""
    with open(script_file, "r", encoding="utf-8") as handle:
        text = handle.read()
    matches = MAX_FRAMES_RE.findall(text)
    assert len(matches) <= 1, "%s: more than one max_frames field" % script_file
    if not matches:
        return int(DEFAULT_MAX_FRAMES)
    frames = int(matches[0])
    assert frames > 0, "%s: max_frames must be positive" % script_file
    assert frames <= MAX_FRAMES_CEILING, "%s: max_frames %d exceeds the ceiling %d" % (
        script_file, frames, MAX_FRAMES_CEILING)
    return frames


def scaled_timeout(timeout, max_frames):
    """The wall-clock ceiling scaled by the same ratio as the frame budget,
    never below the --timeout the caller asked for."""
    default_frames = int(DEFAULT_MAX_FRAMES)
    if max_frames <= default_frames:
        return timeout
    return (timeout * max_frames + default_frames - 1) // default_frames


def write_session_fixture(fixture_name, saves_dir, user):
    """Copies the fixture into <saves_dir>/<user>.ini -- the trap that reads
    as a broken verb if skipped: without it the server makes a fresh
    character and the run boots into the Character Creator modal, which
    blocks tab selection."""
    fixture_path = os.path.join(quest_list.fixtures_dir(REPO_ROOT), fixture_name)
    assert os.path.isfile(fixture_path), fixture_path
    with open(fixture_path, "r", encoding="utf-8") as handle:
        text = handle.read()
    text, count = NAME_LINE_RE.subn("name = %s" % user, text, count=1)
    assert count == 1, "%s: no `name = ...` line to rewrite" % fixture_path
    os.makedirs(saves_dir, exist_ok=True)
    with open(os.path.join(saves_dir, "%s.ini" % user), "w", encoding="utf-8") as handle:
        handle.write(text)


# ------------------------------------------------------------------ legs
#
# A relay-authored quest file (docs/quest_authoring/relay.md "Checkpoints")
# declares `legs = { { name = "<leg>", run = function(t) ... end }, ... }`
# in place of `run`, and may declare a top-level `bind = {...}` (the
# t.quest.bind table). The harness itself is the driver's QD.core_legs_drive
# (script/plugins/quest_driver/core.lua: the leg.<k>.<name> rows, the
# `::checkpoint k` after an all-PASS leg, the finish); this shim only turns
# the legs table into the one QUEST.run the setup loop below and the
# bootstrap already know how to call.
#
# LEG_FROM / LEG_ONLY / LEG_TILE are set by run.py for a checkpoint run
# (--from-leg K / --only-leg K); a full run has LEG_FROM = nil.
LEGS_HARNESS_LUA = r"""
if type(QUEST) == "table" and QUEST.legs ~= nil then
    local legs_quest = QUEST
    QUEST = { setup = legs_quest.setup, run = function(t)
        return t.core_legs_drive(legs_quest, { from = LEG_FROM, only = LEG_ONLY, tile = LEG_TILE })
    end }
end
"""


def write_wrapper_script(quest_file, out_path, pass_through_without_setup=False,
                         leg_mode=None, party=None):
    """A copy of the quest file wrapped so its `setup` cheats run before
    `run(t)` does, matching test/quests/README.md's
    `{ id, fixture, setup = {cheats}, run = function(t) ... end }` shape.

    The TORIRS_QUEST_SCRIPT file is NOT resumed with `t` as a vararg -- it is
    called with ZERO arguments and must itself return a `{ run = ... }`
    table (src/plugin/torirs_plugin_lua.c's PluginLua_ThreadCreate: the fixed
    BOOTSTRAP chunk does `local quest = loader(); return quest.run(arg)`,
    where `loader` is exactly this file compiled, called with no arguments).
    This was verified against a live run, not assumed from the doc comment:
    an earlier version of this wrapper opened with `local t = ...` and
    failed every quest with "attempt to index a nil value (local 't')",
    because the bootstrap never passes anything to the loaded chunk itself --
    only to the TABLE's own `run` field, after calling it.

    So this wrapper loads the quest's own `return {...}` with zero arguments
    (an immediately-invoked function literal, so the source can keep its
    unmodified top-level `return`), then replaces QUEST.run with a closure
    that runs `setup` first and defers to the original. The quest file's
    source is embedded verbatim rather than parsed in Python: a cheat list
    is exactly the kind of Lua data this project's own tooling refuses to
    hand-parse (see verb_list.py's header), and letting the quest's own
    table construct itself as real Lua is the only way `setup` is read
    correctly no matter how it is written.

    A setup cheat that does not answer `ok` ENDS THE RUN with a FAIL row
    named `setup.<cheat text>`, and that is the whole reason this loop is
    worth generating rather than leaving to each quest. The failure it
    catches is silent by construction: `t.cheat` used to reach only content
    debugprocs, so `setup = { "::give egg" }` answered `no_row`, put nothing
    in the backpack, and the quest went on to fail four steps later at a
    dialogue option that never appeared -- with the ledger blaming the
    dialogue. `no_row` (nothing in the server understood the line) and
    `refused` (a debugproc or a ladder branch understood it and said no) are
    both this failure; only `ok` is a setup that happened.

    The loop is preceded by one tick and a settle, for the sibling failure
    that `ok` cannot catch either: a cheat that ran against a world the
    server had not finished building. The fixture's starting backpack is
    granted on the tick AFTER the script's first frame, so `::clearinv` as a
    setup line -- which every generated file now opens with -- answered
    "Cleared 0 item(s)." and left the fourteen tutorial slots to land behind
    it (build/quest_gate/closer_setup2, row 1, 2026-09-19). That is a setup
    that reported ok and did nothing, which is exactly what this wrapper
    exists to make impossible.

    And the loop does not believe `ok` about a `::give` either: it counts the
    item client-side before the cheat, waits for that count to rise, and ends
    on the next tick boundary. A cheat's reply outruns its effect by one
    server tick (the ladder `say()`s from inside the branch; the backpack
    reaches the client in the container listener's end-of-tick UPDATE_INV), so
    run()'s first row used to read a backpack the setup had not reached --
    trap 23, "::give from setup lands nothing". The generated Lua's own banner
    carries the measurement.

    A ::setlevel is not believed either: the loop waits for the CLIENT's
    reading of that stat to say `stated` with base_level == the level asked
    for. The engine's setlevel branch (src/torirsserver/torirs_server_world.c,
    `strncmp(text, "setlevel", 8)`) returns RAN -- `ok` -- having done nothing
    for a stat name the stat pack does not know or a level outside 1..99, so
    `ok` alone could hand run() a character at level 1 in a quest that
    assumed 60 (seam setlevel_in_setup_noop, seam pass 19).

    `pass_through_without_setup` is the --script mode (run_script_direct):
    a scratch file whose table declares no setup, or `setup = {}` (the
    conformance and cheat harnesses), runs exactly as it did before --
    straight into its own run(), no login-grant wait -- while one that DOES
    declare a setup list gets it run by this same loop. Before seam pass 19
    --script ignored the list outright, so every setup line of a scratch
    copy was a silent no-op: parity2a measured `::setlevel` in setup "not
    landing" three times (build/quest_gate/diag_setlevel*, diag_setup3 --
    hitpoints still `stated=false level=10`, the client had not even been
    sent stats) when no setup line had been issued at all.
    """
    # KEEP IN STEP: the Scripts tab runs a test without this file, through
    # QD.core_run_test (script/plugins/quest_driver/core.lua), whose
    # core_run_test_wrapped is a copy of the QUEST.run below (raid seam24). A
    # change to the setup loop here is the same change there.
    with open(quest_file, "r", encoding="utf-8") as handle:
        source = handle.read()
    if leg_mode:
        leg_prelude = "local LEG_FROM = %d\nlocal LEG_ONLY = %s\nlocal LEG_TILE = %s\n" % (
            leg_mode["from"], "true" if leg_mode["only"] else "false",
            ("{ %d, %d, %d }" % tuple(leg_mode["tile"])) if leg_mode.get("tile") else "nil")
    else:
        leg_prelude = "local LEG_FROM = nil\nlocal LEG_ONLY = false\nlocal LEG_TILE = nil\n"
    if party:
        # A party run (run_party): who this client is, read by t.party
        # (script/plugins/quest_driver/raid.lua). A global, not a local: the
        # driver chunk reads it. A solo run writes nothing here.
        leg_prelude = ("QD_PARTY = { role = %d, size = %d, names = { %s } }\n" % (
            party["role"], len(party["names"]),
            ", ".join('"%s"' % n for n in party["names"]))) + leg_prelude
    wrapper = (
        leg_prelude +
        "local QUEST = (function()\n"
        + source +
        "\nend)()\n"
        + LEGS_HARNESS_LUA +
        "if type(QUEST) ~= \"table\" or type(QUEST.run) ~= \"function\" then\n"
        "    error(\"quest file did not return { run = function(t) ... end }\"\n"
        "        .. \" or { legs = { { name =, run = function(t) ... end }, ... } }\")\n"
        "end\n"
        "local quest_setup = QUEST.setup\n"
        "-- A setup that is not a list of cheat lines is a setup nobody reads:\n"
        "-- refuse it rather than run the quest against an unstated world.\n"
        "if quest_setup ~= nil and type(quest_setup) ~= \"table\" then\n"
        "    error(\"quest file's setup is a \" .. type(quest_setup)\n"
        "        .. \", not a table of cheat lines\")\n"
        "end\n"
        "-- A checkpoint run resumes a player who already went through setup\n"
        "-- (and legs 1..K-1): the setup list is NOT run again.\n"
        "if LEG_FROM and LEG_FROM > 1 then\n"
        "    quest_setup = nil\n"
        "end\n"
        + ("-- --script mode: no setup list, nothing to wrap.\n"
           "if quest_setup == nil or next(quest_setup) == nil then\n"
           "    return QUEST\n"
           "end\n" if pass_through_without_setup else "") +
        "local quest_run = QUEST.run\n"
        "QUEST.run = function(t)\n"
        "    -- WAIT FOR THE LOGIN GRANT before the first setup cheat.\n"
        "    --\n"
        "    -- The backpack a fixture starts with is not in the fixture: the\n"
        "    -- containers are adopted EMPTY (torirs_server_world.c, \"what goes\n"
        "    -- in them the first time a character connects is content's\") and\n"
        "    -- content's [login,_] -> [proc,newplayer_inv] fills them on a\n"
        "    -- later tick than the one this script starts on.  Measured\n"
        "    -- 2026-09-19 (build/quest_gate/closer_setup2 row 1,\n"
        "    -- closer_setup3): `::clearinv` issued on the first frame answers\n"
        "    -- \"Cleared 0 item(s).\", the fourteen tutorial slots land behind\n"
        "    -- it, and a `::give` before the grant is overwritten by it -- a\n"
        "    -- setup list that answered `ok` three times and left the world it\n"
        "    -- promised in none of them.  A fixed `t.ticks(n)` does not fix it\n"
        "    -- (the grant landed on tick 1 in one run and after three cheats in\n"
        "    -- the next); the arrival itself is the signal, so wait for it.\n"
        "    --\n"
        "    -- Bounded, and proceeding either way: a fixture whose character\n"
        "    -- really does hold nothing pays the budget and runs its setup\n"
        "    -- anyway, rather than failing a quest over an empty backpack.\n"
        "    t.await({ level = function()\n"
        "        for slot = 0, 27 do\n"
        "            local slot_result, cell = t.inv.slot(slot)\n"
        "            if slot_result == \"ok\" and cell.name ~= \"\" and cell.count ~= 0 then\n"
        "                return true\n"
        "            end\n"
        "        end\n"
        "        return false\n"
        "    end, note = \"setup: the login grant\" }, 10)\n"
        "    t.settle()\n"
        "    -- A CHEAT'S REPLY OUTRUNS ITS EFFECT by exactly one server tick.\n"
        "    --\n"
        "    -- t.cheat waits for the reply line (core.lua: QD.cheat ->\n"
        "    -- msg.await), and the server SAYS `Gave 3 x Egg (1944).` from\n"
        "    -- inside the ladder branch itself, while the backpack the same\n"
        "    -- branch just wrote reaches the client in the container\n"
        "    -- listener's UPDATE_INV at the END of that tick\n"
        "    -- (torirs_server_world.c, the ::give branch: `the backpack's\n"
        "    -- listener sends an UPDATE_INV without this branch knowing a\n"
        "    -- packet exists`).  So the setup loop used to hand run() a client\n"
        "    -- that had heard about the items and not yet received them, and\n"
        "    -- every quest whose first rows read t.inv.count saw an empty\n"
        "    -- backpack from a setup that had answered `ok` three times.\n"
        "    -- Measured 2026-09-21 (build/quest_gate/lag_probe): row `lag.t0`\n"
        "    -- FAIL `egg: absent`, row `lag.t1` -- one t.ticks(1) later --\n"
        "    -- PASS `3`.  Two seam fixers reported it as `::give from setup\n"
        "    -- lands nothing` (docs/QUEST_AUTHORING.md trap 23).\n"
        "    --\n"
        "    -- A ::give is therefore not done when it answers: it is done when\n"
        "    -- the CLIENT can count the items, which is also the only check\n"
        "    -- that catches the ladder's own silent misses -- `::give\n"
        "    -- nosuchitemname` and an ambiguous name both `say()` their\n"
        "    -- complaint and return RAN, i.e. `ok`, having given nothing\n"
        "    -- (same file, `No item named '%s'.` / `Which %s? %s`).  Each one\n"
        "    -- is now a FAIL row named after the cheat, exactly like a cheat\n"
        "    -- that answered no_row.\n"
        "    local function setup_give(text)\n"
        "        local name, count = string.match(text, \"^%s*:*give%s+([%w_]+)%s*(%d*)\")\n"
        "        if not name then\n"
        "            return nil\n"
        "        end\n"
        "        local wanted = tonumber(count)\n"
        "        if not wanted or wanted < 1 then\n"
        "            wanted = 1\n"
        "        end\n"
        "        return name, wanted\n"
        "    end\n"
        "    -- Name-free fingerprint of the backpack: every slot's count, plus\n"
        "    -- one per occupied slot so an item ARRIVING in an empty slot\n"
        "    -- moves it too.  What the fallback below watches when a ::give\n"
        "    -- names something this client cannot count.\n"
        "    local function setup_backpack_mark()\n"
        "        local mark = 0\n"
        "        for slot = 0, 27 do\n"
        "            local slot_result, cell = t.inv.slot(slot)\n"
        "            if slot_result == \"ok\" and cell.name ~= \"\" and cell.count ~= 0 then\n"
        "                mark = mark + cell.count + 1\n"
        "            end\n"
        "        end\n"
        "        return mark\n"
        "    end\n"
        "    local function setup_backpack_empty()\n"
        "        for slot = 0, 27 do\n"
        "            local slot_result, cell = t.inv.slot(slot)\n"
        "            if slot_result == \"ok\" and cell.name ~= \"\" and cell.count ~= 0 then\n"
        "                return false\n"
        "            end\n"
        "        end\n"
        "        return true\n"
        "    end\n"
        "    -- `::setlevel <stat> <level>` -> stat, level; nil for any other line.\n"
        "    -- A line that STARTS as a setlevel (the engine's own strncmp prefix)\n"
        "    -- but does not parse is (\"\", nil): the engine answers ok to it\n"
        "    -- and sets nothing.\n"
        "    local function setup_setlevel(text)\n"
        "        if not string.match(text, \"^%s*:*setlevel\") then\n"
        "            return nil\n"
        "        end\n"
        "        local stat, level = string.match(text, \"^%s*:*setlevel%s+(%a[%w_]*)%s+(%d+)%s*$\")\n"
        "        if not stat then\n"
        "            return \"\", nil\n"
        "        end\n"
        "        return stat, tonumber(level)\n"
        "    end\n"
        "    -- `::wield <item>` -> item; nil for any other line.  The cheat\n"
        "    -- answers once the synthesized Wield click is SENT (seam pass 29:\n"
        "    -- it used to take only an obj id, print its Usage line for a name\n"
        "    -- and answer ok -- arthur fought Mordred unarmed behind a green\n"
        "    -- setup), and content may still refuse the wield (a level, a\n"
        "    -- quest), so the line is done only when the WORN container holds\n"
        "    -- the item (QD.ui._worn_count, ui.lua).\n"
        "    local function setup_wield(text)\n"
        "        return string.match(text, \"^%s*:*wield%s+([%w_]+)%s*$\")\n"
        "    end\n"
        "    local function setup_last_lines(n)\n"
        "        local lines_result, rows = t.msg.last(n)\n"
        "        if lines_result ~= \"ok\" or type(rows) ~= \"table\" then\n"
        "            return \"(no chat lines)\"\n"
        "        end\n"
        "        local texts = {}\n"
        "        for i = 1, #rows do\n"
        "            texts[#texts + 1] = \"'\" .. tostring(rows[i].text) .. \"'\"\n"
        "        end\n"
        "        return table.concat(texts, \" / \")\n"
        "    end\n"
        "    local function setup_failed(cheat, why)\n"
        "        t.step(\"setup.\" .. cheat, \"FAIL\",\n"
        "            why .. \" -- the world this quest assumes was never stated\")\n"
        "        t.finish(1)\n"
        "    end\n"
        "    if type(quest_setup) == \"table\" then\n"
        "        for _, cheat in ipairs(quest_setup) do\n"
        "            -- Counted BEFORE the cheat: a setup list that gives the\n"
        "            -- same item twice, or gives one the login grant already\n"
        "            -- put there, is waiting for an INCREASE, not for a total.\n"
        "            local give_name, give_count = setup_give(cheat)\n"
        "            local before = nil\n"
        "            local before_mark = nil\n"
        "            if give_name then\n"
        "                local count_result, count_total = t.inv.count(give_name)\n"
        "                if count_result == \"ok\" then\n"
        "                    before = count_total\n"
        "                else\n"
        "                    -- A name this client cannot resolve to an obj: the\n"
        "                    -- server's own cheat_obj_from_name is fuzzy and may\n"
        "                    -- still have matched one -- but `::give\n"
        "                    -- nosuchitemname` reaches the SAME `ok` (it say()s\n"
        "                    -- `No item named` and returns RAN), so the whole\n"
        "                    -- backpack is watched for any movement instead of\n"
        "                    -- taking the cheat at its word.\n"
        "                    before_mark = setup_backpack_mark()\n"
        "                end\n"
        "            end\n"
        "            local wield_name = setup_wield(cheat)\n"
        "            local worn_before = nil\n"
        "            if wield_name then\n"
        "                local worn_result, worn_count = t.ui._worn_count(wield_name)\n"
        "                if worn_result ~= \"ok\" then\n"
        "                    setup_failed(cheat, \"cannot read the worn container for \"\n"
        "                        .. wield_name .. \" (\" .. tostring(worn_result) .. \" \"\n"
        "                        .. tostring(worn_count) .. \"), so a wield could never be\"\n"
        "                        .. \" proved\")\n"
        "                    return\n"
        "                end\n"
        "                worn_before = worn_count\n"
        "            elseif string.match(cheat, \"^%s*:*wield\") then\n"
        "                setup_failed(cheat, \"not `::wield <item_name>`: a line the\"\n"
        "                    .. \" read-back cannot name is a wield nobody can prove\")\n"
        "                return\n"
        "            end\n"
        "            local level_stat, level_wanted = setup_setlevel(cheat)\n"
        "            if level_stat == \"\" then\n"
        "                setup_failed(cheat, \"not `::setlevel <stat name> <level>`: the\"\n"
        "                    .. \" engine answers ok to it and sets nothing, and a numeric\"\n"
        "                    .. \" stat id cannot be read back to prove it landed\")\n"
        "                return\n"
        "            end\n"
        "            local setup_result, setup_detail = t.cheat(cheat)\n"
        "            if setup_result ~= \"ok\" then\n"
        "                setup_failed(cheat, \"setup cheat answered \"\n"
        "                    .. tostring(setup_result)\n"
        "                    .. \" (\" .. tostring(setup_detail) .. \"); last lines: \"\n"
        "                    .. setup_last_lines(2))\n"
        "                return\n"
        "            end\n"
        "            if before ~= nil then\n"
        "                local landed = t.inv.await(give_name, before + give_count, 10)\n"
        "                if landed ~= \"ok\" then\n"
        "                    local after_result, after_total = t.inv.count(give_name)\n"
        "                    -- Short of the count asked for is the BACKPACK's\n"
        "                    -- limit speaking (`Gave 21 x Egg (1944), 7 did not\n"
        "                    -- fit.`), not a setup that did not happen; nothing\n"
        "                    -- arriving at all is.\n"
        "                    if after_result ~= \"ok\" or after_total <= before then\n"
        "                        setup_failed(cheat, \"the cheat answered ok and no \"\n"
        "                            .. give_name\n"
        "                            .. \" reached the backpack within 10 ticks (held \"\n"
        "                            .. tostring(before) .. \" before, \"\n"
        "                            .. tostring(after_total) .. \" after)\")\n"
        "                        return\n"
        "                    end\n"
        "                end\n"
        "            elseif before_mark ~= nil then\n"
        "                local moved = t.await({ level = function()\n"
        "                    return setup_backpack_mark() ~= before_mark\n"
        "                end, note = \"setup: ::give reaching the backpack\" }, 10)\n"
        "                if moved ~= \"ok\" then\n"
        "                    setup_failed(cheat, \"the cheat answered ok and the backpack\"\n"
        "                        .. \" did not change within 10 ticks (\" .. give_name\n"
        "                        .. \" is not an obj this client can count, so every\"\n"
        "                        .. \" slot was watched instead)\")\n"
        "                    return\n"
        "                end\n"
        "            elseif worn_before ~= nil then\n"
        "                local worn_last = worn_before\n"
        "                local worn_landed = t.await({ level = function()\n"
        "                    local read_result, reading = t.ui._worn_count(wield_name)\n"
        "                    if read_result == \"ok\" then\n"
        "                        worn_last = reading\n"
        "                    end\n"
        "                    return read_result == \"ok\" and reading > worn_before\n"
        "                end, note = \"setup: ::wield reaching the worn container\" }, 10)\n"
        "                if worn_landed ~= \"ok\" then\n"
        "                    setup_failed(cheat, \"the cheat answered ok and \" .. wield_name\n"
        "                        .. \" is not worn 10 ticks later (worn \" .. tostring(worn_before)\n"
        "                        .. \" before, \" .. tostring(worn_last) .. \" after); last lines: \"\n"
        "                        .. setup_last_lines(3))\n"
        "                    return\n"
        "                end\n"
        "            elseif level_stat ~= nil then\n"
        "                -- ok is not a level: wait for the client's reading.\n"
        "                local level_last = \"unread\"\n"
        "                local level_landed = t.await({ level = function()\n"
        "                    local read_result, reading = t.skill.read(level_stat)\n"
        "                    if read_result ~= \"ok\" then\n"
        "                        level_last = tostring(read_result) .. \" \" .. tostring(reading)\n"
        "                        return false\n"
        "                    end\n"
        "                    level_last = \"stated=\" .. tostring(reading.stated)\n"
        "                        .. \" base_level=\" .. tostring(reading.base_level)\n"
        "                    return reading.stated and reading.base_level == level_wanted\n"
        "                end, note = \"setup: ::setlevel reaching the client\" }, 10)\n"
        "                if level_landed ~= \"ok\" then\n"
        "                    setup_failed(cheat, \"the cheat answered ok and \" .. level_stat\n"
        "                        .. \" never read base_level \" .. tostring(level_wanted)\n"
        "                        .. \" within 10 ticks (last reading: \" .. level_last .. \")\")\n"
        "                    return\n"
        "                end\n"
        "            elseif string.match(cheat, \"^%s*:*clearinv\") then\n"
        "                -- The same race, and the one cheat every generated\n"
        "                -- setup list opens with: an unfinished ::clearinv also\n"
        "                -- makes the NEXT give's before-count wrong.\n"
        "                local cleared = t.await({ level = setup_backpack_empty,\n"
        "                    note = \"setup: ::clearinv reaching the client\" }, 10)\n"
        "                if cleared ~= \"ok\" then\n"
        "                    setup_failed(cheat, \"the cheat answered ok and the\"\n"
        "                        .. \" backpack still holds items 10 ticks later\")\n"
        "                    return\n"
        "                end\n"
        "            end\n"
        "        end\n"
        "        -- Every OTHER cheat (::setvar, a quest's own reset\n"
        "        -- debugproc) has the same one-tick reply/effect gap with no\n"
        "        -- single reading to wait on, so the loop ends on the tick\n"
        "        -- boundary its last reply outran.  run()'s first row reads a\n"
        "        -- client that has seen the setup, which is what `setup` means.\n"
        "        t.ticks(1)\n"
        "        t.settle()\n"
        "    end\n"
        "    return quest_run(t)\n"
        "end\n"
        "return QUEST\n"
    )
    with open(out_path, "w", encoding="utf-8") as handle:
        handle.write(wrapper)


# ------------------------------------------------------------ checkpoints
#
# docs/quest_authoring/relay.md "Checkpoints". A full run of a legs file asks
# the server for `::checkpoint k` after every all-PASS leg; the server writes
# the player -- its own save serialiser, checkpoint mode: every varp, the
# player's random stream -- to <session>/saves/checkpoints/<k>.ini. After the
# run, collect_checkpoints wraps each in a manifest as
# build/quest_gate/<run>/checkpoints/<k>.ckpt. `--from-leg K` refuses a
# checkpoint whose manifest disagrees with the file, the content pack or the
# binary it would run with (exit 2), and otherwise logs the same player back
# in from it and runs legs K..end under the run name <id>.leg<K>.

CHECKPOINT_HEADER = "[checkpoint]"
# The server script pack the embedded server boots on (ensure_scripts builds
# it; a stale one is refused at boot). A checkpoint is only as good as the
# scripts that wrote its varps.
PACK_FILES = [
    os.path.join(REPO_ROOT, "OSRS-Content", "osrs239-content", "server", "scripts", "build",
                 "script.dat"),
    os.path.join(REPO_ROOT, "OSRS-Content", "osrs239-content", "server", "scripts", "build",
                 "script.idx"),
]
# The manifest keys compared for staleness, and what each one means when it
# differs -- the refusal names the key in these words.
STALE_KEYS = [
    ("legs_hash", "the source text of legs 1..%(leg)s changed"),
    ("setup_hash", "the file's setup list changed"),
    ("bind_hash", "the file's bind field changed"),
    ("fixture", "the file's fixture changed"),
    ("pack_hash", "the content pack (server/scripts/build/script.dat) changed"),
    ("binary_id", "the engine binary changed"),
]
CHECKPOINT_RUN_RE = re.compile(r"^(?P<id>.+)\.leg(?P<leg>\d+)$")
LEG_ROW_CHECKPOINT_RE = re.compile(r"checkpoint (\d+) (written|NOT written): (.*)$")

_file_hash_cache = {}


def file_sha256(path):
    """sha256 of a file's bytes, cached per (path, size, mtime) for the life
    of this process -- the binary is several MB and is hashed per checkpoint."""
    assert path
    stat = os.stat(path)
    key = (os.path.abspath(path), stat.st_size, stat.st_mtime_ns)
    cached = _file_hash_cache.get(key)
    if cached:
        return cached
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    _file_hash_cache[key] = digest.hexdigest()
    return _file_hash_cache[key]


def text_sha256(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def pack_hash():
    digest = hashlib.sha256()
    for path in PACK_FILES:
        assert os.path.isfile(path), "no content pack at %s (make -C src torirsserver-scripts)" % path
        digest.update(os.path.basename(path).encode("utf-8"))
        digest.update(file_sha256(path).encode("utf-8"))
    return digest.hexdigest()


def save_file_stem(display_name):
    """ToriRSServer_SavePath's sanitising, mirrored (torirs_server_save.c):
    alnum lowered, `_`/`-`/space to `_`, everything else dropped."""
    assert display_name
    out = []
    for ch in display_name:
        if ch.isascii() and ch.isalnum():
            out.append(ch.lower())
        elif ch in "_- ":
            out.append("_")
    stem = "".join(out)
    assert stem, "account name %r sanitises to nothing" % display_name
    return stem


def legs_source(quest_file):
    """(text, layout) for a legs file, or (text, None) for a run file."""
    with open(quest_file, "r", encoding="utf-8") as handle:
        text = handle.read()
    return text, lint_quest.legs_layout(text)


def current_manifest(test_id, leg, quest_file, binary):
    """The manifest a checkpoint of leg `leg` written NOW would carry."""
    text, layout = legs_source(quest_file)
    assert layout, "%s has no legs table" % quest_file
    assert 1 <= leg <= len(layout["legs"]), "leg %d of %d" % (leg, len(layout["legs"]))
    leg_texts = [entry["text"] for entry in layout["legs"][:leg]]
    return {
        "test_id": test_id,
        "leg": str(leg),
        "legs": ",".join(str(entry["name"]) for entry in layout["legs"]),
        "legs_hash": text_sha256("\n\0".join(leg_texts)),
        "setup_hash": text_sha256(layout["setup"]),
        "bind_hash": text_sha256(layout["bind"]),
        "fixture": read_fixture_name(quest_file),
        "pack_hash": pack_hash(),
        "binary_id": file_sha256(binary),
    }


def read_checkpoint(path):
    """(manifest dict, save text) from a .ckpt file."""
    manifest = {}
    save_lines = []
    in_manifest = False
    with open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            stripped = line.strip()
            if stripped == CHECKPOINT_HEADER:
                in_manifest = True
                continue
            if in_manifest and stripped.startswith("[") and stripped.endswith("]"):
                in_manifest = False
            if in_manifest:
                if stripped and not stripped.startswith(";"):
                    key, _, value = stripped.partition("=")
                    manifest[key.strip()] = value.strip()
                continue
            save_lines.append(line)
    return manifest, "".join(save_lines)


def save_tile(save_text):
    """(x, z, level) off a save's [player] section, or None."""
    fields = {}
    section = None
    for line in save_text.splitlines():
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            section = stripped[1:-1]
            continue
        if section == "player" and "=" in stripped:
            key, _, value = stripped.partition("=")
            fields[key.strip()] = value.strip()
    try:
        return int(fields["x"]), int(fields["z"]), int(fields["level"])
    except (KeyError, ValueError):
        return None


def collect_checkpoints(result, test_id, quest_file, binary):
    """After a run of a legs file: every <session>/saves/checkpoints/<k>.ini
    the server wrote becomes checkpoints/<k>.ckpt (manifest + save), and every
    leg row that says a checkpoint was NOT written leaves checkpoints/<k>.refused
    carrying the reason, so a later --from-leg can name it. Returns the lines
    it printed."""
    lines = []
    directory = result["directory"]
    _, layout = legs_source(quest_file)
    if not layout:
        return lines
    raw_dir = os.path.join(directory, "saves", "checkpoints")
    out_dir = os.path.join(directory, "checkpoints")
    if os.path.isdir(raw_dir):
        for entry in sorted(os.listdir(raw_dir)):
            match = re.match(r"^(\d+)\.ini$", entry)
            if not match:
                continue
            leg = int(match.group(1))
            if not 1 <= leg <= len(layout["legs"]):
                continue
            with open(os.path.join(raw_dir, entry), "r", encoding="utf-8") as handle:
                save_text = handle.read()
            manifest = current_manifest(test_id, leg, quest_file, binary)
            manifest["source_run"] = result["name"]
            manifest["written"] = time.strftime("%Y-%m-%dT%H:%M:%S")
            tile = save_tile(save_text)
            if tile:
                manifest["tile"] = "%d,%d,%d" % tile
            os.makedirs(out_dir, exist_ok=True)
            target = os.path.join(out_dir, "%d.ckpt" % leg)
            with open(target, "w", encoding="utf-8") as handle:
                handle.write("; quest-harness leg checkpoint (tools/quest_gate/run.py); "
                             "docs/quest_authoring/relay.md \"Checkpoints\"\n")
                handle.write("%s\n" % CHECKPOINT_HEADER)
                for key in sorted(manifest):
                    handle.write("%s = %s\n" % (key, manifest[key]))
                handle.write("\n")
                handle.write(save_text)
            lines.append("run.py: checkpoint %d -> %s" % (leg, os.path.relpath(target, REPO_ROOT)))
    ledger_path = os.path.join(directory, "ledger.tsv")
    rows, _ = ledger.read(ledger_path)
    for row in rows or []:
        if not row["step"].startswith("leg."):
            continue
        match = LEG_ROW_CHECKPOINT_RE.search(row["detail"])
        if not match or match.group(2) != "NOT written":
            continue
        os.makedirs(out_dir, exist_ok=True)
        with open(os.path.join(out_dir, "%s.refused" % match.group(1)), "w",
                  encoding="utf-8") as handle:
            handle.write(match.group(3).strip() + "\n")
        lines.append("run.py: checkpoint %s NOT written -- %s" % (match.group(1), match.group(3)))
    for line in lines:
        print(line, flush=True)
    return lines


def checkpoint_candidates(test_id, leg):
    """Every <k>.ckpt this id's runs left: the full run's and each checkpoint
    run's (<id>.leg<J>), which writes checkpoints for the legs it completes."""
    base = os.path.join(REPO_ROOT, "build", "quest_gate")
    found = []
    names = [test_id]
    if os.path.isdir(base):
        for entry in sorted(os.listdir(base)):
            match = CHECKPOINT_RUN_RE.match(entry)
            if match and match.group("id") == test_id:
                names.append(entry)
    for name in names:
        path = os.path.join(base, name, "checkpoints", "%d.ckpt" % leg)
        if os.path.isfile(path):
            found.append(path)
    return found


def find_checkpoint(test_id, leg, quest_file, binary):
    """(path, manifest, save_text, None) for the newest checkpoint of `leg`
    that is not stale, or (None, None, None, why) naming what was looked at
    and why each was refused."""
    current = current_manifest(test_id, leg, quest_file, binary)
    reasons = []
    fresh = []
    for path in checkpoint_candidates(test_id, leg):
        manifest, save_text = read_checkpoint(path)
        stale = []
        if manifest.get("test_id") != test_id or manifest.get("leg") != str(leg):
            stale.append("it is %s leg %s, not %s leg %d" % (
                manifest.get("test_id"), manifest.get("leg"), test_id, leg))
        for key, meaning in STALE_KEYS:
            if manifest.get(key) != current[key]:
                stale.append("%s (%s)" % (meaning % {"leg": leg}, key))
        rel = os.path.relpath(path, REPO_ROOT)
        if stale:
            reasons.append("STALE %s: %s" % (rel, "; ".join(stale)))
        else:
            fresh.append((os.path.getmtime(path), path, manifest, save_text))
    if fresh:
        fresh.sort()
        _, path, manifest, save_text = fresh[-1]
        return path, manifest, save_text, None
    if not reasons:
        refused = os.path.join(REPO_ROOT, "build", "quest_gate", test_id, "checkpoints",
                               "%d.refused" % leg)
        why = "MISSING: no checkpoint %d under build/quest_gate/%s/checkpoints/ or any %s.leg<J>/" % (
            leg, test_id, test_id)
        if os.path.isfile(refused):
            with open(refused, "r", encoding="utf-8") as handle:
                why += " -- the last full run did not write it: %s" % handle.read().strip()
        else:
            why += " -- run the full test first (run.py %s --no-build --no-publish)" % test_id
        reasons.append(why)
    return None, None, None, "\n    ".join(reasons)


def stamp_summary_from_leg(ledger_path, leg, only):
    """Append from_leg=K (and only_leg=1) to the SUMMARY row's counts column:
    gate.py refuses to grade such a ledger, queue.py refuses green on it,
    and publish() never copies it."""
    if not os.path.isfile(ledger_path):
        return False
    with open(ledger_path, "r", encoding="utf-8") as handle:
        lines = handle.readlines()
    stamped = False
    for index, line in enumerate(lines):
        if not line.startswith("SUMMARY\t"):
            continue
        fields = line.rstrip("\n").split("\t")
        while len(fields) < 6:
            fields.append("")
        fields[5] = (fields[5] + " from_leg=%d" % leg + (" only_leg=1" if only else "")).strip()
        lines[index] = "\t".join(fields) + "\n"
        stamped = True
    with open(ledger_path, "w", encoding="utf-8") as handle:
        handle.writelines(lines)
    return stamped


def summary_from_leg(summary):
    """The from_leg=K token's K off a ledger SUMMARY row, or None."""
    if summary is None or len(summary) < 6:
        return None
    for token in summary[5].split():
        key, _, value = token.partition("=")
        if key == "from_leg":
            return value
    return None


# RENDER SKIP (seam34, owner request 2026-09-30: "When running these clips -
# do so. Make sure screenshot requests still work. Just don't waste time
# rendering every frame."). A quest client is frame-locked and uncapped, so a
# run goes as fast as the software renderer draws; TORIRS_RENDER_SKIP=1 makes
# a frame draw only when something must see it -- a screenshot, a pickset
# read, a pushed click (src/app/app_render.c's render-skip banner lists every
# piece of render-time state and what forces a draw for it). On by default;
# --render-every-frame is the A/B (main() clears this).
RENDER_SKIP = True


def client_env(directory, saves, script, max_frames=int(DEFAULT_MAX_FRAMES), extra_env=None):
    environment = dict(os.environ)
    environment.update({
        "SDL_VIDEODRIVER": "dummy",
        "SDL_AUDIODRIVER": "dummy",
        "TORIRS_STDERR_UNBUFFERED": "1",
        "TORIRS_PLUGINS": "1",
        # Only the Lua host registers, and its manifest below names only the
        # quest driver: no item-stats, xp/loot trackers, minimap orbs, tile
        # indicator or NXT plugins. Run book rule (owner, 2026-09-20): a quest
        # test's screenshots show the engine's own frame and nothing a plugin
        # painted, and no plugin's hooks sit between the driver and the
        # client. TORIRS_PLUGIN_ONLY is torirs_plugin_registry.c's switch;
        # "lua" is TORIRS_PLUGIN_LUA.id. Proved 2026-09-20: doric green, 23
        # shots, zero plugin= lines in client.log.
        "TORIRS_PLUGIN_ONLY": "lua",
        "TORIRS_PLUGIN_LOG": "1",
        "TORIRS_CONTENT_TEST": directory,
        "TORIRS_QUEST_SCRIPT": script,
        # Resolved UNDER script/: "script/plugins/..." double-prefixes and
        # silently loads no driver at all, which reads exactly like a dead
        # one.
        "TORIRS_PLUGIN_MANIFEST": "plugins/quest_driver.ini",
        "TORIRS_PLUGIN_PREFS": os.path.join(directory, "plugin_prefs.ini"),
        "TORIRS_PREFS": "",
        "TORIRSSERVER_SAVES": saves,
        "TORIRSSERVER_STAFF_LEVEL": "2",
        "TORIRSSERVER_HOME": "3222,3218",
        "TORIRS_MAX_FRAMES": str(max_frames),
        "TORIRS_EMBED_CLOCK_MS": "20",
        "TORIRS_RENDER_SKIP": "1" if RENDER_SKIP else "0",
    })
    # A party run's link knobs (run_party): TORIRS_EMBED_PARTY_* per seat.
    if extra_env:
        environment.update(extra_env)
    return environment


# The account every quest client logs in as is the run's own name -- the
# same name as its session directory, build/quest_gate/<name>/ -- and this
# password. script/plugins/quest_driver/session.lua's t.session.login() (the
# relog half of the guide's "or log out" steps) types exactly these two into
# the title screen: the name as the last component of api_drive.session().dir,
# the password as QD.session.PASSWORD. Change one, change the other.
QUEST_PASSWORD = "test"


def env_seconds(name, default):
    """A non-negative whole number of seconds from the environment, or
    `default` when unset. A value that is not one is the caller's typo and
    stops the run rather than silently meaning the default."""
    text = os.environ.get(name, "").strip()
    if not text:
        return default
    assert text.isdigit(), "%s must be a whole number of seconds, got %r" % (name, text)
    return int(text)


_heartbeat_binaries = {}


def binary_writes_heartbeat(binary):
    """True when `binary` carries drive_heartbeat's stderr line, i.e. it was
    built with the heartbeat. Cached per (path, size, mtime): one read of a
    ~6 MB file per binary per run.py process."""
    assert binary
    info = os.stat(binary)
    key = (binary, info.st_size, info.st_mtime)
    if key not in _heartbeat_binaries:
        with open(binary, "rb") as handle:
            _heartbeat_binaries[key] = HEARTBEAT_MARKER in handle.read()
    return _heartbeat_binaries[key]


def read_heartbeat(path):
    """(mtime, tick) of the heartbeat file, or (None, None) when it does not
    exist yet. The tick is None when the file is caught mid-rewrite."""
    assert path
    try:
        mtime = os.stat(path).st_mtime
    except FileNotFoundError:
        return None, None
    tick = None
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            match = re.match(r"tick=(\d+)", handle.read())
        if match:
            tick = int(match.group(1))
    except OSError:
        pass
    return mtime, tick


def kill_process_group(process):
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except OSError:
        pass
    process.wait()


def launch_client(binary, manifest_path, user, directory, saves, script, log_path, timeout,
                  max_frames=int(DEFAULT_MAX_FRAMES), stall_out=None, extra_env=None):
    """One client process, killed (whole process group) when it STALLS --
    its <session>/heartbeat older than TORIRS_QUEST_STALL_SECONDS, or never
    written within the boot grace (seam32; see STALL_SECONDS_DEFAULT) -- or
    when it outlives `timeout` seconds of WALL-CLOCK time, the backstop.
    Both are independent of TORIRS_MAX_FRAMES, which only bounds the virtual
    clock and cannot catch a real hang. Returns (exit_code_or_None,
    timed_out). A stall kill returns (None, False) and, when the caller
    passes a dict as `stall_out`, fills it with {seconds, tick, boot,
    stall_seconds, boot_grace, timeout} (the 2-tuple is kept for
    fingerprints/capture_fingerprints.py)."""
    command = [binary, "--manifest", manifest_path, "--user", user, "--pass", QUEST_PASSWORD,
               "--soft3d", "--window", "765x503"]
    environment = client_env(directory, saves, script, max_frames, extra_env)
    # The npc world seed (src/torirsserver/torirs_server.h npc_run_seed): the
    # account this client logs in as, so each run name is its own npc
    # scenario, deterministic per name. A party's leader passes its own
    # account and the members are handed the leader's (run_party), so every
    # seat agrees. TORIRS_NPC_SEED_LEGACY=1 in the caller's environment brings
    # back the tile-and-life streams (docs/minigames/raid_loop/DRIVER_NOTES.md).
    # Set, never inherited: a name left exported in a shell would make every
    # run one scenario again.
    environment["TORIRSSERVER_RUN_NAME"] = (extra_env or {}).get("TORIRSSERVER_RUN_NAME", user)
    print("+ " + " ".join(command), flush=True)
    if max_frames != int(DEFAULT_MAX_FRAMES):
        print("run.py: %s declares max_frames = %d (wall-clock timeout %d s)"
              % (user, max_frames, timeout), flush=True)
    stall_seconds = env_seconds("TORIRS_QUEST_STALL_SECONDS", STALL_SECONDS_DEFAULT)
    boot_grace = env_seconds("TORIRS_QUEST_STALL_BOOT_SECONDS", STALL_BOOT_GRACE_DEFAULT)
    watch = stall_seconds > 0 and binary_writes_heartbeat(binary)
    if stall_seconds > 0 and not watch:
        print("run.py: %s has no heartbeat (built before seam32) -- stall detector off, "
              "only the %d s wall-clock ceiling applies" % (binary, timeout), flush=True)
    heartbeat_path = os.path.join(directory, HEARTBEAT_FILE)
    # A heartbeat left by an earlier launch into this directory would read as
    # this client's first beat.
    if os.path.exists(heartbeat_path):
        os.unlink(heartbeat_path)
    with open(log_path, "wb") as log:
        # cwd is the repo root, ALWAYS: TORIRS_PLUGIN_MANIFEST resolves under
        # script/ relative to the working directory, not to the binary.
        process = subprocess.Popen(command, env=environment, cwd=REPO_ROOT,
                                    stdout=log, stderr=subprocess.STDOUT,
                                    start_new_session=True)
        started = time.monotonic()
        while True:
            remaining = timeout - (time.monotonic() - started)
            if remaining <= 0:
                kill_process_group(process)
                return None, True
            try:
                code = process.wait(timeout=min(STALL_POLL_SECONDS, remaining))
                return code, False
            except subprocess.TimeoutExpired:
                pass
            if not watch:
                continue
            mtime, tick = read_heartbeat(heartbeat_path)
            stall = None
            if mtime is None:
                elapsed = time.monotonic() - started
                if elapsed > boot_grace:
                    stall = {"seconds": int(elapsed), "tick": None, "boot": True}
            else:
                age = time.time() - mtime
                if age > stall_seconds:
                    stall = {"seconds": int(age), "tick": tick, "boot": False}
            if stall:
                stall["stall_seconds"] = stall_seconds
                stall["boot_grace"] = boot_grace
                stall["timeout"] = timeout
                kill_process_group(process)
                if stall_out is not None:
                    stall_out.update(stall)
                return None, False


# ---------------------------------------------------------------- session lock
#
# build/quest_gate/<quest>/ is not one run's output directory, it is THE
# directory for that quest id: TORIRS_CONTENT_TEST itself, where the driver
# writes ledger.tsv, shots/NNN-name.png and client.log, and which
# prepare_session deletes whole before every run. Two `run.py <same id>`
# processes therefore do not race a little -- the second one rm -rf's the
# first one's session out from under a live client and then writes its own
# rows into the same file, and what is left afterwards belongs to neither
# run. That read as a quest REGRESSION on 2026-09-20 (a seam worker's "druid
# regression" was two sessions writing one directory), which is the worst way
# for a harness bug to present: wearing a content bug's clothes.
#
# One run per id at a time, then. Refusing is the whole fix -- a second
# directory would leave gate.py, publish() and the batch contact sheets
# guessing which one is the quest's evidence, and they all resolve
# build/quest_gate/<id> by name.
#
# The lock file lives OUTSIDE the session directory (prepare_session would
# delete one inside it) and carries the pid, so the refusal can name the live
# run and so a lock whose process is gone -- a killed run, a crashed one --
# is recognised as stale and taken over rather than blocking the id forever.
# --jobs N is untouched: it runs DISTINCT ids, one lock each.
LOCK_DIR = os.path.join(REPO_ROOT, "build", "quest_gate", ".locks")

_held_locks = set()


def session_lock_path(name):
    assert name
    return os.path.join(LOCK_DIR, "%s.lock" % name)


def read_session_lock(path):
    """(pid, started, command) from a lock file, or None when it does not
    exist or cannot be read as one -- a lock nobody can read says nothing
    about a live run, so it is treated as stale."""
    assert path
    try:
        with open(path, "r", encoding="utf-8") as handle:
            fields = handle.read().strip().split("\t")
    except OSError:
        return None
    if len(fields) < 2 or not fields[0].isdigit():
        return None
    return int(fields[0]), fields[1], fields[2] if len(fields) > 2 else ""


def pid_is_live(pid):
    """Signal 0 to `pid`: EPERM is somebody else's process, which is still a
    process, and ESRCH is the only answer that means the run is gone."""
    assert pid > 0
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def release_session_lock(name):
    """Drop the lock, if this process is the one holding it. Idempotent: the
    run path calls it in a finally, and atexit calls it again for the runs
    that a Ctrl-C or a kill never returned from."""
    assert name
    path = session_lock_path(name)
    if path not in _held_locks:
        return
    _held_locks.discard(path)
    entry = read_session_lock(path)
    # Somebody else's lock in our slot (ours was cleared as stale while this
    # process was stopped): removing it would hand a third run the session a
    # live one is using.
    if entry and entry[0] != os.getpid():
        return
    try:
        os.unlink(path)
    except OSError:
        pass


def release_all_session_locks():
    for path in sorted(_held_locks):
        release_session_lock(os.path.splitext(os.path.basename(path))[0])


atexit.register(release_all_session_locks)


def acquire_session_lock(name):
    """Take quest id `name`'s one-run-at-a-time lock. Returns None when it is
    ours, or the refusal message (naming the live run) when it is not."""
    assert name
    os.makedirs(LOCK_DIR, exist_ok=True)
    path = session_lock_path(name)
    payload = "%d\t%s\t%s\n" % (os.getpid(), time.strftime("%Y-%m-%dT%H:%M:%S"),
                                " ".join(sys.argv[1:]))
    for _attempt in (1, 2):
        try:
            handle = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o644)
        except FileExistsError:
            entry = read_session_lock(path)
            if entry and entry[0] != os.getpid() and pid_is_live(entry[0]):
                return (
                    "run.py: REFUSING to run %s -- another run of the same quest id is "
                    "live:\n"
                    "    pid %d, started %s%s\n"
                    "    It owns %s, which is that quest's ONE session directory: this "
                    "run would delete its ledger.tsv, shots/ and client.log mid-flight "
                    "and leave a mixture of the two behind (the 2026-09-20 \"druid "
                    "regression\").\n"
                    "    Wait for pid %d to finish, or drive a private copy under "
                    "another name: run.py --script <file> --name %s_2.\n"
                    "    lock: %s (remove it by hand only once you know pid %d is gone)"
                    % (name, entry[0], entry[1],
                       (" (%s)" % entry[2]) if entry[2] else "",
                       os.path.join("build", "quest_gate", name),
                       entry[0], name, path, entry[0]))
            print("run.py: clearing a stale session lock for %s (%s) -- %s"
                  % (name, path,
                     "pid %d is gone" % entry[0] if entry else "unreadable"), flush=True)
            try:
                os.unlink(path)
            except OSError:
                pass
            continue
        with os.fdopen(handle, "w") as out:
            out.write(payload)
        _held_locks.add(path)
        return None
    return ("run.py: REFUSING to run %s -- its session lock (%s) was re-taken by "
            "another run while this one was clearing it as stale" % (name, path))


def locked_result(name, message):
    """The result row a refused run contributes: no process, no ledger, not
    ok -- so --all keeps going for every other id and main() still exits
    non-zero."""
    assert name
    assert message
    print(message, file=sys.stderr, flush=True)
    return {
        "name": name, "exit_code": None, "timed_out": False, "has_ledger": False,
        "directory": os.path.join(REPO_ROOT, "build", "quest_gate", name),
        "ok": False, "locked": message,
    }


def clear_session_directory(directory):
    """Empty a session directory for a new run, except a detached run's own
    run.pid / run.status / run.out (DETACH_FILES): the child is writing
    run.out and --wait reads the other two while this run is live. A
    foreground run clears stale ones too, so --wait never reports an old
    detached run's end as this run's."""
    assert directory
    keep = DETACH_FILES if os.environ.get(DETACH_CHILD_ENV) else ()
    for entry in os.listdir(directory):
        if entry in keep:
            continue
        path = os.path.join(directory, entry)
        if os.path.isdir(path) and not os.path.islink(path):
            shutil.rmtree(path)
        else:
            os.unlink(path)


def prepare_session(name, fixture_name, user=None, checkpoint_save=None):
    """A fresh, private build/quest_gate/<name>/ directory -- never reused;
    the server writes into it on exit. A checkpoint run logs in as `user`
    (the full run's account) from `checkpoint_save` instead of the fixture."""
    directory = os.path.join(REPO_ROOT, "build", "quest_gate", name)
    if os.path.isdir(directory):
        clear_session_directory(directory)
    os.makedirs(directory, exist_ok=True)
    saves = os.path.join(directory, "saves")
    if checkpoint_save is not None:
        assert user
        os.makedirs(saves, exist_ok=True)
        with open(os.path.join(saves, "%s.ini" % save_file_stem(user)), "w",
                  encoding="utf-8") as handle:
            handle.write(checkpoint_save)
    else:
        write_session_fixture(fixture_name, saves, user or name)
    return directory, saves


def shot_sort_key(filename):
    """Chronological order of a shots/ entry: the numeric prefix core.lua
    gives every capture ("007-talkToGuard.png" -> 7), then the name. A
    file without a numeric prefix (TIMEOUT.png copied beside them) sorts
    last."""
    assert filename
    match = re.match(r"(\d+)-", filename)
    return (int(match.group(1)) if match else 10 ** 9, filename)


def copy_timeout_shot(directory):
    """On a wall-clock timeout, the driver's own LAST shot (if it took any
    at all) is copied to <directory>/TIMEOUT.png -- run.py's own exit code
    already says the process did not finish cleanly, but a human (or
    print_failure_block, below) reading build/quest_gate/<quest>/ afterward
    should not have to re-run the quest just to see what the screen looked
    like when it hung. Shots are named "NNN-name.png" (core.lua:
    string.format("%03d-%s", ...)); they are sorted by that NUMBER, because
    a plain string sort put "99-" after "100-" and copied the wrong frame
    (runs before the three-digit prefix still have two-digit names).
    None (no copy made) when the run never got far enough to take even one
    shot -- a timeout that early has nothing to show."""
    assert directory
    shots_dir = os.path.join(directory, "shots")
    if not os.path.isdir(shots_dir):
        return None
    pngs = sorted((entry for entry in os.listdir(shots_dir) if entry.endswith(".png")),
                  key=shot_sort_key)
    if not pngs:
        return None
    target = os.path.join(directory, "TIMEOUT.png")
    shutil.copy2(os.path.join(shots_dir, pngs[-1]), target)
    return target


# ------------------------------------------------ a run that ended unfinished
#
# seam31 run_never_ends_silently. The ledger's SUMMARY row is written by the
# client (drive_ledger_write_summary) and ONLY by the client: on t.finish, on
# the script returning, on a Lua error. A client that stops any other way --
# the TORIRS_MAX_FRAMES virtual clock running out (main.c returns 0 and prints
# nothing), a crash, an assert, the wall-clock kill below -- left a ledger that
# just stopped, and "exit 0, no SUMMARY, nothing in client.log" is what a relay
# runner spent ten runs on (legends leg 7, sonnet-b37: the frame budget ran out
# inside the Nezikchened kill wait, three runs in a row).
#
# So run.py always finishes the ledger itself: one FAIL row `run.unfinished`
# whose detail says `run ended without finishing: <reason>`, naming the row
# that was open (the driver's `QUEST row-begin` line, core.lua), the last tick
# the driver reported, and the last `QUEST progress` line of a long wait; then
# a SUMMARY counted from the rows. A Lua error's own `script-error` row gets
# the same "inside row <name>" suffix, because the C writer cannot know it.
ROW_BEGIN_RE = re.compile(r"^QUEST row-begin (?P<name>\S+) tick=(?P<tick>\d+)")
PROGRESS_RE = re.compile(r"^QUEST progress (?P<text>.*) tick=(?P<tick>\d+)$")
ROW_WRITTEN_RE = re.compile(r"^QUEST \S+ (?:PASS|FAIL|BLOCKED) (?P<step>\S+) ticks=")
# torirs_plugin_drive.c reports a raising await predicate on stderr and treats
# it as "not yet true", so the row times out with only its note for a detail;
# run.py folds the error into that row (the error is the author's bug, and a
# timeout that hides it reads like a slow world).
PREDICATE_ERROR_RE = re.compile(r"^QUEST \S+ await (?:level|match)\(\) error: (?P<error>.*)$")
FATAL_LINE_RE = re.compile(r"Assertion failed|assertion|Segmentation|Abort|panic|error:",
                           re.IGNORECASE)
# EMBED_CLOCK_MS (client_env) is 20 ms of server time per frame, and a server
# tick is 600 ms: TORIRS_MAX_FRAMES frames is about max_frames / 30 ticks.
FRAMES_PER_SERVER_TICK = 30
# A run whose last reported tick is within this fraction of the frame budget's
# ticks is said to have run the budget out; below it, exit 0 is named as
# unexplained rather than blamed on the clock.
FRAME_BUDGET_NEAR = 0.9


def scan_client_log(log_path):
    """What the driver said on stderr about where it was: (open_row, last_tick,
    last_progress, fatal_line, predicate_errors). open_row is (name, tick) for a
    `row-begin` with no written row after it, else None; last_progress is the
    open row's own last progress line; predicate_errors maps a written step to
    the first await-predicate error reported while it was open."""
    open_row = None
    last_tick = None
    last_progress = None
    fatal_line = None
    predicate_errors = {}
    pending_error = None
    if not os.path.isfile(log_path):
        return open_row, last_tick, last_progress, fatal_line, predicate_errors
    with open(log_path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            stripped = line.rstrip("\n")
            match = ROW_BEGIN_RE.match(stripped)
            if match:
                open_row = (match.group("name"), int(match.group("tick")))
                last_tick = int(match.group("tick"))
                last_progress = None
                pending_error = None
                continue
            match = PREDICATE_ERROR_RE.match(stripped)
            if match:
                pending_error = pending_error or match.group("error")[:200]
                continue
            match = PROGRESS_RE.match(stripped)
            if match:
                last_progress = stripped[len("QUEST progress "):]
                last_tick = int(match.group("tick"))
                continue
            match = ROW_WRITTEN_RE.match(stripped)
            if match:
                step = match.group("step")
                if pending_error and step not in predicate_errors:
                    predicate_errors[step] = pending_error
                pending_error = None
                if open_row and (step == open_row[0] or step.startswith(open_row[0] + "-")):
                    open_row = None
                    last_progress = None
                continue
            if not stripped.startswith("QUEST ") and FATAL_LINE_RE.search(stripped):
                fatal_line = stripped[:300]
    return open_row, last_tick, last_progress, fatal_line, predicate_errors


def stall_reason(stall, row_name, last_tick):
    """seam32: the words for a stall kill. `row_name` is the open row, else
    the last one written; the tick is the heartbeat's own (the last tick the
    client lived to), else the driver's last reported one."""
    assert stall
    # The later of the two: the heartbeat is written every 25 ticks, the
    # driver's row-begin/progress lines can be a few ticks past it.
    ticks = [value for value in (stall["tick"], last_tick) if value is not None]
    tick = max(ticks) if ticks else "none reported"
    if row_name == "none":
        row_name = "(none written)"
    if stall["boot"]:
        return ("run stalled at boot: no heartbeat %d s after launch (boot grace %d s, "
                "TORIRS_QUEST_STALL_BOOT_SECONDS) -- the quest script never started ticking; "
                "last row %s at tick %s; run.py killed the process group"
                % (stall["seconds"], stall["boot_grace"], row_name, tick))
    return ("run stalled: no client tick for %d s; last row %s at tick %s (run.py killed the "
            "process group: heartbeat older than TORIRS_QUEST_STALL_SECONDS=%d, long before "
            "the %d s wall-clock ceiling)"
            % (stall["seconds"], row_name, tick, stall["stall_seconds"], stall["timeout"]))


def unfinished_reason(code, timed_out, timeout, max_frames, last_tick, fatal_line):
    """Why the client stopped, in words: exited / timed out / killed."""
    budget_ticks = max_frames // FRAMES_PER_SERVER_TICK
    if timed_out:
        return ("killed by run.py after the wall-clock timeout of %d s (the process was still "
                "running; last tick the driver reported %s)" % (timeout, last_tick))
    if code is not None and code < 0:
        return "the client was killed by signal %d%s" % (
            -code, (" -- last fatal-looking line: %s" % fatal_line) if fatal_line else "")
    if code:
        return ("the client exited %d without t.finish (a crash or an assert)%s" % (
            code, (" -- last fatal-looking line: %s" % fatal_line) if fatal_line else
            " -- nothing fatal-looking in client.log"))
    if last_tick is not None and last_tick >= budget_ticks * FRAME_BUDGET_NEAR:
        return ("the client exited 0 at the frame budget: TORIRS_MAX_FRAMES=%d is about %d "
                "server ticks and the driver last reported tick %d -- the run needs a larger "
                "`max_frames = <n>,` in its quest table (ceiling %d) or fewer ticks"
                % (max_frames, budget_ticks, last_tick, MAX_FRAMES_CEILING))
    if last_tick is None:
        return ("the client exited 0 without t.finish and the driver reported no tick, so "
                "whether TORIRS_MAX_FRAMES=%d (about %d server ticks) ran out cannot be told"
                % (max_frames, budget_ticks))
    return ("the client exited 0 without t.finish at tick %d, short of the %d ticks "
            "TORIRS_MAX_FRAMES=%d allows -- not the frame budget; see client.log's tail"
            % (last_tick, budget_ticks, max_frames))


def finish_unfinished_ledger(directory, code, timed_out, timeout, max_frames, stall=None):
    """Append `run.unfinished` FAIL + a SUMMARY to a ledger the client left
    without one (or create the ledger when the client wrote none), and name the
    open row in a `script-error` row's detail. `stall` is launch_client's stall
    dict when the stall detector killed the client (seam32). Returns the reason
    written, or None when the client's own SUMMARY is there."""
    assert directory
    ledger_path = os.path.join(directory, "ledger.tsv")
    log_path = os.path.join(directory, "client.log")
    open_row, last_tick, last_progress, fatal_line, predicate_errors = scan_client_log(log_path)
    lines = []
    if os.path.isfile(ledger_path):
        with open(ledger_path, "r", encoding="utf-8", errors="replace") as handle:
            lines = handle.read().splitlines()
    where = ""
    if open_row:
        where = "inside row '%s' (begun at tick %d)" % open_row
    if last_progress:
        where += ("; " if where else "") + "last progress: " + last_progress
    changed = False
    for position, line in enumerate(lines):
        columns = line.split("\t")
        if len(columns) >= 6 and columns[1] == "script-error" and where \
                and "inside row" not in columns[5]:
            columns[5] = "%s -- raised %s" % (columns[5], where)
            lines[position] = "\t".join(columns)
            changed = True
        elif len(columns) >= 6 and columns[2] == "FAIL" and columns[1] in predicate_errors \
                and "predicate raised" not in columns[5]:
            columns[5] = "%s -- its await predicate raised (read as not-yet-true until the " \
                "deadline): %s" % (columns[5], predicate_errors[columns[1]].replace("\t", " "))
            lines[position] = "\t".join(columns)
            changed = True
    has_summary = any(line.startswith("SUMMARY\t") for line in lines)
    reason = None
    if not has_summary:
        if not lines:
            lines = ["quest-ledger-v1", "index\tstep\tverdict\tticks\tshots\tdetail"]
        rows = [line.split("\t") for line in lines[2:] if line and not line.startswith("SUMMARY")]
        index = len(rows) + 1
        last_written = rows[-1][1] if rows else "none"
        if stall:
            reason = stall_reason(stall, open_row[0] if open_row else last_written, last_tick)
        else:
            reason = unfinished_reason(code, timed_out, timeout, max_frames, last_tick,
                                       fatal_line)
        detail = "run ended without finishing: %s; %s; last row written: %s" % (
            reason, where or "no row was open (the driver reported no row-begin after the "
            "last written row)", last_written)
        lines.append("%d\trun.unfinished\tFAIL\t0\t\t%s" % (index, detail.replace("\t", " ")))
        verdicts = [row[2] for row in rows if len(row) > 2] + ["FAIL"]
        blocked = verdicts.count("BLOCKED")
        summary = "SUMMARY\t%d\tFAIL\t%d\texit=%s\tpass=%d fail=%d" % (
            index, sum(int(row[3]) for row in rows if len(row) > 3 and row[3].isdigit()),
            "none" if code is None else code, verdicts.count("PASS"), verdicts.count("FAIL"))
        if blocked:
            summary += " blocked=%d" % blocked
        lines.append(summary)
        changed = True
        print("run.py: %s ended without finishing: %s" % (os.path.basename(directory), reason),
              flush=True)
    if changed:
        with open(ledger_path, "w", encoding="utf-8") as handle:
            handle.write("\n".join(lines) + "\n")
    return reason


def launch_and_report(name, binary, manifest_path, directory, saves, script, timeout,
                      max_frames, user=None):
    log_path = os.path.join(directory, "client.log")
    stall = {}
    code, timed_out = launch_client(binary, manifest_path, user or name, directory, saves,
                                     script, log_path, scaled_timeout(timeout, max_frames),
                                     max_frames, stall_out=stall)
    stall = stall or None
    if timed_out or stall:
        copy_timeout_shot(directory)
    # seam31: never leave a ledger without a SUMMARY (finish_unfinished_ledger).
    unfinished = finish_unfinished_ledger(directory, code, timed_out,
                                          scaled_timeout(timeout, max_frames), max_frames,
                                          stall=stall)
    ledger_path = os.path.join(directory, "ledger.tsv")
    # The raid HUD against the overhead bar, on every run whose tick log has boss
    # HUD pushes (gate.hudbar_check; a party run gets it from gate.party_union).
    import gate
    hud_verdict, hud_detail = gate.hudbar_check(os.path.join(directory, "ticklog.tsv"))
    if hud_verdict is not None:
        gate.append_ledger_row(ledger_path, gate.HUDBAR_STEP, hud_verdict, hud_detail)
    has_ledger = os.path.isfile(ledger_path)
    ok = (not timed_out) and (not stall) and code == 0 and has_ledger and unfinished is None
    return {
        "name": name, "exit_code": code, "timed_out": timed_out,
        "has_ledger": has_ledger, "directory": directory, "ok": ok,
        "unfinished": unfinished, "stalled": stall is not None,
    }


def run_quest(name, binary, manifest_path, timeout):
    quest_file = quest_list.quest_path(REPO_ROOT, name)
    assert os.path.isfile(quest_file), quest_file
    fixture_name = read_fixture_name(quest_file)
    max_frames = read_max_frames(quest_file)
    refusal = acquire_session_lock(name)
    if refusal:
        return locked_result(name, refusal)
    try:
        def prepare_and_launch():
            directory, saves = prepare_session(name, fixture_name)
            script = os.path.join(directory, "%s.lua" % name)
            write_wrapper_script(quest_file, script)
            return launch_and_report(name, binary, manifest_path, directory, saves, script,
                                     timeout, max_frames)
        result = launch_with_stale_retry(prepare_and_launch)
        if result["has_ledger"]:
            collect_checkpoints(result, name, quest_file, binary)
        return result
    finally:
        release_session_lock(name)


def run_quest_from_leg(test_id, leg, only, binary, manifest_path, timeout):
    """`run.py <id> --from-leg K` / `--only-leg K`: legs K..end (or K alone)
    of a legs file, from checkpoint K-1, under the run name <id>.leg<K>,
    logged in as <id> (the checkpoint IS that account's save). Returns
    (result, None) or (None, refusal) -- a refusal is exit 2, and nothing ran."""
    quest_file = quest_list.quest_path(REPO_ROOT, test_id)
    assert os.path.isfile(quest_file), quest_file
    _, layout = legs_source(quest_file)
    if layout is None:
        return None, ("run.py: REFUSING --%s-leg %d: %s has no `legs` table (a run file is "
                      "run whole)" % ("only" if only else "from", leg,
                                      os.path.relpath(quest_file, REPO_ROOT)))
    if not 1 <= leg <= len(layout["legs"]):
        return None, ("run.py: REFUSING --%s-leg %d: %s has %d leg(s)" % (
            "only" if only else "from", leg, test_id, len(layout["legs"])))
    save_text = None
    tile = None
    source = None
    if leg > 1:
        path, manifest, save_text, why = find_checkpoint(test_id, leg - 1, quest_file, binary)
        if path is None:
            return None, ("run.py: REFUSING --%s-leg %d of %s: checkpoint %d is not usable\n"
                          "    %s" % ("only" if only else "from", leg, test_id, leg - 1, why))
        tile = save_tile(save_text)
        source = os.path.relpath(path, REPO_ROOT)
        print("run.py: %s leg %d from checkpoint %s (written %s by run %s)" % (
            test_id, leg, source, manifest.get("written"), manifest.get("source_run")),
            flush=True)
    name = "%s.leg%d" % (test_id, leg)
    fixture_name = read_fixture_name(quest_file)
    max_frames = read_max_frames(quest_file)
    refusal = acquire_session_lock(name)
    if refusal:
        return locked_result(name, refusal), None
    try:
        def prepare_and_launch():
            directory, saves = prepare_session(name, fixture_name, user=test_id,
                                               checkpoint_save=save_text)
            script = os.path.join(directory, "%s.lua" % name)
            write_wrapper_script(quest_file, script,
                                 leg_mode={"from": leg, "only": only, "tile": tile})
            return launch_and_report(name, binary, manifest_path, directory, saves, script,
                                     timeout, max_frames, user=test_id)
        result = launch_with_stale_retry(prepare_and_launch)
        directory = result["directory"]
        result["from_leg"] = leg
        if result["has_ledger"]:
            stamp_summary_from_leg(os.path.join(directory, "ledger.tsv"), leg, only)
            collect_checkpoints(result, test_id, quest_file, binary)
        if source:
            with open(os.path.join(directory, "FROM_CHECKPOINT"), "w", encoding="utf-8") as handle:
                handle.write(source + "\n")
        return result, None
    finally:
        release_session_lock(name)


def run_script_direct(name, script_path, fixture_name, binary, manifest_path, timeout):
    """The --script escape hatch: runs `script_path` under
    build/quest_gate/<name>/. A file whose table declares a NON-EMPTY `setup`
    list gets it run by the same generated loop a quest run uses
    (write_wrapper_script, pass_through_without_setup=True): before seam
    pass 19 this path ran the file as-is and silently ignored the list, so a
    scratch copy of a quest issued none of its setup lines and parity2a read
    that as "::setlevel in setup is a no-op". A file with no setup, or
    `setup = {}`, runs exactly as written. test/quests/_conformance.lua
    is ALSO a `{ run = function(t) ... end }` table (its own `setup = {}` is
    empty, and its comment says it re-issues any cheats itself through
    t.cheat "so it can also run standalone"), so it loads through the same
    zero-argument bootstrap a quest file does with no wrapping needed at
    all. This is what lets it be driven through this runner as a direct
    cross-check against `make test-quest-conformance`'s own answer, rather
    than only ever through conformance.py's separate attempt loop."""
    assert os.path.isfile(script_path), script_path
    max_frames = read_max_frames(script_path)
    refusal = acquire_session_lock(name)
    if refusal:
        return locked_result(name, refusal)
    try:
        def prepare_and_launch():
            directory, saves = prepare_session(name, fixture_name)
            # Same basename as the source, in a subdirectory: the driver names
            # the run after the script file (`QUEST <basename> ...` lines), and
            # that name must not change because the file is now generated.
            wrapped_dir = os.path.join(directory, "script")
            os.makedirs(wrapped_dir, exist_ok=True)
            script = os.path.join(wrapped_dir, os.path.basename(script_path))
            write_wrapper_script(script_path, script, pass_through_without_setup=True)
            return launch_and_report(name, binary, manifest_path, directory, saves, script,
                                     timeout, max_frames)
        return launch_with_stale_retry(prepare_and_launch)
    finally:
        release_session_lock(name)


# ------------------------------------------------------------------ party run
#
# raid seam17 party_run_and_verbs: N clients, ONE world. A test file that
# declares `party = N,` (or a run given --party N) is launched as N client
# processes. The LEADER (seat 1) hosts the embedded world as usual and
# listens on a loopback port (TORIRS_EMBED_PARTY_LISTEN/_SIZE); every MEMBER
# (seats 2..N) joins that world over the party link
# (TORIRS_EMBED_PARTY_JOIN/_SEAT; src/torirsserver/torirs_server_embed.h),
# and the world ticks in lock step with every client. Each raider:
#   * logs in as <base>_p<n> (base = the run name, sanitised and cut to 9
#     characters, so the account is at most 12 -- only the first 12 characters
#     of a name seed a run, jbase37), password QUEST_PASSWORD;
#   * has its own session directory build/quest_gate/<run>/p<n>/ (ledger.tsv,
#     shots/, heartbeat, client.log, its wrapper script);
#   * runs the SAME test file, wrapped with QD_PARTY = {role, size, names}
#     (write_wrapper_script), so the file branches on t.party.role().
# Every fixture goes into the WORLD's saves directory,
# build/quest_gate/<run>/saves/, written before any client starts. The tick
# log is the world's one log, in p1/; it is copied into every p<n>/ and the
# run directory afterwards. The LEADER's process is the run: its stall or exit
# ends it, then the members get PARTY_MEMBER_GRACE_SECONDS to finish their own
# script before their process groups are killed, and each member's ledger gets
# a SUMMARY like any unfinished run. gate.py grades the union of the ledgers
# (gate.party_union, run here too so the report and publish see it).
PARTY_RE = re.compile(r"(?m)^\s{0,8}party\s*=\s*(\d+)\s*,")
PARTY_MAX = 4  # TORIRSSERVER_EMBED_CLIENT_MAX (torirs_server_embed.h)
PARTY_MEMBER_GRACE_SECONDS = 20


def read_party_size(script_file):
    """The `party = <n>,` field a test file declares, or 1."""
    with open(script_file, "r", encoding="utf-8") as handle:
        matches = PARTY_RE.findall(handle.read())
    assert len(matches) <= 1, "%s: more than one party field" % script_file
    return int(matches[0]) if matches else 1


def party_accounts(name, size):
    base = save_file_stem(name)[:9]
    return ["%s_p%d" % (base, n) for n in range(1, size + 1)]


def free_loopback_port():
    import socket
    probe = socket.socket()
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()
    return port


def run_party(name, source_file, fixture_name, binary, manifest_path, timeout, size,
              pass_through_without_setup):
    """One party run of `source_file` under build/quest_gate/<name>/ (see the
    banner above). Returns a result dict shaped like launch_and_report's, its
    `directory` the run directory that holds the union ledger."""
    assert 2 <= size <= PARTY_MAX, "a party is 2..%d raiders, not %d" % (PARTY_MAX, size)
    import gate  # the union lives with the grader (gate.party_union)
    max_frames = read_max_frames(source_file)
    accounts = party_accounts(name, size)
    refusal = acquire_session_lock(name)
    if refusal:
        return locked_result(name, refusal)
    try:
        directory = os.path.join(REPO_ROOT, "build", "quest_gate", name)
        if os.path.isdir(directory):
            clear_session_directory(directory)
        os.makedirs(directory, exist_ok=True)
        saves = os.path.join(directory, "saves")
        seats = []
        with open(os.path.join(directory, gate.PARTY_MARKER), "w", encoding="utf-8") as marker:
            for seat in range(1, size + 1):
                account = accounts[seat - 1]
                session = os.path.join(directory, "p%d" % seat)
                os.makedirs(session)
                write_session_fixture(fixture_name, saves, account)
                script_dir = os.path.join(session, "script")
                os.makedirs(script_dir)
                script = os.path.join(script_dir, os.path.basename(source_file))
                write_wrapper_script(source_file, script,
                                     pass_through_without_setup=pass_through_without_setup,
                                     party={"role": seat, "names": accounts})
                seats.append((seat, account, session, script))
                marker.write("p%d\t%s\tp%d\n" % (seat, account, seat))
        port = free_loopback_port()
        wait_s = os.environ.get("TORIRS_EMBED_PARTY_WAIT_S", "60")
        wall = scaled_timeout(timeout, max_frames)
        print("run.py: party of %d on port %d: %s" % (size, port, ", ".join(accounts)), flush=True)
        members = []
        for seat, account, session, script in seats[1:]:
            command = [binary, "--manifest", manifest_path, "--user", account, "--pass",
                       QUEST_PASSWORD, "--soft3d", "--window", "765x503"]
            environment = client_env(session, saves, script, max_frames, {
                "TORIRS_EMBED_PARTY_JOIN": str(port),
                "TORIRS_EMBED_PARTY_SEAT": str(seat),
                "TORIRS_EMBED_PARTY_WAIT_S": wait_s,
                "TORIRS_EMBED_PARTY_TRACE": "1",
                # The leader hosts the world; one run, one npc seed.
                "TORIRSSERVER_RUN_NAME": accounts[0],
            })
            print("+ [p%d] %s" % (seat, " ".join(command)), flush=True)
            log = open(os.path.join(session, "client.log"), "wb")
            # QUEST_PARTY_NICE_SEAT=<n> (raid seam21, party_repeat.py --load):
            # member n runs at nice 19, so under a load generator it is the
            # slowest raider; the lock step must not care.
            slow = os.environ.get("QUEST_PARTY_NICE_SEAT") == str(seat)
            if slow:
                print("run.py: p%d runs at nice 19 (QUEST_PARTY_NICE_SEAT)" % seat, flush=True)
            process = subprocess.Popen(command, env=environment, cwd=REPO_ROOT, stdout=log,
                                       stderr=subprocess.STDOUT, start_new_session=True,
                                       preexec_fn=(lambda: os.nice(19)) if slow else None)
            members.append((seat, session, process, log))
        started = time.monotonic()
        leader_session = seats[0][2]
        stall = {}
        code, timed_out = launch_client(binary, manifest_path, accounts[0], leader_session, saves,
                                        seats[0][3], os.path.join(leader_session, "client.log"),
                                        wall, max_frames, stall_out=stall, extra_env={
                                            "TORIRS_EMBED_PARTY_LISTEN": str(port),
                                            "TORIRS_EMBED_PARTY_SIZE": str(size),
                                            "TORIRS_EMBED_PARTY_WAIT_S": wait_s,
                                            "TORIRS_EMBED_PARTY_TRACE": "1",
                                        })
        stall = stall or None
        leader_seconds = time.monotonic() - started
        if timed_out or stall:
            copy_timeout_shot(leader_session)
        unfinished = finish_unfinished_ledger(leader_session, code, timed_out, wall, max_frames,
                                              stall=stall)
        member_codes = {}
        deadline = time.monotonic() + PARTY_MEMBER_GRACE_SECONDS
        for seat, session, process, log in members:
            try:
                member_codes[seat] = process.wait(timeout=max(0.1, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                kill_process_group(process)
                member_codes[seat] = None
            log.close()
            member_unfinished = finish_unfinished_ledger(session, member_codes[seat], False, wall,
                                                         max_frames)
            if member_unfinished and unfinished is None:
                unfinished = "p%d: %s" % (seat, member_unfinished)
        leader_ticklog = os.path.join(leader_session, "ticklog.tsv")
        if os.path.isfile(leader_ticklog):
            for seat, account, session, script in seats[1:]:
                shutil.copy2(leader_ticklog, os.path.join(session, "ticklog.tsv"))
        gate.party_union(directory)
        # raid seam21: every raider's boundary trace against the leader's
        # (gate.party_lockstep); the union ledger carries it as party.lockstep.
        lock_verdict, lock_detail = gate.party_lockstep(directory)
        print("run.py: party %s: party.lockstep %s -- %s" % (name, lock_verdict, lock_detail),
              flush=True)
        print("run.py: party %s: leader exit %s after %.1f s; members %s" % (
            name, "none" if code is None else code, leader_seconds,
            ", ".join("p%d exit %s" % (seat, "killed" if member_codes[seat] is None
                                       else member_codes[seat]) for seat in sorted(member_codes))),
            flush=True)
        has_ledger = os.path.isfile(os.path.join(directory, "ledger.tsv"))
        ok = (not timed_out) and (not stall) and code == 0 and has_ledger and unfinished is None \
            and lock_verdict == "PASS"
        return {
            "name": name, "exit_code": code, "timed_out": timed_out, "has_ledger": has_ledger,
            "directory": directory, "ok": ok, "unfinished": unfinished,
            "stalled": stall is not None, "party": size, "lockstep": lock_verdict,
        }
    finally:
        release_session_lock(name)


def ledger_verdict(ledger_path):
    """The SUMMARY row's verdict word (PASS/FAIL), or None when the ledger
    has no SUMMARY row -- a run that died mid-way."""
    assert ledger_path
    with open(ledger_path, "r", encoding="utf-8") as handle:
        for line in handle:
            if line.startswith("SUMMARY\t"):
                columns = line.rstrip("\n").split("\t")
                return columns[2] if len(columns) > 2 else None
    return None


COMPLETION_ROWS = ("quest.varp_complete", "quest.scroll_title")


def publish_refusal_incomplete(test_id, ledger_path):
    """Why a PASS SUMMARY is still not evidence, or None (seam31).

    A legs file whose last leg returns without t.finish is finished by the
    harness with a PASS SUMMARY: a relay author's partial run (legends, six
    of nine legs) read as a played-through quest and replaced OSRS-Content's
    evidence. So a quest that is meant to reach t.quest.expect_complete --
    every legs file, and every run file that calls it -- publishes only a
    ledger whose expect_complete rows are PASS. A test with no quest varp
    (hans) never calls it and keeps the plain SUMMARY rule."""
    quest_file = quest_list.quest_path(REPO_ROOT, test_id)
    if not os.path.isfile(quest_file):
        return None
    text, layout = legs_source(quest_file)
    if layout is None and "expect_complete" not in text:
        return None
    rows, _ = ledger.read(ledger_path)
    passed = set(row["step"] for row in rows or [] if row["verdict"] == "PASS")
    missing = [name for name in COMPLETION_ROWS if name not in passed]
    if not missing:
        return None
    return ("the run did not reach t.quest.expect_complete (no PASS %s row)%s; a partial run "
            "is not evidence" % (" / ".join(missing),
                                 " -- a legs file's last leg returned without finishing the "
                                 "quest" if layout is not None else ""))


def publish(result):
    """Copy a PASSING quest's ledger.tsv and every shots/*.png into
    PUBLISH_DIR/<quest_dir>/play/, replacing whatever an earlier run
    published there -- beside that same quest_dir's scenes/ (the Gate D BMPs
    content audits already commit).

    Only a PASS is published: the directory is the persisted evidence that
    the quest played through, and a red run overwriting a green set would
    erase the evidence rather than add to it. A FAIL leaves the previous
    published set untouched and says so. Returns (path, shot_count) or
    None when nothing was published and why in the second slot."""
    if result.get("locked"):
        return None, "refused: another run of this id holds the session lock"
    if not result["has_ledger"]:
        return None, "no ledger"
    ledger_path = os.path.join(result["directory"], "ledger.tsv")
    if result.get("from_leg") or summary_from_leg(ledger.read(ledger_path)[1]):
        return None, "a checkpoint run (from_leg) is for authoring; only a full run is evidence"
    verdict = ledger_verdict(ledger_path)
    if verdict != "PASS":
        return None, "ledger SUMMARY is %s, not PASS" % verdict
    incomplete = publish_refusal_incomplete(result["name"], ledger_path)
    if incomplete:
        return None, incomplete
    target = os.path.join(PUBLISH_DIR, quest_dir_for(result["name"]), play_dir_for(result["name"]))
    if os.path.isdir(target):
        shutil.rmtree(target)
    os.makedirs(target)
    shutil.copy2(ledger_path, os.path.join(target, "ledger.tsv"))
    shots = os.path.join(result["directory"], "shots")
    count = 0
    if os.path.isdir(shots):
        for entry in sorted(os.listdir(shots)):
            if entry.endswith(".png"):
                shutil.copy2(os.path.join(shots, entry), os.path.join(target, entry))
                count += 1
    return target, count


def print_report(results):
    width = max((len(r["name"]) for r in results), default=5)
    print("")
    print("%-*s  %-10s  %-9s  %-6s  %s" % (
        width, "quest", "exit", "timed_out", "ledger", "directory"))
    print("%s  %s  %s  %s  %s" % ("-" * width, "-" * 10, "-" * 9, "-" * 6, "-" * 40))
    for r in results:
        print("%-*s  %-10s  %-9s  %-6s  %s" % (
            width, r["name"],
            "locked" if r.get("locked") else
            ("none" if r["exit_code"] is None else str(r["exit_code"])),
            "yes" if r["timed_out"] else ("stall" if r.get("stalled") else "no"),
            "yes" if r["has_ledger"] else "no",
            r["directory"]))
    print("")


CHAT_MIRROR_LINE_RE = re.compile(r'^QUEST ')


def last_fail_or_blocked_row(rows):
    """The LAST row (in ledger order) whose verdict is FAIL or BLOCKED, or
    None -- a quest can have several; the one that actually stopped the run
    (or is the tail of a t.blocked stub) is the last one written."""
    last = None
    for row in rows:
        if row["verdict"] in ("FAIL", "BLOCKED"):
            last = row
    return last


def fail_shot_path(directory, row):
    """The row's own "<name>-FAIL" shot (core.lua's record_with_shot takes
    one on every non-PASS t.do/t.check row, on top of the row's ordinary
    shot), or None when the row's `shots` column carries no such name --
    either because the verb was t.step/t.expect (never auto-shoots) or
    because the capture itself did not make it to disk."""
    for shot_name in ledger.shot_names(row):
        if shot_name.endswith("-FAIL"):
            candidate = os.path.join(directory, "shots", "%s.png" % shot_name)
            if os.path.isfile(candidate):
                return candidate
    return None


def last_chat_lines(log_path, count=5):
    """The last `count` lines of client.log that read as driver/chat
    narration. The stderr mirror's own `QUEST ...` lines
    (torirs_plugin_drive.c: drive_ledger_write's per-row mirror and
    lua_drive_report) are the one shape this log is GUARANTEED
    to carry -- captured stdout+stderr, every run (launch_client). A
    server-side `mes()` line is kept too, on a best-effort basis, when it
    is recognisable as one (containing " mes(" or " mes "), since a content
    script's own chat text is exactly what a human debugging a FAIL wants
    to see and some lanes do echo it into this same log -- but only the
    `QUEST ` lines are something this pass could verify are always there."""
    if not os.path.isfile(log_path):
        return []
    matches = []
    with open(log_path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            stripped = line.rstrip("\n")
            if CHAT_MIRROR_LINE_RE.match(stripped) or " mes(" in stripped or " mes " in stripped:
                matches.append(stripped)
    return matches[-count:] if count > 0 else matches


def quest_is_green(result):
    """True only for a quest whose process completed cleanly (result["ok"])
    AND whose ledger has no FAIL/BLOCKED row anywhere and a PASS SUMMARY.
    This is run.py's own narrow question -- just enough to decide whether
    print_failure_block has anything to say about a quest -- not the
    authoritative verdict, which is gate.py's alone (module docstring)."""
    if not result["ok"]:
        return False
    ledger_path = os.path.join(result["directory"], "ledger.tsv")
    rows, _ = ledger.read(ledger_path)
    if rows is None:
        return False
    if any(row["verdict"] in ("FAIL", "BLOCKED") for row in rows):
        return False
    return ledger_verdict(ledger_path) == "PASS"


def print_failure_block(results):
    """After the report table: for every quest that is not a clean PASS,
    the one screenful a human needs to start debugging without re-running
    anything -- the last FAIL/BLOCKED row's name and detail, its own -FAIL
    shot if the driver captured one, and the last few lines of client.log
    narration."""
    non_green = [r for r in results if not quest_is_green(r)]
    if not non_green:
        return
    print("---- failures ----")
    for r in non_green:
        print("%s:" % r["name"])
        if r.get("locked"):
            # Deliberately reads NOTHING under the directory: it belongs to
            # the live run, and quoting its half-written ledger here is how a
            # refusal would get mistaken for this run's result.
            for line in r["locked"].splitlines():
                print("    %s" % line.strip())
            print("")
            continue
        row = None
        if r["has_ledger"]:
            ledger_path = os.path.join(r["directory"], "ledger.tsv")
            rows, _ = ledger.read(ledger_path)
            row = last_fail_or_blocked_row(rows) if rows else None
        if row:
            print("    last FAIL/BLOCKED row: %s (%s) -- %s"
                  % (row["step"], row["verdict"], row["detail"]))
            shot_path = fail_shot_path(r["directory"], row)
            timeout_shot = os.path.join(r["directory"], "TIMEOUT.png")
            if not shot_path and row["step"] == "run.unfinished" and os.path.isfile(timeout_shot):
                shot_path = timeout_shot + " (the driver's last shot before the kill)"
            print("    shot: %s" % (shot_path if shot_path else "none captured"))
        elif r["timed_out"] or r.get("stalled"):
            timeout_shot = os.path.join(r["directory"], "TIMEOUT.png")
            print("    timed out with no FAIL/BLOCKED row written -- %s"
                  % (timeout_shot if os.path.isfile(timeout_shot) else "no TIMEOUT.png captured"))
        elif not r["has_ledger"]:
            print("    no ledger.tsv at all -- the process never got that far")
        else:
            print("    ledger has no FAIL/BLOCKED row (see gate.py for the full verdict)")
        chat_lines = last_chat_lines(os.path.join(r["directory"], "client.log"))
        if chat_lines:
            print("    last %d chat/driver line(s):" % len(chat_lines))
            for line in chat_lines:
                print("        %s" % line)
        else:
            print("    (no QUEST/mes line found in client.log)")
    print("")


# ---------------------------------------------------------------- detach / wait
#
# An agent's shell call is capped at ten minutes, and a full run of a long
# quest (legends, every relay's last leg, a reviewer's full run) takes longer,
# so a foreground `run.py <id>` is killed with its client before it writes a
# SUMMARY (seam31 long_runs_past_the_foreground_cap). `--detach` starts the
# SAME run.py command as a child in its own session and returns at once;
# `--wait <name>` blocks for at most --timeout seconds (default 540: nine
# minutes, under the cap) and either prints how the run ended or says it is
# still running and where it has got to. A run of any length is then driven
# as one --detach and as many 9-minute --waits as it takes.
#
# The child is run.py itself, re-executed with the same arguments minus
# --detach and DETACH_CHILD_ENV set to the run name; every other rule
# (session lock, build, publish, checkpoints, the per-process wall-clock
# ceiling) is the foreground path's own. Its files live IN the session
# directory, build/quest_gate/<name>/, which prepare_session keeps
# (DETACH_FILES) when it clears the directory for the run:
#   run.pid     the child's pid (written by the --detach parent)
#   run.status  key=value lines; state=starting|running|done, and at the
#               end exit=<code> ended=<time> elapsed=<s> (written by the
#               child: detach_child_started / detach_finish)
#   run.out     the child's stdout+stderr: build, report, failure block
# fail.py reads run.status too, so a partial ledger is reported as a run in
# progress, never as "the run stopped early".
DETACH_CHILD_ENV = "TORIRS_QUEST_DETACHED_NAME"
DETACH_FILES = ("run.pid", "run.status", "run.out")
DETACH_WAIT_DEFAULT = 540
DETACH_WAIT_EXIT_RUNNING = 3
DETACH_WAIT_POLL = 2.0
DETACH_OUT_TAIL_CHARS = 6000
REPORT_HEADER_RE = re.compile(r"^\S+\s+exit\s+timed_out\s+ledger\s+directory$")


def detach_directory(name):
    assert name
    return os.path.join(REPO_ROOT, "build", "quest_gate", name)


def read_detach_status(name):
    """run.status as a dict, or None when the run was never detached (or
    the file cannot be read -- which says nothing about a live run)."""
    assert name
    path = os.path.join(detach_directory(name), "run.status")
    try:
        with open(path, "r", encoding="utf-8") as handle:
            lines = handle.read().splitlines()
    except OSError:
        return None
    status = {}
    for line in lines:
        key, sep, value = line.partition("=")
        if sep:
            status[key.strip()] = value.strip()
    return status


def write_detach_status(name, fields):
    """Replace run.status whole (write + rename, so a --wait or fail.py never
    reads half a file)."""
    assert name
    assert fields
    directory = detach_directory(name)
    os.makedirs(directory, exist_ok=True)
    path = os.path.join(directory, "run.status")
    temporary = path + ".tmp"
    with open(temporary, "w", encoding="utf-8") as handle:
        for key, value in fields:
            handle.write("%s=%s\n" % (key, value))
    os.replace(temporary, path)


def detach_run_name(parser, arguments):
    """The session name the run will write under -- the same name main()'s
    own paths use: <id>, <id>.leg<K>, or --script's --name/basename."""
    if arguments.all:
        parser.error("--detach runs ONE session: name a quest, a --script or a leg "
                     "(detach the long quests one at a time)")
    if arguments.script:
        return arguments.name or os.path.splitext(os.path.basename(arguments.script))[0]
    name = arguments.quest_flag or arguments.quest
    if not name:
        parser.error("--detach needs a quest name or --script")
    leg = arguments.from_leg if arguments.from_leg is not None else arguments.only_leg
    if leg is not None:
        return "%s.leg%d" % (name, leg)
    return name


def detach_live_pid(name):
    """The pid of a live run already using session `name` -- its lock's
    holder, or a detached child that has not taken the lock yet -- or None."""
    entry = read_session_lock(session_lock_path(name))
    if entry and pid_is_live(entry[0]):
        return entry[0]
    status = read_detach_status(name)
    if status and status.get("state") in ("starting", "running"):
        pid = status.get("pid", "")
        if pid.isdigit() and int(pid) > 0 and pid_is_live(int(pid)):
            return int(pid)
    return None


def detach_start(parser, arguments):
    """`run.py ... --detach`: start the same command as a session-leader
    child and return at once (exit 0), or refuse (exit 1) when a run of the
    same session name is live -- the child would be refused by the session
    lock anyway, but only after --detach had already said it started."""
    name = detach_run_name(parser, arguments)
    live = detach_live_pid(name)
    if live is not None:
        print("run.py: REFUSING to detach %s -- pid %d is already running it; "
              "wait for it with: python3 tools/quest_gate/run.py --wait %s"
              % (name, live, name), file=sys.stderr, flush=True)
        return 1
    directory = detach_directory(name)
    os.makedirs(directory, exist_ok=True)
    child_argv = [a for a in sys.argv[1:] if a != "--detach"]
    command = [sys.executable, os.path.abspath(__file__)] + child_argv
    started = time.strftime("%Y-%m-%dT%H:%M:%S")
    write_detach_status(name, [("state", "starting"), ("name", name),
                               ("started", started), ("started_epoch", "%.0f" % time.time()),
                               ("command", " ".join(child_argv))])
    environment = dict(os.environ)
    environment[DETACH_CHILD_ENV] = name
    with open(os.path.join(directory, "run.out"), "wb") as out:
        # start_new_session: the shell call that ran --detach ends at once,
        # and its process group must not take the run down with it.
        process = subprocess.Popen(command, cwd=REPO_ROOT, env=environment,
                                    stdin=subprocess.DEVNULL, stdout=out,
                                    stderr=subprocess.STDOUT, start_new_session=True)
    with open(os.path.join(directory, "run.pid"), "w", encoding="utf-8") as handle:
        handle.write("%d\n" % process.pid)
    relative = os.path.relpath(directory, REPO_ROOT)
    print("run.py: detached %s as pid %d (started %s)\n"
          "    output: %s/run.out   status: %s/run.status\n"
          "    wait:   python3 tools/quest_gate/run.py --wait %s --timeout %d   "
          "(exit = the run's own exit, 0 or 1; %d still running: wait again)"
          % (name, process.pid, started, relative, relative, name, DETACH_WAIT_DEFAULT,
             DETACH_WAIT_EXIT_RUNNING), flush=True)
    return 0


def detach_child_started():
    """In the detached child, before anything else: run.status says
    running with the child's own pid (the parent may not have written
    run.pid yet)."""
    name = os.environ.get(DETACH_CHILD_ENV)
    assert name
    previous = read_detach_status(name) or {}
    write_detach_status(name, [("state", "running"), ("name", name), ("pid", str(os.getpid())),
                               ("started", previous.get("started", "")),
                               ("started_epoch", previous.get("started_epoch", "%.0f" % time.time())),
                               ("command", previous.get("command", " ".join(sys.argv[1:])))])


def detach_finish(code):
    """The exit path: a detached child records how it ended, then exits
    with `code` unchanged. A foreground run is untouched."""
    name = os.environ.get(DETACH_CHILD_ENV)
    if not name:
        return code
    previous = read_detach_status(name) or {}
    started_epoch = previous.get("started_epoch", "")
    elapsed = ("%.0f" % (time.time() - float(started_epoch))) if started_epoch else ""
    write_detach_status(name, [("state", "done"), ("name", name), ("pid", str(os.getpid())),
                               ("exit", str(code)),
                               ("started", previous.get("started", "")),
                               ("started_epoch", started_epoch),
                               ("ended", time.strftime("%Y-%m-%dT%H:%M:%S")),
                               ("elapsed", elapsed),
                               ("command", previous.get("command", ""))])
    return code


def detach_last_row(name):
    """(rows so far, last row, run tick) of the session's ledger, or
    (0, None, 0). A row's ticks column is the ticks THAT row took; the run's
    tick is their sum (the SUMMARY's total_ticks, before there is one)."""
    rows, _ = ledger.read(os.path.join(detach_directory(name), "ledger.tsv"))
    if not rows:
        return 0, None, 0
    tick = sum(int(r["ticks"]) for r in rows if r["ticks"].isdigit())
    return len(rows), rows[-1], tick


def detach_out_tail(name):
    """The report and failure block from run.out (from the last report
    header on), else its last lines -- capped, never the build log."""
    path = os.path.join(detach_directory(name), "run.out")
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            lines = handle.read().splitlines()
    except OSError:
        return "(no run.out)"
    start = None
    for index in range(len(lines) - 1, -1, -1):
        if REPORT_HEADER_RE.match(lines[index]):
            start = index
            break
    if start is None:
        # No report: the run stopped before it (a refusal, an exception).
        # Its last lines say why; the build log above them never does.
        tail = [line if len(line) <= 300 else line[:299] + "..." for line in lines[-8:]]
        return "last %d line(s) of run.out:\n%s" % (len(tail), "\n".join(tail))
    text = "\n".join(lines[start:])
    if len(text) > DETACH_OUT_TAIL_CHARS:
        text = "[...]\n" + text[-DETACH_OUT_TAIL_CHARS:]
    return text


def detach_wait(name, timeout):
    """`run.py --wait <name> [--timeout S]`: block until detached run `name`
    ends or S seconds pass. Exit 0 the run ended with exit 0, 1 it ended
    otherwise (or died without writing its end), 2 there is no detached run
    of that name, DETACH_WAIT_EXIT_RUNNING it is still running."""
    assert name
    assert timeout > 0
    if read_detach_status(name) is None:
        print("run.py: no detached run named %s (no %s) -- start one with run.py ... --detach"
              % (name, os.path.relpath(os.path.join(detach_directory(name), "run.status"),
                                       REPO_ROOT)), file=sys.stderr, flush=True)
        return 2
    deadline = time.time() + timeout
    while True:
        status = read_detach_status(name) or {}
        state = status.get("state", "")
        if state == "done":
            code = status.get("exit", "")
            summary = ledger.read(os.path.join(detach_directory(name), "ledger.tsv"))[1]
            print("run.py: %s ended: exit=%s after %ss (started %s, ended %s)"
                  % (name, code, status.get("elapsed", "?"), status.get("started", "?"),
                     status.get("ended", "?")))
            print("SUMMARY " + ("\t".join(summary[1:]) if summary else
                                "missing: the ledger has no SUMMARY row"))
            print(detach_out_tail(name), flush=True)
            return 0 if code == "0" else 1
        pid_text = status.get("pid", "")
        if not pid_text.isdigit():
            try:
                with open(os.path.join(detach_directory(name), "run.pid"), "r",
                          encoding="utf-8") as handle:
                    pid_text = handle.read().strip()
            except OSError:
                pid_text = ""
        if pid_text.isdigit() and int(pid_text) > 0 and not pid_is_live(int(pid_text)):
            again = read_detach_status(name) or {}
            if again.get("state") != "done":
                count, row, tick = detach_last_row(name)
                print("run.py: %s DIED without writing its end status (pid %s is gone, "
                      "state=%s); %d ledger row(s), last %s. Its output:"
                      % (name, pid_text, again.get("state", "?"), count,
                         "%s at tick %d" % (row["step"], tick) if row else "none"))
                print(detach_out_tail(name), flush=True)
                return 1
            continue
        remaining = deadline - time.time()
        if remaining <= 0:
            count, row, tick = detach_last_row(name)
            started_epoch = status.get("started_epoch", "")
            elapsed = ("%.0fs" % (time.time() - float(started_epoch))) if started_epoch else "?"
            if row:
                print("still running: last row %s at tick %d (%s; %d row(s); %s pid %s, %s "
                      "elapsed)" % (row["step"], tick, row["verdict"], count, name,
                                    pid_text or "?", elapsed))
            else:
                print("still running: no ledger row yet (%s, pid %s, %s elapsed; state=%s)"
                      % (name, pid_text or "?", elapsed, state or "?"))
            print("    wait again: python3 tools/quest_gate/run.py --wait %s --timeout %d"
                  % (name, DETACH_WAIT_DEFAULT), flush=True)
            return DETACH_WAIT_EXIT_RUNNING
        time.sleep(min(DETACH_WAIT_POLL, remaining))


def detach_dispatch(parser, arguments):
    """main()'s one call into this block, right after parse_args. Resolves
    --timeout (a wait's budget, or the per-process ceiling), then runs
    --wait or --detach and returns their exit code, or None to let main()
    run the command in this process (a foreground run, or the detached
    child itself)."""
    if arguments.wait is not None:
        if arguments.detach:
            parser.error("--wait and --detach are exclusive")
        if arguments.timeout is None:
            arguments.timeout = DETACH_WAIT_DEFAULT
        if arguments.timeout < 1:
            parser.error("--timeout must be at least 1 second")
        return detach_wait(arguments.wait, arguments.timeout)
    if arguments.timeout is None:
        arguments.timeout = DEFAULT_TIMEOUT
    if os.environ.get(DETACH_CHILD_ENV):
        # The child re-runs the command without --detach; an abbreviated
        # flag (--deta) is still parsed as detach here and must not fork again.
        detach_child_started()
        return None
    if arguments.detach:
        return detach_start(parser, arguments)
    return None


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("quest", nargs="?", help="run one quest: test/quests/<quest>.lua")
    parser.add_argument("--quest", dest="quest_flag", default=None, help=argparse.SUPPRESS)
    parser.add_argument("--all", action="store_true", help="run every discovered quest")
    parser.add_argument("--jobs", type=int, default=1,
                        help="quest client processes to run in parallel (default 1)")
    parser.add_argument("--timeout", type=int, default=None,
                        help="wall-clock ceiling per quest process, in seconds "
                             "(default %d) -- a hung run fails, it never hangs the suite; "
                             "scaled up by a quest's own max_frames / %s. With --wait: "
                             "how long to wait (default %d)"
                             % (DEFAULT_TIMEOUT, DEFAULT_MAX_FRAMES, DETACH_WAIT_DEFAULT))
    parser.add_argument("--warm-from", default="auto",
                        help="seed a not-yet-existing build_questtest objdir from this "
                             "directory, or 'auto' for the freshest sibling *_opt_es objdir "
                             "(default: auto; see build_support.py for why this is safe)")
    parser.add_argument("--no-warm", action="store_true",
                        help="always build cold; overrides --warm-from")
    parser.add_argument("--no-build", action="store_true", help="use the binary already built")
    parser.add_argument("--rebuild-scripts", action="store_true",
                        help="rebuild the server script pack (content contract checks + "
                             "sscompile) even when its fingerprint says it is current; "
                             "TORIRS_QUEST_ALWAYS_BUILD=1 does this on every run "
                             "(tools/quest_gate/pack_fingerprint.py)")
    parser.add_argument("--no-publish", action="store_true",
                        help="do not copy a PASSING quest's ledger and shots into "
                             "OSRS-Content (%s/<quest_dir>/play/); by default every "
                             "quest run does. TORIRS_QUEST_NO_PUBLISH=1 in the environment "
                             "does the same for every run.py it reaches (a seam pass, a "
                             "private-binary regression run)"
                             % os.path.relpath(PUBLISH_DIR, REPO_ROOT))
    parser.add_argument("--script", default=None,
                        help="advanced: run this .lua file directly as a single session "
                             "(a non-empty setup list is run first, as for a quest) -- see the module "
                             "docstring")
    parser.add_argument("--name", default=None,
                        help="artefact directory name for --script (default: its basename), or "
                             "for ONE party test id (needs --no-publish; raid seam21)")
    parser.add_argument("--from-leg", type=int, default=None, metavar="K",
                        help="a legs file only: log in from checkpoint K-1 (written by an "
                             "earlier run) and run legs K..end, under the run name <id>.leg<K>; "
                             "refuses with exit 2 when the checkpoint is missing or stale; "
                             "never publishes, and gate.py refuses its ledger "
                             "(docs/quest_authoring/relay.md \"Checkpoints\")")
    parser.add_argument("--only-leg", type=int, default=None, metavar="K",
                        help="as --from-leg K, but run leg K alone")
    parser.add_argument("--fixture", default=DEFAULT_FIXTURE,
                        help="fixture .ini for --script (default %s)" % DEFAULT_FIXTURE)
    parser.add_argument("--detach", action="store_true",
                        help="start this run in the background and return at once; it "
                             "writes build/quest_gate/<name>/run.pid, run.status and run.out "
                             "(docs/quest_authoring/relay.md \"Runs longer than the shell cap\")")
    parser.add_argument("--wait", default=None, metavar="NAME",
                        help="wait for detached run NAME (a quest id, <id>.leg<K> or a "
                             "--script --name) for at most --timeout seconds (default %d): "
                             "exits with the run's own exit (0 clean, 1 not; gate.py is the "
                             "verdict), 2 no such run, %d still running"
                             % (DETACH_WAIT_DEFAULT, DETACH_WAIT_EXIT_RUNNING))
    parser.add_argument("--party", type=int, default=None, metavar="N",
                        help="run N clients in one world as one party (raid seam17): the "
                             "leader hosts, N-1 members join over the party link; a test file "
                             "may declare `party = N,` instead (test/raids/README.md)")
    parser.add_argument("--render-every-frame", action="store_true",
                        help="draw every frame (TORIRS_RENDER_SKIP=0); by default a quest "
                             "client draws only the frames a screenshot or a click needs")
    arguments = parser.parse_args()
    global RENDER_SKIP
    RENDER_SKIP = not arguments.render_every_frame
    # A harness that must never publish (a seam pass, a private-binary
    # regression run) sets TORIRS_QUEST_NO_PUBLISH once instead of trusting
    # every command line it hands out to carry --no-publish: before this a
    # seam fixer's `run.py cooks_assistant --no-build` replaced the committed
    # evidence under OSRS-Content selftest/quests/quest_cook/play/ with its
    # private-binary shots. Any value but empty or "0" means "never publish".
    no_publish_env = os.environ.get("TORIRS_QUEST_NO_PUBLISH", "")
    if no_publish_env not in ("", "0") and not arguments.no_publish:
        print("run.py: TORIRS_QUEST_NO_PUBLISH=%s -- this run publishes nothing "
              "(as --no-publish)" % no_publish_env, flush=True)
        arguments.no_publish = True
    detach_code = detach_dispatch(parser, arguments)
    if detach_code is not None:
        return detach_code

    name = arguments.quest_flag or arguments.quest
    if not arguments.script and not arguments.all and not name:
        parser.error("give a quest name, --all, or --script")
    if arguments.jobs < 1:
        parser.error("--jobs must be at least 1")
    leg_request = arguments.from_leg if arguments.from_leg is not None else arguments.only_leg
    if leg_request is not None:
        if arguments.from_leg is not None and arguments.only_leg is not None:
            parser.error("--from-leg and --only-leg are exclusive")
        if arguments.script or arguments.all or not name:
            parser.error("--from-leg/--only-leg take one quest id (not --all, not --script)")
        if leg_request < 1:
            parser.error("a leg number is 1 or more")

    if not arguments.no_build:
        warm_from = None if arguments.no_warm else arguments.warm_from
        code, binary, seeded_from = build(warm_from)
        if code != 0:
            print("run.py: build failed", file=sys.stderr)
            return code
        if seeded_from:
            print("run.py: seeded %s_opt_es from %s" % (OBJ_BASE, seeded_from), flush=True)
    else:
        # QUEST_BINARY lets a worker that built the client into a PRIVATE
        # objdir/target (a C change under test) drive quests with that binary
        # while other workers keep using src/torirs_questtest untouched --
        # the shared binary is never swapped under a concurrent run.
        binary = os.environ.get("QUEST_BINARY") or os.path.join(REPO_ROOT, "src", TARGET)
    if not os.path.isfile(binary):
        print("run.py: no binary at %s" % binary, file=sys.stderr)
        return 1

    code = ensure_scripts(force=arguments.rebuild_scripts)
    if code != 0:
        print("run.py: the script pack did not build", file=sys.stderr)
        return code

    manifest_path = write_manifest()

    if leg_request is not None:
        if not os.path.isfile(quest_list.quest_path(REPO_ROOT, name)):
            print("run.py: no such quest file %s" % quest_list.quest_path(REPO_ROOT, name),
                  file=sys.stderr)
            return 1
        result, refusal = run_quest_from_leg(name, leg_request,
                                             arguments.only_leg is not None, binary,
                                             manifest_path, arguments.timeout)
        if refusal:
            print(refusal, file=sys.stderr, flush=True)
            return 2
        print_report([result])
        print_failure_block([result])
        print("run.py: not published %s (a checkpoint run is for authoring; the full run "
              "is the evidence)" % result["name"], flush=True)
        return 0 if result["ok"] else 1

    if arguments.script:
        script_path = os.path.abspath(arguments.script)
        script_name = arguments.name or os.path.splitext(os.path.basename(script_path))[0]
        party = arguments.party or read_party_size(script_path)
        if party > 1:
            result = run_party(script_name, script_path, arguments.fixture, binary,
                               manifest_path, arguments.timeout, party, True)
        else:
            result = run_script_direct(script_name, script_path, arguments.fixture,
                                        binary, manifest_path, arguments.timeout)
        print_report([result])
        print_failure_block([result])
        return 0 if result["ok"] else 1

    if arguments.all:
        names = quest_list.discover(REPO_ROOT)
        if not names:
            # An empty suite is a discovery FAILURE, not an empty pass --
            # indistinguishable, from here, from test/quests/ having been
            # wiped or misconfigured (gate.py makes the same call, same
            # reasoning, for the same input).
            print("run.py: no quest files under %s/ -- nothing to run "
                  "(this is a discovery fact, not a pass)"
                  % os.path.relpath(quest_list.quests_dir(REPO_ROOT), REPO_ROOT), file=sys.stderr)
            return 1
    else:
        quest_file = quest_list.quest_path(REPO_ROOT, name)
        if not os.path.isfile(quest_file):
            print("run.py: no such quest file %s" % quest_file, file=sys.stderr)
            return 1
        names = [name]

    def party_of(n):
        return arguments.party or read_party_size(quest_list.quest_path(REPO_ROOT, n))

    if any(party_of(n) > 1 for n in names):
        # A party run is N clients already: run them one at a time.
        # --name gives ONE party test id its own run name (raid seam21:
        # tools/raid_gate/party_repeat.py, scratch repeats beside the kept
        # artefact). The name seeds the run (its accounts), and a renamed run
        # is not the test's evidence, so it never publishes.
        if arguments.name and (arguments.all or len(names) != 1):
            parser.error("--name renames one run: give one test id, not --all")
        if arguments.name and not arguments.no_publish:
            parser.error("--name with a test id needs --no-publish (a renamed run is a scratch)")
        results = []
        for n in names:
            quest_file = quest_list.quest_path(REPO_ROOT, n)
            if party_of(n) > 1:
                results.append(run_party(arguments.name or n, quest_file,
                                         read_fixture_name(quest_file), binary,
                                         manifest_path, arguments.timeout, party_of(n), False))
            else:
                results.append(run_quest(n, binary, manifest_path, arguments.timeout))
    elif arguments.jobs == 1:
        results = [run_quest(n, binary, manifest_path, arguments.timeout) for n in names]
    else:
        with ThreadPoolExecutor(max_workers=arguments.jobs) as pool:
            results = list(pool.map(
                lambda n: run_quest(n, binary, manifest_path, arguments.timeout), names))

    print_report(results)
    print_failure_block(results)
    if not arguments.no_publish:
        for r in results:
            target, detail = publish(r)
            if target:
                print("run.py: published %s -> %s (%d shot(s) + ledger.tsv)"
                      % (r["name"], os.path.relpath(target, REPO_ROOT), detail), flush=True)
            else:
                print("run.py: not published %s (%s); the previously published set, if any, "
                      "stands" % (r["name"], detail), flush=True)
    failed = [r["name"] for r in results if not r["ok"]]
    if failed:
        print("run.py: %d of %d quest process(es) did not complete cleanly: %s"
              % (len(failed), len(results), ", ".join(failed)), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(detach_finish(main()))
