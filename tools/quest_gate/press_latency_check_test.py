"""Fixture test: gate.py press_latency_check, the party ledger's
raid.press_latency row (owner_presslat, 2026-10-07).

    python3 tools/quest_gate/press_latency_check_test.py

The relay's Maiden freezer pressed its +16 barrage at t291 and the server
received it at t293, the tick the 4s walked in and healed her (mrlya on
aa98a5360). Each seat's `<plan>.press_latency` row lists the presses it sent
(`t<decide tick>s|a`) and its players() pid; the leader's ticklog.tsv raider
rows say when the server received a packet (`input 1`, on the row of the tick
AFTER the boundary it was handled at). A press received more than one tick
after its decide tick fails the row.

  case            want
  on_time         t291s received at the t291 boundary (input 1 on t292) -> PASS
  member_on_time  p2's t300a received at t300 (input 1 on t301, log pid 1) -> PASS
  late            t291s received at t293 (input 1 on t294) -> FAIL naming
                  "p1 tob_maiden t291 cast -> received t293 (+2)"
  never           t291s with no input 1 after it -> FAIL naming "never"
  no_presses      no press_latency row -> (None, None)
"""
import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gate  # noqa: E402

FAILURES = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        FAILURES.append(what)


def raider(tick, pid, flag):
    label = "hpmax 99 prmax 99 head 4 input %d tgt -1 energy 10000 run 1" % flag
    return "1\t%d\traider\t%d\t99\t99\t0\t0\t0\t%s\t600" % (tick, pid, label)


def party(root, ledgers, ticklog_rows):
    with open(os.path.join(root, gate.PARTY_MARKER), "w") as handle:
        for seat in sorted(ledgers):
            handle.write("p%d\tacct_p%d\tp%d\n" % (seat, seat, seat))
    for seat, detail in ledgers.items():
        session = os.path.join(root, "p%d" % seat)
        os.makedirs(session)
        with open(os.path.join(session, "ledger.tsv"), "w") as handle:
            handle.write("quest-ledger-v1\nindex\tstep\tverdict\tticks\tshots\tdetail\n")
            if detail is not None:
                handle.write("1\ttob_maiden.press_latency\tPASS\t0\t\t%s\n" % detail)
            handle.write("SUMMARY\t1\tPASS\t0\texit=0\tpass=1 fail=0\n")
    with open(os.path.join(root, "p1", "ticklog.tsv"), "w") as handle:
        handle.write("\n".join(ticklog_rows) + "\n")
    return gate.party_seats(root)


def run_case(name, ledgers, rows):
    root = tempfile.mkdtemp(prefix="presslat_%s_" % name)
    try:
        return gate.press_latency_check(party(root, ledgers, rows))
    finally:
        shutil.rmtree(root)


def main():
    leader = "the leader; judged by gate.py raid.press_latency; players pid 1; sent t291s"
    member = "a member (no tick log); judged by gate.py raid.press_latency; players pid 2; sent t300a"

    verdict, detail = run_case("on_time", {1: leader}, [raider(291, 0, 0), raider(292, 0, 1)])
    check(verdict == "PASS", "on_time: a press received at its own boundary passes (%s)" % detail)

    verdict, detail = run_case("member", {1: leader, 2: member},
                               [raider(292, 0, 1), raider(300, 1, 0), raider(301, 1, 1)])
    check(verdict == "PASS" and "p2 1 of 1" in detail,
          "member_on_time: a member's press is judged from the leader's log (%s)" % detail)

    verdict, detail = run_case("late", {1: leader},
                               [raider(292, 0, 0), raider(293, 0, 0), raider(294, 0, 1)])
    check(verdict == "FAIL" and "p1 tob_maiden t291 cast -> received t293 (+2)" in detail,
          "late: the relay's t291 barrage received at t293 fails (%s)" % detail)

    verdict, detail = run_case("never", {1: leader}, [raider(292, 0, 0)])
    check(verdict == "FAIL" and "never" in detail, "never: a press never received fails (%s)" % detail)

    verdict, detail = run_case("none", {1: None}, [raider(292, 0, 1)])
    check(verdict is None, "no_presses: no row, no verdict")

    if FAILURES:
        print("%d failure(s)" % len(FAILURES))
        return 1
    print("press_latency_check_test: all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
