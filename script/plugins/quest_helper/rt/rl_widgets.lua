-- net.runelite.api.widgets for the Quest Helper port: Widget over the
-- client's api.widgets, JavaScriptCallback, ScriptEvent, the widget constant
-- classes, ChatboxPanelManager/ChatboxInput, BankSearch and the
-- Grand Exchange search hooks. docs/QUEST_HELPER_PORT.md is the contract.
--
-- Two kinds of Widget:
--
--   native  a game component (api.widgets.get, or a child of one). Reads go
--           through widget:detail(), cached for one frame; writes map onto the
--           host's retained edits (set_hidden, set_native_text,
--           set_native_text_color, set_sprite, set_item, set_position,
--           set_size). A write the host has no edit for is kept on the Lua
--           side only, so the Java that set it reads it back.
--   owned   a child made by createChild. Its properties live here; the host
--           control (create_text / create_image / create_item on the native
--           parent) is made at the first revalidate(), when the type and
--           content are known, and re-made if they change what it must be.
--
-- Wrappers are interned by (component id, child index), so a Widget read
-- twice is the same object: HashMap<Widget, ...> and == work as in Java.
--
-- Exports (J.rlw) the rest of the RuneLite layer and the plugin entry call:
--   get(component_id), get_child(group, child)    Client.getWidget
--   from_menu(component_id, slot)                  MenuEntry/MenuOptionClicked.getWidget
--   wrap(handle), also J.rl_widget(handle)          any api.widgets handle
--   script_event(fields)                           a ScriptEvent (ScriptPreFired etc.)
--   on_key(ev) -> consumed                         entry's on_key
--   on_menu_select(component_id, slot, op) -> consumed   entry's on_menu_select
--   on_interface(ev)                               entry's on_interface
--   on_screen_changed(screen)                      entry's on_screen_changed
--   invalidate()                                   after native scripts ran mid-frame
--   on_stop()                                      plugin shutdown
--   ge_searched(event_bus) -> consumed, result     the GE search hook
--   ge_set_index/ge_set_count/ge_set_ids           Client.setGeSearchResult*
--   on_script_pre(script_id)                       before the entry posts ScriptPreFired:
--                                                  a script that rebuilds a container drops
--                                                  this plugin's edits on its children
--   menu_retarget(entry) -> consumed               after MenuOptionClicked: a native CC_OP
--                                                  row whose param0/param1 a subscriber moved
--                                                  is pressed on THAT widget instead

local J, C = J, C
local function api() return QH.api end

local rlw = J.rlw or {}
J.rlw = rlw

local WIDGET = "net.runelite.api.widgets.Widget"
local NATIVE, OWNED = 1, 2

-- WidgetType
local T_LAYER, T_RECTANGLE, T_TEXT, T_GRAPHIC, T_MODEL = 0, 3, 4, 5, 6

local function log(s) if J.log then J.log("rl_widgets: " .. s) end end

-- A host answer. invalid_argument / wrong_context are this file passing the
-- host something it must not; anything else (stale, unavailable, budget,
-- native_blocked) is a runtime state the next frame may not have.
local function host(ok, why, what)
  if ok then return true end
  assert(why ~= "invalid_argument", what)
  assert(why ~= "wrong_context", what)
  return false
end

local function rgb(c) return c & 0xFFFFFF end

-- ------------------------------------------------------------------ caches

local wrappers = {}      -- intern key -> native Widget
local epoch = 0          -- bumped by invalidate()
local owned_serial = 0
local owned_live = {}    -- owned Widget -> true while it has a host control
local key_listeners = {} -- Widget -> true (a JavaScriptCallback onKey)
local native_edited = {} -- native Widget -> true once this file wrote to it (see on_script_pre)
-- native layer -> {width, height}: the GAME's scroll size before this file
-- first wrote one (the bank tab re-lays Bankmain.ITEMS taller). Put back at
-- on_stop; forgotten when the game's own rebuild writes it again (505).
local scroll_saved = {}
local warned = {}

local function warn_once(k, s)
  if warned[k] then return end
  warned[k] = true
  log(s)
end

local function frame_ms()
  return api().core.frame_ms()
end

function rlw.invalidate()
  epoch = epoch + 1
end

-- Defaults RuneLite's getters return for a widget with nothing of that kind.
local DEFAULTS = {
  id = -1, index = -1, type = 0, content_type = 0, item_id = -1, item_quantity = 0,
  sprite_id = -1, model_id = -1, model_type = 0, animation_id = -1, text = "",
  text_color = 0, font_id = -1, name = "", hidden = true, self_hidden = true,
  x = 0, y = 0, width = 0, height = 0, original_x = 0, original_y = 0,
  original_width = 0, original_height = 0, relative_x = 0, relative_y = 0,
  scroll_x = 0, scroll_y = 0, scroll_width = 0, scroll_height = 0, opacity = 0,
  parent_id = -1,
}

local function detail(w)
  local ms = frame_ms()
  if w._det_ms ~= ms or w._det_ep ~= epoch then
    w._det = w._h and w._h:detail() or nil
    w._det_ms, w._det_ep = ms, epoch
  end
  return w._det
end

-- The value a native widget answers for `name`: what this file set, else
-- what the host says, else RuneLite's default.
local function nprop(w, name)
  local s = w._sh[name]
  if s ~= nil then return s end
  local d = detail(w)
  local v = d and d[name]
  if v == nil then return DEFAULTS[name] end
  return v
end

local function oprop(w, name)
  local v = w._p[name]
  if v == nil then return DEFAULTS[name] end
  return v
end

local function prop(w, name)
  if w._k == OWNED then return oprop(w, name) end
  return nprop(w, name)
end

-- ------------------------------------------------------------------ interning

local function forget_owned(w)
  if w._owned then
    for _, c in pairs(w._owned) do
      if c._h then c._h:remove() end
      c._h, c._ctl = nil, nil
      c._dead = true
      owned_live[c] = nil
      key_listeners[c] = nil
    end
  end
  w._owned = nil
  w._drop_natives = nil
end

-- A native wrapper for `handle` under `key`. A fresh handle for the same key
-- is checked against the incarnation the wrapper last saw: a different one is
-- a new node in the same slot (a dialogue page rebuilt by CS2), whose state
-- is not the old node's.
local function intern(handle, key)
  local w = wrappers[key]
  if w == nil then
    w = setmetatable({ _k = NATIVE, _key = key, _sh = {} }, C[WIDGET])
    wrappers[key] = w
  end
  if w._h ~= handle then
    local st = handle:state()
    local inc = st and st.incarnation
    if w._inc ~= inc then
      if w._inc ~= nil then
        w._sh = {}
        w._lis = nil
        key_listeners[w] = nil
        forget_owned(w)
      end
      w._inc = inc
    end
    w._h = handle
    w._det, w._det_ms = nil, nil
  end
  return w
end

local function static_key(component_id) return component_id .. ":-1" end

function rlw.get(component_id)
  assert(math.type(component_id) == "integer")
  local h = api().widgets.get(component_id)
  if h == nil then return nil end
  return intern(h, static_key(component_id))
end

J.rl_widget = function(handle) return rlw.wrap(handle) end

function rlw.get_child(group, child)
  return rlw.get((group << 16) | child)
end

-- Any handle the host gave out: keyed by what it says it is. Also
-- J.rl_widget, the name rl_client.lua calls for Client.getWidget and
-- MenuEntry.getWidget.
function rlw.wrap(handle)
  assert(handle)
  local d = handle:detail()
  if d == nil then return nil end
  local w = intern(handle, d.id .. ":" .. (d.index or -1))
  w._det, w._det_ms, w._det_ep = d, frame_ms(), epoch
  return w
end

-- ------------------------------------------------------------------ children

-- The native dynamic children, read once a frame (and again after invalidate):
-- the bank's item list has ~1400, and QuestBankTab asks getChild(i) for each
-- of them, which must not rebuild the list every time.
local function natives_of(w)
  if w._k ~= NATIVE or w._h == nil or w._drop_natives then return {} end
  local ms = frame_ms()
  if w._nat ~= nil and w._nat_ms == ms and w._nat_ep == epoch then return w._nat end
  local hs = w._h:dynamic_children()
  local out = {}
  if hs ~= nil then
    local id = nprop(w, "id")
    for i, h in ipairs(hs) do out[i] = intern(h, id .. ":" .. (i - 1)) end
  end
  w._nat, w._nat_ms, w._nat_ep = out, ms, epoch
  return out
end

-- RuneLite getChildren(): the dynamic children with this plugin's own
-- children at their indices (createChild at an index replaces), nil when the
-- widget has none at all.
local function children_of(w)
  local nat = natives_of(w)
  local n = #nat
  local own = w._owned
  if own then for i in pairs(own) do if i + 1 > n then n = i + 1 end end end
  if n == 0 then return nil end
  local arr = { n = n }
  for i = 1, #nat do arr[i] = nat[i] end
  if own then for i, c in pairs(own) do arr[i + 1] = c end end
  return arr
end

local function wrap_list(hs)
  local arr = { n = 0 }
  if hs == nil then return arr end
  for _, h in ipairs(hs) do
    local w = rlw.wrap(h)
    if w then arr.n = arr.n + 1; arr[arr.n] = w end
  end
  return arr
end

-- ------------------------------------------------------------------ owned controls

local function owned_kind(p)
  if p.type == T_TEXT then return "text" end
  if p.type == T_MODEL then return "model" end
  if (p.item_id or -1) >= 0 then return "item" end
  if p.type == T_GRAPHIC then return "image" end
  if p.type == T_RECTANGLE then return "rect" end
  return nil -- LAYER, LINE: nothing the host can draw for us
end

-- WidgetModelType -> what set_model shows: an item model when the widget
-- carries an item (QuestCompletedWidget's icon), an npc chathead (2), the
-- local player's chathead (3), else a cache model.
local MT_NPC_CHATHEAD, MT_LOCAL_PLAYER_CHATHEAD = 2, 3
local function model_of(p)
  if (p.item_id or -1) >= 0 then return "obj", p.item_id end
  local mt = p.model_type or 0
  if mt == MT_LOCAL_PLAYER_CHATHEAD then return "player", nil end
  if (p.model_id or -1) < 0 then return "none", nil end
  if mt == MT_NPC_CHATHEAD then return "npc", p.model_id end
  return "model", p.model_id
end

local function fire(listener, ev)
  if listener == nil then return false end
  local cb = listener[1]
  if type(cb) == "table" and cb.run then
    cb:run(ev)
    return true
  end
  return false
end

local function listener_of(w, field)
  if w._k == OWNED then return w._p[field] end
  return w._lis and w._lis[field]
end

local function owned_op(w)
  return function(_, hev)
    local ev = rlw.script_event({ source = w, op = hev.operation })
    fire(w._p.on_op, ev)
    fire(w._p.on_click, ev)
  end
end

-- Arm the host's menu rows: RuneLite action index i is op i + 1. A widget
-- with only a click listener gets op 1 "Select" so the click can reach it.
local function arm_ops(w)
  local h = w._h
  if h == nil then return end
  local want = {}
  local acts = w._p.actions
  if acts then for i, label in pairs(acts) do if label ~= nil and label ~= "" then want[i + 1] = label end end end
  if next(want) == nil and w._p.on_click and type(w._p.on_click[1]) == "table" then want[1] = "Select" end
  w._armed = w._armed or {}
  for op in pairs(w._armed) do
    if want[op] == nil then host(h:set_on_op(op, nil, nil)); w._armed[op] = nil end
  end
  local cb = w._opcb or owned_op(w)
  w._opcb = cb
  for op, label in pairs(want) do
    if w._armed[op] ~= label then
      host(h:set_on_op(op, label, cb))
      w._armed[op] = label
    end
  end
end

local function parent_size(w)
  local p = w._parent
  return prop(p, "width"), prop(p, "height")
end

-- RuneLite's revalidate: original geometry through the position and size modes.
local function layout(w)
  local p = w._p
  local pw, ph = parent_size(w)
  local ow, oh = p.original_width or 0, p.original_height or 0
  local wm, hm = p.width_mode or 0, p.height_mode or 0
  local width, height = ow, oh
  if wm == 1 then width = pw - ow elseif wm == 2 then width = (ow * pw) >> 14 end
  if hm == 1 then height = ph - oh elseif hm == 2 then height = (oh * ph) >> 14 end
  local ox, oy = p.original_x or 0, p.original_y or 0
  local xm, ym = p.x_mode or 0, p.y_mode or 0
  local x, y = ox, oy
  if xm == 1 then x = (pw - width) // 2 + ox
  elseif xm == 2 then x = pw - width - ox
  elseif xm == 3 then x = (ox * pw) >> 14
  elseif xm == 4 then x = (pw - width) // 2 + ((ox * pw) >> 14)
  elseif xm == 5 then x = pw - width - ((ox * pw) >> 14) end
  if ym == 1 then y = (ph - height) // 2 + oy
  elseif ym == 2 then y = ph - height - oy
  elseif ym == 3 then y = (oy * ph) >> 14
  elseif ym == 4 then y = (ph - height) // 2 + ((oy * ph) >> 14)
  elseif ym == 5 then y = ph - height - ((oy * ph) >> 14) end
  p.relative_x, p.relative_y, p.width, p.height = x, y, width, height
  return x, y, width, height
end

local function apply_owned(w)
  local h, p = w._h, w._p
  if h == nil then return end
  local x, y, width, height = layout(w)
  host(h:set_size(width, height))
  host(h:set_position(x, y))
  if w._ctl == "text" then
    host(h:set_text(p.text or ""))
    host(h:set_text_color(rgb(p.text_color or 0)))
    host(h:set_text_align(p.x_align or 0, p.y_align or 0))
  elseif w._ctl == "image" then
    if (p.sprite_id or -1) >= 0 then host(h:set_sprite(p.sprite_id)) end
  elseif w._ctl == "item" then
    host(h:set_item(p.item_id, p.item_quantity or 0))
  elseif w._ctl == "rect" then
    host(h:set_rect(rgb(p.text_color or 0), p.filled == true))
  elseif w._ctl == "model" then
    local kind, id = model_of(p)
    host(h:set_model(kind, id))
    host(h:set_model_pose(p.rotation_x or 0, p.rotation_y or 0, p.rotation_z or 0, p.model_zoom or 0))
    host(h:set_model_anim(p.animation_id or -1))
  end
  -- RuneLite opacity is transparency (0 opaque); the host's is alpha.
  if (p.opacity or 0) ~= 0 then host(h:set_opacity(255 - p.opacity)) end
  host(h:set_hidden(p.hidden == true))
  arm_ops(w)
end

local function realize(w)
  if w._dead then return end
  local kind = owned_kind(w._p)
  if kind ~= w._ctl then
    if w._h then w._h:remove() end
    w._h, w._ctl, w._armed = nil, nil, nil
    owned_live[w] = nil
    local ph = w._parent._h
    if kind and ph then
      local key = w._ckey
      local h
      if kind == "text" then h = ph:create_text(key)
      elseif kind == "image" then h = ph:create_image(key)
      elseif kind == "rect" then h = ph:create_rect(key)
      elseif kind == "model" then h = ph:create_model(key)
      else h = ph:create_item(key) end
      if h == nil then
        warn_once("cap", "the host refused an owned " .. kind .. " control (owned widget limit?); further refusals are not logged")
      else
        w._h, w._ctl = h, kind
        owned_live[w] = true
      end
    elseif kind == nil then
      warn_once("type" .. tostring(w._p.type), "owned widget type " .. tostring(w._p.type) .. " has no host control; it is kept Lua-side only")
    end
  end
  apply_owned(w)
end

-- An owned setter that applies at once when the control exists.
local function live(w) return w._k == OWNED and w._h ~= nil end

-- ------------------------------------------------------------------ Widget

local function rect(x, y, width, height)
  return C["java.awt.Rectangle"].__new__int_int_int_int(x, y, width, height)
end

local function canvas_bounds(w)
  if w._h then
    local b = w._h:bounds()
    if b then return b.x, b.y, b.width, b.height end
  end
  if w._k == OWNED then
    local px, py = canvas_bounds(w._parent)
    local x, y, width, height = layout(w)
    return px + x, py + y, width, height
  end
  return -1, -1, 0, 0
end

J.def("net.runelite.api.widgets.JavaScriptCallback", { kind = "interface", super = "java.lang.Object", ifaces = {}, body = function(cls)
  -- the single abstract method: every lambda and implementor brings its own
  function cls.run(self, ev) error("JavaScriptCallback.run is abstract", 2) end
end })

J.def(WIDGET, { kind = "class", super = "java.lang.Object", ifaces = {}, body = function(cls)
  -- reads
  function cls.getId(self)
    if self._k == OWNED then return self._parent:getId() end
    return nprop(self, "id")
  end
  function cls.getIndex(self)
    if self._k == OWNED then return self._index end
    return nprop(self, "index")
  end
  function cls.getType(self) return prop(self, "type") end
  function cls.getContentType(self) return prop(self, "content_type") end
  function cls.getText(self) return prop(self, "text") end
  function cls.getTextColor(self) return prop(self, "text_color") end
  function cls.getFontId(self) return prop(self, "font_id") end
  function cls.getName(self) return prop(self, "name") end
  function cls.getItemId(self) return prop(self, "item_id") end
  function cls.getItemQuantity(self) return prop(self, "item_quantity") end
  function cls.getItemQuantityMode(self) return prop(self, "item_quantity_mode") or 2 end
  function cls.getSpriteId(self) return prop(self, "sprite_id") end
  function cls.getModelId(self) return prop(self, "model_id") end
  function cls.getModelType(self) return prop(self, "model_type") end
  function cls.getAnimationId(self) return prop(self, "animation_id") end
  function cls.getRotationX(self) return prop(self, "rotation_x") or 0 end
  function cls.getRotationY(self) return prop(self, "rotation_y") or 0 end
  function cls.getRotationZ(self) return prop(self, "rotation_z") or 0 end
  function cls.getModelZoom(self) return prop(self, "model_zoom") or 0 end
  function cls.getOpacity(self) return prop(self, "opacity") end
  function cls.getWidth(self) if self._k == OWNED then layout(self) end return prop(self, "width") end
  function cls.getHeight(self) if self._k == OWNED then layout(self) end return prop(self, "height") end
  function cls.getOriginalX(self) return prop(self, "original_x") end
  function cls.getOriginalY(self) return prop(self, "original_y") end
  function cls.getOriginalWidth(self) return prop(self, "original_width") end
  function cls.getOriginalHeight(self) return prop(self, "original_height") end
  function cls.getRelativeX(self) if self._k == OWNED then layout(self) end return prop(self, "relative_x") end
  function cls.getRelativeY(self) if self._k == OWNED then layout(self) end return prop(self, "relative_y") end
  function cls.getScrollX(self) return prop(self, "scroll_x") end
  function cls.getScrollY(self) return prop(self, "scroll_y") end
  function cls.getScrollWidth(self) return prop(self, "scroll_width") end
  function cls.getScrollHeight(self) return prop(self, "scroll_height") end
  function cls.getXPositionMode(self) return prop(self, "x_mode") or 0 end
  function cls.getYPositionMode(self) return prop(self, "y_mode") or 0 end
  function cls.getWidthMode(self) return prop(self, "width_mode") or 0 end
  function cls.getHeightMode(self) return prop(self, "height_mode") or 0 end
  function cls.getXTextAlignment(self) return prop(self, "x_align") or 0 end
  function cls.getYTextAlignment(self) return prop(self, "y_align") or 0 end
  function cls.getTextShadowed(self) return prop(self, "text_shadowed") == true end
  function cls.isFilled(self) return prop(self, "filled") == true end
  function cls.getDragDeadTime(self) return prop(self, "drag_dead_time") or 0 end
  function cls.getNoClickThrough(self) return prop(self, "no_click_through") == true end
  function cls.hasListener(self) return prop(self, "has_listener") == true end

  function cls.getActions(self)
    local acts = (self._k == OWNED and self._p.actions) or (self._k == NATIVE and self._sh.actions)
    if acts == nil and self._k == NATIVE then
      local d = detail(self)
      local a = d and d.actions
      if a then
        local arr = { n = #a }
        for i = 1, #a do arr[i] = a[i] end
        return arr
      end
      return nil
    end
    if acts == nil then return nil end
    local n = 0
    for i in pairs(acts) do if i + 1 > n then n = i + 1 end end
    local arr = { n = n }
    for i, v in pairs(acts) do arr[i + 1] = v end
    return arr
  end

  function cls.isSelfHidden(self)
    if self._k == OWNED then return self._p.hidden == true end
    if self._sh.hidden then return true end
    return nprop(self, "self_hidden")
  end
  function cls.isHidden(self)
    if self._k == OWNED then
      return self._p.hidden == true or self._dead == true or self._parent:isHidden()
    end
    if self._sh.hidden then return true end
    return nprop(self, "hidden")
  end

  function cls.getParent(self)
    if self._k == OWNED then return self._parent end
    local idx = nprop(self, "index")
    if idx >= 0 then return rlw.get(nprop(self, "id")) end
    local pid = nprop(self, "parent_id")
    if pid < 0 then return nil end
    return rlw.get(pid)
  end
  function cls.getParentId(self)
    local p = self:getParent()
    return p and p:getId() or -1
  end

  function cls.getChildren(self) return children_of(self) end
  function cls.getChild(self, index)
    if index < 0 then return nil end
    local own = self._owned and self._owned[index]
    if own ~= nil then return own end
    return natives_of(self)[index + 1]
  end
  function cls.getDynamicChildren(self)
    local arr = children_of(self)
    local out = { n = 0 }
    if arr then for i = 1, arr.n do if arr[i] ~= nil then out.n = out.n + 1; out[out.n] = arr[i] end end end
    return out
  end
  -- Read once a frame (and again after invalidate), as natives_of is:
  -- WidgetTextRequirement's getChildren indexes getStaticChildren()[i] inside
  -- its loop over the same array, which RuneLite answers from the widget's
  -- own array and which here was a host read and a detail() per child every
  -- time -- quadratic. Pirate's Treasure's five journal requirements over the
  -- quest journal's text layer ran on_server_tick out of its 20M budget and
  -- the plugin was disabled (live, 2026-11-19).
  local function frame_list(w, slot, read)
    local ms, h = frame_ms(), w._h
    local c = w[slot]
    if c ~= nil and c.ms == ms and c.ep == epoch and c.h == h then return c.arr end
    local arr = wrap_list(read(h))
    w[slot] = { ms = ms, ep = epoch, h = h, arr = arr }
    return arr
  end
  local function read_static(h) return h:static_children() end
  local function read_nested(h) return h:nested_children() end
  function cls.getStaticChildren(self)
    if self._k == OWNED or self._h == nil then return { n = 0 } end
    return frame_list(self, "_stat", read_static)
  end
  function cls.getNestedChildren(self)
    if self._k == OWNED or self._h == nil then return { n = 0 } end
    return frame_list(self, "_nest", read_nested)
  end

  function cls.getBounds(self)
    local x, y, width, height = canvas_bounds(self)
    return rect(x, y, width, height)
  end
  function cls.getCanvasLocation(self)
    local x, y = canvas_bounds(self)
    return C["net.runelite.api.Point"].__new(x, y)
  end
  function cls.contains(self, point)
    assert(point)
    local x, y, width, height = canvas_bounds(self)
    local px, py = point:getX(), point:getY()
    return px >= x and py >= y and px < x + width and py < y + height
  end

  -- listeners: Object[] is either {JavaScriptCallback} or CS2 script args
  local LISTENERS = {
    setOnOpListener = "on_op", setOnClickListener = "on_click", setOnKeyListener = "on_key",
    setOnMouseOverListener = "on_mouse_over", setOnMouseLeaveListener = "on_mouse_leave",
    setOnMouseRepeatListener = "on_mouse_repeat", setOnTimerListener = "on_timer",
    setOnDragListener = "on_drag", setOnDragCompleteListener = "on_drag_complete",
    setOnHoldListener = "on_hold", setOnReleaseListener = "on_release",
    setOnTargetEnterListener = "on_target_enter", setOnTargetLeaveListener = "on_target_leave",
    setOnVarTransmitListener = "on_var_transmit", setOnInvTransmitListener = "on_inv_transmit",
    setOnStatTransmitListener = "on_stat_transmit", setOnDialogAbortListener = "on_dialog_abort",
    setOnScrollWheelListener = "on_scroll_wheel", setOnLoadListener = "on_load",
  }
  for jname, field in pairs(LISTENERS) do
    cls[jname] = function(self, args)
      if self._k == OWNED then
        self._p[field] = args
      else
        self._lis = self._lis or {}
        self._lis[field] = args
        native_edited[self] = true
      end
      if field == "on_key" then
        key_listeners[self] = (args ~= nil and type(args[1]) == "table" and args[1].run) and true or nil
      end
      if live(self) and (field == "on_op" or field == "on_click") then arm_ops(self) end
    end
  end

  -- The host's native CS2 listener as RuneLite's Object[] (script id first),
  -- or what this file was given.
  local function native_listener(self, field, host_name)
    if self._k == OWNED then return self._p[field] end
    if self._lis and self._lis[field] ~= nil then return self._lis[field] end
    if self._h == nil then return nil end
    local a = self._h:listener(host_name)
    if a == nil then return nil end
    local arr = { n = #a }
    for i = 1, #a do arr[i] = a[i] end
    return arr
  end
  function cls.getOnKeyListener(self) return native_listener(self, "on_key", "onKey") end
  function cls.getOnOpListener(self) return native_listener(self, "on_op", "onOp") end
  function cls.getOnInvTransmitListener(self) return native_listener(self, "on_inv_transmit", "onInvTransmit") end
  function cls.getOnVarTransmitListener(self) return native_listener(self, "on_var_transmit", "onVarTransmit") end
  function cls.getOnLoadListener(self) return native_listener(self, "on_load", "onLoad") end
  function cls.getOnTimerListener(self) return native_listener(self, "on_timer", "onTimer") end

  -- writes
  local function set_field(self, field, v)
    if self._k == OWNED then self._p[field] = v
    else
      self._sh[field] = v
      native_edited[self] = true
    end
  end
  local SHADOW_ONLY = {
    setFontId = "font_id", setName = "name", setModelId = "model_id", setModelType = "model_type",
    setAnimationId = "animation_id", setRotationX = "rotation_x", setRotationY = "rotation_y",
    setRotationZ = "rotation_z", setModelZoom = "model_zoom", setItemQuantityMode = "item_quantity_mode",
    setXPositionMode = "x_mode", setYPositionMode = "y_mode", setWidthMode = "width_mode",
    setHeightMode = "height_mode", setXTextAlignment = "x_align", setYTextAlignment = "y_align",
    setTextShadowed = "text_shadowed", setDragDeadTime = "drag_dead_time", setFilled = "filled",
    setNoClickThrough = "no_click_through", setHasListener = "has_listener",
    setScrollX = "scroll_x", setScrollY = "scroll_y", setContentType = "content_type",
    setOriginalX = "original_x", setOriginalY = "original_y",
    setOriginalWidth = "original_width", setOriginalHeight = "original_height",
    setType = "type",
  }
  for jname, field in pairs(SHADOW_ONLY) do
    cls[jname] = function(self, v) set_field(self, field, v) end
  end
  local GEOMETRY = { original_x = true, original_y = true, original_width = true, original_height = true }
  for jname, field in pairs(SHADOW_ONLY) do
    if GEOMETRY[field] then
      cls[jname] = function(self, v)
        set_field(self, field, v)
        if self._k == NATIVE then self._geo = true end
      end
    end
  end
  -- A native layer's scroll size goes to the host (a plain write the game's
  -- next rebuild replaces, as RuneLite's field write is).
  local function scroll_size(self)
    if self._k ~= NATIVE or self._h == nil then return end
    host(self._h:set_scroll_size(nprop(self, "scroll_width"), nprop(self, "scroll_height")))
  end
  local function save_scroll(self)
    if self._k ~= NATIVE or self._h == nil or scroll_saved[self] ~= nil then return end
    local d = detail(self)
    if d == nil or d.scroll_width == nil or d.scroll_height == nil then return end
    scroll_saved[self] = { d.scroll_width, d.scroll_height }
  end
  function cls.setScrollHeight(self, v)
    save_scroll(self)
    set_field(self, "scroll_height", v)
    scroll_size(self)
    if self._k == NATIVE then self._sh.scroll_height = nil; self._det_ms = nil end
  end
  function cls.setScrollWidth(self, v)
    save_scroll(self)
    set_field(self, "scroll_width", v)
    scroll_size(self)
    if self._k == NATIVE then self._sh.scroll_width = nil; self._det_ms = nil end
  end
  function cls.setXTextAlignment(self, v)
    set_field(self, "x_align", v)
    if live(self) and self._ctl == "text" then host(self._h:set_text_align(v, self._p.y_align or 0)) end
  end
  function cls.setYTextAlignment(self, v)
    set_field(self, "y_align", v)
    if live(self) and self._ctl == "text" then host(self._h:set_text_align(self._p.x_align or 0, v)) end
  end

  function cls.setHidden(self, hidden)
    if self._k == OWNED then
      self._p.hidden = hidden
      if self._h then host(self._h:set_hidden(hidden)) end
      return
    end
    -- true is a presentation hide; false hands visibility back to the native
    -- state and, where the GAME hid the node, shows it (RuneLite's setHidden
    -- writes the field: a write-through edit the reset gives back)
    self._sh.hidden = hidden or nil
    native_edited[self] = true
    if self._h then
      host(self._h:set_hidden(hidden))
      if not hidden then
        local d = detail(self)
        if d and d.self_hidden then
          host(self._h:set_native_hidden(false))
          self._det_ms = nil
        end
      end
    end
  end

  function cls.setText(self, text)
    assert(text)
    set_field(self, "text", text)
    if self._k == NATIVE then
      if self._h then host(self._h:set_native_text(text)) end
    elseif self._ctl == "text" then
      host(self._h:set_text(text))
    end
  end

  function cls.setTextColor(self, color)
    set_field(self, "text_color", color)
    if self._k == NATIVE then
      if self._h then host(self._h:set_native_text_color(rgb(color))) end
    elseif self._ctl == "text" then
      host(self._h:set_text_color(rgb(color)))
    end
  end

  function cls.setSpriteId(self, sprite)
    set_field(self, "sprite_id", sprite)
    if self._k == NATIVE then
      if self._h and sprite >= 0 then host(self._h:set_sprite(sprite)) end
    elseif self._ctl == "image" and sprite >= 0 then
      host(self._h:set_sprite(sprite))
    end
  end

  local function item_changed(self)
    if self._k == NATIVE then
      if self._h then host(self._h:set_item(nprop(self, "item_id"), nprop(self, "item_quantity"))) end
    elseif self._ctl == "item" then
      host(self._h:set_item(self._p.item_id, self._p.item_quantity or 0))
    elseif self._h then
      realize(self) -- became (or stopped being) an item: a different control
    end
  end
  function cls.setItemId(self, id)
    set_field(self, "item_id", id)
    item_changed(self)
  end
  function cls.setItemQuantity(self, q)
    set_field(self, "item_quantity", q)
    item_changed(self)
  end

  function cls.setOpacity(self, v)
    set_field(self, "opacity", v)
    -- a native widget's is a retained edit the reset gives back
    if live(self) or (self._k == NATIVE and self._h) then host(self._h:set_opacity(255 - v)) end
  end

  function cls.setAction(self, index, label)
    local t = (self._k == OWNED) and self._p or self._sh
    t.actions = t.actions or {}
    t.actions[index] = label
    if live(self) then arm_ops(self) end
  end
  function cls.clearActions(self)
    local t = (self._k == OWNED) and self._p or self._sh
    t.actions = {}
    if live(self) then arm_ops(self) end
  end

  function cls.revalidate(self)
    if self._k == OWNED then
      realize(self)
      return
    end
    if self._geo and self._h then
      self._geo = nil
      host(self._h:set_size(nprop(self, "original_width"), nprop(self, "original_height")))
      host(self._h:set_position(nprop(self, "original_x"), nprop(self, "original_y")))
    end
  end
  function cls.revalidateScroll(self) self:revalidate() end

  function cls.createChild__int_int(self, index, type_)
    assert(self._k == NATIVE) -- the host nests owned controls under game components only
    self._owned = self._owned or {}
    if index < 0 then
      local arr = children_of(self)
      index = arr and arr.n or 0
    end
    local old = self._owned[index]
    if old then
      if old._h then old._h:remove() end
      old._h, old._dead = nil, true
      owned_live[old] = nil
    end
    owned_serial = owned_serial + 1
    local c = setmetatable({
      _k = OWNED, _parent = self, _index = index, _ckey = "qh" .. owned_serial,
      _p = { type = type_ },
    }, C[WIDGET])
    self._owned[index] = c
    return c
  end
  function cls.createChild__int(self, type_) return self:createChild__int_int(-1, type_) end

  local function remove_owned(c)
    if c._h then c._h:remove() end
    c._h, c._ctl, c._dead = nil, nil, true
    owned_live[c] = nil
    key_listeners[c] = nil
  end

  function cls.deleteAllChildren(self)
    if self._owned then for _, c in pairs(self._owned) do remove_owned(c) end end
    self._owned = nil
    -- RuneLite deletes the native dynamic children too; the host can only
    -- hide them (reset() on the parent's children brings them back).
    for _, n in ipairs(natives_of(self)) do n:setHidden(true) end
    self._drop_natives = true
  end

  function cls.setChildren(self, arr)
    local keep = {}
    local own = {}
    if arr then
      for i = 1, arr.n do
        local c = arr[i]
        if c ~= nil and c._k == OWNED and c._parent == self then
          keep[c] = true
          c._index = i - 1
          own[i - 1] = c
        end
      end
    end
    if self._owned then for _, c in pairs(self._owned) do if not keep[c] then remove_owned(c) end end end
    self._owned = next(own) and own or nil
  end

  function cls.toString(self)
    return string.format("Widget(%d,%d)", self:getId(), self:getIndex())
  end
end })

-- ------------------------------------------------------------------ ScriptEvent

J.def("net.runelite.api.ScriptEvent", { kind = "class", super = "java.lang.Object", ifaces = {}, body = function(cls)
  function cls.getSource(self) return self.source end
  function cls.getOp(self) return self.op or 0 end
  function cls.getOpbase(self) return self.opbase or "" end
  function cls.getMouseX(self) return self.mouse_x or 0 end
  function cls.getMouseY(self) return self.mouse_y or 0 end
  function cls.getTypedKeyCode(self) return self.typed_key_code or 0 end
  function cls.getTypedKeyChar(self) return self.typed_key_char or 0 end
  function cls.getArguments(self) return self.arguments end
  function cls.setArguments(self, a) self.arguments = a end
  function cls.setSource(self, w) self.source = w end
  function cls.setOp(self, op) self.op = op end
  function cls.setTypedKeyCode(self, k) self.typed_key_code = k end
  function cls.setTypedKeyChar(self, c) self.typed_key_char = c end
end, clinit = function(cls)
  cls.MOUSE_X = -2147483647
  cls.MOUSE_Y = -2147483646
  cls.WIDGET_ID = -2147483645
  cls.OPCODE = -2147483644
  cls.WIDGET_INDEX = -2147483643
  cls.WIDGET_TARGET_ID = -2147483642
  cls.WIDGET_TARGET_INDEX = -2147483641
  cls.KEY_CODE = -2147483640
  cls.KEY_CHAR = -2147483639
  cls.NAME = "event_opbase"
end })

function rlw.script_event(fields)
  assert(fields)
  local ev = setmetatable({}, C["net.runelite.api.ScriptEvent"])
  for k, v in pairs(fields) do ev[k] = v end
  return ev
end

-- ------------------------------------------------------------------ constants

local function consts(key, t)
  J.def(key, { kind = "class", super = "java.lang.Object", ifaces = {}, clinit = function(cls)
    for k, v in pairs(t) do cls[k] = v end
  end })
end
consts("net.runelite.api.widgets.WidgetType", { LAYER = 0, RECTANGLE = 3, TEXT = 4, GRAPHIC = 5, MODEL = 6,
  TEXT_INVENTORY = 7, IF1_TOOLTIP = 8, LINE = 9, INPUT_FIELD = 12 })
consts("net.runelite.api.widgets.WidgetPositionMode", { ABSOLUTE_LEFT = 0, ABSOLUTE_TOP = 0, ABSOLUTE_CENTER = 1,
  ABSOLUTE_RIGHT = 2, ABSOLUTE_BOTTOM = 2, LEFT_16384THS = 3, TOP_16384THS = 3, CENTER_16384THS = 4,
  RIGHT_16384THS = 5, BOTTOM_16384THS = 5 })
consts("net.runelite.api.widgets.WidgetSizeMode", { ABSOLUTE = 0, MINUS = 1, ABSOLUTE_16384THS = 2 })
consts("net.runelite.api.widgets.WidgetTextAlignment", { LEFT = 0, TOP = 0, CENTER = 1, RIGHT = 2, BOTTOM = 2 })
consts("net.runelite.api.widgets.ItemQuantityMode", { NEVER = 0, ALWAYS = 1, STACKABLE = 2 })
consts("net.runelite.api.widgets.WidgetModelType", { NULL = 0, MODEL = 1, NPC_CHATHEAD = 2,
  LOCAL_PLAYER_CHATHEAD = 3, ITEM = 4, PLAYER = 5, NPC_INDEX_CHATHEAD = 6 })

-- ------------------------------------------------------------------ keys

-- net.runelite.client.input.KeyListener is rl_core.lua's; a ChatboxInput that
-- implements it hears keys through rlw.on_key below.

-- The key event a KeyListener receives (java.awt.event.KeyEvent's getters):
-- key code in AWT VK numbering, char as an integer.
local KeyEventImpl = J.local_class("rt.rl_widgets.KeyEvent", { kind = "class", super = "java.lang.Object", ifaces = {}, body = function(cls)
  function cls.getKeyChar(self) return self.ch end
  function cls.getKeyCode(self) return self.code end
  function cls.consume(self) self.consumed = true end
  function cls.isConsumed(self) return self.consumed == true end
  function cls.isShiftDown(self) return false end
  function cls.isControlDown(self) return false end
  function cls.isAltDown(self) return false end
end })

local TORIRS_KEY_ESCAPE, TORIRS_KEY_SPACE = 37, 45

local function vk_of(ev)
  if ev.key == TORIRS_KEY_ESCAPE then return 27 end
  if ev.key == TORIRS_KEY_SPACE then return 32 end
  local ch = ev.ch or 0
  if ch >= 97 and ch <= 122 then return ch - 32 end -- VK_A.. are upper case
  if (ch >= 48 and ch <= 57) or (ch >= 65 and ch <= 90) or ch == 10 or ch == 8 then return ch end
  return 0
end

local function char_of(ev)
  local ch = ev.ch or 0
  if ch == 0 and ev.key == TORIRS_KEY_ESCAPE then return 27 end
  if ch == 0 and ev.key == TORIRS_KEY_SPACE then return 32 end
  return ch
end

-- Jagex key codes (net.runelite.api.KeyCode), which ScriptEvent.getTypedKeyCode answers.
local JAGEX_ROWS = { [32] = "qwertyuiop", [48] = "asdfghjkl", [64] = "zxcvbnm" }
local JAGEX_CHAR = {}
for base, row in pairs(JAGEX_ROWS) do
  for i = 1, #row do JAGEX_CHAR[row:byte(i)] = base + i - 1 end
end
for d = 1, 9 do JAGEX_CHAR[48 + d] = 15 + d end
JAGEX_CHAR[48] = 25
JAGEX_CHAR[32] = 83
JAGEX_CHAR[27] = 13

local function jagex_of(ev)
  local ch = char_of(ev)
  if ch >= 65 and ch <= 90 then ch = ch + 32 end
  return JAGEX_CHAR[ch] or -1
end

-- ------------------------------------------------------------------ chatbox

local MES_LAYER = 0x00a20027
local MES_TEXT = 0x00a2002b
local MES_TEXT2 = 0x00a2002c
local WORLDMAP_MAPLIST_DISPLAY = 0x0253001a
local VARC_MESLAYERMODE = 5
local VARC_WORLDMAP_SEARCHING = 190
local SCRIPT_MESSAGE_LAYER_OPEN = 677
local SCRIPT_MESSAGE_LAYER_CLOSE = 299
local INPUT_RUNELITE_CHATBOX_PANEL = -3

local chatbox = { current = nil, hidden = {} }

J.def("net.runelite.client.game.chatbox.ChatboxInput", { kind = "class", super = "java.lang.Object", ifaces = {}, body = function(cls)
  function cls.open(self) end
  function cls.close(self) end
end })

local function kill_current()
  local input = chatbox.current
  if input == nil then return end
  chatbox.current = nil
  local ok, err = J.protect(function() input:close() end)
  if not ok then log("closing chatbox input: " .. J.str(err)) end
  local c = rlw.get(MES_LAYER)
  if c then
    if c._owned then for _, w in pairs(c._owned) do if w._h then w._h:remove() end w._h, w._dead = nil, true; owned_live[w] = nil end end
    c._owned, c._drop_natives = nil, nil
  end
  -- hand back what open hid
  for _, w in ipairs(chatbox.hidden) do if w._h then w._h:reset() end w._sh.hidden = nil end
  chatbox.hidden = {}
end

local function hide_for_panel(component_id)
  local w = rlw.get(component_id)
  if w == nil then return end
  w:setHidden(true)
  chatbox.hidden[#chatbox.hidden + 1] = w
end

J.def("net.runelite.client.game.chatbox.ChatboxPanelManager", { kind = "class", super = "java.lang.Object", ifaces = {}, body = function(cls)
  function cls.getCurrentInput(self) return chatbox.current end
  function cls.getContainerWidget(self) return rlw.get(MES_LAYER) end
  function cls.openInput(self, input)
    assert(input)
    api().scripts.run(SCRIPT_MESSAGE_LAYER_OPEN, 0)
    if chatbox.current ~= nil then kill_current() end
    chatbox.current = input
    api().cache.set_varc_int(VARC_MESLAYERMODE, INPUT_RUNELITE_CHATBOX_PANEL)
    hide_for_panel(MES_TEXT)
    hide_for_panel(MES_TEXT2)
    local c = self:getContainerWidget()
    if c then
      c:deleteAllChildren()
      for _, n in ipairs(c._h and c._h:dynamic_children() or {}) do
        chatbox.hidden[#chatbox.hidden + 1] = rlw.wrap(n)
      end
    end
    input:open()
  end
  function cls.close(self)
    api().scripts.run(SCRIPT_MESSAGE_LAYER_CLOSE, 0, 1, 0)
    kill_current()
  end
  function cls.shouldTakeInput(self)
    return rlw.get(WORLDMAP_MAPLIST_DISPLAY) == nil or api().cache.varc_int(VARC_WORLDMAP_SEARCHING) ~= 1
  end
end })

function rlw.on_screen_changed(screen)
  if screen ~= "game" and chatbox.current ~= nil then kill_current() end
end

-- ------------------------------------------------------------------ input fan-in

-- The entry's on_key. A ChatboxInput that is a KeyListener hears every key
-- (pressed, then typed when it has a character; released on key up); a
-- visible widget with a JavaScriptCallback onKey hears key downs as a
-- ScriptEvent. Returns whether anything consumed it.
function rlw.on_key(ev)
  assert(ev)
  local consumed = false
  local input = chatbox.current
  if input ~= nil and J.inst(input, "net.runelite.client.input.KeyListener") then
    local ke = setmetatable({ ch = char_of(ev), code = vk_of(ev) }, KeyEventImpl)
    if ev.down == false then
      input:keyReleased(ke)
    else
      input:keyPressed(ke)
      if ke.ch ~= 0 and chatbox.current == input then input:keyTyped(ke) end
    end
    consumed = ke.consumed == true
  end
  if ev.down ~= false then
    local se
    local ws = {}
    for w in pairs(key_listeners) do ws[#ws + 1] = w end
    for _, w in ipairs(ws) do
      if not w:isHidden() then
        se = se or rlw.script_event({ typed_key_code = jagex_of(ev), typed_key_char = char_of(ev) })
        se.source = w
        fire(listener_of(w, "on_key"), se)
      end
    end
  end
  return consumed
end

-- The widget a menu row names: a component, or its dynamic child `slot`.
function rlw.from_menu(component_id, slot)
  local w = rlw.get(component_id)
  if w == nil or slot == nil or slot < 0 then return w end
  return w:getChild(slot)
end

-- The entry's on_menu_select for a row on a game component: a
-- JavaScriptCallback onOp / onClick the Java put on that native widget runs
-- (the native script still runs too). Owned controls are dispatched by the
-- host through set_on_op and never come here.
function rlw.on_menu_select(component_id, slot, op)
  local w = rlw.from_menu(component_id, slot)
  if w == nil or w._k ~= NATIVE or w._lis == nil then return false end
  local ev = rlw.script_event({ source = w, op = op })
  local a = fire(w._lis.on_op, ev)
  local b = fire(w._lis.on_click, ev)
  return a or b
end

function rlw.on_interface(ev)
  assert(ev)
  for key, w in pairs(wrappers) do
    local id = tonumber(key:match("^(%-?%d+):"))
    if id and (id >> 16) == ev.group then
      forget_owned(w)
      key_listeners[w] = nil
      wrappers[key] = nil
    end
  end
  epoch = epoch + 1
end

function rlw.on_stop()
  native_edited = {}
  -- the scroll size is a plain write no reset undoes: give the game's back
  for w, size in pairs(scroll_saved) do
    if w._h then host(w._h:set_scroll_size(size[1], size[2])) end
  end
  scroll_saved = {}
  if chatbox.current ~= nil then kill_current() end
  for w in pairs(owned_live) do
    if w._h then w._h:remove() end
    w._h, w._dead = nil, true
  end
  owned_live = {}
  for _, w in pairs(wrappers) do
    if w._h and (next(w._sh) ~= nil) then w._h:reset() end
  end
  wrappers = {}
  key_listeners = {}
  epoch = epoch + 1
end

-- ------------------------------------------------------------------ BankSearch

local BANK_ITEMS = 0x000c000c

-- A script run the bank needs: a refusal is said once per reason (the Java
-- has no return value to read it from).
-- RuneLite's runScript never answers "not yet"; this client's does while a
-- script (or a record it reaches) is still loading. The run is then asked for
-- again on each following frame -- the ClientThread's invokeLater, as a
-- BooleanSupplier -- until it runs or is refused for another reason (at most
-- BANK_RUN_TRIES frames).
local BANK_RUN_TRIES = 120
local function bank_run(what, ...)
  local args = table.pack(...)
  local ints, why = api().scripts.run(table.unpack(args, 1, args.n))
  if ints ~= nil then rlw.invalidate(); return true end
  if why == "pending" and QH.core and QH.core.clientThread then
    local tries = 0
    QH.core.clientThread:invokeLater__BooleanSupplier(J.fn("java.util.function.BooleanSupplier", "getAsBoolean", function()
      tries = tries + 1
      local again, why2 = api().scripts.run(table.unpack(args, 1, args.n))
      if again ~= nil then rlw.invalidate(); return true end
      if why2 ~= "pending" or tries >= BANK_RUN_TRIES then
        warn_once("bank:" .. what .. ":" .. tostring(why2), "BankSearch " .. what .. ": scripts.run refused (" .. tostring(why2) .. ")")
        return true
      end
      return false
    end))
    return false
  end
  warn_once("bank:" .. what .. ":" .. tostring(why), "BankSearch " .. what .. ": scripts.run refused (" .. tostring(why) .. ")")
  return false
end

J.def("net.runelite.client.plugins.bank.BankSearch", { kind = "class", super = "java.lang.Object", ifaces = {}, body = function(cls)
  function cls.layoutBank(self)
    local c = rlw.get(BANK_ITEMS)
    if c == nil then return end
    local args = c:getOnInvTransmitListener()
    if args == nil then
      warn_once("bank:no-listener", "BankSearch.layoutBank: Bankmain.ITEMS has no onInvTransmit listener")
      return
    end
    bank_run("layoutBank", table.unpack(args, 1, args.n))
  end
  function cls.reset(self, closeChat)
    if closeChat then
      bank_run("reset", SCRIPT_MESSAGE_LAYER_CLOSE, 1, 1, 0)
    else
      api().cache.set_varc_int(VARC_MESLAYERMODE, 0)
      api().cache.set_varc_str(359, "")
    end
    self:layoutBank()
  end
  function cls.initSearch(self)
    local c = rlw.get(BANK_ITEMS)
    if c == nil or c:isHidden() then return end
    local args = c:getOnInvTransmitListener()
    if args == nil then return end
    self:reset(true)
    -- bankmain_search_toggle takes 1 before the build script's own arguments
    bank_run("initSearch", 281, 1, table.unpack(args, 2, args.n))
  end
end })

-- ------------------------------------------------------------------ script rebuilds

-- Containers a script lays out wholesale, by the script that finishes the
-- job. In RuneLite the script's own writes (position, hide, item, ops)
-- overwrite whatever a plugin set on those children; here the plugin's edits
-- are retained by the host, so they are dropped when the script runs again:
-- at PRE of bankmain_finishbuilding the build has just positioned every slot,
-- and the bank shows the game's layout until a subscriber re-lays it (the
-- Quest Helper tab does, at its POST). Obj and transparency edits the script
-- rewrote are already gone (the host drops them on the game's write).
local REBUILDS = { [505] = { BANK_ITEMS } } -- BANKMAIN_FINISHBUILDING -> Bankmain.ITEMS

function rlw.on_script_pre(script_id)
  local containers = REBUILDS[script_id]
  if containers == nil then return end
  -- the build writes the container's scroll size itself: nothing to restore
  for w in pairs(scroll_saved) do
    for _, component_id in ipairs(containers) do
      if w._key == component_id .. ":-1" then scroll_saved[w] = nil end
    end
  end
  if next(native_edited) == nil then return end
  for _, component_id in ipairs(containers) do
    local prefix = component_id .. ":"
    for w in pairs(native_edited) do
      local key = w._key
      if key:sub(1, #prefix) == prefix and key ~= prefix .. "-1" then
        if w._h then w._h:reset() end
        w._sh, w._lis, w._geo = {}, nil, nil
        w._det, w._det_ms = nil, nil
        key_listeners[w] = nil
        native_edited[w] = nil
      end
    end
  end
  epoch = epoch + 1
end

-- ------------------------------------------------------------------ menu retarget

-- MenuOptionClicked subscribers may move a native CC_OP row onto another
-- widget (QuestBankTab: a laid-out bank slot shows item X, so its Withdraw
-- names the slot that really holds X). The client already chose the widget the
-- row was built on, so the click is consumed and the same option pressed on
-- the widget the row now names -- never the original, whose slot holds some
-- other item.
-- The menu target of a row on one of this plugin's owned controls. RuneLite's
-- CC_OP row names the widget (getMenuTarget() is its setName); the host's row
-- for an owned control carries the label alone, so the entry asks here: the
-- name of the live owned control armed with that label (nil when none, or
-- when two differently named controls share it).
local function trim(str) return (str:gsub("^%s+", ""):gsub("%s+$", "")) end
function rlw.owned_menu_target(option)
  if option == nil then return nil end
  local want = trim(J.str(option))
  local found = nil
  for w in pairs(owned_live) do
    local armed = w._armed
    if armed ~= nil and not w._dead and w._p.hidden ~= true then
      for _, label in pairs(armed) do
        if trim(J.str(label)) == want then
          local name = w._p.name or ""
          if found ~= nil and found ~= name then return nil end
          found = name
        end
      end
    end
  end
  return found
end

local retargeting = false
function rlw.menu_retarget(entry)
  assert(entry)
  local row = entry.row
  if row == nil then return false end
  -- The press below raises the menu select again for the widget it names:
  -- that one is the press itself, never moved a second time.
  if retargeting then return false end
  local MA = C["net.runelite.api.MenuAction"]
  local t = entry.type
  if t ~= MA.CC_OP and t ~= MA.CC_OP_LOW_PRIORITY then return false end
  local p0 = row.param0 or row.slot or -1
  local p1 = row.param1 or row.component_id or -1
  -- The plugin rewrote this native widget's actions (clearActions/setAction:
  -- a laid-out bank slot). The client cannot take a native op away, so the
  -- menu still offers the slot's own ops; one the plugin did not leave on it
  -- would act on the slot's real item, not the one shown. Dropped.
  local own = rlw.from_menu(p1, p0)
  local acts = own ~= nil and own._k == NATIVE and own._sh.actions or nil
  if acts ~= nil then
    local option = entry.option or ""
    local kept = false
    for _, label in pairs(acts) do
      if label ~= nil and label ~= "" and J.str(label):gsub("<[^>]*>", "") == option then kept = true end
    end
    if not kept then
      warn_once("dropped-op:" .. option, "a native widget's op the plugin removed was clicked (" .. option .. "); the click was dropped")
      return true
    end
  end
  if entry.param0 == p0 and entry.param1 == p1 then return false end
  local target = rlw.from_menu(entry.param1, entry.param0)
  local option = entry.option or ""
  if target ~= nil and target._h ~= nil and option ~= "" then
    for _, a in ipairs(target._h:actions() or {}) do
      local label = a.label or ""
      if label == option or label:sub(1, #option + 1) == option .. " " then
        retargeting = true
        local ok, why = api().widgets.invoke(a.ref)
        retargeting = false
        if not ok then warn_once("retarget:" .. tostring(why), "a moved menu row could not be pressed (" .. tostring(why) .. ")") end
        return true
      end
    end
  end
  warn_once("retarget-miss", "a moved menu row names a widget without that option; the click was dropped")
  return true
end

-- ------------------------------------------------------------------ GE search

-- What Client.setGeSearchResult* set during the last GrandExchangeSearched.
rlw.ge = { index = 0, count = 0, ids = nil }
function rlw.ge_set_index(i) rlw.ge.index = i end
function rlw.ge_set_count(n) rlw.ge.count = n end
function rlw.ge_set_ids(ids)
  assert(ids)
  local t = {}
  for i = 1, ids.n do t[i] = ids[i] end
  rlw.ge.ids = t
end

-- Post GrandExchangeSearched (the moment the GE search box's text changes)
-- and say whether a subscriber took it, with the results it set. The caller
-- (the entry, on the GE search script callback) writes them to the CS2 stack.
function rlw.ge_searched(event_bus)
  assert(event_bus)
  rlw.ge = { index = 0, count = 0, ids = nil }
  local ev = C["net.runelite.api.events.GrandExchangeSearched"].__new()
  event_bus:post(ev)
  return ev:isConsumed(), rlw.ge
end

return rlw
