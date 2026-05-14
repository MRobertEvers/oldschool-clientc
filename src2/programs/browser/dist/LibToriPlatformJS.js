const { lua, lauxlib, lualib, to_luastring } = window.fengari;

function fromWASMString(wasm, strPtr, strLen) {
  const heap = wasm.HEAPU8;
  const memory = heap?.buffer ?? wasm.memory?.buffer;
  if (strPtr === 0) return null;

  const view = new Uint8Array(memory, strPtr, strLen ?? 0);
  const jsStr = new TextDecoder().decode(view);

  return jsStr;
}

function fromLuaString(str) {
  if (str instanceof Uint8Array) {
    str = new TextDecoder().decode(str);
  }
  return str;
}

const LIBTORI_PLATFORM_EMSCRIPTEN_LUA_OK = 0;
const LIBTORI_PLATFORM_EMSCRIPTEN_LUA_ERROR = -1;
const LIBTORI_PLATFORM_EMSCRIPTEN_LUA_YIELDED = 1;

/** Emscripten attaches e lauxlib.lua_newthreadxports as Module._symbolName */
function wasmExportFn(mod, baseName) {
  const underscored = mod["_" + baseName];
  if (typeof underscored === "function") {
    return underscored;
  }
  const plain = mod[baseName];
  if (typeof plain === "function") {
    return plain;
  }
  throw new Error("Missing WASM export: " + baseName);
}

class LibToriPlatformEmscripten {
  constructor(wasmModule, platformPtr) {
    this.wasmModule = wasmModule;
    this.platformPtr = platformPtr;

    const mod = wasmModule;
    const getInstancePtr = wasmExportFn(
      mod,
      "ToriPlatformEmscripten_JSHost_GetInstancePtr",
    );
    this._getScriptQueue = wasmExportFn(
      mod,
      "ToriPlatformEmscripten_JSHost_GetScriptQueue",
    );
    this._scriptQueuePop = wasmExportFn(
      mod,
      "ToriPlatformEmscripten_JSHost_ScriptQueuePop",
    );
    this._scriptGetName = wasmExportFn(
      mod,
      "ToriPlatformEmscripten_JSHost_ScriptGetName",
    );
    this._scriptGetNameLength = wasmExportFn(
      mod,
      "ToriPlatformEmscripten_JSHost_ScriptGetNameLength",
    );
    this._browserMainUnlock = wasmExportFn(
      mod,
      "ToriPlatformEmscripten_JSHost_BrowserMainUnlock",
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

    return fromWASMString(this.wasmModule, str, strLen);
  }

  browserMainUnlock() {
    this._browserMainUnlock();
  }
}

class LibToriPlatformJS {
  constructor(wasmModule, instancePtr) {
    this.host = new LibToriPlatformEmscripten(wasmModule, instancePtr);

    this.inMainLoop = false;

    this.L = lauxlib.luaL_newstate();
    lualib.luaL_openlibs(this.L);

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
      lua.lua_pushinteger(this.L_coro, 99);
      const rc = lua.lua_resume(this.L_coro, this.L, 1, 0);
      switch (rc) {
        case lua.LUA_OK:
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
