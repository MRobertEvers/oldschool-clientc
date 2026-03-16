/**
 * JS wrapper for lua_gametypes WASM exports.
 * Structs are malloc'd in WASM; pointers are passed to JS.
 * Use this module to create, read, and free LuaGameType values.
 */
const { lua } = window.fengari;

/**
 * Push a LuaGameType (by ptr) onto the Lua stack. Same as gt.pushToLua(lua, L, ptr).
 * @param {Object} lua - Fengari lua namespace
 * @param {lua_State} L - Lua state
 * @param {number} ptr - Pointer to LuaGameType in WASM heap
 * @param {Object} gt - LuaGameTypes API from createLuaGameTypes(wasm)
 */
export function LuajsGameType_PushToLua(lua, L, ptr, gt) {
  gt.pushToLua(lua, L, ptr);
}

/** LuaGameTypeKind enum values (must match lua_gametypes.h) */
export const LuaGameTypeKind = {
  USERDATA: 0,
  USERDATA_ARRAY: 1,
  INT_ARRAY: 2,
  VARTYPE_ARRAY: 3,
  BOOL: 4,
  INT: 5,
  FLOAT: 6,
  STRING: 7,
  VOID: 8,
};

/**
 * @param {WebAssembly.Instance|Object} wasm - WASM instance with exports (HEAPU8, _malloc, _free, UTF8ToString, luajs_*)
 * @returns {Object} LuaGameTypes API
 */
export function createLuaGameTypes(wasm) {
  const heap = wasm.HEAPU8;
  const memory = heap?.buffer ?? wasm.memory?.buffer;
  const malloc = wasm._malloc ?? wasm.malloc;
  const free = wasm._free ?? wasm.free;
  const UTF8ToString =
    wasm.UTF8ToString ??
    ((ptr, len) => {
      if (ptr === 0) return null;
      const view = new Uint8Array(memory, ptr, len ?? 0);
      return new TextDecoder().decode(view);
    });

  const getExport = (name) => {
    const fn = wasm[`_${name}`] ?? wasm[name];
    if (!fn) throw new Error(`WASM export not found: ${name}`);
    return fn;
  };

  const newUserData = getExport("luajs_LuaGameType_NewUserData");
  const newUserDataArray = getExport("luajs_LuaGameType_NewUserDataArray");
  const newIntArray = getExport("luajs_LuaGameType_NewIntArray");
  const newVarTypeArray = getExport("luajs_LuaGameType_NewVarTypeArray");
  const varTypeArrayPush = getExport("luajs_LuaGameType_VarTypeArrayPush");
  const getVarTypeArrayCount = getExport(
    "luajs_LuaGameType_GetVarTypeArrayCount",
  );
  const getVarTypeArrayAt = getExport("luajs_LuaGameType_GetVarTypeArrayAt");
  const newBool = getExport("luajs_LuaGameType_NewBool");
  const newInt = getExport("luajs_LuaGameType_NewInt");
  const newFloat = getExport("luajs_LuaGameType_NewFloat");
  const newString = getExport("luajs_LuaGameType_NewString");
  const newVoid = getExport("luajs_LuaGameType_NewVoid");
  const freeType = getExport("luajs_LuaGameType_Free");
  const getKind = getExport("luajs_LuaGameType_GetKind");
  const getUserData = getExport("luajs_LuaGameType_GetUserData");
  const getUserDataArray = getExport("luajs_LuaGameType_GetUserDataArray");
  const getUserDataArrayCount = getExport(
    "luajs_LuaGameType_GetUserDataArrayCount",
  );
  const getIntArray = getExport("luajs_LuaGameType_GetIntArray");
  const getIntArrayCount = getExport("luajs_LuaGameType_GetIntArrayCount");
  const getBool = getExport("luajs_LuaGameType_GetBool");
  const getInt = getExport("luajs_LuaGameType_GetInt");
  const getFloat = getExport("luajs_LuaGameType_GetFloat");
  const getString = getExport("luajs_LuaGameType_GetString");
  const getStringLength = getExport("luajs_LuaGameType_GetStringLength");

  /**
   * Read a LuaGameType from WASM into a JS object. Caller must free the ptr when done.
   * @param {number} ptr - Pointer to LuaGameType in WASM heap
   * @returns {Object|null} Decoded value or null if ptr is 0
   */
  function read(ptr) {
    if (!ptr) return null;

    const kind = getKind(ptr);

    switch (kind) {
      case LuaGameTypeKind.USERDATA:
        return { kind: "userdata", value: getUserData(ptr) };
      case LuaGameTypeKind.USERDATA_ARRAY: {
        const dataPtr = getUserDataArray(ptr);
        const count = getUserDataArrayCount(ptr);
        const arr = [];
        const ptrSize = 4; // WASM32 pointers
        for (let i = 0; i < count; i++) {
          arr.push(new Uint32Array(memory, dataPtr + i * ptrSize, 1)[0]);
        }
        return { kind: "userdata_array", value: arr };
      }
      case LuaGameTypeKind.INT_ARRAY: {
        const dataPtr = getIntArray(ptr);
        const count = getIntArrayCount(ptr);
        const arr = new Int32Array(memory, dataPtr, count);
        return { kind: "int_array", value: Array.from(arr) };
      }
      case LuaGameTypeKind.VARTYPE_ARRAY: {
        const count = getVarTypeArrayCount(ptr);
        const arr = [];
        for (let i = 0; i < count; i++) {
          const elemPtr = getVarTypeArrayAt(ptr, i);
          arr.push(read(elemPtr));
        }
        return { kind: "var_type_array", value: arr };
      }
      case LuaGameTypeKind.BOOL:
        return { kind: "bool", value: !!getBool(ptr) };
      case LuaGameTypeKind.INT:
        return { kind: "int", value: getInt(ptr) };
      case LuaGameTypeKind.FLOAT:
        return { kind: "float", value: getFloat(ptr) };
      case LuaGameTypeKind.STRING: {
        const strPtr = getString(ptr);
        const len = getStringLength(ptr);
        const str = strPtr ? UTF8ToString(strPtr, len) : "";
        return { kind: "string", value: str ?? "" };
      }
      case LuaGameTypeKind.VOID:
        return { kind: "void", value: undefined };
      default:
        return { kind: "unknown", value: null };
    }
  }

  /**
   * Free a LuaGameType. Safe to call with 0.
   * @param {number} ptr - Pointer to LuaGameType
   */
  function freeTypeSafe(ptr) {
    if (ptr) freeType(ptr);
  }

  return {
    LuaGameTypeKind,

    newUserData: (userdata) => newUserData(userdata),
    newUserDataArray: (userdata, count) => newUserDataArray(userdata, count),
    newIntArray: (valuesPtr, count) => newIntArray(valuesPtr, count),
    newVarTypeArray: (hint) => newVarTypeArray(hint),
    varTypeArrayPush: (arrPtr, elemPtr) => varTypeArrayPush(arrPtr, elemPtr),
    newBool: (value) => newBool(value ? 1 : 0),
    newInt: (value) => newInt(value),
    newFloat: (value) => newFloat(value),
    newString: (strPtr, length) => newString(strPtr, length),
    newVoid: () => newVoid(),

    free: freeTypeSafe,
    read,

    /** Allocate and encode a string into WASM, return [ptr, length]. Caller must free ptr. */
    stringToWasm(str) {
      const encoder = new TextEncoder();
      const encoded = encoder.encode(str);
      const len = encoded.length;
      const ptr = malloc(len + 1);
      new Uint8Array(memory, ptr, len + 1).set(encoded);
      new Uint8Array(memory, ptr + len, 1)[0] = 0;
      return [ptr, len];
    },

    /** Allocate and encode an int array into WASM. Caller must free ptr. */
    intArrayToWasm(arr) {
      const ptr = malloc(arr.length * 4);
      new Int32Array(memory, ptr, arr.length).set(arr);
      return ptr;
    },
  };
}

export function pushToLua(L, obj) {
  if (!obj) {
    lua.lua_pushnil(L);
    return;
  }
  switch (obj.kind) {
    case "void":
      lua.lua_pushnil(L);
      break;
    case "bool":
      lua.lua_pushboolean(L, obj.value ? 1 : 0);
      break;
    case "int":
      lua.lua_pushinteger(L, obj.value);
      break;
    case "float":
      lua.lua_pushnumber(L, obj.value);
      break;
    case "string":
      lua.lua_pushstring(L, obj.value ?? "");
      break;
    case "userdata":
      lua.lua_pushlightuserdata(L, obj.value);
      break;
    case "userdata_array": {
      lua.lua_createtable(L, obj.value.length, 0);
      for (let i = 0; i < obj.value.length; i++) {
        lua.lua_pushlightuserdata(L, obj.value[i]);
        lua.lua_rawseti(L, -2, i + 1);
      }
      break;
    }
    case "int_array": {
      lua.lua_createtable(L, obj.value.length, 0);
      for (let i = 0; i < obj.value.length; i++) {
        lua.lua_pushinteger(L, obj.value[i]);
        lua.lua_rawseti(L, -2, i + 1);
      }
      break;
    }
    case "var_type_array": {
      lua.lua_createtable(L, obj.value.length, 0);
      for (let i = 0; i < obj.value.length; i++) {
        pushToLua(L, obj.value[i]);
        lua.lua_rawseti(L, -2, i + 1);
      }
      break;
    }
    default:
      lua.lua_pushnil(L);
  }
}

/**
 * JS equivalent of LuacGameType_FromLua(L, idx).
 * Reads the Lua value at stack index idx and returns a LuaGameType pointer (WASM).
 * Handles nil, boolean, number (int/float), string, lightuserdata, and tables
 * (int array, userdata array, or mixed VarTypeArray). Caller must gt.free(ptr).
 *
 * @param {lua_State} L - Lua state (or coroutine)
 * @param {number} idx - Stack index (1-based or negative)
 * @param {Object} gt - LuaGameTypes API from createLuaGameTypes(wasm)
 * @param {Object} wasm - WASM instance (for _free when allocating temp buffers)
 * @returns {number} LuaGameType pointer, or 0 on failure
 */
export function fromLua(L, idx, gt, wasm) {
  const absIdx = lua.lua_absindex
    ? lua.lua_absindex(L, idx)
    : idx > 0
      ? idx
      : lua.lua_gettop(L) + idx + 1;
  const t = lua.lua_type(L, absIdx);

  switch (t) {
    case lua.LUA_TNIL:
      return gt.newVoid();
    case lua.LUA_TBOOLEAN:
      return gt.newBool(lua.lua_toboolean(L, absIdx));
    case lua.LUA_TNUMBER: {
      const n = lua.lua_tonumber(L, absIdx);
      const i = Math.floor(n);
      if (i === n) return gt.newInt(i);
      return gt.newFloat(n);
    }
    case lua.LUA_TSTRING: {
      const [strPtr, strLen] = gt.stringToWasm(
        lua.lua_tostring(L, absIdx) ?? "",
      );
      const elem = gt.newString(strPtr, strLen);
      if (strPtr) wasm._free(strPtr);
      return elem;
    }
    case lua.LUA_TLIGHTUSERDATA:
      return gt.newUserData(lua.lua_touserdata(L, absIdx));
    case lua.LUA_TTABLE: {
      const n = lauxlib.luaL_len(L, absIdx);
      if (n <= 0) return gt.newVoid();

      let allInt = true;
      for (let i = 1; i <= n && allInt; i++) {
        lua.lua_rawgeti(L, absIdx, i);
        if (lua.lua_type(L, -1) !== lua.LUA_TNUMBER) allInt = false;
        lua.lua_pop(L, 1);
      }
      if (allInt) {
        const vals = [];
        for (let i = 1; i <= n; i++) {
          lua.lua_rawgeti(L, absIdx, i);
          vals.push(lua.lua_tointeger(L, -1));
          lua.lua_pop(L, 1);
        }
        const ptr = gt.intArrayToWasm(vals);
        const result = gt.newIntArray(ptr, n);
        if (ptr) wasm._free(ptr);
        return result;
      }

      let allUd = true;
      for (let i = 1; i <= n && allUd; i++) {
        lua.lua_rawgeti(L, absIdx, i);
        if (lua.lua_type(L, -1) !== lua.LUA_TLIGHTUSERDATA) allUd = false;
        lua.lua_pop(L, 1);
      }
      if (allUd) {
        const ptrSize = 4;
        const bufPtr = wasm._malloc(n * ptrSize);
        if (!bufPtr) return 0;
        const buffer = wasm.HEAPU8 ? wasm.HEAPU8.buffer : wasm.memory.buffer;
        const heap = new Uint32Array(buffer, bufPtr, n);
        for (let i = 1; i <= n; i++) {
          lua.lua_rawgeti(L, absIdx, i);
          heap[i - 1] = lua.lua_touserdata(L, -1);
          lua.lua_pop(L, 1);
        }
        const result = gt.newUserDataArray(bufPtr, n);
        wasm._free(bufPtr);
        return result;
      }

      const arr = gt.newVarTypeArray(n > 0 ? n : 4);
      if (!arr) return 0;
      for (let i = 1; i <= n; i++) {
        lua.lua_rawgeti(L, absIdx, i);
        const elem = LuajsGameType_FromLua(L, -1, gt, wasm);
        lua.lua_pop(L, 1);
        if (elem) {
          gt.varTypeArrayPush(arr, elem);
          gt.free(elem);
        }
      }
      return arr;
    }
    default:
      return gt.newVoid();
  }
}
