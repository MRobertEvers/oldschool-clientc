-- Load the generated Quest Helper outside the client and set up every
-- quest, diary and miniquest: init(), then everything the side panel and
-- overlays read from a helper before the first tick.
--
--   lua tools/questhelper_port/harness/run_all.lua [filter]
--
-- Classes the runtime does not provide yet are STUBS that absorb any call
-- and record it; the report lists them, then every helper's failure grouped
-- by message.
local HERE = debug.getinfo(1, "S").source:sub(2):match("^(.*)/") or "."
local ROOT = HERE .. "/../../../script/plugins/quest_helper"
local filter = arg[1]

J = { protect = pcall, log = function(s) end, now_ms = function() return 0 end }
for _, f in ipairs({ "j_core", "j_lang", "j_util", "j_stream", "j_regex", "j_misc" }) do dofile(ROOT .. "/rt/" .. f .. ".lua") end
for _, f in ipairs(HARNESS_EXTRA_RT or {}) do dofile(ROOT .. "/rt/" .. f .. ".lua") end

local index = dofile(ROOT .. "/gen/index.lua")

-- ------------------------------------------------------------------ stubs

local stub_hits = {}
local function hit(k) stub_hits[k] = (stub_hits[k] or 0) + 1 end
local STUB
local stub_mt = {}
stub_mt.__index = function(t, k)
  if k == "__key" then return "stub" end
  if k == "__supers" then return {} end
  hit((rawget(t, "__stubname") or "?") .. "." .. tostring(k))
  return function() return STUB end
end
stub_mt.__call = function() return STUB end
stub_mt.__lt = function() return false end
stub_mt.__le = function() return false end
stub_mt.__concat = function(a, b) return tostring(type(a) == "table" and "<stub>" or a) .. tostring(type(b) == "table" and "<stub>" or b) end
stub_mt.__len = function() return 0 end
stub_mt.__add = function() return 0 end
stub_mt.__sub = function() return 0 end
stub_mt.__mul = function() return 0 end
stub_mt.__unm = function() return 0 end
STUB = setmetatable({ __stubname = "value" }, stub_mt)
HARNESS_STUB = STUB

local loaded = {}
local function load_module(m)
  if loaded[m] then return end
  loaded[m] = true
  for _, d in ipairs(index.modules[m] or {}) do load_module(d) end
  dofile(ROOT .. "/gen/" .. m .. ".lua")
end

J.on_missing(function(key)
  local m = index.classes[key]
  if m then load_module(m); return end
  -- an external class the runtime lacks: a stub class
  J.def(key, { kind = "class", super = "java.lang.Object", ifaces = {}, body = function(cls)
    setmetatable(cls, { __index = function(t, k)
      if type(k) == "string" and k:sub(1, 2) == "__" and k ~= "__new" and k:sub(1, 5) ~= "__new" and k:sub(1, 6) ~= "__init" then
        return C["java.lang.Object"][k]
      end
      hit(key .. "." .. tostring(k))
      return function() return setmetatable({ __stubname = key }, stub_mt) end
    end })
  end })
end)
function J.try_class(key)
  local m = index.classes[key]
  if m then load_module(m) end
  return C[key]
end

dofile(ROOT .. "/gen/enums.lua")
for _, m in ipairs(index.framework) do load_module(m) end

-- ------------------------------------------------------------------ run

local errors = {}
local by_msg = {}
local ok_count, total = 0, 0

local function inject(obj)
  local cls = obj.__cls
  while cls do
    local d = rawget(cls, "__def")
    if d and d.inject then
      for _, f in ipairs(d.inject) do
        if obj[f[1]] == nil then obj[f[1]] = HARNESS_INJECT and HARNESS_INJECT(f[2]) or setmetatable({ __stubname = f[2] }, stub_mt) end
      end
    end
    cls = rawget(cls, "__super")
  end
end

local QHQ = C["com.questhelper.questinfo.QuestHelperQuest"]
local vals = QHQ.values()
for i = 1, vals.n do
  local q = vals[i]
  local name = q:name()
  if not filter or name:lower():find(filter:lower(), 1, true) then
    total = total + 1
    local ok, err = xpcall(function()
      local h = J.force(q:getQuestHelper())
      inject(h)
      -- QuestManager hands every helper the plugin before init
      h.questHelperPlugin = HARNESS_PLUGIN or setmetatable({ __stubname = "QuestHelperPlugin" }, stub_mt)
      h:setQuest(q)
      h:init()
      h:getPanels()
      h:getGeneralRequirements()
      h:getItemRequirements()
      h:getItemRecommended()
      h:getGeneralRecommended()
      h:getQuestRewards()
      h:getNotes()
      h:getCombatRequirements()
    end, function(e) return { e = e, tb = debug.traceback("", 2) } end)
    if ok then ok_count = ok_count + 1
    else
      local msg = J.str(err.e)
      local short = msg:gsub("^.-/gen/", ""):gsub(":%d+:", ":N:")
      by_msg[short] = by_msg[short] or { n = 0, first = name, msg = msg, tb = err.tb }
      by_msg[short].n = by_msg[short].n + 1
      errors[#errors + 1] = name
    end
  end
end

print(string.format("helpers: %d of %d set up", ok_count, total))
local list = {}
for k, v in pairs(by_msg) do list[#list + 1] = v end
table.sort(list, function(a, b) return a.n > b.n end)
for i, v in ipairs(list) do
  if i > (tonumber(os.getenv("QH_ERRORS") or "15")) then break end
  print(string.format("\n[%d] e.g. %s\n  %s", v.n, v.first, v.msg))
  if os.getenv("QH_TB") then print(v.tb) end
end
if os.getenv("QH_STUBS") then
  local s = {}
  for k, n in pairs(stub_hits) do s[#s + 1] = string.format("%6d %s", n, k) end
  table.sort(s)
  print("\nstub hits:\n" .. table.concat(s, "\n"))
end
