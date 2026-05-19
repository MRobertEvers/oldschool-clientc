export class LibToriPlatformJSLuaHost {
  /**
   *
   * @param {*} lua
   * @param {import('./LibToriPlatformEmscriptenJSAPI').LibToriPlatformEmscripten_JSAPI} emscriptenJSAPI
   */
  constructor(lua, emscriptenJSAPI) {
    this.lua = lua;
    this.emscriptenJSAPI = emscriptenJSAPI;
  }

  platformLoadIO() {}

  platformGetIOQueue() {}

  gameDat1ConfigFileFetch() {}

  gameDat1ConfigFileLoad() {}
}
