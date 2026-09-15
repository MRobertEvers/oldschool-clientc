--
-- Entity Highlighter
--
-- Draws a convex hull around tagged NPCs, and offers Tag/Untag on the
-- right-click menu while the reveal key is held.
--
-- Tags are keyed on base_npc_id -- the multinpc SHELL -- and never on npc_id.
-- A multinpc's drawn type changes whenever a varbit flips (a shopkeeper who
-- changes appearance, a boss that swaps phase models), and a tag keyed on the
-- drawn type would silently fall off the moment that happened. The shell id is
-- the one that survives, which is the same rule the server content follows
-- when it binds triggers.
--
-- What this file no longer does, because the Porcelain layer does it:
--
--   * it does not hand-roll the (subject, intent) encoding of a retained menu
--     row -- porcelain.menu_tag is the one encoding every plugin shares;
--   * it does not drop the bool from menu.add. The host's route table is 24
--     rows shared by every plugin in the build, and over it `add` answers
--     false; this plugin used to carry on adding rows that would never appear,
--     and nobody -- plugin or player -- learned that a row was missing;
--   * it does not join a tag list and hope. The host's config value ceiling is
--     192 bytes; a joined list past it was snprintf-truncated, the validator
--     accepted the fragment, and a list cut mid-id stored a WRONG species and
--     read back as one. porcelain.config_list_add measures before it joins;
--   * it does not read a reveal key the lane cannot answer. input.key_held can
--     never be true on 601/Android -- there is no keyboard frame -- so the Tag
--     rows were silently unreachable there. porcelain.key_edge answers ABSENT
--     with one finding, and the feature turns itself off instead.
--
-- That last one used to be only half true, and the missing half was the
-- ENGINE's: key_edge decides ABSENT from core.capability("touch"), which reads
-- App.touch_ui, and App.touch_ui was set inside frame_loop_step() -- AFTER
-- PluginHost_Start, so the capability read false at every on_start and the
-- edge registered even on a touch lane. That is fixed: touch_ui is resolved in
-- App_Init now, beside the clientscript identity it follows, and main.c says
-- so where the assignment used to be.
--
-- Re-measured on the stone601 capture, which is the only lane that logs in as
-- a phone: one finding at FIRST_FRAME=0, `verb=key_edge element=role(reveal_key)
-- detail=touch lane has no key expected=1`. The detail is the tell -- the
-- fence's own absence prints the KEY NAME ("shift", "off"), so a detail of
-- "touch lane has no key" can only have come from the branch inside the call,
-- which means the capability was already true when on_start asked. The rows
-- are off there and say so, which is what shots/plugins/ehreveal-stone601.png
-- and ehtag-stone601.png photograph.
--
-- AND THE EDGE HAS TO BE FORWARDED, WHICH THIS FILE DID NOT DO.
--
-- porcelain.note_key is what moves a key edge, and NOTHING in the host calls
-- it: the only caller in the tree was the layer's own unit test. A plugin has
-- to forward its own on_key. This one did not, so from the day it was ported
-- until now its reveal key could never go down, reveal_down was false for the
-- life of every session, and Tag/Untag were unreachable on EVERY lane -- not
-- just the touch lane the paragraphs above are about. The four lines in
-- plugin.on_key below are the whole fix; the behaviour test pins that the
-- forward happens, and there is a negative control that reddens when it is
-- taken away. Measured end to end with a probe under TORIRS_SIM_KEYHOLD=42:
-- on_key arrives, note_key moves the edge, the callback fires.
--
-- AND FOR A LONG TIME NO CAPTURE COULD TELL. Five shots of this plugin were
-- filed -- one per lane -- and not one of them held a key or opened a menu, so
-- everything the two paragraphs above are about was photographed by drives
-- that could not have seen it fail: a build with this on_key deleted made the
-- same pixels and the same log as a build with it. The drives that CAN see it
-- are jobs rows `ehreveal-*` and `ehtag-*`; see HOW TO PHOTOGRAPH IT below.
--
-- It owns no widget and describes no control: everything it puts on screen is
-- a scene primitive in the world pass. There is therefore no describe
-- callback, and the steady state costs no engine setter and no revalidate at
-- all -- only the fence's poll of the reveal key.
--
-- It does not ask for porcelain.draw_context either. That verb answers where
-- the pass can draw and whether that rect IS the canvas; a hull is named in
-- SCENE terms -- an element id the renderer projects itself -- so there is no
-- rectangle here to derive, clamp or intersect. Calling it would be one engine
-- call a frame for an answer nothing reads.
--
-- HOW TO PHOTOGRAPH IT, because this plugin is the one that can be perfectly
-- alive and photograph as perfectly dead.
--
-- Everything it draws is conditional on a TAG MATCHING AN NPC THE SCENE
-- CONTAINS, and an npc id belongs to a cache, not to the client. The cs1live
-- capture spent its whole life carrying the cs2 job's tag list -- 5037, 6708,
-- 2880 and friends, which are osrs239 ids -- at a LostCity scene whose npcs
-- are three-digit ids out of its own npc.pack. Not one matched, the plugin
-- drew nothing, the config write still reported applied=1 (a tag for a species
-- that is not in front of you is a perfectly valid tag), and the shot was
-- filed as clean. A dead plugin and a working one made the same picture.
--
-- So a capture of this plugin has to name ids from the lane's OWN content, and
-- the tag list is part of the evidence, not part of the boilerplate:
-- jobs/cs2all.txt carries osrs239 ids and jobs/cs1live.txt carries LostCity
-- ones read off content/maps/m50_53.jm2, the map square ~varrock lands in.
-- The offline cs1 lane cannot photograph it at all -- it never logs in, so the
-- npc pool is empty and npc_next answers -1 on the first call, whatever is
-- tagged.
--
-- And the TAG ROWS are a second picture, not a detail of the first one. A hull
-- shot proves nothing about them: they are built only inside a right-click
-- menu, only while the reveal key is held, and a plugin with no on_menu_build
-- at all makes exactly the hull shot that a working one makes. So there are
-- two drives per lane and each proves itself:
--
--   ehreveal-*  holds the key (TORIRS_SIM_KEYHOLD=42, which is
--               TORIRS_KEY_SHIFT, deferred past the title screen because a key
--               pressed on frame 1 lands there and is never pressed again),
--               right-clicks an npc whose id IS IN THE TAG LIST
--               (TORIRS_SIM_CLICK_NPC=<frame>,<type>,1 -- by type, because
--               these npcs wander and because a row over an untagged one
--               reads "Tag" and is a different claim), and leaves the menu
--               open, so the last frame carries the row over the hull.
--   ehtag-*     starts with NO tags, so the row reads "Tag";
--               TORIRS_SIM_MENU_ROW=<frame>,Tag finds the row by its label and
--               clicks it. The hull in the last frame and the `tags=` line the
--               run writes back into its own plugins.ini are there ONLY
--               because that row was picked, which is the only proof of
--               on_menu_select a picture can carry.
--
-- TORIRS_SIM_MENU_ROW prints the label it FOUND before it clicks it, so a row
-- that was never built cannot pass as one that was; exercised.py --menu-row
-- reads that line, and answers DECLARED OFF rather than INERT where the run
-- also carries this plugin's own expected key_edge absence.
--

---@type torirs.Plugin
local plugin = {
    id      = "entity-highlighter",
    title   = "Entity Highlighter",
    version = "2.0.0",
    config  = {
        {
            key = "color",
            type = "color",
            default = "#FF00FF",
            label = "Highlight colour"
        },
        {
            key = "fill",
            type = "int",
            default = "48",
            min = 0,
            max = 255,
            label = "Hull fill"
        },
        {
            key = "shape",
            type = "enum",
            choices = "bounds|mesh",
            default = "mesh",
            label = "Hull shape"
        },
        -- No label: persisted state, not something to hand-edit in the panel.
        { key = "tags", type = "string", default = "" },
        -- No label either, and for a second reason as well as that one:
        -- porcelain.key_edge names a CONFIG KEY and reads the modifier out of
        -- it, so the reveal key has to be a declared key even though the panel
        -- has never offered it. The choices are exactly the names the verb
        -- understands, so a typo is refused by the schema instead of reading
        -- back as ABSENT.
        {
            key = "reveal_key",
            type = "enum",
            choices = "off|shift|ctrl|escape|tab|space",
            default = "shift"
        },
    },
}

-- The two operations a tag row carries, inside the sixteen porcelain.menu_tag
-- reserves per subject. Their values are the encoding, and menu_tag has no
-- decoder, so TAG_OPS is PORCELAIN_MENU_TAG_OPS spelled again here.
local TAG_OPS = 16
local OP_UNTAG, OP_TAG = 0, 1

-- base_npc_id -> true. Rebuilt from config, never the other way round, so the
-- ini and the live set cannot disagree.
local tagged = {}
-- Whether the reveal key is one this lane can answer, and its current state.
local reveal_armed, reveal_down = false, false

local function npcs(api)
    local cursor = -1
    return function()
        local next_cursor, npc = api.world.npc_next(cursor)
        if not next_cursor then return nil end
        cursor = next_cursor
        return npc
    end
end

local function load_tags(api)
    tagged = {}
    for id in string.gmatch(api.config.tags, "%d+") do
        tagged[tonumber(id)] = true
    end
end

-- The sorted, deduplicated comma list the ini has always carried.
local function joined()
    local ids = {}
    for id in pairs(tagged) do ids[#ids + 1] = id end
    table.sort(ids)
    return table.concat(ids, ",")
end

-- Adding is the only direction that can overrun the value ceiling, so it is
-- the only one with a verb: config_list_add measures, refuses rather than let
-- the store truncate, leaves the stored list exactly as it was, and raises one
-- finding naming the id it would not store.
local function tag_add(api, id)
    if not api.porcelain.config_list_add("tags", tostring(id)) then return end
    -- config_list_add APPENDS, and this list has always been sorted. Sorting a
    -- comma list cannot change its length, so the value it just accepted still
    -- fits; a set that agrees with the store is a no-op and writes nothing.
    tagged[id] = true
    api.config.set("tags", joined())
end

-- Removing always shrinks the list. There is no ceiling to measure and no
-- verb to measure it with -- Porcelain has config_list_add and no remove.
local function tag_remove(api, id)
    tagged[id] = nil
    api.config.set("tags", joined())
end

function plugin.on_start(api)
    load_tags(api)
    reveal_armed, reveal_down = false, false
    if not api.porcelain.open() then
        -- Out loud: without the layer there is no refusal channel at all. A
        -- full route table, a truncated tag list and a lane with no keyboard
        -- would each be silent, which is the state this port exists to end.
        api.core.log("entity-highlighter: no porcelain layer -- tagging is off")
        return
    end
    -- Declared BEFORE the edge is asked for, because the ABSENT it may answer
    -- is raised inside that call. A lane with no keyboard frame is a fact
    -- about the lane, not a refusal nobody planned for; the rows stay off
    -- either way, and the finding still names the key.
    api.porcelain.expect_absent("role:reveal_key", "a touch lane has no keyboard frame")
    reveal_armed = api.porcelain.key_edge("reveal_key", function(down)
        reveal_down = down
    end)
end

-- The edge arrives HERE, not at a fence poll: a poll cannot see a press that
-- opens and closes inside one frame, and no part of the host forwards a key
-- into the layer on a plugin's behalf. reveal_armed is false both where there
-- is no layer and where this lane answered ABSENT for the key, which is
-- exactly when there is nothing to forward it to. Nothing is consumed -- the
-- reveal key is a modifier the client is still entitled to.
function plugin.on_key(api, event)
    if not reveal_armed then return end
    api.porcelain.note_key(event.key, event.down)
end

-- No on_frame_start. Nothing in this plugin has a cadence except the reveal
-- key, and the fence that polls its BINDING is the layer's own now: open()
-- installs the pump. The guard this handler used to carry -- fence only when
-- the edge armed, so a lane that answered ABSENT pays nothing -- is not lost,
-- it is answered differently: a handle with no description, no timer, no asset
-- and no armed edge makes ZERO engine calls at a fence, which test-plugin-lua
-- reads off the layer's own counters rather than asserting in prose here.

function plugin.on_config_changed(api, key)
    -- Covers the panel, a hand-edited ini, and our own save alike.
    if key == "tags" then load_tags(api) end
    -- A reveal key that CHANGED says nothing about whether the new one is
    -- held. The layer's edge only fires on a transition, so the state left
    -- over from the old key would stand until the player happened to press
    -- and release something -- and turning the reveal key off would not turn
    -- the rows off, which is the whole point of being able to.
    if key == "reveal_key" then reveal_down = false end
end

function plugin.on_draw_world(api, draw)
    local colour, fill = api.config.color, api.config.fill
    -- "mesh" by default: a tag marks a handful of npcs, which is where paying
    -- a projection per vertex buys an outline that follows the npc instead of
    -- the box around it. Switch to "bounds" when tagging a whole species.
    local shape = api.config.shape
    for npc in npcs(api) do
        if tagged[npc.base_npc_id] then
            draw.world_hull(npc.element_id, colour, fill, shape)
        end
    end
end

function plugin.on_menu_build(api, menu)
    -- The hover pass rebuilds the menu every frame just to compose the line
    -- under the cursor. Rows added there would never be seen, so the cheapest
    -- possible thing to do is leave immediately.
    if menu.hover_pass then return end
    -- reveal_armed is false where the key edge answered ABSENT -- a touch lane
    -- has no keyboard frame, and "off" is a real choice -- and the layer has
    -- already raised the finding that says so. reveal_down is that key's
    -- state, polled at this frame's fence off the same input frame the build
    -- below reads.
    if not reveal_armed or not reveal_down then return end

    local seen = {}
    for _, row in ipairs(menu.rows) do
        local slot = row.npc_slot
        if slot >= 0 and not seen[slot] then
            seen[slot] = true
            local npc = api.world.npc_by_slot(slot)
            if npc then
                local on = tagged[npc.base_npc_id]
                -- This changes a species preference, not the live NPC. Subject
                -- AND intended operation are frozen into the tag at build
                -- time: the server may reuse this slot while the menu is open,
                -- and re-resolving either at select time is exactly what made
                -- a retained Tag row retarget a different species.
                --
                -- The bool is not dropped, it is not needed: a refused route
                -- is a finding naming the label, raised by the verb.
                api.porcelain.menu_add(
                    (on and "Untag" or "Tag") .. " @yel@" .. npc.name,
                    api.porcelain.menu_tag(npc.base_npc_id, on and OP_UNTAG or OP_TAG))
            end
        end
    end
end

function plugin.on_menu_select(api, sel)
    if not sel.owned then return end

    local id = math.floor(sel.tag / TAG_OPS)
    if sel.tag % TAG_OPS == OP_TAG then tag_add(api, id) else tag_remove(api, id) end
    return "consume"
end

-- There is no on_stop. The host closes the handle after the stop callback --
-- which drops the key edge with it -- and on_start rebuilds the set from the
-- ini and re-arms the edge from nothing. A handler here would only be a second
-- close and three assignments on_start makes again.

return plugin
