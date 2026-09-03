--
-- Loot Beam
--
-- A beam of light over every ground item worth more than a threshold, after
-- RuneLite's Ground Items plugin (net.runelite.client.plugins.grounditems.
-- Lootbeam). Both of its styles are here and they are not cosmetic variants of
-- each other -- MODERN is the bright-cored column with two recoloured bands,
-- LIGHT the plainer older graphic with one -- so both are separate models.
--
-- The models are SHIPPED, not named. An earlier version pointed the beam at
-- cache model 43330 and sequence 9260, which is the OSRS pair, and that number
-- means the beam in one revision, means some other model in the next, and is
-- absent from most of the dozen this client boots -- so the plugin worked on
-- the cache it was written against and silently drew nothing, or a crate, on
-- the rest. Four config rows existed only so a user could go and find the
-- right ids for their revision, which is not a thing a user can be asked to
-- do.
--
-- beam_modern.model and beam_light.model are those two models, lifted out of
-- OSRS 239 once and carried in this plugin's own asset folder
-- (`make -C src plugin-beam-assets` re-extracts them). Neither carries a
-- textured face, so nothing about how they draw depends on the booted cache;
-- api.model_load hands the bytes to a decoder that reads the model format off
-- the file's own trailer rather than off the revision.
--
-- What does NOT come with them is the animation: sequence 9260 is a rig
-- driving transform groups, and shipping it would mean shipping its frames and
-- its framemap too. The rise here is a spin instead -- the model turns on its
-- own axis, one yaw update per beam per frame, no model rebuilt for it.
--
-- The beam is a WORLD OBJECT, not an overlay. That is the whole reason this
-- plugin needed anything new: an overlay is painted after the scene and is
-- therefore always in front, so a beam drawn that way shines through the
-- building standing between you and the drop. A world object is registered
-- with the painter and sorts against the locs, which is what makes it read as
-- a thing in the world rather than a mark on the glass.
--
-- Value comes from ObjType.cost -- the cache's own number, what OC_COST reads.
-- It is not a Grand Exchange price and the client has no way to know one, so
-- the plugin also reads an optional `prices.txt` asset: `obj_id=price` lines
-- that override the cache for the items whose cache value is nothing like
-- their real one (the coin-value of a rune scimitar is not 25,600).
--
-- What a beam is measured against is `value_mode`, and its default is the
-- HIGH-ALCHEMY price rather than that raw cost, for the same reason
-- ground_items.lua defaults the same way: cost is a shop number, and the alch
-- price derived from it -- floor(cost * 0.6), the reference's own
-- ItemComposition.getHaPrice -- is the one the game will actually pay. The
-- two plugins spell this row identically so a threshold set on one can be
-- read off the other.
--

---@type torirs.Plugin
local plugin           = {
    name    = "loot-beam",
    title   = "Loot Beams",
    version = "2.0.0",
    config  = {
        {
            key = "tier",
            type = "enum",
            default = "high",
            choices = "off|low|medium|high|insane",
            label = "Beam from tier"
        },
        {
            key = "style",
            type = "enum",
            default = "modern",
            choices = "modern|light",
            label = "Beam style"
        },
        -- Spelled exactly as ground_items.lua's row of the same name, and
        -- defaulted the same way -- see the header. `value` is the raw cost
        -- (prices.txt applies to it), `highest` takes whichever of the two is
        -- larger.
        {
            key = "value_mode",
            type = "enum",
            default = "alch",
            choices = "alch|value|highest",
            label = "Value calculation"
        },

        {
            key = "low_value",
            type = "int",
            default = "20000",
            min = 0,
            max = 2000000000,
            label = "Low value"
        },
        {
            key = "medium_value",
            type = "int",
            default = "100000",
            min = 0,
            max = 2000000000,
            label = "Medium value"
        },
        {
            key = "high_value",
            type = "int",
            default = "1000000",
            min = 0,
            max = 2000000000,
            label = "High value"
        },
        {
            key = "insane_value",
            type = "int",
            default = "10000000",
            min = 0,
            max = 2000000000,
            label = "Insane value"
        },

        { key = "low_color",    type = "color", default = "#66B2FF", label = "Low colour" },
        { key = "medium_color", type = "color", default = "#99FF99", label = "Medium colour" },
        { key = "high_color",   type = "color", default = "#FF9600", label = "High colour" },
        { key = "insane_color", type = "color", default = "#FF66B2", label = "Insane colour" },

        -- The one thing left that is a choice rather than a cache id. The
        -- shipped models carry their own size, so there is no height row: a
        -- beam is as tall as the model is.
        {
            key = "spin",
            type = "int",
            default = "90",
            min = 0,
            max = 720,
            label = "Spin (degrees/sec)"
        },
    },
}

--
-- What each style ships, and what its bands are recoloured from.
--
-- The face colours are the models' own: LIGHT paints its single band 6371,
-- MODERN paints its body 26432 and its core 26584 -- reference
-- Lootbeam.Style, which recolours exactly these. The models are the ones those
-- constants were written against, so they stay the values they are rather than
-- becoming keys this plugin invented.
--
local STYLES        = {
    modern = {
        asset = "beam_modern.model",
        -- Body first, core second: the core is the one that gets the extra
        -- luminance, which is what gives the beam a bright centre instead of a
        -- flat coloured tube.
        body  = 26432,
        core  = 26584,
    },
    light  = {
        asset = "beam_light.model",
        body  = 6371,
        core  = nil,
    },
}

local LUMINANCE_MAX = 127

local PRICES_ASSET  = "prices.txt"

-- tile key -> { handle, rgb, style, x, z, level, phase }. Keyed on the
-- ABSOLUTE tile, which is what survives a scene rebuild -- the same reason
-- api.object_position takes one.
local beams         = {}
-- style name -> model handle from api.model_load. Loaded on first use rather
-- than at start: a client that never shows a LIGHT beam never reads its file.
local models        = {}
-- obj_id -> price, from the asset. Empty until it lands, and empty forever if
-- it is not shipped; the cache cost is the fallback either way.
local prices        = {}
-- Set by every edge that can change what should be lit; drained on the server
-- tick, so a packet burst that adds ten stacks rebuilds once and not ten
-- times.
local dirty         = true
-- Beams standing after the last rebuild, so the count is only reported when it
-- moves.
local live          = 0

-- The model for a style, asked for on first use. nil only when the resident
-- model table is full, which two files cannot fill.
local function model_for(api, style)
    local handle = models[style]

    if handle then return handle end
    handle = api.model_load(STYLES[style].asset)
    models[style] = handle
    return handle
end

local function tier_rank(name)
    if name == "low" then return 1 end
    if name == "medium" then return 2 end
    if name == "high" then return 3 end
    if name == "insane" then return 4 end
    return 0
end

-- The colour a value earns, or nil when it earns none. Walked from the top so
-- the highest tier a value clears wins, and gated on the configured tier so
-- "beam from high" does not light the low-value drops underneath it.
local function tier_colour(api, value)
    local floor = tier_rank(api.config.tier)
    if floor == 0 then return nil end

    if value >= api.config.insane_value and floor <= 4 then return api.config.insane_color end
    if value >= api.config.high_value and floor <= 3 then return api.config.high_color end
    if value >= api.config.medium_value and floor <= 2 then return api.config.medium_color end
    if value >= api.config.low_value and floor <= 1 then return api.config.low_color end
    return nil
end

-- Reference ItemComposition.getHaPrice: price * HIGH_ALCHEMY_MULTIPLIER
-- (0.6f), truncated. Kept as a rational so the arithmetic stays in integers,
-- and applied per UNIT before the stack multiply -- which is where the
-- reference truncates too.
local HA_NUM, HA_DEN = 3, 5

-- What this stack is worth under the configured mode. `alch` is the default;
-- see the header.
local function value_of(api, obj)
    local unit = prices[obj.obj_id] or obj.cost
    local exchange = unit * obj.count
    local alch = (unit * HA_NUM // HA_DEN) * obj.count
    local mode = api.config.value_mode

    if mode == "value" then return exchange end
    if mode == "highest" then return exchange > alch and exchange or alch end
    return alch
end

-- Parse `obj_id=price` lines. Anything else -- blank lines, `#` comments, a
-- line we cannot read -- is skipped rather than failing the file: a price
-- table is a convenience, and one bad row must not cost the plugin the other
-- ten thousand.
local function parse_prices(text)
    local out = {}
    local n = 0
    for id, price in string.gmatch(text, "(%d+)%s*=%s*(%d+)") do
        out[tonumber(id)] = tonumber(price)
        n = n + 1
    end
    return out, n
end

--
-- Point a beam at a colour and a style.
--
-- Everything here is applied to the object's INTENT; the host rebuilds the
-- model behind it and the beam appears when the file has landed.
--
-- Reference Lootbeam.Style.MODERN: the body loses a notch of saturation and
-- the core gains 24 luminance, which is what gives the beam a bright centre
-- instead of a flat coloured tube. The saturation step is skipped on an
-- already-dull colour (sat <= 2) because taking one off would push it to grey.
--
local function dress(api, beam, rgb, style)
    local handle = beam.handle
    local shape = STYLES[style]
    local model = model_for(api, style)
    local hsl = api.hsl(rgb)
    local h, s, l = api.hsl_unpack(hsl)
    local sat_step = s > 2 and 1 or 0

    if not model then return end

    api.object_clear_recolors(handle)
    api.object_model(handle, model, "asset")
    api.object_recolor(handle, shape.body, api.hsl_pack(h, s - sat_step, l))
    if shape.core then
        api.object_recolor(
            handle, shape.core, api.hsl_pack(h, s, math.min(l + 24, LUMINANCE_MAX)))
    end
    -- The reference lights this model well above the default; without it the
    -- recoloured bands read as dark plastic rather than as light.
    api.object_light(handle, 75, 1875)
    beam.rgb, beam.style = rgb, style
end

local function rebuild(api)
    local style = api.config.style
    local want = {}
    local tally = 0

    for obj in api.objs() do
        local value = value_of(api, obj)
        tally = tally + 1
        local rgb = tier_colour(api, value)
        if rgb then
            -- One beam per TILE, coloured by the best thing on it: a tile with
            -- a rune scimitar and a bone under it is one beam, not two in the
            -- same place fighting over the same pixels.
            local key = obj.level .. ":" .. obj.tile_x .. ":" .. obj.tile_z
            local best = want[key]
            if not best or value > best.value then
                want[key] = {
                    value = value,
                    rgb = rgb,
                    x = obj.tile_x,
                    z = obj.tile_z,
                    level = obj.level
                }
            end
        end
    end

    for key, beam in pairs(beams) do
        if not want[key] then
            api.object_destroy(beam.handle)
            beams[key] = nil
        end
    end

    local before = live
    for key, w in pairs(want) do
        local beam = beams[key]
        if not beam then
            local handle = api.object_create()
            if handle then
                beam = { handle = handle }
                beams[key] = beam
            end
        end
        if beam then
            if beam.rgb ~= w.rgb or beam.style ~= style then
                dress(api, beam, w.rgb, style)
            end
            -- Held for the spin, which restates the position every frame with
            -- nothing but the yaw moved.
            beam.x, beam.z, beam.level = w.x, w.z, w.level
            -- Beams on neighbouring tiles turning in lockstep read as one
            -- rigid object rather than as several lights; the tile is a phase
            -- that is stable across a rebuild, which frame_ms alone is not.
            beam.phase = (w.x * 137 + w.z * 311) % 2048
            api.object_position(beam.handle, w.x, w.z, w.level, 0, beam.phase)
            api.object_active(beam.handle, true)
        end
    end

    -- Only when the count moves. "No beams appear" is the report this plugin
    -- will get, and it has two very different causes -- nothing on the floor
    -- clears the threshold, or beams exist and are not being drawn. One line
    -- separates them; a line per tick would bury both.
    live = 0
    for _ in pairs(beams) do live = live + 1 end
    if live ~= before then
        api.log(live .. " beam(s) over " .. tally .. " ground stack(s)")
    end
end

local function clear(api)
    for key, beam in pairs(beams) do
        api.object_destroy(beam.handle)
        beams[key] = nil
    end
    live = 0
end

function plugin.on_start(api)
    beams, models, prices, dirty, live = {}, {}, {}, true, 0
    -- Optional: a client without the file simply prices everything from the
    -- cache. on_asset hears about it either way.
    api.asset_load(PRICES_ASSET)
end

function plugin.on_stop(api)
    -- The model files go with the plugin; the host releases them when it stops
    -- one, for the same reason it takes its objects out of the world.
    clear(api)
end

function plugin.on_asset(api, ev)
    if ev.name ~= PRICES_ASSET then return end
    if not ev.ok then
        api.log("no " .. PRICES_ASSET .. "; pricing from the cache's own OC_COST")
        return
    end
    local n
    prices, n = parse_prices(api.asset_data(PRICES_ASSET) or "")
    api.log(PRICES_ASSET .. ": " .. n .. " price overrides")
    -- The bytes are parsed; there is no reason to keep a copy of the file
    -- resident for the rest of the session.
    api.asset_release(PRICES_ASSET)
    dirty = true
end

-- Every edge that can change what should be lit. They only mark, because a
-- zone update can carry a dozen OBJ_ADDs and rebuilding on each would walk
-- the whole ground-item list a dozen times for one visible result.
function plugin.on_obj_spawn() dirty = true end

function plugin.on_obj_count() dirty = true end

function plugin.on_obj_despawn() dirty = true end

function plugin.on_config_changed(api, ev)
    -- A style or colour change has to reach the beams that are already up, and
    -- those are dressed only when something about them differs -- which is
    -- exactly what `dirty` makes the tick notice.
    dirty = true
end

function plugin.on_world_loaded(api, ev)
    -- The scene was rebuilt. Every stack the client tracked off the new scene
    -- has already been announced as despawned, so the beam set is rebuilt
    -- from scratch rather than trusted. The loaded models survive it -- they
    -- are geometry, and nothing about a scene rebuild changes their shape.
    clear(api)
    dirty = true
end

-- EV_LOGIC_TICK and not EV_SERVER_TICK: the tick fence (PKT_NAME_SERVER_TICK_END)
-- is only on the wire for osrs230, osrs239 and the rsprot bridge -- every
-- xp_tracker.c/loot_tracker.c tracker in this tree already made this switch for
-- the same reason. On lc245_2, lc254, lc289 and xrsps233 the fence never fires
-- at all, so `dirty` was set correctly by every obj event but `rebuild` -- the
-- ONLY place a beam is created or destroyed -- never ran: a beam that existed
-- never came down, and a despawned item's tile never got the memo. LOGIC_TICK
-- is the client's own 20ms cycle and exists on every lane; the `dirty` gate
-- below is what keeps this cheap when nothing changed.
function plugin.on_logic_tick(api, ev)
    if dirty then
        dirty = false
        rebuild(api)
    end
end

--
-- The rise, one yaw per beam.
--
-- Per FRAME rather than per tick because it is motion and a tick is 600ms, and
-- through object_position because turning a standing object is applied to the
-- live element -- no model is rebuilt for it, which is what makes an animation
-- this plugin owns affordable at all.
--
function plugin.on_frame(api, ev)
    local spin = api.config.spin
    local turn

    if spin == 0 then return end
    -- 2048 yaw units to a turn, `spin` degrees to a second.
    turn = (ev.now_ms * spin * 2048) // 360000

    for _, beam in pairs(beams) do
        if beam.x then
            api.object_position(
                beam.handle, beam.x, beam.z, beam.level, 0, (turn + beam.phase) % 2048)
        end
    end
end

return plugin
