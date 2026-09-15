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
-- the decoder reads the model format off the file's own trailer rather than
-- off the revision.
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
-- WHAT PORCELAIN IS HERE FOR
--
-- Not the picture: the beams stand on the same tiles, in the same colours, at
-- the same yaw, spun by the same arithmetic. Five pieces of this file's own
-- bookkeeping moved into the layer, and four of them were recorded defects.
--
--   * the CADENCE -- porcelain.every("logic_tick"). `rebuild` is the only
--     place a beam is created or destroyed, and it used to hang off the server
--     tick, which lc245_2, lc254, lc289 and xrsps233 never sent: `dirty` was
--     set by every obj event and nothing drained it, so a beam that existed
--     never came down. This file answered that by naming the four lanes in a
--     comment. The layer states the rule once; the four names survive in this
--     paragraph as the record of the defect, not as a branch anything reads.
--
--   * the TIER TEST -- porcelain.tier. Two rules, both the reference's, and
--     this file had neither: strictly greater at every threshold (an item
--     worth exactly the low threshold is not a low-value item), and a
--     threshold at or below zero turns its tier OFF rather than matching
--     everything. This plugin used >= and ground-items used >, over nine rows
--     they share spelling-for-spelling. porcelain.tiers_from_config reads
--     THIS plugin's own four keys: there is no cross-plugin config read.
--
--   * the MODEL -- porcelain.model. assets.model answers (handle, state) and a
--     missing file answers (nil, "missing"); the old memo stored that nil, so
--     "never asked" and "asked, and it will never arrive" were one value and
--     the plugin re-asked on every dress with no line printed anywhere. The
--     verb remembers a terminal state and raises exactly one finding for it.
--
--   * the PRICE TABLE -- porcelain.table. One request, one parse, the bytes
--     released, one finding if the file is absent or unreadable: what
--     request/bytes/release did here by hand, minus the resident copy nobody
--     freed on the failure path.
--
--   * the REFUSALS -- porcelain.finding. scene.instance_* answers (ok,result)
--     and every result went on the floor. instance_create is the one that
--     mattered: past the host's 64-object budget it answers "budget", and the
--     old code dropped the tile and retried it next rebuild in pairs() order,
--     so WHICH tile was the missing 65th moved between rebuilds and the floor
--     flickered. The want set is walked sorted now, so the clipped set is the
--     same set twice, and the refusal reaches the plugin's own finding channel
--     instead of only a host log line nobody reads.
--
-- There is no describe and no fence: this plugin owns no widget and states no
-- element, so nothing here is retained for the layer to reconcile, and its
-- steady state is one forwarded tick that returns on a boolean.
--
-- It does not ask for porcelain.draw_context. That verb answers the pass's
-- drawable rectangle and whether that rectangle IS the canvas; a beam is not
-- painted in a pass at all -- it is an object handed to the painter in WORLD
-- coordinates and projected by the renderer -- so there is no rectangle here
-- to derive, clamp or intersect. Its two sibling world overlays,
-- tile-indicator and entity-highlighter, decline it for the same reason.
--
-- Without the layer there are no beams. Every rule above lives in it now, and
-- re-implementing five of them here for a host that has no Porcelain would be
-- keeping four recorded defects alive to serve a configuration that does not
-- ship. The plugin says so in one line instead.
--

---@type torirs.Plugin
local plugin           = {
    id      = "loot-beam",
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

        -- The four porcelain.tiers_from_config reads, by the names it reads
        -- them under. They are the CALLER's own keys: ground-items declares
        -- the same four spellings in its own section and neither plugin can
        -- see the other's.
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
local STYLES           = {
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

local LUMINANCE_MAX    = 127

local function hsl_unpack(hsl)
    return (hsl >> 10) & 63, (hsl >> 7) & 7, hsl & 127
end

local function hsl_pack(h, s, l)
    return ((h & 63) << 10) | ((s & 7) << 7) | (l & 127)
end

local PRICES_ASSET     = "prices.txt"

-- tile key -> { handle, rgb, style, x, z, level, phase }. Keyed on the
-- ABSOLUTE tile, which is what survives a scene rebuild -- the same reason
-- api.scene.instance_position takes one.
local beams            = {}
-- style name -> { handle }. A style whose file answered a TERMINAL state gets
-- an entry whose handle is nil, which is what makes "never asked" and "asked,
-- and it will never arrive" two different states rather than one nil.
local models           = {}
-- obj_id -> price, from the asset. Empty until it lands, and empty forever if
-- it is not shipped; the cache cost is the fallback either way.
local prices           = {}
-- Whether porcelain.open answered. Nothing here runs without it: see header.
local layer            = false
-- The four thresholds, read from this plugin's own config once per change
-- rather than four times per ground stack.
local tiers            = nil
-- One finding per refused scene verb, not one per refused call: a beam that
-- cannot be positioned cannot be positioned every frame, and the layer's
-- coalescing would still cost a call across the boundary each time.
local refused          = {}
-- Set by every edge that can change what should be lit; drained on the logic
-- tick, so a packet burst that adds ten stacks rebuilds once and not ten
-- times.
local dirty            = true
-- Beams standing after the last rebuild, so the count is only reported when it
-- moves.
local live             = 0
-- The parse handed to porcelain.table, made once against the api it logs
-- through: the verb calls it back with the bytes and nothing else.
local prices_parse     = nil

local function items(api)
    local cursor = -1
    return function()
        local next_cursor, item = api.world.item_next(cursor)
        if not next_cursor then return nil end
        cursor = next_cursor
        return item
    end
end

-- Every scene verb answers (ok, result), and every one of those results used
-- to go on the floor. This is the one place they are read: a refusal is named
-- once on the layer's own channel, so a beam that is not where it should be is
-- readable instead of being a picture nobody can explain.
local function applied(api, verb, ok, why)
    if ok then return true end
    if not refused[verb] then
        refused[verb] = true
        api.porcelain.finding("scene", nil, why == "budget" and "budget" or "refused",
            verb .. ": " .. tostring(why))
    end
    return false
end

-- The model for a style, asked for on first use.
--
-- PENDING is deliberately not memoised as a failure and deliberately not
-- waited for: the host hands back a live handle the moment the read is queued
-- and rebuilds the model behind it when the file lands, so the beam is dressed
-- now and fills in. Only MISSING and ERROR are terminal, and porcelain.model
-- is what remembers them -- one finding naming the file, and never asked again.
local function model_for(api, style)
    local slot = models[style]
    local name, handle, state

    if slot then return slot.handle end
    name = STYLES[style].asset
    -- The verb answers (nil, state) by design: a model handle has no Lua
    -- representation, so the STATE is the whole of what it is asked for.
    handle, state = api.porcelain.model(name)
    if state == "missing" or state == "error" then
        models[style] = { handle = nil }
        return nil
    end
    handle = api.assets.model(name)
    if not handle then
        -- The layer says the asset is live and the host would not hand back a
        -- handle for it. That is a refusal, not an absence, and it is the one
        -- shape porcelain.model cannot report on this plugin's behalf.
        api.porcelain.finding("model", "role:" .. name, "refused", "no handle for a live asset")
        models[style] = { handle = nil }
        return nil
    end
    models[style] = { handle = handle }
    return handle
end

local function tier_rank(name)
    if name == "low" then return 1 end
    if name == "medium" then return 2 end
    if name == "high" then return 3 end
    if name == "insane" then return 4 end
    return 0
end

-- porcelain.tier answers 0..4, and these are the colour rows it indexes.
local TIER_COLOUR      = { "low_color", "medium_color", "high_color", "insane_color" }

-- The colour a value earns, or nil when it earns none. The tier itself is the
-- layer's: strictly greater at every threshold, and a threshold at or below
-- zero turns its tier off rather than matching everything. The floor is this
-- plugin's, and gates the answer so "beam from high" does not light the
-- low-value drops underneath it.
local function tier_colour(api, value)
    local floor = tier_rank(api.config.tier)
    local rank

    if floor == 0 then return nil end
    rank = api.porcelain.tier(tiers, value)
    if rank < floor then return nil end
    return api.config[TIER_COLOUR[rank]]
end

-- Reference ItemComposition.getHaPrice: price * HIGH_ALCHEMY_MULTIPLIER
-- (0.6f), truncated. Kept as a rational so the arithmetic stays in integers,
-- and applied per UNIT before the stack multiply -- which is where the
-- reference truncates too: GroundItem holds the two prices per unit and its
-- getGePrice/getHaPrice multiply by the quantity on the way out, so a stack of
-- n is n * floor(cost * 0.6) and never floor(n * cost * 0.6).
local HA_NUM, HA_DEN   = 3, 5

-- Coins.
--
-- The per-unit truncation above is the reference's, and for every ordinary
-- item it is right. For ONE item it is catastrophic, and the reference knows
-- it: a coin's cache cost is 1, floor(1 * 0.6) is 0, and a pile of five
-- thousand of them is therefore worth nothing at all -- which, with `alch` as
-- the shipped default value_mode, is a loot beam that can never fire over a
-- pile of gold. The archetypal drop was the one drop this plugin could not
-- see.
--
-- GroundItemsPlugin.buildGroundItem ends with exactly this correction, under
-- the comment "Update item price in case it is coins":
--
--     if (realItemId == COINS) { groundItem.setHaPrice(1); groundItem.setGePrice(1); }
--
-- so a coin is worth one gp under BOTH prices and a pile is worth its count.
-- It is written after the price lookup it overrides, which is why prices.txt
-- does not get a say here either: the rule is not a fallback, it is a
-- correction of whatever the lookup answered. ItemID.COINS_995 is 995 in every
-- revision this client boots.
local COINS            = 995

-- What this stack is worth under the configured mode. `alch` is the default;
-- see the header.
local function value_of(api, obj)
    local unit = prices[obj.obj_id] or obj.cost
    local exchange = unit * obj.count
    local alch = (unit * HA_NUM // HA_DEN) * obj.count
    local mode = api.config.value_mode

    if obj.obj_id == COINS then
        exchange, alch = obj.count, obj.count
    end
    if mode == "value" then return exchange end
    if mode == "highest" then return exchange > alch and exchange or alch end
    return alch
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
    local hsl = api.draw.hsl_from_rgb(rgb)
    local h, s, l = hsl_unpack(hsl)
    local sat_step = s > 2 and 1 or 0

    if not model then return end

    api.scene.instance_clear_recolors(handle)
    if not applied(api, "instance_model", api.scene.instance_model(handle, model)) then return end
    if not applied(api, "instance_recolor",
            api.scene.instance_recolor(handle, shape.body, hsl_pack(h, s - sat_step, l))) then
        return
    end
    if shape.core and not applied(api, "instance_recolor",
            api.scene.instance_recolor(handle, shape.core,
                hsl_pack(h, s, math.min(l + 24, LUMINANCE_MAX)))) then
        return
    end
    -- The reference lights this model well above the default; without it the
    -- recoloured bands read as dark plastic rather than as light.
    if not applied(api, "instance_light", api.scene.instance_light(handle, 75, 1875)) then return end
    beam.rgb, beam.style = rgb, style
end

--
-- The beams that should be standing, from the floor as it is now.
--
-- Filtered by PLANE, which this plugin did not do and ground_items.lua always
-- has (`obj.level == me.level`). The client tracks every ground stack in the
-- LOADED SCENE, not only the ones on the player's own storey, so a drop in the
-- room above was a beam this plugin created, positioned, spun once a frame and
-- counted.
--
-- What it was not was a beam anyone saw: world_cycle.c gates every plugin
-- object on `obj->level != local_level`, so an off-plane object is dropped
-- before the painter. That is what made this the kind of defect that survives
-- -- there was no wrong pixel to notice. What there was:
--
--   * the COUNT LINE, which is the only diagnostic this plugin prints and the
--     only thing separating "nothing on the floor clears the threshold" from
--     "beams exist and are not being drawn". It was reporting beams nobody
--     could see, which is the second answer given for the first question.
--   * the OBJECT BUDGET. The host refuses instance_create past 64 objects per
--     plugin, so every pile on another storey held a slot a visible beam
--     wanted. In a multi-storey building that clips the beams the player CAN
--     see in favour of beams that cannot be drawn at all.
--
-- The tally is filtered with the beams, for the same reason: "0 beam(s) over 20
-- ground stack(s)" reads as a threshold set too high when all twenty of those
-- stacks are upstairs.
--
-- No local player is no floor to filter against -- the lane before login, and
-- between a logout and the next world -- and nothing is lit: the beams come
-- down and the next obj event rebuilds them.
--
local function rebuild(api)
    local style = api.config.style
    local me = api.world.local_player()
    local want = {}
    local order = {}
    local tally = 0
    local before = live

    for obj in items(api) do
        if me and obj.level == me.level then
            local value = value_of(api, obj)
            local rgb = tier_colour(api, value)
            tally = tally + 1
            if rgb then
                -- One beam per TILE, coloured by the best thing on it: a tile
                -- with a rune scimitar and a bone under it is one beam, not two
                -- in the same place fighting over the same pixels.
                local key = obj.level .. ":" .. obj.tile_x .. ":" .. obj.tile_z
                local best = want[key]
                if not best then order[#order + 1] = key end
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
    end

    for key, beam in pairs(beams) do
        if not want[key] then
            api.scene.instance_destroy(beam.handle)
            beams[key] = nil
        end
    end

    -- SORTED, and that sort is the whole of the object-budget fix. The host
    -- refuses instance_create past 64 objects per plugin; walked in pairs()
    -- order the tile that lost the draw changed between rebuilds and the floor
    -- flickered between two arbitrary subsets of the same 65. A total order
    -- over the tile keys makes the clipped set the same set every time.
    table.sort(order)
    for _, key in ipairs(order) do
        local w = want[key]
        local beam = beams[key]
        if not beam then
            local handle, why = api.scene.instance_create()
            if applied(api, "instance_create", handle ~= nil, why) then
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
            applied(api, "instance_position", api.scene.instance_position(
                beam.handle, w.x, w.z, w.level, 0, beam.phase))
            api.scene.instance_active(beam.handle, true)
        end
    end

    -- Only when the count moves. "No beams appear" is the report this plugin
    -- will get, and it has two very different causes -- nothing on the floor
    -- clears the threshold, or beams exist and are not being drawn. One line
    -- separates them; a line per tick would bury both.
    live = 0
    for _ in pairs(beams) do live = live + 1 end
    if live ~= before then
        api.core.log(live .. " beam(s) over " .. tally .. " ground stack(s)")
    end
end

local function clear(api)
    for key, beam in pairs(beams) do
        api.scene.instance_destroy(beam.handle)
        beams[key] = nil
    end
    live = 0
end

-- Parse `obj_id=price` lines. Anything else -- blank lines, `#` comments, a
-- line we cannot read -- is skipped rather than failing the file: a price
-- table is a convenience, and one bad row must not cost the plugin the other
-- ten thousand. Accepting the bytes is what makes porcelain.table release them
-- and never ask again; the parsed table is all that stays resident.
local function make_prices_parse(api)
    return function(text)
        local out, n = {}, 0
        for id, price in string.gmatch(text, "(%d+)%s*=%s*(%d+)") do
            out[tonumber(id)] = tonumber(price)
            n = n + 1
        end
        prices = out
        api.core.log(PRICES_ASSET .. ": " .. n .. " price overrides")
        -- A price is a value and a value is a tier: the beams standing now
        -- were coloured from the cache's own numbers.
        dirty = true
        return true
    end
end

function plugin.on_start(api)
    beams, models, prices, refused = {}, {}, {}, {}
    dirty, live, tiers = true, 0, nil
    layer = api.porcelain.open()
    if not layer then
        -- Out loud, once. The cadence, the tier rule, the terminal asset
        -- states, the price table and every scene refusal are the layer's now;
        -- a beam raised without them would be four recorded defects wearing a
        -- ported plugin's version number.
        api.core.log("loot-beam: no porcelain layer -- no beams")
        return
    end
    -- The one thing the ledger asks for that the layer cannot do yet, declared
    -- rather than left as a sentence in a commit message: it is one expected
    -- finding in every capture, and it comes out when the verb lands.
    api.porcelain.expect_unsupported("shared_price_table",
        "table slots are per plugin handle: ground-items parses prices.txt again")
    tiers = api.porcelain.tiers_from_config()
    if not tiers then
        api.porcelain.finding("tiers_from_config", nil, "refused",
            "none of the four value rows answered; every tier is off")
        tiers = { low = 0, medium = 0, high = 0, insane = 0 }
    end
    prices_parse = make_prices_parse(api)
    -- Optional: a client without the file prices everything from the cache.
    -- The verb arms the read and answers false until the bytes are there;
    -- on_asset is the edge that asks it again.
    api.porcelain.table(PRICES_ASSET, prices_parse)
    -- The only cadence in this file, and it names no lane. rebuild is the one
    -- place a beam is created or destroyed, and it runs at most once per
    -- client logic tick, when something has actually moved.
    api.porcelain.every("logic_tick", function()
        if not dirty then return end
        dirty = false
        rebuild(api)
    end)
end

function plugin.on_stop(api)
    -- The model files go with the plugin; the host releases them when it stops
    -- one, for the same reason it takes its objects out of the world. The
    -- porcelain handle is the host's to close, after this returns.
    clear(api)
end

function plugin.on_asset(api, ev)
    if not layer or ev.name ~= PRICES_ASSET then return end
    -- Whatever landed, the verb is what reads the state: it parses a file that
    -- arrived, raises one finding for one that did not, and asks neither
    -- question twice.
    if api.porcelain.table(PRICES_ASSET, prices_parse) then return end
    if not ev.ok then
        api.core.log("no " .. PRICES_ASSET .. "; pricing from the cache's own OC_COST")
    end
end

-- Every edge that can change what should be lit. They only mark, because a
-- zone update can carry a dozen OBJ_ADDs and rebuilding on each would walk
-- the whole ground-item list a dozen times for one visible result.
function plugin.on_item_spawn() dirty = true end

function plugin.on_item_changed() dirty = true end

function plugin.on_item_despawn() dirty = true end

function plugin.on_config_changed(api, key)
    -- A style or colour change has to reach the beams that are already up, and
    -- those are dressed only when something about them differs -- which is
    -- exactly what `dirty` makes the tick notice.
    dirty = true
    -- A threshold is read once per config change and not once per ground
    -- stack, so this is where the four rows are re-read.
    if layer then tiers = api.porcelain.tiers_from_config() or tiers end
end

function plugin.on_world_loaded(api, ev)
    -- The scene was rebuilt. Every stack the client tracked off the new scene
    -- has already been announced as despawned, so the beam set is rebuilt
    -- from scratch rather than trusted. The loaded models survive it -- they
    -- are geometry, and nothing about a scene rebuild changes their shape.
    clear(api)
    -- A refusal against the old scene says nothing about the new one, and a
    -- latch held across a world load would silence the first real one.
    refused = {}
    dirty = true
end

-- The tick the layer registered for, forwarded. FRAME is the only cadence the
-- layer drives itself; a plugin hands it the other two, because a library
-- cannot install a callback into a definition the host already registered.
function plugin.on_logic_tick(api, ev)
    if not layer then return end
    api.porcelain.tick("logic_tick")
end

--
-- The rise, one yaw per beam.
--
-- Per FRAME rather than per tick because it is motion and a tick is 600ms, and
-- through instance_position because turning a standing object is applied to
-- the live element -- no model is rebuilt for it, which is what makes an
-- animation this plugin owns affordable at all.
--
function plugin.on_frame_start(api, ev)
    local spin = api.config.spin
    local turn

    if not layer or spin == 0 then return end
    -- 2048 yaw units to a turn, `spin` degrees to a second.
    turn = (ev.now_ms * spin * 2048) // 360000

    for _, beam in pairs(beams) do
        if beam.x then
            applied(api, "instance_position", api.scene.instance_position(
                beam.handle, beam.x, beam.z, beam.level, 0, (turn + beam.phase) % 2048))
        end
    end
end

return plugin
