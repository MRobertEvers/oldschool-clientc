--
-- Ground Items
--
-- Names every ground-item stack in the scene, over the tile it lies on,
-- coloured by what it is worth. A port of RuneLite's Ground Items plugin
-- (net.runelite.client.plugins.grounditems.GroundItemsPlugin / -Overlay /
-- -Config): the same value tiers, the same highlight and hide lists, the same
-- string composition, the same 15px stacking gap and 20-unit lift off the
-- ground.
--
-- The loot beam that reference plugin also owns is NOT here -- it is
-- loot_beam.lua, because a beam is a world object and everything below is an
-- overlay, and the two have nothing in common but a threshold. Their tier
-- config is deliberately spelled the same way so one can be read off the
-- other.
--
-- WHAT DOES NOT PORT, AND WHY
--
--   Grand Exchange prices. There is no feed and there never will be. What the
--   client knows is ObjType.cost -- the same number CS2 reads through OC_COST
--   -- so `cost` stands in for the reference's gePrice, and prices.txt
--   overrides it per id the way loot_beam.lua's does. The high-alchemy price
--   is derived from it exactly as the reference derives its own:
--   floor(cost * 0.6).
--
--   Menu recolouring, menu quantities, deprioritising hidden items. The menu
--   api appends rows; it cannot restyle or reorder the ones the client built.
--   What IS here instead is a pair of rows -- Highlight and Hide -- on the
--   right-click menu of any ground item while the reveal key is held, which is
--   the same job the reference's ALT-held click boxes do, done the way this
--   client already does per-entity toggles (see entity_highlighter.lua).
--
--   Despawn timers. They are computed in the reference from the drop rules,
--   and the client is told none of them: OBJ_ADD carries an id, a count and a
--   tile, and nothing about who dropped it or when it goes. A timer here would
--   be a guess drawn as a fact.
--
--   Ownership filters and "do not hide untradeables". Both need a flag that
--   does not reach the client -- ownership is not on the wire, and the resident
--   objtype carries no tradeable bit. Left out rather than approximated,
--   because a filter that silently means something else is worse than no
--   filter.
--
--   The ALT hotkey. enum LibToriRS_KeyCode carries no alt, so `reveal_key`
--   chooses among the modifiers it does carry. Everything the reference does
--   behind ALT is behind that key instead.
--
--   doubleTapDelay, collapseEntries, sortByGEPrice. The first needs a
--   double-tap this client cannot see; the other two describe a list this
--   plugin does not keep -- api.world.item_next() already yields one snapshot per
--   (tile, obj) pair with its own count, so a stack arrives collapsed.
--
-- Scene origin is read directly from the live world. Enabling while moving
-- does not need an earlier world-load event or guessed player coordinates.
--

---@type torirs.Plugin
local plugin = {
    id      = "ground-items",
    title   = "Ground Items",
    version = "1.0.0",
    config  = {
        -- Reference GroundItemsConfig, key for key. The hidden list default is
        -- the reference's own, which is why coins and bones are invisible out
        -- of the box: hold the reveal key to see them.
        -- The two LISTS are multiline boxes, not one-line fields. That is the
        -- reference's own shape: interface 650's ground-items page gives
        -- "Highlighted items" and "Filtered items" a box several lines tall
        -- each, because a comma-separated run of item names shows about a word
        -- and a half of itself in a one-line field.
        {
            key = "highlighted_items",
            type = "text",
            rows = 5,
            default = "",
            label = "Highlighted items"
        },
        {
            key = "hidden_items",
            type = "text",
            rows = 5,
            default = "Vial, Ashes, Coins, Bones, Bucket, Jug, Seaweed",
            label = "Hidden items"
        },
        { key = "highlight_exceptions", type = "text", rows = 3, default = "",
          label = "Highlight exceptions (exact names)" },
        { key = "hide_exceptions", type = "text", rows = 3, default = "",
          label = "Hide exceptions (exact names)" },
        {
            key = "show_highlighted_only",
            type = "bool",
            default = "0",
            label = "Show highlighted only"
        },
        {
            key = "hide_under_value",
            type = "int",
            default = "0",
            min = 0,
            max = 2000000000,
            label = "Hide under value"
        },
        {
            key = "price_mode",
            type = "enum",
            default = "both",
            choices = "off|value|alch|both",
            label = "Price display"
        },
        -- Which of the two prices every threshold in this plugin is measured
        -- against. The reference defaults this to HIGHEST; the default here is
        -- the high-alchemy price, because the exchange price a client without
        -- a price feed can offer is ObjType.cost, and cost is a shop number
        -- rather than what a drop is worth. The alch price is derived from the
        -- same field but is the one the game itself will actually pay for the
        -- item, so it is the honest default -- and `highest` and `value` are
        -- still here for a client that ships a prices.txt worth trusting.
        {
            key = "value_mode",
            type = "enum",
            default = "alch",
            choices = "alch|value|highest",
            label = "Value calculation"
        },
        {
            key = "highlight_tiles",
            type = "bool",
            default = "0",
            label = "Highlight tiles"
        },
        {
            key = "tile_fill",
            type = "int",
            default = "50",
            min = 0,
            max = 255,
            label = "Tile fill"
        },

        { key = "default_color",     type = "color", default = "#FFFFFF", label = "Default colour" },
        { key = "highlighted_color", type = "color", default = "#AA00FF", label = "Highlighted colour" },
        { key = "hidden_color",      type = "color", default = "#808080", label = "Hidden colour" },

        {
            key = "low_value",
            type = "int",
            default = "20000",
            min = 0,
            max = 2000000000,
            label = "Low value"
        },
        { key = "low_color", type = "color", default = "#66B2FF", label = "Low colour" },
        {
            key = "medium_value",
            type = "int",
            default = "100000",
            min = 0,
            max = 2000000000,
            label = "Medium value"
        },
        { key = "medium_color", type = "color", default = "#99FF99", label = "Medium colour" },
        {
            key = "high_value",
            type = "int",
            default = "1000000",
            min = 0,
            max = 2000000000,
            label = "High value"
        },
        { key = "high_color", type = "color", default = "#FF9600", label = "High colour" },
        {
            key = "insane_value",
            type = "int",
            default = "10000000",
            min = 0,
            max = 2000000000,
            label = "Insane value"
        },
        { key = "insane_color", type = "color", default = "#FF66B2", label = "Insane colour" },

        {
            key = "notify_highlighted",
            type = "bool",
            default = "0",
            label = "Notify highlighted drops"
        },
        {
            key = "notify_tier",
            type = "enum",
            default = "off",
            choices = "off|low|medium|high|insane",
            label = "Notify from tier"
        },

        -- The reference's hotkey is ALT, which enum LibToriRS_KeyCode does not
        -- carry, so the modifier is a choice among the ones it does.
        {
            key = "reveal_key",
            type = "enum",
            default = "shift",
            choices = "off|shift|ctrl",
            label = "Reveal / menu key"
        },

        -- The reference holds these four fixed. They are settings here because
        -- three of them are measured in something this client does not share
        -- with it -- a scene font of a different height (line_gap, in pixels),
        -- a projection that takes its lift in 1/128ths of a tile (height), and
        -- a draw distance the reference states in local units (max_distance,
        -- its MAX_DISTANCE 2500, which is 19 whole tiles).
        {
            key = "max_distance",
            type = "int",
            default = "19",
            min = 1,
            max = 104,
            label = "Max distance (tiles)"
        },
        {
            key = "line_gap",
            type = "int",
            default = "15",
            min = 6,
            max = 40,
            label = "Stacked line gap"
        },
        {
            key = "height",
            type = "int",
            default = "20",
            min = 0,
            max = 512,
            label = "Height above ground"
        },
        {
            key = "text_outline",
            type = "bool",
            default = "0",
            label = "Text outline (else shadow)"
        },
    },
}

-- The config defaults above carry the reference's own numbers: MAX_DISTANCE
-- 2500 local units is 19 whole tiles, OFFSET_Z is `height`, STRING_GAP is
-- `line_gap`, and OverlayUtil.renderPolygon's wash is `tile_fill`.
--
-- The stack size at which the reference stops counting.
local MAX_QUANTITY = 65535
-- Reference ItemComposition.getHaPrice: price * HIGH_ALCHEMY_MULTIPLIER (0.6f),
-- truncated. Kept as a rational so the arithmetic stays in integers.
local HA_NUM, HA_DEN = 3, 5
local SHADOW = 0x000000

local PRICES_ASSET = "prices.txt"

-- enum UIMinimenuPickKind. A row that targets a ground item carries the obj id
-- in target_id (see app_plugin_menu_build); nothing else identifies one.
local PICK_OBJ = 6

-- Every character `string.find` treats as magic EXCEPT `*`, which is the one
-- wildcard the reference's list syntax has (WildcardMatchLoader).
local PATTERN_MAGIC = "([%^%$%(%)%%%.%[%]%+%-%?])"

-- obj_id -> price, from the asset. Empty until it lands, and empty forever if
-- it is not shipped; the cache cost is the fallback either way.
local prices = {}
local native_captions = false
-- What the reveal key said the last time the native captions were checked.
-- The native rows re-run their caption script only when a row changes, so a
-- key pressed between rows would otherwise reveal nothing until the next
-- despawn; the change asks for the coalesced rebuild instead.
local reveal_shown = false

local function items(api)
    local cursor = -1
    return function()
        local next_cursor, item = api.world.item_next(cursor)
        if not next_cursor then return nil end
        cursor = next_cursor
        return item
    end
end
-- The two name lists, compiled to anchored Lua patterns once per edit rather
-- than once per item per frame.
local highlight_pats = {}
local hidden_pats = {}
local highlight_exceptions, hide_exceptions = {}, {}
local function trim(s)
    return (string.gsub(s, "^%s*(.-)%s*$", "%1"))
end

-- "Rune scimitar, *(g), Dragon *" -> one anchored, case-folded pattern each.
local function compile_list(csv)
    local out = {}
    -- A generic-for control variable is const in 5.5; trim into a local.
    for raw in string.gmatch(csv, "[^,]+") do
        local entry = trim(raw)
        if entry ~= "" then
            local escaped = string.gsub(string.lower(entry), PATTERN_MAGIC, "%%%1")
            out[#out + 1] = "^" .. string.gsub(escaped, "%*", ".*") .. "$"
        end
    end
    return out
end

local function matches(pats, name)
    local lower = string.lower(name)
    for _, pat in ipairs(pats) do
        if string.find(lower, pat) then return true end
    end
    return false
end

local function exact_list(csv)
    local out = {}
    for raw in string.gmatch(csv, "[^,]+") do out[string.lower(trim(raw))] = true end
    return out
end
local function is_highlighted(name)
    return matches(highlight_pats, name) and not highlight_exceptions[string.lower(name)]
end
local function is_hidden(name)
    return matches(hidden_pats, name) and not hide_exceptions[string.lower(name)]
end

local function load_lists(api)
    highlight_pats = compile_list(api.config.highlighted_items)
    hidden_pats = compile_list(api.config.hidden_items)
    highlight_exceptions = exact_list(api.config.highlight_exceptions)
    hide_exceptions = exact_list(api.config.hide_exceptions)
end

-- Reference QuantityFormatter.quantityToStackSize.
local function stack_size(n)
    if n < 0 then return "0" end
    if n < 10000 then return tostring(n) end
    if n < 10000000 then return (n // 1000) .. "K" end
    if n < 1000000000 then return (n // 1000000) .. "M" end
    return (n // 1000000000) .. "B"
end

-- The two prices the reference reasons about, for the whole stack.
local function prices_of(obj)
    local unit = prices[obj.obj_id] or obj.cost
    return unit * obj.count, (unit * HA_NUM // HA_DEN) * obj.count
end

-- Reference ValueCalculationMode. HIGHEST is the reference's default; `alch`
-- is this plugin's, for the reason the config row states.
local function value_by_mode(api, exchange, alch)
    local mode = api.config.value_mode
    if mode == "value" then return exchange end
    if mode == "highest" then return exchange > alch and exchange or alch end
    return alch
end

local TIER_KEY = { "low_color", "medium_color", "high_color", "insane_color" }

-- Reference GroundItemsPlugin.priceChecks: walked from the top, and the
-- comparison is STRICTLY greater -- an item worth exactly the low threshold is
-- not a low-value item.
local function tier_of(api, price)
    if price > api.config.insane_value then return 4 end
    if price > api.config.high_value then return 3 end
    if price > api.config.medium_value then return 2 end
    if price > api.config.low_value then return 1 end
    return 0
end

local function tier_rank(name)
    if name == "low" then return 1 end
    if name == "medium" then return 2 end
    if name == "high" then return 3 end
    if name == "insane" then return 4 end
    return 0
end

-- Reference GroundItemsPlugin.getHighlighted. nil means "earns no highlight",
-- which is not the same as "is hidden" -- the caller needs both answers.
local function highlighted_colour(api, name, price)
    if is_highlighted(name) then return api.config.highlighted_color end
    -- An explicit hide beats an implicit, value-earned highlight.
    if is_hidden(name) then return nil end
    local tier = tier_of(api, price)
    if tier > 0 then return api.config[TIER_KEY[tier]] end
    return nil
end

-- Reference GroundItemsPlugin.getHidden, less its untradeable clause (see the
-- header). An explicit highlight beats an implicit, value-earned hide.
local function hidden_colour(api, name, exchange, alch)
    if is_hidden(name) then return api.config.hidden_color end
    local under = api.config.hide_under_value
    if under > 0 and exchange < under and alch < under
        and not is_highlighted(name) then
        return api.config.hidden_color
    end
    return nil
end

local function reveal_held(api)
    local key = api.config.reveal_key
    if key == "off" then return false end
    return api.input.key_held(key)
end

-- Reference GroundItemsOverlay's item string: name, then the count, then
-- whichever prices the mode asks for.
local function label_for(api, obj, exchange, alch)
    -- The name is snapshotted onto the stack when it spawns and can be empty
    -- when the objtype was not resident then; the id still identifies it.
    local s = obj.name ~= "" and obj.name or ("#" .. obj.obj_id)

    if obj.count > 1 then
        if obj.count >= MAX_QUANTITY then
            s = s .. " (Lots!)"
        else
            s = s .. " (" .. stack_size(obj.count) .. ")"
        end
    end

    local mode = api.config.price_mode
    if mode == "both" then
        if exchange > 0 then s = s .. " (EX: " .. stack_size(exchange) .. " gp)" end
        if alch > 0 then s = s .. " (HA: " .. stack_size(alch) .. " gp)" end
    elseif mode ~= "off" then
        local price = mode == "value" and exchange or alch
        if price > 0 then s = s .. " (" .. stack_size(price) .. " gp)" end
    end
    return s
end

-- Reference OverlayUtil.renderTextLocation: a shadow one pixel down-right, or
-- a four-way outline when the config asks for one. The outline costs five
-- draws a line against the host's 512-item per-frame budget, which is why it
-- is not the default -- twenty stacks in view is a hundred items.
local function text_at(draw, x, y, s, colour, outline)
    if outline then
        draw.text(x - 1, y, s, SHADOW)
        draw.text(x + 1, y, s, SHADOW)
        draw.text(x, y - 1, s, SHADOW)
        draw.text(x, y + 1, s, SHADOW)
    else
        draw.text(x + 1, y + 1, s, SHADOW)
    end
    draw.text(x, y, s, colour)
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

function plugin.on_start(api)
    prices = {}
    native_captions = false
    load_lists(api)
    -- Native CS2 keeps its buttons, timers and live state. Hide its captions
    -- while this plugin supplies labels; host cleanup restores
    -- their current native visibility on disable. Older clients return nil.
    assert(api.widgets.watch_tree(function()
        for _, widget in ipairs(api.widgets.find_all("ground_item_labels") or {}) do
            if widget then assert(widget:set_hidden(true)) end
        end
    end))
    -- Optional: a client without the file simply prices everything from the
    -- cache. on_asset hears about it either way.
    api.assets.request(PRICES_ASSET)
    api.scripts.invalidate("groundItemCaption")
end

function plugin.on_asset(api, ev)
    if ev.name ~= PRICES_ASSET then return end
    if not ev.ok then
        api.core.log("no " .. PRICES_ASSET .. "; pricing from the cache's own OC_COST")
        return
    end
    local n
    prices, n = parse_prices(api.assets.bytes(PRICES_ASSET) or "")
    api.core.log(PRICES_ASSET .. ": " .. n .. " price overrides")
    -- The bytes are parsed; there is no reason to keep a copy of the file
    -- resident for the rest of the session.
    api.assets.release(PRICES_ASSET)
    if native_captions then api.scripts.invalidate("groundItemCaption") end
end

function plugin.on_config_changed(api, key)
    if key == "highlighted_items" or key == "hidden_items" or
        key == "highlight_exceptions" or key == "hide_exceptions" then
        load_lists(api)
    end
    if native_captions then api.scripts.invalidate("groundItemCaption") end
end

function plugin.on_stop(api)
    if native_captions then api.scripts.invalidate("groundItemCaption") end
    native_captions = false
    reveal_shown = false
end

-- The reveal key is live on the native caption lane too: RuneLite re-renders
-- its overlay every frame, so holding the key shows the hidden stacks at
-- once. The native rows only re-run the caption script when a row changes,
-- so a key transition asks for one coalesced rebuild. An event context, not
-- the paint pass: paint may not schedule native work.
function plugin.on_frame_start(api)
    if not native_captions then return end
    local reveal = reveal_held(api)
    if reveal == reveal_shown then return end
    reveal_shown = reveal
    api.scripts.invalidate("groundItemCaption")
end

-- The native row will measure this result before positioning its buttons and
-- timers. Input slots are immutable; only the caption and color are outputs.
function plugin.on_script_callback(api, event)
    if event.name ~= "groundItemCaption" then return end
    local ref = event.ref
    local ints, strings = api.scripts.counts(ref)
    assert(ints == 11 and strings == 1, "native caption hook contract mismatch")
    if not native_captions then
        native_captions = true
        assert(api.widgets.watch_tree(nil))
        for _, widget in ipairs(api.widgets.find_all("ground_item_labels") or {}) do
            if widget then assert(widget:reset()) end
        end
        api.core.log("native caption formatting active")
    end
    local native_ignore = api.scripts.get_int(ref, 0) == 1
    local native_high = api.scripts.get_int(ref, 1) == 1
    local edit = api.scripts.get_int(ref, 5) == 1
    local count = api.scripts.get_int(ref, 8)
    local id = api.scripts.get_int(ref, 9)
    local coord = api.scripts.get_int(ref, 10)
    local rows = api.scripts.get_int(ref, 3)
    local row = api.scripts.get_int(ref, 4)
    assert(api.scripts.set_int(ref, 2, (rows - row - 1) * api.config.line_gap))
    if event.widget then assert(event.widget:set_text_outline(api.config.text_outline)) end
    local parent = event.widget and event.widget:parent()
    if parent then
        local box = assert(parent:position())
        assert(parent:set_size(box.width, rows * api.config.line_gap))
        assert(parent:set_projection_height(api.config.height))
        assert(parent:revalidate())
    end
    local info = api.game.item_info(id)
    if not info or count < 1 then return end
    local obj = {obj_id=id, name=info.name, count=count, cost=info.cost}
    local exchange, alch = prices_of(obj)
    local high = native_high and api.config.highlighted_color or
        highlighted_colour(api, obj.name, value_by_mode(api, exchange, alch))
    local hide = hidden_colour(api, obj.name, exchange, alch)
    if native_ignore and not native_high then high=nil;hide=api.config.hidden_color end
    local me = api.world.local_player()
    local range = api.config.max_distance
    local visible = me and ((coord >> 28) & 3) == me.level and
        math.abs(((coord >> 14) & 16383) - me.true_x) <= range and
        math.abs((coord & 16383) - me.true_z) <= range and
        (edit or reveal_held(api) or high or (not hide and not api.config.show_highlighted_only))
    assert(api.scripts.set_string(ref, 0, visible and label_for(api, obj, exchange, alch) or ""))
    assert(api.scripts.set_int(ref, 6, high or hide or api.config.default_color))
end

--
-- Reference GroundItemsPlugin.notifyHighlightedItem, as one log line: the api
-- has no notifier, and the log is what a plugin can say.
--
function plugin.on_item_spawn(api, obj)
    local exchange, alch = prices_of(obj)

    if api.config.notify_highlighted and is_highlighted(obj.name) then
        api.core.log("highlighted drop: " .. label_for(api, obj, exchange, alch))
        return
    end

    local floor = tier_rank(api.config.notify_tier)
    if floor > 0 and tier_of(api, value_by_mode(api, exchange, alch)) >= floor then
        api.core.log("drop: " .. label_for(api, obj, exchange, alch))
    end
end

function plugin.on_draw_world(api, draw)
    local me = api.world.local_player()
    if not me then return end
    local base_x, base_z = api.world.scene_origin()
    if not base_x then return end

    local reveal = reveal_held(api)
    local range = api.config.max_distance
    local tiles = {}
    local order = {}

    for obj in items(api) do
        -- Ground items are per-plane, and api.draw.project() samples ground height
        -- at the LOCAL PLAYER's level -- an upper-floor stack projected here
        -- would land on the ground below it.
        if obj.level == me.level then
            local dx = math.abs(obj.tile_x - me.true_x)
            local dz = math.abs(obj.tile_z - me.true_z)
            if (dx > dz and dx or dz) <= range then
                local exchange, alch = prices_of(obj)
                local price = value_by_mode(api, exchange, alch)
                local high = highlighted_colour(api, obj.name, price)
                local hide = hidden_colour(api, obj.name, exchange, alch)
                local show = true

                -- Reference GroundItemsOverlay.render: the hotkey shows
                -- everything, including what is hidden and what the
                -- highlighted-only filter would drop.
                if not high and not reveal then
                    show = not hide and not api.config.show_highlighted_only
                end

                if show then
                    local key = obj.tile_x .. ":" .. obj.tile_z
                    local tile = tiles[key]
                    if not tile then
                        tile = { x = obj.tile_x, z = obj.tile_z, items = {} }
                        tiles[key] = tile
                        order[#order + 1] = tile
                    end
                    local items = tile.items
                    items[#items + 1] = {
                        text = label_for(api, obj, exchange, alch),
                        colour = high or hide or api.config.default_color,
                        value = price,
                    }
                end
            end
        end
    end

    local gap = api.config.line_gap
    local height = api.config.height
    local fill_tiles = api.config.highlight_tiles
    local tile_fill = api.config.tile_fill
    local outline = api.config.text_outline

    for _, tile in ipairs(order) do
        -- The reference stacks a tile's items in collection order, which for
        -- an entity pool is arrival order and reshuffles as stacks come and
        -- go. Sorted by value instead: the most valuable thing on the tile is
        -- always the line nearest the ground, and the column does not jump
        -- about when an unrelated item under it despawns.
        table.sort(tile.items, function(a, b) return a.value > b.value end)

        local sx, sy = api.draw.project(
            (tile.x - base_x) * 128 + 64, (tile.z - base_z) * 128 + 64, height)
        if sx and not native_captions then
            for i, item in ipairs(tile.items) do
                text_at(draw, sx, sy - gap * (i - 1), item.text, item.colour, outline)
            end
        end

        if fill_tiles then
            -- Reference: the tile takes the colour of the item on it.
            draw.world_tile(tile.x, tile.z, me.level,
              tile.items[1].colour, tile.items[1].colour, tile_fill)
        end
    end
end

------------------------------------------------------------------- the menu --
--
-- The reference draws three click boxes beside each item's text while ALT is
-- held: the item, a hide toggle and a highlight toggle. The api has no mouse
-- position and cannot restyle a client row, so the same two toggles are
-- offered where this client already puts per-entity actions -- the right-click
-- menu, behind the same modifier.
--

-- Retain both the item type and the requested action while a menu is open.
local function tag_of(obj_id, hide, enabled)
    return obj_id * 4 + (hide and 2 or 0) + (enabled and 1 or 0)
end

local function name_of_obj(api, obj_id)
    for obj in items(api) do
        if obj.obj_id == obj_id and obj.name ~= "" then return obj.name end
    end
    return nil
end

function plugin.on_menu_build(api, menu)
    -- The hover pass rebuilds the menu every frame just to compose the line
    -- under the cursor. Rows added there would never be seen.
    if menu.hover_pass then return end
    if not reveal_held(api) then return end

    local seen = {}
    for _, row in ipairs(menu.rows) do
        local id = row.target_id
        if row.pick_kind == PICK_OBJ and id >= 0 and not seen[id] then
            seen[id] = true
            local name = name_of_obj(api, id)
            if name then
                local hl = is_highlighted(name) and "Unhighlight" or "Highlight"
                local hd = is_hidden(name) and "Unhide" or "Hide"
                if not api.menu.add(hl .. " @yel@" .. name, tag_of(id, false, not is_highlighted(name))) then break end
                if not api.menu.add(hd .. " @yel@" .. name, tag_of(id, true, not is_hidden(name))) then break end
            end
        end
    end
end

-- Preserve wildcard entries and unrelated names. An explicit exception lets
-- Unhide/Unhighlight affect one item without deleting a user's broad rule.
local CONFIG_VALUE_MAX = 192
local function list_set(csv, name, enabled)
    local entries = {}
    local lower = string.lower(name)
    for raw in string.gmatch(csv, "[^,]+") do
        local entry = trim(raw)
        if entry ~= "" and string.lower(entry) ~= lower then entries[#entries+1] = entry end
    end
    if enabled then entries[#entries+1] = name end
    local joined = table.concat(entries, ", ")
    if #joined >= CONFIG_VALUE_MAX then return nil end
    return joined
end

function plugin.on_menu_select(api, sel)
    if not sel.owned then return end
    local id = sel.tag // 4
    local hide, enabled = sel.tag % 4 >= 2, sel.tag % 2 == 1
    -- Item definitions remain queryable after the clicked stack despawns.
    local info = api.game.item_info(id)
    local name = info and info.name or name_of_obj(api, id)
    if not name or name == "" then return "consume" end
    local key = hide and "hidden_items" or "highlighted_items"
    local exceptions = hide and "hide_exceptions" or "highlight_exceptions"
    local value = list_set(api.config[key], name, false)
    if value and enabled and not matches(compile_list(value), name) then
        value = list_set(value, name, true)
    end
    local except = value and list_set(api.config[exceptions], name,
        not enabled and matches(compile_list(value), name))
    if not value or not except then
        api.core.log("Item preference list is full; " .. name .. " was not changed")
        return "consume"
    end
    api.config.set(key, value)
    api.config.set(exceptions, except)
    return "consume"
end

return plugin
