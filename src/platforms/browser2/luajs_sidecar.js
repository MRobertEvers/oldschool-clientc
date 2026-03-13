import { LuaBuildCacheFunctionNo } from "./luajs_sidecar_fnnos.js";
const { lua, lauxlib, lualib, to_luastring } = window.fengari;

function luaString(str) {
  if (str instanceof Uint8Array) {
    str = new TextDecoder().decode(str);
  }
  return str;
}

function stringToWasm(instance, str) {
  const { memory, malloc } = instance.exports;

  // 1. Encode the JS string into a UTF-8 byte array
  const encoder = new TextEncoder();
  const encodedString = encoder.encode(str);

  // 2. Allocate memory in Wasm for the string (+1 for null terminator)
  const len = encodedString.length;
  const ptr = malloc(len + 1);

  // 3. Copy the bytes into Wasm's linear memory
  const heap = new Uint8Array(memory.buffer, ptr, len + 1);
  heap.set(encodedString);
  heap[len] = 0; // Null terminator for C-style strings

  return ptr; // Return the address (pointer) to pass to Wasm functions
}

function freeStringWasm(instance, ptr) {
  const { free } = instance.exports;
  free(ptr);
}

export class LuaJSSidecar {
  constructor(wasm) {
    this.wasm = wasm;
    this.L = lauxlib.luaL_newstate();
    lualib.luaL_openlibs(this.L);

    // Queue state
    this.queue = [];
    this.isProcessing = false;

    // Fix: index 1 is the message
    lua.lua_register(this.L, to_luastring("_lua_log"), (L) => {
      const msg = lua.lua_tostring(L, 1);
      console.log(`[Lua] ${luaString(msg)}`);
      return 0;
    });
  }

  // Entrance point: adds to queue and triggers processing
  enqueueScript(scriptPath) {
    this.queue.push(scriptPath);
    console.log(
      `Script queued: ${scriptPath} (Queue size: ${this.queue.length})`,
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
    const scriptPath = this.queue.shift();

    try {
      await this.runScript(scriptPath);
    } catch (err) {
      console.error(`Failed to execute script ${scriptPath}:`, err);
    }

    // When one finishes, check for the next
    this.processNext();
  }

  async fetchOrCached(path) {
    return await new Promise((resolve, reject) => {
      const openRequest = indexedDB.open("RSClientScripts", 2);

      openRequest.onerror = () => reject("IDB Open Failed");

      openRequest.onsuccess = (event) => {
        const db = event.target.result;
        const transaction = db.transaction(["cache"], "readonly");
        const store = transaction.objectStore("cache");
        const request = store.get(path);

        request.onsuccess = () => {
          if (request.result) {
            // Assume request.result.data is an ArrayBuffer
            resolve(request.result.content);
          } else {
            fetch(path)
              .then((res) => {
                if (!res.ok) throw new Error("Fetch failed");
                return res.arrayBuffer();
              })
              .then((data) => {
                const writeTransaction = db.transaction(["cache"], "readwrite");
                const writeStore = writeTransaction.objectStore("cache");
                writeStore.put({ path, data }, path);
                resolve(data);
              })
              .catch(reject);
          }
        };
      };
    });
  }

  async runScript(scriptPath) {
    // 1. Create thread and keep track of its position on main stack
    const co = lua.lua_newthread(this.L);
    const threadIdx = lua.lua_gettop(this.L);

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

    await this.drive(co, 0);

    // 2. Cleanup: Remove ONLY this thread from main stack
    lua.lua_remove(this.L, threadIdx);
  }

  async drive(co, nres) {
    const status = lua.lua_resume(co, this.L, nres);

    if (status === lua.LUA_YIELD) {
      let cmd = luaString(lua.lua_tostring(co, 1));

      const args = [];
      const top = lua.lua_gettop(co);

      for (let i = 2; i <= top; i++) {
        if (lua.lua_isnumber(co, i)) args.push(lua.lua_tonumber(co, i));
        else if (lua.lua_isstring(co, i))
          args.push(luaString(lua.lua_tostring(co, i)));
        else args.push(null);
      }

      const results = await this.handleYield(cmd, args);

      // Clear yield results and push return values
      lua.lua_settop(co, 0);
      results.forEach((res) => {
        if (typeof res === "number") lua.lua_pushnumber(co, res);
        else lua.lua_pushlightuserdata(co, res);
      });

      return this.drive(co, results.length);
    }

    if (status !== lua.LUA_OK) {
      console.error(
        "Lua execution error:",
        luaString(lua.lua_tostring(co, -1)),
      );
    }
  }

  async handleYield(cmd, args) {
    switch (cmd) {
      case LuaBuildCacheFunctionNo.FUNC_LOAD_ARCHIVE: {
        const searchParams = new URLSearchParams();
        searchParams.append("epoch", "base");
        searchParams.append("table_id", args[0]);
        searchParams.append("archive_id", args[1]);
        const response = await fetch(
          `/api/load_archive?${searchParams.toString()}`,
        );
        if (!response.ok) {
          return [];
        }

        const data = await response.arrayBuffer();
      }
      default: {
        const argsPtr = this.wasm._malloc(args.length * 8);
        const view = new BigUint64Array(
          this.wasm.HEAPU8.buffer,
          argsPtr,
          args.length,
        );
        // Assuming 64-bit pointers/numbers, adjust as needed
        for (let i = 0; i < args.length; i++) {
          const arg = args[i];
          if (typeof arg === "number") {
            view[i] = BigInt(Math.floor(arg));
          } else if (typeof arg === "string") {
            const strPtr = stringToWasm(this.wasm, arg);
            view[i] = BigInt(strPtr);
          }
        }

        this.wasm._dispatch_lua_command(cmd, args.length, argsPtr);

        for (let i = 0; i < args.length; i++) {
          if (typeof args[i] === "string") {
            freeStringWasm(this.wasm, argsPtr[i]);
          }
        }

        this.wasm._free(argsPtr);

        return [];
      }
    }
  }
}
