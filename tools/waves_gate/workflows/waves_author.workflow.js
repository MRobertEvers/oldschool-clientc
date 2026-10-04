export const meta = {
  name: 'waves-test-pass',
  description: 'Resumable waves test pass: per unit a Sonnet author writes test/waves/<game>_<unit>.lua (the fight driven for real, the technique rows, the tick ledger against the spec table), a Sonnet reviewer grades it and opens its pictures, and an Opus sampler re-derives three spec numbers per kept test, lands the pass and builds the contact sheet list. One worker at a time',
  phases: [
    { title: 'Author', detail: 'Sonnet, one unit at a time: writes the test, runs it, reads the gate; <id>.author.json' },
    { title: 'Review', detail: 'Sonnet, one unit at a time: the gate with coverage, the pictures opened, a verdict; <id>.review.json' },
    { title: 'Sample', detail: 'Opus: every kept test re-read against sources, tick log and pictures; commits kept tests and evidence, pushes, writes TEST_LEDGER.md and the contact sheet list' },
  ],
}
// The waves test pass (docs/WAVES_ORCHESTRATOR.md section 6). Launch with the Workflow
// tool, scriptPath = this file, and args:
//   { pass: "<branch>-test<N>", branch: "<branch>", game: "inferno" | "colosseum",
//     units: ["nibblers_and_pillars", ...], context: "<one paragraph>" }
// State under build/author_state/<pass>/ (<id>.author.json, <id>.review.json, their
// notebooks, sample.json). Relaunch with the SAME args to continue; never
// resumeFromRunId. A test pass is a building pass: never beside a seam pass or a spec
// pass with encounters in this worktree. Workers run one at a time (owner, 2026-10-03).

const MAIN = '/Users/matthewevers/Documents/git_repos/3draster'
const WT = `${MAIN}/build/orchestrator/worktrees/waves`
const pass = args && args.pass
const BATCH = args && args.branch
const game = args && args.game
const units = (args && args.units) || []
if (!pass || !BATCH) throw new Error('args.pass and args.branch are required')
if (!['inferno', 'colosseum'].includes(game)) throw new Error('args.game must be inferno or colosseum')
if (!units.length) throw new Error('args.units is empty')
const STATE = `${WT}/build/author_state/${pass}`
const DOCS = `${WT}/docs/minigames/${game}`
const ID = (u) => `${game}_${u}`
const extraContext = (args && args.context) ? `\n\nCURRENT PICTURE: ${args.context}` : ''

const COMMON = `You are one worker of the WAVES loop's test pass. Work ONLY inside ${WT} (branch ${BATCH} in both repos: run git -C ${WT} branch --show-current before your first edit and stop if it differs). Absolute paths under ${WT}; the main checkout ${MAIN} and the other worktrees are other loops'. Never git stash/checkout/reset/clean/amend or add -A. Never touch test/quests/, QUEUE.tsv, docs/quest_authoring/, test/raids/, tools/raid_gate/. You edit ONLY the test file named in your job (and your notebook): never script/plugins/, src/, tools/, OSRS-Content/ or another test. OUTPUT DISCIPLINE (owner, 2026-10-03: this loop hung his editor): no command prints more than about 4 KB; every run goes to a log file under ${WT}/build/logs/ and you read its tail; read a run's failures with python3 ${WT}/tools/quest_gate/fail.py <id>, never ledger.tsv whole; never cat the test file (grep -n the row, sed -n twenty lines); wc -c before any cat; no shell command over about 8 KB (write the test in several appends); if the Write or Read tool's hook times out use small bash heredocs. RETURN DISCIPLINE: every returned string under 400 characters, every list under 10 short entries; the detail is in your notebook. Run every run in the FOREGROUND and wait for it (python3 ${WT}/tools/waves_gate/run.py <id> --no-build --no-publish > <log> 2>&1; the gate is python3 ${WT}/tools/waves_gate/gate.py <id>; coverage is python3 ${WT}/tools/waves_gate/waves_coverage.py <id>; lint is python3 ${WT}/tools/quest_gate/lint_quest.py ${WT}/test/waves/<id>.lua). Never a recursive grep or find over the OSRS-Content tree. READ FIRST: ${WT}/docs/WAVES_ORCHESTRATOR.md sections 3, 6, 10 (lessons 3, 7, 14, 15) and 12; ${WT}/docs/minigames/waves_loop/DRIVER_NOTES.md (every verb's shape; it is the contract); ${WT}/test/waves/README.md; ${WT}/docs/QUEST_AUTHORING.md's file shape and verb table. THE RULES OF A WAVE TEST (section 6): the fight is DRIVEN FOR REAL: every kill is the player's own attacks, every mechanic is met by a real move (the prayer switched on the tick, the tile stepped, the pillar stood behind, the supply drunk). Setup takes bring-alongs only (::setlevel, ::give, a spellbook) and t.wave.enter for the unit's first wave. Inside the run the only cheats are read-only readouts. NEVER a god mode, a kill, a heal, a wave skip, a teleport past a phase or a debugproc that performs a mechanic: a test that needs one is BLOCKED with the exact reason, not written around. Three kinds of row, all required: the fight rows; one technique row per published technique of the unit, proved from the tick log; the tick ledger: t.check("spec.scope", true, "mode=<m> party=1") first, then one PASS row named spec.<mechanic_id> per in-scope row of ${DOCS}/encounters/<unit>.tsv whose detail starts "measured <value>[, free text] (spec <value>, grade <G>, tol <tolerance>)", the measured value computed from tick-log rows named in the detail, the WHOLE distribution and never the first instance, never the constant restated. Every interaction row carries a screenshot from the client's own shot writer. Loop on state, never on a fixed tick count; break every loop on your own death; re-attack after every eat, drink, dodge or step; sip a restore before prayer runs out and count the backpack; read a footprint from the npc record's size; destructure the driver's two return values. PRAYER RULE (owner, 2026-10-04): in OSRS a protection prayer is checked on the attack's ANIMATION tick, for Jad and for most npcs; a hit whose damage is decided when the projectile lands is the EXCEPTION and needs a pinned source naming that npc. Never call a swing-tick prayer read a defect, never move a prayer read to the landing tick, and a test bot that prays after the animation is using the wrong technique. Sources: OSRS behaviour first; the wiki outranks the cache where they disagree; the 2004 source is never cited. A content fault you meet is cited by its id in ${WT}/docs/minigames/waves_loop/CONTENT_BUGS.md, or reported as new with the tick-log rows; you fix nothing. PASS STATE DIR: ${STATE} (mkdir -p).${extraContext}`

const AUTHOR = { type: 'object', properties: { test_id: { type: 'string' }, outcome: { type: 'string', enum: ['green', 'blocked', 'content_bug', 'gave_up'] }, runs: { type: 'integer' }, spec_rows_measured: { type: 'integer' }, spec_rows_in_scope: { type: 'integer' }, technique_rows: { type: 'array', items: { type: 'string' } }, coverage: { type: 'string' }, blocker: { type: 'string' }, new_faults: { type: 'array', items: { type: 'string' } } }, required: ['test_id', 'outcome', 'runs', 'spec_rows_measured', 'spec_rows_in_scope', 'technique_rows', 'coverage', 'blocker', 'new_faults'] }
const REVIEW = { type: 'object', properties: { test_id: { type: 'string' }, verdict: { type: 'string', enum: ['accepted', 'rejected', 'blocked', 'content_bug'] }, coverage: { type: 'string' }, shots_opened: { type: 'integer' }, findings: { type: 'array', items: { type: 'string' } } }, required: ['test_id', 'verdict', 'coverage', 'shots_opened', 'findings'] }
const SAMPLE = { type: 'object', properties: { commit: { type: 'string' }, pushed: { type: 'boolean' }, kept: { type: 'array', items: { type: 'string' } }, sent_back: { type: 'array', items: { type: 'string' } }, contact_sheet: { type: 'string' }, notes: { type: 'string' } }, required: ['commit', 'pushed', 'kept', 'sent_back', 'contact_sheet', 'notes'] }

const one = async (label, prompt, opts) => {
  for (let i = 1; i <= 2; i++) {
    const r = await agent(prompt, opts).catch(() => null)
    if (r) return r
    log(`${label}: no result on try ${i} of 2`)
  }
  return null
}

phase('Author')
const authored = []
for (const u of units) {
  const id = ID(u)
  const r = await one(`author:${id}`, `${COMMON}

YOU ARE THE AUTHOR of ONE test: ${WT}/test/waves/${id}.lua, for ${game} unit "${u}". RESUME: if ${STATE}/${id}.review.json exists with verdict accepted, return ${STATE}/${id}.author.json unchanged. Otherwise ${STATE}/${id}.author.progress.md is your notebook: continue from it (and from the test file if it exists: never regenerate over it), and append after every run (what you changed, the first failing row, what you learned). The spec is ${DOCS}/encounters/${u}.tsv with its sidecar ${u}.scope.tsv (which rows a solo run measures; rows marked stat or another mode are skipped by the grader and listed): read both whole, then ${DOCS}/RIG_ANIMATIONS.md's section for the unit's monsters and the unit's rows in ${DOCS}/SOURCES.md and sources/wiki/TECHNIQUES.md (the techniques to prove). If a reviewer's findings exist (${STATE}/${id}.review.json with verdict rejected), fix exactly those first. Write the test, make the first run EARLY, then iterate: run, fail.py, fix one thing, run again; at most 25 runs. Set max_frames from a measured run. Before you finish: lint clean, gate green, coverage FULL (or every gap declared with the reason), and Read at least the entry shot, one prayer-switch shot, one technique shot and the last shot yourself. OUTCOMES: green (gate green, coverage FULL, no cheat); blocked (a DRIVER seam: a verb cannot do something that is really there; the file ends in t.blocked("<exact seam>")); content_bug (the content misbehaves against its spec row: t.blocked("content_bug: <id or finding>"), with the tick-log rows in the notebook); gave_up (runs spent or context compacted; the next author resumes). FINISH: write the schema JSON to ${STATE}/${id}.author.json (coverage = the grader's last line), then return it.`, { label: `author:${id}`, phase: 'Author', model: 'claude-sonnet-5-5', schema: AUTHOR })
  authored.push({ unit: u, id, outcome: r ? r.outcome : 'no_report' })
}
log(`author: ${authored.map(a => a.id + '=' + a.outcome).join(', ')}`)

phase('Review')
const reviewed = []
for (const a of authored) {
  if (a.outcome === 'no_report' || a.outcome === 'gave_up') { reviewed.push({ id: a.id, verdict: 'not_reviewed' }); continue }
  const r = await one(`review:${a.id}`, `${COMMON}

YOU ARE THE REVIEWER of ${WT}/test/waves/${a.id}.lua (author's outcome: ${a.outcome}; its report is ${STATE}/${a.id}.author.json and notebook ${STATE}/${a.id}.author.progress.md). RESUME: if ${STATE}/${a.id}.review.json exists and is newer than the test file, return it unchanged. You edit NOTHING: you judge. Run the test once yourself, then the gate, the coverage grader and lint, each into a log. Then check, quoting the row for every finding: (1) NO CHEAT inside run(): grep the test for t.cheat and :: and read each hit; anything beyond setup bring-alongs, t.wave.enter for the first wave and read-only readouts is a rejection. (2) Every kill is the player's attacks (the tick log's hit rows on the npc come from the player) and every mechanic is met by a move: no narrated mechanic, no fixed-tick loop. (3) Each published technique of the unit has a row proved from the tick log, not asserted. (4) Every in-scope spec row has its spec.<mechanic_id> row, measured from named tick-log rows as a distribution: reject a row that restates the constant, reports the first instance, or writes the spec's figure for a bracket. (5) PICTURES: Read every failing row's shot, the entry, each prayer switch, each technique row, each death animation, the reward if the unit has one, and a spread sample of the rest; say how many you opened; a presentation row is seen in a picture AND asserted from the log. For a blocked or content_bug outcome: confirm the blocker is real by reading the rows it names. VERDICT: accepted, rejected (findings the author can act on), blocked or content_bug (confirmed). FINISH: write the schema JSON to ${STATE}/${a.id}.review.json, then return it.`, { label: `review:${a.id}`, phase: 'Review', model: 'claude-sonnet-5-5', schema: REVIEW })
  reviewed.push({ id: a.id, verdict: r ? r.verdict : 'no_report' })
}
log(`review: ${reviewed.map(r => r.id + '=' + r.verdict).join(', ')}`)

phase('Sample')
const land = await one('sample', `${COMMON}

YOU ARE THE SAMPLER (Opus) and you land the pass. Reviews: ${reviewed.map(r => r.id + '=' + r.verdict).join(', ')}; every report is under ${STATE}/. RESUME: ${STATE}/sample.progress.md is your notebook; if ${STATE}/sample.json says landed, return what it records. For EVERY accepted test: read its tick log beside its ledger rows (fail.py --all and targeted greps, never the whole ledger); RE-DERIVE three spec numbers yourself from the sources the table cites (open the file and line; confirm the value and the grade); open the same pictures the reviewer had to open and say how many; send back (do not keep) a test with a row that restates the content instead of measuring it, a cheat inside the run, a technique asserted and not proved, or a number you cannot re-derive. A test you send back is listed with the row and the reason; the next test pass re-authors it. For blocked and content_bug tests: copy each blocker into ${WT}/docs/minigames/waves_loop/CONTENT_BUGS.md (new ids TEST-<n>; append) or name the driver seam it needs. Then LAND: publish each kept test's evidence (python3 ${WT}/tools/waves_gate/run.py <id> without --no-publish, into a log; evidence goes under selftest/minigames/); commit ONLY explicit paths in both repos (the kept tests, blocked and content_bug tests that end in t.blocked, their evidence, CONTENT_BUGS.md, and ${WT}/docs/minigames/waves_loop/TEST_LEDGER.md with one line per test under a heading for this pass: verdict, rows, coverage, pictures opened, what was sent back and why), message "waves: ${game} wave tests [test:${pass}] -- <kept, blocked, sent back>", trailer "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"; push both branches (git push -q origin ${BATCH}; if rejected pull -q --no-rebase and push again; never force). Never commit a test that was sent back. Write ${STATE}/contact_sheet.tsv: one line per kept test's published screenshot (test id, row name, absolute path of the PNG, the row's detail cut to 120 characters), in run order: the orchestrator builds the owner's contact sheet from it. FINISH: write the schema JSON plus "landed": true to ${STATE}/sample.json (contact_sheet = that file's path), then return the schema.`, { label: 'sample+land', phase: 'Sample', model: 'opus', schema: SAMPLE })
if (!land) log('SAMPLE DID NOT LAND: relaunch with the same args')
return { pass, authored, reviewed, land }
