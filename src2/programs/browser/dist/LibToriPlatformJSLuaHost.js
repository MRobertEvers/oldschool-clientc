const { lua, to_luastring } = window.fengari;

/**
 * @param {import('./LibToriPlatformEmscriptenJSAPI').LibToriPlatformEmscriptenJSAPI} platformEmscriptenJSAPI
 */
function luaScriptIntOrLuaInt(L, index, platformEmscriptenJSAPI) {
  const rctype = lua.lua_type(L, index);
  switch (rctype) {
    case lua.LUA_TNUMBER:
      return lua.lua_tonumber(L, index);
    case lua.LUA_TLIGHTUSERDATA:
      return platformEmscriptenJSAPI.scriptValueAsInt(
        lua.lua_touserdata(L, index),
      );
    default:
      return 0;
  }
}

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

  gameModelViewerInit(L) {
    this.emscriptenJSAPI.scriptAPIGameModelViewerInit();
    return 0;
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
  lua.lua_setglobal(L, to_luastring("Game"));
}
