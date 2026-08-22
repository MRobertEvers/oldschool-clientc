/*
 * torirs_channel.js — the renderer <-> command-panel channel, web binding.
 *
 * The native client funnels every external input through one serializable ring,
 * ToriRS_CmdBus: [u32 type][u16 length][payload] frames whose byte layout is
 * also the record-file format (src/cmd/cmdring.h). This is that wire, in the
 * browser, between two tabs. Same framing, same little-endian field order, so
 * a frame produced here can be handed straight to the wasm build's bus and a
 * recorded native session replays into a browser panel unchanged.
 *
 * WHAT THIS IS FOR. The world renderer is a frame loop that must never block;
 * the command panel is forms. On the web they can be two tabs -- a canvas tab
 * and a panel tab -- which buys the thing no in-page split can: the OS routes
 * the keyboard to whichever tab has focus, so typing a height into the panel
 * cannot fly the camera and WASD in the world cannot land in a text field.
 *
 * THE STATE RULE, restated here because the transport is where it gets broken:
 * the renderer owns everything -- document, undo stack, selection, current
 * tool. The panel holds widget state plus a seq-stamped mirror, and on any gap
 * it throws the mirror away and asks for a fresh snapshot. Nothing in this file
 * may make the renderer wait on a panel: intents move state ahead of time, and
 * a dead panel is a panel that stops sending, never a stalled frame.
 *
 *   Canvas tab                                  Panel tab
 *   ----------                                  ---------
 *   const host = ToriRSChannel.createHost({      const panel = ToriRSChannel.createPanel({
 *     onIntent: applyIntent,                       onFact: render
 *     buildSnapshot: snapshotState                });
 *   });                                          panel.sendIntent(
 *   host.openPanelTab('./panel.html');              ToriRSChannel.INTENT.SET,
 *   host.sendDelta(DELTA.HOVER, buf, true);         encodeTool('height'));
 *   host.flush();   // once per frame
 *
 * No build step, no imports: a plain script tag in both pages. It runs in the
 * page, not the wasm module, so a panel tab needs no second wasm instance.
 */
(global => {
  'use strict';

  /* ---- wire ------------------------------------------------------------- *
   *
   * One frame is an 6-byte header plus payload, matching cmdring.h's
   * #pragma pack(1) struct exactly. Several frames are concatenated into one
   * batch and posted as a single transferable, so a busy tick costs one
   * postMessage rather than one per frame.
   */

  const HEADER_BYTES = 6;
  /** cmdring.h's TORIRS_CMD_MAX_PAYLOAD. A frame over this is a caller bug. */
  const MAX_PAYLOAD = 8192;

  /** Panel -> renderer. The panel asks; it never asserts. */
  const INTENT = {
    /** Attach, or re-attach after a reload: answered with a full SNAPSHOT. */
    HELLO: 1,
    /** Set a piece of tool state ahead of the click that uses it. */
    SET: 2,
    /** One semantic editor operation: edit-at, select-at, undo, redo. */
    OP: 3,
    SAVE: 4,
    BAKE: 5,
    /** Detach cleanly, so the host can drop the mirror without a timeout. */
    BYE: 6
  };

  /** Renderer -> panel. The renderer states; it never asks. */
  const FACT = {
    /** Everything the panel mirrors, in one frame, stamped with its seq. */
    SNAPSHOT: 129,
    /** What changed since `seq - 1`. Applied in order or discarded. */
    DELTA: 130,
    ACK: 131,
    NACK: 132,
    /** Streamed save/bake output. */
    PROGRESS: 133
  };

  function writeFrame(type, payload) {
    const body = payload ? new Uint8Array(payload) : new Uint8Array(0);
    if (body.length > MAX_PAYLOAD) {
      throw new Error(`torirs_channel: payload ${body.length} exceeds ${MAX_PAYLOAD}`);
    }
    const out = new Uint8Array(HEADER_BYTES + body.length);
    const view = new DataView(out.buffer);
    /* Little-endian: the bus is memcpy'd into structs on the C side, and every
     * platform this ships to is little-endian. Stated rather than assumed --
     * a big-endian host would need a swap here, not a different protocol. */
    view.setUint32(0, type >>> 0, true);
    view.setUint16(4, body.length, true);
    out.set(body, HEADER_BYTES);
    return out;
  }

  /** Split a received batch back into frames. Truncation ends the walk. */
  function readFrames(buffer) {
    const bytes = new Uint8Array(buffer);
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const frames = [];
    let at = 0;
    while (at + HEADER_BYTES <= bytes.length) {
      const type = view.getUint32(at, true);
      const len = view.getUint16(at + 4, true);
      if (at + HEADER_BYTES + len > bytes.length) {
        break; /* a torn batch: keep what parsed, drop the tail */
      }
      frames.push({
        type,
        payload: bytes.subarray(at + HEADER_BYTES, at + HEADER_BYTES + len)
      });
      at += HEADER_BYTES + len;
    }
    return frames;
  }

  function concat(chunks) {
    let total = 0;
    for (const chunk of chunks) { total += chunk.length; }
    const out = new Uint8Array(total);
    let at = 0;
    for (const chunk of chunks) {
      out.set(chunk, at);
      at += chunk.length;
    }
    return out;
  }

  /* ---- endpoint --------------------------------------------------------- *
   *
   * The half both roles share: batching, the transferable post, origin checks,
   * and the coalescing rule. Roles differ only in which frame types they send
   * and what they do on receipt.
   */

  class Endpoint {
    constructor(opts) {
      this.origin = opts.origin || global.location.origin;
      this.target = opts.target || null;
      this.onFrame = opts.onFrame || (() => {});
      this.label = opts.label || 'channel';
      this.pending = [];
      /* Coalescible kinds keep only their newest frame in `pending`. The hover
       * readout changes on every tile crossing and only the last one matters;
       * without this a backgrounded tab (whose rAF is throttled to ~1Hz) builds
       * an unbounded queue of stale readouts. */
      this.coalesced = {};
      /*
       * Per-connection sequence, assigned at FLUSH rather than at queue time.
       *
       * This is the subtle half of coalescing. Stamping when a delta is queued
       * puts a hole in the sequence every time a later delta replaces an earlier
       * one -- and a hole is exactly what the panel's gap detector treats as a
       * dropped message, so it would throw its mirror away and resync on every
       * tick that coalesced anything. Numbering what is actually sent keeps the
       * stream dense, which is the property the detector needs.
       *
       * Per connection, not global: each attached tab has its own mirror and its
       * own snapshot baseline, so a tab that attaches late does not inherit a
       * sequence it never saw the start of.
       */
      this.seq = 0;
      this.closed = false;
      this.sent = 0;
      this.received = 0;
    }

    /**
     * Queue a frame.
     *
     * `stamped` frames get a sequence number at flush; unstamped ones (an
     * intent, a progress line) carry no ordering claim and so consume no number.
     */
    push(type, payload, coalesceKey, stamped) {
      if (this.closed) { return; }
      const entry = { type, payload, stamped: !!stamped };
      if (coalesceKey !== undefined && coalesceKey !== null) {
        const key = `${type}:${coalesceKey}`;
        if (this.coalesced[key] !== undefined) {
          this.pending[this.coalesced[key]] = entry; /* newest wins in place */
          return;
        }
        this.coalesced[key] = this.pending.length;
      }
      this.pending.push(entry);
    }

    /**
     * Post everything queued since the last flush as one transferable batch.
     *
     * Call once per tick, not per frame pushed. Returns the byte count sent, or
     * 0 when there was nothing to send -- a quiet tick costs no postMessage.
     */
    flush() {
      if (this.closed || !this.target || this.pending.length === 0) { return 0; }
      const frames = [];
      for (const e of this.pending) {
        frames.push(writeFrame(e.type, e.stamped ? withSeq(++this.seq, e.payload) : e.payload));
      }
      const batch = concat(frames);
      this.pending = [];
      this.coalesced = {};
      try {
        /* Transfer, not copy: the buffer is neutered here and adopted there. */
        this.target.postMessage(
          { type: 'torirs-frames', buffer: batch.buffer },
          this.origin,
          [batch.buffer]);
      } catch {
        /* The far side went away between the check and the post (tab closed
         * mid-tick). Not an error condition: the endpoint is simply detached,
         * and a HELLO from a fresh tab re-attaches it. */
        return 0;
      }
      this.sent += batch.length;
      return batch.length;
    }

    deliver(buffer) {
      const frames = readFrames(buffer);
      this.received += buffer.byteLength;
      for (const frame of frames) {
        this.onFrame(frame);
      }
    }

    close() {
      this.closed = true;
      this.pending = [];
      this.coalesced = {};
    }
  }

  /* ---- host (the canvas tab) -------------------------------------------- */

  /**
   * The renderer side: owns the state, answers HELLO with a snapshot, and can
   * drive any number of attached panel and canvas tabs.
   *
   * opts.onIntent(type, payload, conn) — apply a panel intent. Called
   *   synchronously from the message event; it must not block the frame loop.
   * opts.buildSnapshot() — return the bytes of a full SNAPSHOT payload. Called
   *   on every HELLO, including a panel tab's reload.
   */
  function createHost(opts = {}) {
    const origin = opts.origin || global.location.origin;
    const conns = [];

    function connFor(source) {
      return conns.find((conn) => conn.endpoint.target === source) || null;
    }

    function onMessage(ev) {
      if (ev.origin !== origin || !ev.data) { return; }
      const conn = connFor(ev.source);
      if (!conn) { return; }
      if (ev.data.type === 'torirs-ready') {
        conn.ready = true;
        if (opts.onReady) { opts.onReady(conn); }
        return;
      }
      if (ev.data.type === 'torirs-frames' && ev.data.buffer) {
        conn.endpoint.deliver(ev.data.buffer);
      }
    }
    global.addEventListener('message', onMessage);

    function attach(win, kind) {
      const conn = {
        kind,
        window: win,
        ready: false,
        endpoint: null
      };
      conn.endpoint = new Endpoint({
        origin,
        target: win,
        label: kind,
        onFrame: (frame) => {
          if (frame.type === INTENT.HELLO) {
            /* Resync from scratch. This is the only place a snapshot is
             * built, so a first attach and a panel reload take the identical
             * path -- there is no "reconnect" case to get wrong. */
            const snap = opts.buildSnapshot ? opts.buildSnapshot() : new Uint8Array(0);
            /* The snapshot re-bases this connection's stream: anything still
             * queued for it describes the state the snapshot just replaced. */
            conn.endpoint.pending = [];
            conn.endpoint.coalesced = {};
            conn.endpoint.push(FACT.SNAPSHOT, snap, null, true);
            conn.endpoint.flush();
            return;
          }
          if (frame.type === INTENT.BYE) {
            detach(conn);
            return;
          }
          if (opts.onIntent) { opts.onIntent(frame.type, frame.payload, conn); }
        }
      });
      conns.push(conn);
      return conn;
    }

    function detach(conn) {
      conn.endpoint.close();
      const at = conns.indexOf(conn);
      if (at >= 0) { conns.splice(at, 1); }
    }

    return {
      INTENT,
      FACT,

      /**
       * Open a command-panel tab and attach it.
       *
       * The panel announces itself with 'torirs-ready' and then sends HELLO;
       * nothing is pushed at it before that, so there is no retry loop here --
       * unlike a one-shot payload handoff, this channel's first move belongs
       * to the side that just finished loading.
       */
      openPanelTab(url) {
        const win = global.open(url || './panel.html', 'torirs-panel');
        if (!win) { return null; }
        return attach(win, 'panel');
      },

      /**
       * Open an additional canvas tab -- a second view of the world, attached
       * to the same channel as the panel.
       *
       * Each canvas tab is a separate page with its own wasm instance and its
       * own GL context (browsers cap live WebGL contexts per page, not per
       * tab, which is exactly why a second view is a second *tab* rather than
       * a second canvas in this one). It receives the same facts a panel does
       * and can send the same intents, so "the camera in view 2" is an intent
       * like any other rather than a second protocol.
       */
      openCanvasTab(url, name) {
        const win = global.open(url || './index.html', name || 'torirs-canvas-2');
        if (!win) { return null; }
        return attach(win, 'canvas');
      },

      /** Attach a window opened elsewhere (an iframe, a popup you own). */
      attach,
      detach,

      /** Every attached tab, for a caller that wants to fan out by hand. */
      connections: () => conns.slice(),

      /**
       * Queue a delta for every attached tab.
       *
       * `coalesceKey` marks the frame replaceable: a later delta with the same
       * key overwrites this one instead of queueing behind it. Pass it for
       * anything sampled continuously (hover readout, camera position); leave
       * it out for anything an operation produced (an ack, a dirty-square
       * list), where dropping one loses information.
       */
      sendDelta(payload, coalesceKey) {
        for (const conn of conns) {
          conn.endpoint.push(FACT.DELTA, payload, coalesceKey, true);
        }
      },

      send(factType, payload, coalesceKey) {
        for (const conn of conns) {
          conn.endpoint.push(factType, payload, coalesceKey);
        }
      },

      /** Post this tick's batch to every attached tab. Call once per frame. */
      flush() {
        let total = 0;
        for (const conn of conns) {
          total += conn.endpoint.flush();
        }
        return total;
      },

      /** The highest seq sent to `conn`, or to the first tab when omitted. */
      seq(conn) {
        const c = conn || conns[0];
        return c ? c.endpoint.seq : 0;
      },

      dispose() {
        global.removeEventListener('message', onMessage);
        while (conns.length) { detach(conns[0]); }
      }
    };
  }

  /** Prefix a payload with its u32 sequence number. */
  function withSeq(seq, payload) {
    const body = payload ? new Uint8Array(payload) : new Uint8Array(0);
    const out = new Uint8Array(4 + body.length);
    new DataView(out.buffer).setUint32(0, seq >>> 0, true);
    out.set(body, 4);
    return out;
  }

  /* ---- panel (the panel tab) -------------------------------------------- */

  /**
   * The panel side: mirrors, never owns.
   *
   * opts.onFact(type, payload, seq) — apply a fact. SNAPSHOT replaces the
   *   mirror wholesale; DELTA amends it.
   * opts.onDesync() — optional; called when a gap was detected and a fresh
   *   HELLO was sent. The mirror is stale until the snapshot lands.
   */
  function createPanel(opts = {}) {
    const origin = opts.origin || global.location.origin;
    const host = global.opener || global.parent;
    let lastSeq = 0;
    let synced = false;

    const endpoint = new Endpoint({
      origin,
      target: host,
      label: 'panel',
      onFrame: (frame) => {
        const seq = frame.payload.length >= 4
          ? new DataView(
              frame.payload.buffer,
              frame.payload.byteOffset,
              frame.payload.byteLength).getUint32(0, true)
          : 0;
        const body = frame.payload.subarray(Math.min(4, frame.payload.length));

        if (frame.type === FACT.SNAPSHOT) {
          lastSeq = seq;
          synced = true;
          if (opts.onFact) { opts.onFact(frame.type, body, seq); }
          return;
        }

        /* A gap means a delta was dropped, so every later delta is being
         * applied to a mirror that no longer matches. Throw it away and ask
         * again rather than carrying on with plausible-looking wrong state. */
        if (synced && seq !== lastSeq + 1) {
          synced = false;
          if (opts.onDesync) { opts.onDesync(lastSeq, seq); }
          sendIntent(INTENT.HELLO, null);
          endpoint.flush();
          return;
        }
        if (!synced) { return; } /* waiting on the snapshot; ignore deltas */
        lastSeq = seq;
        if (opts.onFact) { opts.onFact(frame.type, body, seq); }
      }
    });

    function onMessage(ev) {
      if (ev.origin !== origin || !ev.data) { return; }
      if (ev.data.type === 'torirs-frames' && ev.data.buffer) {
        endpoint.deliver(ev.data.buffer);
      }
    }
    global.addEventListener('message', onMessage);

    function sendIntent(type, payload) {
      endpoint.push(type, payload);
    }

    /* Announce, then ask. The host attaches on open but pushes nothing until
     * it hears from us, so this pair is what starts the conversation. */
    if (host) {
      try {
        host.postMessage({ type: 'torirs-ready' }, origin);
      } catch { /* opener gone; HELLO below will no-op too */ }
      sendIntent(INTENT.HELLO, null);
      endpoint.flush();
    }

    global.addEventListener('pagehide', () => {
      sendIntent(INTENT.BYE, null);
      endpoint.flush();
    });

    return {
      INTENT,
      FACT,
      sendIntent,
      /** Queue an intent and post immediately -- what a click handler wants. */
      send(type, payload) {
        sendIntent(type, payload);
        return endpoint.flush();
      },
      flush: () => endpoint.flush(),
      synced: () => synced,
      seq: () => lastSeq,
      dispose() {
        global.removeEventListener('message', onMessage);
        endpoint.close();
      }
    };
  }

  global.ToriRSChannel = {
    INTENT,
    FACT,
    HEADER_BYTES,
    MAX_PAYLOAD,
    createHost,
    createPanel,
    /* Exposed for tests and for callers hand-rolling a payload. */
    writeFrame,
    readFrames
  };
})(typeof window !== 'undefined' ? window : globalThis);
