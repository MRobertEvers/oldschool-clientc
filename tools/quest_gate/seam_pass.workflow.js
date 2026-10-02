export const meta = {
  name: 'quest-seam-pass',
  description: 'Resumable seam pass: triage, one Opus fixer per seam, an Opus closer -- every step persists under build/seam_state/<pass>/ and relaunching with the same args continues from disk',
  phases: [
    { title: 'State', detail: 'Sonnet: read what this pass has already persisted; on v3 takes the content lock, on a batch branch checks the batch holds its quests' },
    { title: 'Triage', detail: 'Opus: group the non-green tier 1 rows (batch mode: the batch\'s rows) by seam (skipped when triage.json exists)' },
    { title: 'Fix', detail: 'one Opus agent per seam not yet done; each writes fix.<key>.json' },
    { title: 'Close', detail: 'Opus: audit, gates, commit, reopen rows; on v3 sync+push to v3 and release the lock, on a batch branch claim.py pr-prepare --push to the branch; idempotent; writes close.json' },
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
//
// BATCH MODE (2026-10-01, docs/QUEST_ORCHESTRATOR.md): args { batch:
// "mac1-b49", round: N, quests: [...] (optional: default every row the batch
// holds), context } -- the pass runs INSIDE a batch of quests this machine
// claimed with claim.py start, on the batch's branch in both repos, named
// <batch>-seam<N>. Triage covers the batch's rows only. No content lock
// (args.whole_pack: true takes it anyway); the State step checks claim.py
// status --batch instead. The closer commits on the branch and runs claim.py
// pr-prepare <batch> --push; reopened rows go into the BRANCH's QUEUE.tsv.
// Without args.round this is the v3 pass above, and it refuses any branch
// but v3 (and batch mode refuses v3).

const WT = '/Users/matthewevers/Documents/git_repos/3draster'
const CONTENT = `${WT}/OSRS-Content/osrs239-content`
const round = args && args.round
const batchMode = round !== undefined && round !== null
const questBatch = args && args.batch
if (batchMode && !questBatch) throw new Error('batch mode (args.round) needs args.batch: the batch of quests claim.py start claimed')
const pass = batchMode ? `${questBatch}-seam${round}` : (args && args.pass)
const takeLock = !batchMode || !!(args && args.whole_pack)
const batchQuests = (args && Array.isArray(args.quests)) ? args.quests : []
if (!pass) throw new Error('args.pass is required (e.g. "seam6"): it names build/seam_state/<pass>/')
const STATE = `${WT}/build/seam_state/${pass}`
const extraContext = (args && args.context) ? `\n\nCURRENT PICTURE: ${args.context}` : ''
const extraReopen = (args && Array.isArray(args.reopen)) ? args.reopen : []
const reuse = args && args.reuse_triage

const COMMON = `You are one worker in a multi-agent build. Work ONLY inside ${WT} (${batchMode ? `on batch ${questBatch}'s branch in BOTH repos -- never v3` : 'branch v3'}; the content is the OSRS-Content submodule at ${CONTENT}). No author batch is running. (the owner's checkout; the 2026-09-25 disk cleanup deleted the old worktree, so this checkout IS the working tree now -- never delete build or cache directories, never run git clean/checkout/reset on paths you did not change). Absolute paths. FIRST read ${WT}/docs/QUEST_SUITE_KIT.md and ${WT}/docs/QUEST_AUTHORING.md (the 20 KB core); look anything else up through ${WT}/docs/quest_authoring/INDEX.md or grep -rn over ${WT}/docs/quest_authoring/ when you need it, never by reading the topic files end to end; then the last seam pass's commit (git log --oneline -40 | grep 'quest-driver: seams') --stat and message. Edit ONLY the files assigned to you. Never git stash/checkout/reset/clean/amend, never git add -A. Never touch test/quests/<quest>.lua or QUEUE.tsv unless your brief says so. After ANY content edit: make -C ${WT}/src torirsserver-scripts (the embedded server refuses a stale pack). run.py refuses a second concurrent run of one quest id: give every scratch run its own --name, run it in the FOREGROUND and wait for it (never background a run or wait on a monitor). CLAUDE.md rules for C. The gate is behaviour: quote proving ledger rows and Read the PNGs. Report honestly.

PASS STATE DIR: ${STATE} (mkdir -p it). Every worker persists its result there so a paused or killed pass resumes from disk with nothing redone. Uncommitted test/quests/<quest>.lua files in the tree are author attempts: read-only for everyone in this pass.

THE GOAL, from the owner: every tier 1 quest green.${batchMode ? ` THIS PASS serves batch ${questBatch}: its quests are ${batchQuests.length ? batchQuests.join(', ') : 'every row python3 ' + WT + '/tools/quest_gate/claim.py status lists under ' + questBatch}.` : ''}${extraContext}`

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
  branch: { type: 'string' }, gate_exit: { type: 'integer' }, gate_output: { type: 'string' },
}, required: ['has_triage', 'triage', 'done', 'closed', 'close_commit', 'lock_exit', 'lock_output', 'branch', 'gate_exit', 'gate_output'] }
// Every commit-and-push in this pass goes through this (docs/QUEST_ORCHESTRATOR.md "Closing").
const V3_SYNC = `SYNC WITH ORIGIN BEFORE YOU COMMIT -- several machines push to v3 (docs/QUEST_ORCHESTRATOR.md "Closing"): (a) git -C ${WT}/OSRS-Content fetch origin && git -C ${WT}/OSRS-Content merge --no-edit origin/v3 ; (b) git -C ${WT} fetch origin && git -C ${WT} merge --no-edit origin/v3. If git refuses a merge because a file you edited would be overwritten (test/quests/QUEUE.tsv is the usual one: other machines' claims land there), commit that file first and merge after -- the clash then becomes a conflict. A conflict in test/quests/QUEUE.tsv, test/quests/BATCHES.tsv or tools/quest_gate/PARITY.tsv: python3 ${WT}/tools/quest_gate/claim.py merge-tsv <each conflicted tsv> then git commit --no-edit (one row per test_id/batch; for QUEUE.tsv a verdict beats claimed beats todo; otherwise ours). A conflict in a file this pass changed: resolve it by hand keeping both sides' work and re-run the gate it touches; a conflict anywhere else: git merge --abort and report it. Then commit the submodule, stage the OSRS-Content gitlink of the MERGED submodule in the parent, commit, and push both (git -C ${WT}/OSRS-Content push origin HEAD:v3 ; git -C ${WT} push origin v3). A rejected push: fetch and merge again ((a)-(b)) once, then push. Never force, rebase, reset or stash.`
// Batch mode: the work stays on the batch branch; claim.py pr-prepare merges v3 in and pushes the branch.
const BATCH_SYNC = `SYNC (BATCH MODE, docs/QUEST_ORCHESTRATOR.md "Closing on a batch branch"): this checkout and its OSRS-Content are on batch ${questBatch}'s branch, never v3 -- nothing this pass does is pushed to v3 (only claim.py writes claims there). Commit by explicit path, the SUBMODULE first, then the parent with the OSRS-Content gitlink, WITHOUT pushing; then run python3 ${WT}/tools/quest_gate/claim.py pr-prepare ${questBatch} --push in the FOREGROUND: it fetches both repos, merges origin/v3 into the batch branch (QUEUE/BATCHES/PARITY.tsv row by row; OSRS-Content pack/*.alloc by v3's copy plus a re-allocation and a pack rebuild; the gitlink), commits the merge and pushes both repos to origin <branch>. Exit 0 = pushed. Exit 2 = refused with the reason: a conflict in a file this pass changed is yours -- resolve it by hand keeping both sides' work, commit, run pr-prepare again; any other conflict: stop and report. Never push to v3, never force, rebase, reset or stash.`
const SYNC = batchMode ? BATCH_SYNC : V3_SYNC
const PUSH_SUB = batchMode ? 'no push here' : 'push origin HEAD:v3'
const PUSH_PARENT = batchMode ? `then run python3 ${WT}/tools/quest_gate/claim.py pr-prepare ${questBatch} --push (exit 0 = both repos pushed to the batch branch)` : 'git push origin v3'
const PUSH_AGAIN = batchMode ? `run python3 ${WT}/tools/quest_gate/claim.py pr-prepare ${questBatch} --push again` : 'push (SYNC again first)'
const UNLOCK_STEP = !takeLock ? `(7) NO CONTENT LOCK in batch mode: nothing to release, and never run claim.py release -- the batch keeps its quests claimed on v3 until the orchestrator runs claim.py done.` : batchMode ? `(7) RELEASE THE CONTENT LOCK (args.whole_pack) once pr-prepare has pushed: python3 ${WT}/tools/quest_gate/claim.py content-unlock ${pass} (it commits and pushes test/quests/CONTENT_LOCK to v3 itself; exit 0 = free). Never run claim.py release.` : `(7) RELEASE THE CONTENT LOCK once everything is pushed and git rev-list --count origin/v3..HEAD prints 0 in both repos: python3 ${WT}/tools/quest_gate/claim.py content-unlock ${pass} (it commits and pushes test/quests/CONTENT_LOCK itself; exit 0 = free).`
const LOCK_STEP = takeLock ? `THE CONTENT LOCK (${batchMode ? 'args.whole_pack: a change no batch branch could merge' : 'one content pass at a time across every machine'}): if closed is true, or ${WT}/test/quests/CONTENT_LOCK already starts with "${pass}@", set lock_exit 0 and lock_output to that line without running anything; otherwise run python3 ${WT}/tools/quest_gate/claim.py content-lock ${pass} in the foreground and report its exit code as lock_exit and its output (stdout and stderr, verbatim, at most 600 characters) as lock_output -- never retry it, never edit CONTENT_LOCK by hand.` : 'NO CONTENT LOCK (batch mode): set lock_exit 0 and lock_output "batch mode: no lock" without running anything.'
const GATE_STEP = `BRANCH: branch = the output of git -C ${WT} branch --show-current, verbatim (empty when detached). ${batchMode ? `THE BATCH GATE: run python3 ${WT}/tools/quest_gate/claim.py status --batch ${questBatch}${batchQuests.length ? ' --require ' + batchQuests.join(' ') : ''} in the foreground; gate_exit = its exit code, gate_output = its LAST line verbatim.` : 'gate_exit 0 and gate_output "" without running anything.'}`
const TRIAGE_ROWS = batchMode ? `For each row of batch ${questBatch} (${batchQuests.length ? batchQuests.join(', ') : 'python3 ' + WT + '/tools/quest_gate/claim.py status lists them under ' + questBatch} -- and no other row; read each one on THIS branch with python3 tools/quest_gate/queue.py show <id>)` : 'For each non-green tier 1 row (python3 tools/quest_gate/queue.py summary; the rows in test/quests/QUEUE.tsv with status blocked/content_bug/todo and tier 1)'

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

YOUR JOB: read this pass's persisted state, no edits, no verification, NO SUMMARISING. mkdir -p ${STATE}. Then: has_triage = whether ${STATE}/triage.json exists and parses; triage = EXACTLY its seams, one entry per entry in the file with key, kind, quests and files copied verbatim (summary and evidence may be shortened to their first sentence -- the fixers read the full text from the file themselves); never add, merge, drop or invent a seam, and never describe truncation as a seam. done = one entry per ${STATE}/fix.*.json that parses ({key, report}; report fields may be shortened likewise). closed = whether ${STATE}/close.json exists and says landed; close_commit = the sha it names, else "". ${GATE_STEP} ${LOCK_STEP} Return exactly the schema.`, { label: 'state', model: 'claude-sonnet-5-5', effort: 'low', schema: STATE_SCHEMA }))) || { has_triage: false, triage: { seams: [] }, done: [], closed: false, close_commit: '', lock_exit: -1, lock_output: 'the state agent returned nothing', branch: '', gate_exit: -1, gate_output: 'the state agent returned nothing' }
if (batchMode && state.branch === 'v3') throw new Error(`batch mode (args.round) runs on batch ${questBatch}'s branch, and this checkout is on v3: python3 tools/quest_gate/claim.py start ${questBatch} <ids...> creates it in both repos (docs/QUEST_ORCHESTRATOR.md)`)
if (!batchMode && state.branch !== 'v3') throw new Error(`this checkout is on '${state.branch}', not v3: a pass on a batch branch is launched in batch mode (args { batch, round }); without args.round it would push to v3 from the wrong branch`)
if (batchMode && state.gate_exit !== 0) throw new Error(`batch gate failed (claim.py status exit ${state.gate_exit}): ${state.gate_output} -- exit 3: the batch does not hold its quests on v3; exit 2: wrong branch`)
if (takeLock && !state.closed && state.lock_exit !== 0) throw new Error(`content lock not taken (claim.py exit ${state.lock_exit}): ${state.lock_output} -- exit 3: another machine's content pass holds test/quests/CONTENT_LOCK, wait for it to close; exit 2: this checkout is not level with origin/v3 (docs/QUEST_ORCHESTRATOR.md launch checklist)`)
log(`state: triage ${state.has_triage ? 'present' : 'absent'}, ${state.done.length} seam(s) done, closed=${state.closed}`)

phase('Triage')
let triage = state.has_triage ? state.triage : null
if (!triage) {
  triage = await attempt('triage', 2, () => agent(`${COMMON}

YOUR JOB: triage, no edits. ${reuse ? `Read ${WT}/${reuse} in full: it holds a completed triage (one "## <kind>: <key>" section per seam with Quests, Files, a summary paragraph and an Evidence line) -- transcribe it exactly into the schema.` : `${TRIAGE_ROWS}, read its last_failure, the committed test/quests/<id>.lua's t.blocked line and the evidence it cites (ledgers under build/quest_gate/<id>/, the .rs2 lines named). Group by SEAM and classify: driver, engine, content, design. CONTENT seams are fixable: name the exact OSRS-Content files, and for every content seam name the SOURCE that says what the real game does (the wiki-pinned docs/quests/<quest>.md, the 2004 LostCity source under /Users/matthewevers/Documents/git_repos/LostCity_Content2, or the OSRS wiki) -- a content edit with no source is not a seam, it is a guess. A row whose only remaining work is the AUTHOR's (a rewrite of the test file) is not a seam: list it under kind design with summary starting AUTHOR:. Every seam names its files; two seams never share a file (merge them if they must).`} BEFORE returning, write the schema JSON to ${STATE}/triage.json (python3 -c with json.dump). Return the schema.`, { label: reuse ? 'triage (transcribe)' : 'triage', model: reuse ? 'claude-sonnet-5-5' : 'opus', effort: reuse ? 'low' : undefined, schema: TRIAGE_SCHEMA }))
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
Do: (1) git status/diff in BOTH repos; read every changed file in full; judge each seam genuine (a content fix that cheats a stage instead of making the real branch reachable, or a content edit whose report cites no source for what the real game does, is reverted by rewriting the file from git show HEAD:<path>). (2) You own test/quests/_conformance.lua and tools/quest_gate/verb_list.py: a row for every new/changed verb; count bumped. (3) make -C src torirsserver-scripts compiles clean. If C changed, python3 tools/quest_gate/run.py --all --jobs 3 --no-publish rebuilds the shared binary; run it either way, then python3 tools/quest_gate/gate.py --allow-blocked -> every committed quest keeps or improves its bucket; list the ones that moved and WHY (uncommitted author attempts run their dirty copies -- not this pass's concern). make -C src test-quest-conformance test-quest-cheats check-quest-verbs check-drive-abi check-pt-switch test-plugin-lua -- conformance must be fully green before you commit; the C selftest binary if the server changed; lint on git ls-files test/quests/*.lua. (4) DOCS: fold the reports' doc_notes into the matching topic file under docs/quest_authoring/ (seam-facts.md for a fact this pass established, the verbs-*.md file for a verb's behaviour, traps-*.md for a trap's status; normal line lengths, a heading that names the symptom) and add ONE line per fact to docs/quest_authoring/INDEX.md keyed by what an author sees; docs/QUEST_AUTHORING.md is the core and stays under 25 KB -- change it only when a verb's one-line table entry or a rule itself changes; where a new fact supersedes an old one, mark the old one FIXED with the commit instead of leaving both. (5) ${SYNC} Commit the SUBMODULE first when content changed (git -C ${CONTENT}/.. add the changed content files by explicit path; message "quests: content seams the tier 1 rows named [seam:${pass}] -- <one clause per seam, each naming its source>", trailer "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"; ${PUSH_SUB}), then the parent with explicit paths (driver Lua, C, tools, _conformance.lua, verb_list.py, docs, OSRS-Content gitlink), message "quest-driver: seams the tier 1 rows named [seam:${pass}] -- <one clause per seam>", same trailer; ${PUSH_PARENT}. Never commit test/quests/<quest>.lua. Never git commit --amend, reset, or add -A. (6) Reopen every quest the reports list under unblocks: python3 tools/quest_gate/queue.py set <id> --status todo --failure "RETRY after <sha>: <the fix, one line; which rows must be rewritten>"; ALSO reopen these rows exactly as given: ${extraReopen.map(r => r.id + ' -> --status todo --failure "' + r.note + '"').join(' ; ') || 'none'}. Commit QUEUE.tsv ("quests: <n> tier 1 rows reopened after the seam pass [seam:${pass}]") and ${PUSH_AGAIN}. ${UNLOCK_STEP} (8) FINISH: write {"landed": true, "commit": "<parent sha>", "reopened": [...]} to ${STATE}/close.json, then return the schema.`, { label: 'close+land', model: 'opus', schema: LAND }))
}
if (!land) log('CLOSE DID NOT LAND: relaunch this pass with the same args; the closer resumes from close.progress.md')
return { pass, triage, fixed, failed, land }
