// sidecar.js
const { lua, lauxlib, lualib, to_luastring } = window.fengari;

export class LuaSidecar {
  constructor(wasm) {
    this.wasm = wasm;
    this.L = lauxlib.luaL_newstate();
    lualib.luaL_openlibs(this.L);

    // Define global _lua_log for platform.lua
    lua.lua_register(this.L, to_luastring("_lua_log"), (L) => {
      const msg = lua.lua_tostring(L, 2);
      console.log(`[Lua] ${msg}`);
      return 0;
    });
  }

  async runScript(scriptPath) {
    const co = lua.lua_newthread(this.L);
    const response = await fetch(scriptPath);
    const code = await response.text();

    if (lauxlib.luaL_loadstring(co, to_luastring(code)) !== lua.LUA_OK) {
      throw new Error(lua.lua_tostring(co, -1));
    }

    return this.drive(co, 0);
  }

  async drive(co, nres) {
    const status = lua.lua_resume(co, this.L, nres);

    if (status === lua.LUA_YIELD) {
      const cmd = lua.lua_tostring(co, 1);
      const args = [];
      const top = lua.lua_gettop(co);
      for (let i = 2; i <= top; i++) {
        args.push(lua.lua_isnumber(co, i) ? lua.lua_tonumber(co, i) : null);
      }

      // Handle the Platform Request
      const results = await this.handleYield(cmd, args);

      // Push results back to coroutine stack for resume
      lua.lua_settop(co, 0);
      results.forEach((res) => {
        if (typeof res === "number") lua.lua_pushnumber(co, res);
        else lua.lua_pushlightuserdata(co, res);
      });

      return this.drive(co, results.length);
    }

    // Clean up thread from main stack
    lua.lua_settop(this.L, 0);
  }

  async handleYield(cmd, args) {
    switch (cmd) {
      case "cache_read":
        const [table, archive] = args;
        // Imagine an async fetch for the OSRS cache
        const data = await fetch(`./cache/${table}/${archive}.dat`).then((r) =>
          r.arrayBuffer(),
        );

        // ALLOCATE in WASM
        const ptr = this.wasm.exports.malloc(data.byteLength);

        // WRITE to WASM Memory
        const heap = new Uint8Array(this.wasm.exports.memory.buffer);
        heap.set(new Uint8Array(data), ptr);

        return [ptr, data.byteLength]; // Return pointer and size to Lua

      case "set_player_pos":
        const [x, y, z] = args;
        this.wasm.exports.update_entity_position(0, x, y, z);
        return [];

      case "sleep":
        await new Promise((r) => setTimeout(r, args[0]));
        return [];

      default:
        return [];
    }
  }
}
