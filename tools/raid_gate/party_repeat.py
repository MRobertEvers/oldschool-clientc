#!/usr/bin/env python3
"""party_repeat.py -- the determinism gate for a party run (raid seam21).

    python3 tools/raid_gate/party_repeat.py <id> [--runs 3] [--load] [--cycles k[,k...]]
    python3 tools/raid_gate/party_repeat.py --script <file.lua> --name <run> [...]

Runs one party test N times under ONE run name (run.py clears the run
directory before each run, and this checks that every file in it is newer
than the run's start; the seeds are the name's, so every run has the same
accounts), keeps each run's evidence under build/raid_repeat/<name>/<label>/,
and compares the runs:

  * the world's tick log (<run>/ticklog.tsv): byte-identical;
  * every raider's p<n>/ledger.tsv: identical row for row in its step,
    verdict, ticks, shots and detail columns and its SUMMARY row. The one
    field test/raids/README.md "Determinism" documents as wall-clock is
    stripped first: the seconds in a `run.unfinished` row's reason (run.py
    finish_unfinished_ledger). Nothing else is;
  * every raider's boundary trace (`net: party: boundary k -> tick t digest d`
    in p<n>/client.log; run.py sets TORIRS_EMBED_PARTY_TRACE=1): identical.

--cycles k sets TORIRS_LOGIC_CYCLES_PER_FRAME=k for every client of the run
(the engine's knob; k divides 30). A comma list runs N runs at each k, in
order, and also compares the first run of each k with the first run of the
first k: the cross-k proof (`--cycles default,1` is the party default against
an explicit k=1). Without --cycles, or for `default` in the list, the knob is
left unset, which is the party default (k = 1).

--load runs ONE of the N (the last run of the first k) under artificial CPU
load: raider p2 is started at nice 19 (run.py QUEST_PARTY_NICE_SEAT=2) and
this script runs one busy-loop process per CPU (os.cpu_count(), plain
priority) for the length of that run, so p2 is the slowest raider by far. The
lock step must not care: that run is compared like the others.

Every run must exit 0 (run.py's verdict, which includes the party.lockstep
row) unless --allow-red, which keeps only the comparison (a k whose ledgers
are red, measured for its wall time). Exit 0 only when every run agrees; the
first difference is printed otherwise, under 4 KB. Each run's run.py output
goes to build/raid_repeat/<name>/<label>/run.out, never to this terminal.
"""

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
RAID_RUN = os.path.join(HERE, "run.py")

TRACE_RE = re.compile(r"(?m)^net: party: boundary (\d+) -> tick (-?\d+) digest ([0-9a-f]{8})\s*$")
# The one documented wall-clock field (test/raids/README.md "Determinism"):
# seconds in a run.unfinished row's reason ("killed after 312 s", "no client
# tick for 91 s").
WALL_SECONDS_RE = re.compile(r"\b\d+(?:\.\d+)? ?s\b")
WALL_CLOCK_STEPS = ("run.unfinished",)
LEADER_SECONDS_RE = re.compile(r"leader exit \S+ after ([0-9.]+) s")
LOCKSTEP_RE = re.compile(r"party\.lockstep (PASS|FAIL) -- (.*)")
CUT = 300


def cut(text):
    text = str(text)
    return text if len(text) <= CUT else text[:CUT] + "..."


def seats_of(run_dir):
    """[(n, session_dir)] from <run>/party.tsv."""
    marker = os.path.join(run_dir, "party.tsv")
    assert os.path.isfile(marker), "%s is not a party run (no party.tsv)" % run_dir
    seats = []
    with open(marker, "r", encoding="utf-8") as handle:
        for line in handle:
            fields = line.rstrip("\n").split("\t")
            if len(fields) >= 3 and fields[0].startswith("p"):
                seats.append((int(fields[0][1:]), fields[2]))
    assert seats, "%s names no raider" % marker
    return seats


def ledger_rows(path):
    """The ledger's lines as comparable tuples, wall-clock stripped (see the
    banner), or None when the file is missing."""
    if not os.path.isfile(path):
        return None
    rows = []
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            columns = line.rstrip("\n").split("\t")
            if len(columns) >= 6 and columns[1] in WALL_CLOCK_STEPS:
                columns[5] = WALL_SECONDS_RE.sub("<wall s>", columns[5])
            # index, step, verdict, ticks, shots, detail -- every column.
            rows.append(tuple(columns))
    return rows


def snapshot(run_dir, keep_dir, started):
    """Copy one run's evidence into keep_dir and return its comparable form."""
    stale = []
    for root, _dirs, files in os.walk(run_dir):
        for entry in files:
            path = os.path.join(root, entry)
            if os.path.getmtime(path) < started - 1.0:
                stale.append(os.path.relpath(path, run_dir))
    assert not stale, "the run dir was not cleared: %s predate the run" % ", ".join(stale[:5])
    shot = {"seats": {}, "ticklog": None}
    ticklog = os.path.join(run_dir, "ticklog.tsv")
    if os.path.isfile(ticklog):
        shutil.copy2(ticklog, os.path.join(keep_dir, "ticklog.tsv"))
        with open(ticklog, "rb") as handle:
            data = handle.read()
        shot["ticklog"] = (hashlib.sha256(data).hexdigest()[:12], data.count(b"\n"),
                           os.path.join(keep_dir, "ticklog.tsv"))
    for seat, session in seats_of(run_dir):
        session_dir = os.path.join(run_dir, session)
        ledger = os.path.join(session_dir, "ledger.tsv")
        if os.path.isfile(ledger):
            shutil.copy2(ledger, os.path.join(keep_dir, "p%d_ledger.tsv" % seat))
        trace = []
        log = os.path.join(session_dir, "client.log")
        if os.path.isfile(log):
            with open(log, "r", encoding="utf-8", errors="replace") as handle:
                trace = TRACE_RE.findall(handle.read())
        with open(os.path.join(keep_dir, "p%d_trace.txt" % seat), "w", encoding="utf-8") as handle:
            for k, t, d in trace:
                handle.write("boundary %s -> tick %s digest %s\n" % (k, t, d))
        shot["seats"][seat] = {"ledger": ledger_rows(ledger), "trace": trace}
    return shot


def first_list_difference(a, b):
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            return i
    if len(a) != len(b):
        return min(len(a), len(b))
    return None


def show_at(rows, i, render):
    return render(rows[i]) if i < len(rows) else "<end>"


def compare(base_label, base, label, other):
    """None when `other` agrees with `base`, else one difference, as text."""
    if base["ticklog"] is None or other["ticklog"] is None:
        return "tick log: %s has %s, %s has %s" % (
            base_label, "one" if base["ticklog"] else "none", label,
            "one" if other["ticklog"] else "none")
    if base["ticklog"][0] != other["ticklog"][0]:
        with open(base["ticklog"][2], "r", encoding="utf-8", errors="replace") as handle:
            a = handle.read().split("\n")
        with open(other["ticklog"][2], "r", encoding="utf-8", errors="replace") as handle:
            b = handle.read().split("\n")
        i = first_list_difference(a, b)
        return ("tick log differs (%s sha %s, %d lines; %s sha %s, %d lines); first at line %d:\n"
                "  %s: %s\n  %s: %s" % (
                    base_label, base["ticklog"][0], base["ticklog"][1], label, other["ticklog"][0],
                    other["ticklog"][1], i + 1, base_label, cut(show_at(a, i, str)),
                    label, cut(show_at(b, i, str))))
    if sorted(base["seats"]) != sorted(other["seats"]):
        return "seats: %s has %s, %s has %s" % (base_label, sorted(base["seats"]), label,
                                                sorted(other["seats"]))
    trace_text = lambda row: "boundary %s -> tick %s digest %s" % row
    for seat in sorted(base["seats"]):
        a = base["seats"][seat]["trace"]
        b = other["seats"][seat]["trace"]
        i = first_list_difference(a, b)
        if i is not None:
            return ("p%d boundary trace differs at line %d (%d vs %d lines):\n  %s: %s\n  %s: %s"
                    % (seat, i + 1, len(a), len(b), base_label, show_at(a, i, trace_text),
                       label, show_at(b, i, trace_text)))
    for seat in sorted(base["seats"]):
        a = base["seats"][seat]["ledger"]
        b = other["seats"][seat]["ledger"]
        if a is None or b is None:
            return "p%d ledger: %s %s, %s %s" % (seat, base_label, "present" if a else "MISSING",
                                                label, "present" if b else "MISSING")
        i = first_list_difference(a, b)
        if i is not None:
            return ("p%d ledger differs at line %d (%d vs %d lines):\n  %s: %s\n  %s: %s"
                    % (seat, i + 1, len(a), len(b), base_label, cut(show_at(a, i, "\t".join)),
                       label, cut(show_at(b, i, "\t".join))))
    return None


def start_load():
    """One busy loop per CPU, plain priority (see --load in the banner)."""
    count = os.cpu_count() or 4
    loop = "while True:\n    pass\n"
    return [subprocess.Popen([sys.executable, "-c", loop], stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL) for _ in range(count)]


def stop_load(processes):
    for process in processes:
        process.kill()
    for process in processes:
        process.wait()


def run_once(arguments, k, label, keep_dir, loaded):
    os.makedirs(keep_dir, exist_ok=True)
    command = [sys.executable, RAID_RUN]
    if arguments.script:
        command += ["--script", os.path.abspath(arguments.script)]
    else:
        command += [arguments.id]
    if arguments.name:
        command += ["--name", arguments.name]
    command += ["--no-build", "--no-publish"]
    if arguments.timeout:
        command += ["--timeout", str(arguments.timeout)]
    environment = dict(os.environ)
    environment.pop("TORIRS_LOGIC_CYCLES_PER_FRAME", None)
    environment.pop("QUEST_PARTY_NICE_SEAT", None)
    if k is not None:
        environment["TORIRS_LOGIC_CYCLES_PER_FRAME"] = str(k)
    load = []
    if loaded:
        environment["QUEST_PARTY_NICE_SEAT"] = "2"
        load = start_load()
    started = time.time()
    try:
        with open(os.path.join(keep_dir, "run.out"), "wb") as out:
            code = subprocess.call(command, env=environment, cwd=REPO_ROOT, stdout=out,
                                   stderr=subprocess.STDOUT)
    finally:
        stop_load(load)
    seconds = time.time() - started
    with open(os.path.join(keep_dir, "run.out"), "r", encoding="utf-8", errors="replace") as handle:
        text = handle.read()
    leader = LEADER_SECONDS_RE.search(text)
    lock = LOCKSTEP_RE.search(text)
    run_dir = os.path.join(REPO_ROOT, "build", "quest_gate", arguments.name or arguments.id)
    shot = snapshot(run_dir, keep_dir, started)
    traces = shot["seats"].get(1, {}).get("trace", [])
    summary = "none"
    for row in shot["seats"].get(1, {}).get("ledger") or []:
        if row and row[0] == "SUMMARY":
            summary = " ".join(row[1:5])
    print("party_repeat: %s: run.py exit %d, %.1f s (leader %s s)%s; tick log %s; "
          "%d boundaries; party.lockstep %s; p1 SUMMARY %s" % (
              label, code, seconds, leader.group(1) if leader else "?",
              " [p2 at nice 19 + %d busy loops]" % (os.cpu_count() or 4) if loaded else "",
              ("sha %s, %d lines" % shot["ticklog"][:2]) if shot["ticklog"] else "none",
              len(traces), lock.group(1) if lock else "none", summary), flush=True)
    return code, shot


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("id", nargs="?", help="a party test id under test/raids/")
    parser.add_argument("--script", default=None, help="a party scratch file instead of an id")
    parser.add_argument("--name", default=None,
                        help="run name (default: the id; required with --script); it seeds the run")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--load", action="store_true",
                        help="one run with p2 at nice 19 under one busy loop per CPU")
    parser.add_argument("--cycles", default=None,
                        help="TORIRS_LOGIC_CYCLES_PER_FRAME for every client: k or k1,k2,... "
                             "(`default` = unset, the party default k=1)")
    parser.add_argument("--allow-red", action="store_true",
                        help="compare only; a run.py exit other than 0 is not a failure")
    parser.add_argument("--timeout", type=int, default=None, help="run.py --timeout")
    parser.add_argument("--keep", default=None,
                        help="evidence directory (default build/raid_repeat/<name>)")
    arguments = parser.parse_args()
    if bool(arguments.id) == bool(arguments.script):
        parser.error("give a test id or --script, not both")
    if arguments.script and not arguments.name:
        parser.error("--script needs --name (the run name seeds the run)")
    if arguments.runs < 2:
        parser.error("--runs must be at least 2: one run proves nothing")
    cycles = [None]
    if arguments.cycles:
        # `default` leaves the knob unset (the party default, k = 1).
        cycles = [None if k == "default" else int(k) for k in arguments.cycles.split(",")]
        for k in cycles:
            if k is not None and (k < 1 or 30 % k):
                parser.error("--cycles %d: k must divide 30" % k)
    name = arguments.name or arguments.id
    keep_root = arguments.keep or os.path.join(REPO_ROOT, "build", "raid_repeat", name)
    if os.path.isdir(keep_root):
        shutil.rmtree(keep_root)
    os.makedirs(keep_root)

    failures = []
    firsts = []
    for k_index, k in enumerate(cycles):
        k_text = "default" if k is None else str(k)
        base = None
        for i in range(arguments.runs):
            label = "k%s-run%d" % (k_text, i + 1)
            loaded = arguments.load and k_index == 0 and i == arguments.runs - 1
            code, shot = run_once(arguments, k, label, os.path.join(keep_root, label), loaded)
            if code != 0 and not arguments.allow_red:
                failures.append("%s: run.py exit %d (see %s)" % (
                    label, code, os.path.relpath(os.path.join(keep_root, label, "run.out"),
                                                 REPO_ROOT)))
            if base is None:
                base = (label, shot)
                firsts.append(base)
                continue
            difference = compare(base[0], base[1], label, shot)
            if difference:
                failures.append("%s vs %s: %s" % (base[0], label, difference))
    for label, shot in firsts[1:]:
        difference = compare(firsts[0][0], firsts[0][1], label, shot)
        if difference:
            failures.append("ACROSS k: %s vs %s: %s" % (firsts[0][0], label, difference))

    total = len(cycles) * arguments.runs
    k_list = ",".join("default" if k is None else str(k) for k in cycles)
    if failures:
        text = "party_repeat: %s: %d run(s) at k=%s, DIFFER:\n%s" % (
            name, total, k_list, "\n".join(failures))
        print(text[:3800], flush=True)
        print("party_repeat: evidence in %s" % os.path.relpath(keep_root, REPO_ROOT), flush=True)
        return 1
    print("party_repeat: %s: %d run(s) at k=%s AGREE -- tick log, every raider's ledger and "
          "boundary trace identical%s; evidence in %s" % (
              name, total, k_list, " (one run under load)" if arguments.load else "",
              os.path.relpath(keep_root, REPO_ROOT)), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
