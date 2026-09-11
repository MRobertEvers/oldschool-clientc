/*
 * Two simulated tabs over the real channel code: host <-> panel, including
 * the HELLO/snapshot handshake, delta ordering, coalescing and desync resync.
 */
function makeWindow(name) {
  const w = { name, _l: {}, location: { origin: 'https://x' } };
  w.addEventListener = (t, f) => { (w._l[t] = w._l[t] || []).push(f); };
  w.removeEventListener = (t, f) => {
    if (!w._l[t]) return; const i = w._l[t].indexOf(f); if (i >= 0) w._l[t].splice(i, 1);
  };
  w.fire = (t, ev) => { (w._l[t] || []).slice().forEach(f => { f(ev); }); };
  w.open = () => null;
  return w;
}
const hostWin = makeWindow('host');
const panelWin = makeWindow('panel');

// Cross-wire postMessage between the two fake tabs.
hostWin.postMessage = (data, origin) => {
  hostWin.fire('message', { source: panelWin, origin, data });
};
panelWin.postMessage = (data, origin) => {
  panelWin.fire('message', { source: hostWin, origin, data });
};
panelWin.opener = hostWin;

const src = require('fs').readFileSync(require('path').join(__dirname, '..', 'torirs_channel.js'), 'utf8');
function load(win) { new Function('window', 'self', src)(win, win); return win.ToriRSChannel; }
const HC = load(hostWin);
const PC = load(panelWin);

let fails = 0;
function ok(c, m) { if (!c) { console.log(`FAIL: ${m}`); fails++; } else { console.log(`  ok: ${m}`); } }

const enc = new TextEncoder();
const dec = new TextDecoder();
const intents = [];
const facts = [];
const desyncs = [];

const host = HC.createHost({
  onIntent(type, payload) {
    intents.push({ type, body: payload.length ? JSON.parse(dec.decode(payload)) : {} });
  },
  buildSnapshot() { return enc.encode(JSON.stringify({ tool: 3, square: 'm50_50' })); }
});
const conn = host.attach(panelWin, 'panel');

const panel = PC.createPanel({
  onFact(type, payload, seq) {
    facts.push({ type, seq, body: payload.length ? JSON.parse(dec.decode(payload)) : {} });
  },
  onDesync(had, got) { desyncs.push([had, got]); }
});

// The panel's constructor already sent ready + HELLO, which the host answered.
ok(intents.length === 0, 'HELLO is handled by the channel, not surfaced as an intent');
ok(facts.length === 1 && facts[0].type === PC.FACT.SNAPSHOT, 'panel got a SNAPSHOT');
ok(facts[0].body.tool === 3, 'snapshot carried the host state');
ok(panel.synced(), 'panel reports synced');

// Panel -> host intent.
panel.send(PC.INTENT.SET, enc.encode(JSON.stringify({ tool: 1 })));
ok(intents.length === 1 && intents[0].body.tool === 1, 'intent reached the host');

// Host -> panel deltas, in order.
host.sendDelta(enc.encode(JSON.stringify({ tile: '12,40' })));
host.sendDelta(enc.encode(JSON.stringify({ tile: '12,41' })));
host.flush();
ok(facts.length === 3, 'both deltas applied');
ok(facts[2].body.tile === '12,41', 'newest delta last');

// Coalescing: three hover deltas in one tick collapse to the newest.
const before = facts.length;
host.sendDelta(enc.encode(JSON.stringify({ tile: 'a' })), 'hover');
host.sendDelta(enc.encode(JSON.stringify({ tile: 'b' })), 'hover');
host.sendDelta(enc.encode(JSON.stringify({ tile: 'c' })), 'hover');
host.flush();
ok(facts.length - before === 1, 'coalesced hover deltas collapse to one frame');
ok(facts[facts.length - 1].body.tile === 'c', 'the surviving frame is the newest');

// A quiet tick costs no postMessage.
ok(host.flush() === 0, 'flush with nothing queued sends nothing');

// Desync: drop a batch in transit, the way a real dropped message loses one.
const missed = facts.length;
const realPost = panelWin.postMessage;
let dropped = false;
panelWin.postMessage = (data, origin) => {
  if (!dropped && data && data.type === 'torirs-frames') { dropped = true; return; }
  realPost(data, origin);
};
host.sendDelta(enc.encode(JSON.stringify({ tile: 'x' })));
host.flush();                       // this batch is swallowed in transit
ok(facts.length === missed, 'the dropped batch never arrived');
host.sendDelta(enc.encode(JSON.stringify({ tile: 'y' })));
host.flush();                       // arrives with a seq one higher than expected
ok(desyncs.length === 1, 'gap detected');
const appliedY = facts.some(f => f.body && f.body.tile === 'y');
ok(!appliedY, 'the delta after the gap was NOT applied to a stale mirror');
// The panel asked for a snapshot in response; the host answered it.
ok(facts[facts.length - 1].type === PC.FACT.SNAPSHOT, 'a fresh snapshot closed the gap');
ok(panel.synced(), 'panel is synced again after the resync');

console.log(fails ? (`\n${fails} failure(s)`) : '\nAll channel handshake tests passed.');
process.exit(fails ? 1 : 0);
