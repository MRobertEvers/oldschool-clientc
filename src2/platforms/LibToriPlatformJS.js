const { lua, lauxlib, lualib, to_luastring, from_luastring } = window.fengari;

const LIBTORI_PLATFORM_EMSCRIPTEN_LUA_OK = 0;
const LIBTORI_PLATFORM_EMSCRIPTEN_LUA_ERROR = -1;
const LIBTORI_PLATFORM_EMSCRIPTEN_LUA_YIELDED = 1;

const LUA_SCRIPTS_PREFIX = "/revs/scripts/";

function fromLuaString(sz) {
  if (sz instanceof Uint8Array) {
    return from_luastring(sz);
  }
  return sz;
}

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

function fromWASMStringToJSString(wasm, strPtr, strLen) {
  const heap = wasm.HEAPU8;
  const memory = heap?.buffer ?? wasm.memory?.buffer;
  if (strPtr === 0) return null;
  const view = new Uint8Array(memory, strPtr, strLen ?? 0);
  return new TextDecoder().decode(view);
}

class LibToriPlatformEmscriptenJSAPI {
  constructor(wasmModule, instancePtr) {
    this.wasmModule = wasmModule;
    this.instancePtr = instancePtr;

    const mod = wasmModule;
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
    this._scriptQueueIsEmpty = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_ScriptQueueIsEmpty",
    );
    this._luaHostGetIOQueue = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_LuaHost_GetIOQueue",
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
    this._ioRequestGetTableId = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_IORequestGetTableId",
    );
    this._ioRequestGetArchiveId = wasmExportFn(
      mod,
      "LibToriPlatformEmscripten_JSHost_IORequestGetArchiveId",
    );
  }

  getScriptQueue() {
    return this._getScriptQueue(this.instancePtr);
  }

  scriptQueuePop() {
    return this._scriptQueuePop(this.getScriptQueue());
  }

  scriptQueueIsEmpty() {
    return this._scriptQueueIsEmpty(this.instancePtr) !== 0;
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

  ioRequestGetTableId(item) {
    return this._ioRequestGetTableId(this.instancePtr, item);
  }

  ioRequestGetArchiveId(item) {
    return this._ioRequestGetArchiveId(this.instancePtr, item);
  }
}

function makeLuaHostBindings(platformJS) {
  const host = platformJS.host;
  const instancePtr = platformJS.host.instancePtr;

  function LibToriPlatformJS_LuaHost_Platform_GetIOQueue(L) {
    const ioQueue = host.luaHostGetIOQueue();
    lua.lua_pushlightuserdata(L, ioQueue);
    return 1;
  }

  function LibToriPlatformJS_LuaHost_Platform_LoadIO(L) {
    return lua.lua_yield(L, 0);
  }

  function LibToriPlatformJS_LuaHost_Game_Dat1_ConfigFileFetch(L) {
    host.scriptAPIDat1ConfigFileFetch();
    return 0;
  }

  function LibToriPlatformJS_LuaHost_Game_Dat1_ConfigFileLoad(L) {
    host.scriptAPIDat1ConfigFileLoad();
    return 0;
  }

  function LibToriPlatformJS_LuaHost_Print(L) {
    const sz = lua.lua_tostring(L, 1);
    console.log(fromLuaString(sz));
    return 0;
  }

  return {
    LibToriPlatformJS_LuaHost_Platform_GetIOQueue,
    LibToriPlatformJS_LuaHost_Platform_LoadIO,
    LibToriPlatformJS_LuaHost_Game_Dat1_ConfigFileFetch,
    LibToriPlatformJS_LuaHost_Game_Dat1_ConfigFileLoad,
    LibToriPlatformJS_LuaHost_Print,
  };
}

function bindLuaGlobals(L, platformJS) {
  const fns = makeLuaHostBindings(platformJS);

  lua.lua_newtable(L);
  lua.lua_pushcfunction(L, fns.LibToriPlatformJS_LuaHost_Platform_GetIOQueue);
  lua.lua_setfield(L, -2, to_luastring("GetIOQueue"));
  lua.lua_pushcfunction(L, fns.LibToriPlatformJS_LuaHost_Platform_LoadIO);
  lua.lua_setfield(L, -2, to_luastring("LoadIO"));
  lua.lua_setglobal(L, to_luastring("Platform"));

  lua.lua_newtable(L);
  lua.lua_pushcfunction(
    L,
    fns.LibToriPlatformJS_LuaHost_Game_Dat1_ConfigFileFetch,
  );
  lua.lua_setfield(L, -2, to_luastring("Dat1_ConfigFileFetch"));
  lua.lua_pushcfunction(
    L,
    fns.LibToriPlatformJS_LuaHost_Game_Dat1_ConfigFileLoad,
  );
  lua.lua_setfield(L, -2, to_luastring("Dat1_ConfigFileLoad"));
  lua.lua_setglobal(L, to_luastring("Game"));

  lua.lua_newtable(L);
  lua.lua_pushcfunction(L, fns.LibToriPlatformJS_LuaHost_Print);
  lua.lua_setfield(L, -2, to_luastring("Print"));
  lua.lua_setglobal(L, to_luastring("Host"));
}

class LibToriPlatformJS {
  constructor(wasmModule, instancePtr) {
    this.wasmModule = wasmModule;
    this.host = new LibToriPlatformEmscriptenJSAPI(wasmModule, instancePtr);

    this.inMainLoop = false;

    this.L = lauxlib.luaL_newstate();
    lualib.luaL_openlibs(this.L);

    bindLuaGlobals(this.L, this);

    this.L_coro = null;
  }

  async EmscriptenHost_LuaMainLoop() {
    if (this.inMainLoop) {
      throw new Error("Already in main loop");
    }
    this.inMainLoop = true;

    try {
      while (!this.host.scriptQueueIsEmpty()) {
        let rc = await this.LuaRun();

        while (rc === LIBTORI_PLATFORM_EMSCRIPTEN_LUA_YIELDED) {
          await this.handleIOQueue();
          rc = await this.LuaContinue();
        }

        if (rc !== LIBTORI_PLATFORM_EMSCRIPTEN_LUA_OK) {
          throw new Error("Error in LuaMainLoop, rc=" + rc);
        }
      }
    } finally {
      this.inMainLoop = false;
      this.host.browserMainUnlock();
    }
  }

  loadInlineScriptSource(source) {
    const loadRc = lauxlib.luaL_loadstring(this.L_coro, to_luastring(source));
    if (loadRc !== lua.LUA_OK) {
      const sz = lua.lua_tostring(this.L_coro, -1);
      throw new Error("Lua load error: " + fromLuaString(sz));
    }
  }

  async loadScriptSource(scriptName) {
    const path = LUA_SCRIPTS_PREFIX + scriptName;
    try {
      const rc = lauxlib.luaL_loadfile(this.L_coro, to_luastring(path));
      if (rc === lua.LUA_OK) {
        return;
      }
    } catch (_e) {
      /* fall through to fetch */
    }

    const response = await fetch("revs/scripts/" + scriptName);
    if (!response.ok) {
      throw new Error("Failed to fetch script: " + scriptName);
    }
    const text = await response.text();
    const loadRc = lauxlib.luaL_loadstring(
      this.L_coro,
      to_luastring(text),
      to_luastring(scriptName),
    );
    if (loadRc !== lua.LUA_OK) {
      const sz = lua.lua_tostring(this.L_coro, -1);
      throw new Error("Lua load error: " + fromLuaString(sz));
    }
  }

  async LuaRun() {
    try {
      const script = this.host.scriptQueuePop();
      if (!script) {
        return LIBTORI_PLATFORM_EMSCRIPTEN_LUA_OK;
      }

      const scriptName = this.host.scriptGetNameJSString(script);
      const isInline = this.host.scriptGetIsInline(script);

      this.L_coro = lua.lua_newthread(this.L);
      if (isInline) {
        this.loadInlineScriptSource(scriptName);
      } else {
        await this.loadScriptSource(scriptName);
      }

      const rc = lua.lua_resume(this.L_coro, this.L, 0);

      switch (rc) {
        case lua.LUA_OK:
          this.L_coro = null;
          return LIBTORI_PLATFORM_EMSCRIPTEN_LUA_OK;
        case lua.LUA_YIELD:
          return LIBTORI_PLATFORM_EMSCRIPTEN_LUA_YIELDED;
        default: {
          const sz = lua.lua_tostring(this.L_coro, -1);
          this.L_coro = null;
          throw new Error("Error in LuaRun: " + fromLuaString(sz));
        }
      }
    } catch (error) {
      console.error("Error in LuaRun", error);
      throw error;
    }
  }

  async LuaContinue() {
    try {
      const rc = lua.lua_resume(this.L_coro, this.L, 0);

      switch (rc) {
        case lua.LUA_OK:
          this.L_coro = null;
          return LIBTORI_PLATFORM_EMSCRIPTEN_LUA_OK;
        case lua.LUA_YIELD:
          return LIBTORI_PLATFORM_EMSCRIPTEN_LUA_YIELDED;
        default: {
          const sz = lua.lua_tostring(this.L_coro, -1);
          this.L_coro = null;
          throw new Error("Error in LuaContinue: " + fromLuaString(sz));
        }
      }
    } catch (error) {
      console.error("Error in LuaContinue", error);
      throw error;
    }
  }

  async handleIOQueue() {
    const count = this.host.ioQueueGetCount();
    if (count === 0) {
      return;
    }

    const ioserverUrl = "http://localhost:8080";

    for (let i = 0; i < count; i++) {
      const item = this.host.ioQueueGetItemByIndex(i);
      if (!item) {
        continue;
      }

      const tableId = this.host.ioRequestGetTableId(item);
      const archiveId = this.host.ioRequestGetArchiveId(item);

      try {
        const url = `${ioserverUrl}/archive/${tableId}/${archiveId}`;
        const response = await fetch(url);

        if (!response.ok) {
          throw new Error(
            `Failed to fetch archive ${tableId}/${archiveId}: ${response.status}`,
          );
        }

        const arrayBuffer = await response.arrayBuffer();
        const bytes = new Uint8Array(arrayBuffer);

        const dataPtr = this.host.jshostMalloc(bytes.length);
        if (!dataPtr) {
          throw new Error("Failed to allocate WASM memory for archive data");
        }

        const heap = this.wasmModule.HEAPU8;
        heap.set(bytes, dataPtr);

        const archivePtr = this.host.cacheDatArchiveNewFromBuffer(
          tableId,
          archiveId,
          dataPtr,
          bytes.length,
        );

        if (!archivePtr) {
          throw new Error("Failed to create CacheDatArchive from buffer");
        }

        this.host.ioQueueItemResolve(item, archivePtr);
      } catch (error) {
        console.error(`Error loading archive ${tableId}/${archiveId}:`, error);
        throw error;
      }
    }
  }
}

window.LibToriPlatformJS = LibToriPlatformJS;
