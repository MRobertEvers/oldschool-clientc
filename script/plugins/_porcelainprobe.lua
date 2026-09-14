-- Does every element of the portable vocabulary bind on this lane?
-- The first real user of the Porcelain core: it describes nothing, it only
-- asks, so what it prints is the element table measured rather than stated.
local plugin = { id = "porcelainprobe", version = "1" }

local ELEMENTS = {
    "viewport", "minimap", "compass", "orbs", "chat", "chat_bar", "chat_backing",
    "chat_input", "report_button", "public_chat_button", "sidebar", "modal",
    "canvas", "safe", "usable", "frame_root",
    "orb:hitpoints", "orb:prayer", "orb:run", "orb:spec",
    "chat_filter:0", "chat_filter:3", "lane_chrome:0",
    "tab:inventory", "tab:logout", "panel:inventory",
}

local opened = false
local reported = false
local frames = 0

-- Asking is what registers the watch, so the probe asks every frame and
-- reports once, late: the first answer for any element is always PENDING
-- because its binding arrives at a later fence.
local function ask(api)
    for _, spec in ipairs(ELEMENTS) do
        api.porcelain.element(spec)
    end
end

local function report(api)
    if reported then return end
    reported = true
    for _, spec in ipairs(ELEMENTS) do
        local state = api.porcelain.element(spec)
        if type(state) ~= "table" then
            api.core.log("PORCELAIN_PROBE", spec, "nil")
        else
            local b = state.box or {}
            api.core.log("PORCELAIN_PROBE", spec, state.bind or "?",
                string.format("%d,%d,%d,%d", b.x or 0, b.y or 0, b.width or 0, b.height or 0),
                "presented=" .. tostring(state.presented),
                "facets=" .. tostring(state.facets or 0))
        end
    end
    -- A family's COUNT is lane data, and a lane that answers zero for a
    -- family it visibly has is the same mapping gap an ABSENT element is.
    for _, family in ipairs({ "chat_filter", "tab", "orb", "lane_chrome" }) do
        api.core.log("PORCELAIN_PROBE_COUNT", family, api.porcelain.count(family))
    end
    local findings = api.porcelain.findings()
    api.core.log("PORCELAIN_PROBE_FINDINGS", #(findings or {}), "frames=" .. tostring(frames))
    for _, f in ipairs(findings or {}) do
        api.core.log("PORCELAIN_PROBE_FINDING", f.verb or "?", f.element or "?",
                     f.result or "?", "expected=" .. tostring(f.expected))
    end
end

function plugin.on_start(api)
    opened = api.porcelain.open()
    api.core.log("PORCELAIN_PROBE_OPEN", tostring(opened))
end

function plugin.on_frame_start(api)
    if not opened then return end
    frames = frames + 1
    api.porcelain.fence()
    ask(api)
    -- Late, not early: the interesting answer is what the vocabulary says
    -- once the lane has finished mounting its frame, not during the boot.
    if frames > 1400 then report(api) end
    api.porcelain.commit()
end

return plugin
