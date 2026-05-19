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
    this._scriptGetIsInline = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptGetIsInline",
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

    this._scriptQueueIsEmpty = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptQueueIsEmpty",
    );
    this._scriptAPIDat1ConfigFileFetch = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ConfigFileFetch",
    );
    this._scriptAPIDat1ConfigFileLoad = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ConfigFileLoad",
    );
    this._ioQueueGetCount = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_IOQueueGetCount",
    );
    this._ioQueueGetItemByIndex = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_IOQueueGetItemByIndex",
    );
    this._ioQueueItemResolve = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_IOQueueItemResolve",
    );
    this._malloc = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_Malloc",
    );
    this._cacheDatArchiveNewFromBuffer = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_CacheDatArchiveNewFromBuffer",
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

  scriptGetIsInline(script) {
    return this._scriptGetIsInline(script) !== 0;
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

  scriptQueueIsEmpty() {
    return this._scriptQueueIsEmpty(this.instancePtr) !== 0;
  }

  scriptAPIDat1ConfigFileFetch() {
    this._scriptAPIDat1ConfigFileFetch(this.instancePtr);
  }

  scriptAPIDat1ConfigFileLoad() {
    this._scriptAPIDat1ConfigFileLoad(this.instancePtr);
  }

  ioQueueGetCount() {
    return this._ioQueueGetCount(this.instancePtr);
  }

  ioQueueGetItemByIndex(index) {
    return this._ioQueueGetItemByIndex(this.instancePtr, index);
  }

  ioQueueItemResolve(item, dataPtr) {
    this._ioQueueItemResolve(item, dataPtr);
  }

  jshostMalloc(size) {
    return this._malloc(size);
  }

  cacheDatArchiveNewFromBuffer(tableId, archiveId, dataPtr, dataSize) {
    return this._cacheDatArchiveNewFromBuffer(
      tableId,
      archiveId,
      dataPtr,
      dataSize,
    );
  }
}
