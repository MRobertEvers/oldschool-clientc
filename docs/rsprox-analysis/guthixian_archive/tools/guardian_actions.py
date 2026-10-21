"""The Guardian's own actions per fight: seq, move, transform, special, with gaps."""
import sys
from tfa_events import load
A = '/Users/matthewevers/.rsprox-index-cache/home-default/.rsprox/binary/archive/'
FIGHTS = {4294: [(540, 1050), (1095, 1495)], 4296: [(355, 840)], 4297: [(90, 265), (345, 645), (810, 1185)]}
G = ('tfa_guardian', 'tfa_guardian_start', 'tfa_guardian_unshielded', 'tfa_guardian_dead')
for s, wins in FIGHTS.items():
    ev = load(A + f'idx{s}.txt')
    for (t0, t1) in wins:
        print(f'===== {s} [{t0}..{t1}]')
        gid = None
        last = None
        for e in ev:
            if not (t0 <= e['tick'] <= t1):
                continue
            if e['who'].startswith('npc') and e.get('name') in G:
                k = e['kind']
                if k in ('seq', 'transform', 'move', 'add', 'del', 'level') or (k == 'spot' and 'ranged' not in e['id']):
                    d = '' if last is None else f'+{e["tick"]-last}'
                    if k == 'seq':
                        last = e['tick']
                    x = e.get('id') or e.get('mode') or e.get('level') or ''
                    at = e.get('at') or ''
                    print(f'{e["tick"]:5} {d:>4} {e["name"][13:] or "g"} {k} {x} {at if k in ("move","add") else ""}')
