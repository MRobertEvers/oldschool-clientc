-- rt/awt.lua + rt/rl_overlay.lua under plain Lua, against harness/mock_api.lua.
--
-- The java.awt pieces on their own (Color, geometry, Graphics2D over a
-- recording draw object, images through a mock image store), then the
-- generated framework drawing through them: DirectionArrow, IconOverlay,
-- PolyZone, QuestStep's panel lines inside the real QuestHelperOverlay,
-- QuestHelperWorldMapPoint on the world map, OverlayUtil on rl_world's tile
-- polygons.
--
--   /opt/homebrew/bin/lua tools/questhelper_port/harness/test_awt.lua
local HERE = debug.getinfo(1, "S").source:sub(2):match("^(.*)/") or "."
local ROOT = HERE .. "/../../../script/plugins/quest_helper"

local logs = {}
J = { protect = pcall, log = function(s) logs[#logs + 1] = s end, now_ms = function() return 0 end }
for _, f in ipairs({ "j_core", "j_lang", "j_util", "j_stream", "j_regex", "j_misc" }) do dofile(ROOT .. "/rt/" .. f .. ".lua") end
dofile(HERE .. "/mock_api.lua")
QH = { api = MOCK_API }

local function exists(p) local f = io.open(p, "r"); if f then f:close(); return true end return false end
dofile(ROOT .. "/rt/awt.lua")
dofile(ROOT .. "/rt/rl_overlay.lua")
local have_world = exists(ROOT .. "/rt/rl_world.lua")
local have_client = exists(ROOT .. "/rt/rl_client.lua")
if have_client then dofile(ROOT .. "/rt/rl_client.lua") end
if have_world then dofile(ROOT .. "/rt/rl_world.lua") end

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
for _, m in ipairs(index.framework) do load_module(m) end

local fails, passes = 0, 0
local function check(name, got, want)
  if got ~= want then
    fails = fails + 1
    print("FAIL " .. name .. ": got " .. tostring(got) .. ", want " .. tostring(want))
  else
    passes = passes + 1
  end
end
local function ok(name, cond) check(name, cond and true or false, true) end

local Color = C["java.awt.Color"]
local Rect = C["java.awt.Rectangle"]
local Poly = C["java.awt.Polygon"]
local G2 = C["java.awt.Graphics2D"]
local BI = C["java.awt.image.BufferedImage"]
local O = "net.runelite.client.ui.overlay."

local function draws(kind)
  local out = {}
  for _, d in ipairs(MOCK.draws) do if kind == nil or d[1] == kind then out[#out + 1] = d end end
  return out
end
local function reset() MOCK.draws = {} end
local function with_draw(fn)
  local d = MOCK_DRAW_AWT()
  QH.draw = d
  local g = G2.qh_over(d)
  fn(g, d)
  QH.draw = nil
end

-- ====================================================================== Color

do
  local c = Color.__new__int_int_int_int(10, 20, 30, 40)
  check("color rgb signed", c:getRGB(), J.l2i(0x280A141E))
  check("color qh_rgb", c:qh_rgb(), 0x0A141E)
  check("color alpha", c:getAlpha(), 40)
  check("WHITE rgb", Color.WHITE:getRGB(), -1)
  local d = Color.__new__int_int_int(100, 200, 50):darker()
  check("darker r", d:getRed(), 70); check("darker g", d:getGreen(), 140); check("darker b", d:getBlue(), 35)
  check("decode", Color.decode("#ff9040"):qh_rgb(), 0xff9040)
  check("Color(int,true)", Color.__new__int_boolean(0x80112233, true):getAlpha(), 0x80)
  ok("equals", Color.__new__int_int_int(1, 2, 3):equals(Color.__new__int_int_int(1, 2, 3)))
  local bad = pcall(Color.__new__int_int_int, 256, 0, 0)
  check("out of range throws", bad, false)
  local back = Color.__new__int(Color.HSBtoRGB(table.unpack(Color.RGBtoHSB(12, 200, 99, nil), 1, 3)))
  check("HSB round trip", back:qh_rgb(), (12 << 16) | (200 << 8) | 99)
end

-- ====================================================================== geometry

do
  local r = Rect.__new__int_int_int_int(10, 10, 20, 10)
  ok("rect contains", r:contains__int_int(10, 10))
  ok("rect !contains edge", not r:contains__int_int(30, 15))
  ok("rect contains double", r:contains__double_double(29.5, 19.5))
  ok("rect intersects", r:intersects__Rectangle(Rect.__new__int_int_int_int(29, 19, 5, 5)))
  ok("rect !intersects", not r:intersects__Rectangle(Rect.__new__int_int_int_int(30, 10, 5, 5)))
  check("centerX", r:getCenterX(), 20.0)
  check("maxY", r:getMaxY(), 20)
  local u = r:union(Rect.__new__int_int_int_int(0, 0, 5, 5))
  check("union", u:toString(), "java.awt.Rectangle[x=0,y=0,width=30,height=20]")

  local p = Poly.__new__()
  p:addPoint(0, 0); p:addPoint(10, 0); p:addPoint(10, 10); p:addPoint(0, 10)
  check("poly bounds", p:getBounds():toString(), "java.awt.Rectangle[x=0,y=0,width=10,height=10]")
  ok("poly contains", p:contains__double_double(5.0, 5.0))
  ok("poly !contains", not p:contains__double_double(11.0, 5.0))
  check("poly xpoints", p.xpoints[3], 10)

  local tri = Poly.__new__intArr_intArr_int(J.arr({ 20, 30, 25 }, 3), J.arr({ 0, 0, 10 }, 3), 3)
  local a = C["java.awt.geom.Area"].__new__Shape(p)
  a:add(C["java.awt.geom.Area"].__new__Shape(tri))
  ok("area contains first", a:contains__double_double(1.0, 1.0))
  ok("area contains second", a:contains__double_double(25.0, 3.0))
  ok("area !contains gap", not a:contains__double_double(15.0, 3.0))
  check("area bounds", a:getBounds():toString(), "java.awt.Rectangle[x=0,y=0,width=30,height=10]")
  ok("empty area", C["java.awt.geom.Area"].__new__():isEmpty())

  local l = C["java.awt.geom.Line2D$Double"].__new__double_double_double_double(1.0, 2.0, 5.0, 8.0)
  check("line field x2", l.x2, 5.0)
  check("line bounds", l:getBounds():toString(), "java.awt.Rectangle[x=1,y=2,width=4,height=6]")

  local t = C["java.awt.geom.AffineTransform"].__new__()
  t:translate(10, 20)
  t:rotate__double(math.pi / 2)
  local x, y = t:qh_apply(1, 0)
  check("affine x", math.floor(x + 0.5), 10)
  check("affine y", math.floor(y + 0.5), 21)

  -- the generated PolyZone over rl_world's WorldPoint
  if have_world then
    local WP = C["net.runelite.api.coords.WorldPoint"]
    local pts = C["java.util.ArrayList"].__new__()
    for _, xy in ipairs({ { 3200, 3200 }, { 3210, 3200 }, { 3210, 3210 }, { 3200, 3210 } }) do pts:add(WP.__new(xy[1], xy[2], 0)) end
    local zone = C["com.questhelper.requirements.zone.PolyZone"].__new(pts)
    ok("PolyZone contains", zone:contains__WorldPoint(WP.__new(3205, 3205, 0)))
    ok("PolyZone !contains", not zone:contains__WorldPoint(WP.__new(3215, 3205, 0)))
  end
end

-- ====================================================================== fonts

do
  local TA = C["java.awt.font.TextAttribute"]
  local f = C["net.runelite.client.ui.FontManager"].getRunescapeFont()
  local attrs = C["java.util.HashMap"].__new__Map(f:getAttributes())
  attrs:put(TA.UNDERLINE, TA.UNDERLINE_ON)
  local u = f:deriveFont__Map(attrs)
  ok("underlined", u:qh_isUnderlined())
  ok("source not underlined", not f:qh_isUnderlined())
  check("derive bold", f:deriveFont__int_float(1, 12.0):isBold(), true)
  with_draw(function(g)
    check("stringWidth", g:getFontMetrics__Font(f):stringWidth("abcd"), 24)
    check("height", g:getFontMetrics__():getHeight(), 12)
  end)
  local outside = pcall(function() return g end)
  ok("FontMetrics outside a pass aborts", not pcall(function()
    C["java.awt.FontMetrics"].__new__Font(f):stringWidth("x")
  end))
end

-- ====================================================================== Graphics2D primitives

reset()
with_draw(function(g)
  g:setColor(Color.__new__int_int_int_int(255, 0, 0, 100))
  g:fillRect(10, 20, 30, 40)
  local r = draws("rect")
  check("fillRect call", #r, 1)
  check("fillRect args", table.concat({ r[1][2], r[1][3], r[1][4], r[1][5], r[1][6], r[1][7] }, ","), "10,20,30,40,16711680,100")

  reset()
  g:setColor(Color.WHITE)
  g:drawRect(0, 0, 10, 5)
  local rr = draws("rect")
  check("drawRect = 4 rects", #rr, 4)
  check("drawRect top", table.concat({ rr[1][2], rr[1][3], rr[1][4], rr[1][5] }, ","), "0,0,11,1")
  check("drawRect right", table.concat({ rr[4][2], rr[4][3], rr[4][4], rr[4][5] }, ","), "10,1,1,4")

  reset()
  g:setColor(Color.__new__int_int_int_int(0, 255, 0, 128))
  g:draw(C["java.awt.geom.Line2D$Double"].__new__double_double_double_double(0.0, 0.0, 10.0, 0.0))
  local ln = draws("line")
  check("thin line", #ln, 1)
  check("thin line alpha", ln[1][7], 128)

  reset()
  g:setStroke(C["java.awt.BasicStroke"].__new__float(4.0))
  g:draw(C["java.awt.geom.Line2D$Double"].__new__double_double_double_double(0.0, 10.0, 10.0, 10.0))
  local pq = draws("polygon")
  check("thick line is a quad", #pq, 1)
  check("quad has 4 points", #pq[1][2], 8)
  -- square caps: x from -2 to 12, y from 8 to 12
  local xs, ys = {}, {}
  for i = 1, 8, 2 do xs[#xs + 1] = pq[1][2][i]; ys[#ys + 1] = pq[1][2][i + 1] end
  check("cap min x", math.min(table.unpack(xs)), -2)
  check("cap max x", math.max(table.unpack(xs)), 12)
  check("half width", math.max(table.unpack(ys)) - math.min(table.unpack(ys)), 4)
  g:setStroke(C["java.awt.BasicStroke"].__new__float(1.0))

  reset()
  g:setClip__Shape(Rect.__new__int_int_int_int(0, 0, 50, 50))
  g:drawLine(-10, 25, 100, 25)
  local cl = draws("line")
  check("clipped line x0", cl[1][2], 0)
  check("clipped line x1", cl[1][4], 50)
  g:setClip__Shape(nil)

  reset()
  g:setColor(Color.YELLOW)
  g:drawString__String_int_int("hello", 10, 30)
  local tx = draws("text")
  check("text centred x", tx[1][2], 10 + (5 * 6) // 2)
  check("text baseline", tx[1][3], 30)
  check("text rgb", tx[1][5], 0xFFFF00)

  reset()
  g:drawString__String_int_int(string.rep("x", 200), 0, 10)
  local long = draws("text")
  check("long text in pieces", #long, 3)
  check("second piece starts after the first", long[2][2], 96 * 6 + (96 * 6) // 2)

  reset()
  local g2 = g:create__()
  g2:translate__int_int(100, 0)
  g2:fillRect(0, 0, 1, 1)
  g2:dispose()
  g:fillRect(0, 0, 1, 1)
  local tr = draws("rect")
  check("create__ copies, translation private", tr[1][2] .. "/" .. tr[2][2], "100/0")
end)

-- ====================================================================== the generated DirectionArrow

reset()
with_draw(function(g)
  local DA = C["com.questhelper.steps.overlay.DirectionArrow"]
  DA.drawWorldArrow(g, Color.CYAN, 200, 150)
  local polys = draws("polygon")
  -- black shaft + black head, then the cyan shaft + cyan head
  check("world arrow draws 4 polygons", #polys, 4)
  check("first is black", polys[1][3], 0)
  check("last is cyan", polys[4][3], 0x00FFFF)
  -- the head is drawn under setTransform(translate(x2, y2) rotate): its points sit around (200, 150)
  local hx, hy = 0, 0
  for i = 1, #polys[2][2], 2 do hx = hx + polys[2][2][i]; hy = hy + polys[2][2][i + 1] end
  local n = #polys[2][2] // 2
  ok("head near the tip x", math.abs(hx / n - 200) <= 8)
  ok("head near the tip y", math.abs(hy / n - 150) <= 8)

  reset()
  local line = C["java.awt.geom.Line2D$Double"].__new__double_double_double_double(10.0, 10.0, 300.0, 10.0)
  DA.drawLine(g, line, Color.RED, Rect.__new__int_int_int_int(0, 0, 100, 100))
  local l = draws("line")
  check("minimap line clipped to the widget", l[1][4], 100)
end)

-- ====================================================================== world shapes

local function tile_poly(x, z, level, sx)
  local p = Poly.__new__()
  p:addPoint(0, 0); p:addPoint(20, 0); p:addPoint(20, 10); p:addPoint(0, 10)
  p.__world = { kind = "tile", tile_x = x, tile_z = z, level = level, size_x = sx or 1, size_z = sx or 1 }
  return p
end
local function hull_poly(id)
  local p = Poly.__new__()
  p:addPoint(50, 50); p:addPoint(70, 50); p:addPoint(70, 90); p:addPoint(50, 90)
  p.__world = { kind = "hull", element_id = id }
  return p
end

reset()
with_draw(function(g)
  g:setColor(Color.__new__int_int_int_int(0, 0, 255, 60))
  g:fill(tile_poly(3200, 3201, 0))
  local wt = draws("world_tile")
  check("tile fill", table.concat({ wt[1][2], wt[1][3], wt[1][4], wt[1][5], wt[1][7], wt[1][8] }, ","), "3200,3201,0,255,60,0")
  reset()
  g:setStroke(C["java.awt.BasicStroke"].__new__float(3.0))
  g:draw(tile_poly(3200, 3201, 0))
  wt = draws("world_tile")
  check("tile border", table.concat({ wt[1][7], wt[1][8] }, ","), "0,3")
  reset()
  g:fill(tile_poly(3200, 3201, 0, 2))
  check("2x2 fill = 4 tiles", #draws("world_tile"), 4)
  reset()
  g:draw(tile_poly(3200, 3201, 0, 2))
  check("2x2 border = the polygon's outline", #draws("world_tile"), 0)
  ok("2x2 border drew", #draws("polygon") == 4)

  reset()
  local OU = C[O .. "OverlayUtil"]
  OU.renderPolygon__Graphics2D_Shape_Color(g, tile_poly(1, 2, 3), Color.GREEN)
  wt = draws("world_tile")
  check("renderPolygon: one engine call", #MOCK.draws, 1)
  check("renderPolygon: wash 50, border 2", table.concat({ wt[1][5], wt[1][6], wt[1][7], wt[1][8] }, ","), "0,65280,50,2")

  reset()
  MOCK.pointer = { 60, 60 }
  local mouse = C["net.runelite.api.Point"] and have_world and C["net.runelite.api.Point"].__new(60, 60)
    or { getX = function() return 60 end, getY = function() return 60 end }
  OU.renderHoverableArea(g, hull_poly(77), mouse, Color.__new__int_int_int_int(255, 0, 0, 20), Color.RED:darker(), Color.RED)
  local wh = draws("world_hull")
  check("hover hull: one call", #MOCK.draws, 1)
  check("hover hull: hovered border", wh[1][3], 0xFF0000)
  check("hover hull: fill alpha", wh[1][4], 20)

  reset()
  local area = C["java.awt.geom.Area"].__new__()
  area:add(C["java.awt.geom.Area"].__new__Shape(hull_poly(5)))
  area:add(C["java.awt.geom.Area"].__new__Shape(hull_poly(6)))
  OU.renderHoverableArea(g, area, mouse, Color.__new__int_int_int_int(255, 0, 0, 20), Color.BLUE, Color.RED)
  wh = draws("world_hull")
  check("area of hulls: each natively", #wh, 2)
  check("area of hulls: second id", wh[2][2], 6)

  reset()
  local MOR = C[O .. "outline.ModelOutlineRenderer"].__new()
  local actor = { getConvexHull = function() return hull_poly(42) end }
  MOR:drawOutline__Actor_int_Color_int(actor, 2, Color.ORANGE, 4)
  wh = draws("world_hull")
  check("outline: mesh", wh[1][5], "mesh")
  check("outline: element", wh[1][2], 42)
  reset()
  MOR:drawOutline__Actor_int_Color_int({ getConvexHull = function() return nil end }, 2, Color.ORANGE, 4)
  check("outline of an undrawn actor: nothing", #MOCK.draws, 0)
end)

-- rl_world's own tile polygon, through the real OverlayUtil
if have_world then
  reset()
  with_draw(function(g)
    local LP = C["net.runelite.api.coords.LocalPoint"]
    local P = C["net.runelite.api.Perspective"]
    local ok_, poly = pcall(P.getCanvasTilePoly__Client_LocalPoint, nil, LP.__new__int_int(64 + 128 * 10, 64 + 128 * 12))
    if ok_ and poly ~= nil then
      C[O .. "OverlayUtil"].renderPolygon__Graphics2D_Shape_Color(g, poly, Color.YELLOW)
      check("rl_world tile poly routes to world_tile", #draws("world_tile"), 1)
    else
      print("note: rl_world getCanvasTilePoly under the mock answered " .. tostring(poly))
    end
  end)
end

-- ====================================================================== images

MOCK_IMAGES_INSTALL()
MOCK.image_assets["icon_background.png"] = { w = 32, h = 32, px = (function()
  local t = {}
  for i = 1, 32 * 32 do t[i] = 0xFF101010 end
  return t
end)() }

do
  local item_ref = MOCK_IMAGE(36, 32, 0xFFFF0000)
  local item = BI.qh_of_ref(item_ref)
  check("ref image width from the client", item:getWidth(), 36)
  local icon = C["com.questhelper.steps.overlay.IconOverlay"].createIconImage(item)
  check("icon is the background's size", icon:getWidth() .. "x" .. icon:getHeight(), "32x32")
  local px = icon:qh_pixels()
  ok("icon composed", px ~= nil)
  -- the item is scaled to 32 x 28 and centred: row 16 is red, row 0 is background
  check("icon centre is the item", px[16 * 32 + 16 + 1], 0xFFFF0000)
  check("icon corner is the background", px[1], 0xFF101010)
  reset()
  with_draw(function(g)
    g:drawImage__Image_int_int_ImageObserver(icon, 5, 6, nil)
    g:drawImage__Image_int_int_ImageObserver(icon, 7, 8, nil)
  end)
  check("composed once for two draws", MOCK.composes, 1)
  local im = draws("image")
  check("drawn twice", #im, 2)
  check("draw position", im[2][3] .. "," .. im[2][4], "7,8")

  -- a pending asset: nothing drawn until it is ready, then composed
  MOCK.image_assets["quest_step_arrow.png"] = { w = 49, h = 49, px = {}, pending = true }
  local arrow = C["net.runelite.client.util.ImageUtil"].loadImageResource(nil, "/quest_step_arrow.png")
  check("pending asset has its known size", arrow:getWidth(), 49)
  reset()
  with_draw(function(g) ok("pending draw answers false", g:drawImage__Image_int_int_ImageObserver(arrow, 0, 0, nil) == false) end)
  check("pending: nothing drawn", #MOCK.draws, 0)
  local px49 = {}
  for i = 1, 49 * 49 do px49[i] = 0xFF00FF00 end
  MOCK.image_assets["quest_step_arrow.png"] = { w = 49, h = 49, px = px49 }
  reset()
  with_draw(function(g) g:drawImage__Image_int_int_ImageObserver(arrow, 0, 0, nil) end)
  check("ready: drawn", #draws("image"), 1)

  -- fillImage and a smooth resize, derived from a source that is pending first
  local pending = true
  local src = BI.qh_of_source(function() if pending then return nil end return item_ref end, 36, 32)
  local filled = C["net.runelite.client.util.ImageUtil"].fillImage(src, Color.__new__int_int_int_int(0, 0, 255, 65))
  ok("derived from pending: no pixels", filled:qh_pixels() == nil)
  pending = false
  check("fillImage", filled:qh_pixels()[1], 0x410000FF)
  local small = C["net.runelite.client.util.ImageUtil"].resizeImage__BufferedImage_int_int_boolean(item, 18, 18, true)
  check("resize keeps aspect", small:getWidth() .. "x" .. small:getHeight(), "18x16")
  check("resize pixel", small:qh_pixels()[1], 0xFFFF0000)

  -- rl_client's spelling: a canvas image carrying host_image
  local host = BI.__new__int_int_int(36, 32, 2)
  host.host_image = function() return item_ref, "ready" end
  reset()
  with_draw(function(g) g:drawImage__Image_int_int_ImageObserver(host, 1, 2, nil) end)
  check("host_image drawn by its ref", draws("image")[1][2], item_ref)

  -- scaled drawImage composes a scaled copy once
  local before = MOCK.composes
  reset()
  with_draw(function(g)
    g:drawImage__Image_int_int_int_int_ImageObserver(item, 0, 0, 18, 16, nil)
    g:drawImage__Image_int_int_int_int_ImageObserver(item, 0, 0, 18, 16, nil)
  end)
  check("scaled draw composed once", MOCK.composes - before, 1)
  check("clip makes image_clip", (function()
    reset()
    with_draw(function(g)
      g:setClip__Shape(Rect.__new__int_int_int_int(0, 0, 10, 10))
      g:drawImage__Image_int_int_ImageObserver(item, 5, 5, nil)
    end)
    return #draws("image_clip")
  end)(), 1)
end

-- ====================================================================== overlays and QH.render

QH.render_reset()
local OM = C[O .. "OverlayManager"].__new()

-- the real QuestHelperOverlay, its step adding lines through the real QuestStep helpers
local plugin_stub = {}
local step_lines = { "Talk to the Cook in the kitchen of Lumbridge Castle, then bring him the ingredients he asks for." }
local questhelper_stub = { getQuest = function() return { getName = function() return "Cook's Assistant" end } end }
local step = {
  getActiveStep = function(s) return { isShouldOverlayWidget = function() return false end } end,
  makeOverlayHint = function(s, panel, plugin, a, b)
    local QS = C["com.questhelper.steps.QuestStep"]
    QS.addTitleToPanel({ questHelper = questhelper_stub }, panel)
    for _, l in ipairs(step_lines) do QS.addTextToPanel({}, panel, l) end
  end,
}
local selected = { getCurrentStep = function() return step end }
function plugin_stub.getConfig__() return { showOverlay = function() return true end } end
function plugin_stub.getSelectedQuest() return selected end
function plugin_stub.getQuestOverlayManager() return { updateOverlay = function() end } end

local qho = C["com.questhelper.overlays.QuestHelperOverlay"].__new(plugin_stub)
check("QuestHelperOverlay layer", qho:getLayer(), C[O .. "OverlayLayer"].UNDER_WIDGETS)
check("QuestHelperOverlay priority", qho:getPriority(), 1.0)
ok("add", OM:add(qho))
ok("add twice is false", not OM:add(qho))

-- a DYNAMIC scene overlay that draws a tile, and one whose render throws
J.def("test.SceneOverlay", { kind = "class", super = O .. "Overlay", ifaces = {}, body = function(cls, super)
  function cls.__init(self) super.__init__(self); self:setPosition(C[O .. "OverlayPosition"].DYNAMIC); self:setLayer(C[O .. "OverlayLayer"].ABOVE_SCENE) end
  function cls.render(self, g) g:setColor(Color.RED); g:fill(tile_poly(9, 9, 0)); return nil end
end })
J.def("test.BrokenOverlay", { kind = "class", super = O .. "Overlay", ifaces = {}, body = function(cls, super)
  function cls.__init(self) super.__init__(self); self:setPosition(C[O .. "OverlayPosition"].DYNAMIC); self:setLayer(C[O .. "OverlayLayer"].ABOVE_SCENE) end
  function cls.render(self, g) J.throw(C["java.lang.IllegalStateException"].__new__String("boom")) end
end })
local broken = C["test.BrokenOverlay"].__new()
OM:add(broken)
OM:add(C["test.SceneOverlay"].__new())

reset()
QH.render("world", MOCK_DRAW_AWT())
check("world pass: the scene overlay's tile", #draws("world_tile"), 1)
-- UNDER_WIDGETS (the TOP_LEFT step panel) is drawn before the widgets, as
-- RuneLite's OverlayRenderer does: the world pass, so an interface (the world
-- map's Key panel) covers it.
ok("world pass: the UNDER_WIDGETS step panel draws here", #draws("text") > 0)
ok("broken overlay logged", #logs >= 1 and logs[#logs]:find("boom", 1, true) ~= nil)
local nlogs = #logs
QH.render("world", MOCK_DRAW_AWT())
check("broken overlay logged once", #logs, nlogs)
ok("QH.draw cleared after the pass", QH.draw == nil)

reset()
QH.render("canvas", MOCK_DRAW_AWT())
check("canvas pass: no UNDER_WIDGETS panel over the interfaces", #draws("text"), 0)
reset()
QH.render("world", MOCK_DRAW_AWT())
local bg = draws("rect")[1]
check("panel background at the top-left snap corner", bg[2] .. "," .. bg[3], "5,20")
check("panel background colour", bg[6], (70 << 16) | (61 << 8) | 50)
check("panel background alpha", bg[7], 156)
local texts = draws("text")
-- shadow + text per run: the title is one line, the long step text wraps
ok("panel drew text", #texts >= 4)
check("title shadow first, black", texts[1][5], 0)
local lines_y = {}
for _, t in ipairs(texts) do if t[5] ~= 0 then lines_y[t[3]] = true end end
local n_lines = 0
for _ in pairs(lines_y) do n_lines = n_lines + 1 end
ok("long step text wrapped onto several lines", n_lines >= 4)
check("step text colour", texts[4][5], (190 << 16) | (190 << 8) | 190)
check("children cleared after render", qho.panelComponent:getChildren():size(), 0)
-- the second frame sizes the background from the first frame's children
reset()
QH.render("world", MOCK_DRAW_AWT())
local bg2 = draws("rect")[1]
check("second frame: background width", bg2[4], 129)
ok("second frame: background height covers the lines", bg2[5] >= n_lines * 12)
check("bounds recorded", qho:getBounds().x .. "," .. qho:getBounds().y, "5,20")

-- LineComponent right column
reset()
with_draw(function(g)
  local LC = C[O .. "components.LineComponent"]
  local pc = C[O .. "components.PanelComponent"].__new()
  pc:getChildren():add(LC.builder():left("Left"):right("R"):rightColor(Color.GREEN):build())
  local d = pc:render(g)
  local t = draws("text")
  -- right text starts at x = 0 + 4 + (129 - 8) - width("R") = 119; centred at 119 + 3
  check("right-aligned text", t[4][2], 4 + 121 - 6 + 3)
  check("right colour", t[4][5], 0x00FF00)
  check("empty panel renders nothing", C[O .. "components.PanelComponent"].__new():render(g), nil)
  local title = C[O .. "components.TitleComponent"].builder():text("Title"):color(Color.ORANGE):build()
  local td = title:render(g)
  check("title dimension", td.width .. "x" .. td.height, "129x12")
end)

-- tooltips: at the mouse, </br> lines, <col=>, cleared after the frame
QH.render_reset()
local TM = C[O .. "tooltip.TooltipManager"].__new()
TM:add(C[O .. "tooltip.Tooltip"].__new__String("First</br><col=ff0000>Second"))
MOCK.pointer = { 100, 200 }
reset()
QH.render("canvas", MOCK_DRAW_AWT())
local trect = draws("rect")[1]
check("tooltip under the mouse", trect[2] .. "," .. trect[3], "100,224")
local tt = draws("text")
check("tooltip second line red", tt[4][5], 0xFF0000)
ok("tooltip lines on different baselines", tt[2][3] ~= tt[4][3])
reset()
QH.render("canvas", MOCK_DRAW_AWT())
check("tooltips cleared after a frame", #MOCK.draws, 0)

-- world map points: the real QuestHelperWorldMapPoint
QH.render_reset()
local px32 = {}
for i = 1, 32 * 39 do px32[i] = 0xFF202020 end
MOCK.image_assets["util_clue_arrow.png"] = { w = 32, h = 39, px = px32 }
local arrowpx = {}
for i = 1, 49 * 49 do arrowpx[i] = 0xFF00FF00 end
MOCK.image_assets["quest_step_arrow.png"] = { w = 49, h = 49, px = arrowpx }
if have_world then
  local WP = C["net.runelite.api.coords.WorldPoint"]
  local ok_mp, mp = pcall(C["com.questhelper.tools.QuestHelperWorldMapPoint"].__new, WP.__new(3222, 3218, 0), BI.qh_of_ref(MOCK_IMAGE(20, 20, 0xFFFFFFFF)))
  if not ok_mp then
    print("note: QuestHelperWorldMapPoint could not be built here: " .. J.str(mp))
  else
    C[O .. "worldmap.WorldMapPointManager"].__new():add(mp)
    check("map point image is the clue-arrow composite", mp:getImage():getWidth() .. "x" .. mp:getImage():getHeight(), "32x39")
    MOCK.worldmap = { x = 100, y = 50, width = 400, height = 300 }
    MOCK.worldmap_project = function(x, z, l) return 300, 200 end
    reset()
    QH.render("canvas", MOCK_DRAW_AWT())
    local im = draws("image")
    check("map point drawn", #im, 1)
    -- imagePoint (16, 39): drawn at (300 - 16, 200 - 39)
    check("map point anchored at its image point", im[1][3] .. "," .. im[1][4], "284,161")
    MOCK.worldmap_project = function(x, z, l) return 900, 200 end
    reset()
    QH.render("canvas", MOCK_DRAW_AWT())
    ok("off the map: edge snapped", mp:isCurrentlyEdgeSnapped())
    check("snapped image is the arrow", mp:getImage():getWidth(), 49)
    im = draws("image")
    check("snapped icon drawn", #im, 1)
    -- the snap frame still draws the image it measured (RuneLite's order),
    -- clamped to the map's right edge less half that image, now centred
    check("snap frame x", im[1][3], 100 + 400 - 16 - 16)
    reset()
    QH.render("canvas", MOCK_DRAW_AWT())
    im = draws("image")
    check("next frame: the arrow, at the edge less half the arrow", im[1][3], 100 + 400 - 24 - 24)
    MOCK.worldmap = nil
    reset()
    QH.render("canvas", MOCK_DRAW_AWT())
    check("map closed: nothing", #draws("image"), 0)
  end
end

-- pass routing of a positioned ALWAYS_ON_TOP overlay and the corner stacking
QH.render_reset()
J.def("test.BoxOverlay", { kind = "class", super = O .. "OverlayPanel", ifaces = {}, body = function(cls, super)
  function cls.__init(self, label) super.__init__(self); self.label = label; self:setLayer(C[O .. "OverlayLayer"].ALWAYS_ON_TOP) end
  function cls.render(self, g)
    self.panelComponent:getChildren():add(C[O .. "components.LineComponent"].builder():left(self.label):build())
    return C[O .. "OverlayPanel"].render(self, g)
  end
end })
local b1, b2 = C["test.BoxOverlay"].__new("one"), C["test.BoxOverlay"].__new("two")
OM:add(b1); OM:add(b2)
for _ = 1, 2 do reset(); QH.render("canvas", MOCK_DRAW_AWT()) end
check("first box at the corner", b1:getBounds().y, 20)
check("second box stacked under the first", b2:getBounds().y, 20 + b1:getBounds().height + 2)
reset()
QH.render("world", MOCK_DRAW_AWT())
check("positioned ALWAYS_ON_TOP overlays are not in the world pass", #MOCK.draws, 0)
-- the same panel on UNDER_WIDGETS moves under the interfaces (the world pass)
b1:setLayer(C[O .. "OverlayLayer"].UNDER_WIDGETS); b2:setLayer(C[O .. "OverlayLayer"].UNDER_WIDGETS)
reset(); QH.render("canvas", MOCK_DRAW_AWT())
check("positioned UNDER_WIDGETS overlays are not in the canvas pass", #draws("text"), 0)
reset(); QH.render("world", MOCK_DRAW_AWT())
ok("positioned UNDER_WIDGETS overlays draw in the world pass", #draws("text") > 0)
check("and stack at the same corner", b1:getBounds().y, 20)
ok("removeIf", OM:removeIf(J.fn("java.util.function.Predicate", "test", function(_, o) return o == b1 end)))
check("one left", OM:getOverlays():size(), 1)

-- an opaque closed outline is one engine polygon border; a long outline's
-- fill is split under draw.polygon's 256-point limit; an unresident font
-- measures 0 and draws nothing
reset()
with_draw(function(g, d)
  g:setColor(Color.RED)
  g:setStroke(C["java.awt.BasicStroke"].__new__float(2.0))
  local p = Poly.__new__()
  p:addPoint(10, 10); p:addPoint(40, 10); p:addPoint(25, 30)
  g:draw(p)
  local pp = draws("polygon")
  check("opaque outline: one call", #pp, 1)
  check("opaque outline: no wash", pp[1][4], 0)
  check("opaque outline: width", pp[1][6], 2)

  reset()
  local big = Poly.__new__()
  for i = 0, 599 do
    local a = 2 * math.pi * i / 600
    big:addPoint(300 + math.floor(100 * math.cos(a)), 200 + math.floor(100 * math.sin(a)))
  end
  g:setColor(Color.__new__int_int_int_int(0, 0, 0, 50))
  g:fill(big)
  local pieces = draws("polygon")
  ok("600-point fill split", #pieces >= 3)
  local maxn = 0
  for _, q in ipairs(pieces) do maxn = math.max(maxn, #q[2] // 2) end
  ok("every piece within 256 points", maxn <= 256)

  reset()
  d.text_width = function() return nil end
  d.text_height = function() return nil end
  g:drawString__String_int_int("x", 1, 1)
  check("unresident font: nothing drawn", #draws("text"), 0)
  check("unresident font: width 0", g:getFontMetrics__():stringWidth("abc"), 0)
end)

print(string.format("test_awt: %d passed, %d failed", passes, fails))
if fails > 0 then os.exit(1) end
