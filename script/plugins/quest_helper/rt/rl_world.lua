-- net.runelite.api's world for the Quest Helper port: coordinates
-- (WorldPoint, WorldArea, LocalPoint, Point), the scene (WorldView, Scene,
-- Tile), its contents (Actor/NPC/Player, the TileObject family, TileItem,
-- ItemLayer, GraphicsObject, WorldEntity), Perspective, instance-template
-- conversion, and RuneLiteObject/Model/ModelData/Animation over api.scene.
--
-- Contract: docs/QUEST_HELPER_PORT.md, "World shapes" and "rl_world.lua
-- factories". Method names follow the transpiler's overload rule (a name the
-- RuneLite type declares more than once carries its erased parameters).
-- Coordinates: a LocalPoint is scene-relative, 128 units a tile -- the same
-- units as a snapshot's fine_x/fine_z and api.draw.project. Every height is
-- measured up from the ground (Perspective.getTileHeight answers 0).
--
-- The client is reached only through QH.api, read at call time.

local J, C = J, C

local function A() return QH.api end

local function def(key, super, ifaces, body, clinit)
  J.def(key, { kind = "class", super = super or "java.lang.Object", ifaces = ifaces or {}, body = body, clinit = clinit })
end
local function iface(key, ifaces, body)
  J.def(key, { kind = "interface", super = "java.lang.Object", ifaces = ifaces or {}, body = body })
end

local LOCAL_COORD_BITS = 7
local TILE = 128
local SCENE_SIZE = 104
local CHUNK = 8
local ESCENE_OFFSET = 40 -- (EXTENDED_SCENE_SIZE 184 - SCENE_SIZE) / 2
local TOPLEVEL = -1
local INT_MAX = 2147483647
local DEFAULT_MODEL_HEIGHT = 200
local DEFAULT_MINIMAP_ZOOM = 4

local KEY_WP = "net.runelite.api.coords.WorldPoint"
local KEY_LP = "net.runelite.api.coords.LocalPoint"
local KEY_WA = "net.runelite.api.coords.WorldArea"
local KEY_PT = "net.runelite.api.Point"

local function wp(x, y, plane) return C[KEY_WP].__new(x, y, plane) end
local function lp(x, y, wv) return C[KEY_LP].__new__int_int_int(x, y, wv) end
local function pt(x, y) return C[KEY_PT].__new(x, y) end
local function top() return C["net.runelite.api.WorldView"].__top() end

local function clean_name(s)
  if s == nil or s == "" then return nil end
  s = s:gsub("<[^>]*>", ""):gsub("\194\160", " ")
  return s
end

-- ------------------------------------------------------------------ per-cycle state
--
-- One read of the scene origin and the local player per client cycle; the npc
-- and player sets and the tile index are rebuilt lazily, at most once a cycle.

local F = { cycle = nil, epoch = 0 }
-- key -> Tile, while the scene origin stays. WEAK values: identity holds for
-- as long as anyone keeps a Tile (a step's tile map, an event), and a scan of
-- every tile (Scene.getTiles) does not pin ten thousand of them afterwards.
local TILE_CACHE_MT = { __mode = "v" }
local tile_cache = setmetatable({}, TILE_CACHE_MT)
local tile_cache_bx, tile_cache_bz = nil, nil
local scene_info = nil        -- {bx, bz, epoch, instance, chunks}

local function frame()
  local c = A().client.world_cycle()
  if F.cycle ~= c then
    F.cycle = c
    local bx, bz = A().world.scene_origin()
    if bx == nil then
      F.world = false; F.bx = 0; F.bz = 0
    else
      F.world = true; F.bx = bx; F.bz = bz
    end
    F.player_read = false; F.player = nil
    F.npcs = nil; F.players = nil; F.index = nil; F.planes = nil; F.wevs = nil
    if tile_cache_bx ~= F.bx or tile_cache_bz ~= F.bz then
      tile_cache = setmetatable({}, TILE_CACHE_MT); tile_cache_bx = F.bx; tile_cache_bz = F.bz
    end
  end
  return F
end

local function player_snap()
  local f = frame()
  if not f.player_read then
    f.player_read = true
    f.player = A().world.local_player()
  end
  return f.player
end

-- The live world entities (sailing boats) this frame: {list = {snap...},
-- by = {[id] = snap}}. A client without game.world_entity_next has none.
local function wev_snaps()
  local f = frame()
  if f.wevs == nil then
    local list, by = {}, {}
    local g = A().game
    if g ~= nil and g.world_entity_next ~= nil then
      local cur, s = g.world_entity_next(nil)
      while cur do
        list[#list + 1] = s; by[s.id] = s
        cur, s = g.world_entity_next(cur)
      end
    end
    f.wevs = { list = list, by = by }
  end
  return f.wevs
end
-- The live entity whose staging rectangle holds absolute tile (x, z): where
-- something aboard is addressed (RuneLite's boat WorldView coordinates).
local function wev_holding(x, z)
  for _, s in ipairs(wev_snaps().list) do
    if s.size_x > 0 and x >= s.base_x and x < s.base_x + s.size_x and z >= s.base_z and z < s.base_z + s.size_z then
      return s
    end
  end
  return nil
end

local function current_plane()
  local p = player_snap()
  if p == nil then return 0 end
  -- aboard, the player's level is the deck's (its own world); the top
  -- level's plane is the one the boat floats on, as RuneLite's
  local w = wev_holding(p.true_x, p.true_z)
  if w ~= nil then return w.level end
  return p.level
end

local function tile_key(plane, sx, sy) return (plane * SCENE_SIZE + sx) * SCENE_SIZE + sy end

-- ------------------------------------------------------------------ projection + shapes

local function project(fine_x, fine_z, height)
  return A().draw.project(fine_x, fine_z, height)
end

local function new_polygon(xs, ys, n, world)
  local poly = C["java.awt.Polygon"].__new__()
  for i = 1, n do poly:addPoint(xs[i], ys[i]) end
  poly.__world = world
  return poly
end

-- Andrew's monotone chain over integer canvas points; returns the hull in
-- counter-clockwise order (screen y down: clockwise on screen).
local function convex_hull(px, py, n)
  local idx = {}
  for i = 1, n do idx[i] = i end
  table.sort(idx, function(a, b)
    if px[a] ~= px[b] then return px[a] < px[b] end
    return py[a] < py[b]
  end)
  local function cross(o, a, b)
    return (px[a] - px[o]) * (py[b] - py[o]) - (py[a] - py[o]) * (px[b] - px[o])
  end
  local h, k = {}, 0
  for _, i in ipairs(idx) do
    while k >= 2 and cross(h[k - 1], h[k], i) <= 0 do h[k] = nil; k = k - 1 end
    k = k + 1; h[k] = i
  end
  local lower = k + 1
  for j = #idx - 1, 1, -1 do
    local i = idx[j]
    while k >= lower and cross(h[k - 1], h[k], i) <= 0 do h[k] = nil; k = k - 1 end
    k = k + 1; h[k] = i
  end
  if k > 1 then h[k] = nil; k = k - 1 end
  local xs, ys = {}, {}
  for i = 1, k do xs[i] = px[h[i]]; ys[i] = py[h[i]] end
  return xs, ys, k
end

-- The hull of a footprint box: corners (cx +- hx, cz +- hz) at the ground and
-- at `height`, as a Polygon carrying `world`. nil when a corner does not
-- project.
local function box_hull(cx, cz, hx, hz, height, world)
  local px, py, n = {}, {}, 0
  local corners = { cx - hx, cz - hz, cx + hx, cz - hz, cx + hx, cz + hz, cx - hx, cz + hz }
  local heights = height > 0 and { 0, height } or { 0 }
  for _, h in ipairs(heights) do
    for c = 1, 8, 2 do
      local x, y = project(corners[c], corners[c + 1], h)
      if x == nil then return nil end
      n = n + 1; px[n] = x; py[n] = y
    end
  end
  local xs, ys, k = convex_hull(px, py, n)
  return new_polygon(xs, ys, k, world)
end

local function element_height(element_id)
  if element_id == nil or element_id < 0 then return 0 end
  return A().draw.element_height(element_id)
end

-- ------------------------------------------------------------------ interfaces

iface("net.runelite.api.Node")
iface("net.runelite.api.Renderable", { "net.runelite.api.Node" })
iface("net.runelite.api.CameraFocusableEntity")
iface("net.runelite.api.Mesh")
iface("net.runelite.api.Locatable")

-- ------------------------------------------------------------------ Point

def(KEY_PT, nil, {}, function(cls)
  function cls.__init(self, x, y) self.x = x; self.y = y end
  function cls.getX(self) return self.x end
  function cls.getY(self) return self.y end
  function cls.distanceTo(self, o)
    local dx, dy = self.x - o.x, self.y - o.y
    return J.d2i(math.sqrt(dx * dx + dy * dy))
  end
  function cls.equals(self, o)
    return type(o) == "table" and o.__key == KEY_PT and o.x == self.x and o.y == self.y
  end
  function cls.hashCode(self) return J.l2i((59 + self.x) * 59 + self.y) end
  function cls.toString(self) return "Point(x=" .. self.x .. ", y=" .. self.y .. ")" end
end)

-- ------------------------------------------------------------------ instance templates
--
-- RuneLite's int[][][] of template chunks, rebuilt from api.world.instance_zone
-- per zone: (plane << 24) | (template zone x << 14) | (template zone y << 3) |
-- (rotation << 1), or -1 for a void zone. Cached per scene origin (and the
-- __invalidate epoch): a scene is rebuilt with a new origin or a load.

local function zone_chunk(plane, zone_x, zone_z)
  local tx, tz, tlevel, rot = A().world.instance_zone(plane, zone_x, zone_z)
  if tx == nil then return -1 end
  return ((tlevel & 3) << 24) | (((tx // CHUNK) & 0x3FF) << 14) | (((tz // CHUNK) & 0x7FF) << 3) | ((rot & 3) << 1)
end

local function scene_state()
  local f = frame()
  local s = scene_info
  if s == nil or s.bx ~= f.bx or s.bz ~= f.bz or s.epoch ~= F.epoch or s.world ~= f.world then
    s = { bx = f.bx, bz = f.bz, epoch = F.epoch, world = f.world }
    scene_info = s
  end
  return s
end

local function is_instance()
  local s = scene_state()
  if s.instance ~= nil then return s.instance end
  local found = false
  if s.world then
    local zx0, zz0 = s.bx // CHUNK, s.bz // CHUNK
    local p = player_snap()
    if p ~= nil and A().world.instance_zone(p.level, p.true_x // CHUNK, p.true_z // CHUNK) ~= nil then
      found = true
    else
      for plane = 0, 3 do
        for cx = 0, SCENE_SIZE // CHUNK - 1 do
          for cz = 0, SCENE_SIZE // CHUNK - 1 do
            if A().world.instance_zone(plane, zx0 + cx, zz0 + cz) ~= nil then found = true; break end
          end
          if found then break end
        end
        if found then break end
      end
    end
  end
  s.instance = found
  return found
end

local function template_chunks()
  local s = scene_state()
  if s.chunks then return s.chunks end
  local n = SCENE_SIZE // CHUNK
  local zx0, zz0 = s.bx // CHUNK, s.bz // CHUNK
  local planes = { n = 4 }
  for plane = 0, 3 do
    local xs = { n = n }
    for cx = 0, n - 1 do
      local ys = { n = n }
      for cz = 0, n - 1 do ys[cz + 1] = s.world and zone_chunk(plane, zx0 + cx, zz0 + cz) or -1 end
      xs[cx + 1] = ys
    end
    planes[plane + 1] = xs
  end
  s.chunks = planes
  return planes
end

-- ------------------------------------------------------------------ WorldPoint

def(KEY_WP, nil, {}, function(cls)
  local REGION_MIRRORS = { 12894, 8755, 12895, 8756, 13150, 9011, 13151, 9012 }

  function cls.__init(self, x, y, plane) self.x = x; self.y = y; self.plane = plane end
  function cls.getX(self) return self.x end
  function cls.getY(self) return self.y end
  function cls.getPlane(self) return self.plane end
  function cls.dx(self, d) return cls.__new(self.x + d, self.y, self.plane) end
  function cls.dy(self, d) return cls.__new(self.x, self.y + d, self.plane) end
  function cls.dz(self, d) return cls.__new(self.x, self.y, self.plane + d) end

  function cls.equals(self, o)
    return type(o) == "table" and o.__key == KEY_WP and o.x == self.x and o.y == self.y and o.plane == self.plane
  end
  function cls.hashCode(self) return J.l2i(((59 + self.x) * 59 + self.y) * 59 + self.plane) end
  function cls.toString(self)
    return "WorldPoint(x=" .. self.x .. ", y=" .. self.y .. ", plane=" .. self.plane .. ")"
  end

  function cls.isInScene__Scene_int_int(scene, x, y)
    local bx, by = scene:getBaseX(), scene:getBaseY()
    return x >= bx and x < bx + SCENE_SIZE and y >= by and y < by + SCENE_SIZE
  end
  function cls.isInScene__WorldView_int_int(wv, x, y)
    local bx, by = wv:getBaseX(), wv:getBaseY()
    return x >= bx and x < bx + wv:getSizeX() and y >= by and y < by + wv:getSizeY()
  end
  function cls.isInScene__Client_int_int(client, x, y) return cls.isInScene__WorldView_int_int(top(), x, y) end
  function cls.isInScene__Client(self, client)
    return top():getPlane() == self.plane and cls.isInScene__Client_int_int(client, self.x, self.y)
  end

  function cls.fromLocal__WorldView_int_int_int(wv, x, y, plane)
    return cls.__new((x // TILE) + wv:getBaseX(), (y // TILE) + wv:getBaseY(), plane)
  end
  function cls.fromLocal__Scene_int_int_int(scene, x, y, plane)
    return cls.__new((x // TILE) + scene:getBaseX(), (y // TILE) + scene:getBaseY(), plane)
  end
  function cls.fromLocal__Client_int_int_int(client, x, y, plane)
    return cls.fromLocal__WorldView_int_int_int(top(), x, y, plane)
  end
  function cls.fromLocal__Client_LocalPoint(client, local_point)
    local wv = C["net.runelite.api.WorldView"].__byId(local_point:getWorldView())
    if wv == nil then J.npe("no world view " .. local_point:getWorldView()) end
    return cls.fromLocal__WorldView_int_int_int(wv, local_point:getX(), local_point:getY(), wv:getPlane())
  end

  local function rotate(point, rotation)
    local cx = point.x & ~(CHUNK - 1)
    local cy = point.y & ~(CHUNK - 1)
    local x = point.x & (CHUNK - 1)
    local y = point.y & (CHUNK - 1)
    if rotation == 1 then return cls.__new(cx + y, cy + (CHUNK - 1 - x), point.plane)
    elseif rotation == 2 then return cls.__new(cx + (CHUNK - 1 - x), cy + (CHUNK - 1 - y), point.plane)
    elseif rotation == 3 then return cls.__new(cx + (CHUNK - 1 - y), cy + x, point.plane)
    end
    return point
  end
  cls.__rotate = rotate

  -- RuneLite's private fromLocalInstance(int[][][], ...), over the cached
  -- template chunks (one api read per zone per scene, not per call).
  local function from_instance(local_point, plane)
    local sx, sy = local_point:getSceneX(), local_point:getSceneY()
    local chx, chy = J.idiv(sx, CHUNK), J.idiv(sy, CHUNK)
    local chunk = -1
    if chx >= 0 and chx < SCENE_SIZE // CHUNK and chy >= 0 and chy < SCENE_SIZE // CHUNK then
      chunk = template_chunks()[plane + 1][chx + 1][chy + 1]
    end
    local rotation = (chunk >> 1) & 0x3
    local ty = ((chunk >> 3) & 0x7FF) * CHUNK
    local tx = ((chunk >> 14) & 0x3FF) * CHUNK
    local tplane = (chunk >> 24) & 0x3
    local x = tx + (sx & (CHUNK - 1))
    local y = ty + (sy & (CHUNK - 1))
    return rotate(cls.__new(x, y, tplane), 4 - rotation)
  end

  function cls.fromLocalInstance__Client_LocalPoint_int(client, local_point, plane)
    local wv = C["net.runelite.api.WorldView"].__byId(local_point:getWorldView())
    if wv == nil then J.npe("no world view " .. local_point:getWorldView()) end
    if wv:isInstance() then return from_instance(local_point, plane) end
    return cls.fromLocal__Client_int_int_int(client, local_point:getX(), local_point:getY(), plane)
  end
  function cls.fromLocalInstance__Client_LocalPoint(client, local_point)
    local wv = C["net.runelite.api.WorldView"].__byId(local_point:getWorldView())
    if wv == nil then J.npe("no world view " .. local_point:getWorldView()) end
    return cls.fromLocalInstance__Client_LocalPoint_int(client, local_point, wv:getPlane())
  end
  function cls.fromLocalInstance__Scene_LocalPoint_int(scene, local_point, plane)
    if scene:isInstance() then return from_instance(local_point, plane) end
    return cls.fromLocal__Scene_int_int_int(scene, local_point:getX(), local_point:getY(), plane)
  end

  local function to_instance(chunks, base_x, base_y, point)
    local list = C["java.util.ArrayList"].__new__()
    for z = 1, chunks.n do
      local xs = chunks[z]
      for x = 1, xs.n do
        local ys = xs[x]
        for y = 1, ys.n do
          local data = ys[y]
          local rotation = (data >> 1) & 0x3
          local ty = ((data >> 3) & 0x7FF) * CHUNK
          local tx = ((data >> 14) & 0x3FF) * CHUNK
          local plane = (data >> 24) & 0x3
          if point.x >= tx and point.x < tx + CHUNK and point.y >= ty and point.y < ty + CHUNK and plane == point.plane then
            local p = cls.__new(base_x + (x - 1) * CHUNK + (point.x & (CHUNK - 1)),
              base_y + (y - 1) * CHUNK + (point.y & (CHUNK - 1)), z - 1)
            list:add(rotate(p, rotation))
          end
        end
      end
    end
    return list
  end

  function cls.toLocalInstance__WorldView_WorldPoint(wv, point)
    if wv:isInstance() then
      return to_instance(wv:getInstanceTemplateChunks(), wv:getBaseX(), wv:getBaseY(), point)
    elseif wv:contains__WorldPoint(point) then
      return C["java.util.Collections"].singleton(point)
    end
    return C["java.util.Collections"].emptyList()
  end
  function cls.toLocalInstance__Client_WorldPoint(client, point) return cls.toLocalInstance__WorldView_WorldPoint(top(), point) end
  function cls.toLocalInstance__Scene_WorldPoint(scene, point)
    if scene:isInstance() then
      return to_instance(scene:getInstanceTemplateChunks(), scene:getBaseX(), scene:getBaseY(), point)
    elseif cls.isInScene__Scene_int_int(scene, point.x, point.y) then
      return C["java.util.Collections"].singleton(point)
    end
    return C["java.util.Collections"].emptyList()
  end

  function cls.distanceTo2D(self, o)
    return math.max(math.abs(self.x - o.x), math.abs(self.y - o.y))
  end
  function cls.distanceTo__WorldPoint(self, o)
    if o.plane ~= self.plane then return INT_MAX end
    return cls.distanceTo2D(self, o)
  end
  function cls.distanceTo__WorldArea(self, area) return self:toWorldArea():distanceTo__WorldArea(area) end

  function cls.fromScene__WorldView_int_int_int(wv, x, y, plane) return cls.__new(x + wv:getBaseX(), y + wv:getBaseY(), plane) end
  function cls.fromScene__Scene_int_int_int(scene, x, y, plane) return cls.__new(x + scene:getBaseX(), y + scene:getBaseY(), plane) end
  function cls.fromScene__Client_int_int_int(client, x, y, plane) return cls.fromScene__WorldView_int_int_int(top(), x, y, plane) end

  function cls.getRegionID(self) return J.l2i(((self.x // 64) << 8) | (self.y // 64)) end
  function cls.getRegionX(self) return self.x & 63 end
  function cls.getRegionY(self) return self.y & 63 end
  function cls.fromRegion(region, rx, ry, plane)
    return cls.__new(((region & 0xFFFFFFFF) >> 8 << 6) + rx, ((region & 0xFF) << 6) + ry, plane)
  end
  function cls.getMirrorPoint(point, to_overworld)
    local region = point:getRegionID()
    for i = 1, #REGION_MIRRORS, 2 do
      local real, overworld = REGION_MIRRORS[i], REGION_MIRRORS[i + 1]
      if region == (to_overworld and real or overworld) then
        return cls.fromRegion(to_overworld and overworld or real, point:getRegionX(), point:getRegionY(), point.plane)
      end
    end
    return point
  end
  function cls.isInArea(self, areas)
    for i = 1, areas.n do if areas[i]:contains(self) then return true end end
    return false
  end
  function cls.isInArea2D(self, areas)
    for i = 1, areas.n do if areas[i]:contains2D(self) then return true end end
    return false
  end
  function cls.toWorldArea(self) return C[KEY_WA].__new__WorldPoint_int_int(self, 1, 1) end
  function cls.fromCoord(c)
    c = c & 0xFFFFFFFF
    return cls.__new((c >> 14) & 0x3FFF, c & 0x3FFF, (c >> 28) & 0x3)
  end
end)

-- ------------------------------------------------------------------ WorldArea

def(KEY_WA, nil, {}, function(cls)
  function cls.__init__int_int_int_int_int(self, x, y, w, h, plane)
    self.x = x; self.y = y; self.width = w; self.height = h; self.plane = plane
  end
  function cls.__init__WorldPoint_int_int(self, loc, w, h)
    self.x = loc.x; self.y = loc.y; self.plane = loc.plane; self.width = w; self.height = h
  end
  function cls.getX(self) return self.x end
  function cls.getY(self) return self.y end
  function cls.getWidth(self) return self.width end
  function cls.getHeight(self) return self.height end
  function cls.getPlane(self) return self.plane end

  local function comparison(self, other)
    local x, y
    if other.x <= self.x then x = self.x
    elseif other.x >= self.x + self.width - 1 then x = self.x + self.width - 1
    else x = other.x end
    if other.y <= self.y then y = self.y
    elseif other.y >= self.y + self.height - 1 then y = self.y + self.height - 1
    else y = other.y end
    return x, y
  end
  local function axis(self, other)
    local x1, y1 = comparison(self, other)
    local x2, y2 = comparison(other, self)
    return math.abs(x1 - x2), math.abs(y1 - y2)
  end
  function cls.distanceTo2D__WorldArea(self, other)
    local dx, dy = axis(self, other)
    return math.max(dx, dy)
  end
  function cls.distanceTo2D__WorldPoint(self, p) return cls.distanceTo2D__WorldArea(self, p:toWorldArea()) end
  function cls.distanceTo__WorldArea(self, other)
    if self.plane ~= other.plane then return INT_MAX end
    return cls.distanceTo2D__WorldArea(self, other)
  end
  function cls.distanceTo__WorldPoint(self, p) return cls.distanceTo__WorldArea(self, p:toWorldArea()) end
  function cls.contains(self, p) return cls.distanceTo__WorldPoint(self, p) == 0 end
  function cls.contains2D(self, p) return cls.distanceTo2D__WorldPoint(self, p) == 0 end
  function cls.isInMeleeDistance__WorldArea(self, other)
    if other == nil or self.plane ~= other.plane then return false end
    local dx, dy = axis(self, other)
    return dx + dy == 1
  end
  function cls.isInMeleeDistance__WorldPoint(self, p) return cls.isInMeleeDistance__WorldArea(self, p:toWorldArea()) end
  function cls.intersectsWith(self, other)
    if self.plane ~= other.plane then return false end
    local dx, dy = axis(self, other)
    return dx + dy == 0
  end
  -- RuneLite answers false when the world view has no collision maps, which
  -- is always here (WorldView.getCollisionMaps is nil).
  function cls.canTravelInDirection__WorldView_int_int(self, wv, dx, dy)
    if dx == 0 and dy == 0 then return true end
    return wv:getCollisionMaps() ~= nil
  end
  function cls.canTravelInDirection__WorldView_int_int_Predicate(self, wv, dx, dy, extra)
    return cls.canTravelInDirection__WorldView_int_int(self, wv, dx, dy)
  end
  function cls.toWorldPoint(self) return wp(self.x, self.y, self.plane) end
  function cls.toWorldPointList(self)
    local list = C["java.util.ArrayList"].__new__()
    for x = 0, self.width - 1 do
      for y = 0, self.height - 1 do list:add(wp(self.x + x, self.y + y, self.plane)) end
    end
    return list
  end
  function cls.toString(self)
    return "WorldArea(x=" .. self.x .. ", y=" .. self.y .. ", width=" .. self.width .. ", height=" .. self.height .. ", plane=" .. self.plane .. ")"
  end
end)

-- ------------------------------------------------------------------ LocalPoint

def(KEY_LP, nil, {}, function(cls)
  function cls.__init__int_int_int(self, x, y, wv) self.x = x; self.y = y; self.worldView = wv end
  function cls.__init__int_int_WorldView(self, x, y, wv) self.x = x; self.y = y; self.worldView = wv:getId() end
  function cls.__init__int_int(self, x, y) self.x = x; self.y = y; self.worldView = TOPLEVEL end
  function cls.getX(self) return self.x end
  function cls.getY(self) return self.y end
  function cls.getWorldView(self) return self.worldView end
  function cls.getSceneX(self) return self.x // TILE end
  function cls.getSceneY(self) return self.y // TILE end
  function cls.plus(self, dx, dy) return cls.__new__int_int_int(self.x + dx, self.y + dy, self.worldView) end
  function cls.dx(self, d) return cls.plus(self, d, 0) end
  function cls.dy(self, d) return cls.plus(self, 0, d) end
  function cls.distanceTo(self, o)
    if self.worldView ~= o.worldView then return INT_MAX end
    local dx, dy = self.x - o.x, self.y - o.y
    return J.d2i(math.sqrt(dx * dx + dy * dy))
  end
  function cls.isInScene(self)
    local lim = SCENE_SIZE << LOCAL_COORD_BITS
    return self.x >= 0 and self.x < lim and self.y >= 0 and self.y < lim
  end
  function cls.equals(self, o)
    return type(o) == "table" and o.__key == KEY_LP and o.x == self.x and o.y == self.y and o.worldView == self.worldView
  end
  function cls.hashCode(self) return J.l2i(((59 + self.x) * 59 + self.y) * 59 + self.worldView) end
  function cls.toString(self)
    return "LocalPoint(x=" .. self.x .. ", y=" .. self.y .. ", worldView=" .. self.worldView .. ")"
  end

  local HALF = 1 << (LOCAL_COORD_BITS - 1)
  function cls.fromScene__int_int(x, y) return cls.__new__int_int_int((x << LOCAL_COORD_BITS) + HALF, (y << LOCAL_COORD_BITS) + HALF, TOPLEVEL) end
  function cls.fromScene__int_int_Scene(x, y, scene)
    return cls.__new__int_int_int((x << LOCAL_COORD_BITS) + HALF, (y << LOCAL_COORD_BITS) + HALF, scene:getWorldViewId())
  end
  function cls.fromScene__int_int_WorldView(x, y, wv)
    return cls.__new__int_int_int((x << LOCAL_COORD_BITS) + HALF, (y << LOCAL_COORD_BITS) + HALF, wv:getId())
  end
  function cls.fromWorld__WorldView_int_int(wv, x, y)
    if not C[KEY_WP].isInScene__WorldView_int_int(wv, x, y) then return nil end
    return cls.fromScene__int_int_WorldView(x - wv:getBaseX(), y - wv:getBaseY(), wv)
  end
  function cls.fromWorld__WorldView_WorldPoint(wv, point)
    if wv:getPlane() ~= point.plane then return nil end
    return cls.fromWorld__WorldView_int_int(wv, point.x, point.y)
  end
  function cls.fromWorld__Client_WorldPoint(client, point) return cls.fromWorld__WorldView_WorldPoint(top(), point) end
  function cls.fromWorld__Client_int_int(client, x, y) return cls.fromWorld__WorldView_int_int(top(), x, y) end
  function cls.fromWorld__Scene_int_int(scene, x, y)
    if not C[KEY_WP].isInScene__Scene_int_int(scene, x, y) then return nil end
    return cls.fromScene__int_int_Scene(x - scene:getBaseX(), y - scene:getBaseY(), scene)
  end
end)

-- ------------------------------------------------------------------ IndexedObjectSet

def("net.runelite.api.IndexedObjectSet", nil, { "java.lang.Iterable" }, function(cls)
  function cls.__of(list, by)
    local o = setmetatable({}, cls)
    o.t = list; o.by = by
    return o
  end
  function cls.byIndex(self, i) return self.by[i] end
  function cls.stream(self) return C["java.util.stream.Stream"].__from(J.aclone(self.t)) end
  function cls.iterator(self) return J.array_iterator(self.t, self.t.n) end
  function cls.__iter(self)
    local t, i = self.t, 0
    return function()
      i = i + 1
      if i <= t.n then return i, t[i] end
    end
  end
  function cls.size(self) return self.t.n end
end)

local EMPTY_SET = nil
local function empty_set()
  if EMPTY_SET == nil then EMPTY_SET = C["net.runelite.api.IndexedObjectSet"].__of({ n = 0 }, {}) end
  return EMPTY_SET
end

-- ------------------------------------------------------------------ Model / ModelData / Animation

-- A Model is what a Renderable answers: either an entity's drawn model in
-- the scene (`element_id`) or a lit ModelData destined for a RuneLiteObject.
-- Neither carries vertices to Lua: getFaceTransparencies is nil, which
-- RuneLite callers read as "every face visible".
def("net.runelite.api.Model", nil, { "net.runelite.api.Mesh", "net.runelite.api.Renderable" }, function(cls)
  function cls.__of_element(element_id)
    local o = setmetatable({}, cls)
    o.element_id = element_id
    return o
  end
  function cls.__of_data(data, ambient, contrast)
    local o = setmetatable({}, cls)
    o.data = data; o.ambient = ambient; o.contrast = contrast
    return o
  end
  function cls.getModel(self) return self end
  function cls.getModelHeight(self)
    if self.element_id ~= nil then
      local h = element_height(self.element_id)
      if h > 0 then return h end
    end
    return DEFAULT_MODEL_HEIGHT
  end
  function cls.getAnimationHeightOffset(self) return 0 end
  function cls.getRenderMode(self) return 0 end
  function cls.getHash(self) return 0 end
  function cls.getNext(self) return nil end
  function cls.getPrevious(self) return nil end
  function cls.getVerticesCount(self) return 0 end
  function cls.getFaceCount(self) return 0 end
  function cls.getFaceTransparencies(self) return nil end
  function cls.getFaceTextures(self) return nil end
  function cls.getVerticesX(self) return nil end
  function cls.getVerticesY(self) return nil end
  function cls.getVerticesZ(self) return nil end
  function cls.getRadius(self) return 64 end
  function cls.getDiameter(self) return 128 end
  function cls.getBottomY(self) return 0 end
  function cls.getXYZMag(self) return 64 end
  function cls.calculateBoundsCylinder(self) end
  function cls.calculateExtreme(self, orientation) end
  function cls.useBoundingBox(self) return false end
  function cls.scale(self, x, y, z) if self.data then self.data:scale(x, y, z) end return self end
  function cls.translate(self, x, y, z) return self end
  function cls.rotateY90Ccw(self) if self.data then self.data:rotateY90Ccw() end return self end
  function cls.rotateY180Ccw(self) if self.data then self.data:rotateY180Ccw() end return self end
  function cls.rotateY270Ccw(self) if self.data then self.data:rotateY270Ccw() end return self end
  function cls.getUnskewedModel(self) return self end
end)

-- ModelData: the cache model ids it is made of plus the edits asked of it;
-- api.scene applies them to one scene instance per id (RuneLiteObject).
def("net.runelite.api.ModelData", nil, { "net.runelite.api.Mesh", "net.runelite.api.Renderable" }, function(cls)
  local function make(ids, recolors, scale, yaw)
    local o = setmetatable({}, cls)
    o.ids = ids; o.recolors = recolors; o.scale_xyz = scale; o.yaw = yaw
    return o
  end
  local function copy(self)
    local ids = { n = self.ids.n }
    for i = 1, ids.n do ids[i] = self.ids[i] end
    local rc = { n = self.recolors.n }
    for i = 1, rc.n do rc[i] = self.recolors[i] end
    return make(ids, rc, { self.scale_xyz[1], self.scale_xyz[2], self.scale_xyz[3] }, self.yaw)
  end
  function cls.__load(id) return make({ id, n = 1 }, { n = 0 }, { 128, 128, 128 }, 0) end
  function cls.__merge(arr)
    local ids, rc = { n = 0 }, { n = 0 }
    for i = 1, arr.n do
      local m = arr[i]
      if m ~= nil then
        for k = 1, m.ids.n do ids.n = ids.n + 1; ids[ids.n] = m.ids[k] end
        for k = 1, m.recolors.n do rc.n = rc.n + 1; rc[rc.n] = m.recolors[k] end
      end
    end
    return make(ids, rc, { 128, 128, 128 }, 0)
  end
  function cls.shallowCopy(self) return copy(self) end
  function cls.cloneVertices(self) return copy(self) end
  function cls.cloneColors(self) return copy(self) end
  function cls.cloneTextures(self) return copy(self) end
  function cls.cloneTransparencies__(self) return copy(self) end
  function cls.cloneTransparencies__boolean(self) return copy(self) end
  function cls.recolor(self, from, to)
    self.recolors.n = self.recolors.n + 1
    self.recolors[self.recolors.n] = { from & 0xFFFF, to & 0xFFFF }
    return self
  end
  function cls.retexture(self, from, to) return self end
  function cls.scale(self, x, y, z)
    local s = self.scale_xyz
    s[1] = s[1] * x // 128; s[2] = s[2] * y // 128; s[3] = s[3] * z // 128
    return self
  end
  function cls.translate(self, x, y, z) return self end
  function cls.rotateY90Ccw(self) self.yaw = (self.yaw + 512) & 2047; return self end
  function cls.rotateY180Ccw(self) self.yaw = (self.yaw + 1024) & 2047; return self end
  function cls.rotateY270Ccw(self) self.yaw = (self.yaw + 1536) & 2047; return self end
  -- ModelData.DEFAULT_AMBIENT / DEFAULT_CONTRAST
  function cls.light__(self) return C["net.runelite.api.Model"].__of_data(copy(self), 64, 768) end
  function cls.light__int_int_int_int_int(self, ambient, contrast, x, y, z)
    return C["net.runelite.api.Model"].__of_data(copy(self), ambient, contrast)
  end
  function cls.getFaceColors(self) return nil end
  function cls.getVerticesCount(self) return 0 end
  function cls.getFaceCount(self) return 0 end
  function cls.getFaceTransparencies(self) return nil end
  function cls.getFaceTextures(self) return nil end
  function cls.getModel(self) return nil end
  function cls.getModelHeight(self) return DEFAULT_MODEL_HEIGHT end
  function cls.getAnimationHeightOffset(self) return 0 end
  function cls.getRenderMode(self) return 0 end
  function cls.getHash(self) return 0 end
end)

-- api.cache.seq_def: frame lengths in client cycles. A seq the cache is still
-- loading is 'pending' (its load queued): the Animation reads 0 frames until a
-- later call finds it in (every getter asks again while it is pending).
def("net.runelite.api.Animation", nil, {}, function(cls)
  local function fill(o)
    local seq_def = A().cache.seq_def
    if seq_def == nil then o.pending = false; return end
    local d, st = seq_def(o.id)
    o.pending = d == nil and st == "pending"
    if d == nil then return end
    local fl = d.frame_lengths or {}
    local lengths = { n = d.frame_count }
    for i = 1, d.frame_count do lengths[i] = fl[i] or 1 end
    o.lengths = lengths
    o.frame_step = d.frame_step or -1
    o.maya = d.skeletal == true
  end
  local function ready(self)
    if self.pending then fill(self) end
    return self
  end
  function cls.__load(id)
    local o = setmetatable({}, cls)
    o.id = id
    o.lengths = { n = 0 }
    o.frame_step = -1
    o.maya = false
    fill(o)
    return o
  end
  function cls.getId(self) return self.id end
  function cls.isMayaAnim(self) return ready(self).maya end
  function cls.getNumFrames(self) return ready(self).lengths.n end
  function cls.getRestartMode(self) return 0 end
  function cls.getDuration(self)
    local d = 0
    ready(self)
    for i = 1, self.lengths.n do d = d + self.lengths[i] end
    return d
  end
  function cls.getFrameStep(self) return ready(self).frame_step end
  function cls.getFrameLengths(self) return J.aclone(ready(self).lengths) end
  function cls.getLeftHandItem(self) return -1 end
  function cls.getRightHandItem(self) return -1 end
  -- frame index `cycles` client cycles after the start; loop wraps.
  function cls.__frame_at(self, cycles, loop)
    local n = ready(self).lengths.n
    if n == 0 then return 0 end
    local total = cls.getDuration(self)
    if total <= 0 then return 0 end
    if loop then cycles = cycles % total
    elseif cycles >= total then return n - 1 end
    for i = 1, n do
      cycles = cycles - self.lengths[i]
      if cycles < 0 then return i - 1 end
    end
    return n - 1
  end
end)

-- ------------------------------------------------------------------ Actor / NPC / Player

def("net.runelite.api.PlayerComposition", nil, {}, function(cls)
  function cls.__of(gender) local o = setmetatable({}, cls); o.gender = gender; return o end
  function cls.getGender(self) return self.gender end
  function cls.isFemale(self) return self.gender == 1 end
  function cls.getColors(self) return nil end
  function cls.getEquipmentIds(self) return nil end
  function cls.getTransformedNpcId(self) return -1 end
end)

local function actor_canvas_tile_poly(self)
  local s = self:__snap()
  local size = self:getFootprintSize()
  local poly = C["net.runelite.api.Perspective"].getCanvasTileAreaPoly__Client_LocalPoint_int_int_int_int(nil, self:getLocalLocation(), size, size, -1, 0)
  if poly ~= nil then
    poly.__world = { kind = "tile", tile_x = s.true_x, tile_z = s.true_z, level = s.level, size_x = size, size_z = size }
  end
  return poly
end

def("net.runelite.api.Actor", nil, { "net.runelite.api.Renderable", "net.runelite.api.CameraFocusableEntity" }, function(cls)
  -- subclasses define __snap(self) -> the latest snapshot (never nil once made)
  -- An actor aboard a boat is addressed in the boat's own world (its true
  -- tile is in the staging rectangle), as RuneLite's getWorldView answers;
  -- its LocalPoint stays the root's (where it is drawn).
  function cls.getWorldView(self)
    local s = self:__snap()
    local w = s and wev_holding(s.true_x, s.true_z)
    if w ~= nil then return C["net.runelite.api.WorldView"].__byId(w.id) end
    return top()
  end
  function cls.getCameraFocus(self) return self:getLocalLocation() end
  function cls.getName(self) return clean_name(self:__snap().name) end
  function cls.getCombatLevel(self) return self:__snap().combat_level end
  function cls.getWorldLocation(self)
    local s = self:__snap()
    return wp(s.true_x, s.true_z, s.level)
  end
  function cls.getLocalLocation(self)
    local s = self:__snap()
    return lp(s.fine_x, s.fine_z, TOPLEVEL)
  end
  function cls.getFootprintSize(self) return 1 end
  function cls.getWorldArea(self)
    local size = self:getFootprintSize()
    return C[KEY_WA].__new__WorldPoint_int_int(self:getWorldLocation(), size, size)
  end
  function cls.isInteracting(self) return self:getInteracting() ~= nil end
  function cls.getInteracting(self)
    local i = self:__snap().interacting
    if i == nil or i < 0 then return nil end
    if i < 32768 then return C["net.runelite.api.NPC"].__bySlot(i) end
    return C["net.runelite.api.Player"].__byPid(i - 32768)
  end
  function cls.getHealthRatio(self) return self:__snap().health_ratio or -1 end
  function cls.getHealthScale(self) return self:__snap().health_scale or -1 end
  function cls.getOrientation(self) return self:__snap().orientation or 0 end
  function cls.getCurrentOrientation(self) return self:__snap().orientation or 0 end
  -- The client's animation is the server's; a plugin's setAnimation is kept
  -- on this object (RuneLite's changes the drawn sequence; ours cannot).
  function cls.getAnimation(self)
    if self.anim_override ~= nil then return self.anim_override end
    return self:__snap().anim_id or -1
  end
  function cls.setAnimation(self, id) self.anim_override = id; self.anim_frame = 0 end
  function cls.getAnimationFrame(self) return self.anim_frame or 0 end
  function cls.setAnimationFrame(self, f) self.anim_frame = f end
  function cls.setActionFrame(self, f) self.anim_frame = f end
  local POSES = { "PoseAnimation", "IdlePoseAnimation", "IdleRotateLeft", "IdleRotateRight", "WalkAnimation",
    "WalkRotateLeft", "WalkRotateRight", "WalkRotate180", "RunAnimation", "PoseAnimationFrame" }
  for _, p in ipairs(POSES) do
    local k = "pose_" .. p
    cls["get" .. p] = function(self) local v = self[k]; if v == nil then return -1 end return v end
    cls["set" .. p] = function(self, v) self[k] = v end
  end
  function cls.getGraphic(self) return -1 end
  function cls.getGraphicHeight(self) return 0 end
  function cls.getSpotAnimFrame(self) return 0 end
  function cls.getSpotAnims(self) return nil end
  function cls.hasSpotAnim(self, id) return false end
  function cls.getOverheadText(self) return self.overhead_text end
  function cls.setOverheadText(self, t) self.overhead_text = t end
  function cls.getOverheadCycle(self) return 0 end
  function cls.isDead(self) return self.dead == true end
  function cls.setDead(self, d) self.dead = d end
  function cls.getAnimationHeightOffset(self) return 0 end
  function cls.getRenderMode(self) return 0 end
  function cls.getHash(self) return 0 end
  function cls.getNext(self) return nil end
  function cls.getPrevious(self) return nil end
  function cls.getModel(self)
    local e = self:__snap().element_id
    if e == nil or e < 0 then return nil end
    return C["net.runelite.api.Model"].__of_element(e)
  end
  function cls.getModelHeight(self)
    local h = element_height(self:__snap().element_id)
    if h > 0 then return h end
    return DEFAULT_MODEL_HEIGHT
  end
  function cls.getLogicalHeight(self) return cls.getModelHeight(self) end
  function cls.getCanvasTilePoly(self) return actor_canvas_tile_poly(self) end
  function cls.getCanvasImageLocation(self, image, z_offset)
    return C["net.runelite.api.Perspective"].getCanvasImageLocation(nil, self:getLocalLocation(), image, z_offset)
  end
  function cls.getCanvasTextLocation(self, graphics, text, z_offset)
    return C["net.runelite.api.Perspective"].getCanvasTextLocation(nil, graphics, self:getLocalLocation(), text, z_offset)
  end
  function cls.getMinimapLocation(self)
    return C["net.runelite.api.Perspective"].localToMinimap__Client_LocalPoint(nil, self:getLocalLocation())
  end
  function cls.getConvexHull(self)
    local s = self:__snap()
    if s.element_id == nil or s.element_id < 0 then return nil end
    local half = self:getFootprintSize() * TILE // 2
    return box_hull(s.fine_x, s.fine_z, half, half, element_height(s.element_id), { kind = "hull", element_id = s.element_id })
  end
end)

def("net.runelite.api.NPC", "net.runelite.api.Actor", {}, function(cls)
  local reg = {}
  function cls.__fromSnap(s)
    local o = reg[s.server_slot]
    if o == nil or o.s.base_npc_id ~= s.base_npc_id then
      o = setmetatable({}, cls)
      o.slot = s.server_slot
      reg[o.slot] = o
    end
    o.s = s
    o.cyc = frame().cycle
    return o
  end
  function cls.__bySlot(slot)
    local o = reg[slot]
    if o ~= nil then return o end
    local s = A().world.npc_by_slot(slot)
    if s == nil then return nil end
    return cls.__fromSnap(s)
  end
  function cls.__forget(slot) reg[slot] = nil end
  function cls.__snap(self)
    local f = frame()
    if self.cyc ~= f.cycle then
      self.cyc = f.cycle
      local s = A().world.npc_by_slot(self.slot)
      if s ~= nil and s.base_npc_id == self.s.base_npc_id then self.s = s end
    end
    return self.s
  end
  function cls.getId(self) return self:__snap().npc_id end
  function cls.getIndex(self) return self.slot end
  function cls.getFootprintSize(self) return self:__snap().size end
  -- rl_client's cached definitions (Client.getNpcDefinition)
  function cls.getComposition(self) return C["net.runelite.api.Client"].npc_composition(self:__snap().base_npc_id) end
  function cls.getTransformedComposition(self) return C["net.runelite.api.Client"].npc_composition(self:__snap().npc_id) end
  function cls.getModelOverrides(self) return nil end
  function cls.getChatheadOverrides(self) return nil end
  function cls.getOverheadArchiveIds(self) return nil end
  function cls.getOverheadSpriteIds(self) return nil end
  function cls.toString(self) return "NPC(" .. tostring(self.s.npc_id) .. "@" .. self.slot .. ")" end
end)

def("net.runelite.api.Player", "net.runelite.api.Actor", {}, function(cls)
  local the_local = nil
  local reg = {}
  local function local_pid()
    local s = player_snap()
    return s and s.server_pid or nil
  end
  function cls.__local()
    local s = player_snap()
    if s == nil then return nil end
    if the_local == nil then
      the_local = setmetatable({}, cls)
      the_local.is_local = true
    end
    the_local.s = s
    return the_local
  end
  function cls.__fromSnap(s)
    if s.server_pid >= 0 and s.server_pid == local_pid() then return cls.__local() end
    local o = reg[s.server_pid]
    if o == nil then
      o = setmetatable({}, cls)
      o.pid = s.server_pid
      reg[o.pid] = o
    end
    o.s = s
    o.cyc = frame().cycle
    return o
  end
  function cls.__byPid(pid)
    if pid == local_pid() then return cls.__local() end
    return top():players():byIndex(pid)
  end
  function cls.__snap(self)
    if self.is_local then
      local s = player_snap()
      if s ~= nil then self.s = s end
      return self.s
    end
    local f = frame()
    if self.cyc ~= f.cycle then
      self.cyc = f.cycle
      top():players() -- refreshes every registered player seen this cycle
    end
    return self.s
  end
  function cls.getId(self) return self:__snap().server_pid end
  function cls.getPlayerComposition(self) return C["net.runelite.api.PlayerComposition"].__of(self:__snap().gender or 0) end
  function cls.getTeam(self) return 0 end
  function cls.isFriendsChatMember(self) return false end
  function cls.isFriend(self) return false end
  function cls.isClanMember(self) return false end
  function cls.getOverheadIcon(self) return nil end
  function cls.getSkullIcon(self) return -1 end
  function cls.toString(self) return "Player(" .. tostring(self.s.name) .. ")" end
end)

-- ------------------------------------------------------------------ the scene index

local KIND_WALL, KIND_DECOR, KIND_GAME, KIND_GROUND = 0, 1, 2, 3
local function loc_kind(shape)
  if shape <= 3 then return KIND_WALL end
  if shape <= 8 then return KIND_DECOR end
  if shape <= 21 then return KIND_GAME end
  return KIND_GROUND
end
local LOC_CLASS = {
  [KIND_WALL] = "net.runelite.api.WallObject",
  [KIND_DECOR] = "net.runelite.api.DecorativeObject",
  [KIND_GAME] = "net.runelite.api.GameObject",
  [KIND_GROUND] = "net.runelite.api.GroundObject",
}

-- Every loc and ground item of one PLANE of the scene, bucketed by tile,
-- once a cycle and only when something asks. A game object is listed on every
-- tile it covers, as RuneLite's Scene does.
--
-- A loc is kept as the scenery CURSOR that precedes it (scenery_next(prev)
-- answers it again), never as its snapshot table: a scene holds 20-40
-- thousand locs, and a table each (12 fields) is 15-25 MB -- past the plugin
-- heap, which is what a quest step's DetailedQuestStep.addItemTiles hit live
-- (2026-11-19, "on_server_tick: not enough memory"). The snapshot is fetched
-- again only for a tile somebody asks the objects of (contents below).
-- Ground items are few and keep their snapshots.
local function scene_index(plane)
  local f = frame()
  f.index = f.index or {}
  local ix = f.index[plane]
  if ix then return ix end
  -- locs[key] is ONE cursor (a number) for a tile holding one loc -- most
  -- do -- and a list once it holds a second; items[key] a list of snapshots.
  ix = { locs = {}, items = {} }
  local bx, bz = f.bx, f.bz
  local function add_loc(key, prev)
    local b = ix.locs[key]
    if b == nil then ix.locs[key] = prev
    elseif type(b) == "number" then ix.locs[key] = { b, prev }
    else b[#b + 1] = prev end
  end
  if f.world and plane >= 0 and plane <= 3 then
    local prev = -1
    local cur, s = A().world.scenery_next(nil)
    while cur do
      if s.level == plane then
        local sx, sy = s.tile_x - bx, s.tile_z - bz
        local w, h = 1, 1
        if loc_kind(s.shape) == KIND_GAME then w, h = math.max(s.size_x, 1), math.max(s.size_z, 1) end
        for dx = 0, w - 1 do
          for dy = 0, h - 1 do
            local x, y = sx + dx, sy + dy
            if x >= 0 and x < SCENE_SIZE and y >= 0 and y < SCENE_SIZE then
              add_loc(tile_key(plane, x, y), prev)
            end
          end
        end
      end
      prev = cur
      cur, s = A().world.scenery_next(cur)
    end
    cur, s = A().world.item_next(nil)
    while cur do
      local sx, sy = s.tile_x - bx, s.tile_z - bz
      if s.level == plane and sx >= 0 and sx < SCENE_SIZE and sy >= 0 and sy < SCENE_SIZE then
        local key = tile_key(plane, sx, sy)
        local b = ix.items[key]
        if b == nil then b = {}; ix.items[key] = b end
        b[#b + 1] = s
      end
      cur, s = A().world.item_next(cur)
    end
  end
  f.index[plane] = ix
  return ix
end

-- The loc a scene_index bucket names (the cursor before it), or nil when the
-- scene moved on since the index was built.
local function loc_at_cursor(prev)
  local cur, s = A().world.scenery_next(prev)
  if cur == nil then return nil end
  return s
end

-- ------------------------------------------------------------------ TileObject family

local function loc_scene_xy(s)
  local f = frame()
  return s.tile_x - f.bx, s.tile_z - f.bz
end

def("net.runelite.api.TileObject", nil, {}, function(cls)
  function cls.__fromSnap(s)
    local o = setmetatable({}, C[LOC_CLASS[loc_kind(s.shape)]])
    o.s = s
    return o
  end
  -- A loc changed mid-scene (on_loc_changed): this cycle's index is stale.
  -- Tiles keep their identity; only their contents are read again.
  function cls.__locs_changed()
    local f = frame()
    f.index = nil; f.planes = nil
  end
  function cls.__invalidate()
    F.epoch = F.epoch + 1
    F.index = nil; F.planes = nil; F.npcs = nil; F.players = nil
    tile_cache = setmetatable({}, TILE_CACHE_MT)
  end
  function cls.__size(self) return 1, 1 end
  function cls.getId(self) return self.s.loc_id end
  function cls.getPlane(self) return self.s.level end
  function cls.getWorldView(self) return top() end
  function cls.getX(self)
    local sx = loc_scene_xy(self.s)
    local w = self:__size()
    return (2 * sx + w) * (TILE // 2)
  end
  function cls.getY(self)
    local _, sy = loc_scene_xy(self.s)
    local _, h = self:__size()
    return (2 * sy + h) * (TILE // 2)
  end
  function cls.getZ(self) return 0 end
  function cls.getHash(self)
    local sx, sy = loc_scene_xy(self.s)
    return ((self.s.loc_id & 0xFFFFFFFF) << 32) | ((self.s.level & 3) << 16) | ((loc_kind(self.s.shape) & 3) << 14)
      | ((sy & 0x7F) << 7) | (sx & 0x7F)
  end
  function cls.getLocalLocation(self) return lp(self:getX(), self:getY(), TOPLEVEL) end
  function cls.getWorldLocation(self)
    return C[KEY_WP].fromLocal__WorldView_int_int_int(top(), self:getX(), self:getY(), self.s.level)
  end
  function cls.getCanvasLocation__(self) return cls.getCanvasLocation__int(self, 0) end
  function cls.getCanvasLocation__int(self, z)
    return C["net.runelite.api.Perspective"].localToCanvas__Client_LocalPoint_int_int(nil, self:getLocalLocation(), self.s.level, z)
  end
  function cls.getCanvasTilePoly(self)
    local w, h = self:__size()
    local poly = C["net.runelite.api.Perspective"].getCanvasTileAreaPoly__Client_LocalPoint_int_int_int_int(nil, self:getLocalLocation(), w, h, self.s.level, 0)
    if poly ~= nil then
      poly.__world = { kind = "tile", tile_x = self.s.tile_x, tile_z = self.s.tile_z, level = self.s.level, size_x = w, size_z = h }
    end
    return poly
  end
  function cls.getCanvasTextLocation(self, graphics, text, z)
    return C["net.runelite.api.Perspective"].getCanvasTextLocation(nil, graphics, self:getLocalLocation(), text, z)
  end
  function cls.getMinimapLocation(self)
    return C["net.runelite.api.Perspective"].localToMinimap__Client_LocalPoint(nil, self:getLocalLocation())
  end
  function cls.__hull(self)
    local e = self.s.element_id
    if e == nil or e < 0 then return nil end
    local w, h = self:__size()
    return box_hull(self:getX(), self:getY(), w * TILE // 2, h * TILE // 2, element_height(e), { kind = "hull", element_id = e })
  end
  function cls.getClickbox(self) return self:__hull() end
  function cls.getOpOverride(self, op) return nil end
  function cls.isOpShown(self, op) return ((self.s.visible_ops or 0) >> op) & 1 == 1 end
  function cls.getConfig(self) return ((self.s.angle & 3) << 6) | (self.s.shape & 0x3F) end
  function cls.__renderable(self)
    local e = self.s.element_id
    if e == nil or e < 0 then return nil end
    return C["net.runelite.api.Model"].__of_element(e)
  end
  function cls.equals(self, o)
    if type(o) ~= "table" or o.__key ~= self.__key or o.s == nil then return false end
    local a, b = self.s, o.s
    return a.loc_id == b.loc_id and a.tile_x == b.tile_x and a.tile_z == b.tile_z and a.level == b.level and a.shape == b.shape
  end
  function cls.hashCode(self)
    local s = self.s
    return J.l2i(((((59 + s.loc_id) * 59 + s.tile_x) * 59 + s.tile_z) * 59 + s.level) * 59 + s.shape)
  end
  function cls.toString(self)
    return J.simple_name(self.__key) .. "(id=" .. self.s.loc_id .. ", " .. self.s.tile_x .. "," .. self.s.tile_z .. "," .. self.s.level .. ")"
  end
end)

def("net.runelite.api.GameObject", "net.runelite.api.TileObject", {}, function(cls)
  function cls.__size(self) return math.max(self.s.size_x, 1), math.max(self.s.size_z, 1) end
  function cls.sizeX(self) return math.max(self.s.size_x, 1) end
  function cls.sizeY(self) return math.max(self.s.size_z, 1) end
  function cls.getSceneMinLocation(self)
    local sx, sy = loc_scene_xy(self.s)
    return pt(sx, sy)
  end
  function cls.getSceneMaxLocation(self)
    local sx, sy = loc_scene_xy(self.s)
    return pt(sx + cls.sizeX(self) - 1, sy + cls.sizeY(self) - 1)
  end
  function cls.getConvexHull(self) return self:__hull() end
  function cls.getOrientation(self) return (self.s.angle & 3) * 512 end
  function cls.getModelOrientation(self) return (self.s.angle & 3) * 512 end
  function cls.getRenderable(self) return self:__renderable() end
end)

def("net.runelite.api.WallObject", "net.runelite.api.TileObject", {}, function(cls)
  function cls.getOrientationA(self) return 1 << (self.s.angle & 3) end
  function cls.getOrientationB(self)
    if self.s.shape == 2 then return 1 << ((self.s.angle + 1) & 3) end
    return 0
  end
  -- One scene element carries both halves of a corner wall: the second hull
  -- and renderable are nil so nothing is drawn twice.
  function cls.getConvexHull(self) return self:__hull() end
  function cls.getConvexHull2(self) return nil end
  function cls.getRenderable1(self) return self:__renderable() end
  function cls.getRenderable2(self) return nil end
end)

def("net.runelite.api.DecorativeObject", "net.runelite.api.TileObject", {}, function(cls)
  function cls.getConvexHull(self) return self:__hull() end
  function cls.getConvexHull2(self) return nil end
  function cls.getRenderable(self) return self:__renderable() end
  function cls.getRenderable2(self) return nil end
  function cls.getXOffset(self) return 0 end
  function cls.getYOffset(self) return 0 end
  function cls.getXOffset2(self) return 0 end
  function cls.getYOffset2(self) return 0 end
end)

def("net.runelite.api.GroundObject", "net.runelite.api.TileObject", {}, function(cls)
  function cls.getConvexHull(self) return self:__hull() end
  function cls.getRenderable(self) return self:__renderable() end
end)

-- ------------------------------------------------------------------ TileItem / ItemLayer

def("net.runelite.api.TileItem", nil, { "net.runelite.api.Renderable" }, function(cls)
  function cls.__fromSnap(s)
    local o = setmetatable({}, cls)
    o.s = s
    return o
  end
  function cls.getId(self) return self.s.obj_id end
  function cls.getQuantity(self) return self.s.count end
  function cls.getVisibleTime(self) return 0 end
  function cls.getDespawnTime(self) return 0 end
  function cls.getOwnership(self) return 0 end
  function cls.isPrivate(self) return false end
  function cls.getModel(self)
    local e = self.s.element_id
    if e == nil or e < 0 then return nil end
    return C["net.runelite.api.Model"].__of_element(e)
  end
  function cls.getModelHeight(self) return element_height(self.s.element_id) end
  function cls.getAnimationHeightOffset(self) return 0 end
  function cls.getRenderMode(self) return 0 end
  function cls.getHash(self) return 0 end
  function cls.equals(self, o)
    if type(o) ~= "table" or o.__key ~= self.__key then return false end
    local a, b = self.s, o.s
    return a.obj_id == b.obj_id and a.count == b.count and a.tile_x == b.tile_x and a.tile_z == b.tile_z and a.level == b.level
  end
  function cls.hashCode(self)
    local s = self.s
    return J.l2i(((((59 + s.obj_id) * 59 + s.count) * 59 + s.tile_x) * 59 + s.tile_z) * 59 + s.level)
  end
  function cls.toString(self) return "TileItem(id=" .. self.s.obj_id .. ", quantity=" .. self.s.count .. ")" end
end)

def("net.runelite.api.ItemLayer", "net.runelite.api.TileObject", {}, function(cls)
  -- s is a synthetic loc snapshot for the tile; items the TileItem list
  function cls.__of(tile_x, tile_z, level, items)
    local o = setmetatable({}, cls)
    local top_item = items[1]
    o.s = { loc_id = top_item.s.obj_id, tile_x = tile_x, tile_z = tile_z, level = level, shape = 22, angle = 0,
      size_x = 1, size_z = 1, element_id = top_item.s.element_id, visible_ops = 0 }
    o.items = items
    return o
  end
  function cls.getHeight(self) return 0 end
  function cls.getTop(self) return self.items[1] end
  function cls.getMiddle(self) return self.items[2] end
  function cls.getBottom(self) return self.items[#self.items] end
end)

-- ------------------------------------------------------------------ Tile

def("net.runelite.api.Tile", nil, {}, function(cls)
  local function make(plane, sx, sy)
    local key = tile_key(plane, sx, sy)
    local t = tile_cache[key]
    if t == nil then
      t = setmetatable({}, cls)
      t.plane = plane; t.sx = sx; t.sy = sy; t.key = key
      tile_cache[key] = t
    end
    return t
  end
  cls.__make = make
  function cls.__at(x, y, plane)
    local f = frame()
    local sx, sy = x - f.bx, y - f.bz
    if not f.world or plane < 0 or plane > 3 or sx < 0 or sx >= SCENE_SIZE or sy < 0 or sy >= SCENE_SIZE then return nil end
    return make(plane, sx, sy)
  end
  -- This tile's locs, rebuilt when the index has. Apart from its ground
  -- items (below): DetailedQuestStep.addItemTiles asks getGroundItems of
  -- EVERY tile of the scene, and fetching every loc's snapshot for that would
  -- allocate one table per loc of the scene for nothing.
  local function contents(self)
    local ix = scene_index(self.plane)
    if self.ix == ix then return self end
    self.ix = ix
    self.games = { n = 0 }; self.wall = nil; self.decor = nil; self.ground = nil
    local locs = ix.locs[self.key]
    if type(locs) == "number" then locs = { locs } end
    if locs then
      for _, prev in ipairs(locs) do
        local s = loc_at_cursor(prev)
        local o = s and C["net.runelite.api.TileObject"].__fromSnap(s)
        if o ~= nil then
        local k = loc_kind(s.shape)
        if k == KIND_GAME then self.games.n = self.games.n + 1; self.games[self.games.n] = o
        elseif k == KIND_WALL then self.wall = self.wall or o
        elseif k == KIND_DECOR then self.decor = self.decor or o
        else self.ground = self.ground or o end
        end
      end
    end
    return self
  end
  local function item_contents(self)
    local ix = scene_index(self.plane)
    if self.ix_items == ix then return self end
    self.ix_items = ix
    local items = ix.items[self.key]
    self.items = nil; self.layer = nil
    if items then
      local list = C["java.util.ArrayList"].__new__()
      local arr = {}
      for i, s in ipairs(items) do local ti = C["net.runelite.api.TileItem"].__fromSnap(s); arr[i] = ti; list:add(ti) end
      self.items = list
      local f = frame()
      self.layer = C["net.runelite.api.ItemLayer"].__of(self.sx + f.bx, self.sy + f.bz, self.plane, arr)
    end
    return self
  end
  function cls.getGameObjects(self) return contents(self).games end
  function cls.getWallObject(self) return contents(self).wall end
  function cls.getDecorativeObject(self) return contents(self).decor end
  function cls.getGroundObject(self) return contents(self).ground end
  function cls.getItemLayer(self) return item_contents(self).layer end
  function cls.getGroundItems(self) return item_contents(self).items end
  function cls.__has_content(self)
    local ix = scene_index(self.plane)
    return ix.locs[self.key] ~= nil or ix.items[self.key] ~= nil
  end
  function cls.getPlane(self) return self.plane end
  function cls.getRenderLevel(self) return self.plane end
  function cls.getSceneLocation(self) return pt(self.sx, self.sy) end
  function cls.getLocalLocation(self) return C[KEY_LP].fromScene__int_int_WorldView(self.sx, self.sy, top()) end
  function cls.getWorldLocation(self) return C[KEY_WP].fromScene__WorldView_int_int_int(top(), self.sx, self.sy, self.plane) end
  function cls.getBridge(self) return nil end
  function cls.getSceneTilePaint(self) return nil end
  function cls.getSceneTileModel(self) return nil end
  function cls.toString(self) return "Tile(" .. self.sx .. "," .. self.sy .. "," .. self.plane .. ")" end
end)

-- ------------------------------------------------------------------ WorldView / Scene / WorldEntity

-- One plane of Scene.getTiles(): 104 rows of 104, holding a Tile wherever
-- the scene has a loc or a ground item and nil elsewhere.
local function plane_tiles(plane)
  local f = frame()
  f.planes = f.planes or {}
  local rows = f.planes[plane]
  if rows then return rows end
  rows = { n = SCENE_SIZE }
  for x = 1, SCENE_SIZE do rows[x] = { n = SCENE_SIZE } end
  local ix = scene_index(plane)
  local lo, hi = tile_key(plane, 0, 0), tile_key(plane, SCENE_SIZE - 1, SCENE_SIZE - 1)
  local Tile = C["net.runelite.api.Tile"]
  for _, t in ipairs({ ix.locs, ix.items }) do
    for key in pairs(t) do
      if key >= lo and key <= hi then
        local rest = key - lo
        local sx, sy = rest // SCENE_SIZE, rest % SCENE_SIZE
        rows[sx + 1][sy + 1] = Tile.__make(plane, sx, sy)
      end
    end
  end
  f.planes[plane] = rows
  return rows
end

def("net.runelite.api.Scene", nil, { "net.runelite.api.Renderable" }, function(cls)
  function cls.__make(wv) local o = setmetatable({}, cls); o.wv = wv; return o end
  function cls.getTiles(self)
    return setmetatable({ n = 4 }, { __index = function(t, k)
      if math.type(k) == "integer" and k >= 1 and k <= 4 then
        local rows = plane_tiles(k - 1)
        rawset(t, k, rows)
        return rows
      end
      return nil
    end })
  end
  function cls.getExtendedTiles(self) return nil end
  function cls.getExtendedTileSettings(self) return nil end
  function cls.getTileHeights(self) return nil end
  function cls.getTileShapes(self) return nil end
  function cls.getDrawDistance(self) return 25 end
  function cls.getWorldViewId(self) return TOPLEVEL end
  function cls.getMinLevel(self) return 0 end
  function cls.getBaseX(self) return frame().bx end
  function cls.getBaseY(self) return frame().bz end
  function cls.isInstance(self) return is_instance() end
  function cls.getInstanceTemplateChunks(self) return template_chunks() end
  function cls.getMapRegions(self) return self.wv:getMapRegions() end
  -- RuneLite removes the object from the drawn scene; the client gives a
  -- plugin no such edit, so the loc stays drawn.
  function cls.removeGameObject(self, obj) end
  function cls.removeTile(self, tile) end
  function cls.getModel(self) return nil end
  function cls.getModelHeight(self) return 0 end
end)

def("net.runelite.api.WorldEntityConfig", nil, {}, function(cls)
  function cls.__of(id) local o = setmetatable({}, cls); o.id = id; return o end
  function cls.getId(self) return self.id end
  function cls.getCategory(self) return 0 end
  function cls.getBoundsX(self) return 0 end
  function cls.getBoundsY(self) return 0 end
  function cls.getBoundsWidth(self) return 0 end
  function cls.getBoundsHeight(self) return 0 end
end)

-- A live world entity (game.world_entity_next's snapshot; the client's own
-- Wev). Its local location is in its parent (the root, or a carrier boat's
-- own world); its world view is the boat's own world, the staging rectangle
-- everything aboard is addressed in.
def("net.runelite.api.WorldEntity", nil, { "net.runelite.api.CameraFocusableEntity" }, function(cls)
  local by_id = setmetatable({}, { __mode = "v" })
  function cls.__fromSnap(s)
    local e = by_id[s.id]
    if e == nil or e.s.config_id ~= s.config_id then
      e = setmetatable({}, cls)
      by_id[s.id] = e
    end
    e.s = s
    return e
  end
  local function parent_local(self)
    local s = self.s
    if s.parent_view ~= 0 then
      local p = wev_snaps().by[s.parent_view]
      if p == nil then return nil end
      return lp(s.fine_x - p.base_x * TILE, s.fine_z - p.base_z * TILE, s.parent_view)
    end
    local f = frame()
    return lp(s.fine_x - f.bx * TILE, s.fine_z - f.bz * TILE, TOPLEVEL)
  end
  function cls.getWorldView(self) return C["net.runelite.api.WorldView"].__byId(self.s.id) end
  function cls.getLocalLocation(self) return parent_local(self) end
  function cls.getCameraFocus(self) return parent_local(self) end
  function cls.getOrientation(self) return self.s.angle end
  function cls.getTargetLocation(self) return parent_local(self) end
  function cls.getTargetOrientation(self) return self.s.angle end
  -- A point of this entity's own world -> the main world, through the hull's
  -- live position and yaw (game.world_entity_to_main); any other point is
  -- returned as it is, as RuneLite's.
  function cls.transformToMainWorld(self, point)
    if point == nil or point.worldView ~= self.s.id then return point end
    local x, z = A().game.world_entity_to_main(self.s.id, point.x, point.y)
    if x == nil then return nil end
    local f = frame()
    return lp(x - f.bx * TILE, z - f.bz * TILE, TOPLEVEL)
  end
  function cls.isHiddenForOverlap(self) return false end
  function cls.getConfig(self) return C["net.runelite.api.WorldEntityConfig"].__of(self.s.config_id) end
  function cls.getOwnerType(self) return 0 end
  function cls.equals(self, o) return rawequal(self, o) end
  function cls.hashCode(self) return self.s.id end
end)

def("net.runelite.api.WorldView", nil, {}, function(cls)
  cls.TOPLEVEL = TOPLEVEL
  local the_top = nil
  function cls.__top()
    if the_top == nil then
      the_top = setmetatable({}, cls)
      the_top.scene = C["net.runelite.api.Scene"].__make(the_top)
    end
    return the_top
  end
  function cls.__byId(id)
    if id == TOPLEVEL then return cls.__top() end
    local snap = wev_snaps().by[id]
    if snap == nil then return nil end
    return C["net.runelite.api.WorldView$Boat"].__of(snap)
  end
  function cls.getId(self) return TOPLEVEL end
  function cls.isTopLevel(self) return true end
  function cls.getScene(self) return self.scene end
  function cls.getPlane(self) return current_plane() end
  function cls.getSizeX(self) return SCENE_SIZE end
  function cls.getSizeY(self) return SCENE_SIZE end
  function cls.getBaseX(self) return frame().bx end
  function cls.getBaseY(self) return frame().bz end
  function cls.getTileHeight(self, x, y, plane) return 0 end
  function cls.getTileHeights(self) return nil end
  function cls.getTileSettings(self) return nil end
  function cls.getCollisionMaps(self) return nil end
  function cls.isInstance(self) return is_instance() end
  function cls.getInstanceTemplateChunks(self) return template_chunks() end
  function cls.getYellowClickAction(self) return 0 end
  function cls.getMainWorldProjection(self) return nil end
  function cls.getCanvasProjection(self) return nil end
  function cls.getGraphicsObjects(self) return nil end
  function cls.contains__WorldPoint(self, point)
    return C[KEY_WP].isInScene__WorldView_int_int(self, point.x, point.y)
  end
  function cls.contains__LocalPoint(self, point)
    return point.worldView == TOPLEVEL and point:getSceneX() >= 0 and point:getSceneX() < SCENE_SIZE
      and point:getSceneY() >= 0 and point:getSceneY() < SCENE_SIZE
  end
  function cls.getSelectedSceneTile(self)
    local x, z, level = A().input.hover_tile()
    if x == nil then return nil end
    return C["net.runelite.api.Tile"].__at(x, z, level)
  end
  function cls.getMapRegions(self)
    local out, n, seen = {}, 0, {}
    if is_instance() then
      local chunks = template_chunks()
      for z = 1, chunks.n do
        for x = 1, chunks[z].n do
          for y = 1, chunks[z][x].n do
            local data = chunks[z][x][y]
            if data ~= -1 then
              local ty = ((data >> 3) & 0x7FF) * CHUNK
              local tx = ((data >> 14) & 0x3FF) * CHUNK
              local region = ((tx >> 6) << 8) | (ty >> 6)
              if not seen[region] then seen[region] = true; n = n + 1; out[n] = region end
            end
          end
        end
      end
    else
      local f = frame()
      if f.world then
        for rx = f.bx >> 6, (f.bx + SCENE_SIZE - 1) >> 6 do
          for ry = f.bz >> 6, (f.bz + SCENE_SIZE - 1) >> 6 do
            n = n + 1; out[n] = (rx << 8) | ry
          end
        end
      end
    end
    out.n = n
    return out
  end

  function cls.npcs(self)
    local f = frame()
    if f.npcs == nil then
      local list, by, n = {}, {}, 0
      if f.world then
        local NPC = C["net.runelite.api.NPC"]
        local cur, s = A().world.npc_next(nil)
        while cur do
          local npc = NPC.__fromSnap(s)
          n = n + 1; list[n] = npc; by[s.server_slot] = npc
          cur, s = A().world.npc_next(cur)
        end
      end
      list.n = n
      f.npcs = C["net.runelite.api.IndexedObjectSet"].__of(list, by)
    end
    return f.npcs
  end
  function cls.players(self)
    local f = frame()
    if f.players == nil then
      local list, by, n = {}, {}, 0
      if f.world then
        local P = C["net.runelite.api.Player"]
        local cur, s = A().world.player_next(nil)
        while cur do
          local p = P.__fromSnap(s)
          n = n + 1; list[n] = p
          if s.server_pid >= 0 then by[s.server_pid] = p end
          cur, s = A().world.player_next(cur)
        end
      end
      list.n = n
      f.players = C["net.runelite.api.IndexedObjectSet"].__of(list, by)
    end
    return f.players
  end
  -- The boats floating in this view, by id (their own world view's id).
  local function entities_in(self, parent)
    local f = wev_snaps() -- per frame: the sets are kept beside the snapshots
    local key = "set" .. parent
    if f[key] == nil then
      local list, by, n = {}, {}, 0
      local WE = C["net.runelite.api.WorldEntity"]
      for _, snap in ipairs(wev_snaps().list) do
        -- the api's parent 0 is the root, RuneLite's TOPLEVEL (-1)
        if (snap.parent_view == 0 and TOPLEVEL or snap.parent_view) == parent then
          local e = WE.__fromSnap(snap)
          n = n + 1; list[n] = e; by[snap.id] = e
        end
      end
      list.n = n
      f[key] = n == 0 and empty_set() or C["net.runelite.api.IndexedObjectSet"].__of(list, by)
    end
    return f[key]
  end
  cls.__entities_in = entities_in
  function cls.worldEntities(self) return entities_in(self, TOPLEVEL) end
  -- The top level and every boat's own world: ObjectStep.loadObjects and
  -- NpcStep walk worldViews() of the top level to reach the scene. Every
  -- actor and loc is in the top level's sets (a boat's own sets are empty),
  -- so the walks see each once.
  function cls.worldViews(self)
    local f = wev_snaps()
    if f.wv_set == nil then
      local list, by = { n = 1, self }, { [self:getId()] = self }
      for _, snap in ipairs(wev_snaps().list) do
        local v = cls.__byId(snap.id)
        list.n = list.n + 1; list[list.n] = v; by[snap.id] = v
      end
      f.wv_set = C["net.runelite.api.IndexedObjectSet"].__of(list, by)
    end
    return f.wv_set
  end
end)

-- A boat's own world (RuneLite's sub WorldView): the staging rectangle its
-- deck is authored in. Actors and locs reach a plugin through the top level
-- (their coordinates name this rectangle when they are aboard), so its own
-- npcs/players/scene are empty; its id, base, size and plane are the boat's.
def("net.runelite.api.WorldView$Boat", "net.runelite.api.WorldView", {}, function(cls)
  local views = setmetatable({}, { __mode = "v" })
  local EMPTY_TILES = { n = 4, { n = 0 }, { n = 0 }, { n = 0 }, { n = 0 } }
  local scene_of = setmetatable({}, { __mode = "k" })
  function cls.__of(snap)
    local v = views[snap.id]
    if v == nil then v = setmetatable({}, cls); views[snap.id] = v end
    v.snap = snap
    return v
  end
  function cls.getId(self) return self.snap.id end
  function cls.isTopLevel(self) return false end
  function cls.getPlane(self) return self.snap.deck_level end
  function cls.getSizeX(self) return self.snap.size_x end
  function cls.getSizeY(self) return self.snap.size_z end
  function cls.getBaseX(self) return self.snap.base_x end
  function cls.getBaseY(self) return self.snap.base_z end
  function cls.isInstance(self) return false end
  function cls.getInstanceTemplateChunks(self) return nil end
  function cls.getMapRegions(self) return { n = 0 } end
  function cls.npcs(self) return empty_set() end
  function cls.players(self) return empty_set() end
  function cls.worldEntities(self) return C["net.runelite.api.WorldView"].__entities_in(self, self.snap.id) end
  function cls.worldViews(self) return empty_set() end
  function cls.getSelectedSceneTile(self) return nil end
  function cls.contains__LocalPoint(self, point)
    return point.worldView == self.snap.id and point:getSceneX() >= 0 and point:getSceneX() < self.snap.size_x
      and point:getSceneY() >= 0 and point:getSceneY() < self.snap.size_z
  end
  -- No tiles: the deck's locs are in the top level's scene at their staging
  -- tiles, so QuestPerspective.getAreaFromWorldView finds nothing to outline
  -- and WorldEntityStep falls back to its plain arrow.
  local SceneStub = {}
  SceneStub.__index = SceneStub
  function SceneStub.getTiles() return EMPTY_TILES end
  function SceneStub.getExtendedTiles() return EMPTY_TILES end
  function SceneStub.getBaseX(self) return self.v.snap.base_x end
  function SceneStub.getBaseY(self) return self.v.snap.base_z end
  function SceneStub.isInstance() return false end
  function cls.getScene(self)
    local sc = scene_of[self]
    if sc == nil then sc = setmetatable({ v = self }, SceneStub); scene_of[self] = sc end
    return sc
  end
  function cls.equals(self, o) return rawequal(self, o) end
  function cls.hashCode(self) return self.snap.id end
end)

-- ------------------------------------------------------------------ GraphicsObject

-- Built by the entry from on_map_anim (GraphicsObjectCreated) as a table
-- {id, fine_x, fine_z, level, start_cycle}: scene-local fine units.
def("net.runelite.api.GraphicsObject", nil, { "net.runelite.api.Renderable" }, function(cls)
  function cls.__of(t) local o = setmetatable({}, cls); o.t = t; return o end
  function cls.getWorldView(self) return top() end
  function cls.getId(self) return self.t.id end
  function cls.getLocation(self) return lp(self.t.fine_x, self.t.fine_z, TOPLEVEL) end
  function cls.getStartCycle(self) return self.t.start_cycle or 0 end
  function cls.getLevel(self) return self.t.level end
  function cls.getZ(self) return 0 end
  function cls.finished(self) return false end
  function cls.getAnimation(self) return nil end
  function cls.getAnimationFrame(self) return 0 end
  function cls.getModel(self) return nil end
  function cls.getModelHeight(self) return 0 end
end)

-- ------------------------------------------------------------------ Perspective

def("net.runelite.api.Perspective", nil, {}, function(cls)
  function cls.__init(self) end

  local function canvas(x, y, height)
    local cx, cy = project(x, y, height)
    if cx == nil then return nil end
    return pt(cx, cy)
  end

  function cls.localToCanvas__Client_int_int_int(client, x, y, z) return canvas(x, y, -z) end
  function cls.localToCanvas__Client_int_int_int_int(client, world_id, x, y, z)
    if world_id ~= TOPLEVEL then return nil end
    return canvas(x, y, -z)
  end
  -- A point of a boat's own world is drawn where the boat carries it now
  -- (RuneLite's Perspective projects a sub-view point through its entity).
  local function on_main(point)
    if point.worldView == TOPLEVEL then return point end
    local e = top():worldEntities():byIndex(point.worldView)
    if e == nil then return nil end
    return e:transformToMainWorld(point)
  end
  cls.__on_main = on_main
  function cls.localToCanvas__Client_LocalPoint_int_int(client, point, plane, height_offset)
    point = on_main(point)
    if point == nil then return nil end
    return canvas(point.x, point.y, height_offset)
  end
  function cls.localToCanvas__Client_LocalPoint_int(client, point, plane)
    return cls.localToCanvas__Client_LocalPoint_int_int(client, point, plane, 0)
  end

  function cls.getTileHeight(client, point, plane) return 0 end
  function cls.getFootprintTileHeight(client, point, plane, footprint) return 0 end

  -- The minimap through the draw pass's minimap_project. A point the drawn
  -- minimap does not show (beyond its circle) is placed by extending the
  -- projection's own basis, taken from the focus and one tile east and north
  -- of it, so long lines clip the way RuneLite's do.
  function cls.localToMinimap__Client_LocalPoint_int(client, point, distance)
    point = on_main(point)
    if point == nil then return nil end
    local d = QH.draw
    if d == nil then return nil end
    local focus = player_snap()
    if focus == nil then return nil end
    local fx, fz = focus.fine_x, focus.fine_z
    local dx, dy = point.x - fx, point.y - fz
    if dx * dx + dy * dy >= distance * distance then return nil end
    local x, y = d.minimap_project(point.x, point.y)
    if x ~= nil then return pt(x, y) end
    local x0, y0 = d.minimap_project(fx, fz)
    local x1, y1 = d.minimap_project(fx + TILE, fz)
    local x2, y2 = d.minimap_project(fx, fz + TILE)
    if x0 == nil or x1 == nil or x2 == nil then return nil end
    local px = x0 + (dx * (x1 - x0) + dy * (x2 - x0)) / TILE
    local py = y0 + (dx * (y1 - y0) + dy * (y2 - y0)) / TILE
    return pt(J.d2i(px), J.d2i(py))
  end
  function cls.localToMinimap__Client_LocalPoint(client, point)
    local r = 20 << LOCAL_COORD_BITS
    local s = 4.0 / DEFAULT_MINIMAP_ZOOM
    return cls.localToMinimap__Client_LocalPoint_int(client, point, J.d2i(r * s))
  end

  function cls.getCanvasTileAreaPoly__Client_LocalPoint_int_int_int_int(client, point, size_x, size_y, level, height_offset)
    point = on_main(point)
    if point == nil then return nil end
    local wv = C["net.runelite.api.WorldView"].__byId(point.worldView)
    if wv == nil then return nil end
    local offset = ESCENE_OFFSET
    local msx, msy = point:getSceneX() + offset, point:getSceneY() + offset
    if msx < 0 or msy < 0 or msx >= wv:getSizeX() + 2 * offset or msy >= wv:getSizeY() + 2 * offset then return nil end
    if level == -1 then level = wv:getPlane() end
    local sw_x = point.x - (size_x * TILE // 2)
    local sw_y = point.y - (size_y * TILE // 2)
    local ne_x = point.x + (size_x * TILE // 2)
    local ne_y = point.y + (size_y * TILE // 2)
    local xs, ys = {}, {}
    local corners = { sw_x, sw_y, ne_x, sw_y, ne_x, ne_y, sw_x, ne_y }
    for i = 1, 4 do
      local cx, cy = project(corners[2 * i - 1], corners[2 * i], height_offset)
      if cx == nil then return nil end
      xs[i] = cx; ys[i] = cy
    end
    local f = frame()
    return new_polygon(xs, ys, 4, { kind = "tile", tile_x = f.bx + (sw_x // TILE),
      tile_z = f.bz + (sw_y // TILE), level = level, size_x = size_x, size_z = size_y })
  end
  function cls.getCanvasTileAreaPoly__Client_LocalPoint_int(client, point, size)
    return cls.getCanvasTileAreaPoly__Client_LocalPoint_int_int_int_int(client, point, size, size, -1, 0)
  end
  function cls.getCanvasTilePoly__Client_LocalPoint(client, point)
    return cls.getCanvasTileAreaPoly__Client_LocalPoint_int(client, point, 1)
  end
  function cls.getCanvasTilePoly__Client_LocalPoint_int(client, point, z_offset)
    return cls.getCanvasTileAreaPoly__Client_LocalPoint_int_int_int_int(client, point, 1, 1, -1, z_offset)
  end

  function cls.getCanvasImageLocation(client, point, image, z_offset)
    local wv = C["net.runelite.api.WorldView"].__byId(point.worldView)
    if wv == nil then return nil end
    local p = cls.localToCanvas__Client_LocalPoint_int_int(client, point, wv:getPlane(), z_offset)
    if p == nil then return nil end
    return pt(p.x - J.idiv(image:getWidth(), 2), p.y - J.idiv(image:getHeight(), 2))
  end
  function cls.getMiniMapImageLocation(client, point, image)
    local p = cls.localToMinimap__Client_LocalPoint(client, point)
    if p == nil then return nil end
    return pt(p.x - J.idiv(image:getWidth(), 2), p.y - J.idiv(image:getHeight(), 2))
  end
  function cls.getCanvasTextLocation(client, graphics, point, text, z_offset)
    if text == nil then return nil end
    local wv = C["net.runelite.api.WorldView"].__byId(point.worldView)
    if wv == nil then return nil end
    local p = cls.localToCanvas__Client_LocalPoint_int_int(client, point, wv:getPlane(), z_offset)
    if p == nil then return nil end
    local width = graphics:getFontMetrics__():stringWidth(text)
    return pt(p.x - J.idiv(width, 2), p.y)
  end
  function cls.getCanvasTextMiniMapLocation(client, graphics, point, text)
    local p = cls.localToMinimap__Client_LocalPoint(client, point)
    if p == nil then return nil end
    local fm = graphics:getFontMetrics__()
    return pt(p.x - J.idiv(fm:stringWidth(text), 2), p.y + J.idiv(fm:getHeight(), 2))
  end

  -- RuneLite projects the model's own triangles; with no vertices here, the
  -- clickbox is the box of one tile at (x, y) up to the model's height.
  function cls.getClickbox(client, wv, model, orientation, x, y, z)
    if model == nil then return nil end
    return box_hull(x, y, TILE // 2, TILE // 2, model:getModelHeight(), nil)
  end
end, function(cls)
  cls.UNIT = 0.0030679615
  cls.UNIT14 = 3.834951969714103E-4
  cls.LOCAL_COORD_BITS = LOCAL_COORD_BITS
  cls.LOCAL_TILE_SIZE = TILE
  cls.LOCAL_HALF_TILE_SIZE = TILE // 2
  cls.SCENE_SIZE = SCENE_SIZE
  local sine, cosine, sinef, cosinef = { n = 2048 }, { n = 2048 }, { n = 2048 }, { n = 2048 }
  for i = 0, 2047 do
    local s, c = math.sin(i * cls.UNIT), math.cos(i * cls.UNIT)
    sinef[i + 1] = s; cosinef[i + 1] = c
    sine[i + 1] = J.d2i(65536.0 * s); cosine[i + 1] = J.d2i(65536.0 * c)
  end
  cls.SINE = sine; cls.COSINE = cosine; cls.SINEF = sinef; cls.COSINEF = cosinef
  local sine14, cosine14 = { n = 0x4000 }, { n = 0x4000 }
  for i = 0, 0x3FFF do
    sine14[i + 1] = J.d2i(65536.0 * math.sin(i * cls.UNIT14))
    cosine14[i + 1] = J.d2i(65536.0 * math.cos(i * cls.UNIT14))
  end
  cls.SINE14 = sine14; cls.COSINE14 = cosine14
end)

-- ------------------------------------------------------------------ RuneLiteObject

def("net.runelite.api.RuneLiteObjectController", nil, {}, function(cls)
  function cls.__init(self)
    self.x = 0; self.y = 0; self.z = 0; self.level = 0; self.wv = TOPLEVEL
    self.orientation = 0; self.radius = 60; self.front_first = false
    self.tile_x = nil; self.tile_z = nil
  end
  function cls.getLocation(self) return lp(self.x, self.y, self.wv) end
  function cls.setLocation(self, point, level)
    self.x = point.x; self.y = point.y; self.wv = point.worldView; self.level = level
    local f = frame()
    self.tile_x = f.bx + (point.x // TILE)
    self.tile_z = f.bz + (point.y // TILE)
    self:__sync()
  end
  function cls.getX(self) return self.x end
  function cls.getY(self) return self.y end
  function cls.getZ(self) return self.z end
  function cls.setZ(self, z) self.z = z; self:__sync() end
  function cls.getWorldView(self) return self.wv end
  function cls.getLevel(self) return self.level end
  function cls.getRadius(self) return self.radius end
  function cls.setRadius(self, r) self.radius = r end
  function cls.isDrawFrontTilesFirst(self) return self.front_first end
  function cls.setDrawFrontTilesFirst(self, b) self.front_first = b end
  function cls.getOrientation(self) return self.orientation end
  function cls.setOrientation(self, o) self.orientation = o & 2047; self:__sync() end
  function cls.getRenderMode(self) return 0 end
  function cls.getModel(self) return nil end -- abstract in RuneLite
  function cls.tick(self, ticks) end
  function cls.__sync(self) end
end)

-- Every RuneLiteObject that holds scene instances, for teardown.
local live_objects = setmetatable({}, { __mode = "k" })

def("net.runelite.api.RuneLiteObject", "net.runelite.api.RuneLiteObjectController", {}, function(cls, super)
  function cls.__init(self, client)
    super.__init(self)
    self.active = false; self.loop = false
    self.model = nil; self.animation = nil; self.anim_start = 0
    self.instances = nil; self.instances_for = nil; self.anim_applied = nil
  end
  function cls.getModel(self) return self.model end
  function cls.getBaseModel(self) return self.model end
  function cls.setModel(self, model) self.model = model; self:__sync() end
  function cls.getAnimation(self) return self.animation end
  function cls.setAnimation(self, animation)
    self.animation = animation
    self.anim_start = A().client.world_cycle()
    self.anim_applied = nil
    self:__sync()
  end
  function cls.setShouldLoop(self, loop)
    if self.loop ~= loop then self.loop = loop; self.anim_applied = nil; self:__sync() end
  end
  function cls.getAnimationFrame(self)
    if self.animation == nil then return 0 end
    return self.animation:__frame_at(A().client.world_cycle() - self.anim_start, self.loop)
  end
  function cls.finished(self)
    if self.animation == nil or self.loop then return false end
    return cls.getAnimationFrame(self) + 1 >= self.animation:getNumFrames()
  end
  function cls.isActive(self) return self.active end
  function cls.setActive(self, active)
    if self.active == active then return end
    self.active = active
    self:__sync()
  end
  function cls.getAnimationController(self) return nil end
  function cls.getPoseAnimationController(self) return nil end
  function cls.getStartCycle(self) return self.anim_start end
  function cls.tick(self, ticks) end

  local function destroy_instances(self)
    if self.instances == nil then return end
    for _, inst in ipairs(self.instances) do A().scene.instance_destroy(inst) end
    self.instances = nil; self.instances_for = nil; self.anim_applied = nil
    live_objects[self] = nil
  end
  cls.__destroy = destroy_instances
  function cls.__destroyAll()
    local list = {}
    for o in pairs(live_objects) do list[#list + 1] = o end
    for _, o in ipairs(list) do destroy_instances(o) end
  end

  -- Bring the scene instances in line with this object: one instance per
  -- cache model the ModelData was merged from, all at one tile.
  function cls.__sync(self)
    local scene = A().scene
    if not self.active or self.model == nil or self.model.data == nil or self.tile_x == nil then
      if self.instances then for _, inst in ipairs(self.instances) do scene.instance_active(inst, false) end end
      return
    end
    if self.instances_for ~= self.model then
      destroy_instances(self)
      local data = self.model.data
      local list = {}
      for i = 1, data.ids.n do
        local inst, why = scene.instance_create()
        if inst == nil then
          J.log("RuneLiteObject: scene instance refused: " .. tostring(why))
          break
        end
        list[#list + 1] = inst
        scene.instance_cache_model(inst, 0, data.ids[i])
        for k = 1, data.recolors.n do scene.instance_recolor(inst, data.recolors[k][1], data.recolors[k][2]) end
        scene.instance_light(inst, self.model.ambient, self.model.contrast)
      end
      self.instances = list
      self.instances_for = self.model
      live_objects[self] = true
    end
    local anim_key = self.animation and (self.animation.id * 2 + (self.loop and 1 or 0)) or -1
    for _, inst in ipairs(self.instances) do
      scene.instance_position(inst, self.tile_x, self.tile_z, self.level, self.z, (self.orientation + self.model.data.yaw) & 2047)
      if self.anim_applied ~= anim_key and self.animation ~= nil then
        scene.instance_animation(inst, self.animation.id, self.loop)
      end
      scene.instance_active(inst, true)
    end
    self.anim_applied = anim_key
  end
end)

-- ------------------------------------------------------------------ hooks rl_client calls

J.rl_world_view = function() return C["net.runelite.api.WorldView"].__top() end
J.rl_local_player = function() return C["net.runelite.api.Player"].__local() end
J.rl_create_runelite_object = function()
  local client = J.rl_client and J.rl_client.client() or nil
  return C["net.runelite.api.RuneLiteObject"].__new(client)
end
J.rl_load_model_data = function(id) return C["net.runelite.api.ModelData"].__load(id) end
J.rl_merge_models = function(arr) return C["net.runelite.api.ModelData"].__merge(arr) end
J.rl_load_animation = function(id) return C["net.runelite.api.Animation"].__load(id) end
