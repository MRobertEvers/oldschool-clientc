"""Fixture test: raid_report.py does not call an unprayable attack a prayer
mistake (owner_praypress / owner_verzik, 2026-10-07).

    python3 tools/raid_gate/unprayable_classifier_test.py

The classifier called every damaging hit taken with the wrong protection lit a
`prayer` mistake, so Verzik's P3 melee auto -- which content rolls with no
prayer term, and which no protection answers -- was reported as
`p0 prayer t85 took 53 from npc 8374 (attack sent t85) through
protectfrommissiles`. Its owner spent a survey on it and nearly implemented a
wrong fix (standing permanently outside her melee reach, against Blert's 81.5%
of P3 seat-ticks within one tile of her body). Such a hit is now the kind
`unprayable`: information, not a fault.

  case                  want
  unprayable_melee      a hit from seq 8123 -> kind `unprayable`, naming the
                        attack and the seq, never `prayer`
  prayable_ranged       a hit from seq 8125 three ticks later -> `prayer`
                        (the classifier's own job, untouched)
  unknown_seq           a seq that is in no table -> `prayer` as before
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import raid_report  # noqa: E402

FAILURES = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        FAILURES.append(what)


def anims(pairs):
    """{slot: [(tick, seq)]} as rows_by_slot_anim builds it."""
    return {7: list(pairs)}


def main():
    # hit_send carries the seq the attack was sent with
    hit = {"tick": 85, "npc_slot": 7, "damage": 53, "pid": 0, "npc_type": 8374}
    send, seq = raid_report.hit_send(hit, anims([(80, 8125), (85, 8123)]))
    check((send, seq) == (85, 8123), "hit_send names the tick and the seq of the last attack (%s)"
          % str((send, seq)))

    # a hit with no attack row in the ten ticks before is still unjudged
    send, seq = raid_report.hit_send(hit, anims([(60, 8123)]))
    check((send, seq) == (None, None), "a hit with no attack row in ten ticks is not judged (%s)"
          % str((send, seq)))

    # the pinned table names Verzik's P3 melee and carries its source
    row = raid_report.UNPRAYABLE_ATTACKS.get(8123)
    check(row is not None and "Verzik" in row[0] and "melee_max" in row[1],
          "seq 8123 is pinned as Verzik's P3 melee with its content source (%s)" % str(row))

    # the prayable P3 autos are NOT pinned: they stay the classifier's business
    check(8124 not in raid_report.UNPRAYABLE_ATTACKS
          and 8125 not in raid_report.UNPRAYABLE_ATTACKS,
          "her magic (8124) and ranged (8125) autos are not pinned -- a protection answers them")

    # and nothing is pinned by TIMING: melee is usually prayable, so a
    # same-tick landing must not be enough on its own
    check(raid_report.UNPRAYABLE_ATTACKS.get(8116) is None,
          "Verzik's P2 melee (8116) is not pinned -- Protect from Melee answers it, and it also "
          "lands on the tick it is sent")

    if FAILURES:
        print("%d failure(s)" % len(FAILURES))
        return 1
    print("unprayable_classifier_test: all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
