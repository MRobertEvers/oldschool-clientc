// Node harness: exercise the codec + a simulated two-tab handshake.
global.window = undefined;
var listeners = {};
var G = {
  location: { origin: 'https://x' },
  addEventListener: function (t, f) { (listeners[t] = listeners[t] || []).push(f); },
  removeEventListener: function () {},
  open: function () { return null; }
};
var src = require('fs').readFileSync(require('path').join(__dirname, '..', 'torirs_channel.js'), 'utf8');
new Function('window', 'self', src)(G, G);
var C = G.ToriRSChannel;

var fails = 0;
function ok(c, m) { if (!c) { console.log('FAIL: ' + m); fails++; } else { console.log('  ok: ' + m); } }

// 1. codec round trip
var f = C.writeFrame(C.INTENT.SET, new Uint8Array([1, 2, 3, 250]));
ok(f.length === C.HEADER_BYTES + 4, 'frame is header + payload');
var back = C.readFrames(f.buffer);
ok(back.length === 1, 'one frame parsed');
ok(back[0].type === C.INTENT.SET, 'type survives');
ok(back[0].payload[3] === 250, 'payload survives');

// 2. several frames in one batch
var a = C.writeFrame(1, new Uint8Array([9]));
var b = C.writeFrame(2, new Uint8Array([8, 7]));
var both = new Uint8Array(a.length + b.length);
both.set(a, 0); both.set(b, a.length);
var many = C.readFrames(both.buffer);
ok(many.length === 2 && many[1].payload[1] === 7, 'batch splits back into frames');

// 3. a torn batch keeps what parsed
var torn = both.subarray(0, both.length - 1);
ok(C.readFrames(torn.slice().buffer).length === 1, 'truncated batch drops only the torn tail');

// 4. oversize payload is a caller error, not a silent truncation
var threw = false;
try { C.writeFrame(1, new Uint8Array(C.MAX_PAYLOAD + 1)); } catch (e) { threw = true; }
ok(threw, 'payload over the cap throws');

// 5. empty payload
ok(C.readFrames(C.writeFrame(7, null).buffer)[0].payload.length === 0, 'empty payload round-trips');

console.log(fails ? ('\n' + fails + ' failure(s)') : '\nAll channel codec tests passed.');
process.exit(fails ? 1 : 0);
