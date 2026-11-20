-- java.lang for the Quest Helper port. Method names follow the transpiler's
-- overload rule (tools/questhelper_port/QhToLua.java, Names.method): a name
-- overloaded in its JDK class carries the erased parameter types.

local J, C = J, C

local function def(key, super, ifaces, body, kind)
  J.def(key, { kind = kind or "class", super = super or "java.lang.Object", ifaces = ifaces or {}, body = body })
end
local function iface(key, ifaces, body)
  def(key, "java.lang.Object", ifaces, body, "interface")
end

-- ------------------------------------------------------------------ Object

J.def("java.lang.Object", { kind = "class", ifaces = {}, body = function(cls)
  function cls.__zero(self) end
  function cls.__init(self) end
  function cls.__fields(self) end
  function cls.equals(self, o) return rawequal(self, o) end
  function cls.hashCode(self) return J.idhash(self) end
  function cls.toString(self)
    return J.simple_name(self.__key) .. "@" .. string.format("%x", J.idhash(self))
  end
  function cls.getClass(self) return J.classof(self.__key) end
  function cls.clone(self)
    local o = setmetatable({}, self.__cls)
    for k, v in pairs(self) do o[k] = v end
    return o
  end
  function cls.notify(self) end
  function cls.notifyAll(self) end
  function cls.wait(self) end
end })

function J.simple_name(key)
  if key == nil then return "null" end
  local s = key:match("([^%.]+)$") or key
  s = s:gsub("^.*%$", "")
  return s
end

-- ------------------------------------------------------------------ interfaces

iface("java.io.Serializable")
iface("java.lang.Cloneable")
iface("java.lang.AutoCloseable", {}, function(cls) function cls.close(self) end end)
iface("java.io.Closeable", { "java.lang.AutoCloseable" })
iface("java.lang.Comparable")
iface("java.lang.CharSequence", {}, function(cls)
  function cls.isEmpty(self) return self:length() == 0 end
end)
iface("java.lang.Runnable")
iface("java.lang.Iterable", {}, function(cls)
  function cls.forEach(self, consumer)
    for _, v in J.iter(self) do consumer:accept(v) end
  end
end)

-- ------------------------------------------------------------------ Class

def("java.lang.Class", nil, {}, function(cls)
  function cls.__new_for(key)
    local o = setmetatable({}, cls)
    o.k = key
    return o
  end
  function cls.getName(self) return self.k end
  function cls.getSimpleName(self) return J.simple_name(self.k) end
  function cls.getTypeName(self) return self.k end
  function cls.toString(self) return "class " .. self.k end
  function cls.isInstance(self, o) return o ~= nil and J.inst(o, self.k) end
  function cls.cast(self, o) return o end
  function cls.target(self) return C[self.k] end
  function cls.isEnum(self) return C[self.k].__kind == "enum" end
  function cls.getEnumConstants(self) return J.aclone(C[self.k].__values) end
  function cls.equals(self, o) return type(o) == "table" and o.k == self.k end
  function cls.hashCode(self) return string.hashCode(self.k) end
  function cls.desiredAssertionStatus(self) return false end
end)

-- ------------------------------------------------------------------ Enum

def("java.lang.Enum", nil, { "java.lang.Comparable", "java.io.Serializable" }, function(cls)
  function cls.__init(self) end
  function cls.name(self) return self.__name end
  function cls.ordinal(self) return self.__ordinal end
  function cls.toString(self) return self.__name end
  function cls.compareTo(self, o) return self.__ordinal - o.__ordinal end
  function cls.equals(self, o) return rawequal(self, o) end
  function cls.hashCode(self) return J.idhash(self) end
  function cls.getDeclaringClass(self) return J.classof(self.__key) end
  -- Enum.valueOf(Class, String)
  function cls.valueOf(klass, name)
    local vs = C[klass.k].__values
    for i = 1, vs.n do if vs[i].__name == name then return vs[i] end end
    J.throw(C["java.lang.IllegalArgumentException"].__new__String("No enum constant " .. klass.k .. "." .. tostring(name)))
  end
end)

-- ------------------------------------------------------------------ exceptions

def("java.lang.Throwable", nil, { "java.io.Serializable" }, function(cls)
  function cls.__init__(self) end
  function cls.__init__String(self, m) self.message = m end
  function cls.__init__String_Throwable(self, m, c) self.message = m; self.cause = c end
  function cls.__init__Throwable(self, c) self.cause = c; if c then self.message = c:toString() end end
  function cls.__init__String_Throwable_boolean_boolean(self, m, c) self.message = m; self.cause = c end
  function cls.getMessage(self) return self.message end
  function cls.getLocalizedMessage(self) return self.message end
  function cls.getCause(self) return self.cause end
  function cls.toString(self)
    local n = self.__key
    if self.message ~= nil then return n .. ": " .. self.message end
    return n
  end
  function cls.printStackTrace__(self) J.log("exception: " .. self:toString()) end
  function cls.getStackTrace(self) return J.arr({}, 0) end
  function cls.addSuppressed(self) end
end)

local EXCEPTIONS = {
  { "java.lang.Exception", "java.lang.Throwable" },
  { "java.lang.Error", "java.lang.Throwable" },
  { "java.lang.RuntimeException", "java.lang.Exception" },
  { "java.lang.IllegalArgumentException", "java.lang.RuntimeException" },
  { "java.lang.NumberFormatException", "java.lang.IllegalArgumentException" },
  { "java.lang.IllegalStateException", "java.lang.RuntimeException" },
  { "java.lang.NullPointerException", "java.lang.RuntimeException" },
  { "java.lang.UnsupportedOperationException", "java.lang.RuntimeException" },
  { "java.lang.IndexOutOfBoundsException", "java.lang.RuntimeException" },
  { "java.lang.ArrayIndexOutOfBoundsException", "java.lang.IndexOutOfBoundsException" },
  { "java.lang.StringIndexOutOfBoundsException", "java.lang.IndexOutOfBoundsException" },
  { "java.lang.ArithmeticException", "java.lang.RuntimeException" },
  { "java.lang.NegativeArraySizeException", "java.lang.RuntimeException" },
  { "java.lang.ClassCastException", "java.lang.RuntimeException" },
  { "java.lang.CloneNotSupportedException", "java.lang.Exception" },
  { "java.lang.InterruptedException", "java.lang.Exception" },
  { "java.lang.ReflectiveOperationException", "java.lang.Exception" },
  { "java.util.ConcurrentModificationException", "java.lang.RuntimeException" },
  { "java.util.NoSuchElementException", "java.lang.RuntimeException" },
  { "java.io.IOException", "java.lang.Exception" },
  { "java.util.concurrent.ExecutionException", "java.lang.Exception" },
  { "com.google.gson.JsonParseException", "java.lang.RuntimeException" },
  { "com.google.gson.JsonSyntaxException", "com.google.gson.JsonParseException" },
  { "com.google.inject.CreationException", "java.lang.RuntimeException" },
  { "com.google.inject.ProvisionException", "java.lang.RuntimeException" },
}
for _, e in ipairs(EXCEPTIONS) do def(e[1], e[2], {}, function(cls) end) end

-- ------------------------------------------------------------------ Number & boxes

def("java.lang.Number", nil, { "java.io.Serializable" }, function(cls) end)

local function cmp(a, b) if a < b then return -1 elseif a > b then return 1 else return 0 end end

local function parse_int(s, radix)
  radix = radix or 10
  if type(s) ~= "string" then J.throw(C["java.lang.NumberFormatException"].__new__String("null")) end
  local str = s
  local neg = false
  if str:sub(1, 1) == "-" then neg = true; str = str:sub(2) elseif str:sub(1, 1) == "+" then str = str:sub(2) end
  if str == "" then J.throw(C["java.lang.NumberFormatException"].__new__String('For input string: "' .. s .. '"')) end
  local v = 0
  for i = 1, #str do
    local c = str:byte(i)
    local d
    if c >= 48 and c <= 57 then d = c - 48
    elseif c >= 97 and c <= 122 then d = c - 87
    elseif c >= 65 and c <= 90 then d = c - 55
    else d = 99 end
    if d >= radix then J.throw(C["java.lang.NumberFormatException"].__new__String('For input string: "' .. s .. '"')) end
    v = v * radix + d
  end
  if neg then v = -v end
  return v
end
J.parse_int = parse_int

local function int_box(key, bits)
  def(key, "java.lang.Number", { "java.lang.Comparable" }, function(cls)
    cls.parseInt__String = parse_int
    cls.parseInt__String_int = parse_int
    cls.parseInt = parse_int
    cls.parseLong__String = parse_int
    cls.parseLong = parse_int
    cls.parseShort = parse_int
    cls.parseByte = parse_int
    function cls.valueOf__int(v) return v end
    function cls.valueOf__long(v) return v end
    function cls.valueOf__String(s) return parse_int(s) end
    function cls.valueOf(v) if type(v) == "string" then return parse_int(v) end return v end
    function cls.toString__int(v) return tostring(v) end
    function cls.toString__long(v) return tostring(v) end
    function cls.toString__int_int(v, r)
      if r == 10 then return tostring(v) end
      local neg = v < 0; if neg then v = -v end
      local digits = "0123456789abcdefghijklmnopqrstuvwxyz"
      local s = ""
      repeat local d = v % r; s = digits:sub(d + 1, d + 1) .. s; v = v // r until v == 0
      return (neg and "-" or "") .. s
    end
    function cls.toString(v) return tostring(v) end
    function cls.toString__(v) return tostring(v) end
    function cls.toHexString(v) return string.format("%x", v & 0xFFFFFFFF) end
    function cls.toBinaryString(v)
      v = v & 0xFFFFFFFF
      if v == 0 then return "0" end
      local s = ""
      while v > 0 do s = tostring(v & 1) .. s; v = v >> 1 end
      return s
    end
    function cls.intValue(v) return v end
    function cls.longValue(v) return v end
    function cls.shortValue(v) return v end
    function cls.byteValue(v) return v end
    function cls.doubleValue(v) return v + 0.0 end
    function cls.floatValue(v) return v + 0.0 end
    function cls.compare(a, b) return cmp(a, b) end
    function cls.compareTo(a, b) return cmp(a, b) end
    function cls.equals(a, b) return a == b end
    function cls.hashCode(v) return J.hash(v) end
    function cls.hashCode__int(v) return v end
    function cls.sum(a, b) return a + b end
    function cls.max(a, b) return math.max(a, b) end
    function cls.min(a, b) return math.min(a, b) end
    function cls.signum(v) return v > 0 and 1 or (v < 0 and -1 or 0) end
    function cls.bitCount(v)
      v = v & 0xFFFFFFFF
      local c = 0
      while v ~= 0 do c = c + (v & 1); v = v >> 1 end
      return c
    end
    function cls.abs(v) return math.abs(v) end
    function cls.getClass(v) return J.classof(key) end
  end)
end
int_box("java.lang.Integer")
int_box("java.lang.Long")
int_box("java.lang.Short")
int_box("java.lang.Byte")

local function parse_double(s)
  local v = type(s) == "string" and tonumber((s:gsub("^%s+", ""):gsub("%s+$", ""):gsub("[dDfF]$", ""))) or nil
  if v == nil then J.throw(C["java.lang.NumberFormatException"].__new__String('For input string: "' .. tostring(s) .. '"')) end
  return v + 0.0
end

local function float_box(key)
  def(key, "java.lang.Number", { "java.lang.Comparable" }, function(cls)
    cls.parseDouble = parse_double
    cls.parseFloat = parse_double
    function cls.valueOf__double(v) return v + 0.0 end
    function cls.valueOf__float(v) return v + 0.0 end
    function cls.valueOf__String(s) return parse_double(s) end
    function cls.valueOf(v) if type(v) == "string" then return parse_double(v) end return v + 0.0 end
    function cls.toString__double(v) return J.dstr(v) end
    function cls.toString__float(v) return J.dstr(v) end
    function cls.toString(v) return J.dstr(v) end
    function cls.toString__(v) return J.dstr(v) end
    function cls.intValue(v) return J.d2i(v) end
    function cls.longValue(v) return J.d2i(v) end
    function cls.doubleValue(v) return v + 0.0 end
    function cls.floatValue(v) return v + 0.0 end
    function cls.compare(a, b) return cmp(a, b) end
    function cls.compareTo(a, b) return cmp(a, b) end
    function cls.equals(a, b) return a == b end
    function cls.hashCode(v) return J.hash(v) end
    function cls.isNaN(v) return v ~= v end
    function cls.isInfinite(v) return v == math.huge or v == -math.huge end
    function cls.sum(a, b) return a + b end
    function cls.max(a, b) return math.max(a, b) end
    function cls.min(a, b) return math.min(a, b) end
    function cls.getClass(v) return J.classof(key) end
  end)
end
float_box("java.lang.Double")
float_box("java.lang.Float")

def("java.lang.Boolean", nil, { "java.lang.Comparable", "java.io.Serializable" }, function(cls)
  cls.TRUE = true
  cls.FALSE = false
  function cls.parseBoolean(s) return type(s) == "string" and s:lower() == "true" end
  function cls.valueOf__boolean(b) return b end
  function cls.valueOf__String(s) return type(s) == "string" and s:lower() == "true" end
  function cls.valueOf(b) if type(b) == "string" then return b:lower() == "true" end return b end
  function cls.toString__boolean(b) return tostring(b) end
  function cls.toString(b) return tostring(b) end
  function cls.toString__(b) return tostring(b) end
  function cls.booleanValue(b) return b end
  function cls.equals(a, b) return a == b end
  function cls.hashCode(b) return b and 1231 or 1237 end
  function cls.compare(a, b) if a == b then return 0 elseif a then return 1 else return -1 end end
  function cls.compareTo(a, b) return cls.compare(a, b) end
  function cls.logicalAnd(a, b) return a and b end
  function cls.logicalOr(a, b) return a or b end
  function cls.getClass(v) return J.classof("java.lang.Boolean") end
end)

local function isupper(c) return c >= 65 and c <= 90 end
local function islower(c) return c >= 97 and c <= 122 end
local function isdigit(c) return c >= 48 and c <= 57 end
local function isspace(c) return c == 32 or (c >= 9 and c <= 13) or c == 28 or c == 29 or c == 30 or c == 31 end

def("java.lang.Character", nil, { "java.lang.Comparable", "java.io.Serializable" }, function(cls)
  local function letter(c) return isupper(c) or islower(c) or c >= 0xC0 end
  cls.isDigit__char = isdigit; cls.isDigit__int = isdigit; cls.isDigit = isdigit
  cls.isLetter__char = letter; cls.isLetter__int = letter; cls.isLetter = letter
  local function lod(c) return letter(c) or isdigit(c) end
  cls.isLetterOrDigit__char = lod; cls.isLetterOrDigit__int = lod; cls.isLetterOrDigit = lod
  cls.isWhitespace__char = isspace; cls.isWhitespace__int = isspace; cls.isWhitespace = isspace
  cls.isSpaceChar__char = isspace; cls.isSpaceChar = isspace
  cls.isUpperCase__char = isupper; cls.isUpperCase__int = isupper; cls.isUpperCase = isupper
  cls.isLowerCase__char = islower; cls.isLowerCase__int = islower; cls.isLowerCase = islower
  local function up(c) if islower(c) then return c - 32 end return c end
  local function lo(c) if isupper(c) then return c + 32 end return c end
  cls.toUpperCase__char = up; cls.toUpperCase__int = up; cls.toUpperCase = up
  cls.toLowerCase__char = lo; cls.toLowerCase__int = lo; cls.toLowerCase = lo
  function cls.getNumericValue__char(c) if isdigit(c) then return c - 48 end if letter(c) then return lo(c) - 87 end return -1 end
  cls.getNumericValue = cls.getNumericValue__char
  function cls.valueOf(c) return c end
  function cls.charValue(c) return c end
  function cls.toString__char(c) return utf8.char(c) end
  function cls.toString(c) return utf8.char(c) end
  function cls.toString__(c) return utf8.char(c) end
  function cls.compare(a, b) return a - b end
  function cls.compareTo(a, b) return a - b end
  function cls.equals(a, b) return a == b end
  function cls.hashCode(c) return c end
  function cls.isAlphabetic(c) return letter(c) end
  function cls.toChars(c) return J.arr({ c }, 1) end
end)

-- ------------------------------------------------------------------ Math

def("java.lang.Math", nil, {}, function(cls)
  local m = math
  cls.abs__int = m.abs; cls.abs__long = m.abs; cls.abs__double = m.abs; cls.abs__float = m.abs; cls.abs = m.abs
  local function maxf(a, b) if a >= b then return a end return b end
  local function minf(a, b) if a <= b then return a end return b end
  cls.max__int_int = maxf; cls.max__long_long = maxf; cls.max__double_double = maxf; cls.max__float_float = maxf
  cls.min__int_int = minf; cls.min__long_long = minf; cls.min__double_double = minf; cls.min__float_float = minf
  function cls.floor(d) return m.floor(d) + 0.0 end
  function cls.ceil(d) return m.ceil(d) + 0.0 end
  function cls.sqrt(d) return m.sqrt(d) end
  function cls.cbrt(d) return d < 0 and -((-d) ^ (1 / 3)) or d ^ (1 / 3) end
  function cls.pow(a, b) return (a + 0.0) ^ b end
  cls.sin = m.sin; cls.cos = m.cos; cls.tan = m.tan
  cls.asin = m.asin; cls.acos = m.acos; cls.atan = function(x) return m.atan(x) end
  function cls.atan2(y, x) return m.atan(y, x) end
  function cls.exp(x) return m.exp(x) end
  function cls.log(x) return m.log(x) end
  function cls.log10(x) return m.log(x, 10) end
  function cls.hypot(a, b) return m.sqrt(a * a + b * b) end
  function cls.toRadians(d) return d * m.pi / 180 end
  function cls.toDegrees(r) return r * 180 / m.pi end
  function cls.signum__double(d) if d > 0 then return 1.0 elseif d < 0 then return -1.0 end return d end
  cls.signum = cls.signum__double
  function cls.round__double(d) return m.floor(d + 0.5) end
  function cls.round__float(d) return m.floor(d + 0.5) end
  cls.round = cls.round__double
  function cls.random() return J.random() end
  function cls.floorMod__int_int(a, b) return a % b end
  cls.floorMod__long_long = cls.floorMod__int_int; cls.floorMod__long_int = cls.floorMod__int_int
  function cls.floorDiv__int_int(a, b) return a // b end
  cls.floorDiv__long_long = cls.floorDiv__int_int
  function cls.clamp__long_int_int(v, lo, hi) return minf(maxf(v, lo), hi) end
  function cls.addExact__int_int(a, b) return a + b end
  function cls.toIntExact(a) return a end
  function cls.rint(d) local f = m.floor(d); if d - f > 0.5 or (d - f == 0.5 and f % 2 == 1) then f = f + 1 end return f + 0.0 end
end)

local rng = 0x2545F491
function J.random()
  rng = (rng * 6364136223846793005 + 1442695040888963407) & 0x7FFFFFFFFFFFFFFF
  return ((rng >> 11) & 0xFFFFFFFFFFFFF) / 4503599627370496.0
end

-- ------------------------------------------------------------------ System

J.now_ms = J.now_ms or function() return 0 end
J.log = J.log or function(...) end

def("java.lang.System", nil, {}, function(cls)
  function cls.currentTimeMillis() return J.now_ms() end
  function cls.nanoTime() return J.now_ms() * 1000000 end
  function cls.arraycopy(src, sp, dst, dp, n)
    if rawequal(src, dst) and sp < dp then
      for i = n - 1, 0, -1 do dst[dp + i + 1] = src[sp + i + 1] end
    else
      for i = 0, n - 1 do dst[dp + i + 1] = src[sp + i + 1] end
    end
  end
  function cls.lineSeparator() return "\n" end
  function cls.identityHashCode(o) return J.idhash(o) end
  function cls.getProperty() return nil end
  function cls.getenv() return nil end
end)

-- ------------------------------------------------------------------ String

-- Java's String.hashCode over UTF-16 units; for ASCII (all quest text that
-- reaches a HashMap key) the bytes are the units.
local function str_hash(s)
  local h = 0
  for i = 1, #s do h = (h * 31 + s:byte(i)) & 0xFFFFFFFF end
  if h >= 0x80000000 then h = h - 0x100000000 end
  return h
end

local function idx_error(i, n)
  J.throw(C["java.lang.StringIndexOutOfBoundsException"].__new__String("index " .. tostring(i) .. ", length " .. tostring(n)))
end

local S = {}
function S.length(s) return #s end
function S.isEmpty(s) return #s == 0 end
function S.isBlank(s) return s:find("^%s*$") ~= nil end
function S.charAt(s, i)
  if i < 0 or i >= #s then idx_error(i, #s) end
  return s:byte(i + 1)
end
S.codePointAt = S.charAt
function S.equals(s, o) return s == o end
function S.equalsIgnoreCase(s, o) return type(o) == "string" and s:lower() == o:lower() end
function S.hashCode(s) return str_hash(s) end
function S.toString(s) return s end
function S.toString__(s) return s end
function S.compareTo(s, o)
  local n = math.min(#s, #o)
  for i = 1, n do
    local a, b = s:byte(i), o:byte(i)
    if a ~= b then return a - b end
  end
  return #s - #o
end
function S.compareToIgnoreCase(s, o) return S.compareTo(s:lower(), o:lower()) end
function S.contains(s, t) return s:find(J.sval(t), 1, true) ~= nil end
function S.startsWith__String(s, p) return s:sub(1, #p) == p end
function S.startsWith__String_int(s, p, off) return s:sub(off + 1, off + #p) == p end
function S.endsWith(s, p) return #p == 0 or s:sub(-#p) == p end
local function index_of(s, t, from)
  from = from or 0
  if from < 0 then from = 0 end
  if type(t) == "number" then t = utf8.char(t) end
  local i = s:find(t, from + 1, true)
  return i and (i - 1) or -1
end
S.indexOf__String = function(s, t) return index_of(s, t, 0) end
S.indexOf__int = function(s, c) return index_of(s, c, 0) end
S.indexOf__String_int = index_of
S.indexOf__int_int = index_of
local function last_index_of(s, t, from)
  if type(t) == "number" then t = utf8.char(t) end
  from = from or #s
  local last = -1
  local i = 1
  while true do
    local j = s:find(t, i, true)
    if not j or j - 1 > from then break end
    last = j - 1
    i = j + 1
  end
  return last
end
S.lastIndexOf__String = function(s, t) return last_index_of(s, t) end
S.lastIndexOf__int = function(s, c) return last_index_of(s, c) end
S.lastIndexOf__String_int = last_index_of
S.lastIndexOf__int_int = last_index_of
function S.substring__int(s, b)
  if b < 0 or b > #s then idx_error(b, #s) end
  return s:sub(b + 1)
end
function S.substring__int_int(s, b, e)
  if b < 0 or e > #s or b > e then idx_error(b, #s) end
  return s:sub(b + 1, e)
end
S.subSequence = S.substring__int_int
function S.toLowerCase__(s) return s:lower() end
S.toLowerCase__Locale = S.toLowerCase__
function S.toUpperCase__(s) return s:upper() end
S.toUpperCase__Locale = S.toUpperCase__
-- Java trim() removes every char <= ' '
function S.trim(s) return (s:gsub("^[%z\1- ]+", ""):gsub("[%z\1- ]+$", "")) end
function S.strip(s) return (s:gsub("^%s+", ""):gsub("%s+$", "")) end
S.stripLeading = function(s) return (s:gsub("^%s+", "")) end
S.stripTrailing = function(s) return (s:gsub("%s+$", "")) end
local function plain_replace(s, a, b)
  if #a == 0 then
    local out = { b }
    for i = 1, #s do out[#out + 1] = s:sub(i, i); out[#out + 1] = b end
    return table.concat(out)
  end
  local out, i = {}, 1
  while true do
    local j, k = s:find(a, i, true)
    if not j then break end
    out[#out + 1] = s:sub(i, j - 1)
    out[#out + 1] = b
    i = k + 1
  end
  out[#out + 1] = s:sub(i)
  return table.concat(out)
end
function S.replace__CharSequence_CharSequence(s, a, b) return plain_replace(s, J.str(a), J.str(b)) end
function S.replace__char_char(s, a, b) return plain_replace(s, utf8.char(a), utf8.char(b)) end
function S.replaceAll(s, re, rep) return J.regex_replace(s, re, rep, true) end
function S.replaceFirst(s, re, rep) return J.regex_replace(s, re, rep, false) end
function S.matches(s, re) return J.regex_matches(s, re) end
function S.split__String(s, re) return J.regex_split(s, re, 0) end
function S.split__String_int(s, re, lim) return J.regex_split(s, re, lim) end
function S.concat(s, t) return s .. t end
function S.toCharArray(s)
  local t = { n = #s }
  for i = 1, #s do t[i] = s:byte(i) end
  return t
end
function S.chars(s)
  local t = { n = #s }
  for i = 1, #s do t[i] = s:byte(i) end
  return C["java.util.stream.IntStream"].__from(t)
end
function S.repeat_(s, n) return s:rep(n) end
function S.intern(s) return s end
function S.getClass(s) return J.classof("java.lang.String") end
function S.lines(s)
  local t = { n = 0 }
  for line in (s .. "\n"):gmatch("(.-)\r?\n") do t.n = t.n + 1; t[t.n] = line end
  if t.n > 0 and t[t.n] == "" and s:sub(-1) == "\n" then t[t.n] = nil; t.n = t.n - 1 end
  return C["java.util.stream.Stream"].__from(t)
end
function S.formatted(s, args) return J.format(s, args) end
function S.getBytes(s) return S.toCharArray(s) end
function S.describeConstable(s) return C["java.util.Optional"].of(s) end

-- Instance methods reach strings through their metatable (index = string).
for k, v in pairs(S) do string[k] = v end
string.hashCode = str_hash

def("java.lang.String", nil, { "java.lang.CharSequence", "java.lang.Comparable", "java.io.Serializable" }, function(cls)
  for k, v in pairs(S) do cls[k] = v end
  function cls.__init__(self) return "" end
  function cls.valueOf__Object(o) return J.str(o) end
  function cls.valueOf__int(v) return tostring(v) end
  function cls.valueOf__long(v) return tostring(v) end
  function cls.valueOf__char(c) return utf8.char(c) end
  function cls.valueOf__boolean(b) return tostring(b) end
  function cls.valueOf__double(d) return J.dstr(d) end
  function cls.valueOf__float(d) return J.dstr(d) end
  function cls.valueOf__charArr(a)
    local t = {}
    for i = 1, a.n do t[i] = utf8.char(a[i]) end
    return table.concat(t)
  end
  cls.copyValueOf__charArr = cls.valueOf__charArr
  function cls.format__String_ObjectArr(fmt, args) return J.format(fmt, args) end
  function cls.format__Locale_String_ObjectArr(_, fmt, args) return J.format(fmt, args) end
  function cls.join__CharSequence_CharSequenceArr(sep, parts)
    local t = {}
    for i = 1, parts.n do t[i] = J.str(parts[i]) end
    return table.concat(t, J.str(sep))
  end
  function cls.join__CharSequence_Iterable(sep, parts)
    local t = {}
    for _, v in J.iter(parts) do t[#t + 1] = J.str(v) end
    return table.concat(t, J.str(sep))
  end
  -- `new String(chars)` and friends: strings are values, so the
  -- constructors are plain functions returning one.
  cls.__new__ = function() return "" end
  cls.__new__String = function(s) return s end
  cls.__new__charArr = function(a) return cls.valueOf__charArr(a) end
  cls.__new__charArr_int_int = function(a, off, n)
    local t = {}
    for i = 1, n do t[i] = utf8.char(a[off + i]) end
    return table.concat(t)
  end
end)
J.def_clinit("java.lang.String", function(cls)
  cls.CASE_INSENSITIVE_ORDER = J.fn("java.util.Comparator", "compare", function(_, a, b) return S.compareTo(a:lower(), b:lower()) end)
end)

-- ------------------------------------------------------------------ StringBuilder

def("java.lang.AbstractStringBuilder", nil, { "java.lang.CharSequence" }, function(cls) end)

def("java.lang.StringBuilder", "java.lang.AbstractStringBuilder", { "java.lang.CharSequence", "java.io.Serializable" }, function(cls)
  function cls.__init__(self) self.parts = {}; self.len = 0 end
  function cls.__init__int(self) self.parts = {}; self.len = 0 end
  function cls.__init__String(self, s) self.parts = { s }; self.len = #s end
  function cls.__init__CharSequence(self, s) s = J.str(s); self.parts = { s }; self.len = #s end
  local function app(self, s)
    local p = self.parts
    p[#p + 1] = s
    self.len = self.len + #s
    if #p > 64 then self.parts = { table.concat(p) } end
    return self
  end
  local function flat(self)
    local p = self.parts
    if #p ~= 1 then
      local s = table.concat(p)
      self.parts = { s }
      return s
    end
    return p[1]
  end
  function cls.append__String(self, s) return app(self, J.sval(s)) end
  function cls.append__Object(self, o) return app(self, J.str(o)) end
  function cls.append__CharSequence(self, o) return app(self, J.str(o)) end
  function cls.append__int(self, v) return app(self, tostring(v)) end
  function cls.append__long(self, v) return app(self, tostring(v)) end
  function cls.append__char(self, c) return app(self, utf8.char(c)) end
  function cls.append__boolean(self, b) return app(self, tostring(b)) end
  function cls.append__double(self, d) return app(self, J.dstr(d)) end
  function cls.append__float(self, d) return app(self, J.dstr(d)) end
  function cls.append__StringBuffer(self, o) return app(self, J.str(o)) end
  function cls.append__charArr(self, a) return app(self, C["java.lang.String"].valueOf__charArr(a)) end
  function cls.toString(self) return flat(self) end
  function cls.length(self) return self.len end
  function cls.isEmpty(self) return self.len == 0 end
  function cls.charAt(self, i) return flat(self):byte(i + 1) end
  function cls.setLength(self, n)
    local s = flat(self)
    if n <= #s then s = s:sub(1, n) else s = s .. string.rep("\0", n - #s) end
    self.parts = { s }
    self.len = #s
  end
  function cls.insert__int_String(self, i, t)
    local s = flat(self)
    s = s:sub(1, i) .. J.sval(t) .. s:sub(i + 1)
    self.parts = { s }; self.len = #s
    return self
  end
  function cls.insert__int_char(self, i, c) return cls.insert__int_String(self, i, utf8.char(c)) end
  function cls.insert__int_int(self, i, v) return cls.insert__int_String(self, i, tostring(v)) end
  function cls.insert__int_Object(self, i, o) return cls.insert__int_String(self, i, J.str(o)) end
  function cls.reverse(self)
    local s = flat(self):reverse()
    self.parts = { s }
    return self
  end
  function cls.deleteCharAt(self, i)
    local s = flat(self)
    s = s:sub(1, i) .. s:sub(i + 2)
    self.parts = { s }; self.len = #s
    return self
  end
  function cls.delete(self, a, b)
    local s = flat(self)
    s = s:sub(1, a) .. s:sub(b + 1)
    self.parts = { s }; self.len = #s
    return self
  end
  function cls.setCharAt(self, i, c)
    local s = flat(self)
    s = s:sub(1, i) .. utf8.char(c) .. s:sub(i + 2)
    self.parts = { s }
  end
  function cls.indexOf__String(self, t) return S.indexOf__String(flat(self), t) end
  function cls.lastIndexOf__String(self, t) return S.lastIndexOf__String(flat(self), t) end
  function cls.substring__int(self, b) return S.substring__int(flat(self), b) end
  function cls.substring__int_int(self, b, e) return S.substring__int_int(flat(self), b, e) end
  function cls.replace(self, a, b, t)
    local s = flat(self)
    s = s:sub(1, a) .. t .. s:sub(b + 1)
    self.parts = { s }; self.len = #s
    return self
  end
end)
J.def("java.lang.StringBuffer", { kind = "class", super = "java.lang.StringBuilder", ifaces = {}, body = function(cls) end })

-- ------------------------------------------------------------------ String.format

local function group3(digits)
  local s = digits
  local out = ""
  while #s > 3 do
    out = "," .. s:sub(-3) .. out
    s = s:sub(1, -4)
  end
  return s .. out
end

function J.format(fmt, args)
  args = args or { n = 0 }
  local out = {}
  local ai = 0
  local i = 1
  local n = #fmt
  while i <= n do
    local c = fmt:sub(i, i)
    if c ~= "%" then
      local j = fmt:find("%", i, true) or (n + 1)
      out[#out + 1] = fmt:sub(i, j - 1)
      i = j
    else
      local spec_start = i
      i = i + 1
      local idx = fmt:match("^(%d+)%$", i)
      if idx then i = i + #idx + 1 end
      local flags = fmt:match("^[-#+ 0,(]*", i)
      i = i + #flags
      local width = fmt:match("^%d*", i)
      i = i + #width
      local prec = fmt:match("^%.(%d+)", i)
      if prec then i = i + #prec + 1 end
      local conv = fmt:sub(i, i)
      i = i + 1
      if conv == "%" then out[#out + 1] = "%"
      elseif conv == "n" then out[#out + 1] = "\n"
      else
        local arg
        if idx then arg = args[tonumber(idx)] else ai = ai + 1; arg = args[ai] end
        local s
        if conv == "s" or conv == "S" then
          s = J.str(arg)
          if prec then s = s:sub(1, tonumber(prec)) end
          if conv == "S" then s = s:upper() end
        elseif conv == "d" then
          local v = J.d2i(arg)
          local neg = v < 0
          s = tostring(math.abs(v))
          if flags:find(",", 1, true) then s = group3(s) end
          if neg then s = "-" .. s elseif flags:find("+", 1, true) then s = "+" .. s end
        elseif conv == "f" or conv == "e" or conv == "g" then
          local p = prec or "6"
          s = string.format("%." .. p .. conv, arg)
          if flags:find(",", 1, true) then
            local ip, fp = s:match("^(-?%d+)(.*)$")
            if ip then
              local neg = ip:sub(1, 1) == "-"
              ip = group3(neg and ip:sub(2) or ip)
              s = (neg and "-" or "") .. ip .. fp
            end
          end
        elseif conv == "x" or conv == "X" then
          s = string.format("%" .. conv, J.d2i(arg) & 0xFFFFFFFF)
        elseif conv == "c" then
          s = type(arg) == "number" and utf8.char(arg) or J.str(arg)
        elseif conv == "b" then
          s = tostring(arg ~= nil and arg ~= false)
        else
          s = fmt:sub(spec_start, i - 1)
        end
        local w = tonumber(width)
        if w and #s < w then
          if flags:find("-", 1, true) then s = s .. string.rep(" ", w - #s)
          elseif flags:find("0", 1, true) and conv ~= "s" then
            local sign = s:match("^[-+]") or ""
            s = sign .. string.rep("0", w - #s) .. s:sub(#sign + 1)
          else s = string.rep(" ", w - #s) .. s end
        end
        out[#out + 1] = s
      end
    end
  end
  return table.concat(out)
end
