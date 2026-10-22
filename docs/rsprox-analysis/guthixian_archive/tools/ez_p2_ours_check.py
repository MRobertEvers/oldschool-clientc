#!/usr/bin/env python3
"""Ezekiel's finale (P2) in our tick log against RSProx 4300/4301: the
`[bas]` blocks (file-only npc_info rows, kind 7: ready, walk) and the body
models every finale npc carries.

  Ezekiel        an empty bas a tick before the jump (F-1); ready
                 npc_ezekiel_finale_loop01 with the cast (F+3)
  the fence      each its own drop and the idle of the same number, fixed
                 by the cell: (0,2) 1, (1,2) 2, (2,2) 3, (2,1) 4, (0,1) 5,
                 (0,0) 6, (1,0) 7, (2,0) 1
  the V          front 62639 and an empty bas; left 62641, walk and ready
                 idle03; right 62642, idle04
  the death sword  hidden (-2) as it is added, shown (-1) seven ticks on

    python3 ez_p2_ours_check.py <ticklog.tsv>
"""
import sys

EZ, FENCE, VEE, DEATH = 16652, 16654, 16655, 16656
CELL = {(0, 2): 1, (1, 2): 2, (2, 2): 3, (2, 1): 4, (0, 1): 5, (0, 0): 6, (1, 0): 7, (2, 0): 1}
VEE_WANT = {14710: (62639, -1, -1), 14718: (62641, 14721, 14721), 14722: (62642, 14725, 14725)}


def main():
    rows = [l.rstrip("\n").split("\t") for l in open(sys.argv[1]) if l.count("\t") >= 8]
    rows = [r for r in rows if r[1].lstrip("-").isdigit()]
    # Each npc life, from its spawn row on (slots are reused).
    lives, life_of = [], {}
    ez_bas, ez_anims = [], {}
    for r in rows:
        t, k = int(r[1]), r[2]
        if k == "npc_spawn":
            life = {"tick": t, "type": int(r[4]), "coord": int(r[5]), "drop": None, "bas": [], "body": []}
            lives.append(life)
            life_of[r[3]] = life
            continue
        life = life_of.get(r[3]) if k in ("npc_anim", "npc_info", "npc_body") else None
        if k == "npc_anim":
            if life and life["drop"] is None:
                life["drop"] = int(r[5])
            if r[4] == str(EZ):
                ez_anims.setdefault(int(r[5]), t)
        elif k == "npc_info" and r[9] == "bas":
            if life:
                life["bas"].append((t, int(r[6]), int(r[7])))
            if r[4] == str(EZ):
                ez_bas.append((t, int(r[6])))
        elif k == "npc_body" and life:
            life["body"].append((t, int(r[5])))
    ok = True

    def say(good, text):
        nonlocal ok
        ok = ok and good
        print(("ok   " if good else "BAD  ") + text)

    cast = ez_anims.get(14694)
    jump = ez_anims.get(14684)
    say(cast is not None and (cast, 14696) in ez_bas, "Ezekiel ready finale_loop01 with the cast (cast %s, bas %s)" % (cast, ez_bas[-3:]))
    say(jump is not None and (jump - 1, -1) in ez_bas, "Ezekiel's stance reset a tick before the jump (jump %s)" % jump)
    for life in lives:
        t, typ, c = life["tick"], life["type"], life["coord"]
        if typ == FENCE:
            x, z = (c >> 14) & 0x3FFF, c & 0x3FFF
            cell = ((x % 64 - 27) // 3, (z % 64 - 27) // 3)
            n = (life["drop"] - 14731) // 2 + 1
            idle = life["bas"][0][1] if life["bas"] else None
            say(n == CELL[cell] and idle == 14730 + 2 * n, "fence %s drop %d idle %s" % (cell, n, idle))
        elif typ == VEE:
            want = VEE_WANT[life["drop"]]
            got = (life["body"][0][1] if life["body"] else None,) + \
                ((life["bas"][0][1], life["bas"][0][2]) if life["bas"] else (None, None))
            say(got == want, "V drop %d: model %s ready %s walk %s" % (life["drop"], got[0], got[1], got[2]))
        elif typ == DEATH:
            b = life["body"]
            say(len(b) >= 2 and b[0] == (t, -2) and b[1] == (t + 7, -1), "death sword hidden at add %d, shown at +7: %s" % (t, b))
    print("ALL OK" if ok else "MISMATCH")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
