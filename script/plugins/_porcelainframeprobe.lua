-- What do the Porcelain FRAME verbs answer on this lane?
--
-- The element probe beside this one asks whether the vocabulary binds; this
-- one asks the questions a frame PROVIDER asks: how big is the canvas it may
-- lay out in, how big did the lane author each surface and each numbered
-- member of one, which of the fourteen tabs does this lane number and mount,
-- and how does THIS root arrange its stones. It describes nothing and moves
-- nothing -- its one frame description is empty on purpose, so driving the
-- gameframe event costs the lane no pixels.
--
-- Asking is what registers a watch, so it asks every frame and reports once,
-- late: the first answer for any element is PENDING because its binding
-- arrives at a later fence, and the osrs239 lane does not bind its frame
-- until about frame 560.
local plugin = { id = "porcelainframeprobe", version = "1" }

local TABS = {
    "combat", "stats", "quests", "inventory", "equipment", "prayer", "magic",
    "clan", "account", "friends", "logout", "options", "emotes", "music",
}

-- Every element the frame verbs can be asked about, whole surfaces first and
-- then the numbered members of one.
local SURFACES = {
    "viewport", "minimap", "compass", "chat", "chat_bar", "sidebar", "modal", "orbs",
    "chat_filter:0", "chat_filter:3",
    "canvas", "usable",
}

local OFFER = "probe-frame"

local opened = false
local reported = false
local frames = 0

local function ask(api)
    api.porcelain.usable()
    for _, spec in ipairs(SURFACES) do api.porcelain.native_size(spec) end
    for _, name in ipairs(TABS) do
        api.porcelain.element("tab:" .. name)
        api.porcelain.lane_icon("tab:" .. name)
    end
end

local function box(b)
    if not b then return "-" end
    return string.format("%d,%d,%d,%d", b.x, b.y, b.width, b.height)
end

local function report(api)
    if reported then return end
    reported = true

    api.core.log("FRAMEPROBE", "usable", box(api.porcelain.usable()))

    for _, spec in ipairs(SURFACES) do
        api.core.log("FRAMEPROBE", "native_size", spec, box(api.porcelain.native_size(spec)))
    end
    -- The orb block's own members: 548 draws the world-map globe 30x30 where
    -- 601 draws it 34x34, and both keep their offsets when the block moves.
    for m = 0, 3 do
        local spec = "orbs"
        local x, y, w, h = api.frame.surface_member_native_box("orbs", m)
        api.core.log("FRAMEPROBE", "orb_member", spec .. "[" .. m .. "]",
            x and string.format("%d,%d,%d,%d", x, y, w, h) or "-")
    end

    for _, name in ipairs(TABS) do
        local state = api.porcelain.element("tab:" .. name)
        api.core.log("FRAMEPROBE", "tab", name,
            (state and state.bind) or "?",
            "icon=" .. tostring(api.porcelain.lane_icon("tab:" .. name)),
            "box=" .. box(state and state.box))
    end

    for _, axis in ipairs({ "rows", "columns" }) do
        local groups = api.porcelain.tab_group_count(axis)
        api.core.log("FRAMEPROBE", "tab_group_count", axis, groups)
        for g = 0, groups - 1 do
            api.core.log("FRAMEPROBE", "tab_group", axis, g,
                table.concat(api.porcelain.tab_group(axis, g), " "))
        end
    end
    api.core.log("FRAMEPROBE", "tab_detached", tostring(api.porcelain.tab_detached()))

    -- The refusal paths, in order: an offer nothing described, then the one
    -- this probe bound, then the release that takes it back off.
    api.porcelain.commit()
    local result, reason = api.porcelain.frame_event(
        { offer_id = "no-such-offer", active = true, width = 765, height = 503 })
    api.core.log("FRAMEPROBE", "frame_event", "no-such-offer", result, reason)
    api.porcelain.commit()
    result, reason = api.porcelain.frame_event(
        { offer_id = OFFER, active = true, width = 765, height = 503 })
    api.core.log("FRAMEPROBE", "frame_event", OFFER, result, reason)
    api.porcelain.commit()
    result, reason = api.porcelain.frame_event({ offer_id = OFFER, active = false })
    api.core.log("FRAMEPROBE", "frame_event", OFFER .. "/release", result, reason)

    local findings = api.porcelain.findings() or {}
    api.core.log("FRAMEPROBE_FINDINGS", #findings)
    for _, f in ipairs(findings) do
        api.core.log("FRAMEPROBE_FINDING", f.verb or "?", f.result or "?",
            f.detail or "", "expected=" .. tostring(f.expected), "count=" .. tostring(f.count))
    end
end

-- The frame description this probe binds: deliberately empty. What is being
-- proved is the OFFER machinery -- which description runs, what a release
-- takes off, what an unbound id answers -- not a layout.
local function describe(_) end

function plugin.on_start(api)
    opened = api.porcelain.open()
    api.core.log("FRAMEPROBE_OPEN", tostring(opened))
    if opened then
        api.porcelain.frame(OFFER, "fixed", 765, 503, describe)
    end
end

function plugin.on_frame_start(api)
    if not opened then return end
    frames = frames + 1
    -- open() installs the pump, and this handler runs between its fence and
    -- its commit, so the probe asks its questions inside a fenced epoch
    -- without spelling one.
    ask(api)
    if frames > 560 then report(api) end
end

return plugin
