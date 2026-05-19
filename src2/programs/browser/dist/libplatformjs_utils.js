export function fromWASMStringToJSString(wasm, strPtr, strLen) {
  const heap = wasm.HEAPU8;
  const memory = heap?.buffer ?? wasm.memory?.buffer;
  if (strPtr === 0) return null;

  const view = new Uint8Array(memory, strPtr, strLen ?? 0);
  const jsStr = new TextDecoder().decode(view);

  return jsStr;
}

export function wasmExportFn(mod, baseName) {
  const underscored = mod["_" + baseName];
  if (typeof underscored === "function") {
    return underscored;
  }
  const plain = mod[baseName];
  if (typeof plain === "function") {
    return plain;
  }
  throw new Error("Missing WASM export: " + baseName);
}

export function fromLuaStringToJSString(str) {
  if (str instanceof Uint8Array) {
    str = new TextDecoder().decode(str);
  }
  return str;
}
