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
// file's content inline to the Workflow tool with no args: Triage groups the
// blocked/content_bug queue rows by seam, one Opus agent fixes each DRIVER
// seam with a live proof, and the closer adds conformance rows, runs the
// gates, commits, pushes, and reopens the rows the fixes free
// (queue.py set <id> --status todo --failure "RETRY after <sha>: ..."). Content,
// engine and design seams are reported, not fixed here.
//
// Never run it concurrently with an author batch's REVIEW phase if the fix is
// in C: the closer rebuilds the shared binary and polls for a quiet tree
// first, but a long batch can outlast its patience.

const WT = '/Users/matthewevers/Documents/git_repos/3draster-quest-driver'
const COMMON = `You are one worker in a multi-agent build. Work ONLY inside ${WT} (branch lane-quest-driver). No author batch is running; the tree is quiet. NEVER cd into, build in, or touch /Users/matthewevers/Documents/git_repos/3draster. Absolute paths under ${WT}. FIRST read ${WT}/docs/QUEST_SUITE_KIT.md (working rules: a C change is built into a PRIVATE objdir and driven with QUEST_BINARY=...; the closer alone rebuilds src/torirs_questtest) and ${WT}/docs/QUEST_AUTHORING.md. Read the previous seam pass's commit for context: git show 62c051fb8 --stat and its message. Edit ONLY the files assigned to you. Do NOT commit or push. Never git stash/checkout/reset/clean. Never touch test/quests/*.lua or QUEUE.tsv (the closer reopens rows). CLAUDE.md rules for C. The gate is behaviour: quote proving ledger rows and Read the PNGs. Report honestly.`
const TRIAGE_SCHEMA = { type: 'object', properties: {
  seams: { type: 'array', items: { type: 'object', properties: {
    key: { type: 'string' }, kind: { type: 'string', enum: ['driver', 'content', 'engine', 'design'] },
    quests: { type: 'array', items: { type: 'string' } }, summary: { type: 'string' }, evidence: { type: 'string' },
  }, required: ['key', 'kind', 'quests', 'summary', 'evidence'] } },
}, required: ['seams'] }
const REPORT = { type: 'object', properties: { summary: { type: 'string' }, files_changed: { type: 'array', items: { type: 'string' } }, verified_by: { type: 'string' }, open_issues: { type: 'array', items: { type: 'string' } }, unblocks: { type: 'array', items: { type: 'string' } }, doc_notes: { type: 'array', items: { type: 'string' } } }, required: ['summary', 'files_changed', 'verified_by', 'open_issues', 'unblocks', 'doc_notes'] }
const LAND = { type: 'object', properties: { commit: { type: 'string' }, pushed: { type: 'boolean' }, gates: { type: 'string' }, reverted: { type: 'array', items: { type: 'string' } }, reopened: { type: 'array', items: { type: 'string' } }, open_issues: { type: 'array', items: { type: 'string' } }, notes: { type: 'string' } }, required: ['commit', 'pushed', 'gates', 'reverted', 'reopened', 'open_issues', 'notes'] }
const fmt = (r) => r ? JSON.stringify(r, null, 1) : 'null'

phase('Triage')
const triage = await agent(`${COMMON}

YOUR JOB: triage, no edits. Read every row of test/quests/QUEUE.tsv whose status is blocked or content_bug (python3 tools/quest_gate/queue.py summary; awk the TSV), the t.blocked line of each committed test/quests/<id>.lua, and the row's last_failure. Group by SEAM (the same underlying defect), classify each: driver (script/plugins/quest_driver, src/plugin, the cheat ladder), content (OSRS-Content scripts/configs), engine (client/server outside the driver), design. Two ENGINE seams were named last pass and are still open -- say whether they are now the largest remaining lever and, if so, treat them as fixable this pass with an engine owner: (a) handle_oplocu (src/torirsserver/torirs_server_world.c ~7167) skips the level/transform/approach resolution handle_oploc (~6578) performs, so item-on-loc answers "I can't reach that!" from a tile plain click_loc succeeds on (biohazard, and it shapes fishingcompo/cog); (b) db_getfield pushes 0 for an unset column where scripts compare against null = -1 (mortton). Also re-examine the three new content_bug rows (betweenarock, eadgar, mourningsendparti) and makinghistory's 'varp transmit' block: are any of them actually an engine or driver seam in disguise? For each seam say exactly which quests it frees and quote the evidence.`, { label: 'triage', model: 'opus', schema: TRIAGE_SCHEMA })
const fixable = (triage?.seams || []).filter(s => s.kind === 'driver' || s.kind === 'engine')
log(`Triage: ${triage?.seams?.length ?? 0} seams; fixable here: ${fixable.map(s => s.kind + ':' + s.key + ' (' + s.quests.join(',') + ')').join('; ')}`)

phase('Fix')
const fixes = await parallel(fixable.map(s => () => agent(`${COMMON}

YOUR JOB: fix ONE ${s.kind} seam, prove it, do not commit. Seam "${s.key}", blocking ${s.quests.join(', ')}. Triage summary: ${s.summary}. Evidence: ${s.evidence}.
You own the files this seam lives in (name them in files_changed). Seams being fixed concurrently: ${fixable.map(x => x.key).join(' | ')} -- do not touch another seam's files; if two seams share a file, append a marked block or make the minimal edit and say so in open_issues. An ENGINE seam is fixed in the engine (server or client C), never papered over in the driver; CLAUDE.md rules apply (assert on contract violations, no early return on a bad parameter, no switch in a protothread; make -C src check-pt-switch must print total 0). A change to the SERVER's op handlers must keep the 21 C selftests green: make -C src test-torirsserver (or the focused target the Makefile offers) after your change.
Method: (1) reproduce the blocked row live with a scratch script (run.py --script <file> --name <label> --no-build; setup cheats as t.cheat inside run(); QUEST_BINARY=<private binary> when C changes: make -C src OPT=1 EMBED_SERVER=1 PLATFORM_OBJ_BASE=build_qd_seam_${s.key.replace(/[^a-z0-9]/gi, '_').slice(0, 24)} PLATFORM_TARGET=torirs_qd_seam_${s.key.replace(/[^a-z0-9]/gi, '_').slice(0, 24)} torirs_qd_seam_${s.key.replace(/[^a-z0-9]/gi, '_').slice(0, 24)}); (2) fix it at the right layer; (3) prove it on the SAME scratch script AND on a copy of one blocked committed quest file under build/ with its t.blocked removed (never edit test/quests/); (4) regressions: run.py cooks_assistant druid --no-build (+QUEST_BINARY) and gate.py; make -C src check-drive-abi check-pt-switch; do NOT run test-quest-conformance (the closer does). Report unblocks = the quests whose t.blocked reason this fix removes, verified by (3).`, { label: `fix:${s.kind}:${s.key.slice(0, 36)}`, model: 'opus', schema: REPORT })))

phase('Close')
const land = await agent(`${COMMON}

YOU ARE THE CLOSER (Opus). Triage: ${fmt(triage)}. Fix reports: ${fixes.map(fmt).join('\n---\n')}.
Do: (1) git diff; read every changed file in full; judge genuine; revert and name anything that weakens a check. (2) You own test/quests/_conformance.lua and tools/quest_gate/verb_list.py: a conformance row for every new verb or a re-graded row for a changed one; bump the count. (3) Rebuild the shared binary (python3 tools/quest_gate/run.py --all --no-publish builds build_questtest and runs every committed quest; --jobs 3 is safe now) then python3 tools/quest_gate/gate.py --allow-blocked -> every committed quest keeps or improves its bucket, list any that moved; make -C src test-quest-conformance test-quest-cheats check-quest-verbs check-drive-abi check-pt-switch test-plugin-lua; if the server changed, make -C src test-torirsserver too; lint on git ls-files test/quests/*.lua. (4) docs/QUEST_AUTHORING.md: apply the doc_notes (<= 300 lines; trim section 8's gaps list). (5) Commit with explicit paths (driver Lua, C, _conformance.lua, verb_list.py, docs), message "quest-driver: seams the blocked rows named -- <one clause per seam>", trailer "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"; never commit test/quests/*.lua; OSRS-Content only if run.py republished a committed quest's evidence (then the submodule first, same trailer, push it). git push origin lane-quest-driver. (6) Reopen every quest each fix report lists under unblocks: python3 tools/quest_gate/queue.py set <id> --status todo --failure "RETRY after <sha>: <the fix, one line>"; commit QUEUE.tsv ("quests: <n> blocked rows reopened after the seam pass") and push. Report sha, gates, the reopened list, and the seams left (content/design) with their quests.`, { label: 'close+land', model: 'opus', schema: LAND })
return { triage, fixes: fixes.filter(Boolean), land }