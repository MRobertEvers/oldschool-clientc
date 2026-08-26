/*
 * Exercises platform_web_io.js against a stub wasm heap.
 *
 *   node src/platform/test/platform_web_io_test.js
 *
 * The point is the ASYNC CONTRACT, not the byte fiddling: Process must return
 * without waiting, Pending must be truthful from that instant until the slot is
 * filled, and the answer must land in the slot it was started for.
 *
 * Node rather than the C test harness because the subject is JavaScript -- this
 * file IS the platform executor on the browser lane, and running it under the
 * thing it actually runs under is worth more than a C shim that pretends.
 *
 * The layout below is a STUB, deliberately independent of asyncio_abi.c: the
 * executor is supposed to work off whatever layout the queue reports, so
 * hard-coding one here is how "it reads the reported offsets, not offsets it
 * assumed" gets tested at all. It is not a claim about the real struct.
 */
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const SRC = path.join(__dirname, '..', 'platform_web_io.js');

// --- the layout the stub heap uses, mirroring wasm32 C ---------------------
const ITEM = { kind: 0, u: 4, error: 260, data: 264, dataSize: 268, size: 272 };
const IO = { slots: 0, active: 8704, activeCount: 8832, size: 8836 };
const MAX_ITEMS = 32, MAX_PATH = 256;
const ABI = [
  0x494f4131, IO.size, IO.slots, IO.active, IO.activeCount, MAX_ITEMS,
  ITEM.size, ITEM.kind, ITEM.error, ITEM.data, ITEM.dataSize, ITEM.u,
  0, 4, 8, 12,   // cache epoch/table/archive/flags
  0, 0, 0, 0,    // config, script, reftable, file
  MAX_PATH,
];

// --- stub heap + runtime --------------------------------------------------
const HEAP_BYTES = 1 << 20;
const buffer = new ArrayBuffer(HEAP_BYTES);
const HEAPU8 = new Uint8Array(buffer);
const HEAP32 = new Int32Array(buffer);
let brk = 0x10000;                       // bump allocator, well clear of the queue
const allocations = new Set();

const ctx = {
  console,
  HEAPU8, HEAP32,
  _malloc(n) { const p = brk; brk += (n + 7) & ~7; allocations.add(p); return p; },
  _free(p) { allocations.delete(p); },
  _ToriRS_IO_DescribeAbiCount: () => ABI.length,
  _ToriRS_IO_DescribeAbi(ptr) { for (let i = 0; i < ABI.length; i++) HEAP32[(ptr >> 2) + i] = ABI[i]; },
  UTF8ToString(p) { let e = p; while (HEAPU8[e]) e++; return Buffer.from(HEAPU8.slice(p, e)).toString('utf8'); },
  UTF8ArrayToString(h, p, len) { return Buffer.from(h.slice(p, p + len)).toString('utf8'); },
  Module: {},
  LibraryManager: { library: {} },
};
ctx.mergeInto = (lib, obj) => Object.assign(lib, obj);
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(SRC, 'utf8'), ctx, { filename: 'platform_web_io.js' });

// The library declares `$TORIRS_WEB_IO` as an emscripten dep-object; emscripten
// would hoist it to a global. Do that by hand.
const L = ctx.LibraryManager.library;
ctx.TORIRS_WEB_IO = L.$TORIRS_WEB_IO;

// --- the host the executor calls -----------------------------------------
let resolveRead;
const served = [];
ctx.Module.torirsHostIO = {
  readFile(p) { served.push(p); return new Promise(r => { resolveRead = r; }); },
  readClientFile(p) { served.push(p); return Promise.resolve(null); },
  writeClientFile(p, b) { served.push(`W:${p}`); return Promise.resolve(); },
};

// --- lay one SCRIPT item into slot 3 --------------------------------------
const IO_PTR = 0x100;
const SLOT = 3;
const KIND_SCRIPT = 3;
const itemPtr = IO_PTR + IO.slots + SLOT * ITEM.size;
HEAP32[(itemPtr + ITEM.kind) >> 2] = KIND_SCRIPT;
Buffer.from('plugins/plugins.ini\0', 'utf8').forEach((b, i) => { HEAPU8[itemPtr + ITEM.u + i] = b; });
HEAP32[(IO_PTR + IO.active) >> 2] = SLOT;
HEAP32[(IO_PTR + IO.activeCount) >> 2] = 1;

(async () => {
  const fail = (m) => { console.error(`FAIL: ${m}`); process.exit(1); };

  const px = L.PlatformX_IO_New();
  const dir = brk; brk += 64;
  Buffer.from('script\0', 'utf8').forEach((b, i) => { HEAPU8[dir + i] = b; });
  L.PlatformX_IO_InitScriptPath(px, dir);

  // Process must NOT block.
  const t0 = Date.now();
  L.PlatformX_IO_Process(px, IO_PTR);
  if (Date.now() - t0 > 50) fail('Process blocked');

  // Active list consumed, item outstanding.
  if (HEAP32[(IO_PTR + IO.activeCount) >> 2] !== 0) fail('active list not reset');
  if (L.PlatformX_IO_Pending(px, IO_PTR) !== 1) fail('Pending should be 1 while in flight');
  if (HEAP32[(itemPtr + ITEM.error) >> 2] !== 0) fail('outstanding item must not look failed');
  if (served[0] !== 'script/plugins/plugins.ini') fail(`wrong path: ${served[0]}`);

  // Answer it.
  const payload = Buffer.from('[plugin:x]\nsource=x.lua\n', 'utf8');
  resolveRead(new Uint8Array(payload));
  await new Promise(r => setImmediate(r));

  if (L.PlatformX_IO_Pending(px, IO_PTR) !== 0) fail('Pending should be 0 once answered');
  if (HEAP32[(itemPtr + ITEM.error) >> 2] !== 0) fail('answered item should not be an error');
  const size = HEAP32[(itemPtr + ITEM.dataSize) >> 2];
  const ptr = HEAP32[(itemPtr + ITEM.data) >> 2];
  if (size !== payload.length) fail(`size ${size} != ${payload.length}`);
  if (Buffer.from(HEAPU8.slice(ptr, ptr + size)).toString('utf8') !== payload.toString('utf8'))
    fail('payload mismatch');

  // A failing read must land as error_code -1, and must not be an outage.
  const item2 = IO_PTR + IO.slots + 5 * ITEM.size;
  HEAP32[(item2 + ITEM.kind) >> 2] = KIND_SCRIPT;
  Buffer.from('missing.lua\0', 'utf8').forEach((b, i) => { HEAPU8[item2 + ITEM.u + i] = b; });
  HEAP32[(IO_PTR + IO.active) >> 2] = 5;
  HEAP32[(IO_PTR + IO.activeCount) >> 2] = 1;
  ctx.Module.torirsHostIO.readFile = () => Promise.reject(new Error('404'));
  L.PlatformX_IO_Process(px, IO_PTR);
  await new Promise(r => setImmediate(r));
  if (HEAP32[(item2 + ITEM.error) >> 2] !== -1) fail('a failed read must be error -1');
  if (L.PlatformX_IO_ServerReachable(px) !== 1) fail('a 404 must NOT read as unreachable');

  // Only an explicitly-unreachable error is an outage.
  const item3 = IO_PTR + IO.slots + 6 * ITEM.size;
  HEAP32[(item3 + ITEM.kind) >> 2] = KIND_SCRIPT;
  Buffer.from('x.lua\0', 'utf8').forEach((b, i) => { HEAPU8[item3 + ITEM.u + i] = b; });
  HEAP32[(IO_PTR + IO.active) >> 2] = 6;
  HEAP32[(IO_PTR + IO.activeCount) >> 2] = 1;
  ctx.Module.torirsHostIO.readFile = () => {
    const e = new Error('nothing answered'); e.torirsUnreachable = true; return Promise.reject(e);
  };
  L.PlatformX_IO_Process(px, IO_PTR);
  await new Promise(r => setImmediate(r));
  if (L.PlatformX_IO_ServerReachable(px) !== 0) fail('an unreachable transport must read as down');

  // Two queues must not see each other's pending.
  const IO_B = 0x8000;
  if (L.PlatformX_IO_Pending(px, IO_B) !== 0) fail('pending must be per-queue');

  console.log('PASS: Process non-blocking, Pending truthful, slot filled, ' +
              '404 != outage, unreachable == outage, pending is per-queue');
})();
