/*
 * The JS5 cache producer, in JavaScript.
 *
 * JS5 is how a dat2 cache fills over the network: the client asks for a group
 * by (archive, group) and the server streams back the exact container an idx
 * record would have addressed. In this build the asking is done here rather
 * than in C, for the same reason the platform IO executor is JavaScript -- the
 * transport is a WebSocket, a WebSocket delivers between turns of the event
 * loop, and the language that can wait for that without freezing the tab is
 * this one.
 *
 * What that buys is not just style. The C client is a state machine with
 * explicit connect / handshake-write / handshake-read / request-write /
 * response-read states, partial-read and partial-write bookkeeping, and a
 * pending table to match responses to requests, because it must be able to
 * return to its caller at any point and resume later. Here `await` is that
 * mechanism, so the states become straight-line code and the pending table
 * becomes a Map of promise resolvers -- which is what it always was.
 *
 * ## The protocol, as implemented below
 *
 * Handshake, 21 bytes:      [15][revision:u32][seed0..3:u32]
 *   answered with one status byte; 0 is success. Anything else is a refusal to
 *   serve this revision, which is a configuration mistake rather than something
 *   to retry -- a client asking for a generation the server does not have will
 *   be refused identically forever.
 *
 * Request, 4 bytes:         [urgent ? 1 : 0][archive][group >> 8][group & 0xFF]
 *
 * Response header, 8 bytes: [archive][group:u16][compression][compressed:u32]
 *   then the payload, in 512-byte blocks. Every block after the first begins
 *   with a 0xFF marker byte that is NOT part of the payload -- the header
 *   occupies the first 8 bytes of the first block, so the first block carries
 *   504 payload bytes and each one after it carries 511.
 *
 * The container handed back is what the cache stores and what rscache decodes:
 * [compression][compressed:u32]( [uncompressed:u32] if compressed )[payload].
 * Bytes are never decoded here -- compressed and XTEA-encrypted groups are
 * passed through exactly as received, because validating them is the cache
 * format's job and doing it twice in two languages is how the two disagree.
 */

(function () {
  'use strict';

  const HANDSHAKE_BYTES = 21;
  const RESPONSE_HEADER_BYTES = 8;
  const NETWORK_BLOCK_BYTES = 512;
  const COMPRESSION_NONE = 0;
  const COMPRESSION_GZIP = 2;

  /** The master index: archive 255 addresses the reference tables, and 255/255
   *  is the index OF those tables. */
  const MASTER_ARCHIVE = 255;

  /*
   * A byte sink that reassembles JS5 responses.
   *
   * Kept separate from the socket because it is the one piece with real state:
   * a WebSocket message boundary has nothing to do with a block boundary or a
   * response boundary, so bytes must be consumable in whatever sizes arrive.
   */
  class ResponseReader {
    constructor(onComplete) {
      this.onComplete = onComplete;
      this.reset();
    }

    reset() {
      this.header = new Uint8Array(RESPONSE_HEADER_BYTES);
      this.headerSize = 0;
      this.data = null;
      this.position = 0;
      this.blockPosition = 0;
    }

    /** Begin a response once its 8-byte header is complete. */
    begin() {
      const archive = this.header[0];
      const group = (this.header[1] << 8) | this.header[2];
      const compression = this.header[3];
      const compressedSize =
        ((this.header[4] << 24) | (this.header[5] << 16) |
         (this.header[6] << 8) | this.header[7]) >>> 0;

      if (compression > COMPRESSION_GZIP) {
        throw new Error(`js5: unknown compression ${compression} for ${archive}/${group}`);
      }

      /* An uncompressed container carries only its compressed length; a
       * compressed one carries the uncompressed length as well. */
      const headerSize = compression === COMPRESSION_NONE ? 5 : 9;
      const total = headerSize + compressedSize;

      this.data = new Uint8Array(total);
      this.data[0] = compression;
      this.data.set(this.header.subarray(4, 8), 1);
      this.position = 5;
      /* The header consumed the first 8 bytes of this block. */
      this.blockPosition = RESPONSE_HEADER_BYTES;
      this.archive = archive;
      this.group = group;

      if (this.position === this.data.length) { this.finish(); }
    }

    finish() {
      const done = { archive: this.archive, group: this.group, bytes: this.data };
      this.reset();
      this.onComplete(done);
    }

    /** Consume one arrival, whatever size it happens to be. */
    push(chunk) {
      let at = 0;
      while (at < chunk.length) {
        if (!this.data) {
          const take = Math.min(RESPONSE_HEADER_BYTES - this.headerSize, chunk.length - at);
          this.header.set(chunk.subarray(at, at + take), this.headerSize);
          this.headerSize += take;
          at += take;
          if (this.headerSize === RESPONSE_HEADER_BYTES) {
            this.headerSize = 0;
            this.begin();
          }
          continue;
        }

        if (this.blockPosition === NETWORK_BLOCK_BYTES) {
          if (chunk[at++] !== 0xFF) { throw new Error('js5: missing block marker'); }
          this.blockPosition = 1;
          if (at === chunk.length) { continue; }
        }

        const take = Math.min(
          this.data.length - this.position,
          NETWORK_BLOCK_BYTES - this.blockPosition,
          chunk.length - at);
        this.data.set(chunk.subarray(at, at + take), this.position);
        this.position += take;
        this.blockPosition += take;
        at += take;
        if (this.position === this.data.length) { this.finish(); }
      }
    }
  }

  class Js5Client {
    constructor(host, port, revision) {
      this.host = host;
      this.port = port;
      this.revision = revision;
      this.socket = null;
      /* key -> {resolve, reject}. What the C client called its in-flight table;
       * here the continuation IS the promise. */
      this.pending = new Map();
      this.readyPromise = null;
      this.failed = null;
      this.bytesReceived = 0;
      this.groupsReceived = 0;

      this.reader = new ResponseReader(done => {
        const key = (done.archive << 16) | done.group;
        const waiter = this.pending.get(key);
        this.bytesReceived += done.bytes.length;
        this.groupsReceived++;
        if (waiter) {
          this.pending.delete(key);
          waiter.resolve(done.bytes);
        }
        /* A response nobody is waiting for is dropped rather than treated as a
         * protocol error: a request can be abandoned while its bytes are in
         * flight, and the server is not wrong to have finished sending. */
      });
    }

    /** Connected and past the handshake. Idempotent; every caller awaits the
     *  same attempt rather than opening a second socket. */
    ready() {
      return this.readyPromise || (this.readyPromise = this.#connect());
    }

    async #connect() {
      const url = `ws://${this.host}:${this.port}/`;
      const socket = new WebSocket(url);
      socket.binaryType = 'arraybuffer';
      this.socket = socket;

      await new Promise((resolve, reject) => {
        socket.onopen = resolve;
        socket.onerror = () => reject(this.#fail(`cannot reach the JS5 server at ${url}`));
      });

      /*
       * The handshake's reply is a single byte, and it arrives before any
       * response bytes. Read it with a one-shot handler, then hand the socket
       * to the reassembler -- rather than teaching the reassembler about a
       * state it sees exactly once.
       */
      const status = await new Promise((resolve, reject) => {
        socket.onmessage = ev => resolve(new Uint8Array(ev.data)[0]);
        socket.onclose = () => reject(this.#fail('the JS5 server closed during the handshake'));
        socket.send(this.#handshake());
      });

      if (status !== 0) {
        throw this.#fail(
          `the JS5 server refused revision ${this.revision} (status ${status}). ` +
          'That is a configuration mismatch, not a transient failure.');
      }

      socket.onmessage = ev => {
        try {
          this.reader.push(new Uint8Array(ev.data));
        } catch (err) {
          this.#fail(err.message);
        }
      };
      socket.onclose = () => this.#fail('the JS5 server closed the connection');
      socket.onerror = () => this.#fail('the JS5 connection failed');
      return this;
    }

    #handshake() {
      const out = new Uint8Array(HANDSHAKE_BYTES);
      const view = new DataView(out.buffer);
      out[0] = 15;
      view.setUint32(1, this.revision >>> 0);
      /* Seeds are zero: this client neither encrypts nor is asked to prove
       * anything by them, and the reference implementation sends whatever it
       * was seeded with. A server that checked would reject every client. */
      return out;
    }

    /*
     * Fail everything outstanding, once.
     *
     * A dead socket is one condition, not one per request: without this each
     * waiter would hang forever, and the task that queued the read would park
     * with nothing to say why.
     */
    #fail(message) {
      const err = new Error(`js5: ${message}`);
      err.torirsUnreachable = true;
      if (!this.failed) {
        this.failed = err;
        this.pending.forEach(w => w.reject(err));
        this.pending.clear();
      }
      return this.failed;
    }

    /**
     * One group's raw container.
     *
     * `urgent` is the protocol's own priority bit: a read the client is blocked
     * on jumps the background fill. Two callers asking for the same group share
     * one request and one answer, because the server would send the same bytes
     * twice and the second copy has nowhere to go.
     */
    async group(archive, group, urgent = true) {
      if (this.failed) { throw this.failed; }
      await this.ready();

      const key = (archive << 16) | group;
      const existing = this.pending.get(key);
      if (existing) { return await existing.promise; }

      let resolve, reject;
      const promise = new Promise((res, rej) => { resolve = res; reject = rej; });
      this.pending.set(key, { resolve, reject, promise });

      const request = new Uint8Array(4);
      request[0] = urgent ? 1 : 0;
      request[1] = archive & 0xFF;
      request[2] = (group >> 8) & 0xFF;
      request[3] = group & 0xFF;
      try {
        this.socket.send(request);
      } catch (err) {
        this.pending.delete(key);
        throw this.#fail(err.message);
      }
      return await promise;
    }

    /** The master index: the (CRC, version) of every reference table. */
    async masterIndex() {
      return await this.group(MASTER_ARCHIVE, MASTER_ARCHIVE);
    }

    /** One table's reference table. */
    async referenceTable(table) {
      return await this.group(MASTER_ARCHIVE, table);
    }

    stats() {
      return {
        groups: this.groupsReceived,
        bytes: this.bytesReceived,
        inflight: this.pending.size,
        failed: this.failed ? this.failed.message : null,
      };
    }
  }

  window.ToriRS_CreateJs5 = function (host, port, revision) {
    return new Js5Client(host, port, revision);
  };
})();
