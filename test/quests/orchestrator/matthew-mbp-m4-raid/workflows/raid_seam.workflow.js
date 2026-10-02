export const meta = {
  name: 'raid-seam-pass',
  description: 'Resumable seam pass: triage, one Opus fixer per seam, an Opus closer -- every step persists under build/seam_state/<pass>/ and relaunching with the same args continues from disk',
  phases: [
    { title: 'State', detail: 'Sonnet: read what this pass has already persisted; takes the content lock' },
    { title: 'Triage', detail: 'Opus: group the seams of the tier 6 rows (RAID_ORCHESTRATOR.md section 4) (skipped when triage.json exists)' },
    { title: 'Fix', detail: 'one Opus agent per seam not yet done; each writes fix.<key>.json' },
    { title: 'Close', detail: 'Opus: audit, gates, sync with origin, commit, push, reopen rows, release the content lock; idempotent; writes close.json' },
  ],
}
// The resumable seam pass (2026-09-22). Paste this file's content inline to
// the Workflow tool with args:
//   { pass: "seam6", context: "<the current picture, one paragraph>",
//     reuse_triage: "docs/QUEST_SEAM_TRIAGE_....md" (optional),
//     reopen: [{id, note}] (optional extra rows for the closer) }
// Every step writes its result under build/seam_state/<pass>/ (triage.json,
// fix.<key>.json, fix.<key>.progress.md, close.progress.md, close.json), and
// the State phase reads them back, so a pass that is paused, killed, or
// starved by API errors is relaunched with the SAME args and continues: done
// seams are not re-fixed, a landed close is not re-landed. Never use
// resumeFromRunId with this script -- the disk is the resume.
//
// Rules carried over: fixers never touch test/quests/<quest>.lua or
// QUEUE.tsv; only the closer commits; no author batch runs alongside ON THIS
// MACHINE. Across machines (docs/QUEST_ORCHESTRATOR.md) a seam pass is a
// content pass: the State step takes the content lock (tools/quest_gate/
// claim.py content-lock <pass>) and the pass stops if another machine's
// content pass holds it; the closer syncs with origin before committing and
// releases the lock after its last push.

const WT = '/Users/matthewevers/Documents/git_repos/3draster/build/orchestrator/worktrees/raid'
const CONTENT = `${WT}/OSRS-Content/osrs239-content`
const pass = args && args.pass
// The batch branch (docs/QUEST_ORCHESTRATOR.md): <host>-b<N>, the pass name without its -parity / -seamN suffix, or args.branch.
const BATCH = (args && args.branch) || String(pass || '').replace(/-(parity|seam\d+)$/, '')
if (!pass) throw new Error('args.pass is required (e.g. "seam6"): it names build/seam_state/<pass>/')
const STATE = `${WT}/build/seam_state/${pass}`
const extraContext = (args && args.context) ? `\n\nCURRENT PICTURE: ${args.context}` : ''
const extraReopen = (args && Array.isArray(args.reopen)) ? args.reopen : []
const reuse = args && args.reuse_triage

const COMMON = `You are one worker in a multi-agent build. Work ONLY inside ${WT} (the RAID orchestrator's git worktree, on the batch branch ${BATCH} in both repos; the content is the OSRS-Content submodule at ${CONTENT}; cache.osrs239 there is a symlink to the owner's checkout -- never write into it). No author batch is running. Never delete build or cache directories, never run git clean/checkout/reset on paths you did not change. The owner's main checkout /Users/matthewevers/Documents/git_repos/3draster belongs to a QUEST orchestrator: never read its uncommitted files, never build in it, never run anything there. Absolute paths. FIRST read ${WT}/docs/RAID_ORCHESTRATOR.md (sections 2, 4 and 6) and ${WT}/docs/minigames/theater_of_blood/ENCOUNTER_TIMING.md section 1 (the T-1 rule), then ${WT}/docs/QUEST_SUITE_KIT.md and ${WT}/docs/QUEST_AUTHORING.md (the 20 KB core); look anything else up through ${WT}/docs/quest_authoring/INDEX.md or grep -rn over ${WT}/docs/quest_authoring/ when you need it, never by reading the topic files end to end; then the last seam pass's commit (git log --oneline -40 | grep 'quest-driver: seams') --stat and message. Edit ONLY the files assigned to you. Never git stash/checkout/reset/clean/amend, never git add -A. Never touch test/quests/<quest>.lua or QUEUE.tsv unless your brief says so. After ANY content edit: make -C ${WT}/src torirsserver-scripts (the embedded server refuses a stale pack). run.py refuses a second concurrent run of one quest id: give every scratch run its own --name, run it in the FOREGROUND and wait for it (never background a run or wait on a monitor). CLAUDE.md rules for C. The gate is behaviour: quote proving ledger rows and Read the PNGs. Report honestly.

PASS STATE DIR: ${STATE} (mkdir -p it). Every worker persists its result there so a paused or killed pass resumes from disk with nothing redone. Uncommitted test/quests/<quest>.lua files in the tree are author attempts: read-only for everyone in this pass.

THE GOAL, from the owner: the three raids played for real by the quest driver, tick-measured and defensible (docs/RAID_ORCHESTRATOR.md). THIS PASS is the driver seam of its section 4: every verb a raid room needs, proved on an ordinary npc, with a conformance row each, so that the tier 6 encounter rows (test/quests/QUEUE.tsv, ids tob_* cox_* toa_*) can be authored. Raid content scripts (OSRS-Content/.../minigames/minigame_tob etc.) are touched only where a seam row names them.${extraContext}`

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
// Every commit-and-push in this pass goes through this (docs/QUEST_ORCHESTRATOR.md "Closing").
const SYNC = `BATCH-BRANCH MODEL (docs/QUEST_ORCHESTRATOR.md): this pass runs on the batch branch ${BATCH} in BOTH repos (git -C ${WT} branch --show-current and git -C ${WT}/OSRS-Content branch --show-current must both print ${BATCH}; if not, stop and report). Never merge origin/v3 into the branch and never push to v3: v3 carries only the claim ledger and the batch reaches it through one PR at the end (claim.py done). Commit the submodule, stage the OSRS-Content gitlink in the parent, commit, then push BOTH branches: git -C ${WT}/OSRS-Content push -u origin ${BATCH} ; git -C ${WT} push -u origin ${BATCH}. A rejected push on the batch branch means another agent of THIS batch pushed first: git pull --no-rebase origin ${BATCH} in that repo, then push again. Never force, rebase, reset or stash.`

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

YOUR JOB: read this pass's persisted state, no edits, no verification, NO SUMMARISING. mkdir -p ${STATE}. Then: has_triage = whether ${STATE}/triage.json exists and parses; triage = EXACTLY its seams, one entry per entry in the file with key, kind, quests and files copied verbatim (summary and evidence may be shortened to their first sentence -- the fixers read the full text from the file themselves); never add, merge, drop or invent a seam, and never describe truncation as a seam. done = one entry per ${STATE}/fix.*.json that parses ({key, report}; report fields may be shortened likewise). closed = whether ${STATE}/close.json exists and says landed; close_commit = the sha it names, else "". THE CONTENT LOCK is not used in the batch-branch model (docs/QUEST_ORCHESTRATOR.md): this pass edits content on its own batch branch ${BATCH} and the pack alloc files are reconciled with v3 when the batch's PR is made (claim.py done). Set lock_exit 0 and lock_output "batch-branch model: no lock" without running anything. Return exactly the schema.`, { label: 'state', model: 'claude-sonnet-5-5', effort: 'low', schema: STATE_SCHEMA }))) || { has_triage: false, triage: { seams: [] }, done: [], closed: false, close_commit: '', lock_exit: -1, lock_output: 'the state agent returned nothing' }
if (!state.closed && state.lock_exit !== 0) throw new Error(`content lock not taken (claim.py exit ${state.lock_exit}): ${state.lock_output} -- exit 3: another machine's content pass holds test/quests/CONTENT_LOCK, wait for it to close; exit 2: this checkout is not level with origin/v3 (docs/QUEST_ORCHESTRATOR.md launch checklist)`)
log(`state: triage ${state.has_triage ? 'present' : 'absent'}, ${state.done.length} seam(s) done, closed=${state.closed}`)

phase('Triage')
let triage = state.has_triage ? state.triage : null
if (!triage) {
  triage = await attempt('triage', 2, () => agent(`${COMMON}

YOUR JOB: triage, no edits. ${reuse ? `Read ${WT}/${reuse} in full: it holds a completed triage (one "## <kind>: <key>" section per seam with Quests, Files, a summary paragraph and an Evidence line) -- transcribe it exactly into the schema.` : `For each tier 6 row (python3 tools/quest_gate/queue.py summary; the rows in test/quests/QUEUE.tsv with tier 6), read its last_failure, the committed test/quests/<id>.lua's t.blocked line and the evidence it cites (ledgers under build/quest_gate/<id>/, the .rs2 lines named). Group by SEAM and classify: driver, engine, content, design. CONTENT seams are fixable: name the exact OSRS-Content files, and for every content seam name the SOURCE that says what the real game does (the wiki-pinned docs/quests/<quest>.md, the 2004 LostCity source under /Users/matthewevers/Documents/git_repos/LostCity_Content2, or the OSRS wiki) -- a content edit with no source is not a seam, it is a guess. A row whose only remaining work is the AUTHOR's (a rewrite of the test file) is not a seam: list it under kind design with summary starting AUTHOR:. Every seam names its files; two seams never share a file (merge them if they must).`} BEFORE returning, write the schema JSON to ${STATE}/triage.json (python3 -c with json.dump). Return the schema.`, { label: reuse ? 'triage (transcribe)' : 'triage', model: reuse ? 'claude-sonnet-5-5' : 'opus', effort: reuse ? 'low' : undefined, schema: TRIAGE_SCHEMA }))
  if (!triage) throw new Error('triage produced no result after two tries; relaunch with the same args')
}
const doneKeys = new Set(state.done.map(d => d.key))
const fixable = triage.seams.filter(s => s.kind !== 'design')
const todo = fixable.filter(s => !doneKeys.has(s.key))
log(`triage: ${triage.seams.length} seams, ${fixable.length} fixable, ${todo.length} still to fix: ${todo.map(s => s.kind + ':' + s.key).join('; ') || 'none'}`)

phase('Fix')
const fresh = await parallel(todo.map(s => () => attempt(`fix:${s.key}`, 2, () => agent(`${COMMON}

YOUR JOB: fix ONE ${s.kind} seam, prove it, persist your report, do not commit. Seam "${s.key}", blocking ${s.quests.join(', ')}. READ YOUR SEAM'S FULL summary and evidence from ${STATE}/triage.json (the entry whose key is "${s.key}") before anything else; the short form: ${s.summary}. Files (yours alone): ${s.files.join(', ')}.
Seams being fixed concurrently, never touch their files: ${fixable.filter(x => x !== s).map(x => x.key + ' -> ' + x.files.join(',')).join(' | ') || 'none'}. Seams already fixed on disk in this pass (their edits are uncommitted in the tree; leave them alone): ${state.done.map(d => d.key).join(', ') || 'none'}.
RESUME DISCIPLINE: ${STATE}/fix.${s.key}.progress.md is your notebook. If it exists, a previous attempt at this seam was killed: read it first and continue from its last proved step -- never redo what it proved, and audit any edit it left in your files against its notes. Append to it after every meaningful step (what you changed, what you measured, the ledger row that proves it) so the next attempt can continue if you die. Your files may already carry that attempt's edits.
Rules by kind. DRIVER: Lua in script/plugins/quest_driver (C in src/plugin only if Lua cannot do it honestly); prove with a scratch script and two existing green quests. ENGINE: server/client C; keep the C selftests at their count (the make target test-torirsserver is RED at HEAD on a pre-existing servpack error -- run the selftest binary directly, HEAD baseline from a throwaway git worktree with its own PLATFORM_OBJ_BASE, compare failure counts before/after). CONTENT: edit the quest's own scripts/configs under ${CONTENT} (or the shared file the seam names) the way the original author would have, and ONLY to restore what the real game does -- cite the source (docs/quests/<quest>.md, the LostCity 2004 source under /Users/matthewevers/Documents/git_repos/LostCity_Content2, or the OSRS wiki) in your report; if no source says what the game does, do not invent it: report the seam as UNSOURCED and propose a test affordance (a cheat or driver verb) instead. The fix must compile (make -C ${WT}/src torirsserver-scripts) and keep that quest's C selftest green if one exists. For every kind the PROOF is through the driver: a scratch script (run.py --script <file> --name <unique label> --no-build; cheats as t.cheat inside run()) that reproduces the blocked row first, then passes after the fix, AND a copy of the committed test/quests/<id>.lua under build/ with its t.blocked removed driven as far as it now goes (say where it stops next). Regressions: run.py cooks_assistant druid --no-build (+QUEST_BINARY if C) and gate.py; make -C src check-drive-abi check-pt-switch. Do NOT run test-quest-conformance unless your seam names a conformance row.
FINISH: write your report as JSON {"key": "${s.key}", "report": <the schema>} to ${STATE}/fix.${s.key}.json (python3 json.dump), THEN return the schema with key = "${s.key}". unblocks = the quests whose t.blocked reason is gone, each verified by the copied file.`, { label: `fix:${s.kind}:${s.key.slice(0, 34)}`, model: 'opus', schema: REPORT }))))
const fixed = fresh.filter(Boolean)
const failed = todo.filter((s, i) => !fresh[i]).map(s => s.key)
log(`fix: ${fixed.length} of ${todo.length} reported this launch; ${failed.length ? 'NO REPORT from ' + failed.join(', ') + ' (relaunch with the same args to retry them)' : 'none missing'}`)

if (args && args.stop_after_fix) { log('stop_after_fix: relaunch without it to close'); return { pass, triage, fixed, failed, land: null } }

phase('Close')
let land = state.closed ? { commit: state.close_commit, pushed: true, gates: 'landed by a previous launch of this pass', reverted: [], reopened: [], open_issues: [], notes: 'close.json already present' } : null
if (!land) {
  land = await attempt('close', 2, () => agent(`${COMMON}

YOU ARE THE CLOSER (Opus). The pass's triage is ${STATE}/triage.json; every fix report is a ${STATE}/fix.*.json ({key, report}) -- read them ALL from disk (this launch produced ${fixed.length}; earlier launches left ${state.done.length}). Seams with no report: ${failed.join(', ') || 'none'} -- their files may carry an unfinished edit: judge it by the seam's progress notes ${STATE}/fix.<key>.progress.md and by a live run; keep it only if proved, else restore the file from git show HEAD:<path> (never checkout/reset) and say so.
RESUME DISCIPLINE: ${STATE}/close.progress.md is your notebook: if it exists, a previous closer was killed -- read it and continue from its last completed step (check git log for the tag [seam:${pass}] before committing again; check QUEUE.tsv before reopening again). Append to it after every step.
Do: (1) git status/diff in BOTH repos; read every changed file in full; judge each seam genuine (a content fix that cheats a stage instead of making the real branch reachable, or a content edit whose report cites no source for what the real game does, is reverted by rewriting the file from git show HEAD:<path>). (2) You own test/quests/_conformance.lua and tools/quest_gate/verb_list.py: a row for every new/changed verb; count bumped. (3) make -C src torirsserver-scripts compiles clean. If C changed, python3 tools/quest_gate/run.py --all --jobs 3 --no-publish rebuilds the shared binary; run it either way, then python3 tools/quest_gate/gate.py --allow-blocked -> every committed quest keeps or improves its bucket; list the ones that moved and WHY (uncommitted author attempts run their dirty copies -- not this pass's concern). make -C src test-quest-conformance test-quest-cheats check-quest-verbs check-drive-abi check-pt-switch test-plugin-lua -- conformance must be fully green before you commit; the C selftest binary if the server changed; lint on git ls-files test/quests/*.lua. (4) DOCS: fold the reports' doc_notes into the matching topic file (the folded doc lines are protocol work: PROTOCOL WORK GOES TO v3 DIRECTLY (owner, 2026-10-01; docs/QUEST_ORCHESTRATOR.md 'Changing the protocol or shared tooling'): the doc lines you add under docs/quest_authoring/ are protocol work, not batch work -- make them in a v3 worktree (git -C ${WT} fetch origin; git -C ${WT} worktree add --detach ${WT}/build/orchestrator/worktrees/v3doc-doc origin/v3), commit there by explicit path, git push origin HEAD:v3 (on a rejected push: fetch, rebase onto origin/v3, push again, never force), remove that worktree, then git cherry-pick the commit onto the batch branch so this batch's authors read it too.) under docs/quest_authoring/ (seam-facts.md for a fact this pass established, the verbs-*.md file for a verb's behaviour, traps-*.md for a trap's status; normal line lengths, a heading that names the symptom) and add ONE line per fact to docs/quest_authoring/INDEX.md keyed by what an author sees; docs/QUEST_AUTHORING.md is the core and stays under 25 KB -- change it only when a verb's one-line table entry or a rule itself changes; where a new fact supersedes an old one, mark the old one FIXED with the commit instead of leaving both. (5) ${SYNC} Commit the SUBMODULE first when content changed (git -C ${CONTENT}/.. add the changed content files by explicit path; message "raids: content seams the tier 6 rows named [seam:${pass}] -- <one clause per seam, each naming its source>", trailer "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"; git -C ${WT}/OSRS-Content push -u origin ${BATCH}), then the parent with explicit paths (driver Lua, C, tools, _conformance.lua, verb_list.py, docs, OSRS-Content gitlink), message "raid-driver: seams the tier 6 rows named [seam:${pass}] -- <one clause per seam>", same trailer; git push -u origin ${BATCH}. Never commit test/quests/<quest>.lua. Never git commit --amend, reset, or add -A. (6) Reopen every quest the reports list under unblocks: python3 tools/quest_gate/queue.py set <id> --status todo --failure "RETRY after <sha>: <the fix, one line; which rows must be rewritten>"; ALSO reopen these rows exactly as given: ${extraReopen.map(r => r.id + ' -> --status todo --failure "' + r.note + '"').join(' ; ') || 'none'}. Commit QUEUE.tsv ("raids: <n> tier 6 rows reopened after the seam pass [seam:${pass}]") and push (SYNC again first). (7) There is no content lock to release in the batch-branch model; confirm instead that git -C ${WT} status -sb and git -C ${WT}/OSRS-Content status -sb both read "## ${BATCH}...origin/${BATCH}" with nothing ahead. (8) FINISH: write {"landed": true, "commit": "<parent sha>", "reopened": [...]} to ${STATE}/close.json, then return the schema.`, { label: 'close+land', model: 'opus', schema: LAND }))
}
if (!land) log('CLOSE DID NOT LAND: relaunch this pass with the same args; the closer resumes from close.progress.md')
return { pass, triage, fixed, failed, land }
