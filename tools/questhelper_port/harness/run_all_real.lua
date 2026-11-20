-- Every quest, diary, miniquest and helper set up on the REAL runtime: the
-- plugin entry booted against harness/mock_api.lua (rt/ rl_* layer, the real
-- injector, ConfigManager, EventBus, ClientThread, the panel), NO stubs. Each
-- helper is loaded through the entry's own QH.load_quest, then init() and
-- everything the panel and overlays read before the first tick, as
-- run_all.lua does with stubs.
--
--   /opt/homebrew/bin/lua tools/questhelper_port/harness/run_all_real.lua [filter]

local HERE = debug.getinfo(1, "S").source:sub(2):match("^(.*)/") or "."
local PLUGINS = HERE .. "/../../../script/plugins"
local ROOT = PLUGINS .. "/quest_helper"
local filter = arg[1]

dofile(HERE .. "/mock_api.lua")
local api = MOCK_API
local LOGS = {}
api.core.log = function(...) LOGS[#LOGS + 1] = table.concat({ ... }, " ") end
MOCK.screen = 30
local function place_player(x, z)
  MOCK.player.true_x, MOCK.player.true_z = x, z
  local ox, oz = (x // 8 - 6) * 8, (z // 8 - 6) * 8
  MOCK.player.fine_x, MOCK.player.fine_z = (x - ox) * 128 + 64, (z - oz) * 128 + 64
end
MOCK.player = { name = "Tester", true_x = 3222, true_z = 3218, level = 0, server_pid = 1, element_id = 700,
  combat_level = 3, dest_x = -1, dest_z = -1, flag_x = -1, flag_z = -1 }
place_player(3222, 3218)
MOCK.npc_defs[2108] = { id = 2108, name = "Wise Old Man", actions = { false, false, false, false, false }, size = 1,
  combat_level = 0, transform_varbit = -1, transform_varp = -1, impostor = -1, transforms = {},
  models = { 1, 2 }, recolor_from = {}, recolor_to = {} }
MOCK.scripts[4029] = function(row) return { 1 }, {} end

-- the sandbox's loader: ready the frame after it is asked for
local FRAME = 0
local requested, results = {}, {}
api.sandbox.protect = pcall
api.sandbox.module_request = function(name)
  local r = requested[name]
  if r == nil then
    local f = io.open(ROOT .. "/" .. name:gsub("%.", "/") .. ".lua")
    if not f then requested[name] = "missing"; return "missing" end
    f:close()
    requested[name] = FRAME
    return "pending"
  end
  if r == "missing" then return "missing" end
  return FRAME > r and "ready" or "pending"
end
api.sandbox.module_load = function(name)
  if results[name] ~= nil then return results[name] end
  local v = dofile(ROOT .. "/" .. name:gsub("%.", "/") .. ".lua")
  if v == nil then v = true end
  results[name] = v
  return v
end

local plugin = dofile(PLUGINS .. "/quest_helper.lua")
MOCK_DECLARE_CONFIG(plugin.config)
-- QH_BUDGET=1: count the instructions of each callback, as the client's
-- budget hook does (budget= in plugins.ini is 20,000,000 per callback)
local MAXI, MAXWHAT = 0, ""
local function counted(what, fn, ...)
  if not os.getenv("QH_BUDGET") then return fn(...) end
  local n = 0
  debug.sethook(function() n = n + 1000 end, "", 1000)
  fn(...)
  debug.sethook()
  if n > MAXI then MAXI, MAXWHAT = n, what end
  if n > 5000000 and os.getenv("QH_BUDGET") == "2" then print("BIG", what, n) end
  return n
end
local function frame()
  FRAME = FRAME + 1
  MOCK.now_ms = MOCK.now_ms + 20
  counted("frame " .. FRAME, plugin.on_frame_start, api, { now_ms = MOCK.now_ms, drawn_frames = FRAME })
end
plugin.on_start(api)
for _ = 1, 400 do
  frame()
  if plugin._boot.phase == "running" or plugin._boot.phase == "failed" then break end
end
assert(plugin._boot.phase == "running", "boot: " .. plugin._boot.phase .. " " .. table.concat(LOGS, " | "))
for _ = 1, 4 do MOCK.tick = MOCK.tick + 1; plugin.on_server_tick(api, { cycle = MOCK.tick }); frame() end

local QHQ = C["com.questhelper.questinfo.QuestHelperQuest"]
local vals = QHQ.values()
local ok_count, total = 0, 0
local by_msg = {}
for i = 1, vals.n do
  local q = vals[i]
  local name = q:name()
  if not filter or name:lower():find(filter:lower(), 1, true) then
    total = total + 1
    local loaded, why = nil, nil
    QH.load_quest(q, function(ok, err) loaded, why = ok, err end)
    for _ = 1, 10 do
      if loaded ~= nil then break end
      frame()
    end
    local ok, err = xpcall(function()
      assert(loaded == true, "QH.load_quest: " .. tostring(why))
      local h = q:getQuestHelper()
      counted("init " .. name, h.init, h)
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
      local msg = (type(err.e) == "table" and err.e.__supers) and J.str(err.e) or tostring(err.e)
      local short = msg:gsub("^.-/gen/", ""):gsub(":%d+:", ":N:")
      by_msg[short] = by_msg[short] or { n = 0, first = name, msg = msg, tb = err.tb }
      by_msg[short].n = by_msg[short].n + 1
    end
  end
end

if os.getenv("QH_BUDGET") then print(string.format("most instructions in one callback: %d (%s)", MAXI, MAXWHAT)) end
collectgarbage("collect")
print(string.format("helpers (real runtime): %d of %d set up; heap %.0f KB with every module loaded",
  ok_count, total, collectgarbage("count")))
local list = {}
for _, v in pairs(by_msg) do list[#list + 1] = v end
table.sort(list, function(a, b) return a.n > b.n end)
for i, v in ipairs(list) do
  if i > 15 then break end
  print(string.format("\n[%d] e.g. %s\n  %s", v.n, v.first, v.msg))
  if os.getenv("QH_TB") then print(v.tb) end
end
local errs = {}
for _, l in ipairs(LOGS) do
  -- (residency: this loader has no module_unload, so the pass stops at its
  -- ceiling and says so -- test_residency.lua covers it)
  if l:find("quest-helper:", 1, true) and not l:find("quest-helper: started", 1, true)
    and not l:find("quest-helper: loaded", 1, true) and not l:find("quest-helper: residency", 1, true) then errs[#errs + 1] = l end
end
if #errs > 0 then print("\nentry errors logged:\n  " .. table.concat(errs, "\n  ")) end
os.exit(ok_count == total and 0 or 1)
