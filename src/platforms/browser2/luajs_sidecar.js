import { LuaCacheFunctionNo } from "./luajs_sidecar_fnnos.js";
import { createLuaGameTypes, pushToLua, fromLua } from "./luajs_gametypes.js";
const { lua, lauxlib, lualib, to_luastring } = window.fengari;

function luaString(str) {
  if (str instanceof Uint8Array) {
    str = new TextDecoder().decode(str);
  }
  return str;
}

// function stringToWasm(instance, str) {
//   const { memory, malloc } = instance.exports;

//   // 1. Encode the JS string into a UTF-8 byte array
//   const encoder = new TextEncoder();
//   const encodedString = encoder.encode(str);

//   // 2. Allocate memory in Wasm for the string (+1 for null terminator)
//   const len = encodedString.length;
//   const ptr = malloc(len + 1);

//   // 3. Copy the bytes into Wasm's linear memory
//   const heap = new Uint8Array(memory.buffer, ptr, len + 1);
//   heap.set(encodedString);
//   heap[len] = 0; // Null terminator for C-style strings

//   return ptr; // Return the address (pointer) to pass to Wasm functions
// }

// 1. The Core Dispatcher (matches c_wasm_dispatcher)
function wasmDispatcher(L) {
  const func_name = lua.lua_tostring(L, lua.lua_upvalueindex(1));
  const ctx = lua.lua_touserdata(L, lua.lua_upvalueindex(2));
  const wasm = lua.lua_touserdata(L, lua.lua_upvalueindex(3));

  const nargs = lua.lua_gettop(L);
  const gt = ctx.luaGameTypes;
  const args = gt.newVarTypeArray(nargs + 1);
  if (!args) return lua.lua_error(L);

  const [strPtr, strLen] = gt.stringToWasm(luaString(func_name) ?? "");

  const funcNameElem = gt.newString(strPtr, strLen);
  if (funcNameElem) gt.varTypeArrayPush(args, funcNameElem);

  for (let i = 1; i <= nargs; i++) {
    const elem = fromLua(L, i, gt, ctx.wasm);
    if (elem) {
      gt.varTypeArrayPush(args, elem);
    }
  }

  const result = wasm._dispatch_lua_command(args);
  gt.free(args);

  if (result) {
    const decoded = gt.read(result);

    pushToLua(L, decoded);
    gt.free(result);
    return 1;
  }
  return 0;
}

// 2. The __index with Weak Caching (matches mt_index in luac_sidecar.c)
function mtIndex(L) {
  const ctx = lua.lua_touserdata(L, lua.lua_upvalueindex(1));
  const callback = lua.lua_touserdata(L, lua.lua_upvalueindex(2));

  if (!lua.lua_getmetatable(L, 1)) return 0;
  lua.lua_pushstring(L, "__cache");
  lua.lua_rawget(L, -2);
  const cacheIdx = lua.lua_gettop(L);

  lua.lua_pushvalue(L, 2);
  lua.lua_rawget(L, cacheIdx);
  if (!lua.lua_isnil(L, -1)) return 1;
  lua.lua_pop(L, 1);

  lua.lua_pushvalue(L, 2);
  lua.lua_pushlightuserdata(L, ctx);
  lua.lua_pushlightuserdata(L, callback);
  lua.lua_pushcclosure(L, wasmDispatcher, 3);

  lua.lua_pushvalue(L, 2);
  lua.lua_pushvalue(L, -2);
  lua.lua_rawset(L, cacheIdx);

  return 1;
}

function createWasmLuaBridge(L, ctx, callback) {
  lua.lua_newtable(L);

  lua.lua_newtable(L);
  lua.lua_pushstring(L, "__cache");
  lua.lua_newtable(L);
  lua.lua_newtable(L);
  lua.lua_pushstring(L, "v");
  lua.lua_setfield(L, -2, "__mode");
  lua.lua_setmetatable(L, -2);
  lua.lua_rawset(L, -3);

  lua.lua_pushlightuserdata(L, ctx);
  lua.lua_pushlightuserdata(L, callback ?? null);
  lua.lua_pushcclosure(L, mtIndex, 2);
  lua.lua_setfield(L, -2, "__index");

  lua.lua_setmetatable(L, -2);
  lua.lua_setglobal(L, "Game");
  return 1;
}

export class LuaJSSidecar {
  constructor(wasm, options = {}) {
    this.wasm = wasm;
    /** Base URL for fetching scripts when not in IndexedDB (e.g. 'http://localhost:8080/') */
    this.scriptBaseUrl = options.scriptBaseUrl ?? "";
    this.luaGameTypes = createLuaGameTypes(wasm);
    this.pushToLua = (value) => pushToLua(lua, this.L, value);
    this.L = lauxlib.luaL_newstate();
    lualib.luaL_openlibs(this.L);

    // Queue state
    this.queue = [];
    this.isProcessing = false;

    createWasmLuaBridge(this.L, this, this.wasm);

    // Fix: index 1 is the message
    lua.lua_register(this.L, to_luastring("_lua_log"), (L) => {
      const msg = lua.lua_tostring(L, 1);
      console.log(`[Lua] ${luaString(msg)}`);
      return 0;
    });

    // Preload cachedat.lua into package.preload so require("cachedat") works
    this._cachedatPreloaded = this._preloadCachedat();
  }

  /**
   * Registers a Lua module in package.preload so require(name) returns it.
   * Loader must run synchronously (no yield).
   */
  _registerModuleInPreload(name, source) {
    const L = this.L;
    const luaSource = to_luastring(source);
    const chunkName = to_luastring(`@${name}.lua`);

    lua.lua_getglobal(L, to_luastring("package"));
    lua.lua_getfield(L, -1, to_luastring("preload"));

    lua.lua_pushcfunction(L, (state) => {
      if (
        lauxlib.luaL_loadbuffer(
          state,
          luaSource,
          luaSource.length,
          chunkName,
        ) !== lua.LUA_OK
      ) {
        lua.lua_error(state);
        return 0;
      }
      if (lua.lua_pcall(state, 0, 1, 0) !== lua.LUA_OK) {
        lua.lua_error(state);
        return 0;
      }
      return 1;
    });

    lua.lua_setfield(L, -2, to_luastring(name));
    lua.lua_pop(L, 2);
  }

  /**
   * Fetches cachedat.lua (from IndexedDB or network) and puts it in package.preload
   * so require("cachedat") works. Resolves when done; safe to await before running scripts.
   */
  async _preloadCachedat() {
    try {
      const path = "cachedat.lua";
      const content = await this.fetchOrCached(path);
      const code =
        typeof content === "string"
          ? content
          : new TextDecoder().decode(content);
      this._registerModuleInPreload("cachedat", code);
      console.log("[LuaJSSidecar] cachedat.lua preloaded into package.preload");
    } catch (err) {
      console.warn("[LuaJSSidecar] failed to preload cachedat.lua:", err);
    }
  }

  // Entrance point: adds to queue and triggers processing
  enqueueScript(scriptPtr) {
    this.queue.push(scriptPtr);
    console.log(
      `Script queued: ${scriptPtr} (Queue size: ${this.queue.length})`,
    );

    if (!this.isProcessing) {
      this.processNext();
    }
  }

  async processNext() {
    if (this.queue.length === 0) {
      this.isProcessing = false;
      return;
    }

    this.isProcessing = true;
    const scriptPtr = this.queue.shift();

    try {
      await this.runScript(scriptPtr);
    } catch (err) {
      console.error(`Failed to execute script ${scriptPtr}:`, err);
    }

    // When one finishes, check for the next
    this.processNext();
  }

  /** Get script content from IndexedDB cache. Returns null if not found or on error. */
  _getFromCache(path) {
    return new Promise((resolve) => {
      const openRequest = indexedDB.open("RSClientScripts", 2);
      openRequest.onerror = () => resolve(null);
      openRequest.onsuccess = (event) => {
        const db = event.target.result;
        const tx = db.transaction(["cache"], "readonly");
        const store = tx.objectStore("cache");
        const request = store.get(path);
        request.onsuccess = () =>
          resolve(request.result ? request.result.content : null);
        request.onerror = () => resolve(null);
      };
    });
  }

  /** Write script content to IndexedDB cache. Best-effort; ignores errors. */
  _writeToCache(path, content) {
    return new Promise((resolve) => {
      const openRequest = indexedDB.open("RSClientScripts", 2);
      openRequest.onerror = () => resolve();
      openRequest.onsuccess = (event) => {
        const db = event.target.result;
        const tx = db.transaction(["cache"], "readwrite");
        const store = tx.objectStore("cache");
        store.put({ path, content });
        tx.oncomplete = () => resolve();
        tx.onerror = () => resolve();
      };
    });
  }

  /**
   * Load script: always try network first; use IndexedDB cache only if fetch fails.
   */
  async fetchOrCached(path) {
    const url = this.scriptBaseUrl
      ? this.scriptBaseUrl.replace(/\/?$/, "/") + path.replace(/^\//, "")
      : path;

    try {
      const res = await fetch(url);
      if (!res.ok) throw new Error(`Fetch failed: ${res.status}`);
      const content = await res.text();
      await this._writeToCache(path, content);
      return content;
    } catch (err) {
      const cached = await this._getFromCache(path);
      if (cached != null) return cached;
      throw err;
    }
  }

  async runScript(luaGameScriptPtr) {
    await this._cachedatPreloaded;

    // 1. Create thread and keep track of its position on main stack
    const co = lua.lua_newthread(this.L);
    const threadIdx = lua.lua_gettop(this.L);

    const scriptPathPtr =
      this.luaGameTypes.getLuaGameScriptName(luaGameScriptPtr);
    const scriptPath = this.luaGameTypes.wasmStringToJsString(scriptPathPtr);

    console.log("scriptPath", scriptPath);
    const scriptArgsPtr =
      this.luaGameTypes.getLuaGameScriptArgs(luaGameScriptPtr);
    const scriptArgs = this.luaGameTypes.read(scriptArgsPtr);

    const buffer = await this.fetchOrCached(scriptPath);
    let code;
    if (buffer instanceof ArrayBuffer) {
      const decoder = new TextDecoder();
      code = decoder.decode(buffer);
    } else if (typeof buffer === "string") {
      code = buffer;
    }

    if (lauxlib.luaL_loadstring(co, to_luastring(code)) !== lua.LUA_OK) {
      const err = lua.lua_tostring(co, -1);
      lua.lua_remove(this.L, threadIdx); // Cleanup
      throw new Error(luaString(err));
    }

    await this.drive(co, scriptArgs);

    this.luaGameTypes.freeLuaGameScript(luaGameScriptPtr);

    // 2. Cleanup: Remove ONLY this thread from main stack
    lua.lua_remove(this.L, threadIdx);
  }

  async drive(co, scriptArgs) {
    let nres = 0;
    if (scriptArgs) {
      nres = pushToLua(co, scriptArgs);
    }

    let q = [[co, nres]];

    let iters = 0;
    while (q.length > 0) {
      iters++;
      console.log("Iterations", iters);
      let element = q.shift();
      co = element[0];
      nres = element[1];

      console.log("nres", nres);
      const status = lua.lua_resume(co, this.L, nres);

      if (status === lua.LUA_YIELD) {
        const top = lua.lua_gettop(co);
        const cmd = lua.lua_tonumber(co, 1);
        let args = [];
        for (let i = 2; i <= top; i++) {
          lua.lua_pushvalue(co, i);
          if (lua.lua_type(co, -1) === lua.LUA_TNUMBER) {
            let num = lua.lua_tointeger(co, -1);
            args.push(num);
          } else if (lua.lua_type(co, -1) === lua.LUA_TSTRING) {
            let str = lua.lua_tostring(co, -1);
            args.push(luaString(str));
          }
          lua.lua_pop(co, 1);
        }

        const results = await this.handleYield(cmd, args);
        lua.lua_settop(co, 0);
        results.forEach((res) => {
          lua.lua_pushlightuserdata(co, res);
        });

        q.push([co, results.length]);
      }

      if (status !== lua.LUA_OK && status !== lua.LUA_YIELD) {
        console.error(
          "Lua execution error:",
          luaString(lua.lua_tostring(co, -1)),
          status,
        );
      }
    }
  }

  async handleYield(cmd, args) {
    switch (cmd) {
      case LuaCacheFunctionNo.FUNC_LOAD_ARCHIVE: {
        const searchParams = new URLSearchParams();
        searchParams.append("epoch", "base");
        searchParams.append("table_id", args[0]);
        searchParams.append("archive_id", args[1]);
        searchParams.append("flags", args[2]);
        const response = await fetch(
          `http://localhost:8096/api/load_archive?${searchParams.toString()}`,
        );
        if (!response.ok) {
          return [];
        }

        const data = await response.arrayBuffer();

        // Copy the ArrayBuffer to WASM memory and then deserialize
        const deserialize = this.wasm._luajs_CacheDatArchive_deserialize;
        const heapu8 = new Uint8Array(this.wasm.HEAPU8.buffer);
        const dataView = new Uint8Array(data);
        const ptr = this.wasm._malloc(dataView.length);
        heapu8.set(dataView, ptr);
        const archive = deserialize(ptr, dataView.length);
        this.wasm._free(ptr);

        return [archive];
      }
      case LuaCacheFunctionNo.FUNC_LOAD_ARCHIVES: {
        const requests = [];
        for (let i = 0; i + 2 < args.length; i += 3) {
          requests.push({
            table_id: args[i],
            archive_id: args[i + 1],
            flags: args[i + 2],
          });
        }

        const response = await fetch(
          "http://localhost:8096/api/load_archives",
          {
            method: "POST",
            headers: {
              "Content-Type": "application/json",
            },
            body: JSON.stringify(requests),
          },
        );

        if (!response.ok) {
          return [];
        }

        const contentTypeRaw = response.headers.get("Content-Type") || "";
        const boundaryMatch = contentTypeRaw.match(/boundary=([^;,\s]+)/i);
        if (!boundaryMatch) {
          return [];
        }
        let boundary = boundaryMatch[1].trim();
        if (boundary.startsWith('"') && boundary.endsWith('"')) {
          boundary = boundary.slice(1, -1);
        }

        const raw = await response.arrayBuffer();
        const bytes = new Uint8Array(raw);

        const enc = new TextEncoder();
        const boundaryBytes = enc.encode(`--${boundary}`);
        const crlf = new Uint8Array([0x0d, 0x0a]);
        const headerSep = new Uint8Array([0x0d, 0x0a, 0x0d, 0x0a]);

        function indexOfBytes(haystack, needle, from) {
          for (let i = from; i <= haystack.length - needle.length; i++) {
            let j = 0;
            while (j < needle.length && haystack[i + j] === needle[j]) j++;
            if (j === needle.length) return i;
          }
          return -1;
        }

        const archives = [];
        let pos = indexOfBytes(bytes, boundaryBytes, 0);
        if (pos === -1) return [];

        while (true) {
          let lineEnd = indexOfBytes(bytes, crlf, pos);
          if (lineEnd === -1) break;
          const afterBoundary = lineEnd + crlf.length;
          const headerEnd = indexOfBytes(bytes, headerSep, afterBoundary);
          if (headerEnd === -1) break;
          const bodyStart = headerEnd + headerSep.length;
          const nextBoundary = indexOfBytes(bytes, boundaryBytes, bodyStart);
          if (nextBoundary === -1) break;
          /* Body ends at CRLF before next boundary; strip those 2 bytes */
          const bodyEnd =
            nextBoundary >= 2 &&
            bytes[nextBoundary - 2] === 0x0d &&
            bytes[nextBoundary - 1] === 0x0a
              ? nextBoundary - 2
              : nextBoundary;
          if (bodyEnd > bodyStart) {
            const bodyLen = bodyEnd - bodyStart;
            const deserialize = this.wasm._luajs_CacheDatArchive_deserialize;
            const heapu8 = new Uint8Array(this.wasm.HEAPU8.buffer);
            const ptr = this.wasm._malloc(bodyLen);
            heapu8.set(bytes.subarray(bodyStart, bodyEnd), ptr);
            const archive = deserialize(ptr, bodyLen);
            this.wasm._free(ptr);
            if (archive) archives.push(archive);
          }
          pos = nextBoundary;
        }

        return archives;
      }
      default: {
        console.error(`Unknown command: ${cmd}`);
        return [];
      }
    }
  }
}
