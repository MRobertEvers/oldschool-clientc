#!/usr/bin/env python3
"""Play one raid test under several account names and pass only if every name is green.

The account name seeds the server's random numbers (its first 12 characters).
A room authored and kept under one name has been tested on one roll of the
dice: on 2026-10-05 the six kept ToB Entry rooms, replayed unchanged under four
other names, were green on 7 of 24 runs (docs/minigames/raid_loop/
SEED_SURVEY_2026-10-05.md).  A room is KEPT only when this tool exits 0.

    python3 tools/raid_gate/seed_survey.py tob_bloat
    python3 tools/raid_gate/seed_survey.py _play_smoke --names 5 --party 3

It runs the test's own name first, then the fixed other names (the prefixes
below, so two surveys are comparable), one run at a time, each through
tools/raid_gate/run.py (fresh private directory, fixture copied, virtual
clock, headless).  A red name is read from its tick log by raid_report.py
and its first three MISTAKES are printed (tick and raider each: a missed
attack, a prayer, a hazard, a food, a stall, a death -- raid_report.py's
mistakes block): nothing is replayed or watched.  Results:
build/seed_survey/<id>/results.tsv.
"""

import argparse
import csv
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import raid_report  # noqa: E402

OTHER_NAME_PREFIXES = ["sva", "svb", "svc", "svd", "sve", "svf", "svg", "svh", "svi"]
ACCOUNT_NAME_LIMIT = 12


def other_name(prefix, test_id):
    stem = re.sub(r"[^a-z0-9]", "", test_id.lower().replace("tob_", ""))
    return (prefix + stem)[:ACCOUNT_NAME_LIMIT]


def read_ledger(run_directory):
    path = os.path.join(run_directory, "ledger.tsv")
    if not os.path.isfile(path):
        return "NO LEDGER", 0, ["run.no_ledger"]
    verdict, rows, failing = "NO SUMMARY", 0, []
    with open(path, newline="") as handle:
        for record in csv.reader(handle, delimiter="\t"):
            if record and record[0] == "SUMMARY":
                rows = int(record[1]) if len(record) > 1 and record[1].isdigit() else 0
                verdict = record[2] if len(record) > 2 else "NO SUMMARY"
            elif len(record) > 2 and record[2] in ("FAIL", "BLOCKED"):
                failing.append(record[1])
    return verdict, rows, failing


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("test_id", help="a test under test/raids/ (its id, without .lua)")
    parser.add_argument("--names", type=int, default=5, help="how many names in all, the test's own included")
    parser.add_argument("--party", type=int, default=0, help="party size passed to run.py (the name is the leader's)")
    parser.add_argument("--skip-own", action="store_true", help="do not re-run the test's own name")
    arguments = parser.parse_args()
    assert 2 <= arguments.names <= len(OTHER_NAME_PREFIXES) + 1, "--names must be 2..%d" % (len(OTHER_NAME_PREFIXES) + 1)

    source = os.path.join(ROOT, "test", "raids", arguments.test_id + ".lua")
    assert os.path.isfile(source), "no such raid test: %s" % source
    survey_directory = os.path.join(ROOT, "build", "seed_survey", arguments.test_id)
    os.makedirs(survey_directory, exist_ok=True)
    script_copy = os.path.join(survey_directory, arguments.test_id + ".lua")
    shutil.copyfile(source, script_copy)

    runner = [sys.executable, os.path.join(HERE, "run.py")]
    common = ["--no-build", "--no-publish"] + (["--party", str(arguments.party)] if arguments.party else [])
    plan = []
    if not arguments.skip_own:
        plan.append((arguments.test_id, runner + [arguments.test_id] + common))
    for prefix in OTHER_NAME_PREFIXES[: arguments.names - 1]:
        name = other_name(prefix, arguments.test_id)
        plan.append((name, runner + ["--script", script_copy, "--name", name] + common))

    results = []
    for name, command in plan:
        log_path = os.path.join(survey_directory, name + ".log")
        with open(log_path, "w") as log:
            subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        run_directory = os.path.join(ROOT, "build", "quest_gate", name)
        verdict, rows, failing = read_ledger(run_directory)
        reading = ""
        mistakes = []
        if verdict != "PASS" and os.path.isfile(os.path.join(run_directory, "ticklog.tsv")):
            report, _ = raid_report.analyse(run_directory)
            mistakes = ["p%d %s %s" % (pid, kind, text) for _, pid, kind, text in report.get("mistakes", [])[:3]]
            if not report.get("empty"):
                reading = "ticks %d, dealt %d in %d hits, took %d, about %d ticks with no attack, npc deaths %d" % (
                    report["last_tick"] - report["first_tick"] + 1,
                    report["damage_dealt"],
                    report["hits_dealt"],
                    report["damage_taken"],
                    report.get("ticks_lost", 0),
                    len(report["deaths"]),
                )
        results.append((name, verdict, rows, failing, reading, mistakes))
        print("%-13s %-10s rows %-4d %s" % (name, verdict, rows, ", ".join(failing[:4]) + (" ..." if len(failing) > 4 else "")))
        if reading:
            print("              log: %s" % reading)
        for line in mistakes:
            print("              mistake: %s" % line[:200])
        sys.stdout.flush()

    with open(os.path.join(survey_directory, "results.tsv"), "w", newline="") as handle:
        writer = csv.writer(handle, delimiter="\t")
        writer.writerow(["name", "verdict", "rows", "failing_rows", "log_reading", "first_mistakes"])
        for name, verdict, rows, failing, reading, mistakes in results:
            writer.writerow([name, verdict, rows, ",".join(failing), reading, " | ".join(mistakes)])

    green = sum(1 for result in results if result[1] == "PASS")
    print("seed survey %s: %d of %d names green -> %s" % (arguments.test_id, green, len(results), "KEEP" if green == len(results) else "NOT KEPT"))
    return 0 if green == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
