export const meta = {
  name: 'quest-seam-pass',
  description: 'Resumable seam pass: triage, one Opus fixer per seam, an Opus closer -- every step persists under build/seam_state/<pass>/ and relaunching with the same args continues from disk',
  phases: [
    { title: 'State', detail: 'Sonnet: read what this pass has already persisted' },
    { title: 'Triage', detail: 'Opus: group the non-green tier 1 rows by seam (skipped when triage.json exists)' },
    { title: 'Fix', detail: 'one Opus agent per seam not yet done; each writes fix.<key>.json' },
    { title: 'Close', detail: 'Opus: audit, gates, commit, push, reopen rows; idempotent; writes close.json' },
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
// QUEUE.tsv; only the closer commits; no author batch runs alongside.

const WT = '/Users/matthewevers/Documents/git_repos/3draster-quest-driver'
const CONTENT = `${WT}/OSRS-Content/osrs239-content`
const pass = args && args.pass
if (!pass) throw new Error('args.pass is required (e.g. "seam6"): it names build/seam_state/<pass>/')
const STATE = `${WT}/build/seam_state/${pass}`
const extraContext = (args && args.context) ? `\n\nCURRENT PICTURE: ${args.context}` : ''
const extraReopen = (args && Array.isArray(args.reopen)) ? args.reopen : []
const reuse = args && args.reuse_triage

const COMMON = `You are one worker in a multi-agent build. Work ONLY inside ${WT} (branch lane-quest-driver; the content is the OSRS-Content submodule at ${CONTENT}). No author batch is running. NEVER cd into, build in, or touch /Users/matthewevers/Documents/git_repos/3draster. Absolute paths. FIRST read ${WT}/docs/QUEST_SUITE_KIT.md and ${WT}/docs/QUEST_AUTHORING.md; then the last seam pass's commit (git log --oneline -40 | grep 'quest-driver: seams') --stat and message. Edit ONLY the files assigned to you. Never git stash/checkout/reset/clean/amend, never git add -A. Never touch test/quests/<quest>.lua or QUEUE.tsv unless your brief says so. After ANY content edit: make -C ${WT}/src torirsserver-scripts (the embedded server refuses a stale pack). run.py refuses a second concurrent run of one quest id: give every scratch run its own --name, run it in the FOREGROUND and wait for it (never background a run or wait on a monitor). CLAUDE.md rules for C. The gate is behaviour: quote proving ledger rows and Read the PNGs. Report honestly.

PASS STATE DIR: ${STATE} (mkdir -p it). Every worker persists its result there so a paused or killed pass resumes from disk with nothing redone. Uncommitted test/quests/<quest>.lua files in the tree are author attempts: read-only for everyone in this pass.

THE GOAL, from the owner: every tier 1 quest green.${extraContext}`

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
  closed: { type: 'boolean' }, close_commit: { type: 'string' },
}, required: ['has_triage', 'triage', 'done', 'closed', 'close_commit'] }

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

YOUR JOB: read this pass's persisted state, no edits, no verification. mkdir -p ${STATE}. Then: has_triage = whether ${STATE}/triage.json exists and parses; triage = its content (or {seams: []}); done = one entry per ${STATE}/fix.*.json that parses ({key, report}); closed = whether ${STATE}/close.json exists and says landed; close_commit = the sha it names, else "". Return exactly the schema.`, { label: 'state', model: 'sonnet', effort: 'low', schema: STATE_SCHEMA }))) || { has_triage: false, triage: { seams: [] }, done: [], closed: false, close_commit: '' }
log(`state: triage ${state.has_triage ? 'present' : 'absent'}, ${state.done.length} seam(s) done, closed=${state.closed}`)

phase('Triage')
let triage = state.has_triage ? state.triage : null
if (!triage) {
  triage = await attempt('triage', 2, () => agent(`${COMMON}

YOUR JOB: triage, no edits. ${reuse ? `Read ${WT}/${reuse} in full: it holds a completed triage (one "## <kind>: <key>" section per seam with Quests, Files, a summary paragraph and an Evidence line) -- transcribe it exactly into the schema.` : `For each non-green tier 1 row (python3 tools/quest_gate/queue.py summary; the rows in test/quests/QUEUE.tsv with status blocked/content_bug/todo and tier 1), read its last_failure, the committed test/quests/<id>.lua's t.blocked line and the evidence it cites (ledgers under build/quest_gate/<id>/, the .rs2 lines named). Group by SEAM and classify: driver, engine, content, design. CONTENT seams are fixable: name the exact OSRS-Content files. A row whose only remaining work is the AUTHOR's (a rewrite of the test file) is not a seam: list it under kind design with summary starting AUTHOR:. Every seam names its files; two seams never share a file (merge them if they must).`} BEFORE returning, write the schema JSON to ${STATE}/triage.json (python3 -c with json.dump). Return the schema.`, { label: reuse ? 'triage (transcribe)' : 'triage', model: reuse ? 'sonnet' : 'opus', effort: reuse ? 'low' : undefined, schema: TRIAGE_SCHEMA }))
  if (!triage) throw new Error('triage produced no result after two tries; relaunch with the same args')
}
const doneKeys = new Set(state.done.map(d => d.key))
const fixable = triage.seams.filter(s => s.kind !== 'design')
const todo = fixable.filter(s => !doneKeys.has(s.key))
log(`triage: ${triage.seams.length} seams, ${fixable.length} fixable, ${todo.length} still to fix: ${todo.map(s => s.kind + ':' + s.key).join('; ') || 'none'}`)

phase('Fix')
const fresh = await parallel(todo.map(s => () => attempt(`fix:${s.key}`, 2, () => agent(`${COMMON}

YOUR JOB: fix ONE ${s.kind} seam, prove it, persist your report, do not commit. Seam "${s.key}", blocking ${s.quests.join(', ')}. Triage summary: ${s.summary}. Evidence: ${s.evidence}. Files (yours alone): ${s.files.join(', ')}.
Seams being fixed concurrently, never touch their files: ${fixable.filter(x => x !== s).map(x => x.key + ' -> ' + x.files.join(',')).join(' | ') || 'none'}. Seams already fixed on disk in this pass (their edits are uncommitted in the tree; leave them alone): ${state.done.map(d => d.key).join(', ') || 'none'}.
RESUME DISCIPLINE: ${STATE}/fix.${s.key}.progress.md is your notebook. If it exists, a previous attempt at this seam was killed: read it first and continue from its last proved step -- never redo what it proved, and audit any edit it left in your files against its notes. Append to it after every meaningful step (what you changed, what you measured, the ledger row that proves it) so the next attempt can continue if you die. Your files may already carry that attempt's edits.
Rules by kind. DRIVER: Lua in script/plugins/quest_driver (C in src/plugin only if Lua cannot do it honestly); prove with a scratch script and two existing green quests. ENGINE: server/client C; keep the C selftests at their count (the make target test-torirsserver is RED at HEAD on a pre-existing servpack error -- run the selftest binary directly, HEAD baseline from a throwaway git worktree with its own PLATFORM_OBJ_BASE, compare failure counts before/after). CONTENT: edit the quest's own scripts/configs under ${CONTENT} (or the shared file the seam names) the way the original author would have -- read the wiki-pinned docs/quests/<quest>.md if one exists and the quest's own selftest or <abbr>run debugproc; the fix must compile (make -C ${WT}/src torirsserver-scripts) and keep that quest's C selftest green if one exists. For every kind the PROOF is through the driver: a scratch script (run.py --script <file> --name <unique label> --no-build; cheats as t.cheat inside run()) that reproduces the blocked row first, then passes after the fix, AND a copy of the committed test/quests/<id>.lua under build/ with its t.blocked removed driven as far as it now goes (say where it stops next). Regressions: run.py cooks_assistant druid --no-build (+QUEST_BINARY if C) and gate.py; make -C src check-drive-abi check-pt-switch. Do NOT run test-quest-conformance unless your seam names a conformance row.
FINISH: write your report as JSON {"key": "${s.key}", "report": <the schema>} to ${STATE}/fix.${s.key}.json (python3 json.dump), THEN return the schema with key = "${s.key}". unblocks = the quests whose t.blocked reason is gone, each verified by the copied file.`, { label: `fix:${s.kind}:${s.key.slice(0, 34)}`, model: 'opus', schema: REPORT }))))
const fixed = fresh.filter(Boolean)
const failed = todo.filter((s, i) => !fresh[i]).map(s => s.key)
log(`fix: ${fixed.length} of ${todo.length} reported this launch; ${failed.length ? 'NO REPORT from ' + failed.join(', ') + ' (relaunch with the same args to retry them)' : 'none missing'}`)

phase('Close')
let land = state.closed ? { commit: state.close_commit, pushed: true, gates: 'landed by a previous launch of this pass', reverted: [], reopened: [], open_issues: [], notes: 'close.json already present' } : null
if (!land) {
  land = await attempt('close', 2, () => agent(`${COMMON}

YOU ARE THE CLOSER (Opus). The pass's triage is ${STATE}/triage.json; every fix report is a ${STATE}/fix.*.json ({key, report}) -- read them ALL from disk (this launch produced ${fixed.length}; earlier launches left ${state.done.length}). Seams with no report: ${failed.join(', ') || 'none'} -- their files may carry an unfinished edit: judge it by the seam's progress notes ${STATE}/fix.<key>.progress.md and by a live run; keep it only if proved, else restore the file from git show HEAD:<path> (never checkout/reset) and say so.
RESUME DISCIPLINE: ${STATE}/close.progress.md is your notebook: if it exists, a previous closer was killed -- read it and continue from its last completed step (check git log for the tag [seam:${pass}] before committing again; check QUEUE.tsv before reopening again). Append to it after every step.
Do: (1) git status/diff in BOTH repos; read every changed file in full; judge each seam genuine (a content fix that cheats a stage instead of making the real branch reachable is reverted by rewriting the file from git show HEAD:<path>). (2) You own test/quests/_conformance.lua and tools/quest_gate/verb_list.py: a row for every new/changed verb; count bumped. (3) make -C src torirsserver-scripts compiles clean. If C changed, python3 tools/quest_gate/run.py --all --jobs 3 --no-publish rebuilds the shared binary; run it either way, then python3 tools/quest_gate/gate.py --allow-blocked -> every committed quest keeps or improves its bucket; list the ones that moved and WHY (uncommitted author attempts run their dirty copies -- not this pass's concern). make -C src test-quest-conformance test-quest-cheats check-quest-verbs check-drive-abi check-pt-switch test-plugin-lua -- conformance must be fully green before you commit; the C selftest binary if the server changed; lint on git ls-files test/quests/*.lua. (4) docs/QUEST_AUTHORING.md: fold the reports' doc_notes (<= 300 lines). (5) Commit the SUBMODULE first when content changed (git -C ${CONTENT}/.. add the changed content files by explicit path; message "quests: content seams the tier 1 rows named [seam:${pass}] -- <one clause per seam>", trailer "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"; push origin HEAD:lane-quest-driver), then the parent with explicit paths (driver Lua, C, tools, _conformance.lua, verb_list.py, docs, OSRS-Content gitlink), message "quest-driver: seams the tier 1 rows named [seam:${pass}] -- <one clause per seam>", same trailer; git push origin lane-quest-driver. Never commit test/quests/<quest>.lua. Never git commit --amend, reset, or add -A. (6) Reopen every quest the reports list under unblocks: python3 tools/quest_gate/queue.py set <id> --status todo --failure "RETRY after <sha>: <the fix, one line; which rows must be rewritten>"; ALSO reopen these rows exactly as given: ${extraReopen.map(r => r.id + ' -> --status todo --failure "' + r.note + '"').join(' ; ') || 'none'}. Commit QUEUE.tsv ("quests: <n> tier 1 rows reopened after the seam pass [seam:${pass}]") and push. (7) FINISH: write {"landed": true, "commit": "<parent sha>", "reopened": [...]} to ${STATE}/close.json, then return the schema.`, { label: 'close+land', model: 'opus', schema: LAND }))
}
if (!land) log('CLOSE DID NOT LAND: relaunch this pass with the same args; the closer resumes from close.progress.md')
return { pass, triage, fixed, failed, land }
