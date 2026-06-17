const { lua, to_luastring } = window.fengari;

export class LibToriPlatformJSLuaHost {
  /**
   * @param {*} lua - Fengari lua instance
   * @param {*} wasmModule - Emscripten WASM module
   * @param {import('./LibToriPlatformEmscriptenJSAPI').LibToriPlatformEmscriptenJSAPI} emscriptenJSAPI
   */
  constructor(wasmModule, emscriptenJSAPI) {
    this.wasmModule = wasmModule;
    this.emscriptenJSAPI = emscriptenJSAPI;
  }

  platformGetIOQueue(L) {
    const ioQueue = this.emscriptenJSAPI.luaHostGetIOQueue();
    lua.lua_pushlightuserdata(L, ioQueue);
    return 1;
  }

  platformLoadIO(L) {
    return lua.lua_yield(L, 0);
  }

  gameDat1ConfigFileFetch(L) {
    const ioQueue = lua.lua_touserdata(L, 1);
    this.emscriptenJSAPI.scriptAPIDat1ConfigFileFetch(ioQueue);
    return 0;
  }

  gameDat1ConfigFileLoad(L) {
    const ioQueue = lua.lua_touserdata(L, 1);
    const ok = this.emscriptenJSAPI.scriptAPIDat1ConfigFileLoad(ioQueue);
    lua.lua_pushboolean(L, ok);
    return 1;
  }

  gameDat1TexturesFetch(L) {
    const ioQueue = lua.lua_touserdata(L, 1);
    this.emscriptenJSAPI.scriptAPIDat1TexturesFetch(ioQueue);
    return 0;
  }

  gameDat1TexturesLoad(L) {
    const ioQueue = lua.lua_touserdata(L, 1);
    const ok = this.emscriptenJSAPI.scriptAPIDat1TexturesLoad(ioQueue);
    lua.lua_pushboolean(L, ok);
    return 1;
  }

  gameDat1ModelFetchNativeInt(L) {
    const ioQueue = lua.lua_touserdata(L, 1);
    const modelId = lua.lua_tointeger(L, 2);
    this.emscriptenJSAPI.scriptAPIDat1ModelFetchNativeInt(ioQueue, modelId);
    return 0;
  }

  gameDat1ModelFetchScriptInt(L) {
    const ioQueue = lua.lua_touserdata(L, 1);
    const modelId = lua.lua_touserdata(L, 2);
    this.emscriptenJSAPI.scriptAPIDat1ModelFetchScriptInt(ioQueue, modelId);
    return 0;
  }

  gameDat1ModelLoad(L) {
    const ioQueue = lua.lua_touserdata(L, 1);
    const ok = this.emscriptenJSAPI.scriptAPIDat1ModelLoad(ioQueue);
    lua.lua_pushboolean(L, ok);
    return 1;
  }

  gameDat1ModelFetchLuaInt(L) {
    const ioQueue = lua.lua_touserdata(L, 1);

    const modelId = lua.lua_tointeger(L, 2);
    this.emscriptenJSAPI.scriptAPIDat1ModelFetchNativeInt(ioQueue, modelId);
    return 0;
  }

  gameDat1ModelFetchLuaScriptInt(L) {
    const ioQueue = lua.lua_touserdata(L, 1);
    const modelId = lua.lua_touserdata(L, 2);
    this.emscriptenJSAPI.scriptAPIDat1ModelFetchScriptInt(ioQueue, modelId);
    return 0;
  }

  gameDat1MapChunkTerrainFetch(L) {
    const ioQueue = lua.lua_touserdata(L, 1);
    const mapx = lua.lua_tointeger(L, 2);
    const mapz = lua.lua_tointeger(L, 3);
    this.emscriptenJSAPI.scriptAPIDat1MapChunkTerrainFetch(ioQueue, mapx, mapz);
    return 0;
  }

  gameDat1MapChunkSceneryFetch(L) {
    const ioQueue = lua.lua_touserdata(L, 1);
    const mapx = lua.lua_tointeger(L, 2);
    const mapz = lua.lua_tointeger(L, 3);
    this.emscriptenJSAPI.scriptAPIDat1MapChunkSceneryFetch(ioQueue, mapx, mapz);
    return 0;
  }

  gameDat1MapChunkTerrainLoad(L) {
    const ioQueue = lua.lua_touserdata(L, 1);
    const ok = this.emscriptenJSAPI.scriptAPIDat1MapChunkTerrainLoad(ioQueue);
    lua.lua_pushboolean(L, ok);
    return 1;
  }

  gameDat1MapChunkSceneryLoad(L) {
    const ioQueue = lua.lua_touserdata(L, 1);
    const ok = this.emscriptenJSAPI.scriptAPIDat1MapChunkSceneryLoad(ioQueue);
    lua.lua_pushboolean(L, ok);
    return 1;
  }

  gameDat1SubmitGameCacheModelNativeInt(L) {
    const modelId = lua.lua_tointeger(L, 1);
    this.emscriptenJSAPI.scriptAPIDat1SubmitGameCacheModelNativeInt(modelId);
    return 0;
  }

  gameDat1SubmitGameCacheModelScriptInt(L) {
    const modelId = lua.lua_touserdata(L, 1);
    this.emscriptenJSAPI.scriptAPIDat1SubmitGameCacheModelScriptInt(modelId);
    return 0;
  }

  gameDat1ModelCleanupNativeInt(L) {
    const modelId = lua.lua_tointeger(L, 1);
    this.emscriptenJSAPI.scriptAPIDat1ModelCleanupNativeInt(modelId);
    return 0;
  }

  gameDat1ModelCleanupScriptInt(L) {
    const modelId = lua.lua_touserdata(L, 1);
    this.emscriptenJSAPI.scriptAPIDat1ModelCleanupScriptInt(modelId);
    return 0;
  }

  gameDat1TexturesCleanup(L) {
    this.emscriptenJSAPI.scriptAPIDat1TexturesCleanup();
    return 0;
  }

  gameDat1SubmitTextures(L) {
    this.emscriptenJSAPI.scriptAPIDat1SubmitTextures();
    return 0;
  }

  gameGameCacheModelsClearAll(L) {
    this.emscriptenJSAPI.scriptAPIGameCacheModelsClearAll();
    return 0;
  }

  gameModelViewerInit(L) {
    this.emscriptenJSAPI.scriptAPIGameModelViewerInit();
    return 0;
  }

  gameRunescapeInit(L) {
    this.emscriptenJSAPI.scriptAPIGameRunescapeInit();
    return 0;
  }

  gameRunescapeBuildWorld(L) {
    this.emscriptenJSAPI.scriptAPIGameRunescapeBuildWorld();
    return 0;
  }

  gameModelViewerGetGameHandle(L) {
    const handle = this.emscriptenJSAPI.scriptAPIGameModelViewerGetGameHandle();
    lua.lua_pushlightuserdata(L, handle);
    return 1;
  }

  gameCoreTaskDat1LoadModelNativeInt(L) {
    const gameHandle = lua.lua_touserdata(L, 1);
    const modelId = lua.lua_tointeger(L, 2);
    this.emscriptenJSAPI.scriptAPICoreTaskDat1LoadModelNativeInt(
      gameHandle,
      modelId,
    );
    return 0;
  }

  gameRunTasks(L) {
    const allDone = this.emscriptenJSAPI.scriptAPIRunTasks();
    lua.lua_pushboolean(L, allDone);
    return 1;
  }

  gameModelViewerRenderModelNativeInt(L) {
    // const modelId = lua.lua_tointeger(L, 1);
    const modelId = lua.lua_tointeger(L, 1);
    this.emscriptenJSAPI.scriptAPIGameModelViewerRenderModelNativeInt(modelId);
    return 0;
  }

  gameModelViewerRenderModelScriptInt(L) {
    const modelId = lua.lua_touserdata(L, 1);
    this.emscriptenJSAPI.scriptAPIGameModelViewerRenderModelScriptInt(modelId);
    return 0;
  }

  gameDat1VersionListFetch(L) {
    const ioQueue = lua.lua_touserdata(L, 1);
    this.emscriptenJSAPI.scriptAPIDat1VersionListFetch(ioQueue);
    return 0;
  }

  gameDat1VersionListLoad(L) {
    const ioQueue = lua.lua_touserdata(L, 1);
    const ok = this.emscriptenJSAPI.scriptAPIDat1VersionListLoad(ioQueue);
    lua.lua_pushboolean(L, ok);
    return 1;
  }

  gameDat1AnimationsFetchNativeInt(L) {
    const ioQueue = lua.lua_touserdata(L, 1);
    const archiveId = lua.lua_tointeger(L, 2);
    this.emscriptenJSAPI.scriptAPIDat1AnimationsFetchNativeInt(
      ioQueue,
      archiveId,
    );
    return 0;
  }

  gameDat1AnimationsLoad(L) {
    const ioQueue = lua.lua_touserdata(L, 1);
    const ok = this.emscriptenJSAPI.scriptAPIDat1AnimationsLoad(ioQueue);
    lua.lua_pushboolean(L, ok);
    return 1;
  }

  gameDat1SequencesInitFromConfigJagfile(L) {
    this.emscriptenJSAPI.scriptAPIDat1SequencesInitFromConfigJagfile();
    return 0;
  }

  gameDat1FloortypesInitFromConfigJagfile(L) {
    this.emscriptenJSAPI.scriptAPIDat1FloortypesInitFromConfigJagfile();
    return 0;
  }

  gameDat1SceneryConfigsInitFromConfigJagfile(L) {
    this.emscriptenJSAPI.scriptAPIDat1SceneryConfigsInitFromConfigJagfile();
    return 0;
  }

  gameDat1SubmitSequences(L) {
    this.emscriptenJSAPI.scriptAPIDat1SubmitSequences();
    return 0;
  }

  gameDat1SubmitFloortypes(L) {
    this.emscriptenJSAPI.scriptAPIDat1SubmitFloortypes();
    return 0;
  }

  gameDat1SubmitSceneryConfigs(L) {
    this.emscriptenJSAPI.scriptAPIDat1SubmitSceneryConfigs();
    return 0;
  }

  gameDat1SubmitAnimations(L) {
    this.emscriptenJSAPI.scriptAPIDat1SubmitAnimations();
    return 0;
  }

  gameDat1SequencesCleanup(L) {
    this.emscriptenJSAPI.scriptAPIDat1SequencesCleanup();
    return 0;
  }

  gameDat1FloortypesCleanup(L) {
    this.emscriptenJSAPI.scriptAPIDat1FloortypesCleanup();
    return 0;
  }

  gameDat1SceneryConfigsCleanup(L) {
    this.emscriptenJSAPI.scriptAPIDat1SceneryConfigsCleanup();
    return 0;
  }

  gameDat1AnimationsCleanup(L) {
    this.emscriptenJSAPI.scriptAPIDat1AnimationsCleanup();
    return 0;
  }

  gameGameCacheSequencesClearAll(L) {
    this.emscriptenJSAPI.scriptAPIGameCacheSequencesClearAll();
    return 0;
  }

  gameGameCacheFloortypesClearAll(L) {
    this.emscriptenJSAPI.scriptAPIGameCacheFloortypesClearAll();
    return 0;
  }

  gameGameCacheSceneryConfigsClearAll(L) {
    this.emscriptenJSAPI.scriptAPIGameCacheSceneryConfigsClearAll();
    return 0;
  }

  gameGameCacheAnimationsClearAll(L) {
    this.emscriptenJSAPI.scriptAPIGameCacheAnimationsClearAll();
    return 0;
  }
}

/**
 *
 * @param {*} L
 * @param {import('./LibToriPlatformJSLuaHost').LibToriPlatformJSLuaHost} platformLuaHost
 */
export function luaBindToPlatformJSLuaHost(L, platformLuaHost) {
  function bindLuaFunctionToPlatform(name, fn) {
    let boundFn = function bindFn() {
      console.log("bindFn", name);
      return fn.apply(platformLuaHost, arguments);
    };
    lua.lua_pushcfunction(L, boundFn);
    lua.lua_setfield(L, -2, to_luastring(name));
  }

  lua.lua_newtable(L);
  bindLuaFunctionToPlatform(
    "GetIOQueue", //
    platformLuaHost.platformGetIOQueue,
  );
  bindLuaFunctionToPlatform(
    "LoadIO", //
    platformLuaHost.platformLoadIO,
  );
  lua.lua_setglobal(L, to_luastring("Platform"));

  lua.lua_newtable(L);
  bindLuaFunctionToPlatform(
    "Dat1_ConfigFileFetch",
    platformLuaHost.gameDat1ConfigFileFetch,
  );
  bindLuaFunctionToPlatform(
    "Dat1_ConfigFileLoad",
    platformLuaHost.gameDat1ConfigFileLoad,
  );
  bindLuaFunctionToPlatform(
    "Dat1_TexturesFetch",
    platformLuaHost.gameDat1TexturesFetch,
  );
  bindLuaFunctionToPlatform(
    "Dat1_TexturesLoad",
    platformLuaHost.gameDat1TexturesLoad,
  );
  bindLuaFunctionToPlatform(
    "Dat1_ModelFetchNativeInt",
    platformLuaHost.gameDat1ModelFetchNativeInt,
  );
  bindLuaFunctionToPlatform(
    "Dat1_ModelFetchScriptInt",
    platformLuaHost.gameDat1ModelFetchScriptInt,
  );
  bindLuaFunctionToPlatform(
    "Dat1_ModelLoad",
    platformLuaHost.gameDat1ModelLoad,
  );
  bindLuaFunctionToPlatform(
    "Dat1_SubmitGameCacheModelNativeInt",
    platformLuaHost.gameDat1SubmitGameCacheModelNativeInt,
  );
  bindLuaFunctionToPlatform(
    "Dat1_SubmitGameCacheModelScriptInt",
    platformLuaHost.gameDat1SubmitGameCacheModelScriptInt,
  );
  bindLuaFunctionToPlatform(
    "ModelViewer_Init",
    platformLuaHost.gameModelViewerInit,
  );
  bindLuaFunctionToPlatform(
    "Runescape_Init",
    platformLuaHost.gameRunescapeInit,
  );
  bindLuaFunctionToPlatform(
    "Runescape_BuildWorld",
    platformLuaHost.gameRunescapeBuildWorld,
  );
  bindLuaFunctionToPlatform(
    "ModelViewer_GetGameHandle",
    platformLuaHost.gameModelViewerGetGameHandle,
  );
  bindLuaFunctionToPlatform(
    "CoreTask_Dat1LoadModelNativeInt",
    platformLuaHost.gameCoreTaskDat1LoadModelNativeInt,
  );
  bindLuaFunctionToPlatform(
    "RunTasks",
    platformLuaHost.gameRunTasks,
  );
  bindLuaFunctionToPlatform(
    "ModelViewer_RenderModelNativeInt",
    platformLuaHost.gameModelViewerRenderModelNativeInt,
  );
  bindLuaFunctionToPlatform(
    "ModelViewer_RenderModelScriptInt",
    platformLuaHost.gameModelViewerRenderModelScriptInt,
  );
  bindLuaFunctionToPlatform(
    "Dat1_ModelCleanupNativeInt",
    platformLuaHost.gameDat1ModelCleanupNativeInt,
  );
  bindLuaFunctionToPlatform(
    "Dat1_ModelCleanupScriptInt",
    platformLuaHost.gameDat1ModelCleanupScriptInt,
  );
  bindLuaFunctionToPlatform(
    "Dat1_TexturesCleanup",
    platformLuaHost.gameDat1TexturesCleanup,
  );
  bindLuaFunctionToPlatform(
    "Dat1_SubmitTextures",
    platformLuaHost.gameDat1SubmitTextures,
  );
  bindLuaFunctionToPlatform(
    "Dat1_MapChunkTerrainFetch",
    platformLuaHost.gameDat1MapChunkTerrainFetch,
  );
  bindLuaFunctionToPlatform(
    "Dat1_MapChunkSceneryFetch",
    platformLuaHost.gameDat1MapChunkSceneryFetch,
  );
  bindLuaFunctionToPlatform(
    "Dat1_MapChunkTerrainLoad",
    platformLuaHost.gameDat1MapChunkTerrainLoad,
  );
  bindLuaFunctionToPlatform(
    "Dat1_MapChunkSceneryLoad",
    platformLuaHost.gameDat1MapChunkSceneryLoad,
  );
  bindLuaFunctionToPlatform(
    "GameCache_ModelsClearAll",
    platformLuaHost.gameGameCacheModelsClearAll,
  );
  bindLuaFunctionToPlatform(
    "Dat1_VersionListFetch",
    platformLuaHost.gameDat1VersionListFetch,
  );
  bindLuaFunctionToPlatform(
    "Dat1_VersionListLoad",
    platformLuaHost.gameDat1VersionListLoad,
  );
  bindLuaFunctionToPlatform(
    "Dat1_AnimationsFetchNativeInt",
    platformLuaHost.gameDat1AnimationsFetchNativeInt,
  );
  bindLuaFunctionToPlatform(
    "Dat1_AnimationsLoad",
    platformLuaHost.gameDat1AnimationsLoad,
  );
  bindLuaFunctionToPlatform(
    "Dat1_SequencesInitFromConfigJagfile",
    platformLuaHost.gameDat1SequencesInitFromConfigJagfile,
  );
  bindLuaFunctionToPlatform(
    "Dat1_FloortypesInitFromConfigJagfile",
    platformLuaHost.gameDat1FloortypesInitFromConfigJagfile,
  );
  bindLuaFunctionToPlatform(
    "Dat1_SceneryConfigsInitFromConfigJagfile",
    platformLuaHost.gameDat1SceneryConfigsInitFromConfigJagfile,
  );
  bindLuaFunctionToPlatform(
    "Dat1_SubmitSequences",
    platformLuaHost.gameDat1SubmitSequences,
  );
  bindLuaFunctionToPlatform(
    "Dat1_SubmitFloortypes",
    platformLuaHost.gameDat1SubmitFloortypes,
  );
  bindLuaFunctionToPlatform(
    "Dat1_SubmitSceneryConfigs",
    platformLuaHost.gameDat1SubmitSceneryConfigs,
  );
  bindLuaFunctionToPlatform(
    "Dat1_SubmitAnimations",
    platformLuaHost.gameDat1SubmitAnimations,
  );
  bindLuaFunctionToPlatform(
    "Dat1_SequencesCleanup",
    platformLuaHost.gameDat1SequencesCleanup,
  );
  bindLuaFunctionToPlatform(
    "Dat1_FloortypesCleanup",
    platformLuaHost.gameDat1FloortypesCleanup,
  );
  bindLuaFunctionToPlatform(
    "Dat1_SceneryConfigsCleanup",
    platformLuaHost.gameDat1SceneryConfigsCleanup,
  );
  bindLuaFunctionToPlatform(
    "Dat1_AnimationsCleanup",
    platformLuaHost.gameDat1AnimationsCleanup,
  );
  bindLuaFunctionToPlatform(
    "GameCache_SequencesClearAll",
    platformLuaHost.gameGameCacheSequencesClearAll,
  );
  bindLuaFunctionToPlatform(
    "GameCache_FloortypesClearAll",
    platformLuaHost.gameGameCacheFloortypesClearAll,
  );
  bindLuaFunctionToPlatform(
    "GameCache_SceneryConfigsClearAll",
    platformLuaHost.gameGameCacheSceneryConfigsClearAll,
  );
  bindLuaFunctionToPlatform(
    "GameCache_AnimationsClearAll",
    platformLuaHost.gameGameCacheAnimationsClearAll,
  );
  lua.lua_setglobal(L, to_luastring("Game"));
}
