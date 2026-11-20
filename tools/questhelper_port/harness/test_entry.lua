-- The plugin ENTRY (script/plugins/quest_helper.lua) end to end under plain
-- Lua, against harness/mock_api.lua with a module loader that behaves like
-- api.sandbox's: a module asked for is 'pending' until the next frame, then
-- 'ready', and module_load runs it once.
--
--   /opt/homebrew/bin/lua tools/questhelper_port/harness/test_entry.lua
--
-- What it drives, all through the entry's own callbacks:
--   * the boot: rt + gen/index + enums + every framework module + ui/panel,
--     one a frame, then QuestHelperPlugin.startUp through the injector, with
--     NO quest module loaded (the QuestHelper facade answers the list);
--   * login: the first ticks run QuestManager.setupRequirements /
--     setupOnLogin / updateQuestList, the panel lists the quests;
--   * the panel: search "x marks", click it -> QH.load_quest loads the quest
--     module across frames -> assist page -> full assistance -> started;
--   * the quest varbit (8063) through its states via on_var_changed +
--     on_server_tick -> VarbitChanged/GameTick: the current step changes;
--   * the world pass draws the step's highlight (a world_tile for a dig
--     tile, a world_hull for Veos once he is spawned);
--   * the spade requirement goes met / unmet with MOCK.inv[93] through
--     on_inventory_changed -> ItemContainerChanged.

local print, dofile, pcall, os, io, debug = print, dofile, pcall, os, io, debug
local HERE = debug.getinfo(1, "S").source:sub(2):match("^(.*)/") or "."
local PLUGINS = HERE .. "/../../../script/plugins"
local ROOT = PLUGINS .. "/quest_helper"

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
-- a whole PlayerSnap (the client's), in Lumbridge
local function place_player(x, z)
  MOCK.player.true_x, MOCK.player.true_z = x, z
  local ox, oz = (x // 8 - 6) * 8, (z // 8 - 6) * 8
  MOCK.player.fine_x, MOCK.player.fine_z = (x - ox) * 128 + 64, (z - oz) * 128 + 64
end
MOCK.player = { name = "Tester", true_x = 3222, true_z = 3218, level = 0, server_pid = 1, element_id = 700,
  combat_level = 3, dest_x = -1, dest_z = -1, flag_x = -1, flag_z = -1 }
place_player(3222, 3218)

-- ------------------------------------------------------------------ the sandbox's module loader
local FRAME = 0
local requested = {}   -- name -> frame it was asked for, or "missing"
local results = {}
local loaded_order = {}
api.sandbox.protect = pcall
api.sandbox.module_request = function(name)
  assert(name:match("^[a-z0-9_%-]+[a-z0-9_%.%-]*$"), "module name: " .. name)
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
  if os.getenv("QH_TRACE_LOADS") and name:find("^gen%.helpers") then print("LOAD " .. name .. debug.traceback("", 2):sub(1, 3000)) end
  local v = dofile(ROOT .. "/" .. name:gsub("%.", "/") .. ".lua")
  if v == nil then v = true end
  results[name] = v
  loaded_order[#loaded_order + 1] = name
  return v
end
-- the module_unload verb the residency pass uses (docs/QUEST_HELPER_PORT.md):
-- forget the module so the next request reads its file again
local unloaded = {}
api.sandbox.module_unload = function(name)
  assert(results[name] ~= nil, "module_unload of a module not loaded: " .. name)
  results[name] = nil
  requested[name] = nil
  unloaded[#unloaded + 1] = name
  return true
end

-- ------------------------------------------------------------------ checks
local fails, passes = 0, 0
local function check(name, cond, detail)
  if cond then passes = passes + 1
  else
    fails = fails + 1
    print("FAIL " .. name .. (detail ~= nil and (": " .. tostring(detail)) or ""))
  end
end

-- The client's sandbox (torirs_plugin_lua.c lua_open_sandbox_libs): no
-- io/os/debug, no dofile/load/require/print/warn, no pcall/xpcall, no
-- getmetatable. The test keeps its own copies; the plugin sees none.
for _, name in ipairs({ "io", "os", "debug", "dofile", "loadfile", "load", "require", "print", "warn",
  "pcall", "xpcall", "getmetatable", "package" }) do _G[name] = nil end

local plugin = dofile(PLUGINS .. "/quest_helper.lua")
-- Every callback runs under an instruction count like the client's budget
-- hook (plugins.ini budget=20000000 per outermost callback).
local BUDGET = 20000000
local over = {}
local MAXI, MAXWHAT = 0, ""
for name, fn in pairs(plugin) do
  if type(fn) == "function" and name:sub(1, 3) == "on_" then
    plugin[name] = function(...)
      local n = 0
      local prof = os.getenv("QH_PROF") and {} or nil
      debug.sethook(function()
        n = n + 1000
        if prof then
          -- the innermost frame inside the generated or runtime code, with its caller
          local lvl, parts = 2, {}
          while #parts < 3 do
            local i = debug.getinfo(lvl, "Sl")
            if i == nil then break end
            local src = i.short_src:match("[^/]*$")
            if src ~= "j_regex.lua" and src ~= "j_lang.lua" and src ~= "j_util.lua" and src ~= "j_stream.lua" and src ~= "j_core.lua" and src ~= "[C]" then
              parts[#parts + 1] = src .. ":" .. i.currentline
            end
            lvl = lvl + 1
          end
          local k = table.concat(parts, " < ")
          prof[k] = (prof[k] or 0) + 1
        end
      end, "", 1000)
      local r = table.pack(pcall(fn, ...))
      debug.sethook()
      if prof and n > 2000000 then
        local l = {}
        for k, c in pairs(prof) do l[#l + 1] = { k, c } end
        table.sort(l, function(a, b) return a[2] > b[2] end)
        print("PROFILE " .. name .. " " .. n)
        for x = 1, math.min(12, #l) do print(string.format("  %6d  %s", l[x][2], l[x][1])) end
      end
      if n > MAXI then MAXI, MAXWHAT = n, name end
      if n > BUDGET then over[#over + 1] = name .. " " .. n end
      if os.getenv("QH_BUDGET") and n > 2000000 then print("BIG", name, n) end
      if not r[1] then error(r[2], 0) end
      return table.unpack(r, 2, r.n)
    end
  end
end
check("definition id", plugin.id == "quest-helper")
check("definition title", plugin.title == "Quest Helper")
check("config spliced from the generator", #plugin.config == 37, #plugin.config)
MOCK_DECLARE_CONFIG(plugin.config)
for _, it in ipairs(plugin.config) do
  check("config key " .. it.key .. " fits", #it.key <= 95 and it.key:match("^[a-z0-9_]+$"))
end

local function frame()
  FRAME = FRAME + 1
  MOCK.now_ms = MOCK.now_ms + 20
  plugin.on_frame_start(api, { now_ms = MOCK.now_ms, drawn_frames = FRAME })
end
local function tick(n)
  for _ = 1, n or 1 do
    MOCK.tick = MOCK.tick + 1
    plugin.on_server_tick(api, { cycle = MOCK.tick })
    frame()
  end
end

-- the Wise Old Man (GlobalFakeObjects' cheerer) and Veos, as the cache has them
MOCK.npc_defs[2108] = { id = 2108, name = "Wise Old Man", actions = { false, false, false, false, false }, size = 1,
  combat_level = 0, transform_varbit = -1, transform_varp = -1, impostor = -1, transforms = {},
  models = { 1, 2 }, recolor_from = {}, recolor_to = {} }
MOCK.npc_defs[8484] = { id = 8484, name = "Veos", actions = { "Talk-to", false, false, false, false }, size = 1,
  combat_level = 0, transform_varbit = -1, transform_varp = -1, impostor = -1, transforms = {},
  models = { 3 }, recolor_from = {}, recolor_to = {} }

-- quest state: QUEST_STATUS_GET(row) -> 0 in progress, 1 not started, 2 finished
local QUEST_STATE = {}
MOCK.scripts[4029] = function(row) return { QUEST_STATE[row] or 1 }, {} end

-- the Wise Old Man's record is still loading for the first reads: the
-- Cheerer's build goes pending half way through GlobalFakeObjects.createNpcs
-- (the entry undoes it so the replay builds both fans; live 2026-11-19)
local wom_pending = 3
do
  local real = api.cache.npc_def
  api.cache.npc_def = function(id)
    if id == 2108 and wom_pending > 0 then wom_pending = wom_pending - 1; return nil, "pending" end
    return real(id)
  end
end

-- ------------------------------------------------------------------ boot
plugin.on_start(api)
check("panel registered with the icon", MOCK_PORCELAIN.descriptor and MOCK_PORCELAIN.descriptor.icon_asset == "quest_icon.png")
for _ = 1, 400 do
  frame()
  if plugin._boot.phase == "running" or plugin._boot.phase == "failed" then break end
end
check("boot reaches running", plugin._boot.phase == "running", plugin._boot.phase .. " " .. table.concat(LOGS, " | "))
if plugin._boot.phase ~= "running" then
  print(string.format("test_entry: %d passed, %d failed", passes, fails))
  os.exit(1)
end
check("frames to boot (one module a frame)", FRAME < 120, FRAME)
-- Boot loads only the quest modules RuneLite's own startUp/login code must
-- run: the "Check items" helper (setupOnLogin inits it) and the quests that
-- override a state read the list asks of every helper (Shield of Arrav's
-- isCompleted, Recipe for Disaster's getState). Everything else is a facade.
local ALLOWED = { ["gen.helpers.mischelpers.allneededitems"] = true, ["gen.helpers.quests.shieldofarrav"] = true,
  ["gen.helpers.quests.recipefordisaster"] = true }
-- (the residency pass loads every quest module in turn once in game, and
-- unloads it: those are not startUp's or login's)
local function extra_quest_modules()
  local extra = {}
  for _, n in ipairs(loaded_order) do
    local m = n:gsub("^gen%.", ""):gsub("%.", "/")
    if (n:find("^gen%.helpers") or n:find("^gen%.playerquests")) and not ALLOWED[n]
      and not plugin._residency.touched[m] then extra[#extra + 1] = n end
  end
  return extra
end
check("startUp loaded no other quest module (facades)", #extra_quest_modules() == 0, table.concat(extra_quest_modules(), ","))
local panel_at = nil
for i, n in ipairs(loaded_order) do if n == "ui.panel" then panel_at = i end end
check("runtime first, framework, then the panel", loaded_order[1] == "rt.j_core" and panel_at ~= nil)
check("plugin started", QH.core.plugin ~= nil)
check("panel built by startUp", QH.ui.current() ~= nil)
collectgarbage("collect")
local kb_boot = collectgarbage("count")
check("heap after boot under 12 MB (the client caps a plugin at 16)", kb_boot < 12 * 1024, string.format("%.0f KB", kb_boot))

-- ------------------------------------------------------------------ login ticks
if os.getenv("QH_TRACE_LOADS") then
  local rc = J.request_class
  J.request_class = function(key)
    print("REQUEST " .. key .. debug.traceback("", 2):sub(1, 2500))
    return rc(key)
  end
end
if os.getenv("QH_ROWS") then
  local P = QH.ui.current()
  local r0 = P.refresh
  P.refresh = function(self, ...) print("REFRESH", select("#", ...), (...) and (...):size()); return r0(self, ...) end
  local qm0 = QH.core.plugin.questManager
  local u0 = qm0.updateQuestList
  qm0.updateQuestList = function(self) print("updateQuestList", J.rl_client.client():getGameState():name()); local ok, e = pcall(u0, self); print("  ->", ok, e); if not ok then error(e, 0) end end
end
tick(6)
-- the list update is spread over frames (the entry's list jobs): let it land
local list_frames = 0
for _ = 1, 200 do
  if plugin._list_jobs.list == nil and plugin._list_jobs.items == nil then break end
  frame()
  list_frames = list_frames + 1
end
check("the login's list update lands within a second", list_frames < 50, list_frames)
for _ = 1, 2 do frame() end
local qm = QH.core.plugin.questManager
check("QuestManager injected", qm ~= nil)
check("the npc record went pending at least once", wom_pending < 3, wom_pending)
local cheerers = C["com.questhelper.runeliteobjects.Cheerer"].cheerers:size()
check("a pending fan build is replayed whole: both cheerers exist", cheerers == 2, cheerers)
check("and the latch is set once they do", C["com.questhelper.runeliteobjects.GlobalFakeObjects"].initialized == true)
check("login loads no other quest module", #extra_quest_modules() == 0, table.concat(extra_quest_modules(), ","))

-- the page
local function run_page()
  MOCK_PORCELAIN.describe_fn = nil
  -- the entry registered ONE describe function; the mock keeps the last
  -- registration, so re-register through the entry's start shape
  local built = {}
  local builder = { node = function(n) built[#built + 1] = n end }
  plugin.on_ui_build(api, builder, "page")
  return MOCK_PORCELAIN.rows, built
end
-- the mock's describe registration was the entry's (on_start)
local describe_fn = nil
do
  local saved = MOCK_API.porcelain.describe
  MOCK_API.porcelain.describe = function(fn) describe_fn = fn; MOCK_PORCELAIN.describe_fn = fn end
  plugin.on_start(api)   -- a second start is a no-op for the boot; re-registers the page
  MOCK_API.porcelain.describe = saved
end
check("second on_start keeps running", plugin._boot.phase == "running")
local function rows()
  MOCK_PORCELAIN.describe_fn = describe_fn
  MOCK_PORCELAIN_RUN()
  return MOCK_PORCELAIN.rows
end
local function row(key) return MOCK_PORCELAIN.by_key[key] end
local function paint(key)
  local d = MOCK_PANEL_DRAW()
  MOCK_API.porcelain.panel_draw(key, d)
  local texts = {}
  for _, c in ipairs(MOCK.panel_draws) do if c[1] == "text" then texts[#texts + 1] = { x = c[2], y = c[3], s = c[4], rgb = c[5] } end end
  return texts
end
local function find_line(prefix, pattern)
  for _, r in ipairs(MOCK_PORCELAIN.rows) do
    if r.kind == "custom" and r.key:sub(1, #prefix) == prefix then
      for _, t in ipairs(paint(r.key)) do
        if t.s:find(pattern, 1, true) then return { key = r.key, s = t.s, y = t.y, rgb = t.rgb } end
      end
    end
  end
  return nil
end

rows()
-- The client keeps at most 512 primitives across EVERY custom well of a page
-- (PANEL_OVERLAY_STAGE_MAX); a well that pushes past it is not shown at all.
do
  local total, per = 0, {}
  for _, r in ipairs(MOCK_PORCELAIN.rows) do
    if r.kind == "custom" then
      local d = MOCK_PANEL_DRAW()
      MOCK_API.porcelain.panel_draw(r.key, d)
      per[#per + 1] = r.key .. "=" .. #MOCK.panel_draws
      total = total + #MOCK.panel_draws
    end
  end
  if os.getenv("QH_ROWS") then print("WELL PRIMS", total, table.concat(per, " ")) end
  check("the full list's wells fit the page's 512 primitives", total <= 512, total .. ": " .. table.concat(per, " "))
end
if os.getenv("QH_ROWS") then
  for _, r in ipairs(MOCK_PORCELAIN.rows) do print("ROW", r.key, r.kind, r.text or r.label or "") end
  print("panel view", QH.ui.current().view, "quests", QH.ui.current().quests and #QH.ui.current().quests)
end
local xline = find_line("qh_l", "X Marks the Spot")
check("quest list shows X Marks the Spot after login", xline ~= nil)

-- search, then click it
plugin.on_ui_action(api, { id = "qh_search", action = "text", text = "x marks" })
rows()
xline = find_line("qh_l", "X Marks the Spot")
check("search finds it", xline ~= nil)
local QHQ = C["com.questhelper.questinfo.QuestHelperQuest"]
local xh = QHQ.X_MARKS_THE_SPOT:getQuestHelper()
check("X Marks the Spot not loaded before the click", not J.lazy_forced(xh))
plugin.on_ui_action(api, { id = xline.key, action = "activate", x = 20, y = xline.y - 4 })
rows()
check("loading row while the module is read", row("qh_msg") and row("qh_msg").text:find("Loading", 1, true), row("qh_msg") and row("qh_msg").text)
for _ = 1, 5 do frame() end
check("module loaded on demand", results["gen.helpers.quests.xmarksthespot"] ~= nil)
check("helper is a real instance now", J.lazy_forced(xh))
rows()
check("assist page", row("qh_a_title") ~= nil)
if row("qh_a1b") then
  plugin.on_ui_action(api, { id = "qh_a1b", action = "activate" })
end
for _ = 1, 3 do frame() end
check("quest selected", qm:getSelectedQuest() == xh, qm:getSelectedQuest())
tick(1)
rows()
check("overview shows the quest name", row("qh_qname") and row("qh_qname").text == "X Marks the Spot", row("qh_qname") and row("qh_qname").text)
-- The profile became known at the first tick (after the screen's LOGGED_IN):
-- the next scene load must NOT run the login code -- that shut the open
-- helper down and printed the "updated" line on the first region edge
-- (live in the browser, 2026-11-20).
do
  -- a hop back to the title and in again, the local player unknown when the
  -- screen turns to the game (the browser's resume login did exactly this)
  local saved_player = MOCK.player
  -- the login code (QuestHelperPlugin's LOGGED_IN && profileChanged block)
  -- counted by the one call only it makes
  local nvm = QH.core.plugin.newVersionManager
  local logins = 0
  nvm.updateChatWithNotificationIfNewVersion = function(self)
    logins = logins + 1
    return C[J.classname and J.classname(self) or "com.questhelper.managers.NewVersionManager"].updateChatWithNotificationIfNewVersion(self)
  end
  plugin.on_screen_changed(api, { screen = "title" })
  MOCK.no_world = true
  plugin.on_screen_changed(api, { screen = "game" })
  check("no login runs while the profile is unknown", logins == 0, logins)
  -- the scene is in and the game state is LOGGED_IN, but the player's name
  -- is not yet (live: the login's scene load before the first tick)
  MOCK.no_world = nil
  local nameless = {}
  for k, v in pairs(saved_player) do nameless[k] = v end
  nameless.name = ""
  MOCK.player = nameless
  plugin.on_world_loaded(api, {})
  check("the scene's state reads LOGGED_IN", J.str(QH.core.plugin.client:getGameState()) == "LOGGED_IN",
    J.str(QH.core.plugin.client:getGameState()))
  check("no login at a scene load before the profile is known", logins == 0, logins)
  MOCK.player = saved_player
  tick(2)
  check("the login ran once, for the known profile", logins == 1, logins)
  nvm.updateChatWithNotificationIfNewVersion = nil
  for _ = 1, 60 do
    if plugin._list_jobs.list == nil and plugin._list_jobs.items == nil then break end
    frame()
  end
  check("the first tick's profile change was consumed by a login", QH.core.plugin.profileChanged == false,
    QH.core.plugin.profileChanged)
  qm:startUpQuest__QuestHelper_boolean(xh, false)
  for _ = 1, 3 do frame() end
  check("helper reopened after the relogin", qm:getSelectedQuest() == xh, qm:getSelectedQuest())
  plugin.on_world_loaded(api, {})
  frame()
  check("a scene load after login keeps the open helper", qm:getSelectedQuest() == xh, qm:getSelectedQuest())
end

-- ------------------------------------------------------------------ a full scene
-- A live Lumbridge scene holds tens of thousands of locs. Every quest step
-- with requirements scans the scene's tiles at startUp
-- (DetailedQuestStep.addItemTiles -> Scene.getTiles), which once kept a
-- snapshot table per loc and ran the client's 16 MB plugin heap out
-- ("on_server_tick: not enough memory", live 2026-11-19).
do
  local ox, oz = api.world.scene_origin()
  local n = 0
  for x = 0, 103 do
    for z = 0, 103 do
      for k = 1, 3 do
        n = n + 1
        MOCK.scenery[n] = { loc_id = 1000 + k, name = "Thing", tile_x = ox + x, tile_z = oz + z, level = (k == 3) and 1 or 0,
          size_x = 1, size_z = 1, shape = (k == 1) and 10 or 22, angle = 0, element_id = -1, interactive = true, visible_ops = 1 }
      end
    end
  end
end
collectgarbage("collect")
local kb_scene_base = collectgarbage("count")
-- ------------------------------------------------------------------ the varbit through its states
local VB = 8063
local function set_quest_var(v)
  MOCK.varbits[VB] = v
  plugin.on_var_changed(api, { varp = 2111, value = v, old = 0 })
  tick(1)
end
local function current_text()
  local s = xh:getCurrentStep()
  if s == nil then return nil end
  local a = s:getActiveStep()
  local t = a:getText()
  if t == nil or t:size() == 0 then return "" end
  return J.str(t:get(0))
end

set_quest_var(0)
local t0 = current_text()
check("state 0: talk to Veos", t0 and t0:find("Talk to Veos in The Sheared Ram", 1, true), t0)
set_quest_var(2)
local t2 = current_text()
check("state 2: dig outside Bob's", t2 and t2:find("Dig north of Bob", 1, true), t2)
set_quest_var(3)
collectgarbage("collect")
local kb_scene = collectgarbage("count") - kb_scene_base
if os.getenv("QH_ROWS") then
  C["net.runelite.api.TileObject"].__invalidate()
  collectgarbage("collect")
  print("SCENE after invalidate", collectgarbage("count") - kb_scene_base, "before", kb_scene)
end
-- Bounded by the scene's TILES (10816 a plane: a Tile object each that holds
-- something, while the cycle's index lives), not by its locs.
check("a 32k-loc scene scanned by a step costs under 5 MB of plugin heap", kb_scene < 5 * 1024, string.format("%.0f KB", kb_scene))
local t3 = current_text()
check("state 3: dig behind the castle", t3 and t3:find("Dig behind Lumbridge Castle", 1, true), t3)
check("the step changed with the varbit", t2 ~= t3)

-- the highlight for a dig: a world tile (OnTile) while standing near it
place_player(3203, 3212)
MOCK.draws = {}
local d = MOCK_DRAW_AWT()
plugin.on_draw_world(api, d)
plugin.on_draw_canvas(api, d)
local kinds = {}
for _, c in ipairs(MOCK.draws) do kinds[c[1]] = (kinds[c[1]] or 0) + 1 end
check("world pass draws the dig tile", (kinds.world_tile or 0) > 0 or (kinds.polygon or 0) > 0,
  (function() local s = {} for k, n in pairs(kinds) do s[#s + 1] = k .. "=" .. n end return table.concat(s, ",") end)())
check("canvas pass draws the step panel text", (kinds.text or 0) > 0)
local found_step_text = false
for _, c in ipairs(MOCK.draws) do
  if c[1] == "text" and type(c[4]) == "string" and c[4]:find("Castle", 1, true) then found_step_text = true end
end
check("overlay text names the step", found_step_text)

-- The minimap arrow to a dig OUTSIDE the scene (Draynor, ~94 tiles from the
-- castle): DirectionArrow.createMinimapDirectionArrow through
-- QuestPerspective.getMinimapPoint, whose `x /= maxDistance` (int /= float)
-- must narrow to an int as Java does (the transpiler once left it a float).
do
  set_quest_var(4)
  place_player(3203, 3212)
  local saved_canvas = MOCK.canvas
  MOCK.canvas = { x = 0, y = 0, width = 1238, height = 800 }
  MOCK.widgets[10551326] = { type = 0, width = 152, height = 152, x = 1038, y = 8 }
  local QP = C["com.questhelper.steps.tools.QuestPerspective"]
  local mp = QP.getMinimapPoint(J.rl_client.client(),
    C["net.runelite.api.coords.WorldPoint"].__new(3203, 3212, 0), C["net.runelite.api.coords.WorldPoint"].__new(3109, 3264, 0))
  check("getMinimapPoint answers ints", mp ~= nil and math.type(mp:getX()) == "integer" and math.type(mp:getY()) == "integer",
    mp and (tostring(mp:getX()) .. "," .. tostring(mp:getY())))
  local step = xh:getCurrentStep():getActiveStep()
  local polys = 0
  for render = 0, 3 do
    step.currentRender = render
    MOCK.draws = {}
    local dm = MOCK_DRAW_AWT()
    dm.minimap_project = function(fx, fz)
      local p = MOCK.player
      local dx, dz = (fx - p.fine_x) / 128, (fz - p.fine_z) / 128
      if dx * dx + dz * dz > 18 * 18 then return nil end
      return 1114 + (dx * 4) // 1, 84 - (dz * 4) // 1
    end
    plugin.on_draw_canvas(api, dm)
    for _, c in ipairs(MOCK.draws) do if c[1] == "polygon" then polys = polys + 1 end end
  end
  check("minimap arrow to an out-of-scene dig draws (flashing: two of four ticks)", polys > 0, polys)
  MOCK.widgets[10551326] = nil
  MOCK.canvas = saved_canvas
  set_quest_var(3)
end

-- Veos: back to state 0, spawn him, and the world pass outlines his model
set_quest_var(0)
place_player(3226, 3240)
local veos = { server_slot = 17, npc_id = 8484, base_npc_id = 8484, name = "Veos", combat_level = 0, size = 1,
  true_x = 3228, true_z = 3242, level = 0, fine_x = 0, fine_z = 0, element_id = 901, visible_ops = 1,
  health_ratio = -1, health_scale = -1 }
do
  local ox, oz = api.world.scene_origin()
  veos.fine_x, veos.fine_z = (veos.true_x - ox) * 128 + 64, (veos.true_z - oz) * 128 + 64
end
MOCK.npcs = { veos }
plugin.on_npc_spawn(api, veos)
tick(1)
MOCK.draws = {}
plugin.on_draw_world(api, MOCK_DRAW_AWT())
local hulls = 0
for _, c in ipairs(MOCK.draws) do if c[1] == "world_hull" and c[2] == 901 then hulls = hulls + 1 end end
check("world pass highlights Veos's model", hulls > 0, hulls)

-- ------------------------------------------------------------------ the spade requirement with the inventory
local spade = xh.spade
check("spade requirement exists", spade ~= nil)
local client = J.rl_client.client()
MOCK.inv[93] = {}
plugin.on_inventory_changed(api, { inv = 93 })
tick(1)
check("spade unmet with an empty pack", spade and spade:check__Client(client) == false)
MOCK.inv[93] = { { 952, 1 } }
plugin.on_inventory_changed(api, { inv = 93 })
tick(1)
check("spade met once it is in the pack", spade and spade:check__Client(client) == true)
MOCK.inv[93] = {}
plugin.on_inventory_changed(api, { inv = 93 })
tick(1)
check("spade unmet again when it leaves", spade and spade:check__Client(client) == false)

-- ItemContainerChanged for a container other than 93/94/95: RuneLite never
-- posts it without a container (QuestHelperPlugin.onItemContainerChanged
-- reads getItemContainer():getItems() first). A shop, group storage (659,
-- read by its id) and a container written with no slots.
do
  local heard = {}
  QH.core.eventBus:register__Class_Consumer_float(J.classof("net.runelite.api.events.ItemContainerChanged"),
    J.fn("java.util.function.Consumer", "accept", function(_, e) heard[#heard + 1] = e end), 0)
  MOCK.inv[516] = { { 1925, 5 }, { 1931, 3 }, size = 40 }
  plugin.on_inventory_changed(api, { inv = 516 })
  local e = heard[#heard]
  check("a shop's ItemContainerChanged carries its container", e ~= nil and e:getItemContainer() ~= nil
    and e:getItemContainer():getItems().n == 40 and e:getItemContainer():getItems()[2]:getId() == 1931
    and e:getItemContainer():count__int(1925) == 5)
  MOCK.inv[659] = { { 2434, 2 }, size = 4 }
  plugin.on_inventory_changed(api, { inv = 659 })
  local gs = C["com.questhelper.managers.QuestContainerManager"].getGroupStorageData():getItems__()
  local found = false
  for i = 1, (gs and gs.n or 0) do if gs[i] and gs[i]:getId() == 2434 then found = true end end
  check("group storage (659) reaches QuestContainerManager.getGroupStorageData", found)
  MOCK.inv[600] = { size = 0 }
  local before = #heard
  plugin.on_inventory_changed(api, { inv = 600 })
  e = heard[#heard]
  check("a container written with no slots posts an EMPTY container, not nil", #heard == before + 1
    and e:getItemContainer() ~= nil and e:getItemContainer():getItems().n == 0)
  MOCK.inv[516], MOCK.inv[659], MOCK.inv[600] = nil, nil, nil
end

-- ------------------------------------------------------------------ the fan-out the critic found missing
local bus = QH.core.eventBus
local heard_events = {}
local function listen(key)
  heard_events[key] = {}
  bus:register__Class_Consumer_float(J.classof(key), J.fn("java.util.function.Consumer", "accept", function(_, e)
    local l = heard_events[key]; l[#l + 1] = e
  end), 0)
end
local EV = "net.runelite.api.events."
for _, k in ipairs({ "VarClientIntChanged", "InteractingChanged", "GraphicsObjectCreated", "ScriptPostFired",
  "WallObjectDespawned", "WallObjectSpawned", "GameObjectSpawned" }) do listen(EV .. k) end
listen("net.runelite.client.events.ServerNpcLoot")
listen("net.runelite.client.events.ClientShutdown")

-- VarClientIntChanged: a seeded dial varc moves; one event, its index
MOCK.varcs_int[1113] = 7
frame()
local vc = heard_events[EV .. "VarClientIntChanged"]
check("VarClientIntChanged posted for a seeded varc", #vc == 1 and vc[1]:getIndex() == 1113, #vc)
frame()
check("VarClientIntChanged not posted again for an unchanged varc", #vc == 1, #vc)

-- InteractingChanged: Veos turns to face the player
veos.interacting = 32768 + MOCK.player.server_pid
tick(1)
local ic = heard_events[EV .. "InteractingChanged"]
check("InteractingChanged posted", #ic >= 1, #ic)
check("InteractingChanged names Veos and the player", #ic >= 1 and ic[#ic]:getSource():getName() == "Veos"
  and ic[#ic]:getTarget() ~= nil and ic[#ic]:getTarget():getName() == "Tester")
check("an actor's interacting reads from the snap", C["net.runelite.api.NPC"].__bySlot(17):getInteracting() ~= nil)
veos.anim_id, veos.orientation = 866, 1024
check("an actor's animation and orientation read from the snap",
  C["net.runelite.api.NPC"].__bySlot(17):getAnimation() == 866 and C["net.runelite.api.NPC"].__bySlot(17):getOrientation() == 1024)
veos.anim_id, veos.orientation = -1, 0
local before_ic = #ic
tick(1)
check("InteractingChanged not posted while it holds", #ic == before_ic, #ic)

-- GraphicsObjectCreated from on_map_anim
plugin.on_map_anim(api, { tile_x = 3227, tile_z = 3241, level = 0, spotanim = 86, height = 0, delay = 0, cycle = 5 })
local go = heard_events[EV .. "GraphicsObjectCreated"]
check("GraphicsObjectCreated posted", #go == 1 and go[1]:getGraphicsObject():getId() == 86, #go)
do
  local lp = go[1] and go[1]:getGraphicsObject():getLocation()
  local w = lp and C["net.runelite.api.coords.WorldPoint"].fromLocal__Client_LocalPoint(client, lp)
  check("GraphicsObject on its tile", w ~= nil and w:getX() == 3227 and w:getY() == 3241, w and w:toString())
end

-- on_loc_changed: a door replaced -> WallObjectDespawned(old) + WallObjectSpawned(new, the real snapshot)
do
  local door = { loc_id = 1531, name = "Door", tile_x = 3226, tile_z = 3240, level = 0, size_x = 1, size_z = 1,
    shape = 0, angle = 1, element_id = 4242, interactive = 1, visible_ops = 1 }
  MOCK.scenery = { door }
  plugin.on_loc_changed(api, { tile_x = 3226, tile_z = 3240, level = 0, shape = 0, angle = 1, loc_id = 1531,
    old_loc_id = 1530, old_shape = 0, old_angle = 0 })
  local wd, ws = heard_events[EV .. "WallObjectDespawned"], heard_events[EV .. "WallObjectSpawned"]
  check("WallObjectDespawned posted with the old id", #wd == 1 and wd[1]:getWallObject():getId() == 1530, #wd)
  check("WallObjectSpawned posted with the new loc", #ws == 1 and ws[1]:getWallObject():getId() == 1531, #ws)
  check("the spawned loc is the scene's (drawable)", #ws == 1 and ws[1]:getWallObject().s.element_id == 4242)
  plugin.on_loc_changed(api, { tile_x = 3226, tile_z = 3240, level = 0, shape = 0, angle = 1, loc_id = -1,
    old_loc_id = 1531, old_shape = 0, old_angle = 1 })
  check("a removal posts only the despawn", #wd == 2 and #ws == 1, #wd .. "/" .. #ws)
  MOCK.scenery = {}
end

-- Script hooks: the entry watches its ids; WorldMapAreaManager reads the area
-- on ScriptPostFired(WORLDMAP_LOADMAP 1712) and ScriptPreFired(1711, the
-- clicked map row is the event's source).
do
  check("the entry watches its script hooks", MOCK.watched[505] and MOCK.watched[514] and MOCK.watched[281]
    and MOCK.watched[750] and MOCK.watched[1711] and MOCK.watched[1712])
  check("6555 is not watched (another script in this pack)", MOCK.watched[6555] == nil)
  local MAPLIST = 38993946
  MOCK.widgets[MAPLIST] = { type = 0, width = 100, height = 100,
    dynamic = { { type = 4, text = "" }, { type = 4, text = "" }, { type = 4, text = "" }, { type = 4, text = "" },
      { type = 4, text = "Gielinor Surface" } } }
  plugin.on_interface(api, { group = 595, open = true })
  frame()
  local wm = QH.core.plugin.worldMapAreaManager
  local WMA = C["com.questhelper.util.worldmap.WorldMapArea"]
  local ref = { ints = {}, strs = {} }
  plugin.on_script_post(api, { script_id = 1712, invoked = true, ref = ref })
  check("world map area read at WORLDMAP_LOADMAP post", wm:getWorldMapArea() == WMA.GIELINOR_SURFACE, tostring(wm:getWorldMapArea()))
  MOCK.widgets[MAPLIST].dynamic[5].text = "Keldagrim"
  plugin.on_script_post(api, { script_id = 1712, invoked = true, ref = ref })
  check("world map area follows the map list", wm:getWorldMapArea() == WMA.KELDAGRIM, tostring(wm:getWorldMapArea()))
  -- the map list row clicked: its dynamic index is the area's ordinal
  local rowh = api.widgets.get(MAPLIST):dynamic_children()[1]
  plugin.on_script_pre(api, { script_id = 1711, invoked = false, ref = ref, widget = rowh })
  check("ScriptPreFired(1711) reads the clicked row's index", wm:getWorldMapArea() == WMA.fromId(0), tostring(wm:getWorldMapArea()))
  local n = #heard_events[EV .. "ScriptPostFired"]
  frame()
  check("no ScriptPostFired without the VM's hook", #heard_events[EV .. "ScriptPostFired"] == n)
  plugin.on_interface(api, { group = 595, open = false })
  MOCK.widgets[MAPLIST] = nil
end

-- BANKMAIN_SEARCHING (514) post: the return value is the top of the stack;
-- the bank tab answers 1 while its tab is active (and leaves it otherwise).
do
  local tab = QH.core.plugin.questBankManager.questBankTab
  local bti = tab and tab.questBankTabInterface
  check("the bank tab is injected", bti ~= nil)
  if bti then
    local ref = { ints = { 0 }, strs = {} }
    plugin.on_script_post(api, { script_id = 514, invoked = true, ref = ref })
    check("514 post leaves the return value with the tab closed", ref.ints[1] == 0, ref.ints[1])
    bti:setQuestTabActive(true)
    plugin.on_script_post(api, { script_id = 514, invoked = true, ref = ref })
    check("514 post makes the bank search while the quest tab is active", ref.ints[1] == 1, ref.ints[1])
    bti:setQuestTabActive(false)
  end
end

-- ServerNpcLoot: an npc whose bar reached empty despawns and drops bones on its tile
do
  local goblin = { server_slot = 40, npc_id = 8484, base_npc_id = 8484, name = "Veos", combat_level = 0, size = 1,
    true_x = 3230, true_z = 3244, level = 0, fine_x = 0, fine_z = 0, element_id = 950, visible_ops = 1,
    health_ratio = 0, health_scale = 30, interacting = -1 }
  MOCK.npcs = { veos, goblin }
  plugin.on_npc_spawn(api, goblin)
  plugin.on_npc_despawn(api, goblin)
  plugin.on_item_spawn(api, { obj_id = 526, count = 1, tile_x = 3230, tile_z = 3244, level = 0, element_id = 951,
    name = "Bones", cost = 1, value = 1 })
  MOCK.npcs = { veos }
  tick(1)
  local loot = heard_events["net.runelite.client.events.ServerNpcLoot"]
  check("ServerNpcLoot posted the tick after the kill", #loot == 1 and loot[1]:getComposition():getId() == 8484, #loot)
  check("ServerNpcLoot carries the drop", #loot == 1 and loot[1]:getItems():size() == 1)
  local alive = { server_slot = 41, npc_id = 8484, base_npc_id = 8484, name = "Veos", combat_level = 0, size = 1,
    true_x = 3231, true_z = 3244, level = 0, fine_x = 0, fine_z = 0, element_id = 952, visible_ops = 1,
    health_ratio = 12, health_scale = 30, interacting = -1 }
  plugin.on_npc_spawn(api, alive)
  plugin.on_npc_despawn(api, alive)
  tick(1)
  check("a despawn with health left is no kill", #loot == 1, #loot)
end

-- ClientToolbar.openPanel -> api.panel.show
do
  MOCK.panel_shown = 0
  api.panel.show = function() MOCK.panel_shown = MOCK.panel_shown + 1 end
  local tb = QH.core.toolbar()
  tb:openPanel(tb.buttons[1] or {})
  check("ClientToolbar.openPanel opens the side panel", MOCK.panel_shown == 1, MOCK.panel_shown)
end

-- A subscriber that hits a module still loading gets the SAME event again once it lands, the others once
do
  local key = EV .. "GameTick"
  local calls, other = 0, 0
  local flaky = J.fn("java.util.function.Consumer", "accept", function()
    calls = calls + 1
    if calls == 1 then error(setmetatable({ pending = "com.questhelper.helpers.quests.cooksassistant.CooksAssistant" }, J.PendingMT), 0) end
  end)
  local steady = J.fn("java.util.function.Consumer", "accept", function() other = other + 1 end)
  local s1 = bus:register__Class_Consumer_float(J.classof(key), flaky, 0)
  local s2 = bus:register__Class_Consumer_float(J.classof(key), steady, 0)
  tick(1)
  check("a pending subscriber's GameTick is not lost", calls >= 2, calls)
  for _ = 1, 3 do frame() end
  check("the replay reaches only the pending subscriber", other == 1 + 0, other)
  bus:unregister__Subscriber(s1)
  bus:unregister__Subscriber(s2)
end

-- The GE item search (on_item_search -> GrandExchangeSearched): only the
-- "quest-helper" tag is answered; at the GE the answer is the quest's items
do
  MOCK.varcs_str[359] = "spade"
  check("a plain GE search is left to the client", plugin.on_item_search(api, { query = "spade", tradeable_only = true }) == nil)
  MOCK.varcs_str[359] = "quest-helper"
  local was_x, was_z = MOCK.player.true_x, MOCK.player.true_z
  place_player(3164, 3489)
  local ids = plugin.on_item_search(api, { query = "quest-helper", tradeable_only = true })
  local has_spade = false
  for _, id in ipairs(ids or {}) do if id == 952 then has_spade = true end end
  check("the quest-helper tag at the GE answers with the quest's items", ids ~= nil and has_spade,
    ids and table.concat(ids, ",") or "nil")
  place_player(was_x, was_z)
  MOCK.varcs_str[359] = ""
end

-- client-APIs round: a "::" command posts CommandExecuted (QuestHelperPlugin's
-- own commands answer only in developer mode, so nothing is consumed), and a
-- jump-on-click world map point under the mouse puts "Focus on" on the menu
do
  listen(EV .. "CommandExecuted")
  local r = plugin.on_command(api, { name = "questhelperdebug", args = { "enable" }, text = "questhelperdebug enable" })
  local ce = heard_events[EV .. "CommandExecuted"]
  check("on_command posts CommandExecuted", #ce == 1 and J.str(ce[1]:getCommand()) == "questhelperdebug" and
    ce[1]:getArguments().n == 1 and J.str(ce[1]:getArguments()[1]) == "enable", #ce)
  check("a command is not consumed outside developer mode", r == nil)

  local WMP = C["net.runelite.client.ui.overlay.worldmap.WorldMapPoint"]
  local wp = C["net.runelite.api.coords.WorldPoint"].__new(3222, 3218, 0)
  local point = WMP.__new__WorldPoint_BufferedImage(wp, C["java.awt.image.BufferedImage"].__new__int_int_int(16, 16, 2))
  point:setName("Quest Helper")
  point:setJumpOnClick(true)
  point:setTooltip("Talk to Veos")
  local mgr = C["net.runelite.client.ui.overlay.worldmap.WorldMapPointManager"].__new()
  mgr:add(point)
  -- the point as the last canvas pass drew it, under the mouse
  point.qh_wmp.box = { x = MOCK.pointer[1] - 4, y = MOCK.pointer[2] - 4, w = 16, h = 16 }
  MOCK.menu_added, MOCK.menu_order, MOCK.map_jumps = {}, nil, {}
  local saved = QH.worldmap_menu
  check("the world map menu hook exists", saved ~= nil)
  -- render_world_map sets the hovered point; the test seam sets it as a draw would
  QH._set_hovered_map_point(point)
  plugin.on_menu_build(api, { hover_pass = false, rows = { { text = "Cancel", action = 1006, type = 1006, npc_slot = -1, player_pid = -1, target_id = -1, component_id = -1, slot = -1 } } })
  local focus
  for _, row in ipairs(MOCK.menu_added) do if row[1]:find("Focus on", 1, true) then focus = row end end
  check("a jump-on-click point offers Focus on", focus ~= nil and focus[1] == "Focus on @lre@Quest Helper",
    focus and focus[1])
  check("Focus on is the top row", MOCK.menu_order and MOCK.menu_order[#MOCK.menu_order].tag == (focus and focus[2]))
  if focus then
    plugin.on_menu_select(api, { owned = true, tag = focus[2], row = { text = focus[1], action = 0 }, x = 0, y = 0 })
  end
  check("Focus on glides the map to the point", MOCK.map_jumps[1] and table.concat(MOCK.map_jumps[1], ",") == "3222,3218,0",
    MOCK.map_jumps[1] and table.concat(MOCK.map_jumps[1], ","))
  mgr:remove(point)
  QH._set_hovered_map_point(nil)
end

-- ------------------------------------------------------------------ errors and stop
local bad = {}
for _, l in ipairs(LOGS) do
  if l:find("quest-helper:", 1, true) and not l:find("quest-helper: started", 1, true) and not l:find("quest-helper: loaded", 1, true) then bad[#bad + 1] = l end
end
check("no dispatch errors logged", #bad == 0, table.concat(bad, "\n  "))
check("every callback within the 20M instruction budget", #over == 0, table.concat(over, "; "))
plugin.on_stop(api)
check("stopped", QH.core.plugin == nil)
check("ClientShutdown posted on stop (the quest bank is saved)", #heard_events["net.runelite.client.events.ClientShutdown"] == 1)
plugin.on_start(api)
for _ = 1, 5 do frame() end
check("restarts after a stop", QH.core.plugin ~= nil and plugin._boot.phase == "running", plugin._boot.phase)

-- ------------------------------------------------------------------ eager ternaries
-- J.cond(c, a, b) evaluates both branches before it is called: a branch that
-- calls a method or constructs must be hoisted by the transpiler
-- (ConditionalStep.makeWidgetOverlayHint called getDefinedPoint on a step
-- without one, live 2026-11-19). Every J.cond branch in gen/ is a plain read.
local function eager_cond_calls(gen_dir)
  local bad = {}
  local p = io.popen("grep -rn 'J.cond(' '" .. gen_dir .. "'")
  for line in p:lines() do
    local args = line:match("J%.cond%((.*)$") or ""
    local depth, cut = 0, nil
    for i = 1, #args do
      local ch = args:sub(i, i)
      if ch == "(" then depth = depth + 1 elseif ch == ")" then depth = depth - 1 end
      if ch == "," and depth == 0 then cut = i; break end
    end
    local branches = cut and args:sub(cut + 1) or ""
    if branches:find("[%w_%]%)]:[%w_]+%(") or branches:find("__new") then bad[#bad + 1] = (line:gsub("^.-/gen/", "")) end
  end
  p:close()
  return bad
end
do
  local bad = eager_cond_calls(os.getenv("QH_GEN_DIR") or (ROOT .. "/gen"))
  check("no J.cond branch calls a method", #bad == 0, table.concat(bad, "\n"))
end

collectgarbage("collect")
print(string.format("test_entry: %d passed, %d failed (boot %d frames, heap %.0f KB after boot, %.0f KB at end, largest callback %d instructions: %s)",
  passes, fails, FRAME, kb_boot, collectgarbage("count"), MAXI, MAXWHAT))
os.exit(fails == 0 and 0 or 1)
