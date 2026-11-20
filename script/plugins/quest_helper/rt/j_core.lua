-- The Java object model the Quest Helper port runs on.
--
-- tools/questhelper_port/QhToLua.java writes Lua that calls into this file:
-- classes are registered with J.def and realised lazily through the global
-- registry C (C["com.questhelper.steps.NpcStep"]), objects are tables whose
-- metatable is their class, `null` is nil, arrays are {n = length, ...}.
-- tools/questhelper_port/README.md has the conventions.
--
-- Needs: setmetatable (the host's safe one), and J.protect -- a protected
-- call the host provides (api.core.protect) that never catches its own
-- budget errors. Both are installed by quest_helper.lua before anything
-- here runs.

J = J or {}
local J = J

local defs = {}          -- key -> definition, until realised
local missing_hook = nil -- (key) -> nil; asked when a key has no definition

J.NIL = setmetatable({}, { __name = "J.NIL" })

local function realize(key)
  local def = defs[key]
  if def == nil then
    if missing_hook then missing_hook(key) end
    def = defs[key]
    if def == nil then
      error("class not loaded: " .. tostring(key), 2)
    end
  end
  return J.realize_def(key, def, true)
end

C = setmetatable({}, {
  __index = function(_, key)
    return realize(key)
  end,
})
local C = C

function J.def(key, def)
  if rawget(C, key) ~= nil then
    -- a module loaded twice re-registers; the realised class stands
    return
  end
  defs[key] = def
end

function J.defined(key)
  return rawget(C, key) ~= nil or defs[key] ~= nil
end

function J.on_missing(fn)
  missing_hook = fn
end

local function make_new(cls, init)
  return function(...)
    local o = setmetatable({}, cls)
    cls.__zero(o)
    init(o, ...)
    return o
  end
end

local function is_function(v) return type(v) == "function" end

-- Interface default methods: copied into an implementing class only where
-- the class chain has nothing by that name (a superclass method wins).
local function mixin(cls, iface)
  for k, v in pairs(iface) do
    if type(k) == "string" and is_function(v) and k:sub(1, 2) ~= "__" and cls[k] == nil then
      cls[k] = v
    end
  end
end

local function finish_class(cls, def, super, ifaces)
  local sup = { [cls.__key] = true }
  if super then for k in pairs(super.__supers) do sup[k] = true end end
  for _, i in ipairs(ifaces) do for k in pairs(i.__supers) do sup[k] = true end end
  cls.__supers = sup
  if def.body then def.body(cls, super) end
  for _, i in ipairs(ifaces) do mixin(cls, i) end
  -- __new<sig> for every initialiser reachable from this class, so a
  -- runtime class that inherits its constructors (the exceptions) builds
  -- itself and not its superclass.
  local news = {}
  local level = cls
  while level do
    for k, v in pairs(level) do
      if type(k) == "string" and k:sub(1, 6) == "__init" and is_function(v) then
        local nk = "__new" .. k:sub(7)
        if news[nk] == nil then news[nk] = make_new(cls, cls[k]) end
      end
    end
    level = rawget(level, "__super")
  end
  for k, v in pairs(news) do cls[k] = v end
  if def.kind == "enum" then
    cls.values = function() return J.aclone(cls.__values) end
    local function value_of(name)
      local vs = cls.__values
      for i = 1, vs.n do if vs[i].__name == name then return vs[i] end end
      J.throw(C["java.lang.IllegalArgumentException"].__new__String("No enum constant " .. cls.__key .. "." .. tostring(name)))
    end
    cls.valueOf = value_of
    cls.valueOf__String = value_of
  end
end

function J.realize_def(key, def, register)
  local super = nil
  if def.super and key ~= "java.lang.Object" then super = C[def.super] end
  local cls = {}
  cls.__index = cls
  cls.__cls = cls
  cls.__key = key
  cls.__def = def
  cls.__super = super
  cls.__kind = def.kind or "class"
  cls.__outer = nil
  if super then setmetatable(cls, { __index = super }) end
  if register then
    rawset(C, key, cls)
    defs[key] = nil
  end
  local ifaces = {}
  for _, ik in ipairs(def.ifaces or {}) do ifaces[#ifaces + 1] = C[ik] end
  cls.__ifaces = ifaces
  finish_class(cls, def, super, ifaces)
  if def.clinit then def.clinit(cls) end
  return cls
end

function J.local_class(key, def)
  return J.realize_def(key, def, false)
end

function J.anon(base, iface_keys, body, init, outer, ...)
  local cls = { __anon = true }
  cls.__index = cls
  cls.__cls = cls
  cls.__key = base.__key
  cls.__super = base
  cls.__kind = "class"
  setmetatable(cls, { __index = base })
  local ifaces = {}
  for _, ik in ipairs(iface_keys) do ifaces[#ifaces + 1] = C[ik] end
  finish_class(cls, { body = body }, base, ifaces)
  local o = setmetatable({}, cls)
  cls.__zero(o)
  if init then
    if outer ~= nil then base[init](o, outer, ...) else base[init](o, ...) end
  end
  local f = rawget(cls, "__fields")
  if f then f(o) end
  return o
end

function J.enum_new(cls, name, ordinal, init, ...)
  local o = setmetatable({ __name = name, __ordinal = ordinal }, cls)
  cls.__zero(o)
  cls[init](o, ...)
  return o
end

function J.enum_new_anon(cls, name, ordinal, body, init, ...)
  local sub = { __anon = true }
  sub.__index = sub
  sub.__cls = sub
  sub.__key = cls.__key
  sub.__super = cls
  sub.__kind = "class"
  setmetatable(sub, { __index = cls })
  finish_class(sub, { body = body }, cls, {})
  sub.__supers[cls.__key] = true
  local o = setmetatable({ __name = name, __ordinal = ordinal }, sub)
  sub.__zero(o)
  cls[init](o, ...)
  local f = rawget(sub, "__fields")
  if f then f(o) end
  return o
end

-- External (RuneLite) enums, dumped by reflection. getters[lua_name] is a
-- list of per-constant values in constant order.
local enum_refs = {}
function J.ENUMREF(key, name)
  local r = { key = key, name = name }
  enum_refs[#enum_refs + 1] = r
  return r
end

function J.ext_enum(key, names, getters)
  J.def(key, { kind = "enum", super = "java.lang.Enum", ifaces = {}, body = function(cls, super)
    for gname, values in pairs(getters) do
      cls[gname] = function(self)
        local v = values[self.__ordinal + 1]
        if v == J.NIL then return nil end
        if type(v) == "table" and v.key and v.name and not v.__key then
          return C[v.key][v.name]
        end
        return v
      end
    end
  end, clinit = function(cls)
    local vs = { n = #names }
    for i, n in ipairs(names) do
      local o = setmetatable({ __name = n, __ordinal = i - 1 }, cls)
      cls[n] = o
      vs[i] = o
    end
    cls.__values = vs
  end })
end

-- Lambdas and method references: an object carrying the interface's single
-- abstract method, whose class is the interface (so its default methods and
-- Object's methods resolve).
function J.fn(iface_key, sam, f)
  return setmetatable({ [sam] = f }, C[iface_key])
end

-- `new SomeQuestHelper()` written in a framework module, whose class lives in
-- a quest module that is only loaded when needed. The proxy IS the object:
-- the first time anything is asked of it, the module must be loaded and the
-- proxy is turned into the instance in place, so its identity never changes.
local lazy = setmetatable({}, { __mode = "k" })
J.Pending = { __pending = true }

-- A FACADE (when the loader says the class extends a framework QuestHelper,
-- J.lazy_base below) lets a proxy answer what QuestHelperPlugin.instantiate,
-- QuestManager.updateQuestList and the quest list ask of every helper --
-- injection, setQuest/setConfig/..., getQuest, getState, isCompleted -- from
-- the framework base class WITHOUT loading the quest's module: fields go to a
-- side table, the whitelisted base methods run on the proxy, a field nobody
-- wrote reads nil. Anything else (an overridden or a quest-only method) loads
-- the module; the constructor then runs, and every field written through the
-- facade is put back over what it left (Java's order: the enum built the
-- helper long before anything was injected or set).
local FACADE_METHODS
FACADE_METHODS = {
  setInjector = true, setQuest = true, setConfig = true, setQuestHelperPlugin = true,
  getQuest = true, getState__Client = true, isCompleted = true, getVar__ = true,
  configure = true, getConfigManager = true, getQuestHelperPlugin = true, getConfig__ = true,
  getDisplayedQuestName = true, getPlayerFacingQuestName = true, hasQuestStateBecomeFinished = true,
  equals = true, hashCode = true, getClass = true,
  -- QuestFilter.SHOW_MEETS_REQS: the base method over the facade's harvested
  -- getGeneralRequirements (below), unless the quest overrides it
  clientMeetsRequirements = true,
}

-- The loader preloads a quest module whose class overrides one of these
-- (Shield of Arrav's isCompleted, Recipe for Disaster's getState): the list
-- reads them of every helper on every update.
J.FACADE_METHODS = FACADE_METHODS

-- Asked of EVERY helper at login (QuestManager.setupRequirements ->
-- initializeRequirements), which would load every quest module: on a facade
-- it is remembered instead, and run once the module is in -- the
-- requirements exist from the moment anything can read them.
local FACADE_DEFERRED = { initializeRequirements = true }

-- Asked of EVERY helper whenever the list updates or at login --
-- QuestManager.getAllItemRequirements (the "Check items" helper and the
-- background ground-item highlight), SkillFiltering (general requirements,
-- experience rewards), QuestFilter.SHOW_MEETS_REQS (clientMeetsRequirements).
-- Loading every quest module at once does not fit the plugin heap (~14 MB of
-- code alone, the cap is 16), so these are HARVESTED: the entry's residency
-- pass (quest_helper.lua) loads each quest module in turn, builds a SHADOW
-- instance (J.lazy_shadow: the constructor, the facade's fields, the quest's
-- own injections), runs its initializeRequirements, keeps what these five
-- answer (J.lazy_answer) and lets the shadow -- and, where the sandbox can
-- unload a module, the module's code -- go. The proxy then answers them from
-- the harvest; before its quest is harvested it answers null (RuneLite's
-- "lists none"), and the list is updated again once the pass is through.
local FACADE_NIL = {
  getItemRequirements = true, getItemRecommended = true,
  getGeneralRequirements = true, getGeneralRecommended = true, getExperienceRewards = true,
}
J.FACADE_HARVEST = FACADE_NIL

-- forced proxy -> the facade state it was forced from (J.lazy_unforce)
local forced_states = setmetatable({}, { __mode = "k" })

local function lazy_force(p)
  local st = lazy[p]
  if st == nil then return end
  local cls = J.try_class(st.key)
  if cls == nil then
    J.request_class(st.key)
    error(setmetatable({ pending = st.key }, J.PendingMT), 0)
  end
  lazy[p] = nil
  setmetatable(p, cls)
  cls.__zero(p)
  cls[st.init](p, table.unpack(st.args, 1, st.args.n))
  if st.fields then
    forced_states[p] = st
    for k, v in pairs(st.fields) do rawset(p, k, v) end
    if J.lazy_after_force then J.lazy_after_force(p) end
    local deferred = st.deferred
    st.deferred = {}
    for k in pairs(deferred) do p[k](p) end
  end
end
J.PendingMT = { __tostring = function(e) return "module pending for " .. tostring(e.pending) end }

-- Set by the loader: (key) -> the framework base class key a facade may stand
-- in for, and the set of that class's own overrides of framework methods (nil
-- when no facade applies).
function J.lazy_base(key) return nil end

local function facade_index(t, k, st)
  local v = st.fields[k]
  if v ~= nil then return v, true end
  local base = st.base
  if FACADE_DEFERRED[k] and not st.overrides[k] then
    return function(self) st.deferred[k] = true end, true
  end
  if FACADE_NIL[k] and not st.overrides[k] then
    -- the base method answers null; nothing to harvest
    return function(self) return nil end, true
  end
  if FACADE_NIL[k] then
    local a = st.answers and st.answers[k]
    if a == J.NIL then a = nil end
    return function(self) return a end, true
  end
  if k == "__cls" or k == "__supers" or k == "__kind" then return base[k], true end
  local m = base[k]
  if m == nil then
    -- a field nobody wrote, unless it is a method name the quest adds
    if type(k) == "string" and not st.overrides[k] then return nil, true end
    return nil, false
  end
  if type(m) == "function" and FACADE_METHODS[k] and not st.overrides[k] then return m, true end
  return nil, false
end

function J.lazy_new(key, newname, ...)
  local init = "__init" .. newname:sub(6)
  local p = {}
  local st = { key = key, init = init, args = table.pack(...) }
  lazy[p] = st
  local base_key, overrides = J.lazy_base(key)
  if base_key ~= nil then
    st.base = C[base_key]
    st.overrides = overrides or {}
    st.fields = {}
    st.deferred = {}
  end
  local mt = {}
  st.mt = mt
  mt.__index = function(t, k)
    local s = lazy[t]
    if s and s.fields then
      local v, answered = facade_index(t, k, s)
      if answered then return v end
    end
    lazy_force(t)
    return t[k]
  end
  mt.__newindex = function(t, k, v)
    local s = lazy[t]
    if s and s.fields then
      s.fields[k] = v
      return
    end
    lazy_force(t)
    t[k] = v
  end
  return setmetatable(p, mt)
end

function J.lazy_key(p)
  local st = lazy[p]
  return st and st.key or nil
end

function J.lazy_forced(p)
  return lazy[p] == nil
end

function J.force(p)
  lazy_force(p)
  return p
end

-- ------------------------------------------------------------------ residency
-- What the entry's residency pass (quest_helper.lua) works with: harvest a
-- facade's list answers from a throwaway instance, revert a forced helper
-- to a facade, and forget a module's classes so the module can be unloaded.

-- A facade proxy (not forced) whose quest class extends a framework QuestHelper.
function J.lazy_is_facade(p)
  local st = lazy[p]
  return st ~= nil and st.fields ~= nil
end

-- A fresh instance of a facade's quest class, built the way forcing builds
-- the proxy (the constructor, then every field written through the facade,
-- then the quest's own @Inject fields) but WITHOUT touching the proxy: what
-- it holds goes when the caller drops it. Raises J.PendingMT while the class's
-- module is not in (after asking for it).
function J.lazy_shadow(p)
  local st = lazy[p]
  assert(st, "J.lazy_shadow: not a lazy proxy")
  assert(st.fields, "J.lazy_shadow: not a facade")
  local cls = J.try_class(st.key)
  if cls == nil then
    J.request_class(st.key)
    error(setmetatable({ pending = st.key }, J.PendingMT), 0)
  end
  local o = setmetatable({}, cls)
  cls.__zero(o)
  cls[st.init](o, table.unpack(st.args, 1, st.args.n))
  for k, v in pairs(st.fields) do rawset(o, k, v) end
  if J.lazy_after_force then J.lazy_after_force(o) end
  return o
end

-- The facade's answer for one of J.FACADE_HARVEST's methods (value may be nil:
-- the quest lists none).
function J.lazy_answer(p, name, value)
  local st = lazy[p]
  assert(st, "J.lazy_answer: not a lazy proxy")
  assert(st.fields, "J.lazy_answer: not a facade")
  assert(FACADE_NIL[name], name)
  st.answers = st.answers or {}
  if value == nil then value = J.NIL end
  st.answers[name] = value
end

-- Whether every harvested method of a facade has an answer.
function J.lazy_harvested(p)
  local st = lazy[p]
  if st == nil or st.fields == nil then return false end
  local a = st.answers
  if a == nil then return false end
  for name in pairs(FACADE_NIL) do
    if a[name] == nil and st.overrides[name] then return false end
  end
  return true
end

-- Turn a forced helper back into its facade, in place (its identity --
-- QuestHelperQuest's helper, hash codes, map keys -- never changes): the
-- facade's fields take the instance's current values, everything else the
-- instance held is dropped, and initializeRequirements is deferred again
-- (RuneLite ran it at login; it runs again when the module is next forced).
-- The caller harvests first (J.lazy_answer after this) if it wants answers.
function J.lazy_unforce(p)
  local st = forced_states[p]
  assert(st, "J.lazy_unforce: not a forced facade")
  assert(lazy[p] == nil, "J.lazy_unforce: still a facade")
  for k in pairs(st.fields) do st.fields[k] = rawget(p, k) end
  local keys = {}
  for k in pairs(p) do keys[#keys + 1] = k end
  for _, k in ipairs(keys) do rawset(p, k, nil) end
  forced_states[p] = nil
  st.deferred = { initializeRequirements = true }
  st.answers = nil
  lazy[p] = st
  setmetatable(p, st.mt)
end

-- Forget a class: the registry entry, an unrealised definition and its Class
-- object. Instances keep working on the class table they have; the next
-- J.def of the key (a reloaded module) defines it afresh.
function J.undef(key)
  rawset(C, key, nil)
  defs[key] = nil
  J.forget_class_object(key)
end

-- Class lookup that does not raise: the module loader decides. Set by the
-- loader (rt/modules.lua).
function J.try_class(key)
  if rawget(C, key) ~= nil then return rawget(C, key) end
  if defs[key] ~= nil then return C[key] end
  return nil
end

function J.request_class(key) end

-- ------------------------------------------------------------------ identity

local idhashes = setmetatable({}, { __mode = "k" })
local idserial = 0x1F2E3D
function J.idhash(o)
  local h = idhashes[o]
  if h == nil then
    idserial = (idserial * 1103515245 + 12345) & 0x7FFFFFFF
    h = idserial
    idhashes[o] = h
  end
  return h
end

function J.isarray(x)
  return type(x) == "table" and x.__key == nil and rawget(x, "n") ~= nil
end

local STRING_SUPERS = {
  ["java.lang.String"] = true, ["java.lang.Object"] = true, ["java.lang.CharSequence"] = true,
  ["java.lang.Comparable"] = true, ["java.io.Serializable"] = true,
}
local INT_SUPERS = {
  ["java.lang.Integer"] = true, ["java.lang.Long"] = true, ["java.lang.Short"] = true, ["java.lang.Byte"] = true,
  ["java.lang.Number"] = true, ["java.lang.Object"] = true, ["java.lang.Comparable"] = true,
  ["java.io.Serializable"] = true, ["java.lang.Character"] = true,
}
local FLOAT_SUPERS = {
  ["java.lang.Double"] = true, ["java.lang.Float"] = true, ["java.lang.Number"] = true,
  ["java.lang.Object"] = true, ["java.lang.Comparable"] = true, ["java.io.Serializable"] = true,
}
local BOOL_SUPERS = {
  ["java.lang.Boolean"] = true, ["java.lang.Object"] = true, ["java.lang.Comparable"] = true,
  ["java.io.Serializable"] = true,
}

function J.inst(x, key)
  local t = type(x)
  if t == "table" then
    local st = lazy[x]
    if st then
      -- a facade knows its framework ancestry without the module
      if st.base and st.base.__supers[key] then return true end
      lazy_force(x)
    end
    local s = x.__supers
    if s == nil then return key == "java.lang.Object" end
    return s[key] == true
  elseif t == "string" then return STRING_SUPERS[key] == true
  elseif t == "number" then
    if math.type(x) == "integer" then return INT_SUPERS[key] == true end
    return FLOAT_SUPERS[key] == true
  elseif t == "boolean" then return BOOL_SUPERS[key] == true
  end
  return false
end

function J.classname(o)
  local t = type(o)
  if t == "string" then return "java.lang.String" end
  if t == "number" then return math.type(o) == "integer" and "java.lang.Integer" or "java.lang.Double" end
  if t == "boolean" then return "java.lang.Boolean" end
  if t == "table" then return o.__key or "Array" end
  return "null"
end

-- ------------------------------------------------------------------ dispatch

local ARRAY_METHODS = {}
function ARRAY_METHODS.equals(a, b) return rawequal(a, b) end
function ARRAY_METHODS.hashCode(a) return J.idhash(a) end
function ARRAY_METHODS.toString(a) return "[L@" .. string.format("%x", J.idhash(a)) end
function ARRAY_METHODS.clone(a) return J.aclone(a) end
function ARRAY_METHODS.getClass(a) return J.classof("Array") end

function J.vcall(recv, name, ...)
  local t = type(recv)
  if t == "table" then
    if recv.__key == nil and rawget(recv, "n") ~= nil then return ARRAY_METHODS[name](recv, ...) end
    return recv[name](recv, ...)
  elseif t == "string" then
    return string[name](recv, ...)
  elseif t == "number" then
    if math.type(recv) == "integer" then return C["java.lang.Integer"][name](recv, ...) end
    return C["java.lang.Double"][name](recv, ...)
  elseif t == "boolean" then
    return C["java.lang.Boolean"][name](recv, ...)
  end
  J.npe("call of " .. tostring(name) .. " on null")
end

function J.equals(a, b)
  if a == b then return true end
  if type(a) == "table" and a.__key ~= nil then return a:equals(b) end
  return false
end

function J.hash(x)
  local t = type(x)
  if t == "string" then return string.hashCode(x) end
  if t == "number" then
    if math.type(x) == "integer" then
      if x >= -2147483648 and x <= 2147483647 then return x end
      return J.l2i(x ~ (x >> 32))
    end
    if x == math.floor(x) and x >= -2147483648 and x <= 2147483647 then return math.tointeger(x) end
    return J.l2i(math.tointeger(math.floor(x * 1000003)) or 0)
  end
  if t == "boolean" then return x and 1231 or 1237 end
  if t == "table" then
    if x.__key ~= nil then return x:hashCode() end
    return J.idhash(x)
  end
  return 0
end

function J.compare(a, b)
  local t = type(a)
  if t == "number" or t == "string" then
    if a < b then return -1 elseif a > b then return 1 else return 0 end
  end
  if t == "boolean" then
    if a == b then return 0 elseif a then return 1 else return -1 end
  end
  return a:compareTo(b)
end

-- ------------------------------------------------------------------ values

function J.cond(c, a, b) if c then return a else return b end end
function J.band(a, b) return a and b end
function J.bor(a, b) return a or b end

function J.idiv(a, b)
  if b == 0 then J.throw(C["java.lang.ArithmeticException"].__new__String("/ by zero")) end
  if math.type(a) == "float" or math.type(b) == "float" then
    local q = a / b
    return q < 0 and math.ceil(q) or math.floor(q)
  end
  local q = a // b
  if q < 0 and q * b ~= a then q = q + 1 end
  return q
end

function J.d2i(d)
  if math.type(d) == "integer" then return d end
  if d ~= d then return 0 end
  if d >= 9.2233720368547758e18 then return math.maxinteger end
  if d <= -9.2233720368547758e18 then return math.mininteger end
  if d < 0 then return math.tointeger(math.ceil(d)) end
  return math.tointeger(math.floor(d))
end

function J.l2i(x)
  x = x & 0xFFFFFFFF
  if x >= 0x80000000 then x = x - 0x100000000 end
  return x
end
function J.i2s(x)
  x = x & 0xFFFF
  if x >= 0x8000 then x = x - 0x10000 end
  return x
end
function J.i2b(x)
  x = x & 0xFF
  if x >= 0x80 then x = x - 0x100 end
  return x
end
function J.shl32(a, n) return J.l2i(a << (n & 31)) end
function J.sar(a, n)
  n = n & 63
  if n == 0 then return a end
  return a // (1 << n)
end
function J.ushr32(a, n) return J.l2i((a & 0xFFFFFFFF) >> (n & 31)) end
function J.ushr64(a, n) return a >> (n & 63) end

function J.sval(s)
  if s == nil then return "null" end
  return s
end

function J.dstr(d)
  if math.type(d) == "integer" then return tostring(d) .. ".0" end
  if d ~= d then return "NaN" end
  if d == math.huge then return "Infinity" end
  if d == -math.huge then return "-Infinity" end
  local a = math.abs(d)
  if a == 0 then return (1 / d < 0) and "-0.0" or "0.0" end
  if a >= 1e-3 and a < 1e7 then
    local s
    for p = 1, 17 do
      s = string.format("%." .. p .. "g", d)
      if tonumber(s) == d then break end
    end
    if s:find("e") then s = string.format("%.17f", d):gsub("0+$", "") end
    if not s:find("%.") then s = s .. ".0" end
    if s:sub(-1) == "." then s = s .. "0" end
    return s
  end
  local s
  for p = 1, 17 do
    s = string.format("%." .. (p - 1) .. "e", d)
    if tonumber(s) == d then break end
  end
  local m, e = s:match("^(-?[%d%.]+)e([-+]%d+)$")
  if not m:find("%.") then m = m .. ".0" end
  return m .. "E" .. tostring(math.tointeger(tonumber(e)))
end

function J.str(x)
  local t = type(x)
  if x == nil then return "null" end
  if t == "string" then return x end
  if t == "number" then
    if math.type(x) == "integer" then return tostring(x) end
    return J.dstr(x)
  end
  if t == "boolean" then return tostring(x) end
  if t == "table" then
    if x.__key == nil and rawget(x, "n") ~= nil then return ARRAY_METHODS.toString(x) end
    return x:toString()
  end
  return tostring(x)
end

-- ------------------------------------------------------------------ arrays

function J.arr(t, n)
  t.n = n
  return t
end

function J.newarr(n, dv)
  if n < 0 then J.throw(C["java.lang.NegativeArraySizeException"].__new__String(tostring(n))) end
  local t = { n = n }
  if dv ~= nil then for i = 1, n do t[i] = dv end end
  return t
end

function J.newarrN(dims, dv, level)
  level = level or 1
  local n = dims[level]
  if level == #dims then return J.newarr(n, dv) end
  local t = { n = n }
  for i = 1, n do t[i] = J.newarrN(dims, dv, level + 1) end
  return t
end

function J.aclone(a)
  local t = { n = a.n }
  for i = 1, a.n do t[i] = a[i] end
  return t
end

-- Generic iteration over an array or any Iterable: (index, value) pairs, so
-- a nil element does not end the loop.
function J.iter(x)
  if x == nil then J.npe("iteration over null") end
  if x.__key == nil and rawget(x, "n") ~= nil then
    local i, n = 0, x.n
    return function()
      i = i + 1
      if i <= n then return i, x[i] end
    end
  end
  local fast = x.__iter
  if fast then return fast(x) end
  local it = x:iterator()
  local i = 0
  return function()
    if it:hasNext() then
      i = i + 1
      return i, it:next()
    end
  end
end

-- ------------------------------------------------------------------ classes

local class_objects = {}
function J.forget_class_object(key) class_objects[key] = nil end
function J.classof(key)
  local c = class_objects[key]
  if c == nil then
    c = C["java.lang.Class"].__new_for(key)
    class_objects[key] = c
  end
  return c
end

function J.getclass(o)
  if type(o) == "table" and o.__key ~= nil then return J.classof(o.__key) end
  return J.classof(J.classname(o))
end

function J.unresolved(name)
  error("unresolved gameval " .. tostring(name), 2)
end

-- ------------------------------------------------------------------ exceptions

function J.throw(ex)
  if ex == nil then ex = C["java.lang.NullPointerException"].__new__String("throw null") end
  error(ex, 0)
end

function J.npe(msg)
  J.throw(C["java.lang.NullPointerException"].__new__String(msg or "null"))
end

local function is_budget(e)
  return type(e) == "string" and (e:find("instruction budget exhausted", 1, true) or e:find("not enough memory", 1, true))
end

-- A Lua runtime error stands for the Java runtime exceptions it can be.
local LUA_ERROR_SUPERS = {
  ["java.lang.Throwable"] = true, ["java.lang.Exception"] = true, ["java.lang.RuntimeException"] = true,
  ["java.lang.NullPointerException"] = true, ["java.lang.IndexOutOfBoundsException"] = true,
  ["java.lang.ArrayIndexOutOfBoundsException"] = true, ["java.lang.ClassCastException"] = true,
  ["java.lang.IllegalStateException"] = true, ["java.lang.IllegalArgumentException"] = true,
}

function J.catches(e, key)
  if is_budget(e) then return false end
  if type(e) == "table" then
    if rawget(e, "pending") ~= nil then return false end
    if e.__supers then return e.__supers[key] == true end
    return false
  end
  return LUA_ERROR_SUPERS[key] == true
end

function J.exception(e)
  if type(e) == "table" and e.__supers then return e end
  return C["java.lang.RuntimeException"].__new__String(tostring(e))
end

function J.rethrow(e)
  error(e, 0)
end

-- try/catch/finally. body returns (flag, value) for a jump out of it: 1 is a
-- return, 2 a break, 3 a continue (value names the label).
function J.try(body, catcher, finally)
  local results = table.pack(J.protect(body))
  local ok = results[1]
  local st, rv
  if ok then
    st, rv = results[2], results[3]
  else
    local err = results[2]
    if catcher == nil or is_budget(err) or (type(err) == "table" and rawget(err, "pending") ~= nil) then
      if finally then finally() end
      error(err, 0)
    end
    local r2 = table.pack(J.protect(catcher, err))
    if not r2[1] then
      if finally then finally() end
      error(r2[2], 0)
    end
    st, rv = r2[2], r2[3]
  end
  if finally then
    local fs, fv = finally()
    if fs ~= nil then return fs, fv end
  end
  return st, rv
end



-- Attach a static initialiser to a runtime-defined class after the fact.
function J.def_clinit(key, fn)
  local d = defs[key]
  assert(d, key)
  d.clinit = fn
end

-- Add members to a runtime-defined class before it is realised.
function J.extend(key, fn)
  local d = defs[key]
  assert(d, key)
  local prev = d.body
  d.body = function(cls, super)
    if prev then prev(cls, super) end
    fn(cls, super)
  end
end
