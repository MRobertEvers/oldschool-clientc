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
-- Boat-aware for free: aboard a vessel the api hands back deck tiles as
-- STAGING-ABSOLUTE addresses (the boat instance's own tiles), and draw.world_tile
-- recognises that band and draws the marker through the hull's live
-- transform -- position, yaw and bob -- so every marker rides the boat.
-- Nothing in this file has to know any of that.
--
-- The C twin of this file is src/plugin/plugins/tileind.c, which registers as
-- "tile-indicator-c". Both are built on the same api and draw the same thing,
-- which is what keeps the contract honest about being language-agnostic
-- rather than Lua-shaped -- so this one carries the "-lua" suffix. Two
-- plugins cannot share a name: it is the ini section and the panel row, and
-- the host refuses the second one outright.
--

---@type torirs.Plugin
local plugin = {
  id      = "tile-indicator-lua",
  title   = "Tile Indicator (Lua)",
  version = "1.0.0",
  config  = {
    -- Every marker is an outline colour, a fill colour and the fill's
    -- opacity. The fill used to be the opacity alone, washed in the outline's
    -- colour, which left a text box asking for a number between 0 and 255
    -- sitting between two colour rows -- the one setting on the tab you could
    -- not point at. The number stays for the one thing a palette entry cannot
    -- say: how much of the ground below still shows through, 0 meaning
    -- outline only. The defaults keep every marker looking as it did.
    {
      key = "true_color",
      type = "color",
      default = "#00FFFF",
      label = "True tile colour"
    },
    {
      key = "true_fill_color",
      type = "color",
      default = "#00FFFF",
      label = "True tile fill"
    },
    {
      key = "true_fill_alpha",
      type = "int",
      default = "40",
      min = 0,
      max = 255,
      label = "True tile fill opacity"
    },
    {
      key = "dest_color",
      type = "color",
      default = "#FFFF00",
      label = "Destination colour"
    },
    {
      key = "dest_fill_color",
      type = "color",
      default = "#FFFF00",
      label = "Destination fill"
    },
    {
      key = "dest_fill_alpha",
      type = "int",
      default = "0",
      min = 0,
      max = 255,
      label = "Destination fill opacity"
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
      key = "hover_fill_color",
      type = "color",
      default = "#FFFFFF",
      label = "Hover tile fill"
    },
    {
      key = "hover_fill_alpha",
      type = "int",
      default = "0",
      min = 0,
      max = 255,
      label = "Hover tile fill opacity"
    },
    {
      key = "show_hover",
      type = "bool",
      default = "1",
      label = "Show hover tile"
    },
  },
}

-- One marker, drawn and then CHECKED.
--
-- The result is not decoration. The client's world overlay pool is finite, and
-- in a crowded scene -- a hundred health bars, hitsplats and overhead names --
-- it fills, at which point a quad pushed into it is dropped. On screen that is
-- indistinguishable from the plugin being switched off, so the log has to be
-- the thing that tells them apart.
--
-- Once per load, not once per frame: the condition lasts as long as the crowd
-- does, and a line a frame would bury everything else in the log. The C twin
-- reports the same TILEIND_MARKER_DROPPED, with the result as its number
-- rather than its name -- the name is what the Lua binding is handed.
local drop_reported = false

local function marker(api, draw, tile_x, tile_z, level, fill, outline, alpha)
  local ok, reason = draw.world_tile(tile_x, tile_z, level, fill, outline, alpha)
  if ok or drop_reported then return end
  drop_reported = true
  api.core.log("TILEIND_MARKER_DROPPED result=", reason,
    " tile=", tile_x, ",", tile_z, " level=", level)
end

local function plugin_draw_player(api, draw)
  local me = api.world.local_player()
  if not me then return end

  marker(api, draw, me.true_x, me.true_z, me.level,
    api.config.true_fill_color, api.config.true_color, api.config.true_fill_alpha)

  if not api.config.show_dest then return end

  -- Where the walk ends, which is the map flag and nothing else -- the same
  -- value RuneLite's tile indicator draws through
  -- Client.getLocalDestinationLocation().
  --
  -- Reading the route queue instead is what made this marker vanish a tick
  -- after the click: that queue is the interpolator's history, so its far end
  -- trails BEHIND the player and never holds the destination at all. The flag
  -- is set from the routed destination and cleared on arrival, which is
  -- exactly as long as a destination marker should live.
  if me.dest_x ~= me.true_x or me.dest_z ~= me.true_z then
    marker(api, draw, me.dest_x, me.dest_z, me.level,
      api.config.dest_fill_color, api.config.dest_color, api.config.dest_fill_alpha)
  end
end

function plugin.on_draw_world(api, draw)
  -- Match the C implementation: hover below true/destination markers.
  if api.config.show_hover then
    local hx, hz, hlevel = api.input.hover_tile()
    if hx then
      marker(api, draw, hx, hz, hlevel,
        api.config.hover_fill_color, api.config.hover_color, api.config.hover_fill_alpha)
    end
  end
  plugin_draw_player(api, draw)
end

return plugin
