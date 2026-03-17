---
name: lua-multi-archive-load-archives
overview: Add a multi-archive LOAD_ARCHIVES Lua interface that returns a userdata array of CacheDatArchive objects, wired through both native lua_buildcachedat and the browser JS/datserver path with a multipart HTTP response.
todos:
  - id: wire-opcode-func
    content: Add FUNC_LOAD_ARCHIVES opcode in lua_cache_fnnos.h and luajs_sidecar_fnnos.js
    status: completed
  - id: native-multi-archive
    content: Implement LuaCSidecar_CachedatLoadArchives and native dispatcher wiring
    status: completed
  - id: http-multipart-endpoint
    content: Extend datserver.c with POST /api/load_archives returning multipart/form-data
    status: completed
  - id: js-multipart-fetch
    content: Update luajs_sidecar.js to call POST /api/load_archives and parse multipart into CacheDatArchive pointers
    status: completed
  - id: lua-helper-load-archives
    content: Add LOAD_ARCHIVES helper API to cachedat.lua and keep LOAD_ARCHIVE as thin wrapper
    status: completed
isProject: false
---

## Goal

Implement a new Lua interface `LOAD_ARCHIVES` that can request multiple archives (including from mixed table_ids) and returns a **userdata array of `CacheDatArchive\*`**, with full support in:

- Native side (`lua_buildcachedat`, sidecar/dispatch code)
- HTTP `datserver.c` (multipart response)
- Browser JS bridge (`luajs_sidecar.js`)
- Lua helper layer (`cachedat.lua`).

## High-level design

- **Lua API**: Add `LOAD_ARCHIVES` to `cachedat.lua` as a helper that takes a list of `(table_id, archive_id, flags)` tuples and returns an array of archive userdata.
- **Lua ↔ C opcode**: Introduce a new function number (e.g. `FUNC_LOAD_ARCHIVES`) alongside `FUNC_LOAD_ARCHIVE` in both C and JS fnno enums so Lua can yield a multi-archive request.
- **Native C implementation**: Implement a new `LuaCSidecar_CachedatLoadArchives` that accepts an array of triplets and returns a `LuaGameType` of kind `USERDATA_ARRAY` containing `CacheDatArchive` pointers, mirroring the existing single-archive loader but batched.
- **HTTP server**: Extend `datserver.c` with a new endpoint (e.g. `POST /api/load_archives`) that accepts a request specifying multiple `(table_id, archive_id, flags)` entries and returns a `multipart/form-data` response where each part is the current binary archive serialization for one archive.
- **Browser JS bridge**: Update `luajs_sidecar.js` to handle `FUNC_LOAD_ARCHIVES` by issuing a fetch to the new endpoint, parsing the multipart body into individual ArrayBuffers, deserializing each into `CacheDatArchive` in WASM, and returning a JS array of pointers back to Lua as userdata array.

## Detailed steps

### 1. Wire up new opcode / function number

- **Files**:
  - `[src/osrs/scripts/lua_cache_fnnos.h](src/osrs/scripts/lua_cache_fnnos.h)`
  - `[src/platforms/browser2/luajs_sidecar_fnnos.js](src/platforms/browser2/luajs_sidecar_fnnos.js)`
- **Changes**:
  - Add `FUNC_LOAD_ARCHIVES` to the `LuaCacheFunctionNo` enum in C and mirror it in the JS enum.
  - Ensure numbering does not conflict with existing values (place immediately after `FUNC_LOAD_ARCHIVE` for clarity).

### 2. Native sidecar: implement multi-archive loader

- **Files**:
  - `[src/osrs/lua_sidecar/luac_sidecar_cachedat.c](src/osrs/lua_sidecar/luac_sidecar_cachedat.c)`
  - `[src/platforms/platform_impl2_osx_sdl2.cpp](src/platforms/platform_impl2_osx_sdl2.cpp)` (or equivalent native dispatcher)
  - `[src/osrs/lua_sidecar/lua_gametypes.h](src/osrs/lua_sidecar/lua_gametypes.h)` (uses existing `LuaGameType_NewUserDataArray`)
- **Changes**:
  - Define a new function `LuaCSidecar_CachedatLoadArchives(struct CacheDat* cache_dat, struct LuaGameType* args)` that:
    - Interprets `args` as a flat list of integers: `count` followed by `count` triplets `(table_id, archive_id, flags)` **or** as a sequence of triplets with implicit `count` (pick one and document in a comment).
    - Allocates a C array `struct CacheDatArchive** archives` with `count` entries.
    - For each entry, calls the existing single-archive loader logic (the same branch that `LuaCSidecar_CachedatLoadArchive` uses) to get a `CacheDatArchive` or `NULL`.
    - Populates the array, leaving `NULL` for missing/404 archives if you want sparse results, or skips them if you want a dense array (document the behavior).
    - Wraps the array in a `LuaGameType` via `LuaGameType_NewUserDataArray(archives, count)` and returns it to Lua.
  - Update the native Lua async dispatcher (e.g. `on_lua_async_call` in `platform_impl2_osx_sdl2.cpp`) to handle `FUNC_LOAD_ARCHIVES` by calling this new function and returning its `LuaGameType` result.

### 3. HTTP server: new multipart multi-archive endpoint

- **File**:
  - `[test/datserver/datserver.c](test/datserver/datserver.c)` (or current path of `datserver.c`)
- **Changes**:
  - Add routing for a new endpoint, e.g. `POST /api/load_archives` (keeping `GET /api/load_archive` unchanged for backward compatibility).
  - Decide request format (simple and JS-friendly), for example:
    - `Content-Type: application/json` body: an array of `{ table_id, archive_id, flags }` objects.
  - Implement request parser:
    - For `POST /api/load_archives`, read the entire request body, parse JSON into a temporary C representation.
  - Implement multipart response builder:
    - Use `Content-Type: multipart/form-data; boundary=...`.
    - For each requested entry, attempt to load the archive with the existing `cache_dat_archive_new_load` / `gioqb_cache_dat_map` logic.
    - For found archives, serialize using the existing `archive_serialize_to_buffer` helper.
    - Emit each part as:
      - `--<boundary>\r\n`
      - `Content-Type: application/octet-stream\r\n`
      - Optionally include `Content-Disposition` or custom headers with `table_id`, `archive_id`, `flags` for easier parsing.
      - `\r\n<binary payload>\r\n`.
    - Finish with `--<boundary>--\r\n`.
  - Keep CORS and basic headers consistent with the single-archive path.

### 4. JS bridge: handle LOAD_ARCHIVES and parse multipart

- **File**:
  - `[src/platforms/browser2/luajs_sidecar.js](src/platforms/browser2/luajs_sidecar.js)`
- **Changes**:
  - Extend `handleYield(cmd, args)` to add a `case LuaCacheFunctionNo.FUNC_LOAD_ARCHIVES:` branch that expects `args` to encode multiple `(table_id, archive_id, flags)` entries.
    - Align args encoding with what you chose in step 2 (either `count` + flat triples, or array semantics driven from `cachedat.lua`).
    - Build a JSON payload from `args` and POST it to `/api/load_archives`.
  - Implement a small multipart parser in JS that:
    - Reads the full response as `ArrayBuffer`.
    - Scans for boundary sequences in the textual header portion.
    - Splits parts and extracts each part's body as an `ArrayBuffer`.
  - For each part/body:
    - Allocate memory in WASM (`_malloc`), copy bytes into `HEAPU8`, and call `_luajs_CacheDatArchive_deserialize(ptr, len)`.
    - Free the temporary buffer.
    - Collect the returned `CacheDatArchive` pointers into a JS array.
  - Return the array of pointers from `handleYield`, so the driver can push them into Lua as a **userdata array** using the existing `LuaGameType_NewUserDataArray` mechanism (ensure the WASM side is ready to interpret an array return correctly, or add a small shim if only scalar returns are currently supported).

### 5. Lua helper: expose LOAD_ARCHIVES in cachedat.lua

- **File**:
  - `[src/osrs/scripts/cachedat.lua](src/osrs/scripts/cachedat.lua)` (or wherever `cachedat.lua` lives)
- **Changes**:
  - Add a `LOAD_ARCHIVES` function with the desired API, for example:
    - `function LOAD_ARCHIVES(requests) ... end` with `requests` being a Lua array of `{ table_id, archive_id, flags }` tables.
  - Inside `LOAD_ARCHIVES`:
    - Flatten `requests` into the argument encoding expected by `FUNC_LOAD_ARCHIVES` (e.g. `count` followed by `count` triplets) and yield/dispatch it via the existing async mechanism used for `FUNC_LOAD_ARCHIVE`.
    - Receive the returned userdata array of archives and hand it back to callers as-is (or with light Lua wrapping if needed).
  - Optionally, keep `LOAD_ARCHIVE` around as a convenience wrapper that delegates to `LOAD_ARCHIVES` with a single request.

### 6. Integrate with lua_buildcachedat usage

- **File**:
  - `[src/osrs/lua_sidecar/lua_buildcachedat.c](src/osrs/lua_sidecar/lua_buildcachedat.c)`
- **Changes**:
  - No direct changes required for existing `buildcachedat`_ commands; they already accept single `CacheDatArchive`_ userdata values.
  - Update any higher-level Lua scripts that currently loop over `LOAD_ARCHIVE` to prefer `LOAD_ARCHIVES` for batching, but keep them compatible so both paths work.

### 7. Testing strategy

- **Unit / local checks**:
  - Add or adapt a small Lua script that calls `LOAD_ARCHIVES` with a mix of table_ids and verifies the returned array length and basic properties (e.g. non-null userdata, matching `archive_id`/`table_id` when inspected via helper C functions or debug prints).
  - For `datserver.c`, manually curl or write a small JS test that:
    - Posts a JSON array of requests to `/api/load_archives`.
    - Parses the multipart response, verifying that each part deserializes into a valid archive via `_luajs_CacheDatArchive_deserialize`.
- **End-to-end browser**:
  - In the browser build, run the Lua script that uses `LOAD_ARCHIVES`, ensure `luajs_sidecar.js` correctly fetches and deserializes multiple archives, and confirm `lua_buildcachedat`-
