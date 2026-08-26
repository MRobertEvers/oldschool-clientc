/*
 * The browser's platform IO executor.
 *
 * ARCHITECTURE: [game -> IO Queue] :> [platform IO executor]. The game puts
 * items on the queue and the platform's executor executes them. On the desktop
 * that executor is platform_x_io.c, which has a filesystem and sockets. In a
 * browser it is THIS FILE -- there is no C shim in between, and there must not
 * be one: a C file that forwarded each call into JavaScript would be a second
 * executor in the seam, and the queue would be read twice, in two languages,
 * with two chances to disagree about what an item says.
 *
 * So this is an emscripten JS library (--js-library): the functions below ARE
 * the definitions of the PlatformWeb_IO_* symbols the client calls. C declares
 * them, the linker resolves them here, and nothing about the call site changes.
 *
 * ## Reading the queue
 *
 * The queue is a plain C struct in the wasm heap, so this reads it there. The
 * offsets are not written down here -- they are asked of the queue itself at
 * startup (ToriRS_IO_DescribeAbi, src/asyncio_abi.c), because a hand-copied
 * offset desyncs silently the first time somebody adds a field, and a silently
 * misread queue dispatches an item on a `kind` decoded out of the middle of a
 * path.
 *
 * ## Plain async/await, and what "pending" means
 *
 * A browser cannot read anything synchronously without freezing the tab, so
 * every real answer arrives after an await. This file therefore writes an
 * ORDINARY async function -- `execute` below awaits its host call the way any
 * other JavaScript would, with no callback bookkeeping and no hand-rolled
 * continuation table. There was a version of this that registered `.then()`
 * handlers against a table of parked entries; it did by hand exactly what the
 * language does for free, and it is not what the queue asks for either.
 *
 * What the queue asks for is only this: while an item has not been answered,
 * say so. Process kicks off the async loop and returns immediately, Pending
 * reports how many items for that queue are still in flight (which is what
 * stops the task runner resuming a task against an empty slot), and the loop
 * fills each slot in as its await resolves. The C side needs no ASYNCIFY --
 * a forbidden flag on this lane (platform_check.mk) -- because nothing in C is
 * ever suspended: C hands the work over and asks later whether it is done.
 */

mergeInto(LibraryManager.library, {
  // ---------------------------------------------------------------- state

  $TORIRS_WEB_IO__postset: 'TORIRS_WEB_IO.init();',
  $TORIRS_WEB_IO: {
    /* Filled from ToriRS_IO_DescribeAbi on first use. Never literals: see the
     * file comment. */
    abi: null,

    /* handle id -> executor instance. The client holds an opaque
     * `struct PlatformWeb_IO*` it never dereferences -- the type is declared
     * and never defined, precisely because the state is here -- so a small
     * integer is a legal handle and saves pretending to be a struct this file
     * does not have. Ids start at 1 so a handle is never a false-y pointer on
     * the C side. */
    instances: new Map(),
    nextHandle: 1,

    /*
     * How many items are still in flight, per queue pointer.
     *
     * A COUNT, not a table of parked entries. The async loop below already
     * knows which slot it is filling -- that is what a local variable in an
     * async function is -- so the only thing left for anyone else to ask is
     * "are you done?", and a number answers it. Per-io because the app runs
     * two pipelines over one executor and one being blocked must not stall the
     * other.
     */
    inflight: new Map(),

    addInflight: function (io, delta) {
      const now = (this.inflight.get(io) || 0) + delta;
      if (now > 0) { this.inflight.set(io, now); }
      else { this.inflight.delete(io); }
    },

    init: function () {
      /* Nothing to do until the module is running; the ABI is fetched lazily
       * because a postset runs before main() has necessarily set anything up.
       * Kept as a hook so the shape is obvious to the next person. */
    },

    /*
     * Ask the queue for its own layout, once.
     *
     * Refuses loudly on a mismatch rather than reading with what it has: every
     * value below is an offset into somebody else's memory, and being wrong
     * about one does not produce an error, it produces a wrong archive.
     */
    layout: function () {
      if (this.abi) { return this.abi; }

      const count = _ToriRS_IO_DescribeAbiCount();
      const EXPECTED_MAGIC = 0x494f4131; /* "IOA1", see asyncio_abi.c */
      const EXPECTED_COUNT = 21;

      if (count !== EXPECTED_COUNT) {
        throw new Error(
          `torirs: IO queue ABI has ${count} fields, this executor knows ` +
          `${EXPECTED_COUNT}. asyncio_abi.c and platform_web_io.js are out of step.`);
      }

      const ptr = _malloc(count * 4);
      if (!ptr) { throw new Error('torirs: out of memory reading the IO queue ABI'); }
      try {
        _ToriRS_IO_DescribeAbi(ptr);
        const v = new Int32Array(HEAP32.buffer, ptr, count).slice();
        if (v[0] !== EXPECTED_MAGIC) {
          throw new Error(
            `torirs: IO queue ABI magic ${v[0].toString(16)} != ` +
            `${EXPECTED_MAGIC.toString(16)}; the field order changed.`);
        }
        this.abi = {
          ioSize: v[1], slotsOff: v[2], activeOff: v[3], activeCountOff: v[4],
          maxItems: v[5],
          itemSize: v[6], kindOff: v[7], errorOff: v[8], dataOff: v[9],
          dataSizeOff: v[10], uOff: v[11],
          cacheEpochOff: v[12], cacheTableOff: v[13], cacheArchiveOff: v[14],
          cacheFlagsOff: v[15],
          configPathOff: v[16], scriptPathOff: v[17],
          refTableOff: v[18], filePathOff: v[19],
          maxPath: v[20],
        };
      } finally {
        _free(ptr);
      }
      return this.abi;
    },

    // ------------------------------------------------------- queue access

    itemPtr: function (io, slot) {
      const a = this.layout();
      return io + a.slotsOff + slot * a.itemSize;
    },

    /* The `kind` field is an enum, which is an int in this ABI. */
    itemKind: function (item) {
      return HEAP32[(item + this.layout().kindOff) >> 2];
    },

    /* A NUL-terminated path out of one of the union members. Bounded by the
     * queue's own TORIRS_IOITEM_MAX_PATH so a slot that somehow holds no
     * terminator cannot walk off into the rest of the heap. */
    itemPath: function (item, memberOff) {
      const a = this.layout();
      const at = item + a.uOff + memberOff;
      let end = at;
      const limit = at + a.maxPath;
      while (end < limit && HEAPU8[end] !== 0) { end++; }
      return UTF8ArrayToString(HEAPU8, at, end - at);
    },

    itemCache: function (item) {
      const a = this.layout();
      const u = item + a.uOff;
      return {
        epoch: HEAP32[(u + a.cacheEpochOff) >> 2],
        table: HEAP32[(u + a.cacheTableOff) >> 2],
        archive: HEAP32[(u + a.cacheArchiveOff) >> 2],
        flags: HEAP32[(u + a.cacheFlagsOff) >> 2],
      };
    },

    /*
     * Answer an item.
     *
     * `result` is one of three things, matching what the queue's kinds
     * actually carry:
     *
     *   null            the read failed -- error_code -1 and an empty payload,
     *                   the same answer the desktop gives for an absent file
     *   a Uint8Array    bytes, copied into a fresh _malloc the C side then owns
     *   {ptr, size}     something already IN wasm memory that C allocated -- a
     *                   decoded archive or reference table. Stored as-is: it is
     *                   a pointer to a struct, not a buffer to copy, and its
     *                   `size` is the struct's size because that is what the
     *                   platform has always written there.
     *
     * Either way the client owns what it receives and frees it as it always
     * has; nothing here is freed by the executor once it is placed.
     */
    answer: function (item, result) {
      const a = this.layout();
      if (!result) {
        HEAP32[(item + a.dataOff) >> 2] = 0;
        HEAP32[(item + a.dataSizeOff) >> 2] = 0;
        HEAP32[(item + a.errorOff) >> 2] = -1;
        return;
      }
      if (result.ptr !== undefined) {
        HEAP32[(item + a.dataOff) >> 2] = result.ptr;
        HEAP32[(item + a.dataSizeOff) >> 2] = result.size;
        HEAP32[(item + a.errorOff) >> 2] = 0;
        return;
      }
      const ptr = _malloc(result.length ? result.length : 1);
      if (!ptr) {
        /* Out of wasm memory is not a read failure and must not be reported as
         * one -- a caller told "no such file" would carry on with a plausible
         * empty result. */
        throw new Error(`torirs: out of memory answering a ${result.length} byte read`);
      }
      HEAPU8.set(result, ptr);
      HEAP32[(item + a.dataOff) >> 2] = ptr;
      HEAP32[(item + a.dataSizeOff) >> 2] = result.length;
      HEAP32[(item + a.errorOff) >> 2] = 0;
    },

    /*
     * A reference table's raw container, fetched once per table id.
     *
     * The BYTES are what is cached, never a decoded table. Every group in a
     * table needs its metadata, so re-fetching would be a download per model;
     * but a decoded table handed to the client becomes the client's to free
     * (the queue's consumer frees `data`), and a cache of pointers it had
     * already freed would be a use-after-free on the next request. Bytes have
     * no such problem: each decode below produces a fresh object with a single
     * owner.
     *
     * A table the cache does not ship is remembered as null rather than
     * retried, so a miss costs one fetch and not one per group.
     */
    refTableBytes: async function (inst, table) {
      if (inst.refTableBytes.has(table)) { return inst.refTableBytes.get(table); }
      const bytes = (await inst.host.readReferenceTable(table)) || null;
      inst.refTableBytes.set(table, bytes);
      return bytes;
    },

    /* One decode of that container. Every call returns a NEW table, owned by
     * whoever asked. */
    refTableDecode: async function (inst, table) {
      const bytes = await this.refTableBytes(inst, table);
      if (!bytes) { return 0; }

      const scratch = _malloc(bytes.length);
      if (!scratch) { throw new Error('torirs: out of memory for a reference table'); }
      try {
        HEAPU8.set(bytes, scratch);
        return _ToriRS_WebApi_ReferenceTableFromContainer(scratch, bytes.length, table);
      } finally {
        _free(scratch);
      }
    },

    /*
     * The executor's OWN copy, for attaching metadata to groups.
     *
     * Kept apart from the one handed out above precisely because the lifetimes
     * differ: this one belongs to the executor for as long as the queue lives
     * and is never given to anybody, which is what makes caching it safe.
     */
    refTableForMetadata: async function (inst, table) {
      if (inst.refTablesOwned.has(table)) { return inst.refTablesOwned.get(table); }
      const ptr = await this.refTableDecode(inst, table);
      inst.refTablesOwned.set(table, ptr || null);
      return ptr || null;
    },

    // ---------------------------------------------------------- execution

    /*
     * Everything the host needs to know about one item, read BEFORE any await.
     *
     * Read up front on purpose. Once this function awaits, the wasm heap may
     * have grown and moved -- every HEAPU8/HEAP32 view taken before the await
     * is detached afterwards -- so reading the request out of the queue late
     * is a use-after-move. The pointer arithmetic is re-done after the await
     * instead (itemPtr), which is cheap and always current.
     *
     * FILE_WRITE's payload is copied here for a second reason as well: the
     * queue only LENDS those bytes for the duration of the request, and the
     * task may reuse the buffer the moment Process returns.
     */
    describe: function (inst, item) {
      const a = this.layout();
      const kind = this.itemKind(item);
      const K = inst.kinds;

      if (kind === K.CONFIG_FILE) {
        return { kind: kind, path: inst.join(inst.configDir, this.itemPath(item, a.configPathOff)) };
      }
      if (kind === K.SCRIPT) {
        return { kind: kind, path: inst.join(inst.scriptDir, this.itemPath(item, a.scriptPathOff)) };
      }
      if (kind === K.FILE_READ) {
        return { kind: kind, path: this.itemPath(item, a.filePathOff) };
      }
      if (kind === K.FILE_WRITE) {
        const ptr = HEAP32[(item + a.dataOff) >> 2];
        const size = HEAP32[(item + a.dataSizeOff) >> 2];
        return {
          kind: kind,
          path: this.itemPath(item, a.filePathOff),
          bytes: HEAPU8.slice(ptr, ptr + size),
        };
      }
      if (kind === K.CACHE) {
        const c = this.itemCache(item);
        return { kind: kind, table: c.table, archive: c.archive, flags: c.flags, epoch: c.epoch };
      }
      if (kind === K.REFERENCE_TABLE) {
        return { kind: kind, table: HEAP32[(item + a.uOff + a.refTableOff) >> 2] };
      }
      return { kind: kind };
    },

    /*
     * Hand raw container bytes to the cache format's own decoder.
     *
     * The decode is C (platform_web_api.c -> 3rd/rscache) and deliberately so:
     * container framing, bzip2, gzip and XTEA have one implementation in this
     * tree and this is not the place to grow a second. A wrong decode does not
     * throw, it yields a plausible archive, so a JavaScript reimplementation
     * would be wrong in ways nothing downstream could catch.
     *
     * Bytes cross by copy into a scratch buffer rather than by view, because
     * the decoder owns and reallocates what it is given.
     */
    decodeArchive: function (bytes, table, archive, xteaKey) {
      const scratch = _malloc(bytes.length);
      if (!scratch) { throw new Error(`torirs: out of memory for a ${bytes.length} byte container`); }

      let keyPtr = 0;
      try {
        HEAPU8.set(bytes, scratch);
        if (xteaKey) {
          keyPtr = _malloc(16);
          if (!keyPtr) { throw new Error('torirs: out of memory for an XTEA key'); }
          for (let i = 0; i < 4; i++) { HEAP32[(keyPtr >> 2) + i] = xteaKey[i] | 0; }
        }
        return _ToriRS_WebApi_ArchiveDecode(scratch, bytes.length, table, archive, keyPtr);
      } finally {
        _free(scratch);
        if (keyPtr) { _free(keyPtr); }
      }
    },

    /*
     * Execute one item. An ordinary async function: it awaits the host and
     * returns the bytes, or null when there are none.
     *
     * The host interface is deliberately small -- read a file, read or write
     * one of the player's own -- because that is the whole of what a platform
     * executor does. Everything else in this file is queue mechanics.
     */
    execute: async function (inst, req) {
      const K = inst.kinds;

      if (req.kind === K.CONFIG_FILE || req.kind === K.SCRIPT) {
        return await inst.host.readFile(req.path);
      }
      if (req.kind === K.FILE_READ) {
        /* The player's own file, and it never leaves this browser -- see
         * host.readClientFile. */
        return await inst.host.readClientFile(req.path);
      }
      if (req.kind === K.FILE_WRITE) {
        await inst.host.writeClientFile(req.path, req.bytes);
        return null;
      }

      if (req.kind === K.CACHE) {
        /*
         * Two awaits, and the order matters only in that both must finish
         * before the decode: the group's bytes, and the key the cache profile
         * says this table needs (null for everything but encrypted maps -- the
         * host decides, because that is a property of the profile and not of
         * these bytes).
         */
        const [bytes, xtea] = await Promise.all([
          inst.host.readArchive(req.table, req.archive, req.flags),
          inst.host.xteaKey(req.table, req.archive),
        ]);
        if (!bytes) { return null; }

        const ptr = this.decodeArchive(bytes, req.table, req.archive, xtea);
        if (!ptr) { return null; }

        /* Metadata is a separate fetch, and a group whose table has no entry
         * for it is a missing archive rather than a failure -- the same
         * judgement the desktop makes. The executor's own table, never the
         * client's: see refTableForMetadata. */
        const table = await this.refTableForMetadata(inst, req.table);
        if (table) { _ToriRS_WebApi_ArchiveApplyMetadata(ptr, table); }

        return { ptr: ptr, size: _ToriRS_WebApi_ArchiveStructSize() };
      }

      if (req.kind === K.REFERENCE_TABLE) {
        /* A FRESH decode, because this one is handed over and freed by whoever
         * asked for it. */
        const table = await this.refTableDecode(inst, req.table);
        if (!table) { return null; }
        return { ptr: table, size: _ToriRS_WebApi_ReferenceTableStructSize() };
      }

      /*
       * Loud rather than a failed read: an item answered "not found" would
       * surface far away as a blank model or a missing config, and the thing
       * that went wrong -- an executor handed a kind it does not know -- would
       * not appear anywhere in the report.
       */
      throw new Error(`torirs: the web IO executor cannot execute kind ${req.kind}`);
    },

    /*
     * Run one item to completion and fill its slot in.
     *
     * Kicked off by Process and never awaited by it -- that is what makes the
     * item outstanding rather than blocking the frame. The count is adjusted
     * around the whole thing so Pending is accurate from the instant Process
     * returns to the instant the slot is filled.
     */
    run: async function (inst, io, slot) {
      const req = this.describe(inst, this.itemPtr(io, slot));
      this.addInflight(io, 1);
      try {
        const bytes = await this.execute(inst, req);
        /* itemPtr recomputed AFTER the await: see describe. */
        this.answer(this.itemPtr(io, slot), bytes);
      } catch (err) {
        /* A transport that answered nothing is a different fact from a file
         * that is not there, and only the first is an outage. */
        if (err && err.torirsUnreachable) { inst.transportDown = true; }
        else { err && console.error(`torirs io: ${err.message}`); }
        this.answer(this.itemPtr(io, slot), null);
      } finally {
        this.addInflight(io, -1);
      }
    },
  },

  // ------------------------------------------------------ PlatformWeb_IO_*

  PlatformWeb_IO_New__deps: ['$TORIRS_WEB_IO'],
  PlatformWeb_IO_New: function () {
    const S = TORIRS_WEB_IO;
    const handle = S.nextHandle++;
    S.instances.set(handle, {
      handle: handle,
      configDir: '',
      scriptDir: '',
      transportDown: false,
      /* Raw reference-table containers, and the executor's own decoded copies.
       * Two maps because the two have different owners -- see refTableBytes. */
      refTableBytes: new Map(),
      refTablesOwned: new Map(),
      /* Mirrors of enum ToriRS_IOKind. Not read from the ABI: the ABI
       * describes the LAYOUT, and these are values -- appending a kind does
       * not move a field, so the two change for different reasons. */
      kinds: {
        NONE: 0, CACHE: 1, CONFIG_FILE: 2, SCRIPT: 3,
        REFERENCE_TABLE: 4, FILE_READ: 5, FILE_WRITE: 6,
      },
      join: function (base, path) {
        return base && base.length ? `${base}/${path}` : path;
      },
      /* The page supplies the actual IO. Everything above is queue mechanics;
       * this is where bytes come from, and it is deliberately the only part a
       * different host would have to replace. */
      host: Module.torirsHostIO,
    });
    return handle;
  },

  PlatformWeb_IO_Free__deps: ['$TORIRS_WEB_IO'],
  PlatformWeb_IO_Free: function (px) {
    if (!px) { return; }
    TORIRS_WEB_IO.instances.delete(px);
  },

  /* The browser has no cache directory and no disk handle to be given. These
   * exist so the client's boot reads the same on every platform; a browser
   * simply has nothing to record. */
  PlatformWeb_IO_InitDat2Disk: function (px, disk) {},
  PlatformWeb_IO_InitDat1Disk: function (px, disk) {},
  PlatformWeb_IO_InitCacheId: function (px, epoch, game, revision, quirks, dir) {},

  PlatformWeb_IO_InitConfigPath__deps: ['$TORIRS_WEB_IO'],
  PlatformWeb_IO_InitConfigPath: function (px, path) {
    TORIRS_WEB_IO.instances.get(px).configDir = UTF8ToString(path);
  },

  PlatformWeb_IO_InitScriptPath__deps: ['$TORIRS_WEB_IO'],
  PlatformWeb_IO_InitScriptPath: function (px, path) {
    TORIRS_WEB_IO.instances.get(px).scriptDir = UTF8ToString(path);
  },

  /*
   * There is no synchronous read on this platform, so there is nothing this
   * can honestly do.
   *
   * It exists because the queue's interface has it and the desktop needs it --
   * a caller that has an answer already may take it without going round the
   * event loop. Here every answer is an await, so refusing is the truthful
   * reply and Process is the only way in.
   */
  PlatformWeb_IO_LoadItem: function (px, item) {
    return -1;
  },

  /*
   * Hand this pass's items to the executor and return.
   *
   * Deliberately NOT async and deliberately not awaited: Process is called
   * from the frame loop, and a Process that waited for its reads would be the
   * frozen tab this whole design exists to avoid. Each item runs on its own,
   * and Pending is how the caller learns when one is finished.
   */
  PlatformWeb_IO_Process__deps: ['$TORIRS_WEB_IO'],
  PlatformWeb_IO_Process: function (px, io) {
    const S = TORIRS_WEB_IO;
    const a = S.layout();
    const inst = S.instances.get(px);
    const activeCount = HEAP32[(io + a.activeCountOff) >> 2];

    for (let i = 0; i < activeCount; i++) {
      S.run(inst, io, HEAP32[(io + a.activeOff + i * 4) >> 2]);
    }

    /* ToriRS_IO_ResetActive, done here because the queue expects Process to
     * have consumed the active list by the time it returns. The items
     * themselves stay outstanding -- the active list is what is new THIS pass,
     * not what is unanswered. */
    HEAPU8.fill(0, io + a.activeOff, io + a.activeOff + activeCount * 4);
    HEAP32[(io + a.activeCountOff) >> 2] = 0;

    return activeCount;
  },

  PlatformWeb_IO_Pending__deps: ['$TORIRS_WEB_IO'],
  PlatformWeb_IO_Pending: function (px, io) {
    return TORIRS_WEB_IO.inflight.get(io) || 0;
  },

  /*
   * Everything outstanding, across every queue.
   *
   * The frame loop uses this for PACING (platform_web_host.h): while reads are
   * in flight it runs from the event loop rather than requestAnimationFrame.
   * Answered here because what is outstanding is exactly what this executor is
   * still awaiting -- nothing else in the process knows.
   */
  PlatformWeb_PendingTotal__deps: ['$TORIRS_WEB_IO'],
  PlatformWeb_PendingTotal: function () {
    let total = 0;
    TORIRS_WEB_IO.inflight.forEach(n => { total += n; });
    return total;
  },

  PlatformWeb_IO_ServerReachable__deps: ['$TORIRS_WEB_IO'],
  PlatformWeb_IO_ServerReachable: function (px) {
    const inst = TORIRS_WEB_IO.instances.get(px);
    return inst && inst.transportDown ? 0 : 1;
  },
});
