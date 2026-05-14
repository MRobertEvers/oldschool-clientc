const { lua, lauxlib, lualib, to_luastring } = window.fengari;

function fromWASMString(str) {
  return new TextDecoder().decode(str);
}

const LIBTORI_PLATFORM_EMSCRIPTEN_LUA_OK = 0;
const LIBTORI_PLATFORM_EMSCRIPTEN_LUA_ERROR = -1;
const LIBTORI_PLATFORM_EMSCRIPTEN_LUA_YIELDED = 1;

class LibToriPlatformEmscripten {
  constructor(wasmModule, platformPtr) {
    this.wasmModule = wasmModule;
    this.platformPtr = platformPtr;

    this.instancePtr =
      this.wasmModule.ToriPlatformEmscripten_JSHost_GetInstancePtr(
        this.platformPtr,
      );
  }

  getScriptQueue() {
    return this.wasmModule.ToriPlatformEmscripten_JSHost_GetScriptQueue(
      this.instancePtr,
    );
  }

  scriptQueuePop() {
    return this.wasmModule.ToriPlatformEmscripten_JSHost_ScriptQueuePop(
      this.getScriptQueue(),
    );
  }

  scriptGetNameJSString(script) {
    return fromWASMString(
      this.wasmModule.ToriPlatformEmscripten_JSHost_ScriptGetName(script),
    );
  }

  browserMainUnlock() {
    this.wasmModule.ToriPlatformEmscripten_JSHost_BrowserMainUnlock();
  }
}

export class LibToriPlatformJS {
  constructor(wasmModule, instancePtr) {
    this.host = new LibToriPlatformEmscripten(wasmModule, instancePtr);

    this.inMainLoop = false;

    this.L = lauxlib.luaL_newstate();
    lauxlib.luaL_openlibs(this.L);

    this.L_coro = null;
  }

  async LuaMainLoop() {
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
    }
  }

  async LuaRun() {
    const scriptQueue = this.host.getScriptQueue();
    try {
      while (true) {
        const script = this.host.scriptQueuePop(scriptQueue);
        if (!script) {
          break;
        }
        const scriptName = this.host.scriptGetNameJSString(script);

        this.L_coro = lauxlib.lua_newthread(this.L);
        lauxlib.luaL_loadstring(this.L_coro, scriptName);
        if (lauxlib.lua_status(this.L_coro) !== lauxlib.LUA_OK) {
          throw new Error(
            "Error in LuaRun",
            lauxlib.lua_tostring(this.L_coro, -1),
          );
        }
        const rc = lauxlib.lua_resume(this.L_coro, this.L, 0, 0);
        if (rc !== lauxlib.LUA_OK) {
          throw new Error(
            "Error in LuaRun",
            lauxlib.lua_tostring(this.L_coro, -1),
          );
        }
        return rc;
      }
    } catch (error) {
      console.error("Error in LuaRun", error);
      throw error;
    }

    return LIBTORI_PLATFORM_EMSCRIPTEN_LUA_OK;
  }

  async LuaContinue() {
    try {
      lauxlib.lua_pushinteger(this.L_coro, 99);
      lauxlib.lua_resume(this.L_coro, this.L, 0, 0);
    } catch (error) {
      console.error("Error in LuaContinue", error);
      throw error;
    }
  }
}
