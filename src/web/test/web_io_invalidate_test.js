'use strict';
/*
 * PlatformWeb_IO_InvalidateArchive -- the web IO executor's half of a live
 * reload (src/platform/platform_web_io.js), evaluated as shipped under node
 * with the emscripten runtime and the C exports it calls stubbed.
 *
 *   - the decoded-archive LRU entry for (index, archive) is released, the
 *     resident container dropped and the index's reference-table memo
 *     cleared (its owned decode retired, never freed); every other entry
 *     stays;
 *   - the host is asked to re-prime the DISK table with a reference decoder
 *     and, under TORIRS_CACHE_INVALIDATE_DEBUG, a log; the executor prints
 *     the native client's io line;
 *   - a read already out when the reload lands keeps nothing it read and
 *     reads again: the LRU and the resident store end on the new bytes;
 *   - referenceListing reads (id -> crc, version) out of the rscache decode,
 *     unsigned, and frees the table;
 *   - an unknown handle is a loud error, not a no-op.
 *
 *   node src/web/test/web_io_invalidate_test.js
 */
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const FILE = path.join(__dirname, '..', '..', 'platform', 'platform_web_io.js');

function makeRuntime() {
  const heap = new ArrayBuffer(1 << 20);
  let brk = 1024;
  const calls = { free: [], decoded: new Map(), refFree: [], log: [], error: [] };
  let nextPtr = 100000;
  const ctx = {
    HEAPU8: new Uint8Array(heap),
    HEAP32: new Int32Array(heap),
    ENV: { TORIRS_CACHE_INVALIDATE_DEBUG: '1' },
    performance: { now: () => Date.now() },
    console: {
      log: line => calls.log.push(line),
      error: line => calls.error.push(line),
      warn: () => {},
    },
    Promise,
    Map,
    Array,
    Error,
    _malloc: n => { const at = brk; brk += (n + 7) & ~7; return at; },
    _free: () => {},
    _ToriRS_WebApi_Dat2TableDiskId: (game, logical) => logical,
    _ToriRS_WebApi_XteaKey: () => 0,
    _ToriRS_WebApi_ArchiveDecode: (scratch, size) => {
      const ptr = nextPtr++;
      calls.decoded.set(ptr, Buffer.from(ctx.HEAPU8.slice(scratch, scratch + size)).toString('latin1'));
      return ptr;
    },
    _ToriRS_WebApi_ArchiveApplyMetadata: () => 1,
    _ToriRS_WebApi_ArchiveRetain: () => {},
    _ToriRS_WebApi_ArchiveFree: ptr => calls.free.push(ptr),
    _ToriRS_WebApi_ArchiveDataSize: () => 16,
    _ToriRS_WebApi_ArchiveStructSize: () => 64,
    _ToriRS_WebApi_ReferenceTableFromContainer: (scratch, size) => {
      const tag = Buffer.from(ctx.HEAPU8.slice(scratch, scratch + size)).toString('latin1');
      return tag === 'undecodable' ? 0 : 7000 + size;
    },
    _ToriRS_WebApi_ReferenceTableHasArchive: (table, id) => (id === 0 || id === 2 || id === 6 || id === 9 ? 1 : 0),
    _ToriRS_WebApi_ReferenceTableArchiveCount: () => 10,
    _ToriRS_WebApi_ReferenceTableArchiveCrc: (table, id) => (id === 6 ? -2 : id * 11),
    _ToriRS_WebApi_ReferenceTableArchiveVersion: (table, id) => id + 1,
    _ToriRS_WebApi_ReferenceTableFree: ptr => calls.refFree.push(ptr),
    Module: {},
  };
  const lib = {};
  ctx.mergeInto = (target, obj) => Object.assign(lib, obj);
  ctx.LibraryManager = { library: {} };
  vm.createContext(ctx);
  vm.runInContext(fs.readFileSync(FILE, 'utf8'), ctx, { filename: FILE });
  ctx.TORIRS_WEB_IO = lib.$TORIRS_WEB_IO;
  ctx.TORIRS_WEB_IO.init();
  return { ctx, lib, calls };
}

function makeHost() {
  const host = {
    reprimes: [],
    reads: [],
    archives: [],    /* queued answers for readArchive, in order */
    async readReferenceTable() { return new Uint8Array([1, 2, 3]); },
    readArchive(table, archive) {
      host.reads.push(`${table}/${archive}`);
      const next = host.archives.shift();
      return next instanceof Promise ? next : Promise.resolve(next);
    },
    async xteaKey() { return null; },
    invalidateArchive(table, archive, opts) {
      host.reprimes.push({ table, archive, opts });
      return Promise.resolve({ refetched: [] });
    },
  };
  return host;
}

const bytes = s => new Uint8Array(Buffer.from(s, 'latin1'));
const tick = () => new Promise(r => setImmediate(r));

async function main() {
  let passed = 0;

  /* 1. What is dropped, what stays, what the host is asked, what is logged. */
  {
    const { ctx, lib, calls } = makeRuntime();
    const host = makeHost();
    ctx.Module.torirsHostIO = host;
    const px = lib.PlatformWeb_IO_New();
    lib.PlatformWeb_IO_InitCacheId(px, 0, 7, 241, 0, 0);
    const S = ctx.TORIRS_WEB_IO;
    const inst = S.instances.get(px);
    inst.decoded.set('0|2|6', { ptr: 11, bytes: 100 });
    inst.decoded.set('0|2|9', { ptr: 12, bytes: 100 });
    inst.decoded.set('0|12|6', { ptr: 13, bytes: 100 });
    inst.decoded.set('1|2|6', { ptr: 14, bytes: 100 });
    inst.decodedBytes = 400;
    inst.resident.set('0|2|6', bytes('old6'));
    inst.resident.set('0|2|9', bytes('old9'));
    inst.residentBytes = 8;
    inst.refTablesReady.set(2, 500);
    inst.refTablesOwned.set(2, Promise.resolve(500));
    inst.refTableBytes.set(2, Promise.resolve(bytes('ref')));
    inst.refTablesReady.set(3, 600);

    lib.PlatformWeb_IO_InvalidateArchive(px, 2, 6);

    assert.deepStrictEqual(Array.from(inst.decoded.keys()), ['0|2|9', '0|12|6', '1|2|6'],
      'only the dat2 copy of 2/6 leaves the LRU');
    assert.deepStrictEqual(calls.free, [11], "and the LRU's reference is released");
    assert.strictEqual(inst.decodedBytes, 300);
    assert.deepStrictEqual(Array.from(inst.resident.keys()), ['0|2|9']);
    assert.strictEqual(inst.residentBytes, 4);
    assert.ok(!inst.refTablesReady.has(2) && !inst.refTablesOwned.has(2) && !inst.refTableBytes.has(2),
      "the index's reference-table memo is cleared");
    assert.strictEqual(inst.refTablesReady.get(3), 600, "another table's is not");
    assert.deepStrictEqual(Array.from(inst.retiredRefTables), [500], 'the owned table is retired, not freed');
    assert.strictEqual(host.reprimes.length, 1);
    assert.strictEqual(host.reprimes[0].table, 2);
    assert.strictEqual(host.reprimes[0].archive, 6);
    assert.strictEqual(typeof host.reprimes[0].opts.decodeReference, 'function');
    assert.strictEqual(typeof host.reprimes[0].opts.log, 'function', 'debug on: the host logs its fetches');
    assert.deepStrictEqual(calls.log, [
      "cache invalidate: io 2/6: 1 decompressed copy dropped; js5 re-prime of 255/255 and the index's reference table queued",
    ]);
    passed++;

    /* Debug off: no line, no log handed down. */
    ctx.ENV.TORIRS_CACHE_INVALIDATE_DEBUG = undefined;
    delete ctx.ENV.TORIRS_CACHE_INVALIDATE_DEBUG;
    calls.log.length = 0;
    lib.PlatformWeb_IO_InvalidateArchive(px, 2, 9);
    assert.deepStrictEqual(calls.log, []);
    assert.strictEqual(host.reprimes[1].opts.log, null);
    assert.ok(!inst.decoded.has('0|2|9'));
    passed++;

    /* An unknown handle, or an index no cache has, is loud. */
    assert.throws(() => lib.PlatformWeb_IO_InvalidateArchive(px + 99, 2, 6), /unknown IO handle/);
    assert.throws(() => lib.PlatformWeb_IO_InvalidateArchive(px, 255, 6), /index 255/);
    passed++;
  }

  /* 2. A read out across the reload keeps nothing it read and reads again. */
  {
    const { ctx, lib } = makeRuntime();
    const host = makeHost();
    ctx.Module.torirsHostIO = host;
    const px = lib.PlatformWeb_IO_New();
    lib.PlatformWeb_IO_InitCacheId(px, 0, 7, 241, 0, 0);
    const S = ctx.TORIRS_WEB_IO;
    const inst = S.instances.get(px);

    let land;
    host.archives.push(new Promise(r => { land = r; }));
    host.archives.push(bytes('new6'));
    const read = S.execute(inst, { kind: inst.kinds.CACHE, table: 2, archive: 6, flags: 0, epoch: 0 });
    await tick();
    assert.deepStrictEqual(host.reads, ['2/6'], 'the first read is out');
    lib.PlatformWeb_IO_InvalidateArchive(px, 2, 6);
    land(bytes('old6'));
    const answer = await read;
    assert.deepStrictEqual(host.reads, ['2/6', '2/6'], 'read again after the reload');
    assert.strictEqual(S.decodedArchive(inst, 0, 2, 6), answer.ptr, 'the LRU holds the answer');
    assert.strictEqual(Buffer.from(inst.resident.get('0|2|6')).toString('latin1'), 'new6',
      'the resident store holds the new bytes, never the old');
    passed++;
  }

  /* 3. referenceListing: unsigned, holes skipped, the table freed. */
  {
    const { ctx, calls } = makeRuntime();
    const S = ctx.TORIRS_WEB_IO;
    const listing = S.referenceListing(2, bytes('abcd'));
    assert.deepStrictEqual(Array.from(listing.keys()), [0, 2, 6, 9]);
    assert.deepStrictEqual({ ...listing.get(6) }, { crc: 0xFFFFFFFE, version: 7 });
    assert.deepStrictEqual({ ...listing.get(9) }, { crc: 99, version: 10 });
    assert.deepStrictEqual(calls.refFree, [7004]);
    assert.strictEqual(S.referenceListing(2, bytes('undecodable')), null, 'an undecodable table is null');
    passed++;
  }

  console.log(`web_io_invalidate_test: ${passed} passed`);
}

main().catch(err => { console.error(err); process.exit(1); });
