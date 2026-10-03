"""Does a Hard Mode Prinkipas self-destruct? (restated 2026-10-02, seam2 tob_nylocas_room)

    cd build/spec_state/<pass>; python3 an_blert_prince.py      # reads blert_nylo_raw/*.m12.*.json

an_blert_hard.py's 'prince spawn->death' (blert_analysis_output.txt:36) gave the third
Prinkipas 54-60 ticks, which the spec pass read as a left-alone lifetime. This prints, per
prince, the hitpoints it carried and the tick it reached 0, so a kill and a self-destruct can
be told apart: a self-destruct would end the npc with hitpoints left.
Ticks are relative to the spawning form (10803); the combat form appears 2 ticks later.
"""
import collections, glob, json
SPAWNING, COMBAT = 10803, {10804, 10805, 10806}
rows = []
for f in sorted(glob.glob('blert_nylo_raw/*.m12.*.json')):
    ev = sorted(json.load(open(f)), key=lambda e: e['tick'])
    sp = [e['tick'] for e in ev if e['type'] == 7 and e.get('npc') and e['npc']['id'] == SPAWNING]
    by = collections.defaultdict(list)
    for e in ev:
        if e.get('npc') and e['npc']['id'] in COMBAT:
            by[e['npc']['roomId']].append(e)
    for es in sorted(by.values(), key=lambda es: es[0]['tick']):
        t0 = max(t for t in sp if t <= es[0]['tick'])
        hp = [(e['tick'] - t0, e['npc']['hitpoints'] >> 16) for e in es if 'hitpoints' in e['npc']]
        zero = next((t for t, h in hp if h == 0), None)
        dead = next((e['tick'] - t0 for e in es if e['type'] == 9), None)
        at52 = [h for t, h in hp if t <= 52][-1]
        rows.append((f.split('/')[-1][:8], t0, zero, dead, at52, hp[-1][1]))
order = collections.defaultdict(int)
for r in rows:
    order[r[0]] += 1
    print('raid %s prince %d spawn %d: hp 0 at +%s, death event +%s, hp at +52 %d, last hp %d' % (r[0], order[r[0]], r[1], r[2], r[3], r[4], r[5]))
print('princes %d, death event with hp 0 first %d, ended with hitpoints left %d, alive with hp > 0 past +52 %d' % (
    len(rows), sum(1 for r in rows if r[2] is not None and r[3] is not None and r[2] < r[3]),
    sum(1 for r in rows if r[5] > 0), sum(1 for r in rows if r[4] > 0 and r[2] is not None and r[2] > 52)))
