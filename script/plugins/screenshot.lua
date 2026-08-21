--
-- Screenshot
--
-- Takes a picture when something worth keeping happens, after RuneLite's
-- Screenshot plugin (net.runelite.client.plugins.screenshot). The moments are
-- the same ones: a level-up, a quest completion, a valuable or untradeable
-- drop, a boss kill, a pet, a collection-log entry, a combat achievement, a
-- death, a clue casket, the end of a duel.
--
-- What is NOT here is the pattern matching. RuneLite recognises each of those
-- by re-reading the chatbox itself, which is why its Screenshot plugin carries
-- a dozen regexes that every other plugin wanting the same moments has to
-- carry again. Here the client recognises them once (src/game/rs_game_events.c)
-- and this plugin only decides which ones are worth a picture -- so the
-- config below is a list of switches rather than a list of patterns, and the
-- next plugin to want "on boss kill" gets it for free.
--
-- Two things are worth knowing about the capture itself:
--
--   * It is DELAYED. A game event is recognised while the packet carrying it
--     is being executed, which is before the interface announcing it has been
--     laid out. `delay_ticks` is how long to let the moment settle -- the
--     default of 2 is enough for a level-up box or a quest scroll to be on
--     screen.
--   * `destination` is a DIRECTORY, and empty is the right default. The client
--     runs in a browser as well as on a desktop, and the browser lane has no
--     filesystem to name a path in; empty means "the client's own plugin data
--     folder", which exists on every platform. Set it on a desktop if you want
--     the pictures somewhere you can find them.
--

---@type torirs.Plugin
local plugin = {
    name    = "screenshot",
    version = "1.0.0",
    config  = {
        {
            key = "destination",
            type = "string",
            default = "",
            label = "Save folder (empty = client plugin folder)"
        },
        {
            key = "delay_ticks",
            type = "int",
            default = "2",
            min = 0,
            max = 20,
            label = "Ticks to wait before capturing"
        },

        { key = "on_level_up",           type = "bool", default = true,  label = "Level up" },
        { key = "on_quest_complete",     type = "bool", default = true,  label = "Quest complete" },
        { key = "on_boss_kill",          type = "bool", default = true,  label = "Boss kill" },
        { key = "on_pet",                type = "bool", default = true,  label = "Pet drop" },
        { key = "on_collection_log",     type = "bool", default = true,  label = "Collection log" },
        { key = "on_combat_achievement", type = "bool", default = true,  label = "Combat achievement" },
        { key = "on_treasure_trail",     type = "bool", default = true,  label = "Clue casket" },
        { key = "on_valuable_drop",      type = "bool", default = true,  label = "Valuable drop" },
        { key = "on_untradeable_drop",   type = "bool", default = false, label = "Untradeable drop" },
        -- Off by default, both of them, for the same reason: they fire on the
        -- worst moment of the session and on every single duel, and a client
        -- that photographs your death unasked is not a feature.
        { key = "on_death",              type = "bool", default = false, label = "Death" },
        { key = "on_duel_end",           type = "bool", default = false, label = "Duel end" },

        {
            key = "min_drop_value",
            type = "int",
            default = "100000",
            min = 0,
            max = 2000000000,
            label = "Valuable drop threshold"
        },

        -- No label: this is state the plugin keeps for itself, not a setting.
        -- It is what stops the second Woodcutting level of the session
        -- overwriting the first, and it is persisted rather than counted from
        -- zero so a restart does not start overwriting either.
        { key = "counter", type = "int", default = "0", min = 0, max = 2000000000 },
    },
}

-- kind -> the config key that switches it on. A kind the client learns to
-- recognise later and this table does not know is IGNORED rather than
-- photographed: a new moment appearing in someone's screenshots folder
-- unannounced is the wrong way round.
local SWITCH = {
    level_up           = "on_level_up",
    quest_complete     = "on_quest_complete",
    valuable_drop      = "on_valuable_drop",
    untradeable_drop   = "on_untradeable_drop",
    boss_kill          = "on_boss_kill",
    pet                = "on_pet",
    collection_log     = "on_collection_log",
    combat_achievement = "on_combat_achievement",
    death              = "on_death",
    treasure_trail     = "on_treasure_trail",
    duel_end           = "on_duel_end",
}

-- Captures waiting out their delay: { ticks_left, filename }.
local pending = {}

-- The host only accepts a bare filename of [A-Za-z0-9._-], which is not a
-- limitation to work around -- it is what stops a plugin writing outside the
-- folder it was given. Every other character becomes a dash, and a run of them
-- collapses, so "Chambers of Xeric" is "Chambers-of-Xeric" and not
-- "Chambers---of---Xeric".
local function slug(text)
    if not text or text == "" then return "" end
    local out = string.gsub(text, "[^A-Za-z0-9]+", "-")
    out = string.gsub(out, "^%-+", "")
    out = string.gsub(out, "%-+$", "")
    if #out > 40 then out = string.sub(out, 1, 40) end
    return out
end

-- kind, subject and a number that never repeats. The number is last so the
-- name sorts by what happened rather than by when, which is how you find the
-- Fishing levels in a folder of four hundred pictures.
local function filename(ev, n)
    local name = ev.kind
    local subject = slug(ev.subject)

    if subject ~= "" then
        name = name .. "-" .. subject
    end
    -- The level or the kill count, when the moment has one. `value` is -1 for
    -- the kinds that carry none.
    if ev.value and ev.value >= 0 then
        name = name .. "-" .. tostring(ev.value)
    end
    return name .. "-" .. tostring(n) .. ".png"
end

-- Is this moment one we were asked for?
local function wanted(api, ev)
    local switch = SWITCH[ev.kind]

    if not switch then return false end
    if not api.config[switch] then return false end

    -- The one kind with a threshold as well as a switch. A boss that drops
    -- five stackable things per kill would otherwise fill the folder.
    if ev.kind == "valuable_drop" and ev.value >= 0 then
        return ev.value >= api.config.min_drop_value
    end
    return true
end

function plugin.on_game_event(api, ev)
    if not wanted(api, ev) then return end

    local n = api.config.counter + 1
    api.cfg_set("counter", n)

    local delay = api.config.delay_ticks
    local shot = { ticks_left = delay, name = filename(ev, n) }

    -- Zero delay means this frame, and the frame is still the one BEFORE the
    -- event landed on screen -- so it is honoured rather than special-cased,
    -- and anyone who sets it gets what they asked for.
    if delay <= 0 then
        api.screenshot(shot.name, api.config.destination)
        api.log("captured " .. shot.name)
        return
    end
    pending[#pending + 1] = shot
end

-- Counted in SERVER ticks rather than frames, because what is being waited for
-- is the server's own doing: the level-up box and the quest scroll arrive in a
-- later packet, not a later frame. A frame count would mean something
-- different on every machine.
function plugin.on_server_tick(api, ev)
    if #pending == 0 then return end

    local keep = {}
    for i = 1, #pending do
        local shot = pending[i]
        shot.ticks_left = shot.ticks_left - 1
        if shot.ticks_left <= 0 then
            if api.screenshot(shot.name, api.config.destination) then
                api.log("captured " .. shot.name)
            end
        else
            keep[#keep + 1] = shot
        end
    end
    pending = keep
end

function plugin.on_stop(api)
    -- Anything still waiting belongs to a session that is over. Dropped rather
    -- than taken on the way out: the frame it was waiting for never came.
    pending = {}
end

return plugin
