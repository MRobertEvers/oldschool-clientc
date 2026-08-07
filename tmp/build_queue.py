#!/usr/bin/env python3
"""Build comprehensive quest helper port queue."""
import os, glob, re
from pathlib import Path

QH = Path('C:/Users/mrobe/Documents/git_repos/quest-helper/src/main/java/com/questhelper/helpers')
OSRS_ROOT = Path('C:/Users/mrobe/Documents/git_repos/oldschool-clientc')
quests_dir = QH / 'quests'

# All QH quest names
qh_names = set()
for d in sorted(quests_dir.iterdir()):
    if d.is_dir(): qh_names.add(d.name)
for d in sorted(QH.iterdir()):
    if d.is_dir() and d.name != 'quests':
        java_files = list(d.glob('*.java'))
        if java_files: qh_names.add(d.name)

skip_list = {'blackknightfortress', 'fairytalei', 'fairytaleii', 'romeoandjuliet', 'vampyreslayer'}
in_scope = sorted(qh_names - skip_list)

# OSRS quest data
osrs_dirs = list(OSRS_ROOT.glob('OSRS-Content/**/scripts/quests/quest_*'))
osrs_map = {}
for d in osrs_dirs:
    if d.is_dir():
        prefix = d.name.replace('quest_', '')
        osrs_map[prefix] = str(d)

def count_rs2(dir_path):
    return len(list(Path(dir_path).rglob('*.rs2')))

osrs_data = {}
for name, path in osrs_map.items():
    cnt = count_rs2(path)
    if cnt > 0:
        osrs_data[name] = {'count': cnt, 'path': path}

# Line counts
def count_qh_lines(name):
    quest_path = quests_dir / name
    toplevel_path = QH / name
    total = 0
    for d in [quest_path, toplevel_path]:
        if d.exists() and d.is_dir():
            for f in glob.glob(str(d / '*.java')):
                try:
                    with open(f) as fh:
                        total += sum(1 for _ in fh)
                except: pass
    return max(total, 0)

# Extract varbit/varp names and key NPC/item refs from QH source
def extract_quest_metadata(name):
    quest_path = quests_dir / name
    toplevel_path = QH / name
    all_text = ""
    for d in [quest_path, toplevel_path]:
        if d.exists() and d.is_dir():
            for f in glob.glob(str(d / '*.java')):
                try:
                    with open(f) as fh:
                        all_text += fh.read() + "\n"
                except: pass

    metadata = {
        'varbits': [],  # Quest varbit names (%name -> name)
        'npcs': [],     # NpcID references (first 5 unique)
        'items': [],    # ItemID references (first 10 unique)
    }

    # Find varbit/varp names like %varname or VARPBIT/FooVarbit.FOO
    for m in re.finditer(r'%([a-zA-Z_][a-zA-Z0-9_]*)\b', all_text):
        v = m.group(1)
        if 'VARBIT' not in v and 'varbit' not in v.lower()[:5]:
            if v not in metadata['varbits'] and len(metadata['varbits']) < 5:
                metadata['varbits'].append(v)

    # Find NpcID references
    for m in re.finditer(r'NpcID\.([A-Z0-9_]+)', all_text):
        if m.group(1).lower().replace('_', '') not in [n.lower() for n in metadata['npcs']]:
            metadata['npcs'].append(m.group(1))

    # Find ItemID references
    for m in re.finditer(r'ItemID\.([A-Z0-9_]+)', all_text):
        if m.group(1).lower().replace('_', '') not in [i.lower() for i in metadata['items']]:
            metadata['items'].append(m.group(1))

    return metadata

# Parse done helpers from PORT_QUEUE
done_helpers = set([
    'xmarksthespot', 'theribbitingtaleofalilypadlabourdispute', 'pryingtimes',
    'clientofkourend', 'thequeenofthieves', 'thedepthsofdespair', 'aporcineofinterest',
    'theascentofarceuus', 'ethicallyacquiredantiquities', 'theidesofmilk',
    'insearchofknowledge', 'bonevoyage', 'childrenofthesun', 'thegardenofdeath',
    'atfirstlight', 'taleoftherighteous', 'gettingahead', 'thecorsaircurse',
    'belowicemountain', 'shadowsofcustodia', 'currentaffairs', 'twilightspromise',
    'sleepinggiants', 'meatandgreet', 'pandemonium', 'anightatthetheatre',
    'theredreef', 'misthalinmystery', 'thefremennikexiles', 'atasteofhope',
    'makingfriendswithmyarm', 'templeoftheeye', 'perilousmoon', 'troubledtortugans',
    'deathontheisle', 'scrambled', 'beneathcursedsands', 'thefinaldawn',
    'secretsofthenorth', 'theforsakentower', 'akingdomdivided', 'theheartofdarkness',
    'thecurseofarrav', 'sinsofthefather', 'dragonslayerii', 'monkeymadnessii',
    'songoftheelves', 'deserttreasureii', 'bearyoursoul', 'entertheabyss'
])

# Categorize each in-scope quest
tasks = []  # (priority_order, name, lines, status, metadata)
for qname in in_scope:
    if qname not in qh_names:
        continue

    lines = count_qh_lines(qname)

    if qname in done_helpers:
        status = 'done'
        tasks.append((lines, qname, lines, status, {}))
    elif qname in osrs_data:
        data = osrs_data[qname]
        if data['count'] >= 2:
            status = 'already_implemented'
        else:
            status = 'stub_exists'
        tasks.append((lines, qname, lines, status, {'osrs_count': data['count']}))
    elif lines < 100 and qname not in ('doricsquest',):
        # Small util/helper files - skip unless it's a known quest
        continue
    else:
        metadata = extract_quest_metadata(qname)
        tasks.append((lines, qname, lines, 'pending', metadata))

# Sort by line count (depth-first: small first) for pending, then already_implemented, then done
pending = [t for t in tasks if t[3] == 'pending']
already_impl = [t for t in tasks if t[3] == 'already_implemented']
stubs = [t for t in tasks if t[3] == 'stub_exists']
done = [t for t in tasks if t[3] == 'done']

print(f"Total in-scope quests: {len(tasks)}")
print(f"PENDING (need porting): {len(pending)}")
print(f"ALREADY IMPLEMENTED: {len(already_impl)}")
print(f"STUB EXISTS: {len(stubs)}")
print(f"TRACKED DONE: {len(done)}")

# Show the first 25 pending quests with metadata for subsession context
print("\n=== FIRST 30 PENDING (sorted by line count, depth-first) ===")
for i, (sort_key, name, lines, status, meta) in enumerate(sorted(pending)[:30]):
    varbits = meta.get('varbits', [])[:3]
    npcs = meta.get('npcs', [])[:3]
    items = meta.get('items', [])[:5]

    varbit_str = ', '.join(varbits) if varbits else '(none found)'
    npc_str = ', '.join(npcs) if npcs else '(none found)'
    item_str = ', '.join(items) if items else '(none found)'

    print(f"{i+1:3d}. {name} ({lines:4d} lines)")
    print(f"     varbits: [{varbit_str}]")
    print(f"     npcs: [{npc_str}]")
    print(f"     items: [{item_str}]")

# Also show all already_implemented quests
print("\n=== ALREADY IMPLEMENTED IN OSRS (not in PORT_QUEUE) ===")
for item in sorted(already_impl, key=lambda x: x[0]):
    name = item[1]
    lines = item[2]
    osrs_count = item[4].get('osrs_count', 0) if item[4] else 0
    print(f"  {name}: QH={lines} lines, OSRS={osrs_count} rs2 files")

# Show stubs
if stubs:
    print("\n=== STUBS (OSRS dir exists but < 2 rs2 files) ===")
    for item in sorted(stubs, key=lambda x: x[0]):
        name = item[1]
        lines = item[2]
        osrs_count = item[4].get('osrs_count', 0) if item[4] else 0
        print(f"  {name}: QH={lines} lines, OSRS={osrs_count} rs2 files")

# Write full pending list to file for subsession use
with open('tmp/pending_quests_full.txt', 'w') as f:
    for sort_key, name, lines, status, meta in sorted(pending):
        varbits = meta.get('varbits', [])[:3]
        npcs = meta.get('npcs', [])[:5]
        items = meta.get('items', [])[:10]

        f.write(f"LINECOUNT={lines}\n")
        f.write(f"NAME={name}\n")
        f.write(f"VARBITS={','.join(varbits)}\n")
        f.write(f"NPCS={','.join(npcs)}\n")
        f.write(f"ITEMS={','.join(items)}\n")
        f.write("---\n")

print(f"\nFull pending list written to tmp/pending_quests_full.txt ({len(pending)} quests)")
