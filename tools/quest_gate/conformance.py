#!/usr/bin/env python3
"""Run the verb conformance harness and score it, one row per verb.

This is the gate that replaces "it compiles".  It builds its own binary into
its own objdir, ensures the script pack (the server refuses to boot on a stale
one), rewrites the manifest to the embedded transport, runs
test/quests/_conformance.lua headlessly in a private session, prints the
per-verb table, and exits 0 only when EVERY verb passed.

A harness that returns 0 with failures is worse than none, so every one of
these is non-zero:
  * the build, the pack, or the client process failing,
  * verb_list.py --check disagreeing with the harness,
  * no ledger, or a ledger with no SUMMARY row,
  * any verb row that is not PASS,
  * any verb with no row at all (the run aborted before reaching it),
  * t.finish's row without an exit=0 SUMMARY behind it,
  * t.note's row without its probe folded into the detail.

Usage:
  tools/quest_gate/conformance.py [--label truth] [--keep] [--session DIR]
"""

import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import verb_list  # noqa: E402

HARNESS = os.path.join(REPO_ROOT, "test", "quests", "_conformance.lua")
SKIP_MARKER = "--@skip"
MAX_ATTEMPTS = 12
FIXTURE = os.path.join(REPO_ROOT, "test", "quests", "fixtures", "fresh_lumbridge.ini")
NOTE_PROBE = "CONFORMANCE_NOTE_PROBE"
USER = "qdconform"
MAX_FRAMES = "40000"


def run(command, **kwargs):
    print("+ " + " ".join(command), flush=True)
    return subprocess.call(command, **kwargs)


def build(label):
    """Build into THIS gate's own objdir, and never seed it from another one.

    Seeding from a warm objdir was tried and it poisoned the answer: `cp -Rp`
    preserves mtimes, GNU Make 3.81 here compares whole seconds, and make then
    declared 465 of 540 copied objects fresh.  The binary that came out ran a
    client whose npc pool stayed empty, whose backpack never synced and whose
    tile never moved -- a fabricated R-B "reproduction" that a clean build does
    not show.  That is CLAUDE.md's "if a build ever disagrees with its source,
    delete the object and rebuild", and the whole point of this gate is that
    its answer is real, so the first build here is a cold one.
    """
    target = "torirs_qd_%s" % label
    code = run(["make", "-C", os.path.join(REPO_ROOT, "src"), "OPT=1", "EMBED_SERVER=1",
                "PLATFORM_OBJ_BASE=build_qd_%s" % label,
                "PLATFORM_TARGET=%s" % target, target])
    return code, os.path.join(REPO_ROOT, "src", target)


def manifest():
    """The shipped manifest points at a sparse JS5 cache this tree does not
    have; the content test rewrites it the same way (tools/content_selftest.py).

    It is written INTO manifests/, not into the run's own directory, because
    the manifest's own directory is load-bearing: the same rewritten file, run
    from the session dir instead, boots a client where ::objbox and the cook's
    quest-start mount nothing at all -- four chat verbs flip from PASS to
    not_visible and nothing in the log says why. Measured A/B, 2026-09-19:
    manifests/ 50/78, session dir 46/78. The name is a dotfile this gate owns
    and rewrites every run; `dir=` inside it is absolute, so two sessions
    racing it write the same bytes.""" 
    source = os.path.join(REPO_ROOT, "manifests", "manifest_osrs239.ini")
    out = os.path.join(REPO_ROOT, "manifests", ".conformance.ini")
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


def session(directory):
    saves = os.path.join(directory, "saves")
    os.makedirs(saves, exist_ok=True)
    with open(FIXTURE, "r", encoding="utf-8") as handle:
        save = handle.read()
    save = save.replace("name = fresh_lumbridge", "name = %s" % USER)
    with open(os.path.join(saves, "%s.ini" % USER), "w", encoding="utf-8") as handle:
        handle.write(save)
    return saves


def harness_with_skips(skips, out_path):
    """A copy of the harness with its SKIP table filled in.

    There is no pcall in the plugin sandbox, so a verb that RAISES ends the
    run where it stands and every verb after it never gets called.  Rather
    than score 25 verbs "no row", the runner attributes the error to the first
    verb with no row, marks it, and re-runs from the top: the aborting verb
    costs its own row and nobody else's.  The generated copy is kept beside
    the attempt's ledger because it is the exact script that produced it."""
    entries = ", ".join('["%s"] = true' % name for name in sorted(skips))
    table = "        local SKIP = {%s} %s\n" % (
        (" " + entries + " ") if entries else "", SKIP_MARKER)
    replaced = False
    with open(HARNESS, "r", encoding="utf-8") as handle:
        lines = handle.readlines()
    for index, line in enumerate(lines):
        if SKIP_MARKER in line and "local SKIP" in line:
            lines[index] = table
            replaced = True
            break
    assert replaced, "test/quests/_conformance.lua lost its %s line" % SKIP_MARKER
    with open(out_path, "w", encoding="utf-8") as handle:
        handle.writelines(lines)
    return out_path


def client(binary, manifest_path, directory, saves, log_path, script):
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
        # silently loads no driver at all, which reads exactly like a dead one.
        "TORIRS_PLUGIN_MANIFEST": "plugins/quest_driver.ini",
        "TORIRS_PLUGIN_PREFS": os.path.join(directory, "plugin_prefs.ini"),
        "TORIRS_PREFS": "",
        "TORIRSSERVER_SAVES": saves,
        "TORIRSSERVER_STAFF_LEVEL": "2",
        "TORIRSSERVER_HOME": "3222,3218",
        "TORIRS_MAX_FRAMES": MAX_FRAMES,
        "TORIRS_EMBED_CLOCK_MS": "20",
    })
    command = [binary, "--manifest", manifest_path, "--user", USER, "--pass", "test",
               "--soft3d", "--window", "765x503"]
    print("+ " + " ".join(command), flush=True)
    with open(log_path, "wb") as log:
        # cwd is the repo root, ALWAYS: TORIRS_PLUGIN_MANIFEST resolves under
        # script/ relative to the working directory, so running this from
        # src/ (which is what `make -C src test-quest-conformance` does) finds
        # no quest_driver.ini, silently loads the default plugin set instead,
        # and the run reads exactly like a dead driver -- no QUEST line, no
        # ledger, nothing to say why.
        return subprocess.call(command, env=environment, cwd=REPO_ROOT,
                               stdout=log, stderr=subprocess.STDOUT)


def read_ledger(path):
    """(rows, summary).  A row is (index, step, verdict, ticks, shots, detail)."""
    if not os.path.isfile(path):
        return [], None
    rows = []
    summary = None
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.rstrip("\n")
            if not line or line == "quest-ledger-v1" or line.startswith("index\t"):
                continue
            fields = line.split("\t")
            if fields[0] == "SUMMARY":
                summary = fields
                continue
            while len(fields) < 6:
                fields.append("")
            rows.append(fields)
    return rows, summary


def script_error(rows):
    """The message of the row the scheduler wrote when the coroutine raised."""
    for row in rows:
        if row[1] == "script-error":
            return row[5]
    return None


def score(order, collected, summary, exit_code):
    """One result line per verb, in the harness's own plan order, re-graded
    against the evidence a Lua coroutine cannot see."""
    results = []
    unreached = False
    for name in order:
        row = collected.get(name)
        if row is None:
            results.append((name, "no-row", "never reached: the run ended before this verb"))
            unreached = True
            continue
        verdict, detail = row[0], row[1]
        if name == "note" and verdict != "ERROR" and NOTE_PROBE not in detail:
            verdict = "FAIL"
            detail = "t.note did not fold %s into the next row's detail -- %s" % (
                NOTE_PROBE, detail)
        if name == "t.finish" and verdict != "ERROR":
            clean = summary is not None and "exit=0" in summary[4] and exit_code == 0
            if not clean:
                verdict = "FAIL"
                detail = "no exit=0 SUMMARY behind the row (summary=%s, process exit=%d)" % (
                    summary[4] if summary else "none", exit_code)
        results.append((name, verdict, detail))
    return results, unreached


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label", default="conform",
                        help="private objdir/target suffix (build_qd_<label>_*, torirs_qd_<label>)")
    parser.add_argument("--session", default=None,
                        help="session directory (default build/quest_gate/_conformance)")
    parser.add_argument("--keep", action="store_true", help="keep a previous session directory")
    parser.add_argument("--no-build", action="store_true", help="use the binary already built")
    arguments = parser.parse_args()

    if run([sys.executable, os.path.join(HERE, "verb_list.py"), "--check"]) != 0:
        print("conformance: the harness and the driver disagree about the verb set",
              file=sys.stderr)
        return 1

    binary = os.path.join(REPO_ROOT, "src", "torirs_qd_%s" % arguments.label)
    if not arguments.no_build:
        code, binary = build(arguments.label)
        if code != 0:
            print("conformance: build failed", file=sys.stderr)
            return code
    if not os.path.isfile(binary):
        print("conformance: no binary at %s" % binary, file=sys.stderr)
        return 1

    # The server REFUSES to boot on a stale pack, and this tree carries
    # uncommitted content, so the pack is rebuilt every run.
    code = run(["make", "-C", os.path.join(REPO_ROOT, "src"), "torirsserver-scripts"])
    if code != 0:
        print("conformance: the script pack did not build", file=sys.stderr)
        return code

    root = arguments.session or os.path.join(REPO_ROOT, "build", "quest_gate", "_conformance")
    if os.path.isdir(root) and not arguments.keep:
        shutil.rmtree(root)
    os.makedirs(root, exist_ok=True)
    manifest_path = manifest()

    order = verb_list.verbs_from_harness()
    collected = {}          # verb -> (verdict, detail)
    skips = set()
    summary = None
    exit_code = 1
    attempts = []

    for attempt in range(1, MAX_ATTEMPTS + 1):
        directory = os.path.join(root, "attempt-%02d" % attempt)
        os.makedirs(directory, exist_ok=True)
        saves = session(directory)
        script = harness_with_skips(skips, os.path.join(directory, "_conformance.lua"))
        log_path = os.path.join(directory, "log.txt")
        exit_code = client(binary, manifest_path, directory, saves, log_path, script)
        rows, summary = read_ledger(os.path.join(directory, "ledger.tsv"))
        attempts.append((attempt, directory, exit_code, len(rows)))

        for row in rows:
            if row[1] in ("script-error", "conformance-plan"):
                continue
            collected.setdefault(row[1], (row[2], row[5]))

        if not rows:
            print("conformance: attempt %d wrote no ledger row at all -- the driver "
                  "never ran; see %s" % (attempt, log_path), file=sys.stderr)
            break

        remaining = [name for name in order if name not in collected]
        if not remaining:
            break

        # The first verb still without a row is the one the run died on.
        blocked = remaining[0]
        error = script_error(rows)
        if error:
            collected[blocked] = ("ERROR", "raised instead of answering: %s" % error)
        else:
            collected[blocked] = ("ERROR",
                "the run ended here with no script-error row (frame cap, or the "
                "client exited %d) -- see %s" % (exit_code, log_path))
        skips.add(blocked)
        print("conformance: attempt %d stopped at %s; re-running with it skipped"
              % (attempt, blocked), flush=True)
    else:
        print("conformance: gave up after %d attempts" % MAX_ATTEMPTS, file=sys.stderr)

    results, unreached = score(order, collected, summary, exit_code)

    width = max(len(name) for name in order)
    print("")
    print("verb conformance -- %d attempt(s) under %s" % (len(attempts), root))
    print("%-*s  %-8s  %s" % (width, "verb", "result", "detail"))
    print("%s  %s  %s" % ("-" * width, "-" * 8, "-" * 40))
    passed = 0
    for name, verdict, detail in results:
        if verdict == "PASS":
            passed += 1
        print("%-*s  %-8s  %s" % (width, name, verdict, detail[:160]))
    print("")
    for attempt, directory, code, count in attempts:
        print("attempt %d: exit %d, %d ledger row(s), %s" % (attempt, code, count, directory))
    print("summary: %s" % ("\t".join(summary) if summary else "NONE"))
    print("verbs:   %d/%d PASS" % (passed, len(order)))
    print("")

    if not collected:
        print("conformance: no ledger rows at all -- see the attempt logs", file=sys.stderr)
        return 1
    if unreached:
        print("conformance: %d verb(s) never got a row"
              % sum(1 for _, verdict, _ in results if verdict == "no-row"), file=sys.stderr)
        return 1
    if passed != len(order):
        print("conformance: %d of %d verbs failed" % (len(order) - passed, len(order)),
              file=sys.stderr)
        return 1
    if summary is None:
        print("conformance: the last attempt wrote no SUMMARY row", file=sys.stderr)
        return 1
    if exit_code != 0:
        print("conformance: the client exited %d" % exit_code, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
