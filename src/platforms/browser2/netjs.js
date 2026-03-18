const TORI_MSG = { CONNECT: 1, SEND_DATA: 2, CONN_STATUS: 3, RECV_DATA: 4 };
const TORI_STATUS = { DISCONNECTED: 0, CONNECTING: 1, CONNECTED: 2, FAILED: 3 };
const BUF_SIZE = 65536;

export class LibToriRSPlatformJS {
  constructor(wasmModule, gamePtr) {
    this.mod = wasmModule;
    this.sbPtr = this.mod._LibToriRS_GetBuffer(gamePtr);
    this.socket = null;

    // Map WASM memory offsets
    this.off = {
      g2pData: this.sbPtr + this.mod._Export_NetGetOffset_G2P_Data(),
      g2pHead: this.sbPtr + this.mod._Export_NetGetOffset_G2P_Head(),
      g2pTail: this.sbPtr + this.mod._Export_NetGetOffset_G2P_Tail(),
      p2gData: this.sbPtr + this.mod._Export_NetGetOffset_P2G_Data(),
      p2gHead: this.sbPtr + this.mod._Export_NetGetOffset_P2G_Head(),
      p2gTail: this.sbPtr + this.mod._Export_NetGetOffset_P2G_Tail(),
      status: this.sbPtr + this.mod._Export_NetGetOffset_Status(),
    };
  }

  // --- Manual Byte Accessors (Little Endian) ---

  _readU32(ptr) {
    const b = this.mod.HEAPU8;
    return (
      (b[ptr] | (b[ptr + 1] << 8) | (b[ptr + 2] << 16) | (b[ptr + 3] << 24)) >>>
      0
    );
  }

  _readU16(ptr) {
    const b = this.mod.HEAPU8;
    return (b[ptr] | (b[ptr + 1] << 8)) >>> 0;
  }

  _writeU32(ptr, val) {
    this.mod.HEAPU8[ptr] = val & 0xff;
    this.mod.HEAPU8[ptr + 1] = (val >> 8) & 0xff;
    this.mod.HEAPU8[ptr + 2] = (val >> 16) & 0xff;
    this.mod.HEAPU8[ptr + 3] = (val >> 24) & 0xff;
  }

  _writeU16(ptr, val) {
    this.mod.HEAPU8[ptr] = val & 0xff;
    this.mod.HEAPU8[ptr + 1] = (val >> 8) & 0xff;
  }

  tick() {
    let tail = this._readU32(this.off.g2pTail);
    const head = this._readU32(this.off.g2pHead);

    while (tail !== head) {
      const msgPtr = this.off.g2pData + tail;

      // Header is 4 bytes (type) + 2 bytes (length)
      const type = this._readU32(msgPtr);
      const len = this._readU16(msgPtr + 4);

      if (type === TORI_MSG.CONNECT) {
        const host = this._readString(tail + 6, len);
        this._connect(host);
      } else if (type === TORI_MSG.SEND_DATA) {
        this._sendToSocket(tail + 6, len);
      }

      tail = (tail + 6 + len) % BUF_SIZE;
      this._writeU32(this.off.g2pTail, tail);
    }
  }

  _connect(host) {
    if (this.socket) this.socket.close();

    console.log(`[JS Platform] Connecting to ${host}...`);
    this._writeU32(this.off.status, TORI_STATUS.CONNECTING);

    this.socket = new WebSocket(`ws://${host}`);
    this.socket.binaryType = "arraybuffer";

    this.socket.onopen = () => {
      this._writeU32(this.off.status, TORI_STATUS.CONNECTED);
      this._pushToGame(TORI_MSG.CONN_STATUS);
    };

    this.socket.onmessage = (e) => {
      this._pushToGame(TORI_MSG.RECV_DATA, new Uint8Array(e.data));
    };

    this.socket.onclose = () => {
      this._writeU32(this.off.status, TORI_STATUS.DISCONNECTED);
      this._pushToGame(TORI_MSG.CONN_STATUS);
    };
  }

  _pushToGame(type, data = null) {
    const len = data ? data.length : 0;
    let head = this._readU32(this.off.p2gHead);

    // Write Header into Platform-to-Game data buffer
    const headerPtr = this.off.p2gData + head;
    this._writeU32(headerPtr, type);
    this._writeU16(headerPtr + 4, len);

    // Write Body (Handling Ring Wrap-around)
    if (data) {
      for (let i = 0; i < len; i++) {
        const writeIdx = (head + 6 + i) % BUF_SIZE;
        this.mod.HEAPU8[this.off.p2gData + writeIdx] = data[i];
      }
    }

    // Update the P2G Head pointer in shared memory
    this._writeU32(this.off.p2gHead, (head + 6 + len) % BUF_SIZE);
  }

  _sendToSocket(offset, len) {
    if (!this.socket || this.socket.readyState !== WebSocket.OPEN) return;

    const data = new Uint8Array(len);
    for (let i = 0; i < len; i++) {
      const readIdx = (offset + i) % BUF_SIZE;
      data[i] = this.mod.HEAPU8[this.off.g2pData + readIdx];
    }
    this.socket.send(data);
  }

  _readString(offset, len) {
    let str = "";
    for (let i = 0; i < len; i++) {
      const readIdx = (offset + i) % BUF_SIZE;
      str += String.fromCharCode(this.mod.HEAPU8[this.off.g2pData + readIdx]);
    }
    return str;
  }
}
