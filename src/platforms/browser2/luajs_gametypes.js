/**
 * JS wrapper for lua_gametypes WASM exports.
 * Structs are malloc'd in WASM; pointers are passed to JS.
 * Use this module to create, read, and free LuaGameType values.
 */

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
    varTypeArrayPush: (arrPtr, elemPtr) =>
      varTypeArrayPush(arrPtr, elemPtr),
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
