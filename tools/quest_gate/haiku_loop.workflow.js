export const meta = {
  name: 'quest-haiku-loop',
  description: 'One Haiku author per queued quest test, a Sonnet reviewer that commits each green one and sets the queue row, and an Opus sample of every tenth',
  phases: [
    { title: 'Author', detail: 'Haiku: scaffold, resolve CHECKs, run, fix one thing per run, at most eight runs', model: 'haiku' },
    { title: 'Review', detail: 'Sonnet: gates, shots, diff; commit + queue row' },
    { title: 'Sample', detail: 'Opus: three of every ten accepted, adversarial' },
  ],
}

// The Haiku quest loop -- docs/QUEST_SUITE_KIT.md's phase 5, as a Claude Code
// Workflow script. Run it with the Workflow tool:
//
//   Workflow({ scriptPath: "<repo>/tools/quest_gate/haiku_loop.workflow.js",
//              args: { tests: ["doric", "sheep"], owner: "haiku-a", jobs: 4 } })
//
// `tests` is the list of QUEUE.tsv test_ids to attempt (pick them first with
// `python3 tools/quest_gate/queue.py next --tier 1` or `summary`); one Haiku
// author per test, then one Sonnet reviewer per green test, then an Opus
// sample of every tenth. Each accepted quest is committed on its own and the
// queue row is set by the reviewer, never by the author.
//
// The rules the author gets are the task card in docs/QUEST_AUTHORING.md's
// terms: eight runs per quest, edit only the quest file, BLOCKED instead of
// a workaround, content_bug with file:line instead of a driver patch.

const WT = '/Users/matthewevers/Documents/git_repos/3draster-quest-driver'
const tests = (args && args.tests) || []
const owner = (args && args.owner) || 'haiku'
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
  },
  required: ['test_id', 'outcome', 'runs', 'checks_resolved', 'last_failure', 'blocker', 'doc_gaps'],
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
0. You start in Lumbridge, beside Hans -- the fixture never moves you closer. The scaffold's generated goto rows (t.player.goto_tile, x/z/level -- never t.player.goto, which is a Lua reserved word and will not parse) put you there. If a talk_to (or npc lookup) answers screen_position, fix the goto's coordinates, never the verb.
1. python3 ${WT}/tools/quest_gate/queue.py show ${id}   (the quest_dir and helper it maps to)
   If test/quests/${id}.lua already exists and the queue row's status is todo with a last_failure: that file is the PREVIOUS author's rejected attempt. Read the failure, read the file, and continue from it -- do NOT run new_quest.py over it (it refuses without --force anyway).
   Otherwise: python3 ${WT}/tools/quest_gate/new_quest.py ${id}   (writes test/quests/${id}.lua)
   python3 ${WT}/tools/quest_gate/lint_quest.py --allow-check test/quests/${id}.lua
   Rewards: the scaffold emits skill.snapshot() before the hand-in and skill.expect_gain/a coins delta after quest.expect_complete() for every reward Quest Helper lists -- a quest test with a reward and no reward row is rejected, so if you touch that part of the file keep the reward rows in it.
   Doors: "I can't reach that!" in the chat log after a click means a door, gate or wall is between you and the target -- click_loc the door (op 1, its symbol from the area's configs/*.loc) or goto_tile past it; a refused talk_to with that line is the door, not a broken verb.
2. Read the generated file. Resolve every "-- CHECK" marker by reading the quest's own scripts under ${WT}/OSRS-Content/osrs239-content/server/scripts/quests/<quest_dir>/ : op numbers come from the [oploc<N>,...] / [opnpc<N>,...] trigger heads, chat row text from the ~p_choice lines VERBATIM, stage values from configs/*.constant. Never guess a symbol; the compack is the truth and lint checks it.
3. python3 ${WT}/tools/quest_gate/run.py ${id} --no-build ; python3 ${WT}/tools/quest_gate/gate.py ${id}
   Read the failure block run.py prints (the last FAIL row, its detail, its -FAIL.png path, the last chat lines). Open the -FAIL.png with the Read tool. Fix ONE thing, run again. Count your runs. STOP after eight runs.
4. You may edit ONLY test/quests/${id}.lua. Never script/plugins/, src/, tools/, OSRS-Content/, or another quest's file. Never ::complete the quest under test in its own setup. No local helper functions in the quest file. Never create any other file -- no fixture, no helper script, nothing else under test/.
5. Look at your own screenshots (Read tool on build/quest_gate/${id}/shots/*.png): each must show what its name says. A blank frame, a login screen, or the Character Creator is a failure you have not caught yet.
6. Done when gate.py says green AND lint_quest.py (without --allow-check) is clean. If a verb misbehaves, a cheat does nothing, a symbol will not resolve, or a stage never changes: OUTCOME BLOCKED REQUIRES A t.blocked("<exact seam>") ROW AT THAT POINT, THEN return -- ANYTHING ELSE (a stray FAIL, a run that falls through to expect_complete) IS REJECTED, NOT BLOCKED. Make the file green up to there and report outcome blocked. If the quest's own script misbehaves (a dialogue that cannot be reached, a varp the script never writes), report outcome content_bug with the file:line.

Do NOT commit, push, or edit QUEUE.tsv; the reviewer does both. Report exactly the schema; put the final failure block verbatim in last_failure if you did not reach green.`

const reviewCard = (id, a) => `${COMMON}

You are the reviewer for quest test "${id}". The Haiku author reported: ${JSON.stringify(a, null, 1)}.

Do, in order:
1. If the author's outcome is blocked or content_bug: confirm the file at test/quests/${id}.lua is green up to its t.blocked row (python3 tools/quest_gate/run.py ${id} --no-build ; python3 tools/quest_gate/gate.py ${id} --allow-blocked ; python3 tools/quest_gate/lint_quest.py test/quests/${id}.lua). If it is, commit it (step 4) and set the queue row to that status with --failure "<the blocker>". If it is not, set the row back to todo with the failure and remove the file (git rm is fine only if it was never committed; otherwise git checkout -- the file). Verdict = the outcome.
2. Otherwise re-run yourself: python3 tools/quest_gate/run.py ${id} --no-build ; python3 tools/quest_gate/gate.py ${id} ; python3 tools/quest_gate/lint_quest.py test/quests/${id}.lua. All three must be green or the verdict is rejected (set todo, keep the file, record why in --failure).
3. Read the quest file against the quest's own .rs2 scripts: does it drive the real accept and hand-in branches through clicks and chat, not through a cheat that does the quest's work? Does it end in t.quest.expect_complete() (or expect_stage + one BLOCKED row)? A quest whose helper lists an ExperienceReward or ItemReward must have a reward.* row; missing = one finding. Open EVERY PNG under build/quest_gate/${id}/shots/ with the Read tool and confirm each shows what its name says; count them in shots_checked. Any shot that lies, or any assertion the quest's script makes possible but the test skips, is a finding; two or more findings = rejected.
4. Accept: git add test/quests/${id}.lua ; commit with message "quests: ${id} green (<rows> rows, <shots> shots)" and the trailer "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"; also commit the published evidence in the submodule first (git -C OSRS-Content add osrs239-content/server/scripts/selftest/quest_tests/${id} && git -C OSRS-Content commit -m "selftest/quest_tests: ${id}" with the same trailer), then git add OSRS-Content in the parent before the parent commit. Do not push (the sampler pushes once per batch). Then: python3 tools/quest_gate/queue.py set ${id} --status green --owner ${owner}.
Report the schema.`

const sampleCard = (accepted) => `${COMMON}

You are the Opus sampler for this batch. Accepted this batch: ${JSON.stringify(accepted.map(r => r.test_id))}. Pick these three (or all if fewer): ${JSON.stringify(accepted.filter((_, i) => i % Math.max(1, Math.ceil(accepted.length / 3)) === 0).slice(0, 3).map(r => r.test_id))}.
For each: read the quest file and the quest's own .rs2 scripts; confirm the test drove the real accept and hand-in branches (no cheat did the quest's work), that its ledger's tick counts are plausible for the walks, that no row is ok with an empty detail, and open every published PNG under OSRS-Content/osrs239-content/server/scripts/selftest/quest_tests/<id>/ to confirm each shows what its name claims. A quest that fails: python3 tools/quest_gate/queue.py set <id> --status todo --failure "<your finding>" and revert its commit with git revert --no-edit <sha> (parent) -- never reset. Then push both repos: git -C OSRS-Content push origin HEAD:lane-quest-driver ; git push origin lane-quest-driver. Also collect every author's doc_gaps from this batch: ${JSON.stringify(accepted.flatMap(r => r.doc_gaps || []))} and append the ones that are real, deduplicated, as a "Gaps reported by authors" list at the end of docs/QUEST_AUTHORING.md (stay under 300 lines; if it would exceed, trim the list, not the rules), commit that, push. Report which quests you checked, which you sent back and why, and the doc lines you added.`

// Pipeline: each quest flows author -> review independently; no barrier between quests.
const results = await pipeline(
  tests,
  (id) => agent(authorCard(id), { label: `author:${id}`, phase: 'Author', model: 'haiku', schema: AUTHOR_SCHEMA }),
  (a, id) => a ? agent(reviewCard(id, a), { label: `review:${id}`, phase: 'Review', model: 'sonnet', schema: REVIEW_SCHEMA }).then(r => ({ ...r, doc_gaps: a.doc_gaps, runs: a.runs })) : null,
)

const done = results.filter(Boolean)
const accepted = done.filter(r => r.verdict === 'accepted')
log(`${accepted.length} accepted, ${done.filter(r => r.verdict === 'blocked').length} blocked, ${done.filter(r => r.verdict === 'content_bug').length} content bugs, ${done.filter(r => r.verdict === 'rejected').length} rejected, ${tests.length - done.length} no report`)

phase('Sample')
const sample = accepted.length
  ? await agent(sampleCard(accepted), { label: 'sample', phase: 'Sample', model: 'opus' })
  : 'nothing accepted; nothing pushed'

return { results: done, sample }
