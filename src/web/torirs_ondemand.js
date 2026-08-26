/*
 * The dat1 on-demand cache producer, in JavaScript.
 *
 * The 2004 counterpart of JS5: a LostCity-era server keeps the dat1 cache and
 * hands out one archive at a time over the game port. Same reasoning as
 * torirs_js5.js for why this is JavaScript -- the transport is a WebSocket in a
 * browser, and `await` is what lets a client wait for one without freezing the
 * tab.
 *
 * ## The protocol, as implemented below
 *
 * Handshake:  send one byte, 15. The server replies with 8 bytes -- the same
 *   unused seed a login handshake gets -- and the connection is out of login
 *   negotiation for good. It is then a file pipe and stays open, because the
 *   handshake costs a round trip and the server keys its request queues on the
 *   connection.
 *
 * Request, 4 bytes:  [archive][file >> 8][file & 0xFF][priority]
 *   Priority 2 is the urgent queue, which is the only one this uses: every read
 *   here is one the client is already waiting on.
 *
 * Response, in chunks with a 6-byte header each:
 *   [archive][file:u16][total:u16][part]
 *   followed by up to 500 payload bytes. `part` says where the payload belongs
 *   (part * 500), and `total` is the whole archive's length, repeated in every
 *   chunk. A `total` of zero means the server does not have it -- a legitimate
 *   answer at the edges of a built world, not a failure.
 *
 * One request is in flight at a time. That is the protocol's own constraint
 * rather than a simplification: chunks carry no request id beyond the
 * (archive, file) they name, so two overlapping requests would be
 * indistinguishable if they ever interleaved. Callers are serialised on a
 * promise chain below.
 */

(function () {
  'use strict';

  const CHUNK_PAYLOAD = 500;
  const CHUNK_HEADER_BYTES = 6;
  const PRIORITY_URGENT = 2;
  const HELLO = 15;

  /*
   * A WebSocket as a byte stream you can await a fixed number of bytes from.
   *
   * The protocol is framed by counts, not by messages, so this has to be able
   * to serve a 6-byte header out of the middle of whatever arrived. Everything
   * else here reads as if it were a socket.
   */
  class ByteStream {
    constructor(socket) {
      this.socket = socket;
      this.chunks = [];
      this.available = 0;
      this.waiter = null;
      this.error = null;

      socket.onmessage = ev => {
        this.chunks.push(new Uint8Array(ev.data));
        this.available += this.chunks[this.chunks.length - 1].length;
        this.#serve();
      };
      socket.onclose = () => this.fail('the on-demand server closed the connection');
      socket.onerror = () => this.fail('the on-demand connection failed');
    }

    fail(message) {
      const err = new Error(`ondemand: ${message}`);
      err.torirsUnreachable = true;
      if (!this.error) { this.error = err; }
      if (this.waiter) {
        const w = this.waiter;
        this.waiter = null;
        w.reject(this.error);
      }
      return this.error;
    }

    #serve() {
      if (!this.waiter || this.available < this.waiter.want) { return; }
      const want = this.waiter.want;
      const out = new Uint8Array(want);
      let filled = 0;
      while (filled < want) {
        const head = this.chunks[0];
        const take = Math.min(head.length, want - filled);
        out.set(head.subarray(0, take), filled);
        filled += take;
        if (take === head.length) { this.chunks.shift(); }
        else { this.chunks[0] = head.subarray(take); }
      }
      this.available -= want;
      const w = this.waiter;
      this.waiter = null;
      w.resolve(out);
    }

    read(want) {
      if (this.error) { return Promise.reject(this.error); }
      return new Promise((resolve, reject) => {
        this.waiter = { want, resolve, reject };
        this.#serve();
      });
    }

    send(bytes) {
      if (this.error) { throw this.error; }
      this.socket.send(bytes);
    }
  }

  class OnDemandClient {
    constructor(host, port) {
      this.host = host;
      this.port = port;
      this.stream = null;
      this.readyPromise = null;
      /* One request at a time: see the file comment. Each call chains onto the
       * last, so the socket only ever has one (archive, file) outstanding. */
      this.tail = Promise.resolve();
      this.bytesReceived = 0;
      this.filesReceived = 0;
    }

    ready() {
      return this.readyPromise || (this.readyPromise = this.#connect());
    }

    async #connect() {
      const url = `ws://${this.host}:${this.port}/`;
      const socket = new WebSocket(url);
      socket.binaryType = 'arraybuffer';

      await new Promise((resolve, reject) => {
        socket.onopen = resolve;
        socket.onerror = () => {
          const err = new Error(`ondemand: cannot reach the server at ${url}`);
          err.torirsUnreachable = true;
          reject(err);
        };
      });

      this.stream = new ByteStream(socket);
      this.stream.send(new Uint8Array([HELLO]));
      await this.stream.read(8);
      return this;
    }

    /**
     * One archive's bytes, or null when the server does not have it.
     *
     * Null is an ANSWER here, not a failure: a built world has holes, and the
     * caller distinguishes it from an outage the same way everything else in
     * this lane does -- by whether an error was thrown.
     */
    async file(archive, file) {
      const run = async () => {
        await this.ready();
        const request = new Uint8Array(4);
        request[0] = archive & 0xFF;
        request[1] = (file >> 8) & 0xFF;
        request[2] = file & 0xFF;
        request[3] = PRIORITY_URGENT;
        this.stream.send(request);

        let data = null;
        let total = -1;
        let received = 0;

        for (;;) {
          const header = await this.stream.read(CHUNK_HEADER_BYTES);
          const gotArchive = header[0];
          const gotFile = (header[1] << 8) | header[2];
          const chunkTotal = (header[3] << 8) | header[4];
          const part = header[5];

          /* Anything else on this socket means the two ends have lost sync, and
           * every later read would be misframed. */
          if (gotArchive !== archive || gotFile !== file) {
            throw this.stream.fail(
              `expected ${archive}/${file}, server sent ${gotArchive}/${gotFile}`);
          }
          if (chunkTotal === 0) { return null; }

          if (total < 0) { total = chunkTotal; data = new Uint8Array(total); }
          else if (chunkTotal !== total) {
            throw this.stream.fail(`chunk length ${chunkTotal} != ${total}`);
          }

          const offset = part * CHUNK_PAYLOAD;
          if (offset < 0 || offset >= total) {
            throw this.stream.fail(`chunk part ${part} outside a ${total} byte archive`);
          }
          const count = Math.min(total - offset, CHUNK_PAYLOAD);
          data.set(await this.stream.read(count), offset);
          received += count;
          if (received >= total) { break; }
        }

        this.bytesReceived += total;
        this.filesReceived++;
        return data;
      };

      /* Chain, and keep the chain alive across a failure: a rejected link must
       * not stop later requests from being attempted. */
      const result = this.tail.then(run, run);
      this.tail = result.catch(() => {});
      return await result;
    }

    stats() {
      return {
        files: this.filesReceived,
        bytes: this.bytesReceived,
        failed: this.stream && this.stream.error ? this.stream.error.message : null,
      };
    }
  }

  window.ToriRS_CreateOnDemand = function (host, port) {
    return new OnDemandClient(host, port);
  };
})();
