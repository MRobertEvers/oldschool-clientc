--
-- Tile Indicator
--
-- Marks the local player's TRUE tile -- the one the server believes they are
-- standing on -- the tile they are walking toward, and the tile under the
-- mouse pointer.
--
-- The distinction is the entire point. What the model draws is an interpolated
-- position that slides between tiles every frame; the server only ever knows
-- the player as being on one whole tile. Anything reasoned about in ticks
-- (a safespot, a stall, a tick-perfect step) is reasoned about in true tiles,
-- and until you can see it you are guessing.
--
-- The C twin of this file is src/plugin/plugins/tileind.c, which registers as
-- "tile-indicator". Both are built on the same api and draw the same thing,
-- which is what keeps the contract honest about being language-agnostic
-- rather than Lua-shaped -- so this one carries the "-lua" suffix. Two
-- plugins cannot share a name: it is the ini section and the panel row, and
-- the host refuses the second one outright.
--

---@type torirs.Plugin
local plugin = {
  name    = "tile-indicator-lua",
  version = "1.0.0",
  config  = {
    {
      key = "true_color",
      type = "color",
      default = "#00FFFF",
      label = "True tile colour"
    },
    {
      key = "true_fill",
      type = "int",
      default = "40",
      min = 0,
      max = 255,
      label = "True tile fill"
    },
    {
      key = "dest_color",
      type = "color",
      default = "#FFFF00",
      label = "Destination colour"
    },
    {
      key = "show_dest",
      type = "bool",
      default = "1",
      label = "Show destination"
    },
    {
      key = "hover_color",
      type = "color",
      default = "#FFFFFF",
      label = "Hover tile colour"
    },
    {
      key = "hover_fill",
      type = "int",
      default = "0",
      min = 0,
      max = 255,
      label = "Hover tile fill"
    },
    {
      key = "show_hover",
      type = "bool",
      default = "1",
      label = "Show hover tile"
    },
  },
}

local function plugin_draw_player(api, draw)
  local me = api.local_player()
  if not me then return end

  draw.tile(me.true_x, me.true_z, me.level,
    api.config.true_color, api.config.true_fill)

  if not api.config.show_dest then return end

  -- Where the walk ends: the far end of the route queue.
  --
  -- Between the click and the server's echo that queue is still empty, and
  -- for that window the minimap flag latch is the only record of where the
  -- click was aimed. Without the fallback the destination marker blinks off
  -- for exactly the tick a player is watching it.
  local dx, dz = me.dest_x, me.dest_z
  if dx == me.true_x and dz == me.true_z and me.flag_x >= 0 then
    dx, dz = me.flag_x, me.flag_z
  end

  if dx ~= me.true_x or dz ~= me.true_z then
    draw.tile(dx, dz, me.level, api.config.dest_color, 0)
  end
end

function plugin.on_draw_world(api, draw)
  plugin_draw_player(api, draw)

  if not api.config.show_hover then return end

  -- The tile the pointer is over, which is the tile a click would act on --
  -- so it answers the question the true-tile marker cannot: not "where does
  -- the server think I am" but "where am I about to send myself".
  --
  -- Its own level, not the player's: on an upper floor the pick answers the
  -- storey the pointer actually landed on, and drawing it at the player's
  -- level would put the marker on the ground below. The api hands back the
  -- WALKED level, so a bridge deck's marker sits on the deck rather than a
  -- storey above it.
  local hx, hz, hlevel = api.hover_tile()
  if not hx then return end

  draw.tile(hx, hz, hlevel, api.config.hover_color, api.config.hover_fill)
end

return plugin
