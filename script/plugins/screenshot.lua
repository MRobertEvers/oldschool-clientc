--
-- Screenshot
--
-- Takes a picture when something worth keeping happens, after RuneLite's
-- Screenshot plugin (net.runelite.client.plugins.screenshot). The moments are
-- the same ones, the folders are laid out the same way, and the filenames
-- carry the same timestamp -- see ImageCapture.saveScreenshot for the layout
-- this mirrors:
--
--      <destination>/<Player>/<Category>/<what>_<when>.png
--
-- (RuneLite separates the two halves with a space and brackets the number:
-- "Fishing(42) 2026-08-21_10-14-44.png". Neither character is in the set the
-- host holds a filename to, so a dash and an underscore carry the same
-- meaning; everything else about the layout is theirs.)
--
-- What is NOT here is the pattern matching. RuneLite recognises each moment by
-- re-reading the chatbox itself, which is why its Screenshot plugin carries a
-- dozen regexes -- and why every other plugin wanting the same moments has to
-- carry them again. Here the client recognises them once
-- (src/game/rs_game_events.c, tested against RuneLite's own corpus) and this
-- plugin only decides which are worth a picture. The config below is a list of
-- switches rather than a list of patterns, and the next plugin to want "on
-- boss kill" gets it for free.
--
-- Three things are worth knowing about the capture itself:
--
--   * It is DELAYED. A game event is recognised while the packet carrying it
--     is being executed, which is before the interface announcing it has been
--     laid out. RuneLite has the same problem and solves it the same way --
--     its widget handler only sets a flag, and the shot is taken on the next
--     GameTick. `delay_ticks` is that wait, made adjustable because our
--     server may take a tick longer to put the box up than Jagex's does.
--   * `destination` is a DIRECTORY. Absolute means exactly where you said;
--     anything else (including the empty default) lands under the client's own
--     plugin folder, which is the only place the browser lane can write. The
--     Player/Category structure is built either way, so a browser run's
--     captures are organised exactly like a desktop one's.
--   * Everything is switchable, and two things are off by default: `on_death`
--     and `on_duel_end`. RuneLite defaults those on; a client that
--     photographs your death without being asked is a different judgement
--     call, and this one errs the other way.
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

        -- RuneLite's manual screenshot, which is its `hotkey` config and its
        -- toolbar button. There is no toolbar to put a button on here, so the
        -- key is the whole of it. 0 means unbound.
        {
            key = "hotkey",
            type = "int",
            default = "0",
            min = 0,
            max = 512,
            label = "Manual screenshot key (LibToriRS_KeyCode, 0 = off)"
        },
    },
}

-- kind -> { config key that switches it on, folder it files under }.
--
-- A kind the client learns to recognise later and this table does not know is
-- IGNORED rather than photographed: a new moment appearing in someone's
-- screenshots folder unannounced is the wrong way round.
--
-- The folder names are RuneLite's own (its SD_* constants), with its spaces
-- turned into dashes because the host holds a filename to
-- [A-Za-z0-9._-]. Keeping the names means a folder of RuneLite screenshots
-- and a folder of these ones sort the same and merge cleanly.
local KINDS = {
    level_up           = { "on_level_up",           "Levels" },
    quest_complete     = { "on_quest_complete",     "Quests" },
    valuable_drop      = { "on_valuable_drop",      "Valuable-Drops" },
    untradeable_drop   = { "on_untradeable_drop",   "Untradeable-Drops" },
    boss_kill          = { "on_boss_kill",          "Boss-Kills" },
    pet                = { "on_pet",                "Pets" },
    collection_log     = { "on_collection_log",     "Collection-Log" },
    combat_achievement = { "on_combat_achievement", "Combat-Achievements" },
    death              = { "on_death",              "Deaths" },
    treasure_trail     = { "on_treasure_trail",     "Clue-Scroll-Rewards" },
    duel_end           = { "on_duel_end",           "Duels" },
}

-- Captures waiting out their delay: { ticks_left, name, dir }.
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

-- <destination>/<Player>/<Category>, RuneLite's layout.
--
-- The player folder is theirs too, and for the reason they have it: two
-- accounts played from one install would otherwise drop their screenshots into
-- the same pile. An unknown player (not logged in yet) simply leaves that
-- level out rather than inventing a name.
local function folder(api, category)
    local out = api.config.destination
    local player = api.local_player()

    if player and player.name ~= "" then
        local who = slug(player.name)
        if who ~= "" then
            out = (out ~= "" and (out .. "/") or "") .. who
        end
    end
    if category then
        out = (out ~= "" and (out .. "/") or "") .. category
    end
    return out
end

-- "<what> <when>", RuneLite's naming, with its space as an underscore.
--
-- The timestamp rather than a counter, and that is worth being deliberate
-- about: a counter has to be persisted, and a persisted counter that resets --
-- a fresh install, a cleared config -- starts overwriting the screenshots it
-- already took. A wall-clock stamp cannot collide with the past no matter what
-- state is lost. api.datestamp() exists because the plugin sandbox does not
-- link `os`, so there is no other clock a script can read.
local function filename(api, ev)
    local name = slug(ev.subject)

    if name == "" then
        name = ev.kind
    end
    -- The level or the kill count, when the moment has one. RuneLite writes
    -- these as "Fishing(42)"; brackets are not in the host's character set, so
    -- the dash carries the same meaning.
    if ev.value and ev.value >= 0 then
        name = name .. "-" .. tostring(ev.value)
    end
    return name .. "_" .. (api.datestamp() or "unknown") .. ".png"
end

-- Is this moment one we were asked for? Returns the folder when it is.
local function wanted(api, ev)
    local kind = KINDS[ev.kind]

    if not kind then return nil end
    if not api.config[kind[1]] then return nil end

    -- The one kind with a threshold as well as a switch, exactly as RuneLite
    -- pairs screenshotValuableDrop with valuableDropThreshold. A boss that
    -- drops five stackable things per kill would otherwise fill the folder.
    if ev.kind == "valuable_drop" and ev.value >= 0 and
        ev.value < api.config.min_drop_value then
        return nil
    end
    return kind[2]
end

local function capture(api, name, dir)
    if api.screenshot(name, dir) then
        api.log("captured " .. (dir ~= "" and (dir .. "/") or "") .. name)
    end
end

function plugin.on_game_event(api, ev)
    local category = wanted(api, ev)

    if not category then return end

    local shot = { ticks_left = api.config.delay_ticks,
                   name = filename(api, ev),
                   dir = folder(api, category) }

    -- Zero delay means this frame, and the frame is still the one BEFORE the
    -- event landed on screen -- so it is honoured rather than special-cased,
    -- and anyone who sets it gets what they asked for.
    if shot.ticks_left <= 0 then
        capture(api, shot.name, shot.dir)
        return
    end
    pending[#pending + 1] = shot
end

-- Counted in SERVER ticks rather than frames, because what is being waited for
-- is the server's own doing: the level-up box and the quest scroll arrive in a
-- later packet, not a later frame. RuneLite counts GameTicks here for the same
-- reason. A frame count would mean something different on every machine.
function plugin.on_server_tick(api, ev)
    if #pending == 0 then return end

    local keep = {}
    for i = 1, #pending do
        local shot = pending[i]
        shot.ticks_left = shot.ticks_left - 1
        if shot.ticks_left <= 0 then
            capture(api, shot.name, shot.dir)
        else
            keep[#keep + 1] = shot
        end
    end
    pending = keep
end

-- RuneLite's manual screenshot: no category, no delay, no waiting for anything
-- to settle. Whatever is on screen is what was asked for.
function plugin.on_key(api, ev)
    local key = api.config.hotkey

    if key == 0 or not ev.down or ev.key ~= key then return end
    capture(api,
        "screenshot_" .. (api.datestamp() or "unknown") .. ".png",
        folder(api, nil))
end

function plugin.on_stop(api)
    -- Anything still waiting belongs to a session that is over. Dropped rather
    -- than taken on the way out: the frame it was waiting for never came.
    pending = {}
end

return plugin
