#!/usr/bin/env python3
"""Turn a quest run's artefacts into a verdict.

Red on, and only on, things that mean the test did not actually happen:

  * any row in ledger.tsv whose verdict is not PASS (a FAIL, or anything a
    quest script's own error path wrote),
  * a missing ledger, or one whose summary row disagrees with its rows,
  * a listed quest with no ledger at all -- run.py always creates
    build/quest_gate/<quest>/ before it launches a client, so this also
    catches a process that never got that far,
  * a step that claims a shot (its `shots` column) that is not on disk,
  * a shot that is on disk but undersized (a capture that raced the
    renderer -- less than 1000 bytes),
  * two shots with the same MD5, anywhere in one quest's shots/ directory --
    the failure that looks most like a pass: a screenshot per interaction,
    all of them identical, is a driver that never drove anything.

Reads build/quest_gate/<quest>/ for every quest `run.py` would run (see
tools/quest_gate/quest_list.py) unless told to check only specific ones.

Usage:
  tools/quest_gate/gate.py [--all]            (default: --all)
  tools/quest_gate/gate.py <quest> [<quest> ...]
"""

import argparse
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import ledger  # noqa: E402
import quest_list  # noqa: E402

MIN_SHOT_BYTES = 1000


def artefact_dir(name):
    assert name
    return os.path.join(REPO_ROOT, "build", "quest_gate", name)


def parse_summary_counts(summary):
    """("pass=<n> fail=<m>" -> (n, m), or (None, None) if it cannot be
    read -- treated as its own disagreement, never as a pass by default."""
    if summary is None or len(summary) < 6:
        return None, None
    text = summary[5]
    counts = {}
    for token in text.split():
        key, _, value = token.partition("=")
        if key in ("pass", "fail"):
            try:
                counts[key] = int(value)
            except ValueError:
                return None, None
    return counts.get("pass"), counts.get("fail")


def md5_of(path):
    digest = hashlib.md5()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def check_quest(name):
    """Every finding for one quest, as plain strings. Empty means green."""
    findings = []
    directory = artefact_dir(name)
    ledger_path = os.path.join(directory, "ledger.tsv")
    rows, summary = ledger.read(ledger_path)

    if rows is None:
        findings.append("no ledger.tsv at %s" % ledger_path)
        return findings

    if not rows:
        findings.append("ledger.tsv has no step rows")

    for row in rows:
        if row["verdict"] != "PASS":
            findings.append("step %r: verdict %s -- %s" % (
                row["step"], row["verdict"], row["detail"]))

    if summary is None:
        findings.append("ledger.tsv has no trailing SUMMARY row")
    else:
        counted_pass = sum(1 for row in rows if row["verdict"] == "PASS")
        counted_fail = len(rows) - counted_pass
        summary_pass, summary_fail = parse_summary_counts(summary)
        if summary_pass is None or summary_fail is None:
            findings.append("SUMMARY row's pass/fail counts could not be read: %s"
                             % "\t".join(summary))
        elif (summary_pass, summary_fail) != (counted_pass, counted_fail):
            findings.append(
                "SUMMARY row disagrees with its own rows: SUMMARY says "
                "pass=%d fail=%d, the rows say pass=%d fail=%d"
                % (summary_pass, summary_fail, counted_pass, counted_fail))
        summary_verdict = summary[2] if len(summary) > 2 else None
        expected_verdict = "PASS" if counted_fail == 0 else "FAIL"
        if summary_verdict != expected_verdict:
            findings.append(
                "SUMMARY row's own verdict is %s but its rows say %s"
                % (summary_verdict, expected_verdict))

    shots_dir = os.path.join(directory, "shots")
    for row in rows:
        for shot_name in ledger.shot_names(row):
            shot_path = os.path.join(shots_dir, "%s.png" % shot_name)
            if not os.path.isfile(shot_path):
                findings.append("step %r claims shot %r, which is not on disk (%s)"
                                 % (row["step"], shot_name, shot_path))
                continue
            size = os.path.getsize(shot_path)
            if size < MIN_SHOT_BYTES:
                findings.append("step %r's shot %r is only %d byte(s) (< %d)"
                                 % (row["step"], shot_name, size, MIN_SHOT_BYTES))

    if os.path.isdir(shots_dir):
        by_digest = {}
        for entry in sorted(os.listdir(shots_dir)):
            if not entry.endswith(".png"):
                continue
            path = os.path.join(shots_dir, entry)
            by_digest.setdefault(md5_of(path), []).append(entry)
        for digest, names in sorted(by_digest.items()):
            if len(names) > 1:
                findings.append("%d shots share one MD5 (%s), a screenshot that never "
                                 "changed: %s" % (len(names), digest, ", ".join(names)))

    return findings


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("quests", nargs="*", help="check only these quests (default: --all)")
    parser.add_argument("--all", action="store_true", help="check every discovered quest")
    arguments = parser.parse_args()

    if arguments.quests:
        names = arguments.quests
    else:
        names = quest_list.discover(REPO_ROOT)
        if not names:
            print("gate: no quest files under test/quests/ -- nothing to check "
                  "(this is a discovery fact, not a pass)", file=sys.stderr)
            return 0

    total_findings = 0
    for name in names:
        findings = check_quest(name)
        status = "RED" if findings else "green"
        print("%-24s %s" % (name, status))
        for finding in findings:
            print("    - %s" % finding)
        total_findings += len(findings)

    print("")
    if total_findings:
        print("gate: %d finding(s) across %d quest(s)" % (total_findings, len(names)),
              file=sys.stderr)
        return 1
    print("gate: %d quest(s) green" % len(names))
    return 0


if __name__ == "__main__":
    sys.exit(main())
