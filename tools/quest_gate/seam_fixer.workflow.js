export const meta = {
  name: 'quest-seam-fixer',
  description: 'Read the blocked queue rows, group them by driver seam, fix each seam that blocks a quest (C-side item arming for use_on retries, item-on-loc dig, a multi-npc orb sequence), prove it, reopen the rows, commit, push',
  phases: [
    { title: 'Triage', detail: 'Opus: group blocked rows by seam; separate driver seams from content/engine' },
    { title: 'Fix', detail: 'one Opus agent per driver seam' },
    { title: 'Close', detail: 'Opus: audit, conformance rows, gates, reopen rows, commit, push' },
  ],
}
// The seam fixer -- docs/QUEST_SUITE_KIT.md phase 5, the step the plan called
// "the reviewer turns repeated seams into section 4 work for an Opus agent".
// Run it after EVERY author batch (after the sampler has pushed), passing this
// file's content inline to the Workflow tool (args {reuse_triage, partial} optional): Triage groups the
// blocked/content_bug queue rows by seam, one Opus agent fixes each DRIVER
// seam with a live proof, and the closer adds conformance rows, runs the
// gates, commits, pushes, and reopens the rows the fixes free
// (queue.py set <id> --status todo --failure "RETRY after <sha>: ..."). Content,
// engine seams are fixed here too; from 2026-09-20 so are CONTENT seams (one
// Opus agent editing the quest's own scripts/configs, gated by the script
// pack compiling and the quest's own C selftest), because the owner's goal is
// every tier 1 quest green. Design gaps are reported, not fixed.
//
// Never run it concurrently with an author batch's REVIEW phase if the fix is
// in C: the closer rebuilds the shared binary and polls for a quiet tree
// first, but a long batch can outlast its patience.

const WT = '/Users/matthewevers/Documents/git_repos/3draster-quest-driver'
const CONTENT = `${WT}/OSRS-Content/osrs239-content`
const COMMON = `You are one worker in a multi-agent build. Work ONLY inside ${WT} (branch lane-quest-driver; the content is the OSRS-Content submodule at ${CONTENT}). No author batch is running. NEVER cd into, build in, or touch /Users/matthewevers/Documents/git_repos/3draster. Absolute paths. FIRST read ${WT}/docs/QUEST_SUITE_KIT.md (working rules: a C change is built into a PRIVATE objdir and driven with QUEST_BINARY=...; only the closer rebuilds src/torirs_questtest) and ${WT}/docs/QUEST_AUTHORING.md; then the last seam pass's commit (git log --oneline -30 | grep 'quest-driver: seams') --stat and message. Edit ONLY the files assigned to you. Do NOT commit or push. Never git stash/checkout/reset/clean/amend. Never touch test/quests/*.lua or QUEUE.tsv (the closer reopens rows). After ANY content edit: make -C ${WT}/src torirsserver-scripts (the embedded server refuses a stale pack). CLAUDE.md rules for C. The gate is behaviour: quote proving ledger rows and Read the PNGs. Report honestly.

THE GOAL, from the owner: every tier 1 quest green. Never git commit --amend, reset, or add -A.`
// args.context: a paragraph appended to every worker's brief (e.g. "the fixes
// are committed as WIP at <sha>, the tree is clean"). args.reopen: extra rows
// the closer reopens, [{id, note}], beyond the ones the fix reports free.
const extraContext = (args && args.context) ? `\n\n${args.context}` : ''
const extraReopen = (args && Array.isArray(args.reopen)) ? args.reopen : []
const COMMON_ALL = COMMON + extraContext
const TRIAGE_SCHEMA = { type: 'object', properties: {
  seams: { type: 'array', items: { type: 'object', properties: {
    key: { type: 'string' }, kind: { type: 'string', enum: ['driver', 'engine', 'content', 'design'] },
    quests: { type: 'array', items: { type: 'string' } }, summary: { type: 'string' }, evidence: { type: 'string' },
    files: { type: 'array', items: { type: 'string' }, description: 'the files the fix will touch' },
  }, required: ['key', 'kind', 'quests', 'summary', 'evidence', 'files'] } },
}, required: ['seams'] }
const REPORT = { type: 'object', properties: { summary: { type: 'string' }, files_changed: { type: 'array', items: { type: 'string' } }, verified_by: { type: 'string' }, open_issues: { type: 'array', items: { type: 'string' } }, unblocks: { type: 'array', items: { type: 'string' } }, doc_notes: { type: 'array', items: { type: 'string' } } }, required: ['summary', 'files_changed', 'verified_by', 'open_issues', 'unblocks', 'doc_notes'] }
const LAND = { type: 'object', properties: { commit: { type: 'string' }, pushed: { type: 'boolean' }, gates: { type: 'string' }, reverted: { type: 'array', items: { type: 'string' } }, reopened: { type: 'array', items: { type: 'string' } }, open_issues: { type: 'array', items: { type: 'string' } }, notes: { type: 'string' } }, required: ['commit', 'pushed', 'gates', 'reverted', 'reopened', 'open_issues', 'notes'] }
const fmt = (r) => r ? JSON.stringify(r, null, 1) : 'null'

phase('Triage')
// args.reuse_triage = a doc under docs/ holding a previous pass's triage (the
// 2026-09-20 pass died after triage on a usage limit): the triage agent then
// transcribes it instead of re-deriving it. args.partial = a dir holding that
// pass's unproven, unaudited partial diffs, offered to the fix agents to read.
const reuse = args && args.reuse_triage
const partial = (args && args.partial) ? `A previous attempt at this pass died mid-edit; its UNPROVEN, UNAUDITED partial diffs are under ${args.partial} (driver.patch, content.patch, untracked/). Read the hunks for your seam for ideas; never apply them blindly, and never apply another seam's hunks.` : ''
const triage = reuse ? await agent(`${COMMON_ALL}

YOUR JOB: transcribe, no edits, no verification. Read ${WT}/${reuse} in full: it holds a completed triage (one "## <kind>: <key>" section per seam with its Quests, Files, summary paragraph and Evidence line). Return it EXACTLY as the schema: one seam per section, key and kind from the heading, quests and files from their lines, summary = the paragraph, evidence = the Evidence line. Nothing else.`, { label: 'triage (transcribe)', model: 'sonnet', effort: 'low', schema: TRIAGE_SCHEMA })
: await agent(`${COMMON_ALL}

YOUR JOB: triage, no edits. For each non-green tier 1 row (python3 tools/quest_gate/queue.py summary; the rows in test/quests/QUEUE.tsv with status blocked/content_bug/todo and tier 1), read the row's last_failure, the committed test/quests/<id>.lua's t.blocked line and the evidence it cites (ledgers under build/quest_gate/<id>/, the .rs2 lines named). Group by SEAM and classify: driver, engine, content, design. CONTENT seams are fixable: name the exact OSRS-Content files. A row whose only remaining work is the AUTHOR's (a rewrite of the test file: a missing reward row, rows asserting an old bug, a wrong varp bound) is not a seam: list it under kind design with summary starting AUTHOR: so the closer leaves it todo for the next batch. Return the schema; every seam names its files.`, { label: 'triage', model: 'opus', schema: TRIAGE_SCHEMA })
const fixable = (triage?.seams || []).filter(s => s.kind !== 'design')
log(`Triage: ${triage?.seams?.length ?? 0} seams; fixing ${fixable.length}: ${fixable.map(s => s.kind + ':' + s.key + ' (' + s.quests.join(',') + ')').join('; ')}`)

phase('Fix')
const fixes = await parallel(fixable.map(s => () => agent(`${COMMON_ALL}

YOUR JOB: fix ONE ${s.kind} seam, prove it, do not commit. Seam "${s.key}", blocking ${s.quests.join(', ')}. Triage summary: ${s.summary}. Evidence: ${s.evidence}. Files: ${s.files.join(', ')}.
${partial}
Seams being fixed concurrently, do not touch their files: ${fixable.filter(x => x !== s).map(x => x.key + ' -> ' + x.files.join(',')).join(' | ')}. If a shared file is unavoidable, make the minimal edit and say so in open_issues.
Rules by kind. DRIVER: Lua in script/plugins/quest_driver (C in src/plugin only if Lua cannot do it honestly); prove with a scratch script and both existing green quests. ENGINE: server/client C; keep the C selftests at their count (the make target test-torirsserver is RED at HEAD on a pre-existing servpack error -- run the selftest binary the way 73a4251d0's closer did and compare failure counts before/after). CONTENT: edit the quest's own scripts/configs under ${CONTENT} (or the shared file the seam names) the way the original author would have -- read the wiki-pinned docs/quests/<quest>.md if one exists and the quest's own selftest under src/torirsserver/test/ or its <abbr>run debugproc; a spawn goes in the area's .spawn file with the constant the script already uses; a guard that can never pass is corrected to the condition the transcript implies; the fix must compile (make -C ${WT}/src torirsserver-scripts) and keep that quest's C selftest / <abbr>run green if one exists. For every kind, the PROOF is through the driver: a scratch script (run.py --script <file> --name <label> --no-build; cheats as t.cheat inside run()) that reproduces the blocked row first, then passes after the fix, AND a copy of the committed test/quests/<id>.lua under build/ with its t.blocked removed driven as far as it now goes (report where it stops next, if anywhere). Regressions: run.py cooks_assistant druid --no-build (+QUEST_BINARY if C) and gate.py; make -C src check-drive-abi check-pt-switch. Do NOT run test-quest-conformance (the closer does). Report unblocks = the quests whose t.blocked reason is gone, each verified by the copied file.`, { label: `fix:${s.kind}:${s.key.slice(0, 34)}`, model: 'opus', schema: REPORT }).catch(() => null)))

phase('Close')
const land = await agent(`${COMMON_ALL}

YOU ARE THE CLOSER (Opus). Triage: ${fmt(triage)}. Fix reports: ${fixes.map(fmt).join('\n---\n')}.
Do: (1) git status/diff in BOTH repos; read every changed file in full; judge genuine (a content fix that cheats a stage instead of making the real branch reachable is reverted). (2) You own test/quests/_conformance.lua and tools/quest_gate/verb_list.py: a row for every new/changed verb; count bumped. (3) make -C src torirsserver-scripts compiles clean. Rebuild the shared binary if C changed (python3 tools/quest_gate/run.py --all --jobs 3 --no-publish builds build_questtest and runs every committed quest) then python3 tools/quest_gate/gate.py --allow-blocked -> every committed quest keeps or improves its bucket; list the ones that moved and WHY (a row that asserted an old bug now failing is expected -- name it for the author). make -C src test-quest-conformance test-quest-cheats check-quest-verbs check-drive-abi check-pt-switch test-plugin-lua; the C selftest binary if the server changed; lint on git ls-files test/quests/*.lua. (4) docs/QUEST_AUTHORING.md: doc_notes (<= 300 lines). (5) Commit the SUBMODULE first when content changed (git -C ${CONTENT}/.. add the changed .rs2/.spawn/.varp/.constant files; message "quests: content seams the tier 1 rows named -- <one clause per seam>", trailer "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"; push origin HEAD:lane-quest-driver), then the parent with explicit paths (driver Lua, C, _conformance.lua, verb_list.py, docs, OSRS-Content gitlink), message "quest-driver: seams the tier 1 rows named -- <one clause per seam>", same trailer; git push origin lane-quest-driver. Never commit test/quests/*.lua. (6) Reopen every quest each fix report lists under unblocks: python3 tools/quest_gate/queue.py set <id> --status todo --failure "RETRY after <sha>: <the fix, one line; if the committed file asserts the old bug, say which rows must be rewritten>"; ALSO reopen these rows exactly as given, whatever the fix reports say: ${extraReopen.map(r => r.id + ' -> --status todo --failure "' + r.note + '"').join(' ; ') || 'none'}. Then commit QUEUE.tsv ("quests: <n> tier 1 rows reopened after the seam pass") and push. Report sha, gates, reopened, and the seams left with their quests.`, { label: 'close+land', model: 'opus', schema: LAND })
return { triage, fixes: fixes.filter(Boolean), land }