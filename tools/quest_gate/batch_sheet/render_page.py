#!/usr/bin/env python3
"""Render the batch screenshot page from index.json. Usage: render_page.py <out_dir> <batch_name> <title>"""
import json, os, sys, html

out_dir, batch, title = sys.argv[1], sys.argv[2], sys.argv[3]
index = json.load(open(os.path.join(out_dir, "index.json"), encoding="utf-8"))
data = json.dumps(index).replace("</", "<\\/")

page = r"""<title>__TITLE__</title>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=IBM+Plex+Sans:wght@400;500;600&family=IBM+Plex+Mono:wght@400;500&display=swap">
<style>
:root{
  --bg:#f3efe6;--ink:#1d1a16;--muted:#6b645a;--line:#d9d2c4;--panel:#fbf9f4;
  --accent:#8a5a1f;--pass:#2e7d4f;--fail:#b3382b;--blocked:#8a6d1c;--chip:#ece6d9;
  --thumb-w:380px;--thumb-h:250px;
}
@media (prefers-color-scheme: dark){:root:not([data-theme="light"]){
  --bg:#171411;--ink:#ece6da;--muted:#a1988b;--line:#3a332b;--panel:#1f1b17;
  --accent:#d9a45b;--pass:#6fc48f;--fail:#e5716a;--blocked:#d9b85c;--chip:#2b2620;}}
:root[data-theme="dark"]{
  --bg:#171411;--ink:#ece6da;--muted:#a1988b;--line:#3a332b;--panel:#1f1b17;
  --accent:#d9a45b;--pass:#6fc48f;--fail:#e5716a;--blocked:#d9b85c;--chip:#2b2620;}
body{background:var(--bg);color:var(--ink);font-family:"IBM Plex Sans",system-ui,sans-serif;padding-inline:16px;padding-block:24px 48px;line-height:1.45}
h1,h2,h3{text-wrap:balance;margin:0}
h1{font-size:1.6rem;font-weight:600;letter-spacing:-.01em}
.lead{max-width:68ch;color:var(--muted);margin:6px 0 20px}
.mono{font-family:"IBM Plex Mono",ui-monospace,monospace;font-variant-numeric:tabular-nums}
.tally{display:flex;flex-wrap:wrap;gap:8px 18px;margin-bottom:28px;font-size:.92rem}
.tally b{font-weight:600}
nav.quests{display:flex;flex-wrap:wrap;gap:8px;margin-bottom:28px}
nav.quests a{color:var(--ink);text-decoration:none;background:var(--chip);padding:5px 10px;border-radius:999px;font-size:.85rem;border:1px solid var(--line)}
nav.quests a:focus-visible,button:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
section.quest{border-top:1px solid var(--line);padding-block:22px 30px}
.qhead{display:flex;flex-wrap:wrap;align-items:baseline;gap:6px 14px;margin-bottom:6px}
.qhead h2{font-size:1.15rem;font-weight:600}
.status{font-size:.72rem;letter-spacing:.06em;text-transform:uppercase;padding:2px 8px;border-radius:4px;border:1px solid currentColor;font-weight:500}
.status.green{color:var(--pass)}.status.blocked,.status.content_bug{color:var(--blocked)}.status.todo{color:var(--fail)}
.note{max-width:78ch;color:var(--muted);font-size:.86rem;margin:4px 0 14px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(230px,1fr));gap:14px 12px}
figure{margin:0;display:flex;flex-direction:column;gap:6px;min-width:0}
.shot{width:100%;aspect-ratio:765/503;max-width:100%;border:1px solid var(--line);border-radius:3px;background-color:#111;background-repeat:no-repeat;background-size:calc(var(--cols) * 100%) auto;cursor:zoom-in;padding:0;display:block}
figcaption{font-size:.78rem;line-height:1.35;min-width:0}
figcaption .name{display:block;font-weight:500;overflow-wrap:anywhere}
figcaption .step{color:var(--muted);overflow-wrap:anywhere}
.v{font-family:"IBM Plex Mono",ui-monospace,monospace;font-size:.7rem;padding:0 5px;border-radius:3px;margin-right:4px}
.v.PASS{color:var(--pass)}.v.FAIL{color:var(--fail)}.v.BLOCKED{color:var(--blocked)}
.v.none{color:var(--muted)}
details.ledger{margin-top:16px;font-size:.82rem}
details.ledger summary{cursor:pointer;color:var(--accent)}
.ledger table{border-collapse:collapse;margin-top:8px;min-width:640px}
.ledger th,.ledger td{text-align:left;padding:3px 10px 3px 0;vertical-align:top;border-bottom:1px solid var(--line)}
.ledger th{font-weight:500;color:var(--muted);font-size:.74rem;text-transform:uppercase;letter-spacing:.05em}
.tablewrap{overflow-x:auto}
dialog{border:none;padding:0;background:transparent;max-width:min(96vw,1560px)}
dialog::backdrop{background:rgba(0,0,0,.82)}
.zoom{background:var(--panel);border-radius:6px;padding:12px;color:var(--ink);display:flex;flex-direction:column;gap:8px}
.zoom .full{width:min(92vw,1530px);aspect-ratio:765/503;background-repeat:no-repeat;background-size:calc(var(--cols) * 100%) auto;background-color:#111;border-radius:3px}
.zoom .cap{display:flex;justify-content:space-between;gap:12px;flex-wrap:wrap;font-size:.85rem}
.zoom button{background:var(--chip);color:var(--ink);border:1px solid var(--line);border-radius:4px;padding:4px 10px;cursor:pointer;font:inherit}
.missing{color:var(--fail)}
@media (prefers-reduced-motion: reduce){*{transition:none!important}}
</style>
<h1>__TITLE__</h1>
<p class="lead">Every screenshot the eight quest tests of batch <span class="mono">__BATCH__</span> took through the real client, in the order the driver took them, each under the ledger row that owns it. Click a shot for full size. Verdicts and notes come from the reviewers' queue rows.</p>
<div class="tally" id="tally"></div>
<nav class="quests" id="nav"></nav>
<div id="quests"></div>
<dialog id="dlg"><div class="zoom"><div class="full" id="zfull"></div><div class="cap"><span id="zcap"></span><button id="zclose" type="button">Close</button></div></div></dialog>
<script>
const DATA = __DATA__;
const COLS = 4;
const pos = p => `${p.col * 100 / (COLS - 1)}% ${p.rows > 1 ? p.row * 100 / (p.rows - 1) : 0}%`;
const esc = s => String(s ?? '').replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const label = s => s === 'green' ? 'green' : s === 'blocked' ? 'blocked' : s === 'content_bug' ? 'content bug' : s || 'not in queue';
const tally = {green:0, blocked:0, content_bug:0, todo:0, shots:0};
const nav = document.getElementById('nav'), root = document.getElementById('quests');
for (const q of DATA.quests) {
  tally[q.status in tally ? q.status : 'todo'] += 1; tally.shots += (q.shots||[]).length;
  const a = document.createElement('a'); a.href = '#q-' + q.id; a.textContent = q.id; nav.appendChild(a);
  const sec = document.createElement('section'); sec.className = 'quest'; sec.id = 'q-' + q.id;
  if (q.missing) { sec.innerHTML = `<div class="qhead"><h2>${esc(q.id)}</h2><span class="status todo">no ledger</span></div><p class="note missing">This quest left no ledger in the batch's session directory.</p>`; root.appendChild(sec); continue; }
  const head = `<div class="qhead"><h2>${esc(q.id)}</h2><span class="status ${esc(q.status)}">${esc(label(q.status))}</span><span class="mono" style="color:var(--muted);font-size:.82rem">${q.rows} rows · ${q.pass} pass · ${q.fail} fail · ${q.blocked} blocked · ${q.shots.length} shots · ${esc(q.ticks)} ticks</span></div>`;
  const note = q.note ? `<p class="note">${esc(q.note)}</p>` : '';
  const figs = q.shots.map((s, i) => `<figure><button class="shot" type="button" data-q="${esc(q.id)}" data-i="${i}" style="--cols:${COLS};background-image:url('${esc(q.id)}-thumb-${s.thumb.sheet}.webp');background-position:${pos(s.thumb)}" aria-label="open ${esc(s.file)}"></button><figcaption><span class="name">${esc(s.file)}</span><span class="v ${esc(s.verdict || 'none')}">${esc(s.verdict || 'auto')}</span><span class="step">${esc(s.step ? s.step + ' — ' : '')}${esc(s.detail)}</span></figcaption></figure>`).join('');
  const ledger = `<details class="ledger"><summary>Ledger, ${q.rows} rows</summary><div class="tablewrap"><table><thead><tr><th>#</th><th>step</th><th>verdict</th><th>ticks</th><th>shots</th><th>detail</th></tr></thead><tbody>${q.steps.map(r => `<tr><td class="mono">${esc(r.index)}</td><td>${esc(r.step)}</td><td><span class="v ${esc(r.verdict)}">${esc(r.verdict)}</span></td><td class="mono">${esc(r.ticks)}</td><td class="mono">${esc(r.shots.join(', '))}</td><td>${esc(r.detail)}</td></tr>`).join('')}</tbody></table></div></details>`;
  sec.innerHTML = head + note + `<div class="grid">${figs}</div>` + ledger;
  root.appendChild(sec);
}
document.getElementById('tally').innerHTML = [['green', tally.green], ['blocked', tally.blocked], ['content bug', tally.content_bug], ['sent back', tally.todo], ['screenshots', tally.shots]].map(([k, v]) => `<span><b class="mono">${v}</b> ${k}</span>`).join('');
const dlg = document.getElementById('dlg'), zfull = document.getElementById('zfull'), zcap = document.getElementById('zcap');
root.addEventListener('click', e => {
  const b = e.target.closest('button.shot'); if (!b) return;
  const q = DATA.quests.find(x => x.id === b.dataset.q); const s = q.shots[+b.dataset.i];
  zfull.style.setProperty('--cols', COLS);
  zfull.style.backgroundImage = `url('${q.id}-full-${s.full.sheet}.webp')`;
  zfull.style.backgroundPosition = pos(s.full);
  zcap.innerHTML = `<b>${esc(q.id)}</b> · ${esc(s.file)} · <span class="v ${esc(s.verdict || 'none')}">${esc(s.verdict || 'auto')}</span> ${esc(s.step)} ${esc(s.detail)}`;
  dlg.showModal();
});
document.getElementById('zclose').addEventListener('click', () => dlg.close());
dlg.addEventListener('click', e => { if (e.target === dlg) dlg.close(); });
</script>
"""
page = page.replace("__TITLE__", html.escape(title)).replace("__BATCH__", html.escape(batch)).replace("__DATA__", data)
open(os.path.join(out_dir, "index.html"), "w", encoding="utf-8").write(page)
print("wrote", os.path.join(out_dir, "index.html"), len(page), "bytes")
