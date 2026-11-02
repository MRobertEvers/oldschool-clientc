-- Dad, Troll Stronghold's arena champion, against RSProx index 4455 [698..980]
-- (docs/rsprox-analysis/quests/troll_stronghold_dad.md). It starts where the
-- recorder stood at [715]: quest stage 10 with troll_told set, just inside the
-- arena's west door. Two legs, one per answer to Dad's surrender menu:
--   A  Protect from Melee on, as the recorder fought (16 of 16 of Dad's hits 0):
--      Attack -> the challenge page -> the fight -> "Stop! You win." ->
--      "I'll be going now.": Attack refused after, Dad stands.
--   B  no prayer (his hits land, sharks): the same fight -> "I'm not done yet!
--      Prepare to die!": Attack refused after, the arrow back on Dad and Dad
--      swinging at the player again ([939..972]); then out of the arena, and he
--      stands down ([980]).
-- Every leg checks the throw (human_troll_flyback with a 5-tile jump or none)
-- and Dad's own swings from the tick log.
local DAD = "troll_champion"

return {
    id = "troll_dad",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        "::clearinv",
        -- A bow, as the recorder fought from range (earth strike, 4455): a
        -- thrown player is still in reach and fights on where they land. Its
        -- best hit (19) cannot take Dad from 20 to nothing, so every leg ends
        -- in the surrender, not a kill.
        "::setlevel ranged 99",
        "::setlevel defence 60",
        "::setlevel hitpoints 99",
        "::setlevel prayer 99",
        "::give magic_shortbow 1",
        "::wield magic_shortbow",
        "::give rune_arrow 1000",
        "::wield rune_arrow",
        "::give shark 20",
        "::setvar varp317_troll_quest ^troll_started",
        "::setvar varb4_troll_told ^true",
    },

    run = function(t)
        -- The log first, so Dad's spawn (quest_troll.rs2 [mapzone,0_45_56]) is a row in it and
        -- tools/rsprox/rsprox_hit_causes.py can attribute his swings.
        local log_result, log_detail = t.ticklog.start()
        t.check("ticklog", log_result, tostring(log_detail))
        -- 2898,3618: one step inside the west door (4455 [715]).
        t.exec("arena", t.cheat, "::tele 0_45_56_18_34")
        t.ticks(2)
        local flyback = t.player.by_symbol("seq", "human_troll_flyback")
        local treetrunk = t.player.by_symbol("seq", "troll_treetrunk_attack")
        local swing = t.player.by_symbol("seq", "troll_attack")

        -- A mark's serial: rows after it are the leg's.
        local function mark(label)
            local _, detail = t.ticklog.mark(label)
            return tonumber(string.match(tostring(detail), "serial (%d+)")) or 0
        end
        -- Dad's swings since a serial: treetrunk and plain, and the player's throws.
        local function swings_since(since)
            local _, anims = t.ticklog.rows({ kind = "npc_anim", since = since, type = 4130 })
            local trunks, plains = 0, 0
            for _, row in ipairs(anims or {}) do
                if treetrunk and row.seq == treetrunk.id then trunks = trunks + 1 end
                if swing and row.seq == swing.id then plains = plains + 1 end
            end
            return trunks, plains
        end
        local function throws_since(since)
            local _, anims = t.ticklog.rows({ kind = "player_anim", since = since })
            local _, tiles = t.ticklog.rows({ kind = "player_tile", since = since })
            local out = {}
            for _, row in ipairs(anims or {}) do
                if flyback and row.seq == flyback.id then
                    local before, after = nil, nil
                    for _, tile in ipairs(tiles or {}) do
                        if tile.tick < row.tick then before = tile end
                        if tile.tick == row.tick then after = tile end
                    end
                    out[#out + 1] = { tick = row.tick, delay = row.delay, before = before, after = after }
                end
            end
            return out
        end
        local function describe_throws(list)
            local parts = {}
            for _, th in ipairs(list) do
                local from = th.before and (th.before.x .. "," .. th.before.z) or "?"
                local to = th.after and (th.after.x .. "," .. th.after.z) or "stayed"
                parts[#parts + 1] = "[" .. th.tick .. "] delay " .. tostring(th.delay) .. " " .. from .. "->" .. to
            end
            return table.concat(parts, "; ")
        end
        -- A throw moves 5 tiles on each axis it moves on, or not at all, or
        -- (diagonally) less where a wall stops it; never more than 5.
        local function throws_ok(list)
            for _, th in ipairs(list) do
                if th.delay ~= 20 then return false end
                if th.before and th.after then
                    local dx = math.abs(th.after.x - th.before.x)
                    local dz = math.abs(th.after.z - th.before.z)
                    if dx > 5 or dz > 5 then return false end
                    if dx ~= 0 and dz ~= 0 and dx ~= dz then return false end
                    -- straight: the whole 5 or nothing (4455 [769])
                    if (dx == 0 or dz == 0) and math.max(dx, dz) ~= 5 and math.max(dx, dz) ~= 0 then return false end
                end
            end
            return true
        end

        -- One fight: until Dad's surrender page is up. Attack is pressed only
        -- when the fight has stalled (no hit on Dad for 10 ticks: a throw or an
        -- eat ends the player's attack): a click that lands while his page is
        -- up closes it, as it would in the client, and the menu with it.
        local function fight(leg)
            local start = mark(leg .. ".fight")
            local presses, last, last_detail, last_press = 0, nil, nil, -100
            for _ = 1, 900 do
                if t.chat.kind() ~= "none" then break end
                local _, now = t.tick()
                local _, hits = t.ticklog.rows({ kind = "hit_npc", since = start, type = 4130 })
                local last_hit = (hits and #hits > 0) and hits[#hits].tick or -100
                local _, hp = t.skill.read("hitpoints")
                if type(hp) == "table" and hp.level ~= nil and hp.level < 60 then
                    t.player.inv_op("shark", 1)
                    t.ticks(1)
                elseif (now or 0) - math.max(last_hit, last_press) >= 10 and presses < 80 then
                    presses = presses + 1
                    last_press = now or 0
                    last, last_detail = t.player.attack(DAD, 2, 1)
                else
                    t.ticks(1)
                end
            end
            local _, text = t.chat.text()
            t.check(leg .. ".surrender", string.find(tostring(text), "Stop! You win", 1, true) ~= nil,
                "after " .. presses .. " attack press(es) (last " .. tostring(last) .. " " .. tostring(last_detail)
                .. "): chat " .. t.chat.kind() .. " '" .. tostring(text) .. "'")
        end

        -- Attack before the challenge opens Dad's page on the op tick (4455 [718]);
        -- the press itself lands no hit, so its own verdict is not a row.
        local function challenge(leg)
            t.exec(leg .. ".dad", t.npc.await_present, DAD, 24, 10)
            local pressed, press_detail = t.player.attack(DAD, 2, 8)
            local _, accepted = t.var.server("varb3_troll_accepted_challenge")
            t.check(leg .. ".accepted", accepted == 1 and t.chat.kind() == "npc",
                "Attack -> " .. tostring(pressed) .. " (" .. tostring(press_detail) .. "); troll_accepted_challenge "
                .. tostring(accepted) .. ", chat " .. t.chat.kind() .. " (4455 [718]: the varbit with Dad's page)")
            t.exec(leg .. ".page", t.chat.play, { "npc:Tiny human brave. Dad squish!" })
        end

        ------------------------------------------------------------------
        -- Leg A: prayed, "I'll be going now."
        ------------------------------------------------------------------
        local since_a = mark("leg.a")
        t.exec("a.pray", t.prayer.set, "protectfrommelee", true)
        challenge("a")
        fight("a")
        local _, stage_a = t.var.server("varp317_troll_quest")
        local _, death_a = t.var.server("varb5_troll_to_the_death")
        t.check("a.stage", stage_a == 20 and death_a == 1,
            "at the surrender page: troll_quest " .. tostring(stage_a) .. " (want 20), troll_to_the_death "
            .. tostring(death_a) .. " (want 1; both before the answer, 4455 [925])")
        t.exec("a.answer", t.chat.play, {
            "npc:Stop! You win. Not hurt Dad.",
            "choose:I'll be going now.",
            "player:I'll be going now.",
        })
        t.chat.drain({})
        t.ticks(2)
        local trunks_a, plains_a = swings_since(since_a)
        local throws_a = throws_since(since_a)
        t.check("a.swings", trunks_a + plains_a > 0 and #throws_a == trunks_a and throws_ok(throws_a),
            trunks_a .. " treetrunk + " .. plains_a .. " plain swing(s); throws: " .. describe_throws(throws_a))
        local serial_a = mark("a.after")
        t.player.attack(DAD, 2, 4)
        t.exec("a.refused", t.msg.expect, "You don't need to fight him again.")
        t.ticks(16)
        local trunks_a2, plains_a2 = swings_since(serial_a)
        t.check("a.stands", trunks_a2 + plains_a2 == 0,
            "Dad's swings in the 20 ticks after 'I'll be going now.': " .. (trunks_a2 + plains_a2))
        t.exec("a.unpray", t.prayer.set, "protectfrommelee", false)

        ------------------------------------------------------------------
        -- Leg B: unprayed, "I'm not done yet! Prepare to die!"
        ------------------------------------------------------------------
        t.cheat("::setvar varp317_troll_quest ^troll_started")
        t.cheat("::setvar varb3_troll_accepted_challenge ^false")
        t.cheat("::setvar varb5_troll_to_the_death ^false")
        t.ticks(2)
        local since_b = mark("leg.b")
        challenge("b")
        fight("b")
        local _, stage_b = t.var.server("varp317_troll_quest")
        local _, death_b = t.var.server("varb5_troll_to_the_death")
        t.check("b.stage", stage_b == 20 and death_b == 1,
            "at the surrender page: troll_quest " .. tostring(stage_b) .. ", troll_to_the_death " .. tostring(death_b))
        t.exec("b.answer", t.chat.play, {
            "npc:Stop! You win. Not hurt Dad.",
            "choose:I'm not done yet! Prepare to die!",
            "player:I'm not done yet! Prepare to die!",
        })
        t.chat.drain({})
        local serial_b = mark("b.after")
        t.player.attack(DAD, 2, 4)
        t.exec("b.refused", t.msg.expect, "You don't need to fight him again.")
        -- Stand by him: he comes for the player again (4455 [952], [964], [972]).
        local hits_after = 0
        for _ = 1, 6 do
            t.ticks(5)
            local _, hp = t.skill.read("hitpoints")
            if type(hp) == "table" and hp.level ~= nil and hp.level < 60 then t.player.inv_op("shark", 1) end
        end
        local trunks_b2, plains_b2 = swings_since(serial_b)
        local _, hits = t.ticklog.rows({ kind = "hit_player", since = serial_b })
        hits_after = #(hits or {})
        t.check("b.dad_fights_on", trunks_b2 + plains_b2 > 0 and hits_after > 0,
            "after 'Prepare to die': Dad " .. trunks_b2 .. " treetrunk + " .. plains_b2 .. " plain swing(s), "
            .. hits_after .. " splat(s) on the player")
        local trunks_b, plains_b = swings_since(since_b)
        local throws_b = throws_since(since_b)
        t.check("b.swings", trunks_b + plains_b > 0 and #throws_b == trunks_b and throws_ok(throws_b),
            trunks_b .. " treetrunk + " .. plains_b .. " plain swing(s); throws: " .. describe_throws(throws_b))

        -- Out of the arena: the fight ends (the arrow off, Dad stands; 4455 [980]).
        t.cheat("::tele 0_45_56_36_47")
        t.ticks(4)
        local serial_out = mark("b.out")
        t.ticks(16)
        local trunks_o, plains_o = swings_since(serial_out)
        t.check("b.stands_down", trunks_o + plains_o == 0,
            "Dad's swings in the 16 ticks after the player left the arena: " .. (trunks_o + plains_o))
        local _, stage_end = t.var.server("varp317_troll_quest")
        t.check("stage.end", stage_end == 20, "troll_quest " .. tostring(stage_end))
    end,
}
