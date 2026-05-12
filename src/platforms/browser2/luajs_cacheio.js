/**
 * Browser-side handler for Lua cache I/O yields (DAT1 request lists).
 * Uses `luajs_cacheio.c` (`_luajs_LuaCacheIO_flatten_dat1_triplets`) plus datserver fetch and archive deserialize.
 */

/**
 * @param {Object} wasm - Emscripten module
 * @param {any[]} args - yield args; args[0] is lightuserdata `LuaCacheIORequestList*`
 * @param {(raw: ArrayBuffer|Uint8Array, contentTypeHeader: string) => { headers: string, body: Uint8Array }[]} parseMultipartPartsWithHeaders
 * @returns {Promise<any[]>} pointers to pass to `lua_resume` as lightuserdata results
 */
export async function handleCacheioLoadArchivesYield(
  wasm,
  args,
  parseMultipartPartsWithHeaders,
) {
  const listPtr = args[0];
  if (listPtr == null || listPtr === 0) {
    return [];
  }

  const countPtr = wasm._malloc(4);
  if (!countPtr) {
    return [];
  }

  try {
    const countView = new Int32Array(wasm.HEAPU8.buffer, countPtr, 1);
    countView[0] = 0;
    const flatTri = wasm._luajs_LuaCacheIO_flatten_dat1_triplets(
      listPtr,
      countPtr,
    );
    const tripletCount = countView[0];
    if (!flatTri || tripletCount <= 0) {
      return [];
    }

    const heapAll = new Int32Array(
      wasm.HEAPU8.buffer,
      flatTri,
      tripletCount * 3,
    );
    const requests = [];
    for (let i = 0; i < tripletCount; i++) {
      requests.push({
        table_id: heapAll[i * 3],
        archive_id: heapAll[i * 3 + 1],
        flags: heapAll[i * 3 + 2],
      });
    }
    wasm._free(flatTri);

    const response = await fetch(
      `http://${window.location.hostname}:8096/api/load_archives`,
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

    const raw = await response.arrayBuffer();
    const contentTypeRaw = response.headers.get("Content-Type") || "";
    const multipartParts = parseMultipartPartsWithHeaders(raw, contentTypeRaw);
    if (multipartParts.length === 0) {
      return [];
    }

    const deserialize = wasm._luajs_CacheDatArchive_deserialize;
    const archives = [];
    for (const { body } of multipartParts) {
      if (body.length === 0) continue;
      let ptr = wasm._malloc(body.length);
      if (!ptr) continue;
      try {
        const heapu8 = new Uint8Array(wasm.HEAPU8.buffer);
        heapu8.set(body, ptr);
        const archive = deserialize(ptr, body.length);
        wasm._free(ptr);
        ptr = 0;
        if (archive) archives.push(archive);
      } catch (error) {
        if (ptr) wasm._free(ptr);
        console.error(
          "Error deserializing cache dat archive (cacheio):",
          error,
          body,
        );
        return [];
      }
    }

    return archives;
  } finally {
    wasm._free(countPtr);
  }
}
