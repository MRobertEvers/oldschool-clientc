const { lua, lauxlib, lualib, to_luastring } = window.fengari;

const LIBTORI_PLATFORM_EMSCRIPTEN_LUA_OK = 0;
const LIBTORI_PLATFORM_EMSCRIPTEN_LUA_ERROR = -1;
const LIBTORI_PLATFORM_EMSCRIPTEN_LUA_YIELDED = 1;

function bindLuaHostFunctions(lua) {
  lua.newtable();
  lua.pushcfunction(lua, LibToriPlatformJS_LuaHost_Platform_LoadIO);
  lua.setfield(lua, -1, "Platform_LoadIO");
  lua.pushcfunction(lua, LibToriPlatformJS_LuaHost_Platform_GetIOQueue);
  lua.setfield(lua, -1, "Platform_GetIOQueue");
  lua.pushcfunction(lua, LibToriPlatformJS_LuaHost_Game_Dat1_ConfigFileFetch);
  lua.setfield(lua, -1, "Game_Dat1_ConfigFileFetch");
  lua.pushcfunction(lua, LibToriPlatformJS_LuaHost_Game_Dat1_ConfigFileLoad);
  lua.setfield(lua, -1, "Game_Dat1_ConfigFileLoad");
  lua.setglobal(lua, "Host");
}

class LibToriPlatformJS {
  constructor(wasmModule, instancePtr) {
    this.host = new LibToriPlatformEmscripten(wasmModule, instancePtr);

    this.inMainLoop = false;

    this.L = lauxlib.luaL_newstate();
    lualib.luaL_openlibs(this.L);

    bindLuaHostFunctions(this.L);

    this.L_coro = null;
  }

  async EmscriptenHost_LuaMainLoop() {
    if (this.inMainLoop) {
      throw new Error("Already in main loop");
    }
    this.inMainLoop = true;

    try {
      while (true) {
        let rc = await this.LuaRun();

        while (rc === LIBTORI_PLATFORM_EMSCRIPTEN_LUA_YIELDED) {
          // TODO: resolve yield requests
          rc = await this.LuaContinue();
        }
        if (rc === LIBTORI_PLATFORM_EMSCRIPTEN_LUA_OK) {
          break;
        } else {
          throw new Error("Error in LuaMainLoop", rc);
        }
      }
    } finally {
      this.inMainLoop = false;
      this.host.browserMainUnlock();
    }
  }

  async LuaRun() {
    const scriptQueue = this.host.getScriptQueue();
    try {
      const script = this.host.scriptQueuePop(scriptQueue);
      if (!script) {
        return LIBTORI_PLATFORM_EMSCRIPTEN_LUA_OK;
      }
      const scriptName = this.host.scriptGetNameJSString(script);

      this.L_coro = lua.lua_newthread(this.L);
      if (
        lauxlib.luaL_loadstring(this.L_coro, to_luastring(scriptName)) !==
        lua.LUA_OK
      ) {
        const sz = lua.lua_tostring(this.L_coro, -1);
        throw new Error("Lua load error: " + fromLuaString(sz));
      }

      const rc = lua.lua_resume(this.L_coro, this.L, 0, 0);

      switch (rc) {
        case lua.LUA_OK:
          lua.lua_close(this.L_coro);
          this.L_coro = null;
          return LIBTORI_PLATFORM_EMSCRIPTEN_LUA_OK;
        case lua.LUA_YIELD:
          return LIBTORI_PLATFORM_EMSCRIPTEN_LUA_YIELDED;
        default:
          const sz = lua.lua_tostring(this.L_coro, -1);
          lua.lua_close(this.L_coro);
          this.L_coro = null;
          throw new Error("Error in LuaRun" + fromLuaString(sz));
      }
    } catch (error) {
      console.error("Error in LuaRun", error.message);
      throw error;
    }
  }

  async LuaContinue() {
    try {
      const rc = lua.lua_resume(this.L_coro, this.L, 1, 0);
      switch (rc) {
        case lua.LUA_OK:
          this.L_coro = null;
          return LIBTORI_PLATFORM_EMSCRIPTEN_LUA_OK;
        case lua.LUA_YIELD:
          return LIBTORI_PLATFORM_EMSCRIPTEN_LUA_YIELDED;
        default:
          throw new Error("Error in LuaContinue", rc);
      }
    } catch (error) {
      console.error("Error in LuaContinue", error);
      throw error;
    }
  }
}

window.LibToriPlatformJS = LibToriPlatformJS;
