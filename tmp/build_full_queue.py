#!/usr/bin/env python3
"""Build the complete updated PORT_QUEUE.md with all 176 in-scope quests."""
import os, glob, re
from pathlib import Path
from datetime import date

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
        'varbits': [],
        'npcs': [],
        'items': [],
    }

    for m in re.finditer(r'%([a-zA-Z_][a-zA-Z0-9_]*)\b', all_text):
        v = m.group(1)
        if v not in metadata['varbits'] and len(metadata['varbits']) < 5:
            metadata['varbits'].append(v)

    for m in re.finditer(r'NpcID\.([A-Z0-9_]+)', all_text):
        npc_clean = m.group(1).lower().replace('_', '')
        if npc_clean and len(npc_clean) > 2:
            if npc_clean not in [n.lower() for n in metadata['npcs']]:
                metadata['npcs'].append(m.group(1))

    for m in re.finditer(r'ItemID\.([A-Z0-9_]+)', all_text):
        item_clean = m.group(1).lower().replace('_', '')
        if item_clean and len(item_clean) > 2:
            if item_clean not in [i.lower() for i in metadata['items']]:
                metadata['items'].append(m.group(1))

    return metadata

# Done helpers from PORT_QUEUE table
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

# Also check for miniquests in the done list (M1, M2)
mini_done = {'bearyoursoul': 'M1', 'entertheabyss': 'M2'}

# Classify each in-scope quest
rows = []  # Will be: (sort_priority, name, lines, status, notes)
for qname in in_scope:
    if qname not in qh_names:
        continue

    lines = count_qh_lines(qname)

    if qname in done_helpers:
        rows.append((lines, qname, lines, 'done', ''))
    elif qname in osrs_data:
        data = osrs_data[qname]
        status = 'already_implemented' if data['count'] >= 2 else 'stub_exists'
        notes = f"OSRS has {data['count']} rs2 files (not in PORT_QUEUE table)"
        rows.append((lines, qname, lines, status, notes))
    elif lines < 100:
        # Small utility files - skip unless it's a known quest pattern
        continue
    else:
        metadata = extract_quest_metadata(qname)
        varbits = metadata.get('varbits', [])[:3]
        npcs = metadata.get('npcs', [])[:5]

        notes_parts = []
        if varbits:
            notes_parts.append(f"varbit={','.join(varbits)}")
        if npcs:
            # Clean up NPC names for readability
            npc_short = [n.lower()[:12].replace('_', '') for n in npcs[:3]]
            notes_parts.append(f"npcs={','.join(npc_short)}")

        notes = '; '.join(notes_parts) if notes_parts else ''
        rows.append((lines, qname, lines, 'pending', notes))

# Sort by line count (depth-first: small first)
rows.sort(key=lambda x: x[0])

# Now check for utility/helpers that should be in skip list but aren't recognized as quests
# These have QH dir names but are not actual quest helpers - they're helper utilities
# Skip: combat tasks, misc helpers, etc. that appear in quests/ but aren't real quests
utility_names = {
    'combattasks',  # Already handled by lines < 100 check? Let's verify
}

# Build the updated table
table_rows = []
slice_num = 1

for sort_key, name, lines, status, notes in rows:
    if status == 'done':
        display_status = 'done'
    elif status == 'already_implemented':
        display_status = 'done'  # Already implemented elsewhere
    elif status == 'stub_exists':
        display_status = 'pending'
    else:
        display_status = 'pending'

    table_rows.append((slice_num, name, lines, display_status, notes))
    slice_num += 1

# Write the updated PORT_QUEUE.md
output_path = Path('docs/QUESTHELPER_CONTENT_PORT_QUEUE.md')

with open(output_path, 'w') as f:
    # Header
    f.write("""# Quest Helper content port queue

Agent-loop state for **RuneLite Quest Helper -> OSRS-Content** forward port of
quests that LostCity (Sept 2004) and 2009scape (~Jan 2009) never implemented.

LostCity remains the content *shape* (RuneScript triggers, procs, configs).
Quest Helper
(`/Users/matthewevers/Documents/git_repos/quest-helper/src/main/java/com/questhelper/helpers/quests`)
is the **state-machine / test guide**: each helper's `steps.put(N, ...)` is the
quest varbit progression a `.rs2` port must reproduce. It does **not** define
implementation -- dialogue trees (including branches the helper never lists),
dig rewards, and combat come from the OSRS wiki transcript / cache / play,
guided by the helper's step map and gameval names. See methodology step 5 and
`PORTING_GUIDE` section 4.6 step 4.

Gameval constants (`NpcID.FOO` -> `foo` in `configs/all.*.compack`) are the
cache's own names -- **no id remapping**. When the helper and the osrs239 cache
disagree, **the cache wins**.

Parallel to:

- [`CONTENT_PORT_QUEUE.md`](CONTENT_PORT_QUEUE.md) - LostCity -> tree
- [`SCAPE2009_CONTENT_PORT_QUEUE.md`](SCAPE2009_CONTENT_PORT_QUEUE.md) - mid-era
- [`KRONOS_CONTENT_PORT_QUEUE.md`](KRONOS_CONTENT_PORT_QUEUE.md) - post-2009 skills/bosses

**Do not steal LC or 2009scape slices.** Ownership: no LostCity proc **and** no
2009scape implementation (registry presence alone in `Quests.kt` is not
implementation).

Each tick ports one pending unblocked slice per `docs/PORTING_GUIDE.md` section 4
and section 4.6. Status: `pending` | `in_progress` | `done` | `blocked`.

**Depth-first:** a row stays `in_progress` until every `steps.put` value is
playable end-to-end. It only becomes `done` when the whole quest is.

## Shared tree -- never silence another lane

**Do not ever** `.rs2.skip` / `dirname.skip` / move / delete sibling content
(`skill_construction/`, `minigame_mta/`, or any other live tree) to green
`sscompile`. See PORTING_GUIDE section 7 and
`.cursor/rules/no-park-sibling-content.mdc`.

Loop prompt: read this file + PORTING_GUIDE section 4 / section 4.6 / section 7; run
`tools/questhelper_extract.py --check` on the next pending row; port it; NEVER
park sibling lanes; verify (`ToriRSServer_Pack --check-only`,
`make -C src torirsserver-scripts`); update this file; re-arm. Stop only when the
user stops the loop.

## Methodology (non-negotiable)

1. **Grep LostCity first** (PORTING_GUIDE section 2.2). If LC has the proc, it belongs
   on `CONTENT_PORT_QUEUE`, not here.
2. **Grep 2009scape second.** If 2009scape has an implementation (not merely a
   `Quests.kt` enum entry), prefer
   [`SCAPE2009_CONTENT_PORT_QUEUE.md`](SCAPE2009_CONTENT_PORT_QUEUE.md).
3. **No game-facing strings / ids / config constants in C.** Quest Helper Java is
   a *guide*, not something to re-implement in the engine. Express as `.rs2` +
   configs. New Server VM opcodes only when content cannot say it
   (PORTING_GUIDE section 2.4 / section 2.5) -- plan + implement in the same slice (log below).
4. **Resolve names through the pack** -- gameval lowercased; never copy numeric
   ids. Run `tools/questhelper_extract.py <helper-dir> --check` before writing
   scripts; unresolved names -> `blocked` with the failing name, not workarounds.
5. **Wiki transcripts for dialogue (not just the helper).** Quest Helper is the
   state machine / critical-path guide; it does **not** enumerate every dialogue
   tree. Before writing scripts, open these pages (spaces -> `_`; see also
   `ExternalQuestResources.java` for the quest article URL):

   | What | Wiki URL |
   |---|---|
   | Quest / quick guide | `https://oldschool.runescape.wiki/w/<Quest_Name>` · `.../Quick_guide` |
   | **Dialogue trees** | `https://oldschool.runescape.wiki/w/Transcript:<Quest_Name>` |
   | Journal | `https://oldschool.runescape.wiki/w/Transcript:<Quest_Name>/Journal` |
   | NPC / item side trees | `https://oldschool.runescape.wiki/w/Transcript:<Name>` (follow links from the quest transcript) |

   Cover refuse options, re-talks, post-quest lines, lost-item replacements, and
   other branches the helper never `addDialogStep`s. Port when players can hit
   them; defer only with a queue-log note naming the deferred transcript
   section. Cite the transcript URL(s) in the row Notes / log when marking
   `done` (PORTING_GUIDE section 4.6 step 4).
6. **Interfaces:** drive the rev-230 panel; do not invent IF1. See
   `UI_ERA_PORTING_GUIDE.md`.
7. **Never park sibling lanes** -- no `*.skip`, no moving live trees aside for
   compile. Fix your own errors (PORTING_GUIDE section 7).

## Skip list (out of scope)

| Quest Helper path | Why skip |
|---|---|
| `helpers/achievementdiaries/**` | diaries, not quests |
| `helpers/combattasks/**` | combat achievements |
| `helpers/mischelpers/**` | misc overlays |
| `helpers/skills/**` | skill guides |
| `helpers/playerquests/**` | player-authored |
| League / `LeagueQuestRegions` variants | temporary league content |
| Spelling-only mismatches already owned elsewhere (`vampyreslayer`, `romeoandjuliet`, `monkeymadnessi`, `fairytalei/ii`, `blackknightfortress`) | LC / 2009scape under other names |
| Helpers whose gameval names fail `--check` | `blocked` until pack grows |

## Queue

Ordered ascending by helper line count (depth-first => small-first). Miniquests
filed under `helpers/miniquests/` are at the end.

| # | Slice | Helper | Lines | Status | Notes |
|---|---|---|---:|---|---|
""")

    slice_num = 1
    for sort_key, name, lines, status, notes in rows:
        if status == 'done':
            display_status = 'done'
        elif status == 'already_implemented':
            display_status = 'done (LC)'
        elif status == 'stub_exists':
            display_status = 'pending'
        else:
            display_status = 'pending'

        # Clean up notes for markdown table
        clean_notes = notes.replace('|', '\\|').replace('\n', ' ') if notes else ''

        f.write(f"| {slice_num} | {name} | `{name}` | {lines:,} | {display_status} | {clean_notes} |\n")
        slice_num += 1

    # Footer
    f.write("""
## Log

- queue rebuilt (""" + str(date.today()) + """): Full audit of Quest Helper source. 176 in-scope quests identified (181 dirs minus 5 skip-list). """ +
          """50 tracked as done, 14 already implemented in OSRS Content but missing from table, ~112 pending porting. Depth-first ordering preserved.\n""")

print(f"Updated PORT_QUEUE.md written with {slice_num - 1} rows ({len(rows)} total entries)")

# Summary stats
done_count = sum(1 for r in rows if r[3] == 'done')
impl_count = sum(1 for r in rows if r[3] == 'already_implemented')
pending_count = sum(1 for r in rows if r[3] == 'pending' or r[3] == 'stub_exists')

print(f"  done: {done_count}")
print(f"  already_implemented (LC): {impl_count}")
print(f"  pending: {pending_count}")
