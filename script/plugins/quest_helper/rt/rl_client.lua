-- The RuneLite client API for the Quest Helper port, over the client's
-- plugin API (QH.api, read through api() on every call, never cached):
--
--   net.runelite.api.Client (vars, varcs, skills, game state, tick, plane,
--   world types, item containers, widgets, runScript/getIntStack, the menu,
--   enums and definitions, hint arrows, chat, sound, canvas, camera),
--   ItemContainer/Item, ItemComposition/NPCComposition/ObjectComposition/
--   EnumComposition, WorldMap/WorldMapData, Menu/MenuEntry, ItemManager,
--   SpriteManager, SkillIconManager, ChatMessageManager/ChatMessageBuilder/
--   QueuedMessage, Text, QuantityFormatter, ColorUtil, JagexColor,
--   JagexColors, ColorScheme.
--
-- What other files reach through (docs/QUEST_HELPER_PORT.md, "rl_client
-- hooks"):
--   J.rl_client.client()              the one Client
--   J.rl_client.menu_begin(ev)        on_menu_build, before MenuEntryAdded
--   J.rl_client.menu_end()            on_menu_build, after MenuEntryAdded
--   J.rl_client.menu_select(ev)       on_menu_select -> the MenuEntry chosen
--   J.rl_client.menu_click(entry)     run the entry's onClick consumer
--   J.rl_client.script_enter(ref) / script_leave()   around a script callback
--   J.rl_client.inventory_changed(inv)                on_inventory_changed
-- and what this file needs from the others:
--   WorldView.__top() / __byId(id), Player.__local(), NPC.__bySlot(slot),
--   RuneLiteObject.__new(client), ModelData.__load(id) / __merge(arr),
--   Animation.__load(id) (rl_world factories); J.rlw.get / get_child /
--   from_menu / ge_set_* (rl_widgets); BufferedImage.qh_of_source(fn, w, h)
--   (awt); QH.draw, the draw callback's graphics object during a pass.
-- rl_world calls NPCComposition.__forId(id) (ObjectComposition.__forId and
-- ItemComposition.__forId answer the same way).
--
-- RuneLite semantics are from runelite-api / runelite-client 1.13.6.

local J, C = J, C
local function api() return QH.api end

local function def(key, super, ifaces, body, clinit, kind)
  J.def(key, { kind = kind or "class", super = super or "java.lang.Object", ifaces = ifaces or {}, body = body, clinit = clinit })
end
local function iface(key, ifaces, body)
  def(key, nil, ifaces, body, nil, "interface")
end

local function need(name)
  local f = J[name]
  if f == nil then error("rl_client: J." .. name .. " is not provided (docs/QUEST_HELPER_PORT.md, rl_client hooks)", 3) end
  return f
end

local warned = {}
local function warn_once(what)
  if warned[what] then return end
  warned[what] = true
  J.log("quest-helper rl_client: " .. what)
end

local function color_rgb(c) return c:getRGB() & 0xFFFFFF end
local function color_alpha(c) return (c:getRGB() >> 24) & 0xFF end
local function new_color(r, g, b, a)
  if a == nil then return C["java.awt.Color"].__new__int_int_int(r, g, b) end
  return C["java.awt.Color"].__new__int_int_int_int(r, g, b, a)
end
local function color_hex(rgb) return string.format("%06x", rgb & 0xFFFFFF) end

-- A Lua sequence -> Java array.
local function jarr(t, n)
  local a = { n = n or #t }
  for i = 1, a.n do a[i] = t[i] end
  return a
end

-- ------------------------------------------------------------------ enum maps

local MENU_ACTION_BY_ID
local function menu_action(id)
  if MENU_ACTION_BY_ID == nil then
    MENU_ACTION_BY_ID = {}
    local vs = C["net.runelite.api.MenuAction"].__values
    for i = 1, vs.n do
      local v = vs[i]
      local k = v:getId()
      if MENU_ACTION_BY_ID[k] == nil then MENU_ACTION_BY_ID[k] = v end
    end
  end
  return MENU_ACTION_BY_ID[id] or C["net.runelite.api.MenuAction"].UNKNOWN
end

-- RuneLite's WorldType masks (WorldType.java; the generated enum carries no
-- getter for the private mask).
local WORLD_TYPE_MASK = {
  MEMBERS = 1, PVP = 1 << 2, BOUNTY = 1 << 5, PVP_ARENA = 1 << 6, SKILL_TOTAL = 1 << 7,
  QUEST_SPEEDRUNNING = 1 << 8, HIGH_RISK = 1 << 10, HARDCORE_MAIN = 0, LAST_MAN_STANDING = 1 << 14,
  BETA_WORLD = 1 << 16, LEGACY_ONLY = 1 << 22, EOC_ONLY = 1 << 23, NOSAVE_MODE = 1 << 25,
  TOURNAMENT_WORLD = 1 << 26, FRESH_START_WORLD = 1 << 27, DEADMAN = 1 << 29, SEASONAL = 1 << 30,
}

-- The client's screen (api.core.screen: TORIRS_SCREEN_* 0 boot, 10 title,
-- 20 connecting, 30 game) carries GameState's own numbers.
local SCREEN_NAME_STATE = { boot = 0, title = 10, connecting = 20, game = 30 }
local function game_state()
  local s = api().core.screen()
  if type(s) == "string" then s = SCREEN_NAME_STATE[s] or -1 end
  local GS = C["net.runelite.api.GameState"]
  if s == 30 then
    -- In game but with no scene yet is RuneLite's LOADING.
    local x = api().world.scene_origin()
    if x == nil then return GS.LOADING end
    return GS.LOGGED_IN
  end
  local vs = GS.__values
  for i = 1, vs.n do if vs[i]:getState() == s then return vs[i] end end
  return GS.UNKNOWN
end

-- Skill.ordinal() IS the client's stat index (rs_game_events.c: Attack 0 ..
-- Construction 22, Sailing 23); the names pin it.
local STAT_NAME = {
  [0] = "ATTACK", "DEFENCE", "STRENGTH", "HITPOINTS", "RANGED", "PRAYER", "MAGIC", "COOKING",
  "WOODCUTTING", "FLETCHING", "FISHING", "FIREMAKING", "CRAFTING", "SMITHING", "MINING", "HERBLORE",
  "AGILITY", "THIEVING", "SLAYER", "FARMING", "RUNECRAFT", "HUNTER", "CONSTRUCTION", "SAILING",
}
local function stat_index(skill)
  assert(skill)
  local i = skill:ordinal()
  assert(STAT_NAME[i] == skill:name(), "rl_client: Skill." .. skill:name() .. " has no client stat")
  return i
end
local function stat(skill) return api().game.skill(stat_index(skill)) end

-- ------------------------------------------------------------------ markup

-- Jagex's @tag@ colours, both ways: a native menu row's text carries them,
-- and a row this file adds is written in them.
local AT_TAG_RGB = {
  red = 0xff0000, gre = 0x00ff00, blu = 0x0000ff, yel = 0xffff00, cya = 0x00ffff, mag = 0xff00ff,
  whi = 0xffffff, bla = 0x000000, lre = 0xff9040, dre = 0x800000, dbl = 0x000080, or1 = 0xffb000,
  or2 = 0xff9000, or3 = 0xff7000, gr1 = 0xc0ff00, gr2 = 0x80ff00, gr3 = 0x40ff00,
}
local RGB_AT_TAG = {}
for k, v in pairs(AT_TAG_RGB) do RGB_AT_TAG[v] = k end

local function at_tags_to_col(s)
  return (s:gsub("@(%w%w%w)@", function(t)
    local rgb = AT_TAG_RGB[t]
    if rgb then return "<col=" .. color_hex(rgb) .. ">" end
    return nil
  end))
end

local function col_to_at_tags(s)
  s = s:gsub("<col=(%x+)>", function(h)
    local tag = RGB_AT_TAG[tonumber(h, 16) & 0xFFFFFF]
    return tag and ("@" .. tag .. "@") or ""
  end)
  s = s:gsub("</col>", "")
  return (s:gsub("<[^>]*>", ""))
end

-- ------------------------------------------------------------------ images

-- Images handed to awt: BufferedImage.qh_of_source(fn, w, h), fn() -> an
-- ImageRef, or nil while it is pending (docs/QUEST_HELPER_PORT.md, "Images").
-- The providers below answer (ref, state); awt reads the first value.
local function host_image(provider, w, h)
  return C["java.awt.image.BufferedImage"].qh_of_source(provider, w, h)
end

local ITEM_W, ITEM_H = 36, 32

-- A source answers its ref only once the client says it is ready.
local function item_provider(id, qty, style)
  local ref
  return function()
    if ref ~= nil then return ref, "ready" end
    local r, st = api().game.item_image(id, qty, style)
    if r ~= nil and st == "ready" then ref = r; return r, st end
    return nil, st
  end
end

-- ItemManager.getItemOutline: SpritePixels.toBufferedOutline -- the pixels
-- that are transparent in the icon and touch an opaque one (4-neighbour),
-- in the outline colour; everything else transparent.
local function outline_pixels(px, w, h, rgb)
  local out = {}
  local col = 0xFF000000 | rgb
  local function opaque(x, y)
    if x < 0 or y < 0 or x >= w or y >= h then return false end
    return ((px[y * w + x + 1] >> 24) & 0xFF) ~= 0
  end
  for y = 0, h - 1 do
    for x = 0, w - 1 do
      local i = y * w + x + 1
      if not opaque(x, y) and (opaque(x - 1, y) or opaque(x + 1, y) or opaque(x, y - 1) or opaque(x, y + 1)) then
        out[i] = col
      else
        out[i] = 0
      end
    end
  end
  return out
end

local function sprite_provider(id, frame)
  local ref
  return function()
    if ref ~= nil then return ref, "ready" end
    local f = api().cache.sprite_image
    if f == nil then
      warn_once("api.cache.sprite_image is not in this client; sprite " .. id .. " draws nothing")
      return nil, "missing"
    end
    local r, st = f(id, frame)
    if r ~= nil and st == "ready" then ref = r; return r, st end
    return nil, st
  end
end

-- ------------------------------------------------------------------ Item / ItemContainer

def("net.runelite.api.Item", nil, {}, function(cls)
  function cls.__init(self, id, quantity)
    self.id = id
    self.quantity = quantity
  end
  function cls.getId(self) return self.id end
  function cls.getQuantity(self) return self.quantity end
  function cls.equals(self, o)
    return type(o) == "table" and o.__key == "net.runelite.api.Item" and o.id == self.id and o.quantity == self.quantity
  end
  function cls.hashCode(self) return J.l2i((59 + self.id) * 59 + self.quantity) end
  function cls.toString(self) return "Item(id=" .. self.id .. ", quantity=" .. self.quantity .. ")" end
end)

def("net.runelite.api.ItemContainer", nil, {}, function(cls)
  function cls.__new_for(inv, items)
    local o = setmetatable({}, cls)
    o.inv = inv
    o.items = items
    return o
  end
  function cls.getId(self) return self.inv end
  function cls.getItems(self) return J.aclone(self.items) end
  function cls.size(self) return self.items.n end
  function cls.getItem(self, slot)
    if slot < 0 or slot >= self.items.n then return nil end
    local it = self.items[slot + 1]
    if it.id == -1 then return nil end
    return it
  end
  function cls.contains(self, id)
    for i = 1, self.items.n do if self.items[i].id == id then return true end end
    return false
  end
  function cls.count__int(self, id)
    local n = 0
    for i = 1, self.items.n do local it = self.items[i]; if it.id == id then n = n + it.quantity end end
    return n
  end
  function cls.count__(self)
    local n = 0
    for i = 1, self.items.n do if self.items[i].id ~= -1 then n = n + 1 end end
    return n
  end
  function cls.find(self, id)
    for i = 1, self.items.n do if self.items[i].id == id then return i - 1 end end
    return -1
  end
end)

-- One snapshot per container per frame; on_inventory_changed drops it.
local containers = {}
-- A container is RuneLite's id (InventoryID / gameval InventoryID: 93 INV,
-- 94 WORN, 95 BANK, 516 a shop, 659 group storage, 660 its inventory, ...
-- -- the id on_inventory_changed reports). The client's own three go
-- through inventory_size/inventory_slot by ToriRS_InventoryKind (a client
-- older than container_slot reads only those); every other id through
-- container_size/container_slot, which take the wire id itself. A container
-- the client has never been sent (size 0) reads as absent, as RuneLite's
-- getItemContainer answers null for it.
local INVENTORY_KIND = { [93] = 0, [94] = 1, [95] = 2 }
local function read_container(inv)
  local g = api().game
  local kind = INVENTORY_KIND[inv]
  local size_of, slot_of, key
  if kind ~= nil then
    size_of, slot_of, key = g.inventory_size, g.inventory_slot, kind
  elseif g.container_size ~= nil then
    size_of, slot_of, key = g.container_size, g.container_slot, inv
  else
    return nil
  end
  local size = size_of(key)
  if size == nil or size <= 0 then return nil end
  local Item = C["net.runelite.api.Item"]
  local items = { n = size }
  for slot = 0, size - 1 do
    local id, count = slot_of(key, slot)
    if id == nil or id < 0 then items[slot + 1] = Item.__new(-1, 0)
    else items[slot + 1] = Item.__new(id, count or 1) end
  end
  return C["net.runelite.api.ItemContainer"].__new_for(inv, items)
end
local function container(inv)
  local stamp = api().core.frame_ms()
  local c = containers[inv]
  if c ~= nil and c.stamp == stamp then return c.value end
  local v = read_container(inv)
  containers[inv] = { stamp = stamp, value = v }
  return v
end
-- ItemContainerChanged's container: the client raised the event because
-- this container was just written, so it exists; one with no slots (or one
-- this client cannot read) is RuneLite's empty container, never null --
-- RuneLite never posts the event without one.
local function changed_container(inv)
  local c = container(inv)
  if c ~= nil then return c end
  return C["net.runelite.api.ItemContainer"].__new_for(inv, { n = 0 })
end

-- ------------------------------------------------------------------ compositions

local item_composition -- below: ItemComposition.__forId reaches it

local function name_of(n)
  if n == nil then return "null" end
  return n
end

local function actions_of(t, count)
  local a = { n = count }
  if t then
    for i = 1, count do
      local s = t[i]
      if s ~= nil and s ~= "" and s ~= false then a[i] = s end
    end
  end
  return a
end

local function int_array(t)
  if t == nil then return nil end
  return jarr(t, t.n or #t)
end

-- Multiloc / multinpc resolution, as the client does it: the var's value
-- picks transforms[value], anything out of range the LAST entry (the default).
local function transform_id(d)
  local t = d.transforms
  if t == nil or #t == 0 then return nil end
  local value = -1
  local vb, vp = d.transform_varbit, d.transform_varp
  if vb ~= nil and vb ~= -1 then value = api().cache.varbit(vb)
  elseif vp ~= nil and vp ~= -1 then value = api().cache.varp(vp) end
  local id
  if value >= 0 and value < #t - 1 then id = t[value + 1] else id = t[#t] end
  if id == nil or id < 0 then return nil end
  return id
end

def("net.runelite.api.ParamHolder", nil, {}, function(cls)
  -- every holder here answers its own params; -1 is the absent param
  function cls.getIntValue(self, param) return -1 end
end, nil, "interface")

def("net.runelite.api.ItemComposition", nil, { "net.runelite.api.ParamHolder" }, function(cls)
  function cls.__new_for(id, info)
    local o = setmetatable({}, cls)
    o.id = id
    o.info = info
    return o
  end
  function cls.__forId(id) return item_composition(id) end
  function cls.getId(self) return self.id end
  function cls.getName(self) return name_of(self.info and self.info.name) end
  function cls.getMembersName(self) return self:getName() end
  function cls.getPrice(self) return self.info and self.info.cost or 0 end
  function cls.getHaPrice(self) return J.d2i(self:getPrice() * 0.6) end
  function cls.isStackable(self) return self.info ~= nil and self.info.stackable == true end
  function cls.isMembers(self) return self.info ~= nil and self.info.members == true end
  function cls.getLinkedNoteId(self)
    local l = self.info and self.info.cert_link
    if l == nil then return -1 end
    return l
  end
  function cls.getNote(self)
    local t = self.info and self.info.cert_template
    if t == nil then return -1 end
    return t
  end
  function cls.getPlaceholderId(self)
    local l = self.info and self.info.placeholder_link
    if l == nil then return -1 end
    return l
  end
  function cls.getPlaceholderTemplateId(self)
    local t = self.info and self.info.placeholder_template
    if t == nil then return -1 end
    return t
  end
  -- item_info carries no tradeable flag yet; RuneLite's default is true for
  -- anything the GE search would offer.
  function cls.isTradeable(self)
    local t = self.info and self.info.tradeable
    if t == nil then return true end
    return t
  end
  function cls.getInventoryActions(self) return actions_of(self.info and self.info.actions, 5) end
  function cls.getIntValue(self, param)
    local p = self.info and self.info.params
    local v = p and p[param]
    if v == nil then return -1 end
    return v
  end
  function cls.getStringValue(self, param)
    local p = self.info and self.info.params
    local v = p and p[param]
    if v == nil then return "" end
    return v
  end
end)

def("net.runelite.api.NPCComposition", nil, { "net.runelite.api.ParamHolder" }, function(cls)
  function cls.__new_for(d)
    local o = setmetatable({}, cls)
    o.d = d
    return o
  end
  function cls.getId(self) return self.d.id end
  function cls.getName(self) return name_of(self.d.name) end
  function cls.getActions(self) return actions_of(self.d.actions, 5) end
  function cls.getSize(self) return self.d.size or 1 end
  function cls.getCombatLevel(self) return self.d.combat_level or 0 end
  function cls.getConfigs(self) return int_array(self.d.transforms) end
  function cls.__forId(id) return C["net.runelite.api.Client"].npc_composition(id) end
  function cls.getTransformedComposition(self)
    local id = transform_id(self.d)
    if id == nil then return nil end
    return C["net.runelite.api.Client"].npc_composition(id)
  end
  function cls.getModels(self) return int_array(self.d.models) end
  function cls.getColorToReplace(self) return int_array(self.d.recolor_from) end
  function cls.getColorToReplaceWith(self) return int_array(self.d.recolor_to) end
  function cls.isInteractible(self) return self.d.interactive ~= false end
  function cls.isMinimapVisible(self) return self.d.minimap_visible ~= false end
  function cls.isFollower(self) return self.d.follower == true end
  function cls.getIntValue(self, param)
    local v = self.d.params and self.d.params[param]
    if v == nil then return -1 end
    return v
  end
end)

def("net.runelite.api.ObjectComposition", nil, { "net.runelite.api.ParamHolder" }, function(cls)
  function cls.__new_for(d)
    local o = setmetatable({}, cls)
    o.d = d
    return o
  end
  function cls.getId(self) return self.d.id end
  function cls.getName(self) return name_of(self.d.name) end
  function cls.getActions(self) return actions_of(self.d.actions, 5) end
  function cls.getSizeX(self) return self.d.size_x or 1 end
  function cls.getSizeY(self) return self.d.size_z or 1 end
  function cls.getMapSceneId(self) return self.d.map_scene or -1 end
  function cls.getMapIconId(self) return self.d.map_icon or -1 end
  function cls.getVarbitId(self) return self.d.transform_varbit or -1 end
  function cls.getVarPlayerId(self) return self.d.transform_varp or -1 end
  function cls.getImpostorIds(self)
    local t = self.d.transforms
    if t == nil or #t == 0 then return nil end
    return int_array(t)
  end
  function cls.__forId(id) return C["net.runelite.api.Client"].object_composition(id) end
  function cls.getImpostor(self)
    local id = transform_id(self.d)
    if id == nil then return nil end
    return C["net.runelite.api.Client"].object_composition(id)
  end
  function cls.getIntValue(self, param)
    local v = self.d.params and self.d.params[param]
    if v == nil then return -1 end
    return v
  end
end)

def("net.runelite.api.EnumComposition", nil, {}, function(cls)
  function cls.__new_for(id, e)
    local o = setmetatable({}, cls)
    o.id = id
    o.e = e
    local by = {}
    local keys = e.keys or {}
    local vals = e.values or {}
    for i = 1, #keys do by[keys[i]] = vals[i] end
    o.by = by
    return o
  end
  function cls.getId(self) return self.id end
  function cls.size(self) return #(self.e.keys or {}) end
  function cls.getKeys(self) return int_array(self.e.keys or {}) end
  function cls.getIntVals(self) return int_array(self.e.values or {}) end
  function cls.getStringVals(self) return int_array(self.e.values or {}) end
  function cls.getIntValue(self, key)
    local v = self.by[key]
    if v == nil then
      local d = self.e.default
      if d == nil then return -1 end
      return d
    end
    return v
  end
  function cls.getStringValue(self, key)
    local v = self.by[key]
    if v == nil then
      local d = self.e.default
      if type(d) ~= "string" then return "null" end
      return d
    end
    return v
  end
end)

local item_comps, npc_comps, obj_comps = {}, {}, {}
local npc_state, obj_state = {}, {}   -- id -> the cache's last answer state

function item_composition(id)
  local c = item_comps[id]
  if c ~= nil then return c end
  local info = api().game.item_info(id)
  c = C["net.runelite.api.ItemComposition"].__new_for(id, info)
  -- an obj the client has not read yet answers again next time
  if info ~= nil then item_comps[id] = c end
  return c
end

-- ------------------------------------------------------------------ Menu / MenuEntry

local menu = {
  entries = { n = 0 }, -- MenuEntry[] as getMenuEntries answers, bottom row first
  building = false,
  created = {},       -- entries createMenuEntry made during this build
  dirty = false,      -- setMenuEntries / removeMenuEntry ran during this build
  build = 0,          -- build serial: a native entry belongs to the build that made it
  serial = 0,
  open_map = {},      -- tag -> entry, the last right-click build
  hover_map = {},     -- tag -> entry, the last hover build
  native_open = {},   -- row key -> native entry carrying an onClick, the last right-click build
  native_hover = {},  -- the same, the last hover build
}

-- A native row's identity across on_menu_build and on_menu_select (its text
-- may be the plugin's by then): RuneLite's MenuEntry numbers.
local function row_key(row)
  return tostring(row.type or row.action or -1) .. ":" .. tostring(row.identifier or -1) .. ":" ..
    tostring(row.param0 or row.slot or -1) .. ":" .. tostring(row.param1 or row.component_id or -1)
end

local function split_row_text(text)
  -- "Talk-to @yel@Hans" -> "Talk-to", "<col=ffff00>Hans"
  local at = text:find(" @", 1, true)
  if at == nil then return text, "" end
  return text:sub(1, at - 1), at_tags_to_col(text:sub(at + 1))
end

def("net.runelite.api.MenuEntry", nil, {}, function(cls)
  function cls.__new_blank()
    local o = setmetatable({}, cls)
    o.option, o.target = "", ""
    o.identifier, o.param0, o.param1, o.item_id = 0, 0, 0, -1
    o.type = C["net.runelite.api.MenuAction"].RUNELITE
    o.deprioritized, o.force_left = false, false
    o.world_view = -1
    return o
  end
  function cls.__new_row(row)
    local o = cls.__new_blank()
    o.row = row
    if row.option ~= nil then o.option, o.target = row.option, row.target or ""
    else o.option, o.target = split_row_text(row.text or "") end
    o.type = menu_action(row.type or row.action or -1)
    if row.identifier ~= nil then o.identifier = row.identifier
    elseif row.npc_slot ~= nil and row.npc_slot >= 0 then o.identifier = row.npc_slot
    elseif row.player_pid ~= nil and row.player_pid >= 0 then o.identifier = row.player_pid
    elseif row.target_id ~= nil and row.target_id >= 0 then o.identifier = row.target_id end
    o.param0 = row.param0 or row.slot or -1
    o.param1 = row.param1 or row.component_id or -1
    if row.component_id ~= nil and row.component_id >= 0 and row.target_id ~= nil and row.target_id >= 0 then
      o.item_id = row.target_id
    end
    return o
  end
  function cls.getOption(self) return self.option end
  function cls.setOption(self, s) self.option = s; return self end
  function cls.getTarget(self) return self.target end
  function cls.setTarget(self, s) self.target = s; return self end
  function cls.getIdentifier(self) return self.identifier end
  function cls.setIdentifier(self, v) self.identifier = v; return self end
  function cls.getType(self) return self.type end
  function cls.setType(self, t) assert(t); self.type = t; return self end
  function cls.getParam0(self) return self.param0 end
  function cls.setParam0(self, v) self.param0 = v; return self end
  function cls.getParam1(self) return self.param1 end
  function cls.setParam1(self, v) self.param1 = v; return self end
  function cls.isForceLeftClick(self) return self.force_left end
  function cls.setForceLeftClick(self, b) self.force_left = b; return self end
  function cls.getWorldViewId(self) return self.world_view end
  function cls.setWorldViewId(self, v) self.world_view = v; return self end
  -- A native row cannot be reordered from a plugin; the flag is kept so the
  -- plugin reads back what it wrote.
  function cls.isDeprioritized(self) return self.deprioritized end
  function cls.setDeprioritized(self, b) self.deprioritized = b; return self end
  function cls.onClick__Consumer(self, c) self.consumer = c; return self end
  function cls.onClick__(self) return self.consumer end
  function cls.getItemId(self) return self.item_id end
  function cls.setItemId(self, v) self.item_id = v; return self end
  function cls.isItemOp(self) return self.item_id ~= -1 and self.row ~= nil and self.row.slot ~= nil and self.row.slot >= 0 end
  function cls.getItemOp(self) return -1 end
  -- A widget row: param1 the component, param0 its dynamic child.
  function cls.getWidget(self)
    if self.param1 == nil or self.param1 < 0 then return nil end
    return need("rlw").from_menu(self.param1, self.param0)
  end
  function cls.getNpc(self)
    local row = self.row
    if row == nil or row.npc_slot == nil or row.npc_slot < 0 then return nil end
    return C["net.runelite.api.NPC"].__bySlot(row.npc_slot)
  end
  function cls.getPlayer(self)
    local row = self.row
    if row == nil or row.player_pid == nil or row.player_pid < 0 then return nil end
    return C["net.runelite.api.WorldView"].__top():players():byIndex(row.player_pid)
  end
  function cls.getActor(self) return self:getNpc() or self:getPlayer() end
  function cls.getSubMenu(self) return nil end
  function cls.toString(self)
    return "MenuEntry(option=" .. J.sval(self.option) .. ", target=" .. J.sval(self.target) .. ", type=" .. self.type:name() .. ")"
  end
end)

def("net.runelite.api.Menu", nil, {}, function(cls)
  function cls.getMenuEntries(self) return J.aclone(menu.entries) end
  -- The menu of the build in progress, bottom row first: menu_end hands the
  -- final list to api.menu.set_order (rows left out are removed, the order
  -- stands as given). Outside a build there is no menu to edit.
  function cls.setMenuEntries(self, a)
    assert(a)
    if not menu.building then
      warn_once("Menu.setMenuEntries outside on_menu_build: the menu is not changed")
      return
    end
    menu.entries = J.aclone(a)
    menu.dirty = true
  end
  function cls.removeMenuEntry(self, e)
    assert(e)
    if not menu.building then
      warn_once("Menu.removeMenuEntry outside on_menu_build: the menu is not changed")
      return
    end
    local es = menu.entries
    for i = 1, es.n do
      if rawequal(es[i], e) then
        table.remove(es, i)
        es.n = es.n - 1
        menu.dirty = true
        return
      end
    end
  end
  -- RuneLite inserts at idx (negative counts from the end, -1 appends = the
  -- top row); menu_end places the row there through api.menu.set_order.
  function cls.createMenuEntry(self, idx)
    local e = C["net.runelite.api.MenuEntry"].__new_blank()
    local es = menu.entries
    local pos
    if idx < 0 then pos = es.n + idx + 2 else pos = idx + 1 end
    if pos < 1 then pos = 1 end
    if pos > es.n + 1 then pos = es.n + 1 end
    table.insert(es, pos, e)
    es.n = es.n + 1
    if menu.building then
      menu.created[#menu.created + 1] = e
    else
      warn_once("Menu.createMenuEntry outside on_menu_build: the row is not shown")
    end
    return e
  end
  -- The open menu's box (api.menu.shown); 0 while no menu is up.
  local function box(field)
    local b = api().menu.shown()
    if b == nil then return 0 end
    return b[field]
  end
  function cls.getMenuX(self) return box("x") end
  function cls.getMenuY(self) return box("y") end
  function cls.getMenuWidth(self) return box("width") end
  function cls.getMenuHeight(self) return box("height") end
end)

local the_menu

-- varbit id -> the value last seen (the entry's VarbitChanged fan-out owns it)
local watched_varbits = {}
local watched_varcs = {}

local function menu_begin(ev)
  assert(ev)
  assert(ev.rows)
  local Entry = C["net.runelite.api.MenuEntry"]
  local es = { n = #ev.rows }
  menu.build = menu.build + 1
  for i, row in ipairs(ev.rows) do
    local e = Entry.__new_row(row)
    -- which payload row it is, in this build, and what it said
    e.row_index, e.row_build = i, menu.build
    e.orig_option, e.orig_target = e.option, e.target
    es[i] = e
  end
  menu.entries = es
  menu.created = {}
  menu.dirty = false
  menu.building = true
  menu.hover = ev.hover_pass == true
  return J.aclone(es)
end

local function row_text(option, target)
  local text = option or ""
  local t = col_to_at_tags(target or "")
  if t ~= "" then text = text .. " " .. t end
  return text
end

-- RuneLite's menu, as the subscribers left it: created rows added, then --
-- when anything moved (a created row, setMenuEntries / removeMenuEntry, a
-- deprioritized row) -- the whole order through api.menu.set_order, and a
-- native row whose option or target was rewritten through api.menu.set_text.
local function menu_end()
  assert(menu.building)
  menu.building = false
  local map = {}
  local natives = {}
  local m = api().menu
  for _, e in ipairs(menu.created) do
    menu.serial = menu.serial % 0x7FFFFFFF + 1
    if m.add(row_text(e.option, e.target), menu.serial) then
      map[menu.serial] = e
      e.tag = menu.serial
    else
      e.tag = nil
    end
  end
  local es = menu.entries
  local reorder = menu.dirty or #menu.created > 0
  for i = 1, es.n do
    local e = es[i]
    if e.deprioritized then reorder = true end
    if e.row_build == menu.build then
      if e.consumer ~= nil then natives[row_key(e.row)] = e end
      if (e.option ~= e.orig_option or e.target ~= e.orig_target) and m.set_text then
        local ok, why = m.set_text(e.row_index, row_text(e.option, e.target))
        if not ok then warn_once("MenuEntry.setOption/setTarget on a native row refused: " .. tostring(why)) end
      end
    end
  end
  if reorder and m.set_order then
    local refs = {}
    for i = 1, es.n do
      local e = es[i]
      if e.row_build == menu.build then
        refs[#refs + 1] = { row = e.row_index, deprioritized = e.deprioritized or nil }
      elseif e.tag ~= nil and map[e.tag] == e then
        refs[#refs + 1] = { tag = e.tag, deprioritized = e.deprioritized or nil }
      end
    end
    local ok, why = m.set_order(refs)
    if not ok then warn_once("Menu order refused: " .. tostring(why)) end
  end
  if menu.hover then menu.hover_map, menu.native_hover = map, natives
  else menu.open_map, menu.native_open = map, natives end
  menu.created = {}
  return map
end

-- The entry a click chose: a created row by its tag, a native row whose entry
-- a subscriber gave an onClick (or rewrote) by its numbers, else a fresh
-- entry over the row.
local function menu_select(ev)
  assert(ev)
  assert(ev.row)
  if ev.owned then
    local e = menu.open_map[ev.tag] or menu.hover_map[ev.tag]
    if e ~= nil then return e end
  else
    local key = row_key(ev.row)
    local e = menu.native_open[key] or menu.native_hover[key]
    if e ~= nil then return e end
  end
  return C["net.runelite.api.MenuEntry"].__new_row(ev.row)
end

local function menu_click(entry)
  assert(entry)
  local c = entry.consumer
  if c ~= nil then c:accept(entry) end
end

-- ------------------------------------------------------------------ client scripts

local script = {
  ref = nil,                       -- api.scripts ScriptRef while a script callback runs
  ints = { n = 0 }, strs = { n = 0 }, -- the last runScript's results
}

-- The int stack of a running script, as RuneLite's int[]: Lua index k is
-- Java index k-1; api.scripts index 0 is the top slot (Java size-1).
local function stack_proxy(ref, get, set, count_index)
  local n = select(count_index, api().scripts.counts(ref))
  local p = { n = n }
  return setmetatable(p, {
    __index = function(_, k)
      if math.type(k) ~= "integer" or k < 1 or k > n then return nil end
      return get(ref, n - k)
    end,
    __newindex = function(_, k, v)
      assert(math.type(k) == "integer")
      assert(k >= 1 and k <= n)
      set(ref, n - k, v)
    end,
  })
end

-- ------------------------------------------------------------------ WorldMap

-- The world map's view, read off the draw object: worldmap_bounds() and,
-- when it does not say centre and zoom itself, two projections of the
-- player's tile.
local function worldmap_view()
  local d = QH.draw
  if d == nil then return nil end
  local b = d.worldmap_bounds()
  if b == nil then return nil end
  if b.center_x ~= nil and b.zoom ~= nil then return b.center_x, b.center_z, b.zoom end
  local p = api().world.local_player()
  if p == nil then return nil end
  local x0, y0 = d.worldmap_project(p.true_x, p.true_z, p.level)
  local x1 = d.worldmap_project(p.true_x + 1, p.true_z, p.level)
  if x0 == nil or x1 == nil or x1 == x0 then return nil end
  local zoom = x1 - x0
  local cx = b.x + b.width / 2
  local cy = b.y + b.height / 2
  return J.d2i(p.true_x + (cx - x0) / zoom), J.d2i(p.true_z - (cy - y0) / zoom), zoom
end

def("net.runelite.api.worldmap.WorldMapData", nil, {}, function(cls)
  -- The surface map: everything below the underground band (dungeons and
  -- instances sit at y >= 4600 + 6400 offsets).
  function cls.surfaceContainsPosition(self, x, y) return x >= 1024 and x < 4224 and y >= 2496 and y < 4600 end
end)

def("net.runelite.api.worldmap.WorldMap", nil, {}, function(cls)
  function cls.getWorldMapData(self)
    if self.data == nil then self.data = setmetatable({}, C["net.runelite.api.worldmap.WorldMapData"]) end
    return self.data
  end
  function cls.getWorldMapPosition(self)
    local x, z = worldmap_view()
    if x ~= nil then self.last_x, self.last_z = x, z end
    if self.last_x == nil then
      local p = api().world.local_player()
      self.last_x, self.last_z = p and p.true_x or 0, p and p.true_z or 0
    end
    return C["net.runelite.api.Point"].__new(self.last_x, self.last_z)
  end
  function cls.getWorldMapZoom(self)
    local _, _, zoom = worldmap_view()
    if zoom ~= nil then self.last_zoom = zoom end
    return (self.last_zoom or 4) + 0.0
  end
  -- The open map glides there (the map's jumptosourcecoord); nothing while
  -- it is closed, as RuneLite's does nothing without a map.
  function cls.setWorldMapPositionTarget(self, wp)
    assert(wp)
    api().client.worldmap_jump(wp:getX(), wp:getY(), wp:getPlane())
  end
end)

-- ------------------------------------------------------------------ camera off the minimap

-- getCameraYawTarget and getMinimapZoom: a tile four east of the player
-- lands at (cos, sin) * 4 * zoom on the minimap (Perspective.localToMinimap),
-- so two projections give both. Without a draw object: yaw 0, zoom 4.
local cam = { stamp = -1, yaw = 0, zoom = 4.0 }
local function read_minimap_camera()
  local stamp = api().core.frame_ms()
  if cam.stamp == stamp then return end
  local d = QH.draw
  if d == nil then return end
  local p = api().world.local_player()
  if p == nil or p.fine_x == nil then return end
  local x0, y0 = d.minimap_project(p.fine_x, p.fine_z)
  local x1, y1 = d.minimap_project(p.fine_x + 512, p.fine_z)
  if x0 == nil or x1 == nil then return end
  cam.stamp = stamp
  local dx, dy = x1 - x0, y1 - y0
  local len = math.sqrt(dx * dx + dy * dy)
  if len == 0 then return end
  cam.zoom = len / 4
  local a = math.atan(dy, dx)
  cam.yaw = J.d2i(a * 8192 / math.pi + 0.5) & 0x3FFF
end

-- ------------------------------------------------------------------ Client

local function display(name)
  local c = api().client
  local v = c.display_get(c.display[name])
  return v
end

def("net.runelite.api.GameEngine", nil, {}, function(cls)
  function cls.isClientThread(self) return true end
end, nil, "interface")

def("net.runelite.api.Client", nil, { "net.runelite.api.GameEngine" }, function(cls)
  -- the injector builds it with either spelling; every Client shares the state above
  function cls.__init(self) end
  function cls.__init__(self) end

  function cls.isClientThread(self) return true end

  -- vars
  -- Every varbit the plugin reads is remembered: on_var_changed reports
  -- varps only, so the entry re-reads these to post VarbitChanged with the
  -- right varbitId (J.rl_client.watched_varbits).
  function cls.getVarbitValue__int(self, id)
    local v = api().cache.varbit(id)
    if watched_varbits[id] == nil then watched_varbits[id] = v end
    return v
  end
  function cls.getVarpValue(self, id) return api().cache.varp(id) end
  -- every varc read is watched: the entry posts VarClientIntChanged for it
  function cls.getVarcIntValue(self, id)
    local v = api().cache.varc_int(id)
    if watched_varcs[id] == nil then watched_varcs[id] = v end
    return v
  end
  function cls.getVarcStrValue(self, id) return api().cache.varc_str(id) end
  function cls.setVarcIntValue(self, id, v) api().cache.set_varc_int(id, v) end
  function cls.setVarcStrValue(self, id, s) api().cache.set_varc_str(id, s) end
  -- A CLIENT-SIDE varbit write (the bank's "current tab" reset, the
  -- player-made quests' state): the client's own copy and its transmit
  -- listeners, never a packet; the server's next VARP replaces it.
  local function set_varbit(id, v)
    local ok, why = api().cache.set_varbit(id, v)
    if not ok then warn_once("Client.setVarbit(" .. id .. ") refused: " .. tostring(why)) end
  end
  function cls.setVarbit(self, id, v) set_varbit(id, v) end
  -- setVarbitValue(varps, varbit, value): RuneLite writes the array it is
  -- handed, which is always getVarps() -- the client's own.
  function cls.setVarbitValue(self, varps, id, v) set_varbit(id, v) end
  function cls.getVarps(self) return {} end

  -- skills
  function cls.getRealSkillLevel(self, skill)
    local s = stat(skill)
    if s == nil then return 0 end
    return s.base_level
  end
  function cls.getBoostedSkillLevel(self, skill)
    local s = stat(skill)
    if s == nil then return 0 end
    return s.current_level
  end
  function cls.getSkillExperience(self, skill)
    local s = stat(skill)
    if s == nil then return 0 end
    return s.xp
  end
  function cls.getTotalLevel(self)
    local t = 0
    local vs = C["net.runelite.api.Skill"].__values
    for i = 1, vs.n do t = t + self:getRealSkillLevel(vs[i]) end
    return t
  end

  -- game
  function cls.getGameState(self) return game_state() end
  function cls.getTickCount(self) return api().game.tick() end
  function cls.getWeight(self) return api().game.weight() end
  function cls.getEnergy(self) return api().game.run_energy() * 100 end
  function cls.getPlane(self) return C["net.runelite.api.WorldView"].__top():getPlane() end
  function cls.isInInstancedRegion(self)
    local p = api().world.local_player()
    if p == nil then return false end
    return api().world.instance_zone(p.level, p.true_x // 8, p.true_z // 8) ~= nil
  end
  function cls.getWorldType(self)
    local mask = api().game.world_types()
    local set = C["java.util.EnumSet"].noneOf(J.classof("net.runelite.api.WorldType"))
    local vs = C["net.runelite.api.WorldType"].__values
    for i = 1, vs.n do
      local m = WORLD_TYPE_MASK[vs[i]:name()]
      if m ~= nil and m ~= 0 and (mask & m) ~= 0 then set:add(vs[i]) end
    end
    return set
  end

  -- item containers (InventoryID constants are inlined as the client's inv ids)
  function cls.getItemContainer__int(self, inv) return container(inv) end

  -- definitions
  function cls.getItemDefinition(self, id) return item_composition(id) end
  -- RuneLite's definition getters never answer "not yet": a record the cache
  -- is still loading raises J.PendingMT (a 'cache:' key), so a ClientThread
  -- task or a one-off dispatch runs again once it is in, instead of reading
  -- null as "no such npc" (rl_world's npc_composition keeps the nil answer).
  local function pending_record(kind, id)
    error(setmetatable({ pending = "cache:" .. kind .. ":" .. tostring(id) }, J.PendingMT), 0)
  end
  function cls.getNpcDefinition(self, id)
    local c = cls.npc_composition(id)
    if c == nil and npc_state[id] == "pending" then pending_record("npc", id) end
    return c
  end
  function cls.getObjectDefinition(self, id)
    local c = cls.object_composition(id)
    if c == nil and obj_state[id] == "pending" then pending_record("loc", id) end
    return c
  end
  function cls.npc_composition(id)
    local c = npc_comps[id]
    if c ~= nil then return c end
    local d, st = api().cache.npc_def(id)
    npc_state[id] = st
    if d == nil then return nil end
    c = C["net.runelite.api.NPCComposition"].__new_for(d)
    npc_comps[id] = c
    return c
  end
  function cls.object_composition(id)
    local c = obj_comps[id]
    if c ~= nil then return c end
    local d, st = api().cache.loc_def(id)
    obj_state[id] = st
    if d == nil then return nil end
    c = C["net.runelite.api.ObjectComposition"].__new_for(d)
    obj_comps[id] = c
    return c
  end
  function cls.getEnum(self, id)
    -- enum_def answers (record, 'ready') | (nil, 'pending'|'missing'):
    -- RuneLite's getEnum has no "not yet", so a record still loading raises
    -- pending (above) and the caller runs again once it is in.
    local e, st = api().cache.enum_def(id)
    if e == nil and st == "pending" then pending_record("enum", id) end
    if e == nil then return nil end
    return C["net.runelite.api.EnumComposition"].__new_for(id, e)
  end

  -- widgets
  function cls.getWidget__int(self, id) return need("rlw").get(id) end
  function cls.getWidget__int_int(self, group, child) return need("rlw").get_child(group, child) end

  -- client scripts: runScript(id, args...) then getIntStack()[0]
  function cls.runScript(self, args)
    assert(args)
    assert(args.n >= 1)
    local id = args[1]
    assert(math.type(id) == "integer")
    local ints, strs = api().scripts.run(id, table.unpack(args, 2, args.n))
    if ints == nil and strs == "pending" then
      -- RuneLite's runScript never answers "not yet": a script (or a record
      -- it reaches) still loading is a pending record, so the ClientThread
      -- task or dispatch that asked runs again once it is in (the load is
      -- queued already).
      error(setmetatable({ pending = "cache:script:" .. tostring(id) }, J.PendingMT), 0)
    end
    if ints == nil then
      J.throw(C["java.lang.IllegalStateException"].__new__String("runScript(" .. id .. ") refused: " .. tostring(strs)))
    end
    script.ints = jarr(ints, #ints)
    script.strs = jarr(strs or {}, strs and #strs or 0)
    -- the script may have rebuilt widgets this frame (rl_widgets caches reads per frame)
    local rlw = J.rlw
    if rlw and rlw.invalidate then rlw.invalidate() end
  end
  function cls.getIntStack(self)
    local ref = script.ref
    if ref ~= nil then
      local s = api().scripts
      return stack_proxy(ref, s.get_int, s.set_int, 1)
    end
    return J.aclone(script.ints)
  end
  function cls.getIntStackSize(self)
    if script.ref ~= nil then return (api().scripts.counts(script.ref)) end
    return script.ints.n
  end
  function cls.getStringStack(self)
    local ref = script.ref
    if ref ~= nil then
      local s = api().scripts
      return stack_proxy(ref, s.get_string, s.set_string, 2)
    end
    return J.aclone(script.strs)
  end
  function cls.getStringStackSize(self)
    if script.ref ~= nil then return (select(2, api().scripts.counts(script.ref))) end
    return script.strs.n
  end

  -- menu
  function cls.getMenu(self)
    if the_menu == nil then the_menu = setmetatable({}, C["net.runelite.api.Menu"]) end
    return the_menu
  end
  function cls.getMenuEntries(self) return self:getMenu():getMenuEntries() end
  function cls.isMenuOpen(self) return api().menu.shown() ~= nil end
  -- Only a component operation can be invoked: CC_OP with identifier = op
  -- and param1 = the component (QuestBankTab's potion store button).
  function cls.menuAction(self, p0, p1, action, id, item_id, option, target)
    assert(action)
    local MA = C["net.runelite.api.MenuAction"]
    if action == MA.CC_OP or action == MA.CC_OP_LOW_PRIORITY then
      return api().cache.invoke(p1, id)
    end
    -- A plugin's own row (createMenuEntry's RUNELITE kinds) has no client
    -- action behind it: RuneLite's menuAction of one runs nothing either.
    if action == MA.RUNELITE or action == MA.RUNELITE_HIGH_PRIORITY or action == MA.RUNELITE_LOW_PRIORITY then
      return false
    end
    warn_once("Client.menuAction(" .. action:name() .. "): only CC_OP can be invoked from a plugin")
    return false
  end

  -- hint arrows
  function cls.getHintArrowNpc(self)
    local h = api().game.hint_arrow()
    if h == nil or h.npc_slot == nil or h.npc_slot < 0 then return nil end
    return C["net.runelite.api.NPC"].__bySlot(h.npc_slot)
  end
  function cls.hasHintArrow(self) return api().game.hint_arrow() ~= nil end
  function cls.setHintArrow__NPC(self, npc)
    assert(npc)
    api().game.hint_arrow_npc(npc:getIndex())
  end
  function cls.setHintArrow__WorldPoint(self, wp)
    assert(wp)
    api().game.hint_arrow_tile(wp:getX(), wp:getY(), wp:getPlane())
  end
  function cls.clearHintArrow(self) api().game.hint_arrow_clear() end

  -- chat and sound
  function cls.addChatMessage__ChatMessageType_String_String_String(self, type, name, message, sender)
    assert(type)
    assert(message)
    api().game.chat_add(type:getType(), name or "", message)
    return nil
  end
  function cls.addChatMessage__ChatMessageType_String_String_String_boolean(self, type, name, message, sender, post)
    return self:addChatMessage__ChatMessageType_String_String_String(type, name, message, sender)
  end
  function cls.playSoundEffect__int(self, id) api().game.sound(id) end

  -- canvas and input
  function cls.getCanvasWidth(self) return display("layout_width") or 765 end
  function cls.getCanvasHeight(self) return display("layout_height") or 503 end
  function cls.isResized(self) return (display("window_fixed") or 0) == 0 end
  function cls.getViewportWidth(self)
    if not self:isResized() then return 512 end
    return self:getCanvasWidth()
  end
  function cls.getViewportHeight(self)
    if not self:isResized() then return 334 end
    return self:getCanvasHeight()
  end
  function cls.getViewportXOffset(self) if self:isResized() then return 0 end return 4 end
  function cls.getViewportYOffset(self) if self:isResized() then return 0 end return 4 end
  function cls.getMouseCanvasPosition(self)
    local x, y = api().input.pointer()
    if x == nil then return C["net.runelite.api.Point"].__new(-1, -1) end
    return C["net.runelite.api.Point"].__new(x, y)
  end

  -- camera (no plugin API reads it: the minimap answers yaw and zoom, the
  -- player's fine position stands for the eye's)
  function cls.getCameraYawTarget(self) read_minimap_camera(); return cam.yaw end
  function cls.getCameraYaw(self) read_minimap_camera(); return cam.yaw end
  function cls.getMinimapZoom(self) read_minimap_camera(); return cam.zoom + 0.0 end
  function cls.getCameraX(self)
    local p = api().world.local_player()
    if p == nil or p.fine_x == nil then return 0 end
    return p.fine_x - (select(1, api().world.scene_origin()) or 0) * 128
  end
  function cls.getCameraY(self)
    local p = api().world.local_player()
    if p == nil or p.fine_z == nil then return 0 end
    return p.fine_z - (select(2, api().world.scene_origin()) or 0) * 128
  end

  -- the world (rl_world's factories)
  function cls.getTopLevelWorldView(self) return C["net.runelite.api.WorldView"].__top() end
  function cls.getWorldView(self, id) return C["net.runelite.api.WorldView"].__byId(id) end
  function cls.getLocalPlayer(self) return C["net.runelite.api.Player"].__local() end
  function cls.getScene(self) return C["net.runelite.api.WorldView"].__top():getScene() end
  function cls.createRuneLiteObject(self) return C["net.runelite.api.RuneLiteObject"].__new(self) end
  function cls.loadModelData(self, id) return C["net.runelite.api.ModelData"].__load(id) end
  function cls.mergeModels__ModelDataArr(self, models)
    assert(models)
    return C["net.runelite.api.ModelData"].__merge(models)
  end
  function cls.loadAnimation(self, id) return C["net.runelite.api.Animation"].__load(id) end
  function cls.getWorldMap(self)
    if self.world_map == nil then self.world_map = setmetatable({}, C["net.runelite.api.worldmap.WorldMap"]) end
    return self.world_map
  end

  -- The Grand Exchange search results a GrandExchangeSearched subscriber
  -- sets: rl_widgets keeps them for the GE search script callback.
  function cls.setGeSearchResultCount(self, n) need("rlw").ge_set_count(n) end
  function cls.setGeSearchResultIds(self, ids) need("rlw").ge_set_ids(ids) end
  function cls.setGeSearchResultIndex(self, i) need("rlw").ge_set_index(i) end
end, function(cls)
  -- One Client: the injector's __new and J.rl_client.client() are the same object.
  local make = cls.__new
  local one
  local function the() if one == nil then one = make() end return one end
  cls.__new = the
  cls.__new__ = the
end)

local function client() return C["net.runelite.api.Client"].__new() end

-- ------------------------------------------------------------------ managers

def("net.runelite.client.game.ItemManager", nil, {}, function(cls)
  function cls.__init(self) self.images = {} end
  function cls.__init__(self) self.images = {} end
  function cls.getItemComposition(self, id) return item_composition(id) end
  function cls.getImage__int(self, id) return self:getImage__int_int_boolean(id, 1, false) end
  function cls.getImage__int_int_boolean(self, id, qty, stackable)
    local key = id .. ":" .. qty .. ":" .. tostring(stackable)
    local img = self.images[key]
    if img == nil then
      img = host_image(item_provider(id, qty, 0), ITEM_W, ITEM_H)
      self.images[key] = img
    end
    return img
  end
  function cls.getItemOutline(self, id, qty, color)
    assert(color)
    local rgb = color_rgb(color)
    local key = "o" .. id .. ":" .. qty .. ":" .. rgb
    local img = self.images[key]
    if img == nil then
      local icon = self:getImage__int_int_boolean(id, qty, false)
      img = C["java.awt.image.BufferedImage"].qh_derived(ITEM_W, ITEM_H, function()
        local px = icon:qh_pixels()
        if px == nil then return nil end
        return outline_pixels(px, icon:getWidth(), icon:getHeight(), rgb)
      end)
      self.images[key] = img
    end
    return img
  end
  function cls.getItemPrice__int(self, id) return item_composition(id):getPrice() end
  function cls.canonicalize(self, id)
    local c = item_composition(id)
    if c:getNote() ~= -1 then return c:getLinkedNoteId() end
    return id
  end
end)

def("net.runelite.client.game.SpriteManager", nil, {}, function(cls)
  function cls.__init(self) self.images = {} end
  function cls.__init__(self) self.images = {} end
  function cls.getSprite(self, id, frame)
    local key = id .. ":" .. frame
    local img = self.images[key]
    if img == nil then
      img = host_image(sprite_provider(id, frame), nil, nil)
      self.images[key] = img
    end
    return img
  end
end)

-- RuneLite's SkillIconManager reads its own resources /skill_icons/<name>.png
-- and /skill_icons_small/<name>.png: here the plugin assets
-- skill_icons_<name>.png / skill_icons_small_<name>.png (docs, "Images").
def("net.runelite.client.game.SkillIconManager", nil, {}, function(cls)
  function cls.__init(self) self.cache = {} end
  function cls.__init__(self) self.cache = {} end
  function cls.getSkillImage__Skill_boolean(self, skill, small)
    assert(skill)
    local name = (small and "skill_icons_small_" or "skill_icons_") .. skill:getName():lower() .. ".png"
    local img = self.cache[name]
    if img == nil then
      img = C["java.awt.image.BufferedImage"].qh_of_asset(name)
      self.cache[name] = img
    end
    return img
  end
  function cls.getSkillImage__Skill(self, skill) return self:getSkillImage__Skill_boolean(skill, false) end
end)

-- ------------------------------------------------------------------ chat

-- ChatMessageManager.formatRuneLiteMessage: <colNORMAL> back to the line's
-- own colour, <colHIGHLIGHT> the default highlight (ChatColorConfig).
local CHAT_HIGHLIGHT = "<col=ef1020>"
local function format_runelite(s)
  s = s:gsub("<colNORMAL>", "</col>")
  s = s:gsub("<colHIGHLIGHT>", CHAT_HIGHLIGHT)
  return s
end

def("net.runelite.client.chat.QueuedMessage", nil, {}, function(cls)
  function cls.builder() return setmetatable({}, C["net.runelite.client.chat.QueuedMessage$QueuedMessageBuilder"]) end
  function cls.__new_for(b)
    local o = setmetatable({}, cls)
    o.type, o.value, o.name, o.sender = b.type_, b.value_, b.name_, b.sender_
    o.runeLiteFormattedMessage, o.timestamp = b.fmt_, b.timestamp_ or 0
    return o
  end
  function cls.getType(self) return self.type end
  function cls.getValue(self) return self.value end
  function cls.getName(self) return self.name end
  function cls.getSender(self) return self.sender end
  function cls.getRuneLiteFormattedMessage(self) return self.runeLiteFormattedMessage end
  function cls.getTimestamp(self) return self.timestamp end
end)

def("net.runelite.client.chat.QueuedMessage$QueuedMessageBuilder", nil, {}, function(cls)
  function cls.type(self, t) self.type_ = t; return self end
  function cls.value(self, v) self.value_ = v; return self end
  function cls.name(self, n) self.name_ = n; return self end
  function cls.sender(self, s) self.sender_ = s; return self end
  function cls.runeLiteFormattedMessage(self, s) self.fmt_ = s; return self end
  function cls.timestamp(self, t) self.timestamp_ = t; return self end
  function cls.build(self) return C["net.runelite.client.chat.QueuedMessage"].__new_for(self) end
end)

def("net.runelite.client.chat.ChatMessageManager", nil, {}, function(cls)
  function cls.__init(self) end
  function cls.__init__(self) end
  -- RuneLite adds the line on the next client tick; a client-side chat line
  -- here is written at once.
  function cls.queue(self, m)
    assert(m)
    assert(m.type)
    local text = m.runeLiteFormattedMessage
    if text ~= nil then text = format_runelite(text) else text = m.value end
    assert(text)
    api().game.chat_add(m.type:getType(), m.name or "", text)
  end
  function cls.update(self, node) end
  function cls.process(self) end
end)

def("net.runelite.client.chat.ChatMessageBuilder", nil, {}, function(cls)
  function cls.__init(self) self.parts = {} end
  function cls.append__ChatColorType(self, t)
    assert(t)
    self.parts[#self.parts + 1] = "<col" .. t:name() .. ">"
    return self
  end
  function cls.append__Color_String(self, color, message)
    self.parts[#self.parts + 1] = C["net.runelite.client.util.ColorUtil"].wrapWithColorTag(message, color)
    return self
  end
  function cls.append__String(self, message)
    self.parts[#self.parts + 1] = C["net.runelite.client.util.Text"].escapeJagex(J.sval(message))
    return self
  end
  function cls.img(self, id)
    self.parts[#self.parts + 1] = "<img=" .. id .. ">"
    return self
  end
  function cls.build(self) return table.concat(self.parts) end
end)

-- ------------------------------------------------------------------ Text

local NBSP = "\xC2\xA0"

def("net.runelite.client.util.Text", nil, {}, function(cls)
  function cls.removeTags(s)
    assert(s)
    return (s:gsub("<[^>]*>", ""))
  end
  function cls.removeFormattingTags(s)
    assert(s)
    return (s:gsub("<([^>]*)>", function(t)
      if t == "lt" or t == "gt" then return nil end
      return ""
    end))
  end
  function cls.standardize(s)
    assert(s)
    local t = cls.removeTags(s):gsub(NBSP, " ")
    return t:trim():lower()
  end
  function cls.toJagexName(s)
    assert(s)
    local t = s:gsub(NBSP, " "):gsub("[_%-]", " "):gsub("[\128-\255]", "")
    return t:trim()
  end
  function cls.sanitize(name)
    assert(name)
    local cleaned = name
    if name:find("<img", 1, true) then
      local last
      for i = #name, 1, -1 do if name:sub(i, i) == ">" then last = i; break end end
      cleaned = name:sub(last + 1)
    end
    return (cleaned:gsub(NBSP, " "))
  end
  function cls.sanitizeMultilineText(s)
    assert(s)
    local t = s:gsub("%-<br>", "-"):gsub("<br>", " "):gsub(" +", " ")
    return cls.removeTags(t)
  end
  function cls.escapeJagex(s)
    assert(s)
    return (s:gsub("\r", ""):gsub("[<>\n]", { ["<"] = "<lt>", [">"] = "<gt>", ["\n"] = "<br>" }))
  end
  -- WordUtils.capitalize(lower, '_') then '_' -> ' ', unless toString is
  -- the enum's own.
  function cls.titleCase(o)
    assert(o)
    local s = o:toString()
    if o:name() ~= s then return s end
    local out = s:lower():gsub("^(%a)", string.upper):gsub("_(%a)", function(c) return "_" .. c:upper() end)
    return (out:gsub("_", " "))
  end
  function cls.fromCSV(s)
    assert(s)
    local l = C["java.util.ArrayList"].__new__()
    for part in (s .. ","):gmatch("([^,]*),") do
      local t = part:trim()
      if t ~= "" then l:add(t) end
    end
    return l
  end
  function cls.toCSV(c)
    assert(c)
    local t = {}
    for _, v in J.iter(c) do if v ~= nil then t[#t + 1] = J.str(v) end end
    return table.concat(t, ",")
  end
end)

-- ------------------------------------------------------------------ QuantityFormatter

local function group3(int_digits)
  local s = int_digits
  local out = s:reverse():gsub("(%d%d%d)", "%1,"):reverse()
  if out:sub(1, 1) == "," then out = out:sub(2) end
  return out
end

-- NumberFormat.getInstance(Locale.ENGLISH): grouping, at most three
-- fraction digits, half-even on the exact binary value.
local function number_format(v)
  if math.type(v) == "integer" then
    if v < 0 then return "-" .. group3(tostring(v):sub(2)) end
    return group3(tostring(v))
  end
  local neg = v < 0
  local s = string.format("%.3f", math.abs(v))
  local ip, fp = s:match("^(%d+)%.(%d+)$")
  fp = fp:gsub("0+$", "")
  local r = group3(ip)
  if fp ~= "" then r = r .. "." .. fp end
  if neg and r ~= "0" then r = "-" .. r end
  return r
end

local SUFFIXES = { [0] = "", "K", "M", "B" }

def("net.runelite.client.util.QuantityFormatter", nil, {}, function(cls)
  function cls.formatNumber__long(n) return number_format(n) end
  function cls.formatNumber__double(n) return number_format(n + 0.0) end
  function cls.quantityToStackSize(q)
    if q < 0 then
      if q == math.mininteger then return "-" .. cls.quantityToStackSize(math.maxinteger) end
      return "-" .. cls.quantityToStackSize(-q)
    elseif q < 10000 then
      return number_format(q)
    end
    local suffix, divide = SUFFIXES[0], 1
    for i = 3, 0, -1 do
      divide = math.tointeger(10 ^ (i * 3))
      if q / divide >= 1 then suffix = SUFFIXES[i]; break end
    end
    local f = number_format(q / divide)
    if #f > 4 then f = f:sub(1, 4) end
    if f:sub(-1) == "." then f = f:sub(1, 3) end
    return f .. suffix
  end
  function cls.quantityToRSDecimalStack__int(q) return cls.quantityToRSDecimalStack__int_boolean(q, false) end
  function cls.quantityToRSDecimalStack__int_boolean(q, precise)
    local s = tostring(q)
    if #s <= 4 then return s end
    local power = J.d2i(math.log(q, 10))
    local digits = (precise and power >= 6) and 3 or 1
    local v = q / 10 ^ ((power // 3) * 3)
    local scale = 10 ^ digits
    v = math.floor(v * scale) / scale -- RoundingMode.FLOOR
    local f = string.format("%." .. digits .. "f", v):gsub("0+$", ""):gsub("%.$", "")
    local ip, fp = f:match("^(%d+)(.*)$")
    return group3(ip) .. fp .. SUFFIXES[power // 3]
  end
end)

-- ------------------------------------------------------------------ colours

def("net.runelite.client.util.ColorUtil", nil, {}, function(cls)
  function cls.colorToHexCode(c) assert(c); return color_hex(color_rgb(c)) end
  function cls.colorToAlphaHexCode(c) assert(c); return string.format("%08x", c:getRGB() & 0xFFFFFFFF) end
  function cls.toHexColor(c) return "#" .. cls.colorToHexCode(c) end
  function cls.colorTag(c) return "<col=" .. cls.colorToHexCode(c) .. ">" end
  function cls.prependColorTag(s, c) return cls.colorTag(c) .. J.sval(s) end
  function cls.wrapWithColorTag(s, c) return cls.prependColorTag(s, c) .. "</col>" end
  function cls.colorWithAlpha(c, alpha)
    assert(c)
    if color_alpha(c) == alpha then return c end
    if alpha < 0 then alpha = 0 elseif alpha > 255 then alpha = 255 end
    local rgb = color_rgb(c)
    return new_color((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, alpha)
  end
  function cls.colorLerp(a, b, t)
    local function lerp(x, y) return J.d2i(x + (y - x) * t + 0.5) end
    return new_color(lerp(a:getRed(), b:getRed()), lerp(a:getGreen(), b:getGreen()), lerp(a:getBlue(), b:getBlue()), lerp(color_alpha(a), color_alpha(b)))
  end
  function cls.fromHex(s)
    assert(s)
    local h = s:gsub("^#", "")
    local v = tonumber(h, 16)
    if v == nil then return nil end
    if #h > 6 then
      return new_color((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF, (v >> 24) & 0xFF)
    end
    return new_color((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF)
  end
end)

-- java.awt.Color.RGBtoHSB
local function rgb_to_hsb(r, g, b)
  local cmax = math.max(r, g, b)
  local cmin = math.min(r, g, b)
  local bright = cmax / 255.0
  local sat = 0.0
  if cmax ~= 0 then sat = (cmax - cmin) / cmax end
  local hue = 0.0
  if sat ~= 0 then
    local d = cmax - cmin
    local rc, gc, bc = (cmax - r) / d, (cmax - g) / d, (cmax - b) / d
    if r == cmax then hue = bc - gc
    elseif g == cmax then hue = 2.0 + rc - bc
    else hue = 4.0 + gc - rc end
    hue = hue / 6.0
    if hue < 0 then hue = hue + 1.0 end
  end
  return hue, sat, bright
end

def("net.runelite.api.JagexColor", nil, {}, function(cls)
  cls.HUE_MAX, cls.SATURATION_MAX, cls.LUMINANCE_MAX = 63, 7, 127
  function cls.packHSL(h, s, l)
    return J.i2s(((h & 63) << 10) | ((s & 7) << 7) | (l & 127))
  end
  function cls.unpackHue(hsl) return (hsl >> 10) & 63 end
  function cls.unpackSaturation(hsl) return (hsl >> 7) & 7 end
  function cls.unpackLuminance(hsl) return hsl & 127 end
  function cls.rgbToHSL(rgb, brightness)
    if rgb == 1 then return 0 end
    brightness = 1.0 / brightness
    local r = (((rgb >> 16) & 255) / 256.0) ^ brightness
    local g = (((rgb >> 8) & 255) / 256.0) ^ brightness
    local b = ((rgb & 255) / 256.0) ^ brightness
    local hue, s, v = rgb_to_hsb(J.d2i(r * 256.0), J.d2i(g * 256.0), J.d2i(b * 256.0))
    local lum = v - (v * s) / 2.0
    local m = math.min(lum, 1 - lum)
    local sat = 0.0
    if m ~= 0 then sat = (v - lum) / m end
    return cls.packHSL(J.d2i(math.ceil(hue * 64.0) % 63.0), J.d2i(math.ceil(sat * 7.0)), J.d2i(math.ceil(lum * 127.0)))
  end
end)

def("net.runelite.client.ui.JagexColors", nil, {}, nil, function(cls)
  cls.CHAT_PUBLIC_TEXT_OPAQUE_BACKGROUND = new_color(0, 0, 255)
  cls.CHAT_PRIVATE_MESSAGE_TEXT_OPAQUE_BACKGROUND = new_color(0x7F, 0, 0)
  cls.CHAT_FC_TEXT_OPAQUE_BACKGROUND = new_color(127, 0, 0)
  cls.CHAT_FC_NAME_OPAQUE_BACKGROUND = new_color(0, 0, 255)
  cls.CHAT_GAME_EXAMINE_TEXT_OPAQUE_BACKGROUND = new_color(0, 0, 0)
  cls.CHAT_TYPED_TEXT_OPAQUE_BACKGROUND = new_color(0, 0, 255)
  cls.CHAT_PUBLIC_TEXT_TRANSPARENT_BACKGROUND = new_color(144, 144, 255)
  cls.CHAT_PRIVATE_MESSAGE_TEXT_TRANSPARENT_BACKGROUND = new_color(0xBF, 0x20, 0x20)
  cls.CHAT_FC_TEXT_TRANSPARENT_BACKGROUND = new_color(239, 80, 80)
  cls.CHAT_FC_NAME_TRANSPARENT_BACKGROUND = new_color(144, 112, 255)
  cls.CHAT_GAME_EXAMINE_TEXT_TRANSPARENT_BACKGROUND = new_color(255, 255, 255)
  cls.CHAT_TYPED_TEXT_TRANSPARENT_BACKGROUND = new_color(144, 144, 255)
  cls.MENU_TARGET = new_color(0xFF, 0x90, 0x40)
  cls.TOOLTIP_BACKGROUND = new_color(255, 255, 160)
  cls.TOOLTIP_BORDER = new_color(0, 0, 0)
  cls.TOOLTIP_TEXT = new_color(0, 0, 0)
  cls.DARK_ORANGE_INTERFACE_TEXT = new_color(255, 152, 31)
  cls.YELLOW_INTERFACE_TEXT = new_color(255, 255, 0)
end)

def("net.runelite.client.ui.ColorScheme", nil, {}, nil, function(cls)
  cls.BRAND_ORANGE = new_color(220, 138, 0)
  cls.BRAND_ORANGE_TRANSPARENT = new_color(220, 138, 0, 120)
  cls.DARKER_GRAY_COLOR = new_color(30, 30, 30)
  cls.DARK_GRAY_COLOR = new_color(40, 40, 40)
  cls.MEDIUM_GRAY_COLOR = new_color(77, 77, 77)
  cls.LIGHT_GRAY_COLOR = new_color(165, 165, 165)
  cls.TEXT_COLOR = new_color(198, 198, 198)
  cls.CONTROL_COLOR = new_color(30, 30, 30)
  cls.BORDER_COLOR = new_color(23, 23, 23)
  cls.DARKER_GRAY_HOVER_COLOR = new_color(60, 60, 60)
  cls.DARK_GRAY_HOVER_COLOR = new_color(35, 35, 35)
  cls.PROGRESS_COMPLETE_COLOR = new_color(55, 240, 70)
  cls.PROGRESS_ERROR_COLOR = new_color(230, 30, 30)
  cls.PROGRESS_INPROGRESS_COLOR = new_color(230, 150, 30)
  cls.GRAND_EXCHANGE_PRICE = new_color(110, 225, 110)
  cls.GRAND_EXCHANGE_ALCH = new_color(240, 207, 123)
  cls.GRAND_EXCHANGE_LIMIT = new_color(50, 160, 250)
  cls.SCROLL_TRACK_COLOR = new_color(25, 25, 25)
end)

-- ------------------------------------------------------------------ hooks

-- LinkBrowser.browse: the player's browser through api.client.open_url (the
-- desktop browser, the browser lane's window.open, an Android intent). A lane
-- with none ('unsupported', a headless run) says so once in the log. Returns
-- whether the url went out (RuneLite's is void; the panel ignores it).
def("net.runelite.client.util.LinkBrowser", nil, {}, function(cls)
  function cls.browse(url)
    assert(url)
    local ok, why = api().client.open_url(J.str(url))
    if not ok then J.log("quest-helper: could not open " .. J.str(url) .. ": " .. tostring(why)) end
    return ok == true
  end
end)

J.rl_client = {
  client = client,
  menu_begin = menu_begin,
  menu_end = menu_end,
  menu_select = menu_select,
  menu_click = menu_click,
  script_enter = function(ref)
    assert(ref)
    script.ref = ref
  end,
  script_leave = function() script.ref = nil end,
  inventory_changed = function(inv) containers[inv] = nil end,
  -- the ItemContainerChanged container for `inv` (never nil)
  changed_container = changed_container,
  watched_varbits = watched_varbits,
  -- varc id -> last value: every varc getVarcIntValue was asked for
  watched_varcs = watched_varcs,
}
