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

local function plugin_draw_player(api, draw)
  local me = api.local_player()
  if not me then return end

  draw.tile(me.true_x, me.true_z, me.level,
    api.config.true_color, api.config.true_fill_color, api.config.true_fill_alpha)

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
    draw.tile(me.dest_x, me.dest_z, me.level,
      api.config.dest_color, api.config.dest_fill_color, api.config.dest_fill_alpha)
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

  draw.tile(hx, hz, hlevel,
    api.config.hover_color, api.config.hover_fill_color, api.config.hover_fill_alpha)
end

return plugin
