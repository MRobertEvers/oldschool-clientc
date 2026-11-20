-- rt/rl_core.lua under plain Lua: the inventory rows it owns, the events,
-- EventBus, ClientThread, the injector over real generated framework
-- classes, ConfigManager (declared items, the config file, RS profiles,
-- the QuestHelperConfig proxy), the plugin glue, the clock.
--
--   /opt/homebrew/bin/lua tools/questhelper_port/harness/test_rl_core.lua
local HERE = debug.getinfo(1, "S").source:sub(2):match("^(.*)/") or "."
local ROOT = HERE .. "/../../../script/plugins/quest_helper"

J = { protect = pcall }
for _, f in ipairs({ "j_core", "j_lang", "j_util", "j_stream", "j_regex", "j_misc" }) do dofile(ROOT .. "/rt/" .. f .. ".lua") end
dofile(HERE .. "/mock_api.lua")
QH = { api = MOCK_API }

local logs = {}
MOCK_API.core.log = function(s) logs[#logs + 1] = s end
MOCK_API.client = MOCK_API.client or {}
MOCK_API.client.datestamp = function() return "2026-11-19_15-00-00" end
MOCK.now_ms = 5000

local awt = io.open(ROOT .. "/rt/awt.lua")
if awt then awt:close(); dofile(ROOT .. "/rt/awt.lua") else dofile(HERE .. "/awt_standin.lua") end

-- ------------------------------------------------------------------ loader + stubs (as run_all)
local index = dofile(ROOT .. "/gen/index.lua")
local loaded = {}
local function load_module(m)
  if loaded[m] then return end
  loaded[m] = true
  for _, d in ipairs(index.modules[m] or {}) do load_module(d) end
  dofile(ROOT .. "/gen/" .. m .. ".lua")
end
local STUB
local stub_mt = {}
stub_mt.__index = function(t, k)
  if k == "__key" then return "stub" end
  if k == "__supers" then return {} end
  return function() return STUB end
end
stub_mt.__call = function() return STUB end
STUB = setmetatable({}, stub_mt)
local stubbed = {}
J.on_missing(function(key)
  local m = index.classes[key]
  if m then load_module(m); return end
  stubbed[key] = true
  J.def(key, { kind = "class", super = "java.lang.Object", ifaces = {}, body = function(cls)
    setmetatable(cls, { __index = function(t, k)
      if type(k) == "string" and k:sub(1, 2) == "__" and k:sub(1, 5) ~= "__new" and k:sub(1, 6) ~= "__init" then
        return C["java.lang.Object"][k]
      end
      return function() return setmetatable({}, stub_mt) end
    end })
  end })
end)
function J.try_class(key)
  local m = index.classes[key]
  if m then load_module(m) end
  if J.defined(key) then return C[key] end
  return nil
end

dofile(ROOT .. "/gen/enums.lua")
for _, m in ipairs(index.framework) do load_module(m) end
dofile(ROOT .. "/rt/rl_core.lua")
local core = QH.core

-- ------------------------------------------------------------------ checks
local pass, fails = 0, {}
local function check(name, got, want)
  if got == want then pass = pass + 1
  else fails[#fails + 1] = string.format("%s: got %s, want %s", name, tostring(got), tostring(want)) end
end
local function ok(name, cond) check(name, cond and true or false, true) end
local function raises(name, fn, pattern)
  local good, err = pcall(fn)
  local msg = type(err) == "table" and err.__supers and err:toString() or tostring(err)
  if good then fails[#fails + 1] = name .. ": did not raise"
  elseif pattern and not msg:find(pattern) then fails[#fails + 1] = name .. ": raised '" .. msg .. "', want /" .. pattern .. "/"
  else pass = pass + 1 end
end

-- ------------------------------------------------------------------ 1. inventory rows

local MINE = {
  "^net%.runelite%.api%.events%.", "^net%.runelite%.client%.events%.", "^com%.google%.inject",
  "^net%.runelite%.client%.callback%.", "^net%.runelite%.client%.eventbus%.", "^net%.runelite%.client%.config%.Config$",
  "^net%.runelite%.client%.config%.ConfigManager$", "^net%.runelite%.client%.plugins%.Plugin", "^net%.runelite%.client%.ui%.ClientToolbar$",
  "^net%.runelite%.client%.ui%.NavigationButton", "^net%.runelite%.client%.RuneLite$", "^net%.runelite%.client%.input%.KeyListener$",
  "^javax%.swing%.SwingUtilities$",
}
local function mine(owner)
  for _, p in ipairs(MINE) do if owner:find(p) then return true end end
  return false
end
local rows = 0
for line in io.lines(ROOT .. "/gen/inventory.tsv") do
  local kind, owner, lua = line:match("^([^\t]*)\t([^\t]*)\t([^\t]*)")
  if kind and kind:sub(1, 1) ~= "#" and owner and mine(owner) then
    rows = rows + 1
    ok("inventory class " .. owner, J.defined(owner) and not stubbed[owner])
    if J.defined(owner) and not stubbed[owner] then
      local cls = C[owner]
      -- an interface's abstract method is the implementer's (a lambda, a class)
      if (kind == "method" and cls.__kind ~= "interface") or kind == "static" or kind == "ctor" then
        ok("inventory " .. kind .. " " .. owner .. "." .. lua, type(cls[lua]) == "function")
      end
    end
  end
end
-- RuneScapeProfileType.getCurrent lives on a generated enum
ok("inventory static RuneScapeProfileType.getCurrent", type(C["net.runelite.client.config.RuneScapeProfileType"].getCurrent) == "function")
ok("inventory rows seen", rows > 120)

-- ------------------------------------------------------------------ 2. clock

local t0 = J.now_ms()
check("clock epoch anchor", t0, 1795100400000)
MOCK.now_ms = 5250
check("clock advances with frame_ms", J.now_ms(), 1795100400250)
check("System.currentTimeMillis", C["java.lang.System"].currentTimeMillis(), 1795100400250)

-- ------------------------------------------------------------------ 3. events

local E = "net.runelite.api.events."
local CE = "net.runelite.client.events."
local vc = core.event(E .. "VarbitChanged", { varbitId = 1777, value = 2 })
check("VarbitChanged varp default", vc:getVarpId(), -1)
check("VarbitChanged varbit", vc:getVarbitId(), 1777)
check("VarbitChanged value", vc:getValue(), 2)
local cm_ev = core.event(E .. "ChatMessage", { type = C["net.runelite.api.ChatMessageType"].GAMEMESSAGE, message = "Hi", timestamp = 9 })
check("ChatMessage type", cm_ev:getType(), C["net.runelite.api.ChatMessageType"].GAMEMESSAGE)
check("ChatMessage message", cm_ev:getMessage(), "Hi")
check("ChatMessage timestamp", cm_ev:getTimestamp(), 9)
local fake_entry = { getOption = function() return "Talk-to" end, getParam0 = function() return 3 end }
local mea = core.event(E .. "MenuEntryAdded", { menuEntry = fake_entry, target = "<col=ffff00>Bob", param1 = 7 })
check("MenuEntryAdded via entry", mea:getOption(), "Talk-to")
check("MenuEntryAdded param0 via entry", mea:getActionParam0(), 3)
check("MenuEntryAdded target fallback", mea:getTarget(), "<col=ffff00>Bob")
check("MenuEntryAdded param1 fallback", mea:getActionParam1(), 7)
check("MenuEntryAdded entry", mea:getMenuEntry(), fake_entry)
local moc = C[E .. "MenuOptionClicked"].__new__MenuEntry(fake_entry)
check("MenuOptionClicked option", moc:getMenuOption(), "Talk-to")
check("MenuOptionClicked not consumed", moc:isConsumed(), false)
moc:consume()
check("MenuOptionClicked consumed", moc:isConsumed(), true)
local pm = C[CE .. "PluginMessage"].__new__String_String("shortestpath", "clear")
check("PluginMessage namespace", pm:getNamespace(), "shortestpath")
check("PluginMessage data empty", pm:getData():isEmpty(), true)
raises("PluginMessage null name", function() C[CE .. "PluginMessage"].__new__String_String("x", nil) end, "name")
local ce = core.command_event("::questhelperdebug enable now")
check("CommandExecuted command", ce:getCommand(), "questhelperdebug")
check("CommandExecuted args n", ce:getArguments().n, 2)
check("CommandExecuted arg 1", ce:getArguments()[1], "enable")
check("WidgetClosed unload default", core.event(E .. "WidgetClosed"):isUnload(), false)
local ges = core.event(E .. "GrandExchangeSearched")
ges:consume()
check("GrandExchangeSearched consume", ges:isConsumed(), true)

-- ------------------------------------------------------------------ 4. boot, EventBus

local root = core.boot()
check("boot idempotent", core.boot(), root)
check("RuneLite.getInjector", C["net.runelite.client.RuneLite"].getInjector(), root)
local bus = core.event_bus()

local order = {}
J.def("test.SubA", { kind = "class", super = "java.lang.Object", ifaces = {},
  subscribe = { { "onGameTick", E .. "GameTick", 0 }, { "onChatMessage", E .. "ChatMessage", 5 } },
  body = function(cls)
    function cls.__init(self, tag) self.tag = tag end
    function cls.onGameTick(self, ev) order[#order + 1] = self.tag .. ":tick" end
    function cls.onChatMessage(self, ev) order[#order + 1] = self.tag .. ":chat:" .. ev:getMessage() end
  end })
J.def("test.SubB", { kind = "class", super = "test.SubA", ifaces = {},
  subscribe = { { "onVarbitChanged", E .. "VarbitChanged", -1.0 } },
  body = function(cls)
    function cls.onVarbitChanged(self, ev) order[#order + 1] = self.tag .. ":var" end
    function cls.onGameTick(self, ev) order[#order + 1] = self.tag .. ":tickB" end
  end })
J.def("test.Hi", { kind = "class", super = "java.lang.Object", ifaces = {},
  subscribe = { { "onGameTick", E .. "GameTick", 10 } },
  body = function(cls)
    function cls.__init(self) end
    function cls.onGameTick(self, ev) order[#order + 1] = "hi:tick" end
  end })
local b1 = C["test.SubB"].__new("b1")
local a1 = C["test.SubA"].__new("a1")
local hi = C["test.Hi"].__new()
bus:register__Object(b1)
bus:register__Object(a1)
bus:register__Object(hi)
bus:post(core.event(E .. "GameTick"))
-- priority 10 first; then equal priority by class name (test.SubA < test.SubB),
-- and a superclass @Subscribe dispatches virtually
check("bus order", table.concat(order, ","), "hi:tick,a1:tick,b1:tickB")
order = {}
bus:post(core.event(E .. "VarbitChanged"))
check("bus inherited subscribe list", table.concat(order, ","), "b1:var")
check("bus exact class only", bus:subscriber_count(E .. "NpcSpawned"), 0)
bus:unregister__Object(b1)
order = {}
bus:post(core.event(E .. "GameTick"))
check("bus unregister", table.concat(order, ","), "hi:tick,a1:tick")
bus:unregister__Object(hi)
bus:unregister__Object(a1)

-- a throwing subscriber is logged; the next one still runs
J.def("test.Boom", { kind = "class", super = "java.lang.Object", ifaces = {},
  subscribe = { { "onGameTick", E .. "GameTick", 1 } },
  body = function(cls)
    function cls.__init(self) end
    function cls.onGameTick(self, ev) J.throw(C["java.lang.IllegalStateException"].__new__String("kaboom")) end
  end })
local boom = C["test.Boom"].__new()
local a2 = C["test.SubA"].__new("a2")
bus:register__Object(boom)
bus:register__Object(a2)
order = {}
local nlogs = #logs
bus:post(core.event(E .. "GameTick"))
check("bus continues past exception", table.concat(order, ","), "a2:tick")
ok("bus logs exception", logs[nlogs + 1] and logs[nlogs + 1]:find("kaboom", 1, true))
bus:unregister__Object(boom)
-- a pending module is raised again after every subscriber has run
J.def("test.Pend", { kind = "class", super = "java.lang.Object", ifaces = {},
  subscribe = { { "onGameTick", E .. "GameTick", 1 } },
  body = function(cls)
    function cls.__init(self) end
    function cls.onGameTick(self, ev) error(setmetatable({ pending = "x.Y" }, J.PendingMT), 0) end
  end })
local pend = C["test.Pend"].__new()
bus:register__Object(pend)
order = {}
local good, err = pcall(bus.post, bus, core.event(E .. "GameTick"))
check("bus pending raised", good, false)
check("bus pending key", type(err) == "table" and err.pending, "x.Y")
check("bus pending others ran", table.concat(order, ","), "a2:tick")
bus:unregister__Object(pend)
bus:unregister__Object(a2)
raises("bus post nil", function() bus:post(nil) end, "event")

-- ------------------------------------------------------------------ 5. ClientThread, Swing

local ct = core.client_thread()
local ran = {}
local function R(tag) return J.fn("java.lang.Runnable", "run", function() ran[#ran + 1] = tag end) end
ct:invoke__Runnable(R("now"))
check("invoke runs now", table.concat(ran, ","), "now")
ran = {}
local tries = 0
ct:invokeLater__Runnable(J.fn("java.lang.Runnable", "run", function()
  ran[#ran + 1] = "later"
  ct:invokeLater__Runnable(R("nested"))
end))
ct:invokeLater__BooleanSupplier(J.fn("java.util.function.BooleanSupplier", "getAsBoolean", function()
  tries = tries + 1
  ran[#ran + 1] = "try" .. tries
  return tries >= 2
end))
ct:invokeAtTickEnd(R("tickend"))
C["javax.swing.SwingUtilities"].invokeLater(R("swing"))
check("invokeLater waits", #ran, 0)
core.frame()
check("frame drains (nested in same drain, swing after)", table.concat(ran, ","), "later,try1,nested,swing")
ran = {}
core.frame()
check("BooleanSupplier false requeued", table.concat(ran, ","), "try2")
ran = {}
core.frame()
check("BooleanSupplier true removed", #ran, 0)
core.tick_end()
check("invokeAtTickEnd", table.concat(ran, ","), "tickend")
ran = {}
ct:invokeLater__Runnable(J.fn("java.lang.Runnable", "run", function() J.throw(C["java.lang.RuntimeException"].__new__String("bad task")) end))
ct:invokeLater__Runnable(R("after-bad"))
nlogs = #logs
core.frame()
check("invoke exception isolated", table.concat(ran, ","), "after-bad")
ok("invoke exception logged", logs[nlogs + 1] and logs[nlogs + 1]:find("bad task", 1, true))
local pend_once = true
ct:invokeLater__Runnable(J.fn("java.lang.Runnable", "run", function()
  if pend_once then pend_once = false; error(setmetatable({ pending = "q.Z" }, J.PendingMT), 0) end
  ran[#ran + 1] = "pend-done"
end))
ran = {}
core.frame()
check("pending task kept", select(1, ct:queued()), 1)
core.frame()
check("pending task retried", table.concat(ran, ","), "pend-done")
raises("invokeLater nil", function() ct:invokeLater__Runnable(nil) end, "Runnable")

-- ------------------------------------------------------------------ 6. injector over generated classes

local Client = {}
Client.__index = Client
function Client.getWorldType(self) return self.wt end
function Client.getLocalPlayer(self) return nil end
function Client.getGameState(self) return C["net.runelite.api.GameState"].LOGGED_IN end
local client = setmetatable({ wt = C["java.util.HashSet"].__new__() }, Client)
root:bind("net.runelite.api.Client", client)

local WMAM = "com.questhelper.util.worldmap.WorldMapAreaManager"
local wm = root:getInstance__Class(J.classof(WMAM))
check("injector singleton", root:getInstance__Class(J.classof(WMAM)), wm)
check("injector field client", wm.client, client)
check("injector field eventBus", wm.eventBus, bus)

-- the plugin child injector, as start_plugin builds it
local QHP = "com.questhelper.QuestHelperPlugin"
local plugin_obj = rawget(C[QHP], "__new")()
local module = J.fn("com.google.inject.Module", "configure", function(_, binder)
  binder:bind__Class(J.classof(QHP)):toInstance(plugin_obj)
  binder:install(plugin_obj)
end)
local child = root:createChildInjector__ModuleArr(J.arr({ module }, 1))
check("child sees parent binding", child:resolve("net.runelite.api.Client"), client)
check("child binding", child:resolve(QHP), plugin_obj)
check("parent lacks child binding", root:binding(QHP), nil)
local OV = "com.questhelper.overlays.QuestHelperWorldOverlay"
local ov1 = child:getInstance__Class(J.classof(OV))
local ov2 = child:getInstance__Class(J.classof(OV))
check("inject_ctor gets plugin", ov1.plugin_QuestHelperWorldOverlay, plugin_obj)
ok("unscoped class built per request", not rawequal(ov1, ov2))
check("Injector injects itself", child:resolve("com.google.inject.Injector"), child)
child:bind_named("developerMode", true)
local QM = "com.questhelper.managers.QuestManager"
local qm = child:getInstance__Class(J.classof(QM))
check("@Named primitive", qm.developerMode, true)
check("singleton shared through the tree", qm, root:getInstance__Class(J.classof(QM)))
-- A repeat injectMembers by the same injector under the same bindings is a
-- no-op (QuestHelper.instantiateSteps re-injects an OwnerStep's children);
-- a binding made since fills the object again.
do
  local STEP = "com.questhelper.steps.DetailedQuestStep"
  local d = rawget(C[STEP], "__def")
  ok("DetailedQuestStep has @Inject fields", d ~= nil and d.inject ~= nil and #d.inject > 0)
  local obj = setmetatable({}, { __index = C[STEP] })
  obj.__cls = C[STEP]
  child:injectMembers(obj)
  check("step gets the client", obj.client, client)
  local vh = obj.visibilityHelper
  child:injectMembers(obj)
  ok("a repeat injection is skipped", vh ~= nil and rawequal(obj.visibilityHelper, vh))
  local client2 = setmetatable({ wt = C["java.util.HashSet"].__new__() }, Client)
  child:bind("net.runelite.api.Client", client2)
  check("a new binding is resolved, not a memo", child:resolve("net.runelite.api.Client"), client2)
  child:injectMembers(obj)
  check("a new binding fills the object again", obj.client, client2)
  child.bindings["net.runelite.api.Client"] = nil
end
local cfg = child:resolve("com.questhelper.QuestHelperConfig")
check("Config interface resolves to the proxy", cfg, core.config_manager():getConfig(J.classof("com.questhelper.QuestHelperConfig")))
check("plugin getConfig__ConfigManager", plugin_obj:getConfig__ConfigManager(core.config_manager()), cfg)
raises("no binding for an interface", function() root:resolve("java.util.List") end, "no implementation")
J.def("test.Cyc", { kind = "class", super = "java.lang.Object", ifaces = {}, inject_ctor = { "__init", { "test.Cyc" } },
  body = function(cls) function cls.__init(self, other) self.other = other end end })
raises("constructor cycle", function() root:resolve("test.Cyc") end, "circular")
local j_obj = C["test.SubA"].__new("x")
J.inject(j_obj)
check("J.inject", j_obj.tag, "x")

-- ------------------------------------------------------------------ 7. generated subscribers end to end

bus:register__Object(wm)
local areas = {}
J.def("test.AreaListener", { kind = "class", super = "java.lang.Object", ifaces = {},
  subscribe = { { "onWorldMapAreaChanged", "com.questhelper.util.worldmap.WorldMapAreaChanged", 0 } },
  body = function(cls)
    function cls.__init(self) end
    function cls.onWorldMapAreaChanged(self, ev) areas[#areas + 1] = ev:getWorldMapArea() end
  end })
local listener = C["test.AreaListener"].__new()
bus:register__Object(listener)
local source = { getIndex = function() return 1 end }
local script_event = { getSource = function() return source end }
bus:post(core.event(E .. "ScriptPreFired", { scriptId = 1711, scriptEvent = script_event }))
local WMA = C["com.questhelper.util.worldmap.WorldMapArea"]
check("WorldMapAreaManager handled ScriptPreFired", wm:getWorldMapArea(), WMA.AREAS[2])
check("WorldMapAreaChanged re-posted", areas[1], WMA.AREAS[2])
bus:post(core.event(E .. "ScriptPreFired", { scriptId = 5, scriptEvent = script_event }))
check("other script ignored", #areas, 1)
bus:unregister__Object(wm)
bus:unregister__Object(listener)

local PSM = "com.questhelper.statemanagement.PlayerStateManager"
local psm = child:getInstance__Class(J.classof(PSM))
bus:register__Object(psm)
bus:post(core.event(E .. "VarbitChanged", { varbitId = 1777, value = 1 }))
check("PlayerStateManager account type", psm.accountType, C["com.questhelper.domain.AccountType"].IRONMAN)
bus:unregister__Object(psm)

-- ------------------------------------------------------------------ 8. config declaration

local QHC = "com.questhelper.QuestHelperConfig"
local decl = core.config_decl(QHC)
ok("decl within 64 items", #decl.items <= 64 and #decl.items > 30)
local keys_ok, strs_ok, types_ok = true, true, true
local TYPES = { bool = true, int = true, color = true, string = true, enum = true }
for _, it in ipairs(decl.items) do
  if not it.key:match("^[a-z0-9_]+$") then keys_ok = false end
  for _, f in ipairs({ "key", "label", "default", "choices" }) do
    if type(it[f]) == "string" and #it[f] > 95 then strs_ok = false end
  end
  if not TYPES[it.type] then types_ok = false end
end
ok("decl keys [a-z0-9_]", keys_ok)
ok("decl strings <= 95 bytes", strs_ok)
ok("decl types", types_ok)
check("decl showOverlay key", decl.by_name.showOverlayPanel.key, "show_overlay_panel")
check("decl hidden item has no label", (function() for _, it in ipairs(decl.items) do if it.key == "debug_color" then return it.label end end return "absent" end)(), nil)
check("decl filterListBy stays in the file", decl.by_name.filterListBy, nil)
-- settings with nothing behind them in this client are stored, not offered
local function label_of(key) for _, it in ipairs(decl.items) do if it.key == key then return it.label end end return "absent" end
check("decl use_shortest_path not offered (no Shortest Path plugin)", label_of("use_shortest_path"), nil)
check("decl dev_show_overlay_on_launch not offered (developer mode off)", label_of("dev_show_overlay_on_launch"), nil)
check("decl a not-offered item is still stored", decl.by_name.useShortestPath and decl.by_name.useShortestPath.key, "use_shortest_path")
check("decl color default", (function() for _, it in ipairs(decl.items) do if it.key == "boost_colour" then return it.default end end end)(), "#FFC800")
MOCK_DECLARE_CONFIG(decl.items)

-- ------------------------------------------------------------------ 9. ConfigManager

local cm = core.config_manager()
check("config step ready (no file)", core.config_step(), "ready")
check("config load state", cm.load_state, "missing")
cm:prime_declared()

check("proxy default bool", cfg:showOverlay(), true)
check("proxy default int", cfg:outlineThickness(), 4)
check("proxy default enum", cfg:highlightStyleNpcs(), C["com.questhelper.QuestHelperConfig$NpcHighlightStyle"].OUTLINE)
check("proxy default color", cfg:textHighlightColor():getRGB(), C["java.awt.Color"].BLUE:getRGB())
check("proxy undeclared default", cfg:filterListBy(), C["com.questhelper.QuestHelperConfig$QuestFilter"].SHOW_ALL)
ok("proxy instanceof", J.inst(cfg, QHC) and J.inst(cfg, "net.runelite.client.config.Config"))

local changes = {}
J.def("test.CfgListener", { kind = "class", super = "java.lang.Object", ifaces = {},
  subscribe = { { "onConfigChanged", CE .. "ConfigChanged", 0 }, { "onRuneScapeProfileChanged", CE .. "RuneScapeProfileChanged", 0 } },
  body = function(cls)
    function cls.__init(self) end
    function cls.onConfigChanged(self, ev)
      changes[#changes + 1] = ev:getGroup() .. "/" .. tostring(ev:getProfile()) .. "/" .. ev:getKey() .. ":" .. tostring(ev:getOldValue()) .. ">" .. tostring(ev:getNewValue())
    end
    function cls.onRuneScapeProfileChanged(self, ev)
      changes[#changes + 1] = "profile:" .. tostring(ev:getPreviousProfile()) .. ">" .. tostring(ev:getNewProfile())
    end
  end })
local cl = C["test.CfgListener"].__new()
bus:register__Object(cl)

-- the user edits a declared item in the client's settings panel
MOCK.config.show_overlay_panel = false
check("proxy caches until a change is reported", cfg:showOverlay(), true)
core.config_changed("show_overlay_panel")
check("client change reaches the proxy", cfg:showOverlay(), false)
check("client change posts ConfigChanged", changes[1], "questhelper/nil/showOverlayPanel:true>false")
core.config_changed("show_overlay_panel")
check("unchanged client value posts nothing", #changes, 1)
core.config_changed("not_ours")
check("unknown client key ignored", #changes, 1)

-- the plugin sets a declared item (the panel's ordering dropdown)
changes = {}
cm:setConfiguration__String_String_Object("questhelper", "orderListBy", C["com.questhelper.QuestHelperConfig$QuestOrdering"].Z_TO_A)
check("declared set lands in the client", MOCK.config.order_list_by, "Z_TO_A")
check("declared set read back", cfg:orderListBy(), C["com.questhelper.QuestHelperConfig$QuestOrdering"].Z_TO_A)
check("declared set posts once", #changes, 1)
check("declared set event", changes[1], "questhelper/nil/orderListBy:A_TO_Z>Z_TO_A")
core.config_changed("order_list_by")
check("client echo of our own set posts nothing", #changes, 1)
cm:setConfiguration__String_String_Object("questhelper", "textHighlightColor", C["java.awt.Color"].RED)
check("declared colour stored as rgb", MOCK.config.text_highlight_color, 0xFF0000)
check("declared colour as RuneLite's string", cm:getConfiguration__String_String("questhelper", "textHighlightColor"), "-65536")
check("declared colour read back", cfg:textHighlightColor():getRGB(), C["java.awt.Color"].RED:getRGB())
cm:setConfiguration__String_String_String("questhelper", "outlineThickness", "7")
check("declared int", cfg:outlineThickness(), 7)
check("declared int typed get", cm:getConfiguration__String_String_Type("questhelper", "outlineThickness", "int"), 7)
cm:unsetConfiguration__String_String("questhelper", "outlineThickness")
check("declared unset -> default", cfg:outlineThickness(), 4)
check("declared bool string", cm:getConfiguration__String_String("questhelper", "showOverlayPanel"), "false")

-- an undeclared item and free keys live in the config file
changes = {}
cm:setConfiguration__String_String_Object("questhelper", "filterListBy", C["com.questhelper.QuestHelperConfig$QuestFilter"].QUEST)
check("undeclared enum read back", cfg:filterListBy(), C["com.questhelper.QuestHelperConfig$QuestFilter"].QUEST)
check("undeclared posts", changes[1], "questhelper/nil/filterListBy:nil>QUEST")
cm:setConfiguration__String_String_Object("questhelper", "skillfilterAttack", true)
check("free key string", cm:getConfiguration__String_String("questhelper", "skillfilterAttack"), "true")
check("free key typed", cm:getConfiguration__String_String_Type("questhelper", "skillfilterAttack", J.classof("java.lang.Boolean")), true)
cm:setConfiguration__String_String_String("questhelper", "bad", "abc")
check("typed get that does not parse is nil", cm:getConfiguration__String_String_Type("questhelper", "bad", "int"), nil)
raises("key with ':' refused", function() cm:setConfiguration__String_String_String("questhelper", "a:b", "x") end, "IllegalArgument")
raises("null value refused", function() cm:setConfiguration__String_String_String("questhelper", "k", nil) end, "NullPointer")
local before = #changes
cm:setConfiguration__String_String_String("questhelper", "skillfilterAttack", "true")
check("same value posts nothing", #changes, before)

-- RS profile
changes = {}
local saved_player = MOCK.player
MOCK.player = nil
MOCK_API.world.local_player = function() return MOCK.player end
nlogs = #logs
cm:setRSProfileConfiguration("questhelpervars", "x", "1")
check("RS profile set while logged out is dropped", cm:getRSProfileKey(), nil)
ok("RS profile set while logged out warns", logs[nlogs + 1] and logs[nlogs + 1]:find("not logged in", 1, true))
check("RS profile get while logged out", cm:getRSProfileConfiguration__String_String("questhelpervars", "x"), nil)
MOCK.player = { name = "Te ster", true_x = 3222, true_z = 3218, level = 0 }
core.update_rs_profile()
check("RS profile key", cm:getRSProfileKey(), "rsprofile.te_ster")
check("RS profile changed event", changes[1], "profile:nil>rsprofile.te_ster")
client.wt:add(C["net.runelite.api.WorldType"].SEASONAL)
check("RuneScapeProfileType.getCurrent seasonal", C["net.runelite.client.config.RuneScapeProfileType"].getCurrent(client).__name, "DEMONIC_PACTS_LEAGUE")
core.update_rs_profile()
check("RS profile key per profile type", cm:getRSProfileKey(), "rsprofile.te_ster.demonic_pacts_league")
client.wt:clear()
core.update_rs_profile()
check("RuneScapeProfileType.getCurrent standard", C["net.runelite.client.config.RuneScapeProfileType"].getCurrent(client), C["net.runelite.client.config.RuneScapeProfileType"].STANDARD)

-- a generated requirement over the RS profile config
local RR = C["com.questhelper.requirements.runelite.RuneliteRequirement"]
local req = RR.__new__ConfigManager_String_String(cm, "testVar", "true")
check("RuneliteRequirement init value written", cm:getRSProfileConfiguration__String_String("questhelpervars", "testVar"), "false")
check("RuneliteRequirement unmet", req:check__(), false)
req:setConfigValue__String("true")
check("RuneliteRequirement met", req:check__(), true)
check("RS profile whole key in the store", cm.store["questhelpervars.rsprofile.te_ster.testVar"], "true")
cm:unsetRSProfileConfiguration("questhelpervars", "testVar")
check("RS profile unset", req:configExists(), false)
req:setConfigValue__String("true")

-- persistence: the debounced save, then a fresh manager reads it back
MOCK.asset_saves = {}
MOCK.now_ms = 100000
core.frame()
check("config saved on frame", MOCK.asset_saves["qh_config.json"], 1)
cm:setConfiguration__String_String_String("questhelper", "n1", "a")
MOCK.now_ms = 100400
core.frame()
check("save debounced", MOCK.asset_saves["qh_config.json"], 1)
MOCK.now_ms = 101500
core.frame()
check("save after debounce", MOCK.asset_saves["qh_config.json"], 2)
core.frame()
check("nothing dirty, no save", MOCK.asset_saves["qh_config.json"], 2)
local text = MOCK.assets["qh_config.json"]
ok("file holds RS profile value", text:find('"questhelpervars.rsprofile.te_ster.testVar": "true"', 1, true))
ok("file holds undeclared item", text:find('"questhelper.filterListBy": "QUEST"', 1, true))
ok("file has no declared item", not text:find("orderListBy", 1, true))

local cm2 = C["net.runelite.client.config.ConfigManager"].__new()
check("fresh manager loads", cm2:load_step(), "ready")
check("fresh manager value", cm2:getConfiguration__String_String("questhelper", "n1"), "a")
check("fresh manager profile value", cm2:getConfiguration__String_String_String("questhelpervars", "rsprofile.te_ster", "testVar"), "true")

-- a slow read: writes made meanwhile win; never saved before the read lands
MOCK.asset_states["qh_config.json"] = "pending"
local cm3 = C["net.runelite.client.config.ConfigManager"].__new()
check("slow read pending", cm3:load_step(), "pending")
cm3:setConfiguration__String_String_String("questhelper", "n1", "b")
MOCK.asset_saves = {}
ok("no save while pending", not cm3:save_now())
MOCK.asset_states["qh_config.json"] = nil
check("slow read lands", cm3:load_step(), "ready")
check("write during read wins", cm3:getConfiguration__String_String("questhelper", "n1"), "b")
check("rest of file merged", cm3:getConfiguration__String_String("questhelper", "filterListBy"), "QUEST")

-- an unreadable file is never overwritten
local good_text = MOCK.assets["qh_config.json"]
MOCK.assets["qh_config.json"] = "{ not json"
local cm4 = C["net.runelite.client.config.ConfigManager"].__new()
check("unreadable loads (empty)", cm4:load_step(), "ready")
cm4:setConfiguration__String_String_String("questhelper", "n1", "c")
MOCK.asset_saves = {}
ok("unreadable file not saved", not cm4:save_now())
check("unreadable file untouched", MOCK.assets["qh_config.json"], "{ not json")
MOCK.assets["qh_config.json"] = good_text
bus:unregister__Object(cl)

-- ------------------------------------------------------------------ 10. plugin glue

local started = {}
J.def("test.Plugin", { kind = "class", super = "net.runelite.client.plugins.Plugin", ifaces = {},
  inject = { { "config", QHC }, { "eventBus", "net.runelite.client.eventbus.EventBus" }, { "toolbar", "net.runelite.client.ui.ClientToolbar" }, { "client", "net.runelite.api.Client" } },
  subscribe = { { "onGameTick", E .. "GameTick", 0 } },
  body = function(cls, super)
    function cls.__init(self) super.__init(self) end
    function cls.startUp(self)
      started[#started + 1] = "up:" .. tostring(self.config:showOverlay())
      self.nav = C["net.runelite.client.ui.NavigationButton"].builder():tooltip("Quest Helper"):priority(7):panel("P"):build()
      self.toolbar:addNavigation(self.nav)
      self.toolbar:openPanel(self.nav)
    end
    function cls.shutDown(self)
      started[#started + 1] = "down"
      self.toolbar:removeNavigation(self.nav)
    end
    function cls.onGameTick(self, ev) started[#started + 1] = "tick" end
  end })
local seen = {}
core.toolbar():listen(function(kind, b) seen[#seen + 1] = kind .. ":" .. b:getTooltip() end)
local p = core.start_plugin("test.Plugin", "Quest Helper")
check("plugin started", started[1], "up:false")
check("plugin injector", p:getInjector():resolve("test.Plugin"), p)
check("plugin client", p.client, client)
check("plugin name", p:getName(), "Quest Helper")
check("PluginManager lists it", root:binding("net.runelite.client.plugins.PluginManager"):getPlugins():contains(p), true)
check("toolbar add + open", table.concat(seen, ","), "add:Quest Helper,open:Quest Helper")
check("nav button priority", p.nav:getPriority(), 7)
core.post(E .. "GameTick")
check("plugin registered after startUp", started[2], "tick")
raises("second plugin refused", function() core.start_plugin("test.Plugin") end, "already started")
core.stop_plugin()
check("plugin shut down", started[3], "down")
core.post(E .. "GameTick")
check("plugin unregistered", #started, 3)
check("toolbar remove", seen[3], "remove:Quest Helper")

-- Hooks
local hooks = core.hooks()
local hide = J.fn("net.runelite.client.callback.Hooks$RenderableDrawListener", "draw", function(_, r, ui) return r ~= "hidden" end)
hooks:registerRenderableDrawListener(hide)
check("hooks hide", hooks:should_draw("hidden", false), false)
check("hooks show", hooks:should_draw("shown", false), true)
hooks:unregisterRenderableDrawListener(hide)
check("hooks unregistered", hooks:should_draw("hidden", false), true)

-- ------------------------------------------------------------------ report
MOCK.player = saved_player
if #fails > 0 then
  for _, f in ipairs(fails) do print("FAIL " .. f) end
end
print(string.format("test_rl_core: %d passed, %d failed", pass, #fails))
if #fails > 0 then os.exit(1) end
