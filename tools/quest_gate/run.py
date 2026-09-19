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

import argparse
import os
import re
import shutil
import signal
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import build_support  # noqa: E402
import ledger  # noqa: E402
import quest_list  # noqa: E402

OBJ_BASE = "build_questtest"
TARGET = "torirs_questtest"
DEFAULT_MAX_FRAMES = "60000"
DEFAULT_TIMEOUT = 180
DEFAULT_FIXTURE = "fresh_lumbridge.ini"

# Where a PASSING quest's evidence is kept. build/quest_gate/<quest>/ is
# deleted on every run, so a screenshot there lives exactly until the next
# run; this directory is inside the OSRS-Content submodule, beside the
# per-quest `<quest_dir>/*.bmp` sets the content audits already commit
# (server/scripts/selftest/quest_cook/01_talk_cook.bmp, ...), so the shots
# a quest test took are versioned with the content they photograph.
PUBLISH_DIR = os.path.join(REPO_ROOT, "OSRS-Content", "osrs239-content", "server", "scripts",
                           "selftest", "quest_tests")

FIXTURE_RE = re.compile(r'fixture\s*=\s*"([^"]+)"')
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
        "    if type(quest_setup) == \"table\" then\n"
        "        for _, cheat in ipairs(quest_setup) do\n"
        "            local setup_result, setup_detail = t.cheat(cheat)\n"
        "            if setup_result ~= \"ok\" then\n"
        "                t.step(\"setup.\" .. cheat, \"FAIL\",\n"
        "                    \"setup cheat answered \" .. tostring(setup_result)\n"
        "                        .. \" (\" .. tostring(setup_detail) .. \")\"\n"
        "                        .. \" -- the world this quest assumes was never stated\")\n"
        "                t.finish(1)\n"
        "                return\n"
        "            end\n"
        "        end\n"
        "    end\n"
        "    return quest_run(t)\n"
        "end\n"
        "return QUEST\n"
    )
    with open(out_path, "w", encoding="utf-8") as handle:
        handle.write(wrapper)


def client_env(directory, saves, script):
    environment = dict(os.environ)
    environment.update({
        "SDL_VIDEODRIVER": "dummy",
        "SDL_AUDIODRIVER": "dummy",
        "TORIRS_STDERR_UNBUFFERED": "1",
        "TORIRS_PLUGINS": "1",
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
        "TORIRS_MAX_FRAMES": DEFAULT_MAX_FRAMES,
        "TORIRS_EMBED_CLOCK_MS": "20",
    })
    return environment


def launch_client(binary, manifest_path, user, directory, saves, script, log_path, timeout):
    """One client process, killed (whole process group) if it outlives
    `timeout` seconds of WALL-CLOCK time -- independent of
    TORIRS_MAX_FRAMES, which only bounds the virtual clock and cannot catch
    a real hang. Returns (exit_code_or_None, timed_out)."""
    command = [binary, "--manifest", manifest_path, "--user", user, "--pass", "test",
               "--soft3d", "--window", "765x503"]
    environment = client_env(directory, saves, script)
    print("+ " + " ".join(command), flush=True)
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


def launch_and_report(name, binary, manifest_path, directory, saves, script, timeout):
    log_path = os.path.join(directory, "client.log")
    code, timed_out = launch_client(binary, manifest_path, name, directory, saves,
                                     script, log_path, timeout)
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
    directory, saves = prepare_session(name, fixture_name)
    script = os.path.join(directory, "%s.lua" % name)
    write_wrapper_script(quest_file, script)
    return launch_and_report(name, binary, manifest_path, directory, saves, script, timeout)


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
    directory, saves = prepare_session(name, fixture_name)
    return launch_and_report(name, binary, manifest_path, directory, saves, script_path, timeout)


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
    PUBLISH_DIR/<quest>/, replacing whatever an earlier run published there.

    Only a PASS is published: the directory is the persisted evidence that
    the quest played through, and a red run overwriting a green set would
    erase the evidence rather than add to it. A FAIL leaves the previous
    published set untouched and says so. Returns (path, shot_count) or
    None when nothing was published and why in the second slot."""
    if not result["has_ledger"]:
        return None, "no ledger"
    ledger_path = os.path.join(result["directory"], "ledger.tsv")
    verdict = ledger_verdict(ledger_path)
    if verdict != "PASS":
        return None, "ledger SUMMARY is %s, not PASS" % verdict
    target = os.path.join(PUBLISH_DIR, result["name"])
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
            "none" if r["exit_code"] is None else str(r["exit_code"]),
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
                             "(default %d) -- a hung run fails, it never hangs the suite"
                             % DEFAULT_TIMEOUT)
    parser.add_argument("--warm-from", default="auto",
                        help="seed a not-yet-existing build_questtest objdir from this "
                             "directory, or 'auto' for the freshest sibling *_opt_es objdir "
                             "(default: auto; see build_support.py for why this is safe)")
    parser.add_argument("--no-warm", action="store_true",
                        help="always build cold; overrides --warm-from")
    parser.add_argument("--no-build", action="store_true", help="use the binary already built")
    parser.add_argument("--no-publish", action="store_true",
                        help="do not copy a PASSING quest's ledger and shots into "
                             "OSRS-Content (%s); by default every quest run does"
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
