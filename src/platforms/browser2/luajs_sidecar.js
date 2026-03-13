const { lua, lauxlib, lualib, to_luastring } = window.fengari;

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
      console.log(`[Lua] ${msg}`);
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
    const decoder = new TextDecoder();
    const code = decoder.decode(buffer);

    if (lauxlib.luaL_loadstring(co, to_luastring(code)) !== lua.LUA_OK) {
      const err = lua.lua_tostring(co, -1);
      lua.lua_remove(this.L, threadIdx); // Cleanup
      throw new Error(err);
    }

    await this.drive(co, 0);

    // 2. Cleanup: Remove ONLY this thread from main stack
    lua.lua_remove(this.L, threadIdx);
  }

  async drive(co, nres) {
    const status = lua.lua_resume(co, this.L, nres);

    if (status === lua.LUA_YIELD) {
      const cmd = lua.lua_tostring(co, 1);
      const args = [];
      const top = lua.lua_gettop(co);

      for (let i = 2; i <= top; i++) {
        if (lua.lua_isnumber(co, i)) args.push(lua.lua_tonumber(co, i));
        else if (lua.lua_isstring(co, i)) args.push(lua.lua_tostring(co, i));
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
      console.error("Lua execution error:", lua.lua_tostring(co, -1));
    }
  }

  async handleYield(cmd, args) {
    switch (cmd) {
      case "cache_read":
        const [table, archive] = args;
        const res = await fetch(`./cache/${table}/${archive}.dat`);
        const data = await res.arrayBuffer();

        const ptr = this.wasm.exports.malloc(data.byteLength);
        const heap = new Uint8Array(this.wasm.exports.memory.buffer);
        heap.set(new Uint8Array(data), ptr);

        return [ptr, data.byteLength];

      case "sleep":
        await new Promise((r) => setTimeout(r, args[0]));
        return [];

      default:
        return [];
    }
  }
}
