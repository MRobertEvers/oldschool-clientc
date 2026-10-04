export const meta = {
  name: 'raid-seam-pass',
  description: 'Resumable seam pass: triage, one Opus fixer per seam, an Opus closer -- every step persists under build/seam_state/<pass>/ and relaunching with the same args continues from disk',
  phases: [
    { title: 'State', detail: 'Sonnet: read what this pass has already persisted; takes the content lock' },
    { title: 'Triage', detail: 'Opus: group the seams the raid rooms need (RAID_ORCHESTRATOR.md section 4) (skipped when triage.json exists)' },
    { title: 'Fix', detail: 'one Opus agent per seam not yet done; each writes fix.<key>.json' },
    { title: 'Close', detail: 'Opus: audit, gates, conformance rows, DRIVER_NOTES, commit, push; idempotent; writes close.json' },
  ],
}
// The resumable seam pass (2026-09-22). Paste this file's content inline to
// the Workflow tool with args:
//   { pass: "seam6", context: "<the current picture, one paragraph>",
//     reuse_triage: "docs/QUEST_SEAM_TRIAGE_....md" (optional),
// Every step writes its result under build/seam_state/<pass>/ (triage.json,
// fix.<key>.json, fix.<key>.progress.md, close.progress.md, close.json), and
// the State phase reads them back, so a pass that is paused, killed, or
// starved by API errors is relaunched with the SAME args and continues: done
// seams are not re-fixed, a landed close is not re-landed. Never use
// resumeFromRunId with this script -- the disk is the resume.
//
// Rules carried over: fixers never touch test/quests/<quest>.lua; only the
// closer commits; no other pass runs in this worktree at the same time. The raid
// loop is NOT the quest loop (owner, 2026-10-02): no QUEUE.tsv, no claims, no
// content lock, nothing pushed to v3; this branch reaches v3 by one PR.

const WT = '/Users/matthewevers/Documents/git_repos/3draster/build/orchestrator/worktrees/raid'
const CONTENT = `${WT}/OSRS-Content/osrs239-content`
const pass = args && args.pass
// The raid branch: the pass name without its -seamN / -spec suffix, or args.branch.
const BATCH = (args && args.branch) || String(pass || '').replace(/-(parity|spec|seam\d+)$/, '')
if (!pass) throw new Error('args.pass is required (e.g. "seam6"): it names build/seam_state/<pass>/')
const STATE = `${WT}/build/seam_state/${pass}`
const extraContext = (args && args.context) ? `\n\nCURRENT PICTURE: ${args.context}` : ''
const reuse = args && args.reuse_triage

const COMMON = `You are one worker in a multi-agent build. Work ONLY inside ${WT} (the RAID orchestrator's git worktree, on the batch branch ${BATCH} in both repos; the content is the OSRS-Content submodule at ${CONTENT}; cache.osrs239 there is a symlink to the owner's checkout -- never write into it). No author batch is running. Never delete build or cache directories, never run git clean/checkout/reset on paths you did not change. The owner's main checkout /Users/matthewevers/Documents/git_repos/3draster belongs to a QUEST orchestrator: never read its uncommitted files, never build in it, never run anything there. Absolute paths. FIRST read ${WT}/docs/RAID_ORCHESTRATOR.md (sections 2, 4 and 6) and ${WT}/docs/minigames/theater_of_blood/ENCOUNTER_TIMING.md section 1 (the T-1 rule), then ${WT}/docs/QUEST_SUITE_KIT.md and ${WT}/docs/QUEST_AUTHORING.md (the 20 KB core); look anything else up through ${WT}/docs/quest_authoring/INDEX.md or grep -rn over ${WT}/docs/quest_authoring/ when you need it, never by reading the topic files end to end; then the last seam pass's commit (git log --oneline -40 | grep 'quest-driver: seams') --stat and message. Edit ONLY the files assigned to you. Never git stash/checkout/reset/clean/amend, never git add -A. Never touch test/quests/<quest>.lua or test/quests/QUEUE.tsv. After ANY content edit: make -C ${WT}/src torirsserver-scripts (the embedded server refuses a stale pack). run.py refuses a second concurrent run of one quest id: give every scratch run its own --name, run it in the FOREGROUND and wait for it (never background a run or wait on a monitor). CLAUDE.md rules for C. The gate is behaviour: quote proving ledger rows and Read the PNGs. Report honestly. OUTPUT DISCIPLINE (owner, 2026-10-03; the editor crashed and took every running pass with it): no command prints more than about 4 KB into a tool result. Builds, runs, suites and diffs go to a log file under your scratch dir and you read its tail or grep it (cmd > log 2>&1; tail -40 log); git diff --stat first, then one file through sed -n with a line range; -q on git commands that print progress; cut -c1-300 on anything with long lines; never cat a ledger, a ticklog or a spec table whole (grep -n the rows you need); no single shell command longer than about 8 KB (write a long file in several appends).
SEARCH DISCIPLINE (owner, 2026-10-03; a session crashed on this): never run a recursive grep or find over the whole OSRS-Content tree or over osrs239-content/ (about 238,000 files) -- scope every search to a named directory and file type and cap the output (grep -rn --include='*.rs2' '<symbol>' <one directory> | head -40; never pack/, cache or map directories; never without --include); match whole symbols (word boundaries) and numeric ids separately, so tob_bloat does not match tob_bloat_hard and 809 does not match 8091.

PASS STATE DIR: ${STATE} (mkdir -p it). Every worker persists its result there so a paused or killed pass resumes from disk with nothing redone. Uncommitted test/quests/<quest>.lua files in the tree are author attempts: read-only for everyone in this pass.

THE GOAL, from the owner: the three raids played for real by the quest driver, tick-measured and defensible (docs/RAID_ORCHESTRATOR.md). THIS PASS is the driver seam of its section 4: every verb a raid room needs, proved on an ordinary npc, with a conformance row each, so that the encounter tests (test/raids/tob_*.lua, cox_*.lua, toa_*.lua) can be authored. The raid loop never touches the quest loop: no QUEUE.tsv, no claims, no docs/quest_authoring/, nothing pushed to v3. Raid content scripts (OSRS-Content/.../minigames/minigame_tob etc.) are touched only where a seam row names them.${extraContext}`

const SEAM = { type: 'object', properties: {
  key: { type: 'string' }, kind: { type: 'string', enum: ['driver', 'engine', 'content', 'design'] },
  quests: { type: 'array', items: { type: 'string' } }, summary: { type: 'string' }, evidence: { type: 'string' },
  files: { type: 'array', items: { type: 'string' } },
}, required: ['key', 'kind', 'quests', 'summary', 'evidence', 'files'] }
const TRIAGE_SCHEMA = { type: 'object', properties: { seams: { type: 'array', items: SEAM } }, required: ['seams'] }
const REPORT = { type: 'object', properties: { key: { type: 'string' }, summary: { type: 'string' }, files_changed: { type: 'array', items: { type: 'string' } }, verified_by: { type: 'string' }, open_issues: { type: 'array', items: { type: 'string' } }, unblocks: { type: 'array', items: { type: 'string' } }, doc_notes: { type: 'array', items: { type: 'string' } } }, required: ['key', 'summary', 'files_changed', 'verified_by', 'open_issues', 'unblocks', 'doc_notes'] }
const LAND = { type: 'object', properties: { commit: { type: 'string' }, pushed: { type: 'boolean' }, gates: { type: 'string' }, reverted: { type: 'array', items: { type: 'string' } }, reopened: { type: 'array', items: { type: 'string' } }, open_issues: { type: 'array', items: { type: 'string' } }, notes: { type: 'string' } }, required: ['commit', 'pushed', 'gates', 'reverted', 'reopened', 'open_issues', 'notes'] }
const STATE_SCHEMA = { type: 'object', properties: {
  has_triage: { type: 'boolean' }, triage: TRIAGE_SCHEMA,
  done: { type: 'array', items: { type: 'object', properties: { key: { type: 'string' }, report: REPORT }, required: ['key', 'report'] } },
  closed: { type: 'boolean' }, close_commit: { type: 'string' }, lock_exit: { type: 'integer' }, lock_output: { type: 'string' },
}, required: ['has_triage', 'triage', 'done', 'closed', 'close_commit', 'lock_exit', 'lock_output'] }
// Every commit-and-push in this pass goes through this.
const SYNC = `BRANCH MODEL: this pass runs on the raid branch ${BATCH} in BOTH repos (git -C ${WT} branch --show-current and git -C ${WT}/OSRS-Content branch --show-current must both print ${BATCH}; if not, stop and report). Never merge origin/v3 into the branch and never push to v3: the raid branch reaches v3 through one PR when the owner asks for it. Commit the submodule, stage the OSRS-Content gitlink in the parent, commit, then push BOTH branches: git -C ${WT}/OSRS-Content push -u origin ${BATCH} ; git -C ${WT} push -u origin ${BATCH}. A rejected push on the batch branch means another agent of THIS batch pushed first: git pull --no-rebase origin ${BATCH} in that repo, then push again. Never force, rebase, reset or stash.`

// Every agent call goes through this: an API death or a stall returns null
// instead of throwing, and a null is retried up to `tries` times.
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

YOUR JOB: read this pass's persisted state, no edits, no verification, NO SUMMARISING. mkdir -p ${STATE}. Then: has_triage = whether ${STATE}/triage.json exists and parses; triage = EXACTLY its seams, one entry per entry in the file with key, kind, quests and files copied verbatim (summary and evidence may be shortened to their first sentence -- the fixers read the full text from the file themselves); never add, merge, drop or invent a seam, and never describe truncation as a seam. done = one entry per ${STATE}/fix.*.json that parses ({key, report}; report fields may be shortened likewise). closed = whether ${STATE}/close.json exists and says landed; close_commit = the sha it names, else "". There is no content lock: this pass edits content on its own branch ${BATCH}. Set lock_exit 0 and lock_output "raid branch: no lock" without running anything. Return exactly the schema.`, { label: 'state', model: 'claude-sonnet-5-5', effort: 'low', schema: STATE_SCHEMA }))) || { has_triage: false, triage: { seams: [] }, done: [], closed: false, close_commit: '', lock_exit: -1, lock_output: 'the state agent returned nothing' }
if (!state.closed && state.lock_exit !== 0) throw new Error(`state agent failed (${state.lock_exit}): ${state.lock_output}`)
log(`state: triage ${state.has_triage ? 'present' : 'absent'}, ${state.done.length} seam(s) done, closed=${state.closed}`)

phase('Triage')
let triage = state.has_triage ? state.triage : null
if (!triage) {
  triage = await attempt('triage', 2, () => agent(`${COMMON}

YOUR JOB: triage, no edits. ${reuse ? `Read ${WT}/${reuse} in full: it holds a completed triage (one "## <kind>: <key>" section per seam with Quests, Files, a summary paragraph and an Evidence line) -- transcribe it exactly into the schema.` : `Read the newest ${WT}/docs/minigames/raid_loop/SEAM_TRIAGE_*.md in full and transcribe it exactly into the schema (the raid loop always writes its triage by hand; a pass without one is a launch error).`} BEFORE returning, write the schema JSON to ${STATE}/triage.json (python3 -c with json.dump). Return the schema.`, { label: reuse ? 'triage (transcribe)' : 'triage', model: reuse ? 'claude-sonnet-5-5' : 'opus', effort: reuse ? 'low' : undefined, schema: TRIAGE_SCHEMA }))
  if (!triage) throw new Error('triage produced no result after two tries; relaunch with the same args')
}
// A triage with no seams is a lost result, never a finished pass (seam10, 2026-10-03: the
// triage agent wrote four seams to triage.json and returned none, no fixer ran, and the
// closer spent a whole suite closing nothing). The State phase reads the file next launch.
if (!triage.seams || triage.seams.length === 0) throw new Error('triage returned no seams; if ' + STATE + '/triage.json holds them, relaunch with the same args (the State phase reads it)')
const doneKeys = new Set(state.done.map(d => d.key))
const fixable = triage.seams.filter(s => s.kind !== 'design')
const todo = fixable.filter(s => !doneKeys.has(s.key))
log(`triage: ${triage.seams.length} seams, ${fixable.length} fixable, ${todo.length} still to fix: ${todo.map(s => s.kind + ':' + s.key).join('; ') || 'none'}`)

phase('Fix')
const jobs = todo.map(s => () => attempt(`fix:${s.key}`, 2, () => agent(`${COMMON}

YOUR JOB: fix ONE ${s.kind} seam, prove it, persist your report, do not commit. Seam "${s.key}", blocking ${s.quests.join(', ')}. READ YOUR SEAM'S FULL summary and evidence from ${STATE}/triage.json (the entry whose key is "${s.key}") before anything else; the short form: ${s.summary}. Files (yours alone): ${s.files.join(', ')}.
Seams being fixed concurrently, never touch their files: ${fixable.filter(x => x !== s).map(x => x.key + ' -> ' + x.files.join(',')).join(' | ') || 'none'}. Seams already fixed on disk in this pass (their edits are uncommitted in the tree; leave them alone): ${state.done.map(d => d.key).join(', ') || 'none'}.
RESUME DISCIPLINE: ${STATE}/fix.${s.key}.progress.md is your notebook. If it exists, a previous attempt at this seam was killed: read it first and continue from its last proved step -- never redo what it proved, and audit any edit it left in your files against its notes. Append to it after every meaningful step (what you changed, what you measured, the ledger row that proves it) so the next attempt can continue if you die. Your files may already carry that attempt's edits.
Rules by kind. DRIVER: Lua in script/plugins/quest_driver (C in src/plugin only if Lua cannot do it honestly); prove with a scratch script and two existing green quests. ENGINE: server/client C; keep the C selftests at their count (the make target test-torirsserver is RED at HEAD on a pre-existing servpack error -- run the selftest binary directly, HEAD baseline from a throwaway git worktree with its own PLATFORM_OBJ_BASE, compare failure counts before/after). CONTENT: edit the quest's own scripts/configs under ${CONTENT} (or the shared file the seam names) the way the original author would have, and ONLY to restore what the real game does -- cite the source (docs/quests/<quest>.md, the LostCity 2004 source under /Users/matthewevers/Documents/git_repos/LostCity_Content2, or the OSRS wiki) in your report; if no source says what the game does, do not invent it: report the seam as UNSOURCED and propose a test affordance (a cheat or driver verb) instead. The fix must compile (make -C ${WT}/src torirsserver-scripts) and keep that quest's C selftest green if one exists. For every kind the PROOF is through the driver: a scratch script (run.py --script <file> --name <unique label> --no-build; cheats as t.cheat inside run()) that reproduces the blocked row first, then passes after the fix, AND a copy of the committed test/quests/<id>.lua under build/ with its t.blocked removed driven as far as it now goes (say where it stops next). Regressions: run.py cooks_assistant druid --no-build (+QUEST_BINARY if C) and gate.py; make -C src check-drive-abi check-pt-switch. Do NOT run test-quest-conformance. CONFORMANCE ROWS: for every verb you add or change, write its step("<ns>.<verb>", function() ... end) row (and any seam("seam.<name>", ...) row) as a Lua snippet in ${STATE}/conformance.${s.key}.lua, with a comment naming the place in _conformance.lua's PLAN it belongs (after which existing step); the closer merges them -- never edit test/quests/_conformance.lua or tools/quest_gate/verb_list.py yourself. DOC NOTES: your doc_notes are folded by the closer into ${WT}/docs/minigames/raid_loop/DRIVER_NOTES.md on this branch -- never into docs/quest_authoring/ or docs/QUEST_AUTHORING.md, which the quest loop owns and which this pass never touches.
FINISH: write your report as JSON {"key": "${s.key}", "report": <the schema>} to ${STATE}/fix.${s.key}.json (python3 json.dump), THEN return the schema with key = "${s.key}". unblocks = the quests whose t.blocked reason is gone, each verified by the copied file.`, { label: `fix:${s.kind}:${s.key.slice(0, 34)}`, model: 'opus', schema: REPORT })))
// At most WIDTH fixers at a time (owner, 2026-10-03: many agents at once hung the editor;
// 2026-10-04 the editor's file hooks stopped answering under three fixers and a closer).
const WIDTH = (args && args.width) || 2
const fresh = []
for (let i = 0; i < jobs.length; i += WIDTH) fresh.push(...await parallel(jobs.slice(i, i + WIDTH)))
const fixed = fresh.filter(Boolean)
const failed = todo.filter((s, i) => !fresh[i]).map(s => s.key)
log(`fix: ${fixed.length} of ${todo.length} reported this launch; ${failed.length ? 'NO REPORT from ' + failed.join(', ') + ' (relaunch with the same args to retry them)' : 'none missing'}`)

if (args && args.stop_after_fix) { log('stop_after_fix: relaunch without it to close'); return { pass, triage, fixed, failed, land: null } }

phase('Close')
let land = state.closed ? { commit: state.close_commit, pushed: true, gates: 'landed by a previous launch of this pass', reverted: [], reopened: [], open_issues: [], notes: 'close.json already present' } : null
if (!land) {
  land = await attempt('close', 2, () => agent(`${COMMON}

YOU ARE THE CLOSER (Opus). The pass's triage is ${STATE}/triage.json; every fix report is a ${STATE}/fix.*.json ({key, report}) -- read them ALL from disk (this launch produced ${fixed.length}; earlier launches left ${state.done.length}). Seams with no report: ${failed.join(', ') || 'none'} -- their files may carry an unfinished edit: judge it by the seam's progress notes ${STATE}/fix.<key>.progress.md and by a live run; keep it only if proved, else restore the file from git show HEAD:<path> (never checkout/reset) and say so.
RESUME DISCIPLINE: ${STATE}/close.progress.md is your notebook: if it exists, a previous closer was killed -- read it and continue from its last completed step (check git log for the tag [seam:${pass}] before committing again). Append to it after every step.
Do: (1) git status/diff in BOTH repos; read every changed file in full; judge each seam genuine (a content fix that cheats a stage instead of making the real branch reachable, or a content edit whose report cites no source for what the real game does, is reverted by rewriting the file from git show HEAD:<path>). (2) You own test/quests/_conformance.lua and tools/quest_gate/verb_list.py: merge every ${STATE}/conformance.*.lua snippet into the PLAN at the place its comment names, one row per new/changed verb, both counts bumped (-- @verb-count with VERB_COUNT, -- @seam-count with SEAM_COUNT); every merged row must PASS live. (3) make -C src torirsserver-scripts compiles clean. If C changed, python3 tools/quest_gate/run.py --all --jobs 3 --no-publish rebuilds the shared binary; run it either way, then python3 tools/quest_gate/gate.py --allow-blocked -> every committed quest keeps or improves its bucket; list the ones that moved and WHY (uncommitted author attempts run their dirty copies -- not this pass's concern). make -C src test-quest-conformance test-quest-cheats check-quest-verbs check-drive-abi check-pt-switch test-plugin-lua -- conformance must be fully green before you commit; the C selftest binary if the server changed; lint on git ls-files test/quests/*.lua. (4) DOCS: fold the reports' doc_notes into ${WT}/docs/minigames/raid_loop/DRIVER_NOTES.md (create it with a one-paragraph header if absent; one heading per verb or fact that names what a test author sees; normal line lengths) on THIS branch, committed with the parent commit below. Never touch docs/quest_authoring/, docs/QUEST_AUTHORING.md or any v3 worktree, and never push to v3: the raid loop pushes nothing to v3 (owner, 2026-10-02). (5) ${SYNC} Commit the SUBMODULE first when content changed (git -C ${CONTENT}/.. add the changed content files by explicit path; message "raids: content seams the raid rooms need [seam:${pass}] -- <one clause per seam, each naming its source>", trailer "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"; git -C ${WT}/OSRS-Content push -u origin ${BATCH}), then the parent with explicit paths (driver Lua, C, tools, _conformance.lua, verb_list.py, docs, OSRS-Content gitlink), message "raid-driver: seams the raid rooms need [seam:${pass}] -- <one clause per seam>", same trailer; git push -u origin ${BATCH}. Never commit test/quests/<quest>.lua. Never git commit --amend, reset, or add -A. (6) Nothing is reopened and no QUEUE.tsv is written: the raid loop keeps no queue (owner, 2026-10-02). Write one line per seam (key; landed or not; what remains open) to ${WT}/docs/minigames/raid_loop/SEAM_LEDGER.md under a heading for this pass, commit it on the branch by explicit path and push (SYNC again first). (7) Confirm that git -C ${WT} status -sb and git -C ${WT}/OSRS-Content status -sb both read "## ${BATCH}...origin/${BATCH}" with nothing ahead. (8) FINISH: write {"landed": true, "commit": "<parent sha>"} to ${STATE}/close.json, then return the schema.`, { label: 'close+land', model: 'opus', schema: LAND }))
}
if (!land) log('CLOSE DID NOT LAND: relaunch this pass with the same args; the closer resumes from close.progress.md')
return { pass, triage, fixed, failed, land }
