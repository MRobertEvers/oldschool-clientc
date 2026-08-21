--
-- Loot Beam
--
-- A beam of light over every ground item worth more than a threshold, after
-- RuneLite's Ground Items plugin (net.runelite.client.plugins.grounditems.
-- Lootbeam). Both of its styles are here and they are not cosmetic variants of
-- each other -- MODERN is model 43330 with two recoloured bands and the
-- FX_BEAM_IDLE sequence, LIGHT is the older model 5809 with one band and
-- ENAKH_LIGHT_STREAMING -- so both ids and both sequences are config, not
-- constants. This client runs a dozen cache revisions and only some of them
-- have either model.
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

---@type torirs.Plugin
local plugin           = {
    name    = "loot-beam",
    version = "1.0.0",
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

        -- Cache ids. Defaults are OSRS's; a revision without them draws
        -- nothing and says so once, rather than silently.
        {
            key = "modern_model",
            type = "int",
            default = "43330",
            min = 0,
            max = 200000,
            label = "Modern beam model"
        },
        {
            key = "modern_seq",
            type = "int",
            default = "9260",
            min = -1,
            max = 200000,
            label = "Modern beam seq"
        },
        {
            key = "light_model",
            type = "int",
            default = "5809",
            min = 0,
            max = 200000,
            label = "Light beam model"
        },
        {
            key = "light_seq",
            type = "int",
            default = "3101",
            min = -1,
            max = 200000,
            label = "Light beam seq"
        },
    },
}

-- The face colours each style's model paints its bands with. Reference
-- Lootbeam.Style: LIGHT recolours the single colour 6371, MODERN the pair
-- 26432 (body) and 26584 (core, one notch brighter).
local FACE_LIGHT       = 6371
local FACE_MODERN_BODY = 26432
local FACE_MODERN_CORE = 26584

local LUMINANCE_MAX    = 127

local PRICES_ASSET     = "prices.txt"

-- tile key -> { handle, rgb, style }. Keyed on the ABSOLUTE tile, which is
-- what survives a scene rebuild -- the same reason api.object_position takes
-- one.
local beams            = {}
-- obj_id -> price, from the asset. Empty until it lands, and empty forever if
-- it is not shipped; the cache cost is the fallback either way.
local prices           = {}
-- Set by every edge that can change what should be lit; drained on the server
-- tick, so a packet burst that adds ten stacks rebuilds once and not ten
-- times.
local dirty            = true
-- One complaint per model id, not one per tick.
local warned_model     = nil
-- Ticks a beam has existed without its model landing. A load is asynchronous,
-- so "not ready" is the normal state for a tick or two; only a run of them
-- means the id is not in this cache.
local unready_ticks    = 0

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

local function value_of(obj)
    local unit = prices[obj.obj_id] or obj.cost
    return unit * obj.count
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

-- Point a beam at a colour and a style. Everything here is applied to the
-- object's INTENT; the host rebuilds the model behind it and the beam appears
-- when the cache assets land.
local function dress(api, beam, rgb, style)
    local handle = beam.handle
    local hsl = api.hsl(rgb)

    api.object_clear_recolors(handle)
    if style == "light" then
        api.object_model(handle, api.config.light_model)
        api.object_recolor(handle, FACE_LIGHT, hsl)
        api.object_anim(handle, api.config.light_seq, true)
        api.object_light(handle, 0, 0)
    else
        -- Reference Lootbeam.Style.MODERN: the body loses a notch of
        -- saturation and the core gains 24 luminance, which is what gives the
        -- beam a bright centre instead of a flat coloured tube. The saturation
        -- step is skipped on an already-dull colour (sat <= 2) because taking
        -- one off would push it to grey.
        local h, s, l = api.hsl_unpack(hsl)
        local sat_step = s > 2 and 1 or 0
        api.object_model(handle, api.config.modern_model)
        api.object_recolor(handle, FACE_MODERN_BODY, api.hsl_pack(h, s - sat_step, l))
        api.object_recolor(
            handle, FACE_MODERN_CORE, api.hsl_pack(h, s, math.min(l + 24, LUMINANCE_MAX)))
        api.object_anim(handle, api.config.modern_seq, true)
        -- The reference lights this model well above the default; without it
        -- the recoloured bands read as dark plastic rather than as light.
        api.object_light(handle, 75, 1875)
    end
    beam.rgb, beam.style = rgb, style
end

local function rebuild(api)
    local style = api.config.style
    local want = {}

    for obj in api.objs() do
        local value = value_of(obj)
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
            api.object_position(beam.handle, w.x, w.z, w.level)
            api.object_active(beam.handle, true)
        end
    end
end

local function clear(api)
    for key, beam in pairs(beams) do
        api.object_destroy(beam.handle)
        beams[key] = nil
    end
end

function plugin.on_start(api)
    beams, prices, dirty, warned_model, unready_ticks = {}, {}, true, nil, 0
    -- Optional: a client without the file simply prices everything from the
    -- cache. on_asset hears about it either way.
    api.asset_load(PRICES_ASSET)
end

function plugin.on_stop(api)
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
    -- from scratch rather than trusted.
    clear(api)
    dirty = true
end

function plugin.on_server_tick(api, ev)
    if dirty then
        dirty = false
        rebuild(api)
    end

    -- One line when a configured model does not exist in this cache. Without
    -- it, "no beams appear" is indistinguishable between a threshold nobody
    -- meets and a model id that belongs to another revision. Counted over
    -- several ticks because a model load is asynchronous: not-ready-yet is the
    -- ordinary state right after a beam is created.
    local model = api.config.style == "light" and api.config.light_model
        or api.config.modern_model
    local waiting = false
    for _, beam in pairs(beams) do
        if not api.object_ready(beam.handle) then
            waiting = true
            break
        end
    end
    if not waiting then
        unready_ticks = 0
        return
    end
    unready_ticks = unready_ticks + 1
    if unready_ticks > 5 and warned_model ~= model then
        warned_model = model
        api.log("beam model " .. model .. " has not loaded after "
            .. unready_ticks .. " ticks; is it in this cache?")
    end
end

return plugin
