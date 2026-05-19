export class LibToriPlatformJSLuaHost {
  /**
   * @param {*} lua - Fengari lua instance
   * @param {*} wasmModule - Emscripten WASM module
   * @param {import('./LibToriPlatformEmscriptenJSAPI').LibToriPlatformEmscriptenJSAPI} emscriptenJSAPI
   */
  constructor(lua, wasmModule, emscriptenJSAPI) {
    this.lua = lua;
    this.wasmModule = wasmModule;
    this.emscriptenJSAPI = emscriptenJSAPI;
    this.ioserverUrl = "http://localhost:8080";
  }

  platformGetIOQueue(L) {
    const ioQueue = this.emscriptenJSAPI.luaHostGetIOQueue();
    this.lua.lua_pushlightuserdata(L, ioQueue);
    return 1;
  }

  platformLoadIO(L) {
    return this.lua.lua_yield(L, 0);
  }

  async handleIOQueue() {
    const count = this.emscriptenJSAPI.ioQueueGetCount();
    if (count === 0) {
      return;
    }

    for (let i = 0; i < count; i++) {
      const item = this.emscriptenJSAPI.ioQueueGetItemByIndex(i);
      if (!item) {
        continue;
      }

      const tableId = this.emscriptenJSAPI.ioRequestGetTableId(item);
      const archiveId = this.emscriptenJSAPI.ioRequestGetArchiveId(item);

      try {
        const url = `${this.ioserverUrl}/archive/${tableId}/${archiveId}`;
        const response = await fetch(url);

        if (!response.ok) {
          throw new Error(
            `Failed to fetch archive ${tableId}/${archiveId}: ${response.status}`,
          );
        }

        const arrayBuffer = await response.arrayBuffer();
        const bytes = new Uint8Array(arrayBuffer);

        const dataPtr = this.emscriptenJSAPI.jshostMalloc(bytes.length);
        if (!dataPtr) {
          throw new Error("Failed to allocate WASM memory for archive data");
        }

        const heap = this.wasmModule.HEAPU8;
        heap.set(bytes, dataPtr);

        const archivePtr =
          this.emscriptenJSAPI.cacheDatArchiveNewFromBuffer(
            tableId,
            archiveId,
            dataPtr,
            bytes.length,
          );

        if (!archivePtr) {
          throw new Error("Failed to create CacheDatArchive from buffer");
        }

        this.emscriptenJSAPI.ioQueueItemResolve(item, archivePtr);
      } catch (error) {
        console.error(
          `Error loading archive ${tableId}/${archiveId}:`,
          error,
        );
        throw error;
      }
    }
  }

  gameDat1ConfigFileFetch() {
    this.emscriptenJSAPI.scriptAPIDat1ConfigFileFetch();
  }

  gameDat1ConfigFileLoad() {
    this.emscriptenJSAPI.scriptAPIDat1ConfigFileLoad();
  }
}
