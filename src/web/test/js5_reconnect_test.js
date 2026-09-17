'use strict';
/*
 * The JS5 client survives a dropped socket.
 *
 * A proxy or server that closes an idle WebSocket (the public reverse proxy
 * did, at 120 s) must not take the cache producer down with it: the next
 * request reconnects, and a request that was in flight when the socket went
 * is re-sent on the new one. Only a refused handshake or an unreachable
 * server is a failure.
 *
 * Runs under plain node (22+, for the global WebSocket): the server is a
 * hand-rolled RFC 6455 endpoint on a loopback port, so nothing is installed.
 *
 *   node src/web/test/js5_reconnect_test.js
 */
const net = require('net');
const crypto = require('crypto');
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const assert = require('assert');

const GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';

/* A JS5-over-WebSocket server with two policies under test: it closes a
 * connection that has been quiet for `idleMs`, and it hangs up instead of
 * answering the first request for `dropOnGroup`. */
function fakeServer({ idleMs, dropOnGroup, handshakeStatus = 0 }) {
  const state = { connections: 0, dropped: false, server: null, port: 0 };
  state.server = net.createServer(socket => {
    state.connections++;
    let buffered = Buffer.alloc(0);
    let upgraded = false;
    let shook = false;
    let idle = null;
    const bump = () => {
      if (idle) clearTimeout(idle);
      if (idleMs) idle = setTimeout(() => socket.end(), idleMs);
    };
    const send = payload => {
      const head = payload.length < 126
        ? Buffer.from([0x82, payload.length])
        : Buffer.from([0x82, 126, payload.length >> 8, payload.length & 0xff]);
      socket.write(Buffer.concat([head, Buffer.from(payload)]));
    };
    const onMessage = payload => {
      bump();
      if (!shook) {
        assert.strictEqual(payload.length, 21, 'handshake is 21 bytes');
        assert.strictEqual(payload[0], 15);
        shook = true;
        send(Buffer.from([handshakeStatus]));
        return;
      }
      assert.strictEqual(payload.length, 4, 'a request is 4 bytes');
      const archive = payload[1];
      const group = (payload[2] << 8) | payload[3];
      if (group === dropOnGroup && !state.dropped) {
        state.dropped = true;
        socket.destroy();
        return;
      }
      /* [archive][group:u16][compression 0][size:u32] then the payload. The
       * master index (255/255) lists tables 0..2 as present, eight bytes
       * each, non-zero; every other group answers with four marker bytes. */
      const body = archive === 255 && group === 255
        ? Buffer.alloc(24, 1)
        : Buffer.from([archive, group >> 8, group & 0xff, 0xa5]);
      const header = Buffer.from([archive, (group >> 8) & 0xff, group & 0xff, 0, 0, 0, 0, body.length]);
      send(Buffer.concat([header, body]));
    };
    socket.on('data', chunk => {
      buffered = Buffer.concat([buffered, chunk]);
      if (!upgraded) {
        const end = buffered.indexOf('\r\n\r\n');
        if (end < 0) return;
        const request = buffered.subarray(0, end).toString();
        buffered = buffered.subarray(end + 4);
        const key = /Sec-WebSocket-Key: (.+)\r\n/i.exec(request)[1].trim();
        const accept = crypto.createHash('sha1').update(key + GUID).digest('base64');
        socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n' +
          `Connection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
        upgraded = true;
        bump();
      }
      for (;;) {
        if (buffered.length < 2) return;
        const opcode = buffered[0] & 0x0f;
        let len = buffered[1] & 0x7f;
        let at = 2;
        if (len === 126) { len = buffered.readUInt16BE(2); at = 4; }
        const masked = (buffered[1] & 0x80) !== 0;
        const need = at + (masked ? 4 : 0) + len;
        if (buffered.length < need) return;
        let payload = buffered.subarray(at + (masked ? 4 : 0), need);
        if (masked) {
          const mask = buffered.subarray(at, at + 4);
          payload = Buffer.from(payload.map((b, i) => b ^ mask[i % 4]));
        }
        buffered = buffered.subarray(need);
        if (opcode === 8) { socket.end(); return; }
        if (opcode === 2) onMessage(payload);
      }
    });
    socket.on('error', () => {});
  });
  return new Promise(resolve => state.server.listen(0, '127.0.0.1', () => {
    state.port = state.server.address().port;
    resolve(state);
  }));
}

function loadClient() {
  const file = path.join(__dirname, '..', 'torirs_js5.js');
  const context = { window: {}, WebSocket, console, setTimeout, clearTimeout };
  context.window = context;
  vm.runInNewContext(fs.readFileSync(file, 'utf8'), context, { filename: file });
  return context.window.ToriRS_CreateJs5;
}

const sleep = ms => new Promise(r => setTimeout(r, ms));

async function main() {
  const create = loadClient();
  let passed = 0;

  /* 1. Idle drop, then a request: reconnects, answers, no failure. */
  {
    const server = await fakeServer({ idleMs: 150, dropOnGroup: 7 });
    const js5 = create('127.0.0.1', server.port, 239, `ws://127.0.0.1:${server.port}/`);
    const first = await js5.group(2, 1);
    assert.deepStrictEqual(Array.from(first), [0, 0, 0, 0, 4, 2, 0, 1, 0xa5], 'container = [compression][size][payload]');
    assert.strictEqual(server.connections, 1);
    await sleep(400);
    assert.strictEqual(js5.stats().drops, 1, 'the idle close was noticed');
    assert.strictEqual(js5.stats().failed, null, 'and is not a failure');
    const second = await js5.group(2, 2);
    assert.strictEqual(second[8], 0xa5);
    assert.strictEqual(server.connections, 2, 'the request reconnected');
    passed++;

    /* 2. Drop with the request in flight: re-sent on the new socket. */
    const third = await js5.group(2, 7);
    assert.strictEqual(third[7], 7, 'the in-flight request was answered after the reconnect');
    assert.strictEqual(server.connections, 3);
    assert.strictEqual(js5.stats().drops, 2);
    assert.strictEqual(js5.stats().inflight, 0);
    assert.strictEqual(js5.stats().failed, null);
    passed++;

    /* 3. Two requests share the drop: both answered, neither duplicated. */
    server.dropped = false;
    const [a, b] = await Promise.all([js5.group(2, 7), js5.group(2, 8)]);
    assert.strictEqual(a[7], 7);
    assert.strictEqual(b[7], 8);
    assert.strictEqual(server.connections, 4);
    passed++;

    /* 4. The server goes away for good: a request fails, and stays failed. */
    server.server.close();
    await sleep(400);
    let err = null;
    try { await js5.group(2, 3); } catch (e) { err = e; }
    assert.ok(err && err.torirsUnreachable, 'an unreachable server is an outage');
    assert.ok(js5.stats().failed, 'and the client stays failed');
    passed++;
  }

  /* 5. A refused handshake is a configuration failure, never a reconnect. */
  {
    const server = await fakeServer({ idleMs: 0, handshakeStatus: 6 });
    const js5 = create('127.0.0.1', server.port, 239, `ws://127.0.0.1:${server.port}/`);
    let err = null;
    try { await js5.group(2, 1); } catch (e) { err = e; }
    assert.ok(err && /refused revision 239/.test(err.message), err && err.message);
    assert.strictEqual(js5.stats().drops, 0);
    server.server.close();
    passed++;
  }

  console.log(`js5_reconnect_test: ${passed} passed`);
  /* The last fake server still holds its client's socket open. */
  process.exit(0);
}

main().catch(err => { console.error(err); process.exit(1); });
