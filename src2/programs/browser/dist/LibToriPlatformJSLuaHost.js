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

  gameDat1ModelLoad(L) {
    const ioQueue = lua.lua_touserdata(L, 1);
    const ok = this.emscriptenJSAPI.scriptAPIDat1ModelLoad(ioQueue);
    lua.lua_pushboolean(L, ok);
    return 1;
  }

  gameDat1ModelFetch(L) {
    const ioQueue = lua.lua_touserdata(L, 1);

    // const modelId = lua.lua_tointeger(L, 2);
    const modelId = luaScriptIntOrLuaInt(L, 2, this.emscriptenJSAPI);
    this.emscriptenJSAPI.scriptAPIDat1ModelFetch(ioQueue, modelId);
    return 0;
  }

  gameDat1SubmitGameCacheModel(L) {
    // const modelId = lua.lua_tointeger(L, 1);
    const modelId = luaScriptIntOrLuaInt(L, 1, this.emscriptenJSAPI);
    this.emscriptenJSAPI.scriptAPIDat1SubmitGameCacheModel(modelId);
    return 0;
  }

  gameModelViewerInit(L) {
    this.emscriptenJSAPI.scriptAPIGameModelViewerInit();
    return 0;
  }

  gameModelViewerRenderModel(L) {
    // const modelId = lua.lua_tointeger(L, 1);
    const modelId = luaScriptIntOrLuaInt(L, 1, this.emscriptenJSAPI);
    this.emscriptenJSAPI.scriptAPIGameModelViewerRenderModel(modelId);
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
    "Dat1_ModelFetch",
    platformLuaHost.gameDat1ModelFetch,
  );
  bindLuaFunctionToPlatform(
    "Dat1_ModelLoad",
    platformLuaHost.gameDat1ModelLoad,
  );
  bindLuaFunctionToPlatform(
    "Dat1_SubmitGameCacheModel",
    platformLuaHost.gameDat1SubmitGameCacheModel,
  );
  bindLuaFunctionToPlatform(
    "ModelViewer_Init",
    platformLuaHost.gameModelViewerInit,
  );
  bindLuaFunctionToPlatform(
    "ModelViewer_RenderModel",
    platformLuaHost.gameModelViewerRenderModel,
  );
  lua.lua_setglobal(L, to_luastring("Game"));
}
