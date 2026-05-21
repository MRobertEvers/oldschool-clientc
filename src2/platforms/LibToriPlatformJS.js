import { fromLuaStringToJSString } from "./libplatformjs_utils.js";
import { LibToriPlatformEmscriptenJSAPI } from "./LibToriPlatformEmscriptenJSAPI.js";
import {
  LibToriPlatformJSLuaHost,
  luaBindToPlatformJSLuaHost,
} from "./LibToriPlatformJSLuaHost.js";

const { lua, lauxlib, lualib, to_luastring } = window.fengari;

const LIBTORI_PLATFORM_EMSCRIPTEN_LUA_OK = 0;
const LIBTORI_PLATFORM_EMSCRIPTEN_LUA_ERROR = -1;
const LIBTORI_PLATFORM_EMSCRIPTEN_LUA_YIELDED = 1;

const LUA_SCRIPTS_PREFIX = "/revs/scripts/";

class LibToriPlatformJS {
  constructor(wasmModule, instancePtr) {
    this.wasmModule = wasmModule;
    this.host = new LibToriPlatformEmscriptenJSAPI(wasmModule, instancePtr);
    this.luaHost = new LibToriPlatformJSLuaHost(wasmModule, this.host);

    this.inMainLoop = false;

    this.L = lauxlib.luaL_newstate();
    lualib.luaL_openlibs(this.L);

    luaBindToPlatformJSLuaHost(this.L, this.luaHost);

    // 2. Define and set the native error handler
    lua.lua_atnativeerror(this.L, function (l_state) {
      // The JavaScript error/exception is currently at the top of the stack (index -1) as userdata.
      const jsError = lua.lua_touserdata(l_state, -1);

      // You can log it, store it globally, or extract the message:
      console.error("Native JS error caught in Lua:", jsError);

      // Convert the error to a standard string
      const errorMessage = String(jsError);

      // 3. Push the formatted error message back onto the Lua stack
      // (We must convert the JS string to a Lua string using to_luastring)
      lua.lua_pushstring(
        l_state,
        fengari.to_luastring("JS Exception: " + errorMessage),
      );
    });

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
      throw new Error("Lua load error: " + fromLuaStringToJSString(sz));
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
      throw new Error("Lua load error: " + fromLuaStringToJSString(sz));
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
          throw new Error("Error in LuaRun: " + fromLuaStringToJSString(sz));
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
          throw new Error(
            "Error in LuaContinue: " + fromLuaStringToJSString(sz),
          );
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

        const dataPtr = this.host.malloc(bytes.length);
        if (!dataPtr) {
          throw new Error("Failed to allocate WASM memory for archive data");
        }

        const heap = this.wasmModule.HEAPU8;
        heap.set(bytes, dataPtr);

        const archivePtr = this.host.cacheDatArchiveDeserialize(
          dataPtr,
          bytes.length,
        );

        if (!archivePtr) {
          throw new Error("Failed to deserialize CacheDatArchive from buffer");
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
