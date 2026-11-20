-- The RuneLite plugin framework under the Quest Helper port: the EventBus,
-- the Guice injector, ClientThread, ConfigManager (with persistence and the
-- QuestHelperConfig proxy), Plugin / PluginManager / ClientToolbar /
-- NavigationButton, every event class the generated code names under
-- net.runelite.api.events and net.runelite.client.events, the clock and the
-- log. docs/QUEST_HELPER_PORT.md, "The RuneLite layer", row rl_core.lua.
--
-- The client is reached only through QH.api, read at call time (the entry
-- sets it before any of this runs). What the entry drives lives on QH.core:
--
--   QH.core.boot()                -> root injector (EventBus, ClientThread,
--                                    ConfigManager, ClientToolbar, Hooks,
--                                    PluginManager, Gson bound); idempotent
--   QH.core.config_step()         -> 'pending' | 'ready': loads qh_config.json;
--                                    start the plugin only once it says ready
--   QH.core.start_plugin(key, name) / QH.core.stop_plugin()
--   QH.core.frame()               -> ClientThread.invokeLater + Swing queues,
--                                    the debounced config save (once a frame)
--   QH.core.tick_end()            -> ClientThread.invokeAtTickEnd queue (after
--                                    GameTick is posted)
--   QH.core.post(key, fields)     -> builds an event (QH.core.event) and posts it
--   QH.core.config_changed(key)   -> the client's on_config_changed(key):
--                                    posts ConfigChanged for a declared item
--   QH.core.update_rs_profile()   -> recompute the RS profile key (login/logout)
--   QH.core.config_decl(iface)    -> the plugin definition's config = {...}
--   QH.core.flush()               -> write the config file now (on_stop)

local J, C = J, C

QH = QH or {}
local M = QH.core or {}
QH.core = M

local function api()
  local a = QH.api
  assert(a, "QH.api is not set")
  return a
end

local function def(key, super, ifaces, body, kind)
  J.def(key, { kind = kind or "class", super = super or "java.lang.Object", ifaces = ifaces or {}, body = body })
end
local function iface(key, ifaces, body)
  J.def(key, { kind = "interface", super = "java.lang.Object", ifaces = ifaces or {}, body = body })
end

local function is_pending(e)
  return type(e) == "table" and rawget(e, "pending") ~= nil
end

local function describe(e)
  if type(e) == "table" and e.__supers then return e:toString() end
  return tostring(e)
end

-- ------------------------------------------------------------------ log, clock

-- J.log is what slf4j's Logger and Throwable.printStackTrace write through.
J.log = function(s)
  api().core.log(tostring(s))
end

-- System.currentTimeMillis: epoch milliseconds, anchored once on the
-- machine's wall clock (api.client.datestamp, local time, whole seconds) and
-- advanced by the client's monotonic frame clock, so it never runs backwards.
local clock_anchor_epoch, clock_anchor_frame = nil, nil

local function days_from_civil(y, m, d)
  y = (m <= 2) and (y - 1) or y
  local era = (y >= 0 and y or y - 399) // 400
  local yoe = y - era * 400
  local mp = (m + 9) % 12
  local doy = (153 * mp + 2) // 5 + d - 1
  local doe = yoe * 365 + yoe // 4 - yoe // 100 + doy
  return era * 146097 + doe - 719468
end
M.days_from_civil = days_from_civil

J.now_ms = function()
  local a = api()
  local frame = a.core.frame_ms()
  if clock_anchor_epoch == nil then
    local stamp = a.client and a.client.datestamp and a.client.datestamp() or nil
    local y, mo, d, h, mi, s
    if stamp then
      y, mo, d, h, mi, s = stamp:match("^(%d+)%-(%d+)%-(%d+)_(%d+)%-(%d+)%-(%d+)$")
    end
    if y then
      local days = days_from_civil(math.tointeger(tonumber(y)), math.tointeger(tonumber(mo)), math.tointeger(tonumber(d)))
      clock_anchor_epoch = ((days * 24 + tonumber(h)) * 60 + tonumber(mi)) * 60000 + tonumber(s) * 1000
      clock_anchor_epoch = math.tointeger(clock_anchor_epoch)
    else
      clock_anchor_epoch = 0
    end
    clock_anchor_frame = frame
  end
  return clock_anchor_epoch + (frame - clock_anchor_frame)
end

-- ------------------------------------------------------------------ event classes

-- Plain field events: a no-arg constructor, a getter and setter per field
-- (Lombok @Data / @Value), and toString. QH.core.event(key, fields) builds one.
local function event_class(key, fields, extra, init)
  def(key, nil, {}, function(cls)
    function cls.__init(self)
      if init then init(self) end
    end
    for _, f in ipairs(fields) do
      local cap = f:sub(1, 1):upper() .. f:sub(2)
      cls["get" .. cap] = function(self) return self[f] end
      cls["set" .. cap] = function(self, v) self[f] = v end
    end
    function cls.toString(self)
      local parts = {}
      for _, f in ipairs(fields) do parts[#parts + 1] = f .. "=" .. J.str(self[f]) end
      return J.simple_name(self.__key) .. "(" .. table.concat(parts, ", ") .. ")"
    end
    if extra then extra(cls) end
  end)
end

local function consumable(cls)
  function cls.consume(self) self.consumed = true end
  function cls.isConsumed(self) return self.consumed == true end
end

local E = "net.runelite.api.events."
event_class(E .. "GameTick", {})
event_class(E .. "ClientTick", {})
event_class(E .. "GameStateChanged", { "gameState" })
event_class(E .. "ChatMessage", { "messageNode", "type", "name", "message", "sender", "timestamp" }, nil,
  function(self) self.timestamp = 0 end)
event_class(E .. "CommandExecuted", { "command", "arguments" }, nil,
  function(self) self.arguments = J.arr({}, 0) end)
event_class(E .. "VarbitChanged", { "varpId", "varbitId", "value" }, function(cls)
  -- getIndex is the deprecated spelling of getVarpId
  function cls.getIndex(self) return self.varpId end
  function cls.getLongValue(self) return self.value end
end, function(self) self.varpId = -1; self.varbitId = -1; self.value = 0 end)
event_class(E .. "VarClientIntChanged", { "index" }, nil, function(self) self.index = 0 end)
event_class(E .. "VarClientStrChanged", { "index" }, nil, function(self) self.index = 0 end)
event_class(E .. "WidgetLoaded", { "groupId" }, nil, function(self) self.groupId = -1 end)
event_class(E .. "WidgetClosed", { "groupId", "modalMode", "unload" }, function(cls)
  function cls.isUnload(self) return self.unload == true end
end, function(self) self.groupId = -1; self.modalMode = 0; self.unload = false end)
event_class(E .. "ItemContainerChanged", { "containerId", "itemContainer" }, nil, function(self) self.containerId = -1 end)
event_class(E .. "NpcSpawned", { "npc" })
event_class(E .. "NpcDespawned", { "npc" })
event_class(E .. "NpcChanged", { "npc", "old" })
event_class(E .. "InteractingChanged", { "source", "target" })
event_class(E .. "ItemSpawned", { "tile", "item" })
event_class(E .. "ItemDespawned", { "tile", "item" })
event_class(E .. "GameObjectSpawned", { "tile", "gameObject" })
event_class(E .. "GameObjectDespawned", { "tile", "gameObject" })
event_class(E .. "WallObjectSpawned", { "tile", "wallObject" })
event_class(E .. "WallObjectDespawned", { "tile", "wallObject" })
event_class(E .. "DecorativeObjectSpawned", { "tile", "decorativeObject" })
event_class(E .. "DecorativeObjectDespawned", { "tile", "decorativeObject" })
event_class(E .. "GroundObjectSpawned", { "tile", "groundObject" })
event_class(E .. "GroundObjectDespawned", { "tile", "groundObject" })
event_class(E .. "GraphicsObjectCreated", { "graphicsObject" })
event_class(E .. "ScriptCallbackEvent", { "script", "eventName" })
event_class(E .. "ScriptPreFired", { "scriptId", "scriptEvent" }, nil, function(self) self.scriptId = -1 end)
event_class(E .. "ScriptPostFired", { "scriptId" }, nil, function(self) self.scriptId = -1 end)
event_class(E .. "GrandExchangeSearched", { "consumed" }, consumable, function(self) self.consumed = false end)

-- The menu events wrap a MenuEntry (rt/rl_client.lua or rl_widgets.lua) and
-- read through it, as RuneLite's do. A field of the same meaning set on the
-- event (QH.core.event(key, {menuEntry = e, option = ...})) answers when the
-- entry object lacks that getter.
local function via_entry(self, method, field)
  local e = self.menuEntry
  if e ~= nil then
    local f = e[method]
    if f ~= nil then return f(e) end
  end
  return self[field]
end

local function menu_action_id(t)
  if type(t) == "table" then return t:getId() end
  return t
end

event_class(E .. "MenuEntryAdded", { "menuEntry" }, function(cls)
  function cls.__init__MenuEntry(self, e) self.menuEntry = e end
  function cls.getOption(self) return via_entry(self, "getOption", "option") end
  function cls.getTarget(self) return via_entry(self, "getTarget", "target") end
  function cls.getType(self) return menu_action_id(via_entry(self, "getType", "type")) end
  function cls.getIdentifier(self) return via_entry(self, "getIdentifier", "identifier") end
  function cls.getActionParam0(self) return via_entry(self, "getParam0", "param0") end
  function cls.getActionParam1(self) return via_entry(self, "getParam1", "param1") end
  function cls.getItemId(self) return via_entry(self, "getItemId", "itemId") end
end)

event_class(E .. "MenuOptionClicked", { "menuEntry" }, function(cls)
  consumable(cls)
  function cls.__init__MenuEntry(self, e) self.menuEntry = e end
  function cls.getParam0(self) return via_entry(self, "getParam0", "param0") end
  function cls.getParam1(self) return via_entry(self, "getParam1", "param1") end
  function cls.getActionParam(self) return cls.getParam0(self) end
  function cls.getWidgetId(self) return cls.getParam1(self) end
  function cls.getMenuOption(self) return via_entry(self, "getOption", "option") end
  function cls.getMenuTarget(self) return via_entry(self, "getTarget", "target") end
  function cls.getMenuAction(self) return via_entry(self, "getType", "type") end
  function cls.getId(self) return via_entry(self, "getIdentifier", "identifier") end
  function cls.getItemId(self) return via_entry(self, "getItemId", "itemId") end
  function cls.getItemOp(self) return via_entry(self, "getItemOp", "itemOp") end
  function cls.isItemOp(self) return via_entry(self, "isItemOp", "isItemOpValue") == true end
  function cls.getWidget(self) return via_entry(self, "getWidget", "widget") end
end, function(self) self.consumed = false end)

local CE = "net.runelite.client.events."
event_class(CE .. "ConfigChanged", { "group", "profile", "key", "oldValue", "newValue" })
event_class(CE .. "RuneScapeProfileChanged", { "previousProfile", "newProfile" })
event_class(CE .. "ServerNpcLoot", { "composition", "items" })
event_class(CE .. "ClientShutdown", { "tasks" }, function(cls)
  function cls.waitFor(self, f) end
  function cls.waitForAllConsumers(self, d) end
end)
event_class(CE .. "PluginMessage", { "namespace", "name", "data" }, function(cls)
  function cls.__init__String_String(self, namespace, name)
    assert(namespace ~= nil, "PluginMessage namespace")
    assert(name ~= nil, "PluginMessage name")
    self.namespace = namespace
    self.name = name
    self.data = C["java.util.Collections"].emptyMap()
  end
  function cls.__init__String_String_Map(self, namespace, name, data)
    assert(namespace ~= nil, "PluginMessage namespace")
    assert(name ~= nil, "PluginMessage name")
    assert(data ~= nil, "PluginMessage data")
    self.namespace = namespace
    self.name = name
    self.data = data
  end
end)

-- An event built from a Lua table of its fields.
function M.event(key, fields)
  local e = C[key].__new()
  if fields then
    for k, v in pairs(fields) do e[k] = v end
  end
  return e
end

-- "::questhelperdebug enable" / "questhelperdebug enable" -> CommandExecuted.
function M.command_event(text)
  assert(type(text) == "string", "command text")
  local body = text:gsub("^::", "")
  local words = {}
  for w in body:gmatch("%S+") do words[#words + 1] = w end
  local cmd = table.remove(words, 1) or ""
  return M.event(E .. "CommandExecuted", { command = cmd, arguments = J.arr(words, #words) })
end

-- ------------------------------------------------------------------ EventBus

def("net.runelite.client.eventbus.EventBus$Subscriber", nil, {}, function(cls)
  function cls.getObject(self) return self.object end
  function cls.getPriority(self) return self.priority end
end)

def("net.runelite.client.eventbus.EventBus", nil, {}, function(cls)
  function cls.__init(self)
    self.subs = {}    -- event key -> array of Subscriber, dispatch order
    self.serial = 0
  end

  local function sort_list(list)
    table.sort(list, function(a, b)
      if a.priority ~= b.priority then return a.priority > b.priority end
      if a.cname ~= b.cname then return a.cname < b.cname end
      return a.serial < b.serial
    end)
  end

  local function add(self, key, sub)
    self.serial = self.serial + 1
    sub.serial = self.serial
    local list = self.subs[key]
    if list == nil then
      list = {}
      self.subs[key] = list
    end
    list[#list + 1] = sub
    sort_list(list)
  end

  -- Every @Subscribe method of the object's class and its superclasses
  -- (__def.subscribe = {{method, eventKey, priority}, ...}); ordered by
  -- priority (higher first), then the subscriber's class name, as RuneLite.
  function cls.register__Object(self, obj)
    assert(obj ~= nil, "EventBus.register: object")
    local level = obj.__cls
    assert(level, "EventBus.register: not an object")
    while level do
      local d = rawget(level, "__def")
      if d and d.subscribe then
        for _, s in ipairs(d.subscribe) do
          local sub = setmetatable({}, C["net.runelite.client.eventbus.EventBus$Subscriber"])
          sub.object = obj
          sub.method = s[1]
          sub.priority = s[3] or 0
          sub.cname = obj.__key
          add(self, s[2], sub)
        end
      end
      level = rawget(level, "__super")
    end
  end

  -- register(Class, Consumer, float): a lambda subscriber.
  function cls.register__Class_Consumer_float(self, klass, fn, priority)
    assert(klass ~= nil, "EventBus.register: class")
    assert(fn ~= nil, "EventBus.register: consumer")
    local sub = setmetatable({}, C["net.runelite.client.eventbus.EventBus$Subscriber"])
    sub.object = fn
    sub.lambda = fn
    sub.priority = priority or 0
    sub.cname = ""
    add(self, klass.k, sub)
    return sub
  end

  function cls.unregister__Object(self, obj)
    assert(obj ~= nil, "EventBus.unregister: object")
    for key, list in pairs(self.subs) do
      local kept = {}
      for _, s in ipairs(list) do
        if not rawequal(s.object, obj) then kept[#kept + 1] = s end
      end
      self.subs[key] = kept
    end
  end

  function cls.unregister__Subscriber(self, sub)
    if sub == nil then return end
    for key, list in pairs(self.subs) do
      local kept = {}
      for _, s in ipairs(list) do
        if not rawequal(s, sub) then kept[#kept + 1] = s end
      end
      self.subs[key] = kept
    end
  end

  local function invoke(sub, event)
    if sub.lambda then
      sub.lambda:accept(event)
    else
      local o = sub.object
      o[sub.method](o, event)
    end
  end

  -- Posts to the subscribers of exactly the event's class, over a snapshot
  -- (a subscriber that registers or unregisters does not change this post).
  -- A subscriber's exception is logged and the next one still runs, as in
  -- RuneLite. A subscriber that needed a quest module not loaded yet
  -- (J.PendingMT) does not stop the others either: once every subscriber
  -- has run, the first such error is raised again, so the entry retries.
  -- The error a post re-raises carries `redo = {event, subs}`: the event and
  -- exactly the subscribers that went pending, so the entry can deliver it to
  -- THEM once the module lands (cls.redeliver) without running the others a
  -- second time.
  local function deliver(self, event, list)
    local pending, redo = nil, nil
    for _, sub in ipairs(list) do
      local ok, err = J.protect(invoke, sub, event)
      if not ok then
        if is_pending(err) then
          pending = pending or err
          redo = redo or {}
          redo[#redo + 1] = sub
        else
          J.log("warn EventBus: exception in " .. J.simple_name(sub.cname) .. "." .. tostring(sub.method)
            .. " for " .. J.simple_name(event.__key) .. ": " .. describe(err))
        end
      end
    end
    if pending then
      error(setmetatable({ pending = rawget(pending, "pending"), redo = { event = event, subs = redo } },
        J.PendingMT), 0)
    end
  end

  function cls.post(self, event)
    assert(event ~= nil, "EventBus.post: event")
    local list = self.subs[event.__key]
    if list == nil or #list == 0 then return end
    deliver(self, event, table.move(list, 1, #list, 1, {}))
  end

  -- Lua-side: post `event` again to `subs` only (a pending post's redo), those
  -- still registered. Raises like post for the ones that go pending again.
  function cls.redeliver(self, event, subs)
    assert(event ~= nil, "EventBus.redeliver: event")
    assert(subs ~= nil, "EventBus.redeliver: subscribers")
    local live = {}
    local list = self.subs[event.__key] or {}
    for _, sub in ipairs(subs) do
      for _, s in ipairs(list) do
        if rawequal(s, sub) then live[#live + 1] = sub; break end
      end
    end
    deliver(self, event, live)
  end

  -- Lua-side: how many subscribers an event class has (tests, diagnostics).
  function cls.subscriber_count(self, key)
    local list = self.subs[key]
    return list and #list or 0
  end
end)

-- ------------------------------------------------------------------ ClientThread, Swing

-- Everything in this client runs on one thread, so invoke() runs now.
-- invokeLater queues to the next QH.core.frame(); invokeAtTickEnd to the
-- next QH.core.tick_end(). A BooleanSupplier that answers false stays queued.
-- Work queued while a queue drains runs in the same drain (RuneLite's
-- ConcurrentLinkedQueue iterator sees appended items).
local function drain(list, label)
  local i = 1
  while i <= #list do
    local item = list[i]
    local ok, res = J.protect(item)
    local remove = true
    if ok then
      remove = res ~= false
    elseif is_pending(res) then
      -- a quest module is still being read: keep it, try next time
      remove = false
    else
      J.log("error " .. label .. ": exception in invoke: " .. describe(res))
    end
    if remove then table.remove(list, i) else i = i + 1 end
  end
end

local function runnable_item(r)
  assert(r ~= nil, "Runnable")
  return function() r:run(); return true end
end
local function supplier_item(r)
  assert(r ~= nil, "BooleanSupplier")
  return function() return r:getAsBoolean() == true end
end

def("net.runelite.client.callback.ClientThread", nil, {}, function(cls)
  function cls.__init(self)
    self.invokes = {}
    self.tick_end = {}
  end
  function cls.invoke__Runnable(self, r)
    assert(r ~= nil, "ClientThread.invoke: runnable")
    r:run()
  end
  function cls.invoke__BooleanSupplier(self, r)
    assert(r ~= nil, "ClientThread.invoke: supplier")
    if not r:getAsBoolean() then self.invokes[#self.invokes + 1] = supplier_item(r) end
  end
  function cls.invokeLater__Runnable(self, r)
    self.invokes[#self.invokes + 1] = runnable_item(r)
  end
  function cls.invokeLater__BooleanSupplier(self, r)
    self.invokes[#self.invokes + 1] = supplier_item(r)
  end
  function cls.invokeAtTickEnd(self, r)
    self.tick_end[#self.tick_end + 1] = runnable_item(r)
  end
  function cls.drain_invokes(self) drain(self.invokes, "ClientThread") end
  function cls.drain_tick_end(self) drain(self.tick_end, "ClientThread") end
  function cls.queued(self) return #self.invokes, #self.tick_end end
end)

local swing_queue = {}
def("javax.swing.SwingUtilities", nil, {}, function(cls)
  function cls.invokeLater(r)
    swing_queue[#swing_queue + 1] = runnable_item(r)
  end
  function cls.invokeAndWait(r)
    assert(r ~= nil, "SwingUtilities.invokeAndWait: runnable")
    r:run()
  end
  function cls.isEventDispatchThread() return true end
end)

-- ------------------------------------------------------------------ Hooks

iface("net.runelite.client.callback.Hooks$RenderableDrawListener")
def("net.runelite.client.callback.Hooks", nil, {}, function(cls)
  function cls.__init(self) self.draw_listeners = {} end
  function cls.registerRenderableDrawListener(self, l)
    assert(l ~= nil, "Hooks.registerRenderableDrawListener: listener")
    self.draw_listeners[#self.draw_listeners + 1] = l
  end
  function cls.unregisterRenderableDrawListener(self, l)
    assert(l ~= nil, "Hooks.unregisterRenderableDrawListener: listener")
    for i = #self.draw_listeners, 1, -1 do
      if rawequal(self.draw_listeners[i], l) then table.remove(self.draw_listeners, i) end
    end
  end
  -- Lua-side: false when a listener hides this renderable (rl_world asks).
  function cls.should_draw(self, renderable, drawing_ui)
    for _, l in ipairs(self.draw_listeners) do
      if not l:draw(renderable, drawing_ui == true) then return false end
    end
    return true
  end
end)

-- ------------------------------------------------------------------ Guice

-- Bumped by every binding or @Named write: the injectors' resolve memos
-- (Injector.resolve) are only good for the bindings they were made under.
local BIND_VER = 0

iface("com.google.inject.Module")
iface("net.runelite.client.input.KeyListener", {}, function(cls)
  function cls.keyTyped(self, e) end
  function cls.keyPressed(self, e) end
  function cls.keyReleased(self, e) end
end)

def("com.google.inject.binder.LinkedBindingBuilder", nil, {}, function(cls)
  function cls.toInstance(self, obj)
    assert(obj ~= nil, "LinkedBindingBuilder.toInstance: instance")
    self.injector.bindings[self.key] = obj
    BIND_VER = BIND_VER + 1
  end
end)

def("com.google.inject.Binder", nil, {}, function(cls)
  function cls.bind__Class(self, klass)
    assert(klass ~= nil, "Binder.bind: class")
    local b = setmetatable({}, C["com.google.inject.binder.LinkedBindingBuilder"])
    b.injector = self.injector
    b.key = klass.k
    return b
  end
  function cls.install(self, module)
    assert(module ~= nil, "Binder.install: module")
    module:configure(self)
  end
end)

-- @Singleton classes of the Quest Helper (Guice shares one instance per
-- injector tree; any other class is built anew at every injection point).
-- The transpiler emits `singleton = true` for @Singleton (2026-11-19); this
-- list is the same 17 classes, kept as a check for an older gen/.
local QH_SINGLETONS = {}
for _, k in ipairs({
  "bank.GroupBank", "bank.QuestBank", "bank.banktab.PotionStorage", "bank.banktab.QuestBankTab",
  "bank.banktab.QuestHelperBankTagService", "helpers.mischelpers.farmruns.FarmingWorld",
  "helpers.mischelpers.farmruns.PaymentTracker", "managers.NewVersionManager", "managers.QuestBankManager",
  "managers.QuestManager", "managers.QuestMenuHandler", "managers.QuestOverlayManager",
  "panel.skillfiltering.SkillFilterPanel", "runeliteobjects.extendedruneliteobjects.RuneliteObjectManager",
  "statemanagement.AchievementDiaryStepManager", "statemanagement.PlayerStateManager",
  "util.worldmap.WorldMapAreaManager",
}) do QH_SINGLETONS["com.questhelper." .. k] = true end
M.QH_SINGLETONS = QH_SINGLETONS

local PRIMITIVE = { boolean = true, int = true, long = true, double = true, float = true, short = true, byte = true, char = true }
local SKIP = {}  -- resolve(): leave the field as the constructor left it

local function is_singleton(key, cls)
  if key:sub(1, 15) ~= "com.questhelper" then return true end  -- runtime-provided services
  if QH_SINGLETONS[key] then return true end
  local d = rawget(cls, "__def")
  return d ~= nil and d.singleton == true
end

def("com.google.inject.Injector", nil, {}, function(cls)
  function cls.__init__Injector(self, parent)
    self.parent = parent
    self.bindings = {}   -- type key -> instance
    self.named = {}      -- field name -> value, for @Named primitives
    if parent == nil then
      self.jit = {}      -- singletons built just in time (root only)
      self.constructing = {}
    end
  end

  function cls.root(self)
    local r = self
    while r.parent do r = r.parent end
    return r
  end

  -- Lua-side: bind an instance to a type key, or a @Named value to a field.
  function cls.bind(self, key, instance)
    assert(type(key) == "string", "Injector.bind: key")
    assert(instance ~= nil, "Injector.bind: instance")
    self.bindings[key] = instance
    BIND_VER = BIND_VER + 1
  end
  function cls.bind_named(self, field, value)
    assert(type(field) == "string", "Injector.bind_named: field")
    self.named[field] = value
    BIND_VER = BIND_VER + 1
  end

  function cls.binding(self, key)
    local i = self
    while i do
      local b = i.bindings[key]
      if b ~= nil then return b end
      i = i.parent
    end
    return nil
  end

  local function named(self, field)
    local i = self
    while i do
      local v = i.named[field]
      if v ~= nil then return v end
      i = i.parent
    end
    return nil
  end

  local function creation_error(msg)
    J.throw(C["com.google.inject.CreationException"].__new__String(msg))
  end

  local function construct(self, key, cls_)
    local d = rawget(cls_, "__def")
    local ctor = d and d.inject_ctor
    if ctor then
      local types = ctor[2]
      local args = {}
      for i, t in ipairs(types) do args[i] = self:resolve(t, nil) end
      local new = rawget(cls_, "__new" .. ctor[1]:sub(7))
      assert(new, "inject_ctor names no constructor: " .. key)
      return new(table.unpack(args, 1, #types))
    end
    -- a no-arg constructor: `__init` (generated), or `__init__` (a runtime
    -- class whose constructor name is overloaded)
    local new = rawget(cls_, "__new") or rawget(cls_, "__new__")
    if new == nil then creation_error("no injectable constructor for " .. key) end
    return new()
  end

  -- What this injector answered for a key that answers the same every time
  -- (a binding, a Config, a singleton), until a binding changes. A quest's
  -- startUp injects every step -- and an OwnerStep's children again with
  -- it -- so Desert Treasure II resolved the same dozen keys thousands of
  -- times: 11M of its 14M startUp instructions (live: on_frame_start ran
  -- 16M of the 20M budget).
  local function memo_of(self)
    local m = self.memo
    if m == nil or m.ver ~= BIND_VER then
      m = { ver = BIND_VER }
      self.memo = m
    end
    return m
  end

  -- The instance a field (or a constructor parameter) of type `key` gets.
  function cls.resolve(self, key, field)
    if key == "com.google.inject.Injector" then return self end
    local memo = memo_of(self)
    local hit = memo[key]
    if hit ~= nil then return hit end
    local b = self:binding(key)
    if b ~= nil then memo[key] = b; return b end
    if PRIMITIVE[key] then
      local v = field and named(self, field)
      if v ~= nil then return v end
      return SKIP
    end
    local cls_ = C[key]
    local d = rawget(cls_, "__def")
    if d and d.config then
      -- a Config interface: the @Provides getConfig(ConfigManager) every
      -- RuneLite plugin writes
      local config = self:resolve("net.runelite.client.config.ConfigManager", nil):getConfig(J.classof(key))
      memo[key] = config
      return config
    end
    local root = self:root()
    local single = is_singleton(key, cls_)
    if single then
      local s = root.jit[key]
      if s ~= nil then memo[key] = s; return s end
    end
    if cls_.__kind == "interface" or (d and d.abstract) then
      creation_error("no implementation bound for " .. key)
    end
    if root.constructing[key] then creation_error("circular construction of " .. key) end
    root.constructing[key] = true
    local ok, obj = J.protect(construct, self, key, cls_)
    root.constructing[key] = nil
    if not ok then error(obj, 0) end
    -- cached before its members are injected, so a field cycle between
    -- singletons meets the same instance (Guice's construction context)
    if single then root.jit[key] = obj end
    self:injectMembers(obj)
    return obj
  end

  function cls.getInstance__Class(self, klass)
    assert(klass ~= nil, "Injector.getInstance: class")
    local v = self:resolve(klass.k, nil)
    assert(v ~= SKIP, "Injector.getInstance: unbound primitive " .. klass.k)
    return v
  end
  cls.getInstance__Key = cls.getInstance__Class

  -- A class's @Inject fields, superclass first as Guice fills them, read
  -- once per class (weak: a class the residency pass forgets goes with it).
  local inject_lists = setmetatable({}, { __mode = "k" })
  local function inject_list(top)
    local list = inject_lists[top]
    if list ~= nil then return list end
    local chain = {}
    local level = top
    while level do
      chain[#chain + 1] = level
      level = rawget(level, "__super")
    end
    list = {}
    for i = #chain, 1, -1 do
      local d = rawget(chain[i], "__def")
      if d and d.inject then
        for _, f in ipairs(d.inject) do list[#list + 1] = f end
      end
    end
    inject_lists[top] = list
    return list
  end

  -- Fills every @Inject field of the object's class chain
  -- (__def.inject = {{field, typeKey}, ...}).
  --
  -- An object this injector already filled under the same bindings is
  -- filled already: QuestHelper.instantiateSteps injects an OwnerStep's
  -- children again with every owner above them (Desert Treasure II: 29,723
  -- calls for 15,540 steps, 11M of its 14M startUp instructions; live
  -- on_frame_start ran 16M of the 20M budget). The one difference from Guice:
  -- a NON-singleton field (a step's VisibilityHelper, an empty cache) is not
  -- built a second time by the repeat.
  local injected = setmetatable({}, { __mode = "k" })
  function cls.injectMembers(self, obj)
    assert(obj ~= nil, "Injector.injectMembers: object")
    local level = obj.__cls
    assert(level, "Injector.injectMembers: not an object")
    local memo = memo_of(self)
    if injected[obj] == memo then return end
    for _, f in ipairs(inject_list(level)) do
      -- f[3] = the @Named binding (the transpiler emits it), else the field name
      local v = self:resolve(f[2], f[3] or f[1])
      if v ~= SKIP then obj[f[1]] = v end
    end
    -- resolve may have replaced the memo (a binding made while constructing)
    if memo == self.memo then injected[obj] = memo end
  end

  function cls.createChildInjector__ModuleArr(self, modules)
    assert(modules ~= nil, "Injector.createChildInjector: modules")
    local child = C["com.google.inject.Injector"].__new__Injector(self)
    local binder = setmetatable({ injector = child }, C["com.google.inject.Binder"])
    for i = 1, modules.n do
      assert(modules[i] ~= nil, "Injector.createChildInjector: null module")
      modules[i]:configure(binder)
    end
    return child
  end
  function cls.createChildInjector__Iterable(self, modules)
    local a = { n = 0 }
    for _, m in J.iter(modules) do a.n = a.n + 1; a[a.n] = m end
    return cls.createChildInjector__ModuleArr(self, a)
  end
  function cls.getParent(self) return self.parent end
end)

def("net.runelite.client.RuneLite", nil, {}, function(cls)
  function cls.getInjector()
    assert(M.injector, "RuneLite.getInjector before QH.core.boot")
    return M.injector
  end
end)

-- J.inject(obj): the contract's spelling of the root injector's injectMembers.
function J.inject(obj)
  assert(M.injector, "J.inject before QH.core.boot")
  M.injector:injectMembers(obj)
  return obj
end

-- ------------------------------------------------------------------ config values

local CONFIG_STR_MAX = 95      -- PLUGIN_LUA_STR_MAX - 1: key, label, default, choices
local CONFIG_ITEMS_MAX = 64    -- PLUGIN_LUA_MAX_CONFIG

local function type_key(t)
  if type(t) == "string" then return t end
  if type(t) == "table" then
    if t.k then return t.k end                       -- java.lang.Class
    if t.getTypeName then return t:getTypeName() end
  end
  return nil
end

local function number_format(s)
  J.throw(C["java.lang.NumberFormatException"].__new__String('For input string: "' .. tostring(s) .. '"'))
end

-- Integer.decode: decimal, 0x / 0X / # hex, leading 0 octal, a sign.
local function int_decode(s)
  local sign, rest = s:match("^([+-]?)(.*)$")
  local v
  if rest:match("^0[xX]%x+$") then v = tonumber(rest:sub(3), 16)
  elseif rest:match("^#%x+$") then v = tonumber(rest:sub(2), 16)
  elseif rest:match("^0[0-7]+$") then v = tonumber(rest:sub(2), 8)
  elseif rest:match("^%d+$") then v = tonumber(rest, 10)
  end
  if v == nil then number_format(s) end
  v = math.tointeger(v)
  if sign == "-" then v = -v end
  if v < -2147483648 or v > 2147483647 then number_format(s) end
  return v
end

local function parse_int(s)
  if not s:match("^[+-]?%d+$") then number_format(s) end
  local v = math.tointeger(tonumber(s))
  if v == nil or v < -2147483648 or v > 2147483647 then number_format(s) end
  return v
end

local function color_from_argb(argb)
  argb = argb & 0xFFFFFFFF
  return C["java.awt.Color"].__new__int_int_int_int((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, (argb >> 24) & 0xFF)
end

local function color_argb(c)
  return c:getRGB() & 0xFFFFFFFF
end

local function enum_value(key, name)
  local vs = C[key].__values
  for i = 1, vs.n do
    if vs[i].__name == name then return vs[i] end
  end
  J.throw(C["java.lang.IllegalArgumentException"].__new__String("No enum constant " .. key .. "." .. tostring(name)))
end

local function is_enum_key(key)
  if key == nil or PRIMITIVE[key] or key:sub(1, 10) == "java.lang." then return false end
  local cls = J.try_class(key)
  return cls ~= nil and cls.__kind == "enum"
end

-- ConfigManager.stringToObject.
local function string_to_object(str, t)
  local k = type_key(t)
  if k == "boolean" or k == "java.lang.Boolean" then return str:lower() == "true" end
  if k == "int" or k == "java.lang.Integer" or k == "long" or k == "java.lang.Long" then return parse_int(str) end
  if k == "double" or k == "java.lang.Double" or k == "float" or k == "java.lang.Float" then
    local v = tonumber(str)
    if v == nil then number_format(str) end
    return v + 0.0
  end
  if k == "java.awt.Color" then
    -- ColorUtil.fromString: Integer.decode, ARGB; null when it does not parse
    local ok, v = J.protect(int_decode, str)
    if not ok then return nil end
    return color_from_argb(v)
  end
  if k == "net.runelite.api.coords.WorldPoint" then
    local x, y, p = str:match("^(-?%d+):(-?%d+):(-?%d+)$")
    if not x then number_format(str) end
    return C["net.runelite.api.coords.WorldPoint"].__new__int_int_int(math.tointeger(tonumber(x)), math.tointeger(tonumber(y)), math.tointeger(tonumber(p)))
  end
  if k == "java.time.Duration" then return C["java.time.Duration"].ofMillis(parse_int(str)) end
  if is_enum_key(k) then return enum_value(k, str) end
  return str
end
M.string_to_object = string_to_object

-- ConfigManager.objectToString.
local function object_to_string(v)
  if v == nil then return nil end
  local t = type(v)
  if t == "string" then return v end
  if t == "boolean" then return v and "true" or "false" end
  if t == "number" then return J.str(v) end
  if J.inst(v, "java.awt.Color") then return tostring(J.l2i(color_argb(v))) end
  if J.inst(v, "java.lang.Enum") then return v:name() end
  if J.inst(v, "net.runelite.api.coords.WorldPoint") then
    return v:getX() .. ":" .. v:getY() .. ":" .. v:getPlane()
  end
  if J.inst(v, "java.time.Duration") then return tostring(v:toMillis()) end
  return J.str(v)
end
M.object_to_string = object_to_string

-- ------------------------------------------------------------------ config declaration

local function snake(name)
  local s = name:gsub("(%l)(%u)", "%1_%2"):gsub("(%d)(%u)", "%1_%2"):gsub("(%u)(%u%l)", "%1_%2")
  s = s:lower():gsub("[^a-z0-9_]", "_")
  return s
end
M.config_snake = snake

local function fit_label(s)
  if #s <= CONFIG_STR_MAX then return s end
  local cut = s:sub(1, CONFIG_STR_MAX)
  local at = cut:match("^.*()%s")
  if at and at > 1 then cut = cut:sub(1, at - 1) end
  return cut
end

local function default_of(iface_cls, method)
  local f = iface_cls[method]
  if f == nil then return nil end
  local probe = setmetatable({}, iface_cls)
  local ok, v = J.protect(f, probe)
  if not ok then
    if is_pending(v) then error(v, 0) end
    return nil
  end
  return v
end

local decl_cache = {}

-- @ConfigItems whose feature has nothing behind it in this client: declared
-- WITHOUT a label, like RuneLite's hidden items (stored and read at their
-- default, never offered), since a setting that does nothing is a bug.
--   useShortestPath        -- QuestManager.activateShortestPath posts a
--                             PluginMessage to the Shortest Path plugin;
--                             this client has none, so nothing listens.
--   devShowOverlayOnLaunch -- read only when @Named("developerMode") is
--                             true, and the entry binds it false for every
--                             player (QuestHelperDebugOverlay, ::questhelperdebug,
--                             ::qh-inv and ::qh are developer tools).
M.CONFIG_NOT_OFFERED = {
  useShortestPath = "no Shortest Path plugin in this client",
  devShowOverlayOnLaunch = "developer mode is off for every player",
}

-- The client declaration of a Config interface's @ConfigItems
-- (__def.config) and how each keyName is stored:
--   items   -> the plugin definition's config = {...} (key, type, label,
--              default, choices, min, max), at most 64, every string <= 95 bytes
--   by_name -> keyName -> {key = client key, ctype, jtype, method, default}
--              for the items in `items`; any other @ConfigItem lives in the
--              plugin's own config file like an undeclared key.
-- Booleans, ints, Colors, Strings and enums whose constant names fit one
-- choices string are declared; RuneLite's hidden items are declared without
-- a label (stored, not shown).
function M.config_decl(iface_key)
  local cached = decl_cache[iface_key]
  if cached then return cached end
  local cls = C[iface_key]
  local d = rawget(cls, "__def")
  assert(d and d.config, "not a Config interface: " .. tostring(iface_key))
  local out = { group = d.config_group, items = {}, by_name = {}, skipped = {} }
  local used = {}
  for _, c in ipairs(d.config) do
    if c.method ~= nil and c.keyName ~= nil then
      local key = snake(c.keyName)
      local jt = c.type
      local item = { key = key }
      local why = nil
      local dv = default_of(cls, c.method)
      if jt == "boolean" then
        item.type = "bool"
        item.default = dv == true
      elseif jt == "int" then
        item.type = "int"
        if math.type(dv) == "integer" then item.default = dv end
        local mn = c.min or (c.range and c.range.min)
        local mx = c.max or (c.range and c.range.max)
        if mn ~= nil and mx ~= nil then item.min = mn; item.max = mx end
      elseif jt == "java.awt.Color" then
        item.type = "color"
        if dv ~= nil then item.default = string.format("#%06X", color_argb(dv) & 0xFFFFFF) end
      elseif jt == "java.lang.String" then
        item.type = "string"
        if type(dv) == "string" then item.default = dv end
      elseif is_enum_key(jt) then
        item.type = "enum"
        local names = {}
        local vs = C[jt].__values
        for i = 1, vs.n do names[i] = vs[i].__name end
        item.choices = table.concat(names, "|")
        if #item.choices > CONFIG_STR_MAX then why = "choices exceed " .. CONFIG_STR_MAX .. " bytes" end
        if dv ~= nil then item.default = dv.__name end
      else
        why = "type " .. tostring(jt)
      end
      if why == nil and #key > CONFIG_STR_MAX then why = "key too long" end
      if why == nil and used[key] then why = "key collides with " .. used[key] end
      if why == nil and type(item.default) == "string" and #item.default > CONFIG_STR_MAX then why = "default too long" end
      if why == nil and #out.items >= CONFIG_ITEMS_MAX then why = "over " .. CONFIG_ITEMS_MAX .. " items" end
      if why == nil then
        if not c.hidden and M.CONFIG_NOT_OFFERED[c.keyName] == nil then
          item.label = fit_label(c.name or c.keyName)
        end
        out.items[#out.items + 1] = item
        used[key] = c.keyName
        out.by_name[c.keyName] = { key = key, ctype = item.type, jtype = jt, method = c.method, default = dv }
      else
        out.skipped[#out.skipped + 1] = { keyName = c.keyName, why = why }
      end
    end
  end
  decl_cache[iface_key] = out
  return out
end

-- ------------------------------------------------------------------ ConfigManager

local CONFIG_ASSET = "qh_config.json"
local SAVE_DEBOUNCE_MS = 1000

local function json_escape(s)
  return (s:gsub('[%c"\\]', function(ch)
    local m = { ['"'] = '\\"', ["\\"] = "\\\\", ["\n"] = "\\n", ["\r"] = "\\r", ["\t"] = "\\t" }
    return m[ch] or string.format("\\u%04x", ch:byte())
  end))
end

local function encode_store(store)
  local keys = {}
  for k in pairs(store) do keys[#keys + 1] = k end
  table.sort(keys)
  local parts = {}
  for i, k in ipairs(keys) do parts[i] = '  "' .. json_escape(k) .. '": "' .. json_escape(store[k]) .. '"' end
  return "{\n" .. table.concat(parts, ",\n") .. "\n}\n"
end

local function decode_store(text)
  local m = J.json.decode(text)
  assert(J.inst(m, "java.util.Map"), "config file is not a JSON object")
  local out = {}
  for _, e in J.iter(m:entrySet()) do
    local v = e:getValue()
    if v ~= nil then out[J.str(e:getKey())] = J.str(v) end
  end
  return out
end

local function whole_key(group, profile, key)
  if profile == nil then return group .. "." .. key end
  return group .. "." .. profile .. "." .. key
end

local function check_key(group, key)
  if group == nil or group == "" or key == nil or key == "" or key:find(":", 1, true) or key:sub(1, 1) == "$" then
    J.throw(C["java.lang.IllegalArgumentException"].__new__String("bad config key " .. tostring(group) .. "." .. tostring(key)))
  end
end

def("net.runelite.client.config.Config", nil, {}, nil, "interface")

def("net.runelite.client.config.ConfigManager", nil, {}, function(cls)
  function cls.__init(self)
    self.store = {}          -- whole key -> string, the config file
    self.touched = {}        -- whole keys written before the file was read
    self.load_state = nil    -- nil, 'pending', 'ready', 'missing', or a failure
    self.dirty = false
    self.last_save_ms = nil
    self.generation = 0
    self.proxies = {}        -- interface key -> proxy
    self.declared = {}       -- group -> keyName -> by_name entry
    self.declared_by_key = {} -- client key -> {group, keyName}
    self.declared_last = {}  -- group.keyName -> last string seen
    self.rsProfileKey = nil
  end

  -- ---------------------------------------------------------------- the file

  -- Reads the config file; 'pending' until the client answers. Writes made
  -- before then win over what the file held.
  function cls.load_step(self)
    if self.load_state ~= nil and self.load_state ~= "pending" then return "ready" end
    local a = api()
    local st = a.assets.request(CONFIG_ASSET)
    if st == "pending" then
      self.load_state = "pending"
      return "pending"
    end
    if st == "ready" then
      local bytes = a.assets.bytes(CONFIG_ASSET)
      local ok, loaded = J.protect(decode_store, bytes or "")
      if ok then
        for k, v in pairs(loaded) do
          if not self.touched[k] then self.store[k] = v end
        end
        self.load_state = "ready"
      else
        -- never overwrite a file that would not parse: it may be the only copy
        self.load_state = "unreadable"
        J.log("error ConfigManager: " .. CONFIG_ASSET .. " does not parse, config will not be saved: " .. describe(loaded))
      end
    elseif st == "missing" then
      self.load_state = "missing"
    else
      self.load_state = tostring(st)
      J.log("error ConfigManager: " .. CONFIG_ASSET .. " could not be read (" .. tostring(st) .. "), config will not be saved")
    end
    self.touched = {}
    self.generation = self.generation + 1
    return "ready"
  end

  function cls.can_save(self)
    return self.load_state == "ready" or self.load_state == "missing"
  end

  function cls.save_now(self)
    if not self.dirty or not self:can_save() then return false end
    local ok, result = api().assets.save(CONFIG_ASSET, encode_store(self.store))
    if not ok then
      J.log("warn ConfigManager: saving " .. CONFIG_ASSET .. " failed: " .. tostring(result))
      return false
    end
    self.dirty = false
    self.load_state = "ready"
    self.last_save_ms = api().core.frame_ms()
    return true
  end

  function cls.maybe_save(self)
    if not self.dirty or not self:can_save() then return end
    local now = api().core.frame_ms()
    if self.last_save_ms ~= nil and now - self.last_save_ms < SAVE_DEBOUNCE_MS then return end
    self:save_now()
  end

  -- ---------------------------------------------------------------- declared items

  function cls.declare(self, iface_key)
    local decl = M.config_decl(iface_key)
    local group = decl.group
    assert(group, "Config interface has no @ConfigGroup: " .. iface_key)
    if self.declared[group] then return decl end
    self.declared[group] = decl.by_name
    for name, e in pairs(decl.by_name) do
      self.declared_by_key[e.key] = { group = group, name = name }
    end
    return decl
  end

  local function declared_entry(self, group, profile, key)
    if profile ~= nil then return nil end
    local g = self.declared[group]
    return g and g[key] or nil
  end

  -- The Java value of a declared item as the client holds it, or nil.
  local function declared_value(self, e)
    local cfg = api().config
    local ct = e.ctype
    if ct == "bool" then return cfg.get_bool(e.key) end
    if ct == "int" then return cfg.get_int(e.key) end
    if ct == "color" then
      local rgb = cfg.get_color(e.key)
      if rgb == nil then return nil end
      local alpha = 255
      if e.default ~= nil then alpha = (color_argb(e.default) >> 24) & 0xFF end
      return color_from_argb((alpha << 24) | (rgb & 0xFFFFFF))
    end
    local s = cfg.get_string(e.key)
    if s == nil then return nil end
    if ct == "enum" then
      local ok, v = J.protect(enum_value, e.jtype, s)
      if ok then return v end
      return nil
    end
    return s
  end

  local function declared_string(self, e)
    local v = declared_value(self, e)
    if v == nil then return nil end
    return object_to_string(v)
  end

  local function declared_set(self, e, value)
    local cfg = api().config
    local ct = e.ctype
    local v
    if value == nil then
      -- unset: back to the declared default
      if e.default == nil then return false end
      value = object_to_string(e.default)
    end
    if ct == "bool" then v = value:lower() == "true"
    elseif ct == "int" then
      local ok, n = J.protect(parse_int, value)
      if not ok then J.log("warn ConfigManager: " .. e.key .. " is an int, not '" .. value .. "'"); return false end
      v = n
    elseif ct == "color" then
      local ok, n = J.protect(int_decode, value)
      if not ok then J.log("warn ConfigManager: " .. e.key .. " is a colour, not '" .. value .. "'"); return false end
      v = string.format("#%06X", n & 0xFFFFFF)
    else
      v = value
    end
    local ok, result = cfg.set(e.key, v)
    if not ok then
      J.log("warn ConfigManager: setting " .. e.key .. " refused: " .. tostring(result))
      return false
    end
    return true
  end

  -- Reads every declared item once, so a change made in the client's own
  -- settings panel can report what it was before.
  function cls.prime_declared(self)
    for group, items in pairs(self.declared) do
      for name, e in pairs(items) do
        self.declared_last[group .. "." .. name] = declared_string(self, e)
      end
    end
  end

  local function post_changed(self, group, profile, key, old, new)
    self.generation = self.generation + 1
    if self.eventBus == nil then return end
    local ev = M.event(CE .. "ConfigChanged", { group = group, profile = profile, key = key, oldValue = old, newValue = new })
    self.eventBus:post(ev)
  end

  -- The client's on_config_changed(key) for this plugin.
  function cls.client_config_changed(self, client_key)
    assert(type(client_key) == "string", "client config key")
    local ref = self.declared_by_key[client_key]
    if ref == nil then return end
    local e = self.declared[ref.group][ref.name]
    local wk = ref.group .. "." .. ref.name
    local new = declared_string(self, e)
    local old = self.declared_last[wk]
    if old == new then return end
    self.declared_last[wk] = new
    post_changed(self, ref.group, nil, ref.name, old, new)
  end

  -- ---------------------------------------------------------------- get

  function cls.get_raw(self, group, profile, key)
    local e = declared_entry(self, group, profile, key)
    if e then return declared_string(self, e) end
    return self.store[whole_key(group, profile, key)]
  end

  function cls.getConfiguration__String_String(self, group, key)
    return self:get_raw(group, nil, key)
  end
  function cls.getConfiguration__String_String_String(self, group, profile, key)
    return self:get_raw(group, profile, key)
  end
  function cls.getConfiguration__String_String_String_Type(self, group, profile, key, t)
    local value = self:get_raw(group, profile, key)
    if value == nil or value == "" then return nil end
    local ok, v = J.protect(string_to_object, value, t)
    if ok then return v end
    if is_pending(v) then error(v, 0) end
    J.log("warn ConfigManager: Unable to unmarshal " .. whole_key(group, profile, key) .. " " .. describe(v))
    return nil
  end
  function cls.getConfiguration__String_String_Type(self, group, key, t)
    return cls.getConfiguration__String_String_String_Type(self, group, nil, key, t)
  end
  function cls.getRSProfileConfiguration__String_String(self, group, key)
    if self.rsProfileKey == nil then return nil end
    return self:get_raw(group, self.rsProfileKey, key)
  end
  function cls.getRSProfileConfiguration__String_String_Type(self, group, key, t)
    if self.rsProfileKey == nil then return nil end
    return cls.getConfiguration__String_String_String_Type(self, group, self.rsProfileKey, key, t)
  end
  function cls.getRSProfileKey(self) return self.rsProfileKey end

  -- ---------------------------------------------------------------- set / unset

  function cls.set_raw(self, group, profile, key, value)
    check_key(group, key)
    if value == nil then J.npe("value is marked non-null but is null") end
    local e = declared_entry(self, group, profile, key)
    if e then
      local wk = group .. "." .. key
      local old = declared_string(self, e)
      if old == value then return end
      if not declared_set(self, e, value) then return end
      local new = declared_string(self, e)
      self.declared_last[wk] = new
      if old ~= new then post_changed(self, group, nil, key, old, new) end
      return
    end
    local wk = whole_key(group, profile, key)
    local old = self.store[wk]
    if old == value then return end
    self.store[wk] = value
    if self.load_state == nil or self.load_state == "pending" then self.touched[wk] = true end
    self.dirty = true
    post_changed(self, group, profile, key, old, value)
  end

  function cls.unset_raw(self, group, profile, key)
    local e = declared_entry(self, group, profile, key)
    if e then
      local wk = group .. "." .. key
      local old = declared_string(self, e)
      if not declared_set(self, e, nil) then return end
      local new = declared_string(self, e)
      self.declared_last[wk] = new
      if old ~= new then post_changed(self, group, nil, key, old, nil) end
      return
    end
    local wk = whole_key(group, profile, key)
    local old = self.store[wk]
    if self.load_state == nil or self.load_state == "pending" then self.touched[wk] = true end
    if old == nil then return end
    self.store[wk] = nil
    self.dirty = true
    post_changed(self, group, profile, key, old, nil)
  end

  function cls.setConfiguration__String_String_String(self, group, key, value)
    self:set_raw(group, nil, key, value)
  end
  function cls.setConfiguration__String_String_Object(self, group, key, value)
    if value == nil then J.npe("value is marked non-null but is null") end
    self:set_raw(group, nil, key, object_to_string(value))
  end
  function cls.setConfiguration__String_String_String_String(self, group, profile, key, value)
    self:set_raw(group, profile, key, value)
  end
  function cls.setConfiguration__String_String_String_Object(self, group, profile, key, value)
    if value == nil then J.npe("value is marked non-null but is null") end
    self:set_raw(group, profile, key, object_to_string(value))
  end
  function cls.setRSProfileConfiguration(self, group, key, value)
    if self.rsProfileKey == nil then self:update_rs_profile() end
    if self.rsProfileKey == nil then
      J.log("warn ConfigManager: trying to create a profile while not logged in")
      return
    end
    if value == nil then J.npe("value is marked non-null but is null") end
    self:set_raw(group, self.rsProfileKey, key, object_to_string(value))
  end
  function cls.unsetConfiguration__String_String(self, group, key)
    self:unset_raw(group, nil, key)
  end
  function cls.unsetConfiguration__String_String_String(self, group, profile, key)
    self:unset_raw(group, profile, key)
  end
  function cls.unsetRSProfileConfiguration(self, group, key)
    if self.rsProfileKey == nil then return end
    self:unset_raw(group, self.rsProfileKey, key)
  end

  -- ---------------------------------------------------------------- RS profile

  -- The profile is the logged-in account: its display name (RuneLite keys by
  -- account hash, which this client does not have) and RuneScapeProfileType.
  -- Logged out, there is none. Posts RuneScapeProfileChanged on a change.
  function cls.profile_for(self)
    local p = api().world.local_player()
    if p == nil or p.name == nil or p.name == "" then return nil end
    local name = p.name:gsub("[^A-Za-z0-9%-]", "_"):lower()
    local key = "rsprofile." .. name
    local client = M.injector and M.injector:binding("net.runelite.api.Client")
    if client ~= nil then
      local t = M.profile_type(client)
      if t.__name ~= "STANDARD" then key = key .. "." .. t.__name:lower() end
    end
    return key
  end

  function cls.update_rs_profile(self)
    local key = self:profile_for()
    if key == self.rsProfileKey then return end
    local previous = self.rsProfileKey
    self.rsProfileKey = key
    self.generation = self.generation + 1
    if self.eventBus then
      self.eventBus:post(M.event(CE .. "RuneScapeProfileChanged", { previousProfile = previous, newProfile = key }))
    end
  end

  -- ---------------------------------------------------------------- the proxy

  -- getConfig(SomeConfig.class): an object of the interface whose
  -- @ConfigItem methods read the stored value (declared items from the
  -- client's config, the rest from the config file) and fall back to the
  -- interface's default method, as RuneLite's invocation handler. Values are
  -- cached until any config value changes.
  function cls.getConfig(self, klass)
    assert(klass ~= nil, "ConfigManager.getConfig: class")
    local key = klass.k
    local p = self.proxies[key]
    if p then return p end
    local iface_cls = C[key]
    local d = rawget(iface_cls, "__def")
    assert(d and d.config, "ConfigManager.getConfig: not a Config interface: " .. key)
    self:declare(key)
    local group = d.config_group
    local manager = self
    local items = {}
    for _, c in ipairs(d.config) do
      if c.method ~= nil and c.keyName ~= nil then items[#items + 1] = c end
    end
    local proxy_cls = J.local_class(key .. "$Proxy", { kind = "class", super = "java.lang.Object", ifaces = { key },
      body = function(pc)
        for _, c in ipairs(items) do
          local method, keyName, jtype = c.method, c.keyName, c.type
          local default_fn = iface_cls[method]
          pc[method] = function(this)
            local cache = rawget(this, "__cache")
            if rawget(this, "__gen") ~= manager.generation then
              cache = {}
              rawset(this, "__cache", cache)
              rawset(this, "__gen", manager.generation)
            end
            local hit = cache[method]
            if hit ~= nil then
              if hit == J.NIL then return nil end
              return hit
            end
            local v
            local e = declared_entry(manager, group, nil, keyName)
            if e then
              v = declared_value(manager, e)
            else
              local raw = manager.store[whole_key(group, nil, keyName)]
              if raw ~= nil then
                local ok, conv = J.protect(string_to_object, raw, jtype)
                if ok then v = conv elseif is_pending(conv) then error(conv, 0) end
              end
            end
            if v == nil and default_fn ~= nil then v = default_fn(this) end
            if v == nil then cache[method] = J.NIL else cache[method] = v end
            return v
          end
        end
        function pc.toString(this) return "Proxy(" .. J.simple_name(key) .. ")" end
      end })
    p = setmetatable({}, proxy_cls)
    proxy_cls.__zero(p)
    self.proxies[key] = p
    return p
  end
end)

-- RuneScapeProfileType.getCurrent(Client): the last type whose test holds.
local PROFILE_TESTS = {
  BETA = { "NOSAVE_MODE", "BETA_WORLD" },
  QUEST_SPEEDRUNNING = { "QUEST_SPEEDRUNNING" },
  DEADMAN = { "DEADMAN" },
  PVP_ARENA = { "PVP_ARENA" },
  DEMONIC_PACTS_LEAGUE = { "SEASONAL" },
}
function M.profile_type(client)
  assert(client ~= nil, "RuneScapeProfileType.getCurrent: client")
  local T = C["net.runelite.client.config.RuneScapeProfileType"]
  local WT = C["net.runelite.api.WorldType"]
  local types = client:getWorldType()
  local vs = T.__values
  for i = vs.n, 1, -1 do
    local tests = PROFILE_TESTS[vs[i].__name]
    if tests then
      for _, wt in ipairs(tests) do
        local w = rawget(WT, wt)
        if w ~= nil and types:contains(w) then return vs[i] end
      end
    end
  end
  return T.STANDARD
end

do
  local RSPT = "net.runelite.client.config.RuneScapeProfileType"
  local function attach(cls) cls.getCurrent = M.profile_type end
  if rawget(C, RSPT) ~= nil then
    attach(rawget(C, RSPT))
  elseif J.defined(RSPT) then
    J.extend(RSPT, attach)
  else
    -- gen/enums.lua defines it later: extend it as it is defined
    local ext = J.ext_enum
    J.ext_enum = function(key, names, getters)
      ext(key, names, getters)
      if key == RSPT then J.extend(RSPT, attach) end
    end
  end
end

-- ------------------------------------------------------------------ Plugin, toolbar

def("net.runelite.client.plugins.Plugin", nil, { "com.google.inject.Module" }, function(cls)
  function cls.__init(self) end
  function cls.configure(self, binder) end
  function cls.startUp(self) end
  function cls.shutDown(self) end
  function cls.resetConfiguration(self) end
  function cls.getInjector(self) return self.injector end
  function cls.getName(self) return rawget(self, "__plugin_name") or J.simple_name(self.__key) end
end)

def("net.runelite.client.plugins.PluginManager", nil, {}, function(cls)
  function cls.__init(self) self.plugins = C["java.util.ArrayList"].__new__() end
  function cls.getPlugins(self) return self.plugins end
  function cls.isPluginEnabled(self, p) return self.plugins:contains(p) end
end)

def("net.runelite.client.ui.NavigationButton", nil, {}, function(cls)
  function cls.builder()
    local b = setmetatable({}, C["net.runelite.client.ui.NavigationButton$NavigationButtonBuilder"])
    b.f = { priority = 0 }
    return b
  end
  function cls.getTooltip(self) return self.tooltip end
  function cls.getIcon(self) return self.icon end
  function cls.getPriority(self) return self.priority end
  function cls.getPanel(self) return self.panel end
  function cls.isTab(self) return true end
  function cls.getOnClick(self) return self.onClick end
end)

def("net.runelite.client.ui.NavigationButton$NavigationButtonBuilder", nil, {}, function(cls)
  for _, f in ipairs({ "tooltip", "icon", "priority", "panel", "onClick", "tab", "popup" }) do
    cls[f] = function(self, v) self.f[f] = v; return self end
  end
  function cls.build(self)
    local b = setmetatable({}, C["net.runelite.client.ui.NavigationButton"])
    for k, v in pairs(self.f) do b[k] = v end
    return b
  end
end)

-- The side panel (ui/) listens: fn(kind, button) with kind 'add', 'remove'
-- or 'open'; buttons are in priority order.
-- RuneLite's colour-picker service: QuestHelperPlugin holds one (@Inject,
-- a Lombok getter) for its Swing config panel and calls nothing on it here.
def("net.runelite.client.ui.components.colorpicker.ColorPickerManager", nil, {}, function(cls)
  function cls.__init(self) end
end)

def("net.runelite.client.ui.ClientToolbar", nil, {}, function(cls)
  function cls.__init(self)
    self.buttons = {}
    self.listeners = {}
    self.opened = nil
  end
  local function notify(self, kind, b)
    for _, fn in ipairs(self.listeners) do fn(kind, b) end
  end
  function cls.listen(self, fn)
    assert(type(fn) == "function", "ClientToolbar.listen: function")
    self.listeners[#self.listeners + 1] = fn
  end
  function cls.addNavigation(self, b)
    assert(b ~= nil, "ClientToolbar.addNavigation: button")
    for _, x in ipairs(self.buttons) do if rawequal(x, b) then return end end
    self.buttons[#self.buttons + 1] = b
    table.sort(self.buttons, function(x, y) return (x.priority or 0) < (y.priority or 0) end)
    notify(self, "add", b)
  end
  function cls.removeNavigation(self, b)
    assert(b ~= nil, "ClientToolbar.removeNavigation: button")
    for i = #self.buttons, 1, -1 do
      if rawequal(self.buttons[i], b) then table.remove(self.buttons, i) end
    end
    if rawequal(self.opened, b) then self.opened = nil end
    notify(self, "remove", b)
  end
  function cls.openPanel(self, b)
    assert(b ~= nil, "ClientToolbar.openPanel: button")
    self.opened = b
    notify(self, "open", b)
  end
end)

-- ------------------------------------------------------------------ boot and drive

-- The root injector and the services every plugin is given. Idempotent.
function M.boot()
  if M.injector then return M.injector end
  local root = C["com.google.inject.Injector"].__new__Injector(nil)
  M.injector = root
  local bus = C["net.runelite.client.eventbus.EventBus"].__new()
  local thread = C["net.runelite.client.callback.ClientThread"].__new()
  local cm = C["net.runelite.client.config.ConfigManager"].__new()
  cm.eventBus = bus
  root:bind("net.runelite.client.eventbus.EventBus", bus)
  root:bind("net.runelite.client.callback.ClientThread", thread)
  root:bind("net.runelite.client.config.ConfigManager", cm)
  root:bind("net.runelite.client.ui.ClientToolbar", C["net.runelite.client.ui.ClientToolbar"].__new())
  root:bind("net.runelite.client.callback.Hooks", C["net.runelite.client.callback.Hooks"].__new())
  root:bind("net.runelite.client.plugins.PluginManager", C["net.runelite.client.plugins.PluginManager"].__new())
  root:bind("com.google.gson.Gson", C["com.google.gson.Gson"].__new__())
  M.eventBus, M.clientThread, M.configManager = bus, thread, cm
  return root
end

function M.event_bus() return M.eventBus end
function M.client_thread() return M.clientThread end
function M.config_manager() return M.configManager end
function M.toolbar() return M.injector:binding("net.runelite.client.ui.ClientToolbar") end
function M.hooks() return M.injector:binding("net.runelite.client.callback.Hooks") end

function M.config_step()
  assert(M.configManager, "QH.core.config_step before boot")
  return M.configManager:load_step()
end

-- PluginManager.startPlugin: the plugin's child injector (binds the plugin
-- class to the instance, installs it as a module), members injected,
-- startUp(), then registered on the EventBus.
function M.start_plugin(key, name)
  assert(M.injector, "QH.core.start_plugin before boot")
  assert(M.configManager.load_state ~= nil and M.configManager.load_state ~= "pending",
    "QH.core.start_plugin before config_step() answered ready")
  assert(M.plugin == nil, "a plugin is already started")
  local cls = C[key]
  local plugin = rawget(cls, "__new")()
  if name then rawset(plugin, "__plugin_name", name) end
  local module = J.fn("com.google.inject.Module", "configure", function(_, binder)
    binder:bind__Class(J.classof(key)):toInstance(plugin)
    binder:install(plugin)
  end)
  local child = M.injector:createChildInjector__ModuleArr(J.arr({ module }, 1))
  plugin.injector = child
  -- injecting a Config field builds its proxy, which declares its items
  child:injectMembers(plugin)
  M.configManager:prime_declared()
  M.configManager:update_rs_profile()
  plugin:startUp()
  M.eventBus:register__Object(plugin)
  M.injector:binding("net.runelite.client.plugins.PluginManager").plugins:add(plugin)
  M.plugin = plugin
  return plugin
end

function M.stop_plugin()
  local plugin = M.plugin
  if plugin == nil then return end
  M.plugin = nil
  M.eventBus:unregister__Object(plugin)
  M.injector:binding("net.runelite.client.plugins.PluginManager").plugins:remove(plugin)
  plugin:shutDown()
  M.flush()
end

-- Once a frame: the ClientThread's invokeLater queue, the Swing queue, then
-- the debounced config save.
function M.frame()
  assert(M.clientThread, "QH.core.frame before boot")
  M.clientThread:drain_invokes()
  drain(swing_queue, "SwingUtilities")
  M.configManager:maybe_save()
end

-- After the server tick's GameTick has been posted.
function M.tick_end()
  assert(M.clientThread, "QH.core.tick_end before boot")
  M.clientThread:drain_tick_end()
end

function M.post(key, fields)
  assert(M.eventBus, "QH.core.post before boot")
  local ev = M.event(key, fields)
  M.eventBus:post(ev)
  return ev
end

function M.config_changed(client_key)
  assert(M.configManager, "QH.core.config_changed before boot")
  M.configManager:client_config_changed(client_key)
end

function M.update_rs_profile()
  assert(M.configManager, "QH.core.update_rs_profile before boot")
  M.configManager:update_rs_profile()
end

function M.flush()
  if M.configManager then M.configManager:save_now() end
end

function M.swing_queued() return #swing_queue end

return M
