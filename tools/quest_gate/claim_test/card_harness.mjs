// tools/quest_gate/claim_test/card_harness.mjs -- runs a workflow card with mocked agent()/parallel()/pipeline(), capturing
// every prompt. Usage: node card_harness.mjs <card.js> '<args json>' <branch> <gate_exit> <out.json>
import fs from 'fs'
const [file, argsJson, branch, gateExit, out] = process.argv.slice(2)
const args = JSON.parse(argsJson)
const src = fs.readFileSync(file, 'utf8').replace(/^export const meta/m, 'const meta')
const prompts = []
const logs = []
const kind = (opts) => (opts.label || '').split(/[:+ ]/)[0]
const agent = async (prompt, opts) => {
  prompts.push({ label: opts.label, model: opts.model, prompt })
  const k = kind(opts)
  const base = { branch, gate_exit: Number(gateExit), gate_output: 'status ' + (args.batch || '') + ': ok', lock_exit: 0, lock_output: 'content lock: x' }
  if (k === 'state') {
    if (file.includes('seam')) return { ...base, has_triage: true, triage: { seams: [{ key: 'k1', kind: 'driver', quests: ['q1'], summary: 's', evidence: 'e', files: ['f.lua'] }] }, done: [], closed: false, close_commit: '' }
    if (file.includes('parity')) return { ...base, done: [], closed: false, close_commit: '' }
    return { ...base, reviewed: [], authored: [], queue_written: false, sampled: false, sheet_built: false, queue_written_ids: [], sample_considered: [], sample_sent_back: [] }
  }
  if (k === 'claim') return { exit: 0, claimed: args.tests, dropped: [], output: 'claimed' }
  if (k === 'parity') return { test_id: 'q1', quest_dir: 'd', source: 'wiki', legs_fixed: [], legs_left: [], files_changed: [], proof: 'p', open_issues: [] }
  if (k === 'fix') return { key: 'k1', summary: 's', files_changed: [], verified_by: 'v', open_issues: [], unblocks: ['q1'], doc_notes: [] }
  if (k === 'close') return { commit: 'abc', pushed: true, gates: 'g', reverted: [], parity_rows: [], reopened: [], open_issues: [], notes: '' }
  if (k === 'author') return { test_id: 'q1', outcome: 'green', runs: 1, checks_resolved: [], last_failure: '', blocker: '', doc_gaps: [], compacted: false }
  if (k === 'review') return { test_id: opts.label.split(':')[1], verdict: 'accepted', commit: 'c', queue_status: 'green', queue_failure: '', findings: [], shots_checked: 1, doc_gaps: [] }
  if (k === 'sheet') return { index_html: 'i', files: [], bytes: 1, quality: 1, quests: [] }
  return 'ok'
}
const parallel = (fns) => Promise.all(fns.map(f => f()))
const pipeline = (items, f1, f2) => Promise.all(items.map(async (it) => f2(await f1(it), it)))
const log = (m) => logs.push(m)
const phase = () => {}
let result, error = null
try {
  result = await new Function('args', 'phase', 'agent', 'parallel', 'pipeline', 'log', 'return (async () => {' + src + '})()')(args, phase, agent, parallel, pipeline, log)
} catch (e) { error = String(e.message || e) }
fs.writeFileSync(out, JSON.stringify({ error, logs, prompts }, null, 1))
console.log(`${file.split('/').pop()} ${argsJson} on ${branch}: ${error ? 'THREW: ' + error.slice(0, 200) : prompts.length + ' prompts'}`)
