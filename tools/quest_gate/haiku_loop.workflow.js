export const meta = {
  name: 'quest-author-loop',
  description: 'One author per queued quest test (model from args.author_model), a Sonnet reviewer that commits each green one and sets the queue row, and an Opus sample of every tenth',
  phases: [
    { title: 'Author', detail: 'scaffold or resume, resolve CHECKs, run, fix one thing per run, at most eight runs' },
    { title: 'Review', detail: 'Sonnet: gates, shots, diff, reward rows, cheated hand-ins; commit + queue row' },
    { title: 'Sample', detail: 'Opus: three of every ten accepted, adversarial; commits the queue and pushes' },
  ],
}

// The quest author loop -- docs/QUEST_SUITE_KIT.md's phase 5, as a Claude Code
// Workflow script. The Workflow tool refuses a scriptPath outside its working
// directory, so pass this file's CONTENT inline as `script`, with
//   args: { tests: ["doric", "sheep"], owner: "sonnet-b2", author_model: "sonnet" }
// `tests` are QUEUE.tsv test_ids (pick with tools/quest_gate/queue.py next/summary);
// author_model defaults to haiku. One author per test, a Sonnet reviewer per
// test that commits green ones and sets the queue row, an Opus sampler that
// checks three of every ten accepted, commits the queue and pushes the batch.
//
// Run book rule: a Haiku author whose context compacts mid-quest is replaced by
// Sonnet 5 at medium effort for that quest (see ESCALATE_MODEL below).
//
// Measured 2026-09-19 over four Haiku batches: 4 green from 24 attempts, every
// accepted quest under ~20 generated steps; the first Sonnet batch is the
// comparison. Edit the cards HERE and paste; the two must stay identical.

const WT = '/Users/matthewevers/Documents/git_repos/3draster-quest-driver'
const tests = (args && args.tests) || []
const owner = (args && args.owner) || 'haiku'
const authorModel = (args && args.author_model) || 'haiku'
if (!tests.length) throw new Error('args.tests is empty: pick test_ids with tools/quest_gate/queue.py first')

const COMMON = `Work ONLY inside ${WT} (git worktree, branch lane-quest-driver). NEVER cd into, build in, or touch /Users/matthewevers/Documents/git_repos/3draster. Absolute paths under ${WT} for every command. Never git stash/checkout/reset/clean. Never commit saves/, build*, cache*, manifests/.*.ini, preferences.ini, plugin_prefs.ini.`

const AUTHOR_SCHEMA = {
  type: 'object',
  properties: {
    test_id: { type: 'string' },
    outcome: { type: 'string', enum: ['green', 'blocked', 'content_bug', 'gave_up'] },
    runs: { type: 'integer' },
    checks_resolved: { type: 'array', items: { type: 'string' }, description: 'each -- CHECK marker and how you found the answer (file:line in the .rs2)' },
    last_failure: { type: 'string', description: 'the last failure block verbatim, or empty' },
    blocker: { type: 'string', description: 'for blocked/content_bug: the exact seam or file:line' },
    doc_gaps: { type: 'array', items: { type: 'string' }, description: 'what QUEST_AUTHORING.md did not tell you that you needed' },
    compacted: { type: 'boolean', description: 'true if your conversation was compacted/summarized at any point during this task' },
  },
  required: ['test_id', 'outcome', 'runs', 'checks_resolved', 'last_failure', 'blocker', 'doc_gaps', 'compacted'],
}

const REVIEW_SCHEMA = {
  type: 'object',
  properties: {
    test_id: { type: 'string' },
    verdict: { type: 'string', enum: ['accepted', 'rejected', 'blocked', 'content_bug'] },
    commit: { type: 'string' },
    queue_status: { type: 'string' },
    findings: { type: 'array', items: { type: 'string' } },
    shots_checked: { type: 'integer' },
  },
  required: ['test_id', 'verdict', 'commit', 'queue_status', 'findings', 'shots_checked'],
}

const authorCard = (id) => `${COMMON}

You are writing ONE client-driven quest test: test_id "${id}". Read ${WT}/docs/QUEST_AUTHORING.md once, in full. It is the only page you need; it names every verb, the result words, the traps, the run command, and the definition of done.

Steps:
0. You start in Lumbridge, beside Hans -- the fixture never moves you closer. The scaffold's generated goto rows (t.player.goto_tile, x/z/level -- never t.player.goto, which is a Lua reserved word and will not parse) put you there. If a talk_to (or npc lookup) answers "screen_position: ..." read the reason after the colon: not_visible means aim (goto closer or another side), not_found means the npc is not in the world at that tile -- check its spawn row, never the verb.
1. python3 ${WT}/tools/quest_gate/queue.py show ${id}   (the quest_dir and helper it maps to, and last_failure -- READ IT, it is the previous reviewer telling you exactly what to fix)
   If test/quests/${id}.lua already exists and the queue row's status is todo with a last_failure: that file is the PREVIOUS author's rejected attempt. Read the failure, read the file, and continue from it -- do NOT run new_quest.py over it (it refuses without --force anyway). If the file is committed (git ls-files knows it) and ends on a t.blocked whose reason the queue row says is FIXED, delete the t.blocked and drive on from there. If the last_failure points at a reverted commit (git show <sha>:test/quests/${id}.lua), that reviewed version is the one to resume, not the shorter file on disk.
   Otherwise: python3 ${WT}/tools/quest_gate/new_quest.py ${id}   (writes test/quests/${id}.lua)
   python3 ${WT}/tools/quest_gate/lint_quest.py --allow-check test/quests/${id}.lua
   Rewards: the scaffold emits skill.snapshot() before the hand-in and skill.expect_gain/a coins delta after quest.expect_complete() for every reward Quest Helper lists -- a quest test with a reward and no reward row is rejected. Assert the LITERAL reward the quest documents (150 xp, 60 coins), never a number read back from the scroll: a row that compares the game against itself cannot fail and is rejected. An item reward (a talisman, a key) is asserted with inv.expect_has after expect_complete.
   Doors: "I can't reach that!" in the chat log after a click means a door, gate or wall is between you and the target -- click_loc the door (op 1, its symbol from the area's configs/*.loc) or goto_tile past it; a refused talk_to with that line is the door, not a broken verb. A door or any loc that changes form resolves by its base symbol now; "screen_position: not_found" on a loc that is on screen is a real seam -- t.blocked it with the symbol and tile.
   Chat order: an [opnpc1,...] branch almost always opens with the PLAYER's line (~chatplayer_anim), so a chat.play list starting "npc:..." dies on page 1 with "expected kind=npc, got player" -- the generator now writes the list from the script's own branch; verify it against the .rs2, and when you must write one yourself (a nested p_choice the generator marked -- CHECK) spell every page in the order it actually opens.
   Every row's detail must say something: a t.check whose detail is empty or only a result word is rejected by the sampler; say what was read (the value, the count, the tile). click_obj answers ok with a nil detail: call it directly and write the before/after count yourself.
   New since the last batch, in the verb table: t.player.use_item_on_item(a, b) for a backpack item used on another; t.player.attack(npc) and t.npc.await_dead(npc, ticks) for a required kill; click_loc steps off the loc's own tile by itself, so covered from every pose is now a real seam.
2. Read the generated file. Resolve every "-- CHECK" marker by reading the quest's own scripts under ${WT}/OSRS-Content/osrs239-content/server/scripts/quests/<quest_dir>/ : op numbers come from the [oploc<N>,...] / [opnpc<N>,...] trigger heads, chat row text from the ~p_choice lines VERBATIM, stage values from configs/*.constant. Never guess a symbol; the compack is the truth and lint checks it.
3. python3 ${WT}/tools/quest_gate/run.py ${id} --no-build ; python3 ${WT}/tools/quest_gate/gate.py ${id}
   Read the failure block run.py prints (the last FAIL row, its detail, its -FAIL.png path, the last chat lines). Open the -FAIL.png with the Read tool. Fix ONE thing, run again. Count your runs. STOP after eight runs.
4. You may edit ONLY test/quests/${id}.lua. Never script/plugins/, src/, tools/, OSRS-Content/, or another quest's file. Never ::complete the quest under test in its own setup. No local helper functions in the quest file. Never create any other file -- no fixture, no helper script, nothing else under test/. Lua: nothing may follow a return in the same block; dead code after t.blocked(...); return is a SYNTAX error that fails the whole run.
5. Look at your own screenshots (Read tool on build/quest_gate/${id}/shots/*.png): each must show what its name says. A blank frame, a login screen, or the Character Creator is a failure you have not caught yet. If an npc or a loc you need is visible in a shot, click it before declaring anything blocked. A row whose detail carries "[frame unchanged]" has no shot on purpose.
6. Done when gate.py says green AND lint_quest.py (without --allow-check) is clean. If a verb misbehaves, a cheat does nothing, a symbol will not resolve, or a stage never changes: OUTCOME BLOCKED REQUIRES A t.blocked("<exact seam>") ROW AT THAT POINT, THEN return -- ANYTHING ELSE (a stray FAIL, a run that falls through to expect_complete) IS REJECTED, NOT BLOCKED. Make the file green up to there and report outcome blocked. If the quest's own script misbehaves (a dialogue that cannot be reached, a varp the script never writes), report outcome content_bug with the file:line.

Five things earlier batches got rejected for -- one line each:
(a) If an npc is not where the goto sent you, that is not "location unknown": grep -rn --include='*.spawn' "<symbol>" OSRS-Content/osrs239-content/server/scripts/  (the whole tree -- a quest's own npcs are often under quests/<dir>/configs, not areas/) -- each row is "symbol x z level" under an ==== NPC ==== header, decode it and goto_tile there.
(b) Name every quest.expect_stage row quest.stage.<constant> (never quest_started or cook.started) -- the gate's minimum shape needs a row whose NAME starts with the literal "quest." prefix, and if you end BLOCKED before hand-in, quest.expect_complete's own four rows never fire to cover it.
(c) Never take a screenshot to pad the row count -- a shot exists because t.exec/t.check fired after a click that changed something.
(d) Never cheat the quest's own work with ::give/::kill/::setvar -- an item, kill, craft, search or fetch the quest's own .rs2 makes you do is driven through clicks; ::give is only for prerequisites Quest Helper lists as brought-along items, never the quest's own deliverable, and the reviewer reads your file against the .rs2 looking for exactly this. Setting the quest's own stage varp with ::setvar mid-run is the same cheat.
(e) Resolve every "-- CHECK" marker before your FIRST run, not after -- lint_quest.py without --allow-check refuses a file that still has one.

Do NOT commit, push, or edit QUEUE.tsv; the reviewer does both. Report exactly the schema; put the final failure block verbatim in last_failure if you did not reach green. You MUST end by calling StructuredOutput with the schema even if you gave up.
COMPACTION: if your conversation is ever compacted or summarized while you work on this quest (you will see a summary of earlier context instead of the messages themselves), STOP at once, do not edit the file further, and report outcome gave_up with compacted=true and blocker "context compacted"; a larger model resumes the file from where you left it.`

const reviewCard = (id, a) => `${COMMON}

You are the reviewer for quest test "${id}". The author reported: ${JSON.stringify(a, null, 1)}.

Do, in order:
1. If the author's outcome is blocked or content_bug: confirm the file at test/quests/${id}.lua is green up to its t.blocked row (python3 tools/quest_gate/run.py ${id} --no-build ; python3 tools/quest_gate/gate.py ${id} --allow-blocked ; python3 tools/quest_gate/lint_quest.py test/quests/${id}.lua). If it is, commit it (step 4) and set the queue row to that status with --failure "<the blocker>". If it is not, set the row back to todo with the failure and remove the file (git rm is fine only if it was never committed; otherwise git checkout -- the file). Verdict = the outcome. A blocker that names an npc or loc visible in the author's own shots, never clicked, is not a blocker: reject. An author outcome of gave_up: judge whatever file exists by step 2; if there is no file, set the queue row to todo with the author's blocker text and verdict rejected.
2. Otherwise re-run yourself: python3 tools/quest_gate/run.py ${id} --no-build ; python3 tools/quest_gate/gate.py ${id} ; python3 tools/quest_gate/lint_quest.py test/quests/${id}.lua. All three must be green or the verdict is rejected (set todo, keep the file, record why in --failure).
3. Read the quest file against the quest's own .rs2 scripts: does it drive the real accept and hand-in branches through clicks and chat, not through a cheat that does the quest's work? EXPLICIT FINDING: any item, kill, craft, search or fetch the quest's own .rs2 makes the player do that the file instead hands over with ::give/::kill/::setvar is a cheated hand-in -- name it as its own finding, not a style note; ::give is only for prerequisites Quest Helper lists as brought-along, never the quest's own deliverable. Does it end in t.quest.expect_complete() (or expect_stage + one BLOCKED row)? A quest whose helper lists an ExperienceReward or ItemReward must have a reward.* row asserting the LITERAL documented amount (a row that compares against a number read from the scroll is one finding; an item reward needs an inv.expect_has row). A t.check row with an empty detail is one finding. Open EVERY PNG under build/quest_gate/${id}/shots/ with the Read tool and confirm each shows what its name says; count them in shots_checked. Any shot that lies, or any assertion the quest's script makes possible but the test skips, is a finding; two or more findings = rejected.
4. Accept: git add test/quests/${id}.lua ; commit with message "quests: ${id} green (<rows> rows, <shots> shots)" and the trailer "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"; also commit the published evidence in the submodule first (git -C OSRS-Content add osrs239-content/server/scripts/selftest/quest_tests/${id} && git -C OSRS-Content commit -m "selftest/quest_tests: ${id}" with the same trailer), then git add OSRS-Content in the parent before the parent commit. Do not push (the sampler pushes once per batch). Then: python3 tools/quest_gate/queue.py set ${id} --status green --owner ${owner}. If you reject or block, also delete any published evidence run.py left under OSRS-Content/osrs239-content/server/scripts/selftest/quest_tests/${id}/ that is not committed (git -C OSRS-Content status shows it untracked).
Report the schema.`

const sampleCard = (accepted) => `${COMMON}

You are the Opus sampler for this batch. Accepted this batch: ${JSON.stringify(accepted.map(r => r.test_id))}. Pick these three (or all if fewer): ${JSON.stringify(accepted.filter((_, i) => i % Math.max(1, Math.ceil(accepted.length / 3)) === 0).slice(0, 3).map(r => r.test_id))}.
For each: read the quest file and the quest's own .rs2 scripts; confirm the test drove the real accept and hand-in branches (no cheat did the quest's work), that its ledger's tick counts are plausible for the walks, that no row is ok with an empty detail, that reward rows assert literal documented amounts, and open every published PNG under OSRS-Content/osrs239-content/server/scripts/selftest/quest_tests/<id>/ to confirm each shows what its name claims. A quest that fails: python3 tools/quest_gate/queue.py set <id> --status todo --failure "<your finding>" and revert its commit with git revert --no-edit <sha> (parent) -- never reset. Then commit test/quests/QUEUE.tsv ("quests: queue after batch ${owner}", trailer "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>") and push both repos: git -C OSRS-Content push origin HEAD:lane-quest-driver ; git push origin lane-quest-driver. Also collect every author's doc_gaps from this batch: ${JSON.stringify(accepted.flatMap(r => r.doc_gaps || []))} and append the ones that are real, deduplicated, as a "Gaps reported by authors" list at the end of docs/QUEST_AUTHORING.md (stay under 300 lines; if it would exceed, trim the list, not the rules), commit that, push. Report which quests you checked, which you sent back and why, and the doc lines you added.`

// Run book rule (owner, 2026-09-20): if a Haiku author's context compacts during
// the authoring step, that quest switches to Sonnet 5 at medium effort. The
// author reports `compacted`; an author that returned no report at all (schema
// failure, which compaction also causes) is treated the same way. The Sonnet
// author resumes the file the Haiku author left (the card's own resume rule).
const ESCALATE_MODEL = 'sonnet', ESCALATE_EFFORT = 'medium'
const results = await pipeline(
  tests,
  async (id) => {
    const first = await agent(authorCard(id), { label: `author:${id}`, phase: 'Author', model: authorModel, schema: AUTHOR_SCHEMA }).catch(() => null)
    const compacted = !first || first.compacted === true
    if (!compacted || authorModel === ESCALATE_MODEL) return first
    log(`${id}: the ${authorModel} author compacted (or returned no report); re-running with ${ESCALATE_MODEL} at ${ESCALATE_EFFORT} effort`)
    const second = await agent(authorCard(id), { label: `author:${id} (escalated)`, phase: 'Author', model: ESCALATE_MODEL, effort: ESCALATE_EFFORT, schema: AUTHOR_SCHEMA }).catch(() => null)
    return second ? { ...second, escalated_from: authorModel } : second
  },
  (a, id) => {
    const report = a || { test_id: id, outcome: 'gave_up', runs: 0, checks_resolved: [], last_failure: '', blocker: 'the author returned no report; review whatever file it left', doc_gaps: [], compacted: true }
    return agent(reviewCard(id, report), { label: `review:${id}`, phase: 'Review', model: 'sonnet', schema: REVIEW_SCHEMA }).then(r => ({ ...r, doc_gaps: report.doc_gaps, runs: report.runs, escalated_from: report.escalated_from }))
  },
)

const done = results.filter(Boolean)
const accepted = done.filter(r => r.verdict === 'accepted')
log(`${accepted.length} accepted, ${done.filter(r => r.verdict === 'blocked').length} blocked, ${done.filter(r => r.verdict === 'content_bug').length} content bugs, ${done.filter(r => r.verdict === 'rejected').length} rejected, ${tests.length - done.length} no report`)

phase('Sample')
const sample = accepted.length
  ? await agent(sampleCard(accepted), { label: 'sample', phase: 'Sample', model: 'opus' })
  : 'nothing accepted; nothing pushed'

return { results: done, sample }