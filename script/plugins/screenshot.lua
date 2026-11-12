-- Screenshot capture: game-event and hotkey captures, plus a camera control
-- described to the Porcelain layer. The report-button mode stands the camera
-- IN PLACE of the native Report button through a REPLACE placement, so it
-- follows the button's native visibility both ways and the native identity,
-- operation and later server updates stay intact. The corner modes place the
-- control at an inset inside the live viewport.
--
-- The pictures themselves have a home in the client: a rail stone
-- (panel_icon.png) opens a gallery of them, newest first, by album, a page at
-- a time; a press on one opens it (desktop) or downloads it (browser). Its
-- settings face says where they are kept -- a folder chosen with the
-- platform's own chooser, or this browser's storage laid out as a tree -- and
-- hands them over: Open folder on a desktop, Download all (.zip) in a
-- browser. The platform half is api.assets.capture_* and pick_folder; no path
-- is ever typed.
--
-- What this file no longer does, because the layer does it: it does not watch
-- widgets, does not remember which control is alive, does not re-place a
-- control when the frame moves its element or the window resizes (the corner
-- camera going stale on a resize was a defect; geometry now follows the target
-- at every fence), does not remove a control whose target died, does not hold
-- or release image handles, and does not drive the layer: open() installs the
-- pump, so the fence, the commit, the config note and the server-tick forward
-- are the layer's and this file's handlers run inside them.
---@type torirs.Plugin
local plugin = {
    id = "screenshot",
    title = "Screenshots",
    version = "5.0.0",
    config = {
        -- Where the pictures go. NOT a text box: the settings face chooses it
        -- with the platform's folder chooser, or says it is this browser's
        -- storage, and "" is the plugin's own folder. A key with no label is
        -- kept as state and left out of the generated form.
        { key = "destination", type = "string", default = "" },
        { key = "delay_ticks", type = "int", default = "2", min = 0, max = 20,
          label = "Ticks to wait before capturing" },
        { key = "on_level_up", type = "bool", default = true, label = "Level up" },
        { key = "on_quest_complete", type = "bool", default = true, label = "Quest complete" },
        { key = "on_boss_kill", type = "bool", default = true, label = "Boss kill" },
        { key = "on_pet", type = "bool", default = true, label = "Pet drop" },
        { key = "on_collection_log", type = "bool", default = true, label = "Collection log" },
        { key = "on_combat_achievement", type = "bool", default = true, label = "Combat achievement" },
        { key = "on_treasure_trail", type = "bool", default = true, label = "Clue casket" },
        { key = "on_valuable_drop", type = "bool", default = true, label = "Valuable drop" },
        { key = "on_untradeable_drop", type = "bool", default = false, label = "Untradeable drop" },
        { key = "on_death", type = "bool", default = false, label = "Death" },
        { key = "on_duel_end", type = "bool", default = false, label = "Duel end" },
        { key = "min_drop_value", type = "int", default = "100000", min = 0,
          max = 2000000000, label = "Valuable drop threshold" },
        { key = "hotkey", type = "int", default = "0", min = 0, max = 512,
          label = "Manual screenshot key (0 = off)" },
        -- REPLACE consumes the target's paint, input and hover, so the native
        -- "Report abuse" row is unreachable while the camera stands there.
        -- That is a consequence of the mode and is stated, not discovered.
        --
        -- What it consumes it also ANSWERS, all of it: the whole Report
        -- button takes the screenshot, not just the 20x16 of it the camera
        -- art covers. The layer stands an invisible hit box the size of the
        -- target under the picture for exactly this -- before it did, a click
        -- 7px left of the icon's ink was inert on both sides, because the
        -- native op had been taken out of the frame and the camera was not
        -- there to answer.
        { key = "camera", type = "enum",
          choices = "off|top-left|top-right|bottom-left|bottom-right|report-button",
          default = "off", label = "Camera button (report-button hides the Report abuse option)" },
    },
}

local MARGIN = 6
local OPERATION = "Take screenshot"
local IDLE_OPACITY, PRESSED_OPACITY = 170, 255
-- Frames the press stays lit. Without it the control that has ever been
-- pressed is opaque for ever, which is what it did before the port.
local PRESS_FRAMES = 12

local CORNERS = {
    ["top-left"] = "top_left", ["top-right"] = "top_right",
    ["bottom-left"] = "bottom_left", ["bottom-right"] = "bottom_right",
}

local KINDS = {
    level_up = { "on_level_up", "Levels" },
    quest_complete = { "on_quest_complete", "Quests" },
    valuable_drop = { "on_valuable_drop", "Valuable-Drops" },
    untradeable_drop = { "on_untradeable_drop", "Untradeable-Drops" },
    boss_kill = { "on_boss_kill", "Boss-Kills" },
    pet = { "on_pet", "Pets" },
    collection_log = { "on_collection_log", "Collection-Log" },
    combat_achievement = { "on_combat_achievement", "Combat-Achievements" },
    death = { "on_death", "Deaths" },
    treasure_trail = { "on_treasure_trail", "Clue-Scroll-Rewards" },
    duel_end = { "on_duel_end", "Duels" },
}

-- The api table is one object for the script's whole life, so the describe and
-- the op handler reach it without a fresh closure per describe run.
local API
local pending = {}
local lit_key, lit_frames = nil, 0
local gallery_refresh_soon

local function slug(text)
    if not text or text == "" then return "" end
    local out = text:gsub("[^A-Za-z0-9]+", "-"):gsub("^%-+", ""):gsub("%-+$", "")
    return #out > 40 and out:sub(1, 40) or out
end

local function folder(api, category)
    local out = api.config.destination
    local player = api.world.local_player()
    if player and player.name ~= "" then
        local who = slug(player.name)
        if who ~= "" then out = (out ~= "" and out .. "/" or "") .. who end
    end
    if category then out = (out ~= "" and out .. "/" or "") .. category end
    return out
end

local function filename(api, ev)
    local name = slug(ev.subject)
    if name == "" then name = ev.kind end
    if ev.value and ev.value >= 0 then name = name .. "-" .. ev.value end
    return name .. "_" .. (api.client.datestamp() or "unknown") .. ".png"
end

local function capture(api, name, directory)
    local ok, path = api.assets.screenshot(directory, name)
    if not ok then
        api.core.log("screenshot failed:", path)
        return
    end
    api.core.log("captured", path)
    api.core.notify("Screenshot saved: " .. path)
    -- The write is queued behind the frame that takes it, so the gallery
    -- asks again a few ticks later rather than now.
    gallery_refresh_soon()
end

local function capture_now(api)
    capture(api, "screenshot_" .. (api.client.datestamp() or "unknown") .. ".png",
        folder(api, nil))
end

-- The press: motion, not structure, so it takes the direct path and never
-- re-runs the describe. on_frame_start puts it back.
--
-- `key` is the ITEM's key whichever node the engine dispatched from: a
-- REPLACE's hit box is a second node of the same item, so a press on the dead
-- ring lights the same picture a press on the art does.
local function on_camera_op(key)
    lit_key, lit_frames = key, PRESS_FRAMES
    API.porcelain.set(key, { opacity = PRESSED_OPACITY })
    capture_now(API)
end

-- One camera item. Porcelain owns the image handle; its size is the only
-- thing the layer has no verb for, so it is read through the same handle.
local function camera(key, image, place)
    local ref = API.porcelain.image(image)
    if not ref then return nil end
    local width, height = API.assets.image_size(ref)
    if not width then return nil end
    return { key = key, image = image, w = width, h = height, opacity = IDLE_OPACITY,
             place = place, op_label = OPERATION, hit = true, on_op = on_camera_op }
end

-- WITHIN, not INSIDE: a CHILD of the viewport in the viewport's own
-- coordinates, reading corner and offsets identically but taking NO anchor.
-- INSIDE's sibling-plus-anchor is what REPLACE needs and an ornament does not.
-- The resolved box is the same either way -- 482,306,28,26 on classic548 --
-- and so, MEASURED, is the frame time: the difference is 0.32 ms under a
-- 0.73 ms noise floor. The 13.5 ms this mode used to be justified by belongs
-- to an anchor implementation that no longer exists; see PORCELAIN_WITHIN in
-- torirs_plugin_api.h. An ornament that needs no ordering should still not
-- state one.
-- THE PLATE THE REPLACEMENT STANDS ON.
--
-- REPLACE consumes the target's whole record set, and what that set contains
-- is not the same on every root. On classic548/161 the plate under the caption
-- belongs to the PARENT strip, so replacing the button drops the camera into a
-- stone hollow that was already there. On modern164 the button's own subtree
-- paints the plate, so replacing it left a 79x23 hole with a 20x16 camera
-- floating in the middle of it.
--
-- The two are told apart by ONE ANSWER, and it is the engine's:
-- `element.paints_own_art` is whether this element or anything below it paints
-- a picture, which is exactly what a REPLACE of it consumes. A plate goes in
-- where that is true and NOWHERE ELSE -- on a lane whose strip already drew
-- the hollow, a plate of our own is a slab laid over the 2004 stone, which is
-- the defect this plugin shipped for a morning.
--
-- Nothing else in the state answers it. element.graphic_token is 0 on ALL FOUR
-- toplevels (the art is on a child, and the field is a change token rather
-- than an identity), `presented` is true on all four (the BUTTON is presented
-- on every lane; it is its art child that the classic frame puts away), and
-- `facets` is a world-element word that no widget adapter fills. MEASURED, the
-- art child: classic548 false, classic161 false, modern164 true, native548
-- true -- the 2004 frame provider hides the eight filter plates and draws
-- their hollows into its own chat-bar rock, and the lane's own 548 does not.
--
-- THE PLATE IS THE LANE'S OWN BUTTON SPRITE, not a palette. It was a palette
-- for one morning -- five tones sampled off a screenshot of the hollow and
-- ramped across the box by hand -- and that is what a hand-mixed plate always
-- looks like next to the real thing: a flat slab where the neighbours are
-- pictures. The picture already exists. `chat_button.png` beside this file is
-- cache sprite 3051, what the chatbox draws under every one of its eight
-- filters, transparent at the four clipped corners and nowhere else: a button
-- with NO background baked into it. @see SOURCES.sh.
local PLATE_ART = "chat_button.png"
-- The ends the three-slice must copy rather than resample. MEASURED on the
-- sprite's own alpha (56x22): each corner is clipped over FOUR rows, 4 columns
-- off the outermost and then 2, 1, 1, and the rest of the picture is opaque. A
-- cap smaller than that resamples the clip and turns a stepped corner into a
-- ragged diagonal; a cap larger than half the box leaves nothing between the
-- two ends to stretch, which is the case the slice below answers first.
local PLATE_CAP_X = 5
local PLATE_CAP_Y = 4

-- The sprite's pixels, read once and kept. `porcelain.derived` calls the paint
-- function from inside the layer's own fence, so the read happens HERE, at
-- describe time, and the callback below only indexes what this already holds.
local plate_src, plate_src_w, plate_src_h

local function plate_source()
    if plate_src then return true end
    local ref = API.porcelain.image(PLATE_ART)
    if not ref then return false end
    local width, height = API.assets.image_size(ref)
    if not width or width <= 2 * PLATE_CAP_X or height <= 2 * PLATE_CAP_Y then return false end
    local pixels = API.assets.image_pixels(ref)
    if not pixels then return false end
    plate_src, plate_src_w, plate_src_h = pixels, width, height
    return true
end

-- One axis of the three-slice: which source column (or row) an output one
-- shows. The outermost `cap` are copied one to one from each end so the clip
-- and the bevel survive at whatever box the lane reports, and what is between
-- them is resampled. A box with no room for both caps has no slice to make and
-- takes the source straight, clamped.
local function plate_slice(i, out_n, src_n, cap)
    if out_n <= 2 * cap then return i < src_n and i or src_n - 1 end
    if i < cap then return i end
    if i >= out_n - cap then return src_n - (out_n - i) end
    return cap + ((i - cap) * (src_n - 2 * cap)) // (out_n - 2 * cap)
end

-- One plate, as the w*h table of ARGB cells porcelain.derived wants back.
-- Painted at most once per (key, source, size): the layer hashes the inputs,
-- and the inputs name the sprite so a plate cut from one can never be handed
-- back for another.
local function plate_pixels(w, h)
    local out = {}
    for y = 0, h - 1 do
        local row = y * w
        local source_row = plate_slice(y, h, plate_src_h, PLATE_CAP_Y) * plate_src_w
        for x = 0, w - 1 do
            out[row + x + 1] =
                plate_src[source_row + plate_slice(x, w, plate_src_w, PLATE_CAP_X) + 1]
        end
    end
    return out
end

local function corner_item(where)
    return camera("camera", "camera.png",
        { kind = "within", on = "viewport", corner = CORNERS[where], dx = MARGIN, dy = MARGIN })
end

-- Both icons and the plate sprite, held from the start and touched on every
-- describe run, whichever mode is live: the layer releases an image no run
-- asked for, and a camera whose mode changes must not pay a decode to come
-- back. The plate is held here as well as read in plate_source -- the read
-- happens once and the HOLD has to happen every run, or the sprite the paint
-- callback indexes is one the layer has let go.
local function hold_icons()
    API.porcelain.image("camera.png")
    API.porcelain.image("camera_small.png")
    API.porcelain.image(PLATE_ART)
end

-- The derived plate's name. A name and not a handle: the layer re-reads it at
-- every fence and the description has to outlive the call that made it.
local PLATE_KEY = "camera_plate.png"

-- --------------------------------------------------------------- the gallery
--
-- One page of the rail: where the pictures are, a button that hands them to
-- the player, an album picker, and the pictures themselves two to a row.
--
-- LAZY BY THE PAGE, NOT BY THE SCROLL. The presenter draws every dirty custom
-- well whether or not it is scrolled into view, and keeps what it drew; so a
-- well-per-picture gallery would read every picture on its first frame. A
-- page is PER_PAGE pictures, and only the page on screen is ever asked for --
-- half the host's 24-thumbnail cache, so the page just left is still warm.
-- A picture on no page anybody turned to is never read.
--
-- Every platform verb is a ticket that lands on a later frame (a browser's
-- storage is async, a desktop chooser is the player's to close), polled from
-- on_frame_start only while one is outstanding.

local PER_PAGE = 12
local COLUMNS = 2
local ROWS_PER_PAGE = PER_PAGE // COLUMNS
-- The capture is the client canvas, 765x503; this is it at 0.18, which puts
-- both columns inside the 320-wide pane with a gap between them. The box is
-- the cache key, so it is one size and never derived from a measured width.
local THUMB_W, THUMB_H = 136, 90
local CAPTION_H = 16
local CELL_GAP = 6
local ROW_H = THUMB_H + 4 + CAPTION_H + CELL_GAP
-- Server ticks between a capture and the listing that should find it: the
-- PNG is encoded at the end of the capturing frame and written by a task
-- after that.
local REFRESH_TICKS = 3
local ALBUM_ALL = "*"
local MANUAL = "Manual"

local WELL_RGB, FRAME_RGB = 0x1e1a14, 0x3e3529
local CAPTION_RGB, DIM_RGB = 0xff981f, 0x9f9f9f

-- The folders on_game_event files into, so a listing can tell an album from
-- a player's name: Player/Levels/x.png and Player/x.png are both two deep.
local ALBUMS = {}
for _, kind in pairs(KINDS) do ALBUMS[kind[2]] = true end

local gallery

local function gallery_reset()
    gallery = {
        store = "none", root = "",
        items = {}, albums = {}, dirs = {},
        album = ALBUM_ALL, page = 1,
        state = "idle", note = "",
        list_ticket = nil, relist = false, open_ticket = nil, pick_ticket = nil,
        refresh_in = 0,
        shown_view = nil,
        -- path -> state, for the pictures on the page in view; what the
        -- describe hashes, and what on_frame_start polls while any is pending.
        thumb_state = {},
        waiting = false,
        width = {},
        -- Bumped by every change either face can show. The describe re-runs
        -- for the camera's sake too (an element moved, a config note), and
        -- the rows it made last time are the answer then.
        rev = 0, rows = nil, rows_rev = -1,
    }
end
gallery_reset()

local function plural(n, one, many)
    return n .. " " .. (n == 1 and one or many)
end

-- A string as a small integer, for paint_key and hit_key: equal strings get
-- equal numbers and different ones never share one. Interned rather than
-- hashed -- a describe asks for one per grid row, and a per-character hash of
-- two paths a row was most of the describe's cost. The table is forgotten
-- past 512 entries; the counter never is, so a number is never reused.
local key_ids, key_count, key_next = {}, 0, 0

local function key_id(text)
    local id = key_ids[text]
    if not id then
        if key_count >= 512 then key_ids, key_count = {}, 0 end
        key_next = key_next + 1
        id = key_next
        key_ids[text], key_count = id, key_count + 1
    end
    return id
end

local function parse_capture(path)
    local parts = {}
    for part in path:gmatch("[^/]+") do parts[#parts + 1] = part end
    local file = parts[#parts]
    local album, player = MANUAL, ""
    if #parts >= 2 and ALBUMS[parts[#parts - 1]] then
        album = parts[#parts - 1]
        player = #parts >= 3 and parts[#parts - 2] or ""
    elseif #parts >= 2 then
        player = parts[#parts - 1]
    end
    local stamp = file:match("(%d%d%d%d%-%d%d%-%d%d_%d%d%-%d%d%-%d%d)") or ""
    local title = file:gsub("%.[Pp][Nn][Gg]$", "")
    if stamp ~= "" then title = title:gsub("_?" .. stamp:gsub("%-", "%%-") .. "$", "") end
    title = title:gsub("%-", " ")
    if title == "" or title == "screenshot" then title = "Screenshot" end
    local when = ""
    if stamp ~= "" then
        when = stamp:sub(9, 10) .. "/" .. stamp:sub(6, 7) .. " " .. stamp:sub(12, 13) .. ":"
            .. stamp:sub(15, 16)
    end
    return { path = path, album = album, player = player, title = title, stamp = stamp,
             when = when, dir = path:match("^(.*)/[^/]*$") or "" }
end

local function album_key(item) return item.album .. "|" .. item.player end

-- capture_list's rest: `<path>\t<mtime>\t<bytes>` a line.
local function gallery_set_items(text)
    local items = {}
    for line in text:gmatch("[^\n]+") do
        local path, mtime = line:match("^([^\t]+)\t?(%-?%d*)")
        if path then
            local item = parse_capture(path)
            item.mtime = tonumber(mtime) or 0
            items[#items + 1] = item
        end
    end
    -- Newest first. The stamp in the name is the time it was TAKEN and is
    -- there on every store; mtime is a desktop's alone, so it only breaks ties.
    table.sort(items, function(a, b)
        if a.stamp ~= b.stamp then return a.stamp > b.stamp end
        if a.mtime ~= b.mtime then return a.mtime > b.mtime end
        return a.path < b.path
    end)
    local by_key, albums, players, dirs, by_dir = {}, {}, {}, {}, {}
    for _, item in ipairs(items) do
        local key = album_key(item)
        if not by_key[key] then
            by_key[key] = { value = key, album = item.album, player = item.player, count = 0 }
            albums[#albums + 1] = by_key[key]
        end
        by_key[key].count = by_key[key].count + 1
        players[item.player] = true
        if not by_dir[item.dir] then
            by_dir[item.dir] = { dir = item.dir, count = 0 }
            dirs[#dirs + 1] = by_dir[item.dir]
        end
        by_dir[item.dir].count = by_dir[item.dir].count + 1
    end
    local many_players = next(players) ~= nil and next(players, next(players)) ~= nil
    table.sort(albums, function(a, b)
        if a.album ~= b.album then return a.album < b.album end
        return a.player < b.player
    end)
    for _, album in ipairs(albums) do
        local name = album.album:gsub("%-", " ")
        album.label = (many_players and album.player ~= "" and name .. " - " .. album.player or name)
            .. " (" .. album.count .. ")"
    end
    table.sort(dirs, function(a, b) return a.dir < b.dir end)
    gallery.items, gallery.albums, gallery.dirs = items, albums, dirs
    if gallery.album ~= ALBUM_ALL and not by_key[gallery.album] then gallery.album = ALBUM_ALL end
end

local function gallery_filtered()
    if gallery.album == ALBUM_ALL then return gallery.items end
    local out = {}
    for _, item in ipairs(gallery.items) do
        if album_key(item) == gallery.album then out[#out + 1] = item end
    end
    return out
end

local function gallery_pages(items)
    return math.max(1, (#items + PER_PAGE - 1) // PER_PAGE)
end

-- Everything that can change what either face shows. The page is the
-- layer's to reconcile; the settings face is a plain build, asked again.
local function gallery_changed()
    gallery.rev = gallery.rev + 1
    API.porcelain.invalidate()
    if gallery.shown_view == "settings" then API.panel.invalidate() end
end

local function gallery_root()
    gallery.store, gallery.root = API.assets.capture_root(API.config.destination)
end

local function gallery_list()
    if gallery.store == "none" then return end
    if gallery.list_ticket then
        gallery.relist = true
        return
    end
    local ticket, result = API.assets.capture_list(API.config.destination)
    if ticket then
        gallery.list_ticket = ticket
        if gallery.state ~= "ready" then gallery.state = "loading" end
    else
        gallery.state, gallery.note = "failed", "Could not look for pictures (" .. result .. ")."
    end
    gallery_changed()
end

gallery_refresh_soon = function()
    gallery.refresh_in = REFRESH_TICKS
end

-- Hand the pictures to the player: the folder, or one picture, on a desktop;
-- a download in a browser.
local function gallery_open(item)
    local ticket, result = API.assets.capture_open(API.config.destination, item and item.path)
    if not ticket then
        gallery.note = "Could not open them (" .. result .. ")."
    else
        gallery.open_ticket = ticket
        gallery.note = gallery.store == "browser"
            and (item and "Downloading " .. item.title .. "..." or "Preparing the download...")
            or ""
    end
    gallery_changed()
end

local function gallery_set_destination(folder)
    -- The host refuses a destination it cannot save under, and capture_root
    -- is where that answer is cheapest to ask for: before it is stored.
    if folder ~= "" and API.assets.capture_root(folder) == "none" then
        gallery.note = "That folder cannot be used: its path has characters a "
            .. "screenshot cannot be saved under."
        gallery_changed()
        return
    end
    API.config.set("destination", folder)
    gallery.note = ""
    gallery_root()
    gallery.items, gallery.albums, gallery.dirs, gallery.state = {}, {}, {}, "idle"
    gallery.album, gallery.page = ALBUM_ALL, 1
    gallery_list()
end

local function gallery_pick_folder()
    local ticket, result = API.assets.pick_folder(gallery.root)
    if ticket then gallery.pick_ticket = ticket
    else gallery.note = "Could not open the folder chooser (" .. result .. ")." end
    gallery_changed()
end

local function gallery_poll()
    if gallery.list_ticket then
        local state, verdict, rest = API.assets.capture_answer(gallery.list_ticket)
        if state ~= "pending" then
            gallery.list_ticket = nil
            if state == "ready" and verdict == "ok" then
                gallery_set_items(rest)
                gallery.state = "ready"
            else
                gallery.state = "failed"
                gallery.note = "Could not look for pictures: " .. (rest or "the answer was lost") .. "."
            end
            gallery_changed()
            if gallery.relist then
                gallery.relist = false
                gallery_list()
            end
        end
    end
    if gallery.open_ticket then
        local state, verdict, rest = API.assets.capture_answer(gallery.open_ticket)
        if state ~= "pending" then
            gallery.open_ticket = nil
            if state ~= "ready" then gallery.note = "The request was lost."
            elseif verdict == "ok" then
                gallery.note = gallery.store == "browser" and "Downloaded." or ""
            else gallery.note = rest ~= "" and rest or verdict end
            gallery_changed()
        end
    end
    if gallery.pick_ticket then
        local state, verdict, rest = API.assets.capture_answer(gallery.pick_ticket)
        if state ~= "pending" then
            gallery.pick_ticket = nil
            if state == "ready" and verdict == "ok" then
                gallery_set_destination((rest:match("^[^\n]+") or ""))
            elseif state == "ready" and verdict ~= "cancelled" then
                gallery.note = rest ~= "" and rest or verdict
                gallery_changed()
            end
        end
    end
end

-- The picture in column `c` of grid row `r` on the page in view, or nil.
local function gallery_cell(items, r, c)
    return items[(gallery.page - 1) * PER_PAGE + (r - 1) * COLUMNS + c]
end

local function thumb(item)
    return API.assets.capture_image(API.config.destination, item.path, THUMB_W, THUMB_H)
end

-- What the grid row will draw, as one number: which pictures, and which of
-- them have landed. A change to it is one redraw of that row and no other.
local function gallery_row_key(items, r)
    local parts = {}
    for c = 1, COLUMNS do
        local item = gallery_cell(items, r, c)
        if item then
            parts[#parts + 1] = item.path .. "=" .. (gallery.thumb_state[item.path] or "?")
        end
    end
    return key_id(table.concat(parts, "|"))
end

local function gallery_paint(key, draw)
    local items = gallery_filtered()
    local r = tonumber(key:match("%d+$"))
    local ctx = draw.context()
    local width = ctx and ctx.bounds.width or COLUMNS * (THUMB_W + 8) + CELL_GAP
    local col_w = (width - CELL_GAP * (COLUMNS - 1)) // COLUMNS
    gallery.width[key] = width
    for c = 1, COLUMNS do
        local item = gallery_cell(items, r, c)
        if item then
            local x = (c - 1) * (col_w + CELL_GAP)
            local box_h = THUMB_H + 4
            draw.rect(x, 0, col_w, box_h, WELL_RGB)
            draw.rect(x, 0, col_w, box_h, FRAME_RGB, 0)
            local ref, state = thumb(item)
            gallery.thumb_state[item.path] = state
            if ref then
                local iw, ih = API.assets.image_size(ref)
                draw.image(ref, x + (col_w - iw) // 2, 2 + (THUMB_H - ih) // 2)
            else
                gallery.waiting = gallery.waiting or state == "pending"
                draw.text(x + 8, box_h // 2 - 6,
                    state == "pending" and "Loading..." or "Cannot be shown", DIM_RGB)
            end
            -- About six pixels a glyph in this face: cut rather than overrun
            -- into the next column.
            local caption = item.title
            local room = (col_w - 4) // 6 - #item.when - 1
            if #caption > room then caption = caption:sub(1, math.max(room - 2, 1)) .. ".." end
            draw.text(x + 2, box_h + 2, caption, CAPTION_RGB)
            if item.when ~= "" then
                draw.text(x + col_w - 2 - #item.when * 6, box_h + 2, item.when, DIM_RGB)
            end
        end
    end
end

local function gallery_row_action(key, action)
    if action.action ~= "activate" then return end
    local width = gallery.width[key]
    if not width then return end
    local c = math.min(COLUMNS, action.x * COLUMNS // math.max(width, 1) + 1)
    local item = gallery_cell(gallery_filtered(), tonumber(key:match("%d+$")), c)
    if item then gallery_open(item) end
end

local function gallery_status(items, pages)
    if gallery.store == "none" then return "This run keeps no files, so nothing is saved." end
    if gallery.state == "failed" then return gallery.note end
    if #gallery.items == 0 then
        return gallery.state == "ready" and "No pictures yet. They appear here as they are taken."
            or "Looking for pictures..."
    end
    local status = plural(#items, "picture", "pictures")
    if pages > 1 then status = status .. ", page " .. gallery.page .. " of " .. pages end
    return status
end

-- The page face's rows. Always the same sequence -- six grid rows whether or
-- not the page fills them, an empty one at height 0 -- so turning a page or
-- changing album is setters on rows that exist, never a rebuild of the page.
local function describe_gallery(d)
    if gallery.rows_rev == gallery.rev then
        for _, row in ipairs(gallery.rows) do d.row(row) end
        return
    end
    local rows = {}
    local real_d = d
    d = { row = function(row) rows[#rows + 1] = row; real_d.row(row) end }
    gallery.rows, gallery.rows_rev = rows, gallery.rev
    local items = gallery_filtered()
    local pages = gallery_pages(items)
    if gallery.page > pages then gallery.page = pages end
    local browser = gallery.store == "browser"

    d.row({ key = "where", kind = "key_value", label = browser and "Saved in" or "Folder",
            text = browser and "This browser's storage"
                or (gallery.store == "folder" and gallery.root or "Nowhere") })
    d.row({ key = "open", kind = "button",
            text = browser and "Download all (.zip)" or "Open folder",
            disabled = gallery.store == "none" or (browser and #gallery.items == 0),
            on_action = function() gallery_open(nil) end })
    local options = { { value = ALBUM_ALL, label = "All (" .. #gallery.items .. ")" } }
    for _, album in ipairs(gallery.albums) do
        options[#options + 1] = { value = album.value, label = album.label }
    end
    d.row({ key = "album", kind = "select", label = "Album", text = gallery.album,
            options = options,
            on_action = function(_, action)
                if action.text ~= "" and action.text ~= gallery.album then
                    gallery.album, gallery.page = action.text, 1
                    gallery_changed()
                end
            end })
    d.row({ key = "status", kind = "label", text = gallery_status(items, pages) })
    local note = gallery.state ~= "failed" and gallery.note or ""
    d.row({ key = "note", kind = "label", text = note })

    -- Which pictures are on screen, and which of those have landed: asked
    -- here so the paint key can say so, and so a page nobody turned to is
    -- never read.
    local seen = {}
    for r = 1, ROWS_PER_PAGE do
        for c = 1, COLUMNS do
            local item = gallery_cell(items, r, c)
            if item then
                local _, state = thumb(item)
                seen[item.path] = state
            end
        end
    end
    gallery.thumb_state = seen
    gallery.waiting = false
    for _, state in pairs(seen) do gallery.waiting = gallery.waiting or state == "pending" end

    for r = 1, ROWS_PER_PAGE do
        local filled = gallery_cell(items, r, 1) ~= nil
        d.row({ key = "grid" .. r, kind = "custom", height = filled and ROW_H or 0,
                hit_key = key_id(gallery.album .. "#" .. gallery.page .. "#" .. r),
                paint_key = gallery_row_key(items, r),
                paint = gallery_paint, on_action = gallery_row_action })
    end
    d.row({ key = "newer", kind = "button", text = "Newer", disabled = gallery.page <= 1,
            on_action = function()
                gallery.page = math.max(1, gallery.page - 1)
                gallery_changed()
            end })
    d.row({ key = "older", kind = "button", text = "Older", disabled = gallery.page >= pages,
            on_action = function()
                gallery.page = math.min(pages, gallery.page + 1)
                gallery_changed()
            end })
    d.row({ key = "refresh", kind = "button", text = "Refresh",
            disabled = gallery.store == "none", on_action = function() gallery_list() end })
end

-- The settings face. Porcelain declares nothing here (the description covers
-- the page), so these rows sit above the form the host generates from the
-- config schema, and their actions come back through on_ui_action.
local function build_settings(panel)
    panel.heading("Where pictures are saved")
    if gallery.store == "folder" then
        panel.key_value("where", "Folder", gallery.root)
        panel.button("choose", "Choose folder...", gallery.pick_ticket == nil)
        panel.button("default", "Use the default folder", API.config.destination ~= "")
        panel.button("reveal", "Open folder", true)
    elseif gallery.store == "browser" then
        -- A browser has no folder to choose. What it has is a database, and
        -- the pictures in it are laid out the way a desktop's folder would
        -- be: say so, as the tree it is.
        panel.key_value("where", "Saved in", "This browser's storage")
        panel.paragraph("Kept in this browser's IndexedDB storage (the files store), "
            .. "under the paths below. Clearing the site's data deletes them; "
            .. "download them to keep a copy.")
        panel.key_value("tree", gallery.root .. "/", plural(#gallery.items, "picture", "pictures"))
        for i, dir in ipairs(gallery.dirs) do
            if dir.dir ~= "" then
                local depth = select(2, dir.dir:gsub("/", "")) + 1
                local name = dir.dir:match("([^/]+)$")
                panel.key_value("tree" .. i, string.rep("  ", depth) .. name .. "/",
                    plural(dir.count, "picture", "pictures"))
            end
        end
        panel.button("download", "Download all (.zip)", #gallery.items > 0)
    else
        panel.paragraph("This run keeps no files, so pictures are not saved anywhere.")
    end
    if gallery.note ~= "" then panel.label("note", gallery.note) end
end

local SETTINGS_ACTIONS = {
    choose = function() gallery_pick_folder() end,
    default = function() gallery_set_destination("") end,
    reveal = function() gallery_open(nil) end,
    download = function() gallery_open(nil) end,
}

local function describe_camera(d)
    local where = API.config.camera
    if where == "off" then return end
    local item
    if where == "report-button" then
        -- PENDING is not absent: the element resolves at a later fence and the
        -- layer defers the item until it does. ABSENT is a lane fact -- no
        -- chat-filter art at all -- already reported on the findings channel;
        -- the camera goes to a corner rather than nowhere.
        local report = API.porcelain.element("report_button")
        if report.bind ~= "absent" and report.box.width > 0 and report.box.height > 0 then
            local bw, bh = report.box.width, report.box.height
            -- The sprite before the plate that is cut from it: a describe that
            -- reached porcelain.derived without it would hand the layer a
            -- paint callback with nothing to index. The read is one decode,
            -- once, and every later run takes the early answer.
            local state
            if report.paints_own_art and plate_source() then
                local _, derived = API.porcelain.derived(
                    PLATE_KEY, PLATE_ART .. " " .. bw .. "x" .. bh, bw, bh, plate_pixels)
                state = derived
            end
            if state == "ready" then
                -- The plate takes the target's place at the target's own size,
                -- so nothing of what REPLACE removed is left as a hole. It
                -- carries no hit box: the camera above it owns the press.
                d.control({ key = "camera_plate", image = PLATE_KEY, w = bw, h = bh,
                            place = { kind = "replace", on = "report_button" } })
                -- INSIDE, so the camera is anchored OVER the button and lands
                -- after the plate in the same unit's draw order.
                item = camera("camera_report", "camera_small.png",
                    { kind = "inside", on = "report_button", corner = "centre" })
            elseif not report.paints_own_art then
                -- NO PLATE, because the target paints no picture to lose: the
                -- camera itself takes the records, and what shows behind it is
                -- the hollow the strip was already drawing. This is the shape
                -- the plugin had before it grew a plate, kept for the lane it
                -- was always right on.
                item = camera("camera_report", "camera_small.png",
                    { kind = "replace", on = "report_button" })
            end
        else
            item = corner_item("bottom-right")
        end
    else
        item = corner_item(where)
    end
    if item then d.control(item) end
end

local function describe(d)
    hold_icons()
    describe_gallery(d)
    describe_camera(d)
end

local function tick_pending()
    if gallery.refresh_in > 0 then
        gallery.refresh_in = gallery.refresh_in - 1
        if gallery.refresh_in == 0 then gallery_list() end
    end
    local keep = {}
    for _, shot in ipairs(pending) do
        shot.ticks_left = shot.ticks_left - 1
        if shot.ticks_left <= 0 then capture(API, shot.name, shot.directory)
        else keep[#keep + 1] = shot end
    end
    pending = keep
end

local function wanted(api, ev)
    local kind = KINDS[ev.kind]
    if not kind or not api.config[kind[1]] then return nil end
    if ev.kind == "valuable_drop" and ev.value >= 0 and
        ev.value < api.config.min_drop_value then return nil end
    return kind[2]
end

function plugin.on_start(api)
    API = api
    pending = {}
    lit_key, lit_frames = nil, 0
    gallery_reset()
    assert(api.porcelain.open(), "screenshot needs the porcelain layer")
    -- The page face is the layer's; the settings face is built below, so
    -- the host's generated form keeps the event switches and the camera.
    api.porcelain.panel({ icon_asset = "panel_icon.png", width = 320, faces = "page" })
    hold_icons()
    api.porcelain.describe(describe)
    api.porcelain.every_server_tick(tick_pending)
    gallery_root()
    gallery_list()
end

function plugin.on_config_changed(api, key)
    if key == "destination" then
        gallery_root()
        gallery_list()
        gallery_changed()
    end
end

function plugin.on_ui_build(api, panel, view)
    gallery.shown_view = view
    if view == "settings" then
        build_settings(panel)
        return
    end
    api.porcelain.panel_build(view)
end

function plugin.on_ui_action(api, ev)
    if api.porcelain.panel_action(ev) then return end
    local act = SETTINGS_ACTIONS[ev.id]
    if act and ev.action == "activate" then act() end
end

function plugin.on_ui_draw(api, node)
    api.porcelain.panel_draw(node)
end

-- The pump calls this between its fence and its commit, so the set() below is
-- inside the frame the layer opened -- where a direct-motion write belongs.
function plugin.on_frame_start(api)
    if gallery.list_ticket or gallery.open_ticket or gallery.pick_ticket then gallery_poll() end
    -- A picture on the page in view still being read: ask the cache again,
    -- which costs a lookup, and redescribe only when one has landed.
    if gallery.waiting then
        for path, state in pairs(gallery.thumb_state) do
            if state == "pending" then
                local _, now = API.assets.capture_image(
                    api.config.destination, path, THUMB_W, THUMB_H)
                if now ~= "pending" then
                    gallery.waiting = false
                    gallery_changed()
                    break
                end
            end
        end
    end
    if lit_frames > 0 then
        lit_frames = lit_frames - 1
        if lit_frames == 0 then
            api.porcelain.set(lit_key, { opacity = IDLE_OPACITY })
            lit_key = nil
        end
    end
end

function plugin.on_game_event(api, ev)
    local category = wanted(api, ev)
    if not category then return end
    local shot = {
        ticks_left = api.config.delay_ticks,
        name = filename(api, ev),
        directory = folder(api, category),
    }
    if shot.ticks_left <= 0 then
        capture(api, shot.name, shot.directory)
    else
        pending[#pending + 1] = shot
    end
end

-- Raw, not Porcelain_KeyEdge: that verb names five modifier keys by string and
-- polls key_held at the fence, and this hotkey is an arbitrary key code read
-- on the press edge.
function plugin.on_key(api, ev)
    local hotkey = api.config.hotkey
    if hotkey ~= 0 and ev.down and ev.key == hotkey then capture_now(api) end
end

function plugin.on_stop()
    -- The layer's close removes the control and releases the image; the host
    -- calls it after this handler returns.
    pending = {}
    lit_key, lit_frames = nil, 0
    gallery_reset()
end

return plugin
