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
const COMMON = `You are one worker in a multi-agent build. Work ONLY inside ${WT} (branch lane-quest-driver). A Sonnet author batch may be running in this same tree: never touch test/quests/*.lua, QUEUE.tsv or OSRS-Content, and expect build/quest_gate/<id>/ to change under you. NEVER cd into, build in, or touch /Users/matthewevers/Documents/git_repos/3draster. Absolute paths under ${WT}. FIRST read ${WT}/docs/QUEST_SUITE_KIT.md (working rules: a C change is built into a PRIVATE objdir and driven with QUEST_BINARY=...; never rebuild src/torirs_questtest -- the running batch uses it) and ${WT}/docs/QUEST_AUTHORING.md. Edit ONLY the files assigned to you. Do NOT commit or push. Never git stash/checkout/reset/clean. CLAUDE.md rules for C. The gate is behaviour: quote proving ledger rows and Read the PNGs. Report honestly.`
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

YOUR JOB: triage, no edits. Read every row of test/quests/QUEUE.tsv whose status is blocked or content_bug (python3 tools/quest_gate/queue.py summary; awk the TSV), the t.blocked line of each committed test/quests/<id>.lua, and the row's last_failure. Group them by SEAM -- the same underlying defect, not the same quest -- and classify each seam: driver (script/plugins/quest_driver, src/plugin, the cheat ladder), content (OSRS-Content scripts/configs), engine (client/server outside the driver), design (a puzzle or mechanic no verb models yet). For each driver seam say exactly which quests it frees and quote the evidence (the ledger detail, file:line). Known from the last close: four quests (biohazard, cog, fishingcompo, golem) stop on use_on's far-side retry losing the armed item -- the closer of b5b720b49 wrote that fixing it needs a C-side arm that refuses instead of encoding an item-on-itself when objsel is already active (DrivePointer_InvArm); makinghistory needs inv_op('spade',1) at a dig site to reach [opheld1,spade]'s dig chain; entertheabyss needs three npcs to each take the scrying orb. Confirm or correct each from the current rows.`, { label: 'triage', model: 'opus', schema: TRIAGE_SCHEMA })
const driverSeams = (triage?.seams || []).filter(s => s.kind === 'driver')
log(`Triage: ${triage?.seams?.length ?? 0} seams, ${driverSeams.length} driver seams: ${driverSeams.map(s => s.key + ' (' + s.quests.join(',') + ')').join('; ')}`)

phase('Fix')
const fixes = await parallel(driverSeams.map(s => () => agent(`${COMMON}

YOUR JOB: fix ONE driver seam, prove it, do not commit. Seam "${s.key}", kind driver, blocking ${s.quests.join(', ')}. Triage summary: ${s.summary}. Evidence: ${s.evidence}.
You own the files this seam lives in (name them in files_changed); coordinate by NOT touching another seam's files: the seams being fixed concurrently are ${driverSeams.map(x => x.key).join(' | ')}. If two seams share pointer.lua, append your block at the end with a marked banner and touch nothing above it; if you must change a shared function, say so in open_issues and make the minimal edit.
Method: (1) reproduce the blocked row live with a scratch script (run.py --script <file> --name <label> --no-build; setup cheats as t.cheat inside run(); QUEST_BINARY=<your private binary> if you change C -- build with make -C src OPT=1 EMBED_SERVER=1 PLATFORM_OBJ_BASE=build_qd_seam_${s.key.replace(/[^a-z0-9]/gi, '_').slice(0, 24)} PLATFORM_TARGET=torirs_qd_seam_${s.key.replace(/[^a-z0-9]/gi, '_').slice(0, 24)} torirs_qd_seam_${s.key.replace(/[^a-z0-9]/gi, '_').slice(0, 24)}); (2) fix it at the right layer (a C seam when the Lua cannot do it honestly -- the b5b720b49 closer's reasoning about re-arming is in that commit and in pointer.lua's banner; read it before designing); (3) prove it on the SAME scratch script (the blocked row now PASSes) AND on one of the blocked committed quest files copied to build/ and edited to remove its t.blocked (never edit test/quests/ -- a batch is running); (4) regressions: run.py cooks_assistant druid --no-build (+ QUEST_BINARY if C) and gate.py; make -C src check-drive-abi check-pt-switch; do NOT run test-quest-conformance (the closer does). Report unblocks = the quests whose t.blocked reason this fix removes, verified by (3).`, { label: `fix:${s.key.slice(0, 40)}`, model: 'opus', schema: REPORT })))

phase('Close')
const land = await agent(`${COMMON}

YOU ARE THE CLOSER (Opus). Triage: ${fmt(triage)}. Fix reports: ${fixes.map(fmt).join('\n---\n')}.
Do: (1) git diff; read every changed file in full; judge genuine; if C changed, note that the SHARED binary must not be rebuilt while a batch runs: check for live client processes (pgrep -fl torirs_questtest) and for a running workflow's fresh writes under build/quest_gate/ (any ledger.tsv modified in the last 10 minutes); if a batch is live, build the shared binary ONLY after it is quiet (poll every 2 minutes up to 40 minutes), otherwise proceed. (2) You own test/quests/_conformance.lua and tools/quest_gate/verb_list.py: a conformance row for every new verb or a re-graded row for a changed one (e.g. use_on's far-side retry must have a row that proves the arming survives -- the harness's own subjects); bump the count. (3) Gates on the rebuilt shared binary: python3 tools/quest_gate/run.py --all --no-publish then gate.py --allow-blocked (every committed quest keeps or improves its bucket; list any that moved); make -C src test-quest-conformance test-quest-cheats check-quest-verbs check-drive-abi check-pt-switch test-plugin-lua; lint on git ls-files test/quests/*.lua. (4) docs/QUEST_AUTHORING.md: apply the doc_notes (<= 300 lines; trim section 8's gaps list). (5) Commit with explicit paths (driver Lua, C, _conformance.lua, verb_list.py, docs), message "quest-driver: seams the blocked rows named -- <one clause per seam>", trailer "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"; never commit test/quests/*.lua or OSRS-Content unless run.py republished a committed quest's evidence (then the submodule first). git push origin lane-quest-driver. (6) Reopen every quest each fix report lists under unblocks: python3 tools/quest_gate/queue.py set <id> --status todo --failure "RETRY after <sha>: <the fix, one line>"; commit QUEUE.tsv ("quests: <n> blocked rows reopened after the seam pass") and push. Report sha, gates, the reopened list, and the seams left (content/engine/design) with their quests.`, { label: 'close+land', model: 'opus', schema: LAND })
return { triage, fixes: fixes.filter(Boolean), land }