-- quest-driver / waves: enter a wave minigame at a wave the way the content's
-- own debugproc builds it (`::inferno <wave>`; the Colosseum's equivalent),
-- and read the wave number, the alive count, the pillars or modifiers, paused
-- or not (docs/WAVES_ORCHESTRATOR.md section 5, rows `wave.enter / wave.state`
-- and `pause and logout`).  Waves seam pass 2 (wave_enter_state_pause,
-- docs/minigames/waves_loop/SEAM_TRIAGE_2026-10-03b.md).
-- It stands where the raid branch's raid.lua stood (the raid loop's t.raid.*
-- enters ToB/ToA/CoX rooms through debugprocs only that branch's content has,
-- so it was not copied: docs/minigames/waves_loop/FORKED_FROM.md).
-- The namespace QD.wave is declared in core.lua; only core.lua declares
-- chunk-scope locals, so every helper here hangs off QD.wave.
--
--   t.wave.enter(game, wave[, opts])   -> ok | refused | unsupported | no_row | timeout, detail, state
--   t.wave.state([game])               -> ok | unsupported | no_row | <var read failure>, detail, state
--   t.wave.await_wave(n, ticks[, game])-> ok | timeout | ..., detail, state
--   t.wave.await_clear(ticks[, game])  -> ok | timeout | ..., detail, state
--   t.wave.pause([opts])               -> ok | refused | unsupported | not_found | not_visible | timeout, detail
--   t.wave.resume([opts])              -> ok | refused | timeout | <click/choose failure>, detail, state
--
-- Every tick in a detail is the EMBEDDED SERVER's tick (t.tick(), srv->tick,
-- the clock of every t.ticklog row) unless it says "drive tick"; the deadline
-- arguments (`ticks`) are drive ticks, the clock t.ticks counts.
--
-- WHAT THE STATE IS.  The Inferno keeps its run in player varps
-- (minigame_inferno/configs/inferno.varp): all of them sit above the cache's
-- varp range (5725), so the client has no copy and QD.var.server answers off
-- the embedded server's own (api_drive.var_content; state.lua's
-- QD._var_server_varp).  `state` reads them every call; nothing is cached.
--
-- WHAT ENTER STARTS.  `[debugproc,inferno](int $wave)` (minigame_inferno/
-- scripts/inferno.rs2:413) clamps the wave to 1..69, leaves a running run,
-- marks the fire cape sacrificed and calls `~inferno_enter($wave, 1)`: the
-- second argument is PRACTICE.  So every run `enter` makes is a practice run,
-- and a practice run is ONE wave: when its last npc dies the content prints
-- "Practice wave complete." and leaves (inferno_waves.rs2, `inferno_npc_died`,
-- the `%varp6038_inferno_practice = 1` branch).  A real run is entered by
-- clicking the Cave entrance (`[oploc1,inferno_entrance_op]`, inferno.rs2:273),
-- which the entry unit's test does itself with t.player.click_loc.  `enter` is
-- a bring-along: it refuses when a run is already active unless
-- `opts.restart` says to replace it (the debugproc's own ~inferno_leave).
--
-- PAUSE.  The real game pauses on the LOGOUT BUTTON: "You can pause the
-- minigame between waves by pressing the logout button, which sends a pause
-- request that will take place at the end of the wave. Pressing it again
-- (provided that you're not in combat) will reset the current wave. ... To
-- continue the run, log out and back in" (docs/minigames/inferno/sources/
-- wiki/wiki_Inferno_Strategies.wikitext:856).  The content arms its pause
-- request (`~inferno_request_pause`, inferno.rs2:189) only from the arena's
-- Cave exit (`[oploc1,inferno_exit]` / `[oploc2,...]`, inferno.rs2:288,299)
-- and the `::infernopause` cheat; its logout button is
-- `[if_button,logout:logout] p_logout;` (interface_logout/scripts/
-- logout.rs2:21) with no Inferno check, and `[logout,_]` ->
-- `~inferno_on_logout` (inferno.rs2:399) clears the run.  So `pause` presses
-- what the caller names -- `opts.via = "logout"` (default, the real game's
-- button, pressed ONCE: a second press is the real game's wave reset) or
-- `"exit"` (the content's Cave exit, op 1) -- and answers what the server
-- did, never routing through the debugproc.  A logout press that ends the
-- session answers `unsupported` (the content has no pause on that button)
-- and leaves the client on the title screen: t.session.login() next.
--
-- RESUME.  The real game resumes on login (wiki line above).  The content
-- resumes by re-entry: `[login]` -> `~inferno_login` (inferno.rs2:408)
-- prints "Your Inferno run is paused at wave N. Re-enter to resume." and the
-- player stands on the exit pad; the Cave entrance's op 1 offers
-- "Resume the Inferno (wave N)." (inferno.rs2:273-280).  `resume` does that:
-- the click, the choice, and settles on the run active on the saved wave.

QD.wave.SERVER_TICKS_NOTE = "server tick"

-- The games this file knows.  `colosseum` has no content on this branch
-- (docs/WAVES_ORCHESTRATOR.md section 1): every verb answers unsupported for
-- it, naming that, and nothing is guessed.
QD.wave.UNSUPPORTED = {
    colosseum = "the Fortis Colosseum has no content on this branch (no minigame_colosseum/; "
        .. "docs/WAVES_ORCHESTRATOR.md section 1): nothing to enter or read",
}

QD.wave.INFERNO = {
    wave_min = 1,
    wave_zuk = 69,  -- ^inferno_wave_zuk (minigame_inferno/configs/inferno.constant:10)
    cheat = "::inferno",
    vars = {
        active = "varp5889_inferno_active",
        wave = "varp6040_inferno_wave",
        alive = "varp5890_inferno_alive",
        practice = "varp6038_inferno_practice",
        paused = "varp6056_inferno_paused",
        logout_requested = "varp6055_inferno_logout_req",
        saved_wave = "varp6065_inferno_saved_wave",
        death_pending = "varp6256_inferno_death_pending",
    },
    -- Read in this order; the detail prints them w, s, e.
    pillar_order = { "w", "s", "e" },
    pillars = {
        w = { hp = "varp6037_inferno_pillar_w_hp", dead = "varp6036_inferno_pillar_w_dead" },
        s = { hp = "varp6035_inferno_pillar_s_hp", dead = "varp6034_inferno_pillar_s_dead" },
        e = { hp = "varp6033_inferno_pillar_e_hp", dead = "varp6032_inferno_pillar_e_dead" },
    },
    -- The npcs that are wave credit: every type whose death runs
    -- `~inferno_npc_died` (inferno_waves.rs2's [ai_queue3,...] list, plus the
    -- Jad, whose death inferno_jad.rs2 owns).  The pillars
    -- (`inferno_invisible_3x3`) and the Zuk fight's own npcs are not.
    creatures = {
        "inferno_nibbler", "inferno_creature_harpie", "inferno_creature_splitter",
        "inferno_creature_splitter_melee", "inferno_creature_splitter_mage",
        "inferno_creature_splitter_range", "inferno_creature_melee",
        "inferno_creature_melee_small", "inferno_creature_ranger",
        "inferno_creature_mager", "inferno_jad", "inferno_jad_healer",
    },
    exit_loc = "inferno_exit",          -- 30283 "Cave exit", in the arena (m35_83 29,13)
    entrance_loc = "inferno_entrance",  -- 30352 "The Inferno", Mor Ul Rek
    -- The cache places the entrance twice (m38_80 61,4 = 2493,5124 and
    -- m38_79 56,51 = 2488,5107; AV_INVENTORY.md section 4) and the content
    -- walks players to the second (^inferno_entrance_walk, inferno.constant:29),
    -- but the client's scene holds only the first (ws2_wave_b3 row 8: "nearest
    -- copies: 2493,5124,0"), and from the exit pad no walk reaches it (walks
    -- end at 2496,5119, the server says "I can't reach that!": ws2_wave_b2
    -- row 8).  So resume presses the nearest copy unless opts.at names one,
    -- and answers the server's refusal as it comes.
    resume_choice = "/^Resume the Inferno/",
}

-- Defaults, in drive ticks.  The wave starts ^inferno_wave_delay = 8 server
-- ticks after the enter (inferno.constant:11) and ^inferno_resume_delay = 16
-- after a resume (inferno.constant:426); the budgets carry room for the
-- instance build and the client's pool catching up.
QD.wave.ENTER_TICKS = 30
QD.wave.RESUME_TICKS = 40
QD.wave.PAUSE_TICKS = 10
QD.wave.CHOICE_TICKS = 15

-- The game name -> its table, or (nil, result, detail).
function QD.wave._game(game)
    game = game or "inferno"
    assert(type(game) == "string", "t.wave: game must be a string such as \"inferno\"")
    if game == "inferno" then
        return QD.wave.INFERNO
    end
    if QD.wave.UNSUPPORTED[game] then
        return nil, "unsupported", "wave: " .. game .. ": " .. QD.wave.UNSUPPORTED[game]
    end
    return nil, "no_row", "wave: no wave minigame named '" .. game .. "' (known: inferno; colosseum unsupported)"
end

-- One var through QD.var.server: (true, value) or (false, result, detail).
function QD.wave._read(name)
    local result, value = QD.var.server(name)
    if result ~= "ok" then
        return false, result, "wave.state: " .. name .. " read " .. tostring(result) .. " " .. tostring(value)
    end
    return true, value
end

function QD.wave._server_tick()
    local result, tick = QD.tick()
    if result == "ok" then
        return tick
    end
    return -1
end

-- The ids of the wave's npc types, resolved once per run.
function QD.wave._creature_ids(spec)
    if spec._ids then
        return spec._ids
    end
    local ids = {}
    for i = 1, #spec.creatures do
        local result, id = api_drive.symbol("npc", spec.creatures[i])
        if result == "ok" then
            local short = string.gsub(spec.creatures[i], "^inferno_creature_", "")
            short = string.gsub(short, "^inferno_", "")
            ids[id] = short
        end
    end
    spec._ids = ids
    return ids
end

-- (count, "nibbler x3, harpie x1") of the wave's npcs in the client's pool.
function QD.wave._pool(spec)
    local result, rows = api_drive.npcs(0)
    if result ~= "ok" or type(rows) ~= "table" then
        return 0, "pool read " .. tostring(result)
    end
    local ids = QD.wave._creature_ids(spec)
    local counts = {}
    local order = {}
    local total = 0
    for i = 1, #rows do
        local name = ids[rows[i].npc_id] or ids[rows[i].base_npc_id]
        if name then
            total = total + 1
            if counts[name] == nil then
                counts[name] = 0
                order[#order + 1] = name
            end
            counts[name] = counts[name] + 1
        end
    end
    local parts = {}
    for i = 1, #order do
        parts[#parts + 1] = order[i] .. " x" .. counts[order[i]]
    end
    return total, (#parts > 0 and table.concat(parts, ", ") or "none")
end

function QD.wave._tile()
    local result, tile = api_drive.player_tile()
    if result ~= "ok" or type(tile) ~= "table" then
        return nil, "?"
    end
    return tile, string.format("%d,%d,%d", tile.x, tile.z, tile.level)
end

-- The one-line reading of a state table.
function QD.wave._text(s)
    local pillars = {}
    for i = 1, #QD.wave.INFERNO.pillar_order do
        local key = QD.wave.INFERNO.pillar_order[i]
        local p = s.pillars[key]
        pillars[#pillars + 1] = key .. (p.dead and " dead" or (" " .. tostring(p.hp)))
    end
    return string.format(
        "%s %s wave %d alive %d (pool %d: %s) %s paused=%d logout_req=%d saved_wave=%d%s pillars %s; %s %d at %s",
        s.game, s.active and "ACTIVE" or "inactive", s.wave, s.alive, s.pool, s.pool_text,
        s.practice and "PRACTICE" or "real", s.paused and 1 or 0, s.logout_requested and 1 or 0,
        s.saved_wave, s.death_pending and " DEATH_PENDING" or "",
        table.concat(pillars, " "), QD.wave.SERVER_TICKS_NOTE, s.tick, s.tile_text)
end

-- t.wave.state([game]) -> ("ok", detail, state) or (result, detail).
--
-- state = { game, active, wave, alive, practice, paused, logout_requested,
--           saved_wave, death_pending, pillars = { w = {hp=, dead=}, s=, e= },
--           pool, pool_text, tick, tile = {x, z, level}, tile_text }
-- booleans for the 0/1 flags, numbers for the rest; `pool` counts the wave's
-- npc types in the client's pool (pillars excluded), `tick` is the server's.
function QD.wave.state(game)
    local spec, fail_result, fail_detail = QD.wave._game(game)
    if not spec then
        return fail_result, fail_detail
    end
    local s = { game = "inferno", pillars = {} }
    for key, name in pairs(spec.vars) do
        local good, value, detail = QD.wave._read(name)
        if not good then
            return value, detail
        end
        s[key] = value
    end
    for i = 1, #spec.pillar_order do
        local key = spec.pillar_order[i]
        local good_hp, hp, hp_detail = QD.wave._read(spec.pillars[key].hp)
        if not good_hp then
            return hp, hp_detail
        end
        local good_dead, dead, dead_detail = QD.wave._read(spec.pillars[key].dead)
        if not good_dead then
            return dead, dead_detail
        end
        s.pillars[key] = { hp = hp, dead = dead == 1 }
    end
    s.active = s.active == 1
    s.practice = s.practice == 1
    s.paused = s.paused == 1
    s.logout_requested = s.logout_requested == 1
    s.death_pending = s.death_pending == 1
    s.pool, s.pool_text = QD.wave._pool(spec)
    s.tick = QD.wave._server_tick()
    s.tile, s.tile_text = QD.wave._tile()
    return "ok", QD.wave._text(s), s
end

-- Lines that arrived after `since` (a message serial), oldest first, joined.
function QD.wave._lines_since(since)
    local result, list = api_drive.messages()
    if result ~= "ok" or type(list) ~= "table" then
        return ""
    end
    local lines = {}
    for i = #list, 1, -1 do
        if list[i].serial > since then
            lines[#lines + 1] = "'" .. list[i].text .. "'"
        end
    end
    return table.concat(lines, " | ")
end

function QD.wave._serial()
    local result, serial = api_drive.message_serial()
    if result ~= "ok" then
        return 0
    end
    return serial
end

-- Poll one var each frame until `want(value)`; returns (true, value, server
-- tick first seen) or (false, last value).  One var per frame keeps the
-- level function cheap; the caller reads the whole state once after.
function QD.wave._await_var(name, want, ticks, note)
    local seen_value = nil
    local seen_tick = nil
    local result = await({
        level = function()
            local r, v = QD.var.server(name)
            if r ~= "ok" then
                return false
            end
            seen_value = v
            if want(v) then
                seen_tick = QD.wave._server_tick()
                return true
            end
            return false
        end,
        note = note,
    }, ticks)
    if result == "ok" and seen_tick then
        return true, seen_value, seen_tick
    end
    return false, seen_value
end

-- t.wave.enter(game, wave[, opts]) -> (result, detail[, state])
--
-- opts.restart = true replaces an active run (the debugproc's ~inferno_leave);
-- opts.ticks = the drive-tick budget for the wave to begin (ENTER_TICKS).
-- `ok` once the server says the run is active on `wave`, the wave has begun
-- (alive > 0) and the client's pool holds at least `alive` of the wave's npcs.
-- Wave 69 (Zuk) has its own start (queue inferno_zuk_start, no alive count):
-- it settles on active + wave only and says so.
function QD.wave.enter(game, wave, opts)
    local spec, fail_result, fail_detail = QD.wave._game(game)
    if not spec then
        return fail_result, fail_detail
    end
    assert(type(wave) == "number", "t.wave.enter: wave must be a number")
    opts = opts or {}
    if wave ~= math.floor(wave) or wave < spec.wave_min or wave > spec.wave_zuk then
        return "refused", string.format(
            "wave.enter: inferno wave %s is outside %d..%d (the debugproc would clamp it; this verb does not)",
            tostring(wave), spec.wave_min, spec.wave_zuk)
    end
    local before_result, before_detail, before = QD.wave.state(game)
    if before_result ~= "ok" then
        return before_result, "wave.enter: state before: " .. tostring(before_detail)
    end
    if before.active and not opts.restart then
        return "refused", "wave.enter: a run is already active (" .. before_detail
            .. "); pass opts.restart to replace it", before
    end
    if before.paused and not opts.restart then
        return "refused", "wave.enter: a paused run is saved at wave " .. before.saved_wave
            .. " and ::inferno clears it (inferno.rs2:424); pass opts.restart to discard it", before
    end
    local tick0 = QD.wave._server_tick()
    local cheat_text = spec.cheat .. " " .. tostring(wave)
    local cheat_result, cheat_detail = QD.cheat(cheat_text)
    if cheat_result ~= "ok" then
        return cheat_result, "wave.enter: " .. cheat_text .. " answered " .. tostring(cheat_result)
            .. " " .. tostring(cheat_detail)
    end
    local ticks = opts.ticks or QD.wave.ENTER_TICKS
    local begun_tick = nil
    local zuk = wave >= spec.wave_zuk
    local result = await({
        level = function()
            local ra, active = QD.var.server(spec.vars.active)
            if ra ~= "ok" or active ~= 1 then
                return false
            end
            local rw, current = QD.var.server(spec.vars.wave)
            if rw ~= "ok" or current ~= wave then
                return false
            end
            if zuk then
                begun_tick = QD.wave._server_tick()
                return true
            end
            local rl, alive = QD.var.server(spec.vars.alive)
            if rl ~= "ok" or alive <= 0 then
                return false
            end
            local pool = QD.wave._pool(spec)
            if pool < alive then
                return false
            end
            begun_tick = QD.wave._server_tick()
            return true
        end,
        note = "wave.enter " .. cheat_text,
    }, ticks)
    local _, now_detail, now = QD.wave.state(game)
    if result ~= "ok" then
        return "timeout", string.format(
            "wave.enter: %s sent on %s %d; not settled in %d drive tick(s): %s",
            cheat_text, QD.wave.SERVER_TICKS_NOTE, tick0, ticks, tostring(now_detail)), now
    end
    return "ok", string.format(
        "inferno wave %d entered by %s on %s %d (a PRACTICE run: [debugproc,inferno] calls "
            .. "~inferno_enter($wave, 1), inferno.rs2:413; one wave, then it leaves)%s; %s by %s %d (+%d): "
            .. "alive %d, pool %d (%s); player at %s%s",
        wave, cheat_text, QD.wave.SERVER_TICKS_NOTE, tick0,
        before.active and " replacing an active run" or "",
        zuk and "Zuk start queued (settled on active + wave only)" or "wave begun",
        QD.wave.SERVER_TICKS_NOTE, begun_tick, begun_tick - tick0,
        now.alive, now.pool, now.pool_text, now.tile_text,
        (not zuk and now.pool ~= now.alive)
            and string.format(" [pool %d != alive %d]", now.pool, now.alive) or ""), now
end

-- t.wave.await_wave(n, ticks[, game]) -> ok when the server's wave number is n.
function QD.wave.await_wave(n, ticks, game)
    local spec, fail_result, fail_detail = QD.wave._game(game)
    if not spec then
        return fail_result, fail_detail
    end
    assert(type(n) == "number", "t.wave.await_wave: n must be a number")
    assert(type(ticks) == "number", "t.wave.await_wave: ticks must be a number")
    local tick0 = QD.wave._server_tick()
    local met, _, at = QD.wave._await_var(spec.vars.wave,
        function(v) return v == n end, ticks, "wave.await_wave " .. n)
    local _, detail, s = QD.wave.state(game)
    if not met then
        return "timeout", string.format("wave.await_wave: wave %d not reached in %d drive tick(s) from %s %d; last: %s",
            n, ticks, QD.wave.SERVER_TICKS_NOTE, tick0, tostring(detail)), s
    end
    return "ok", string.format("wave %d on %s %d (waited from %d): %s",
        n, QD.wave.SERVER_TICKS_NOTE, at, tick0, tostring(detail)), s
end

-- t.wave.await_clear(ticks[, game]) -> ok when the wave in progress is over:
-- alive reached 0 after the wave had begun, or the wave number moved on, or
-- the run ended (a practice wave's clear leaves; a pause clears the run).
-- Called before the wave has begun (alive 0 at the start), it waits for the
-- begin and then the clear.
function QD.wave.await_clear(ticks, game)
    local spec, fail_result, fail_detail = QD.wave._game(game)
    if not spec then
        return fail_result, fail_detail
    end
    assert(type(ticks) == "number", "t.wave.await_clear: ticks must be a number")
    local before_result, before_detail, before = QD.wave.state(game)
    if before_result ~= "ok" then
        return before_result, "wave.await_clear: " .. tostring(before_detail)
    end
    if not before.active then
        return "refused", "wave.await_clear: no run is active (" .. before_detail .. ")", before
    end
    local wave0 = before.wave
    local seen_alive = before.alive > 0
    local how = nil
    local at = nil
    local result = await({
        level = function()
            local ra, active = QD.var.server(spec.vars.active)
            if ra ~= "ok" then
                return false
            end
            if active ~= 1 then
                how = "the run ended (active 0)"
                at = QD.wave._server_tick()
                return true
            end
            local rw, current = QD.var.server(spec.vars.wave)
            if rw == "ok" and current ~= wave0 then
                how = string.format("the wave number moved %d -> %d", wave0, current)
                at = QD.wave._server_tick()
                return true
            end
            local rl, alive = QD.var.server(spec.vars.alive)
            if rl ~= "ok" then
                return false
            end
            if alive > 0 then
                seen_alive = true
                return false
            end
            if seen_alive then
                how = "alive reached 0"
                at = QD.wave._server_tick()
                return true
            end
            return false
        end,
        note = "wave.await_clear " .. wave0,
    }, ticks)
    local _, detail, s = QD.wave.state(game)
    if result ~= "ok" then
        return "timeout", string.format("wave.await_clear: wave %d not cleared in %d drive tick(s) from %s %d; last: %s",
            wave0, ticks, QD.wave.SERVER_TICKS_NOTE, before.tick, tostring(detail)), s
    end
    return "ok", string.format("wave %d over on %s %d: %s; now: %s",
        wave0, QD.wave.SERVER_TICKS_NOTE, at, how, tostring(detail)), s
end

-- The logout button, pressed ONCE (session.lua's logout re-presses after 90
-- quiet frames; here a second press is the real game's wave reset, so it is
-- never made).  (true, detail) or (false, result, detail).
function QD.wave._press_logout()
    local tab_result, tab_detail = QD.ui.tab("logout")
    if tab_result ~= "ok" then
        return false, tab_result, "the logout tab did not open: " .. tostring(tab_detail)
    end
    local component = nil
    local shown, polls = QD.session._await_polls(function()
        local result, id = api_drive.component("logout:logout")
        if result ~= "ok" then
            return false
        end
        component = id
        local presented_result, presented = api_drive.widget_presented(id)
        return presented_result == "ok" and presented == true
    end, QD.session.LOGOUT_PAINT_POLLS, "wave.pause logout paint")
    if not shown then
        if component == nil then
            return false, "not_found", "logout:logout never resolved after the logout tab opened"
        end
        return false, "not_visible", string.format(
            "logout:logout (component %d) was never displayed in %d frame(s)", component, polls)
    end
    local click_result, click_detail = api_drive.if_click(component, 1)
    if click_result ~= "ok" then
        return false, click_result, "the press on logout:logout answered " .. tostring(click_detail)
    end
    return true, string.format("pressed logout:logout once (displayed after %d frame(s))", polls)
end

-- t.wave.pause([opts]) -> (result, detail)
--
-- opts.via = "logout" (default) | "exit"; opts.ticks = drive ticks to wait for
-- the server's answer (PAUSE_TICKS).  Inferno only.
--   ok          the server armed the pause request (logout_requested 0 -> 1,
--               the content's "Your logout request has been received" line) or
--               paused the run (paused 1, saved_wave N, player on the exit pad);
--   refused     no run is active, or the server answered without pausing (a
--               practice run leaves; a request already noted); the lines quoted;
--   unsupported the logout press ended the session instead (the content has no
--               pause on the logout button); the client is on the title screen;
--   timeout     nothing changed and nothing was printed.
function QD.wave.pause(opts)
    opts = opts or {}
    local spec, fail_result, fail_detail = QD.wave._game(opts.game)
    if not spec then
        return fail_result, fail_detail
    end
    local via = opts.via or "logout"
    assert(via == "logout" or via == "exit", "t.wave.pause: opts.via must be \"logout\" or \"exit\"")
    local before_result, before_detail, before = QD.wave.state(opts.game)
    if before_result ~= "ok" then
        return before_result, "wave.pause: " .. tostring(before_detail)
    end
    if not before.active then
        return "refused", "wave.pause: no run is active (" .. before_detail .. ")"
    end
    local since = QD.wave._serial()
    local tick0 = QD.wave._server_tick()
    local how
    if via == "logout" then
        local pressed, press_result, press_detail = QD.wave._press_logout()
        if not pressed then
            return press_result, "wave.pause: " .. tostring(press_detail)
        end
        how = press_result
    else
        local click_result, click_detail = QD.player.click_loc(spec.exit_loc, 1)
        if click_result ~= "ok" then
            return click_result, "wave.pause: click on the Cave exit (" .. spec.exit_loc .. " op 1) answered "
                .. tostring(click_result) .. " " .. tostring(click_detail)
        end
        how = "clicked the content's Cave exit (" .. spec.exit_loc .. " op 1, inferno.rs2:288; "
            .. "the real game pauses on the logout button: wiki_Inferno_Strategies.wikitext:856)"
    end
    local ticks = opts.ticks or QD.wave.PAUSE_TICKS
    local outcome = nil
    local at = nil
    await({
        level = function()
            if api_core.screen() ~= QD.session.SCREEN.game then
                outcome = "session"
                at = QD.wave._server_tick()
                return true
            end
            local rp, paused = QD.var.server(spec.vars.paused)
            if rp == "ok" and paused == 1 then
                outcome = "paused"
                at = QD.wave._server_tick()
                return true
            end
            local rr, req = QD.var.server(spec.vars.logout_requested)
            if rr == "ok" and req == 1 and not before.logout_requested then
                outcome = "requested"
                at = QD.wave._server_tick()
                return true
            end
            local ra, active = QD.var.server(spec.vars.active)
            if ra == "ok" and active ~= 1 then
                outcome = "ended"
                at = QD.wave._server_tick()
                return true
            end
            return false
        end,
        note = "wave.pause " .. via,
    }, ticks)
    if outcome == "session" then
        -- The player is gone: no var read answers now.  Wait a moment for the
        -- title screen so the caller's t.session.login() starts from it.
        QD.session._await_polls(QD.session._screen_is(QD.session.SCREEN.title),
            QD.session.LOGOUT_POLLS, "wave.pause -> title")
        return "unsupported", string.format(
            "wave.pause: %s inside %s wave %d (alive %d) on %s %d; the server ENDED THE SESSION on %s %d "
                .. "instead of a pause request: the content's logout button is `[if_button,logout:logout] p_logout;` "
                .. "(interface_logout/scripts/logout.rs2:21, no Inferno check) and [logout,_] -> ~inferno_on_logout "
                .. "(inferno.rs2:399) clears the run; the content's pause is armed only by the Cave exit "
                .. "(inferno.rs2:288; opts.via=\"exit\"). Screen now %s: t.session.login() next",
            how, before.practice and "PRACTICE" or "real", before.wave, before.alive,
            QD.wave.SERVER_TICKS_NOTE, tick0, QD.wave.SERVER_TICKS_NOTE, at,
            QD.session.SCREEN_NAME[api_core.screen()] or tostring(api_core.screen()))
    end
    local lines = QD.wave._lines_since(since)
    local _, after_detail, after = QD.wave.state(opts.game)
    if outcome == "paused" then
        return "ok", string.format("wave.pause: %s on %s %d; PAUSED on %s %d at saved wave %d; lines: %s; now: %s",
            how, QD.wave.SERVER_TICKS_NOTE, tick0, QD.wave.SERVER_TICKS_NOTE, at, after.saved_wave, lines, after_detail)
    end
    if outcome == "requested" then
        return "ok", string.format(
            "wave.pause: %s on %s %d; pause REQUESTED on %s %d (logout_req 0 -> 1, wave %d alive %d: "
                .. "the run pauses when this wave is cleared, inferno_waves.rs2 ~inferno_pause_now); lines: %s",
            how, QD.wave.SERVER_TICKS_NOTE, tick0, QD.wave.SERVER_TICKS_NOTE, at, after.wave, after.alive, lines)
    end
    if outcome == "ended" then
        return "refused", string.format(
            "wave.pause: %s on %s %d; the run ENDED on %s %d without a pause (%s run: ~inferno_request_pause "
                .. "leaves a practice run, inferno.rs2:190); lines: %s; now: %s",
            how, QD.wave.SERVER_TICKS_NOTE, tick0, QD.wave.SERVER_TICKS_NOTE, at,
            before.practice and "a PRACTICE" or "a real", lines, after_detail)
    end
    if lines ~= "" then
        return "refused", string.format("wave.pause: %s on %s %d; nothing changed in %d drive tick(s); the server said: %s; now: %s",
            how, QD.wave.SERVER_TICKS_NOTE, tick0, ticks, lines, after_detail)
    end
    return "timeout", string.format("wave.pause: %s on %s %d; nothing changed and nothing was printed in %d drive tick(s); now: %s",
        how, QD.wave.SERVER_TICKS_NOTE, tick0, ticks, after_detail)
end

-- t.wave.resume([opts]) -> (result, detail[, state])
--
-- After t.session.login(): what a player does in this content to resume a
-- paused run -- the Cave entrance's op 1 and "Resume the Inferno (wave N)."
-- (inferno.rs2:273-280) -- settled on the run active on the saved wave and,
-- unless opts.begin == false, on that wave having begun (alive > 0, pool
-- caught up).  opts.ticks = drive ticks (RESUME_TICKS); opts.at = the
-- entrance copy {x, z, level} (default: the nearest copy).  `refused` when
-- no run is paused or one is already active.  The entrance has an op only
-- while varb5646 = 2 (the cache's multiloc binding); the content writes 1 on
-- a sacrifice, and then the press answers `covered` (Examine only).
function QD.wave.resume(opts)
    opts = opts or {}
    local spec, fail_result, fail_detail = QD.wave._game(opts.game)
    if not spec then
        return fail_result, fail_detail
    end
    local before_result, before_detail, before = QD.wave.state(opts.game)
    if before_result ~= "ok" then
        return before_result, "wave.resume: " .. tostring(before_detail)
    end
    if before.active then
        return "refused", "wave.resume: a run is already active (" .. before_detail .. ")", before
    end
    if not before.paused or before.saved_wave <= 0 then
        return "refused", "wave.resume: no run is paused (" .. before_detail .. ")", before
    end
    local saved = before.saved_wave
    local tick0 = QD.wave._server_tick()
    local click_result, click_detail = QD.player.click_loc(spec.entrance_loc, 1,
        opts.at and { at = opts.at } or nil)
    if click_result ~= "ok" then
        return click_result, "wave.resume: click on the Cave entrance (" .. spec.entrance_loc
            .. " op 1) answered " .. tostring(click_result) .. " " .. tostring(click_detail), before
    end
    local offered = await({
        level = function() return QD.chat.kind() == "options" end,
        note = "wave.resume options",
    }, QD.wave.CHOICE_TICKS)
    if offered ~= "ok" then
        return "timeout", "wave.resume: the entrance click (" .. tostring(click_detail)
            .. ") opened no choice in " .. QD.wave.CHOICE_TICKS .. " drive tick(s); chat kind "
            .. tostring(QD.chat.kind()), before
    end
    -- The row's own text, for the detail (chat.choose's ok detail names the
    -- page change, not the row).
    local row_text = spec.resume_choice
    local options_result, rows = QD.chat.options()
    if options_result == "ok" and type(rows) == "table" then
        local pattern = string.sub(spec.resume_choice, 2, -2)
        for i = 1, #rows do
            local text = type(rows[i]) == "table" and rows[i].text or rows[i]
            if type(text) == "string" and string.find(text, pattern) then
                row_text = text
                break
            end
        end
    end
    local choose_result, choose_detail = QD.chat.choose(spec.resume_choice)
    if choose_result ~= "ok" then
        return choose_result, "wave.resume: the entrance offered no resume row: "
            .. tostring(choose_detail), before
    end
    choose_detail = row_text
    local ticks = opts.ticks or QD.wave.RESUME_TICKS
    local wait_begin = opts.begin ~= false
    local entered_tick = nil
    local begun_tick = nil
    local result = await({
        level = function()
            local ra, active = QD.var.server(spec.vars.active)
            if ra ~= "ok" or active ~= 1 then
                return false
            end
            local rw, current = QD.var.server(spec.vars.wave)
            if rw ~= "ok" or current ~= saved then
                return false
            end
            if entered_tick == nil then
                entered_tick = QD.wave._server_tick()
            end
            if not wait_begin or saved >= spec.wave_zuk then
                return true
            end
            local rl, alive = QD.var.server(spec.vars.alive)
            if rl ~= "ok" or alive <= 0 then
                return false
            end
            if QD.wave._pool(spec) < alive then
                return false
            end
            begun_tick = QD.wave._server_tick()
            return true
        end,
        note = "wave.resume " .. saved,
    }, ticks)
    local _, detail, s = QD.wave.state(opts.game)
    if result ~= "ok" then
        return "timeout", string.format(
            "wave.resume: chose '%s' on %s %d; run %s on wave %d in %d drive tick(s): %s",
            tostring(choose_detail), QD.wave.SERVER_TICKS_NOTE, tick0,
            entered_tick and ("active from " .. entered_tick .. " but not begun") or "never active",
            saved, ticks, tostring(detail)), s
    end
    return "ok", string.format(
        "wave.resume: resumed on wave %d -- the content's re-entry (Cave entrance op 1 + '%s', inferno.rs2:273; "
            .. "the real game resumes on login, wiki_Inferno_Strategies.wikitext:856); active on %s %d%s; now: %s",
        saved, tostring(choose_detail), QD.wave.SERVER_TICKS_NOTE, entered_tick,
        begun_tick and string.format(", wave begun on %d (+%d)", begun_tick, begun_tick - entered_tick) or "",
        tostring(detail)), s
end
