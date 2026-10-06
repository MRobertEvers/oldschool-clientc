export const meta = {
  name: 'raid-rig-pass',
  description: 'Rig pass: for each raid room, one Sonnet worker at a time lists every animation on its monsters\' rigs and checks the spec rows and generated ledgers against them; an Opus closer writes the findings and commits the documents',
  phases: [
    { title: 'Rig', detail: 'one Sonnet worker per room, ONE AT A TIME; documents only' },
    { title: 'Close', detail: 'Opus: findings into CONTENT_BUGS.md and a list of spec rows to correct; commit, push' },
  ],
}
// The rig pass (owner, 2026-10-03: "The corpus is unlikely to supply animations. You will
// have to use the tooling that finds animations that match the rigging of the walk and idle
// animations of the npc. ... Do not do many of those at once"). Method: the waves loop's
// docs/minigames/waves_loop/RIG_PASS.md, restated in the worker prompt below so this loop
// stays independent. Args: { pass: "<branch>-rig-tob", raid: "tob", rooms: [...] }.
// Resume: relaunch with the same args; a worker whose two files and report exist returns
// its report unchanged. Never resumeFromRunId.

const WT = '/Users/matthewevers/Documents/git_repos/3draster/build/orchestrator/worktrees/raid'
const pass = args && args.pass
if (!pass) throw new Error('args.pass is required')
const raid = (args && args.raid) || 'tob'
const RAIDS = { tob: { name: 'Theatre of Blood', docs: 'docs/minigames/theater_of_blood', scripts: 'OSRS-Content/osrs239-content/server/scripts/minigames/minigame_tob' } }
const RAID = RAIDS[raid]
if (!RAID) throw new Error('unknown raid ' + raid)
const rooms = (args && args.rooms) || []
if (!rooms.length) throw new Error('args.rooms is required')
const BATCH = (args && args.branch) || String(pass).replace(/-rig(-[a-z0-9]+)*$/, '')
const STATE = `${WT}/build/rig_state/${pass}`
const extra = (args && args.context) ? `\n\nCURRENT PICTURE: ${args.context}` : ''

const COMMON = `Work ONLY inside ${WT} (the RAID orchestrator's git worktree, branch ${BATCH}; use absolute paths). The owner's main checkout /Users/matthewevers/Documents/git_repos/3draster and every other worktree belong to other sessions: never read, build or write there. Another pass may be building and running tests in this worktree right now: this pass is DOCUMENTS ONLY -- never build, never run the client or the server, never edit src/, test/, tools/, OSRS-Content/ or any generated file.
OUTPUT DISCIPLINE (owner, 2026-10-03; the editor crashed and took every running pass with it): no command prints more than about 4 KB into a tool result. Send long output to a file under ${STATE} and read its tail or grep it; cut -c1-300 on anything with long lines; never cat a catalog, a ledger or a table whole; no single shell command longer than about 8 KB.
SEARCH DISCIPLINE: never run a recursive grep or find over the whole OSRS-Content tree or over osrs239-content/ (about 238,000 files; a session crashed on this). Scope every search to one directory, with --include and head; match whole symbols, and numeric ids with word boundaries.${extra}`

const REPORT = { type: 'object', properties: {
  room: { type: 'string' }, npcs: { type: 'number' }, rigs: { type: 'string' }, candidates: { type: 'number' },
  ledger_disagreements: { type: 'array', items: { type: 'string' } },
  spec_disagreements: { type: 'array', items: { type: 'string' } },
  unknown_roles: { type: 'number' }, note: { type: 'string' },
}, required: ['room', 'npcs', 'candidates', 'ledger_disagreements', 'spec_disagreements'] }

const worker = (room) => `${COMMON}

You are the rig worker for ${RAID.name}, room "${room}". RESUME: if ${WT}/${RAID.docs}/sources/rig/${room}.tsv, ${room}.md and ${STATE}/${room}.json all exist, return the JSON's content unchanged. mkdir -p ${STATE} and ${WT}/${RAID.docs}/sources/rig.

WHY: a cache npc record names only its ready (idle) and walk animations. Every other animation the monster can play is built on the same skeleton (framemap) as those two, so the rig closes the candidate set. Plugins, the wiki and recordings rarely name animations, and a generated combat ledger can be wrong. READ docs/DEATH_ATK_DEF_ANIMS.md (the method) first, then the top of tools/gen_npc_combat.py's main() for how it builds the rig index and which catalog directory it needs (grep -n "def main" and read 60 lines).

METHOD. (1) THE NPCS: every npc of this room in every mode, from the room's scripts and configs under ${RAID.scripts}/ (grep -n the room name in configs/*.npc and configs/*.constant; the Entry "_story" and Hard "_hard" records too) and the cache dumps under ${RAID.docs}/sources/ (ls it; the cache_npc*.txt files): id, name, size, readyanim, walkanim and any other animation field, with the dump line. (2) THE RIG: the framemap of each npc's ready and walk animations. Import tools/gen_npc_combat.py AS A MODULE from a scratch script under ${STATE}/ and use its own loaders (load_catalog, seq_features, seq_names_by_rig). NEVER run it with --write or --fix-authored: it rewrites thousands of generated ledgers. If ready and walk sit on different framemaps, list both. (3) EVERY SEQUENCE ON THE RIG: id, cache name, frame count, length in client cycles and game ticks (30 cycles per tick), every frame sound with its name, and the fields that hint at use (priority, loops, hand items). A rig shared by many creatures (the human rig, framemap 0, about 3,900 sequences) is NOT closed by the join: narrow by the npc's own name words and mark those rows tier "name". (4) CLASSIFY each candidate: role one of ready, walk, attack (number the variants), special, spawn, death, defend, transition, other, unknown. Tier says what the role rests on and nothing may be claimed above it: "bound" (the npc record names it); "rig+name" (on the rig and its Jagex name states the role); "rig+sound" (on the rig and a frame sound's Jagex name states the role); "rig" (on the rig, role not stated: role unknown); "name" (shared rig or off-rig, matched by name words only). Never classify by resemblance to another monster. (5) GRAPHICS AND PROJECTILES have their own models and rigs, so the join does not reach them: list the spotanims in the cache spotanim dump whose Jagex name shares the room's name words, tier "name", with their anim= sequence and its length. (6) THE GENERATED LEDGER: compare with OSRS-Content/osrs239-content/npc_combat/<first letter>/<npc>.combat for each npc (ls that one directory with a name filter; never recurse) and write down every row where the ledger names an animation that is NOT on the npc's rig, or that belongs to another monster by name. Do not edit the ledger. (7) THE SPEC TABLE: ${RAID.docs}/encounters/${room}.tsv rows whose mechanic_id contains ".av." name animations by id (grep -n "\\.av\\." and read only those lines, cut -c1-260). For each row that names a sequence played by an npc: is that sequence on that npc's rig? A row whose sequence is off the rig, or whose claimed role is above what its tier supports (for example grade A or B on a role known only by name), is a spec disagreement: write the row id, the sequence, the rig it is on, and the tier it deserves. Also list rig candidates with a stated role (rig+name or rig+sound: an attack, special, spawn or death) that NO spec row and no script of the room (grep -n the id in the room's .rs2 and .constant files) uses: an animation the monster has and ours never plays.

WRITE: ${WT}/${RAID.docs}/sources/rig/${room}.tsv with the header npc, npc_id, framemap, kind (seq or spotanim), id, cache_name, frames, game_ticks, frame_sounds, role, tier, note (tab-separated; one row per candidate per npc; write it from your scratch script, never by hand). ${WT}/${RAID.docs}/sources/rig/${room}.md, under 80 lines: the rig or rigs, the count of candidates, the attack, special, spawn and death candidates with their tiers, what stays unknown, the ledger disagreements, the spec disagreements, the unused candidates, and what only a recording or a plugin constant could settle. Then the schema JSON to ${STATE}/${room}.json and return it (each list entry one short line: id, name, what is wrong; detail stays in the .md). Commit nothing.`

phase('Rig')
const reports = []
for (const room of rooms) {
  // ONE AT A TIME, by the owner's rule.
  const r = await agent(worker(room), { label: `rig:${raid}_${room}`, phase: 'Rig', model: 'claude-sonnet-5-5', schema: REPORT }).catch(() => null)
  if (!r) log(`rig:${room}: no report (relaunch with the same args)`)
  reports.push(r)
}
const done = reports.filter(Boolean)
log(`${done.length} of ${rooms.length} rooms reported; ${done.reduce((n, r) => n + r.ledger_disagreements.length, 0)} ledger and ${done.reduce((n, r) => n + r.spec_disagreements.length, 0)} spec disagreements`)
if (done.length < rooms.length) return { pass, reports, land: null }

phase('Close')
const land = await agent(`${COMMON}

YOU ARE THE CLOSER (Opus) of rig pass ${pass}. The workers wrote ${WT}/${RAID.docs}/sources/rig/<room>.tsv and <room>.md for ${rooms.join(', ')} and their reports under ${STATE}/<room>.json. RESUME: ${STATE}/close.progress.md is your notebook; if ${STATE}/close.json exists return its content. (1) AUDIT: read each <room>.md whole and spot-check three disagreements per room against the tsv and the cache dump line (a claimed off-rig sequence really is on another framemap; a tier is not above its evidence). Strike what does not hold, in the .md, with the reason. (2) WRITE ${WT}/${RAID.docs}/RIG_AUDIT.md: per room, the rigs, then three lists -- LEDGER (a generated or authored combat record naming an animation its npc cannot play: npc, field, sequence, what the rig offers for that role), SPEC (a spec row whose sequence is off the rig or graded above its tier: row id, what to change), UNUSED (a rig animation with a stated role that ours never plays) -- each entry one line with the file:line of its evidence. (3) Add each LEDGER and UNUSED entry that is a real content difference to ${WT}/docs/minigames/raid_loop/CONTENT_BUGS.md under a heading for this pass (found is not fixed: the row is what makes the next seam pass fix it). Do NOT edit the spec tables, the content or any ledger: the next seam pass does. (4) COMMIT by explicit path, parent repo only (the rig directory's files, RIG_AUDIT.md, CONTENT_BUGS.md; message "raids: rig pass ${pass} -- <one line of what it found>", trailer "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"); never add -A, never stash, reset, clean or amend; other passes may have uncommitted files in the tree (test/raids/*.lua, src/, OSRS-Content): leave every one of them alone. git push -q origin ${BATCH}; a rejected push means another agent pushed first: git pull --no-rebase -q origin ${BATCH}, then push again. (5) Write {"commit": "<sha>", "ledger": <n>, "spec": <n>, "unused": <n>} to ${STATE}/close.json and report the counts and the five most consequential findings in under 25 lines.`, { label: 'close', phase: 'Close', model: 'opus' }).catch(() => null)
return { pass, reports, land }
