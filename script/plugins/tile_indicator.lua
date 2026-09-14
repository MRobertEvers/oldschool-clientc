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
-- Nothing in this file has to know any of that: the three addresses it is
-- handed are the three addresses it passes on, untouched, and that is exactly
-- why the staging band survives the trip.
--
-- The C twin of this file is src/plugin/plugins/tileind.c, which registers as
-- "tile-indicator-c". Both are built on the same api and draw the same thing,
-- which is what keeps the contract honest about being language-agnostic
-- rather than Lua-shaped -- so this one carries the "-lua" suffix. Two
-- plugins cannot share a name: it is the ini section and the panel row, and
-- the host refuses the second one outright.
--
-- WHAT PORCELAIN IS HERE FOR, AND WHAT IT IS NOT
--
-- Not the picture. Three primitives, the same order, the same colours, the
-- same tiles, on every lane. The ledger calls this plugin the yardstick and
-- every one of its rows is SUPPORTED; a port that moved a pixel of it would be
-- a defect, not a port.
--
-- It does not ask for porcelain.draw_context. That verb answers the pass's
-- drawable rectangle and whether that rectangle IS the canvas, and a world
-- tile is named in SCENE terms: v2_builder_world_tile hands the address
-- straight to api_draw_tile without the scope's origin or clip touching it, so
-- the pass region is not an input to a single one of these three calls.
-- Returning early on the false it can answer would SUPPRESS three markers that
-- would have drawn correctly -- which is a behaviour change dressed as a
-- check. Its sibling overlay, entity-highlighter, declines the same verb for
-- the same reason.
--
-- What the layer IS opened for is the refusal channel, and this plugin has
-- exactly one refusal to put on it -- the one it cannot see:
--
--   draw.world_tile returns (ok, result) and the ledger reads that pair as "a
--   refused draw is visible to the script". It is not. v2_builder_world_tile
--   returns TORIRS_RESULT_OK unconditionally, and api_draw_tile -- which is
--   where the per-frame draw-budget gate actually lives -- returns void and
--   swallows the refusal. Reading the pair here would pin a constant while
--   reading like a check that bites, so it is not read; the gap is DECLARED
--   instead, with porcelain.expect_unsupported, so it is one expected finding
--   in every capture rather than a sentence in a commit message. When the
--   engine half lands, the declaration comes out and a real refusal becomes an
--   unexpected finding on the same channel.
--
-- One more verb that does not fit, for the record: porcelain.hover answers the
-- hovered container CELL -- an obj, a slot, which panel it belongs to -- and
-- there is no tile anywhere in it. The hovered TILE is still
-- api.input.hover_tile(), which is the scene pick, at the level the pick
-- landed on.
--
-- There is no describe, no fence and no commit: nothing here is retained, so
-- there is nothing to reconcile, and the steady state costs zero engine
-- setters and zero revalidates because it makes no layer call at all.
--

---@type torirs.Plugin
local plugin = {
  id      = "tile-indicator-lua",
  title   = "Tile Indicator (Lua)",
  version = "2.0.0",
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
  local me = api.world.local_player()
  if not me then return end

  draw.world_tile(me.true_x, me.true_z, me.level,
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
    draw.world_tile(me.dest_x, me.dest_z, me.level,
      api.config.dest_fill_color, api.config.dest_color, api.config.dest_fill_alpha)
  end
end

function plugin.on_start(api)
  if not api.porcelain.open() then
    -- Out loud: without the layer there is no refusal channel at all, so the
    -- one thing this plugin knows it cannot report goes unsaid. The markers
    -- outrank the declaration and still draw.
    api.core.log("tile-indicator-lua: no porcelain layer -- the draw refusal gap is unsaid")
    return
  end
  api.porcelain.expect_unsupported("draw_refusal_readout",
    "world_tile answers OK even when the budget refused it: api_draw_tile returns void")
end

function plugin.on_draw_world(api, draw)
  -- Match the C implementation: hover below true/destination markers.
  if api.config.show_hover then
    local hx, hz, hlevel = api.input.hover_tile()
    if hx then
      draw.world_tile(hx, hz, hlevel,
        api.config.hover_fill_color, api.config.hover_color, api.config.hover_fill_alpha)
    end
  end
  plugin_draw_player(api, draw)
end

return plugin
