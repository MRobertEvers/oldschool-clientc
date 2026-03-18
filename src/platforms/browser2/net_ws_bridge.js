/**
 * WebSocket bridge for the WASM game net API.
 * Uses LibToriRS_NetContext shared buffers: writes received data into P2G (platform-to-game),
 * drains G2P (game-to-platform) to the WebSocket, and sets connection state via LibToriRSPlatformJS_SetState.
 *
 * Expects Module.toriNetCtx (set by C in signal_browser_ready) to be the LibToriRS_NetContext* pointer.
 */

export const NET_IDLE = 0;
export const NET_CONNECTING = 1;
export const NET_CONNECTED = 2;
export const NET_ERROR = 3;

const NET_HEADER_SIZE = 8;

/* Outbound message types; must match tori_rs_net_shared.h (LIBTORIRS_NET_MSG_*) */
const MSG_DATA = 0;
const MSG_RECONNECT = 1;
const MSG_HEADER_SIZE = 3;

/**
 * @param {object} Module - Emscripten Module (HEAPU8, _LibToriRSPlatformJS_*, toriNetCtx)
 * @param {object} options
 * @param {string} [options.url='ws://localhost:43594'] - WebSocket server URL
 * @param {number} [options.netStatusIdle=0]
 * @param {number} [options.netStatusConnecting=1]
 * @param {number} [options.netStatusConnected=2]
 * @param {number} [options.netStatusError=3]
 */
export function setupNetWSBridge(Module, options = {}) {
  const url = options.url || 'ws://localhost:43594';
  const NET_IDLE_V = options.netStatusIdle ?? NET_IDLE;
  const NET_CONNECTING_V = options.netStatusConnecting ?? NET_CONNECTING;
  const NET_CONNECTED_V = options.netStatusConnected ?? NET_CONNECTED;
  const NET_ERROR_V = options.netStatusError ?? NET_ERROR;

  let ws = null;
  let ctxPtr = 0;
  let p2gPtr = 0;
  let p2gCap = 0;
  let g2pPtr = 0;
  let g2pCap = 0;
  let p2gPayloadCap = 0;
  let g2pPayloadCap = 0;

  function readRingBytes(heapU8, payloadStart, readPos, payloadCap, n) {
    const out = new Uint8Array(n);
    for (let i = 0; i < n; i++) {
      out[i] = heapU8[payloadStart + (readPos + i) % payloadCap];
    }
    return out;
  }

  function appendToP2G(bytes) {
    if (ctxPtr === 0 || p2gCap < NET_HEADER_SIZE + 1 || bytes.length === 0) return;
    const heapU8 = Module.HEAPU8;
    const payloadStart = p2gPtr + NET_HEADER_SIZE;
    let writePos = Module._LibToriRSPlatformJS_GetP2GWritePos(ctxPtr);
    const len = bytes.length;
    for (let i = 0; i < len; i++) {
      heapU8[payloadStart + (writePos + i) % p2gPayloadCap] = bytes[i];
    }
    writePos = (writePos + len) % p2gPayloadCap;
    Module._LibToriRSPlatformJS_SetP2GWritePos(ctxPtr, writePos);
  }

  function doReconnect() {
    if (ws) {
      ws.close();
      ws = null;
    }
    if (ctxPtr !== 0 && Module._LibToriRSPlatformJS_SetState) {
      Module._LibToriRSPlatformJS_SetState(ctxPtr, NET_CONNECTING_V);
    }
    try {
      ws = new WebSocket(url);
    } catch (e) {
      console.error('net_ws_bridge: WebSocket create failed', e);
      if (ctxPtr !== 0 && Module._LibToriRSPlatformJS_SetState) {
        Module._LibToriRSPlatformJS_SetState(ctxPtr, NET_ERROR_V);
      }
      return;
    }
    ws.binaryType = 'arraybuffer';
    ws.onopen = () => {
      if (ctxPtr !== 0 && Module._LibToriRSPlatformJS_SetState) {
        Module._LibToriRSPlatformJS_SetState(ctxPtr, NET_CONNECTED_V);
      }
    };
    ws.onmessage = (event) => {
      const data = event.data;
      if (!data) return;
      const bytes = data instanceof ArrayBuffer ? new Uint8Array(data) : new Uint8Array(data);
      if (bytes.length === 0) return;
      appendToP2G(bytes);
    };
    ws.onclose = () => {
      if (ctxPtr !== 0 && Module._LibToriRSPlatformJS_SetState) {
        Module._LibToriRSPlatformJS_SetState(ctxPtr, NET_IDLE_V);
      }
      ws = null;
    };
    ws.onerror = () => {
      if (ctxPtr !== 0 && Module._LibToriRSPlatformJS_SetState) {
        Module._LibToriRSPlatformJS_SetState(ctxPtr, NET_ERROR_V);
      }
    };
  }

  function drainOutbound() {
    if (ctxPtr === 0 || g2pPtr === 0 || g2pCap < NET_HEADER_SIZE + 1) return;
    const heapU8 = Module.HEAPU8;
    const payloadStart = g2pPtr + NET_HEADER_SIZE;
    let readPos = Module._LibToriRSPlatformJS_GetG2PReadPos(ctxPtr);
    const writePos = Module._LibToriRSPlatformJS_GetG2PWritePos(ctxPtr);

    while (true) {
      const used = (writePos - readPos + g2pPayloadCap) % g2pPayloadCap;
      if (used < MSG_HEADER_SIZE) break;

      const header = readRingBytes(heapU8, payloadStart, readPos, g2pPayloadCap, MSG_HEADER_SIZE);
      const type = header[0];
      const len = header[1] | (header[2] << 8);
      if (len < 0 || len > 65535) {
        readPos = (readPos + MSG_HEADER_SIZE) % g2pPayloadCap;
        Module._LibToriRSPlatformJS_SetG2PReadPos(ctxPtr, readPos);
        continue;
      }
      if (type === MSG_RECONNECT) {
        readPos = (readPos + MSG_HEADER_SIZE) % g2pPayloadCap;
        Module._LibToriRSPlatformJS_SetG2PReadPos(ctxPtr, readPos);
        doReconnect();
        continue;
      }
      if (type === MSG_DATA) {
        if (used < MSG_HEADER_SIZE + len) break;
        readPos = (readPos + MSG_HEADER_SIZE) % g2pPayloadCap;
        if (len > 0 && ws && ws.readyState === WebSocket.OPEN) {
          const payload = readRingBytes(heapU8, payloadStart, readPos, g2pPayloadCap, len);
          try {
            ws.send(payload.buffer);
          } catch (e) {
            console.warn('net_ws_bridge: send failed', e);
          }
        }
        readPos = (readPos + len) % g2pPayloadCap;
        Module._LibToriRSPlatformJS_SetG2PReadPos(ctxPtr, readPos);
        continue;
      }
      readPos = (readPos + MSG_HEADER_SIZE) % g2pPayloadCap;
      Module._LibToriRSPlatformJS_SetG2PReadPos(ctxPtr, readPos);
    }
  }

  function onRSClientReady(event) {
    ctxPtr = Module.toriNetCtx | 0;
    if (ctxPtr === 0) {
      console.warn('net_ws_bridge: toriNetCtx not set');
      return;
    }
    p2gPtr = Module._LibToriRSPlatformJS_GetP2GDataPtr(ctxPtr) | 0;
    p2gCap = Module._LibToriRSPlatformJS_GetP2GCapacity(ctxPtr) | 0;
    g2pPtr = Module._LibToriRSPlatformJS_GetG2PDataPtr(ctxPtr) | 0;
    g2pCap = Module._LibToriRSPlatformJS_GetG2PCapacity(ctxPtr) | 0;
    if (p2gPtr === 0 || p2gCap < NET_HEADER_SIZE + 1 || g2pPtr === 0 || g2pCap < NET_HEADER_SIZE + 1) {
      console.warn('net_ws_bridge: invalid context buffers');
      return;
    }
    p2gPayloadCap = p2gCap - NET_HEADER_SIZE;
    g2pPayloadCap = g2pCap - NET_HEADER_SIZE;
    doReconnect();
  }

  function onRSClientLooped() {
    drainOutbound();
  }

  window.addEventListener('rs_client_ready', onRSClientReady);
  window.addEventListener('rs_client_looped', onRSClientLooped);

  return function teardown() {
    window.removeEventListener('rs_client_ready', onRSClientReady);
    window.removeEventListener('rs_client_looped', onRSClientLooped);
    if (ws) {
      ws.close();
      ws = null;
    }
  };
}
