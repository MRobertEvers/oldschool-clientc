export const meta = {
  name: 'quest-content-parity',
  description: 'Resumable content-parity pass: one agent per quest makes the OSRS-Content port match LostCity where LostCity has the quest, else constructs the quest from the OSRS wiki and the Quest Helper guide; an Opus closer proves, commits, records PARITY.tsv and reopens test rows',
  phases: [
    { title: 'State', detail: 'Sonnet: which quests of this pass already have a parity report; takes the content lock' },
    { title: 'Parity', detail: 'Opus this pass (escalated after two stalled Sonnet passes), one per quest: diff the port against its source and fix it; persists <id>.parity.json + notebook' },
    { title: 'Close', detail: 'Opus: audit every edit against its source, compile, C selftests, full suite, sync with origin, commit, push, PARITY.tsv, reopen rows, release the content lock' },
  ],
}
// The content-parity pass (owner rule, 2026-09-23): BEFORE a quest test is
// authored, the quest's content must be right. Rule 1: if LostCity (the 2004
// source under ~/Documents/git_repos/LostCity_Content2 + LostCity_Server) has
// the quest, the port matches LostCity leg by leg. Rule 2: otherwise the quest
// is constructed from the OSRS wiki (the pinned brief docs/quests/<name>.md,
// or the live wiki page) and the Quest Helper guide's step ladder. A narrating
// mes() that stands in for a leg, an auto-granted item the player gathers in
// the game, a puzzle collapsed to one click, a debugproc as the only path --
// none of these is content; each is a gap this pass closes.
//
// Paste inline with args { pass: "parity1", quests: ["mourningsendpartii", ...],
// context: "<one paragraph>" }. State under build/parity_state/<pass>/;
// relaunch with the SAME args to continue from disk. Never resumeFromRunId.
// Never run it alongside an author batch, a seam pass, or a content move ON
// THIS MACHINE. Across machines (docs/QUEST_ORCHESTRATOR.md) the State step
// takes the content lock (tools/quest_gate/claim.py content-lock <pass>) and
// the pass stops if another machine's content pass holds it; the closer syncs
// with origin before committing and releases the lock after its push.
// Optional args.worker_model (default claude-sonnet-5-5) picks the parity
// workers' model.

const WT = '/home/user/oldschool-clientc'
const CONTENT = `${WT}/OSRS-Content/osrs239-content`
const LC = '/home/user/LostCity_Content2'
const LCS = '/home/user/LostCity_Server'
const QH = '/home/user/quest-helper'
const pass = args && args.pass
// The batch branch (docs/QUEST_ORCHESTRATOR.md): <host>-b<N>, the pass name without its -parity / -seamN suffix, or args.branch.
const BATCH = (args && args.branch) || String(pass || '').replace(/-(parity|seam\d+)$/, '')
const quests = (args && args.quests) || []
const workerModel = (args && args.worker_model) || 'claude-sonnet-5-5'
if (!pass) throw new Error('args.pass is required (e.g. "parity1")')
if (!quests.length) throw new Error('args.quests is empty: test_ids from test/quests/QUEUE.tsv')
const STATE = `${WT}/build/parity_state/${pass}`
const extraContext = (args && args.context) ? `\n\nCURRENT PICTURE: ${args.context}` : ''

const COMMON = `You are one worker in a multi-agent build. Work ONLY inside ${WT} (branch v3; the content is the OSRS-Content submodule at ${CONTENT}) and, READ-ONLY, the reference repos ${LC} (LostCity content: scripts/quests/<quest_dir>/, maps/*.jm2 spawns, the .npc/.loc/.obj configs), ${LCS} (LostCity engine + content/scripts) and ${QH} (Quest Helper guides under src/main/java/com/questhelper/helpers/quests/). (the owner's checkout; the 2026-09-25 disk cleanup deleted the old worktree, so this checkout IS the working tree now -- never delete build or cache directories, never run git clean/checkout/reset on paths you did not change). Absolute paths. Never git stash/checkout/reset/clean/amend, never git add -A. Never touch test/quests/*.lua or QUEUE.tsv. Do NOT commit or push (the closer does). After ANY content edit: make -C ${WT}/src torirsserver-scripts (the embedded server refuses a stale pack). run.py refuses a second concurrent run of one quest id: give every scratch run its own --name; run everything in the FOREGROUND. Read ${WT}/docs/QUEST_SUITE_KIT.md's working rules and ${WT}/docs/QUEST_CONTENT_AUDIT_2026-09-22.md (how earlier content edits were judged) first.

PASS STATE DIR: ${STATE} (mkdir -p it). Every worker persists its result there and keeps a notebook so a paused or killed pass resumes from disk with nothing redone.

THE OWNER'S RULE: the content must be the real quest. (1) If LostCity has the quest (a directory ${LC}/scripts/quests/<quest_dir> or the same under ${LCS}/content/scripts/quests), the port MATCHES LostCity leg by leg: stages and their writers, every trigger ([opnpc*], [oploc*], [opheld*], [opobj*], [ai_*]), spawns (LostCity's maps/*.jm2 rows), dialogues and choices, item flows (what is gathered, bought, looted, made, consumed), gates and their conditions. Where OSRS later changed a quest (the wiki-pinned brief docs/quests/<name>.md says so), the OSRS form wins for that detail and the report says which. (2) Otherwise the quest is CONSTRUCTED from the OSRS wiki (docs/quests/<name>.md if pinned; else the live page https://oldschool.runescape.wiki/w/<Quest_name> -- read its Walkthrough and Rewards and pin the brief to docs/quests/<name>.md) and the Quest Helper guide's step ladder (steps.put stages, the DetailedQuestStep/NpcStep/ObjectStep texts, the ConditionalStep sub-steps, item and zone requirements). NEVER acceptable as content: a mes() that narrates a leg and writes the next stage; an item the game has the player gather/buy/loot/make granted by inv_add on a dialogue; a puzzle collapsed to one click; a debugproc or a soft-skip as the only path; a stage advanced with no player action. Each such placeholder is a gap this pass closes with real content (locs, npcs, spawns, triggers, dialogues, stage writes) the way the original author would have written it, in the pack's own idioms (read sibling quests for the pattern).${extraContext}`

const PARITY = { type: 'object', properties: {
  test_id: { type: 'string' }, quest_dir: { type: 'string' }, source: { type: 'string', enum: ['lostcity', 'wiki', 'unknown'] },
  legs_fixed: { type: 'array', items: { type: 'string' } }, legs_left: { type: 'array', items: { type: 'string' } },
  files_changed: { type: 'array', items: { type: 'string' } }, proof: { type: 'string' }, open_issues: { type: 'array', items: { type: 'string' } },
}, required: ['test_id', 'quest_dir', 'source', 'legs_fixed', 'legs_left', 'files_changed', 'proof', 'open_issues'] }
const LAND = { type: 'object', properties: { commit: { type: 'string' }, pushed: { type: 'boolean' }, gates: { type: 'string' }, reverted: { type: 'array', items: { type: 'string' } }, parity_rows: { type: 'array', items: { type: 'string' } }, reopened: { type: 'array', items: { type: 'string' } }, open_issues: { type: 'array', items: { type: 'string' } }, notes: { type: 'string' } }, required: ['commit', 'pushed', 'gates', 'reverted', 'parity_rows', 'reopened', 'open_issues', 'notes'] }
const STATE_SCHEMA = { type: 'object', properties: { done: { type: 'array', items: PARITY }, closed: { type: 'boolean' }, close_commit: { type: 'string' }, lock_exit: { type: 'integer' }, lock_output: { type: 'string' } }, required: ['done', 'closed', 'close_commit', 'lock_exit', 'lock_output'] }
// Every commit-and-push in this pass goes through this (docs/QUEST_ORCHESTRATOR.md "Closing").
const SYNC = `BATCH-BRANCH MODEL (docs/QUEST_ORCHESTRATOR.md): this pass runs on the batch branch ${BATCH} in BOTH repos (git -C ${WT} branch --show-current and git -C ${WT}/OSRS-Content branch --show-current must both print ${BATCH}; if not, stop and report). Never merge origin/v3 into the branch and never push to v3: v3 carries only the claim ledger and the batch reaches it through one PR at the end (claim.py done). Commit the submodule, stage the OSRS-Content gitlink in the parent, commit, then push BOTH branches: git -C ${WT}/OSRS-Content push -u origin ${BATCH} ; git -C ${WT} push -u origin ${BATCH}. A rejected push on the batch branch means another agent of THIS batch pushed first: git pull --no-rebase origin ${BATCH} in that repo, then push again. Never force, rebase, reset or stash.`

const attempt = async (label, tries, make) => {
  for (let i = 1; i <= tries; i++) {
    const r = await make().catch(() => null)
    if (r) return r
    log(`${label}: no result on try ${i} of ${tries}`)
  }
  return null
}

phase('State')
const state = (await attempt('state', 3, () => agent(`${COMMON}

YOUR JOB: read this pass's persisted state, take the content lock, no other edits, no summarising. mkdir -p ${STATE}. done = the content of every ${STATE}/<id>.parity.json that parses, copied verbatim; closed = whether ${STATE}/close.json exists and says landed; close_commit = the sha it names, else "". THE CONTENT LOCK is not used in the batch-branch model (docs/QUEST_ORCHESTRATOR.md): this pass edits content on its own batch branch ${BATCH} and the pack alloc files are reconciled with v3 when the batch's PR is made (claim.py done). Set lock_exit 0 and lock_output "batch-branch model: no lock" without running anything. Never invent an entry. Return exactly the schema.`, { label: 'state', model: 'claude-sonnet-5-5', effort: 'low', schema: STATE_SCHEMA }))) || { done: [], closed: false, close_commit: '', lock_exit: -1, lock_output: 'the state agent returned nothing' }
if (!state.closed && state.lock_exit !== 0) throw new Error(`content lock not taken (claim.py exit ${state.lock_exit}): ${state.lock_output} -- exit 3: another machine's content pass holds test/quests/CONTENT_LOCK, wait for it to close; exit 2: this checkout is not level with origin/v3 (docs/QUEST_ORCHESTRATOR.md launch checklist)`)
const doneIds = new Set(state.done.map(d => d.test_id))
const todo = quests.filter(q => !doneIds.has(q))
log(`state: ${state.done.length} done, ${todo.length} to do: ${todo.join(', ') || 'none'}`)

phase('Parity')
const fresh = await parallel(todo.map(id => () => attempt(`parity:${id}`, 2, () => agent(`${COMMON}

YOUR JOB: make ONE quest's content match its source, prove it, persist your report, do not commit. Quest test_id "${id}": python3 ${WT}/tools/quest_gate/queue.py show ${id} gives its quest_dir (under ${CONTENT}/server/scripts/quests/) and its Quest Helper guide (helper_dir/helper_file under ${QH}/src/main/java/com/questhelper/helpers/quests/). Concurrent workers edit OTHER quests' directories; touch a shared file (areas/*.spawn, configs/all.*, a shared skill script, maps/) only when your quest's leg needs it, with the smallest edit, and name it in open_issues.
RESUME DISCIPLINE: ${STATE}/${id}.parity.progress.md is your notebook. If it exists, a previous attempt was killed: read it, audit any edits it left, continue from its last proved step. Append after every meaningful step.
DO: (1) Read the coverage audit for this quest -- the section in ${WT}/docs/QUEST_HELPER_COVERAGE_2026-09-23.md if present -- and the row's last_failure. (2) Decide the source: does ${LC}/scripts/quests/<quest_dir> (or ${LCS}/content/scripts/quests/<quest_dir>) exist? If yes, source = lostcity: build a leg-by-leg table (stage writer, trigger, spawn, dialogue, item flow, gate) of LostCity vs the port; every divergence that is not an OSRS-era change documented in docs/quests/<name>.md is a fix. If no, source = wiki: build the leg table from the wiki brief and the guide's step ladder vs the port; every guide step the port narrates, auto-grants, collapses or lacks is a fix. (3) Implement the fixes as real content in the quest's own directory (and shared files only as above), in the pack's idioms; keep the quest's existing debugproc/selftest working and extend it to the new legs. Large legs (a multi-floor puzzle) are implemented completely when possible; if not, implement what you can in full working pieces, leave nothing half-wired, and list the rest under legs_left with a plan in the notebook. (4) PROVE: make -C ${WT}/src torirsserver-scripts compiles; the quest's C selftest stanza or <abbr>run debugproc is green (run the selftest binary the way docs/QUEST_SUITE_KIT.md says, private objdir); a scratch driver script (python3 ${WT}/tools/quest_gate/run.py --script <file> --name parity_${id} --no-build --no-publish, cheats inside run(), verbs per docs/QUEST_AUTHORING.md, the 20 KB core -- look a failing row up through docs/quest_authoring/INDEX.md instead of reading the topic files) drives EACH newly implemented leg through the real client and reads its stage/item/loc effect back -- quote the ledger rows; regressions: run.py cooks_assistant druid --no-build --no-publish + gate.py. (4b) CUTSCENES (owner rule 2026-09-30: a port that drops a cutscene is a divergence, though the guide never asks for one): run python3 ${WT}/tools/quest_gate/cutscene_sweep.py --repo ${WT} | grep ' ${id} ' and read its verdict. DROPPED or PARTIAL (a LostCity quest -- LostCity alone decides these): port the camera sequence (cam_moveto/cam_lookat/cam_shake/cam_reset with LostCity's coordinates and timings, file:line cited) into the same trigger, and drive it in your scratch script. WIKI_MISSING (a post-LostCity quest whose real cutscenes the owner is speccing in a SEPARATE session): do NOT port it and do NOT invent one -- read the quest's rows in ${WT}/docs/quests/cutscenes/TRIAGE.tsv (the scenes) and VIDEOS.tsv (reference footage) if present, and put 'cutscene (spec pending: <n> scenes, docs/quests/cutscenes/)' in legs_left. NEVER edit, stage or commit ${WT}/docs/quests/CUTSCENES.tsv or anything under ${WT}/docs/quests/cutscenes/: another session owns them. (5) LEAVE THE AUTHOR A LADDER (owner-approved, 2026-09-29): run python3 ${WT}/tools/quest_gate/ladder.py ${id} --write (it writes docs/quests/ladders/${id}.ladder.tsv: the guide as a small table cut into legs, with each step's trigger file:line), then write docs/quests/ladders/${id}.notes.md, at most 3 KB in plain short lines, holding ONLY what the ladder cannot know and you learned by driving: where an npc or loc really stands when it is not at the guide's tile; every door, gate, stair or barrier that stands between two steps and how it opens; which dialogue options gate progress; npcs that wander; what each fight needs (levels, weapon style, food, a special item); any step the port implements in a different form than the guide with the file:line. (6) FINISH: write the schema JSON to ${STATE}/${id}.parity.json (test_id, quest_dir, source, legs_fixed with file:line each, legs_left, files_changed, proof = the ledger rows, open_issues), then return it.`, { label: `parity:${id}`, phase: 'Parity', model: workerModel, schema: PARITY }))))
const reports = fresh.filter(Boolean)
const failed = todo.filter((q, i) => !fresh[i])
log(`parity: ${reports.length} of ${todo.length} reported; ${failed.length ? 'NO REPORT from ' + failed.join(', ') + ' (relaunch with the same args)' : 'none missing'}`)

if (args && args.stop_after_parity) { log('stop_after_parity: relaunch without it to close'); return { pass, reports, failed, land: null } }

phase('Close')
let land = state.closed ? { commit: state.close_commit, pushed: true, gates: 'landed by a previous launch', reverted: [], parity_rows: [], reopened: [], open_issues: [], notes: 'close.json already present' } : null
if (!land) {
  land = await attempt('close', 2, () => agent(`${COMMON}

YOU ARE THE CLOSER (Opus). Every parity report is a ${STATE}/<id>.parity.json -- read them ALL from disk (${state.done.length} from earlier launches, ${reports.length} from this one). Quests with no report: ${failed.join(', ') || 'none'} -- their directories may carry unfinished edits: judge by ${STATE}/<id>.parity.progress.md and a compile; keep only what is complete and proved, else restore those files from git show HEAD:<path> and say so.
RESUME DISCIPLINE: ${STATE}/close.progress.md is your notebook; if it exists continue from its last step (check git log for [parity:${pass}] before committing again; check PARITY.tsv and QUEUE.tsv before writing rows again). Append after every step.
Do: (1) git -C ${CONTENT}/.. status/diff; read every changed content file in full; judge each edit against its SOURCE (the LostCity script or the wiki brief + guide step the report cites): an edit that narrates, auto-grants, collapses a puzzle, or lacks a citation is reverted from git show HEAD:<path> and named. (2) make -C src torirsserver-scripts clean; the C selftest binary (private objdir) with the failure count against a HEAD baseline in a throwaway worktree (symlinked OSRS-Content and cache; unlink before removing) -- the count may only go DOWN, and every stanza the reports say they extended must be green. (3) python3 tools/quest_gate/run.py --all --jobs 3 --no-publish then gate.py --all: a committed test that was green and is now RED because the content now HAS a leg the test skipped (a placeholder it drove, an item it was handed) is EXPECTED -- list each with the first failing row, and reopen its QUEUE row to todo with 'RE-AUTHOR after <sha>: content now has <leg>; drive it per the guide'; any other regression is fixed or reverted before committing. make -C src test-quest-conformance test-quest-cheats check-quest-verbs check-drive-abi check-pt-switch. (4) ${SYNC} Commit the SUBMODULE first (git -C ${CONTENT}/.. add the changed quest dirs and shared files by explicit path; message "quests: content parity [parity:${pass}] -- <one clause per quest naming its source>", trailer "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"; git -C ${WT}/OSRS-Content push -u origin ${BATCH}), then the parent (docs/quests/*.md briefs you pinned, the docs/quests/ladders/<id>.ladder.tsv and <id>.notes.md files the workers wrote, (NEVER stage or commit docs/quests/CUTSCENES.tsv or docs/quests/cutscenes/ -- another session owns them and they sit modified/untracked on purpose; run python3 tools/quest_gate/cutscene_sweep.py --only-open and keep 'cutscene' in legs_left for any of this pass's quests it lists), the OSRS-Content gitlink, tools/quest_gate/PARITY.tsv), message "quest-gate: PARITY.tsv after [parity:${pass}]", same trailer; git push -u origin ${BATCH}. (5) tools/quest_gate/PARITY.tsv (create with header quest_dir\ttest_id\tsource\tstatus\tsha\tlegs_left\tnotes if absent): one row per quest in this pass -- status done when legs_left is empty, partial otherwise. (6) Reopen QUEUE rows as in (3); a quest with legs_left stays todo with the legs named. Commit QUEUE.tsv and push (SYNC again first). (7) There is no content lock to release in the batch-branch model; confirm instead that git -C ${WT} status -sb and git -C ${WT}/OSRS-Content status -sb both read "## ${BATCH}...origin/${BATCH}" with nothing ahead. (8) FINISH: write {"landed": true, "commit": "<parent sha>"} to ${STATE}/close.json, then return the schema.`, { label: 'close+land', model: 'opus', schema: LAND }))
}
if (!land) log('CLOSE DID NOT LAND: relaunch this pass with the same args')
return { pass, reports, failed, land }
