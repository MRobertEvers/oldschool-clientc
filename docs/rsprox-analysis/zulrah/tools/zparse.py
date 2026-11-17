#!/usr/bin/env python3
"""Parse an RSProx index transcript into per-tick Zulrah-relevant events.

Events are dicts with a 'k' kind. Every npc block is attributed to the npc
index it sits under; every player block to the player index.
"""
import re
import sys

TICK = re.compile(r'^\[(\d+)\]\s*$')
COORD = r'\((\d+), (\d+), (\d+)\)'
NPC_HDR = re.compile(r'^        \[(add|idle|walk|run|crawl|telejump|teleport|del)\] npc=\(index=(\d+), id=([^ ]+) \((\d+)\)(?:, multinpc=[^ ]+ \(\d+\))?, coord=' + COORD + r'\)(.*)$')
PL_HDR = re.compile(r'^        \[(add|idle|walk|run|crawl|telejump|teleport|del)\] player=\(index=(\d+), name=([^,]*), coord=' + COORD + r'\)(.*)$')
NEWC = re.compile(r'newcoord=' + COORD)
SPAWNANG = re.compile(r'spawnangle=(\w+)')
PROJ = re.compile(r'^    \[(map_projanim_v\d|projanim_specific_v\d)\] id=([^ ]+) \((\d+)\), starttime=(-?\d+), endtime=(-?\d+), angle=(-?\d+), progress=(-?\d+), startheight=(-?\d+), endheight=(-?\d+)')
SRC = re.compile(r'^        \[source\] coord=' + COORD)
TGT = re.compile(r'^        \[target\] coord=' + COORD + r'(?:, (npc|player)=\(index=(\d+)[^)]*?coord=' + COORD + r'\))?')
LOCADD = re.compile(r'^    \[loc_add_change_v\d\] id=([^ ]+) \((\d+)\), coord=' + COORD + r', shape=(\d+), rotation=(\d+)')
LOCDEL = re.compile(r'^    \[loc_del\] coord=' + COORD + r', shape=(\d+), rotation=(\d+)')
SAREA = re.compile(r'^    \[sound_area\] id=(\d+), loops=(\d+)(?:, delay=(\d+))?(?:, range=(\d+))?(?:, size=(\d+))?, coord=' + COORD)
SYNTH = re.compile(r'^    \[synth_sound\] id=(\d+)(?:, loops=(\d+))?(?:, delay=(\d+))?')
VARBIT = re.compile(r'^    \[varbit\] id=([^ ]+) \((\d+)\), old=(-?\d+), new=(-?\d+)')
VARP = re.compile(r'^    \[varp_(?:small|large)\] id=([^ ]+) \((\d+)\), old=(-?\d+), new=(-?\d+)')
MSG = re.compile(r'^    \[message_game\] type=([^,]+), message="(.*)"$')
MIDI = re.compile(r'^    \[(midi_song_v2|midi_jingle)\] (.*)$')
MAPANIM = re.compile(r'^    \[map_anim\] id=([^ ]+) \((\d+)\)(.*?)coord=' + COORD)
OPNPC = re.compile(r'^    \[opnpc(\d)(?:_v\d)?\] npc=\(index=(\d+), id=([^ ]+) \((\d+)\)')
CAM = re.compile(r'^    \[(cam_[a-z_0-9]+)\] ?(.*)$')
IF = re.compile(r'^    \[(if_opensub|if_closesub)\] (.*)$')

EXT = re.compile(r'^            \[([a-z_]+)\] ?(.*)$')


def parse(path):
    """Yield (tick, events) for every tick."""
    tick = None
    ev = []
    cur = None  # ('npc', idx, name, id) or ('pl', idx, name)
    lastproj = None
    with open(path, errors='replace') as f:
        for line in f:
            line = line.rstrip('\n')
            m = TICK.match(line)
            if m:
                if tick is not None:
                    yield tick, ev
                tick = int(m.group(1))
                ev = []
                cur = None
                lastproj = None
                continue
            if tick is None:
                continue
            m = NPC_HDR.match(line)
            if m:
                kind, idx, name, nid, x, z, p, rest = m.groups()
                e = dict(k='npc', mv=kind, idx=int(idx), name=name, id=int(nid),
                         x=int(x), z=int(z), p=int(p))
                n = NEWC.search(rest)
                if n:
                    e['nx'], e['nz'] = int(n.group(1)), int(n.group(2))
                sa = SPAWNANG.search(rest)
                if sa:
                    e['spawnangle'] = sa.group(1)
                ev.append(e)
                cur = ('npc', int(idx), name, int(nid))
                continue
            m = PL_HDR.match(line)
            if m:
                kind, idx, name, x, z, p, rest = m.groups()
                e = dict(k='pl', mv=kind, idx=int(idx), name=name, x=int(x), z=int(z), p=int(p))
                n = NEWC.search(rest)
                if n:
                    e['nx'], e['nz'] = int(n.group(1)), int(n.group(2))
                ev.append(e)
                cur = ('pl', int(idx), name)
                continue
            m = EXT.match(line)
            if m and cur is not None:
                blk, body = m.groups()
                e = dict(k=cur[0] + 'x', idx=cur[1], blk=blk, body=body)
                if cur[0] == 'npc':
                    e['name'] = cur[2]
                    e['id'] = cur[3]
                ev.append(e)
                continue
            if line.startswith('    [') and not line.startswith('        '):
                cur = None
            m = PROJ.match(line)
            if m:
                pk, name, pid, st, et, ang, prog, sh, eh = m.groups()
                lastproj = dict(k='proj', pkt=pk, name=name, id=int(pid), st=int(st), et=int(et),
                                ang=int(ang), prog=int(prog), sh=int(sh), eh=int(eh))
                ev.append(lastproj)
                continue
            m = SRC.match(line)
            if m and lastproj is not None and 'sx' not in lastproj:
                lastproj['sx'], lastproj['sz'] = int(m.group(1)), int(m.group(2))
                continue
            m = TGT.match(line)
            if m and lastproj is not None and 'tx' not in lastproj:
                lastproj['tx'], lastproj['tz'] = int(m.group(1)), int(m.group(2))
                if m.group(4):
                    lastproj['tkind'] = m.group(4)
                    lastproj['tidx'] = int(m.group(5))
                continue
            m = LOCADD.match(line)
            if m:
                name, lid, x, z, p, sh, rot = m.groups()
                ev.append(dict(k='locadd', name=name, id=int(lid), x=int(x), z=int(z), shape=int(sh), rot=int(rot)))
                continue
            m = LOCDEL.match(line)
            if m:
                x, z, p, sh, rot = m.groups()
                ev.append(dict(k='locdel', x=int(x), z=int(z), shape=int(sh), rot=int(rot)))
                continue
            m = SAREA.match(line)
            if m:
                sid, loops, delay, rng, size, x, z, p = m.groups()
                ev.append(dict(k='sarea', id=int(sid), loops=int(loops), delay=int(delay or 0), range=int(rng or 0),
                               size=int(size or 0), x=int(x), z=int(z)))
                continue
            m = SYNTH.match(line)
            if m:
                sid, loops, delay = m.groups()
                ev.append(dict(k='synth', id=int(sid), delay=int(delay or 0)))
                continue
            m = VARBIT.match(line)
            if m:
                ev.append(dict(k='varbit', name=m.group(1), id=int(m.group(2)), old=int(m.group(3)), new=int(m.group(4))))
                continue
            m = VARP.match(line)
            if m:
                ev.append(dict(k='varp', name=m.group(1), id=int(m.group(2)), old=int(m.group(3)), new=int(m.group(4))))
                continue
            m = MSG.match(line)
            if m:
                ev.append(dict(k='msg', type=m.group(1), text=m.group(2)))
                continue
            m = MIDI.match(line)
            if m:
                ev.append(dict(k='midi', pkt=m.group(1), body=m.group(2)))
                continue
            m = MAPANIM.match(line)
            if m:
                ev.append(dict(k='mapanim', name=m.group(1), id=int(m.group(2)), rest=m.group(3), x=int(m.group(4)), z=int(m.group(5))))
                continue
            m = OPNPC.match(line)
            if m:
                ev.append(dict(k='opnpc', op=int(m.group(1)), idx=int(m.group(2)), name=m.group(3)))
                continue
            m = CAM.match(line)
            if m:
                ev.append(dict(k='cam', pkt=m.group(1), body=m.group(2)))
                continue
            m = IF.match(line)
            if m:
                ev.append(dict(k='if', pkt=m.group(1), body=m.group(2)))
                continue
    if tick is not None:
        yield tick, ev


def local_index(path):
    with open(path, errors='replace') as f:
        for i, line in enumerate(f):
            if line.startswith('local player index:'):
                return int(line.split(':')[1])
            if i > 20:
                break
    return None


if __name__ == '__main__':
    import json
    for t, ev in parse(sys.argv[1]):
        if ev:
            print(t, json.dumps(ev)[:300])
