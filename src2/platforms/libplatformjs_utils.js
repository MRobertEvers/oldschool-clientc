export function fromWasmCString(wasm, ptr) {
  if (!ptr) return null;
  const heap = wasm.HEAPU8;
  const memory = heap?.buffer ?? wasm.memory?.buffer;
  let end = ptr;
  while (heap[end] !== 0) end++;
  if (end === ptr) return "";
  return new TextDecoder().decode(new Uint8Array(memory, ptr, end - ptr));
}

export function fromWASMStringToJSString(wasm, strPtr, strLen) {
  const heap = wasm.HEAPU8;
  const memory = heap?.buffer ?? wasm.memory?.buffer;
  if (strPtr === 0) return null;

  const view = new Uint8Array(memory, strPtr, strLen ?? 0);
  const jsStr = new TextDecoder().decode(view);

  return jsStr;
}

export function wasmExportFn(mod, baseName) {
  let fn = null;
  const underscored = mod["_" + baseName];
  if (typeof underscored === "function") {
    fn = underscored;
  }
  const plain = mod[baseName];
  if (typeof plain === "function") {
    fn = plain;
  }
  if (!fn) {
    throw new Error("Missing WASM export: " + baseName);
  }

  return fn;
}

export function fromLuaStringToJSString(str) {
  if (str instanceof Uint8Array) {
    str = new TextDecoder().decode(str);
  }
  return str;
}

/** Flush MEMTRACE output from MEMFS and trigger a browser download. */
export function downloadMemtrace(wasmModule) {
  if (!wasmModule?.FS) {
    throw new Error("downloadMemtrace requires Emscripten FS (MEMTRACE build)");
  }
  const flush = wasmExportFn(wasmModule, "LibToriPlatformEmscripten_JSHost_MemtraceFlush");
  const pathFn = wasmExportFn(wasmModule, "LibToriPlatformEmscripten_JSHost_MemtracePath");
  flush();
  const path = fromWasmCString(wasmModule, pathFn()) || "memtrace.bin";
  const data = wasmModule.FS.readFile(path);
  const blob = new Blob([data], { type: "application/octet-stream" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = path.split("/").pop() || "memtrace.bin";
  a.click();
  URL.revokeObjectURL(url);
}

const MEMTRACE_MAGIC = 0x544d5254; /* "TRMT" little-endian */
const MEMTRACE_FOOTER_MAGIC = 0x534d5254; /* "TRMS" */
const MEMTRACE_RECORD_SIZE = 324;
const MEMTRACE_KIND_NAMES = {
  1: "malloc",
  2: "calloc",
  3: "realloc",
  4: "free",
  5: "posix_memalign",
  6: "strdup",
};

function readU32(dv, off) {
  return dv.getUint32(off, true);
}

function readU64(dv, off) {
  const lo = dv.getUint32(off, true);
  const hi = dv.getUint32(off + 4, true);
  return hi * 0x100000000 + lo;
}

function readU16(dv, off) {
  return dv.getUint16(off, true);
}

function readU8(dv, off) {
  return dv.getUint8(off);
}

function parseWasmStackFooter(bytes, footerOff) {
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const magic = readU32(dv, footerOff);
  if (magic !== MEMTRACE_FOOTER_MAGIC) {
    return { stacks: [""], footerOff: bytes.length };
  }
  let off = footerOff + 4;
  const count = readU32(dv, off);
  off += 4;
  const stacks = [""];
  const dec = new TextDecoder();
  for (let i = 0; i < count; i++) {
    const len = readU32(dv, off);
    off += 4;
    const text = dec.decode(bytes.subarray(off, off + len));
    off += len;
    stacks.push(text);
  }
  return { stacks, footerOff };
}

/**
 * Decode WASM memtrace.bin bytes into viewer-ready { events, meta }.
 * @param {Uint8Array} bytes
 */
export function decodeMemtraceWasm(bytes) {
  if (!(bytes instanceof Uint8Array)) {
    bytes = new Uint8Array(bytes);
  }
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (bytes.length < 20) {
    throw new Error("memtrace file too small");
  }
  const magic = readU32(dv, 0);
  if (magic !== MEMTRACE_MAGIC) {
    throw new Error("invalid memtrace magic");
  }
  const version = readU32(dv, 4);
  const platform = readU32(dv, 8);
  const modCount = readU32(dv, 12);
  let off = 20;
  for (let i = 0; i < modCount; i++) {
    if (off + 20 > bytes.length) {
      throw new Error("truncated module map");
    }
    const pathLen = readU32(dv, off + 16);
    off += 20 + pathLen;
  }

  const footerSearch = new Uint8Array([0x54, 0x52, 0x4d, 0x53]); /* TRMS */
  let footerOff = -1;
  for (let i = bytes.length - 4; i >= off; i--) {
    if (
      bytes[i] === footerSearch[0] &&
      bytes[i + 1] === footerSearch[1] &&
      bytes[i + 2] === footerSearch[2] &&
      bytes[i + 3] === footerSearch[3]
    ) {
      footerOff = i;
      break;
    }
  }

  const { stacks, footerOff: eventsEnd } =
    footerOff >= 0
      ? parseWasmStackFooter(bytes, footerOff)
      : { stacks: [""], footerOff: bytes.length };

  const events = [];
  const live = new Map();
  let peakLive = 0;

  for (let recOff = off; recOff + MEMTRACE_RECORD_SIZE <= eventsEnd; recOff += MEMTRACE_RECORD_SIZE) {
    const kind = readU8(dv, recOff);
    const stackCount = readU8(dv, recOff + 1);
    const tsNs = readU64(dv, recOff + 4);
    const threadId = readU64(dv, recOff + 12);
    const reqSize = readU64(dv, recOff + 20);
    const usableSize = readU64(dv, recOff + 28);
    const ptr = readU64(dv, recOff + 36);
    const oldPtr = readU64(dv, recOff + 44);
    const liveBytes = readU64(dv, recOff + 52);
    const heapTotal = readU64(dv, recOff + 60);

    let stack = [];
    if (stackCount >= 1) {
      const stackId = readU64(dv, recOff + 68);
      if (stackId > 0 && stackId < stacks.length) {
        stack = stacks[stackId].split("\n").filter(Boolean);
      }
    }

    const kindName = MEMTRACE_KIND_NAMES[kind] || `kind_${kind}`;
    const ptrHex = ptr ? "0x" + ptr.toString(16) : null;
    const oldPtrHex = oldPtr ? "0x" + oldPtr.toString(16) : null;

    if (kindName !== "free" && ptr) {
      live.set(ptrHex, Number(usableSize || reqSize));
    } else if (kindName === "free" && ptrHex) {
      live.delete(ptrHex);
    }
    peakLive = Math.max(peakLive, Number(liveBytes));

    events.push({
      t: Number(tsNs),
      thread: Number(threadId),
      kind: kindName,
      req: Number(reqSize),
      usable: Number(usableSize),
      ptr: ptrHex,
      old_ptr: oldPtrHex,
      live: Number(liveBytes),
      heap: Number(heapTotal),
      stack,
    });
  }

  const topLeaks = [...live.entries()]
    .map(([ptr, sz]) => ({ ptr, bytes: sz }))
    .sort((a, b) => b.bytes - a.bytes)
    .slice(0, 64);

  const stillLiveBytes = [...live.values()].reduce((s, n) => s + n, 0);

  const meta = {
    platform: platform === 2 ? "wasm" : platform === 1 ? "macos" : "linux",
    version,
    event_count: events.length,
    peak_live_bytes: peakLive,
    still_live_count: live.size,
    still_live_bytes: stillLiveBytes,
    top_leaks: topLeaks,
  };

  return { events, meta };
}

function memtraceExportsAvailable(wasmModule) {
  if (!wasmModule?.FS) {
    return false;
  }
  const names = [
    "LibToriPlatformEmscripten_JSHost_MemtraceFlush",
    "LibToriPlatformEmscripten_JSHost_MemtracePath",
  ];
  for (const name of names) {
    if (
      typeof wasmModule["_" + name] !== "function" &&
      typeof wasmModule[name] !== "function"
    ) {
      return false;
    }
  }
  return true;
}

function readMemtraceBytes(wasmModule) {
  const flush = wasmExportFn(wasmModule, "LibToriPlatformEmscripten_JSHost_MemtraceFlush");
  const pathFn = wasmExportFn(wasmModule, "LibToriPlatformEmscripten_JSHost_MemtracePath");
  flush();
  const path = fromWasmCString(wasmModule, pathFn()) || "memtrace.bin";
  return wasmModule.FS.readFile(path);
}

/**
 * Flush memtrace, decode in-browser, and open the viewer in a new tab.
 * @param {object} wasmModule Emscripten Module
 */
export function openMemtraceViewer(wasmModule) {
  if (!memtraceExportsAvailable(wasmModule)) {
    throw new Error(
      "Memtrace is not available. Rebuild with make MEMTRACE=1 in src2/programs/browser.",
    );
  }

  const bytes = readMemtraceBytes(wasmModule);
  const { events, meta } = decodeMemtraceWasm(bytes);

  const viewer = window.open("./memtrace_viewer.html", "_blank");
  if (!viewer) {
    throw new Error("Popup blocked. Allow popups for this site to open the memtrace viewer.");
  }

  const payload = { type: "memtrace-data", events, meta };
  const origin = window.location.origin;

  function sendData() {
    try {
      viewer.postMessage(payload, origin);
    } catch (_) {
      /* viewer may not be ready yet */
    }
  }

  function onMessage(ev) {
    if (ev.source !== viewer || ev.origin !== origin) {
      return;
    }
    if (ev.data?.type === "memtrace-ready") {
      sendData();
    }
  }

  window.addEventListener("message", onMessage);

  let retries = 0;
  const retryTimer = window.setInterval(() => {
    sendData();
    retries += 1;
    if (retries >= 20) {
      window.clearInterval(retryTimer);
      window.removeEventListener("message", onMessage);
    }
  }, 100);

  viewer.addEventListener?.("load", sendData);
}
