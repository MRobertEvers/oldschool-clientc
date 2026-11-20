-- Third-party Java libraries the Quest Helper uses: Guava collections, Gson
-- (only Map<String, Boolean> and int[] ever cross it), commons-lang3,
-- slf4j, java.time.Instant, java.math.BigInteger. Guice lives with the
-- RuneLite layer (rt/rl_inject.lua) because the injector is the plugin's.

local J, C = J, C

local function def(key, super, ifaces, body, kind)
  J.def(key, { kind = kind or "class", super = super or "java.lang.Object", ifaces = ifaces or {}, body = body })
end

-- ------------------------------------------------------------------ Guava

local G = "com.google.common.collect."

local function list_from(...)
  local l = C["java.util.ArrayList"].__new__()
  local a = table.pack(...)
  for i = 1, a.n do l:add(a[i]) end
  return l
end

def(G .. "ImmutableCollection", "java.util.AbstractCollection", {}, function(cls) end)
def(G .. "ImmutableList", "java.util.ArrayList", {}, function(cls)
  local name = "of_"
  cls.of__ = list_from
  for _ = 1, 12 do
    name = name .. "_Object"
    cls[name:gsub("^of___", "of__")] = list_from
  end
  -- of(e1, ..., e11, E... others)
  cls["of__Object_Object_Object_Object_Object_Object_Object_Object_Object_Object_Object_Object_ObjectArr"] = function(...)
    local a = table.pack(...)
    local l = list_from(table.unpack(a, 1, a.n - 1))
    local rest = a[a.n]
    for i = 1, rest.n do l:add(rest[i]) end
    return l
  end
  function cls.copyOf__Collection(c) return C["java.util.ArrayList"].__new__Collection(c) end
  cls.copyOf__Iterable = cls.copyOf__Collection
  function cls.copyOf__ObjectArr(a) return C["java.util.Arrays"].asList(a) end
  function cls.builder() return C[G .. "ImmutableList$Builder"].__new__() end
end)
def(G .. "ImmutableCollection$Builder", nil, {}, function(cls)
  function cls.__init__(self) self.items = C["java.util.ArrayList"].__new__() end
  function cls.add__Object(self, v) self.items:add(v); return self end
  function cls.add__ObjectArr(self, a) for i = 1, a.n do self.items:add(a[i]) end return self end
  function cls.addAll__Iterable(self, it) for _, v in J.iter(it) do self.items:add(v) end return self end
  cls.addAll__Iterator = function(self, it) while it:hasNext() do self.items:add(it:next()) end return self end
  function cls.build(self) return C["java.util.ArrayList"].__new__Collection(self.items) end
end)
J.def(G .. "ImmutableList$Builder", { kind = "class", super = G .. "ImmutableCollection$Builder", ifaces = {} })
J.def(G .. "ImmutableSet$Builder", { kind = "class", super = G .. "ImmutableCollection$Builder", ifaces = {}, body = function(cls)
  function cls.build(self) return C["java.util.LinkedHashSet"].__new__Collection(self.items) end
end })
def(G .. "ImmutableSet", "java.util.LinkedHashSet", {}, function(cls)
  local function set_from(...)
    local s = C["java.util.LinkedHashSet"].__new__()
    local a = table.pack(...)
    for i = 1, a.n do s:add(a[i]) end
    return s
  end
  local name = "of_"
  cls.of__ = set_from
  for _ = 1, 12 do name = name .. "_Object"; cls[name:gsub("^of___", "of__")] = set_from end
  function cls.copyOf__Collection(c) return C["java.util.LinkedHashSet"].__new__Collection(c) end
  function cls.builder() return C[G .. "ImmutableSet$Builder"].__new__() end
end)
def(G .. "ImmutableMap", "java.util.LinkedHashMap", {}, function(cls)
  local function map_of(...)
    local m = C["java.util.LinkedHashMap"].__new__()
    local a = table.pack(...)
    for i = 1, a.n, 2 do m:put(a[i], a[i + 1]) end
    return m
  end
  cls.of__ = map_of
  local name = "of_"
  for _ = 1, 10 do name = name .. "_Object_Object"; cls[name:gsub("^of___", "of__")] = map_of end
  function cls.copyOf__Map(m) return C["java.util.LinkedHashMap"].__new__Map(m) end
  function cls.builder() return C[G .. "ImmutableMap$Builder"].__new__() end
end)
def(G .. "ImmutableMap$Builder", nil, {}, function(cls)
  function cls.__init__(self) self.m = C["java.util.LinkedHashMap"].__new__() end
  function cls.put__Object_Object(self, k, v) self.m:put(k, v); return self end
  function cls.put__Entry(self, e) self.m:put(e:getKey(), e:getValue()); return self end
  function cls.putAll__Map(self, m) self.m:putAll(m); return self end
  function cls.build(self) return C["java.util.LinkedHashMap"].__new__Map(self.m) end
  cls.buildOrThrow = cls.build
end)
def(G .. "Lists", nil, {}, function(cls)
  function cls.reverse(l)
    local r = C["java.util.ArrayList"].__new__()
    for i = l:size() - 1, 0, -1 do r:add(l:get(i)) end
    return r
  end
  function cls.newArrayList__() return C["java.util.ArrayList"].__new__() end
  function cls.newArrayList__ObjectArr(a) return C["java.util.Arrays"].asList(a) end
  function cls.newArrayList__Iterable(it) return C["java.util.ArrayList"].__new__Collection(it) end
end)
def(G .. "Sets", nil, {}, function(cls)
  function cls.immutableEnumSet__Enum_EnumArr(a, rest) return C["java.util.EnumSet"].of__Enum_EnumArr(a, rest) end
  function cls.immutableEnumSet__Iterable(it) local s = C["java.util.EnumSet"].__new__(); for _, v in J.iter(it) do s:add(v) end return s end
  function cls.newHashSet__() return C["java.util.HashSet"].__new__() end
end)

-- Multimap: a map of key -> collection.
J.def(G .. "Multimap", { kind = "interface", super = "java.lang.Object", ifaces = {} })
J.def(G .. "ListMultimap", { kind = "interface", super = "java.lang.Object", ifaces = { G .. "Multimap" } })
J.def(G .. "SetMultimap", { kind = "interface", super = "java.lang.Object", ifaces = { G .. "Multimap" } })
def(G .. "ArrayListMultimap", nil, { G .. "ListMultimap" }, function(cls)
  function cls.__make(new_coll)
    local o = setmetatable({}, cls)
    o.m = C["java.util.LinkedHashMap"].__new__()
    o.new_coll = new_coll
    return o
  end
  function cls.create__() return cls.__make(function() return C["java.util.ArrayList"].__new__() end) end
  function cls.get(self, k)
    local c = self.m:get(k)
    if c == nil then
      c = self.new_coll()
      -- Guava's get returns a live view that joins the map when written to;
      -- writing through put is what the plugin does, so the empty one is fine
      return c
    end
    return c
  end
  function cls.put(self, k, v)
    local c = self.m:get(k)
    if c == nil then c = self.new_coll(); self.m:put(k, c) end
    return c:add(v)
  end
  function cls.putAll__Object_Iterable(self, k, it) for _, v in J.iter(it) do cls.put(self, k, v) end return true end
  function cls.containsKey(self, k) return self.m:containsKey(k) end
  function cls.containsEntry(self, k, v) local c = self.m:get(k); return c ~= nil and c:contains(v) end
  function cls.remove(self, k, v)
    local c = self.m:get(k)
    if c == nil then return false end
    local r = c:remove(v)
    if c:isEmpty() then self.m:remove__Object(k) end
    return r
  end
  function cls.removeAll(self, k) local c = self.m:remove__Object(k); return c or self.new_coll() end
  function cls.clear(self) self.m:clear() end
  function cls.keySet(self) return self.m:keySet() end
  function cls.asMap(self) return self.m end
  function cls.isEmpty(self) return self.m:isEmpty() end
  function cls.size(self)
    local n = 0
    for _, c in J.iter(self.m:values()) do n = n + c:size() end
    return n
  end
  function cls.values(self)
    local l = C["java.util.ArrayList"].__new__()
    for _, c in J.iter(self.m:values()) do l:addAll(c) end
    return l
  end
  function cls.entries(self)
    local l = C["java.util.ArrayList"].__new__()
    for _, e in J.iter(self.m:entrySet()) do
      for _, v in J.iter(e:getValue()) do l:add(C["java.util.AbstractMap$SimpleEntry"].__new__Object_Object(e:getKey(), v)) end
    end
    return l
  end
  function cls.forEach(self, f)
    for _, e in J.iter(self.m:entrySet()) do
      for _, v in J.iter(e:getValue()) do f:accept(e:getKey(), v) end
    end
  end
end)
J.def(G .. "HashMultimap", { kind = "class", super = G .. "ArrayListMultimap", ifaces = { G .. "SetMultimap" }, body = function(cls, super)
  function cls.create__() return super.__make(function() return C["java.util.HashSet"].__new__() end) end
end })
J.def(G .. "LinkedHashMultimap", { kind = "class", super = G .. "ArrayListMultimap", ifaces = { G .. "SetMultimap" }, body = function(cls, super)
  function cls.create__() return super.__make(function() return C["java.util.LinkedHashSet"].__new__() end) end
end })
def(G .. "Multimaps", nil, {}, function(cls)
  function cls.unmodifiableMultimap__Multimap(m) return m end
  cls.unmodifiableListMultimap = cls.unmodifiableMultimap__Multimap
  cls.unmodifiableSetMultimap = cls.unmodifiableMultimap__Multimap
end)

def("com.google.common.primitives.Ints", nil, {}, function(cls)
  function cls.contains(a, v) for i = 1, a.n do if a[i] == v then return true end end return false end
  function cls.asList(a) return C["java.util.Arrays"].asList(a) end
  function cls.toArray(c) local t, n = J.to_lua_array(c); t.n = n; return t end
  function cls.max(a) local m = a[1]; for i = 2, a.n do if a[i] > m then m = a[i] end end return m end
  function cls.min(a) local m = a[1]; for i = 2, a.n do if a[i] < m then m = a[i] end end return m end
end)
def("com.google.common.primitives.Shorts", nil, {}, function(cls)
  function cls.toArray(c) local t, n = J.to_lua_array(c); t.n = n; return t end
  function cls.contains(a, v) for i = 1, a.n do if a[i] == v then return true end end return false end
end)

-- ------------------------------------------------------------------ JSON / Gson

local json = {}
J.json = json

local function esc(s)
  return (s:gsub('[%c"\\]', function(c)
    local m = { ['"'] = '\\"', ["\\"] = "\\\\", ["\n"] = "\\n", ["\r"] = "\\r", ["\t"] = "\\t", ["\b"] = "\\b", ["\f"] = "\\f" }
    return m[c] or string.format("\\u%04x", c:byte())
  end))
end

function json.encode(v)
  local t = type(v)
  if v == nil then return "null" end
  if t == "string" then return '"' .. esc(v) .. '"' end
  if t == "number" then
    if math.type(v) == "integer" then return tostring(v) end
    if v == math.floor(v) then return string.format("%.1f", v) end
    return string.format("%.17g", v)
  end
  if t == "boolean" then return tostring(v) end
  if t == "table" then
    if v.__key == nil and rawget(v, "n") ~= nil then
      local parts = {}
      for i = 1, v.n do parts[i] = json.encode(v[i]) end
      return "[" .. table.concat(parts, ",") .. "]"
    end
    if J.inst(v, "java.util.Map") then
      local parts = {}
      for _, e in J.iter(v:entrySet()) do parts[#parts + 1] = json.encode(J.str(e:getKey())) .. ":" .. json.encode(e:getValue()) end
      return "{" .. table.concat(parts, ",") .. "}"
    end
    if J.inst(v, "java.util.Collection") then
      local parts = {}
      for _, x in J.iter(v) do parts[#parts + 1] = json.encode(x) end
      return "[" .. table.concat(parts, ",") .. "]"
    end
    if v.__kind == "enum" or v.__name then return json.encode(v.__name) end
    -- a plain object: its own fields
    local parts = {}
    for k, x in pairs(v) do
      if type(k) == "string" and k:sub(1, 2) ~= "__" and type(x) ~= "function" then
        parts[#parts + 1] = json.encode(k) .. ":" .. json.encode(x)
      end
    end
    table.sort(parts)
    return "{" .. table.concat(parts, ",") .. "}"
  end
  return "null"
end

-- Decodes to Java shapes: objects -> LinkedHashMap, arrays -> Java arrays.
function json.decode(s)
  local pos = 1
  local function ws() pos = s:find("[^ \t\r\n]", pos) or (#s + 1) end
  local function err(m) J.throw(C["com.google.gson.JsonSyntaxException"].__new__String("json: " .. m .. " at " .. pos)) end
  local value
  local function str()
    pos = pos + 1
    local out = {}
    while true do
      local c = s:sub(pos, pos)
      if c == "" then err("unterminated string") end
      if c == '"' then pos = pos + 1; break end
      if c == "\\" then
        local e = s:sub(pos + 1, pos + 1)
        if e == "u" then
          out[#out + 1] = utf8.char(tonumber(s:sub(pos + 2, pos + 5), 16))
          pos = pos + 6
        else
          local m = { n = "\n", r = "\r", t = "\t", b = "\b", f = "\f" }
          out[#out + 1] = m[e] or e
          pos = pos + 2
        end
      else
        out[#out + 1] = c
        pos = pos + 1
      end
    end
    return table.concat(out)
  end
  value = function()
    ws()
    local c = s:sub(pos, pos)
    if c == "{" then
      pos = pos + 1
      local m = C["java.util.LinkedHashMap"].__new__()
      ws()
      if s:sub(pos, pos) == "}" then pos = pos + 1; return m end
      while true do
        ws()
        if s:sub(pos, pos) ~= '"' then err("expected key") end
        local k = str()
        ws()
        if s:sub(pos, pos) ~= ":" then err("expected :") end
        pos = pos + 1
        m:put(k, value())
        ws()
        local d = s:sub(pos, pos)
        pos = pos + 1
        if d == "}" then return m end
        if d ~= "," then err("expected ,") end
      end
    elseif c == "[" then
      pos = pos + 1
      local a = { n = 0 }
      ws()
      if s:sub(pos, pos) == "]" then pos = pos + 1; return a end
      while true do
        a.n = a.n + 1
        a[a.n] = value()
        ws()
        local d = s:sub(pos, pos)
        pos = pos + 1
        if d == "]" then return a end
        if d ~= "," then err("expected ,") end
      end
    elseif c == '"' then return str()
    elseif s:sub(pos, pos + 3) == "true" then pos = pos + 4; return true
    elseif s:sub(pos, pos + 4) == "false" then pos = pos + 5; return false
    elseif s:sub(pos, pos + 3) == "null" then pos = pos + 4; return nil
    else
      local num = s:match("^-?%d+%.?%d*[eE]?[-+]?%d*", pos)
      if not num or num == "" then err("unexpected " .. c) end
      pos = pos + #num
      return math.tointeger(tonumber(num)) or tonumber(num)
    end
  end
  local v = value()
  ws()
  return v
end

def("com.google.gson.Gson", nil, {}, function(cls)
  function cls.__init__(self) end
  function cls.toJson__Object(self, o) return json.encode(o) end
  -- Gson answers null for a null (or empty) string
  function cls.fromJson__String_Class(self, s, _)
    if s == nil or s == "" then return nil end
    return json.decode(s)
  end
  function cls.fromJson__String_Type(self, s, _)
    if s == nil or s == "" then return nil end
    return json.decode(s)
  end
  function cls.newBuilder(self) return C["com.google.gson.GsonBuilder"].__new__() end
end)
def("com.google.gson.GsonBuilder", nil, {}, function(cls)
  function cls.__init__(self) end
  function cls.create(self) return C["com.google.gson.Gson"].__new__() end
  function cls.setPrettyPrinting(self) return self end
end)
J.def("java.lang.reflect.Type", { kind = "interface", super = "java.lang.Object", ifaces = {} })
def("com.google.gson.reflect.TypeToken", nil, {}, function(cls)
  function cls.__init__(self) end
  function cls.getType(self) return self end
end)

-- ------------------------------------------------------------------ commons-lang3

def("org.apache.commons.lang3.StringUtils", nil, {}, function(cls)
  function cls.substringBetween__String_String_String(s, open, close)
    if s == nil or open == nil or close == nil then return nil end
    local a = s:find(open, 1, true)
    if not a then return nil end
    local b = s:find(close, a + #open, true)
    if not b then return nil end
    return s:sub(a + #open, b - 1)
  end
  function cls.substringBetween__String_String(s, tag) return cls.substringBetween__String_String_String(s, tag, tag) end
  function cls.isBlank(s) return s == nil or s:find("^%s*$") ~= nil end
  function cls.isEmpty(s) return s == nil or #s == 0 end
  function cls.isNotEmpty(s) return s ~= nil and #s > 0 end
  function cls.capitalize(s) if s == nil or s == "" then return s end return s:sub(1, 1):upper() .. s:sub(2) end
  function cls.join__ObjectArr_String(a, sep) local t = {}; for i = 1, a.n do t[i] = J.str(a[i]) end return table.concat(t, sep) end
  function cls.join__Iterable_String(it, sep) local t = {}; for _, v in J.iter(it) do t[#t + 1] = J.str(v) end return table.concat(t, sep) end
end)
def("org.apache.commons.lang3.tuple.Pair", nil, { "java.util.Map$Entry" }, function(cls)
  function cls.of(l, r) local o = setmetatable({}, cls); o.l = l; o.r = r; return o end
  function cls.getLeft(self) return self.l end
  function cls.getRight(self) return self.r end
  function cls.getKey(self) return self.l end
  function cls.getValue(self) return self.r end
  function cls.equals(self, o) return type(o) == "table" and o.__key == cls.__key and J.equals(self.l, o.l) and J.equals(self.r, o.r) end
  function cls.hashCode(self) return J.hash(self.l) ~ J.hash(self.r) end
  function cls.toString(self) return "(" .. J.str(self.l) .. "," .. J.str(self.r) .. ")" end
end)
J.def("org.apache.commons.lang3.tuple.ImmutablePair", { kind = "class", super = "org.apache.commons.lang3.tuple.Pair", ifaces = {} })

-- ------------------------------------------------------------------ slf4j

def("org.slf4j.Logger", nil, {}, function(cls)
  local function msg(fmt, args)
    local i = 0
    return (J.str(fmt):gsub("{}", function()
      i = i + 1
      return J.str(args[i])
    end))
  end
  local function at(level)
    return function(self, fmt, ...)
      local a = table.pack(...)
      if a.n == 1 and J.isarray(a[1]) then a = a[1] end
      J.log(level .. " " .. self.name .. ": " .. msg(fmt, a))
    end
  end
  for _, level in ipairs({ "debug", "info", "warn", "error", "trace" }) do
    local f = at(level)
    if level == "debug" or level == "trace" then f = function() end end
    cls[level .. "__String"] = f
    cls[level .. "__String_Object"] = f
    cls[level .. "__String_Object_Object"] = f
    cls[level .. "__String_ObjectArr"] = f
    cls[level .. "__String_Throwable"] = f
  end
  function cls.isDebugEnabled__(self) return false end
end)
def("org.slf4j.LoggerFactory", nil, {}, function(cls)
  function cls.getLogger__Class(k)
    local o = setmetatable({}, C["org.slf4j.Logger"])
    o.name = k and J.simple_name(k.k) or "?"
    return o
  end
  function cls.getLogger__String(n)
    local o = setmetatable({}, C["org.slf4j.Logger"])
    o.name = n
    return o
  end
end)

-- ------------------------------------------------------------------ java.time / java.math

def("java.time.Instant", nil, { "java.lang.Comparable" }, function(cls)
  local function mk(ms) local o = setmetatable({}, cls); o.ms = ms; return o end
  function cls.now__() return mk(J.now_ms()) end
  function cls.ofEpochMilli(ms) return mk(ms) end
  function cls.ofEpochSecond__long(s) return mk(s * 1000) end
  function cls.getEpochSecond(self) return self.ms // 1000 end
  function cls.toEpochMilli(self) return self.ms end
  function cls.isAfter(self, o) return self.ms > o.ms end
  function cls.isBefore(self, o) return self.ms < o.ms end
  function cls.plusMillis(self, d) return mk(self.ms + d) end
  function cls.plusSeconds(self, d) return mk(self.ms + d * 1000) end
  function cls.compareTo(self, o) return J.compare(self.ms, o.ms) end
  function cls.equals(self, o) return type(o) == "table" and o.ms == self.ms end
  function cls.hashCode(self) return J.hash(self.ms) end
end)
def("java.time.Duration", nil, {}, function(cls)
  local function mk(ms) local o = setmetatable({}, cls); o.ms = ms; return o end
  function cls.ofMillis(ms) return mk(ms) end
  function cls.ofSeconds__long(s) return mk(s * 1000) end
  function cls.ofMinutes(m) return mk(m * 60000) end
  function cls.between(a, b) return mk(b.ms - a.ms) end
  function cls.toMillis(self) return self.ms end
  function cls.getSeconds(self) return self.ms // 1000 end
end)

-- Integers here are 64-bit; every BigInteger the plugin builds fits.
def("java.math.BigInteger", "java.lang.Number", {}, function(cls)
  local function mk(v) local o = setmetatable({}, cls); o.v = v; return o end
  function cls.valueOf__long(v) return mk(v) end
  cls.valueOf = cls.valueOf__long
  function cls.__init__String(self, s) self.v = J.parse_int(s, 10) end
  function cls.__init__String_int(self, s, r) self.v = J.parse_int(s, r) end
  function cls.testBit(self, n) return (self.v >> n) & 1 == 1 end
  function cls.intValue(self) return J.l2i(self.v) end
  function cls.longValue(self) return self.v end
  function cls.toString(self) return tostring(self.v) end
  function cls.equals(self, o) return type(o) == "table" and o.v == self.v end
  function cls.hashCode(self) return J.hash(self.v) end
  function cls.compareTo(self, o) return J.compare(self.v, o.v) end
end)
