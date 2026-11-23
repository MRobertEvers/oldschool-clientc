'use strict';
/*
 * The web IO executor's derived-record kinds (asyncio.h TORIRS_IOK_DERIVED_READ
 * / _WRITE; docs/CS2_JIT.md 4.6), evaluated as shipped under node against a
 * stub wasm heap and a stub host.
 *
 *   - the IOA4 layout is read (the two derived-name slots), and an IOA3
 *     layout is refused loudly;
 *   - Process returns at once and the item's `pending` word is 1 until the
 *     answer is in it, then 0 (the runner's only signal);
 *   - DERIVED_READ answers the host's bytes in a payload block, or -1 with no
 *     data when the host has no record, no readDerived at all, or the name is
 *     not a record name;
 *   - DERIVED_WRITE copies the lent bytes out of the heap BEFORE the first
 *     await (the buffer is overwritten right after Process returns and the
 *     host still sees the original), answers 0 when the host committed and
 *     -1 when it did not, and never frees or replaces the lent pointer;
 *   - __torirs_io_telemetry.derived counts reads, misses, writes, failures
 *     and bytes.
 *
 *   node src/platform/test/platform_web_io_derived_test.js
 */
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const SRC = path.join(__dirname, '..', 'platform_web_io.js');

const ITEM = { kind: 0, u: 4, error: 260, data: 264, dataSize: 268, pending: 272, size: 276 };
const IO = { active: 0, activeCount: 4, size: 8 };
const MAX_PATH = 256;
const NAME_OFF = 0;
const NAME_MAX = 64;
const abiFor = (magic, withDerived) => {
  const v = [
    magic, IO.size, IO.active, IO.activeCount,
    ITEM.size, ITEM.kind, ITEM.error, ITEM.data, ITEM.dataSize, ITEM.pending, ITEM.u,
    0, 4, 8, 12,
    0, 0, 0, 0,
    MAX_PATH,
  ];
  if (withDerived) { v.push(NAME_OFF, NAME_MAX); }
  return v;
};

const K = { DERIVED_READ: 8, DERIVED_WRITE: 9 };

function makeRuntime(abi) {
  const buffer = new ArrayBuffer(1 << 20);
  const HEAPU8 = new Uint8Array(buffer);
  const HEAP32 = new Int32Array(buffer);
  let brk = 0x10000;
  const errors = [];
  const ctx = {
    console: { log: () => {}, warn: () => {}, error: (m) => errors.push(m) },
    performance: { now: () => Date.now() },
    Promise, Map, Array, Error, Uint8Array, Int32Array, Object,
    HEAPU8, HEAP32,
    _malloc(n) { const p = brk; brk += (n + 7) & ~7; return p; },
    _free() {},
    _ToriRS_IO_AllocPayload(n) { const p = brk; brk += ((n || 1) + 7) & ~7; return p; },
    _ToriRS_IO_DescribeAbiCount: () => abi.length,
    _ToriRS_IO_DescribeAbi(ptr) { for (let i = 0; i < abi.length; i++) HEAP32[(ptr >> 2) + i] = abi[i]; },
    UTF8ToString(p) { let e = p; while (HEAPU8[e]) e++; return Buffer.from(HEAPU8.slice(p, e)).toString('utf8'); },
    UTF8ArrayToString(h, p, len) { return Buffer.from(h.slice(p, p + len)).toString('utf8'); },
    Module: {},
    LibraryManager: { library: {} },
  };
  ctx.globalThis = ctx;
  ctx.mergeInto = (lib, obj) => Object.assign(lib, obj);
  vm.createContext(ctx);
  vm.runInContext(fs.readFileSync(SRC, 'utf8'), ctx, { filename: 'platform_web_io.js' });
  const L = ctx.LibraryManager.library;
  ctx.TORIRS_WEB_IO = L.$TORIRS_WEB_IO;
  ctx.TORIRS_WEB_IO.init();
  return { ctx, L, HEAPU8, HEAP32, errors, alloc: (n) => { const p = brk; brk += (n + 7) & ~7; return p; } };
}

const IO_PTR = 0x100;
const ACTIVE_BASE = 0x200;
const ITEMS_BASE = 0x1000;

function queueItem(rt, n, kind, name, data, size) {
  const item = ITEMS_BASE + n * ITEM.size;
  rt.HEAPU8.fill(0, item, item + ITEM.size);
  rt.HEAP32[(item + ITEM.kind) >> 2] = kind;
  Buffer.from(name, 'utf8').forEach((b, i) => { rt.HEAPU8[item + ITEM.u + NAME_OFF + i] = b; });
  rt.HEAP32[(item + ITEM.data) >> 2] = data || 0;
  rt.HEAP32[(item + ITEM.dataSize) >> 2] = size || 0;
  rt.HEAP32[(IO_PTR + IO.active) >> 2] = ACTIVE_BASE;
  rt.HEAP32[ACTIVE_BASE >> 2] = item;
  rt.HEAP32[(IO_PTR + IO.activeCount) >> 2] = 1;
  return item;
}

const field = (rt, item, off) => rt.HEAP32[(item + off) >> 2];
const settle = () => new Promise(r => setImmediate(r));

async function main() {
  let passed = 0;
  const ok = () => { passed++; };

  /* 0. An IOA3 layout is refused loudly, not misread. */
  {
    const rt = makeRuntime(abiFor(0x494f4133, false));
    assert.throws(() => rt.ctx.TORIRS_WEB_IO.layout(), /ABI has 20 fields/);
    ok();
  }

  /* 1. Reads. */
  {
    const rt = makeRuntime(abiFor(0x494f4134, true));
    const store = new Map([['cs2jit-1a2b3c4d-v1.pack', new Uint8Array([1, 2, 3, 4, 5])]]);
    const gates = [];
    rt.ctx.Module.torirsHostIO = {
      readDerived(name) {
        return new Promise(resolve => gates.push(() => resolve(store.has(name) ? store.get(name) : null)));
      },
      writeDerived() { return Promise.resolve(true); },
    };
    const px = rt.L.PlatformWeb_IO_New();
    rt.L.PlatformWeb_IO_InitCacheId(px, 0, 1, 0, 0, 0);

    let item = queueItem(rt, 0, K.DERIVED_READ, 'cs2jit-1a2b3c4d-v1.pack');
    const t0 = Date.now();
    rt.L.PlatformWeb_IO_Process(px, IO_PTR);
    assert(Date.now() - t0 < 50, 'Process does not wait');
    assert.strictEqual(field(rt, item, ITEM.pending), 1, 'pending while the host has not answered');
    assert.strictEqual(rt.HEAP32[(IO_PTR + IO.activeCount) >> 2], 0, 'batch consumed');
    await settle();
    assert.strictEqual(field(rt, item, ITEM.pending), 1, 'still pending: the host has not answered');
    gates.shift()();
    await settle();
    assert.strictEqual(field(rt, item, ITEM.pending), 0, 'answered');
    assert.strictEqual(field(rt, item, ITEM.error), 0, 'a record is error 0');
    assert.strictEqual(field(rt, item, ITEM.dataSize), 5);
    const at = field(rt, item, ITEM.data);
    assert.deepStrictEqual(Array.from(rt.HEAPU8.slice(at, at + 5)), [1, 2, 3, 4, 5], 'bytes in a payload block');
    ok();

    item = queueItem(rt, 1, K.DERIVED_READ, 'cs2jit-00000000-v1.pack');
    rt.L.PlatformWeb_IO_Process(px, IO_PTR);
    gates.shift()();
    await settle();
    assert.strictEqual(field(rt, item, ITEM.pending), 0);
    assert.strictEqual(field(rt, item, ITEM.error), -1, 'absent is -1');
    assert.strictEqual(field(rt, item, ITEM.data), 0, 'absent carries no data');
    ok();

    /* A name that is not one never reaches the host. */
    item = queueItem(rt, 2, K.DERIVED_READ, '../escape');
    rt.L.PlatformWeb_IO_Process(px, IO_PTR);
    await settle();
    assert.strictEqual(gates.length, 0, 'the host was not asked');
    assert.strictEqual(field(rt, item, ITEM.error), -1);
    ok();

    const d = rt.ctx.__torirs_io_telemetry.derived;
    assert.strictEqual(d.reads, 3);
    assert.strictEqual(d.readMisses, 2);
    assert.strictEqual(d.readBytes, 5);
    ok();
  }

  /* 2. A host without the functions: absent / failed, never an error. */
  {
    const rt = makeRuntime(abiFor(0x494f4134, true));
    rt.ctx.Module.torirsHostIO = {};
    const px = rt.L.PlatformWeb_IO_New();
    let item = queueItem(rt, 0, K.DERIVED_READ, 'x.pack');
    rt.L.PlatformWeb_IO_Process(px, IO_PTR);
    await settle();
    assert.strictEqual(field(rt, item, ITEM.error), -1);
    const buf = rt.alloc(8);
    item = queueItem(rt, 1, K.DERIVED_WRITE, 'x.pack', buf, 8);
    rt.L.PlatformWeb_IO_Process(px, IO_PTR);
    await settle();
    assert.strictEqual(field(rt, item, ITEM.error), -1);
    assert.strictEqual(rt.errors.length, 0, 'nothing reported as an executor error');
    ok();
  }

  /* 3. Writes: bytes copied before the await; commit / refusal answered. */
  {
    const rt = makeRuntime(abiFor(0x494f4134, true));
    const seen = [];
    let commit = true;
    const gates = [];
    rt.ctx.Module.torirsHostIO = {
      readDerived() { return Promise.resolve(null); },
      writeDerived(name, bytes) {
        return new Promise(resolve => gates.push(() => { seen.push({ name, bytes: Array.from(bytes) }); resolve(commit); }));
      },
    };
    const px = rt.L.PlatformWeb_IO_New();
    const buf = rt.alloc(16);
    for (let i = 0; i < 16; i++) { rt.HEAPU8[buf + i] = i + 1; }
    let item = queueItem(rt, 0, K.DERIVED_WRITE, 'cs2jit-1a2b3c4d-v1.pack', buf, 16);
    rt.L.PlatformWeb_IO_Process(px, IO_PTR);
    /* The task reuses its buffer the moment Process returns. */
    rt.HEAPU8.fill(0xEE, buf, buf + 16);
    assert.strictEqual(field(rt, item, ITEM.pending), 1);
    gates.shift()();
    await settle();
    assert.strictEqual(field(rt, item, ITEM.pending), 0);
    assert.strictEqual(field(rt, item, ITEM.error), 0, 'a committed write is 0');
    assert.strictEqual(field(rt, item, ITEM.data), buf, 'the lent pointer is left as it was');
    assert.deepStrictEqual(seen[0].bytes, Array.from({ length: 16 }, (_, i) => i + 1), 'the original bytes');
    assert.strictEqual(seen[0].name, 'cs2jit-1a2b3c4d-v1.pack');
    ok();

    commit = false;
    item = queueItem(rt, 1, K.DERIVED_WRITE, 'cs2jit-1a2b3c4d-v1.pack', buf, 16);
    rt.L.PlatformWeb_IO_Process(px, IO_PTR);
    gates.shift()();
    await settle();
    assert.strictEqual(field(rt, item, ITEM.error), -1, 'a refused write is -1');
    ok();

    const d = rt.ctx.__torirs_io_telemetry.derived;
    assert.strictEqual(d.writes, 2);
    assert.strictEqual(d.writeFailures, 1);
    assert.strictEqual(d.writeBytes, 16);
    ok();
  }

  console.log(`platform_web_io_derived_test: ${passed} ok`);
}

main().catch(err => { console.error(`FAIL: ${err.stack || err.message}`); process.exit(1); });
