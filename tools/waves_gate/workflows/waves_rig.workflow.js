export const meta = {
  name: 'waves-rig-pass',
  description: 'Resumable waves rig pass: one Sonnet worker per monster unit, strictly one at a time, lists every animation on the npc rig and classifies it by evidence tier; an Opus closer samples, writes the summary and commits. Documents only: nothing is built or run',
  phases: [
    { title: 'Rig', detail: 'Sonnet, one unit at a time: docs/minigames/<game>/sources/rig/<unit>.tsv and .md (units with a report on disk are skipped)' },
    { title: 'Close', detail: 'Opus: sample every unit against the cache records, write RIG_ANIMATIONS.md, commit by explicit path, push' },
  ],
}
// The waves rig pass (docs/minigames/waves_loop/RIG_PASS.md). Launch with the Workflow
// tool, scriptPath = this file, and args:
//   { pass: "<branch>-rig-<game>", game: "inferno" | "colosseum",
//     units: [ { key: "sol_heredit", npcs: ["colosseum_sol_p1", ...] }, ... ] }
// State under build/rig_state/<pass>/ (<unit>.rig.json, <unit>.progress.md, close.json).
// Relaunch with the SAME args to continue from disk; never resumeFromRunId. Documents
// only, so it may run beside ONE building pass. Workers run one at a time (owner,
// 2026-10-03: many at once crashed the editor).

const MAIN = '/Users/matthewevers/Documents/git_repos/3draster'
const WT = `${MAIN}/build/orchestrator/worktrees/waves`
const pass = args && args.pass
const game = args && args.game
const units = (args && args.units) || []
if (!pass) throw new Error('args.pass is required')
if (!['inferno', 'colosseum'].includes(game)) throw new Error('args.game must be inferno or colosseum')
if (!units.length) throw new Error('args.units is empty')
const BATCH = String(pass).replace(/-rig(-[a-z]+)?$/, '')
const STATE = `${WT}/build/rig_state/${pass}`
const DOCS = `${WT}/docs/minigames/${game}`

const COMMON = `You are one worker of the WAVES loop's rig pass. Work ONLY inside ${WT} (branch ${BATCH}; run git -C ${WT} branch --show-current before your first edit and stop if it differs). Absolute paths under ${WT}; the main checkout ${MAIN} and the other worktrees are other loops'. Never git stash/checkout/reset/clean/amend or add -A. DOCUMENTS ONLY: do not build or run a client or server, do not run make, do not rewrite any generated file (never tools/gen_npc_combat.py --write or --fix-authored), do not edit anything under OSRS-Content or src: a seam pass is building in this worktree while you work. OUTPUT DISCIPLINE (owner, 2026-10-03: this loop hung the owner's editor three times): no command prints more than about 4 KB; the cache configs are tens of megabytes: never cat or uncapped-grep them; select inside Python scripts under ${STATE}/ that write files and print only counts; wc -c before any cat; read long files through sed -n ranges; no shell command over about 8 KB; if the Write or Read tool's hook times out use small bash heredocs. RETURN DISCIPLINE: every returned string under 400 characters, every list under 10 short entries; the detail is in your files. Never a recursive grep or find over the whole OSRS-Content tree (238,000 files): one named file or directory, capped. zsh: write "\${VAR}:path". Never cite or use the 2004 source (LostCity) or its tables, even where the tool can load them: the target is OSRS behaviour (owner, 2026-10-03). Never invent a role, an id or a visual. YOUR METHOD is ${WT}/docs/minigames/waves_loop/RIG_PASS.md: read it in full first, then ${WT}/docs/DEATH_ATK_DEF_ANIMS.md through its first 140 lines. The minigame's cache dumps are ${DOCS}/sources/cache_*.txt with ${DOCS}/sources/LEDGER_cache.md, and its inventory is ${DOCS}/AV_INVENTORY.md. PASS STATE DIR: ${STATE} (mkdir -p).`

const RIG = { type: 'object', properties: { unit: { type: 'string' }, npcs: { type: 'integer' }, framemaps: { type: 'array', items: { type: 'string' } }, candidates: { type: 'integer' }, by_tier: { type: 'string' }, unknown: { type: 'integer' }, ledger_disagreements: { type: 'integer' }, files: { type: 'array', items: { type: 'string' } }, notes: { type: 'string' } }, required: ['unit', 'npcs', 'framemaps', 'candidates', 'by_tier', 'unknown', 'ledger_disagreements', 'files', 'notes'] }
const LAND = { type: 'object', properties: { commit: { type: 'string' }, pushed: { type: 'boolean' }, sampled: { type: 'string' }, corrected: { type: 'array', items: { type: 'string' } }, notes: { type: 'string' } }, required: ['commit', 'pushed', 'sampled', 'corrected', 'notes'] }

phase('Rig')
const done = []
const failed = []
for (const u of units) {
  // One at a time, by the owner's instruction. A unit whose report is on disk answers at once.
  let r = null
  for (let i = 1; i <= 2 && !r; i++) {
    r = await agent(`${COMMON}

YOUR UNIT: "${u.key}" of ${game}; npc symbols: ${u.npcs.join(', ')}. RESUME: if ${STATE}/${u.key}.rig.json exists and parses, return its content unchanged and do nothing else. Otherwise ${STATE}/${u.key}.progress.md is your notebook: continue from it if it exists and append after every step. Do the six steps of RIG_PASS.md for these npcs and write ${DOCS}/sources/rig/${u.key}.tsv and ${DOCS}/sources/rig/${u.key}.md exactly as that document says. FINISH: write the schema JSON to ${STATE}/${u.key}.rig.json (unit = "${u.key}"; by_tier = counts like "bound 2, rig+name 9, rig+sound 3, rig 7, name 4"), then return it.`, { label: `rig:${game}:${u.key}`, phase: 'Rig', model: 'claude-sonnet-5-5', schema: RIG }).catch(() => null)
    if (!r) log(`rig:${u.key}: no result on try ${i} of 2`)
  }
  if (r) done.push(u.key); else failed.push(u.key)
}
log(`rig: ${done.length} of ${units.length} units reported; ${failed.length ? 'NO REPORT from ' + failed.join(', ') : 'none missing'}`)

phase('Close')
const land = await agent(`${COMMON}

YOU ARE THE CLOSER (Opus) of rig pass ${pass}. RESUME: if ${STATE}/close.json says landed, return what it records. The unit reports are ${STATE}/<unit>.rig.json and the products are ${DOCS}/sources/rig/<unit>.tsv and .md for: ${units.map(u => u.key).join(', ')} (no report from: ${failed.join(', ') || 'none'}; a unit with no report whose files are incomplete is left out of the commit and named). Do: (1) SAMPLE, and say how many you opened: for every unit open five rows of its table against the cache (the npc record's line in cache_npc.txt, the sequence's record in cache_seq.txt or configs/all.seq through a capped grep) and confirm the id, the name, the framemap membership as RIG_PASS.md step 2 computes it (re-run the unit's scratch script if one is under ${STATE}/), and that the tier does not claim more than its evidence; a row above its evidence is corrected by you in the table and listed. (2) Write ${DOCS}/RIG_ANIMATIONS.md, under 150 lines: per monster, the rig, the attack, special, spawn and death candidates with ids and tiers, what is unknown, every disagreement with the generated npc_combat ledgers, and one closing list "What promotes a candidate" (Blert's attack tables and plugin constants once the corpus lands; a picture by the test driver). (3) Every ledger disagreement is one row in ${WT}/docs/minigames/waves_loop/CONTENT_BUGS.md (append; ids RIG-<n>; the generated ledger's file, the row, the rig evidence; found is not fixed). (4) Commit ONLY explicit paths (git -C ${WT} add <paths>; git -C ${WT} commit -q -m "waves: ${game} rig pass [rig:${pass}] -- <units, candidate counts>" -- <paths>; trailer "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"): ${DOCS}/sources/rig/, ${DOCS}/RIG_ANIMATIONS.md, CONTENT_BUGS.md. A seam pass may be committing: on an index lock wait ten seconds and retry. Then git -C ${WT} push -q origin ${BATCH} (if rejected: pull -q --no-rebase, push again; never force). (5) Write {"landed": true, "commit": "<sha>"} to ${STATE}/close.json and return the schema.`, { label: 'rig-close', phase: 'Close', model: 'opus', schema: LAND }).catch(() => null)
if (!land) log('RIG CLOSE DID NOT LAND: relaunch with the same args')
return { pass, done, failed, land }
