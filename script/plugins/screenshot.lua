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
-- ## The camera button
--
-- RuneLite's manual screenshot is a hotkey and a toolbar button. The hotkey is
-- below; the button is `camera`, and it is off by default because a client
-- that puts a control on screen without being asked is the same judgement call
-- as the one above.
--
-- Where it goes is the whole of the setting, and the two families of answer
-- are different in kind:
--
--   * A CORNER of the world safe area -- the scene with the chrome and every
--     other plugin's claim already taken out, which is the only box that means
--     "somewhere the player is not trying to look" on both a fixed and a
--     resizable frame. It sits there mostly transparent and comes up solid
--     under the pointer, the way the reference's own orbs light up: a control
--     over the world has to be findable without being something you look past
--     the whole session.
--   * The REPORT ABUSE button. Not "beside it" -- in its place: the region is
--     claimed over the button's own box, so the click that was Report abuse is
--     Take Screenshot instead. Solid, because a chat button is chrome and a
--     half-transparent one would read as disabled, and wearing the red plate
--     that button wears -- @see PLATE_RAMP, which is measured off it.
--
-- The art is `camera.png` and `camera_small.png`, both shipped in this
-- plugin's asset folder and hand-authored in the options_icons palette --
-- @see script/plugins/assets/screenshot/camera.txt and camera_small.txt,
-- which are where to edit them. Two sizes because the two placements are two
-- different boxes: a
-- corner of the scene has room for the 28x26 icon and a chat button, at 22
-- rows, does not -- and a picture the client cannot scale is either the right
-- size or sheared off by the clip.
--

---@type torirs.Plugin
local plugin = {
    name    = "screenshot",
    title   = "Screenshots",
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

        -- RuneLite's manual screenshot: its `hotkey` config, and its toolbar
        -- button below. 0 means unbound.
        {
            key = "hotkey",
            type = "int",
            default = "0",
            min = 0,
            max = 512,
            label = "Manual screenshot key (LibToriRS_KeyCode, 0 = off)"
        },

        -- One key and not two (a switch plus a position), because "off" IS a
        -- position in the same sense the others are: every value answers the
        -- one question the button raises, and a pair would let a player set
        -- where a button they turned off would have gone.
        {
            key = "camera",
            type = "enum",
            choices = "off|top-left|top-right|bottom-left|bottom-right|report-button",
            default = "off",
            label = "Camera button"
        },

    },
}

------------------------------------------------------------------ the button

--- The report button, wherever this revision keeps it.
---
--- This used to be a config key holding `<interface>:<component>`, parsed by
--- hand and shifted into a uid, defaulting to the OldSchool chat bar's
--- `162:31` -- which is a correct id on exactly one cache and covers whatever
--- component 31 of interface 162 happens to be on every other. The profile is
--- where that answer belongs, and a role is how it gets asked for.
---
--- Bound once, resolved on every call: on a 2004 frame this is a client
--- builtin with no component id at all, and on an OldSchool one it is
--- interface 162's component 31. Neither fact reaches this file.
local report_button

--- Handed to hit_region and read back in on_canvas_click. One region, so one
--- tag; it is the plugin's own number and the host does not look at it.
local TAG_CAPTURE = 1

--- What the mouseover line says and what the right-click menu offers. First op
--- is also the left click, which is the whole of the interaction.
local CAPTURE_OPS = { "Take Screenshot" }

--- How far a corner button sits off the safe area's edges.
local MARGIN = 6

--- The report button's own red, one entry per row from its top to its bottom.
---
--- The plate is what makes the placement REPLACE the button rather than sit on
--- top of it: the widget underneath still draws its own label, and a camera
--- centred on a bare button leaves "Re" and "rt" showing either side. So
--- something has to be painted over the whole box -- and the question is only
--- what. This used to be a flat dark rect with a light outline: a grey square
--- with corners, in a row of rounded plates, where a red button was. It read
--- as a hole cut in the frame.
---
--- MEASURED off the button it stands in for rather than chosen, on both of the
--- frames that draw one: the OldSchool chatbox's own `Report` (interface 162's
--- component 31, 79x22) and the plate the gameframe composes for a 2004 lane's
--- `Report abuse` (100x23, three-sliced out of `osrs_chat_button_report`). The
--- two ramps agree to within a couple of units a channel -- they are the same
--- sprite family -- so one of them serves both, and this is the taller.
---
--- Drawn rather than shipped as a picture because the BOX is the frame's: 79
--- wide here, 100 there, 33 rows on a dat1 profile's own privacy bar. A
--- picture is one size and the client's blit does not scale, so a plate cut
--- for one frame would be short on the next; a ramp indexed by row fits
--- whatever box the role reports.
---
--- What the ramp does NOT carry is the source sprite's other axis: the real
--- plate also falls into shadow over its last dozen columns, and a rectangle
--- holds one colour, so reproducing that means a rect per column per row.
--- Fifteen columns times twenty-three rows is most of a plugin's frame draw
--- budget (512 items) spent on one button, and in bands wide enough to be
--- affordable it reads as stripes -- which is worse than the flat end it
--- replaces. The vertical ramp is the half that carries the shape.
local PLATE_RAMP = {
    0x872928, 0x722323, 0x722323, 0x722323, 0x722323, 0x722323,
    0x722323, 0x6B2020, 0x6B2020, 0x631F1D, 0x631F1D, 0x631F1D,
    0x631F1D, 0x5C1D1C, 0x531B1A, 0x531B1A, 0x4B1917, 0x4B1917,
    0x431715, 0x431715, 0x3A1614, 0x3A1614, 0x2A1412,
}

--- How far each end row is pulled in, top row first, mirrored at the bottom.
--- The rounded cap the chat plate is cut with, read off the same button: four
--- columns on the corner row, then two, then one, then one.
local PLATE_CAP = { 4, 2, 1, 1 }

--- Hover, in the frame's own arithmetic: a lit chat button is the same plate
--- with every channel half again as bright, which is how `redraw_chat_buttons`
--- gets its hovered art from its idle art (@see FRAME_CHAT_BRIGHT_NUM in
--- src/plugin/plugins/gameframe.c).
local PLATE_LIT_NUM = 3
local PLATE_LIT_DEN = 2

--- Transparency in the reference's sense: 0 is solid, 255 is invisible. The
--- resting value is a judgement -- far enough back that it is not competing
--- with the scene, near enough that it is visibly THERE, which a control the
--- player has to remember is not.
local TRANS_RESTING = 170
local TRANS_HOVER = 0

--- The handles from api.image_load, or nil until on_start has asked for them.
--- Asynchronous: image_size answers nil for the first frames, and the button
--- simply is not drawn until it does.
local icon = nil
local icon_small = nil

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

-- Take the picture and say so, in the chatbox.
--
-- The message is the whole reason api.screenshot answers with a path: a
-- capture is silent by nature -- the file appears somewhere the player is not
-- looking, under a folder layout they configured once and have since
-- forgotten -- and "a screenshot happened" without a destination is only half
-- an answer. The path is the engine's own, resolved, so the browser lane's
-- saved-asset folder and a desktop user's absolute destination both read as
-- the place the file actually is.
--
-- A game message rather than a log line, and both rather than either: the log
-- is for whoever is reading stderr, the chatbox is for whoever is playing.
local function capture(api, name, dir)
    local ok, path = api.screenshot(name, dir)

    if not ok then return end
    api.log("captured " .. path)
    api.notify("Screenshot saved: " .. path)
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
local function capture_now(api)
    capture(api,
        "screenshot_" .. (api.datestamp() or "unknown") .. ".png",
        folder(api, nil))
end

function plugin.on_key(api, ev)
    local key = api.config.hotkey

    if key == 0 or not ev.down or ev.key ~= key then return end
    capture_now(api)
end

-- The box the button lives in this frame, and whether that box is a BUTTON --
-- the report placement, which wears a plate -- or a region to sit in a corner
-- of.
--
-- Measured EVERY frame and never cached, because every input is: the safe area
-- moves when a window is resized or another plugin reserves an edge, and the
-- report button moves when the gameframe is rebuilt. A cached box is a button
-- that answers clicks where it used to be.
--
-- Returns nil when there is nowhere to put it -- the setting is off, the frame
-- has no report button, no region answered -- which the caller draws nothing
-- for rather than guessing a corner.
local function button_box(api)
    local where = api.config.camera

    if where == "off" then return nil end

    if where == "report-button" then
        -- One question, asked once. Which node answers it is the profile's
        -- business: a chat-button member on a 2004 frame, interface 162's
        -- component 31 on an OldSchool one, and neither spelling is here.
        --
        -- A revision whose profile has not named it is an answer, not a fault:
        -- the button is simply not offered rather than landing somewhere
        -- arbitrary, which is what the nil the role hands back becomes.
        return report_button.rect(), true
    end

    -- The scene with the chrome taken out. The fallback chain is slot_rect's
    -- own: ask for the tightest region first, and a frame that has no safe
    -- area still has a canvas.
    return api.layout.safe.rect()
        or api.layout.viewport.rect()
        or api.layout.canvas.rect(), false
end

-- Where the icon sits inside a corner region, as x, y.
local function corner_at(api, area, w, h)
    local where = api.config.camera
    local left = area.x + MARGIN
    local right = area.x + area.w - w - MARGIN
    local top = area.y + MARGIN
    local bottom = area.y + area.h - h - MARGIN

    if where == "top-left" then return left, top end
    if where == "top-right" then return right, top end
    if where == "bottom-left" then return left, bottom end
    if where == "bottom-right" then return right, bottom end
    return nil
end

-- The largest icon that fits `box` with a row of plate showing above and
-- below it, or nil while neither read has landed.
--
-- A height and not a preference, because the box is not this plugin's to
-- choose: 22 rows on an OldSchool chat strip, 23 on a composed plate, 33 on a
-- 2004 profile's privacy bar. The big icon is 26 tall, so on the first two it
-- is the small one that fits and on the last it is not.
--
-- `box` is nil for a corner placement, which is the scene and always has room.
local function icon_that_fits(api, box)
    if icon then
        local w, h = api.image_size(icon)
        if w and (not box or h + 2 <= box.h) then return icon end
    end
    if icon_small and api.image_size(icon_small) then return icon_small end
    return nil
end

-- The plate the report placement wears: the button it replaces, repainted.
--
-- One filled rect a row, which is what a gradient is when the only primitive
-- is a rectangle -- 23 of them for a chat button, and the ramp is indexed by
-- the row's position in the box so a taller button stretches it rather than
-- running off the end of it.
local function lit(rgb)
    local out = 0

    for shift = 0, 16, 8 do
        local c = ((rgb >> shift) & 0xFF) * PLATE_LIT_NUM // PLATE_LIT_DEN
        if c > 255 then c = 255 end
        out = out | (c << shift)
    end
    return out
end

local function draw_plate(draw, box, hovered)
    local rows = #PLATE_RAMP

    for row = 0, box.h - 1 do
        local rgb = PLATE_RAMP[row * rows // box.h + 1]
        -- The rounded ends, top row first and mirrored at the bottom. A box
        -- too short for both caps keeps the top one, and one too narrow for a
        -- cap at all keeps its corners -- neither can be drawn as a negative
        -- width.
        local cap = PLATE_CAP[row + 1] or PLATE_CAP[box.h - row] or 0

        if cap * 2 >= box.w then cap = 0 end
        draw.rect(
            box.x + cap,
            box.y + row,
            box.w - cap * 2,
            1,
            hovered and lit(rgb) or rgb,
            255)
    end
end

--- The camera button: claimed, then drawn.
---
--- The claim comes first for the reason minimap_orbs claims before it blits --
--- later regions win where two overlap, so a region declared after the drawing
--- would be fighting whatever the next handler declares rather than sitting
--- under its own art.
function plugin.on_draw_canvas(api, draw)
    -- Before login there is no session to photograph and no frame to hang a
    -- button off; the login screen is not where this belongs.
    if not api.local_player() then return end

    local box, plated = button_box(api)
    if not box then return end

    -- The read has not landed. Ordinary for the first frames after a start,
    -- and not worth a message: the button appears when the picture does.
    local art = icon_that_fits(api, plated and box or nil)
    if not art then return end

    local w, h = api.image_size(art)
    if not w then return end

    local x, y
    if plated then
        x = box.x + (box.w - w) // 2
        y = box.y + (box.h - h) // 2
    else
        x, y = corner_at(api, box, w, h)
        if not x then return end
    end

    -- What is actually ON SCREEN, which is the art cut to the box it was
    -- placed in: a frame whose report button is smaller than the picture shows
    -- the middle of the camera rather than spilling it over the chat. A corner
    -- button's own area never cuts it, so there the two are the same rectangle.
    local rx = math.max(x, box.x)
    local ry = math.max(y, box.y)
    local rw = math.min(x + w, box.x + box.w) - rx
    local rh = math.min(y + h, box.y + box.h) - ry

    if rw <= 0 or rh <= 0 then return end

    -- What answers the click. In the report button's place that is the whole
    -- BUTTON and not the camera on it -- a chat button is clickable across its
    -- plate, and a region cut to the icon would leave the ends of a control
    -- that plainly is one doing nothing. A corner button has no plate, so
    -- there the region is the art, clipped: a region reaching past what a
    -- player can see is a click that lands on nothing visible.
    local hover_x, hover_y, hover_w, hover_h = rx, ry, rw, rh
    if plated then
        hover_x, hover_y, hover_w, hover_h = box.x, box.y, box.w, box.h
    end
    api.hit_region(hover_x, hover_y, hover_w, hover_h, CAPTURE_OPS, TAG_CAPTURE)

    local mx, my = api.mouse_pos()
    local hovered = mx ~= nil
        and mx >= hover_x and mx < hover_x + hover_w
        and my >= hover_y and my < hover_y + hover_h

    -- In the report button's place it is chrome, and chrome is solid: a
    -- half-transparent chat button reads as a disabled one. It lights under
    -- the pointer the way the button it replaces does -- the plate brightens,
    -- not the camera, which is what a chat button does and what an orb does
    -- not.
    local trans = TRANS_RESTING
    if plated then
        trans = TRANS_HOVER
        draw_plate(draw, box, hovered)
    elseif hovered then
        trans = TRANS_HOVER
    end

    draw.image(art, x, y, trans, rx, ry, rw, rh)
end

--- The click, and the menu row, arriving as the one thing they are.
function plugin.on_canvas_click(api, ev)
    if ev.tag ~= TAG_CAPTURE then return end
    capture_now(api)
end

--- Asked for once, at start, and not on the first frame that wants it: the
--- read is asynchronous either way, and a load issued from the draw handler
--- would be issued again on every frame until it landed.
function plugin.on_start(api)
    icon = api.image_load("camera.png")
    if not icon then
        api.log("camera.png did not load; the camera button is unavailable")
    end
    -- Both, and at start rather than when a short box first asks for one: a
    -- load issued from the draw handler is issued again on every frame until
    -- it lands, and the button that needs this one is the one a player sees
    -- most.
    icon_small = api.image_load("camera_small.png")
    if not icon_small then
        api.log("camera_small.png did not load; the chat-button camera is unavailable")
    end
    -- Bound here and resolved later. on_start runs before the gameframe is
    -- built, so asking now would answer "not here" on every lane.
    report_button = api.role("report_button")
end

function plugin.on_stop(api)
    -- Anything still waiting belongs to a session that is over. Dropped rather
    -- than taken on the way out: the frame it was waiting for never came.
    pending = {}
    if icon then
        api.image_release(icon)
        icon = nil
    end
    if icon_small then
        api.image_release(icon_small)
        icon_small = nil
    end
end

return plugin
