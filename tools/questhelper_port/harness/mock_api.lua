-- A mock of the client's plugin `api` for running the Quest Helper port
-- under plain Lua. One section per area (docs/QUEST_HELPER_PORT.md, "The
-- RuneLite layer"); each section belongs to the agent that owns that rt file:
-- ADD to your section, do not rewrite another's.
--
-- State is plain tables a test sets directly: MOCK.varbits[id] = v,
-- MOCK.inv[93] = {{id, count}, ...}, MOCK.player = {x, z, level}, ...

MOCK = MOCK or {
  varbits = {}, varps = {}, varcs_int = {}, varcs_str = {},
  skills = {},        -- [index] = {current_level, base_level, xp}
  inv = {},           -- [inv_id] = { [slot+1] = {id, count} }
  npcs = {},          -- array of NpcSnap-shaped tables
  scenery = {},       -- array of ScenerySnap-shaped tables
  items = {},         -- ground items
  widgets = {},       -- [component_id] = detail table
  config = {},        -- declared plugin config values
  assets = {},        -- [name] = bytes
  chat = {},          -- lines api.game.chat_add wrote
  draws = {},         -- calls the draw object received
  tick = 0,
  now_ms = 0,
  screen = "game",
  player = { name = "Tester", true_x = 3222, true_z = 3218, level = 0 },
}

local api = {}
MOCK_API = api

-- ------------------------------------------------------------------ core / sandbox
api.core = {
  log = function(...) end,
  notify = function(text) end,
  screen = function() return MOCK.screen end,
  frame_ms = function() return MOCK.now_ms end,
  plugin_id = function() return "quest-helper" end,
  capability = function() return true end,
}
api.sandbox = {
  protect = pcall,
  module_request = function(name) return "ready" end,
  module_load = function(name) error("harness loads modules with dofile") end,
}

-- ------------------------------------------------------------------ config (rl_core)
-- MOCK_DECLARE_CONFIG(items) plays the client reading a definition's
-- config = {...}: types and defaults per key. Values are then held the way
-- the client holds them (bool, integer, 0xRRGGBB, string), set() parses
-- what the panel would write ("#RRGGBB", "1"/"0"), and an undeclared key is
-- refused. Without a declaration the mock stores whatever it is given.
MOCK.config_types = MOCK.config_types or {}
MOCK.config_defaults = MOCK.config_defaults or {}
MOCK.config_sets = MOCK.config_sets or 0
function MOCK_DECLARE_CONFIG(items)
  MOCK.config_types, MOCK.config_defaults = {}, {}
  for _, it in ipairs(items) do
    local t = it.type or "string"
    MOCK.config_types[it.key] = t
    local d = it.default
    if t == "color" and type(d) == "string" then d = tonumber(d:sub(2), 16) end
    if t == "bool" then d = d == true end
    MOCK.config_defaults[it.key] = d
  end
end
local function mock_config_get(k)
  local v = MOCK.config[k]
  if v == nil then v = MOCK.config_defaults[k] end
  return v
end
local function mock_config_set(k, v)
  local t = MOCK.config_types[k]
  if next(MOCK.config_types) ~= nil and t == nil then return false, "invalid" end
  if t == "color" and type(v) == "string" then
    local hex = v:match("^#(%x%x%x%x%x%x)$")
    if not hex then return false, "invalid" end
    v = tonumber(hex, 16)
  elseif t == "bool" then
    if type(v) == "string" then v = (v == "1" or v == "true") end
  elseif t == "int" and type(v) == "string" then
    v = math.tointeger(tonumber(v))
    if v == nil then return false, "invalid" end
  end
  MOCK.config[k] = v
  MOCK.config_sets = MOCK.config_sets + 1
  return true, "ok"
end
api.config = setmetatable({
  get_bool = mock_config_get,
  get_int = mock_config_get,
  get_color = mock_config_get,
  get_string = mock_config_get,
  has = function(k) return mock_config_get(k) ~= nil end,
  set = mock_config_set,
}, { __index = function(_, k) return mock_config_get(k) end })

-- ------------------------------------------------------------------ assets (rl_core)
-- MOCK.asset_states[name] = 'pending' | 'error' | ... overrides what
-- request() answers (a slow or failed read).
MOCK.asset_states = MOCK.asset_states or {}
MOCK.asset_saves = MOCK.asset_saves or {}
api.assets = {
  request = function(name)
    local st = MOCK.asset_states[name]
    if st then return st end
    return MOCK.assets[name] and "ready" or "missing"
  end,
  bytes = function(name) return MOCK.assets[name] end,
  save = function(name, data)
    MOCK.assets[name] = data
    MOCK.asset_saves[name] = (MOCK.asset_saves[name] or 0) + 1
    return true, "ok"
  end,
  release = function(name) end,
}

-- ------------------------------------------------------------------ cache / game / client (rl_client)
api.cache = {
  varbit = function(id) return MOCK.varbits[id] or 0 end,
  varp = function(id) return MOCK.varps[id] or 0 end,
  varc_int = function(id) return MOCK.varcs_int[id] or 0 end,
  varc_str = function(id) return MOCK.varcs_str[id] or "" end,
  set_varc_int = function(id, v) MOCK.varcs_int[id] = v end,
  set_varc_str = function(id, v) MOCK.varcs_str[id] = v end,
  named_id = function(kind, name) return nil end,
}
api.game = {
  skill = function(i)
    local s = MOCK.skills[i] or { current_level = 1, base_level = 1, xp = 0 }
    return { index = i, current_level = s.current_level, base_level = s.base_level, xp = s.xp or 0, stated = true }
  end,
  -- The api takes a ToriRS_InventoryKind (0 backpack, 1 worn, 2 bank), not a
  -- container id; MOCK.inv stays keyed by container id (93/94/95), as
  -- on_inventory_changed reports them.
  inventory_size = function(kind)
    assert(kind == 0 or kind == 1 or kind == 2, "inventory_size takes a kind 0..2, not a container id")
    local t = MOCK.inv[93 + kind]; return t and 28 or 0
  end,
  inventory_slot = function(kind, slot)
    assert(kind == 0 or kind == 1 or kind == 2, "inventory_slot takes a kind 0..2, not a container id")
    local t = MOCK.inv[93 + kind]
    local e = t and t[slot + 1]
    if e then return e[1], e[2] end
    return nil
  end,
  -- any container by its wire id (on_inventory_changed's): MOCK.inv[id] with
  -- `size` slots (#t when absent); 0 / nil for one never sent
  container_size = function(id)
    local t = MOCK.inv[id]; if t == nil then return 0 end
    return t.size or #t
  end,
  container_slot = function(id, slot)
    local t = MOCK.inv[id]
    if t == nil or slot < 0 or slot >= (t.size or #t) then return nil end
    local e = t[slot + 1]
    if e then return e[1], e[2] end
    return -1, 0
  end,
  item_info = function(id) return { obj_id = id, name = "Item " .. id, stackable = false } end,
  chat_add = function(type, sender, text) MOCK.chat[#MOCK.chat + 1] = { type, sender, text } end,
  tick = function() return MOCK.tick end,
}
api.client = { world_cycle = function() return MOCK.tick end }
api.scripts = { available = function() return true end }

-- rl_client additions. MOCK.scripts[id] = function(...) return ints, strs end
-- answers api.scripts.run; MOCK.enums / npc_defs / loc_defs [id] = the
-- contract's tables; MOCK.menu_added collects api.menu.add rows.
MOCK.scripts = MOCK.scripts or {}
MOCK.enums = MOCK.enums or {}
MOCK.npc_defs = MOCK.npc_defs or {}
MOCK.loc_defs = MOCK.loc_defs or {}
MOCK.menu_added = MOCK.menu_added or {}
MOCK.sounds = MOCK.sounds or {}
MOCK.invoked = MOCK.invoked or {}
MOCK.world_types = MOCK.world_types or 0
MOCK.weight = MOCK.weight or 0
MOCK.pointer = MOCK.pointer or { 10, 20 }
MOCK.display = MOCK.display or { layout_width = 1024, layout_height = 768, window_fixed = 0 }
api.scripts.run = function(id, ...)
  if MOCK.scripts_refuse then return nil, MOCK.scripts_refuse end
  local f = MOCK.scripts[id]
  if f == nil then return {}, {} end
  return f(...)
end
api.scripts.counts = function(ref) return #ref.ints, #ref.strs end
api.scripts.get_int = function(ref, i) return ref.ints[#ref.ints - i] end
api.scripts.set_int = function(ref, i, v) ref.ints[#ref.ints - i] = v; return true end
api.scripts.get_string = function(ref, i) return ref.strs[#ref.strs - i] end
api.scripts.set_string = function(ref, i, v) ref.strs[#ref.strs - i] = v; return true end
-- scripts.watch: MOCK.watched[id] = true; MOCK.watch_refuse = reason
MOCK.watched = MOCK.watched or {}
api.scripts.watch = function(id, on)
  assert(math.type(id) == "integer")
  assert(type(on) == "boolean")
  if MOCK.watch_refuse then return false, MOCK.watch_refuse end
  MOCK.watched[id] = on or nil
  return true
end
api.cache.enum_def = function(id)
  local e = MOCK.enums[id]
  if e ~= nil then return e, "ready" end
  return nil, "missing"
end
api.cache.npc_def = function(id) return MOCK.npc_defs[id] end
api.cache.loc_def = function(id) return MOCK.loc_defs[id] end
api.cache.invoke = function(component, op) MOCK.invoked[#MOCK.invoked + 1] = { component, op }; return true end
api.game.weight = function() return MOCK.weight end
api.game.world_types = function() return MOCK.world_types end
api.game.run_energy = function() return 100 end
api.game.sound = function(id) MOCK.sounds[#MOCK.sounds + 1] = id end
api.game.hint_arrow = function() return MOCK.hint_arrow end
api.game.hint_arrow_npc = function(slot) MOCK.hint_arrow = { kind = "npc", npc_slot = slot } end
api.game.hint_arrow_tile = function(x, z, level) MOCK.hint_arrow = { kind = "tile", tile_x = x, tile_z = z, level = level, npc_slot = -1 } end
api.game.hint_arrow_clear = function() MOCK.hint_arrow = nil end
api.game.item_image = function(id, count, style) return { image = id, count = count }, "ready" end
api.menu = api.menu or {}
api.menu.add = function(text, tag) MOCK.menu_added[#MOCK.menu_added + 1] = { text, tag }; return true end
-- client-APIs round: MOCK.menu_order = the last set_order list, MOCK.menu_texts[row] =
-- set_text; MOCK.menu_box = what menu.shown answers (nil: no menu up);
-- MOCK.varbit_writes / MOCK.urls / MOCK.map_jumps record the writes;
-- (seq_def is the rl_world section's, over MOCK.seqs);
-- MOCK.url_refuse = reason refuses open_url.
MOCK.menu_texts = MOCK.menu_texts or {}
MOCK.varbit_writes = MOCK.varbit_writes or {}
MOCK.urls = MOCK.urls or {}
MOCK.map_jumps = MOCK.map_jumps or {}
MOCK.seqs = MOCK.seqs or {}
api.menu.set_order = function(rows)
  assert(type(rows) == "table")
  MOCK.menu_order = rows
  return true, "ok"
end
api.menu.set_text = function(row, text)
  assert(math.type(row) == "integer")
  MOCK.menu_texts[row] = text
  return true, "ok"
end
api.menu.shown = function() return MOCK.menu_box end
api.cache.set_varbit = function(id, v)
  MOCK.varbit_writes[#MOCK.varbit_writes + 1] = { id, v }
  if MOCK.varbits then MOCK.varbits[id] = v end
  return true, "ok"
end
api.client.open_url = function(url)
  if MOCK.url_refuse then return false, MOCK.url_refuse end
  MOCK.urls[#MOCK.urls + 1] = url
  return true, "ok"
end
api.client.worldmap_jump = function(x, z, level)
  MOCK.map_jumps[#MOCK.map_jumps + 1] = { x, z, level or 0 }
  return true, "ok"
end
api.sandbox.memory_limit = api.sandbox.memory_limit or function() return MOCK.memory_limit or 16 * 1024 * 1024 end
api.input = api.input or {}
api.input.pointer = function() return MOCK.pointer[1], MOCK.pointer[2] end
api.client.display = { layout_width = 1, layout_height = 2, window_fixed = 3 }
api.client.display_get = function(setting)
  for k, v in pairs(api.client.display) do if v == setting then return MOCK.display[k] end end
  return nil
end
api.assets.image_size = function(ref) return 3, 3 end
api.assets.image_pixels = function(ref) return MOCK.pixels end
api.assets.image_compose = function(name, w, h, argb) MOCK.composed = { name = name, w = w, h = h, argb = argb }; return { composed = name }, "ready" end

-- ------------------------------------------------------------------ world (rl_world)
api.world = {
  local_player = function() return MOCK.player end,
  scene_origin = function() return (MOCK.player.true_x // 8 - 6) * 8, (MOCK.player.true_z // 8 - 6) * 8 end,
  npc_next = function(cursor) cursor = (cursor == nil or cursor < 0) and 1 or cursor + 1; local n = MOCK.npcs[cursor]; if n then return cursor, n end end,
  scenery_next = function(cursor) cursor = (cursor == nil or cursor < 0) and 1 or cursor + 1; local n = MOCK.scenery[cursor]; if n then return cursor, n end end,
  item_next = function(cursor) cursor = (cursor == nil or cursor < 0) and 1 or cursor + 1; local n = MOCK.items[cursor]; if n then return cursor, n end end,
  instance_zone = function() return nil end,
}

-- rl_world additions. MOCK.no_world = true answers scene_origin nil;
-- MOCK.players = other PlayerSnaps; MOCK.zones["level,zx,zz"] = {template
-- tile x, z, level, rotation} answers instance_zone; MOCK.element_heights
-- [element_id]; MOCK.hover = {x, z, level}; MOCK.seqs[id] = {frame_count,
-- frame_lengths}; api.scene calls land in MOCK.scene_calls. Projection is a
-- flat top-down camera, canvas = (fine / 4, fine / 4 - height / 4), refusing
-- fine < 128 like the client; MOCK.project overrides it.
MOCK.players = MOCK.players or {}
MOCK.zones = MOCK.zones or {}
MOCK.element_heights = MOCK.element_heights or {}
MOCK.seqs = MOCK.seqs or {}
MOCK.scene_calls = MOCK.scene_calls or {}
-- MOCK.scene_origin = {x, z} pins it (a player aboard a boat is addressed in
-- the boat's staging rectangle; the root scene does not follow).
api.world.scene_origin = function()
  if MOCK.no_world or MOCK.player == nil then return nil end
  if MOCK.scene_origin then return MOCK.scene_origin[1], MOCK.scene_origin[2] end
  return (MOCK.player.true_x // 8 - 6) * 8, (MOCK.player.true_z // 8 - 6) * 8
end
api.world.local_player = function() if MOCK.no_world then return nil end return MOCK.player end
api.world.npc_by_slot = function(slot)
  for _, n in ipairs(MOCK.npcs) do if n.server_slot == slot then return n end end
  return nil
end
api.world.player_next = function(cursor)
  -- the api's start is nil or -1 (luaL_optinteger default)
  cursor = (cursor == nil or cursor < 0) and 1 or cursor + 1
  local p = MOCK.players[cursor]
  if p then return cursor, p end
end
api.world.instance_zone = function(level, zx, zz)
  local z = MOCK.zones[level .. "," .. zx .. "," .. zz]
  if z then return z[1], z[2], z[3], z[4] end
  return nil
end
api.draw = api.draw or {}
api.draw.project = api.draw.project or function(fine_x, fine_z, height)
  if MOCK.project then return MOCK.project(fine_x, fine_z, height) end
  if fine_x < 128 or fine_z < 128 then return nil end
  return fine_x // 4, fine_z // 4 - (height or 0) // 4
end
api.draw.element_height = api.draw.element_height or function(id) return MOCK.element_heights[id] or 0 end
api.input = api.input or {}
api.input.hover_tile = function()
  local h = MOCK.hover
  if h then return h[1], h[2], h[3] end
  return nil
end
api.cache.seq_def = function(id) return MOCK.seqs[id] end
do
  local serial = 0
  local function rec(name, ret)
    return function(...)
      MOCK.scene_calls[#MOCK.scene_calls + 1] = { name, ... }
      return ret
    end
  end
  api.scene = {
    instance_create = function()
      serial = serial + 1
      local ref = { instance = serial }
      MOCK.scene_calls[#MOCK.scene_calls + 1] = { "instance_create", ref }
      return ref, "ok"
    end,
    instance_destroy = rec("instance_destroy"),
    instance_cache_model = rec("instance_cache_model", true),
    instance_recolor = rec("instance_recolor", true),
    instance_light = rec("instance_light", true),
    instance_position = rec("instance_position", true),
    instance_animation = rec("instance_animation", true),
    instance_active = rec("instance_active"),
    instance_place = rec("instance_place", true),
    -- MOCK.instance_elements[serial] = the element an instance is drawn as;
    -- MOCK.instance_hits[serial] = true while the pointer's pick holds it
    instance_element = function(ref) return (MOCK.instance_elements or {})[ref.instance] end,
    instance_hit = function(ref) return (MOCK.instance_hits or {})[ref.instance] == true end,
  }
end

-- ------------------------------------------------------------------ widgets (rl_widgets)
api.widgets = {
  get = function(id) return nil end,
}
-- MOCK.widgets[component_id] = a node: the detail() fields plus
--   dynamic = { node, ... }   dynamic children (each a node; id/index filled in)
--   static = { component_id, ... }, nested = { component_id, ... }
--   listeners = { onInvTransmit = { script_id, args... }, ... }
--   incarnation (default 1), bounds = {x, y, width, height}
-- Every host call lands in MOCK.widget_calls as { method, node-or-owned, args... };
-- owned controls are MOCK.owned[key] = { kind, parent, text, ops = {[op] = {label, cb}}, ... }.
MOCK.widget_calls = MOCK.widget_calls or {}
MOCK.owned = MOCK.owned or {}
MOCK.scripts_run = MOCK.scripts_run or {}
MOCK.owned_cap = MOCK.owned_cap or 48
local WMT = {}
WMT.__index = WMT
local function wcall(name, target, ...) MOCK.widget_calls[#MOCK.widget_calls + 1] = { name, target, ... } end
local function whandle(node) return setmetatable({ node = node }, WMT) end
local function ohandle(rec) return setmetatable({ owned = rec }, WMT) end
MOCK_WIDGET_HANDLE = whandle
local function fill(node, id, index, parent_id)
  node.id = id; node.index = index; node.parent_id = node.parent_id or parent_id or -1
  for k, v in pairs({ type = 0, item_id = -1, item_quantity = 0, sprite_id = -1, model_id = -1, text = "",
    text_color = 0, name = "", hidden = false, self_hidden = false, x = 0, y = 0, width = 0, height = 0,
    original_x = 0, original_y = 0, original_width = 0, original_height = 0, relative_x = 0, relative_y = 0,
    scroll_x = 0, scroll_y = 0, scroll_width = 0, scroll_height = 0, opacity = 0, actions = {} }) do
    if node[k] == nil then node[k] = v end
  end
  return node
end
function WMT:detail()
  if self.owned then local r = self.owned; return r.removed and nil or { id = -1, index = -1, text = r.text or "" } end
  local n = self.node
  if n.stale then return nil end
  local d = {}
  for k, v in pairs(n) do if k ~= "dynamic" and k ~= "static" and k ~= "nested" and k ~= "listeners" then d[k] = v end end
  return d
end
function WMT:state()
  if self.owned then return { incarnation = 1 } end
  if self.node.stale then return nil end
  return { incarnation = self.node.incarnation or 1 }
end
function WMT:dynamic_children()
  local out = {}
  for i, c in ipairs(self.node.dynamic or {}) do out[i] = whandle(fill(c, self.node.id, i - 1, self.node.id)) end
  return out
end
local function by_ids(ids, parent_id)
  local out = {}
  for _, id in ipairs(ids or {}) do
    local n = MOCK.widgets[id]
    if n then out[#out + 1] = whandle(fill(n, id, -1, parent_id)) end
  end
  return out
end
function WMT:static_children() return by_ids(self.node.static, self.node.id) end
function WMT:nested_children() return by_ids(self.node.nested, self.node.id) end
function WMT:children() return self:static_children() end
function WMT:bounds()
  if self.owned then local r = self.owned; return { x = (r.x or 0), y = (r.y or 0), width = r.width or 0, height = r.height or 0 } end
  local b = self.node.bounds
  if b then return { x = b[1], y = b[2], width = b[3], height = b[4] } end
  return { x = self.node.x, y = self.node.y, width = self.node.width, height = self.node.height }
end
function WMT:listener(name)
  local l = self.node and self.node.listeners and self.node.listeners[name]
  wcall("listener", self.node, name)
  return l
end
local function target(self) return self.owned or self.node end
local function edit(name, field)
  return function(self, ...)
    wcall(name, target(self), ...)
    local t = target(self)
    t.edits = t.edits or {}
    t.edits[field] = table.pack(...)
    if self.owned then
      local a = ...
      if field == "position" then t.x, t.y = ... elseif field == "size" then t.width, t.height = ... else t[field] = a end
      if field == "item" then t.item_id, t.item_quantity = ... end
    end
    return true, "ok"
  end
end
WMT.set_hidden = edit("set_hidden", "hidden")
WMT.set_native_text = edit("set_native_text", "text")
WMT.set_native_text_color = edit("set_native_text_color", "text_color")
WMT.set_sprite = edit("set_sprite", "sprite")
WMT.set_item = edit("set_item", "item")
WMT.set_position = edit("set_position", "position")
WMT.set_size = edit("set_size", "size")
WMT.set_text = edit("set_text", "text")
WMT.set_text_color = edit("set_text_color", "text_color")
WMT.set_text_align = edit("set_text_align", "align")
WMT.set_opacity = edit("set_opacity", "opacity")
WMT.revalidate = edit("revalidate", "revalidated")
function WMT:set_on_op(op, label, cb)
  wcall("set_on_op", target(self), op, label)
  local r = self.owned
  assert(r, "set_on_op on a native widget is invalid_argument")
  r.ops = r.ops or {}
  r.ops[op] = cb and { label, cb } or nil
  return true, "ok"
end
function WMT:reset() wcall("reset", target(self)); local t = target(self); t.edits = nil; return true, "ok" end
function WMT:remove()
  wcall("remove", target(self))
  if self.owned then self.owned.removed = true; MOCK.owned[self.owned.key] = nil end
  return true, "ok"
end
local function creator(kind)
  return function(self, key)
    assert(self.node, "owned controls nest under native widgets")
    local live = 0
    for _ in pairs(MOCK.owned) do live = live + 1 end
    if MOCK.owned[key] == nil and live >= MOCK.owned_cap then return nil end
    local r = MOCK.owned[key] or { key = key, kind = kind, parent = self.node.id }
    MOCK.owned[key] = r
    wcall("create_" .. kind, self.node, key)
    return ohandle(r)
  end
end
WMT.create_text = creator("text")
WMT.create_image = creator("image")
WMT.create_item = creator("item")
-- owned RECTANGLE / MODEL controls (bank tab / chatbox / quest-complete scroll)
WMT.create_rect = creator("rect")
WMT.create_model = creator("model")
WMT.set_rect = edit("set_rect", "rect")
WMT.set_model = edit("set_model", "model")
WMT.set_model_pose = edit("set_model_pose", "pose")
WMT.set_model_anim = edit("set_model_anim", "anim")
function WMT:set_native_hidden(h)
  wcall("set_native_hidden", target(self), h)
  if self.node then self.node.self_hidden = h; self.node.hidden = h end
  return true, "ok"
end
function WMT:set_scroll_size(w, h)
  wcall("set_scroll_size", target(self), w, h)
  if self.node then self.node.scroll_width, self.node.scroll_height = w, h end
  return true, "ok"
end
function WMT:actions()
  local n = self.node
  local out = {}
  for i, label in ipairs(n and n.menu or {}) do out[i] = { label = label, ref = { node = n, op = i } } end
  return out
end
api.widgets.get = function(id)
  local n = MOCK.widgets[id]
  if n == nil then return nil end
  return whandle(fill(n, id, -1, n.parent_id))
end
-- Press op `op` on an owned control, as the host's hit test would.
function MOCK_PRESS(key, op)
  local r = assert(MOCK.owned[key], "no owned control " .. tostring(key))
  local o = assert(r.ops and r.ops[op], "op " .. op .. " not armed")
  o[2](ohandle(r), { kind = "operation", operation = op, native_revision = 1 })
end
do -- record every script run; an earlier section's run still answers
  local prev = api.scripts.run
  api.scripts.run = function(...)
    MOCK.scripts_run[#MOCK.scripts_run + 1] = table.pack(...)
    if prev then return prev(...) end
    return {}, {}
  end
end

-- ------------------------------------------------------------------ drawing (awt / rl_overlay)
-- A draw object for on_draw_* tests: records every call.
function MOCK_DRAW()
  local d = {}
  local function rec(name) return function(...) MOCK.draws[#MOCK.draws + 1] = { name, ... }; return true end end
  for _, n in ipairs({ "rect", "line", "text", "image", "world_tile", "world_hull", "polygon", "image_clip" }) do d[n] = rec(n) end
  d.text_width = function(s) return #s * 6 end
  d.text_height = function() return 12 end
  d.minimap_project = function() return nil end
  d.worldmap_project = function() return nil end
  d.project = function(x, z) return 100, 100 end
  return d
end

-- awt.lua / rl_overlay.lua: MOCK_DRAW plus the pass context, the world map
-- and the minimap; and an image store a test installs for itself
-- (MOCK_IMAGES_INSTALL replaces the api.assets image calls only for the test
-- that calls it).
MOCK.canvas = MOCK.canvas or { x = 0, y = 0, width = 765, height = 503 }
function MOCK_DRAW_AWT()
  local d = MOCK_DRAW()
  d.context = function()
    local c = MOCK.canvas
    return { bounds = { x = c.x, y = c.y, width = c.width, height = c.height },
      clip = { x = c.x, y = c.y, width = c.width, height = c.height } }
  end
  d.worldmap_bounds = function() return MOCK.worldmap end
  d.worldmap_project = function(x, z, level)
    if MOCK.worldmap_project then return MOCK.worldmap_project(x, z, level) end
    return nil
  end
  d.minimap_bounds = function() return MOCK.minimap end
  return d
end
function MOCK_IMAGES_INSTALL()
  MOCK.images = {}                               -- [ref] = {w, h, px, name}
  MOCK.image_assets = MOCK.image_assets or {}    -- [asset name] = {w, h, px, pending?}
  MOCK.released = {}
  MOCK.composes = 0
  local next_ref = 1000
  local by_name = {}
  local function new_ref(img) next_ref = next_ref + 1; MOCK.images[next_ref] = img; return next_ref end
  api.assets.image = function(name)
    local a = MOCK.image_assets[name]
    if a == nil then return nil, "missing" end
    if a.pending then return nil, "pending" end
    local r = by_name[name]
    if r == nil then r = new_ref(a); by_name[name] = r end
    return r, "ready"
  end
  api.assets.image_size = function(ref) local i = MOCK.images[ref]; if i then return i.w, i.h end return nil end
  api.assets.image_pixels = function(ref) local i = MOCK.images[ref]; return i and i.px end
  api.assets.image_compose = function(name, w, h, px)
    assert(#px == w * h, "image_compose needs exactly w*h pixels")
    for i = 1, #px do assert(math.type(px[i]) == "integer", "image_compose pixel is not an integer") end
    MOCK.composes = MOCK.composes + 1
    return new_ref({ w = w, h = h, px = px, name = name }), "ready"
  end
  api.assets.image_release = function(ref) MOCK.released[#MOCK.released + 1] = ref; MOCK.images[ref] = nil end
  -- a live image of one colour: returns its ref
  function MOCK_IMAGE(w, h, argb)
    local px = {}
    for i = 1, w * h do px[i] = argb end
    return new_ref({ w = w, h = h, px = px })
  end
end

-- ------------------------------------------------------------------ panel (ui)
api.panel = {}
-- The page host and Porcelain's row model, as far as ui/panel.lua uses them,
-- with the host's refusals: a row string over 191 bytes or a key over 31 is
-- dropped, a duplicate key is dropped, more than 48 rows poisons the run.
MOCK.panel_text = {}        -- api.panel.set_text calls: [id] = text
api.panel.set_text = function(id, text) MOCK.panel_text[id] = text; return true, "ok" end
api.panel.invalidate = function() MOCK.panel_invalidated = (MOCK.panel_invalidated or 0) + 1 end
MOCK_PORCELAIN = { describe_fn = nil, invalidated = 0, rows = {}, by_key = {}, refused = {}, poisoned = false, scroll = 0 }
api.porcelain = api.porcelain or {}
do
  local P, MP = api.porcelain, MOCK_PORCELAIN
  P.open = P.open or function() return true end
  P.panel = P.panel or function(desc) MP.descriptor = desc end
  P.describe = P.describe or function(fn) MP.describe_fn = fn end
  P.invalidate = P.invalidate or function() MP.invalidated = MP.invalidated + 1 end
  P.panel_scroll_to = P.panel_scroll_to or function(s) MP.scroll = s end
  -- -1 while no page of the plugin is up (MP.page_down = true)
  P.panel_scroll = P.panel_scroll or function() if MP.page_down then return -1 end return MP.scroll or 0 end
  P.panel_build = P.panel_build or function(view) MOCK_PORCELAIN_RUN() end
  P.panel_action = P.panel_action or function(ev)
    local r = MP.by_key[ev.id]
    if r == nil then return false end
    local h = ev.action == "menu" and r.on_menu or r.on_action
    if h then
      h(ev.id, { key = ev.id, action = ev.action, value = ev.value or 0, on = (ev.value or 0) ~= 0,
        text = ev.text or "", x = ev.x or 0, y = ev.y or 0 })
    end
    return true
  end
  -- The real verb takes the node only (the draw is the callback's); a test
  -- passes the draw it wants recorded.
  P.panel_draw = P.panel_draw or function(node, draw)
    local r = MP.by_key[node]
    if r == nil or r.paint == nil then return false end
    r.paint(node, draw or MOCK_PANEL_DRAW())
    return true
  end
end

-- Run the registered describe function the way the layer does: collect the
-- rows, apply the host's refusals, keep the result in MOCK_PORCELAIN.rows.
function MOCK_PORCELAIN_RUN()
  local MP = MOCK_PORCELAIN
  local rows, by_key, refused = {}, {}, {}
  MP.poisoned = false
  local d = { row = function(r)
    assert(type(r.key) == "string" and r.key ~= "", "a row needs a key")
    assert(r.kind, "a row needs a kind")
    if #rows >= 48 then MP.poisoned = true; return end
    if #r.key > 31 or by_key[r.key] or #(r.text or "") > 191 or #(r.label or "") > 191 then
      refused[#refused + 1] = r.key
      return
    end
    for _, o in ipairs(r.options or {}) do
      if #(o.value or "") > 191 or #(o.label or "") > 191 then refused[#refused + 1] = r.key; return end
    end
    if r.kind == "custom" and r.height then
      -- -1 is TORIRS_PANEL_CUSTOM_HEIGHT_COLLAPSED (a declared, hidden well)
      assert(r.height == -1 or (r.height >= 48 and r.height <= 2048), "custom height out of range: " .. r.key .. " " .. r.height)
    end
    rows[#rows + 1] = r
    by_key[r.key] = r
  end }
  assert(MP.describe_fn, "nothing registered a describe function")
  MP.describe_fn(d)
  MP.rows, MP.by_key, MP.refused = rows, by_key, refused
  return rows
end

-- A panel well's draw object: records into MOCK.panel_draws.
function MOCK_PANEL_DRAW(width)
  local d = {}
  MOCK.panel_draws = {}
  local function rec(name) return function(...) MOCK.panel_draws[#MOCK.panel_draws + 1] = { name, ... }; return true end end
  for _, n in ipairs({ "rect", "line", "text", "image", "image_clip" }) do d[n] = rec(n) end
  d.text_width = function(s) return #s * 6 end
  d.text_height = function() return 12 end
  d.context = function() return { bounds = { x = 0, y = 0, width = width or 296, height = 2048 }, clip = { x = 0, y = 0, width = width or 296, height = 2048 } } end
  return d
end

return api
