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
    this._scriptGetArgCount = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptGetArgCount",
    );
    this._scriptGetArg = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptGetArg",
    );
    this._scriptValueAsInt = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptValueAsInt",
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
    this._malloc = wasmExportFn(mod, "LibToriPlatformEmscripten_JSHost_Malloc");
    this._cacheDatArchiveNewFromBuffer = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_CacheDatArchiveNewFromBuffer",
    );
    this._cacheDatArchiveDeserialize = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_CacheDatArchiveDeserialize",
    );

    this._scriptAPIGameModelViewerInit = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Game_ModelViewer_Init",
    );
    this._scriptAPIDat1ModelFetch = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ModelFetch",
    );
    this._scriptAPIDat1ModelLoad = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ModelLoad",
    );
    this._scriptAPIDat1SubmitGameCacheModel = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SubmitGameCacheModel",
    );
    this._scriptAPIGameModelViewerRenderModel = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Game_ModelViewer_RenderModel",
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

  scriptGetArgCount(script) {
    return this._scriptGetArgCount(script);
  }

  scriptGetArg(script, index) {
    return this._scriptGetArg(script, index);
  }

  scriptValueAsInt(scriptValue) {
    return this._scriptValueAsInt(scriptValue);
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

  scriptAPIDat1ConfigFileFetch(ioQueue) {
    this._scriptAPIDat1ConfigFileFetch(this.instancePtr, ioQueue);
  }

  scriptAPIDat1ConfigFileLoad(ioQueue) {
    return !!this._scriptAPIDat1ConfigFileLoad(this.instancePtr, ioQueue);
  }

  scriptAPIDat1ModelFetch(ioqueue, modelId) {
    this._scriptAPIDat1ModelFetch(this.instancePtr, modelId, ioqueue);
  }

  scriptAPIDat1ModelLoad(ioqueue) {
    return !!this._scriptAPIDat1ModelLoad(this.instancePtr, ioqueue);
  }

  scriptAPIDat1SubmitGameCacheModel(modelId) {
    this._scriptAPIDat1SubmitGameCacheModel(this.instancePtr, modelId);
  }

  scriptAPIGameModelViewerInit() {
    this._scriptAPIGameModelViewerInit(this.instancePtr);
  }

  scriptAPIGameModelViewerRenderModel(modelId) {
    this._scriptAPIGameModelViewerRenderModel(this.instancePtr, modelId);
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

  malloc(size) {
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

  cacheDatArchiveDeserialize(dataPtr, dataSize) {
    return this._cacheDatArchiveDeserialize(dataPtr, dataSize);
  }
}
