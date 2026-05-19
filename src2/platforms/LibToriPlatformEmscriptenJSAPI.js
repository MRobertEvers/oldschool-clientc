import {
  fromWASMStringToJSString,
  wasmExportFn,
} from "./libplatformjs_utils.js";

export class LibToriPlatformEmscriptenJSAPI {
  constructor(wasmModule, platformPtr) {
    this.wasmModule = wasmModule;
    this.platformPtr = platformPtr;

    const mod = wasmModule;
    const getInstancePtr = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_GetInstancePtr",
    );
    this._getScriptQueue = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_GetScriptQueue",
    );
    this._scriptQueuePop = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptQueuePop",
    );
    this._scriptGetName = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptGetName",
    );
    this._scriptGetNameLength = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptGetNameLength",
    );
    this._browserMainUnlock = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_BrowserMainUnlock",
    );

    this._luaHostGetIOQueue = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_LuaHost_GetIOQueue",
    );
    this._ioQueuePop = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_IOQueuePop",
    );
    this._ioRequestGetArchiveId = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_IORequestGetArchiveId",
    );
    this._ioRequestGetTableId = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_IORequestGetTableId",
    );
    this._ioRequestGetFlags = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_IORequestGetFlags",
    );

    this.instancePtr = getInstancePtr(this.platformPtr);
  }

  getScriptQueue() {
    return this._getScriptQueue(this.instancePtr);
  }

  scriptQueuePop() {
    return this._scriptQueuePop(this.getScriptQueue());
  }

  scriptGetNameJSString(script) {
    const str = this._scriptGetName(script);
    const strLen = this._scriptGetNameLength(script);

    return fromWASMStringToJSString(this.wasmModule, str, strLen);
  }

  browserMainUnlock() {
    this._browserMainUnlock();
  }

  luaHostGetIOQueue() {
    return this._luaHostGetIOQueue(this.instancePtr);
  }

  ioQueuePop() {
    return this._ioQueuePop(this.instancePtr);
  }

  ioRequestGetArchiveId(item) {
    return this._ioRequestGetArchiveId(this.instancePtr, item);
  }

  ioRequestGetTableId(item) {
    return this._ioRequestGetTableId(this.instancePtr, item);
  }

  ioRequestGetFlags(item) {
    return this._ioRequestGetFlags(this.instancePtr, item);
  }
}
