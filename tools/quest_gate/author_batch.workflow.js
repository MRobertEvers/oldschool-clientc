export const meta = {
  name: 'quest-author-batch',
  description: 'Resumable author batch: one author per quest, a reviewer per quest, one queue write, an Opus sample, a contact sheet -- every step persists under build/author_state/<batch>/ and relaunching with the same args continues from disk',
  phases: [
    { title: 'State', detail: 'Sonnet: which quests of this batch are already reviewed' },
    { title: 'Author', detail: 'Sonnet authors; each writes <id>.author.json and a progress notebook' },
    { title: 'Review', detail: 'Sonnet reviewers; each commits and writes <id>.review.json; nobody touches QUEUE.tsv here' },
    { title: 'Queue', detail: 'Sonnet: ONE write of every row from the review files (no clobber race)' },
    { title: 'Sample', detail: 'Opus: adversarial sample, reverts, pushes; idempotent' },
    { title: 'Sheet', detail: 'Sonnet: builds the contact sheet under the artifact size limit; the orchestrator publishes' },
  ],
}
// The resumable author batch (2026-09-22). Paste this file's content inline
// with args:
//   { batch: "sonnet-b11", tests: ["misc", ...],
//     sheet_dir: "<scratchpad>/batch_sheet/sonnet-b11" }
// Every step writes under build/author_state/<batch>/ (<id>.author.json,
// <id>.author.progress.md, <id>.review.json, queue.json, sample.json,
// sheet.json) and the State phase reads them back, so a killed or paused batch
// relaunched with the SAME args continues: reviewed quests are not
// re-authored, a pushed sample is not re-pushed. Never resumeFromRunId.
//
// Change from the 2026-09-19 loop: reviewers no longer write QUEUE.tsv (twelve
// concurrent read-modify-writes clobbered rows); the Queue phase writes every
// row once from the review files. Every author is Sonnet (owner rule,
// 2026-09-22: Haiku is no longer used anywhere in the loop); an author that
// compacts or returns no report is re-run once, still Sonnet, at medium effort.

const WT = '/Users/matthewevers/Documents/git_repos/3draster'
const batch = args && args.batch
const tests = (args && args.tests) || []
const authorModel = 'sonnet'
const sheetDir = (args && args.sheet_dir) || `${WT}/build/author_state/${batch}/sheet`
if (!batch) throw new Error('args.batch is required (e.g. "sonnet-b11")')
if (!tests.length) throw new Error('args.tests is empty: pick test_ids with tools/quest_gate/queue.py first')
const STATE = `${WT}/build/author_state/${batch}`

const COMMON = `Work ONLY inside ${WT} (branch lane-quest-driver). (the owner's checkout; the 2026-09-25 disk cleanup deleted the old worktree, so this checkout IS the working tree now -- never delete build or cache directories, never run git clean/checkout/reset on paths you did not change). Absolute paths under ${WT} for every command. Never git stash/checkout/reset/clean/amend, never git add -A or -u. Never commit saves/, build*, cache*, manifests/.*.ini, preferences.ini, plugin_prefs.ini. Run every run.py in the FOREGROUND and wait for it; never background it or wait on a monitor/notification. run.py refuses a second concurrent run of one quest id. BATCH STATE DIR: ${STATE} (mkdir -p it); every worker persists its result there so a paused or killed batch resumes from disk.`

const AUTHOR_SCHEMA = { type: 'object', properties: {
  test_id: { type: 'string' }, outcome: { type: 'string', enum: ['green', 'blocked', 'content_bug', 'gave_up'] }, runs: { type: 'integer' },
  checks_resolved: { type: 'array', items: { type: 'string' } }, last_failure: { type: 'string' }, blocker: { type: 'string' },
  doc_gaps: { type: 'array', items: { type: 'string' } }, compacted: { type: 'boolean' },
}, required: ['test_id', 'outcome', 'runs', 'checks_resolved', 'last_failure', 'blocker', 'doc_gaps', 'compacted'] }
const REVIEW_SCHEMA = { type: 'object', properties: {
  test_id: { type: 'string' }, verdict: { type: 'string', enum: ['accepted', 'rejected', 'blocked', 'content_bug'] }, commit: { type: 'string' },
  queue_status: { type: 'string', enum: ['green', 'blocked', 'content_bug', 'todo'] }, queue_failure: { type: 'string' },
  findings: { type: 'array', items: { type: 'string' } }, shots_checked: { type: 'integer' }, doc_gaps: { type: 'array', items: { type: 'string' } },
}, required: ['test_id', 'verdict', 'commit', 'queue_status', 'queue_failure', 'findings', 'shots_checked', 'doc_gaps'] }
const STATE_SCHEMA = { type: 'object', properties: {
  reviewed: { type: 'array', items: REVIEW_SCHEMA },
  authored: { type: 'array', items: AUTHOR_SCHEMA },
  queue_written: { type: 'boolean' }, sampled: { type: 'boolean' }, sheet_built: { type: 'boolean' },
  queue_written_ids: { type: 'array', items: { type: 'string' } }, sample_considered: { type: 'array', items: { type: 'string' } }, sample_sent_back: { type: 'array', items: { type: 'string' } },
}, required: ['reviewed', 'authored', 'queue_written', 'sampled', 'sheet_built', 'queue_written_ids', 'sample_considered', 'sample_sent_back'] }
const SHEET_SCHEMA = { type: 'object', properties: { index_html: { type: 'string' }, files: { type: 'array', items: { type: 'string' } }, bytes: { type: 'integer' }, quality: { type: 'integer' }, quests: { type: 'array', items: { type: 'string' } } }, required: ['index_html', 'files', 'bytes', 'quality', 'quests'] }

const attempt = async (label, tries, make) => {
  for (let i = 1; i <= tries; i++) {
    const r = await make().catch(() => null)
    if (r) return r
    log(`${label}: no result on try ${i} of ${tries}`)
  }
  return null
}

const authorCard = (id) => `${COMMON}
${sentBack.has(id) ? `
THIS QUEST WAS SENT BACK by the batch's Opus sampler after a previous launch (its finding is the queue row's last_failure, prefixed REVERTED by sampler): first remove ${STATE}/${id}.author.json, ${STATE}/${id}.review.json and ${STATE}/${id}.review.progress.md (keep your notebook), then author it again from the committed file and the sampler's finding.\n` : ''}
You are writing ONE client-driven quest test: test_id "${id}". Read ${WT}/docs/QUEST_AUTHORING.md once, in full. It is the only page you need; it names every verb, the result words, the traps, the run command, and the definition of done.
RESUME DISCIPLINE: ${STATE}/${id}.author.progress.md is your notebook. If it exists, a previous attempt at this quest was killed: read it first, count its runs toward your eight, and continue from its last step -- test/quests/${id}.lua on disk is that attempt's file. Append to it after every run (run number, the failing row, what you changed).

Steps:
0. You start in Lumbridge, beside Hans -- the fixture never moves you closer. The scaffold's generated goto rows (t.player.goto_tile, x/z/level -- never t.player.goto, a Lua reserved word) put you there. If a talk_to answers "screen_position: ..." read the reason after the colon: not_visible means aim (goto closer or another side), not_found means the npc is not in the world at that tile -- check its spawn row, never the verb.
1. python3 ${WT}/tools/quest_gate/queue.py show ${id}   (the quest_dir and helper it maps to, and last_failure -- READ IT, it is the previous reviewer or the seam pass telling you exactly what to fix)
   A RE-OPENED ROW: when last_failure starts with "RETRY after <sha>" it names a fix that landed. The committed test/quests/${id}.lua is the previous author's file: resume it, delete the t.blocked whose reason is now fixed, and drive on. Rows that ASSERT THE OLD BUG are now FAIL and must be rewritten to assert the fixed behaviour. If test/quests/${id}.lua exists and the row is todo with a last_failure: continue from it -- do NOT run new_quest.py over it. If the last_failure points at a reverted commit (git show <sha>:test/quests/${id}.lua), that reviewed version is the one to resume. Otherwise: python3 ${WT}/tools/quest_gate/new_quest.py ${id}; python3 ${WT}/tools/quest_gate/lint_quest.py --allow-check test/quests/${id}.lua
   Rewards: skill.snapshot() before the hand-in and a t.check/t.expect row around every skill.expect_gain / coins delta / inv.expect_has after quest.expect_complete() for every reward Quest Helper lists -- a bare expect_gain writes no row and is rejected. Assert the LITERAL documented reward.
   Doors: "I can't reach that!" means a door, gate or wall is between you and the target -- click_loc the door (op 1) or goto_tile past it. Chat order: an [opnpc1,...] branch almost always opens with the PLAYER's line; verify against the .rs2. mes() is a chat-log line, not a page. Every row's detail must say something.
2. Resolve every "-- CHECK" marker by reading the quest's own scripts under ${WT}/OSRS-Content/osrs239-content/server/scripts/quests/<quest_dir>/. Never guess a symbol; lint checks it.
3. python3 ${WT}/tools/quest_gate/run.py ${id} --no-build ; python3 ${WT}/tools/quest_gate/gate.py ${id}. Read the failure block. Open the -FAIL.png with the Read tool. Fix ONE thing, run again. Count your runs (including a killed attempt's). STOP after eight runs.
4. You may edit ONLY test/quests/${id}.lua and your notebook. Never script/plugins/, src/, tools/, OSRS-Content/, or another quest's file. Never ::complete the quest under test in its own setup. No local helper functions in the quest file. Lua: nothing may follow a return in the same block.
5. Look at your own screenshots (Read on build/quest_gate/${id}/shots/*.png): each must show what its name says. If an npc or loc you need is visible in a shot, click it before declaring anything blocked.
6. Done when gate.py says green AND lint_quest.py (without --allow-check) is clean. If a verb misbehaves, a cheat does nothing, a symbol will not resolve, or a stage never changes: OUTCOME BLOCKED REQUIRES A t.blocked("<exact seam>") ROW AT THAT POINT, THEN return -- ANYTHING ELSE IS REJECTED, NOT BLOCKED. If the quest's own script misbehaves, report content_bug with the file:line.

THE GUIDE IS THE SPEC (owner rule, 2026-09-23). The Quest Helper guide's step ladder (steps.put stages and each step's text) is what the test must drive, end to end. Rules that earlier batches broke and are now rejections: (a) every guide step is driven by a real row, or the file stops at t.blocked with a content_bug naming the leg the port lacks -- a stage the content advances through a narrating mes() with no player action is a CONTENT GAP, never a PASS; (b) t.player.goto_tile is a ::goto teleport: it is for plain travel only -- teleporting past a locked or disguise-gated door, a secret wall, a fence, a stair, a puzzle or any loc the guide names as a step is a cheat; click the thing; (c) ::give is only for items the guide's getItemRequirements lists as brought along -- an item the guide has you gather, buy, loot, make or receive in dialogue must be obtained that way; (d) a debugproc (::<quest>run, ::*_give_*, a grind fast-forward) may not do quest work; only the fast-forwards docs/QUEST_SERVER_CHEATS.md names for a grind may be used, and their effect is read back; (e) never ::setvar a quest varp/varbit mid-run (a prerequisite quest's state comes only from ::complete <that quest>); (f) dialogue choices follow the guide's path where it gates anything; (g) an npc not where the goto sent you: grep its *.spawn row; (h) name every quest.expect_stage row quest.stage.<constant>; (i) never screenshot to pad rows; (j) resolve every -- CHECK before your first run. If tools/quest_gate/helper_coverage.py exists, run python3 tools/quest_gate/helper_coverage.py ${id} before you finish: every guide step must read driven or content_gap.

Do NOT commit, push, or edit QUEUE.tsv. FINISH: write the schema JSON to ${STATE}/${id}.author.json, then return it; put the final failure block verbatim in last_failure if you did not reach green. You MUST end by calling StructuredOutput even if you gave up.
COMPACTION: if your conversation is ever compacted or summarized while you work, STOP at once, write what you have to the notebook, and report outcome gave_up with compacted=true and blocker "context compacted"; a larger model resumes from your notebook.`

const reviewCard = (id, a) => `${COMMON}

You are the reviewer for quest test "${id}". The author reported: ${JSON.stringify(a, null, 1)}.
RESUME DISCIPLINE: if ${STATE}/${id}.review.json exists, a previous reviewer finished: return its content unchanged. If ${STATE}/${id}.review.progress.md exists, continue from its last step (check git log for "quests: ${id}" before committing again). Append to it after every step.

Do, in order:
1. If the author's outcome is blocked or content_bug: confirm the file is green up to its t.blocked row (python3 tools/quest_gate/run.py ${id} --no-build ; python3 tools/quest_gate/gate.py ${id} --allow-blocked ; python3 tools/quest_gate/lint_quest.py test/quests/${id}.lua). If it is, commit it (step 4) and report queue_status = that status with queue_failure = the blocker. If it is not: queue_status todo with the failure; if the file was never committed remove it, otherwise restore it with git show HEAD:test/quests/${id}.lua > the file. A blocker that names an npc or loc visible in the author's own shots, never clicked, is not a blocker: reject. A blocker that re-states a seam the queue row says is FIXED ("RETRY after <sha>") must be re-verified live before it is believed. gave_up: judge whatever file exists by step 2.
2. Otherwise re-run yourself: run.py ${id} --no-build ; gate.py ${id} ; lint_quest.py test/quests/${id}.lua. All three green or the verdict is rejected (queue_status todo, keep the file, queue_failure = why, prefixed "REJECTED (${batch}): ").
3. THE GUIDE FIRST: open the Quest Helper guide (queue.py show ${id} names helper_dir/helper_file under /Users/matthewevers/Documents/git_repos/quest-helper/src/main/java/com/questhelper/helpers/quests/) and walk its step ladder against the file and ledger: every step must be driven by a row, or the file must stop at t.blocked content_bug naming the leg the port lacks. A stage the content advances by a narrating mes() is a content gap (verdict content_bug, not accepted). A goto_tile past a gated door, wall, fence, stair or puzzle the guide names; a ::give of an item the guide has you obtain in game; a debugproc doing quest work; a ::setvar on a quest varp -- each is one finding and any one of them rejects. If tools/quest_gate/helper_coverage.py exists, run it and quote its verdict. Then read the quest file against the quest's own .rs2 scripts: does it drive the real accept and hand-in branches through clicks and chat, not through a cheat that does the quest's work? Any item, kill, craft, search or fetch the .rs2 makes the player do that the file hands over with ::give/::kill/::setvar is a cheated hand-in. Does it end in t.quest.expect_complete() (or expect_stage + one BLOCKED row)? Every documented reward has a row asserting the LITERAL amount inside a t.check/t.expect. Open EVERY PNG under build/quest_gate/${id}/shots/ with the Read tool and confirm each shows what its name says; count them in shots_checked. Two or more findings = rejected.
4. Accept: run.py just published evidence under osrs239-content/server/scripts/selftest/quests/<quest_dir>/play/ (<quest_dir> is QUEUE.tsv's quest_dir column for ${id}, or quest_${id} if ${id} has no row -- run.py's own "published ... -> ..." line names the exact path). Commit that directory in the submodule first (git -C OSRS-Content add osrs239-content/server/scripts/selftest/quests/<quest_dir>/play && git -C OSRS-Content commit -m "selftest/quests: ${id} play evidence" with the trailer "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"), then in the parent: git add test/quests/${id}.lua OSRS-Content ; git commit -m "quests: ${id} <green|blocked|content_bug> (<rows> rows, <shots> shots)" with the same trailer. Stage only those two paths; other reviewers commit in this worktree at the same time, so never amend, reset, or add -A. Do not push. If you reject, delete any published evidence run.py left under OSRS-Content/.../selftest/quests/<quest_dir>/play/ that is not committed.
Do NOT edit QUEUE.tsv (the Queue phase writes every row once from the review files). FINISH: write the schema JSON to ${STATE}/${id}.review.json, then return it. doc_gaps = the author's doc_gaps you judge real.`

phase('State')
const state = (await attempt('state', 3, () => agent(`${COMMON}

YOUR JOB: read this batch's persisted state, no edits, no summarising. mkdir -p ${STATE}. reviewed = the content of every ${STATE}/<id>.review.json that parses, copied verbatim; authored = every ${STATE}/<id>.author.json, verbatim; queue_written = ${STATE}/queue.json exists; queue_written_ids = its "written" list (or []); sampled = ${STATE}/sample.json exists and says pushed; sample_considered = its "considered" list (or its "checked" list, or []); sample_sent_back = its "sent_back" list (or []); sheet_built = ${STATE}/sheet.json exists. Never invent an entry. Return exactly the schema.`, { label: 'state', model: 'sonnet', effort: 'low', schema: STATE_SCHEMA }))) || { reviewed: [], authored: [], queue_written: false, sampled: false, sheet_built: false, queue_written_ids: [], sample_considered: [], sample_sent_back: [] }
// A quest the sampler sent back is authored again: its old author/review files are ignored (the author removes them).
const sentBack = new Set(state.sample_sent_back || [])
const keptReviews = state.reviewed.filter(r => !sentBack.has(r.test_id))
const reviewedIds = new Set(keptReviews.map(r => r.test_id))
const authoredById = Object.fromEntries(state.authored.filter(a => !sentBack.has(a.test_id)).map(a => [a.test_id, a]))
const pending = tests.filter(id => !reviewedIds.has(id))
log(`state: ${keptReviews.length} reviewed, ${Object.keys(authoredById).length} authored, ${sentBack.size} sent back, ${pending.length} pending: ${pending.join(', ') || 'none'}`)

const RETRY_EFFORT = 'medium'
const results = await pipeline(
  pending,
  async (id) => {
    if (authoredById[id]) return authoredById[id]
    const first = await attempt(`author:${id}`, 2, () => agent(authorCard(id), { label: `author:${id}`, phase: 'Author', model: authorModel, schema: AUTHOR_SCHEMA }))
    const compacted = !first || first.compacted === true
    if (!compacted) return first
    log(`${id}: the author compacted (or returned no report); re-running once at ${RETRY_EFFORT} effort from its notebook`)
    const second = await attempt(`author:${id} (retry)`, 2, () => agent(authorCard(id), { label: `author:${id} (retry)`, phase: 'Author', model: authorModel, effort: RETRY_EFFORT, schema: AUTHOR_SCHEMA }))
    return second ? { ...second, retried: true } : second
  },
  async (a, id) => {
    const report = a || { test_id: id, outcome: 'gave_up', runs: 0, checks_resolved: [], last_failure: '', blocker: 'the author returned no report; review whatever file it left', doc_gaps: [], compacted: true }
    return attempt(`review:${id}`, 2, () => agent(reviewCard(id, report), { label: `review:${id}`, phase: 'Review', model: 'sonnet', schema: REVIEW_SCHEMA }))
  },
)
const reviewed = [...keptReviews, ...results.filter(Boolean)]
const freshReviews = results.filter(Boolean)
const missing = tests.filter(id => !reviewed.some(r => r.test_id === id))
log(`${reviewed.filter(r => r.verdict === 'accepted').length} accepted, ${reviewed.filter(r => r.verdict === 'blocked').length} blocked, ${reviewed.filter(r => r.verdict === 'content_bug').length} content bugs, ${reviewed.filter(r => r.verdict === 'rejected').length} rejected; ${missing.length ? 'NO REVIEW for ' + missing.join(', ') + ' (relaunch with the same args)' : 'every quest reviewed'}`)

phase('Queue')
const writtenIds = new Set(state.queue_written_ids || [])
const toWrite = reviewed.filter(r => !writtenIds.has(r.test_id))
if (toWrite.length) {
  await attempt('queue', 3, () => agent(`${COMMON}

YOUR JOB: write this batch's queue rows ONCE, from the review files, commit, no push. Rows to write now: ${toWrite.map(r => r.test_id).join(', ')} (read each ${STATE}/<id>.review.json); rows already written by an earlier launch and left alone: ${[...writtenIds].join(', ') || 'none'}. For each: python3 tools/quest_gate/queue.py set <id> --status <queue_status> --owner ${batch} --failure "<queue_failure>" (green rows get --failure ""). If queue.py refuses green because the file carries a t.blocked( that a guard makes unreachable (the ledger has no BLOCKED row), write that row through queue.py's own loader/writer and say so. Quests with no review file (${missing.join(', ') || 'none'}) are left untouched. Then git add test/quests/QUEUE.tsv; git commit -m "quests: queue after batch ${batch}" with the trailer "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>". FINISH: write {"written": [every id written by any launch: ${[...writtenIds].map(x => '"' + x + '"').join(', ')}${writtenIds.size ? ', ' : ''}plus the ids you wrote now]} to ${STATE}/queue.json. Return a one-line summary per row.`, { label: 'queue', model: 'sonnet' }))
} else log('queue: every reviewed row already written by a previous launch')

phase('Sample')
const consideredIds = new Set((state.sample_considered || []).filter(x => !sentBack.has(x)))
const accepted = reviewed.filter(r => r.verdict === 'accepted' && !consideredIds.has(r.test_id))
let sample = null
if (accepted.length) {
  sample = await attempt('sample', 2, () => agent(`${COMMON}

You are the Opus sampler for batch ${batch}. Accepted: ${JSON.stringify(accepted.map(r => r.test_id))}. Pick these three (or all if fewer): ${JSON.stringify(accepted.filter((_, i) => i % Math.max(1, Math.ceil(accepted.length / 3)) === 0).slice(0, 3).map(r => r.test_id))}.
RESUME DISCIPLINE: ${STATE}/sample.progress.md is your notebook; if it exists continue from its last step (check git log for a revert before reverting again; check git status -sb for "ahead" before pushing again). Append after every step. Quests an earlier launch of this batch already sampled are not yours: ${[...consideredIds].join(', ') || 'none'}.
For each: open the Quest Helper guide and walk its step ladder against the ledger -- every step driven, none narrated by the content, none teleported past, none handed over by ::give/::setvar/a debugproc (any of these sends the quest back); then read the quest file and the quest's own .rs2 scripts; confirm the test drove the real accept and hand-in branches (no cheat did the quest's work), tick counts plausible, no ok row with an empty detail, reward rows assert literal documented amounts, and open every published PNG under OSRS-Content/osrs239-content/server/scripts/selftest/quests/<quest_dir>/play/ (<quest_dir> per queue.py show <id>, or quest_<id> if it has no row) to confirm each shows what its name claims. A quest that fails: git revert --no-edit <its parent sha> (never reset), then python3 tools/quest_gate/queue.py set <id> --status todo --failure "REVERTED by sampler (${batch}): <finding>". Then commit QUEUE.tsv if changed ("quests: queue after sample ${batch}", trailer "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>") and push both repos: git -C OSRS-Content push origin HEAD:lane-quest-driver ; git push origin lane-quest-driver. Fold the reviewers' doc_gaps -- ${JSON.stringify(reviewed.flatMap(r => r.doc_gaps || []))} -- deduplicated and real, into docs/QUEST_AUTHORING.md (<= 300 lines; trim the gaps list, not the rules), commit, push. FINISH: write {"pushed": true, "considered": [every accepted id ever handed to a sampler of this batch: ${[...consideredIds].map(x => '"' + x + '"').join(', ')}${consideredIds.size ? ', ' : ''}plus ${accepted.map(r => '"' + r.test_id + '"').join(', ')}], "checked": [...], "sent_back": [ids you sent back now]} to ${STATE}/sample.json (merge, never drop an earlier launch's ids). Report which quests you checked, which you sent back and why, and the doc lines you added.`, { label: 'sample', model: 'opus' }))
} else {
  sample = state.sampled ? 'nothing newly accepted; an earlier launch sampled and pushed' : 'nothing accepted; nothing pushed'
}

phase('Sheet')
let sheet = null
if (!state.sheet_built || freshReviews.length) {
  sheet = await attempt('sheet', 2, () => agent(`${COMMON}

YOUR JOB: build the contact-sheet page for batch ${batch} (quests: ${tests.join(' ')}) with /usr/bin/python3 tools/quest_gate/batch_sheet/build_sheet.py . ${sheetDir} ${tests.join(' ')} [--quality N] then /usr/bin/python3 tools/quest_gate/batch_sheet/render_page.py ${sheetDir} ${batch} "Quest Batch ${batch}". The published total (all .webp + index.html) must be under 58 MB: build at the default quality first; if over, rebuild with --quality lowered until under. Do not publish; do not edit BATCHES.tsv; never commit. FINISH: write the schema JSON to ${STATE}/sheet.json, then return it (index_html = the absolute path, files = the .webp names, bytes = total published bytes, quality used, quests included).`, { label: 'sheet', model: 'sonnet', schema: SHEET_SCHEMA }))
} else log('sheet: already built by a previous launch (read build/author_state/' + batch + '/sheet.json)')

return { batch, reviewed, missing, sample, sheet }
