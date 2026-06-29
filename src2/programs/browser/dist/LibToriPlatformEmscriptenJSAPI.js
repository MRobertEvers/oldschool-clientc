import {
  fromWasmCString,
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
    this._ioRequestGetKind = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_IORequestGetKind",
    );
    this._ioRequestGetStatus = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_IORequestGetStatus",
    );
    this._ioRequestGetPath = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_IORequestGetPath",
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
    this._ioQueueItemResolveWithSize = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_IOQueueItemResolveWithSize",
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
    this._scriptAPIGameRunescapeInit = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Game_Runescape_Init",
    );
    this._scriptAPIGameRunescapeBuildWorld = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Game_Runescape_BuildWorld",
    );
    this._scriptAPIGameRunescapeBuildWorldCenterzoneNativeInt = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Game_Runescape_BuildWorldCenterzoneNativeInt",
    );
    this._scriptAPIGameRunescapeBuildWorldChunkListNativeInt = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Game_Runescape_BuildWorldChunkListNativeInt",
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
    this._scriptAPIDat1MapChunkTerrainFetch = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_MapChunkTerrainFetch",
    );
    this._scriptAPIDat1MapChunkSceneryFetch = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_MapChunkSceneryFetch",
    );
    this._scriptAPIDat1MapChunkTerrainLoad = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_MapChunkTerrainLoad",
    );
    this._scriptAPIDat1MapChunkSceneryLoad = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_MapChunkSceneryLoad",
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
    this._scriptAPIGameModelViewerGetGameHandle = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Game_ModelViewer_GetGameHandle",
    );
    this._scriptAPICoreTaskDat1LoadModelNativeInt = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_CoreTask_Dat1LoadModelNativeInt",
    );
    this._scriptAPIRunTasks = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_RunTasks",
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
    this._scriptAPIDat2TexturesLoad = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat2_TexturesLoad",
    );
    this._scriptAPIDat2SubmitTextures = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat2_SubmitTextures",
    );
    this._scriptAPIGetCacheMode = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_GetCacheMode",
    );
    this._scriptAPIGameCacheModelsClearAll = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_GameCache_ModelsClearAll",
    );
    this._scriptAPIDat1VersionListFetch = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_VersionListFetch",
    );
    this._scriptAPIDat1VersionListLoad = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_VersionListLoad",
    );
    this._scriptAPIDat1AnimationsFetchNativeInt = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_AnimationsFetchNativeInt",
    );
    this._scriptAPIDat1AnimationsLoad = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_AnimationsLoad",
    );
    this._scriptAPIDat1SequencesInitFromConfigJagfile = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SequencesInitFromConfigJagfile",
    );
    this._scriptAPIDat1FloortypesInitFromConfigJagfile = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_FloortypesInitFromConfigJagfile",
    );
    this._scriptAPIDat1SceneryConfigsInitFromConfigJagfile = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SceneryConfigsInitFromConfigJagfile",
    );
    this._scriptAPIDat1SubmitSequences = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SubmitSequences",
    );
    this._scriptAPIDat1SubmitFloortypes = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SubmitFloortypes",
    );
    this._scriptAPIDat1SubmitSceneryConfigs = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SubmitSceneryConfigs",
    );
    this._scriptAPIDat1SubmitAnimations = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SubmitAnimations",
    );
    this._scriptAPIDat1SequencesCleanup = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SequencesCleanup",
    );
    this._scriptAPIDat1FloortypesCleanup = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_FloortypesCleanup",
    );
    this._scriptAPIDat1SceneryConfigsCleanup = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SceneryConfigsCleanup",
    );
    this._scriptAPIDat1AnimationsCleanup = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_AnimationsCleanup",
    );
    this._scriptAPIGameCacheSequencesClearAll = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_GameCache_SequencesClearAll",
    );
    this._scriptAPIGameCacheFloortypesClearAll = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_GameCache_FloortypesClearAll",
    );
    this._scriptAPIGameCacheSceneryConfigsClearAll = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_GameCache_SceneryConfigsClearAll",
    );
    this._scriptAPIGameCacheAnimationsClearAll = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptAPI_GameCache_AnimationsClearAll",
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

  ioRequestGetKind(item) {
    return this._ioRequestGetKind(this.instancePtr, item);
  }

  ioRequestGetStatus(item) {
    return this._ioRequestGetStatus(this.instancePtr, item);
  }

  ioRequestGetPath(item) {
    return this._ioRequestGetPath(this.instancePtr, item);
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

  scriptAPIDat1MapChunkTerrainFetch(ioqueue, mapx, mapz) {
    this._scriptAPIDat1MapChunkTerrainFetch(
      this.instancePtr,
      mapx,
      mapz,
      ioqueue,
    );
  }

  scriptAPIDat1MapChunkSceneryFetch(ioqueue, mapx, mapz) {
    this._scriptAPIDat1MapChunkSceneryFetch(
      this.instancePtr,
      mapx,
      mapz,
      ioqueue,
    );
  }

  scriptAPIDat1MapChunkTerrainLoad(ioqueue) {
    return !!this._scriptAPIDat1MapChunkTerrainLoad(this.instancePtr, ioqueue);
  }

  scriptAPIDat1MapChunkSceneryLoad(ioqueue) {
    return !!this._scriptAPIDat1MapChunkSceneryLoad(this.instancePtr, ioqueue);
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

  scriptAPIGameRunescapeInit() {
    this._scriptAPIGameRunescapeInit(this.instancePtr);
  }

  scriptAPIGameRunescapeBuildWorld() {
    this._scriptAPIGameRunescapeBuildWorld(this.instancePtr);
  }

  scriptAPIGameRunescapeBuildWorldCenterzoneNativeInt(centerX, centerZ, sceneSize) {
    this._scriptAPIGameRunescapeBuildWorldCenterzoneNativeInt(
      this.instancePtr,
      centerX,
      centerZ,
      sceneSize,
    );
  }

  scriptAPIGameRunescapeBuildWorldChunkListNativeInt(chunksPtr, count) {
    this._scriptAPIGameRunescapeBuildWorldChunkListNativeInt(
      this.instancePtr,
      chunksPtr,
      count,
    );
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

  scriptAPIGameModelViewerGetGameHandle() {
    return this._scriptAPIGameModelViewerGetGameHandle(this.instancePtr);
  }

  scriptAPICoreTaskDat1LoadModelNativeInt(gameHandle, modelId) {
    this._scriptAPICoreTaskDat1LoadModelNativeInt(
      this.instancePtr,
      gameHandle,
      modelId,
    );
  }

  scriptAPIRunTasks() {
    return this._scriptAPIRunTasks(this.instancePtr);
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

  scriptAPIDat2TexturesLoad() {
    return !!this._scriptAPIDat2TexturesLoad(this.instancePtr);
  }

  scriptAPIDat2SubmitTextures() {
    this._scriptAPIDat2SubmitTextures(this.instancePtr);
  }

  scriptAPIGetCacheMode() {
    const ptr = this._scriptAPIGetCacheMode(this.instancePtr);
    return fromWasmCString(this.wasmModule, ptr) ?? "dat1";
  }

  scriptAPIGameCacheModelsClearAll() {
    this._scriptAPIGameCacheModelsClearAll(this.instancePtr);
  }

  scriptAPIDat1VersionListFetch(ioQueue) {
    this._scriptAPIDat1VersionListFetch(this.instancePtr, ioQueue);
  }

  scriptAPIDat1VersionListLoad(ioQueue) {
    return !!this._scriptAPIDat1VersionListLoad(this.instancePtr, ioQueue);
  }

  scriptAPIDat1AnimationsFetchNativeInt(ioQueue, archiveId) {
    this._scriptAPIDat1AnimationsFetchNativeInt(
      this.instancePtr,
      archiveId,
      ioQueue,
    );
  }

  scriptAPIDat1AnimationsLoad(ioQueue) {
    return !!this._scriptAPIDat1AnimationsLoad(this.instancePtr, ioQueue);
  }

  scriptAPIDat1SequencesInitFromConfigJagfile() {
    this._scriptAPIDat1SequencesInitFromConfigJagfile(this.instancePtr);
  }

  scriptAPIDat1FloortypesInitFromConfigJagfile() {
    this._scriptAPIDat1FloortypesInitFromConfigJagfile(this.instancePtr);
  }

  scriptAPIDat1SceneryConfigsInitFromConfigJagfile() {
    this._scriptAPIDat1SceneryConfigsInitFromConfigJagfile(this.instancePtr);
  }

  scriptAPIDat1SubmitSequences() {
    this._scriptAPIDat1SubmitSequences(this.instancePtr);
  }

  scriptAPIDat1SubmitFloortypes() {
    this._scriptAPIDat1SubmitFloortypes(this.instancePtr);
  }

  scriptAPIDat1SubmitSceneryConfigs() {
    this._scriptAPIDat1SubmitSceneryConfigs(this.instancePtr);
  }

  scriptAPIDat1SubmitAnimations() {
    this._scriptAPIDat1SubmitAnimations(this.instancePtr);
  }

  scriptAPIDat1SequencesCleanup() {
    this._scriptAPIDat1SequencesCleanup(this.instancePtr);
  }

  scriptAPIDat1FloortypesCleanup() {
    this._scriptAPIDat1FloortypesCleanup(this.instancePtr);
  }

  scriptAPIDat1SceneryConfigsCleanup() {
    this._scriptAPIDat1SceneryConfigsCleanup(this.instancePtr);
  }

  scriptAPIDat1AnimationsCleanup() {
    this._scriptAPIDat1AnimationsCleanup(this.instancePtr);
  }

  scriptAPIGameCacheSequencesClearAll() {
    this._scriptAPIGameCacheSequencesClearAll(this.instancePtr);
  }

  scriptAPIGameCacheFloortypesClearAll() {
    this._scriptAPIGameCacheFloortypesClearAll(this.instancePtr);
  }

  scriptAPIGameCacheSceneryConfigsClearAll() {
    this._scriptAPIGameCacheSceneryConfigsClearAll(this.instancePtr);
  }

  scriptAPIGameCacheAnimationsClearAll() {
    this._scriptAPIGameCacheAnimationsClearAll(this.instancePtr);
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

  ioQueueItemResolveWithSize(item, dataPtr, dataSize) {
    this._ioQueueItemResolveWithSize(item, dataPtr, dataSize);
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
