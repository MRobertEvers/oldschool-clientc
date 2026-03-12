"use strict";

// ─── Fengari API references ──────────────────────────────────────────────────
//
// Fengari exposes the raw Lua 5.3 C API as JavaScript functions.
// We use these directly instead of <script type="application/lua"> so we can
// manage coroutines manually and drive async operations from the outside.

let lua, lauxlib, lualib;

// Shorthand converters between JS strings and Lua's Uint8Array byte strings.
let to_ls; // JS string  → Uint8Array (for pushing into Lua)
let to_js; // Uint8Array → JS string  (for reading from Lua stack)

function initFengari() {
  ({ lua, lauxlib, lualib } = window.fengari);
  to_ls = (s) => window.fengari.to_luastring(s);
  to_js = (p) => (p != null ? window.fengari.to_jsstring(p) : "");
}

// ─── WASM / Emscripten module ────────────────────────────────────────────────

let wasmMod = null;

// Dynamically inject filestore.js so missing it doesn't break the page.
async function loadWasm() {
  return new Promise((resolve) => {
    const script = document.createElement("script");
    script.src = "filestore.js";
    script.onload = async () => {
      try {
        wasmMod = await window.createFilestoreModule();
        resolve(true);
      } catch (e) {
        console.warn("WASM init failed:", e);
        resolve(false);
      }
    };
    script.onerror = () => {
      console.warn(
        "filestore.js not found – run 'make' to compile the C module",
      );
      resolve(false);
    };
    document.head.appendChild(script);
  });
}

// Calls the C read_file() exported from the WASM module via ccall.
function wasmReadFile(path) {
  if (!wasmMod) return "(WASM not compiled – run 'make' first)";
  return wasmMod.ccall("read_file", "string", ["string"], [path]);
}

// ─── IndexedDB ───────────────────────────────────────────────────────────────

let idb = null;

async function openIndexedDB() {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open("lua-async-demo", 1);
    req.onupgradeneeded = (e) => {
      const db = e.target.result;
      if (!db.objectStoreNames.contains("files")) {
        db.createObjectStore("files", { keyPath: "path" });
      }
    };
    req.onsuccess = (e) => {
      idb = e.target.result;
      resolve(idb);
    };
    req.onerror = (e) => reject(e.target.error);
  });
}

async function idbWriteFile(path, content) {
  return new Promise((resolve, reject) => {
    const tx = idb.transaction("files", "readwrite");
    const store = tx.objectStore("files");
    const req = store.put({ path, content });
    req.onsuccess = () => resolve();
    req.onerror = (e) => reject(e.target.error);
  });
}

// Async file read – the key difference vs the WASM path.
async function idbReadFile(path) {
  return new Promise((resolve, reject) => {
    const tx = idb.transaction("files", "readonly");
    const store = tx.objectStore("files");
    const req = store.get(path);
    req.onsuccess = (e) => {
      const rec = e.target.result;
      resolve(rec ? rec.content : `(no entry for "${path}" in IndexedDB)`);
    };
    req.onerror = (e) => reject(e.target.error);
  });
}

// ─── Lua module preloader ────────────────────────────────────────────────────
//
// Registers a Lua module in package.preload so that require("name") works
// from any coroutine sharing this global state.
//
// Important: the loader CFunction may NOT yield – it must run synchronously.
// platform.lua satisfies this (it only defines functions; it doesn't call them).

function preloadModule(L, name, source) {
  const luaSource = to_ls(source);
  const chunkName = to_ls(`@${name}.lua`);

  lua.lua_getglobal(L, to_ls("package"));
  lua.lua_getfield(L, -1, to_ls("preload"));

  lua.lua_pushcfunction(L, (state) => {
    // Compile the module source into a function and push it.
    if (
      lauxlib.luaL_loadbuffer(state, luaSource, luaSource.length, chunkName) !==
      lua.LUA_OK
    ) {
      lua.lua_error(state); // propagates the compile error; never returns
      return 0;
    }
    // Execute the chunk (should return the module table).
    if (lua.lua_pcall(state, 0, 1, 0) !== lua.LUA_OK) {
      lua.lua_error(state); // propagates the runtime error; never returns
      return 0;
    }
    return 1; // one return value: the module
  });

  lua.lua_setfield(L, -2, to_ls(name)); // package.preload[name] = loader
  lua.lua_pop(L, 2); // pop package.preload, package
}

// ─── Coroutine driver ────────────────────────────────────────────────────────
//
// Drives a Lua coroutine in an async JS loop.
//
// When Lua yields ("read_file", path) or ("sleep", ms), JS performs the real
// async work and resumes the coroutine with the result.  From Lua's point of
// view, platform.read_file() looks like a blocking synchronous call.
//
// Stack discipline:
//   • After lua_resume returns LUA_YIELD, the yielded values are on co's stack.
//   • We read them, clear the stack with lua_settop(co,0), push the result,
//     and call lua_resume(co, L, nresult).

async function driveCoroutine(co, L, mode) {
  let nresume = 0; // values on co's stack to pass as results of the yield

  for (;;) {
    const status = lua.lua_resume(co, L, nresume);
    nresume = 0;

    if (status === lua.LUA_OK) {
      // Coroutine finished normally (won't happen with our infinite loop).
      break;
    }

    if (status !== lua.LUA_YIELD) {
      // A Lua error occurred.
      const n = lua.lua_gettop(co);
      const err = n > 0 ? to_js(lua.lua_tostring(co, -1)) : "unknown Lua error";
      throw new Error(err);
    }

    // ── Coroutine yielded – read the command ─────────────────────────────
    const n = lua.lua_gettop(co);
    const cmd = n >= 1 ? to_js(lua.lua_tostring(co, 1)) : null;
    const a2s = n >= 2 ? to_js(lua.lua_tostring(co, 2)) : null; // string arg
    const a2n = n >= 2 ? lua.lua_tonumber(co, 2) : 0; // numeric arg

    lua.lua_settop(co, 0); // clear all yielded values from the stack

    // ── Dispatch ─────────────────────────────────────────────────────────

    if (cmd === "read_file") {
      // WASM: synchronous C call; IndexedDB: async browser API.
      const content =
        mode === "wasm"
          ? wasmReadFile(a2s || "data.txt")
          : await idbReadFile(a2s || "data.txt");

      lua.lua_pushstring(co, to_ls(content));
      nresume = 1; // resume with the content as the return value of yield
    } else if (cmd === "sleep") {
      await new Promise((r) => setTimeout(r, a2n || 1000));
      // nresume stays 0 – platform.sleep() discards the resume value
    }
    // Unknown commands are silently resumed with nothing.
  }
}

// ─── Lua instance setup ──────────────────────────────────────────────────────
//
// Creates an independent Lua state for one mode (wasm or idb), preloads the
// platform module, loads core.lua into a new thread, and drives it.
//
// Two instances can run truly concurrently because driveCoroutine is async –
// it yields to the JS event loop on every await, letting the other instance
// make progress.

async function runLuaInstance({
  mode,
  instanceId,
  panelEl,
  platformSrc,
  coreSrc,
}) {
  // Fresh Lua state – no shared interpreter state between instances.
  const L = lauxlib.luaL_newstate();
  lualib.luaL_openlibs(L);

  // ── Register _lua_log as a synchronous C function ────────────────────────
  // platform.lua calls _lua_log(id, msg) for logging without yielding.
  lua.lua_pushcfunction(L, (state) => {
    const iid = to_js(lua.lua_tostring(state, 1));
    const msg = to_js(lua.lua_tostring(state, 2));
    appendLog(panelEl, iid, msg);
    return 0;
  });
  lua.lua_setglobal(L, to_ls("_lua_log"));

  // ── Set INSTANCE_ID global ───────────────────────────────────────────────
  lua.lua_pushstring(L, to_ls(instanceId));
  lua.lua_setglobal(L, to_ls("INSTANCE_ID"));

  // ── Preload platform module into this state's package.preload ────────────
  // The coroutine thread shares the global state, so require("platform")
  // from within the thread will find this preloaded module.
  preloadModule(L, "platform", platformSrc);

  // ── Create coroutine thread ──────────────────────────────────────────────
  // lua_newthread pushes the new thread onto L's stack.
  // We keep it there (never pop) so the Lua GC cannot collect it.
  lua.lua_newthread(L);
  const co = lua.lua_tothread(L, -1);

  // ── Load core.lua directly into the coroutine's stack ───────────────────
  // luaL_loadbuffer compiles the source and pushes the resulting function.
  // Loading into `co` (not L) puts the function on the thread's own stack,
  // ready for the first lua_resume(co, L, 0) call.
  const coreBytes = to_ls(coreSrc);
  if (
    lauxlib.luaL_loadbuffer(
      co,
      coreBytes,
      coreBytes.length,
      to_ls("@core.lua"),
    ) !== lua.LUA_OK
  ) {
    const err = to_js(lua.lua_tostring(co, -1));
    appendLog(panelEl, instanceId, `load error: ${err}`);
    return;
  }

  // ── Drive the coroutine ──────────────────────────────────────────────────
  try {
    await driveCoroutine(co, L, mode);
  } catch (e) {
    appendLog(panelEl, instanceId, `fatal: ${e.message}`);
  }
}

// ─── UI helpers ──────────────────────────────────────────────────────────────

function appendLog(panelEl, instanceId, msg) {
  const log = panelEl.querySelector(".log");
  const entry = document.createElement("div");
  entry.className = "entry";
  const ts = new Date().toLocaleTimeString([], {
    hour12: false,
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  });
  entry.textContent = `${ts}  ${msg}`;
  log.insertBefore(entry, log.firstChild);
  // Keep the log from growing unbounded.
  while (log.children.length > 50) log.removeChild(log.lastChild);
}

function setWasmStatus(ok) {
  const el = document.getElementById("wasm-status");
  if (!el) return;
  el.textContent = ok ? "WASM loaded ✓" : "WASM not compiled – run 'make'";
  el.dataset.ok = ok ? "true" : "false";
}

// ─── Bootstrap ───────────────────────────────────────────────────────────────

window.addEventListener("DOMContentLoaded", async () => {
  initFengari();

  // Fetch Lua sources, open IndexedDB, and attempt WASM load in parallel.
  const [[platformSrc, coreSrc], , wasmOk] = await Promise.all([
    Promise.all([
      fetch("platform.lua").then((r) => r.text()),
      fetch("core.lua").then((r) => r.text()),
    ]),
    openIndexedDB(),
    loadWasm(),
  ]);

  // Seed IndexedDB with a file that core.lua will read.
  await idbWriteFile(
    "data.txt",
    JSON.stringify({
      source: "IndexedDB",
      msg: "Async data from browser storage!",
      seededAt: new Date().toISOString(),
    }),
  );

  setWasmStatus(wasmOk);

  const panelWasm = document.getElementById("panel-wasm");
  const panelIdb = document.getElementById("panel-idb");

  // Start both instances concurrently.
  // No await – both async loops run interleaved via the JS event loop.
  runLuaInstance({
    mode: "wasm",
    instanceId: "wasm",
    panelEl: panelWasm,
    platformSrc,
    coreSrc,
  });

  runLuaInstance({
    mode: "idb",
    instanceId: "idb",
    panelEl: panelIdb,
    platformSrc,
    coreSrc,
  });
});
