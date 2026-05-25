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
    this._scriptAPIDat1TexturesFetch = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_TexturesFetch",
    );
    this._scriptAPIDat1TexturesLoad = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_TexturesLoad",
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
    this._ioQueueItemError = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_IOQueueItemError",
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
    this._scriptAPIDat1ModelFetchNativeInt = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ModelFetchNativeInt",
    );
    this._scriptAPIDat1ModelFetchScriptInt = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ModelFetchScriptInt",
    );
    this._scriptAPIDat1ModelLoad = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ModelLoad",
    );
    this._scriptAPIDat1SubmitGameCacheModelNativeInt = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SubmitGameCacheModelNativeInt",
    );
    this._scriptAPIDat1SubmitGameCacheModelScriptInt = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SubmitGameCacheModelScriptInt",
    );
    this._scriptAPIGameModelViewerRenderModelNativeInt = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Game_ModelViewer_RenderModelNativeInt",
    );
    this._scriptAPIGameModelViewerRenderModelScriptInt = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Game_ModelViewer_RenderModelScriptInt",
    );
    this._scriptAPIDat1ModelCleanupNativeInt = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ModelCleanupNativeInt",
    );
    this._scriptAPIDat1ModelCleanupScriptInt = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ModelCleanupScriptInt",
    );
    this._scriptAPIDat1TexturesCleanup = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_TexturesCleanup",
    );
    this._scriptAPIDat1SubmitTextures = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SubmitTextures",
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

  scriptAPIDat1TexturesFetch(ioQueue) {
    this._scriptAPIDat1TexturesFetch(this.instancePtr, ioQueue);
  }

  scriptAPIDat1TexturesLoad(ioQueue) {
    return !!this._scriptAPIDat1TexturesLoad(this.instancePtr, ioQueue);
  }

  scriptAPIDat1ModelFetchNativeInt(ioqueue, modelId) {
    this._scriptAPIDat1ModelFetchNativeInt(this.instancePtr, modelId, ioqueue);
  }

  scriptAPIDat1ModelFetchScriptInt(ioqueue, modelId) {
    this._scriptAPIDat1ModelFetchScriptInt(this.instancePtr, modelId, ioqueue);
  }

  scriptAPIDat1ModelLoad(ioqueue) {
    return !!this._scriptAPIDat1ModelLoad(this.instancePtr, ioqueue);
  }

  scriptAPIDat1SubmitGameCacheModelNativeInt(modelId) {
    this._scriptAPIDat1SubmitGameCacheModelNativeInt(this.instancePtr, modelId);
  }

  scriptAPIDat1SubmitGameCacheModelScriptInt(modelId) {
    this._scriptAPIDat1SubmitGameCacheModelScriptInt(this.instancePtr, modelId);
  }

  scriptAPIGameModelViewerInit() {
    this._scriptAPIGameModelViewerInit(this.instancePtr);
  }

  scriptAPIGameModelViewerRenderModelNativeInt(modelId) {
    this._scriptAPIGameModelViewerRenderModelNativeInt(
      this.instancePtr,
      modelId,
    );
  }

  scriptAPIGameModelViewerRenderModelScriptInt(modelId) {
    this._scriptAPIGameModelViewerRenderModelScriptInt(
      this.instancePtr,
      modelId,
    );
  }

  scriptAPIDat1ModelCleanupNativeInt(modelId) {
    this._scriptAPIDat1ModelCleanupNativeInt(this.instancePtr, modelId);
  }

  scriptAPIDat1ModelCleanupScriptInt(modelId) {
    this._scriptAPIDat1ModelCleanupScriptInt(this.instancePtr, modelId);
  }

  scriptAPIDat1TexturesCleanup() {
    this._scriptAPIDat1TexturesCleanup(this.instancePtr);
  }

  scriptAPIDat1SubmitTextures() {
    this._scriptAPIDat1SubmitTextures(this.instancePtr);
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

  ioQueueItemError(item, errorCode) {
    this._ioQueueItemError(item, errorCode);
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
