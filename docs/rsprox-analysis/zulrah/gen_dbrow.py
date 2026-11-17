#!/usr/bin/env python3
"""Writes zulrah_phase.dbrow from the rotation spec below (recorded where
RSProx shows it, the OSRS wiki where it does not):

    python3 docs/rsprox-analysis/zulrah/gen_dbrow.py \
        > OSRS-Content/osrs-content/server/scripts/minigames/minigame_zulrah/configs/zulrah_phase.dbrow

(tools/zulrah_import_nr.py, the Near-Reality import this replaces, writes the
same file from the old table: do not run it.)

Each step is (kind, a, b, gap, angle, src):
  kind 0 CLOUD   a, b = spots (1..9) of the first and second orb
  kind 1 EGG     a = spot
  kind 2 ATTACK  a = count, b = mode (0 ranged, 1 tanzanite mix,
                 2 Jad ranged-first, 3 Jad magic-first)
  kind 3 TAIL    b = 1 on the phase's last swing
  gap    ticks from this step (its last attack, for kind 2) to the next step
         or, on the phase's last step, to the dive
  angle  the face_angle a CLOUD/EGG step sends (toward spot a), else -1
  src    the projectile's source tile, local x*64+z, else -1
"""
import math
import sys

ORIGIN = (2240, 3040)
POS = {'M': (2266, 3073), 'E': (2276, 3071), 'S': (2266, 3062), 'W': (2256, 3071)}
POS_ID = {'M': 0, 'S': 1, 'E': 2, 'W': 3}
FORM_ID = {'G': 0, 'R': 1, 'B': 2}
SPOTS = {1: (2263, 3070), 2: (2263, 3073), 3: (2263, 3076), 4: (2266, 3069), 5: (2269, 3069),
         6: (2272, 3069), 7: (2273, 3072), 8: (2273, 3075), 9: (2273, 3078)}
DIRV = [(0, -1), (-1, -1), (-1, 0), (-1, 1), (0, 1), (1, 1), (1, 0), (1, -1)]
MODE = {'rng': 0, 'mix': 1, 'jadr': 2, 'jadm': 3}


def angle(src, dst):
    dx = src[0] - dst[0]
    dz = src[1] - dst[1]
    v = math.atan2(dx, dz) * 2048 / (2 * math.pi)
    v = v + 1e-7 if v >= 0 else v - 1e-7
    return int(v) & 2047


def aim(pos, spot):
    c = (POS[pos][0] + 2, POS[pos][1] + 2)
    a = angle(c, SPOTS[spot])
    d = DIRV[((a + 128) // 256) & 7]
    src = (c[0] + d[0] - ORIGIN[0], c[1] + d[1] - ORIGIN[1])
    return a, src[0] * 64 + src[1]


# A phase: (pos, form, [steps]). Steps in shorthand:
#   ('C', a, b)  ('E', a)  ('A', n, mode)  ('T',)
# Gaps come from gap_rule() unless a step carries an explicit 'gap'.
OPEN = ('M', 'G', [('C', 5, 6), ('C', 1, 4), ('C', 7, 8), ('C', 3, 2)])
END = ('M', 'G', [('A', 5, 'rng', 4), ('C', 5, 6), ('C', 1, 4), ('C', 7, 8), ('C', 3, 2)])

ROT = {
    # Rotation 1 "Crimson A": wiki (unrecorded beyond what it shares with 2).
    1: [OPEN,
        ('M', 'R', [('T',), ('T',)]),
        ('M', 'B', [('A', 4, 'mix')]),
        ('S', 'G', [('A', 5, 'rng'), ('E', 3), ('E', 2, 'alt'), ('C', 1, 4, 'alt'), ('C', 6, 7, 'alt'), ('E', 8, 'alt'), ('E', 9)]),
        ('M', 'R', [('T',), ('T',)]),
        ('W', 'B', [('A', 5, 'mix')]),
        ('S', 'G', [('C', 5, 6), ('C', 1, 4), ('C', 2, 3), ('E', 6), ('E', 8), ('E', 9), ('E', 7)]),
        ('S', 'B', [('A', 5, 'mix'), ('E', 1), ('C', 4, 5, 'alt'), ('E', 3, 'alt'), ('C', 6, 7, 'alt'), ('E', 2, 'alt')]),
        ('W', 'G', [('A', 10, 'jadr'), ('C', 1, 4), ('C', 5, 6), ('C', 2, 3), ('C', 7, 8)]),
        ('M', 'R', [('T',), ('T',)]),
        END],
    # Rotation 2 "Crimson B": phases 2-7 and the start of 8 recorded
    # (index 3259 attempt 1, [8686..8847]); 8-11 the wiki.
    2: [OPEN,
        ('M', 'R', [('T',), ('T',)]),
        ('M', 'B', [('A', 4, 'mix')]),
        ('W', 'G', [('C', 7, 6), ('C', 8, 9), ('C', 5, 4), ('E', 4), ('E', 1), ('E', 2), ('E', 3)]),
        ('S', 'B', [('A', 5, 'mix'), ('E', 3), ('E', 2, 'alt'), ('C', 1, 4, 'alt'), ('C', 6, 7, 'alt'), ('E', 8, 'alt'), ('E', 9)]),
        ('M', 'R', [('T',), ('T',)]),
        ('E', 'G', [('A', 5, 'rng')]),
        ('S', 'B', [('A', 5, 'mix'), ('E', 1), ('C', 4, 5, 'alt'), ('E', 3, 'alt'), ('C', 6, 7, 'alt'), ('E', 2, 'alt')]),
        ('W', 'G', [('A', 10, 'jadr'), ('C', 1, 4), ('C', 5, 6), ('C', 2, 3), ('C', 7, 8)]),
        ('M', 'R', [('T',), ('T',)]),
        END],
    # Rotation 3 "Serp": the wiki throughout.
    3: [OPEN,
        ('E', 'G', [('A', 5, 'rng'), ('E', 9), ('E', 8), ('E', 7)]),
        ('M', 'R', [('C', 9, 8), ('E', 7, 'alt'), ('C', 6, 5, 'alt'), ('E', 4, 'alt'), ('C', 1, 2, 'alt'), ('E', 3, 'alt'), ('T',), ('T',)]),
        ('W', 'B', [('A', 5, 'mix')]),
        ('S', 'G', [('A', 5, 'rng')]),
        ('E', 'B', [('A', 5, 'mix')]),
        ('M', 'G', [('C', 9, 8), ('C', 7, 6), ('C', 5, 4), ('E', 1), ('E', 3), ('E', 2)]),
        ('W', 'G', [('A', 5, 'rng')]),
        ('M', 'B', [('A', 5, 'mix'), ('C', 3, 2), ('C', 1, 4), ('E', 5), ('E', 6), ('E', 7)]),
        ('E', 'G', [('A', 10, 'jadm')]),
        ('M', 'B', [('E', 3), ('E', 2), ('E', 8), ('E', 9)]),
        END],
    # Rotation 4 "Tanz": all 13 phases recorded, twice (index 3259).
    4: [OPEN,
        ('E', 'B', [('E', 6), ('E', 9), ('E', 8), ('E', 7), ('A', 6, 'mix')]),
        ('S', 'G', [('A', 4, 'rng'), ('C', 1, 5), ('C', 4, 6)]),
        ('W', 'B', [('E', 3), ('E', 2), ('E', 4), ('E', 5), ('A', 4, 'mix')]),
        ('M', 'R', [('T',), ('T',), ('C', 1, 5), ('C', 4, 6)]),
        ('E', 'G', [('A', 4, 'rng')]),
        ('S', 'G', [('E', 3), ('E', 2), ('E', 1), ('E', 7), ('E', 8), ('E', 9), ('C', 8, 9), ('C', 6, 7), ('C', 5, 4)]),
        ('W', 'B', [('A', 5, 'mix'), ('E', 3), ('E', 2), ('E', 1), ('E', 4)]),
        ('M', 'G', [('A', 5, 'rng')]),
        ('M', 'B', [('A', 4, 'mix'), ('C', 2, 4), ('C', 1, 3), ('C', 5, 6)]),
        ('E', 'G', [('A', 8, 'jadm')]),
        ('M', 'B', [('E', 3), ('E', 2), ('E', 8), ('E', 9)]),
        END],
}


def gap_rule(cur, nxt):
    """Ticks from step `cur` to `nxt` (None = the dive)."""
    k = cur[0]
    if k == 'A':
        if len(cur) > 3 and isinstance(cur[3], int):
            return cur[3]
        return 3
    if k == 'T':
        return 0   # the tail's own whip timing; the runner adds it
    if nxt is None:
        return 4
    if nxt[0] == k:
        return 3
    # a change of kind: a new group (+1), unless the next step continues an
    # alternating series
    if 'alt' in nxt:
        return 3
    return 4


def build():
    out = []
    for r, phases in ROT.items():
        for pi, (pos, form, steps) in enumerate(phases, start=1):
            rows = []
            for i, s in enumerate(steps):
                nxt = steps[i + 1] if i + 1 < len(steps) else None
                gap = gap_rule(s, nxt)
                if s[0] == 'C':
                    a, src = aim(pos, s[1])
                    rows.append((0, s[1], s[2], gap, a, src, f"cloud s{s[1]}+s{s[2]}"))
                elif s[0] == 'E':
                    a, src = aim(pos, s[1])
                    rows.append((1, s[1], 0, gap, a, src, f"egg s{s[1]}"))
                elif s[0] == 'A':
                    rows.append((2, s[1], MODE[s[2]], gap, -1, -1, f"attack {s[1]} {s[2]}"))
                elif s[0] == 'T':
                    last = nxt is None or nxt[0] != 'T'
                    rows.append((3, 0, 1 if last else 0, 0, -1, -1, 'tail' + (' (last)' if last else '')))
            out.append((r, pi, pos, form, rows))
    return out


def main():
    rows = build()
    lines = [
        '// Zulrah\'s four rotations, phase by phase -- the whole fight as data.',
        '//',
        '// Written by the generator kept with the fight analysis',
        '// (docs/rsprox-analysis/zulrah/FIGHT.md, "Rotations"); every phase cites its',
        '// source there. Recorded: rotation 4 entire (RSProx index 3259, two fights),',
        '// rotation 2 phases 2-7 and the start of 8 (3259 [8686..8845]), and the',
        '// second surfacing of each rotation (index 819: crimson middle 6 times,',
        '// tanzanite east 5, green east 1). The rest is the OSRS wiki',
        '// (Zulrah/Strategies), with snakeling/cloud spots from the Near-Reality table',
        '// where the wiki names none -- UNVERIFIED until a recording shows them.',
        '//',
        '// key = rotation * 100 + phase. Phase 1 is the fight\'s opener (four cloud',
        '// barrages, shared). A rotation\'s LAST phase (green, middle, five spits and',
        '// four barrages) is also the next rotation\'s phase 1: after it the runner',
        '// rolls a rotation and continues at that rotation\'s phase 2.',
        '//',
        '// step = (kind, a, b, gap, angle, src):',
        '//   kind 0 cloud  a/b = first/second spot   kind 1 egg  a = spot',
        '//   kind 2 attack a = count, b = 0 ranged / 1 tanzanite mix / 2 Jad ranged first / 3 Jad magic first',
        '//   kind 3 tail   b = 1 on the phase\'s last swing',
        '//   gap   = ticks to the next step (from the LAST attack for kind 2); on the',
        '//           last step, to the dive',
        '//   angle = the face_angle a cloud/egg sends; src = its source tile (local x*64+z)',
        '// Spots 1-9 (centre tiles; a cloud sits on centre - (1,1)):',
    ]
    for k, v in SPOTS.items():
        lines.append(f'//   {k} ({v[0]}, {v[1]})')
    lines.append('')
    for r, pi, pos, form, steps in rows:
        lines.append(f'[zulrah_phase_r{r}_p{pi}]')
        lines.append('table=zulrah_phase')
        lines.append(f'data=key,{r * 100 + pi}')
        lines.append(f'data=pos,{POS_ID[pos]}  // {pos}')
        lines.append(f'data=form,{FORM_ID[form]}  // {form}')
        for k, a, b, gap, ang, src, note in steps:
            lines.append(f'data=step,{k},{a},{b},{gap},{ang},{src}  // {note}')
        lines.append('')
    sys.stdout.write('\n'.join(lines))


if __name__ == '__main__':
    main()
