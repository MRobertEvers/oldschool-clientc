#!/usr/bin/env python3
"""rides.py: every hub-cart ride in 4646 with the junction layout and the path."""
import re, os
path = os.path.expanduser('~/.rsprox-index-cache/home-default/.rsprox/binary/archive/idx4646.txt')
tick = 0
jv = {}
stage = 0
rides = []
cur = None
for line in open(path, encoding='utf-8', errors='replace'):
    m = re.match(r'^\[(\d+)\]', line)
    if m:
        tick = int(m.group(1)); continue
    m = re.search(r'forget_if(\d+) \(\d+\), old=\d+, new=(\d+)', line)
    if m:
        jv[int(m.group(1))] = int(m.group(2)); continue
    m = re.search(r'forget_quest \(822\), old=\d+, new=(\d+)', line)
    if m:
        stage = int(m.group(1))
    if 'oploc1_v3] id=forget_train_cart' in line:
        if cur: rides.append(cur)
        lay = ''.join(str(jv.get(i, 0)) for i in range(1, 20))
        cur = {'tick': tick, 'stage': stage, 'layout': lay, 'path': [], 'end': None}
        continue
    if cur is None:
        continue
    m = re.search(r'\[teleport\] player=.*newcoord=\((\d+), (\d+), (\d)\)', line)
    if m:
        cur['path'].append((tick, int(m.group(1)), int(m.group(2)), int(m.group(3))))
        continue
    m = re.search(r'exactmove\] to1=\((\d+), (\d+), \d\), delay1=(\d+), to2=\((\d+), (\d+), (\d)\), delay2=(\d+), angle=(\d+)', line)
    if m and cur['path']:
        cur['path'][-1] = cur['path'][-1] + ('em', int(m.group(4)), int(m.group(5)), int(m.group(7)), int(m.group(8)))
        continue
    m = re.search(r'message_game\] type=(dialogue|mesbox), message="(.*)"', line)
    if m and cur['end'] is None and cur['path']:
        cur['end'] = (tick, m.group(2)[:60])
        rides.append(cur); cur = None
if cur: rides.append(cur)
for r in rides:
    pts = ' '.join(f"{p[1]},{p[2]}" + (f"/{p[3]}" if p[3] != 1 else '') for p in r['path'])
    print(f"[{r['tick']}] st{r['stage']} lay={r['layout']} end={r['end']}")
    print('   ' + pts)
