-- rt/rl_client.lua under plain Lua, against harness/mock_api.lua, driving
-- the generated framework code that uses it: quest state through
-- runScript/getIntStack (QuestHelperQuest.getQuestState), the inventory
-- through QuestContainerManager.updateInventory and ItemRequirement, the
-- right-click "Start <quest>" row through QuestMenuHandler.addNewEntry and
-- its onClick, the console chat line (Utils.addChatMessage), the bank's
-- chat builder message, compositions (QuestPerspective.isUnnamedScenery,
-- multiloc impostors), enums, and the formatting utilities.
--
--   /opt/homebrew/bin/lua tools/questhelper_port/harness/test_rl_client.lua
local HERE = debug.getinfo(1, "S").source:sub(2):match("^(.*)/") or "."
local ROOT = HERE .. "/../../../script/plugins/quest_helper"

local logs = {}
J = { protect = pcall, log = function(s) logs[#logs + 1] = s end, now_ms = function() return 0 end }
for _, f in ipairs({ "j_core", "j_lang", "j_util", "j_stream", "j_regex", "j_misc" }) do dofile(ROOT .. "/rt/" .. f .. ".lua") end
dofile(HERE .. "/mock_api.lua")
QH = { api = MOCK_API }

local function exists(p) local f = io.open(p, "r"); if f then f:close(); return true end return false end

-- java.awt is the rendering agent's (rt/awt.lua); a stand-in with the
-- contract's spellings when it is not there yet.
if exists(ROOT .. "/rt/awt.lua") then
  dofile(ROOT .. "/rt/awt.lua")
else
  J.def("java.awt.Color", { kind = "class", super = "java.lang.Object", ifaces = {}, body = function(cls)
    function cls.__init__int_int_int(self, r, g, b) self.v = 0xFF000000 | (r << 16) | (g << 8) | b end
    function cls.__init__int_int_int_int(self, r, g, b, a) self.v = (a << 24) | (r << 16) | (g << 8) | b end
    function cls.getRGB(self) return J.l2i(self.v) end
    function cls.getRed(self) return (self.v >> 16) & 255 end
    function cls.getGreen(self) return (self.v >> 8) & 255 end
    function cls.getBlue(self) return self.v & 255 end
  end, clinit = function(cls)
    cls.CYAN = cls.__new__int_int_int(0, 255, 255)
    cls.WHITE = cls.__new__int_int_int(255, 255, 255)
    cls.RED = cls.__new__int_int_int(255, 0, 0)
  end })
  J.def("java.awt.image.BufferedImage", { kind = "class", super = "java.lang.Object", ifaces = {}, body = function(cls)
    function cls.__init__int_int_int(self, w, h, t) self.w, self.h = w, h end
    function cls.getWidth(self) return self.w end
    function cls.getHeight(self) return self.h end
  end })
end
for _, f in ipairs({ "rl_world", "rl_widgets" }) do
  if exists(ROOT .. "/rt/" .. f .. ".lua") then dofile(ROOT .. "/rt/" .. f .. ".lua") end
end
dofile(ROOT .. "/rt/rl_client.lua")

-- generated modules on demand; external classes nobody provides here become
-- absorbing stubs, as in run_all.lua
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

-- ------------------------------------------------------------------ checks

local fails, passes = 0, 0
local function check(name, got, want)
  if got == want then passes = passes + 1
  else
    fails = fails + 1
    print(string.format("FAIL %s: got %s want %s", name, tostring(got), tostring(want)))
  end
end
local function raises(name, f, pattern)
  local ok, e = pcall(f)
  if ok then fails = fails + 1; print("FAIL " .. name .. ": did not raise"); return end
  local msg = type(e) == "table" and e.toString and e:toString() or tostring(e)
  if pattern and not msg:find(pattern, 1, true) then
    fails = fails + 1; print("FAIL " .. name .. ": raised " .. msg); return
  end
  passes = passes + 1
end

local rl = J.rl_client
local client = rl.client()
local Client = C["net.runelite.api.Client"]
check("client singleton", Client.__new(), client)
check("client singleton __new__", Client.__new__(), client)

-- vars
MOCK.varbits[4150] = 3; MOCK.varps[1427] = 7; MOCK.varcs_int[171] = 5; MOCK.varcs_str[335] = "abc"
check("varbit", client:getVarbitValue__int(4150), 3)
check("varp", client:getVarpValue(1427), 7)
check("varc int", client:getVarcIntValue(171), 5)
check("varc str", client:getVarcStrValue(335), "abc")
client:setVarcIntValue(171, 9); check("set varc int", MOCK.varcs_int[171], 9)
client:setVarcStrValue(335, "x"); check("set varc str", MOCK.varcs_str[335], "x")

-- skills: ordinal = stat index
local Skill = C["net.runelite.api.Skill"]
MOCK.skills[21] = { current_level = 12, base_level = 10, xp = 1200 }
MOCK.skills[23] = { current_level = 3, base_level = 4, xp = 300 }
check("real hunter", client:getRealSkillLevel(Skill.HUNTER), 10)
check("boosted hunter", client:getBoostedSkillLevel(Skill.HUNTER), 12)
check("sailing", client:getRealSkillLevel(Skill.SAILING), 4)
check("hunter xp", client:getSkillExperience(Skill.HUNTER), 1200)

-- game state: TORIRS_SCREEN_* are GameState's numbers
local GS = C["net.runelite.api.GameState"]
MOCK.screen = 30; check("logged in", client:getGameState(), GS.LOGGED_IN)
MOCK.screen = 10; check("login screen", client:getGameState(), GS.LOGIN_SCREEN)
MOCK.screen = "game"; check("logged in by name", client:getGameState(), GS.LOGGED_IN)
MOCK.tick = 77; check("tick", client:getTickCount(), 77)
check("plane", client:getPlane(), 0)
MOCK.weight = 42; check("weight", client:getWeight(), 42)

-- world types
local WT = C["net.runelite.api.WorldType"]
MOCK.world_types = 1 | (1 << 30)
local wts = client:getWorldType()
check("members", wts:contains(WT.MEMBERS), true)
check("seasonal", wts:contains(WT.SEASONAL), true)
check("not speedrun", wts:contains(WT.QUEST_SPEEDRUNNING), false)
check("wt size", wts:size(), 2)

-- containers
MOCK.inv[93] = { { 1931, 1 }, nil, { 995, 500 }, { 1931, 1 } }
local inv = client:getItemContainer__int(93)
check("inv size", inv:size(), 28)
check("inv slot0", inv:getItems()[1]:getId(), 1931)
check("inv empty slot", inv:getItems()[2]:getId(), -1)
check("inv empty qty", inv:getItems()[2]:getQuantity(), 0)
check("inv getItem empty", inv:getItem(1), nil)
check("inv getItem", inv:getItem(2):getQuantity(), 500)
check("inv count id", inv:count__int(1931), 2)
check("inv count", inv:count__(), 3)
check("inv find", inv:find(995), 2)
check("inv find missing", inv:find(1), -1)
check("inv contains", inv:contains(995), true)
check("no bank", client:getItemContainer__int(95), nil)
-- any other container by its id (game.container_size/container_slot)
MOCK.inv[516] = { { 1925, 5 }, nil, { 1931, 2 }, size = 6 }
local shop = client:getItemContainer__int(516)
check("shop read by its id", shop and shop:size(), 6)
check("shop slot", shop and shop:getItems()[3]:getId(), 1931)
check("shop empty slot", shop and shop:getItems()[2]:getId(), -1)
check("a container never sent is absent", client:getItemContainer__int(659), nil)
check("ItemContainerChanged's container is never nil", rl.changed_container(600) ~= nil and rl.changed_container(600):size(), 0)
MOCK.inv[516] = nil
rl.inventory_changed(516)
check("item equals", C["net.runelite.api.Item"].__new(1, 2):equals(C["net.runelite.api.Item"].__new(1, 2)), true)

-- the same frame answers the same snapshot; inventory_changed drops it
MOCK.inv[93][1] = { 1935, 1 }
check("snapshot within a frame", client:getItemContainer__int(93):getItems()[1]:getId(), 1931)
rl.inventory_changed(93)
check("after inventory_changed", client:getItemContainer__int(93):getItems()[1]:getId(), 1935)
MOCK.inv[93][1] = { 1931, 1 }
rl.inventory_changed(93)

-- generated: QuestContainerManager.updateInventory + ItemRequirement
local QCM = C["com.questhelper.managers.QuestContainerManager"]
local ok, err = pcall(QCM.updateInventory, client)
check("updateInventory ran", ok, true)
if not ok then print("  " .. tostring(type(err) == "table" and err.toString and err:toString() or err)) end
local items = QCM.getInventoryData():getItems__()
local seen = {}
for i = 1, items.n do if items[i] then seen[items[i]:getId()] = items[i]:getQuantity() end end
check("inventory data jug count", seen[1931], 2)
check("inventory data coins", seen[995], 500)
local IR = C["com.questhelper.requirements.item.ItemRequirement"]
check("ItemRequirement jug x2 met", IR.__new__String_int_int("Jug", 1931, 2):check__Client(client), true)
check("ItemRequirement jug x3 unmet", IR.__new__String_int_int("Jug", 1931, 3):check__Client(client), false)
check("ItemRequirement absent unmet", IR.__new__String_int("Bucket", 1925):check__Client(client), false)

-- runScript / getIntStack: QuestHelperQuest.getQuestState (script 4029)
local QS = C["net.runelite.api.QuestState"]
local QHQ = C["com.questhelper.questinfo.QuestHelperQuest"]
local cook = QHQ.COOKS_ASSISTANT
MOCK.scripts[4029] = function(id) return { id == cook.id and 2 or 0 }, {} end
check("quest finished", cook:getQuestState(client), QS.FINISHED)
MOCK.scripts[4029] = function(id) return { 1 }, {} end
check("quest not started", cook:getQuestState(client), QS.NOT_STARTED)
MOCK.scripts[4029] = function(id) return { 0 }, {} end
check("quest in progress", cook:getQuestState(client), QS.IN_PROGRESS)
check("int stack size", client:getIntStackSize(), 1)
local seen_args
MOCK.scripts[72] = function(...) seen_args = table.pack(...); return {}, {} end
client:runScript(J.arr({ 72, 14155780, 14155777, 9 }, 4))
check("script args", seen_args and seen_args.n == 3 and seen_args[3] == 9, true)
MOCK.scripts_refuse = "vm_busy"
raises("runScript refused", function() client:runScript(J.arr({ 4029, 1 }, 2)) end, "vm_busy")
MOCK.scripts_refuse = nil

-- inside a script callback: getIntStack is the running script's stack
local ref = { ints = { 5, 6, 0 }, strs = { "a" } }
rl.script_enter(ref)
local st = client:getIntStack()
check("cb stack size", client:getIntStackSize(), 3)
check("cb stack n", st.n, 3)
check("cb stack bottom", st[1], 5)
st[client:getIntStackSize() - 1 + 1] = 1   -- QuestBankTab.onScriptCallbackEvent
check("cb stack top written", ref.ints[3], 1)
check("cb string stack", client:getStringStack()[1], "a")
rl.script_leave()
check("after leave", client:getIntStackSize(), 0)

-- compositions
MOCK.loc_defs[100] = { id = 100, name = "Door", actions = { "Open", nil, nil, nil, "Examine" }, size_x = 1, size_z = 2,
  transform_varbit = 4150, transform_varp = -1, transforms = { 101, 102, 103 } }
MOCK.loc_defs[101] = { id = 101, name = "null", actions = {} }
MOCK.loc_defs[103] = { id = 103, name = "Open door", actions = {} }
local door = client:getObjectDefinition(100)
check("loc name", door:getName(), "Door")
check("loc actions", door:getActions()[1], "Open")
check("loc actions n", door:getActions().n, 5)
check("loc size y", door:getSizeY(), 2)
check("impostor ids", door:getImpostorIds().n, 3)
MOCK.varbits[4150] = 0; check("impostor 0", door:getImpostor():getId(), 101)
MOCK.varbits[4150] = 9; check("impostor out of range = last", door:getImpostor():getId(), 103)
check("generated isUnnamedScenery", C["com.questhelper.steps.tools.QuestPerspective"].isUnnamedScenery(client, 101), true)
check("generated isUnnamedScenery named", C["com.questhelper.steps.tools.QuestPerspective"].isUnnamedScenery(client, 100), false)
MOCK.npc_defs[2108] = { id = 2108, name = "Wise Old Man", actions = { "Talk-to" }, size = 1, combat_level = 0 }
local wom = client:getNpcDefinition(2108)
check("npc name", wom:getName(), "Wise Old Man")
check("npc models absent", wom:getModels(), nil)
check("item name", client:getItemDefinition(1931):getName(), "Item 1931")
check("item placeholder", client:getItemDefinition(1931):getPlaceholderTemplateId(), -1)
MOCK.enums[982] = { key_type = "int", value_type = "obj", default = -1, keys = { 1, 2 }, values = { 556, 555 } }
local e = client:getEnum(982)
check("enum value", e:getIntValue(2), 555)
check("enum default", e:getIntValue(7), -1)
check("enum size", e:size(), 2)
check("enum vals", e:getIntVals()[1], 556)

-- menu: QuestMenuHandler.addNewEntry adds "Start <quest>" with an onClick
local started
local QMH = C["com.questhelper.managers.QuestMenuHandler"]
local h = setmetatable({}, QMH)
h.client = client
h.questManager = {
  startUpQuest__QuestHelper_boolean = function(_, q, b) started = q end,
  shutDownQuest__boolean = function() started = "stopped" end,
}
local rows = {
  { text = "Cancel", action = 1006, pick_kind = 0, npc_slot = -1, player_pid = -1, target_id = -1, component_id = -1, slot = -1 },
  { text = "Read-journal @or1@Cook's Assistant", action = 57, pick_kind = 5, npc_slot = -1, player_pid = -1, target_id = -1, component_id = 26148871, slot = 3 },
}
local es = rl.menu_begin({ hover_pass = false, rows = rows })
check("menu entries", es.n, 2)
check("row option", es[2]:getOption(), "Read-journal")
check("row target", es[2]:getTarget(), "<col=ffb000>Cook's Assistant")
check("row type", es[2]:getType(), C["net.runelite.api.MenuAction"].CC_OP)
check("row param1", es[2]:getParam1(), 26148871)
check("row param0", es[2]:getParam0(), 3)
local entries = client:getMenu():getMenuEntries()
h:addNewEntry(entries, "Start Quest Helper", "Cook's Assistant", 3, 26148871)
check("menu grew", client:getMenu():getMenuEntries().n, 3)
check("created entry below top", client:getMenu():getMenuEntries()[2]:getOption(), "Start Quest Helper")
local map = rl.menu_end()
check("menu row added", #MOCK.menu_added, 1)
check("menu row text", MOCK.menu_added[1][1], "Start Quest Helper @lre@Cook's Assistant")
local tag = MOCK.menu_added[1][2]
local picked = rl.menu_select({ row = rows[2], tag = tag, owned = true, x = 0, y = 0 })
check("select owned entry", picked:getOption(), "Start Quest Helper")
check("select type", picked:getType(), C["net.runelite.api.MenuAction"].RUNELITE)
rl.menu_click(picked)
check("onClick started quest", started, QHQ.getByName("Cook's Assistant"))
check("select native", rl.menu_select({ row = rows[2], tag = 0, owned = false }):getOption(), "Read-journal")
-- client-APIs round: the created row goes where createMenuEntry put it
-- (api.menu.set_order, bottom first: Cancel, Start Quest Helper, Read-journal)
check("order sent", MOCK.menu_order and #MOCK.menu_order, 3)
check("order bottom", MOCK.menu_order and MOCK.menu_order[1].row, 1)
check("order created row by tag", MOCK.menu_order and MOCK.menu_order[2].tag, tag)
check("order top", MOCK.menu_order and MOCK.menu_order[3].row, 2)

-- setMenuEntries / removeMenuEntry / setDeprioritized / setOption on a native
-- row, and that native row's onClick on select (RuneliteObjectManager's
-- replace-walk and priorities)
MOCK.menu_order, MOCK.menu_texts = nil, {}
local walk_rows = {
  { text = "Cancel", action = 1006, type = 1006, identifier = 0, param0 = 0, param1 = 0, npc_slot = -1, player_pid = -1, target_id = -1, component_id = -1, slot = -1 },
  { text = "Examine @yel@Tree", action = 1002, type = 1002, identifier = 1276, param0 = 50, param1 = 51, npc_slot = -1, player_pid = -1, target_id = 1276, component_id = -1, slot = -1 },
  { text = "Walk here", action = 23, type = 23, identifier = 0, param0 = 52, param1 = 53, npc_slot = -1, player_pid = -1, target_id = -1, component_id = -1, slot = -1 },
}
local wes = rl.menu_begin({ hover_pass = false, rows = walk_rows })
local menu_obj = client:getMenu()
local all = menu_obj:getMenuEntries()
all[3]:setOption("Talk-to"):setTarget("<col=ffff00>Fake Veos</col>")
local clicked = 0
all[3]:onClick__Consumer(J.fn("java.util.function.Consumer", "accept", function() clicked = clicked + 1 end))
all[2]:setDeprioritized(true)
menu_obj:removeMenuEntry(all[1])
check("removeMenuEntry", menu_obj:getMenuEntries().n, 2)
rl.menu_end()
check("removed row left out", MOCK.menu_order and #MOCK.menu_order, 2)
check("deprioritized row", MOCK.menu_order and MOCK.menu_order[1].row == 2 and MOCK.menu_order[1].deprioritized, true)
check("native text rewrite", MOCK.menu_texts[3], "Talk-to @yel@Fake Veos")
local sel = rl.menu_select({ row = walk_rows[3], tag = 0, owned = false })
rl.menu_click(sel)
check("native row keeps its onClick", clicked, 1)
-- setMenuEntries replaces the build's whole list
MOCK.menu_order = nil
rl.menu_begin({ hover_pass = true, rows = walk_rows })
local cur = menu_obj:getMenuEntries()
menu_obj:setMenuEntries(J.arr({ cur[3], cur[1] }, 2))
rl.menu_end()
check("setMenuEntries order", MOCK.menu_order and (MOCK.menu_order[1].row .. "," .. MOCK.menu_order[2].row), "3,1")
-- a build nobody edits sends no order (the client sorts as before)
MOCK.menu_order = nil
rl.menu_begin({ hover_pass = false, rows = walk_rows })
rl.menu_end()
check("untouched build sends no order", MOCK.menu_order, nil)
-- isMenuOpen / getMenuX.. from api.menu.shown
MOCK.menu_box = nil
check("menu closed", client:isMenuOpen(), false)
check("menu x closed", menu_obj:getMenuX(), 0)
MOCK.menu_box = { x = 300, y = 120, width = 140, height = 80 }
check("menu open", client:isMenuOpen(), true)
check("menu box", menu_obj:getMenuX() .. "," .. menu_obj:getMenuY() .. "," .. menu_obj:getMenuWidth() .. "," .. menu_obj:getMenuHeight(), "300,120,140,80")
MOCK.menu_box = nil
-- Client.setVarbit: the client-side write
MOCK.varbit_writes = {}
client:setVarbit(4150, 0)
check("setVarbit", MOCK.varbit_writes[1] and (MOCK.varbit_writes[1][1] .. "=" .. MOCK.varbit_writes[1][2]), "4150=0")
-- WorldMap.setWorldMapPositionTarget -> api.client.worldmap_jump
MOCK.map_jumps = {}
client:getWorldMap():setWorldMapPositionTarget(C["net.runelite.api.coords.WorldPoint"].__new(3222, 3218, 0))
check("worldmap jump", MOCK.map_jumps[1] and table.concat(MOCK.map_jumps[1], ","), "3222,3218,0")
-- LinkBrowser.browse -> api.client.open_url; a refusal is logged, not raised
MOCK.urls = {}
check("browse", C["net.runelite.client.util.LinkBrowser"].browse("https://github.com/Zoinkwiz/quest-helper"), true)
check("browse url", MOCK.urls[1], "https://github.com/Zoinkwiz/quest-helper")
MOCK.url_refuse = "unsupported"
check("browse refused", C["net.runelite.client.util.LinkBrowser"].browse("https://discord.gg/XCfwNnz6RB"), false)
MOCK.url_refuse = nil

-- chat: Utils.addChatMessage -> CONSOLE line
MOCK.chat = {}
C["com.questhelper.util.Utils"].addChatMessage(client, "hello")
check("chat type", MOCK.chat[1] and MOCK.chat[1][1], 99)
check("chat text", MOCK.chat[1] and MOCK.chat[1][3], "[<col=00ffff>Quest Helper</col>] hello")
-- ChatMessageBuilder + QueuedMessage, as QuestBankTab's "You need ..." line
local CT = C["net.runelite.client.chat.ChatColorType"]
local msg = C["net.runelite.client.chat.ChatMessageBuilder"].__new():append__String("You need "):append__ChatColorType(CT.HIGHLIGHT)
  :append__String("5 x <Pot>"):append__ChatColorType(CT.NORMAL):append__String(" more."):build()
check("builder", msg, "You need <colHIGHLIGHT>5 x <lt>Pot<gt><colNORMAL> more.")
local mgr = C["net.runelite.client.chat.ChatMessageManager"].__new()
mgr:queue(C["net.runelite.client.chat.QueuedMessage"].builder():type(C["net.runelite.api.ChatMessageType"].ITEM_EXAMINE)
  :runeLiteFormattedMessage(msg):build())
check("queued type", MOCK.chat[2][1], 27)
check("queued text", MOCK.chat[2][3], "You need <col=ef1020>5 x <lt>Pot<gt></col> more.")

-- hint arrow, sound, invoke, pointer, canvas
client:playSoundEffect__int(2266); check("sound", MOCK.sounds[1], 2266)
client:menuAction(-1, 786558, C["net.runelite.api.MenuAction"].CC_OP, 1, -1, "Potion store", "")
check("menuAction invoke", MOCK.invoked[1][1] == 786558 and MOCK.invoked[1][2] == 1, true)
check("pointer", client:getMouseCanvasPosition():getX(), 10)
check("canvas w", client:getCanvasWidth(), 1024)
check("resized", client:isResized(), true)
check("viewport w", client:getViewportWidth(), 1024)
MOCK.hint_arrow = nil
check("no hint arrow npc", client:getHintArrowNpc(), nil)

-- minimap camera off the draw object: yaw 4096 (a quarter turn) at zoom 4
MOCK.player.fine_x, MOCK.player.fine_z = 3222 * 128 + 64, 3218 * 128 + 64
local d = MOCK_DRAW()
d.minimap_project = function(fx, fz)
  local dx = (fx - MOCK.player.fine_x) / 128 * 4
  return 600, 100 + J.d2i(dx)
end
QH.draw = d
MOCK.now_ms = 5
check("minimap zoom", client:getMinimapZoom(), 4.0)
check("camera yaw", client:getCameraYawTarget(), 4096)
QH.draw = nil

-- images
local im = C["net.runelite.client.game.ItemManager"].__new()
local icon = im:getImage__int(1931)
check("icon cached", im:getImage__int(1931), icon)
check("icon ref", icon:qh_ref() and icon:qh_ref().image, 1931)
check("icon size", icon:getWidth(), 36)
-- outline of a 3x3 icon with one opaque centre pixel: the four neighbours
MOCK.pixels = { 0, 0, 0, 0, 0xFF112233, 0, 0, 0, 0 }
local icon3 = im:getImage__int_int_boolean(4151, 1, false)
icon3.qh_w, icon3.qh_h = 3, 3 -- the mock's image is 3x3
local ol = im:getItemOutline(4151, 1, C["java.awt.Color"].RED)
local px = ol:qh_pixels() or {}
check("outline n", px[2], 0xFFFF0000)
check("outline w", px[4], 0xFFFF0000)
check("outline centre", px[5], 0)
check("outline corner", px[1], 0)
check("outline cached", im:getItemOutline(4151, 1, C["java.awt.Color"].RED), ol)
local sp = C["net.runelite.client.game.SpriteManager"].__new():getSprite(776, 0)
check("sprite without api", sp:qh_ref(), nil)

local sk = C["net.runelite.client.game.SkillIconManager"].__new():getSkillImage__Skill_boolean(Skill.RUNECRAFT, true)
check("skill icon asset", sk.qh_asset, "skill_icons_small_runecraft.png")

-- utilities
local Text = C["net.runelite.client.util.Text"]
check("removeTags", Text.removeTags("<col=ff9040>Cook's Assistant</col>"), "Cook's Assistant")
check("sanitize", Text.sanitize("<img=2>Some\xC2\xA0Name"), "Some Name")
check("titleCase", Text.titleCase(C["net.runelite.api.Skill"].RUNECRAFT), "Runecraft")
check("titleCase enum", Text.titleCase(GS.LOGIN_SCREEN), "Login Screen")
check("escapeJagex", Text.escapeJagex("a<b>\nc"), "a<lt>b<gt><br>c")
check("standardize", Text.standardize(" <b>Hi\xC2\xA0There</b> "), "hi there")
local QF = C["net.runelite.client.util.QuantityFormatter"]
check("formatNumber", QF.formatNumber__long(1234567), "1,234,567")
check("formatNumber neg", QF.formatNumber__long(-1234), "-1,234")
check("stack 9999", QF.quantityToStackSize(9999), "9,999")
check("stack 12345", QF.quantityToStackSize(12345), "12.3K")
check("stack 100000", QF.quantityToStackSize(100000), "100K")
check("stack 999999", QF.quantityToStackSize(999999), "999K")
check("stack 1234567", QF.quantityToStackSize(1234567), "1.23M")
check("stack 2.5B", QF.quantityToStackSize(2500000000), "2.5B")
check("stack neg", QF.quantityToStackSize(-12345), "-12.3K")
check("rsdecimal", QF.quantityToRSDecimalStack__int(1234567), "1.2M")
local CU = C["net.runelite.client.util.ColorUtil"]
check("wrapWithColorTag", CU.wrapWithColorTag("x", C["java.awt.Color"].CYAN), "<col=00ffff>x</col>")
local ca = CU.colorWithAlpha(C["java.awt.Color"].RED, 65)
check("colorWithAlpha", ca:getRGB() & 0xFFFFFFFF, 0x41FF0000)
check("brand orange transparent", C["net.runelite.client.ui.ColorScheme"].BRAND_ORANGE_TRANSPARENT:getRGB() & 0xFFFFFFFF, 0x78DC8A00)
check("yellow interface", C["net.runelite.client.ui.JagexColors"].YELLOW_INTERFACE_TEXT:getRGB() & 0xFFFFFF, 0xFFFF00)
local JC = C["net.runelite.api.JagexColor"]
-- white at brightness 1.0: hue 0, saturation 0, luminance ceil(0.996*127) = 127
check("rgbToHSL white", JC.rgbToHSL(0xFFFFFF, 1.0), 127)
check("rgbToHSL black", JC.rgbToHSL(0x000000, 1.0), 0)
-- generated: ExperienceReward's text uses QuantityFormatter.formatNumber
local XP = C["com.questhelper.rewards.ExperienceReward"]
check("xp reward text", XP.__new__Skill_int(Skill.MAGIC, 1250):getDisplayText(), "1,250 Magic Experience")

-- contract violations abort
raises("stat of nil skill", function() client:getRealSkillLevel(nil) end)
raises("menu_end without begin", function() rl.menu_end() end)

print(string.format("rl_client: %d passed, %d failed", passes, fails))
if fails > 0 then os.exit(1) end
