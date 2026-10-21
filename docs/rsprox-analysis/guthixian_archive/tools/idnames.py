"""Every (kind, recorded name, id) in the transcripts -> our compack name.
    python3 idnames.py <txt>... [--filter regex]
Prints lines where our name differs, plus all names matching --filter."""
import sys, re, collections
C = '/Users/matthewevers/Documents/git_repos/3draster/OSRS-Content/osrs-content/configs/all.%s.compack'
ours = {}
for k in ('seq', 'spotanim', 'loc', 'npc', 'obj'):
    d = {}
    for line in open(C % k, errors='replace'):
        if '=' in line:
            a, b = line.rstrip('\n').split('=', 1)
            if a.isdigit():
                d[int(a)] = b
    ours[k] = d
filt = None
paths = []
for a in sys.argv[1:]:
    if a.startswith('--filter='):
        filt = re.compile(a[9:])
    else:
        paths.append(a)
seen = {}
for p in paths:
    for line in open(p, errors='replace'):
        s = line.strip()
        if s.startswith('[sequence]'): kind = 'seq'
        elif s.startswith('[spotanim]') or s.startswith('[map_anim]') or s.startswith('[map_projanim'): kind = 'spotanim'
        elif s.startswith('[loc_add_change') or s.startswith('[loc_anim'): kind = 'loc' if 'loc_add' in s else 'seq'
        elif s.startswith('[add] npc') or '[transformation]' in s: kind = 'npc'
        else: continue
        for m in re.finditer(r'id=([a-z0-9_]+) \((\d+)\)', s):
            nm, i = m.group(1), int(m.group(2))
            if kind == 'npc' and 'multinpc' in s and nm != s.split('id=')[1].split(' ')[0]:
                pass
            seen[(kind, i)] = nm
for (k, i), nm in sorted(seen.items()):
    o = ours[k].get(i)
    if (filt and filt.search(nm)) or (o != nm):
        if filt and not filt.search(nm):
            continue
        print(f"{k:8s} {i:6d} rec={nm:50s} ours={o}{'' if o == nm else '   <-- DIFF'}")
