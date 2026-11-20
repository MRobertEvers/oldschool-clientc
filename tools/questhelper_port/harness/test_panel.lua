-- The Quest Helper side panel (script/plugins/quest_helper/ui/panel.lua)
-- under plain Lua, against the generated framework and a mock plugin page.
--
--   /opt/homebrew/bin/lua tools/questhelper_port/harness/test_panel.lua
--
-- Runs real generated code against the panel: QuestHelperPlugin's panel
-- construction path (QuestHelperPanel.__new), QuestManager.handleSelectedQuest
-- (updateStepsTexts / updateHighlight / updateItemRequirements / updateLocks),
-- QuestHelperQuest.getQuestHelpers into refresh(), a real quest's panels,
-- requirements and steps, ManualRequirement skips, AssistanceLevel, Fonts'
-- clinit through JGenerator.

local HERE = debug.getinfo(1, "S").source:sub(2):match("^(.*)/") or "."
local ROOT = HERE .. "/../../../script/plugins/quest_helper"

J = { protect = pcall, now_ms = function() return 0 end }
local LOGS = {}
J.log = function(s) LOGS[#LOGS + 1] = s end
for _, f in ipairs({ "j_core", "j_lang", "j_util", "j_stream", "j_regex", "j_misc" }) do dofile(ROOT .. "/rt/" .. f .. ".lua") end

local index = dofile(ROOT .. "/gen/index.lua")

-- ------------------------------------------------------------------ stubs (as run_all.lua)
local STUB
local stub_mt = {}
stub_mt.__index = function(t, k)
  if k == "__key" then return "stub" end
  if k == "__supers" then return {} end
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
  J.def(key, { kind = "class", super = "java.lang.Object", ifaces = {}, body = function(cls)
    setmetatable(cls, { __index = function(t, k)
      if type(k) == "string" and k:sub(1, 2) == "__" and k ~= "__new" and k:sub(1, 5) ~= "__new" and k:sub(1, 6) ~= "__init" then
        return C["java.lang.Object"][k]
      end
      return function() return setmetatable({ __stubname = key }, stub_mt) end
    end })
  end })
end)
function J.try_class(key)
  local m = index.classes[key]
  if m then load_module(m) end
  return rawget(C, key) or (J.defined(key) and C[key]) or nil
end

-- java.awt.Color is awt.lua's; a test-local one when that file is absent.
if not J.defined("java.awt.Color") then
  J.def("java.awt.Color", { kind = "class", super = "java.lang.Object", ifaces = {}, body = function(cls, super)
    function cls.__init__int_int_int(self, r, g, b) super.__init(self); self.v = (255 << 24) | (r << 16) | (g << 8) | b end
    function cls.__init__int_int_int_int(self, r, g, b, a) super.__init(self); self.v = (a << 24) | (r << 16) | (g << 8) | b end
    function cls.getRGB(self) local v = self.v; if v >= 0x80000000 then v = v - 0x100000000 end; return v end
  end, clinit = function(cls)
    cls.GRAY = cls.__new__int_int_int(128, 128, 128)
    cls.WHITE = cls.__new__int_int_int(255, 255, 255)
    cls.GREEN = cls.__new__int_int_int(0, 255, 0)
    cls.RED = cls.__new__int_int_int(255, 0, 0)
    cls.YELLOW = cls.__new__int_int_int(255, 255, 0)
    cls.LIGHT_GRAY = cls.__new__int_int_int(192, 192, 192)
  end })
end

dofile(ROOT .. "/gen/enums.lua")
for _, m in ipairs(index.framework) do load_module(m) end

dofile(HERE .. "/mock_api.lua")
QH = { api = MOCK_API }
-- item sprites that can still be loading: [obj_id] = "pending"|"missing"
local ITEM_IMAGES = {}
MOCK_API.game.item_image = function(id, count, style)
  local st = ITEM_IMAGES[id] or "ready"
  if st == "ready" then return { image = id, count = count }, "ready" end
  return nil, st
end
dofile(ROOT .. "/ui/panel.lua")

-- ------------------------------------------------------------------ checks
local fails, passes = 0, 0
local function check(name, cond, detail)
  if cond then passes = passes + 1
  else
    fails = fails + 1
    print("FAIL " .. name .. (detail and (": " .. tostring(detail)) or ""))
  end
end

-- ------------------------------------------------------------------ fakes
local Color = C["java.awt.Color"]
local PASS = Color.__new__int_int_int(0, 200, 0)
local FAIL = Color.__new__int_int_int(200, 0, 0)
local PARTIAL = Color.__new__int_int_int(200, 200, 0)

local CFG = {
  filterListBy = C["com.questhelper.QuestHelperConfig$QuestFilter"].SHOW_ALL,
  difficulty = C["com.questhelper.questhelpers.QuestDetails$Difficulty"].ALL,
  orderListBy = C["com.questhelper.QuestHelperConfig$QuestOrdering"].A_TO_Z,
  regionFilterVisibility = C["com.questhelper.QuestHelperConfig$RegionFilterVisibility"].AUTO,
  showFullRequirements = false,
  hideQuestRewards = false,
}
local config = setmetatable({}, { __index = function(_, k)
  if k == "passColour" then return function() return PASS end end
  if k == "failColour" then return function() return FAIL end end
  if k == "partialSuccessColour" then return function() return PARTIAL end end
  if CFG[k] ~= nil then return function() return CFG[k] end end
  return function() return false end
end })

local conf, profile = {}, {}
local cm = {}
function cm:getConfiguration__String_String(g, k) return conf[g .. "." .. k] end
function cm:setConfiguration__String_String_String(g, k, v) conf[g .. "." .. k] = v end
function cm:setConfiguration__String_String_Object(g, k, v)
  conf[g .. "." .. k] = type(v) == "table" and v.name and v:name() or J.str(v)
  if g == "questhelper" and k == "filterListBy" then CFG.filterListBy = v end
end
function cm:getRSProfileKey() return nil end
function cm:getRSProfileConfiguration__String_String(g, k) return profile[g .. "." .. k] end
function cm:setRSProfileConfiguration(g, k, v) profile[g .. "." .. k] = type(v) == "table" and v.name and v:name() or v end
function cm:unsetRSProfileConfiguration(g, k) profile[g .. "." .. k] = nil end

local GameState = C["net.runelite.api.GameState"]
local client = setmetatable({}, { __index = function(_, k)
  if k == "getGameState" then return function() return GameState.LOGGED_IN end end
  if k == "getWorldType" then return function() return C["java.util.EnumSet"].noneOf(nil) end end
  if k == "isClientThread" then return function() return true end end
  return function() return STUB end
end })

local qm_calls = {}
local selected = nil
local qm = {}
function qm:getSelectedQuest() return selected end
function qm:startUpQuest__QuestHelper_boolean(h, open) qm_calls[#qm_calls + 1] = { "start", h, open } end
function qm:shutDownQuest__boolean(b) qm_calls[#qm_calls + 1] = { "shutdown", b } end
function qm:updateQuestList() qm_calls[#qm_calls + 1] = { "update" } end

local plugin = {}
function plugin:getConfig__() return config end
function plugin:getConfigManager() return cm end
function plugin:getClient() return client end
function plugin:getSelectedQuest() return selected end
function plugin:getClientThread()
  return { invokeLater__Runnable = function(_, r) r:run() end }
end
function plugin:loadSidebarOrder(q) return nil end
function plugin:resetSidebarOrderForSection(q, ids) qm_calls[#qm_calls + 1] = { "reset_order", ids:size() } end

local loads = {}
function QH.load_quest(qhq, cb) loads[#loads + 1] = { qhq = qhq, cb = cb } end

-- ------------------------------------------------------------------ page helpers
local function run()
  MOCK_PORCELAIN.describe_fn = nil
  MOCK_API.porcelain.describe(QH.ui.describe)
  local built = {}
  local builder = { node = function(n) built[#built + 1] = n end }
  QH.ui.build(MOCK_API, builder, "page")
  return MOCK_PORCELAIN.rows, built
end

local function row(key) return MOCK_PORCELAIN.by_key[key] end

local function paint(key, width)
  local d = MOCK_PANEL_DRAW(width)
  check("paint " .. key, MOCK_API.porcelain.panel_draw(key, d))
  local texts = {}
  for _, c in ipairs(MOCK.panel_draws) do if c[1] == "text" then texts[#texts + 1] = { x = c[2], y = c[3], s = c[4], rgb = c[5] } end end
  return texts, MOCK.panel_draws
end

-- every line's text in the page's wells, with the well key and the y it is drawn at
local function well_lines(prefix)
  local out = {}
  for _, r in ipairs(MOCK_PORCELAIN.rows) do
    if r.kind == "custom" and r.key:sub(1, #prefix) == prefix then
      local texts = paint(r.key)
      for _, t in ipairs(texts) do out[#out + 1] = { key = r.key, s = t.s, y = t.y, rgb = t.rgb, x = t.x } end
    end
  end
  return out
end

local function find_line(prefix, pattern)
  for _, l in ipairs(well_lines(prefix)) do
    if l.s:find(pattern, 1, true) then return l end
  end
  return nil
end

local function click(key, x, y, action)
  return MOCK_API.porcelain.panel_action({ id = key, action = action or "activate", x = x, y = y })
end

local function no_refusals(what)
  check(what .. ": no refused rows", #MOCK_PORCELAIN.refused == 0, table.concat(MOCK_PORCELAIN.refused, ","))
  check(what .. ": not poisoned", not MOCK_PORCELAIN.poisoned)
  check(what .. ": at most 47 porcelain rows", #MOCK_PORCELAIN.rows <= 47, #MOCK_PORCELAIN.rows)
end

-- ------------------------------------------------------------------ JGenerator / Fonts
do
  local ok, err = pcall(function() return C["com.questhelper.util.Fonts"].getUnderlinedFont() end)
  check("Fonts clinit through JGenerator.makeJTextArea__", ok, err)
end

-- ------------------------------------------------------------------ construction
local panel = C["com.questhelper.panel.QuestHelperPanel"].__new(plugin, qm, cm)
check("constructed through __new", QH.ui.current() == panel)
check("questActive starts false", panel.questActive == false)
check("constructor does not invalidate", MOCK_PORCELAIN.invalidated == 0)

local rows, built = run()
check("search field declared first", built[1] and built[1].id == "qh_search" and built[1].kind == 5)
check("logged-out message", row("qh_msg") and row("qh_msg").text:find("log in", 1, true), row("qh_msg") and row("qh_msg").text)
check("filter select", row("qh_filter") and row("qh_filter").kind == "select" and #row("qh_filter").options > 5)
check("difficulty select", row("qh_difficulty") and row("qh_difficulty").options[1].label == "All", row("qh_difficulty") and row("qh_difficulty").options[1].label)
check("ordering select", row("qh_order") and row("qh_order").options[1].value == "A_TO_Z")
check("filter labels are title case", row("qh_filter").options[2].label == "Show Meets Reqs", row("qh_filter").options[2].label)
no_refusals("empty list")

-- ------------------------------------------------------------------ refresh with the real quest list
local QHQ = C["com.questhelper.questinfo.QuestHelperQuest"]
local helpers = QHQ.getQuestHelpers(false)
local function forced_count()
  local n = 0
  for _, h in J.iter(helpers) do if J.lazy_forced(h) then n = n + 1 end end
  return n
end
local before = forced_count()
local completed = C["java.util.HashMap"].__new__()
completed:put(QHQ.COOKS_ASSISTANT, C["net.runelite.api.QuestState"].IN_PROGRESS)
completed:put(QHQ.DEMON_SLAYER, C["net.runelite.api.QuestState"].FINISHED)
completed:put(QHQ.BLACK_KNIGHTS_FORTRESS, C["net.runelite.api.QuestState"].NOT_STARTED)
panel:refresh(helpers, false, completed, CFG.orderListBy:getSections())
check("refresh did not load any quest module", forced_count() == before, forced_count() .. " vs " .. before)
check("refresh invalidates", MOCK_PORCELAIN.invalidated > 0)
rows = run()
no_refusals("full list")
local cook = find_line("qh_l", "Cook's Assistant")
check("Cook's Assistant listed", cook ~= nil)
check("in-progress colour", cook and cook.rgb == 0xF0CF7B, cook and string.format("%06x", cook.rgb))
local demon = find_line("qh_l", "Demon Slayer")
check("finished colour = passColour", demon and demon.rgb == 0x00C800, demon and string.format("%06x", demon.rgb))
check("section headers", find_line("qh_l", "Quests") ~= nil and find_line("qh_l", "Miniquests") ~= nil)
local nlist = 0
for _, r in ipairs(rows) do if r.key:sub(1, 4) == "qh_l" then nlist = nlist + 1 end end
check("list spread over wells", nlist >= 2, nlist)

-- search
QH.ui.action(MOCK_API, { id = "qh_search", action = "text", text = "cook" })
rows = run()
local all = well_lines("qh_l")
check("search narrows to Cook's Assistant", #all >= 1 and all[1].s == "Cook's Assistant", all[1] and all[1].s)
local nlist2 = 0
for _, r in ipairs(rows) do if r.key:sub(1, 4) == "qh_l" then nlist2 = nlist2 + 1 end end
check("search keeps the well count (no rebuild while typing)", nlist2 == nlist, nlist2 .. " vs " .. nlist)
QH.ui.action(MOCK_API, { id = "qh_search", action = "text", text = "demon slyer" })
run()
check("fuzzy search (Jaro-Winkler)", find_line("qh_l", "Demon Slayer") ~= nil)

-- select a filter: the config is written by name
local picked = click("qh_filter", 0, 0, "pick")
MOCK_PORCELAIN.by_key["qh_filter"].on_action("qh_filter", { action = "pick", text = "MEMBERS" })
check("filter pick writes the config", conf["questhelper.filterListBy"] == "MEMBERS", conf["questhelper.filterListBy"])

-- skill filter grid
MOCK_PORCELAIN.by_key["qh_skill"].on_action("qh_skill", { action = "activate" })
run()
check("skill grid opens", row("qh_skills") ~= nil)
click("qh_skills", 2, 2)
check("skill toggle writes the config", conf["questhelpervars.skillfilterAttack"] == "true", conf["questhelpervars.skillfilterAttack"])
panel:refreshSkillFiltering()
run()
check("skill count summary", row("qh_skill").text:find("1 active", 1, true), row("qh_skill").text)

-- region filter only on league worlds (AUTO)
check("region filter hidden off-league", row("qh_region") == nil)
panel:updateRegionFilterVisibility(true)
run()
check("region filter shown on a league world", row("qh_region") ~= nil)
MOCK_PORCELAIN.by_key["qh_region"].on_action("qh_region", { action = "activate" })
run()
click("qh_regions", 2, 2)
check("region toggle sets LeagueFiltering", C["com.questhelper.config.LeagueFiltering"].selectedRegions ~= nil)
check("region toggle asks for the list", qm_calls[#qm_calls] and qm_calls[#qm_calls][1] == "update")
panel:updateRegionFilterVisibility(false)
check("hiding the region filter clears it", C["com.questhelper.config.LeagueFiltering"].selectedRegions == nil)
no_refusals("filters")

-- ------------------------------------------------------------------ select a quest: load on demand, then assist level
QH.ui.action(MOCK_API, { id = "qh_search", action = "text", text = "cook" })
run()
local cookline = find_line("qh_l", "Cook's Assistant")
local cook_helper = QHQ.COOKS_ASSISTANT:getQuestHelper()
check("cook not loaded yet", not J.lazy_forced(cook_helper))
click(cookline.key, 20, cookline.y - 4)
check("selecting asks the entry to load the quest", #loads == 1 and loads[1].qhq == QHQ.COOKS_ASSISTANT)
check("search emptied on select", panel.search == "" and MOCK.panel_text["qh_search"] == "")
run()
check("loading row", row("qh_msg") and row("qh_msg").text == "Loading Cook's Assistant...", row("qh_msg") and row("qh_msg").text)
no_refusals("loading")
-- the module lands
J.force(cook_helper)
loads[1].cb(true)
run()
check("no assist level chosen -> assist page", row("qh_a_title") ~= nil)
no_refusals("assist")
MOCK_PORCELAIN.by_key["qh_a1b"].on_action("qh_a1b", { action = "activate" })
check("full assistance written", conf["questhelper.assistanceLevel"] == "full", conf["questhelper.assistanceLevel"])
check("assist level marked chosen", conf["questhelpervars.selected-assist-level"] == "true")
local last = qm_calls[#qm_calls]
check("then the quest starts", last and last[1] == "start" and last[2] == cook_helper and last[3] == true)

-- ------------------------------------------------------------------ the overview (real helper)
local h = cook_helper
h.questHelperPlugin = setmetatable({ __stubname = "QuestHelperPlugin" }, stub_mt)
h.client = client
h.configManager = cm
h.config = config
h:setQuest(QHQ.COOKS_ASSISTANT)
h:init()

local pds = {}
for _, pd in J.iter(h:getPanels()) do pds[#pds + 1] = pd end
check("cook has panels", #pds >= 1)
local first_steps = {}
for _, s in J.iter(pds[1]:getSteps()) do first_steps[#first_steps + 1] = s end
h.currentStep = first_steps[1]
-- a persisted manual skip on the first step, through the real QuestStep API
first_steps[1]:withPersistedManualSkip("cook-1")
selected = h
panel:removeQuest()
panel:addQuest(h, true)
check("questActive after addQuest", panel.questActive == true)
rows = run()
no_refusals("overview")
check("quest name heading", row("qh_qname") and row("qh_qname").text == "Cook's Assistant", row("qh_qname") and row("qh_qname").text)
check("close button", row("qh_close") ~= nil)
check("item requirements header", find_line("qh_o", "Item requirements:") ~= nil)
check("rewards header", find_line("qh_o", "Rewards:") ~= nil)
local wiki = find_line("qh_o", "RuneScape Wiki")
check("wiki line", wiki ~= nil)
local header1 = find_line("qh_o", J.str(pds[1]:getHeader()))
check("first section header", header1 ~= nil)
-- item icons
local icons = 0
for _, r in ipairs(rows) do
  if r.kind == "custom" and r.key:sub(1, 4) == "qh_o" then
    local _, calls = paint(r.key)
    for _, c in ipairs(calls) do if c[1] == "image" then icons = icons + 1 end end
  end
end
check("item requirement icons drawn", icons >= 2, icons)

-- real QuestManager.handleSelectedQuest drives the panel
local QM = C["com.questhelper.managers.QuestManager"].__new()
QM.selectedQuest = h
QM.panel = panel
QM.client = client
local ok, err = pcall(function() QM:handleSelectedQuest() end)
check("QuestManager.handleSelectedQuest runs against the panel", ok, err)
run()
no_refusals("after handleSelectedQuest")
local step_text = J.str(first_steps[1]:getText():get(0)):gsub("<[^>]*>", "")
local first_word = step_text:match("^(%S+%s*%S*)")
local sl = find_line("qh_o", first_word)
check("current step highlighted orange", sl and sl.rgb == 0xDC8A00, sl and string.format("%s %06x", sl.s, sl.rgb))
local itemreq = nil
for _, l in ipairs(well_lines("qh_o")) do
  if l.rgb == 0xC80000 or l.rgb == 0x00C800 or l.rgb == 0xC8C800 then itemreq = l; break end
end
check("requirements coloured by getColor after update", itemreq ~= nil)

-- manual skip box: ticked from the persisted state? (no RS profile -> not persisted, unticked)
local skip_line = sl
local well = MOCK_PORCELAIN.by_key[skip_line.key]
click(skip_line.key, 296 - 5, skip_line.y - 4)
local m = first_steps[1]:getSidebarManualSkipRequirement()
check("ticking the box passes the ManualRequirement", m:check__Client(client) == true)
click(skip_line.key, 296 - 5, skip_line.y - 4)
check("unticking clears it", m:check__Client(client) == false)
click(skip_line.key, 296 - 5, skip_line.y - 4, "menu")
check("right-click ticks all up to this step", m:check__Client(client) == true)
local reset = find_line("qh_o", "Reset all tick boxes")
check("reset line", reset ~= nil)
if reset then
  click(reset.key, 20, reset.y - 4)
  check("reset clears the skip", m:check__Client(client) == false)
end

-- collapse the section by its header
header1 = find_line("qh_o", J.str(pds[1]:getHeader()))
click(header1.key, 20, header1.y - 4)
run()
check("collapsed section hides its steps", find_line("qh_o", first_word) == nil)
header1 = find_line("qh_o", J.str(pds[1]:getHeader()))
click(header1.key, 20, header1.y - 4)
run()
check("expanding shows them again", find_line("qh_o", first_word) ~= nil)

-- quest configs (HelperConfig dropdowns) are none for Cook's Assistant
check("no config rows for cook", row("qh_cfg1") == nil)

-- search while a quest is open goes to the list, clearing returns
QH.ui.action(MOCK_API, { id = "qh_search", action = "text", text = "dragon" })
run()
check("searching shows the list", row("qh_filter") ~= nil)
QH.ui.action(MOCK_API, { id = "qh_search", action = "text", text = "" })
run()
check("empty search returns to the overview", row("qh_qname") ~= nil)

-- close
MOCK_PORCELAIN.by_key["qh_close"].on_action("qh_close", { action = "activate" })
check("close shuts the quest down", qm_calls[#qm_calls][1] == "shutdown" and qm_calls[#qm_calls][2] == false)
panel:removeQuest()
run()
check("removeQuest returns to the list", row("qh_filter") ~= nil and panel.questActive == false)

-- ------------------------------------------------------------------ icons landing, metrics learned
panel.icons = {}
for _, req in J.iter(h:getItemRequirements()) do ITEM_IMAGES[req:getId()] = "pending" end
panel:addQuest(h, true)
run()
check("a pending icon is remembered", panel.icons_pending == true)
local inv = MOCK_PORCELAIN.invalidated
ITEM_IMAGES = {}
QH.ui.frame(MOCK_API)
check("an icon that lands re-describes", MOCK_PORCELAIN.invalidated > inv)

rows = run()
local first_o = nil
for _, r in ipairs(rows) do if r.key == "qh_o1" then first_o = r end end
local pk = first_o.paint_key
local d = MOCK_PANEL_DRAW(250)
d.text_width = function(s) return #s * 7 end
MOCK_API.porcelain.panel_draw("qh_o1", d)
inv = MOCK_PORCELAIN.invalidated
QH.ui.frame(MOCK_API)
check("a learned width re-lays the page out", MOCK_PORCELAIN.invalidated > inv)
rows = run()
for _, r in ipairs(rows) do if r.key == "qh_o1" then first_o = r end end
check("and repaints the well", first_o.paint_key ~= pk)
-- steady state: nothing changed, nothing to do
inv = MOCK_PORCELAIN.invalidated
QH.ui.frame(MOCK_API)
QH.ui.frame(MOCK_API)
check("steady frames do nothing", MOCK_PORCELAIN.invalidated == inv)

-- ------------------------------------------------------------------ show full requirements (aggregated across quests)
CFG.showFullRequirements = true
local dt = QHQ.DRAGON_SLAYER_I and QHQ.DRAGON_SLAYER_I:getQuestHelper()
if dt then
  J.force(dt)
  dt.questHelperPlugin = h.questHelperPlugin
  dt.client, dt.configManager, dt.config = client, cm, config
  dt:setQuest(QHQ.DRAGON_SLAYER_I)
  local ok2, e2 = pcall(function() dt:init() end)
  check("Dragon Slayer I init", ok2, e2)
  dt.currentStep = dt.currentStep or (function()
    for _, pd in J.iter(dt:getPanels()) do for _, s in J.iter(pd:getSteps()) do return s end end
  end)()
  selected = dt
  panel:removeQuest()
  local ok3, e3 = pcall(function() panel:addQuest(dt, true) end)
  check("addQuest with showFullRequirements", ok3, e3)
  run()
  no_refusals("full requirements")
  check("aggregated requirements listed", find_line("qh_o", "General requirements:") ~= nil)
end
CFG.showFullRequirements = false

-- ------------------------------------------------------------------ every helper's overview fits the page
local fitted, failed, skipped = 0, {}, 0
local slowest = { "", 0 }
local vals = QHQ.values()
for i = 1, vals.n do
  local q = vals[i]
  local hh = q:getQuestHelper()
  local inited = pcall(function()
    J.force(hh)
    hh.questHelperPlugin = h.questHelperPlugin
    hh.client, hh.configManager, hh.config = client, cm, config
    hh:setQuest(q)
    hh:init()
  end)
  if not inited then skipped = skipped + 1; goto next_helper end
  do
  local okq, eq = pcall(function()
    if hh.currentStep == nil then
      for _, pd in J.iter(hh:getPanels()) do
        for _, s in J.iter(pd:getSteps()) do hh.currentStep = s; break end
        if hh.currentStep then break end
      end
    end
    if hh.currentStep == nil then return end
    selected = hh
    panel:removeQuest()
    panel:addQuest(hh, true)
    QM.selectedQuest = hh
    QM.lastStep = nil
    QM:handleSelectedQuest()
    run()
    -- a steady tick: the framework's per-tick calls, then the describe
    local t0 = os.clock()
    QM:handleSelectedQuest()
    local t1 = os.clock()
    run()
    local dt = os.clock() - t0
    if os.getenv("QH_PERF") and dt > 0.004 then print(string.format("%-28s framework %.1f ms, describe %.1f ms", q:name(), (t1 - t0) * 1000, (os.clock() - t1) * 1000)) end
    if dt > slowest[2] then slowest[1], slowest[2] = q:name(), dt end
    assert(#MOCK_PORCELAIN.refused == 0, "refused rows: " .. table.concat(MOCK_PORCELAIN.refused, ","))
    assert(not MOCK_PORCELAIN.poisoned, "poisoned")
    for _, r in ipairs(MOCK_PORCELAIN.rows) do
      if r.kind == "custom" then MOCK_API.porcelain.panel_draw(r.key, MOCK_PANEL_DRAW()) end
    end
  end)
  if okq then fitted = fitted + 1 else failed[#failed + 1] = q:name() .. ": " .. J.str(eq) end
  end
  ::next_helper::
end
check("every helper's overview builds and paints", #failed == 0, #failed .. " failed, e.g. " .. tostring(failed[1]))
if os.getenv("QH_FAILED") then for _, f in ipairs(failed) do print(f) end end
print(string.format("overviews: %d of %d built (%d helpers did not init under the stubs); slowest steady tick %s %.1f ms",
  fitted, vals.n, skipped, slowest[1], slowest[2] * 1000))

print(string.format("panel: %d passed, %d failed", passes, fails))
if os.getenv("QH_LOGS") then for _, l in ipairs(LOGS) do print(l) end end
if fails > 0 then os.exit(1) end
