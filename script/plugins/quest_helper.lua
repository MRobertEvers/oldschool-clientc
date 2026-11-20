--
-- Quest Helper
--
-- RuneLite's Quest Helper plugin (Zoinkwiz et al., github.com/Zoinkwiz/
-- quest-helper), transpiled to Lua by tools/questhelper_port/QhToLua.java and
-- run on a Java + RuneLite runtime written over this client's plugin api.
-- docs/QUEST_HELPER_PORT.md is the contract; this file is the plugin entry:
--
--   * the definition (id, title, the config list -- spliced in by
--     tools/questhelper_port/build.sh from gen_config_decl.lua, so it matches
--     what rl_core's ConfigManager decides is a client key),
--   * the BOOT: everything lives in modules beside this file
--     (quest_helper/rt, quest_helper/gen, quest_helper/ui), read through
--     api.sandbox.module_request / module_load. on_start asks for the runtime
--     and the framework; on_frame_start loads one module a frame in
--     gen/index.lua's order, and once all are in runs QuestHelperPlugin's
--     startUp through the injector exactly as RuneLite's PluginManager does.
--     Quest modules (gen/helpers/**, gen/playerquests/*) load ON DEMAND:
--     QH.load_quest for the panel, and J.try_class / J.request_class /
--     J.on_missing for a class the code reaches first. Until a quest module
--     is in, its helper is a J.lazy_new proxy whose QuestHelper facade answers
--     the setters and state reads the list needs (rt/j_core.lua).
--   * the FAN-OUT: every plugin callback becomes the RuneLite event the Java
--     subscribes to, posted on rl_core's EventBus (on_server_tick -> GameTick,
--     on_var_changed -> VarbitChanged with varbit ids, on_interface ->
--     WidgetLoaded/WidgetClosed, ...); the draw passes go to rl_overlay's
--     QH.render, the panel callbacks to ui/panel.lua's QH.ui.
--
-- Errors: every dispatch is protected. A module still loading (J.PendingMT)
-- is fetched and the dispatch -- when it was a one-off (a click, a config
-- change, a chat line) -- run again once the module lands; a periodic one
-- (tick, frame, draw) just runs again on its next turn. Any other error is
-- logged once per distinct message and the plugin carries on, as RuneLite's
-- EventBus logs a subscriber's exception and keeps going.
--
-- Ships disabled (plugins.ini enabled=0). A large callback budget
-- (budget=20000000) because opening a quest builds its whole step graph.
--

---@type torirs.Plugin
local plugin = {
    id      = "quest-helper",
    title   = "Quest Helper",
    version = "4.12.0",
    config  = {
        -- BEGIN GENERATED CONFIG (tools/questhelper_port/build.sh; do not edit)
        { key = "autostart_quests", type = "bool", label = "Auto start helper", default = true },
        { key = "auto_open_sidebar", type = "bool", label = "Auto open sidebar", default = true },
        { key = "show_overlay_panel", type = "bool", label = "Display overlay on screen", default = true },
        { key = "stew_boosts_panel", type = "bool", label = "Use Spicy stew for boosts", default = false },
        { key = "show_fan", type = "bool", label = "Fan appears on quest completion", default = false },
        { key = "highlight_items_background", type = "bool", label = "Highlight missing items", default = false },
        { key = "highlight_needed_quest_items", type = "bool", label = "Include quest items", default = true },
        { key = "highlight_needed_miniquest_items", type = "bool", label = "Include miniquest items", default = true },
        { key = "highlight_needed_achievement_diary_items", type = "bool", label = "Include achievement diary items", default = true },
        { key = "show_full_requirements", type = "bool", label = "Show full quest requirements", default = false },
        { key = "hide_quest_rewards", type = "bool", label = "Hide quest rewards", default = false },
        { key = "show_text_highlight", type = "bool", label = "Highlight correct dialog", default = true },
        { key = "show_symbol_overlay", type = "bool", label = "Display icons on NPCs and objects", default = true },
        { key = "highlight_style_npcs", type = "enum", label = "Highlight style NPCs", default = "OUTLINE", choices = "NONE|OUTLINE|CONVEX_HULL|TILE" },
        { key = "highlight_style_objects", type = "enum", label = "Highlight style objects", default = "OUTLINE", choices = "NONE|CLICK_BOX|OUTLINE" },
        { key = "highlight_style_ground_items", type = "enum", label = "Highlight style ground items", default = "OUTLINE", choices = "NONE|CLICK_BOX|OUTLINE|TILE" },
        { key = "highlight_style_inventory_items", type = "enum", label = "Highlight style inventory items", default = "FILLED_OUTLINE", choices = "NONE|SQUARE|OUTLINE|FILLED_OUTLINE" },
        { key = "outline_thickness", type = "int", label = "Outline thickness", default = 4, min = 0, max = 50 },
        { key = "outline_feathering", type = "int", label = "Outline feathering", default = 4, min = 0, max = 4 },
        { key = "show_mini_map_arrow", type = "bool", label = "Display arrows on the mini-map and overworld", default = true },
        { key = "have_minimap_arrow_flash", type = "bool", label = "Have the minimap arrow flash", default = true },
        { key = "show_world_lines", type = "bool", label = "Display navigation paths", default = true },
        { key = "show_widget_hints", type = "bool", label = "Display widget hints", default = true },
        { key = "solve_puzzles", type = "bool", label = "Show Puzzle Solutions", default = true },
        { key = "show_world_map_point", type = "bool", label = "Display world map point", default = true },
        { key = "use_shortest_path", type = "bool", default = false },
        { key = "text_highlight_color", type = "color", label = "Text highlight colour", default = "#0000FF" },
        { key = "target_overlay_color", type = "color", label = "Color of target overlay", default = "#00FFFF" },
        { key = "pass_colour", type = "color", label = "Colour of passed requirements/checks", default = "#00FF00" },
        { key = "fail_colour", type = "color", label = "Colour of failed requirements/checks", default = "#FF0000" },
        { key = "partial_success_colour", type = "color", label = "Colour of partially passed requirements/checks", default = "#FFFFFF" },
        { key = "boost_colour", type = "color", label = "Colour of boostable skill", default = "#FFC800" },
        { key = "debug_color", type = "color", default = "#FF00FF" },
        { key = "order_list_by", type = "enum", label = "Quest order", default = "A_TO_Z", choices = "A_TO_Z|Z_TO_A|OPTIMAL|OPTIMAL_IRONMAN|RELEASE_DATE|QUEST_POINTS_ASC|QUEST_POINTS_DESC" },
        { key = "show_completed_quests", type = "bool", label = "Show Completed Quests", default = false },
        { key = "region_filter_visibility", type = "enum", label = "Region filtering", default = "AUTO", choices = "AUTO|SHOW|HIDE" },
        { key = "dev_show_overlay_on_launch", type = "bool", default = false },
        -- END GENERATED CONFIG
    },
}

local PLUGIN_CLASS = "com.questhelper.QuestHelperPlugin"
local PLUGIN_NAME = "Quest Helper"

-- The runtime, in load order (each file only J.defs, except j_core, which
-- must come first, and awt before rl_world/rl_overlay read its classes at
-- realisation -- a J.def is lazy, so order past j_core is for clarity).
local RT = {
    "j_core", "j_lang", "j_util", "j_stream", "j_regex", "j_misc",
    "awt", "rl_core", "rl_client", "rl_world", "rl_widgets", "rl_overlay",
}

-- VarbitChanged ids the Java compares against without ever reading the
-- varbit (event.getVarbitId() == N): CUTSCENE_STATUS (542), the rune pouch
-- (QuestContainerManager), Recruitment Drive's 666/667, ... Every varbit the
-- plugin READS is watched too (rl_client), and every quest's progress varbit.
local SEED_VARBITS = { 464, 542, 666, 667, 1777, 18931 }

-- VarClientIntChanged: the varcs the dial puzzles read only from inside
-- that handler (COMBINATION_LOCK_VALUE_0..5 = 1113..1118, Arrow chest 1119,
-- 1120), so a read cannot have watched them first. Every varc the plugin
-- reads is watched too (rl_client).
local SEED_VARCS = { 1113, 1114, 1115, 1116, 1117, 1118, 1119, 1120 }

-- ScriptPreFired / ScriptPostFired: the client VM raises on_script_pre /
-- on_script_post for the ids the plugin watches (api.scripts.watch), so the
-- entry watches every id the Java compares getScriptId() with: the bank tab's
-- BANKMAIN_FINISHBUILDING (505, pre and post), BANKMAIN_SEARCHING (514,
-- post: its return value), BANKMAIN_SEARCH_TOGGLE (281, pre), the GE's
-- SEARCHBOX_LOADED (750, post), and WorldMapAreaManager's
-- WORLD_MAP_NEW_MAP_SELECTED (1711, pre: the clicked row is its source) and
-- WORLDMAP_LOADMAP (1712, post). NOT 6555 (QuestBankTab's
-- POTION_STORE_UPDATED): in this client's script pack 6555 is another
-- script, so the potion-store cache refreshes on its varp alone.
local SCRIPT_HOOKS = { 505, 514, 281, 750, 1711, 1712 }
-- A game tick has run since the plugin started (see on_frame_start's tick end).
local ticked_since_start = false

local API = nil

-- ------------------------------------------------------------------ logging

local logged = {}
local logged_count = 0
local function log_once(what, err)
    local msg = tostring(err)
    if type(err) == "table" and err.__supers and err.toString then
        local ok, s = API.sandbox.protect(err.toString, err)
        if ok then msg = tostring(s) end
    end
    -- line numbers vary with the call; the message is the key
    local key = what .. ": " .. msg:gsub(":%d+:", ":N:")
    if logged[key] then return end
    if logged_count >= 200 then return end
    logged[key] = true
    logged_count = logged_count + 1
    API.core.log("quest-helper: " .. what .. ": " .. msg)
end

local function is_pending(e)
    return type(e) == "table" and rawget(e, "pending") ~= nil
end

-- ------------------------------------------------------------------ modules

local index = nil          -- gen/index.lua
local module_state = {}    -- module path -> "loaded" | "failed"
local wanted = {}          -- module path -> true: requested, load when ready
local retries = {}         -- one-off dispatches waiting for a module: {what, fn, args}
local replays = {}         -- posts whose subscribers went pending: {what, redo = {event, subs}, tries}

local function module_name(path)
    -- "gen/helpers/quests/xmarksthespot" -> "gen.helpers.quests.xmarksthespot"
    return (path:gsub("/", "."))
end

local function request(path)
    return API.sandbox.module_request(module_name(path))
end

-- Load a ready module now (its dependencies first). Raises the module's own
-- error; answers false when something it needs is not ready yet.
local function load_now(path)
    if module_state[path] == "loaded" then return true end
    if module_state[path] == "failed" then error("module " .. path .. " failed to load", 0) end
    local st = request(path)
    if st ~= "ready" then
        if st == "missing" or st == "invalid" then
            module_state[path] = "failed"
            error("module " .. path .. " is " .. st, 0)
        end
        wanted[path] = true
        return false
    end
    module_state[path] = "loaded"
    local ok, err = API.sandbox.protect(API.sandbox.module_load, module_name(path))
    if not ok then
        module_state[path] = "failed"
        error(err, 0)
    end
    return true
end

-- A quest module and every module it depends on (gen/index.lua modules).
local function quest_modules(m, out, seen)
    out, seen = out or {}, seen or {}
    if seen[m] then return out end
    seen[m] = true
    for _, d in ipairs(index.modules[m] or {}) do quest_modules(d, out, seen) end
    out[#out + 1] = m
    return out
end

-- Ready means: this module and its deps are all loaded now.
local function ensure_quest_module(m)
    local all = true
    for _, d in ipairs(quest_modules(m)) do
        if not load_now("gen/" .. d) then all = false end
    end
    return all
end

local function install_loader_hooks()
    -- A class lookup that never raises: the module is loaded when it is
    -- already here; otherwise it is asked for and the caller hears nil.
    function J.try_class(key)
        local c = rawget(C, key)
        if c ~= nil then return c end
        if J.defined(key) then return C[key] end
        local m = index.classes[key]
        if m == nil then return nil end
        if ensure_quest_module(m) and J.defined(key) then return C[key] end
        return nil
    end
    function J.request_class(key)
        local m = index.classes[key]
        if m == nil then return end
        for _, d in ipairs(quest_modules(m)) do
            if module_state["gen/" .. d] ~= "loaded" then
                request("gen/" .. d)
                wanted["gen/" .. d] = true
            end
        end
    end
    -- C[key] for a class no module has defined yet (a quest class reached by
    -- a static call): load it when it is in, else raise Pending.
    J.on_missing(function(key)
        local m = index.classes[key]
        if m == nil then return end
        if ensure_quest_module(m) then return end
        J.request_class(key)
        error(setmetatable({ pending = key }, J.PendingMT), 0)
    end)
    -- A lazy QuestHelper proxy that answered through its facade: the fields a
    -- quest class adds are injected once it is a real instance.
    function J.lazy_base(key)
        local base = index.bases and index.bases[key]
        if base == nil then return nil end
        local b = C[base]
        if not b.__supers["com.questhelper.questhelpers.QuestHelper"] then return nil end
        return base, index.overrides and index.overrides[key] or {}
    end
    function J.lazy_after_force(obj)
        local p = QH.core.plugin
        local inj = p and p.injector or QH.core.injector
        local level = obj.__cls
        while level do
            local d = rawget(level, "__def")
            if d and d.inject then
                for _, f in ipairs(d.inject) do
                    -- objects only: a primitive (@Named) field was set by the facade
                    if rawget(obj, f[1]) == nil and f[2]:find(".", 1, true) then
                        obj[f[1]] = inj:resolve(f[2], f[3] or f[1])
                    end
                end
            end
            level = rawget(level, "__super")
        end
    end
end

-- ------------------------------------------------------------------ dispatch

-- Run fn(...) protected. A pending module: fetched, and a one-off dispatch
-- (retry = true) queued to run again once it lands. Any other error: logged
-- once. Returns ok, results...
local function dispatch(what, retry, fn, ...)
    local r = table.pack(API.sandbox.protect(fn, ...))
    if r[1] then return table.unpack(r, 1, r.n) end
    local err = r[2]
    if is_pending(err) then
        J.request_class(err.pending)
        if retry then retries[#retries + 1] = { what = what, fn = fn, args = table.pack(...) } end
        return false
    end
    log_once(what, err)
    return false
end

-- Load whatever was asked for and has arrived; once nothing is outstanding,
-- run the queued one-off dispatches again (each at most a few times).
local function pump_modules()
    local outstanding = false
    for path in pairs(wanted) do
        local st = request(path)
        if st == "ready" then
            wanted[path] = nil
            local ok, err = API.sandbox.protect(load_now, path)
            if not ok then log_once("module " .. path, err) end
        elseif st == "pending" then
            outstanding = true
        else
            wanted[path] = nil
            module_state[path] = "failed"
            log_once("module " .. path, st)
        end
    end
    if outstanding then return end
    if #replays > 0 then
        local rl = replays
        replays = {}
        local bus = QH.core.eventBus
        for _, r in ipairs(rl) do
            local ok, err = API.sandbox.protect(bus.redeliver, bus, r.redo.event, r.redo.subs)
            if not ok and is_pending(err) and rawget(err, "redo") then
                J.request_class(err.pending)
                r.tries = r.tries + 1
                r.redo = err.redo
                if r.tries < 8 then replays[#replays + 1] = r
                else log_once(r.what, "gave up waiting for its module") end
            elseif not ok then
                log_once(r.what, err)
            end
        end
    end
    if #retries == 0 then return end
    local list = retries
    retries = {}
    for _, r in ipairs(list) do
        r.tries = (r.tries or 0) + 1
        local before = #retries
        dispatch(r.what, true, r.fn, table.unpack(r.args, 1, r.args.n))
        -- a dispatch that went pending again was re-queued; give up after 8
        if #retries > before and r.tries >= 8 then
            table.remove(retries)
            log_once(r.what, "gave up waiting for its module")
        elseif #retries > before then
            retries[#retries].tries = r.tries
        end
    end
end

-- ------------------------------------------------------------------ QH.load_quest

local quest_loads = {}  -- { helper, module, callback }
local note_used         -- (module) residency: the module was opened (set below)
local install_list_jobs -- () the quest list's frame-spread jobs (set below)

local function quest_load_step(q)
    local helper = q.helper
    if J.lazy_forced(helper) then return true end
    if not ensure_quest_module(q.module) then return false end
    local ok, err = API.sandbox.protect(J.force, helper)
    if ok then return true end
    if is_pending(err) then
        local m = index.classes[err.pending]
        if m then q.module = m end
        J.request_class(err.pending)
        return false
    end
    error(err, 0)
end

local function quest_load_poll()
    if #quest_loads == 0 then return end
    local list = quest_loads
    quest_loads = {}
    for _, q in ipairs(list) do
        local ok, done = API.sandbox.protect(quest_load_step, q)
        if not ok then
            log_once("loading " .. tostring(q.name), done)
            dispatch("quest loaded callback", false, q.callback, false, tostring(done))
        elseif done then
            -- a quest's step graph is the largest thing built at once: collect
            -- what building it left, and say what stays
            collectgarbage("collect")
            API.core.log(string.format("quest-helper: loaded %s (%d KB Lua heap live)", q.module, math.floor(collectgarbage("count"))))
            dispatch("quest loaded callback", true, q.callback, true)
        else
            quest_loads[#quest_loads + 1] = q
        end
    end
end

-- The panel's: load the module(s) of a QuestHelperQuest's helper, then
-- callback(true) or callback(false, reason). May call back synchronously.
local function load_quest(quest, callback)
    assert(quest)
    assert(callback)
    local helper = quest:getQuestHelper()
    assert(helper)
    if J.lazy_forced(helper) then
        callback(true)
        return
    end
    local key = J.lazy_key(helper)
    local m = index.classes[key]
    assert(m, "quest-helper: no module for " .. tostring(key))
    note_used(m)
    local q = { helper = helper, module = m, callback = callback, name = key }
    local ok, done = API.sandbox.protect(quest_load_step, q)
    if not ok then
        callback(false, tostring(done))
        return
    end
    if done then
        callback(true)
        return
    end
    quest_loads[#quest_loads + 1] = q
end

-- ------------------------------------------------------------------ boot

-- "requesting" -> "loading" (one module a frame) -> "config" -> "running";
-- "failed" stops (logged).
local boot = { phase = "idle", queue = nil, at = 1 }
local boot_quest_modules

local function boot_queue()
    local q = {}
    for _, f in ipairs(RT) do q[#q + 1] = "rt/" .. f end
    q[#q + 1] = "gen/index"
    return q
end

-- Quest modules startUp and login cannot do without, loaded with the
-- framework: the "Check items" helper (QuestManager.setupOnLogin inits it at
-- every login), and every quest class that overrides a state read the quest
-- list asks of all helpers (a facade cannot answer an override).
local BOOT_CLASSES = { "com.questhelper.helpers.mischelpers.allneededitems.AllNeededItems" }
function boot_quest_modules()
    local seen, out = {}, {}
    local function add(m)
        for _, d in ipairs(quest_modules(m)) do
            if not seen[d] then seen[d] = true; out[#out + 1] = d end
        end
    end
    for _, k in ipairs(BOOT_CLASSES) do add(index.classes[k]) end
    local keys = {}
    for k in pairs(index.overrides or {}) do keys[#keys + 1] = k end
    table.sort(keys)
    for _, k in ipairs(keys) do
        for name in pairs(index.overrides[k]) do
            if J.FACADE_METHODS[name] and index.classes[k] then add(index.classes[k]); break end
        end
    end
    return out
end

-- ItemContainerChanged for the containers QuestHelperPlugin.onItemContainerChanged
-- acts on (backpack, worn, bank, group storage 659 and its inventory 660),
-- when the client has them.
local function post_containers()
    local client = J.rl_client.client()
    for _, inv in ipairs({ 93, 94, 95, 659, 660 }) do
        J.rl_client.inventory_changed(inv)
        local c = client:getItemContainer__int(inv)
        if c ~= nil then
            dispatch("ItemContainerChanged", false, QH.core.post,
                "net.runelite.api.events.ItemContainerChanged", { containerId = inv, itemContainer = c })
        end
    end
end

local function seed_varbits()
    local w = J.rl_client.watched_varbits
    for _, id in ipairs(SEED_VARBITS) do
        if w[id] == nil then w[id] = API.cache.varbit(id) end
    end
    local QV = C["com.questhelper.questinfo.QuestVarbits"]
    local vs = QV.values()
    for i = 1, vs.n do
        local id = vs[i]:getId()
        if w[id] == nil then w[id] = API.cache.varbit(id) end
    end
    -- QuestContainerManager.updateRunePouch tests getVarbitId() against a SET
    local pouch = C["com.questhelper.managers.QuestContainerManager"].ALL_RUNE_POUCH_VARBITS
    local it = pouch:iterator()
    while it:hasNext() do
        local id = it:next()
        if w[id] == nil then w[id] = API.cache.varbit(id) end
    end
    local wc = J.rl_client.watched_varcs
    for _, id in ipairs(SEED_VARCS) do
        if wc[id] == nil then wc[id] = API.cache.varc_int(id) end
    end
end

local toolbar_listening = false

-- GlobalFakeObjects.createNpcs sets its `initialized` latch BEFORE the
-- Cheerer builds its fans, and building them reads npc and obj records
-- (Wise Old Man's models, Zoinkwiz's cape): a record still loading raises
-- J.PendingMT half way, the dispatch replays GameStateChanged once it lands,
-- and the replay found the latch set -- no fan ever existed and the quest
-- completion's Cheerer.activateCheerer threw on an empty list (found live).
-- A pending build is undone (its half-made group removed, the latch
-- cleared) so the replay builds it whole. RuneLite's records are never
-- pending, so its code has no such window.
local function guard_global_fakes()
    local G = C["com.questhelper.runeliteobjects.GlobalFakeObjects"]
    if G == nil or rawget(G, "__qh_guarded") then return end
    local create = G.createNpcs
    G.createNpcs = function(client, objects, config_manager, config)
        local was = G.initialized
        local ok, err = API.sandbox.protect(create, client, objects, config_manager, config)
        if ok then return end
        if not was and G.initialized then
            API.sandbox.protect(objects.removeGroupAndSubgroups, objects, "global")
            G.initialized = false
        end
        error(err, 0)
    end
    rawset(G, "__qh_guarded", true)
end

-- The same window in the bank tab: QuestBankTab lays the tab out from a
-- ClientThread.invokeAtTickEnd runnable (sortBankTabItems), which reads item
-- records; one still loading raises J.PendingMT after part of the layout was
-- added, and the queue runs it again once the record lands. Run again
-- whole, the half-run's widgets were never removed (each layout doubled),
-- and a run that landed after the player had turned the tab off put its
-- labels over the native bank (found live after closing and reopening the
-- bank with the tab on). RuneLite runs the layout once, at the tick's end,
-- straight after removeAddedWidgets; so a run here starts from
-- removeAddedWidgets too, and a run for a tab no longer shown does nothing.
local function guard_bank_layout()
    local T = C["com.questhelper.bank.banktab.QuestBankTab"]
    if T == nil or rawget(T, "__qh_guarded") then return end
    local sort = T.sortBankTabItems
    T.sortBankTabItems = function(self, container, children, layout)
        if self.questBankTabInterface == nil or not self.questBankTabInterface:isQuestTabActive() then return end
        self:removeAddedWidgets()
        return sort(self, container, children, layout)
    end
    rawset(T, "__qh_guarded", true)
end

local function start_plugin()
    guard_global_fakes()
    guard_bank_layout()
    local root = QH.core.boot()
    if root:binding("net.runelite.api.Client") == nil then
        root:bind("net.runelite.api.Client", J.rl_client.client())
        -- RuneLite's --developer-mode: off, as for every player. on_command
        -- consumes the plugin's own commands only when it is on.
        QH.developer_mode = false
        root:bind_named("developerMode", QH.developer_mode)
    end
    -- ClientToolbar.openPanel (QuestHelperPlugin.displayPanel, when
    -- autoOpenSidebar is on) opens this plugin's page in the side panel.
    if not toolbar_listening then
        toolbar_listening = true
        QH.core.toolbar():listen(function(kind)
            if kind == "open" then API.panel.show() end
        end)
    end
    install_list_jobs()
    for _, id in ipairs(SCRIPT_HOOKS) do
        local ok, why = API.scripts.watch(id, true)
        if not ok then log_once("script hook " .. id, "scripts.watch refused: " .. tostring(why)) end
    end
    QH.core.start_plugin(PLUGIN_CLASS, PLUGIN_NAME)
    seed_varbits()
    -- RuneLite delivers ItemContainerChanged for every container as it loads;
    -- here they may have loaded while the modules were still being read (a
    -- login during the boot), and on_inventory_changed only reports CHANGES.
    -- So every readable container is posted once, as at a login.
    post_containers()
end

-- One step of the boot, from on_frame_start.
local function boot_step()
    if boot.phase == "loading" then
        local path = boot.queue[boot.at]
        local st = request(path)
        if st == "pending" then return end
        if st ~= "ready" then
            boot.phase = "failed"
            log_once("boot", "module " .. path .. " is " .. st)
            return
        end
        module_state[path] = "loaded"
        local result = API.sandbox.module_load(module_name(path))
        if path == "gen/index" then
            index = result
            install_loader_hooks()
            boot.queue[#boot.queue + 1] = "gen/enums"
            for _, m in ipairs(index.framework) do boot.queue[#boot.queue + 1] = "gen/" .. m end
            boot.queue[#boot.queue + 1] = "ui/panel"
            for _, m in ipairs(boot_quest_modules()) do boot.queue[#boot.queue + 1] = "gen/" .. m end
            for i = boot.at + 1, #boot.queue do request(boot.queue[i]) end
        end
        boot.at = boot.at + 1
        if boot.at > #boot.queue then
            boot.phase = "config"
            QH.core.boot()
        end
        return
    end
    if boot.phase == "config" then
        if QH.core.config_step() ~= "ready" then return end
        boot.phase = "starting"
    end
    if boot.phase == "starting" then
        start_plugin()
        boot.phase = "running"
        API.core.log(string.format("quest-helper: started (%d modules, %d KB Lua heap)",
            #boot.queue, math.floor(collectgarbage("count"))))
    end
end

-- ------------------------------------------------------------------ residency
--
-- RuneLite holds every quest helper in memory, so the quest list's skill
-- filter and "Show meets reqs", the Check-items totals and the background
-- item highlight read the requirements of ALL of them. Here a quest module
-- is code the plugin heap cannot hold all of (~14 MB for 272), so once the
-- player is in game a background pass, on idle frames, loads each quest
-- module in turn (one a frame, the next one's read queued meanwhile), builds a
-- throwaway SHADOW of each of its helpers (rt/j_core.lua J.lazy_shadow), runs
-- its initializeRequirements and keeps what the list features ask of a helper
-- (J.FACADE_HARVEST: item requirements and recommendations, general
-- requirements and recommendations, experience rewards) on the helper's
-- facade, then UNLOADS the module: its classes leave the registry
-- (J.undef), the sandbox forgets it (api.sandbox.module_unload), and its code
-- is garbage. Opening the quest later reads the module again. When the pass
-- is through the list is updated (QuestManager.loadQuestList, RuneLite's own
-- login path), so every filter and total sees every quest.
--
-- General requirements and experience rewards are the quest's own objects
-- (the filters call check(client) on them live). Item lists are kept as
-- COMPACT ItemRequirements -- name, id, quantity, consumed -- which is all
-- their one reader (AllNeededItems.refinedList) uses; the full objects are
-- ~4 KB each (eight container states, alternates, ...), 7 MB for every quest.
--
-- A quest opened and closed again is evicted the same way (its answers taken
-- from the instance, the instance turned back into its facade, the module
-- unloaded) while the live heap is over evict_kb (the cap less 6 MB) and it
-- has not been opened for evict_age frames; the selected quest, a background
-- helper and anything the event bus holds are never evicted.
--
-- Without api.sandbox.module_unload a module cannot be read twice (the
-- sandbox compiles each one once), so nothing is unloaded and the pass stops
-- before the live heap passes ceiling_kb (the cap less 8 MB, room for the
-- largest quest's step graph and the collector's slack): the quests it did
-- not reach answer null as before (logged once). Every module kept takes
-- ~29 MB, so that mode needs a larger cap (a manifest memory_mb=, read
-- through api.sandbox.memory_limit when the sandbox has it) to see them all.
--
-- The live heap after the pass with unloading is ~8.6 MB (harness): ~1.1 MB of
-- harvested answers over the ~5.7 MB of a login, the rest framework classes
-- the requirements realised and the pinned modules' helpers.

-- J.FACADE_HARVEST in a fixed order (the overview panel's), so a harvest
-- asks its getters the same way every run
local HARVEST = { "getGeneralRequirements", "getGeneralRecommended", "getItemRequirements",
    "getItemRecommended", "getExperienceRewards" }

local function heap_cap_kb()
    local f = API.sandbox.memory_limit
    if f then return f() // 1024 end
    return 16 * 1024
end

local residency = {
    ceiling_kb = nil,    -- no unloading: stop harvesting past this live heap (cap - 8 MB)
    evict_kb = nil,      -- evict closed quests while the live heap is past this (cap - 6 MB)
    phase = "idle",      -- "idle" -> "harvesting" -> "done"
    touched = {},        -- module -> true: loaded by the pass (tests)
    queue = nil,         -- quest module names (index.modules keys), QuestHelperQuest order
    at = 1,
    helpers = nil,       -- module -> { proxy, ... } (QuestHelperQuest helpers whose class is there)
    classes = nil,       -- module -> { class key, ... }
    dependents = nil,    -- module -> { module that depends on it, ... }
    pinned = nil,        -- module -> true: loaded with the framework, never unloaded
    used = {},           -- module -> residency frame of its last QH.load_quest
    frame = 0,           -- idle in-game frames the pass has seen
    evict_age = 1500,    -- a quest opened this many residency frames ago may be evicted
    evict_every = 60,    -- frames between live-heap checks for eviction
    live_at = 0,
    harvested = 0,       -- helpers harvested
    unloaded = 0,        -- modules unloaded
    failed = 0,          -- helpers whose harvest raised (they answer null)
    peak_kb = 0,
}

function note_used(m)
    residency.used[m] = residency.frame
end

-- The collector's pause (on_start sets 150%, GC_PAUSE) lowered while the
-- live heap is high, so a cycle starts before live * pause passes the cap
-- less 1 MB: with every helper's answers kept and a quest open, 150% of live
-- would pass the cap and every allocation past it is an emergency collection.
local function tune_gc(live_kb)
    local p = math.floor(100 * (heap_cap_kb() - 1024) / math.max(live_kb, 1))
    if p > 150 then p = 150 elseif p < 110 then p = 110 end
    API.sandbox.protect(collectgarbage, "param", "pause", p)
end

local function residency_maps()
    if residency.helpers then return end
    for _, name in ipairs(HARVEST) do assert(J.FACADE_HARVEST[name], name) end
    local n = 0
    for _ in pairs(J.FACADE_HARVEST) do n = n + 1 end
    assert(n == #HARVEST, "HARVEST lists every J.FACADE_HARVEST method")
    local cap = heap_cap_kb()
    residency.ceiling_kb = residency.ceiling_kb or (cap - 8 * 1024)
    residency.evict_kb = residency.evict_kb or (cap - 6 * 1024)
    local helpers, classes, dependents, pinned = {}, {}, {}, {}
    local queue, seen = {}, {}
    for key, m in pairs(index.classes) do
        classes[m] = classes[m] or {}
        table.insert(classes[m], key)
    end
    for m, deps in pairs(index.modules) do
        for _, d in ipairs(deps) do
            dependents[d] = dependents[d] or {}
            table.insert(dependents[d], m)
        end
    end
    for _, m in ipairs(boot_quest_modules()) do pinned[m] = true end
    local vs = C["com.questhelper.questinfo.QuestHelperQuest"].values()
    for i = 1, vs.n do
        local p = vs[i]:getQuestHelper()
        local key = J.lazy_key(p) or (J.lazy_forced(p) and p.__key)
        local m = key and index.classes[key]
        if m then
            helpers[m] = helpers[m] or {}
            table.insert(helpers[m], p)
            if not seen[m] then seen[m] = true; queue[#queue + 1] = m end
        end
    end
    residency.helpers, residency.classes, residency.dependents, residency.pinned = helpers, classes, dependents, pinned
    residency.queue, residency.at = queue, 1
end

-- An ItemRequirement that carries only what AllNeededItems reads: name, id,
-- quantity, consumed. Every other field reads as a fresh
-- `new ItemRequirement(name, id, quantity)` has it; the tables that
-- constructor builds (alternates, container states, the hide condition) are
-- built on the first read of one.
local compact_mt = nil
local function compact_meta()
    if compact_mt then return compact_mt end
    local IR = C["com.questhelper.requirements.item.ItemRequirement"]
    local template = IR.__new__String_int_int("", -1, 1)
    local plain = {}
    for k, v in pairs(template) do
        if type(v) ~= "table" then plain[k] = v end
    end
    -- what a compact item carries itself (nil when the quest's item had none)
    plain.name_, plain.id, plain.quantity_, plain.isConsumedItem_ = nil, nil, nil, nil
    -- quantity(n) of an item harvested with its copy: that copy, at n
    local function quantity_from_copy(self, n)
        local c = rawget(self, "__copy")
        return setmetatable({ name_ = c.name, id = c.id, quantity_ = n, isConsumedItem_ = c.consumed }, compact_mt)
    end
    compact_mt = {
        __index = function(o, k)
            if k == "quantity" and rawget(o, "__copy") ~= nil then return quantity_from_copy end
            if rawget(o, "__full") == nil then
                local v = plain[k]
                if v ~= nil and rawget(o, k) == nil then return v end
                local m = IR[k]
                if m ~= nil then return m end
                -- a table field: build the rest of the instance
                local full = IR.__new__String_int_int(rawget(o, "name_"), rawget(o, "id"), rawget(o, "quantity_"))
                rawset(o, "__full", true)
                for fk, fv in pairs(full) do
                    if rawget(o, fk) == nil and type(fv) == "table" then rawset(o, fk, fv) end
                end
                for pk, pv in pairs(plain) do
                    if rawget(o, pk) == nil then rawset(o, pk, pv) end
                end
                return rawget(o, k)
            end
            return IR[k]
        end,
    }
    return compact_mt
end

local function compact_items(list)
    if list == nil then return nil end
    local out = C["java.util.ArrayList"].__new__()
    local mt = compact_meta()
    local it = list:iterator()
    while it:hasNext() do
        local r = it:next()
        if r ~= nil and r:getQuantity() == -1 then
            -- refinedList asks quantity(1) of it, which copies it with the
            -- item's own class: keep what that copy answers
            local c = r:quantity(1)
            out:add(setmetatable({ name_ = r:getName(), id = r:getId(), quantity_ = -1,
                isConsumedItem_ = r:isConsumedItem(),
                __copy = { name = c:getName(), id = c:getId(), consumed = c:isConsumedItem() } }, mt))
        elseif r ~= nil then
            out:add(setmetatable({ name_ = r:getName(), id = r:getId(), quantity_ = r:getQuantity(),
                isConsumedItem_ = r:isConsumedItem() }, mt))
        else
            out:add(nil)
        end
    end
    return out
end

local ITEM_LISTS = { getItemRequirements = true, getItemRecommended = true }

-- What a helper answers for the list features, read off `o` (a shadow or the
-- forced instance), as the facade will keep it.
local function harvest_answers(o)
    local out = {}
    for _, name in ipairs(HARVEST) do
        local v = o[name](o)
        if ITEM_LISTS[name] then v = compact_items(v) end
        out[name] = v
    end
    return out
end

local function harvest_one(p)
    local o = J.lazy_shadow(p)
    o:initializeRequirements()
    local a = harvest_answers(o)
    for _, name in ipairs(HARVEST) do J.lazy_answer(p, name, a[name]) end
end

local function module_loaded(m) return module_state["gen/" .. m] == "loaded" end

-- Anything the event bus holds that is an instance of one of the module's
-- classes (a started step, a background helper).
local function bus_holds(m)
    local bus = QH.core.eventBus
    assert(bus)
    assert(bus.subs)
    for _, list in pairs(bus.subs) do
        for _, s in ipairs(list) do
            local obj = s.object
            if type(obj) == "table" then
                local key = J.lazy_key(obj)
                if key == nil and J.lazy_forced(obj) then key = rawget(obj, "__key") or obj.__key end
                if key ~= nil and index.classes[key] == m then return true end
            end
        end
    end
    return false
end

local function in_use(p)
    assert(QH.core.plugin)
    local qm = QH.core.plugin.questManager
    assert(qm)
    if rawequal(qm.selectedQuest, p) then return true end
    local bg = qm.backgroundHelpers
    if bg ~= nil then
        local it = bg:values():iterator()
        while it:hasNext() do if rawequal(it:next(), p) then return true end end
    end
    return false
end

local function can_unload(m)
    if not API.sandbox.module_unload then return false end
    if residency.pinned[m] or not module_loaded(m) then return false end
    for _, d in ipairs(residency.dependents[m] or {}) do
        if module_loaded(d) then return false end
    end
    for _, p in ipairs(residency.helpers[m] or {}) do
        if not J.lazy_is_facade(p) or not J.lazy_harvested(p) then return false end
    end
    return not bus_holds(m)
end

local function unload_module(m)
    for _, key in ipairs(residency.classes[m]) do J.undef(key) end
    local path = "gen/" .. m
    module_state[path] = nil
    wanted[path] = nil
    API.sandbox.module_unload(module_name(path))
    residency.unloaded = residency.unloaded + 1
    -- its dependencies may be free now
    for _, d in ipairs(index.modules[m] or {}) do
        if can_unload(d) then unload_module(d) end
    end
end

-- A closed quest's module, unloaded: the helper's answers are read off the
-- instance, the instance becomes its facade again, the module goes.
local function evict_module(m)
    for _, p in ipairs(residency.helpers[m] or {}) do
        if J.lazy_forced(p) and in_use(p) then return false end
    end
    if bus_holds(m) then return false end
    for _, d in ipairs(residency.dependents[m] or {}) do
        if module_loaded(d) then return false end
    end
    for _, p in ipairs(residency.helpers[m] or {}) do
        if J.lazy_forced(p) then
            local a = harvest_answers(p)
            J.lazy_unforce(p)
            for _, name in ipairs(HARVEST) do J.lazy_answer(p, name, a[name]) end
        end
    end
    if not can_unload(m) then return false end
    unload_module(m)
    return true
end

local function residency_idle()
    if #quest_loads > 0 or #retries > 0 or #replays > 0 then return false end
    for path in pairs(wanted) do
        -- a module somebody else asked for is still on its way
        if not (residency.phase == "harvesting" and path == "gen/" .. tostring(residency.queue[residency.at])) then
            return false
        end
    end
    return true
end

local function residency_finish()
    residency.phase = "done"
    collectgarbage("collect")
    local kb = math.floor(collectgarbage("count"))
    tune_gc(kb)
    local total = 0
    for _, list in pairs(residency.helpers) do total = total + #list end
    API.core.log(string.format("quest-helper: residency: %d of %d helpers harvested, %d modules unloaded, %d failed (%d KB Lua heap live)",
        residency.harvested, total, residency.unloaded, residency.failed, kb))
    if residency.at <= #residency.queue then
        log_once("residency", string.format("stopped at %d KB with %d quest modules left: this sandbox cannot unload a module (api.sandbox.module_unload), so the rest answer no requirements",
            kb, #residency.queue - residency.at + 1))
    end
    -- RuneLite's login path: the next updateQuestState re-runs updateQuestList
    -- and getAllItemRequirements over every helper
    assert(QH.core.plugin)
    local qm = QH.core.plugin.questManager
    assert(qm)
    qm.loadQuestList = true
end

-- One step, from on_frame_start on an idle frame in game: at most one module
-- harvested (and unloaded), or one closed quest evicted.
local function residency_step()
    residency.frame = residency.frame + 1
    if residency.phase == "idle" then
        residency_maps()
        residency.phase = "harvesting"
    end
    local kb = collectgarbage("count")
    if kb > residency.peak_kb then residency.peak_kb = kb end
    if residency.phase == "harvesting" then
        local q = residency.queue
        -- skip what needs nothing: every helper already harvested or opened
        while residency.at <= #q do
            local todo = false
            local m = q[residency.at]
            for _, p in ipairs(residency.helpers[m]) do
                if residency.pinned[m] then
                    -- resident anyway, and its answers may be live (Check
                    -- items' are every other quest's): a real instance, as
                    -- RuneLite's
                    if J.lazy_is_facade(p) then todo = true; break end
                elseif J.lazy_is_facade(p) and not J.lazy_harvested(p) then todo = true; break end
            end
            if todo then break end
            residency.at = residency.at + 1
        end
        if residency.at > #q then residency_finish(); return end
        if not API.sandbox.module_unload and kb > residency.ceiling_kb then
            -- the count holds garbage too: judge what a collection leaves
            collectgarbage("collect")
            if collectgarbage("count") > residency.ceiling_kb then residency_finish(); return end
        end
        local m = q[residency.at]
        if q[residency.at + 1] then request("gen/" .. q[residency.at + 1]) end
        if not module_loaded(m) then residency.touched[m] = true end
        if not ensure_quest_module(m) then return end
        for _, p in ipairs(residency.helpers[m]) do
            if residency.pinned[m] and J.lazy_is_facade(p) then
                local ok, err = API.sandbox.protect(J.force, p)
                if not ok and is_pending(err) then J.request_class(err.pending); return end
                if not ok then log_once("residency " .. m, err) end
            elseif J.lazy_is_facade(p) and not J.lazy_harvested(p) then
                local ok, err = API.sandbox.protect(harvest_one, p)
                if ok then
                    residency.harvested = residency.harvested + 1
                elseif is_pending(err) then
                    -- it reached a class of another module: next frame
                    J.request_class(err.pending)
                    return
                else
                    residency.failed = residency.failed + 1
                    log_once("residency " .. m, err)
                    for _, name in ipairs(HARVEST) do J.lazy_answer(p, name, nil) end
                end
            end
        end
        if can_unload(m) then unload_module(m) end
        residency.at = residency.at + 1
        return
    end
    -- done: keep the heap under the eviction line with closed quests, judged
    -- on what a collection leaves (at most every evict_every frames)
    -- (without unloading only the collector's pause can follow the heap:
    -- every tenth interval)
    if kb <= residency.evict_kb then return end
    local every = residency.evict_every
    if not API.sandbox.module_unload then every = every * 10 end
    if residency.frame - residency.live_at < every then return end
    residency.live_at = residency.frame
    collectgarbage("collect")
    tune_gc(collectgarbage("count"))
    if not API.sandbox.module_unload then return end
    if collectgarbage("count") <= residency.evict_kb then return end
    -- least recently opened first; one not opened for evict_age frames
    local cands = {}
    for m in pairs(residency.helpers) do
        if module_loaded(m) and not residency.pinned[m]
            and residency.frame - (residency.used[m] or 0) >= residency.evict_age then
            cands[#cands + 1] = m
        end
    end
    table.sort(cands, function(a, b)
        local ua, ub = residency.used[a] or 0, residency.used[b] or 0
        if ua ~= ub then return ua < ub end
        return a < b
    end)
    for _, m in ipairs(cands) do
        if evict_module(m) then
            residency.live_at = -residency.evict_every   -- look again next frame
            return
        end
    end
end

-- ------------------------------------------------------------------ the quest list, spread over frames
--
-- QuestManager.updateQuestList and getAllItemRequirements run RuneLite's
-- stream pipeline over every helper in one go: with every helper answering
-- (the residency pass) that is ~18-31M instructions here (SkillFiltering's
-- recursive passesSkillFilter reads 24 config keys per visit; A_TO_Z sorting
-- runs a regex per comparison), past the 20M callback budget. So the two are
-- replaced (install_list_jobs) by JOBS doing the same thing a few helpers a
-- frame: the same predicates in the same order (filterListBy, difficulty,
-- QuestDetails.showCompletedQuests, SkillFiltering.passesSkillFilter,
-- LeagueFiltering.passesLeagueFilter / the item predicate, isNotCompleted,
-- passesLeagueFilter), the same comparator (config.orderListBy()) in a stable
-- merge sort a bounded number of comparisons a frame (Java's sort is stable
-- too), and the same ending (the panel refresh through SwingUtilities, the
-- maps and the background Check-items start). A new call restarts its job,
-- as a later list update supersedes an earlier one. Within one job,
-- passesSkillFilter answers each helper once (it is a pure function of the
-- config and the requirements, which one update does not change):
-- RuneLite's recursion visits a prerequisite once per quest that needs it.

local LIST_HELPERS_PER_FRAME = 32
local LIST_COMPARES_PER_FRAME = 500

local list_jobs = { list = nil, items = nil }
local skill_memo = nil   -- helper -> passesSkillFilter answer, while a job steps

local function sort_step(job)
    local s = job.sort
    if s == nil then
        s = { a = job.kept, b = {}, n = #job.kept, width = 1, lo = 1 }
        job.sort = s
    end
    local cmp, used = job.order, 0
    while s.width < s.n do
        while s.lo <= s.n do
            local a, b, n = s.a, s.b, s.n
            local lo = s.lo
            local mid = math.min(lo + s.width - 1, n)
            local hi = math.min(lo + 2 * s.width - 1, n)
            local i, j, k = lo, mid + 1, lo
            while i <= mid and j <= hi do
                -- the left run wins a tie: stable
                if cmp:compare(a[j], a[i]) < 0 then b[k] = a[j]; j = j + 1
                else b[k] = a[i]; i = i + 1 end
                k = k + 1
                used = used + 1
            end
            while i <= mid do b[k] = a[i]; i = i + 1; k = k + 1 end
            while j <= hi do b[k] = a[j]; j = j + 1; k = k + 1 end
            s.lo = hi + 1
            if used >= LIST_COMPARES_PER_FRAME then return false end
        end
        s.a, s.b = s.b, s.a
        s.width = s.width * 2
        s.lo = 1
    end
    return true
end

local function sorted_list(job)
    local out = C["java.util.ArrayList"].__new__()
    local a = job.sort and job.sort.a or job.kept
    for i = 1, #job.kept do out:add(a[i]) end
    return out
end

-- One frame of a job: filter LIST_HELPERS_PER_FRAME helpers, or sort, or
-- finish. Raises what a predicate raises (a pending module: the same helper
-- is asked again next frame).
local function job_step(job)
    local n = job.helpers:size()
    if job.at < n then
        local stop = math.min(n, job.at + LIST_HELPERS_PER_FRAME)
        skill_memo = job.skill_memo
        while job.at < stop do
            local h = job.helpers:get(job.at)
            local keep = job.keep(h)
            if job.side then job.side(h) end
            if keep then
                job.kept[#job.kept + 1] = h
                -- a comparator is pure: comparing a helper with itself runs
                -- its key once here, so the first sort of a session does not
                -- meet every key (A_TO_Z's regex, memoised by j_regex) in one frame
                job.order:compare(h, h)
            end
            job.at = job.at + 1
        end
        skill_memo = nil
        return false
    end
    if not sort_step(job) then return false end
    job.finish(sorted_list(job))
    return true
end

local function list_jobs_active() return list_jobs.list ~= nil or list_jobs.items ~= nil end

local function list_jobs_step()
    local kind = list_jobs.list and "list" or (list_jobs.items and "items")
    if not kind then return end
    local job = list_jobs[kind]
    local ok, done = API.sandbox.protect(job_step, job)
    skill_memo = nil
    if not ok then
        if is_pending(done) then
            J.request_class(done.pending)
            -- a comparator that went pending left a merge half done
            job.sort = nil
            return
        end
        list_jobs[kind] = nil
        log_once(kind == "list" and "updateQuestList" or "getAllItemRequirements", done)
        return
    end
    if done and list_jobs[kind] == job then list_jobs[kind] = nil end
end

function install_list_jobs()
    local QMC = C["com.questhelper.managers.QuestManager"]
    local SFC = C["com.questhelper.config.SkillFiltering"]
    local QHQC = C["com.questhelper.questinfo.QuestHelperQuest"]
    local QD = C["com.questhelper.questhelpers.QuestDetails"]
    local LF = C["com.questhelper.config.LeagueFiltering"]
    local QF = C["com.questhelper.QuestHelperConfig$QuestFilter"]
    local LOGGED_IN = C["net.runelite.api.GameState"].LOGGED_IN
    assert(QMC.updateQuestList)
    assert(QMC.getAllItemRequirements)
    assert(QMC.updateAllItemsHelper)
    assert(SFC.passesSkillFilter)
    if rawget(QMC, "__qh_list_jobs") then return end
    rawset(QMC, "__qh_list_jobs", true)

    local passes = SFC.passesSkillFilter
    SFC.passesSkillFilter = function(h)
        local memo = skill_memo
        if memo == nil then return passes(h) end
        local v = memo[h]
        if v == nil then
            v = passes(h)
            memo[h] = v
        end
        return v
    end

    -- QuestManager.updateQuestList
    function QMC.updateQuestList(self)
        if self.client:getGameState() ~= LOGGED_IN then return end
        local config = self.config
        local by, difficulty = config:filterListBy(), config:difficulty()
        local client = self.client
        local completed = C["java.util.HashMap"].__new__()
        list_jobs.list = {
            helpers = QHQC.getQuestHelpers(self:isDeveloperMode()), at = 0, kept = {}, skill_memo = {},
            order = config:orderListBy(),
            keep = function(h)
                return by:test(h) and difficulty:test(h) and QD.showCompletedQuests__QuestHelper(h)
                    and SFC.passesSkillFilter(h) and LF.passesLeagueFilter(h)
            end,
            side = function(h) completed:put(h:getQuest(), h:getState__Client(client)) end,
            finish = function(filtered)
                C["javax.swing.SwingUtilities"].invokeLater(J.fn("java.lang.Runnable", "run", function(_)
                    if self.panel ~= nil then
                        self.panel:refresh(filtered, false, completed, self.config:orderListBy():getSections())
                    end
                end))
            end,
        }
    end

    -- QuestManager.getAllItemRequirements (its own invokeLater is the job);
    -- `after` runs once the maps are rebuilt
    local function items_job(self, after)
        local config = self.config
        local pred = J.fn("java.util.function.Predicate", "test", function(_, h) return false end)
        if config:highlightNeededQuestItems() then pred = pred:or_(QF.QUEST) end
        if config:highlightNeededMiniquestItems() then pred = pred:or_(QF.MINIQUEST) end
        if config:highlightNeededAchievementDiaryItems() then pred = pred:or_(QF.ACHIEVEMENT_DIARY) end
        list_jobs.items = {
            helpers = QHQC.getQuestHelpers(self:isDeveloperMode()), at = 0, kept = {}, skill_memo = {},
            order = config:orderListBy(),
            keep = function(h) return pred:test(h) and QD.isNotCompleted(h) and LF.passesLeagueFilter(h) end,
            finish = function(filtered)
                local reqs = C["java.util.TreeMap"].__new__()
                local recs = C["java.util.TreeMap"].__new__()
                local it = filtered:iterator()
                while it:hasNext() do
                    local h = it:next()
                    if h:getItemRequirements() ~= nil then reqs:put(h:getQuest(), h:getItemRequirements()) end
                    if h:getItemRecommended() ~= nil then recs:put(h:getQuest(), h:getItemRecommended()) end
                end
                self.itemRequirements = reqs
                self.itemRecommended = recs
                local check = QHQC.CHECK_ITEMS
                if self.config:highlightItemsBackground()
                    and not (self.selectedQuest ~= nil and self.selectedQuest:getQuest() == check) then
                    self:shutDownBackgroundQuest(self.backgroundHelpers:get(check:getName()))
                    self:startUpBackgroundQuest(check:getName())
                end
                if after then after() end
            end,
        }
    end
    function QMC.getAllItemRequirements(self) items_job(self, nil) end

    -- QuestManager.updateAllItemsHelper: Check items restarts after the maps
    function QMC.updateAllItemsHelper(self)
        if self.client:getGameState() ~= LOGGED_IN then return end
        items_job(self, function()
            if self.selectedQuest ~= nil and self.selectedQuest:getQuest() == QHQC.CHECK_ITEMS then
                self:startUpQuest__QuestHelper_boolean(QHQC.CHECK_ITEMS:getQuestHelper(), false)
            end
        end)
    end
end

-- ------------------------------------------------------------------ fan-out helpers

local function running() return boot.phase == "running" and QH.core.plugin ~= nil end

local E = "net.runelite.api.events."

-- Post an event. A subscriber that reached a module still loading does not
-- lose it: QH.core.post runs every subscriber and re-raises the pending error
-- with the ones that hit it (rl_core EventBus redo); those get exactly this
-- event again once the module lands (pump_modules), the rest never twice.
local function post(key, fields)
    local ok, err = API.sandbox.protect(QH.core.post, key, fields)
    if ok then return end
    if is_pending(err) and rawget(err, "redo") then
        J.request_class(err.pending)
        replays[#replays + 1] = { what = key, redo = err.redo, tries = 0 }
        return
    end
    error(err, 0)
end

-- Post only when somebody listens (a scene's worth of object spawns would
-- otherwise build thousands of events nobody reads).
local function heard(key)
    local bus = QH.core.eventBus
    return not bus.subscriber_count or bus:subscriber_count(key) > 0
end

local function post_if_heard(key, fields)
    if not heard(key) then return end
    post(key, fields)
end

local function game_state()
    return J.rl_client.client():getGameState()
end

-- RuneLite knows the RS profile BEFORE a login's LOGGED_IN (the account hash
-- arrives with the login), so QuestHelperPlugin's onGameStateChanged
-- (LOGGED_IN && profileChanged) runs its login code once, for the right
-- profile. Here the name can be unknown until the first ticks run. A
-- LOGGED_IN that would be posted while there is no profile is posted as
-- LOADING (what RuneLite's state is until then), and the LOGGED_IN is owed:
-- on_server_tick posts it once the profile is known -- one login, never one
-- for "no profile" and a second for the real one.
local login_owed = false

local function profile_known()
    local cm = QH.core.configManager
    return cm ~= nil and cm.rsProfileKey ~= nil
end

local function post_game_state()
    local GS = C["net.runelite.api.GameState"]
    local gs = game_state()
    if gs == GS.LOGGED_IN and not profile_known() then
        login_owed = true
        gs = GS.LOADING
    end
    post(E .. "GameStateChanged", { gameState = gs })
end

-- VarbitChanged: RuneLite posts one per changed varp (varbitId -1) and one
-- per varbit on it whose value moved. The client reports varps only, so the
-- varbits are the WATCHED ones (read by the plugin, or seeded) re-read on a
-- tick where any varp changed; a varbit's varp is learned when a tick moves
-- exactly one varp.
local tick_varps = {}
local varbit_varp = {}

local function post_var_changes()
    if #tick_varps == 0 then return end
    local varps = tick_varps
    tick_varps = {}
    for _, ev in ipairs(varps) do
        post(E .. "VarbitChanged", { varpId = ev.varp, varbitId = -1, value = ev.value })
    end
    local only = #varps == 1 and varps[1].varp or nil
    local w = J.rl_client.watched_varbits
    local moved = {}
    for id, last in pairs(w) do
        local v = API.cache.varbit(id)
        if v ~= last then moved[#moved + 1] = id; w[id] = v end
    end
    table.sort(moved)
    for _, id in ipairs(moved) do
        if only and varbit_varp[id] == nil then varbit_varp[id] = only end
        post(E .. "VarbitChanged", { varpId = varbit_varp[id] or -1, varbitId = id, value = w[id] })
    end
end

local CHAT_TYPES = nil
local function chat_type(t)
    if CHAT_TYPES == nil then
        CHAT_TYPES = {}
        local CMT = C["net.runelite.api.ChatMessageType"]
        local vs = CMT.values()
        for i = 1, vs.n do
            local id = vs[i]:getType()
            if CHAT_TYPES[id] == nil then CHAT_TYPES[id] = vs[i] end
        end
    end
    return CHAT_TYPES[t] or C["net.runelite.api.ChatMessageType"].UNKNOWN
end

-- Every loc of the new scene, as RuneLite raises *ObjectSpawned after a load.
local SPAWN_EVENT = {
    ["net.runelite.api.GameObject"] = { "GameObjectSpawned", "gameObject" },
    ["net.runelite.api.WallObject"] = { "WallObjectSpawned", "wallObject" },
    ["net.runelite.api.DecorativeObject"] = { "DecorativeObjectSpawned", "decorativeObject" },
    ["net.runelite.api.GroundObject"] = { "GroundObjectSpawned", "groundObject" },
}
local function post_scene_spawns()
    local bus = QH.core.eventBus
    local heard = {}
    local any = false
    for k, e in pairs(SPAWN_EVENT) do
        local n = bus.subscriber_count and bus:subscriber_count(E .. e[1]) or 1
        heard[k] = n > 0
        any = any or n > 0
    end
    if not any then return end
    local TO = C["net.runelite.api.TileObject"]
    local Tile = C["net.runelite.api.Tile"]
    local cur, s = API.world.scenery_next(nil)
    while cur do
        local o = TO.__fromSnap(s)
        if o ~= nil then
            for k, e in pairs(SPAWN_EVENT) do
                if heard[k] and o.__supers[k] then
                    post(E .. e[1], { tile = Tile.__at(s.tile_x, s.tile_z, s.level), [e[2]] = o })
                    break
                end
            end
        end
        cur, s = API.world.scenery_next(cur)
    end
end

-- VarClientIntChanged: RuneLite posts one the moment a varc int is written.
-- This client reports none, so the WATCHED varcs (read by the plugin, or
-- seeded) are compared once a frame; a write and its undo inside one frame
-- raise nothing.
local function post_varc_changes()
    local w = J.rl_client.watched_varcs
    local moved = nil
    for id, last in pairs(w) do
        local v = API.cache.varc_int(id)
        if v ~= last then
            w[id] = v
            moved = moved or {}
            moved[#moved + 1] = id
        end
    end
    if moved == nil then return end
    table.sort(moved)
    for _, id in ipairs(moved) do post(E .. "VarClientIntChanged", { index = id }) end
end


-- InteractingChanged: RuneLite posts one when an actor's interacting target
-- changes. Compared once a server tick, for the local player and every npc,
-- and only while a subscriber listens (Biohazard's ingredient step).
local interacting_seen = {}
local function post_interacting_changes()
    local key = E .. "InteractingChanged"
    if not heard(key) then
        interacting_seen = {}
        return
    end
    local now = {}
    local changed = {}
    local p = API.world.local_player()
    if p ~= nil then
        local k = "p"
        now[k] = p.interacting or -1
        if (interacting_seen[k] or -1) ~= now[k] then changed[#changed + 1] = C["net.runelite.api.Player"].__local() end
    end
    local NPC = C["net.runelite.api.NPC"]
    local cur, s = API.world.npc_next(nil)
    while cur do
        local k = s.server_slot
        now[k] = s.interacting or -1
        if (interacting_seen[k] or -1) ~= now[k] then changed[#changed + 1] = NPC.__fromSnap(s) end
        cur, s = API.world.npc_next(cur)
    end
    interacting_seen = now
    for _, src in ipairs(changed) do
        if src ~= nil then post(key, { source = src, target = src:getInteracting() }) end
    end
end

-- ServerNpcLoot: RuneLite's is the server's drop report. This client has
-- none, so a kill is read the way RuneLite's LootManager reads one: an npc
-- whose health bar had reached empty despawns, and the ground items that
-- appear on its footprint that tick are its drop; the event is posted on the
-- next server tick. (AchievementDiaryStepManager keys only on the npc id.)
local kills = {}
local function note_kill(npc, o)
    if npc.health_ratio ~= 0 or (npc.health_scale or -1) <= 0 then return end
    if not heard("net.runelite.client.events.ServerNpcLoot") then return end
    -- a composition still loading (J.PendingMT) costs this kill, not the despawn
    local ok, comp = API.sandbox.protect(o.getTransformedComposition, o)
    if not ok or comp == nil then return end
    kills[#kills + 1] = {
        comp = comp, x = npc.true_x, z = npc.true_z, level = npc.level,
        size = npc.size or 1, tick = API.game.tick(), items = C["java.util.ArrayList"].__new__(),
    }
end
local function note_drop(item)
    for _, k in ipairs(kills) do
        if item.level == k.level and item.tile_x >= k.x and item.tile_x < k.x + k.size
            and item.tile_z >= k.z and item.tile_z < k.z + k.size then
            k.items:add(C["net.runelite.api.Item"].__new(item.obj_id, item.count))
            return
        end
    end
end
local function post_kills()
    if #kills == 0 then return end
    local now = API.game.tick()
    local kept = {}
    for _, k in ipairs(kills) do
        if k.tick < now then
            post("net.runelite.client.events.ServerNpcLoot", { composition = k.comp, items = k.items })
        else
            kept[#kept + 1] = k
        end
    end
    kills = kept
end

-- A loc's RuneLite class by its placed shape (rl_world's loc_kind): wall
-- 0..3, wall decoration 4..8, game object 9..21, ground decoration 22.
local function loc_event(shape)
    if shape <= 3 then return "WallObject", "wallObject" end
    if shape <= 8 then return "DecorativeObject", "decorativeObject" end
    if shape <= 21 then return "GameObject", "gameObject" end
    return "GroundObject", "groundObject"
end

-- The loc of `shape`'s kind with id `loc_id` on a tile now, or nil.
local function loc_on_tile(tile, loc_id, shape)
    local kind = loc_event(shape)
    if kind == "WallObject" then
        local o = tile:getWallObject()
        return o ~= nil and o:getId() == loc_id and o or nil
    elseif kind == "DecorativeObject" then
        local o = tile:getDecorativeObject()
        return o ~= nil and o:getId() == loc_id and o or nil
    elseif kind == "GroundObject" then
        local o = tile:getGroundObject()
        return o ~= nil and o:getId() == loc_id and o or nil
    end
    local games = tile:getGameObjects()
    for i = 1, games.n do
        local o = games[i]
        if o ~= nil and o:getId() == loc_id then return o end
    end
    return nil
end

-- ------------------------------------------------------------------ callbacks

-- The plugin heap is capped at 16 MB of LIVE plus garbage (the allocator
-- refuses past it, and Lua then runs an emergency full collection inside
-- whatever allocated). Live is ~7 MB booted and ~9 with a quest open; Lua's
-- default pause (250%) lets the heap reach 2.5x live before a cycle starts,
-- i.e. always past the cap. 150% keeps steady collection ahead of it.
local GC_PAUSE = 150

function plugin.on_start(api)
    API = api
    api.sandbox.protect(collectgarbage, "param", "pause", GC_PAUSE)
    QH = QH or {}
    QH.api = api
    QH.load_quest = load_quest
    -- the panel's row handlers (called by Porcelain from C, outside every
    -- protected hook): retried when a record or module is pending
    QH.dispatch_action = function(what, fn, ...) dispatch(what, true, fn, ...) end
    J = J or {}
    J.protect = api.sandbox.protect
    if boot.phase == "idle" or boot.phase == "failed" then
        boot.phase = "loading"
        boot.queue = boot_queue()
        boot.at = 1
        for _, path in ipairs(boot.queue) do request(path) end
    elseif boot.phase == "stopped" then
        -- a re-enable: every module is still loaded; the plugin starts again
        boot.phase = "config"
    end
    if api.porcelain.open() then
        local d = { width = 320, faces = "page", icon_asset = "quest_icon.png" }
        api.porcelain.panel(d)
        api.porcelain.describe(function(desc)
            if QH.ui then QH.ui.describe(desc) end
        end)
    end
end

function plugin.on_stop(api)
    if running() then
        -- RuneLite's ClientShutdown (QuestHelperPlugin saves the quest bank on
        -- it): a disable or a client exit while logged in keeps the bank
        -- changes made since the last logout.
        dispatch("ClientShutdown", false, post, "net.runelite.client.events.ClientShutdown", nil)
        dispatch("shutDown", false, QH.core.stop_plugin)
    end
    if QH.render_reset then QH.render_reset() end
    if J.rlw and J.rlw.on_stop then dispatch("widgets stop", false, J.rlw.on_stop) end
    if boot.phase == "running" then
        local ok = API.sandbox.protect(function() C["net.runelite.api.RuneLiteObject"].__destroyAll() end)
        boot.phase = "stopped"
    elseif boot.phase ~= "idle" then
        boot.phase = "idle"
    end
    retries, quest_loads, tick_varps, replays = {}, {}, {}, {}
    list_jobs.list, list_jobs.items = nil, nil
    kills, interacting_seen = {}, {}
    ticked_since_start = false
end

local was_logged_in = false

function plugin.on_frame_start(api, ev)
    if boot.phase ~= "running" then
        local ok, err = API.sandbox.protect(boot_step)
        if not ok then
            if is_pending(err) then J.request_class(err.pending)
            else boot.phase = "failed"; log_once("boot", err) end
        end
        if boot.phase ~= "running" then
            if index then pump_modules() end
            return
        end
    end
    pump_modules()
    quest_load_poll()
    -- the residency pass, on idle frames once the login's first tick ran
    if was_logged_in and api.core.screen() == 30 and not list_jobs_active() and residency_idle() then
        dispatch("residency", false, residency_step)
    end
    dispatch("frame", false, QH.core.frame)
    -- the query scripts an overlay was answered from last frame, run again
    -- here, where a script may run (rl_client DRAW_QUERY_SCRIPTS)
    if running() then dispatch("draw scripts", false, J.rl_client.refresh_draw_scripts) end
    -- RuneLite runs invokeAtTickEnd at the end of the CLIENT tick (a frame
    -- here): the bank tab lays its slots out there, the frame after
    -- bankmain_finishbuilding ran. Only once a game tick has run since the
    -- start, so the start's own tasks (QuestHelperPlugin.startUp's login
    -- setup) still meet a logged-in client with its scene, as they did.
    if running() and ticked_since_start then dispatch("frame tick end", false, QH.core.tick_end) end
    -- the list's jobs after the client-thread tasks that start them
    if list_jobs_active() then list_jobs_step() end
    if QH.ui then dispatch("panel frame", false, QH.ui.frame, api) end
    if api.core.screen() == 30 then
        dispatch("VarClientIntChanged", false, post_varc_changes)
        dispatch("ClientTick", false, post, E .. "ClientTick", nil)
    end
end

function plugin.on_server_tick(api, ev)
    if not running() then return end
    -- the RS profile follows the logged-in name (set once the first ticks run)
    if not was_logged_in or login_owed then
        was_logged_in = true
        -- The login's LOGGED_IN, owed until the profile is known
        -- (post_game_state); or a profile that CHANGED here (a stale name at
        -- the screen change) gets the LOGGED_IN RuneLite posts after its
        -- RuneScapeProfileChanged. Without it the change waited for the next
        -- scene load, whose LOGGED_IN ran the login code -- the open helper
        -- shut down and the "updated" line printed on the first walk across
        -- a region edge (live in the browser, 2026-11-20).
        local before = QH.core.configManager and QH.core.configManager.rsProfileKey
        dispatch("rs profile", false, QH.core.update_rs_profile)
        local after = QH.core.configManager and QH.core.configManager.rsProfileKey
        if after ~= nil and (login_owed or after ~= before) and api.core.screen() == 30 then
            login_owed = false
            dispatch("GameStateChanged", false, function()
                post(E .. "GameStateChanged", { gameState = game_state() })
            end)
        end
    end
    dispatch("VarbitChanged", false, post_var_changes)
    dispatch("ServerNpcLoot", false, post_kills)
    dispatch("InteractingChanged", false, post_interacting_changes)
    dispatch("GameTick", false, post, E .. "GameTick", nil)
    dispatch("tick end", false, QH.core.tick_end)
    ticked_since_start = true
end

function plugin.on_var_changed(api, ev)
    if not running() then return end
    tick_varps[#tick_varps + 1] = { varp = ev.varp, value = ev.value }
end

function plugin.on_interface(api, ev)
    if not running() then return end
    if J.rlw then dispatch("widgets interface", false, J.rlw.on_interface, ev) end
    if ev.open then
        dispatch("WidgetLoaded", false, post, E .. "WidgetLoaded", { groupId = ev.group })
    else
        dispatch("WidgetClosed", false, post, E .. "WidgetClosed", { groupId = ev.group, unload = true })
    end
end

function plugin.on_inventory_changed(api, ev)
    if not running() then return end
    J.rl_client.inventory_changed(ev.inv)
    dispatch("ItemContainerChanged", false, function()
        -- never a nil container: the client wrote this one (a shop, trade,
        -- group storage, ... are read by their id; no slots = empty)
        post(E .. "ItemContainerChanged", { containerId = ev.inv, itemContainer = J.rl_client.changed_container(ev.inv) })
    end)
end

function plugin.on_chat_message(api, ev)
    if not running() then return end
    dispatch("ChatMessage", true, function()
        post(E .. "ChatMessage", {
            type = chat_type(ev.type), name = ev.sender or "", message = ev.text or "",
            sender = nil, timestamp = 0,
        })
    end)
end

function plugin.on_npc_spawn(api, npc)
    if not running() then return end
    dispatch("NpcSpawned", false, function()
        local o = C["net.runelite.api.NPC"].__fromSnap(npc)
        post(E .. "NpcSpawned", { npc = o })
    end)
end

function plugin.on_npc_retype(api, npc)
    if not running() then return end
    dispatch("NpcChanged", false, function()
        local NPC = C["net.runelite.api.NPC"]
        local prev = NPC.__bySlot(npc.server_slot)
        local old = prev and prev:getTransformedComposition() or nil
        local o = NPC.__fromSnap(npc)
        post(E .. "NpcChanged", { npc = o, old = old })
    end)
end

function plugin.on_npc_despawn(api, npc)
    if not running() then return end
    dispatch("NpcDespawned", false, function()
        local NPC = C["net.runelite.api.NPC"]
        local o = NPC.__bySlot(npc.server_slot) or NPC.__fromSnap(npc)
        note_kill(npc, o)
        post(E .. "NpcDespawned", { npc = o })
        NPC.__forget(npc.server_slot)
    end)
end

local function item_event(key, item)
    local ti = C["net.runelite.api.TileItem"].__fromSnap(item)
    local tile = C["net.runelite.api.Tile"].__at(item.tile_x, item.tile_z, item.level)
    post_if_heard(E .. key, { tile = tile, item = ti })
end

function plugin.on_item_spawn(api, item)
    if not running() then return end
    if #kills > 0 then note_drop(item) end
    dispatch("ItemSpawned", false, item_event, "ItemSpawned", item)
end

function plugin.on_item_despawn(api, item)
    if not running() then return end
    dispatch("ItemDespawned", false, item_event, "ItemDespawned", item)
end

-- A scene load: RuneLite's LOADING -> LOGGED_IN, with every loc spawned.
function plugin.on_world_loaded(api, ev)
    if not running() then return end
    dispatch("scene", false, function()
        C["net.runelite.api.TileObject"].__invalidate()
        local GS = C["net.runelite.api.GameState"]
        post(E .. "GameStateChanged", { gameState = GS.LOADING })
        post_scene_spawns()
        post_game_state()
    end)
end

-- A loc changed mid-scene (a door opened, a rock depleted, a zone reset):
-- RuneLite's *ObjectDespawned for the loc that stood there, then
-- *ObjectSpawned for the one that stands there now -- each only when heard.
function plugin.on_loc_changed(api, ev)
    if not running() then return end
    dispatch("loc changed", false, function()
        local TO = C["net.runelite.api.TileObject"]
        TO.__locs_changed()
        local tile = C["net.runelite.api.Tile"].__at(ev.tile_x, ev.tile_z, ev.level)
        if ev.old_loc_id >= 0 then
            local kind, field = loc_event(ev.old_shape)
            local key = E .. kind .. "Despawned"
            if heard(key) then
                local old = TO.__fromSnap({
                    loc_id = ev.old_loc_id, name = "", tile_x = ev.tile_x, tile_z = ev.tile_z, level = ev.level,
                    size_x = 1, size_z = 1, shape = ev.old_shape, angle = ev.old_angle, element_id = -1,
                    interactive = 1, visible_ops = 0,
                })
                post(key, { tile = tile, [field] = old })
            end
        end
        if ev.loc_id >= 0 and tile ~= nil then
            local kind, field = loc_event(ev.shape)
            local key = E .. kind .. "Spawned"
            if heard(key) then
                local o = loc_on_tile(tile, ev.loc_id, ev.shape)
                if o ~= nil then post(key, { tile = tile, [field] = o }) end
            end
        end
    end)
end

-- A MAP_ANIM graphic: GraphicsObjectCreated (Song of the Elves' Baxtorian
-- puzzle reads its smoke puffs).
function plugin.on_map_anim(api, ev)
    if not running() then return end
    dispatch("GraphicsObjectCreated", false, function()
        local key = E .. "GraphicsObjectCreated"
        if not heard(key) then return end
        local bx, bz = API.world.scene_origin()
        if bx == nil then return end
        local go = C["net.runelite.api.GraphicsObject"].__of({
            id = ev.spotanim, fine_x = (ev.tile_x - bx) * 128 + 64, fine_z = (ev.tile_z - bz) * 128 + 64,
            level = ev.level, start_cycle = ev.cycle + ev.delay,
        })
        post(key, { graphicsObject = go })
    end)
end

-- The GE item search (OC_FIND): RuneLite's GrandExchangeSearched. Quest
-- Helper consumes it when the search box holds "quest-helper" and sets the
-- results (Client.setGeSearchResult*, kept by rl_widgets); those ids are the
-- answer, nil lets the client search.
function plugin.on_item_search(api, ev)
    if not running() or not J.rlw then return nil end
    local ok, consumed, ge = dispatch("GrandExchangeSearched", false, J.rlw.ge_searched, QH.core.eventBus)
    if not ok or not consumed then return nil end
    local src = ge.ids or {}
    local first = (ge.index or 0) + 1
    local last = math.min(#src, first + (ge.count or #src) - 1)
    local ids = {}
    for i = first, last do ids[#ids + 1] = src[i] end
    return ids
end

function plugin.on_screen_changed(api, ev)
    if J.rlw and J.rlw.on_screen_changed then dispatch("widgets screen", false, J.rlw.on_screen_changed, ev.screen) end
    if not running() then return end
    if ev.screen ~= "game" then was_logged_in = false; login_owed = false end
    dispatch("rs profile", false, QH.core.update_rs_profile)
    dispatch("GameStateChanged", false, post_game_state)
end

function plugin.on_config_changed(api, key)
    if not running() then return end
    dispatch("ConfigChanged", true, QH.core.config_changed, key)
end

function plugin.on_key(api, ev)
    if not running() or not J.rlw then return end
    local ok, consumed = dispatch("key", false, J.rlw.on_key, ev)
    if ok and consumed then return "consume" end
end

function plugin.on_menu_build(api, ev)
    if not running() then return end
    dispatch("MenuEntryAdded", false, function()
        local es = J.rl_client.menu_begin(ev)
        local ok, err = API.sandbox.protect(function()
            for i = 1, es.n do post(E .. "MenuEntryAdded", { menuEntry = es[i] }) end
            -- a world map point under the mouse that jumps: "Focus on"
            if QH.worldmap_menu then QH.worldmap_menu() end
        end)
        J.rl_client.menu_end()
        if not ok then error(err, 0) end
    end)
end

-- A "::" command, before it goes to the server: CommandExecuted (RuneLite's
-- CommandManager posts it for every "::" line). QuestHelperPlugin answers
-- questhelperdebug / qh-inv / qh only in developer mode; a command it
-- answered is consumed so the server never sees it.
local OWN_COMMANDS = { questhelperdebug = true, ["qh-inv"] = true, qh = true }
function plugin.on_command(api, ev)
    if not running() then return end
    dispatch("CommandExecuted", true, function()
        post(E .. "CommandExecuted", { command = ev.name, arguments = J.arr(ev.args, #ev.args) })
    end)
    if OWN_COMMANDS[ev.name] and QH.developer_mode then return "consume" end
end

function plugin.on_menu_select(api, ev)
    if not running() then return end
    local ok, consumed = dispatch("MenuOptionClicked", true, function()
        local e = J.rl_client.menu_select(ev)
        -- a row on an owned control names it, as RuneLite's CC_OP target does
        if J.rlw and (e.target == nil or e.target == "") then
            local name = J.rlw.owned_menu_target(e.option)
            if name ~= nil then e.target = name end
        end
        local event = QH.core.event(E .. "MenuOptionClicked", { menuEntry = e })
        QH.core.eventBus:post(event)
        J.rl_client.menu_click(e)
        -- a subscriber moved a native row onto another widget (setParam0/1)
        local moved = not ev.owned and J.rlw ~= nil and J.rlw.menu_retarget(e)
        if moved then return true end
        local row = ev.row
        local widget = false
        if J.rlw and row and row.component_id and row.component_id > 0 then
            widget = J.rlw.on_menu_select(row.component_id, row.slot, row.identifier or row.action)
        end
        return ev.owned or event:isConsumed() or widget
    end)
    if ok and consumed then return "consume" end
end

-- ScriptPreFired / ScriptPostFired, from inside the VM for a watched id
-- (SCRIPT_HOOKS). getIntStack()/getStringStack() are the event's values
-- through ev.ref while the subscribers run: an invoked script's arguments at
-- pre, what it returned at post. A top-level script's pre carries a
-- ScriptEvent whose source is the listener's widget (WorldMapAreaManager
-- reads the clicked map row's index from it).
local function script_event_of(ev)
    if ev.invoked or ev.widget == nil or J.rlw == nil then return nil end
    return J.rlw.script_event({ source = J.rlw.wrap(ev.widget) })
end

function plugin.on_script_pre(api, ev)
    if not running() then return end
    if J.rlw then dispatch("script rebuild", false, J.rlw.on_script_pre, ev.script_id) end
    J.rl_client.script_enter(ev.ref)
    dispatch("ScriptPreFired", false, function()
        post(E .. "ScriptPreFired", { scriptId = ev.script_id, scriptEvent = script_event_of(ev) })
    end)
    J.rl_client.script_leave()
    if J.rlw and J.rlw.invalidate then J.rlw.invalidate() end
end

function plugin.on_script_post(api, ev)
    if not running() then return end
    J.rl_client.script_enter(ev.ref)
    dispatch("ScriptPostFired", false, post, E .. "ScriptPostFired", { scriptId = ev.script_id })
    J.rl_client.script_leave()
    if J.rlw and J.rlw.invalidate then J.rlw.invalidate() end
end

function plugin.on_script_callback(api, ev)
    if not running() then return end
    J.rl_client.script_enter(ev.ref)
    dispatch("ScriptCallbackEvent", false, post, E .. "ScriptCallbackEvent",
        { eventName = ev.name, script = nil })
    J.rl_client.script_leave()
    if J.rlw and J.rlw.invalidate then J.rlw.invalidate() end
end

function plugin.on_draw_world(api, draw)
    if not running() or not QH.render then return end
    dispatch("world overlays", false, QH.render, "world", draw)
end

function plugin.on_draw_canvas(api, draw)
    if not running() or not QH.render then return end
    dispatch("canvas overlays", false, QH.render, "canvas", draw)
end

function plugin.on_ui_build(api, panel, view)
    if not QH.ui then
        api.porcelain.panel_build(view)
        return
    end
    dispatch("panel build", false, QH.ui.build, api, panel, view)
end

function plugin.on_ui_action(api, ev)
    if not QH.ui then
        api.porcelain.panel_action(ev)
        return
    end
    dispatch("panel action", true, QH.ui.action, api, ev)
end

function plugin.on_ui_draw(api, node, draw)
    if not QH.ui then
        api.porcelain.panel_draw(node)
        return
    end
    dispatch("panel draw", false, QH.ui.draw, api, node, draw)
end

-- Test seam: the boot state and the module table.
plugin._boot = boot
plugin._modules = module_state
plugin._residency = residency
plugin._list_jobs = list_jobs

return plugin
