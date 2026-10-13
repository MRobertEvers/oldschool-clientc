#!/usr/bin/env python3
"""The healing budget, offline: per seat and room, damage taken, healing
consumed (hp the consume rows actually added), and for every death the
deficit - how much more healing (or how much less damage) that seat needed.

usage: budget.py <runs dir> [--jobs N]
"""
import sys, os, glob, re, collections, multiprocessing

ROOMS = ['maiden', 'bloat', 'nylocas', 'sotetseg', 'xarpus', 'verzik']


def rooms_of(rows):
    rooms, last = [], None
    for f in rows:
        if f[2] == 'hudbar' and f[1].isdigit():
            t = int(f[1])
            if last is None or t - last > 40:
                rooms.append([t, t])
            rooms[-1][1] = t
            last = t
    return rooms[:6]


def room_at(rooms, t):
    for i, (a, b) in enumerate(rooms):
        if a <= t <= b:
            return ROOMS[i]
    return None


def one(d):
    rows = [l.rstrip('\n').split('\t') for l in open(os.path.join(d, 'ticklog.tsv'), errors='replace')][1:]
    out = open(os.path.join(d, 'stdout.txt'), errors='replace').read()
    out_txt = open(os.path.join(d, 'stdout.txt'), errors='replace').read()
    starts = {}
    for m in re.finditer(r'\[r\d+_p1 t(\d+)\] PASS   (maiden|bloat|nylocas|sotetseg|xarpus|verzik)\.barrier\.arrived', out_txt):
        starts.setdefault(m.group(2), int(m.group(1)))
    rooms = []
    for i, rm in enumerate(ROOMS):
        if rm in starts:
            nxt = [starts[r] for r in ROOMS[i + 1:] if r in starts]
            rooms.append([starts[rm], (nxt[0] - 1) if nxt else 10 ** 9])
        else:
            rooms.append([10 ** 9, 10 ** 9])
    SRC = collections.Counter()
    dmg = collections.defaultdict(collections.Counter)    # (pid) -> room -> damage
    heal = collections.defaultdict(collections.Counter)   # (pid) -> room -> hp healed
    over = collections.defaultdict(collections.Counter)   # food eaten with hp it could not add
    for f in rows:
        if not f[1].isdigit():
            continue
        t = int(f[1])
        rm = room_at(rooms, t)
        if rm is None:
            continue
        if f[2] == 'hit_player' and int(f[5]) > 0:
            dmg[f[3]][rm] += int(f[5])
            src = f[8] if f[8] not in ('-1', '') else ('poison' if f[6] == '7' else 'env')
            SRC[(rm, src)] += int(f[5])
        elif f[2] == 'consume':
            before, after = int(f[6]), int(f[7])
            if after > before:
                heal[f[3]][rm] += after - before
    deaths = []
    for m in re.finditer(r'\[r\d+_p(\d) t\d+\] FAIL   player\.died .*?world tick (\d+)', out):
        p, t = str(int(m.group(1)) - 1), int(m.group(2))
        deaths.append((p, room_at(rooms, t) or '?', t))
    passed = bool(re.search(r'^SCRIPTRUN PASS', out, re.M))
    return os.path.basename(d), passed, dict(dmg), dict(heal), deaths, dict(SRC), set(starts)


def main():
    runs = sys.argv[1]
    jobs = 8
    for a in sys.argv[2:]:
        if a.startswith('--jobs='):
            jobs = int(a.split('=')[1])
    dirs = sorted(d for d in glob.glob(os.path.join(runs, 'r*')) if os.path.exists(os.path.join(d, 'ticklog.tsv')))
    with multiprocessing.Pool(jobs) as pool:
        res = pool.map(one, dirs)
    seat_dmg = collections.defaultdict(list)
    seat_heal = collections.defaultdict(list)
    death_room = collections.Counter()
    for name, passed, dmg, heal, deaths, src, nseat in res:
        for p in dmg:
            for rm in ROOMS:
                if rm in dmg[p] or rm in heal.get(p, {}):
                    seat_dmg[(rm, 'pass' if passed else 'fail')].append(dmg[p].get(rm, 0))
                    seat_heal[(rm, 'pass' if passed else 'fail')].append(heal.get(p, {}).get(rm, 0))
        for p, rm, t in deaths:
            death_room[rm] += 1
    print('runs', len(res), 'passed', sum(1 for r in res if r[1]))
    print('%-9s %18s %18s' % ('room', 'dmg/seat pass|fail', 'heal/seat pass|fail'))
    for rm in ROOMS:
        def avg(L): return sum(L) / len(L) if L else 0
        print('%-9s %8.0f | %-8.0f %8.0f | %-8.0f' % (rm, avg(seat_dmg[(rm, 'pass')]), avg(seat_dmg[(rm, 'fail')]),
                                                    avg(seat_heal[(rm, 'pass')]), avg(seat_heal[(rm, 'fail')])))
    print('deaths by room', dict(death_room))
    # the whole raid per seat: damage before Verzik vs damage at Verzik, passing runs
    tot = collections.defaultdict(list)
    for name, passed, dmg, heal, deaths, src, nseat in res:
        for p in dmg:
            pre = sum(v for k, v in dmg[p].items() if k != 'verzik')
            tot['pass' if passed else 'fail'].append((pre, dmg[p].get('verzik', 0)))
    for k, L in tot.items():
        print(k, 'seat-runs', len(L), 'damage before Verzik %.0f, at Verzik %.0f' % (
            sum(a for a, _ in L) / len(L), sum(b for _, b in L) / len(L)))
    print('damage per seat-run by source (all runs):')
    sources(res)


def sources(res):
    agg = collections.Counter(); reached = collections.Counter()
    for name, passed, dmg, heal, deaths, src, rooms_in in res:
        agg.update(src)
        for rm in rooms_in: reached[rm] += 5
    by = collections.defaultdict(list)
    for (rm, sc), v in agg.items(): by[rm].append((v, sc))
    for rm in ROOMS:
        top = sorted(by[rm], reverse=True)[:5]
        tot = sum(v for v, _ in by[rm])
        print('  %-9s (%3d runs) total %5.1f: ' % (rm, reached[rm] // 5, tot / max(reached[rm], 1))
              + ', '.join('%s %.1f' % (sc, v / max(reached[rm], 1)) for v, sc in top))


if __name__ == '__main__':
    main()
