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
const authorModel = (args && args.author_model) || 'claude-sonnet-5-5'
const relay = (args && args.relay) || {}   // { test_id: number of legs } -- from `python3 tools/quest_gate/ladder.py <id>`; a quest over ~30 guide steps is authored as a relay
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
const DISCIPLINE = `CONTEXT DISCIPLINE -- long quests run an author out of context, and these rules are why you will not: (1) never open the Quest Helper guide Java: python3 ${WT}/tools/quest_gate/ladder.py <test_id> prints the guide as a small table (step name, kind, target symbol, tile, stage, instruction, items, and the .rs2 file:line of the trigger); name each test row after the step name in its row. (2) Read a script only at the line the ladder's trig column gives (sed -n 'L,+40p' <file>), never the whole file. (3) After every run read the result with python3 ${WT}/tools/quest_gate/fail.py <test_id> (add --all for every failing row); never open ledger.tsv. (4) Never cat the test file: grep -n '<row name>' test/quests/<test_id>.lua and read twenty lines around the hit. (5) Pipe any exploratory command through | head -c 4000. (6) Open a screenshot only when fail.py names it or a row's claim needs the picture. The rules are in ${WT}/docs/quest_authoring/relay.md.`
const REVIEW_DISCIPLINE = `CONTEXT DISCIPLINE for a reviewer -- a reviewer of a long quest ran out of context opening 442 screenshots one by one: read the run through python3 ${WT}/tools/quest_gate/fail.py <test_id> and the guide through python3 ${WT}/tools/quest_gate/ladder.py <test_id>, never the ledger file or the guide Java; read the test file in windows of 150 lines, never whole; open the completion scroll shot, every -FAIL shot, the shots of each fight and of each reward row, and beyond those a SAMPLE of at most 30 shots spread evenly over the run -- say in shots_checked how many you really opened.`
const LEG_SCHEMA = { type: 'object', properties: {
  test_id: { type: 'string' }, leg: { type: 'integer' }, outcome: { type: 'string', enum: ['done', 'blocked', 'content_bug', 'gave_up'] }, runs: { type: 'integer' },
  last_step: { type: 'string' }, last_failure: { type: 'string' }, blocker: { type: 'string' },
  doc_gaps: { type: 'array', items: { type: 'string' } }, compacted: { type: 'boolean' },
}, required: ['test_id', 'leg', 'outcome', 'runs', 'last_step', 'last_failure', 'blocker', 'doc_gaps', 'compacted'] }
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
You are writing ONE client-driven quest test: test_id "${id}". Read ${WT}/docs/QUEST_AUTHORING.md once, in full: it is the CORE, about 20 KB, and it holds the contract, the file shape, the commands, a one-line verb table and the rules. Do NOT read the files under ${WT}/docs/quest_authoring/ before your first run, and do not read any other doc up front -- your context is for the quest, the guide and the ledgers. MAKE YOUR FIRST RUN EARLY: claim and scaffold (or resume the committed or parked file your QUEUE row names), resolve every -- CHECK marker from the quest's own .rs2, then run run.py --no-build and gate.py and read the ledger and the failure block. When a row fails, look up THAT failure: search ${WT}/docs/quest_authoring/INDEX.md for the ledger message, server sentence or shape you see, or grep -rn '<distinctive words from the detail>' ${WT}/docs/quest_authoring/ ; read only the heading it names, fix one thing, run again. Read a whole topic file only the first time you drive that area (verbs-combat.md before a fight, verbs-sail-session.md before a sea leg, verbs-inventory-shops.md before a shop). A citation such as 'trap 31', 'section 8's payout-reopen' or 'seam pass 21 (d)' is resolved by the Citations group at the bottom of INDEX.md. Write your notebook after EVERY run (what you ran, the first failing row, what you changed), so a successor never starts from zero.
${DISCIPLINE}
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

const legCard = (id, k, n, retry) => `${COMMON}
YOU ARE ONE RUNNER IN A RELAY (owner-approved design, 2026-09-29). The quest test "${id}" is long, so it is written as ${n} legs by ${n} authors in sequence. You write LEG ${k} of ${n} and nothing else; a fresh author takes the next leg. You never need the whole quest in your head, and you must not try to load it.
RESUME: if ${STATE}/${id}.leg${k}.json exists and its outcome is "done", return its content verbatim through StructuredOutput and do nothing else. ${STATE}/${id}.leg${k}.progress.md is your notebook: if it exists a previous runner of this leg was killed or gave up -- read it and continue from its last step.${retry ? ' YOU ARE THE FRESH RUNNER AFTER A GIVE-UP: the previous runner\'s runs DO NOT count toward your budget (you have your own ten); if it stopped with nothing failing and steps of this leg unwritten, the job is simply to keep writing the remaining steps from its last passing row.' : ' If it was killed mid-run, count its runs toward your ten.'}
READ, in this order, and nothing else up front: (a) ${WT}/docs/QUEST_AUTHORING.md, the 20 KB core; (b) ${WT}/docs/quest_authoring/relay.md; (c) ${STATE}/${id}.relay.md if it exists -- the hand-off notes of the runners before you (where the player stands, the stage, what is in the backpack); (d) python3 ${WT}/tools/quest_gate/ladder.py ${id} --leg ${k} -- YOUR LEG: every row is a guide step you must drive with a test row named after the step; its third header line names the last step of the previous leg; (e) python3 ${WT}/tools/quest_gate/queue.py show ${id} -- the row's last_failure may name a committed or parked file and what a seam pass fixed.
${DISCIPLINE}
THE FILE is test/quests/${id}.lua. ${k === 1 ? `You are the FIRST runner: if the queue row names a parked or reverted file to start from, copy it to test/quests/${id}.lua; if test/quests/${id}.lua already exists keep it; otherwise python3 ${WT}/tools/quest_gate/new_quest.py ${id}. Then lay the file out for the relay (below).` : `Runners before you wrote legs 1..${k - 1}. If test/quests/${id}.lua does not exist, the previous relay was parked: copy the file the queue row's RELAY STATE names to test/quests/${id}.lua first. Do NOT read or rewrite their legs: find yours with grep -n 'LEG ${k} ' test/quests/${id}.lua.`}
LAYOUT: run python3 ${WT}/tools/quest_gate/run.py --help | grep -c from-leg. If it prints 1 or more, checkpoints have landed: the file uses the legs table of docs/quest_authoring/relay.md ('Checkpoints'): your leg is the ${k}${k === 1 ? 'st' : k === 2 ? 'nd' : k === 3 ? 'rd' : 'th'} entry of legs = { ... }, a self-contained function; iterate with python3 ${WT}/tools/quest_gate/run.py ${id} --from-leg ${k} --no-build (it starts from the checkpoint the previous leg's green run wrote, in seconds) and read it with fail.py ${id} --name ${id}.leg${k}. If it prints 0, checkpoints have not landed: the file keeps a single run function, your leg sits between the comment lines "-- LEG ${k} BEGIN: <your first step>" and "-- LEG ${k} END" (add them; ${k === 1 ? 'you create the first pair' : 'the previous runner left the pair for its leg'}), and every iteration is a full run (python3 ${WT}/tools/quest_gate/run.py ${id} --no-build) read with fail.py ${id}.
IF YOUR LEG'S STEPS ARE ALREADY IN THE FILE (an earlier single-author attempt wrote them): your job is to make them pass and meet the rules, not to rewrite them. Check each ladder row of your leg has a test row of its name; run; fix what fails; remove any stub t.blocked, any GUIDE-GAP marker that cites no real .rs2 line, any row that photographs the same frame twice.
YOU MAY EDIT ONLY: the inside of your leg, the setup list (one ::give or ::setlevel per item or level the guide lists as brought along for YOUR leg, with a comment naming the guide requirement), your notebook and the relay note. Never another leg, never script/plugins/, src/, tools/, OSRS-Content/ or another quest's file. No state is shared between legs except what the player carries: do not rely on a Lua local from an earlier leg.
EVERY RULE OF THE CORE HOLDS INSIDE YOUR LEG: the guide is the spec; every step of your leg is driven by a real row or the file stops at t.blocked naming the exact seam or content_bug with its file:line; t.player.goto_tile is for plain travel only, never past a door, gate, stair, trapdoor, barrier, puzzle or any loc the guide names; ::give only for brought-along items; no debugproc does quest work; never ::setvar a quest var; a boss is fought for real (t.player.attack or t.player.cast, t.npc.await_dead_engaged with opts.eat, gear worn, food carried) and the stage is read a tick after the corpse stage; every PASS row has a detail that says something.
END YOUR LEG AT A QUIET POINT: outside a fight, a dialogue, a cutscene or an instance. ${k < n ? `Your last row is t.check("leg.${k}.end", true, "<the player's tile and level, the quest stage read from the server, the items later legs need>") so the next runner and the checkpoint have a clean boundary.` : `You are the LAST runner: the file ends with t.quest.expect_complete() and a t.check/t.expect row for every reward Quest Helper lists, asserted as the LITERAL documented amount (skill.snapshot before the hand-in). Then a FULL run from a fresh character (python3 ${WT}/tools/quest_gate/run.py ${id} --no-build, no --from-leg), python3 ${WT}/tools/quest_gate/gate.py ${id}, python3 ${WT}/tools/quest_gate/lint_quest.py test/quests/${id}.lua and python3 ${WT}/tools/quest_gate/helper_coverage.py ${id} must all be green, FULL, with zero GUIDE-GAP markers; if the full run fails in an EARLIER leg, fix it there -- you are the one runner allowed to touch every leg, by grep and twenty-line windows, never by reading the file whole.`}
BUDGET: ten runs. Write your notebook after EVERY run (run number, the first failing row from fail.py, what you changed).
HAND-OFF: append to ${STATE}/${id}.relay.md a block headed "## leg ${k}" of at most ten lines: the tile and level the player stands on at the end of your leg, the quest stage value, what is in the backpack and worn that later legs need, the levels setup gives, and anything that surprised you (a door that must be opened, an npc that wanders, a dialogue that pages). The next runner reads only this.
OUTCOME: done = every step of your leg is a PASS row named after it and the run reaches your leg's last row; blocked = the file stops at t.blocked("<exact seam>") inside your leg; content_bug = the quest's own script misbehaves, named with file:line, and the file stops at t.blocked("content_bug: ..."); gave_up = anything else. Do NOT commit, push or edit QUEUE.tsv. FINISH: write the schema JSON to ${STATE}/${id}.leg${k}.json (test_id "${id}", leg ${k}, last_step = the last guide step your leg drives), then return it. You MUST end by calling StructuredOutput even if you gave up.
COMPACTION: if your conversation is ever compacted or summarized while you work, STOP at once, write what you have to the notebook and the relay note, and report outcome gave_up with compacted=true; a fresh runner resumes this leg from your notebook.`

const reviewCard = (id, a) => `${COMMON}
${REVIEW_DISCIPLINE}

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

YOUR JOB: read this batch's persisted state, no edits, no summarising. mkdir -p ${STATE}. reviewed = the content of every ${STATE}/<id>.review.json that parses, copied verbatim; authored = every ${STATE}/<id>.author.json, verbatim; queue_written = ${STATE}/queue.json exists; queue_written_ids = its "written" list (or []); sampled = ${STATE}/sample.json exists and says pushed; sample_considered = its "considered" list (or its "checked" list, or []); sample_sent_back = its "sent_back" list (or []); sheet_built = ${STATE}/sheet.json exists. Never invent an entry. Return exactly the schema.`, { label: 'state', model: 'claude-sonnet-5-5', effort: 'low', schema: STATE_SCHEMA }))) || { reviewed: [], authored: [], queue_written: false, sampled: false, sheet_built: false, queue_written_ids: [], sample_considered: [], sample_sent_back: [] }
// A quest the sampler sent back is authored again: its old author/review files are ignored (the author removes them).
const sentBack = new Set(state.sample_sent_back || [])
const keptReviews = state.reviewed.filter(r => !sentBack.has(r.test_id))
const reviewedIds = new Set(keptReviews.map(r => r.test_id))
const authoredById = Object.fromEntries(state.authored.filter(a => !sentBack.has(a.test_id)).map(a => [a.test_id, a]))
const pending = tests.filter(id => !reviewedIds.has(id))
log(`state: ${keptReviews.length} reviewed, ${Object.keys(authoredById).length} authored, ${sentBack.size} sent back, ${pending.length} pending: ${pending.join(', ') || 'none'}`)

const RETRY_EFFORT = 'medium'
const runRelay = async (id, n) => {
  let runs = 0
  const gaps = []
  for (let k = 1; k <= n; k++) {
    let leg = await attempt(`leg:${id}:${k}`, 2, () => agent(legCard(id, k, n), { label: `leg:${id} ${k}/${n}`, phase: 'Author', model: authorModel, schema: LEG_SCHEMA }))
    // A give-up with nothing failing (the runner simply ran out of runs while
    // still adding steps -- upass leg 2, b40) gets two fresh runners; a give-up
    // on a real failure gets one. Each fresh runner has its own ten runs: b40's
    // retry read "count its runs toward your ten" and quit at once.
    let tries = 0
    while ((!leg || leg.outcome === 'gave_up') && tries < ((leg && /none failing|budget spent|ran out of runs/i.test(leg.last_failure + ' ' + leg.blocker)) ? 2 : 1)) {
      tries++
      log(`${id}: leg ${k}/${n} ${leg ? 'gave up' : 'returned no report'}; fresh runner ${tries} resumes it from the notebook at ${RETRY_EFFORT} effort with its own budget`)
      const again = await attempt(`leg:${id}:${k} (retry ${tries})`, 2, () => agent(legCard(id, k, n, true), { label: `leg:${id} ${k}/${n} (retry ${tries})`, phase: 'Author', model: authorModel, effort: RETRY_EFFORT, schema: LEG_SCHEMA }))
      if (again) leg = again
    }
    if (leg) { runs += leg.runs || 0; gaps.push(...(leg.doc_gaps || [])) }
    if (!leg || leg.outcome !== 'done') {
      return { test_id: id, outcome: leg ? leg.outcome : 'gave_up', runs, checks_resolved: [], last_failure: leg ? leg.last_failure : '', blocker: `relay stopped at leg ${k} of ${n}: ${leg ? leg.blocker : 'the runner returned no report; read ' + STATE + '/' + id + '.leg' + k + '.progress.md'}`, doc_gaps: gaps, compacted: !leg || leg.compacted === true, retried: true }
    }
    log(`${id}: leg ${k}/${n} done at ${leg.last_step}`)
  }
  return { test_id: id, outcome: 'green', runs, checks_resolved: [`relay of ${n} legs`], last_failure: '', blocker: '', doc_gaps: gaps, compacted: false }
}
const results = await pipeline(
  pending,
  async (id) => {
    if (authoredById[id]) return authoredById[id]
    if (relay[id]) return runRelay(id, relay[id])
    const first = await attempt(`author:${id}`, 2, () => agent(authorCard(id), { label: `author:${id}`, phase: 'Author', model: authorModel, schema: AUTHOR_SCHEMA }))
    const compacted = !first || first.compacted === true
    if (!compacted) return first
    log(`${id}: the author compacted (or returned no report); re-running once at ${RETRY_EFFORT} effort from its notebook`)
    const second = await attempt(`author:${id} (retry)`, 2, () => agent(authorCard(id), { label: `author:${id} (retry)`, phase: 'Author', model: authorModel, effort: RETRY_EFFORT, schema: AUTHOR_SCHEMA }))
    return second ? { ...second, retried: true } : second
  },
  async (a, id) => {
    const report = a || { test_id: id, outcome: 'gave_up', runs: 0, checks_resolved: [], last_failure: '', blocker: 'the author returned no report; review whatever file it left', doc_gaps: [], compacted: true }
    return attempt(`review:${id}`, 2, () => agent(reviewCard(id, report), { label: `review:${id}`, phase: 'Review', model: 'claude-sonnet-5-5', schema: REVIEW_SCHEMA }))
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

YOUR JOB: write this batch's queue rows ONCE, from the review files, commit, no push. Rows to write now: ${toWrite.map(r => r.test_id).join(', ')} (read each ${STATE}/<id>.review.json); rows already written by an earlier launch and left alone: ${[...writtenIds].join(', ') || 'none'}. For each: python3 tools/quest_gate/queue.py set <id> --status <queue_status> --owner ${batch} --failure "<queue_failure>" (green rows get --failure ""). If queue.py refuses green because the file carries a t.blocked( that a guard makes unreachable (the ledger has no BLOCKED row), write that row through queue.py's own loader/writer and say so. Quests with no review file (${missing.join(', ') || 'none'}) are left untouched. Then git add test/quests/QUEUE.tsv; git commit -m "quests: queue after batch ${batch}" with the trailer "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>". FINISH: write {"written": [every id written by any launch: ${[...writtenIds].map(x => '"' + x + '"').join(', ')}${writtenIds.size ? ', ' : ''}plus the ids you wrote now]} to ${STATE}/queue.json. Return a one-line summary per row.`, { label: 'queue', model: 'claude-sonnet-5-5' }))
} else log('queue: every reviewed row already written by a previous launch')

phase('Sample')
const consideredIds = new Set((state.sample_considered || []).filter(x => !sentBack.has(x)))
const accepted = reviewed.filter(r => r.verdict === 'accepted' && !consideredIds.has(r.test_id))
let sample = null
if (accepted.length) {
  sample = await attempt('sample', 2, () => agent(`${COMMON}

You are the Opus sampler for batch ${batch}. Accepted: ${JSON.stringify(accepted.map(r => r.test_id))}. Pick these three (or all if fewer): ${JSON.stringify(accepted.filter((_, i) => i % Math.max(1, Math.ceil(accepted.length / 3)) === 0).slice(0, 3).map(r => r.test_id))}.
RESUME DISCIPLINE: ${STATE}/sample.progress.md is your notebook; if it exists continue from its last step (check git log for a revert before reverting again; check git status -sb for "ahead" before pushing again). Append after every step. Quests an earlier launch of this batch already sampled are not yours: ${[...consideredIds].join(', ') || 'none'}.
For each: open the Quest Helper guide and walk its step ladder against the ledger -- every step driven, none narrated by the content, none teleported past, none handed over by ::give/::setvar/a debugproc (any of these sends the quest back); then read the quest file and the quest's own .rs2 scripts; confirm the test drove the real accept and hand-in branches (no cheat did the quest's work), tick counts plausible, no ok row with an empty detail, reward rows assert literal documented amounts, and open every published PNG under OSRS-Content/osrs239-content/server/scripts/selftest/quests/<quest_dir>/play/ (<quest_dir> per queue.py show <id>, or quest_<id> if it has no row) to confirm each shows what its name claims. A quest that fails: git revert --no-edit <its parent sha> (never reset), then python3 tools/quest_gate/queue.py set <id> --status todo --failure "REVERTED by sampler (${batch}): <finding>". Then commit QUEUE.tsv if changed ("quests: queue after sample ${batch}", trailer "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>") and push both repos: git -C OSRS-Content push origin HEAD:lane-quest-driver ; git push origin lane-quest-driver. Fold the reviewers' doc_gaps -- ${JSON.stringify(reviewed.flatMap(r => r.doc_gaps || []))} -- deduplicated and real, into the matching topic file under docs/quest_authoring/ (normal line lengths, under a heading that names the symptom) with ONE line added to docs/quest_authoring/INDEX.md keyed by what an author sees; docs/QUEST_AUTHORING.md is the core and stays under 25 KB -- touch it only when a verb's one-line table entry or a rule itself changes; commit, push. FINISH: write {"pushed": true, "considered": [every accepted id ever handed to a sampler of this batch: ${[...consideredIds].map(x => '"' + x + '"').join(', ')}${consideredIds.size ? ', ' : ''}plus ${accepted.map(r => '"' + r.test_id + '"').join(', ')}], "checked": [...], "sent_back": [ids you sent back now]} to ${STATE}/sample.json (merge, never drop an earlier launch's ids). Report which quests you checked, which you sent back and why, and the doc lines you added.`, { label: 'sample', model: 'opus' }))
} else {
  sample = state.sampled ? 'nothing newly accepted; an earlier launch sampled and pushed' : 'nothing accepted; nothing pushed'
}

phase('Sheet')
let sheet = null
if (!state.sheet_built || freshReviews.length) {
  sheet = await attempt('sheet', 2, () => agent(`${COMMON}

YOUR JOB: build the contact-sheet page for batch ${batch} (quests: ${tests.join(' ')}) with /usr/bin/python3 tools/quest_gate/batch_sheet/build_sheet.py . ${sheetDir} ${tests.join(' ')} [--quality N] then /usr/bin/python3 tools/quest_gate/batch_sheet/render_page.py ${sheetDir} ${batch} "Quest Batch ${batch}". The published total (all .webp + index.html) must be under 58 MB: build at the default quality first; if over, rebuild with --quality lowered until under. Do not publish; do not edit BATCHES.tsv; never commit. FINISH: write the schema JSON to ${STATE}/sheet.json, then return it (index_html = the absolute path, files = the .webp names, bytes = total published bytes, quality used, quests included).`, { label: 'sheet', model: 'claude-sonnet-5-5', schema: SHEET_SCHEMA }))
} else log('sheet: already built by a previous launch (read build/author_state/' + batch + '/sheet.json)')

return { batch, reviewed, missing, sample, sheet }
