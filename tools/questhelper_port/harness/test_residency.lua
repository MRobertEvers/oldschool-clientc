-- Module RESIDENCY: every quest's requirements reach the quest list features
-- without every quest module resident (script/plugins/quest_helper.lua
-- "residency", rt/j_core.lua J.lazy_shadow / J.lazy_answer / J.lazy_unforce /
-- J.undef). Under plain Lua against harness/mock_api.lua, with the client's
-- sandbox globals removed and every callback's instructions counted.
--
--   /opt/homebrew/bin/lua tools/questhelper_port/harness/test_residency.lua
--
-- What it proves:
--   * the background pass harvests EVERY helper on idle frames, one module a
--     frame, and unloads each module after it (api.sandbox.module_unload);
--   * the skill filter, "Show meets reqs", the quest list and the Check-items
--     totals over all 272 helpers answer exactly what RuneLite's state
--     (every helper a real, loaded instance -- the `--reference` run of this
--     file, which loads them all) answers, without loading a module;
--   * unload / reload identity: a quest opened after its module was unloaded
--     reads the module again and is the same helper object; closed, it is
--     evicted (instance -> facade, module unloaded) and opens a third time;
--     the selected quest is never evicted;
--   * the largest callback during the pass, and the live heap at its peak;
--   * `--no-unload` (a sandbox without module_unload): the pass stops before
--     the heap ceiling and says so.
--
-- Modes: (none) the test; `--reference` prints RuneLite's answers (every
-- module loaded) for the test to compare; `--no-unload` the ceiling run.

local print, dofile, pcall, os, io, debug, arg = print, dofile, pcall, os, io, debug, arg
local MODE = arg and arg[1] or ""
local HERE = debug.getinfo(1, "S").source:sub(2):match("^(.*)/") or "."
local PLUGINS = HERE .. "/../../../script/plugins"
local ROOT = PLUGINS .. "/quest_helper"
local getmetatable = debug.getmetatable

dofile(HERE .. "/mock_api.lua")
local api = MOCK_API
local LOGS = {}
api.core.log = function(...)
  local parts = {}
  for i = 1, select("#", ...) do parts[#parts + 1] = tostring(select(i, ...)) end
  LOGS[#LOGS + 1] = table.concat(parts, " ")
  if os.getenv("QH_LOG") then print("LOG " .. LOGS[#LOGS]) end
end
MOCK.screen = 30
local function place_player(x, z)
  MOCK.player.true_x, MOCK.player.true_z = x, z
  local ox, oz = (x // 8 - 6) * 8, (z // 8 - 6) * 8
  MOCK.player.fine_x, MOCK.player.fine_z = (x - ox) * 128 + 64, (z - oz) * 128 + 64
end
MOCK.player = { name = "Tester", true_x = 3222, true_z = 3218, level = 0, server_pid = 1, element_id = 700,
  combat_level = 60, dest_x = -1, dest_z = -1, flag_x = -1, flag_z = -1 }
place_player(3222, 3218)
MOCK.npc_defs[2108] = { id = 2108, name = "Wise Old Man", actions = { false, false, false, false, false }, size = 1,
  combat_level = 0, transform_varbit = -1, transform_varp = -1, impostor = -1, transforms = {},
  models = { 1, 2 }, recolor_from = {}, recolor_to = {} }
-- every quest not started; a mid-level account, so "meets reqs" splits the list
MOCK.scripts[4029] = function(row) return { 1 }, {} end
for i = 0, 23 do MOCK.skills[i] = { current_level = 40, base_level = 40, xp = 37224 } end

-- (another agent's script hooks, until mock_api has them)
api.scripts = api.scripts or {}
if api.scripts.watch == nil then api.scripts.watch = function() return true end end

-- ------------------------------------------------------------------ the sandbox's module loader
local FRAME = 0
local requested, results, loaded_order, unloaded = {}, {}, {}, {}
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
  local r = requested[name]
  assert(type(r) == "number" and FRAME > r, "module_load before ready: " .. name)
  if results[name] ~= nil then return results[name] end
  local v = dofile(ROOT .. "/" .. name:gsub("%.", "/") .. ".lua")
  if v == nil then v = true end
  results[name] = v
  loaded_order[#loaded_order + 1] = name
  return v
end
if MODE == "" then
  -- the verb the pass needs from the C side (docs/QUEST_HELPER_PORT.md): the
  -- module's slot forgets it, so the next request reads the file again
  api.sandbox.module_unload = function(name)
    assert(results[name] ~= nil, "module_unload of a module not loaded: " .. name)
    results[name] = nil
    requested[name] = nil
    unloaded[#unloaded + 1] = name
    return true
  end
end

local fails, passes = 0, 0
local function check(name, cond, detail)
  if cond then passes = passes + 1
  else
    fails = fails + 1
    print("FAIL " .. name .. (detail ~= nil and (": " .. tostring(detail)) or ""))
  end
end

for _, name in ipairs({ "io", "os", "debug", "dofile", "loadfile", "load", "require", "print", "warn",
  "pcall", "xpcall", "getmetatable", "package" }) do _G[name] = nil end

local plugin = dofile(PLUGINS .. "/quest_helper.lua")
MOCK_DECLARE_CONFIG(plugin.config)
-- every callback's instructions, as the client's budget hook counts them
local BUDGET = 20000000
local MAXI, MAXWHAT, LAST = 0, "", 0
for name, fn in pairs(plugin) do
  if type(fn) == "function" and name:sub(1, 3) == "on_" then
    plugin[name] = function(...)
      local n = 0
      local prof = os.getenv("QH_PROF") and {} or nil
      debug.sethook(function()
        n = n + 1000
        if prof then
          local lvl, parts = 2, {}
          while #parts < 4 do
            local i = debug.getinfo(lvl, "Sl")
            if i == nil then break end
            local src = i.short_src:match("[^/]*$")
            if src:find("%.lua$") and not src:find("^j_") then parts[#parts + 1] = src .. ":" .. i.currentline end
            lvl = lvl + 1
          end
          local k = table.concat(parts, " < ")
          prof[k] = (prof[k] or 0) + 1
        end
      end, "", 1000)
      local r = table.pack(pcall(fn, ...))
      debug.sethook()
      if prof and n > tonumber(os.getenv("QH_PROF_MIN") or "3000000") then
        local l = {}
        for k, c in pairs(prof) do l[#l + 1] = { k, c } end
        table.sort(l, function(a, b) return a[2] > b[2] end)
        print("PROFILE " .. name .. " " .. n)
        for x = 1, math.min(12, #l) do print(string.format("  %6d  %s", l[x][2], l[x][1])) end
      end
      LAST = n
      if n > MAXI then MAXI, MAXWHAT = n, name end
      if n > BUDGET then check("callback within the budget: " .. name, false, n) end
      if not r[1] then error(r[2], 0) end
      return table.unpack(r, 2, r.n)
    end
  end
end
local function counted(fn, ...)
  local n = 0
  debug.sethook(function() n = n + 1000 end, "", 1000)
  local r = table.pack(pcall(fn, ...))
  debug.sethook()
  if not r[1] then error(r[2], 0) end
  return n, table.unpack(r, 2, r.n)
end

local function frame()
  FRAME = FRAME + 1
  MOCK.now_ms = MOCK.now_ms + 20
  plugin.on_frame_start(api, { now_ms = MOCK.now_ms, drawn_frames = FRAME })
end
local function tick()
  MOCK.tick = MOCK.tick + 1
  plugin.on_server_tick(api, { cycle = MOCK.tick })
end

plugin.on_start(api)
for _ = 1, 400 do
  frame()
  if plugin._boot.phase == "running" or plugin._boot.phase == "failed" then break end
end
assert(plugin._boot.phase == "running", "boot: " .. plugin._boot.phase .. " " .. table.concat(LOGS, " | "))
local R = plugin._residency
-- the reference run loads every module: no pass there
if MODE == "--reference" then R.ceiling_kb = 0 end
tick(); frame(); tick(); frame()
collectgarbage("collect")
local kb_login = collectgarbage("count")

local QHQ = C["com.questhelper.questinfo.QuestHelperQuest"]
local vals = QHQ.values()
local qm = QH.core.plugin.questManager
local cm = QH.core.plugin.configManager
local SF = C["com.questhelper.config.SkillFiltering"]
-- the skill filter: hide quests that need or reward Attack or Herblore
cm:setConfiguration__String_String_String("questhelpervars", "skillfilterAttack", "true")
cm:setConfiguration__String_String_String("questhelpervars", "skillfilterHerblore", "true")

local function items_sig(list)
  if list == nil then return "null" end
  local parts = {}
  local it = list:iterator()
  while it:hasNext() do
    local r = it:next()
    parts[#parts + 1] = string.format("%s:%d:%d:%s", tostring(r:getName()), r:getId(), r:getQuantity(), tostring(r:isConsumedItem()))
  end
  return table.concat(parts, ",")
end
-- what the list features read of one helper
local function answers(p)
  local gr = p:getGeneralRequirements()
  local xp = p:getExperienceRewards()
  return {
    skill = tostring(SF.passesSkillFilter(p)),
    own_skill = tostring(SF.questPassesSkillFilter(p)),
    meets = tostring(p:clientMeetsRequirements()),
    general = gr == nil and "null" or tostring(gr:size()),
    xp = xp == nil and "null" or tostring(xp:size()),
    items = items_sig(p:getItemRequirements()),
    recommended = items_sig(p:getItemRecommended()),
  }
end
local FIELDS = { "skill", "own_skill", "meets", "general", "xp", "items", "recommended" }

-- the quest list RuneLite would show (QuestManager.updateQuestList -> panel.refresh)
local function quest_list(filter)
  cm:setConfiguration__String_String_String("questhelper", "filterListBy", filter)
  local P = QH.ui.current()
  local got = nil
  local r0 = P.refresh
  P.refresh = function(self, list, ...) got = list; return r0(self, list, ...) end
  -- the job runs over the next frames; its largest frame is what it costs
  local n0 = counted(qm.updateQuestList, qm)
  local n, frames = n0, 0
  for _ = 1, 200 do
    frame()
    frames = frames + 1
    if LAST > n then n = LAST end
    if plugin._list_jobs.list == nil and got ~= nil then break end
  end
  P.refresh = r0
  LIST_FRAMES = math.max(LIST_FRAMES or 0, frames)
  local names = {}
  if got then
    local it = got:iterator()
    while it:hasNext() do names[#names + 1] = it:next():getQuest():name() end
  end
  return table.concat(names, ","), n
end

-- the Check-items totals (QuestManager.getAllItemRequirements -> AllNeededItems)
local function settle()
  for _ = 1, 200 do
    frame()
    if plugin._list_jobs.list == nil and plugin._list_jobs.items == nil then break end
  end
  frame()
end
local function check_items()
  qm:getAllItemRequirements()
  settle()
  local an = QHQ.CHECK_ITEMS:getQuestHelper()
  return tostring(qm.itemRequirements:size()) .. "/" .. tostring(qm.itemRecommended:size()) .. " " ..
    items_sig(an:getItemRequirements())
end

-- ------------------------------------------------------------------ the reference: RuneLite's state
if MODE == "--reference" then
  for i = 1, vals.n do
    local q = vals[i]
    local loaded = nil
    QH.load_quest(q, function(ok) loaded = ok end)
    for _ = 1, 10 do if loaded ~= nil then break end; frame() end
    assert(loaded == true, "reference: load " .. q:name())
  end
  qm:getAllItemRequirements()
  settle()
  for i = 1, vals.n do
    local q = vals[i]
    local a = answers(q:getQuestHelper())
    local row = { "Q", q:name() }
    for _, f in ipairs(FIELDS) do row[#row + 1] = a[f] end
    print(table.concat(row, "\t"))
  end
  local most = 0
  for _, f in ipairs({ "SHOW_ALL", "SHOW_MEETS_REQS" }) do
    local names, n = quest_list(f)
    if n > most then most = n end
    print("LIST\t" .. f .. "\t" .. names)
  end
  print("LISTMAX\t" .. most .. "\t" .. MAXI .. "\t" .. MAXWHAT)
  print("CHECK\t" .. check_items())
  os.exit(0)
end

-- ------------------------------------------------------------------ the pass
if os.getenv("QH_TRACE_LIST") then
  local QMC = C["com.questhelper.managers.QuestManager"]
  local u0 = QMC.updateQuestList
  QMC.updateQuestList = function(self) print("updateQuestList at frame", FRAME, debug.traceback("", 2):sub(1, 900)); return u0(self) end
end
if os.getenv("QH_TRACE_LOADS") then
  local rc = J.request_class
  J.request_class = function(key)
    print("REQUEST " .. key .. debug.traceback("", 2):sub(1, 1500))
    return rc(key)
  end
end
check("pass waits for an idle frame in game", R.phase == "idle" or R.phase == "harvesting", R.phase)
check("thresholds from the 16 MB cap", R.phase == "idle" or (R.ceiling_kb == 8 * 1024 and R.evict_kb == 10 * 1024),
  tostring(R.ceiling_kb) .. " " .. tostring(R.evict_kb))
local before_pass = #loaded_order
local pass_max, pass_frames, peak_live, peak_count = 0, 0, 0, 0
for f = 1, 2000 do
  if R.phase == "done" then break end
  frame()
  pass_frames = pass_frames + 1
  if LAST > pass_max then pass_max = LAST end
  if os.getenv("QH_BUDGET") and LAST > 2000000 then print("BIG frame", f, LAST, R.phase, R.at, R.queue and R.queue[R.at]) end
  local c = collectgarbage("count")
  if c > peak_count then peak_count = c end
  if f % 20 == 0 then
    collectgarbage("collect")
    local live = collectgarbage("count")
    if live > peak_live then peak_live = live end
  end
  if f % 30 == 0 then
    tick()
    if os.getenv("QH_BUDGET") and LAST > 2000000 then print("BIG tick", f, LAST, R.phase, R.at) end
  end
end
collectgarbage("collect")
local kb_after = collectgarbage("count")
check("the pass finishes", R.phase == "done", R.phase)
settle()
local total, harvested_or_open = 0, 0
for i = 1, vals.n do
  local p = vals[i]:getQuestHelper()
  total = total + 1
  if J.lazy_harvested(p) or J.lazy_forced(p) then harvested_or_open = harvested_or_open + 1 end
end

-- QH_MEM=1: what the harvested answers hold of the live heap, per method
-- (drops them, then exits)
if os.getenv("QH_MEM") then
  collectgarbage("collect")
  local k1 = collectgarbage("count")
  for _, nm in ipairs({ "getItemRecommended", "getItemRequirements", "getGeneralRequirements", "getGeneralRecommended", "getExperienceRewards" }) do
    for i = 1, vals.n do
      local p = vals[i]:getQuestHelper()
      if J.lazy_is_facade(p) then J.lazy_answer(p, nm, nil) end
    end
    collectgarbage("collect")
    print(string.format("QH_MEM without %s: %.0f KB", nm, collectgarbage("count")))
  end
  print(string.format("QH_MEM: live %.0f KB, answers %.0f KB", k1, k1 - collectgarbage("count")))
  os.exit(0)
end
if MODE == "--no-unload" then
  check("without module_unload nothing is unloaded", R.unloaded == 0, R.unloaded)
  check("it harvests until the ceiling", R.harvested > 20, R.harvested)
  local said = false
  for _, l in ipairs(LOGS) do if l:find("cannot unload a module", 1, true) then said = true end end
  check("the stop is logged", said or R.at > #R.queue)
  check("live heap stays under the ceiling + one module", peak_live < R.ceiling_kb + 2048, string.format("%.0f KB", peak_live))
  print(string.format("test_residency --no-unload: %d passed, %d failed (%d of %d harvested before the ceiling, live %.0f KB)",
    passes, fails, R.harvested, total, kb_after))
  os.exit(fails == 0 and 0 or 1)
end

check("every helper harvested (or a real instance)", harvested_or_open == total, harvested_or_open .. " of " .. total)
check("every module the pass loaded it unloaded", #unloaded > 200, #unloaded)
local resident = {}
for path, st in pairs(plugin._modules) do
  local m = path:gsub("^gen/", "")
  if st == "loaded" and (m:find("^helpers/") or m:find("^playerquests/")) and not R.pinned[m] then resident[#resident + 1] = m end
end
check("no quest module stays resident but the pinned ones", #resident == 0, table.concat(resident, ","))
check("the largest pass callback is a few million instructions at most", pass_max < 3000000, pass_max)
check("live heap peak under 12 MB", peak_live < 12 * 1024, string.format("%.0f KB", peak_live))

-- ------------------------------------------------------------------ against RuneLite's state
local ref_rows, ref_lists, ref_check = {}, {}, nil
do
  local self_path = HERE .. "/test_residency.lua"
  local p = io.popen("/opt/homebrew/bin/lua '" .. self_path .. "' --reference 2>&1")
  for line in p:lines() do
    local parts = {}
    for f in (line .. "\t"):gmatch("([^\t]*)\t") do parts[#parts + 1] = f end
    if parts[1] == "Q" then
      local row = {}
      for i, f in ipairs(FIELDS) do row[f] = parts[i + 2] end
      ref_rows[parts[2]] = row
    elseif parts[1] == "LIST" then ref_lists[parts[2]] = parts[3]
    elseif parts[1] == "CHECK" then ref_check = parts[2]
    elseif parts[1] == "LISTMAX" then REF_LISTMAX = tonumber(parts[2]) end
    if parts[1] == "LISTMAX" then REF_MAXI, REF_MAXWHAT = tonumber(parts[3]), parts[4]
    end
  end
  p:close()
end
local nref = 0
for _ in pairs(ref_rows) do nref = nref + 1 end
check("reference run answered every helper", nref == total, nref)
check("with EVERY module loaded, the list update's largest frame is a few million at most",
  REF_LISTMAX ~= nil and REF_LISTMAX < 3000000, REF_LISTMAX)

-- the maps Check items reads, as of now (both runs)
settle()
qm:getAllItemRequirements()
settle()
local loads_before = #loaded_order
local mismatch = {}
local filtered_out, unmet = 0, 0
local n_answers = counted(function()
  for i = 1, vals.n do
    local q = vals[i]
    local a = answers(q:getQuestHelper())
    local ref = ref_rows[q:name()]
    if a.skill == "false" then filtered_out = filtered_out + 1 end
    if a.meets == "false" then unmet = unmet + 1 end
    if ref then
      for _, f in ipairs(FIELDS) do
        if a[f] ~= ref[f] then mismatch[#mismatch + 1] = q:name() .. "." .. f .. " " .. a[f]:sub(1, 80) .. " vs " .. ref[f]:sub(1, 80) end
      end
    end
  end
end)
check("skill filter, meets reqs, item lists of all helpers = RuneLite's", #mismatch == 0, #mismatch .. ": " .. table.concat(mismatch, " | ", 1, math.min(5, #mismatch)))
check("the skill filter hides some quests", filtered_out > 10, filtered_out)
check("a 40s account does not meet every quest", unmet > 10, unmet)
check("reading them loaded no module (the facades answered)", #loaded_order == loads_before, #loaded_order - loads_before)

settle()
local list_all, n_all = quest_list("SHOW_ALL")
local list_meets, n_meets = quest_list("SHOW_MEETS_REQS")
check("quest list (skill filter) = RuneLite's", list_all == ref_lists.SHOW_ALL, #list_all .. " vs " .. #(ref_lists.SHOW_ALL or ""))
check("quest list (Show meets reqs + skill filter) = RuneLite's", list_meets == ref_lists.SHOW_MEETS_REQS,
  #list_meets .. " vs " .. #(ref_lists.SHOW_MEETS_REQS or ""))
check("the list really filters", #list_meets < #list_all and #list_meets > 0)
local ci = check_items()
check("Check-items totals = RuneLite's", ci == ref_check, ci:sub(1, 120) .. " vs " .. tostring(ref_check):sub(1, 120))
check("the list and totals loaded no module", #loaded_order == loads_before, #loaded_order - loads_before)

-- ------------------------------------------------------------------ unload / reload identity
local function open(q)
  local loaded = nil
  QH.load_quest(q, function(ok, why) loaded = ok == true or why end)
  for _ = 1, 10 do if loaded ~= nil then break end; frame() end
  return loaded
end
local XQ = QHQ.X_MARKS_THE_SPOT
local xh = XQ:getQuestHelper()
local xkey = J.lazy_key(xh)
local xmod = "gen.helpers.quests.xmarksthespot"
local h0 = xh:hashCode()
check("X Marks the Spot: a harvested facade, its module unloaded", J.lazy_is_facade(xh) and J.lazy_harvested(xh) and results[xmod] == nil)
local x_items_facade = items_sig(xh:getItemRequirements())
local x_loads = 0
for _, n in ipairs(loaded_order) do if n == xmod then x_loads = x_loads + 1 end end
check("reopen after unload", open(XQ) == true)
local x_loads2 = 0
for _, n in ipairs(loaded_order) do if n == xmod then x_loads2 = x_loads2 + 1 end end
check("the module was read again", x_loads2 == x_loads + 1, x_loads2)
check("the same helper object", rawequal(XQ:getQuestHelper(), xh) and J.lazy_forced(xh))
check("its hash code kept", xh:hashCode() == h0)
check("a real instance of a fresh class", getmetatable(xh) == rawget(C, xkey) and rawget(C, xkey) ~= nil)
local cls1 = rawget(C, xkey)
xh:init()
local function step_count(h)
  local n = 0
  local ps = h:getPanels()
  local it = ps:iterator()
  while it:hasNext() do n = n + it:next():getSteps():size() end
  return n
end
local steps1 = step_count(xh)
check("its steps", steps1 > 0, steps1)
check("its own item requirements (the spade), as harvested", items_sig(xh:getItemRequirements()) == x_items_facade,
  items_sig(xh:getItemRequirements()) .. " vs " .. x_items_facade)
-- selected: never evicted
qm:startUpQuest__QuestHelper_boolean(xh, false)
for _ = 1, 3 do frame() end
check("selected", rawequal(qm:getSelectedQuest(), xh))
EVICT_KB = R.evict_kb
R.evict_kb, R.evict_age, R.evict_every = 0, 0, 1
for _ = 1, 40 do frame() end
check("the selected quest is not evicted", J.lazy_forced(xh) and results[xmod] ~= nil)
qm:shutDownQuest__boolean(false)
for _ = 1, 3 do frame() end
check("closed", qm:getSelectedQuest() == nil)
for _ = 1, 400 do
  frame()
  if not J.lazy_forced(xh) then break end
end
check("closed, it is evicted: a facade again", J.lazy_is_facade(xh) and J.lazy_harvested(xh))
check("evicted module unloaded", results[xmod] == nil and rawget(C, xkey) == nil)
check("evicted answers = the instance's", items_sig(xh:getItemRequirements()) == x_items_facade)
check("hash code still kept", xh:hashCode() == h0)
R.evict_kb, R.evict_age, R.evict_every = EVICT_KB, 1500, 60
check("opens a third time", open(XQ) == true)
check("same object again, a fresh class", rawequal(XQ:getQuestHelper(), xh) and rawget(C, xkey) ~= nil and rawget(C, xkey) ~= cls1)
xh:init()
check("same steps after reload", step_count(xh) == steps1, step_count(xh))

-- a dependency chain: Desert Treasure II needs Secrets of the North's module
local DT = QHQ.DESERT_TREASURE_II
local dth = DT:getQuestHelper()
check("Desert Treasure II harvested, unloaded with its dependency", J.lazy_harvested(dth)
  and results["gen.helpers.quests.deserttreasureii"] == nil and results["gen.helpers.quests.secretsofthenorth"] == nil)
check("Desert Treasure II opens after unload", open(DT) == true)
dth:init()
check("its steps", step_count(dth) > 0)

collectgarbage("collect")
local errs = {}
for _, l in ipairs(LOGS) do
  if l:find("quest-helper:", 1, true) and not l:find("quest-helper: started", 1, true)
    and not l:find("quest-helper: loaded", 1, true) and not l:find("quest-helper: residency:", 1, true) then errs[#errs + 1] = l end
end
check("no dispatch errors logged", #errs == 0, table.concat(errs, "\n  "))

-- the no-unload sandbox, in its own process
do
  local self_path = HERE .. "/test_residency.lua"
  local p = io.popen("/opt/homebrew/bin/lua '" .. self_path .. "' --no-unload 2>&1")
  local out = p:read("a")
  local ok = p:close()
  check("--no-unload run passes", ok, out)
  print((out:gsub("\n$", "")))
end

print(string.format(
  "test_residency: %d passed, %d failed (%d helpers harvested, %d modules unloaded, %d failed, in %d frames; "
  .. "largest pass callback %d instructions, largest callback %d (%s); live heap %.0f KB after login, peak %.0f KB "
  .. "(count peak %.0f KB), %.0f KB after the pass; all helpers' answers %d instructions; list update's largest frame %d / %d instructions, %d frames; "
  .. "every module loaded: list update's largest frame %d, largest callback %d (%s))",
  passes, fails, R.harvested, R.unloaded, R.failed, pass_frames, pass_max, MAXI, MAXWHAT, kb_login, peak_live,
  peak_count, kb_after, n_answers, n_all, n_meets, LIST_FRAMES or 0, REF_LISTMAX or -1, REF_MAXI or -1, tostring(REF_MAXWHAT)))
os.exit(fails == 0 and 0 or 1)
