-- rt/rl_widgets.lua under plain Lua, against harness/mock_api.lua, driving
-- the generated framework code that uses it: dialogue option highlighting
-- (steps/choice), the bank's Quest Helper tab button and BankSearch, the
-- added-widget teardown, the RuneLite chatbox panel (ChatBox), native click
-- and key listeners (Mage Arena II) and the GE search hook.
--
--   /opt/homebrew/bin/lua tools/questhelper_port/harness/test_rl_widgets.lua
local HERE = debug.getinfo(1, "S").source:sub(2):match("^(.*)/") or "."
local ROOT = HERE .. "/../../../script/plugins/quest_helper"

local logs = {}
J = { protect = pcall, log = function(s) logs[#logs + 1] = s end, now_ms = function() return 0 end }
for _, f in ipairs({ "j_core", "j_lang", "j_util", "j_stream", "j_regex", "j_misc" }) do dofile(ROOT .. "/rt/" .. f .. ".lua") end
dofile(HERE .. "/mock_api.lua")
QH = { api = MOCK_API }
local rlw = dofile(ROOT .. "/rt/rl_widgets.lua")

-- generated modules on demand; external classes this test does not provide
-- (other agents' rt files) become absorbing stubs, as in run_all.lua
local index = dofile(ROOT .. "/gen/index.lua")
local loaded = {}
local function load_module(m)
  if loaded[m] then return end
  loaded[m] = true
  for _, d in ipairs(index.modules[m] or {}) do load_module(d) end
  dofile(ROOT .. "/gen/" .. m .. ".lua")
end
local stub_mt = {}
stub_mt.__index = function() return function() return setmetatable({}, stub_mt) end end
J.on_missing(function(key)
  local m = index.classes[key]
  if m then load_module(m); return end
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
  return C[key]
end
dofile(ROOT .. "/gen/enums.lua")
for _, m in ipairs(index.framework) do load_module(m) end

-- the geometry classes rl_widgets returns, when their owners' files are absent
if not J.defined("java.awt.Rectangle") then
  J.def("java.awt.Rectangle", { kind = "class", super = "java.lang.Object", ifaces = {}, body = function(cls)
    function cls.__init__int_int_int_int(self, x, y, w, h) self.x, self.y, self.width, self.height = x, y, w, h end
  end })
end
if not J.defined("net.runelite.api.Point") then
  J.def("net.runelite.api.Point", { kind = "class", super = "java.lang.Object", ifaces = {}, body = function(cls)
    function cls.__init(self, x, y) self.x, self.y = x, y end
    function cls.getX(self) return self.x end
    function cls.getY(self) return self.y end
  end })
end

local fails, passes = 0, 0
local function check(name, got, want)
  if got ~= want then
    fails = fails + 1
    print(string.format("FAIL %s: got %s, want %s", name, tostring(got), tostring(want)))
  else
    passes = passes + 1
  end
end
local function calls(name, target)
  local out = {}
  for _, c in ipairs(MOCK.widget_calls) do
    if c[1] == name and (target == nil or c[2] == target) then out[#out + 1] = c end
  end
  return out
end
local function reset_mock()
  MOCK.widgets, MOCK.widget_calls, MOCK.owned, MOCK.scripts_run = {}, {}, {}, {}
  MOCK.varcs_int, MOCK.varcs_str = {}, {}
  rlw.on_stop()
  MOCK.widget_calls = {}
end
local JSC = "net.runelite.api.widgets.JavaScriptCallback"
local function js(f) return J.arr({ J.fn(JSC, "run", function(_, ev) f(ev) end) }, 1) end

local function client_stub()
  local c = { varbits = {}, sounds = {}, menu = {} }
  function c.getWidget__int(_, id) return rlw.get(id) end
  function c.getWidget__int_int(_, g, ch) return rlw.get_child(g, ch) end
  function c.getVarbitValue__int(_, id) return c.varbits[id] or 0 end
  function c.setVarbit(_, id, v) c.varbits[id] = v end
  function c.playSoundEffect__int(_, id) c.sounds[#c.sounds + 1] = id end
  function c.menuAction(_, ...) c.menu[#c.menu + 1] = table.pack(...) end
  function c.setVarcStrValue(_, id, v) MOCK_API.cache.set_varc_str(id, v) end
  function c.setVarcIntValue(_, id, v) MOCK_API.cache.set_varc_int(id, v) end
  function c.getVarcIntValue(_, id) return MOCK_API.cache.varc_int(id) end
  return c
end
local client_thread = {}
function client_thread.invokeLater__Runnable(_, r) r:run() end
function client_thread.invokeAtTickEnd(_, r) r:run() end
function client_thread.invoke__Runnable(_, r) r:run() end

-- ------------------------------------------------------------------ 1. reads, identity, children

local OPTIONS = (219 << 16) | 1
reset_mock()
MOCK.widgets[OPTIONS] = { type = 0, width = 512, height = 130, x = 10, y = 340,
  dynamic = { { type = 4, text = "Select an Option" }, { type = 4, text = "Yes" }, { type = 4, text = "No" } } }
local w = rlw.get_child(219, 1)
check("get same object", rlw.get(OPTIONS), w)
check("getId", w:getId(), OPTIONS)
check("getWidth", w:getWidth(), 512)
local kids = w:getChildren()
check("children n", kids.n, 3)
check("child text", kids[2]:getText(), "Yes")
check("child index", kids[3]:getIndex(), 2)
check("child id is parent's", kids[3]:getId(), OPTIONS)
check("getChild", w:getChild(1), kids[2])
check("child identity across reads", w:getChildren()[2], kids[2])
check("child parent", kids[2]:getParent(), w)
check("getChild out of range", w:getChild(5), nil)
check("dynamic children", w:getDynamicChildren().n, 3)
check("static children empty", w:getStaticChildren().n, 0)
-- WidgetTextRequirement.getChildren indexes getStaticChildren()[i] inside its
-- own loop: one host read a frame, not one per child (live: Pirate's
-- Treasure's journal requirements ran on_server_tick out of its budget).
do
  local JOURNAL = (119 << 16) | 4
  local ids = {}
  for i = 1, 300 do
    local id = (119 << 16) | (100 + i)
    MOCK.widgets[id] = { type = 4, text = "line " .. i }
    ids[i] = id
  end
  MOCK.widgets[JOURNAL] = { type = 0, width = 400, height = 300, static = ids }
  local reads = 0
  local WMT_static = nil
  local layer = rlw.get(JOURNAL)
  local h = layer._h
  local mt = debug.getmetatable(h)
  local idx = mt and mt.__index
  WMT_static = idx.static_children
  idx.static_children = function(self) reads = reads + 1; return WMT_static(self) end
  local found = 0
  for i = 0, layer:getStaticChildren().n - 1 do
    local c = layer:getStaticChildren()[i + 1]
    if c:getNestedChildren() ~= nil and c:getText():find("line 300", 1, true) then found = found + 1 end
  end
  idx.static_children = WMT_static
  check("journal walk finds the line", found, 1)
  check("static children read once a frame", reads, 1)
  check("same array within the frame", layer:getStaticChildren(), layer:getStaticChildren())
  MOCK.now_ms = (MOCK.now_ms or 0) + 20
  idx.static_children = function(self) reads = reads + 1; return WMT_static(self) end
  layer:getStaticChildren()
  idx.static_children = WMT_static
  check("read again next frame", reads, 2)
  rlw.invalidate()
end
local b = w:getBounds()
check("bounds x", b.x, 10)
check("bounds h", b.height, 130)
check("canvas loc", w:getCanvasLocation():getY(), 340)
check("contains in", w:contains(C["net.runelite.api.Point"].__new(20, 350)), true)
check("contains out", w:contains(C["net.runelite.api.Point"].__new(5, 350)), false)
check("missing widget", rlw.get(12345), nil)
local map = C["java.util.HashMap"].__new__()
map:put(kids[2], "yes")
check("HashMap keyed by widget", map:get(w:getChild(1)), "yes")

-- ------------------------------------------------------------------ 2. dialogue option highlighting (generated WidgetChoiceStep)

local colour = { getRGB = function() return -16711936 end } -- 0xFF00FF00 as a Java int
local config = { showTextHighlight = function() return true end, textHighlightColor = function() return colour end }
local client = client_stub()
local step = C["com.questhelper.steps.choice.WidgetChoiceStep"].__new__QuestHelperConfig_String_int_int(config, "Yes", 219, 1)
step:highlightChoice__Client(client)
local yes_node = MOCK.widgets[OPTIONS].dynamic[2]
local col = calls("set_native_text_color", yes_node)
check("highlight sets native colour", #col, 1)
check("highlight colour is rgb", col[1] and col[1][3], 0x00FF00)
check("other option untouched", #calls("set_native_text_color", MOCK.widgets[OPTIONS].dynamic[3]), 0)
check("colour reads back", w:getChild(1):getTextColor(), -16711936)
check("mouse-leave listener kept", w:getChild(1)._lis.on_mouse_leave ~= nil, true)
-- CS2 rebuilds the page (between frames: reads are cached for a frame, and
-- the entry invalidates after anything that runs a script): a new node in the
-- same slot forgets the old edits
rlw.invalidate()
yes_node.incarnation = 2
yes_node.text = "Maybe"
yes_node.text_color = 0
check("new page text", w:getChildren()[2]:getText(), "Maybe")
check("new page colour", w:getChild(1):getTextColor(), 0)
-- numbering edits the native text
step.shouldNumber = true
yes_node.text = "Yes"; yes_node.incarnation = 3
rlw.invalidate()
step:highlightChoice__Client(client)
local txt = calls("set_native_text", yes_node)
check("numbered text", txt[1] and txt[1][3], "[1] Yes")
check("numbered reads back", w:getChild(1):getText(), "[1] Yes")

-- DialogChoiceStep over the same children
local dstep = C["com.questhelper.steps.choice.DialogChoiceStep"]
check("DialogChoiceStep class", dstep ~= nil, true)

-- ------------------------------------------------------------------ 3. bank tab button + BankSearch (generated QuestBankTabInterface)

local UNIVERSE, BANK_ITEMS, BANK_SEARCH = 786433, 786444, 786474
reset_mock()
MOCK.widgets[UNIVERSE] = { type = 0, width = 488, height = 300 }
MOCK.widgets[BANK_ITEMS] = { type = 0, width = 400, height = 200,
  listeners = { onInvTransmit = { 277, 786444, 786445 } },
  dynamic = { { type = 5, item_id = 995, item_quantity = 10, original_width = 36, original_height = 32 },
              { type = 5, item_id = 1, item_quantity = 1, original_width = 40, original_height = 32 },
              { type = 5, item_id = -1, original_width = 36, original_height = 10 } } }
MOCK.widgets[BANK_SEARCH] = { type = 5, sprite_id = 179 }
client = client_stub()
local bank_search = C["net.runelite.client.plugins.bank.BankSearch"].__new()
local iface = C["com.questhelper.bank.banktab.QuestBankTabInterface"].__new(client, client_thread, bank_search)
check("tab not hidden", iface:isHidden(), false)
iface:init()
local bg = iface:getQuestBackgroundWidget()
check("background widget made", bg ~= nil, true)
check("background is owned image", bg._ctl, "image")
local bg_rec = MOCK.owned[bg._ckey]
check("background sprite", bg_rec and bg_rec.sprite, 195)
check("background size", bg_rec and bg_rec.width, 25)
check("background pos", bg_rec and bg_rec.x, 408)
check("background op 2 armed", bg_rec and bg_rec.ops and bg_rec.ops[2] and bg_rec.ops[2][1], "View tab ")
check("background op 1 not armed", bg_rec and bg_rec.ops and bg_rec.ops[1], nil)
local icon_rec = MOCK.owned[iface:getQuestIconWidget()._ckey]
check("icon sprite", icon_rec and icon_rec.sprite, 835)
check("icon pos", icon_rec and icon_rec.y, 8)
check("owned are UNIVERSE's children", rlw.get(UNIVERSE):getChildren().n, 2)
MOCK_PRESS(bg._ckey, 2)
check("tab active after press", iface:isQuestTabActive(), true)
check("selected sprite", bg_rec.sprite, 196)
check("boop", client.sounds[1], 2266)
check("reset closes chat", MOCK.scripts_run[1] and table.concat(MOCK.scripts_run[1], ",", 1, MOCK.scripts_run[1].n), "299,1,1,0")
check("reset lays out bank", MOCK.scripts_run[2] and table.concat(MOCK.scripts_run[2], ",", 1, MOCK.scripts_run[2].n), "277,786444,786445")
local sb = calls("set_sprite", MOCK.widgets[BANK_SEARCH])
check("search button re-skinned", sb[1] and sb[1][3], 170)
MOCK_PRESS(bg._ckey, 2)
check("tab closed after second press", iface:isQuestTabActive(), false)
check("unselected sprite", bg_rec.sprite, 195)
iface:destroy()
check("destroy hides background", bg_rec.hidden, true)

-- added-widget teardown (QuestBankTab.removeAddedWidgets) and slot reset
local items = rlw.get(BANK_ITEMS)
local t1 = items:createChild__int_int(-1, 4)
t1:setText("/ 5"); t1:setTextColor(-1); t1:setOriginalWidth(48); t1:setOriginalHeight(12); t1:revalidate()
local t2 = items:createChild__int_int(-1, 5)
t2:setSpriteId(1217); t2:revalidate()
check("owned appended after natives", t1:getIndex(), 3)
check("children with owned", items:getChildren().n, 5)
check("owned text colour rgb", MOCK.owned[t1._ckey].text_color, 0xFFFFFF)
local tab = C["com.questhelper.bank.banktab.QuestBankTab"].__new()
tab.client = client
tab.originalContainerChildren = 3
tab.addedWidgets:add(t1); tab.addedWidgets:add(t2)
tab:removeAddedWidgets()
check("added text removed", MOCK.owned[t1._ckey], nil)
check("added icon removed", MOCK.owned[t2._ckey], nil)
check("tab button kept", MOCK.owned[bg._ckey] ~= nil, true)
check("children back to natives", items:getChildren().n, 3)
tab:resetWidgets()
local sz = calls("set_size", MOCK.widgets[BANK_ITEMS].dynamic[2])
check("slot width reset", sz[1] and sz[1][3], 36)
check("first slot untouched", #calls("set_size", MOCK.widgets[BANK_ITEMS].dynamic[1]), 0)

-- the scroll size the tab writes is a plain write no reset undoes: on_stop
-- gives the game's back; the game's own rebuild (505 pre) makes that moot
MOCK.widgets[BANK_ITEMS].scroll_width, MOCK.widgets[BANK_ITEMS].scroll_height = 0, 812
items:setScrollHeight(2400)
check("tab's scroll height written", MOCK.widgets[BANK_ITEMS].scroll_height, 2400)
rlw.on_stop()
check("on_stop restores the game's scroll height", MOCK.widgets[BANK_ITEMS].scroll_height, 812)
items = rlw.get(BANK_ITEMS)
items:setScrollHeight(2400)
rlw.on_script_pre(505)
local before = #calls("set_scroll_size", MOCK.widgets[BANK_ITEMS])
rlw.on_stop()
check("after the game's rebuild there is nothing to restore", #calls("set_scroll_size", MOCK.widgets[BANK_ITEMS]), before)
items = rlw.get(BANK_ITEMS)

-- native item edits (drawItem) and hide/unhide
local slot = items:getChild(0)
slot:setItemId(1511); slot:setItemQuantity(3)
local it = calls("set_item", MOCK.widgets[BANK_ITEMS].dynamic[1])
check("native set_item", it[#it] and (it[#it][3] .. "x" .. it[#it][4]), "1511x3")
check("item reads back", slot:getItemId(), 1511)
slot:setHidden(true)
check("hidden reads back", slot:isHidden(), true)
slot:setHidden(false)
check("unhidden is native again", slot:isHidden(), false)
slot:setOriginalX(51); slot:setOriginalY(36); slot:revalidate()
local pos = calls("set_position", MOCK.widgets[BANK_ITEMS].dynamic[1])
check("native move", pos[1] and (pos[1][3] .. "," .. pos[1][4]), "51,36")

-- ------------------------------------------------------------------ 4. chatbox panel (generated ChatBox / NpcChatBox)

local MES_LAYER, MES_TEXT, MES_TEXT2 = 0x00a20027, 0x00a2002b, 0x00a2002c
reset_mock()
MOCK.widgets[MES_LAYER] = { type = 0, width = 506, height = 130, dynamic = { { type = 4, text = "native line" } } }
MOCK.widgets[MES_TEXT] = { type = 4, text = "" }
MOCK.widgets[MES_TEXT2] = { type = 4, text = "" }
client = client_stub()
local cpm = C["net.runelite.client.game.chatbox.ChatboxPanelManager"].__new()
local progressed, continued = 0, nil
local dialog = {
  getFaceID = function() return 1234 end, getAnimation = function() return 588 end,
  getName = function() return "Hans" end, getText = function() return "Hello there." end,
  isPlayer = function() return false end,
  progressState = function() progressed = progressed + 1 end,
  getContinueDialog = function() return continued end,
}
local closed = 0
local box = C["com.questhelper.runeliteobjects.extendedruneliteobjects.NpcChatBox"].__new(client, cpm)
box:dialog(dialog):onClose(J.fn("java.lang.Runnable", "run", function() closed = closed + 1 end)):build()
check("panel open script", MOCK.scripts_run[1] and table.concat(MOCK.scripts_run[1], ",", 1, MOCK.scripts_run[1].n), "677,0")
check("current input", cpm:getCurrentInput(), box)
check("meslayer mode", MOCK.varcs_int[5], -3)
check("mes text hidden", #calls("set_hidden", MOCK.widgets[MES_TEXT]), 1)
check("native line hidden", #calls("set_hidden", MOCK.widgets[MES_LAYER].dynamic[1]), 1)
local texts = {}
for k, r in pairs(MOCK.owned) do if r.kind == "text" then texts[r.text] = r end end
check("name text", texts["Hans"] ~= nil, true)
check("dialog text", texts["Hello there."] ~= nil, true)
local cont = texts["Click here to continue"]
check("continue text", cont ~= nil, true)
check("continue armed", cont and cont.ops and cont.ops[1] and cont.ops[1][1], "Continue")
check("continue colour", cont and cont.text_color, 0xff)
check("continue centred", cont and cont.edits and cont.edits.align and cont.edits.align[1], 1)
check("name x", texts["Hans"] and texts["Hans"].x, 108)
local face = cpm:getContainerWidget():getChild(0)
check("chathead kept Lua-side", face and face:getModelId(), 1234)
local fr = face and MOCK.owned[face._ckey]
check("chathead is an owned MODEL control", fr and fr.kind, "model")
check("chathead shows the npc's head", fr and fr.edits and fr.edits.model and (fr.edits.model[1] .. ":" .. fr.edits.model[2]), "npc:1234")
check("chathead pose", fr and fr.edits and fr.edits.pose and table.concat(fr.edits.pose, ",", 1, 4), "40,0,1882,796")
check("chathead animation", fr and fr.edits and fr.edits.anim and fr.edits.anim[1], 588)
check("container children", cpm:getContainerWidget():getChildren().n, 4)
-- space continues; no next dialog closes the panel
rlw.on_key({ key = 45, ch = 0, down = true })
check("space progressed", progressed, 1)
check("continue says wait", cont.text, "Please wait...")
check("panel closed", cpm:getCurrentInput(), nil)
check("onClose ran", closed, 1)
check("close script", MOCK.scripts_run[2] and table.concat(MOCK.scripts_run[2], ",", 1, MOCK.scripts_run[2].n), "299,0,1,0")
check("owned removed on close", next(MOCK.owned), nil)
check("mes text restored", #calls("reset", MOCK.widgets[MES_TEXT]), 1)
check("native line restored", #calls("reset", MOCK.widgets[MES_LAYER].dynamic[1]), 1)
-- the continue button by click, with a next dialog waiting
continued = { isPlayer = function() return false end }
box = C["com.questhelper.runeliteobjects.extendedruneliteobjects.NpcChatBox"].__new(client, cpm)
box:dialog(dialog):build()
for k, r in pairs(MOCK.owned) do if r.text == "Click here to continue" then MOCK_PRESS(k, 1) end end
check("click progressed", progressed, 2)
check("next dialog held", box.nextDialog, continued)
check("panel stays open", cpm:getCurrentInput(), box)
-- escape closes
rlw.on_key({ key = 37, ch = 0, down = true })
check("escape closed", cpm:getCurrentInput(), nil)
check("worldmap search blocks input", (function()
  MOCK.widgets[0x0253001a] = { type = 0 }
  MOCK.varcs_int[190] = 1
  local r = cpm:shouldTakeInput()
  MOCK.widgets[0x0253001a] = nil
  return r
end)(), false)

-- ------------------------------------------------------------------ 5. native click / key listeners (Mage Arena II shape)

reset_mock()
MOCK.widgets[OPTIONS] = { type = 0, width = 512, height = 130,
  dynamic = { { type = 4, text = "Pick a god" }, { type = 4, text = "Saradomin" }, { type = 4, text = "Guthix" } } }
local picked = {}
local opts = rlw.get(OPTIONS)
opts:getChild(1):setOnClickListener(js(function(ev) picked[#picked + 1] = "sara:" .. ev:getOp() end))
check("menu select reaches native listener", rlw.on_menu_select(OPTIONS, 1, 1), true)
check("listener ran", picked[1], "sara:1")
check("other slot has none", rlw.on_menu_select(OPTIONS, 2, 1), false)
local typed
opts:setOnKeyListener(js(function(ev) typed = ev:getTypedKeyCode() end))
rlw.on_key({ key = 0, ch = string.byte("2"), down = true })
check("typed jagex key code", typed, 17)
check("from_menu child", rlw.from_menu(OPTIONS, 2):getText(), "Guthix")
opts:setOnKeyListener(nil)
typed = nil
rlw.on_key({ key = 0, ch = string.byte("1"), down = true })
check("cleared key listener", typed, nil)
-- closing the interface forgets its widgets
rlw.on_interface({ group = 219, open = false })
check("fresh wrapper after close", rlw.get(OPTIONS) ~= opts, true)

-- ------------------------------------------------------------------ 6. owned layout modes, opacity, cap

reset_mock()
local MAIN = (548 << 16) | 2
MOCK.widgets[MAIN] = { type = 0, width = 500, height = 300 }
local main = rlw.get(MAIN)
local cw = main:createChild__int_int(-1, 4)
cw:setText("Congratulations!"); cw:setOriginalY(-92); cw:setOriginalWidth(162); cw:setOriginalHeight(18)
cw:setXPositionMode(1); cw:setYPositionMode(1); cw:revalidate()
local cr = MOCK.owned[cw._ckey]
check("centred x", cr.x, (500 - 162) // 2)
check("centred y", cr.y, (300 - 18) // 2 - 92)
local img = main:createChild__int_int(-1, 5)
img:setSpriteId(537); img:setOpacity(120); img:revalidate()
check("opacity is alpha", MOCK.owned[img._ckey].opacity, 135)
img:setOnClickListener(js(function() picked.closed = true end))
check("click-only armed as Select", MOCK.owned[img._ckey].ops[1][1], "Select")
MOCK_PRESS(img._ckey, 1)
check("click listener ran", picked.closed, true)
local icon = main:createChild__int_int(-1, 6)
icon:setItemId(1511); icon:setRotationX(500); icon:setModelZoom(800); icon:revalidate()
check("model with item is a model control", icon._ctl, "model")
check("model control shows the item", MOCK.owned[icon._ckey].edits.model[1] .. ":" .. MOCK.owned[icon._ckey].edits.model[2], "obj:1511")
local scroll = main:createChild__int_int(-1, 6)
scroll:setModelId(3032); scroll:setRotationX(502); scroll:setRotationZ(2047); scroll:setModelZoom(554); scroll:revalidate()
check("cache model control", MOCK.owned[scroll._ckey].edits.model[1] .. ":" .. MOCK.owned[scroll._ckey].edits.model[2], "model:3032")
check("cache model pose", table.concat(MOCK.owned[scroll._ckey].edits.pose, ",", 1, 4), "502,0,2047,554")
local me = main:createChild__int_int(-1, 3)
me:setType(6); me:setModelType(3); me:revalidate()
check("rectangle retyped to a player chathead", MOCK.owned[me._ckey].kind .. ":" .. MOCK.owned[me._ckey].edits.model[1], "model:player")
local box = main:createChild__int_int(-1, 3)
box:setTextColor(0xff0000); box:setFilled(true); box:setOpacity(100); box:revalidate()
check("owned rectangle", MOCK.owned[box._ckey].kind, "rect")
check("rectangle colour and fill", MOCK.owned[box._ckey].edits.rect[1] .. ":" .. tostring(MOCK.owned[box._ckey].edits.rect[2]), 0xff0000 .. ":true")
check("rectangle opacity is alpha", MOCK.owned[box._ckey].opacity, 155)
check("isFilled reads back", box:isFilled(), true)
main:deleteAllChildren()
check("deleteAllChildren removes owned", next(MOCK.owned), nil)
check("deleted children read nil", main:getChildren(), nil)
MOCK.owned_cap = 1
local a1 = main:createChild__int_int(-1, 4); a1:revalidate()
local a2 = main:createChild__int_int(-1, 4); a2:setText("x"); a2:revalidate()
check("over the cap: no control", a2._h, nil)
check("over the cap: still a widget", a2:getText(), "x")
local warned = false
for _, l in ipairs(logs) do if l:find("owned widget limit", 1, true) then warned = true end end
check("cap logged", warned, true)
MOCK.owned_cap = 48

-- ------------------------------------------------------------------ 7. GE search hook

if not J.defined("net.runelite.api.events.GrandExchangeSearched") or J.try_class("net.runelite.api.events.GrandExchangeSearched").consume == nil then
  J.def("t.GrandExchangeSearched", { kind = "class", super = "java.lang.Object", ifaces = {}, body = function(cls)
    function cls.consume(self) self.consumed = true end
    function cls.isConsumed(self) return self.consumed == true end
  end })
  rawset(C, "net.runelite.api.events.GrandExchangeSearched", C["t.GrandExchangeSearched"])
end
local bus = { post = function(_, ev)
  ev:consume()
  rlw.ge_set_index(0); rlw.ge_set_count(2); rlw.ge_set_ids(J.arr({ 1511, 995 }, 2))
end }
local consumed, res = rlw.ge_searched(bus)
check("ge consumed", consumed, true)
check("ge count", res.count, 2)
check("ge ids", res.ids[2], 995)

-- ------------------------------------------------------------------ 7b. bank rebuild + menu retarget

reset_mock()
MOCK.widgets[BANK_ITEMS] = { type = 0, width = 400, height = 300,
  dynamic = { { type = 5, item_id = 995, item_quantity = 10, menu = { "Withdraw-1 Coins", "Withdraw-5 Coins" } },
              { type = 5, item_id = 1511, item_quantity = 1, menu = { "Withdraw-1 Logs", "Withdraw-5 Logs" } } } }
local bank = rlw.get(BANK_ITEMS)
local s0 = bank:getChild(0)
s0:setItemId(1511); s0:setOpacity(120); s0:setHidden(false); s0:setOriginalX(51); s0:revalidate()
check("native opacity is a host edit", #calls("set_opacity", MOCK.widgets[BANK_ITEMS].dynamic[1]), 1)
rlw.on_script_pre(281)
check("an unrelated script keeps the edits", #calls("reset", MOCK.widgets[BANK_ITEMS].dynamic[1]), 0)
rlw.on_script_pre(505)
check("bankmain_finishbuilding drops the slot's edits", #calls("reset", MOCK.widgets[BANK_ITEMS].dynamic[1]), 1)
check("the slot reads native again", bank:getChild(0):getItemId(), 995)
-- a CC_OP row on slot 0 moved to slot 1 by a subscriber
local invoked = nil
MOCK_API.widgets.invoke = function(ref) invoked = ref; return true end
local MA = C["net.runelite.api.MenuAction"]
local entry = { row = { slot = 0, component_id = BANK_ITEMS, param0 = 0, param1 = BANK_ITEMS },
  type = MA.CC_OP, option = "Withdraw-5", param0 = 1, param1 = BANK_ITEMS }
check("a moved row is consumed", rlw.menu_retarget(entry), true)
check("and pressed on the slot it names", invoked and invoked.node == MOCK.widgets[BANK_ITEMS].dynamic[2] and invoked.op, 2)
entry.param0 = 0
invoked = nil
check("an unmoved row is left to the client", rlw.menu_retarget(entry), false)
check("nothing pressed for it", invoked, nil)
-- the plugin rewrote slot 0's actions (a placeholder offers only Details):
-- a native op it removed is dropped, one it kept goes on
bank:getChild(0):clearActions()
bank:getChild(0):setAction(0, "Withdraw-1")
entry.option = "Withdraw-5"
check("an op the plugin removed is dropped", rlw.menu_retarget(entry), true)
check("and pressed nowhere", invoked, nil)
entry.option = "Withdraw-1"
check("an op it kept goes to the client", rlw.menu_retarget(entry), false)
MOCK_API.widgets.invoke = nil

-- ------------------------------------------------------------------ 8. stop releases everything

reset_mock()
MOCK.widgets[MAIN] = { type = 0, width = 500, height = 300 }
main = rlw.get(MAIN)
local s1 = main:createChild__int_int(-1, 4); s1:revalidate()
main:setHidden(true)
rlw.on_stop()
check("stop removes owned", next(MOCK.owned), nil)
check("stop resets native edits", #calls("reset", MOCK.widgets[MAIN]), 1)

print(string.format("rl_widgets: %d passed, %d failed", passes, fails))
if fails > 0 then os.exit(1) end
