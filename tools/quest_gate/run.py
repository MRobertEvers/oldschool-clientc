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
    at once) and rebuilds the server script pack, which the embedded server
    refuses to boot on when stale;
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
shots/NN-name.png, client.log) because that IS the TORIRS_CONTENT_TEST
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
  tools/quest_gate/run.py <quest> [--timeout SECONDS]
  tools/quest_gate/run.py --script test/quests/_conformance.lua --name proof
      (advanced: drives an arbitrary standalone driver script -- no quest
      table, no setup cheats -- through this same build/env/launch path.
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
DEFAULT_TIMEOUT = 400
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

QUEUE_TSV_PATH = os.path.join(REPO_ROOT, "test", "quests", "QUEUE.tsv")


def quest_dir_for(test_id):
    """test_id -> QUEUE.tsv's quest_dir column for that row, or
    `quest_<test_id>` when the id has no row at all (hans, which predates
    QUEUE.tsv and is not in it) -- the same fallback
    docs/quests/selftest layout uses."""
    assert test_id
    rows = quest_queue_tsv.load_rows(QUEUE_TSV_PATH)
    row = quest_queue_tsv.find_row(rows, test_id)
    if row and row.get("quest_dir"):
        return row["quest_dir"]
    return "quest_%s" % test_id

FIXTURE_RE = re.compile(r'fixture\s*=\s*"([^"]+)"')
MAX_FRAMES_RE = quest_list.MAX_FRAMES_RE
NAME_LINE_RE = re.compile(r"(?m)^name\s*=.*$")


def run(command, **kwargs):
    print("+ " + " ".join(command), flush=True)
    return subprocess.call(command, **kwargs)


def build(warm_from):
    return build_support.build(REPO_ROOT, OBJ_BASE, TARGET, warm_from=warm_from, run=run)


def ensure_scripts():
    """The embedded server refuses to boot on a stale script pack, and this
    tree routinely carries uncommitted OSRS-Content edits, so it is rebuilt
    unconditionally, every run (conformance.py does the same)."""
    return run(["make", "-C", os.path.join(REPO_ROOT, "src"), "torirsserver-scripts"])


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
    fixture_path = os.path.join(REPO_ROOT, "test", "quests", "fixtures", fixture_name)
    assert os.path.isfile(fixture_path), fixture_path
    with open(fixture_path, "r", encoding="utf-8") as handle:
        text = handle.read()
    text, count = NAME_LINE_RE.subn("name = %s" % user, text, count=1)
    assert count == 1, "%s: no `name = ...` line to rewrite" % fixture_path
    os.makedirs(saves_dir, exist_ok=True)
    with open(os.path.join(saves_dir, "%s.ini" % user), "w", encoding="utf-8") as handle:
        handle.write(text)


def write_wrapper_script(quest_file, out_path):
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
    """
    with open(quest_file, "r", encoding="utf-8") as handle:
        source = handle.read()
    wrapper = (
        "local QUEST = (function()\n"
        + source +
        "\nend)()\n"
        "if type(QUEST) ~= \"table\" or type(QUEST.run) ~= \"function\" then\n"
        "    error(\"quest file did not return { run = function(t) ... end }\")\n"
        "end\n"
        "local quest_setup = QUEST.setup\n"
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
        "            local setup_result, setup_detail = t.cheat(cheat)\n"
        "            if setup_result ~= \"ok\" then\n"
        "                setup_failed(cheat, \"setup cheat answered \"\n"
        "                    .. tostring(setup_result)\n"
        "                    .. \" (\" .. tostring(setup_detail) .. \")\")\n"
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
        "        -- Every OTHER cheat (::setvar, ::setlevel, a quest's own reset\n"
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


def client_env(directory, saves, script, max_frames=int(DEFAULT_MAX_FRAMES)):
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
    })
    return environment


# The account every quest client logs in as is the run's own name -- the
# same name as its session directory, build/quest_gate/<name>/ -- and this
# password. script/plugins/quest_driver/session.lua's t.session.login() (the
# relog half of the guide's "or log out" steps) types exactly these two into
# the title screen: the name as the last component of api_drive.session().dir,
# the password as QD.session.PASSWORD. Change one, change the other.
QUEST_PASSWORD = "test"


def launch_client(binary, manifest_path, user, directory, saves, script, log_path, timeout,
                  max_frames=int(DEFAULT_MAX_FRAMES)):
    """One client process, killed (whole process group) if it outlives
    `timeout` seconds of WALL-CLOCK time -- independent of
    TORIRS_MAX_FRAMES, which only bounds the virtual clock and cannot catch
    a real hang. Returns (exit_code_or_None, timed_out)."""
    command = [binary, "--manifest", manifest_path, "--user", user, "--pass", QUEST_PASSWORD,
               "--soft3d", "--window", "765x503"]
    environment = client_env(directory, saves, script, max_frames)
    print("+ " + " ".join(command), flush=True)
    if max_frames != int(DEFAULT_MAX_FRAMES):
        print("run.py: %s declares max_frames = %d (wall-clock timeout %d s)"
              % (user, max_frames, timeout), flush=True)
    with open(log_path, "wb") as log:
        # cwd is the repo root, ALWAYS: TORIRS_PLUGIN_MANIFEST resolves under
        # script/ relative to the working directory, not to the binary.
        process = subprocess.Popen(command, env=environment, cwd=REPO_ROOT,
                                    stdout=log, stderr=subprocess.STDOUT,
                                    start_new_session=True)
        try:
            code = process.wait(timeout=timeout)
            return code, False
        except subprocess.TimeoutExpired:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except OSError:
                pass
            process.wait()
            return None, True


# ---------------------------------------------------------------- session lock
#
# build/quest_gate/<quest>/ is not one run's output directory, it is THE
# directory for that quest id: TORIRS_CONTENT_TEST itself, where the driver
# writes ledger.tsv, shots/NN-name.png and client.log, and which
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


def prepare_session(name, fixture_name):
    """A fresh, private build/quest_gate/<name>/ directory -- never reused;
    the server writes into it on exit."""
    directory = os.path.join(REPO_ROOT, "build", "quest_gate", name)
    if os.path.isdir(directory):
        shutil.rmtree(directory)
    os.makedirs(directory, exist_ok=True)
    saves = os.path.join(directory, "saves")
    write_session_fixture(fixture_name, saves, name)
    return directory, saves


def copy_timeout_shot(directory):
    """On a wall-clock timeout, the driver's own LAST shot (if it took any
    at all) is copied to <directory>/TIMEOUT.png -- run.py's own exit code
    already says the process did not finish cleanly, but a human (or
    print_failure_block, below) reading build/quest_gate/<quest>/ afterward
    should not have to re-run the quest just to see what the screen looked
    like when it hung. Shots are named "NN-name.png" (core.lua:
    string.format("%02d-%s", ...)), so a plain filename sort is also
    chronological. None (no copy made) when the run never got far enough to
    take even one shot -- a timeout that early has nothing to show."""
    assert directory
    shots_dir = os.path.join(directory, "shots")
    if not os.path.isdir(shots_dir):
        return None
    pngs = sorted(entry for entry in os.listdir(shots_dir) if entry.endswith(".png"))
    if not pngs:
        return None
    target = os.path.join(directory, "TIMEOUT.png")
    shutil.copy2(os.path.join(shots_dir, pngs[-1]), target)
    return target


def launch_and_report(name, binary, manifest_path, directory, saves, script, timeout,
                      max_frames):
    log_path = os.path.join(directory, "client.log")
    code, timed_out = launch_client(binary, manifest_path, name, directory, saves,
                                     script, log_path, scaled_timeout(timeout, max_frames),
                                     max_frames)
    if timed_out:
        copy_timeout_shot(directory)
    ledger_path = os.path.join(directory, "ledger.tsv")
    has_ledger = os.path.isfile(ledger_path)
    ok = (not timed_out) and code == 0 and has_ledger
    return {
        "name": name, "exit_code": code, "timed_out": timed_out,
        "has_ledger": has_ledger, "directory": directory, "ok": ok,
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
        directory, saves = prepare_session(name, fixture_name)
        script = os.path.join(directory, "%s.lua" % name)
        write_wrapper_script(quest_file, script)
        return launch_and_report(name, binary, manifest_path, directory, saves, script,
                                 timeout, max_frames)
    finally:
        release_session_lock(name)


def run_script_direct(name, script_path, fixture_name, binary, manifest_path, timeout):
    """The --script escape hatch: runs `script_path` as-is -- no setup-cheat
    wrapping -- under build/quest_gate/<name>/. test/quests/_conformance.lua
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
        directory, saves = prepare_session(name, fixture_name)
        return launch_and_report(name, binary, manifest_path, directory, saves, script_path,
                                 timeout, max_frames)
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
    verdict = ledger_verdict(ledger_path)
    if verdict != "PASS":
        return None, "ledger SUMMARY is %s, not PASS" % verdict
    target = os.path.join(PUBLISH_DIR, quest_dir_for(result["name"]), "play")
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
            "yes" if r["timed_out"] else "no",
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
            print("    shot: %s" % (shot_path if shot_path else "none captured"))
        elif r["timed_out"]:
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


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("quest", nargs="?", help="run one quest: test/quests/<quest>.lua")
    parser.add_argument("--quest", dest="quest_flag", default=None, help=argparse.SUPPRESS)
    parser.add_argument("--all", action="store_true", help="run every discovered quest")
    parser.add_argument("--jobs", type=int, default=1,
                        help="quest client processes to run in parallel (default 1)")
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT,
                        help="wall-clock ceiling per quest process, in seconds "
                             "(default %d) -- a hung run fails, it never hangs the suite; "
                             "scaled up by a quest's own max_frames / %s"
                             % (DEFAULT_TIMEOUT, DEFAULT_MAX_FRAMES))
    parser.add_argument("--warm-from", default="auto",
                        help="seed a not-yet-existing build_questtest objdir from this "
                             "directory, or 'auto' for the freshest sibling *_opt_es objdir "
                             "(default: auto; see build_support.py for why this is safe)")
    parser.add_argument("--no-warm", action="store_true",
                        help="always build cold; overrides --warm-from")
    parser.add_argument("--no-build", action="store_true", help="use the binary already built")
    parser.add_argument("--no-publish", action="store_true",
                        help="do not copy a PASSING quest's ledger and shots into "
                             "OSRS-Content (%s/<quest_dir>/play/); by default every "
                             "quest run does"
                             % os.path.relpath(PUBLISH_DIR, REPO_ROOT))
    parser.add_argument("--script", default=None,
                        help="advanced: run this .lua file directly as a single session "
                             "(no quest-table wrapping, no setup cheats) -- see the module "
                             "docstring")
    parser.add_argument("--name", default=None,
                        help="artefact directory name for --script (default: its basename)")
    parser.add_argument("--fixture", default=DEFAULT_FIXTURE,
                        help="fixture .ini for --script (default %s)" % DEFAULT_FIXTURE)
    arguments = parser.parse_args()

    name = arguments.quest_flag or arguments.quest
    if not arguments.script and not arguments.all and not name:
        parser.error("give a quest name, --all, or --script")
    if arguments.jobs < 1:
        parser.error("--jobs must be at least 1")

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

    code = ensure_scripts()
    if code != 0:
        print("run.py: the script pack did not build", file=sys.stderr)
        return code

    manifest_path = write_manifest()

    if arguments.script:
        script_path = os.path.abspath(arguments.script)
        script_name = arguments.name or os.path.splitext(os.path.basename(script_path))[0]
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
            print("run.py: no quest files under test/quests/ -- nothing to run "
                  "(this is a discovery fact, not a pass)", file=sys.stderr)
            return 1
    else:
        quest_file = quest_list.quest_path(REPO_ROOT, name)
        if not os.path.isfile(quest_file):
            print("run.py: no such quest file %s" % quest_file, file=sys.stderr)
            return 1
        names = [name]

    if arguments.jobs == 1:
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
    sys.exit(main())
