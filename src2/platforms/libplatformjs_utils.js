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
