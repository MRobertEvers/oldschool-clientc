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
-- WHAT THE PORCELAIN LAYER NOW DOES, THAT THIS FILE USED TO
--
-- This plugin had the widest lane surface of the four overlays and most of it
-- was bookkeeping around a refusal it then dropped:
--
--   * the SUPPRESS-THEN-FORMAT handoff. Forty lines of three-state
--     choreography -- a tree watch hiding every `ground_item_labels` widget as
--     the lane rebuilt them, the watch dropped and every widget reset on the
--     first caption callback, a boolean for which state it was in -- is
--     porcelain.native_overlay, written once. Four of this plugin's eight
--     recorded defects lived in that choreography.
--
--   * it does not read `widgets.find_all(...) or {}` any more. find_all
--     answers BUDGET_EXCEEDED when there are more label widgets than the
--     collection holds, and `or {}` read that partial answer as "there are no
--     captions" and hid nothing. The latch counts and raises a BUDGET finding.
--
--   * it does not hand-roll `obj_id * 4 + hide*2 + enabled`. porcelain.menu_tag
--     is the one encoding every plugin shares and porcelain.menu_untag is its
--     inverse, so the constant lives in the library and not in two plugins.
--
--   * it does not drop the bool from menu.add. The host's route table is 24
--     rows shared by every plugin in the build; over it `add` answers false and
--     the row never appears. This plugin at least stopped adding, but nobody --
--     plugin or player -- learned that a row was missing. The refusal is a
--     finding now, naming the label.
--
--   * it does not carry its own copy of the host's 192-byte config value
--     ceiling and its own join. That number was the host's, copied into the
--     plugin; the store snprintf-truncated past it and the validator then
--     ACCEPTED the fragment, so a hide list cut mid-name
--     stored a different rule and read back as one. porcelain.config_list_add /
--     _remove measure before they join, refuse rather than truncate, and leave
--     the stored list byte-for-byte as it was.
--
--   * it does not spell the tier walk. porcelain.tier is the shared table, and
--     it carries the gate this file never had: the reference DISABLES a tier
--     whose threshold is at or below zero, where this plugin read a zero low
--     threshold as "everything is low value".
--
--   * it does not poll `input.key_held` four times a frame. key_held answers 0
--     both for "the modifier is up" and for "this lane has no keyboard frame",
--     so on 601 and on Android the reveal key and both menu rows were
--     unreachable and indistinguishable from a key nobody had pressed.
--     porcelain.key_edge answers ABSENT there, with one finding, and the
--     feature reports itself off. The latched state is a local, so the caption
--     callback costs no host read at all where it used to cost two PER ROW PER
--     TICK.
--
--   * it does not log its notifications where no player can see them.
--     porcelain.notify writes a game line and coalesces on (kind, obj), so a
--     stack of twelve bones reaching the ground is one announcement.
--
--   * it does not request, read and release prices.txt by hand.
--     porcelain.table does all three once and drops the bytes the moment the
--     parse returns.
--
-- TWO VERBS THIS PLUGIN DELIBERATELY DECLINES
--
--   porcelain.draw_context answers the pass's drawable rectangle and whether
--   that rectangle IS the canvas. Every primitive here is named in SCENE terms
--   -- api.draw.project resolves a tile centre against the live scene origin,
--   and draw.world_tile hands an absolute tile straight to api_draw_tile -- so
--   the pass region is not an input to any of them. Returning early on the
--   false it can answer would SUPPRESS labels that draw correctly today, which
--   is a behaviour change dressed up as a check. tile-indicator and
--   entity-highlighter decline it for the same reason.
--
--   porcelain.note_menu / porcelain.hover stamp the hovered container CELL:
--   note_menu only looks at rows whose pick_kind is PORCELAIN_MENU_PICK_INV_SLOT
--   (2), and hover() answers an obj, a slot and which panel it came from. A
--   ground stack is pick_kind 6 and has no container at all, so note_menu would
--   stamp nothing on this plugin's rows and hover() would answer about a
--   DIFFERENT subject -- an inventory cell under the same pointer. Calling
--   either would be an engine call a frame for an answer this file must not
--   read.
--
-- ONE GAP THIS PLUGIN CANNOT SEE, SO IT DECLARES IT
--
--   Past 512 primitives a frame the host drops the rest of the overlay. The
--   line is TORIRS_ERR now, so a developer sees it -- but draw.text answers
--   NOTHING AT ALL, and a label here is two primitives (the shadow and the
--   ink) or five (the four-way outline and the ink). Twenty stacks in view
--   with text_outline on is a hundred primitives before a single tile wash, so
--   the truncation is invisible to the plugin, which goes on reporting itself
--   complete. The tile wash is the other half and it is one primitive per
--   occupied tile; whether api_draw_tile answers its budget refusal is the
--   core's to say, and the TEXT path does not answer at all. Reading
--   world_tile's (ok, result) pair here would pin whatever that path currently
--   returns while LOOKING like a check that bites over the labels, which are
--   the part that overruns, so it is not read: the gap is declared with
--   porcelain.expect_unsupported, which puts it on the same channel as every
--   real refusal. When draw.text answers, the declaration comes out and a real
--   truncation becomes an unexpected finding on the same line.
--
-- WHAT DOES NOT PORT FROM THE REFERENCE, AND WHY
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
--   chooses among the modifiers porcelain.key_edge does carry.
--
--   doubleTapDelay, collapseEntries, sortByGEPrice. The first needs a
--   double-tap this client cannot see; the other two describe a list this
--   plugin does not keep -- api.world.item_next() already yields one snapshot
--   per (tile, obj) pair with its own count, so a stack arrives collapsed.
--
-- Scene origin is read directly from the live world. Enabling while moving
-- does not need an earlier world-load event or guessed player coordinates.
--
-- There is no describe and no owned control: everything this plugin puts on
-- screen is either a scene primitive in the world pass or a field it writes
-- into the LANE'S OWN caption widgets. The steady state is therefore one
-- fence, one commit that stages nothing, and no engine setter and no
-- revalidate at all.
--

---@type torirs.Plugin
local plugin = {
    id      = "ground-items",
    title   = "Ground Items",
    version = "2.0.0",
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

        -- The four thresholds porcelain.tiers_from_config reads BY NAME. The
        -- spellings are the verb's, not this plugin's, which is why loot_beam
        -- spells them the same way without either plugin reading the other's.
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
        -- carry, so the modifier is a choice among the ones it does. The three
        -- spellings here are exactly the ones porcelain.key_edge understands,
        -- so a typo is refused by the schema instead of reading back as ABSENT.
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
-- truncated. Kept as a rational so the arithmetic stays in integers, and
-- applied per UNIT before the stack multiply -- which is where the reference
-- truncates too: GroundItem holds both prices per unit and its getGePrice /
-- getHaPrice multiply by the quantity on the way out.
local HA_NUM, HA_DEN = 3, 5

-- Coins, and the one item for which that per-unit truncation is a disaster.
--
-- A coin's cache cost is 1; floor(1 * 0.6) is 0; a pile of five thousand of
-- them is therefore worth nothing at all under the shipped `alch` value_mode,
-- and prints "(HA: 0 gp)" -- which label_for then suppresses, because it only
-- prints a price that is non-zero. The most common drop in the game was the one
-- this plugin could not put a number on.
--
-- GroundItemsPlugin.buildGroundItem ends with exactly this correction, under
-- the comment "Update item price in case it is coins":
--
--     if (realItemId == COINS) { groundItem.setHaPrice(1); groundItem.setGePrice(1); }
--
-- so a coin is worth one gp under BOTH prices and a pile is worth its count. It
-- is written AFTER the price lookup it overrides, which is why prices.txt gets
-- no say here either: the rule corrects whatever the lookup answered rather
-- than standing in for a lookup that answered nothing. ItemID.COINS_995 is 995
-- in every revision this client boots.
local COINS = 995
local SHADOW = 0x000000

local PRICES_ASSET = "prices.txt"

-- enum UIMinimenuPickKind. A row that targets a ground item carries the obj id
-- in target_id (see app_plugin_menu_build); nothing else identifies one, and
-- Porcelain names only the inventory-cell kind, so this literal stays here.
local PICK_OBJ = 6

-- The four operations a row of this plugin's carries, inside the sixteen
-- porcelain.menu_tag reserves per subject. Their VALUES are the encoding and
-- menu_untag has no vocabulary, so these four names are the whole decoder. The
-- low bit is the direction and the high bit is which list, which is the shape
-- the hand-rolled `obj_id * 4 + hide*2 + enabled` had.
local OP_HIGHLIGHT_OFF, OP_HIGHLIGHT_ON = 0, 1
local OP_HIDE_OFF, OP_HIDE_ON = 2, 3

-- Every character `string.find` treats as magic EXCEPT `*`, which is the one
-- wildcard the reference's list syntax has (WildcardMatchLoader).
local PATTERN_MAGIC = "([%^%$%(%)%%%.%[%]%+%-%?])"

-- lower-cased item name -> price, from the asset. Empty until it lands, and
-- empty forever if it is not shipped; the cache cost is the fallback either
-- way.
--
-- By NAME, not by obj id, for the reason prices.txt now states at length: an
-- id is a fact about one cache, and the table shipped keyed to cache.osrs239's
-- while being read on every cache this client boots. It cost loot-beam every
-- beam it could have raised on the dat1 lane -- this plugin got away with it
-- only because its captions do not depend on an override landing. The two
-- tables answer the same question and are kept in the same shape.
local prices = {}
local prices_count = 0
local prices_settled = false
-- Whether porcelain.open answered. Without the layer there is no refusal
-- channel, no tier table, no price table, no native handoff and no key edge;
-- the drawn labels outrank all of that and still draw.
local layer = false
-- The overlay latch has handed the natives back and the cache's own captions
-- now carry this plugin's fields.
local native_captions = false
-- What the reveal key said the last time the native captions were built. The
-- native rows re-run their caption script only when a row changes, so a key
-- pressed between rows would otherwise reveal nothing until the next despawn;
-- the change asks for the coalesced rebuild instead.
local reveal_shown = false
-- Whether this lane can answer the reveal key at all, and its latched state.
-- The state is a local because the caption callback asks for it once per row
-- per tick, where a host read is two engine calls for something the edge
-- already knows.
local reveal_armed, reveal_down = false, false

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

-- "Rune scimitar, *(g), Dragon *" -> the trimmed, non-empty entries.
local function split(csv)
    local out = {}
    -- A generic-for control variable is const in 5.5; trim into a local.
    for raw in string.gmatch(csv, "[^,]+") do
        local entry = trim(raw)
        if entry ~= "" then out[#out + 1] = entry end
    end
    return out
end

-- One anchored, case-folded pattern per entry. Every magic character is
-- escaped, so a name with a `(` in it matches literally; `*` alone is the
-- wildcard.
local function compile_entries(entries)
    local out = {}
    for _, entry in ipairs(entries) do
        local escaped = string.gsub(string.lower(entry), PATTERN_MAGIC, "%%%1")
        out[#out + 1] = "^" .. string.gsub(escaped, "%*", ".*") .. "$"
    end
    return out
end

local function compile_list(csv)
    return compile_entries(split(csv))
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
    for _, entry in ipairs(split(csv)) do out[string.lower(entry)] = true end
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
--
-- Each suffix runs to TEN THOUSAND of its own unit and not to a thousand of
-- them: the reference's ladder is `< 10_000` plain, `< 10_000_000` in K,
-- `< 10_000_000_000` in M, and B after that. The K rung was right here and the
-- M one was not -- it rolled to B at 1e9, a thousand times early -- so a max
-- cash stack printed "2B" where the reference prints "2147M", and every number
-- between 1e9 and 1e10 lost three digits of resolution to a unit the reference
-- does not use until ten times further up.
local function stack_size(n)
    if n < 0 then return "0" end
    if n < 10000 then return tostring(n) end
    if n < 10000000 then return (n // 1000) .. "K" end
    if n < 10000000000 then return (n // 1000000) .. "M" end
    return (n // 1000000000) .. "B"
end

-- The two prices the reference reasons about, for the whole stack.
local function prices_of(obj)
    local name = obj.name
    local unit = (name and prices[string.lower(name)]) or obj.cost
    if obj.obj_id == COINS then return obj.count, obj.count end
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

-- The four thresholds, read fresh once per draw pass and once per caption row
-- -- never once per stack, which is what the hand-rolled walk cost. The verb
-- reads THIS plugin's own four keys; there is no cross-plugin config read.
local function tiers_of(api)
    if not layer then return nil end
    return api.porcelain.tiers_from_config()
end

-- Reference GroundItemsPlugin.priceChecks, as porcelain.tier: walked from the
-- top, the comparison STRICTLY greater -- an item worth exactly the low
-- threshold is not a low-value item -- and a threshold at or below zero
-- disables its tier instead of matching everything, which is the gate this
-- file never had.
local function tier_of(api, tiers, price)
    if not tiers then return 0 end
    return api.porcelain.tier(tiers, price)
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
local function highlighted_colour(api, tiers, name, price)
    if is_highlighted(name) then return api.config.highlighted_color end
    -- An explicit hide beats an implicit, value-earned highlight.
    if is_hidden(name) then return nil end
    local tier = tier_of(api, tiers, price)
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
-- is not the default -- twenty stacks in view is a hundred items, and the
-- overrun is exactly the refusal declared at on_start as unreadable.
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

-- Parse `name = price` lines. Anything else -- blank lines, `#` comments, a
-- line we cannot read -- is skipped rather than failing the file: a price
-- table is a convenience, and one bad row must not cost the plugin the other
-- ten thousand. Never a refusal, so the verb never records one for the shape
-- of a row; a file that is absent or unreadable is the verb's own finding.
--
-- LINE at a time, and the comment cut before the row is read: a name has
-- spaces in it, and the old whole-file gmatch for two numbers around an equals
-- also matched a commented-out row and any pair of numbers that fell either
-- side of one in a sentence of prose.
local function on_prices(text)
    prices, prices_count = {}, 0
    for line in string.gmatch(text, "[^\r\n]+") do
        local body = string.match(line, "^([^#]*)")
        local name, price = string.match(body, "^%s*(.-)%s*=%s*(%d+)%s*$")
        if name and name ~= "" then
            prices[string.lower(name)] = tonumber(price)
            prices_count = prices_count + 1
        end
    end
    return true
end

--------------------------------------------------------- the native captions --
--
-- The lane's own ground-item caption script (osrs239 id 7232: eleven ints and
-- one string, writable mask 68 = int 2, int 6 and string 0) is hooked
-- synchronously so the native captions carry this plugin's price fields,
-- colour and row offset. Input slots are immutable; only the caption, the
-- colour and the row offset are outputs.
--
-- porcelain.native_overlay owns the three-state latch around it. SUPPRESSING:
-- from on_start, every `ground_item_labels` widget is hidden as the lane
-- rebuilds them, so the natives do not double-label. FORMATTING: on the FIRST
-- callback the hidden widgets are handed back to their own visibility and this
-- file starts writing the fields below. ABSENT: a lane with no CS2 never
-- raises the callback, which is one finding rather than a latch that hides
-- captions it will never replace.
--
-- The Lua half of native_overlay hands its callback only the script NAME, so
-- the event -- the ref and the widget -- is stashed by on_script_callback
-- around the note_script that drives the latch.
local caption_api, caption_event = nil, nil

local function format_caption(api, event)
    local ref = event.ref
    local ints, strings = api.scripts.counts(ref)
    assert(ints == 11 and strings == 1, "native caption hook contract mismatch")
    if not native_captions then
        native_captions = true
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
    -- The native row measures this result before it positions its buttons and
    -- timers, so the resolve has to happen HERE and not at the next fence.
    -- porcelain.set moves a key in this plugin's own applied description, and
    -- there is no description here: these are the LANE'S widgets, handed over
    -- one per callback, so the one-revalidate-per-frame rule cannot reach them.
    if event.widget then assert(event.widget:set_text_outline(api.config.text_outline)) end
    local parent = event.widget and event.widget:parent()
    if parent then
        local box = assert(parent:position())
        assert(parent:set_size(box.width, rows * api.config.line_gap))
        assert(parent:set_projection_height(api.config.height))
        assert(parent:revalidate())
    end
    local info = api.game.item_info(id)
    if not info or count < 1 then return true end
    local obj = {obj_id=id, name=info.name, count=count, cost=info.cost}
    local exchange, alch = prices_of(obj)
    local tiers = tiers_of(api)
    local high = native_high and api.config.highlighted_color or
        highlighted_colour(api, tiers, obj.name, value_by_mode(api, exchange, alch))
    local hide = hidden_colour(api, obj.name, exchange, alch)
    if native_ignore and not native_high then high=nil;hide=api.config.hidden_color end
    local me = api.world.local_player()
    local range = api.config.max_distance
    -- reveal_down is the LATCHED edge, not a host poll: this expression runs
    -- once per caption row per tick and used to cost a config read and a
    -- key_held on every one of them.
    local visible = me and ((coord >> 28) & 3) == me.level and
        math.abs(((coord >> 14) & 16383) - me.true_x) <= range and
        math.abs((coord & 16383) - me.true_z) <= range and
        (edit or reveal_down or high or (not hide and not api.config.show_highlighted_only))
    assert(api.scripts.set_string(ref, 0, visible and label_for(api, obj, exchange, alch) or ""))
    assert(api.scripts.set_int(ref, 6, high or hide or api.config.default_color))
    return true
end

-- The latch's own callback. The Lua half of native_overlay hands it only the
-- script NAME, so the event it needs is the one on_script_callback stashed --
-- and there is no other way for this to be called, so a missing one is a
-- contract violation and not a runtime state. False here would be one REFUSED
-- finding naming the caption, which is where a caption this plugin genuinely
-- could not honour belongs.
local function on_caption()
    assert(caption_event, "the caption callback ran outside on_script_callback")
    return format_caption(caption_api, caption_event)
end

function plugin.on_start(api)
    prices, prices_count = {}, 0
    prices_settled = false
    native_captions = false
    reveal_shown = false
    reveal_armed, reveal_down = false, false
    layer = false
    load_lists(api)

    if not api.porcelain.open() then
        -- Out loud: without the layer there is no refusal channel at all, and
        -- five of this plugin's features are the layer's. The labels outrank
        -- all of it and still draw.
        api.core.log("ground-items: no porcelain layer -- prices, value tiers, " ..
            "native captions, the reveal key and the menu rows are off")
        return
    end
    layer = true

    -- Both declarations come BEFORE the calls that can record them.
    -- expect_absent re-labels an absence already in the table; expect_unsupported
    -- does not, so an UNSUPPORTED raised before its declaration stays
    -- unexpected for the life of the session.
    api.porcelain.expect_unsupported("native captions",
        "a CS1 lane runs no CS2, so the cache's own ground-item caption script does not exist")
    -- Trimmed to PORCELAIN_DETAIL_MAX. The primitive arithmetic behind this --
    -- a label is two, or five with the outline, against a 512 budget -- is in
    -- the comment above the draw itself, where it belongs.
    api.porcelain.expect_unsupported("draw_refusal_readout",
        "draw.text answers nothing, so a truncating draw budget is invisible")
    api.porcelain.expect_absent("role:reveal_key", "a touch lane has no keyboard frame")

    reveal_armed = api.porcelain.key_edge("reveal_key", function(down)
        reveal_down = down
    end)

    -- Native CS2 keeps its buttons, timers and live state; only the captions
    -- are this plugin's to write. On a lane with no CS2 the feature turns
    -- itself off and says so once, rather than suppressing labels it will
    -- never replace.
    if api.porcelain.require("cs2_scripts", "native captions") then
        api.porcelain.native_overlay("ground_item_labels", "groundItemCaption", on_caption)
    end

    -- Optional: a client without the file simply prices everything from the
    -- cache's own OC_COST. The verb requests, reads, parses and RELEASES in
    -- one call, and one finding names an absent or unreadable file.
    prices_settled = api.porcelain.table(PRICES_ASSET, on_prices)
    if prices_settled then
        api.core.log(PRICES_ASSET .. ": " .. prices_count .. " price overrides")
    end
end

-- The edge arrives here, not at a fence poll: a poll cannot see a press that
-- opens and closes inside one frame. Nothing is consumed -- the reveal key is
-- a modifier the client is still entitled to.
function plugin.on_key(api, event)
    -- reveal_armed is false both where there is no layer and where this lane
    -- answered ABSENT for the key, which is exactly when there is nothing to
    -- forward it to.
    if not reveal_armed then return end
    api.porcelain.note_key(event.key, event.down)
end

function plugin.on_config_changed(api, key)
    if key == "highlighted_items" or key == "hidden_items" or
        key == "highlight_exceptions" or key == "hide_exceptions" then
        load_lists(api)
    end
    if native_captions then api.scripts.invalidate("groundItemCaption") end
end

function plugin.on_stop(api)
    -- One last coalesced rebuild so the cache's own captions come back
    -- unformatted. Closing the handle -- which the host does after this
    -- callback -- is what hands the suppressed label widgets back to their own
    -- native visibility.
    if native_captions then api.scripts.invalidate("groundItemCaption") end
    native_captions = false
    reveal_shown = false
    reveal_armed, reveal_down = false, false
    layer = false
end

-- The reveal key is live on the native caption lane too: RuneLite re-renders
-- its overlay every frame, so holding the key shows the hidden stacks at
-- once. The native rows only re-run the caption script when a row changes,
-- so a key transition asks for one coalesced rebuild. An event context, not
-- the paint pass: paint may not schedule native work.
function plugin.on_frame_start(api)
    if not layer then return end
    -- No fence and no commit here: open() installs the pump, and this handler
    -- runs BETWEEN the two. Spelling them again would fence an epoch that is
    -- already fenced, which the layer records as a finding.
    --
    -- The verb has no state read-out and the fence does not retry it, so the
    -- retry is here. A settled table -- parsed, missing or unreadable -- makes
    -- no engine call at all; only a web lane still fetching costs one.
    if not prices_settled then
        prices_settled = api.porcelain.table(PRICES_ASSET, on_prices)
        if prices_settled then
            api.core.log(PRICES_ASSET .. ": " .. prices_count .. " price overrides")
        end
    end
    if not native_captions then return end
    if reveal_down == reveal_shown then return end
    reveal_shown = reveal_down
    api.scripts.invalidate("groundItemCaption")
end

function plugin.on_script_callback(api, event)
    if event.name ~= "groundItemCaption" then return end
    -- The latch hands the natives back on the FIRST callback and then routes
    -- to on_caption, which reads the event stashed here.
    caption_api, caption_event = api, event
    api.porcelain.note_script()
    caption_api, caption_event = nil, nil
end

--
-- Reference GroundItemsPlugin.notifyHighlightedItem. porcelain.notify writes a
-- game line -- where a player can see it, unlike the log line this used to be
-- -- and coalesces on (kind, obj) so a stack of twelve bones reaching the
-- ground is one announcement and not twelve.
--
function plugin.on_item_spawn(api, obj)
    if not layer then return end
    local exchange, alch = prices_of(obj)

    if api.config.notify_highlighted and is_highlighted(obj.name) then
        api.porcelain.notify("highlight", obj.obj_id,
            "highlighted drop: " .. label_for(api, obj, exchange, alch))
        return
    end

    local floor = tier_rank(api.config.notify_tier)
    if floor > 0 and tier_of(api, tiers_of(api), value_by_mode(api, exchange, alch)) >= floor then
        api.porcelain.notify("tier", obj.obj_id,
            "drop: " .. label_for(api, obj, exchange, alch))
    end
end

------------------------------------------------------------------ the pass --
--
-- The per-tile tables are reused across frames. A fresh table per tile and per
-- line every frame is Lua GC pressure in the hot pass for a shape that never
-- changes; the pool only ever grows to the busiest frame the session saw, and
-- a tile whose line count SHRINKS drops its surplus rows so table.sort sees
-- exactly the lines this frame put there and not one left over from the last.
local tile_pool = {}
local tile_at = {}
local function by_value(a, b) return a.value > b.value end

function plugin.on_draw_world(api, draw)
    local me = api.world.local_player()
    if not me then return end
    local base_x, base_z = api.world.scene_origin()
    if not base_x then return end

    local range = api.config.max_distance
    local tiers = tiers_of(api)
    local tile_count = 0
    for key in pairs(tile_at) do tile_at[key] = nil end

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
                local high = highlighted_colour(api, tiers, obj.name, price)
                local hide = hidden_colour(api, obj.name, exchange, alch)
                local show = true

                -- Reference GroundItemsOverlay.render: the hotkey shows
                -- everything, including what is hidden and what the
                -- highlighted-only filter would drop.
                if not high and not reveal_down then
                    show = not hide and not api.config.show_highlighted_only
                end

                if show then
                    local key = obj.tile_x .. ":" .. obj.tile_z
                    local index = tile_at[key]
                    if not index then
                        tile_count = tile_count + 1
                        index = tile_count
                        tile_at[key] = index
                        if not tile_pool[index] then tile_pool[index] = { items = {} } end
                        local fresh = tile_pool[index]
                        fresh.x, fresh.z, fresh.count = obj.tile_x, obj.tile_z, 0
                    end
                    local tile = tile_pool[index]
                    tile.count = tile.count + 1
                    local item = tile.items[tile.count]
                    if not item then item = {}; tile.items[tile.count] = item end
                    item.text = label_for(api, obj, exchange, alch)
                    item.colour = high or hide or api.config.default_color
                    item.value = price
                end
            end
        end
    end

    local gap = api.config.line_gap
    local height = api.config.height
    local fill_tiles = api.config.highlight_tiles
    local tile_fill = api.config.tile_fill
    local outline = api.config.text_outline

    for index = 1, tile_count do
        local tile = tile_pool[index]
        for at = #tile.items, tile.count + 1, -1 do tile.items[at] = nil end
        -- The reference stacks a tile's items in collection order, which for
        -- an entity pool is arrival order and reshuffles as stacks come and
        -- go. Sorted by value instead: the most valuable thing on the tile is
        -- always the line nearest the ground, and the column does not jump
        -- about when an unrelated item under it despawns.
        table.sort(tile.items, by_value)
    end

    -- TWO passes, and the order between them is the whole point.
    --
    -- The wash is a translucent quad over the tile and a caption is drawn at
    -- the tile's projected centre, so the two overlap by construction: a
    -- stack's own label sits in the middle of its own tile. Interleaved --
    -- caption then wash, per tile -- every tile painted its own label out,
    -- and with `highlight_tiles` on the middle of every caption was a pink
    -- smear ("Abyssal tentacle (EX: 90M gp)" with the price unreadable).
    -- Worse, the interleaving made it depend on iteration order: a stack
    -- whose neighbour came later in the pool lost its text to the NEIGHBOUR's
    -- wash as well.
    --
    -- Washes first, then every caption: the tile is a highlight and a
    -- highlight goes UNDER the thing it highlights. @see the reference, which
    -- renders the tile polygon in its own overlay pass ahead of the labels.
    if fill_tiles then
        for index = 1, tile_count do
            local tile = tile_pool[index]
            -- Reference: the tile takes the colour of the item on it.
            draw.world_tile(tile.x, tile.z, me.level,
              tile.items[1].colour, tile.items[1].colour, tile_fill)
        end
    end

    if native_captions then return end
    for index = 1, tile_count do
        local tile = tile_pool[index]
        local sx, sy = api.draw.project(
            (tile.x - base_x) * 128 + 64, (tile.z - base_z) * 128 + 64, height)
        if sx then
            for i, item in ipairs(tile.items) do
                text_at(draw, sx, sy - gap * (i - 1), item.text, item.colour, outline)
            end
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

local function name_of_obj(api, obj_id)
    for obj in items(api) do
        if obj.obj_id == obj_id and obj.name ~= "" then return obj.name end
    end
    return nil
end

-- The objtype answers in O(1) and answers after the stack has despawned, which
-- is the call the select path already preferred; walking every stack in the
-- scene per menu row was O(rows x stacks) for the same string.
local function name_of(api, obj_id)
    local info = api.game.item_info(obj_id)
    if info and info.name and info.name ~= "" then return info.name end
    return name_of_obj(api, obj_id)
end

function plugin.on_menu_build(api, menu)
    -- The hover pass rebuilds the menu every frame just to compose the line
    -- under the cursor. Rows added there would never be seen.
    if menu.hover_pass then return end
    -- reveal_armed is false where the key edge answered ABSENT -- a touch lane
    -- has no keyboard frame, and "off" is a real choice -- and the layer has
    -- already raised the finding that says so.
    if not reveal_armed or not reveal_down then return end

    local seen = {}
    for _, row in ipairs(menu.rows) do
        local id = row.target_id
        if row.pick_kind == PICK_OBJ and id >= 0 and not seen[id] then
            seen[id] = true
            local name = name_of(api, id)
            if name then
                local on_high, on_hide = is_highlighted(name), is_hidden(name)
                -- Subject AND intended operation are frozen into the tag at
                -- build time: the stack may despawn while the menu is open,
                -- and re-resolving either at select time is what made a
                -- retained row invert itself.
                --
                -- The bool is not dropped and it is not merely read: a refused
                -- route is a finding naming the label, raised by the verb, and
                -- the loop still stops rather than add rows nobody will see.
                if not api.porcelain.menu_add(
                        (on_high and "Unhighlight" or "Highlight") .. " @yel@" .. name,
                        api.porcelain.menu_tag(id,
                            on_high and OP_HIGHLIGHT_OFF or OP_HIGHLIGHT_ON)) then break end
                if not api.porcelain.menu_add(
                        (on_hide and "Unhide" or "Hide") .. " @yel@" .. name,
                        api.porcelain.menu_tag(id,
                            on_hide and OP_HIDE_OFF or OP_HIDE_ON)) then break end
            end
        end
    end
end

function plugin.on_menu_select(api, sel)
    if not sel.owned then return end
    local id, op = api.porcelain.menu_untag(sel.tag)
    local hide = op >= OP_HIDE_OFF
    local enabled = op % 2 == 1
    -- Item definitions remain queryable after the clicked stack despawns.
    local name = name_of(api, id)
    if not name or name == "" then return "consume" end

    local key = hide and "hidden_items" or "highlighted_items"
    local exceptions = hide and "hide_exceptions" or "highlight_exceptions"

    -- What the list looks like with the exact name taken out, and whether a
    -- broad rule the user wrote -- `Rune *` -- still covers this item without
    -- it. Compiled ONCE, where the old body compiled the joined string twice.
    local lower = string.lower(name)
    local rest = {}
    for _, entry in ipairs(split(api.config[key])) do
        if string.lower(entry) ~= lower then rest[#rest + 1] = entry end
    end
    local covered = matches(compile_entries(rest), name)

    -- Exactly one of the two writes can GROW a list, and it goes first, so a
    -- refusal past the store's value ceiling leaves BOTH lists exactly as they
    -- were. The verb measures before it joins and never truncates; a list cut
    -- mid-name stores a different rule and reads back as one.
    if enabled then
        local stored
        if covered then
            -- The wildcard that still matches carries it: the exact entry
            -- comes out rather than sit there as a duplicate rule.
            stored = api.porcelain.config_list_remove(key, name)
        else
            stored = api.porcelain.config_list_add(key, name)
        end
        if not stored then return "consume" end
        -- Turning it on clears the exception that turned it off. A removal
        -- always shrinks, so it has no ceiling to refuse.
        api.porcelain.config_list_remove(exceptions, name)
    else
        local excepted
        if covered then
            -- A broad rule the user wrote still names this item, so turning it
            -- off is an exception and never a deletion of that rule.
            excepted = api.porcelain.config_list_add(exceptions, name)
        else
            excepted = api.porcelain.config_list_remove(exceptions, name)
        end
        if not excepted then return "consume" end
        api.porcelain.config_list_remove(key, name)
    end
    return "consume"
end

return plugin
