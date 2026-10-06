#!/usr/bin/env python3
"""Fixture test: ladder.py reads a quest whose last run the grader charges a
goto no guide step is named after.

    python3 tools/quest_gate/ladder_pseudo_step_test.py

helper_coverage's charge-any rule (840711dad, Grader._charge_name) grades such
a goto as a step of its own, "(goto past <door>)" or "(goto into a sealed
pocket)", which has no Guide entry. ladder.build() indexed guide.steps by every
graded name, so `ladder.py blackarmgang` died with KeyError: '(goto past
blackarmdoor)' and `ladder.py haunted` with '(goto past
draynor_panelled_door)' (seam pass matthew-mbp-m4-b58-seam1). The ladder is
the guide's table; such a charge is listed under it, never as a step.

The fixtures are the two quests' published ledgers at OSRS-Content 4b277a1d8a
(the v3 pin when the seam was found), read from git; ladder.build() grades
against the real guide with Grader's ledger lookup pointed at them.

  case                       want
  <id>.build                 build() returns; the charge is not a step and is
                             handed back in `charges`
  <id>.same_steps            the steps are the ones an uncharged ledger gives
                             (a ledger with no rows: nothing to charge), numbered
                             1..N without a gap
  <id>.text                  main() prints the table and names the charge
                             under it
  <id>.json                  --json lists it under "charges", not "steps"
  <id>.leg                   --leg 1 prints (fail.py --leg reads the same build)

Writes only temporary files. Exit 0 when every case holds.
"""

import contextlib
import io
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:] = [p for p in sys.path if os.path.abspath(p or ".") != HERE]
sys.path.append(HERE)

import helper_coverage as hc  # noqa: E402
import ladder  # noqa: E402

LEDGER_COMMIT = "4b277a1d8a"
LEDGER = "osrs239-content/server/scripts/selftest/quests/%s/play/ledger.tsv"
QUESTS = {
    "blackarmgang": ("quest_blackarmgang", "(goto past blackarmdoor)"),
    "haunted": ("quest_haunted", "(goto past draynor_panelled_door)"),
}


def fixture(quest_dir):
    return subprocess.run(["git", "-C", os.path.join(hc.REPO_ROOT, "OSRS-Content"), "show",
                           "%s:%s" % (LEDGER_COMMIT, LEDGER % quest_dir)],
                          capture_output=True, text=True, check=True).stdout


@contextlib.contextmanager
def ledger_at(path):
    """Point Grader's ledger lookup (hc.ledger_path) at `path`."""
    real = hc.ledger_path
    hc.ledger_path = lambda test_id, quest_dir: path
    try:
        yield
    finally:
        hc.ledger_path = real


def run_main(argv):
    """(exit code, stdout) of ladder.main() for argv."""
    saved = sys.argv
    sys.argv = ["ladder.py"] + argv
    out = io.StringIO()
    code = 0
    try:
        with contextlib.redirect_stdout(out):
            code = ladder.main() or 0
    except SystemExit as stop:
        code = stop.code
    finally:
        sys.argv = saved
    return code, out.getvalue()


def cases(test_id, quest_dir, charge, scratch):
    results = []
    charged = os.path.join(scratch, test_id + ".ledger.tsv")
    text = fixture(quest_dir)
    with open(charged, "w", encoding="utf-8") as handle:
        handle.write(text)
    # An uncharged ledger: its two header lines and no row (no goto to charge).
    plain = os.path.join(scratch, test_id + ".plain.tsv")
    with open(plain, "w", encoding="utf-8") as handle:
        handle.write("".join(text.splitlines(True)[:2]))

    with ledger_at(charged):
        graded = [r["step"] for r in hc.Grader(test_id, test_text="").grade()]
        got = []
        try:
            _, _, steps = ladder.build(test_id, charges=got)
            error = None
        except Exception as caught:  # the crash this test exists for
            steps, error = [], "%s: %s" % (type(caught).__name__, caught)
    results.append(("%s.build" % test_id,
                    error is None and charge in graded and [c["step"] for c in got] == [charge]
                    and charge not in [s["step"] for s in steps],
                    error or "graded has %s: %s; charges=%s" % (charge, charge in graded,
                                                               [c["step"] for c in got])))

    with ledger_at(plain):
        _, _, plain_steps = ladder.build(test_id)
    names = [s["step"] for s in steps]
    numbers = [s["n"] for s in steps]
    results.append(("%s.same_steps" % test_id,
                    names == [s["step"] for s in plain_steps] and numbers == list(range(1, len(steps) + 1)),
                    "%d steps charged, %d uncharged, n 1..%s" % (
                        len(steps), len(plain_steps), numbers[-1] if numbers else "-")))

    with ledger_at(charged):
        code, out = run_main([test_id])
        results.append(("%s.text" % test_id,
                        code == 0 and "not guide steps" in out and ("  " + charge) in out,
                        "exit %s, %d bytes, charge line %s" % (code, len(out), ("  " + charge) in out)))
        code, out = run_main([test_id, "--json"])
        body = json.loads(out) if code == 0 else {}
        results.append(("%s.json" % test_id,
                        [c["step"] for c in body.get("charges", [])] == [charge]
                        and charge not in [s["step"] for s in body.get("steps", [])],
                        "exit %s, charges=%s" % (code, [c["step"] for c in body.get("charges", [])])))
        code, out = run_main([test_id, "--leg", "1"])
        results.append(("%s.leg" % test_id, code == 0 and out.startswith("quest: %s" % test_id),
                        "exit %s, %d bytes" % (code, len(out))))
    return results


def main():
    failures = total = 0
    with tempfile.TemporaryDirectory() as scratch:
        for test_id, (quest_dir, charge) in QUESTS.items():
            for name, ok, detail in cases(test_id, quest_dir, charge, scratch):
                total += 1
                failures += not ok
                print("%-4s %-26s %s" % ("ok" if ok else "FAIL", name, detail[:110]))
    print("%d/%d" % (total - failures, total))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
