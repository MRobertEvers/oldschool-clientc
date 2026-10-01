#!/usr/bin/env python3
"""The first failing row of a quest run, small.

    python3 tools/quest_gate/fail.py <test_id> [--name <run name>] [--context N] [--all] [--leg K]

WHY. After a run an author needs the first non-PASS row and the rows around
it, not the whole ledger (a 300-row ledger is 30 KB of context). This prints,
hard-capped at 3,000 characters:

  * the SUMMARY row;
  * the FIRST non-PASS row (FAIL or BLOCKED) with its detail (a detail over
    900 characters keeps its head and its tail);
  * the --context rows before it (default 3) and one after, one line each,
    truncated to 160 characters;
  * the failing row's screenshot path, when it names one;
  * the last three lines of the run's *.log that say "script error",
    "STALE SCRIPT PACK" or "assert".

--all lists every non-PASS row instead, one line each, at most 40.
--leg K keeps only rows of ladder leg K (the rows whose name starts with a
guide step of that leg, as ladder.py cuts it); the SUMMARY is the run's.

Reads build/quest_gate/<test_id>/ledger.tsv, or build/quest_gate/<name>/
ledger.tsv with --name. Exit 0 when every row passed, 1 when a row failed
(so a shell `&&` chain stops), 2 when there is no ledger.

A run started with `run.py ... --detach` that is still going (its
run.status says starting/running and its pid is live) is read as IN
PROGRESS: the first line says so, a missing SUMMARY is "still running", not
"stopped early", and the exit is 3 unless a row has already failed (1).
"""

import os as _os
import sys as _sys
_HERE = _os.path.dirname(_os.path.abspath(__file__))
# queue.py beside this file shadows the stdlib: same scrub as helper_coverage.py.
_sys.path[:] = [_p for _p in _sys.path if _os.path.abspath(_p or ".") != _HERE]
_sys.path.append(_HERE)

import argparse
import glob
import os
import sys

import ledger  # noqa: E402

REPO_ROOT = os.path.dirname(os.path.dirname(_HERE))
GATE_DIR = os.path.join(REPO_ROOT, "build", "quest_gate")
CAP_TOTAL = 3000
CAP_DETAIL = 900
CAP_LINE = 160
CAP_ALL = 40
LOG_WORDS = ("script error", "STALE SCRIPT PACK", "assert")


def die(message):
    sys.stderr.write("fail.py: %s\n" % message)
    sys.exit(2)


def one_line(text, cap):
    text = " ".join(text.split())
    return text if len(text) <= cap else text[:cap - 1] + "…"


def head_tail(text, cap):
    if len(text) <= cap:
        return text
    marker = " …[%d chars cut]… " % (len(text) - cap)
    keep = cap - len(marker)
    return text[:keep * 2 // 3] + marker + text[len(text) - (keep - keep * 2 // 3):]


def row_line(row):
    return one_line("%s %s %s t=%s | %s" % (row["index"], row["verdict"], row["step"], row["ticks"],
                                          row["detail"]), CAP_LINE)


def shot_paths(run_dir, row):
    out = []
    for name in ledger.shot_names(row):
        found = sorted(glob.glob(os.path.join(run_dir, "shots", name + "*")))
        out.extend(os.path.relpath(p, REPO_ROOT) for p in found[:1])
        if not found:
            out.append("%s (named, not on disk)" % os.path.relpath(os.path.join(run_dir, "shots", name), REPO_ROOT))
    return out


def log_lines(run_dir):
    hits = []
    for path in sorted(glob.glob(os.path.join(run_dir, "*.log"))):
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                if any(word in line for word in LOG_WORDS):
                    hits.append("%s: %s" % (os.path.basename(path), one_line(line, CAP_LINE)))
    return hits[-3:]


def run_in_progress(run_dir):
    """A one-line note when run_dir belongs to a detached run.py that is
    still running (run.py's detach block writes run.status as key=value
    lines), else None. A status whose pid is gone is not in progress: the
    ledger is then whatever the run left behind."""
    try:
        with open(os.path.join(run_dir, "run.status"), "r", encoding="utf-8") as handle:
            lines = handle.read().splitlines()
    except OSError:
        return None
    status = dict(line.split("=", 1) for line in lines if "=" in line)
    if status.get("state") not in ("starting", "running"):
        return None
    pid = status.get("pid", "")
    if not pid.isdigit():
        try:
            with open(os.path.join(run_dir, "run.pid"), "r", encoding="utf-8") as handle:
                pid = handle.read().strip()
        except OSError:
            pid = ""
    if not pid.isdigit() or int(pid) <= 0:
        return None
    try:
        os.kill(int(pid), 0)
    except ProcessLookupError:
        return None
    except PermissionError:
        pass
    return "IN PROGRESS: detached run pid %s started %s is still running -- this ledger is partial; " \
           "wait with: python3 tools/quest_gate/run.py --wait %s" % (
               pid, status.get("started", "?"), os.path.basename(run_dir))


def leg_steps(test_id, leg):
    """The guide step names of ladder leg `leg`."""
    import ladder  # noqa: E402 -- only --leg pays for the guide parse
    _, _, steps = ladder.build(test_id)
    legs = ladder.cut(steps, ladder.DEFAULT_LEG, False)
    if not 1 <= leg <= len(legs):
        die("--leg %d: %s has %d leg%s" % (leg, test_id, len(legs), "" if len(legs) == 1 else "s"))
    first, last = legs[leg - 1]
    return [s["step"] for s in steps[first:last + 1]]


def in_leg(row, names):
    step = row["step"]
    for prefix in ("goto-", "walk-", "travel-"):
        if step.startswith(prefix):
            step = step[len(prefix):]
    return any(step == n or step.startswith(n + ".") or step.startswith(n + "-") for n in names)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("test_id")
    parser.add_argument("--name", default=None, help="run name: build/quest_gate/<name>/ledger.tsv")
    parser.add_argument("--context", type=int, default=3, help="rows before the failure (default 3)")
    parser.add_argument("--all", action="store_true", help="every non-PASS row, one line each (at most 40)")
    parser.add_argument("--leg", type=int, default=None, help="only rows of ladder leg K")
    args = parser.parse_args()
    if args.context < 0:
        die("--context must be 0 or more, got %d" % args.context)

    run_dir = os.path.join(GATE_DIR, args.name or args.test_id)
    path = os.path.join(run_dir, "ledger.tsv")
    rows, summary = ledger.read(path)
    progress = run_in_progress(run_dir)
    if rows is None and progress:
        print("%s\nno ledger rows yet at %s" % (progress, os.path.relpath(path, REPO_ROOT)))
        return 3
    if rows is None:
        die("no ledger at %s (run the quest first, or name the run with --name)" % os.path.relpath(path, REPO_ROOT))
    if args.leg is not None:
        names = leg_steps(args.test_id, args.leg)
        rows = [r for r in rows if in_leg(r, names)]

    out = []
    if progress:
        out.append(progress)
    out.append("ledger %s: %d rows" % (os.path.relpath(path, REPO_ROOT), len(rows)))
    out.append("SUMMARY " + ("\t".join(summary[1:]) if summary else
                             "not yet: the run is still going" if progress else
                             "missing: the run did not finish"))
    bad = [i for i, r in enumerate(rows) if r["verdict"] != "PASS"]
    if not bad:
        # A run with every row PASS still failed when it never wrote its
        # SUMMARY (it stopped early) or the SUMMARY says FAIL (exit code).
        verdict = summary[2] if summary and len(summary) > 2 else None
        if progress:
            if rows:
                out.append("no failing row so far; last row %s" % row_line(rows[-1]))
            else:
                out.append("no failing row so far")
            print("\n".join(out))
            return 3
        if args.leg is not None or verdict == "PASS":
            out.append("no failing row")
            print("\n".join(out))
            return 0
        out.append("no failing row, but the run failed: %s" % (
            "SUMMARY says %s" % verdict if summary else "no SUMMARY row (the run stopped early)"))
        out.extend("log: " + line for line in log_lines(run_dir))
        print("\n".join(out))
        return 1

    if args.all:
        out.append("%d non-PASS rows%s:" % (len(bad), "" if len(bad) <= CAP_ALL else ", first %d" % CAP_ALL))
        out.extend("  " + row_line(rows[i]) for i in bad[:CAP_ALL])
    else:
        first = bad[0]
        row = rows[first]
        out.append("rows before:")
        out.extend("  " + row_line(r) for r in rows[max(0, first - args.context):first])
        out.append("FIRST NON-PASS: row %s %s %s ticks=%s (%d non-PASS in all)" % (
            row["index"], row["verdict"], row["step"], row["ticks"], len(bad)))
        out.append("  detail: " + head_tail(row["detail"], CAP_DETAIL))
        shots = shot_paths(run_dir, row)
        if shots:
            out.append("  shot: " + ", ".join(shots))
        if first + 1 < len(rows):
            out.append("row after:")
            out.append("  " + row_line(rows[first + 1]))
    logs = log_lines(run_dir)
    if logs:
        out.append("log (last %d script error / STALE SCRIPT PACK / assert):" % len(logs))
        out.extend("  " + line for line in logs)
    text = "\n".join(out)
    if len(text) > CAP_TOTAL:
        note = "\n[cut at %d of %d chars]" % (CAP_TOTAL, len(text))
        text = text[:CAP_TOTAL - len(note)] + note
    print(text)
    return 1


if __name__ == "__main__":
    sys.exit(main())
