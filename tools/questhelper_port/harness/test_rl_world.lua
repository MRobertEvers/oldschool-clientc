-- rt/rl_world.lua under plain Lua, against harness/mock_api.lua.
--
-- Coordinates are checked against values printed by RuneLite 1.13.6's own
-- runelite-api classes (WorldPoint, LocalPoint, WorldArea, Point,
-- Perspective; instance chunks through a proxied WorldView) -- the numbers
-- in the `java` table below. Then the generated framework is run on top:
-- ObjectCondition over Scene.getTiles, NpcRequirement over npcs().stream(),
-- QuestPerspective, Zone, VisibilityHelper, the RuneLiteObject builders.
--
--   /opt/homebrew/bin/lua tools/questhelper_port/harness/test_rl_world.lua
local HERE = debug.getinfo(1, "S").source:sub(2):match("^(.*)/") or "."
local ROOT = HERE .. "/../../../script/plugins/quest_helper"

local logs = {}
J = { protect = pcall, log = function(s) logs[#logs + 1] = s end, now_ms = function() return 0 end }
for _, f in ipairs({ "j_core", "j_lang", "j_util", "j_stream", "j_regex", "j_misc" }) do dofile(ROOT .. "/rt/" .. f .. ".lua") end
dofile(HERE .. "/mock_api.lua")
QH = { api = MOCK_API }

local function exists(p) local f = io.open(p, "r"); if f then f:close(); return true end return false end
if exists(ROOT .. "/rt/awt.lua") then dofile(ROOT .. "/rt/awt.lua") else dofile(HERE .. "/awt_standin.lua") end
local have_client = exists(ROOT .. "/rt/rl_client.lua")
if have_client then dofile(ROOT .. "/rt/rl_client.lua") end
dofile(ROOT .. "/rt/rl_world.lua")

-- generated modules on demand; classes nobody here provides are absorbing stubs
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

local fails, passes = 0, 0
local function check(name, got, want)
  if got ~= want then
    fails = fails + 1
    print("FAIL " .. name .. ": got " .. tostring(got) .. ", want " .. tostring(want))
  else
    passes = passes + 1
  end
end
local function S(x) return J.str(x) end

local WP = C["net.runelite.api.coords.WorldPoint"]
local LP = C["net.runelite.api.coords.LocalPoint"]
local WA = C["net.runelite.api.coords.WorldArea"]
local PT = C["net.runelite.api.Point"]
local WV = C["net.runelite.api.WorldView"]
local P = C["net.runelite.api.Perspective"]

local function tick() MOCK.tick = MOCK.tick + 1 end

-- a world: player at 3222,3218 -> scene origin 3168,3168 (mock: zone - 6)
MOCK.player = { name = "Te\194\160ster", true_x = 3222, true_z = 3218, level = 0,
  fine_x = (3222 - 3168) * 128 + 64, fine_z = (3218 - 3168) * 128 + 64, server_pid = 7, element_id = 900, combat_level = 3 }
MOCK.element_heights[900] = 200
tick()

local client
if have_client then
  client = J.rl_client.client()
else
  client = { getScene = function() return WV.__top():getScene() end, getPlane = function() return WV.__top():getPlane() end,
    getLocalPlayer = function() return C["net.runelite.api.Player"].__local() end,
    getTopLevelWorldView = function() return WV.__top() end, getWorldView = function(_, id) return WV.__byId(id) end }
end

-- ------------------------------------------------------------------ RuneLite's numbers

local w = WP.__new(3222, 3218, 0)
check("wp hash", w:hashCode(), 11611023)
check("wp str", w:toString(), "WorldPoint(x=3222, y=3218, plane=0)")
check("wp hash2", WP.__new(-5, 70000, 3):hashCode(), 4317977)
check("wp region", w:getRegionID() .. " " .. w:getRegionX() .. " " .. w:getRegionY(), "12850 22 18")
check("wp fromRegion", WP.fromRegion(12850, 22, 18, 0):equals(w), true)
check("wp fromCoord", S(WP.fromCoord((2 << 28) | (3222 << 14) | 3218)), "WorldPoint(x=3222, y=3218, plane=2)")
check("wp mirror", S(WP.getMirrorPoint(WP.fromRegion(12894, 10, 20, 1), true)), "WorldPoint(x=2186, y=3284, plane=1)")
check("wp dist", w:distanceTo__WorldPoint(WP.__new(3230, 3210, 0)), 8)
check("wp dist plane", w:distanceTo__WorldPoint(WP.__new(3222, 3218, 1)), 2147483647)
check("wp dist2D", w:distanceTo2D(WP.__new(3220, 3210, 1)), 8)
check("wp dz", S(w:dz(1)), "WorldPoint(x=3222, y=3218, plane=1)")
check("wp isInScene", w:isInScene__Client(client), true)
check("wp not in scene", WP.__new(3100, 3218, 0):isInScene__Client(client), false)
local map = C["java.util.HashMap"].__new__()
map:put(w, "here")
check("wp map key", map:get(WP.__new(3222, 3218, 0)), "here")
check("wp set", C["java.util.Set"].of__Object(WP.__new(1, 2, 0)):contains(WP.__new(1, 2, 0)), true)

local top = WV.__top()
local lp = LP.fromWorld__WorldView_WorldPoint(top, w)
check("lp str", S(lp), "LocalPoint(x=6976, y=6464, worldView=-1)")
check("lp hash", lp:hashCode(), 24870210)
check("lp scene", lp:getSceneX() .. " " .. lp:getSceneY(), "54 50")
check("lp dist", lp:distanceTo(lp:plus(300, -77)), 309)
check("lp dist wv", LP.__new__int_int_int(0, 0, -1):distanceTo(LP.__new__int_int_int(0, 0, 3)), 2147483647)
check("lp plane", LP.fromWorld__WorldView_WorldPoint(top, w:dz(1)), nil)
check("lp out", LP.fromWorld__Client_WorldPoint(client, WP.__new(3000, 3218, 0)), nil)
check("lp back", WP.fromLocal__Client_LocalPoint(client, lp):equals(w), true)
check("lp fromLocalInstance plain", WP.fromLocalInstance__Client_LocalPoint(client, lp):equals(w), true)
check("lp negative scene", LP.__new__int_int(-1, -129):getSceneX() .. " " .. LP.__new__int_int(-1, -129):getSceneY(), "-1 -2")

check("pt", PT.__new(12, -7):hashCode() .. " " .. S(PT.__new(12, -7)) .. " " .. PT.__new(0, 0):distanceTo(PT.__new(3, 5)), "4182 Point(x=12, y=-7) 5")

local area = WA.__new__int_int_int_int_int(3220, 3215, 3, 4, 0)
check("area", tostring(area:contains(w)) .. " " .. area:distanceTo__WorldPoint(WP.__new(3230, 3210, 0)) .. " "
  .. area:distanceTo2D__WorldPoint(WP.__new(3230, 3210, 1)) .. " " .. tostring(area:isInMeleeDistance__WorldPoint(WP.__new(3223, 3216, 0)))
  .. " " .. tostring(area:intersectsWith(WA.__new__int_int_int_int_int(3222, 3218, 2, 2, 0))) .. " " .. area:distanceTo__WorldPoint(WP.__new(3221, 3216, 1)),
  "true 8 8 true true 2147483647")
check("area list", area:toWorldPointList():size(), 12)
check("wp in area", w:isInArea(J.arr({ area }, 1)), true)

-- instance: the player's zone (scene chunk 6,6 -> absolute zone 402,402) copied
-- from template tile 1600,4000 level 1, each rotation
local inst_want = {
  [0] = "WorldPoint(x=1606, y=4002, plane=1)", "WorldPoint(x=1605, y=4006, plane=1)",
  "WorldPoint(x=1601, y=4005, plane=1)", "WorldPoint(x=1602, y=4001, plane=1)" }
for rot = 0, 3 do
  MOCK.zones = { ["0,402,402"] = { 1600, 4000, 1, rot } }
  C["net.runelite.api.TileObject"].__invalidate(); tick()
  check("instance flag " .. rot, top:isInstance(), true)
  local t = WP.fromLocalInstance__Client_LocalPoint(client, lp)
  check("instance " .. rot, S(t), inst_want[rot])
  check("instance back " .. rot, S(WP.toLocalInstance__WorldView_WorldPoint(top, t)), "[WorldPoint(x=3222, y=3218, plane=0)]")
end
check("instance regions", S(C["java.util.Arrays"].toString__intArr(top:getMapRegions())), "[6462]")
MOCK.zones = { ["0,403,402"] = { 1600, 4000, 1, 0 } }
C["net.runelite.api.TileObject"].__invalidate(); tick()
check("instance void zone", S(WP.fromLocalInstance__Client_LocalPoint(client, lp)), "WorldPoint(x=8186, y=16377, plane=3)")
MOCK.zones = {}
C["net.runelite.api.TileObject"].__invalidate(); tick()
check("not instance", top:isInstance(), false)
check("regions", S(C["java.util.Arrays"].toString__intArr(top:getMapRegions())), "[12593, 12594, 12595, 12849, 12850, 12851, 13105, 13106, 13107]")

check("sin14", P.SINE14[4097] .. " " .. P.COSINE14[4097] .. " " .. P.SINE14[1235] .. " " .. P.COSINE14[10000] .. " " .. P.SINE[513] .. " " .. P.COSINE[1501],
  "65536 0 29869 -50420 65535 -7223")

-- ------------------------------------------------------------------ actors

MOCK.npcs = {
  { server_slot = 12, npc_id = 3106, base_npc_id = 3106, name = "<col=ffff00>Hans</col>", combat_level = 0, size = 1,
    true_x = 3221, true_z = 3219, level = 0, fine_x = (3221 - 3168) * 128 + 64, fine_z = (3219 - 3168) * 128 + 64,
    element_id = 501, visible_ops = 1, health_ratio = -1, health_scale = -1, interacting = 32768 + 7 },
  { server_slot = 40, npc_id = 8000, base_npc_id = 7999, name = "Big", combat_level = 99, size = 2,
    true_x = 3225, true_z = 3220, level = 0, fine_x = (3225 - 3168) * 128 + 128, fine_z = (3220 - 3168) * 128 + 128,
    element_id = 502 },
}
MOCK.element_heights[501] = 180
tick()
local npcs = top:npcs()
local seen = {}
for _, n in J.iter(npcs) do seen[#seen + 1] = n:getId() end
check("npcs iter", table.concat(seen, ","), "3106,8000")
local hans = npcs:byIndex(12)
check("npc name", hans:getName(), "Hans")
check("npc loc", S(hans:getWorldLocation()), "WorldPoint(x=3221, y=3219, plane=0)")
check("npc lp", S(hans:getLocalLocation()), "LocalPoint(x=6848, y=6592, worldView=-1)")
check("npc index", hans:getIndex(), 12)
check("npc interacting", hans:getInteracting(), C["net.runelite.api.Player"].__local())
tick()
check("npc identity", top:npcs():byIndex(12), hans)
check("npc bySlot", C["net.runelite.api.NPC"].__bySlot(12), hans)
check("npc stream", top:npcs():stream():filter(J.fn("java.util.function.Predicate", "test", function(_, n) return n:getId() == 8000 end))
  :collect__Collector(C["java.util.stream.Collectors"].toList()):size(), 1)
local big = top:npcs():byIndex(40)
check("big area", S(big:getWorldArea():toWorldPoint()) .. " " .. big:getWorldArea():getWidth(), "WorldPoint(x=3225, y=3220, plane=0) 2")
local hull = hans:getConvexHull()
check("npc hull world", hull.__world.kind .. " " .. hull.__world.element_id, "hull 501")
local b = hull:getBounds()
-- flat mock camera: canvas = fine/4, lifted height/4 = 45 px
check("npc hull bounds", b.x .. " " .. b.y .. " " .. b.width .. " " .. b.height, "1696 1587 32 77")
check("npc hull contains", hull:contains__double_double(1710.0, 1640.0), true)
local tp = big:getCanvasTilePoly()
check("big tile poly", tp.__world.kind .. " " .. tp.__world.tile_x .. " " .. tp.__world.tile_z .. " " .. tp.__world.size_x, "tile 3225 3220 2")
check("big tile poly bounds", tp:getBounds().width, 64)
check("npc model height", hans:getLogicalHeight(), 180)
MOCK.npcs[1].element_id = -1
tick()
check("undrawn npc hull", hans:getConvexHull(), nil)
check("undrawn npc model", hans:getModel(), nil)

local me = client:getLocalPlayer()
check("player name", me:getName(), "Te ster")
check("player id", me:getId(), 7)
check("player loc", S(me:getWorldLocation()), "WorldPoint(x=3222, y=3218, plane=0)")
check("player gender", me:getPlayerComposition():getGender(), 0)
check("player view", me:getWorldView(), top)
check("player plane", client:getPlane(), 0)
MOCK.players = { { name = "Other", true_x = 3230, true_z = 3230, level = 0, fine_x = 128 * 62 + 64, fine_z = 128 * 62 + 64, server_pid = 9, element_id = 3 },
  { name = "Tester", true_x = 3222, true_z = 3218, level = 0, fine_x = 0, fine_z = 0, server_pid = 7, element_id = 900 } }
tick()
check("players", top:players():byIndex(9):getName(), "Other")
check("players local is local", top:players():byIndex(7), me)

-- minimap: no draw pass -> nil; in one, direct then extrapolated
check("minimap outside pass", hans:getMinimapLocation(), nil)
local d = MOCK_DRAW()
d.minimap_project = function(fx, fz)
  local dx, dz = (fx - MOCK.player.fine_x) / 32, (fz - MOCK.player.fine_z) / 32
  if dx * dx + dz * dz > 73 * 73 then return nil end
  return J.d2i(600 + dx), J.d2i(100 - dz)
end
QH.draw = d
local mm = hans:getMinimapLocation()
check("minimap", S(mm), "Point(x=596, y=96)")
local far = LP.__new__int_int(MOCK.player.fine_x + 128 * 30, MOCK.player.fine_z)
check("minimap far default", P.localToMinimap__Client_LocalPoint(client, far), nil)
check("minimap far long", S(P.localToMinimap__Client_LocalPoint_int(client, far, 10000000)), "Point(x=720, y=100)")
QH.draw = nil

-- ------------------------------------------------------------------ the scene

MOCK.scenery = {
  { loc_id = 1530, name = "Door", tile_x = 3220, tile_z = 3218, level = 0, size_x = 1, size_z = 1, shape = 0, angle = 2, element_id = 700, interactive = 1, visible_ops = 1 },
  { loc_id = 2000, name = "Table", tile_x = 3224, tile_z = 3214, level = 0, size_x = 2, size_z = 3, shape = 10, angle = 0, element_id = 701, interactive = 1, visible_ops = 3 },
  { loc_id = 3000, name = "Rug", tile_x = 3224, tile_z = 3214, level = 0, size_x = 1, size_z = 1, shape = 22, angle = 0, element_id = -1, interactive = 0, visible_ops = 0 },
  { loc_id = 4000, name = "Painting", tile_x = 3221, tile_z = 3214, level = 1, size_x = 1, size_z = 1, shape = 4, angle = 0, element_id = 703, interactive = 1, visible_ops = 0 },
}
MOCK.items = { { obj_id = 1925, count = 1, cost = 2, name = "Bucket", tile_x = 3223, tile_z = 3219, level = 0, element_id = 800 } }
MOCK.element_heights[701] = 80
tick()
local tiles = client:getScene():getTiles()
check("tiles planes", tiles.n, 4)
local plane0 = tiles[client:getPlane() + 1]
check("tiles rows", plane0.n .. " " .. plane0[1].n, "104 104")
local table_tile = plane0[3224 - 3168 + 1][3214 - 3168 + 1]
check("table tile", table_tile ~= nil, true)
local gos = table_tile:getGameObjects()
check("table on tile", gos.n .. " " .. gos[1]:getId(), "1 2000")
local covered = plane0[3225 - 3168 + 1][3216 - 3168 + 1]
check("table covers ne tile", covered:getGameObjects()[1]:equals(gos[1]), true)
check("table world loc", S(gos[1]:getWorldLocation()), "WorldPoint(x=3225, y=3215, plane=0)")
check("table size", gos[1]:sizeX() .. "x" .. gos[1]:sizeY(), "2x3")
check("table lp", S(gos[1]:getLocalLocation()), "LocalPoint(x=7296, y=6080, worldView=-1)")
check("table ops", tostring(gos[1]:isOpShown(1)) .. " " .. tostring(gos[1]:isOpShown(2)), "true false")
check("table hull", gos[1]:getConvexHull().__world.element_id, 701)
check("table clickbox bounds", gos[1]:getClickbox():getBounds2D():getWidth(), 64.0)
check("rug ground", table_tile:getGroundObject():getId(), 3000)
check("rug undrawn", table_tile:getGroundObject():getClickbox(), nil)
local door = plane0[3220 - 3168 + 1][3218 - 3168 + 1]:getWallObject()
check("door", door:getId() .. " " .. door:getOrientationA() .. " " .. S(door:getWorldLocation()), "1530 4 WorldPoint(x=3220, y=3218, plane=0)")
check("door class", J.inst(door, "net.runelite.api.TileObject") and J.inst(door, "net.runelite.api.WallObject"), true)
check("door renderable", J.inst(door:getRenderable1(), "net.runelite.api.Model"), true)
check("painting on plane 1", tiles[2][3221 - 3168 + 1][3214 - 3168 + 1]:getDecorativeObject():getId(), 4000)
check("empty tile nil", plane0[1][1], nil)
local item_tile = C["net.runelite.api.Tile"].__at(3223, 3219, 0)
check("tile identity", plane0[3223 - 3168 + 1][3219 - 3168 + 1], item_tile)
check("ground items", item_tile:getGroundItems():size() .. " " .. item_tile:getGroundItems():get(0):getId(), "1 1925")
check("item layer", item_tile:getItemLayer():getHeight(), 0)
check("tile item equality", item_tile:getGroundItems():get(0):equals(C["net.runelite.api.TileItem"].__fromSnap(MOCK.items[1])), true)
check("tile wl", S(item_tile:getWorldLocation()), "WorldPoint(x=3223, y=3219, plane=0)")
check("tile poly", P.getCanvasTilePoly__Client_LocalPoint(client, item_tile:getLocalLocation()).__world.tile_x, 3223)
tick()
check("tile identity next cycle", C["net.runelite.api.Tile"].__at(3223, 3219, 0), item_tile)
check("object value equality", C["net.runelite.api.TileObject"].__fromSnap(MOCK.scenery[2]):equals(gos[1]), true)
check("object hash", C["net.runelite.api.TileObject"].__fromSnap(MOCK.scenery[2]):hashCode(), gos[1]:hashCode())

-- ------------------------------------------------------------------ Perspective

local ltc = P.localToCanvas__Client_LocalPoint_int_int(client, lp, 0, 100)
check("localToCanvas lift", S(ltc), "Point(x=1744, y=1591)")
check("localToCanvas z", S(P.localToCanvas__Client_int_int_int(client, lp:getX(), lp:getY(), P.getTileHeight(client, lp, 0) - 100)), "Point(x=1744, y=1591)")
check("localToCanvas edge", P.localToCanvas__Client_int_int_int(client, 10, 10, 0), nil)
local img = C["java.awt.image.BufferedImage"].__new__int_int_int(20, 10, 2)
check("canvas image", S(P.getCanvasImageLocation(client, lp, img, 0)), "Point(x=1734, y=1611)")

-- ------------------------------------------------------------------ RuneLiteObject over api.scene

MOCK.scene_calls = {}
MOCK.seqs[808] = { frame_count = 3, frame_lengths = { 5, 5, 5 } }
local obj = client:createRuneLiteObject()
local md = client:loadModelData(4321):cloneColors():recolor(J.i2s(-8256), 99)
local merged = client:mergeModels__ModelDataArr(J.arr({ md, client:loadModelData(55) }, 2))
obj:setModel(merged:cloneColors():light__())
obj:setLocation(lp, 0)
obj:setAnimation(client:loadAnimation(808))
obj:setShouldLoop(false)
check("rlo inactive no instances", #MOCK.scene_calls, 0)
obj:setActive(true)
local names = {}
for _, c in ipairs(MOCK.scene_calls) do names[#names + 1] = c[1] end
check("rlo scene calls", table.concat(names, ","),
  "instance_create,instance_cache_model,instance_recolor,instance_light,instance_create,instance_cache_model,instance_recolor,instance_light,"
  .. "instance_position,instance_animation,instance_active,instance_position,instance_animation,instance_active")
check("rlo cache model", MOCK.scene_calls[2][3] .. " " .. MOCK.scene_calls[2][4] .. " " .. MOCK.scene_calls[6][4], "0 4321 55")
check("rlo recolor", MOCK.scene_calls[3][3] .. " " .. MOCK.scene_calls[3][4], (-8256 & 0xFFFF) .. " 99")
check("rlo position", MOCK.scene_calls[9][3] .. " " .. MOCK.scene_calls[9][4] .. " " .. MOCK.scene_calls[9][5], "3222 3218 0")
check("rlo frames", obj:getAnimation():getNumFrames() .. " " .. obj:getAnimationFrame(), "3 0")
MOCK.tick = MOCK.tick + 7
check("rlo frame later", obj:getAnimationFrame(), 1)
MOCK.tick = MOCK.tick + 20
check("rlo finished", obj:finished(), true)
check("rlo location", S(obj:getLocation()), S(lp))
-- api.cache.seq_def 'pending': 0 frames until the seq is in, then its frames
do
  local real = MOCK_API.cache.seq_def
  local landed = false
  MOCK_API.cache.seq_def = function(id)
    if id ~= 4321 then return real(id) end
    if not landed then return nil, "pending" end
    return { id = 4321, frame_count = 2, frame_lengths = { 3, 9 }, duration = 12, frame_step = -1, skeletal = false }, "ready"
  end
  local anim = client:loadAnimation(4321)
  check("seq pending reads 0 frames", anim:getNumFrames(), 0)
  landed = true
  check("seq lands later", anim:getNumFrames() .. " " .. anim:getDuration(), "2 12")
  check("seq frame lengths", anim:getFrameLengths()[2], 9)
  MOCK_API.cache.seq_def = real
end
C["net.runelite.api.RuneLiteObject"].__destroyAll()
check("rlo destroyed", MOCK.scene_calls[#MOCK.scene_calls][1], "instance_destroy")

-- ------------------------------------------------------------------ generated framework code on these classes

for _, m in ipairs(index.framework) do load_module(m) end
tick()
local ok, err = xpcall(function()
  local OC = C["com.questhelper.requirements.conditional.ObjectCondition"]
  check("ObjectCondition table", OC.__new__int(2000):check__Client(client), true)
  check("ObjectCondition at point", OC.__new__int_WorldPoint(1530, WP.__new(3220, 3218, 0)):check__Client(client), true)
  check("ObjectCondition wrong point", OC.__new__int_WorldPoint(1530, WP.__new(3221, 3218, 0)):check__Client(client), false)
  check("ObjectCondition absent", OC.__new__int(99999):check__Client(client), false)

  local NR = C["com.questhelper.requirements.npc.NpcRequirement"]
  if have_client then
    MOCK.npc_defs[3106] = { id = 3106, name = "Hans", actions = {}, size = 1, combat_level = 0 }
    MOCK.npc_defs[7999] = { id = 7999, name = "null", actions = {}, size = 2, combat_level = 0 }
  end
  check("NpcRequirement hans", NR.__new__int(3106):check__Client(client), true)
  local zone = C["com.questhelper.requirements.zone.Zone"].__new__WorldPoint_WorldPoint(WP.__new(3215, 3215, 0), WP.__new(3221, 3220, 0))
  check("NpcRequirement in zone", NR.__new__String_int_Zone("Hans", 3106, zone):check__Client(client), true)
  check("NpcRequirement big not in zone", NR.__new__String_int_Zone("Big", 8000, zone):check__Client(client), false)
  check("Zone contains", zone:contains__WorldPoint(WP.__new(3220, 3219, 0)), true)
  check("Zone excludes", zone:contains__WorldPoint(w), false)

  local QP = C["com.questhelper.steps.tools.QuestPerspective"]
  check("QP world point", S(QP.getWorldPointConsideringWorldView__Client_LocalPoint(client, lp)), S(w))
  check("QP wv world point", S(QP.getWorldPointConsideringWorldView__Client_WorldView_WorldPoint(client, top, w)), S(w))
  check("QP contains", QP.worldViewContainsWorldPoint(top, w), true)
  local areaShape = C["java.awt.geom.Area"].__new__()
  QP.addHull(areaShape, gos[1]:getConvexHull())
  check("QP hull area", areaShape:isEmpty(), false)

  local VH = C["com.questhelper.tools.VisibilityHelper"]
  local vh = VH.__new()
  check("visible table", vh:isObjectVisible(gos[1]), true)
  check("invisible rug", vh:isObjectVisibleChecker(table_tile:getGroundObject()), false)
end, function(e) return J.str(e) .. "\n" .. debug.traceback("", 2) end)
if not ok then fails = fails + 1; print("FAIL generated: " .. err) end

-- ------------------------------------------------------------------ sailing (world entities)
-- One boat (view 4, config 9) floating at 3230,3220 in the root, its deck
-- authored in the staging rectangle 6400..6415 x 6408..6415 at plane 1; the
-- mock transform is a plain shift (deck fine x,z -> hull fine + x - 1024, + z - 512).
do
  local ok2, err2 = xpcall(function()
    local boat = { id = 4, parent_view = 0, config_id = 9, fine_x = 3230 * 128 + 64, fine_z = 3220 * 128 + 64,
      angle = 512, level = 0, deck_level = 1, base_x = 6400, base_z = 6408, size_x = 16, size_z = 8 }
    MOCK_API.game.world_entity_next = function(cur)
      if cur == nil then return 1, boat end
      return nil
    end
    MOCK_API.game.world_entity_to_main = function(id, x, z)
      if id ~= 4 then return nil end
      return boat.fine_x + x - 1024, boat.fine_z + z - 512
    end
    tick()
    local tl = WV.__top()
    local es = tl:worldEntities()
    check("sailing: worldEntities lists the boat", es:size(), 1)
    local e = es:byIndex(4)
    check("sailing: byIndex(view id)", e ~= nil, true)
    check("sailing: getConfig().getId()", e and e:getConfig():getId(), 9)
    check("sailing: getOrientation", e and e:getOrientation(), 512)
    check("sailing: local location in the root", e and S(e:getLocalLocation()), S(LP.__new__int_int_int((3230 - 3168) * 128 + 64, (3220 - 3168) * 128 + 64, -1)))
    local bv = e and e:getWorldView()
    check("sailing: the boat's own world view", bv and bv:isTopLevel(), false)
    check("sailing: its id/base/plane", bv and (bv:getId() .. "," .. bv:getBaseX() .. "," .. bv:getBaseY() .. "," .. bv:getPlane()), "4,6400,6408,1")
    check("sailing: getWorldView(4) through the client", client:getWorldView(4) == bv, true)
    check("sailing: worldViews = top + the boat", tl:worldViews():size(), 2)
    local WES = C["com.questhelper.steps.WorldEntityStep"]
    check("sailing: WorldEntityStep finds the boat by config id", WES.findWorldEntityInWorldView(tl, 9) == e, true)
    check("sailing: an unknown config id finds none", WES.findWorldEntityInWorldView(tl, 10), nil)
    local QP = C["com.questhelper.steps.tools.QuestPerspective"]
    check("sailing: no deck outline (the boat world holds no tiles)", QP.getAreaFromWorldView(client, bv), nil)
    -- a point aboard: staging tile 6402,6410 -> deck-local fine 2*128+64, 2*128+64
    local aboard = WP.__new(6402, 6410, 1)
    local deck_lp = LP.fromWorld__WorldView_WorldPoint(bv, aboard)
    check("sailing: LocalPoint.fromWorld(boat, aboard) carries the boat's id", deck_lp and deck_lp:getWorldView(), 4)
    local main = e:transformToMainWorld(deck_lp)
    check("sailing: transformToMainWorld through the entity", main and S(main),
      S(LP.__new__int_int_int(boat.fine_x + 320 - 1024 - 3168 * 128, boat.fine_z + 320 - 512 - 3168 * 128, -1)))
    check("sailing: a root point is returned as it is", e:transformToMainWorld(LP.__new__int_int(100, 100)):getX(), 100)
    local want = WP.__new((boat.fine_x + 320 - 1024) // 128, (boat.fine_z + 320 - 512) // 128, 0)
    check("sailing: QuestPerspective maps an aboard point to the main world",
      S(QP.getWorldPointConsideringWorldView__Client_WorldView_WorldPoint(client, bv, aboard)), S(want))
    -- the local player aboard: its true tile is in the staging rectangle
    local saved = { MOCK.player.true_x, MOCK.player.true_z, MOCK.player.level }
    MOCK.scene_origin = { 3168, 3168 }
    MOCK.player.true_x, MOCK.player.true_z, MOCK.player.level = 6402, 6410, 1
    tick()
    local me = C["net.runelite.api.Player"].__local()
    check("sailing: a player aboard is in the boat's world view", me:getWorldView():getId(), 4)
    check("sailing: and the main-world point of where it stands",
      S(QP.getWorldPointConsideringWorldView__Client_WorldView_WorldPoint(client, me:getWorldView(), me:getWorldLocation())), S(want))
    check("sailing: a boat-view point draws where the boat carries it",
      S(P.localToCanvas__Client_LocalPoint_int(client, deck_lp, 0)), S(P.localToCanvas__Client_LocalPoint_int(client, main, 0)))
    check("sailing: the top level's plane aboard is the one the boat floats on", WV.__top():getPlane(), 0)
    MOCK.player.true_x, MOCK.player.true_z, MOCK.player.level = saved[1], saved[2], saved[3]
    MOCK.scene_origin = nil
    MOCK_API.game.world_entity_next, MOCK_API.game.world_entity_to_main = nil, nil
    tick()
    check("sailing: ashore again, the top level", C["net.runelite.api.Player"].__local():getWorldView():isTopLevel(), true)
    check("sailing: no boats without the api", WV.__top():worldEntities():size(), 0)
  end, function(e) return J.str(e) .. "\n" .. debug.traceback("", 2) end)
  if not ok2 then fails = fails + 1; print("FAIL sailing: " .. err2) end
end

print(string.format("rl_world: %d passed, %d failed", passes, fails))
if fails > 0 then os.exit(1) end
