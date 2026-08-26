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
 * the definitions of the PlatformX_IO_* symbols the client calls. C declares
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
     * `struct PlatformX_IO*` it never dereferences (the type is incomplete
     * outside platform_x_io.c), so a small integer is a legal handle here and
     * saves pretending to be a struct this file does not have. Ids start at 1
     * so a handle is never a false-y pointer to the C side. */
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
     * Answer an item: bytes, or a failure.
     *
     * `bytes` null means the read failed, which the queue spells as
     * error_code -1 and an empty payload -- the same answer the desktop gives
     * for a file that is not there. The buffer handed over is a fresh _malloc:
     * the C side owns it from here and frees it through IOITEM_FREE_DATA.
     */
    answer: function (item, bytes) {
      const a = this.layout();
      if (!bytes) {
        HEAP32[(item + a.dataOff) >> 2] = 0;
        HEAP32[(item + a.dataSizeOff) >> 2] = 0;
        HEAP32[(item + a.errorOff) >> 2] = -1;
        return;
      }
      const ptr = _malloc(bytes.length ? bytes.length : 1);
      if (!ptr) {
        /* Out of wasm memory is not a read failure and must not be reported as
         * one -- a caller told "no such file" would carry on with a plausible
         * empty result. */
        throw new Error(`torirs: out of memory answering a ${bytes.length} byte read`);
      }
      HEAPU8.set(bytes, ptr);
      HEAP32[(item + a.dataOff) >> 2] = ptr;
      HEAP32[(item + a.dataSizeOff) >> 2] = bytes.length;
      HEAP32[(item + a.errorOff) >> 2] = 0;
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
      return { kind: kind };
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
      /*
       * Not executable by this executor yet.
       *
       * Loud rather than a failed read: a cache item answered "not found"
       * would surface far away as a blank model or a missing config, and the
       * thing that went wrong -- an executor that cannot do its job -- would
       * not appear anywhere in the report.
       */
      throw new Error(
        `torirs: the web IO executor cannot execute kind ${req.kind} yet ` +
        `(cache reads still to move across the seam)`);
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

  // ------------------------------------------------------ PlatformX_IO_*

  PlatformX_IO_New__deps: ['$TORIRS_WEB_IO'],
  PlatformX_IO_New: function () {
    const S = TORIRS_WEB_IO;
    const handle = S.nextHandle++;
    S.instances.set(handle, {
      handle: handle,
      configDir: '',
      scriptDir: '',
      transportDown: false,
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

  PlatformX_IO_Free__deps: ['$TORIRS_WEB_IO'],
  PlatformX_IO_Free: function (px) {
    if (!px) { return; }
    TORIRS_WEB_IO.instances.delete(px);
  },

  /* The browser has no cache directory and no disk handle to be given. These
   * exist so the client's boot reads the same on every platform; a browser
   * simply has nothing to record. */
  PlatformX_IO_InitDat2Disk: function (px, disk) {},
  PlatformX_IO_InitDat1Disk: function (px, disk) {},
  PlatformX_IO_InitCacheId: function (px, epoch, game, revision, quirks, dir) {},

  PlatformX_IO_InitConfigPath__deps: ['$TORIRS_WEB_IO'],
  PlatformX_IO_InitConfigPath: function (px, path) {
    TORIRS_WEB_IO.instances.get(px).configDir = UTF8ToString(path);
  },

  PlatformX_IO_InitScriptPath__deps: ['$TORIRS_WEB_IO'],
  PlatformX_IO_InitScriptPath: function (px, path) {
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
  PlatformX_IO_LoadItem: function (px, item) {
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
  PlatformX_IO_Process__deps: ['$TORIRS_WEB_IO'],
  PlatformX_IO_Process: function (px, io) {
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

  PlatformX_IO_Pending__deps: ['$TORIRS_WEB_IO'],
  PlatformX_IO_Pending: function (px, io) {
    return TORIRS_WEB_IO.inflight.get(io) || 0;
  },

  PlatformX_IO_ServerReachable__deps: ['$TORIRS_WEB_IO'],
  PlatformX_IO_ServerReachable: function (px) {
    const inst = TORIRS_WEB_IO.instances.get(px);
    return inst && inst.transportDown ? 0 : 1;
  },
});
