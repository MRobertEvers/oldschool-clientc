#!/usr/bin/env python3
"""ymparse.py -- parse an RSProx transcript into per-tick structures for the Yama analysis.

parse(path) -> dict: tick -> {'packets':[(kind, line)], 'npcs':[entry], 'players':[entry]}
entry = {'op','index','id','name','multinpc','coord','newcoord','ext':[lines],'line'}
"""
import re

HEADER = re.compile(r'^\[(\d+)\]\s*$')
COORD = re.compile(r'coord=\((\d+), (\d+), (\d+)\)')
NEWCOORD = re.compile(r'newcoord=\((\d+), (\d+), (\d+)\)')
NPCREF = re.compile(r'npc=\(index=(\d+), id=([^ ,)]+)(?: \((\d+)\))?(?:, multinpc=([^ ,)]+)(?: \((\d+)\))?)?')
PLAYERREF = re.compile(r'player=\(index=(\d+), name=([^,]*), coord=\((\d+), (\d+), (\d+)\)\)')

def parse(path, tmin=0, tmax=1 << 40):
    ticks = {}
    tick = None
    cur = None
    curlist = None
    with open(path, errors='replace') as f:
        for raw in f:
            m = HEADER.match(raw)
            if m:
                tick = int(m.group(1))
                if tick > tmax:
                    break
                if tick >= tmin:
                    ticks[tick] = {'packets': [], 'npcs': [], 'players': []}
                cur = None
                curlist = None
                continue
            if tick is None or tick < tmin:
                continue
            line = raw.rstrip('\n')
            if not line.strip():
                continue
            ind = len(line) - len(line.lstrip(' '))
            s = line.strip()
            T = ticks[tick]
            if ind == 4:
                cur = None
                if s.startswith('[npc_info'):
                    curlist = 'npcs'
                    continue
                if s.startswith('[player_info'):
                    curlist = 'players'
                    continue
                curlist = None
                T['packets'].append(s)
            elif ind == 8:
                m = re.match(r'\[(\w+)\]', s)
                op = m.group(1) if m else s
                if curlist == 'npcs':
                    e = {'op': op, 'line': s, 'ext': []}
                    mn = NPCREF.search(s)
                    if mn:
                        e['index'] = int(mn.group(1))
                        e['name'] = mn.group(2)
                        e['id'] = int(mn.group(3)) if mn.group(3) else None
                        e['multinpc'] = mn.group(4)
                    mc = COORD.search(s)
                    if mc:
                        e['coord'] = (int(mc.group(1)), int(mc.group(2)), int(mc.group(3)))
                    mnc = NEWCOORD.search(s)
                    if mnc:
                        e['newcoord'] = (int(mnc.group(1)), int(mnc.group(2)), int(mnc.group(3)))
                    T['npcs'].append(e)
                    cur = e
                elif curlist == 'players':
                    e = {'op': op, 'line': s, 'ext': []}
                    mp = PLAYERREF.search(s)
                    if mp:
                        e['index'] = int(mp.group(1))
                        e['name'] = mp.group(2)
                        e['coord'] = (int(mp.group(3)), int(mp.group(4)), int(mp.group(5)))
                    mnc = NEWCOORD.search(s)
                    if mnc:
                        e['newcoord'] = (int(mnc.group(1)), int(mnc.group(2)), int(mnc.group(3)))
                    T['players'].append(e)
                    cur = e
                else:
                    # continuation of a packet (e.g. [source]/[target] lines)
                    if T['packets']:
                        T['packets'][-1] = T['packets'][-1] + ' ' + s
            else:
                if cur is not None:
                    cur['ext'].append(s)
                elif T['packets']:
                    T['packets'][-1] = T['packets'][-1] + ' ' + s
    return ticks

def ext_get(e, kind):
    return [x for x in e['ext'] if x.startswith('[' + kind)]

SEQ = re.compile(r'\[sequence\] id=([^ ]+)(?: \((\d+)\))?(?:, delay=(\d+))?')
SPOT = re.compile(r'\[spotanim\](?: slot=(\d+),)? id=([^ ]+) \((\d+)\)(?:, delay=(\d+))?(?:, height=(\d+))?')
HIT = re.compile(r'\[hit\] id=(\d+), value=(\d+)')
