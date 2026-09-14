-- Capability probe: prints what core.capability answers on THIS lane, and what
-- a skill snapshot says about being stated. Logged at several frames because
-- some answers are facts about state that arrives (the tree, the cache), not
-- about the build.
local plugin = { id = "capprobe", version = "1" }

local NAMES = {
    "widgets.geometry",
    "scripts.callbacks",
    "cs2_scripts",
    "highlight_groups",
    "varbit:ground_items_enabled",
    "varbit:no_such_varbit_here",
    "varp:tile_marker_color",
    "varp:no_such_varp_here",
    "server_tick",
    "server_tick.fenced",
    "loot_events",
    "item_bonuses",
    "native_orbs",
    "if_settab",
    "tab_select",
    "touch",
    "web",
    "browser",
    "telepathy",
}

local frames = 0

local function report(api, at)
    local parts = {}
    for _, name in ipairs(NAMES) do
        parts[#parts + 1] = name .. "=" .. (api.core.capability(name) and "1" or "0")
    end
    api.core.log("CAPPROBE at=" .. at .. " " .. table.concat(parts, " "))
    for _, index in ipairs({ 0, 3, 5 }) do
        local snapshot = api.game.skill(index)
        if snapshot then
            api.core.log(string.format(
                "CAPPROBE_SKILL at=%s index=%d name=%s stated=%s level=%d xp=%d",
                at, index, snapshot.name, tostring(snapshot.stated),
                snapshot.current_level, snapshot.xp))
        else
            api.core.log(string.format(
                "CAPPROBE_SKILL at=%s index=%d nil", at, index))
        end
    end
end

function plugin.on_start(api)
    report(api, "start")
end

function plugin.on_frame_start(api)
    frames = frames + 1
    if frames == 60 or frames == 300 or frames == 600 then
        report(api, "frame" .. frames)
    end
end

return plugin
