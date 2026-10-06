-- tob_nylocas_normal: the Nylocas room, Normal Mode, a party of THREE (three driven clients in one world).
-- ROLES (named in the sources; the leader holds the world's tick log, so every spec row is read on it):
--   "To keep things simple, there are three roles: melee, ranged, and mage" (transcripts/yt_4i4lv-srJkw.md:33), "a wave-based swarm of
--   crabs where you'll stick to one combat style each. These will be the roles you picked at the start" (yt_4i4lv-srJkw.md:79); the
--   trio guide has one section per role, "Mage Waves", "Ranger Waves", "Melee Waves" (blert_guides/tob_nylocas_trio_content.mdx:51, :196,
--   :372). "Click on a crab that matches your combat style. Gray for melee, green for ranged, and blue for magic" (yt_4i4lv-srJkw.md:81).
--   p1 the leader is the RANGER (the log's owner: it also holds the room's wrong-style probes, the weapon swaps of the boss and the
--     rows), p2 the MELEE raider, p3 the MAGE (the mage "puts on their trident" and may "switch to barrage for any big clumps",
--     yt_4i4lv-srJkw.md:79-81: the lava battlestaff's Ice Burst here).
--   Each raider "prioritize[s] crabs that have aggroed and are attacking other players, then ... the newest crabs entering the room"
--   (yt_4i4lv-srJkw.md:81-83) and "if you see a pillar getting low, it's okay to focus crabs on that pillar" (:83); the boss: "You need to
--   switch prayers and combat style whenever he changes color. Gray means pray melee and attack with melee ... You can usually get in two
--   or three hits depending on your weapon" (:85), "If you hit the boss with the wrong style, you will take damage" (:87).
-- A party of three fixes the pools at 750 permille of the five-man pool (raidwide.scale.party_3_or_fewer): small 8, big 16, Vasilias 1875,
-- supports 230.
local role = (QD_PARTY and QD_PARTY.role) or 1
local kit = {
    -- an empty backpack so the kit below fills exactly the 28 slots (a raider holding no br_ brew is handed eight free doses on entry)
    "::clearinv",
}
if role == 1 then
    -- the ranger's best ranged set and twisted bow (the content's own ::maxrange: per-slot best gear, the levels to wear it, the quiver); the whip and
    -- the lava battlestaff ride in the backpack for Vasilias' other two colours (yt_4i4lv-srJkw.md:85 "switch prayers and combat style whenever he
    -- changes color"), so the leader also needs the levels for them
    kit[#kit + 1] = "::maxrange"
    kit[#kit + 1] = "::setlevel attack 99"
    kit[#kit + 1] = "::setlevel strength 99"
    kit[#kit + 1] = "::setlevel magic 99"
    kit[#kit + 1] = "::give abyssal_whip 1"
    kit[#kit + 1] = "::give lava_battlestaff 1"
    -- Ancient Magicks for Ice Burst on Vasilias' blue form (the spellbook var is a bring-along)
    kit[#kit + 1] = "::setvar varb4070_spellbook 1"
    kit[#kit + 1] = "::give water_rune 1000"
    kit[#kit + 1] = "::give chaos_rune 500"
    kit[#kit + 1] = "::give death_rune 500"
    kit[#kit + 1] = "::give 4dosepotionofsaradomin 6"
    kit[#kit + 1] = "::give 4dose2restore 4"
    kit[#kit + 1] = "::give anglerfish 13"
elseif role == 2 then
    -- the melee raider's best melee set and the charged scythe of vitur (::maxmelee), the weapon the trio guide names for it
    -- (blert_guides/tob_nylocas_trio_content.mdx:372 "Scythe the east melee doubles")
    kit[#kit + 1] = "::maxmelee"
    -- against Vasilias every raider answers every colour ("You need to switch prayers and combat style whenever he changes color", yt_4i4lv-srJkw.md:85):
    -- the melee raider carries the magic shortbow with rune arrows and the lava battlestaff with the Ancient Magicks runes for her green and blue forms
    kit[#kit + 1] = "::setlevel ranged 99"
    kit[#kit + 1] = "::setlevel magic 99"
    kit[#kit + 1] = "::setvar varb4070_spellbook 1"
    kit[#kit + 1] = "::give magic_shortbow 1"
    kit[#kit + 1] = "::give rune_arrow 300"
    kit[#kit + 1] = "::give lava_battlestaff 1"
    kit[#kit + 1] = "::give water_rune 1000"
    kit[#kit + 1] = "::give chaos_rune 500"
    kit[#kit + 1] = "::give death_rune 500"
    kit[#kit + 1] = "::give 4dosepotionofsaradomin 7"
    kit[#kit + 1] = "::give 4dose2restore 4"
    kit[#kit + 1] = "::give anglerfish 11"
else
    -- the mage's best magic set and Tumeken's shadow (::maxmage): "the mage should put on their trident" (yt_4i4lv-srJkw.md:79), a powered staff
    kit[#kit + 1] = "::maxmage"
    -- the Ancient Magicks spellbook var is a bring-along for Ice Burst, the spell a magic nylocas can be hurt by (a powered staff's own attack is nulled by a
    -- Hagios: CONTENT_BUGS.md, tree-wide magic damage type)
    kit[#kit + 1] = "::setvar varb4070_spellbook 1"
    kit[#kit + 1] = "::give water_rune 1000"
    kit[#kit + 1] = "::give chaos_rune 500"
    kit[#kit + 1] = "::give death_rune 500"
    -- and the mage carries the whip and the magic shortbow with rune arrows for her gray and green forms (yt_4i4lv-srJkw.md:85)
    kit[#kit + 1] = "::setlevel attack 99"
    kit[#kit + 1] = "::setlevel strength 99"
    kit[#kit + 1] = "::setlevel ranged 99"
    kit[#kit + 1] = "::give abyssal_whip 1"
    kit[#kit + 1] = "::give magic_shortbow 1"
    kit[#kit + 1] = "::give rune_arrow 300"
    kit[#kit + 1] = "::give 4dosepotionofsaradomin 8"
    kit[#kit + 1] = "::give 4dose2restore 4"
    kit[#kit + 1] = "::give anglerfish 10"
end
-- the prayer level for the protection prayers
kit[#kit + 1] = "::setlevel prayer 99"

return {
    id = "tob_nylocas_normal",
    fixture = "fresh_lumbridge.ini",
    party = 3,
    max_frames = 200000,
    setup = kit,

    run = function(t)
        local role = t.party.role()
        if role == 1 then
            t.check("spec.scope", true, "mode=normal party=3")
            -- the log is on before the room is entered (the room's music starts on the arrival tick)
            t.ticklog.start()
        end
        local enter_result, enter_detail = t.raid.enter("tob", "nylocas", { mode = "normal" })
        t.check("enter", enter_result == "ok", "p" .. role .. " " .. tostring(enter_detail))
        t.expect("party.barrier.entrance", t.party.barrier("entrance", 300))
        if role ~= 1 then
            -- A MEMBER'S PART: its own clicks, prayers, eats and swings; every row its own (the union ledger prefixes it p<n>:).
            -- p2 is the melee raider and p3 the mage (yt_4i4lv-srJkw.md:33, :79): it kills the nylocas of its own style, aggros first and then
            -- the newest chewers (yt_4i4lv-srJkw.md:81-83), and switches prayer and weapon with Vasilias' colour (:85).
            local weapon = { melee = "scythe_of_vitur", ranged = "magic_shortbow", magic = "lava_battlestaff" }
            if role == 3 then weapon = { melee = "abyssal_whip", ranged = "magic_shortbow", magic = "tumekens_shadow" } end
            local ammo_ready = false
            local wielded = (role == 2) and "scythe_of_vitur" or "tumekens_shadow"
            local style_names = { "melee", "ranged", "magic" }
            local prayer_of = { melee = "protectfrommelee", magic = "protectfrommagic", ranged = "protectfrommissiles", off_melee = "piety", off_ranged = "rigour", off_magic = "augury" }
            -- the press itself costs a tick, so the wait after it is the weapon's speed less one (scythe 5, Ice Burst 5)
            local hold_of = { melee = 4, ranged = 3, magic = 4 }
            local boss_symbol = { melee = "nylocas_boss_melee", magic = "nylocas_boss_magic", ranged = "nylocas_boss_ranged" }
            local brew_names = { "1dosepotionofsaradomin", "2dosepotionofsaradomin", "3dosepotionofsaradomin", "4dosepotionofsaradomin" }
            local restore_names = { "1dose2restore", "2dose2restore", "3dose2restore", "4dose2restore" }
            local my_style = (role == 2) and "melee" or "magic"
            local my_stat = (role == 2) and "attack" or "magic"
            -- the melee raider's scythe is worn already (::maxmelee); the mage swaps Tumeken's shadow for the lava battlestaff (a powered staff is nulled by
            -- a Hagios, CONTENT_BUGS.md tree-wide magic damage type: only a SPELL hurts a magic nylocas); the combat tab opens for the retaliate button
            local equip_result, equip_detail = "worn", ""
            -- (the mage keeps Tumeken's shadow worn and casts Ice Burst with it: the lava battlestaff needs 30 Attack, which ::maxmage does not set)
            local current = my_style
            t.ui.tab("combat")
            t.ticks(1)
            -- auto-retaliate off (the combat tab's own button): a retaliation with the wrong weapon in hand nulls a nylocas for good
            local retaliate_before_result, retaliate_before = t.var.varp("varp172_option_nodef")
            local retaliate_widget_result, retaliate_widget = t.ui.widget("combat_interface:retaliate")
            local retaliate_press_result = "skipped"
            if retaliate_before == 0 then retaliate_press_result = t.ui.invoke(retaliate_widget, 1) end
            t.ticks(2)
            local retaliate_after_result, retaliate_after = t.var.varp("varp172_option_nodef")
            t.check("member.setup", (equip_result == "ok" or equip_result == "worn") and retaliate_after == 1, "p" .. role .. " " .. my_style .. " raider: " .. weapon[my_style] .. " wielded (" .. tostring(equip_result) .. " " .. string.sub(tostring(equip_detail), 1, 120)
                .. "), auto-retaliate button pressed " .. tostring(retaliate_press_result) .. ", varp172_option_nodef " .. tostring(retaliate_before) .. " before, " .. tostring(retaliate_after) .. " after (1 is off)")
            -- "Use your offensive prayers at all times in this room" (yt_4i4lv-srJkw.md:83)
            local off_result, off_detail = t.prayer.set(prayer_of["off_" .. my_style], true)
            t.expect("member.offensive_prayer", off_result, "p" .. role .. " " .. prayer_of["off_" .. my_style] .. ": " .. tostring(off_detail))
            t.expect("party.barrier.started", t.party.barrier("started", 900))
            -- the fight is running: the barrier is only a gate now (tob_party.rs2 oploc1 tob_arena_barrier steps the raider across)
            local cross_result, cross_detail = t.player.click_loc("tob_arena_barrier", 1)
            local _, cross_tile = t.world.tile()
            t.check("barrier.cross", cross_result == "ok", "p" .. role .. " crossed the open barrier: " .. tostring(cross_detail) .. ", now at " .. tostring(cross_tile and (cross_tile.x .. "," .. cross_tile.z)))
            local fight_end, swings, casts, eaten, brews, restores, idle, lowest = "tick cap", 0, 0, 0, 0, 0, 0, 99
            local prayer_style, prayer_switches, boss_style, boss_since = nil, 0, nil, 0
            local lock, skips, seen_at = nil, {}, {}
            local now, tick_start, doomed, support_count, fallen = 0, nil, { x = -100, z = -100 }, 4, false
            local sacrifice_frozen, weak = false, nil
            local next_poll, waves_calls, boss_calls, refused, steps = 0, 0, 0, 0, 0
            local expect_hp, expect_until, expect_tick, restore_after = 0, 0, 0, 0
            local first_answers = {}
            local ring = {}
            for iteration = 1, 7000 do
                local support_result, support_row = t.npc.state("tob_nylocas_support")
                if support_result == "ok" then now = tonumber(string.match(t.npc.state_text(support_row), "now tick (%d+)")) or (now + 1) else now = now + 1 end
                if tick_start == nil then tick_start = now end
                if now - tick_start >= 1500 then fight_end = "tick cap, 1500 ticks" break end
                local _, tile = t.world.tile()
                if tile ~= nil and tile.x < 6000 then fight_end = "player died (respawned at " .. tile.x .. ")" break end
                local hp_result, hp = t.skill.read("hitpoints")
                local hp_now = 99
                if hp_result == "ok" and type(hp) == "table" and hp.level ~= nil then hp_now = hp.level end
                if hp_now < lowest then lowest = hp_now end
                -- every fighting copy in view
                local fight_rows = {}
                for _, kind in ipairs({ "big_fighting", "fighting" }) do
                    for index = 1, 3 do
                        local symbol = "tob_nylocas_" .. kind .. "_" .. style_names[index]
                        local near_result, _, near_rows = t.npc.tiles(symbol, 16)
                        if near_result == "ok" then
                            for _, row in ipairs(near_rows) do
                                fight_rows[#fight_rows + 1] = { row = row, symbol = symbol, style = style_names[index], big = (kind == "big_fighting") }
                            end
                        end
                    end
                end
                local fight_count = #fight_rows
                -- the protection prayer of the aggro majority (a big counts two, a melee copy only inside four tiles), as the leader does
                if fight_count > 0 and tile ~= nil and boss_style == nil then
                    local weight = { melee = 0, ranged = 0, magic = 0 }
                    for _, f in ipairs(fight_rows) do
                        local near = math.max(math.abs(f.row.x - tile.x), math.abs(f.row.z - tile.z))
                        if f.style ~= "melee" or near <= 4 then weight[f.style] = weight[f.style] + (f.big and 2 or 1) end
                    end
                    local best_style, best_weight = prayer_style, prayer_style ~= nil and weight[prayer_style] or 0
                    for _, style in ipairs(style_names) do
                        if weight[style] > best_weight then best_style, best_weight = style, weight[style] end
                    end
                    if best_style ~= nil and best_style ~= prayer_style then
                        if t.prayer.set(prayer_of[best_style], true) == "ok" then
                            prayer_style = best_style
                            prayer_switches = prayer_switches + 1
                        end
                    end
                end
                -- a restore when the prayer is nearly gone or the raider's own style stat is drained by the brews
                local _, points = t.skill.read("prayer")
                local _, drained = t.skill.read(my_stat)
                if type(points) == "table" and points.level ~= nil and (points.level < 14 or (type(drained) == "table" and drained.level ~= nil and drained.level < 75)) and now >= restore_after then
                    for _, name in ipairs(restore_names) do
                        local restore_result, restore_count = t.inv.count(name)
                        if restore_result == "ok" and restore_count > 0 then
                            t.player.inv_op(name, 1)
                            restores = restores + 1
                            restore_after = now + 4
                            lock = nil
                            break
                        end
                    end
                end
                -- hitpoints first, as the leader does: eat at 78 or under (84 with three copies swinging, 88 with four or more), a brew while 84 or under, up to
                -- three in a row; the read lags an eat by one to three ticks, so the heal is counted (a brew 16, to 116; a anglerfish 22) and the read trusted
                -- again three ticks on
                local guess = expect_hp - 5 * (now - expect_tick)
                if now < expect_until and hp_now >= 60 and hp_now < guess then hp_now = guess end
                local eat_below = 78 + 2 * math.min(fight_count, 3)
                if fight_count >= 4 then eat_below = 88 end
                -- Vasilias' unprayed swing rolls up to 70 (nylocas.vasilias_max_hit): the buffer is kept high while she stands
                if boss_style ~= nil then eat_below = 90 end
                local ate = 0
                local out_of_food = false
                while hp_now <= eat_below and ate < ((fight_count >= 4 and hp_now > 70) and 2 or 3) do
                    local item = nil
                    for _, name in ipairs(brew_names) do
                        local brew_result, brew_count = t.inv.count(name)
                        if brew_result == "ok" and brew_count > 0 then item = name break end
                    end
                    if item == nil or hp_now > 84 then
                        local food_result, food = t.inv.count("anglerfish")
                        if food_result == "ok" and food > 0 then item = "anglerfish" end
                    end
                    if item == nil then
                        out_of_food = true
                        break
                    end
                    t.player.inv_op(item, 1)
                    if item == "anglerfish" then eaten = eaten + 1 else brews = brews + 1 end
                    ate = ate + 1
                    lock = nil
                    if item == "anglerfish" then expect_hp = math.min(102, hp_now + 22) else expect_hp = math.min(116, hp_now + 16) end
                    hp_now = expect_hp
                    expect_tick = now
                    expect_until = now + 3
                end
                if out_of_food and hp_now < 35 then fight_end = "out of food at hitpoints " .. hp_now break end
                if hp_now < 25 then fight_end = "left the fight at hitpoints " .. hp_now break end
                -- the supports: the one with the lowest bar is the one let fall (the leader's rule); no chewer on it is killed until it has fallen
                if now >= next_poll then
                    next_poll = now + 4
                    local support_rows_result, _, support_rows = t.npc.tiles("tob_nylocas_support", 40)
                    if support_rows_result == "ok" then
                        support_count = #support_rows
                        if support_count < 4 then fallen = true end
                        if now - tick_start >= 300 then sacrifice_frozen = true end
                        local least, weak_fraction = 2, 2
                        for _, row in ipairs(support_rows) do
                            local fraction = 1
                            if row.health_ratio ~= nil and row.health_ratio >= 0 and row.health_scale ~= nil and row.health_scale > 0 then fraction = row.health_ratio / row.health_scale end
                            if fraction < least then
                                least = fraction
                                if not sacrifice_frozen then doomed = { x = row.x + 1, z = row.z + 1 } end
                            end
                            local is_doomed = (row.x + 1 == doomed.x and row.z + 1 == doomed.z)
                            if (not sacrifice_frozen or not is_doomed) and fraction < weak_fraction then
                                weak_fraction = fraction
                                weak = { x = row.x + 1, z = row.z + 1 }
                            end
                        end
                    end
                end
                -- the target: an aggro of my style first (nearest, small before big), then the newest chewer of my style that stands still
                local target, target_symbol, target_style, on_boss = nil, nil, nil, false
                local best = 9999
                for _, f in ipairs(fight_rows) do
                    local dist_me = tile ~= nil and math.max(math.abs(f.row.x - tile.x), math.abs(f.row.z - tile.z)) or 99
                    -- a healthy raider leaves a far aggro to the prayer and keeps to the chewers (the supports are what the room is lost on)
                    if f.style == my_style and (skips[f.row.slot] or 0) <= now and (hp_now < 82 or fight_count >= 3 or dist_me <= 3) then
                        local score = (f.big and 10 or 0) + math.min(dist_me, 90)
                        if score < best then best, target, target_symbol, target_style = score, f.row, f.symbol, f.style end
                    end
                end
                if target == nil then
                    local incoming_symbol = "tob_nylocas_incoming_" .. my_style
                    local incoming_result, _, incoming_rows = t.npc.tiles(incoming_symbol, 10)
                    -- the chewers on the support let fall, of every style: they are left alone only while four or fewer are on it (a fall hands every
                    -- chewer on it to the raiders at once)
                    local zone_count = 0
                    for _, style in ipairs(style_names) do
                        local zone_result, _, zone_rows = t.npc.tiles("tob_nylocas_incoming_" .. style, 12)
                        if zone_result == "ok" then
                            for _, row in ipairs(zone_rows) do
                                if math.abs(row.x - doomed.x) <= 3 and math.abs(row.z - doomed.z) <= 3 then zone_count = zone_count + 1 end
                            end
                        end
                    end
                    if incoming_result == "ok" then
                        local best_chewer = 9999
                        for _, row in ipairs(incoming_rows) do
                            local pos = row.x .. "," .. row.z
                            local seen = seen_at[row.slot]
                            local stable = seen ~= nil and seen.pos == pos and seen.tick < now
                            if seen == nil or seen.pos ~= pos then seen_at[row.slot] = { pos = pos, tick = now } end
                            local in_zone = math.abs(row.x - doomed.x) <= 3 and math.abs(row.z - doomed.z) <= 3
                            if stable and (fallen or not sacrifice_frozen or not in_zone or zone_count > 4) and (skips[row.slot] or 0) <= now then
                                local score = tile ~= nil and math.max(math.abs(row.x - tile.x), math.abs(row.z - tile.z)) or 0
                                if weak ~= nil and math.abs(row.x - weak.x) <= 4 and math.abs(row.z - weak.z) <= 4 then score = score - 8 end
                                if score < best_chewer then best_chewer, target, target_symbol, target_style = score, row, incoming_symbol, my_style end
                            end
                        end
                    end
                end
                local hold_boss = false
                if target == nil then
                    for _, style in ipairs(style_names) do
                        local boss_result, boss_row = t.npc.state(boss_symbol[style])
                        if boss_result == "ok" then target, target_symbol, target_style, on_boss = boss_row, boss_symbol[style], style, true end
                    end
                    if on_boss then
                        if boss_style ~= target_style then
                            boss_style, boss_since = target_style, now
                            t.prayer.set(prayer_of[target_style], true)
                            prayer_style = target_style
                            prayer_switches = prayer_switches + 1
                        end
                        -- every raider answers every colour: pressed only early in the form's ten ticks (a swing or an arrow lands within a tick or three, a
                        -- cast within four), after the prayer and the weapon swap
                        local age = now - boss_since
                        local limit = 6
                        if target_style == "ranged" then limit = 5 end
                        if target_style == "magic" then limit = 4 end
                        if age > limit and age < 10 then hold_boss = true end
                    elseif boss_style ~= nil then
                        fight_end = "boss gone"
                        break
                    end
                end
                if lock ~= nil then
                    local lock_result, lock_row = t.npc.state(lock.symbol, { slot = lock.slot })
                    if lock_result ~= "ok" or now - lock.tick >= lock.hold or (lock_row.health_ratio ~= nil and lock_row.health_ratio == 0) then lock = nil end
                end
                if lock ~= nil or target == nil or hold_boss then
                    idle = idle + 1
                    t.ticks(1)
                else
                    local desired = weapon[target_style]
                    if wielded ~= desired then
                        if target_style == "ranged" and not ammo_ready then
                            t.player.equip("rune_arrow")
                            ammo_ready = true
                        end
                        if t.player.equip(desired) == "ok" then wielded = desired current = target_style end
                    end
                    local call_result, call_detail
                    if target_style == "magic" then
                        -- a spell, not the powered staff: a powered staff is nulled by a Hagios (CONTENT_BUGS.md, tree-wide magic damage type)
                        call_result, call_detail = t.player.cast("ice_burst", target_symbol, 1, 2, { slot = target.slot })
                        casts = casts + 1
                    else
                        call_result, call_detail = t.player.attack(target_symbol, 2, 1, { slot = target.slot })
                        swings = swings + 1
                    end
                    if on_boss then boss_calls = boss_calls + 1 else waves_calls = waves_calls + 1 end
                    if call_result == "ok" or call_result == "timeout" then
                        lock = { symbol = target_symbol, slot = target.slot, tick = now, hold = on_boss and 3 or hold_of[target_style] }
                    else
                        refused = refused + 1
                        if #first_answers < 4 then first_answers[#first_answers + 1] = tostring(call_result) .. ": " .. string.sub(tostring(call_detail), 1, 120) end
                        skips[target.slot] = now + 3
                        -- a refused or covered press (stale menu, a cast left armed): one step away, then press again
                        local _, here = t.world.tile()
                        if here ~= nil then
                            local step_x = here.x + 1
                            if here.x >= 6434 then step_x = here.x - 1 end
                            t.player.walk_to(step_x, here.z, 2)
                        end
                        steps = steps + 1
                    end
                end
                if iteration % 100 == 0 then
                    t.expect("member.progress" .. iteration, "ok", "p" .. role .. " turn " .. iteration .. ", tick " .. (now - tick_start) .. " after the crossing, hitpoints " .. hp_now
                        .. ", swings " .. swings .. ", casts " .. casts .. ", sharks " .. eaten .. ", brew doses " .. brews .. ", restores " .. restores .. ", supports standing " .. support_count)
                end
            end
            t.check("member.fight", swings + casts > 20, "p" .. role .. " (" .. my_style .. ") left the fight loop by " .. fight_end .. " after " .. (now - (tick_start or now)) .. " ticks: " .. swings .. " swings, " .. casts
                .. " casts, " .. idle .. " idle polls, " .. eaten .. " sharks, " .. brews .. " brew doses, " .. restores .. " restores, lowest hitpoints " .. lowest .. ", prayer switched " .. prayer_switches
                .. " times, calls on waves " .. waves_calls .. " and on Vasilias " .. boss_calls .. ", refused presses " .. refused .. " (" .. table.concat(first_answers, " | ") .. "), supports standing " .. support_count)
            t.expect("party.barrier.fight_done", t.party.barrier("fight_done", 1500))
            -- a member leaves with ::tobout and a tile read; the leader reads its log and leaves by t.raid.leave
            t.cheat("::tobout")
            t.await({ level = function()
                local _, out_check = t.world.tile()
                return type(out_check) == "table" and out_check.x > 3600 and out_check.x < 3700
            end, note = "back in Ver Sinhaza" }, 20)
            local _, out_tile = t.world.tile()
            t.check("raid.left", type(out_tile) == "table" and out_tile.x > 3600 and out_tile.x < 3700, "p" .. role .. " after ::tobout at " .. tostring(out_tile and (out_tile.x .. "," .. out_tile.z)))
            t.expect("party.barrier.end", t.party.barrier("end", 3000))
            return
        end
        local state_result, state = t.raid.state()
        t.check("state", state_result == "ok" and state.room == "nylocas" and state.mode == "normal" and state.started == false and string.find(tostring(state.line), " party=3 ", 1, true) ~= nil,
            type(state) == "table" and tostring(state.line) or tostring(state))
        local tile_result, fight, fight_text = t.raid.start_tile()
        t.check("start_tile", tile_result == "ok", tostring(fight_text))
        -- the locs the room shows, read from the player's side before the fight: a support standing, the spectator webs
        local support_loc_result, support_loc = t.world.loc_near("tob_nylocas_support_pristine", 40)
        local web_loc_ids = {}
        for index, symbol in ipairs({ "tob_dungeon_nylocas_twisty_multi", "tob_nylocas_death_web", "tob_dungeon_nylocas_dead_merc_multi" }) do
            local web_result, web = t.world.loc_near(symbol, 60)
            if web_result == "ok" and type(web) == "table" then web_loc_ids[index] = web.id else web_loc_ids[index] = "none(" .. tostring(web_result) .. ")" end
        end
        local support_loc_id = "none"
        if support_loc_result == "ok" and type(support_loc) == "table" then support_loc_id = support_loc.id end
        t.player.walk_to(fight.x + 1, fight.z, 20)
        do
        local click_result, click_detail = t.player.click_loc("tob_arena_barrier", 1)
        t.check("barrier.click", click_result == "ok", tostring(click_detail))
        local play_result, play_detail = t.chat.play({ "options", "choose:Yes, begin the fight." })
        t.check("barrier.confirm", play_result == "ok", tostring(play_detail))
        end
        t.ticklog.mark("room start")
        local _, tick0 = t.tick()
        t.expect("party.barrier.started", t.party.barrier("started", 900))
        -- the scale is the party that walked in: the content's own readout (::tobstate ends "party=N scale=K")
        do
        local scale_result, scale_state = t.raid.state()
        local scale_line = scale_result == "ok" and tostring(scale_state.line) or tostring(scale_state)
        t.check("room.scale", scale_result == "ok" and string.find(scale_line, " party=3 scale=3", 1, true) ~= nil, scale_line)
        end
        t.drive.camera(0, 512, 1100)
        -- the ranger's twisted bow is worn already (::maxrange); the leader's whip and lava battlestaff ride in the backpack for Vasilias' other two colours
        -- the combat tab's second style button: rapid for the bow (an arrow every three ticks, not four)
        do
        local tab_result, tab_detail = t.ui.tab("combat")
        t.ticks(1)
        local style_widget_result, style_widget = t.ui.widget("combat_interface:style_slot_1")
        local style_press_result, style_press_detail = t.ui.invoke(style_widget, 1)
        t.ticks(2)
        local style_read_result, style_read = t.var.varp("varp43_com_mode")
        t.check("setup.rapid", style_widget_result == "ok" and style_press_result == "ok" and style_read == 1,
            "combat tab style slot 1 pressed for the bow: tab " .. tostring(tab_result) .. " " .. tostring(tab_detail) .. ", widget "
            .. tostring(style_widget_result) .. " " .. tostring(style_widget) .. ", press " .. tostring(style_press_result)
            .. " " .. tostring(style_press_detail) .. ", varp43_com_mode reads " .. tostring(style_read_result) .. " " .. tostring(style_read))
        -- auto-retaliate off (the combat tab's own button): the server would otherwise swing back at whatever hit the player with the
        -- weapon in hand, and a wrong-style swing nulls that nylocas for good (56 nulled hits in one pass)
        local retaliate_before_result, retaliate_before = t.var.varp("varp172_option_nodef")
        local retaliate_widget_result, retaliate_widget = t.ui.widget("combat_interface:retaliate")
        local retaliate_press_result = "skipped"
        if retaliate_before == 0 then retaliate_press_result = t.ui.invoke(retaliate_widget, 1) end
        t.ticks(2)
        local retaliate_after_result, retaliate_after = t.var.varp("varp172_option_nodef")
        t.check("setup.retaliate_off", retaliate_after == 1, "combat tab auto-retaliate button: varp172_option_nodef read " .. tostring(retaliate_before)
            .. " before, press " .. tostring(retaliate_press_result) .. ", " .. tostring(retaliate_after) .. " after (1 is off)")
        end
        -- "Use your offensive prayers at all times in this room" (yt_4i4lv-srJkw.md:83): the ranger's is Rigour
        local off_result, off_detail = t.prayer.set("rigour", true)
        t.expect("setup.offensive_prayer", off_result, "rigour: " .. tostring(off_detail))
        local weapon = { melee = "abyssal_whip", ranged = "twisted_bow", magic = "lava_battlestaff" }
        local style_names = { "melee", "ranged", "magic" }
        local fighting_kinds = { "big_fighting", "fighting" }
        -- (a chewing big is left alone: killing it splits it into two smalls that chew on, so a hit on it costs a press and adds a biter)
        local incoming_kinds = { "incoming" }
        local boss_symbol = { melee = "nylocas_boss_melee", magic = "nylocas_boss_magic", ranged = "nylocas_boss_ranged" }
        local prayer_of = { melee = "protectfrommelee", magic = "protectfrommagic", ranged = "protectfrommissiles", off_melee = "piety", off_ranged = "rigour", off_magic = "augury" }
        -- (the press itself costs a tick, so the wait after it is the weapon's speed less one: whip 4, twisted bow rapid 4, Ice Burst 5)
        local hold_of = { melee = 3, ranged = 3, magic = 4 }
        local current, swings, casts, eaten, idle, lowest, disarms = "ranged", 0, 0, 0, 0, 99, 0
        local prayer_style, prayer_switches, prayer_refusals, next_prayer_poll = nil, 0, 0, 0
        -- client-side readings: the animation the client draws on a copy that stood still (idle) or had just moved (walk), by style;
        -- the boss's own while she stands; her hitpoints in the room's own readout the tick she is first seen
        local idle_seen = { melee = {}, ranged = {}, magic = {} }
        local walk_seen = { melee = {}, ranged = {}, magic = {} }
        local boss_idle = { melee = {}, ranged = {}, magic = {} }
        local gfx_seq_seen = {}
        local boss_walk = { melee = {}, ranged = {}, magic = {} }
        local boss_hp_read = nil
        -- the frames taken at the moment a technique happens (inside the loop, on its tick): name -> taken
        local shot_taken = {}
        -- one deliberate wrong-style swing at a chewer (the graphic a hit that does nothing leaves)
        local probe_done, probe = false, nil
        -- Entangle on a chewing magic nylocas (the freeze row): {world_slot, tick, bites_before}, at most three, each left alone for 20 ticks
        local freeze_probes, freeze_last = {}, -100
        -- the prayer on, by tick: {tick, style} for every switch that was accepted
        local prayer_log = {}
        local wave_unprayed, wave_unprayed_zero, wave_unprayed_landed = 0, 0, 0
        local wave_block_count, wave_cover_landed, melee_block_count, melee_cover_landed = 0, 0, 0, 0
        -- one deliberate wrong-style hit on Vasilias (the reflect row): pressed with the whip while she stands in a magic or ranged form
        local reflect_tries, reflect_slot, reflect_press_ticks, reflect_healed, reflect_last = 0, nil, {}, nil, nil
        local reflect_press_max = {}
        -- strength level readings, one per prayer poll and one per reflect press: {tick, level}
        local strength_samples = {}
        local brews, restores, brew_doses, top_up = 0, 0, 0, false
        local burst = { centers = {}, count = 0, frozen = {}, press = nil, next_poll = 0, tile_serial = 0, sound_serial = 0, ptiles = {}, sounds = {} }
        local expect_hp, expect_until, expect_tick, next_trace, last_fallen = 0, 0, 0, 0, 0
        local equip_bad, equip_samples, pending, swap_holds = 0, {}, nil, 0
        local brew_names = { "1dosepotionofsaradomin", "2dosepotionofsaradomin", "3dosepotionofsaradomin", "4dosepotionofsaradomin" }
        local restore_names = { "1dose2restore", "2dose2restore", "3dose2restore", "4dose2restore" }
        local boss_style, boss_calls, waves_calls, slow = nil, 0, 0, {}
        local boss_forms = {}
        local fight_end = "tick cap"
        local results = {}
        local samples, under_attack = {}, 0
        local small_hp_read, big_hp_read, support_hp_read = nil, nil, nil
        -- where the ticks go: equip, eat, press, step-away, waiting for a locked target
        local cost = { equip = 0, eat = 0, press = 0, step = 0, wait = 0, pray = 0 }
        local lock, skips, trace = nil, {}, {}
        local seen_at = {}
        local hang_events, hang_pending, hang_ticks = {}, nil, 0
        local ring, ring_count = {}, 0
        local pillars_fallen, next_pillar_poll, next_progress, steps_away = 0, 0, 60, 0
        local boss_seen_tick, boss_hp_line = nil, nil
        -- the support that is let fall (the south-west one): the collapse rows need one fall, and a fall hands every chewer on it to
        -- the player at once (13 hit together in the last pass), so its chewers are left alone only while five or fewer are on it
        local pillar_result, _, pillar_rows = t.npc.tiles("tob_nylocas_support", 40)
        local doomed = { x = -100, z = -100 }
        if pillar_result == "ok" then
            for _, row in ipairs(pillar_rows) do
                if doomed.x < 0 or (row.x + row.z) < (doomed.x + doomed.z) then doomed = { x = row.x + 1, z = row.z + 1 } end
            end
        end
        local zone_cap, zone_peak, next_support_poll, min_fraction = 99, 0, 0, 1
        -- the supports are all kept up until tick 300 after the click, the chewers on the one with the lowest bar first; from then on the weakest one is the
        -- one let fall (the collapse rows need one fall) and the other three are kept up (Entry Mode page :160-166 "let a support that is about to fall go and
        -- keep the other three up", DRIVER_NOTES "The Nylocas within one kit")
        local sacrifice_frozen, weak = false, nil
        local brace_logged, brace_text = false, nil
        local next_bite_poll, bite_serial, bite_taken, brace_until, braces = 0, 0, {}, 0, 0
        local last_left = nil
        local death_serial = 0
        -- a nylocas that took two hits for nothing is a miss twice over or nulled for good: it is not pressed again
        local dud_serial, zero_count, dud_slot = 0, {}, {}
        local _, serial_rows = t.ticklog.rows({ kind = "mark" })
        for _, mark_row in ipairs(serial_rows) do death_serial = mark_row.serial end
        for iteration = 1, 9000 do
            local _, now = t.tick()
            if burst.cast ~= nil then
                -- every nylocas within one tile of the last cast's target is under the burst too (the 3x3 of Ice Burst, a big is two tiles wide so its south-west corner can sit two tiles off): their next gaps are a freeze's
                for index = 1, 3 do
                    for _, kind in ipairs({ "incoming", "big_incoming" }) do
                        local near_result, _, near_rows = t.npc.tiles("tob_nylocas_" .. kind .. "_" .. style_names[index] .. "", 12)
                        if near_result == "ok" then
                            for _, near_row in ipairs(near_rows) do
                                if math.abs(near_row.x - burst.cast.x) <= (string.find(kind, "big", 1, true) and 2 or 1) and math.abs(near_row.z - burst.cast.z) <= (string.find(kind, "big", 1, true) and 2 or 1) then
                                    local _, member_slot = t.ticklog.slot(near_row)
                                    if type(member_slot) == "number" then burst.frozen[#burst.frozen + 1] = { world_slot = member_slot, tick = burst.cast.tick } end
                                end
                            end
                        end
                    end
                end
                burst.cast = nil
            end
            if now - tick0 >= 1700 then fight_end = "tick cap, 1700 ticks after the click" break end
            if now - tick0 >= next_progress then
                next_progress = next_progress + 100
                local _, deaths_now = t.ticklog.rows({ kind = "npc_death" })
                local _, lv_att = t.skill.read("attack")
                local _, lv_str = t.skill.read("strength")
                local _, lv_rng = t.skill.read("ranged")
                local _, lv_mag = t.skill.read("magic")
                t.expect("fight.levels" .. (now - tick0), "ok", "attack " .. tostring(lv_att and lv_att.level) .. " strength " .. tostring(lv_str and lv_str.level)
                    .. " ranged " .. tostring(lv_rng and lv_rng.level) .. " magic " .. tostring(lv_mag and lv_mag.level))
                t.expect("fight.progress" .. (now - tick0), "ok", "tick " .. (now - tick0) .. " after the click, npc deaths " .. #deaths_now
                    .. ", swings " .. swings .. ", casts " .. casts .. ", idle " .. idle .. ", sharks " .. eaten .. ", brew doses " .. brews .. ", restores " .. restores .. ", swap holds " .. swap_holds .. ", equip failures " .. equip_bad .. " " .. table.concat(equip_samples, " | ") .. ", lowest hp " .. lowest .. ", prayer switches " .. prayer_switches .. " refused " .. prayer_refusals .. " pray ticks " .. cost.pray
                    .. ", pillars fallen " .. pillars_fallen .. ", ticks spent equip " .. cost.equip .. " eat " .. cost.eat .. " press " .. cost.press
                    .. " step " .. cost.step .. " wait " .. cost.wait)
            end
            -- the last polls (tick:hp:eaten+press), written every eight ticks once the room is full
            if now - tick0 >= 370 and now - tick0 >= next_trace then
                next_trace = now - tick0 + 8
                local tail = {}
                for i = math.max(1, ring_count - 11), ring_count do tail[#tail + 1] = ring[(i - 1) % 60 + 1] end
                t.expect("fight.trace" .. (now - tick0), "ok", "pillars fallen " .. pillars_fallen .. ", sharks " .. eaten .. ", brew doses " .. brews .. ": " .. table.concat(tail, " "))
            end
            -- the room's own hitpoint readout (::tobwhy only prints what it reads): the first wave's smalls are untouched
            -- six ticks in, and the first big seen is untouched when this loop first sees it
            local big_near = nil
            if big_hp_read == nil and now - tick0 >= 8 then
                local big_result = t.npc.nearest("tob_nylocas_big_incoming_melee", 16)
                if big_result ~= "ok" then big_result = t.npc.nearest("tob_nylocas_big_incoming_ranged", 16) end
                if big_result ~= "ok" then big_result = t.npc.nearest("tob_nylocas_big_incoming_magic", 16) end
                if big_result == "ok" then big_near = true end
            end
            if (small_hp_read == nil and now - tick0 >= 6) or big_near then
                t.cheat("::tobwhy")
                t.ticks(1)
                local _, lines = t.msg.last(60)
                local count_at, count = nil, 0
                for l = 1, #lines do
                    local k = string.match(lines[l].text, "npcs in instance=(%d+)")
                    if k ~= nil then count_at, count = l, tonumber(k) break end
                end
                if count_at ~= nil then
                    local small_top, big_top, support_top = 0, 0, 0
                    for l = count_at + 1, math.min(#lines, count_at + count) do
                        local value = tonumber(string.match(lines[l].text, "hp=(%d+)"))
                        if value ~= nil then
                            if value >= 100 then
                                if value > support_top then support_top = value end
                            elseif value > big_top then
                                big_top = value
                            end
                        end
                    end
                    if small_hp_read == nil then
                        small_hp_read, support_hp_read = big_top, support_top
                    else
                        big_hp_read = big_top
                    end
                end
            end
            -- hitpoints first, every poll: eat at 74 or under (a shark heals 20), up to three in a row, and the eat drops the engagement
            local hp_result, hp = t.skill.read("hitpoints")
            local _, tile = t.world.tile()
            if tile ~= nil and tile.x < 6000 then fight_end = "player died (respawned at " .. tile.x .. ")" break end
            -- every raider's tile by tick and the synth sounds the leader hears, read from the log as they are written (a 4,000-row fetch at the end exhausts
            -- one resume's instruction budget; fifty ticks of rows do not): for the self-destruct reach row and the support's sound rows
            if now >= burst.next_poll then
                burst.next_poll = now + 40
                local _, fresh_tiles = t.ticklog.rows({ kind = "player_tile", since = burst.tile_serial })
                for _, tile_row in ipairs(fresh_tiles) do
                    if tile_row.serial > burst.tile_serial then burst.tile_serial = tile_row.serial end
                    local at = burst.ptiles[tile_row.tick]
                    if at == nil then at = {} burst.ptiles[tile_row.tick] = at end
                    at[tile_row.pid] = { tile_row.x, tile_row.z }
                end
                local _, fresh_sounds = t.ticklog.rows({ kind = "sound", since = burst.sound_serial })
                for _, sound_row in ipairs(fresh_sounds) do
                    if sound_row.serial > burst.sound_serial then burst.sound_serial = sound_row.serial end
                    if sound_row.source == "synth" then burst.sounds[#burst.sounds + 1] = { sound_row.tick, sound_row.sound } end
                end
            end
            local hp_now = 99
            if hp_result == "ok" and hp.level ~= nil then hp_now = hp.level end
            local guess = expect_hp - 5 * (select(2, t.tick()) - expect_tick)
            if select(2, t.tick()) < expect_until and hp_now >= 60 and hp_now < guess then hp_now = guess end
            if hp_now < lowest then lowest = hp_now end
            -- the client's own sequence on a small's death and detonation graphics (spotanim rows read seq -1 until the element is bound)
            local gfx_result, gfx_rows = t.world.spotanims(24)
            if gfx_result == "ok" and type(gfx_rows) == "table" then
                for _, g in ipairs(gfx_rows) do
                    if g.spotanim_id ~= nil and g.spotanim_id >= 1562 and g.spotanim_id <= 1567 and g.seq ~= nil and g.seq >= 0 then
                        gfx_seq_seen[g.spotanim_id] = g.seq
                    end
                end
            end
            -- the more copies swinging, the higher the hitpoints are kept: one press can hang for twenty ticks on a covered big
            -- every fighting copy in view (the nearest of each of the six forms), and whether it has held its tile since an earlier
            -- tick: a press aimed at a walking copy answers covered, and the cover recovery then spends twenty to forty ticks
            local fight_rows = {}
            local sampled_polls = 0
            for _, kind in ipairs(fighting_kinds) do
                for index = 1, 3 do
                    local symbol = "tob_nylocas_" .. kind .. "_" .. style_names[index] .. ""
                    local near_result, _, near_rows = t.npc.tiles(symbol, 16)
                    if near_result == "ok" then
                        for _, row in ipairs(near_rows) do
                            local pos = row.x .. "," .. row.z
                            local seen = seen_at[row.slot]
                            local stable = seen ~= nil and seen.pos == pos and seen.tick < now
                            local moved = seen ~= nil and seen.pos ~= pos
                            if seen == nil or seen.pos ~= pos then seen_at[row.slot] = { pos = pos, tick = now } end
                            if now - tick0 < 260 and sampled_polls < 3 then
                                local state_result, state_row = t.npc.state(symbol, { slot = row.slot })
                                if state_result == "ok" and state_row.pose_anim ~= nil and state_row.pose_anim >= 0 then
                                    local bucket = nil
                                    if state_row.pose_kind == "ready" then bucket = idle_seen[style_names[index]] end
                                    if state_row.pose_kind == "walk" then bucket = walk_seen[style_names[index]] end
                                    if bucket ~= nil then
                                        bucket[state_row.pose_anim] = (bucket[state_row.pose_anim] or 0) + 1
                                        sampled_polls = sampled_polls + 1
                                    end
                                end
                            end
                            fight_rows[#fight_rows + 1] = { row = row, symbol = symbol, style = style_names[index], stable = stable, big = (kind == "big_fighting") }
                        end
                    end
                end
            end
            local fight_count = #fight_rows
            -- the protection prayer of the aggro majority (a big counts two, a melee copy only inside two tiles), switched before the
            -- swing tick; the waves' swings of the matching style are blocked to 0 (seam6)
            if fight_count > 0 and tile ~= nil then
                local weight = { melee = 0, ranged = 0, magic = 0 }
                for _, f in ipairs(fight_rows) do
                    local near = math.max(math.abs(f.row.x - tile.x), math.abs(f.row.z - tile.z))
                    if f.style ~= "melee" or near <= 4 then weight[f.style] = weight[f.style] + (f.big and 2 or 1) end
                end
                local best_style = prayer_style
                local best_weight = prayer_style ~= nil and weight[prayer_style] or 0
                for _, style in ipairs(style_names) do
                    if weight[style] > best_weight then best_style, best_weight = style, weight[style] end
                end
                if best_style ~= nil and best_style ~= prayer_style then
                    local before_pray = select(2, t.tick())
                    local pray_result = t.prayer.set(prayer_of[best_style], true)
                    if pray_result == "ok" then
                        prayer_style = best_style
                        prayer_switches = prayer_switches + 1
                        prayer_log[#prayer_log + 1] = { select(2, t.tick()), best_style }
                        if prayer_switches <= 3 then t.shot("tech.prayer.switch" .. prayer_switches .. "_" .. best_style) end
                    else
                        prayer_refusals = prayer_refusals + 1
                    end
                    cost.pray = cost.pray + (select(2, t.tick()) - before_pray)
                end
            end
            if now >= next_prayer_poll then
                next_prayer_poll = now + 4
                local _, points = t.skill.read("prayer")
                local _, drained = t.skill.read("strength")
                if type(drained) == "table" and tonumber(drained.level) then strength_samples[#strength_samples + 1] = { select(2, t.tick()), tonumber(drained.level) } end
                if type(points) == "table" and points.level ~= nil and (points.level < 14 or (type(drained) == "table" and drained.level ~= nil and drained.level < 74)) then
                    for _, name in ipairs(restore_names) do
                        local restore_result, restore_count = t.inv.count(name)
                        if restore_result == "ok" and restore_count > 0 then
                            t.player.inv_op(name, 1)
                            restores = restores + 1
                            break
                        end
                    end
                end
            end
            local eat_below = 78 + 2 * math.min(fight_count, 3)
            -- a swarm (four or more copies swinging) takes twelve hitpoints a tick: the buffer is kept high and no more than two doses go down in one block
            if fight_count >= 4 then eat_below = 88 end
            -- Vasilias' unprayed swing rolls up to 70 (nylocas.vasilias_max_hit): the buffer is kept high while she stands
            if boss_style ~= nil then eat_below = 90 end
            if top_up then eat_below = 100 end
            local ate = 0
            local out_of_food = false
            while hp_now <= eat_below and ate < ((fight_count >= 4 and hp_now > 70) and 2 or 3) do
                -- a brew first while the hitpoints are 84 or under (it adds 16 and overheals to 116), a shark under 78
                local item = nil
                if hp_now <= eat_below or top_up then
                    for _, name in ipairs(brew_names) do
                        local brew_result, brew_count = t.inv.count(name)
                        if brew_result == "ok" and brew_count > 0 then item = name break end
                    end
                end
                if item == nil and hp_now <= 78 + 2 * math.min(fight_count, 3) then
                    local food_result, food = t.inv.count("anglerfish")
                    if food_result == "ok" and food > 0 then item = "anglerfish" end
                end
                if item == nil then
                    if hp_now <= 78 then out_of_food = true end
                    break
                end
                local before_eat = select(2, t.tick())
                -- (the bare backpack press left the player in the drink's delay and the next cast was refused: 9 of 9 refusals followed one)
                t.player.inv_op(item, 1)
                if item == "anglerfish" then eaten = eaten + 1 else brews = brews + 1 end
                if not shot_taken["food" .. item] then shot_taken["food" .. item] = true t.shot("tech.food.eat_" .. item) end
                ate = ate + 1
                lock = nil
                -- the hitpoint read lags the eat by one to three ticks: do not wait for it, count the heal (a brew gives 16, to 116; a
                -- shark 20, to 99) and trust the read again three ticks on
                if item == "anglerfish" then
                    expect_hp = math.min(102, hp_now + 22)
                else
                    expect_hp = math.min(116, hp_now + 16)
                end
                hp_now = expect_hp
                expect_tick = select(2, t.tick())
                expect_until = expect_tick + 3
                if item ~= "anglerfish" then
                    brew_doses = brew_doses + 1
                    -- every second dose a super restore puts the drained attack, strength, ranged and magic back
                    if brew_doses >= 99 then
                        for _, name in ipairs(restore_names) do
                            local restore_result, restore_count = t.inv.count(name)
                            if restore_result == "ok" and restore_count > 0 then
                                t.player.inv_op(name, 1)
                                restores = restores + 1
                                break
                            end
                        end
                        brew_doses = 0
                    end
                end
                cost.eat = cost.eat + (select(2, t.tick()) - before_eat)
                if hp_now < lowest then lowest = hp_now end
            end
            top_up = false
            ring_count = ring_count + 1
            ring[(ring_count - 1) % 60 + 1] = (now - tick0) .. ":" .. hp_now .. ":e" .. ate
            if out_of_food and hp_now < 35 then fight_end = "out of food at hitpoints " .. hp_now break end
            -- the swarm of bigs that arrives with waves 20 and on killed the player from 85 to 0 in fifteen ticks (a brew heals 16 a tick at
            -- best, seven bigs hit 12): leave while the room can still be left, so the log is graded and not lost to player.died
            -- the room is left while it can be left (a death aborts the run and the log is never graded): hitpoints under 40 with a swarm on
            -- the player, or the last standing support nearly gone (its collapse is the wipe)
            if pillars_fallen >= 3 and last_left ~= nil and last_left <= 10 then fight_end = "the last support was about to fall (" .. last_left .. " hitpoints left, its collapse is the wipe)" break end
            if (hp_now < 40 and fight_count >= 2) or hp_now < 30 then fight_end = "left the room at hitpoints " .. hp_now .. " with " .. fight_count .. " copies swinging, after eating" break end
            -- a press that took six ticks or more: what it cost in hitpoints is read here, on the poll after it
            if hang_pending ~= nil then
                hang_events[#hang_events + 1] = hang_pending.text .. " hp " .. hang_pending.hp .. "->" .. hp_now
                hang_pending = nil
            end
            -- a support that fell: from then on the chewers are killed too (the first fall is left to happen, the collapse rows need it)
            if now >= next_pillar_poll then
                next_pillar_poll = now + 6
                local _, fell = t.ticklog.rows({ kind = "npc_death", type = 8358 })
                pillars_fallen = #fell
                if pillars_fallen > last_fallen then
                    last_fallen = pillars_fallen
                    if hp_now < 100 then top_up = true end
                end
            end
            -- the support nearest to falling (the lowest health bar) is the one let fall: its chewers are left alone until its bar is
            -- nearly empty, then cut to three, with the hitpoints topped up first, so the collapse hands the player three attackers and
            -- not the thirteen of an unattended fall
            if pillars_fallen == 0 and now >= next_support_poll then
                next_support_poll = now + 4
                local support_result, _, support_rows = t.npc.tiles("tob_nylocas_support", 40)
                if support_result == "ok" then
                    if now - tick0 >= 300 then sacrifice_frozen = true end
                    min_fraction = 2
                    local weak_fraction = 2
                    for _, row in ipairs(support_rows) do
                        local fraction = 1
                        if row.health_ratio ~= nil and row.health_ratio >= 0 and row.health_scale ~= nil and row.health_scale > 0 then
                            fraction = row.health_ratio / row.health_scale
                        end
                        if fraction < min_fraction then
                            min_fraction = fraction
                            if not sacrifice_frozen then doomed = { x = row.x + 1, z = row.z + 1 } end
                        end
                        local is_doomed = (row.x + 1 == doomed.x and row.z + 1 == doomed.z)
                        if (not sacrifice_frozen or not is_doomed) and fraction < weak_fraction then
                            weak_fraction = fraction
                            weak = { x = row.x + 1, z = row.z + 1 }
                        end
                    end
                    zone_cap = 99
                end
            end
            -- a support about to fall: the bites on each standing support (hit_npc rows, read since the last poll) against the hitpoints the
            -- room's readout gave one, and the hitpoints are topped up before its collapse lands on the raider three ticks after the fall
            if now >= next_bite_poll then
                next_bite_poll = now + 3
                local _, new_bites = t.ticklog.rows({ kind = "hit_npc", type = 8358, since = bite_serial })
                for _, b in ipairs(new_bites) do
                    if b.serial > bite_serial then bite_serial = b.serial end
                    bite_taken[b.slot] = (bite_taken[b.slot] or 0) + (b.damage or 0)
                end
                local _, fallen_rows = t.ticklog.rows({ kind = "npc_death", type = 8358 })
                local fallen_slots = {}
                for _, d in ipairs(fallen_rows) do fallen_slots[d.slot] = true end
                local support_total = support_hp_read or 155
                local lowest_left = nil
                for slot, taken in pairs(bite_taken) do
                    if not fallen_slots[slot] and (lowest_left == nil or support_total - taken < lowest_left) then lowest_left = support_total - taken end
                end
                last_left = lowest_left
                if lowest_left ~= nil and lowest_left <= 34 and now >= brace_until then
                    brace_until = now + 40
                    braces = braces + 1
                    if hp_now < 110 then top_up = true end
                    brace_text = (brace_text or "") .. (now - tick0) .. ":" .. lowest_left .. " "
                end
            end
            if zone_cap == 3 and pillars_fallen == 0 and not brace_logged then
                brace_logged = true
                top_up = true
                brace_text = "support bar at " .. string.format("%.2f", min_fraction) .. " on tick " .. (now - tick0) .. " at hitpoints " .. hp_now
                    .. ": hitpoints topped up on the next pass, chewers on it cut to three"
            end
            -- who swings at the player now (read first, so a press on a chewer is dropped for a fresh aggro)
            local target, target_symbol, target_style, on_boss = nil, nil, nil, false
            local first = 1
            for index, name in ipairs(style_names) do
                if name == current then first = index end
            end
            local best_fight = 9999
            for _, f in ipairs(fight_rows) do
                local score = 0
                if not f.big then score = score + 10 end
                if f.style ~= current then score = score + 3 end
                -- the ranger meets the green aggros (yt_4i4lv-srJkw.md:81); a gray or blue one is the melee raider's and the mage's unless it stands within two
                -- tiles of the ranger and is plainly swinging at it
                local dist_me = tile ~= nil and math.max(math.abs(f.row.x - tile.x), math.abs(f.row.z - tile.z)) or 99
                local near_me = dist_me <= 2
                if f.style ~= "ranged" and not near_me then score = score + 1000 end
                -- the supports are what the room is lost on: a healthy raider leaves a far aggro to the prayer and keeps to the chewers (the first two aggros
                -- of a swarm included); below 82 hitpoints, or with three copies swinging, or one within three tiles, the aggros come first
                if hp_now >= 82 and fight_count < 3 and dist_me > 3 then score = score + 1000 end
                -- (a swinging copy is blocked by the prayer: it is pressed only when the hitpoints are low or it is a big)
                if f.style == prayer_style then score = score + 40 end
                if (skips[f.row.slot] or 0) <= now and score < best_fight and score < 1000 then
                    best_fight = score
                    target, target_symbol, target_style = f.row, f.symbol, f.style
                end
            end
            -- is the locked copy still there, and has its press had time to land
            burst.press = nil
            local waiting = false
            local hold_boss, boss_age_text = false, nil
            if lock ~= nil then
                -- the kill is in the tick log the tick it lands: free the next press without waiting out the corpse
                local _, new_deaths = t.ticklog.rows({ kind = "npc_death", since = death_serial })
                for _, d in ipairs(new_deaths) do
                    if d.serial > death_serial then death_serial = d.serial end
                    if d.slot == lock.world_slot then
                        skips[lock.slot] = now + 4
                        lock = nil
                        break
                    end
                end
            end
            if lock ~= nil then
                local state_result = t.npc.state(lock.symbol, { slot = lock.slot })
                if state_result == "ok" and now - lock.tick < lock.hold then waiting = true else lock = nil end
            end
            if waiting then
                ring[(ring_count - 1) % 60 + 1] = ring[(ring_count - 1) % 60 + 1] .. "+w"
                t.ticks(1)
                cost.wait = cost.wait + 1
            else
                -- nothing swings at the player: kill the chewers, nearest first, except those on the support let fall
                -- (a swinging copy of a style the prayer does not cover is pressed before any chewer)
                if best_fight >= 40 then
                    local candidates, zone_count = {}, 0
                    -- (no cast at a chewer: a cast left armed refused 29 of the next presses and the kill rate fell by two thirds)
                    for index = 1, 3 do
                        for _, kind in ipairs(incoming_kinds) do
                            local symbol = "tob_nylocas_" .. kind .. "_" .. style_names[index] .. ""
                            local tiles_result, _, rows = t.npc.tiles(symbol, 10)
                            if tiles_result == "ok" then
                                for _, row in ipairs(rows) do
                                    local pos = row.x .. "," .. row.z
                                    local seen = seen_at[row.slot]
                                    local stable = seen ~= nil and seen.pos == pos and seen.tick < now
                                    if seen == nil or seen.pos ~= pos then seen_at[row.slot] = { pos = pos, tick = now } end
                                    local in_zone = math.abs(row.x - doomed.x) <= 3 and math.abs(row.z - doomed.z) <= 3
                                    if in_zone then zone_count = zone_count + 1 end
                                    if stable then candidates[#candidates + 1] = { row = row, symbol = symbol, style = style_names[index], zone = in_zone } end
                                end
                            end
                        end
                    end
                    if zone_count > zone_peak then zone_peak = zone_count end
                    local best_score = 9999
                    local _, fresh_hits = t.ticklog.rows({ kind = "hit_npc", since = dud_serial })
                    for _, hit_row in ipairs(fresh_hits) do
                        if hit_row.serial > dud_serial then dud_serial = hit_row.serial end
                        if hit_row.type ~= 8358 then
                            if (hit_row.damage or 0) == 0 then
                                zero_count[hit_row.slot] = (zero_count[hit_row.slot] or 0) + 1
                                if zero_count[hit_row.slot] >= 2 then dud_slot[hit_row.slot] = true end
                            end
                        end
                    end
                    -- age of each candidate from its npc_spawn row: every nylocas explodes on its own at lifetime 52, so a chewer bites a support
                    -- once every three ticks for the rest of its life; the youngest copy saves the most bites and one about to explode saves none
                    local spawn_tick_of = {}
                    if #candidates > 0 then
                        local _, spawn_rows = t.ticklog.rows({ kind = "npc_spawn" })
                        for _, spawn_row in ipairs(spawn_rows) do spawn_tick_of[spawn_row.slot] = spawn_row.tick end
                    end
                    for _, c in ipairs(candidates) do
                        local score = 0
                        if tile ~= nil then score = math.max(math.abs(c.row.x - tile.x), math.abs(c.row.z - tile.z)) end
                        -- the ranger kills the green chewers (yt_4i4lv-srJkw.md:81); a gray one is the melee raider's and a blue one the mage's, except the one
                        -- deliberate wrong-style probe at a gray chewer (the graphic a hit that does nothing leaves)
                        if c.style ~= "ranged" then
                            if not probe_done and c.style == "melee" then score = score + 30 else score = score + 1000 end
                        end
                        -- no chewer on the support let fall is killed until it has fallen, while four or fewer are on it (a fall hands every chewer to the raiders)
                        if c.zone and sacrifice_frozen and pillars_fallen == 0 and zone_count <= 4 then score = score + 1000 end
                        -- "if you see a pillar getting low, it's okay to focus crabs on that pillar" (yt_4i4lv-srJkw.md:83)
                        if weak ~= nil and math.abs(c.row.x - weak.x) <= 4 and math.abs(c.row.z - weak.z) <= 4 then score = score - 8 end
                        local _, candidate_world_slot = t.ticklog.slot(c.row)
                        if dud_slot[candidate_world_slot] then score = score + 1000 end
                        local born = spawn_tick_of[candidate_world_slot]
                        if born ~= nil then
                            local age = now - born
                            if age >= 42 then score = score + 100 else score = score + math.floor(age / 4) end
                        end
                        if (skips[c.row.slot] or 0) <= now and score < best_score and score < 100 then
                            best_score = score
                            target, target_symbol, target_style = c.row, c.symbol, c.style
                        end
                    end
                end
                if target == nil then
                    for _, style in ipairs({ "melee", "ranged", "magic" }) do
                        local state_result, row = t.npc.state(boss_symbol[style])
                        if state_result == "ok" then
                            target, target_symbol, target_style, on_boss = row, boss_symbol[style], style, true
                            if row.pose_kind == "ready" and row.pose_anim ~= nil and row.pose_anim >= 0 then boss_idle[style][row.pose_anim] = (boss_idle[style][row.pose_anim] or 0) + 1 end
                            if row.walk_anim ~= nil and row.walk_anim >= 0 then boss_walk[style][row.walk_anim] = (boss_walk[style][row.walk_anim] or 0) + 1 end
                        end
                    end
                    if on_boss and boss_seen_tick == nil then boss_seen_tick = now end
                    -- her hitpoints in the room's own readout, the first poll she is seen (no hit has landed on her yet)
                    if on_boss and boss_hp_read == nil then
                        t.cheat("::tobwhy")
                        t.ticks(1)
                        local _, boss_lines = t.msg.last(60)
                        local boss_count_at, boss_count = nil, 0
                        for l = 1, #boss_lines do
                            local k = string.match(boss_lines[l].text, "npcs in instance=(%d+)")
                            if k ~= nil then boss_count_at, boss_count = l, tonumber(k) break end
                        end
                        local boss_top = 0
                        if boss_count_at ~= nil then
                            for l = boss_count_at + 1, math.min(#boss_lines, boss_count_at + boss_count) do
                                local value = tonumber(string.match(boss_lines[l].text, "hp=(%d+)"))
                                if value ~= nil and string.find(boss_lines[l].text, "upport", 1, true) == nil and value > boss_top then boss_top = value end
                            end
                        end
                        boss_hp_read = boss_top
                    end
                    if on_boss and boss_style ~= target_style then
                        boss_style = target_style
                        boss_forms[#boss_forms + 1] = target_style
                        local boss_pray_result = t.prayer.set(prayer_of[target_style], true)
                        if boss_pray_result == "ok" then prayer_log[#prayer_log + 1] = { select(2, t.tick()), target_style } end
                    end
                    if not on_boss and boss_style ~= nil then fight_end = "boss gone" t.shot("fight.kill_vasilias") break end
                    -- a hit that lands after she turns is the wrong style and nulls her for good: press only early in a form's ten ticks
                    -- (a cast flies six ticks, so it is pressed in the first three; a swing or an arrow in the first six)
                    if on_boss then
                        local _, boss_world_slot = t.ticklog.slot(target)
                        local _, turns = t.ticklog.rows({ kind = "npc_retype", slot = boss_world_slot })
                        local last_turn = turns[#turns]
                        if last_turn ~= nil then
                            local age = now - last_turn.tick
                            local limit = 5
                            if target_style == "magic" then limit = 3 end
                            if age > limit and age < 10 then hold_boss = true end
                            boss_age_text = "form age " .. age
                        end
                    end
                end
                -- a swing or a dart in flight is judged by the weapon in hand when it LANDS (the damage type is the player's, read at the
                -- hit): a swap before the hit_npc row turns it into a wrong-style hit and nulls that nylocas for good
                local hold_swap = false
                if target ~= nil and current ~= target_style and pending ~= nil and now - pending.tick < 6 then
                    local pending_done = false
                    local _, landed = t.ticklog.rows({ kind = "hit_npc", slot = pending.world_slot })
                    for _, r in ipairs(landed) do
                        if r.tick >= pending.tick then pending_done = true break end
                    end
                    if not pending_done then
                        local _, dead = t.ticklog.rows({ kind = "npc_death", slot = pending.world_slot })
                        for _, r in ipairs(dead) do
                            if r.tick >= pending.tick then pending_done = true break end
                        end
                    end
                    if not pending_done then hold_swap = true swap_holds = swap_holds + 1 end
                end
                -- a reflect that healed her is read from the log a few ticks after the press (a missed roll reflects 0 and heals nothing)
                if reflect_last ~= nil and now > reflect_last + 3 then
                    local _, heal_rows = t.ticklog.rows({ kind = "npc_heal", slot = reflect_slot })
                    local healed_count = 0
                    for _, heal_row in ipairs(heal_rows) do
                        if heal_row.tick >= reflect_press_ticks[1] and string.find(tostring(heal_row.source), "tob_prepare_player_hit", 1, true) ~= nil then healed_count = healed_count + 1 end
                    end
                    if healed_count >= 3 then reflect_healed = true end
                end
                local boss_probe = false
                -- her heal is written only when her level rises: probe while she is hurt by at least a tenth of her bar, so no reflect is wasted on a full bar
                local boss_hurt = true
                if target ~= nil and target.health_ratio ~= nil and target.health_ratio >= 0 and target.health_scale ~= nil and target.health_scale > 0 then
                    boss_hurt = target.health_ratio / target.health_scale <= 0.97
                end
                if on_boss and boss_hurt and reflect_tries < 9 and reflect_healed == nil and target_style ~= "melee" and boss_calls >= 2 and (reflect_last == nil or now > reflect_last + 4) then
                    boss_probe = true
                    hold_boss = false
                end
                if target == nil or hold_boss or hold_swap then
                    idle = idle + 1
                    t.ticks(1)
                    cost.wait = cost.wait + 1
                else
                    local wrong_probe = false
                    if not probe_done and burst.press == nil and not on_boss and now - tick0 >= 20 and target_style ~= current and target_style ~= "magic" and current ~= "magic"
                        and target_symbol ~= nil and string.find(target_symbol, "_incoming_", 1, true) ~= nil and target.x ~= nil and target_style == "melee" and current == "ranged" then
                        wrong_probe = true
                    end
                    local freeze_cast, freeze_world_slot, freeze_bites = false, nil, 0
                    local press_style = target_style
                    if boss_probe then press_style = "melee" end
                    if burst.press ~= nil then press_style = "magic" end
                    if freeze_cast then press_style = "magic" end
                    if current ~= press_style and not wrong_probe then
                        local before_equip = select(2, t.tick())
                        local equip_result, equip_detail = t.player.equip(weapon[press_style])
                        if equip_result == "ok" then
                            current = press_style
                            -- (the offensive prayer follows the weapon in the waves; against Vasilias the swap ticks are for the protection prayer and the press)
                            if not on_boss then t.prayer.set(prayer_of["off_" .. press_style], true) end
                        else
                            equip_bad = equip_bad + 1
                            if #equip_samples < 4 then equip_samples[#equip_samples + 1] = weapon[target_style] .. " " .. tostring(equip_result) .. ": " .. string.sub(tostring(equip_detail), 1, 160) end
                        end
                        cost.equip = cost.equip + (select(2, t.tick()) - before_equip)
                    end
                    local before = select(2, t.tick())
                    local call_result, call_detail
                    if press_style == "magic" and not wrong_probe then
                        call_result, call_detail = t.player.cast("ice_burst", target_symbol, 1, 2, { slot = target.slot })
                        if freeze_cast then
                            freeze_last = now
                            freeze_probes[#freeze_probes + 1] = { world_slot = freeze_world_slot, tick = before, bites_before = freeze_bites, answer = tostring(call_result) }
                            skips[target.slot] = now + 20
                        end
                        casts = casts + 1
                        if not on_boss and target.x ~= nil then burst.cast = { x = target.x, z = target.z, tick = before } end
                    else
                        call_result, call_detail = t.player.attack(target_symbol, 2, 1, { slot = target.slot })
                        swings = swings + 1
                    end
                    if wrong_probe then
                        probe_done = true
                        local _, probe_world_slot = t.ticklog.slot(target)
                        probe = { world_slot = probe_world_slot, tick = before, x = target.x, z = target.z, style = target_style, weapon = current, answer = tostring(call_result) }
                    end
                    if boss_probe and call_result == "ok" then
                        local _, probe_boss_slot = t.ticklog.slot(target)
                        reflect_slot, reflect_last = probe_boss_slot, before
                        reflect_tries = reflect_tries + 1
                        reflect_press_ticks[#reflect_press_ticks + 1] = before
                        -- the weapon's maximum hit at this press: standard melee formula, effective strength = current level (reading.level, drained or boosted) + 1 (the whip's Lash is controlled) + 8, bonus 82 (abyssal whip) + 64, no prayer; a missing reading leaves 0 and fails the row
                        local _, press_strength = t.skill.read("strength")
                        if type(press_strength) == "table" and tonumber(press_strength.level) then strength_samples[#strength_samples + 1] = { before, tonumber(press_strength.level) } end
                        reflect_press_max[#reflect_press_max + 1] = (type(press_strength) == "table" and tonumber(press_strength.level)) and math.floor(0.5 + (tonumber(press_strength.level) + 1 + 8) * (82 + 64) / 640) or 0
                        -- one step drops the whip's auto attack, so the next press is the right style
                        local _, probe_here = t.world.tile()
                        if probe_here ~= nil then
                            local probe_step_x = probe_here.x + 1
                            if probe_here.x >= 6434 then probe_step_x = probe_here.x - 1 end
                            t.player.walk_to(probe_step_x, probe_here.z, 2)
                        end
                    end
                    results[tostring(call_result)] = (results[tostring(call_result)] or 0) + 1
                    if boss_probe and call_result == "ok" and reflect_tries <= 3 then t.shot("technique.vasilias_reflect_press" .. reflect_tries) end
                    if call_result == "ok" and not wrong_probe and not boss_probe and not shot_taken["style" .. press_style] then
                        shot_taken["style" .. press_style] = true
                        t.shot("tech.style_kills.press_" .. press_style)
                    end
                    local spent = select(2, t.tick()) - before
                    cost.press = cost.press + spent
                    if spent >= 6 then
                        hang_ticks = hang_ticks + spent
                        hang_pending = { text = (now - tick0) .. ":" .. target_style .. ":" .. tostring(call_result) .. ":" .. spent .. " ticks:"
                            .. (string.find(target_symbol, "_fighting_", 1, true) ~= nil and "swinging" or "chewer"), hp = hp_now }
                    end
                    ring[(ring_count - 1) % 60 + 1] = ring[(ring_count - 1) % 60 + 1] .. "+" .. ({ melee = "a", ranged = "r", magic = "m" })[target_style] .. string.sub(tostring(call_result), 1, 3) .. spent .. (target_symbol ~= nil and string.find(target_symbol, "_fighting_", 1, true) ~= nil and "F" or "C")
                    if #trace < 45 then trace[#trace + 1] = (now - tick0) .. ":" .. string.sub(target_style, 1, 1) .. string.sub(tostring(call_result), 1, 3) .. spent end
                    if on_boss then boss_calls = boss_calls + 1 else waves_calls = waves_calls + 1 end
                    if call_result == "ok" or call_result == "timeout" then
                        local _, world_slot = t.ticklog.slot(target)
                        if type(world_slot) == "number" then pending = { world_slot = world_slot, tick = before } else pending = nil end
                        lock = { world_slot = world_slot, slot = target.slot, symbol = target_symbol, tick = select(2, t.tick()), hold = hold_of[target_style], fight = (target_symbol ~= nil and string.find(target_symbol, "_fighting_", 1, true) ~= nil) or on_boss }
                        if on_boss then lock.hold = 4 end
                        if burst.press ~= nil then
                            lock.hold = 5
                            burst.count = burst.count + 1
                            burst.centers[#burst.centers + 1] = { x = burst.press.x, z = burst.press.z, tick = now, clump = burst.press.clump }
                        end
                    else
                        if string.find(tostring(call_detail), "already under attack", 1, true) ~= nil then under_attack = under_attack + 1 end
                        if #samples < 4 then samples[#samples + 1] = tostring(call_result) .. ": " .. string.sub(tostring(call_detail), 1, 150) end
                        skips[target.slot] = now + 3
                        -- a refused or covered press (stale menu, a cast left armed): step away one tile, then press again
                        local before_step = select(2, t.tick())
                        local _, here = t.world.tile()
                        if here ~= nil then
                            local step_x = here.x + 1
                            if here.x >= 6434 then step_x = here.x - 1 end
                            t.player.walk_to(step_x, here.z, 2)
                        end
                        steps_away = steps_away + 1
                        disarms = disarms + 1
                        cost.step = cost.step + (select(2, t.tick()) - before_step)
                    end
                    if #slow < 40 then slow[#slow + 1] = spent end
                end
            end
        end
        do
            local _, last_tiles = t.ticklog.rows({ kind = "player_tile", since = burst.tile_serial })
            for _, tile_row in ipairs(last_tiles) do
                if tile_row.serial > burst.tile_serial then burst.tile_serial = tile_row.serial end
                local at = burst.ptiles[tile_row.tick]
                if at == nil then at = {} burst.ptiles[tile_row.tick] = at end
                at[tile_row.pid] = { tile_row.x, tile_row.z }
            end
            t.ticks(1)
            local _, last_sounds = t.ticklog.rows({ kind = "sound", since = burst.sound_serial })
            for _, sound_row in ipairs(last_sounds) do
                if sound_row.serial > burst.sound_serial then burst.sound_serial = sound_row.serial end
                if sound_row.source == "synth" then burst.sounds[#burst.sounds + 1] = { sound_row.tick, sound_row.sound } end
            end
        end
        local result_texts = {}
        for name, count in pairs(results) do result_texts[#result_texts + 1] = name .. " " .. count end
        table.sort(result_texts)
        t.check("fight.loop", swings + casts > 20, "melee/ranged swings " .. swings .. ", casts " .. casts .. ", idle polls " .. idle
            .. ", spells put down " .. disarms .. ", sharks eaten " .. eaten .. ", lowest hitpoints " .. lowest .. ", ended by " .. fight_end .. ", "
            .. (select(2, t.tick()) - tick0) .. " ticks after the click; calls on waves " .. waves_calls .. ", on Vasilias " .. boss_calls
            .. "; press results " .. table.concat(result_texts, ", ") .. " (first non-ok answers: " .. table.concat(samples, " | ") .. "); ticks per call " .. table.concat(slow, ",") .. "; ticks spent equip "
            .. cost.equip .. " eat " .. cost.eat .. " press " .. cost.press .. " step " .. cost.step .. " wait " .. cost.wait .. "; pillars fallen "
            .. pillars_fallen .. ", most chewers on the doomed support " .. zone_peak .. ", " .. tostring(brace_text) .. "; last polls (tick:hp:eaten+press) " .. table.concat(ring, " ", ring_count > 60 and (ring_count % 60) + 1 or 1, math.min(ring_count, 60)) .. " " .. (ring_count > 60 and table.concat(ring, " ", 1, ring_count % 60) or "") .. "; trace " .. table.concat(trace, " "))
        -- the room's end: the real chat line and the boss-defeated jingle reach the raider a few ticks after her death
        local complete_text = nil
        -- (the leader may stand out of the loop while its two raiders finish her: the line is read once every raider is done, if the room reads cleared)
        t.expect("party.barrier.fight_done", t.party.barrier("fight_done", 1500))
        do
            t.ticks(2)
            local clear_result, clear_state = t.raid.state()
            if clear_result == "ok" and string.find(tostring(clear_state.line), "cleared=1", 1, true) ~= nil then
                t.shot("fight.wave_complete_line")
                local _, recent = t.msg.last(120)
                for _, line in ipairs(recent) do
                    if string.find(line.text, "complete!", 1, true) ~= nil then complete_text = line.text break end
                end
            end
        end
        local leave_result, leave_detail = t.raid.leave()
        t.check("fight.left", leave_result == "ok", tostring(leave_detail))
        t.ticks(1)
        -- (the analysis runs in its own function body: the run function holds more than 200 locals otherwise)
        ;(function()
        -- ===== THE SPEC ROWS (measured from our own tick log) =====
        local lane_xz = { ["3281,4248"] = true, ["3281,4249"] = true, ["3295,4233"] = true, ["3296,4233"] = true,
            ["3310,4248"] = true, ["3310,4249"] = true, ["3309,4248"] = true }
        local split_offsets = { ["-1,0"] = true, ["0,0"] = true, ["1,0"] = true, ["0,1"] = true, ["1,1"] = true, ["2,1"] = true }
        local attack_seq = { [7989] = true, [7999] = true, [8004] = true }
        local detonate_seq = { [7992] = true, [8000] = true, [8006] = true }
        -- one kind per query (a kind list is filtered in Lua and runs out of the instruction budget on 40,000 npc_tile rows),
        -- merged back into the log's own order by serial
        local lists, heads = {}, {}
        for _, kind in ipairs({ "npc_spawn", "npc_death", "npc_free", "npc_retype", "npc_anim" }) do
            local _, rows_of_kind = t.ticklog.rows({ kind = kind })
            lists[#lists + 1] = rows_of_kind
            heads[#heads + 1] = 1
            t.ticks(1)
        end
        local stream = {}
        while true do
            local best = nil
            for index = 1, #lists do
                local row = lists[index][heads[index]]
                if row ~= nil and (best == nil or row.serial < lists[best][heads[best]].serial) then best = index end
            end
            if best == nil then break end
            stream[#stream + 1] = lists[best][heads[best]]
            heads[best] = heads[best] + 1
            if #stream % 400 == 0 then t.ticks(1) end
        end
        local room_start, off_x, off_z = nil, nil, nil
        local supports = {}
        for _, r in ipairs(stream) do
            if r.kind == "npc_spawn" and r.type == 8358 then
                if room_start == nil then
                    room_start = r.tick
                    -- the instance is the region moved by whole 64-tile blocks (region 13122 base 3264,4224)
                    off_x = (r.x // 64) * 64 - 3264
                    off_z = (r.z // 64) * 64 - 4224
                end
                if r.tick == room_start then supports[#supports + 1] = { x = r.x - off_x, z = r.z - off_z } end
            end
        end
        local wave_tick_set, wave_ticks = {}, {}
        local cur, lives = {}, {}
        local spawns_at = {}
        local m = {}
        for _, name in ipairs({ "cycle", "first_wave", "lane_tiles", "aggro_swap", "lifetime_small", "lifetime_big",
            "big_explode_splits", "kill_despawn_small", "kill_despawn_big", "split_spawn_tick", "split_count",
            "split_tiles", "attackrate", "flicker_first_switch", "flicker_hold", "flicker_first_wave",
            "max_hit_small", "max_hit_big", "explosion_small", "explosion_big", "pillar_bite_cadence", "wave_count", "spawn_big", "spawn_aggro", "spawn_flicker",
            "vasilias_first_switch", "vasilias_switch", "vasilias_first_attack_after_switch", "big_kill_apart" }) do
            m[name] = {}
        end
        local last_detonate = {}
        local lane_spawns = 0
        -- a support's fall hands its chewers to the player three ticks after the npc_death (tob_nylo_retarget): those retypes are not the
        -- aggro turn the row measures, and are counted apart
        local retarget_tick, aggro_excluded = {}, 0
        for _, r in ipairs(stream) do
            if r.kind == "npc_death" and r.type == 8358 then retarget_tick[r.tick + 3] = true end
        end
        local walking_smalls = {}
        local last_attack_tick = {}
        for index, r in ipairs(stream) do
            if index % 250 == 0 then t.ticks(1) end
            if r.kind == "npc_spawn" then
                if r.type >= 8342 and r.type <= 8353 then
                    cur[r.slot] = { slot = r.slot, spawn = r.tick, type = r.type, type0 = r.type, x = r.x, z = r.z, flips = {}, attacks = {} }
                    local key = (r.x - off_x) .. "," .. (r.z - off_z)
                    if lane_xz[key] then
                        cur[r.slot].lane = true
                        lane_spawns = lane_spawns + 1
                        if not wave_tick_set[r.tick] then
                            wave_tick_set[r.tick] = true
                            wave_ticks[#wave_ticks + 1] = r.tick
                        end
                        local lx = m.lane_tiles
                        lx[#lx + 1] = r.x - off_x
                        lx[#lx + 1] = r.z - off_z
                    end
                    spawns_at[r.tick] = spawns_at[r.tick] or {}
                    spawns_at[r.tick][#spawns_at[r.tick] + 1] = { slot = r.slot, type = r.type, x = r.x, z = r.z, lane = cur[r.slot].lane }
                end
            elseif r.kind == "npc_death" then
                if cur[r.slot] ~= nil then cur[r.slot].died = r.tick end
            elseif r.kind == "npc_retype" then
                local c = cur[r.slot]
                if c ~= nil then c.type = r.to_type end
                if c ~= nil and r.from_type >= 8342 and r.from_type <= 8353 and r.to_type >= 8342 and r.to_type <= 8353 then
                    local from_group = (r.from_type - 8342) // 3
                    local to_group = (r.to_type - 8342) // 3
                    if (from_group == 0 and to_group == 2) or (from_group == 1 and to_group == 3) then
                        local list = m.aggro_swap
                        if retarget_tick[r.tick] and (r.tick - c.spawn < 9 or r.tick - c.spawn > 11) then
                            aggro_excluded = aggro_excluded + 1
                        else
                            list[#list + 1] = r.tick - c.spawn
                            if c.lane then c.aggro = true end
                        end
                    elseif (r.from_type - 8342) % 3 ~= (r.to_type - 8342) % 3 then
                        c.flips[#c.flips + 1] = r.tick
                    end
                end
            elseif r.kind == "npc_anim" then
                local c = cur[r.slot]
                if c ~= nil then
                    if c.died ~= nil and c.death_seq == nil and r.tick >= c.died then
                        c.death_seq, c.death_style, c.death_delay = r.seq, (r.type - 8342) % 3, r.tick - c.died
                    elseif c.died == nil then
                        c.last_seq, c.last_style = r.seq, (r.type - 8342) % 3
                    end
                    if attack_seq[r.seq] then
                        local previous = c.attacks[#c.attacks]
                        c.attacks[#c.attacks + 1] = r.tick
                        if previous ~= nil and c.type < 8348 then
                            local frozen_gap = false
                            for _, probe_row in ipairs(freeze_probes) do
                                if probe_row.world_slot == r.slot and previous < probe_row.tick + 6 and r.tick >= probe_row.tick then frozen_gap = true end
                            end
                            for _, probe_row in ipairs(burst.frozen) do
                                if probe_row.world_slot == r.slot and previous < probe_row.tick + 6 and r.tick >= probe_row.tick then frozen_gap = true end
                            end
                            local list = m.attackrate
                            if not frozen_gap then list[#list + 1] = r.tick - previous end
                            local bites = m.pillar_bite_cadence
                            if not frozen_gap then bites[#bites + 1] = r.tick - previous end
                        end
                    elseif detonate_seq[r.seq] then
                        c.detonated = r.tick
                    end
                end
            else
                local c = cur[r.slot]
                if c ~= nil then
                    c.free, c.fx, c.fz = r.tick, r.x, r.z
                    lives[#lives + 1] = c
                    cur[r.slot] = nil
                end
            end
        end
        local wave_index_of = {}
        for index, tick in ipairs(wave_ticks) do wave_index_of[tick] = index end
        if room_start ~= nil and #wave_ticks > 0 then
            local on_cycle = 0
            for _, tick in ipairs(wave_ticks) do
                if (tick - room_start) % 4 == 0 then on_cycle = on_cycle + 1 end
            end
            if on_cycle == #wave_ticks then m.cycle[1] = 4 else m.cycle[1] = 0 end
            m.first_wave[1] = wave_ticks[1] - room_start
        end
        local flicker_wave = nil
        -- the room being left frees every copy on one tick: that last free tick (three or more frees on it) is a teardown, not a despawn
        local frees_at, teardown_tick = {}, -1
        for _, c in ipairs(lives) do
            frees_at[c.free] = (frees_at[c.free] or 0) + 1
            if c.free > teardown_tick then teardown_tick = c.free end
        end
        if (frees_at[teardown_tick] or 0) < 3 then teardown_tick = -1 end
        local kept_lives = {}
        for _, c in ipairs(lives) do
            if c.free ~= teardown_tick then kept_lives[#kept_lives + 1] = c end
        end
        lives = kept_lives
        for index, c in ipairs(lives) do
            if index % 40 == 0 then t.ticks(1) end
            local small = (c.type < 8345) or (c.type >= 8348 and c.type < 8351)
            if #c.flips >= 1 and c.lane then
                local list = m.flicker_first_switch
                list[#list + 1] = c.flips[1] - c.spawn
                if #c.flips >= 2 then
                    local hold = m.flicker_hold
                    hold[#hold + 1] = c.flips[2] - c.flips[1]
                end
                local idx = wave_index_of[c.spawn]
                if idx ~= nil and (flicker_wave == nil or idx < flicker_wave) then flicker_wave = idx end
            end
            if c.died == nil and c.detonated ~= nil then
                local list = small and m.lifetime_small or m.lifetime_big
                list[#list + 1] = c.free - c.spawn
            end
            if c.died ~= nil and small then
                -- the engine delays a nylocas that moved on its killing tick or the one before (CONTENT_BUGS seam3, open): a small
                -- that stood is the spec's 1-2, a walking one is counted apart
                local _, step_rows = t.ticklog.rows({ kind = "npc_tile", slot = c.slot })
                local walked = false
                for _, step in ipairs(step_rows) do
                    if step.tick >= c.died - 1 and step.tick <= c.died then walked = true end
                end
                t.ticks(1)
                local list = m.kill_despawn_small
                list[#list + 1] = c.free - c.died
                if walked then walking_smalls[#walking_smalls + 1] = c.free - c.died end
            elseif c.died ~= nil then
                local list = m.kill_despawn_big
                list[#list + 1] = c.free - c.died
            end
            if not small and (c.died ~= nil or c.detonated ~= nil) then
                -- (a big freed by the room being torn down, neither killed nor exploded, splits nothing)
                -- two bigs freed on one tick next to each other share the spawns around them: each spawn is given to one parent, the
                -- exact (0,0) and (1,1) offsets first and any other offset of the six after them
                if c.split_n == nil then
                    local parents = {}
                    for _, other in ipairs(lives) do
                        if other.free == c.free and other.fx ~= nil and not ((other.type < 8345) or (other.type >= 8348 and other.type < 8351))
                            and (other.died ~= nil or other.detonated ~= nil) then
                            parents[#parents + 1] = other
                            other.split_n, other.split_offsets = 0, {}
                        end
                    end
                    local pool = spawns_at[c.free] or {}
                    for _, s in ipairs(pool) do s.used = nil end
                    -- (a parent takes the spawns it was given in three passes, the exact offsets, the six offsets, then any spawn within three
                    -- tiles, and a parent that already has two takes no more: with four bigs freed on one tick the spawns are all around them)
                    for pass = 1, 3 do
                        for _, parent in ipairs(parents) do
                            for _, s in ipairs(pool) do
                                local dx, dz = s.x - parent.fx, s.z - parent.fz
                                local exact = (dx == 0 and dz == 0) or (dx == 1 and dz == 1)
                                if not s.lane and not s.used and (pass == 1 or parent.split_n < 2 or #parents == 1)
                                    and ((pass == 1 and exact and (parent.split_n < 2 or #parents == 1)) or (pass == 2 and split_offsets[dx .. "," .. dz])
                                    or (pass == 3 and #parents > 1 and math.max(math.abs(dx), math.abs(dz)) <= 3)) then
                                    s.used = true
                                    parent.split_n = parent.split_n + 1
                                    parent.split_offsets[#parent.split_offsets + 1] = { dx, dz }
                                end
                            end
                        end
                    end
                end
                local found = c.split_n or 0
                for _, offset in ipairs(c.split_offsets or {}) do
                    local tiles = m.split_tiles
                    tiles[#tiles + 1] = offset[1]
                    tiles[#tiles + 1] = offset[2]
                end
                local counts = c.died == nil and m.big_explode_splits or m.split_count
                counts[#counts + 1] = found
                local gap = m.split_spawn_tick
                gap[#gap + 1] = 0
            end
        end
        if flicker_wave ~= nil then m.flicker_first_wave[1] = flicker_wave end
        -- the nylocas attack hits on the player, by size, and the explosion hits
        t.ticks(1)
        local _, hits = t.ticklog.rows({ kind = "hit_player" })
        local anim_by_slot = {}
        for index, r in ipairs(stream) do
            if index % 250 == 0 then t.ticks(1) end
            if r.kind == "npc_anim" and (attack_seq[r.seq] or detonate_seq[r.seq]) then
                anim_by_slot[r.slot] = anim_by_slot[r.slot] or {}
                local list = anim_by_slot[r.slot]
                list[#list + 1] = { tick = r.tick, boom = detonate_seq[r.seq] == true }
            end
        end
        for index, h in ipairs(hits) do
            if index % 40 == 0 then t.ticks(1) end
            if h.npc_type ~= nil and h.npc_type >= 8342 and h.npc_type <= 8353 and h.damage > 0 then
                local last = nil
                for _, a in ipairs(anim_by_slot[h.npc_slot] or {}) do
                    if a.tick <= h.tick and (last == nil or a.tick >= last.tick) then last = a end
                end
                if last ~= nil and h.tick - last.tick <= 6 then
                    local small = (h.npc_type < 8345) or (h.npc_type >= 8348 and h.npc_type < 8351)
                    if last.boom then
                        local list = small and m.explosion_small or m.explosion_big
                        list[#list + 1] = h.damage
                    elseif small then
                        local list = m.max_hit_small
                        list[#list + 1] = h.damage
                    else
                        local list = m.max_hit_big
                        list[#list + 1] = h.damage
                    end
                end
            end
        end
        local support_hits = nil
        do
        -- ===== VASILIAS, THE SUPPORTS AND THE PRESENTATION: data from the same log =====
        for _, name in ipairs({ "vasilias_tile", "vasilias_spawn_delay", "vasilias_repeats", "vasilias_attackrate", "vasilias_attack_gap_in_window",
            "vasilias_switch_tick_attacks", "vasilias_max_hit", "vasilias_attacks_per_window", "spawn_total",
            "pillar_collapse_anim", "collapse_hits", "vasilias_hp", "vasilias_land_cycles", "small_kill_gfx_tick",
            "small_kill_anim_offset", "big_kill_anim_offset", "big_death_gfx" }) do
            m[name] = {}
        end
        local boss = { slot = nil, spawn = nil, spawn_row = nil, death = nil, free = nil, retypes = {}, anims = {} }
        for index, r in ipairs(stream) do
            if index % 80 == 0 then t.ticks(1) end
            if boss.slot == nil then
                if r.kind == "npc_spawn" and r.type == 8354 then boss.slot, boss.spawn, boss.spawn_row = r.slot, r.tick, r end
            elseif r.slot == boss.slot and boss.free == nil then
                if r.kind == "npc_retype" then
                    boss.retypes[#boss.retypes + 1] = r
                elseif r.kind == "npc_death" then
                    boss.death = r.tick
                elseif r.kind == "npc_free" then
                    boss.free = r.tick
                elseif r.kind == "npc_anim" then
                    boss.anims[#boss.anims + 1] = r
                end
            end
        end
        local boss_attacks = {}
        local boss_seq_counts = { [8355] = {}, [8356] = {}, [8357] = {} }
        local boss_all_seqs = {}
        for _, a in ipairs(boss.anims) do
            boss_all_seqs[a.seq] = true
            if a.type >= 8355 and a.type <= 8357 and (boss.death == nil or a.tick <= boss.death) then
                boss_attacks[#boss_attacks + 1] = a
                boss_seq_counts[a.type][a.seq] = (boss_seq_counts[a.type][a.seq] or 0) + 1
            end
        end
        local boss_land_seq = nil
        for _, a in ipairs(boss.anims) do
            if a.tick == boss.spawn and boss_land_seq == nil then boss_land_seq = a.seq end
        end
        local boss_window_note = "no boss spawned"
        if boss.slot ~= nil then
            m.vasilias_tile[1] = boss.spawn_row.x - off_x
            m.vasilias_tile[2] = boss.spawn_row.z - off_z
            local last_free = nil
            for _, c in ipairs(lives) do
                if c.free < boss.spawn and (last_free == nil or c.free > last_free) then last_free = c.free end
            end
            if last_free ~= nil then m.vasilias_spawn_delay[1] = boss.spawn - last_free end
            local forms = boss.retypes
            if #forms > 0 then m.vasilias_land_cycles[1] = (forms[1].tick - boss.spawn) * 30 end
            local repeats = 0
            for i = 2, #forms do
                -- the first gap is the melee form's opening window (the first colour switch), every later one a full window
                local list = (i == 2) and m.vasilias_first_switch or m.vasilias_switch
                list[#list + 1] = forms[i].tick - forms[i - 1].tick
                if forms[i].to_type == forms[i - 1].to_type then repeats = repeats + 1 end
            end
            if #forms > 1 then m.vasilias_repeats[1] = repeats end
            local switch_attacks = 0
            for _, a in ipairs(boss_attacks) do
                for _, f in ipairs(forms) do
                    if f.tick == a.tick then switch_attacks = switch_attacks + 1 end
                end
            end
            if #forms > 0 then m.vasilias_switch_tick_attacks[1] = switch_attacks end
            -- a window is the ticks one form holds, from its retype to the next retype (an unfinished last window is not counted)
            for j = 1, #forms - 1 do
                local count, previous = 0, nil
                for _, a in ipairs(boss_attacks) do
                    if a.tick >= forms[j].tick and a.tick < forms[j + 1].tick then
                        count = count + 1
                        if previous == nil then
                            local first_list = m.vasilias_first_attack_after_switch
                            first_list[#first_list + 1] = a.tick - forms[j].tick
                        end
                        if previous ~= nil then
                            local gaps_in = m.vasilias_attack_gap_in_window
                            gaps_in[#gaps_in + 1] = a.tick - previous
                            local rate = m.vasilias_attackrate
                            rate[#rate + 1] = a.tick - previous
                        end
                        previous = a.tick
                    end
                end
                local windows = m.vasilias_attacks_per_window
                windows[#windows + 1] = count
            end
            boss_window_note = #forms .. " retype rows, " .. (#forms - 1) .. " closed windows, " .. #boss_attacks .. " attack rows"
            for _, h in ipairs(hits) do
                if h.tick >= boss.spawn and h.npc_type ~= nil and h.npc_type >= 8355 and h.npc_type <= 8357 and h.damage > 0 then
                    local list = m.vasilias_max_hit
                    list[#list + 1] = h.damage
                end
            end
            if boss_hp_read ~= nil and boss_hp_read > 0 then m.vasilias_hp[1] = boss_hp_read end
        end
        -- the supports: loc rows
        local _, loc_sets = t.ticklog.rows({ kind = "loc_set" })
        local _, loc_anims = t.ticklog.rows({ kind = "loc_anim" })
        local collapse_tick = {}
        local collapse_count = 0
        local collapse_loc_seqs, collapse_loc_ids, rubble_loc_ids = {}, {}, {}
        for _, a in ipairs(loc_anims) do
            collapse_tick[a.tick] = true
            collapse_count = collapse_count + 1
            collapse_loc_seqs[a.seq] = true
            collapse_loc_ids[a.loc] = true
            for _, ls in ipairs(loc_sets) do
                if ls.tick > a.tick and ls.coord == a.coord and ls.loc ~= a.loc then
                    local list = m.pillar_collapse_anim
                    list[#list + 1] = ls.tick - a.tick
                    rubble_loc_ids[ls.loc] = true
                    break
                end
            end
        end
        local collapse_hit_text = {}
        for _, h in ipairs(hits) do
            if h.npc_slot == -1 and collapse_tick[h.tick] then
                local list = m.collapse_hits
                list[#list + 1] = h.damage
                collapse_hit_text[#collapse_hit_text + 1] = h.tick .. ":" .. h.damage
            end
        end
        -- the wave nylocas: graphics at the despawn tile and tick, death and detonation animations
        t.ticks(1)
        local _, msp_rows = t.ticklog.rows({ kind = "map_spotanim" })
        t.ticks(1)
        local msp_at = {}
        for _, r in ipairs(msp_rows) do
            local key = r.tick .. ":" .. r.x .. ":" .. r.z
            msp_at[key] = msp_at[key] or {}
            msp_at[key][#msp_at[key] + 1] = r.spotanim
        end
        local style_count = { kill_gfx = { {}, {}, {} }, det_gfx = { {}, {}, {} }, det_seq = { {}, {}, {} }, death_seq = { {}, {}, {} } }
        local kill_gfx_misses, big_instances = 0, 0
        -- a small freed on the same tile and tick as a big owns its own graphic: those are taken off the big's count
        local small_freed_at = {}
        for _, c in ipairs(lives) do
            if c.fx ~= nil and ((c.type < 8345) or (c.type >= 8348 and c.type < 8351)) then
                local key = c.free .. ":" .. c.fx .. ":" .. c.fz
                small_freed_at[key] = (small_freed_at[key] or 0) + 1
            end
        end
        for index, c in ipairs(lives) do
            if index % 60 == 0 then t.ticks(1) end
            local small = (c.type < 8345) or (c.type >= 8348 and c.type < 8351)
            local style = (c.type - 8342) % 3 + 1
            if c.died ~= nil and c.death_seq ~= nil then
                local counts = style_count.death_seq[c.death_style + 1]
                counts[c.death_seq] = (counts[c.death_seq] or 0) + 1
                local list = small and m.small_kill_anim_offset or m.big_kill_anim_offset
                -- a big that stepped on its killing tick or the one before is held an extra tick or more by the engine's arrive delay (CONTENT_BUGS seam3, open
                -- engine row): it is counted apart, as the walking smalls are
                if not small and c.death_delay > 2 then list = m.big_kill_apart end
                list[#list + 1] = c.death_delay
            end
            if c.died ~= nil and small and c.fx ~= nil then
                local found = nil
                for _, delta in ipairs({ 0, -1, 1, -2, 2 }) do
                    local ids = msp_at[(c.free + delta) .. ":" .. c.fx .. ":" .. c.fz]
                    if ids ~= nil and found == nil then found = { delta = delta, ids = ids } end
                end
                if found ~= nil then
                    local list = m.small_kill_gfx_tick
                    list[#list + 1] = found.delta
                    for _, id in ipairs(found.ids) do
                        style_count.kill_gfx[style][id] = (style_count.kill_gfx[style][id] or 0) + 1
                    end
                else
                    kill_gfx_misses = kill_gfx_misses + 1
                end
            end
            if not small and c.fx ~= nil and (c.died ~= nil or (c.last_seq ~= nil and c.free - c.spawn >= 51)) then
                big_instances = big_instances + 1
                local key = c.free .. ":" .. c.fx .. ":" .. c.fz
                local ids = msp_at[key]
                local list = m.big_death_gfx
                list[#list + 1] = math.max(0, (ids ~= nil and #ids or 0) - (small_freed_at[key] or 0))
            end
            if c.died == nil and c.last_seq ~= nil and c.free - c.spawn >= 51 then
                local counts = style_count.det_seq[c.last_style + 1]
                counts[c.last_seq] = (counts[c.last_seq] or 0) + 1
                if small and c.fx ~= nil then
                    for _, id in ipairs(msp_at[c.free .. ":" .. c.fx .. ":" .. c.fz] or {}) do
                        style_count.det_gfx[style][id] = (style_count.det_gfx[style][id] or 0) + 1
                    end
                end
            end
        end
        -- every swing animation by wave nylocas style, with the death and detonation seqs just read taken out
        local wave_seq_counts = { {}, {}, {} }
        local swing_rows = {}
        for index, r in ipairs(stream) do
            if index % 400 == 0 then t.ticks(1) end
            if r.kind == "npc_anim" and r.type >= 8342 and r.type <= 8353 then
                local counts = wave_seq_counts[(r.type - 8342) % 3 + 1]
                counts[r.seq] = (counts[r.seq] or 0) + 1
                swing_rows[#swing_rows + 1] = r
            end
        end
        local wave_attack_seq = {}
        for style = 1, 3 do
            local best_seq, best_count = nil, 0
            for seq, count in pairs(wave_seq_counts[style]) do
                if style_count.det_seq[style][seq] == nil and style_count.death_seq[style][seq] == nil and count > best_count then
                    best_seq, best_count = seq, count
                end
            end
            wave_attack_seq[style] = best_seq
        end
        -- ===== THE WAVE ROWS: the wave count, the spawn mix, the room cap, the pillar each spawn attacks =====
        do
            -- a wave is a lane-tile spawn on a cycle tick (a big killed on its lane tile splits there on other ticks)
            local cycle_waves, wave_spawns = {}, {}
            for _, tick in ipairs(wave_ticks) do
                if room_start ~= nil and (tick - room_start) % 4 == 0 then cycle_waves[#cycle_waves + 1] = tick end
            end
            table.sort(cycle_waves)
            m.wave_count[1] = #cycle_waves
            local wave_total, big_total, aggro_total, flicker_total = 0, 0, 0, 0
            for _, c in ipairs(lives) do
                if c.lane and room_start ~= nil and (c.spawn - room_start) % 4 == 0 then
                    wave_spawns[#wave_spawns + 1] = c
                    wave_total = wave_total + 1
                    if c.type0 >= 8345 and c.type0 <= 8347 then big_total = big_total + 1 end
                    if c.aggro then aggro_total = aggro_total + 1 end
                    if #c.flips >= 1 then flicker_total = flicker_total + 1 end
                end
            end
            m.spawn_total[1] = wave_total
            m.spawn_big[1] = big_total
            m.spawn_aggro[1] = aggro_total
            m.spawn_flicker[1] = flicker_total
            -- the pillar each wave spawn that is not an aggro chews: the chewer writes an npc_face row when it turns to its pillar. Count the ones that did
            local faced, unfaced = 0, 0
            for index, c in ipairs(wave_spawns) do
                if not c.aggro then
                    if index % 4 == 0 then t.ticks(1) end
                    local _, face_rows = t.ticklog.rows({ kind = "npc_face", slot = c.slot })
                    local seen_face = false
                    for _, face_row in ipairs(face_rows) do
                        if face_row.tick >= c.spawn and face_row.tick <= c.free then seen_face = true end
                    end
                    if seen_face then faced = faced + 1 else unfaced = unfaced + 1 end
                end
            end
            m.faced_chewers, m.unfaced_chewers, m.wave_total_n, m.aggro_total_n = faced, unfaced, wave_total, aggro_total
            -- the room cap: live nylocas at the end of tick T are those spawned before T and freed after it (the cap check runs in the support's phase)
            local alive_diff = {}
            for _, c in ipairs(lives) do
                alive_diff[c.spawn + 1] = (alive_diff[c.spawn + 1] or 0) + 1
                alive_diff[c.free] = (alive_diff[c.free] or 0) - 1
            end
            local alive_at, running = {}, 0
            local last_logged = select(2, t.tick())
            for tick = 0, last_logged do
                running = running + (alive_diff[tick] or 0)
                alive_at[tick] = running
            end
            -- the wave table's natural stalls, waves 1-30 (blert_nylocas-waves.json "naturalStall"): the ticks after wave k spawns before wave k+1 is due. Wave k+1
            -- spawns on the first cycle tick at or after its due tick on which the room holds fewer than the cap of the wave already out
            -- (tob_nylocas.rs2 ~tob_nylo_wave_tick: 12 up to wave 19 out, 24 from wave 20 out); every cycle tick from due to the spawn is a held tick
            local natural = { 4, 4, 4, 4, 16, 4, 12, 4, 12, 8, 8, 8, 8, 8, 8, 4, 12, 8, 12, 16, 8, 12, 8, 8, 8, 4, 8, 4, 4, 4 }
            local pre_min, post_min, pre_held, post_held, bad_held, bad_spawn, early, natural_sum = nil, nil, 0, 0, 0, 0, 0, 0
            local pre_spawn_max, post_spawn_max, uncapped_gaps = 0, 0, 0
            for k = 1, math.min(#cycle_waves - 1, 30) do
                local spawned, next_spawn = cycle_waves[k], cycle_waves[k + 1]
                local cap = (k <= 19) and 12 or 24
                local due = spawned + natural[k]
                if next_spawn < due then early = early + 1 end
                local held = 0
                local u = due
                while u < next_spawn do
                    held = held + 1
                    local live = alive_at[u] or 0
                    if live < cap then bad_held = bad_held + 1 end
                    if k <= 19 then
                        pre_held = pre_held + 1
                        if pre_min == nil or live < pre_min then pre_min = live end
                    else
                        post_held = post_held + 1
                        if post_min == nil or live < post_min then post_min = live end
                    end
                    u = u + 4
                end
                if held == 0 then uncapped_gaps = uncapped_gaps + 1 end
                local live_at_spawn = alive_at[next_spawn] or 0
                if live_at_spawn >= cap then bad_spawn = bad_spawn + 1 end
                if k <= 19 then
                    if live_at_spawn > pre_spawn_max then pre_spawn_max = live_at_spawn end
                else
                    if live_at_spawn > post_spawn_max then post_spawn_max = live_at_spawn end
                end
                natural_sum = natural_sum + (next_spawn - spawned - 4 * held)
            end
            m.cap_pre20_text = { value = pre_min, held = pre_held, spawn_max = pre_spawn_max }
            m.cap_post20_text = { value = post_min, held = post_held, spawn_max = post_spawn_max }
            m.natural_stall_text = { value = natural_sum, early = early, bad_held = bad_held, bad_spawn = bad_spawn, uncapped = uncapped_gaps, gaps = math.min(#cycle_waves - 1, 30) }
            -- the wave that was first let in with twelve or more alive: the cap that gates wave N is the cap of the wave already out, so the last wave gated
            -- by 12 is the one before it
            local gate_wave = nil
            for k = 1, #cycle_waves do
                if gate_wave == nil and (alive_at[cycle_waves[k]] or 0) >= 12 then gate_wave = k end
            end
            m.cap_gate_text = { value = gate_wave ~= nil and (gate_wave - 1) or nil, live = gate_wave ~= nil and alive_at[cycle_waves[gate_wave]] or nil }
        end
        local specs = {
            { "cycle", "ticks", "4", "B", "exact", "every wave spawned on a cycle tick 0 of the room start" },
            { "first_wave", "ticks", "4", "B", "+-1", "first wave spawn minus the supports' spawn tick, the room start" },
            { "lane_tiles", "tiles", "3281,4248,3281,4249,3295,4233,3296,4233,3310,4248,3310,4249,3309,4248", "B", "exact", "distinct lane spawn x and z" },
            { "aggro_swap", "ticks", "9-11", "B", "range", "incoming to fighting retype, ticks after spawn (" .. aggro_excluded .. " more on a support fall's retarget tick, fall+3, are the chewers handed to the player, not an aggro turn, and are left out)" },
            { "lifetime_small", "ticks", "52", "B", "exact", "naturally exploded smalls, spawn to npc_free" },
            { "lifetime_big", "ticks", "55", "B", "exact", "naturally exploded bigs, spawn to npc_free" },
            { "big_explode_splits", "count", "2", "B", "exact", "smalls spawned on the despawn tick at the six offsets" },
            { "kill_despawn_small", "ticks", "1-2", "B", "range", "npc_death to npc_free of smalls the player killed standing (walking ones apart, open engine arrive delay)" },
            { "kill_despawn_big", "ticks", "6-7", "B", "range", "npc_death to npc_free of bigs the player killed" },
            { "split_spawn_tick", "ticks", "0", "B", "exact", "split spawn tick minus the parent's npc_free tick" },
            { "split_count", "count", "2", "B", "exact", "splits of bigs the player killed" },
            { "split_tiles", "tiles", "0,0,1,1", "B", "range", "split offsets from the parent's south-west tile" },
            { "attackrate", "ticks", "3", "A", "exact", "gaps between a chewing nylocas' attack animations" },
            { "flicker_first_switch", "ticks", "5", "B", "+-1", "flickers' first style change after spawn" },
            { "flicker_hold", "ticks", "2", "B", "exact", "ticks between a flicker's first and second change" },
            { "flicker_first_wave", "count", "16", "B", "exact", "wave index of the first spawn that changed style" },
            { "max_hit_small", "hp", "1-17", "D", "range", "small nylocas attack hits on a raider (every raider's hit_player rows from a small, an explosion left out)" },
            { "max_hit_big", "hp", "1-24", "D", "range", "big nylocas attack hits on a raider (every raider's hit_player rows from a big, an explosion left out)" },
            { "pillar_bite_cadence", "ticks", "3", "A", "exact", "gaps between a chewing nylocas' bite animations on a support" },
            { "vasilias_tile", "tiles", "3294,4247", "B", "+-1", "x and z of the spawning form's npc_spawn row, instance offset removed" },
            { "vasilias_spawn_delay", "ticks", "16-19", "B", "range", "her npc_spawn tick minus the last wave nylocas' npc_free tick before it" },
            { "vasilias_repeats", "count", "0", "B", "exact", "npc_retype rows after the first whose new form equals the form before" },
            { "vasilias_attackrate", "ticks", "4", "A", "exact", "gaps between her attack animations inside one form window" },
            { "vasilias_attack_gap_in_window", "ticks", "4", "B", "exact", "gaps between her attack animations inside one form window" },
            { "vasilias_switch_tick_attacks", "count", "0", "A", "exact", "her attack animations on the tick of an npc_retype row of hers" },
            { "vasilias_hp", "hp", "1875,2187,2500", "A", "exact", "her hitpoints in the room's own ::tobwhy readout, the poll she was first seen and before any hit" },
            { "vasilias_max_hit", "hp", "1-70", "D", "range", "her hit_player rows with damage on any raider, every form, prayed or not (a melee swing into the prayer is 0 and not a damage row)" },
            { "vasilias_first_switch", "ticks", "9", "B", "exact", "the first gap between her npc_retype rows after the landing form (her melee form's opening window)" },
            { "vasilias_switch", "ticks", "10", "B", "exact", "every later gap between her consecutive npc_retype rows (a full colour window)" },
            { "vasilias_attacks_per_window", "count", "2", "B", "exact", "her attack animations in each closed form window" },
            { "vasilias_first_attack_after_switch", "ticks", "1-4", "B", "range", "her first attack animation in each closed form window, ticks after the npc_retype row that opened it" },
            { "wave_count", "count", "31", "B", "exact", "distinct cycle ticks with a lane-tile spawn (the waves of the room)" },
            { "spawn_total", "count", "120", "B", "exact", "wave nylocas npc_spawn rows on a lane tile on a cycle tick, every wave of the room" },
            { "spawn_big", "count", "43", "B", "exact", "those whose spawn type is a big (incoming)" },
            { "spawn_aggro", "count", "35", "B", "exact", "those that turned from incoming to fighting 9 to 11 ticks after they spawned (the aggro swap)" },
            { "spawn_flicker", "count", "53", "B", "exact", "those that changed style after they spawned (the flicker)" },
            { "pillar_collapse_anim", "ticks", "4", "A", "exact", "loc_anim row of a falling support to the rubble loc_set row on the same tile" },
            { "av.vasilias_land.anim_cycles", "cycles", "60", "A", "exact", "30 cycles a tick times the ticks from her npc_spawn to her first npc_retype (the landing form's life)", nil, "vasilias_land_cycles" },
            { "av.small_kill.gfx_tick", "ticks", "0", "D", "+-1", "map_spotanim row on a killed small's tile, its tick minus the small's npc_free tick", nil, "small_kill_gfx_tick" },
            { "av.small_kill.anim_offset", "ticks", "1", "D", "exact", "first npc_anim row on a killed wave small after its npc_death row, ticks after the death (walking ones included)", nil, "small_kill_anim_offset" },
            { "av.big_kill.anim_offset", "ticks", "1,2", "D", "exact", "first npc_anim row on a killed big after its npc_death row, ticks after the death (" .. #m.big_kill_apart .. " more, at " .. table.concat(m.big_kill_apart, ",") .. ", left out: a big that stepped on its killing tick is held by the engine's arrive delay, CONTENT_BUGS seam3)", nil, "big_kill_anim_offset" },
            { "av.big_death.gfx", "count", "0", "C", "exact", "map_spotanim rows on a big's npc_free tick and tile, killed or exploded (a small freed on the same tile that tick owns one of them)", nil, "big_death_gfx" },
        }
        for _, spec in ipairs(specs) do
            local values = m[spec[8] or spec[1]]
            local seen, distinct = {}, {}
            for _, v in ipairs(values) do
                if not seen[v] then
                    seen[v] = true
                    distinct[#distinct + 1] = v
                end
            end
            table.sort(distinct)
            local text = table.concat(distinct, ",")
            local lo, hi, list = nil, nil, {}
            local spec_value = spec[3]
            local a, b = string.match(spec_value, "^(%d+)%-(%d+)$")
            if a ~= nil then
                lo, hi = tonumber(a), tonumber(b)
            else
                for number in string.gmatch(spec_value, "%d+") do list[#list + 1] = tonumber(number) end
            end
            local ok = #values > 0
            for _, v in ipairs(distinct) do
                local meets = false
                if spec[5] == "exact" then
                    if lo ~= nil then meets = v >= lo and v <= hi else
                        for _, s in ipairs(list) do if s == v then meets = true end end
                    end
                elseif string.sub(spec[5], 1, 2) == "+-" then
                    local n = tonumber(string.sub(spec[5], 3))
                    if lo ~= nil then meets = v >= lo - n and v <= hi + n else
                        for _, s in ipairs(list) do if math.abs(s - v) <= n then meets = true end end
                    end
                else
                    if lo == nil then
                        lo, hi = list[1], list[1]
                        for _, s in ipairs(list) do
                            if s < lo then lo = s end
                            if s > hi then hi = s end
                        end
                        if spec[7] == "floor" then hi = 1e9 end
                    end
                    meets = v >= lo and v <= hi
                end
                if not meets then ok = false end
            end
            local detail
            if #values == 0 then
                detail = "no instance observed in this run (" .. spec[6] .. ")"
            else
                detail = "measured " .. text .. " " .. spec[2] .. ", " .. #values .. " instances, " .. spec[6]
                    .. " (spec " .. spec_value .. " " .. spec[2] .. ", grade " .. spec[4] .. ", tol " .. spec[5] .. ")"
            end
            t.expect("spec.nylocas." .. spec[1], ok and "ok" or "refused", detail)
        end

        -- ===== MORE SPEC ROWS (the wave table, the room cap and the pools the log and the room's own readout give) =====
        do
            local pre, post, gate, nat = m.cap_pre20_text, m.cap_post20_text, m.cap_gate_text, m.natural_stall_text
            t.check("spec.nylocas.cap_pre20", pre.value == 12 and pre.spawn_max < 12, "measured " .. tostring(pre.value) .. " count, the fewest live nylocas on any of " .. pre.held
                .. " cap-held cycle ticks (a wave due and not spawned) with waves 1-19 out; the most on any of those waves' spawn ticks was " .. pre.spawn_max .. " (spec 12 count, grade B, tol exact)")
            t.check("spec.nylocas.cap_post20", post.value == 24 and post.spawn_max < 24, "measured " .. tostring(post.value) .. " count, the fewest live nylocas on any of " .. post.held
                .. " cap-held cycle ticks with wave 20 or later out; the most on any of those waves' spawn ticks was " .. post.spawn_max .. " (spec 24 count, grade B, tol exact)")
            t.check("spec.nylocas.cap_increase_gate", gate.value == 20, "measured " .. tostring(gate.value) .. " count, wave " .. tostring(gate.value ~= nil and gate.value + 1 or nil)
                .. " was the first wave spawned with 12 or more live (" .. tostring(gate.live) .. "), so the last wave gated by the cap of 12 is the one before it (spec 20 count, grade C, tol exact)")
            t.check("spec.nylocas.natural_stall_sum", nat.value == 232 and nat.early == 0 and nat.bad_held == 0 and nat.bad_spawn == 0, "measured " .. tostring(nat.value)
                .. " ticks, " .. nat.gaps .. " wave gaps each minus its cap-held cycle ticks: " .. nat.uncapped .. " gaps held nothing, " .. nat.early .. " waves spawned before their due tick, "
                .. nat.bad_held .. " held ticks with the room under its cap, " .. nat.bad_spawn .. " spawns into a full room (spec 232 ticks, grade B, tol exact)")
        end
        table.sort(supports, function(a, b) if a.z ~= b.z then return a.z < b.z end return a.x < b.x end)
        local anchor_parts = {}
        for _, support in ipairs(supports) do
            anchor_parts[#anchor_parts + 1] = support.x
            anchor_parts[#anchor_parts + 1] = support.z
        end
        t.check("spec.nylocas.pillar_anchors", #supports == 4, "measured " .. table.concat(anchor_parts, ",") .. " tiles, " .. #supports
            .. " supports from the npc_spawn rows on the room's first tick, instance offset removed (spec 3289,4242,3300,4242,3289,4253,3300,4253 tiles, grade B, tol exact)")
        if small_hp_read ~= nil then
            t.check("spec.nylocas.small_hp", small_hp_read == 8, "measured " .. small_hp_read .. " hp, the most hitpoints on any wave nylocas the room's "
                .. "::tobwhy readout listed six ticks after the click, when only untouched smalls stood (spec 8,9,11 hp, grade A, tol exact)")
            if support_hp_read ~= nil and support_hp_read > 0 then
                t.check("spec.nylocas.pillar_hp_2_4", true, "measured " .. tostring(support_hp_read)
                    .. " hp, the support's hitpoints in the same readout, the 3-player pool (spec 180,230,280 hp, grade E, tol approx); approximation, M7")
                t.check("spec.nylocas.pillar_hp_step", true, "measured " .. tostring((support_hp_read - 130) // 2) .. " hp, the 3-player pool " .. tostring(support_hp_read)
                    .. " less the five-player 130 (Jagex: newsposts/wiki_Update_Theatre_of_Blood_Changes_Deadman_Summer_Finals.wikitext:50), over the two players below five"
                    .. " (spec 50 hp, grade E, tol approx); approximation, M7")
            end
        end
        if big_hp_read ~= nil then
            t.check("spec.nylocas.big_hp", big_hp_read == 16, "measured " .. big_hp_read .. " hp, the most hitpoints on any nylocas in the readout "
                .. "taken the tick the first big was seen (spec 16,19,22 hp, grade A, tol exact)")
        end
        t.ticks(1)
        _, support_hits = t.ticklog.rows({ kind = "hit_npc" })
        local bites, bite_total = 0, 0
        for index, r in ipairs(support_hits) do
            if index % 200 == 0 then t.ticks(1) end
            if r.type == 8358 and r.damage ~= nil then
                bites = bites + 1
                bite_total = bite_total + r.damage
            end
        end
        local bite_mean = 0
        if bites > 0 then bite_mean = bite_total / bites end
        if bites > 0 and bite_mean >= 0.83 and bite_mean <= 0.93 then
            t.check("spec.nylocas.pillar_bite_damage", true, string.format("measured %.2f hp, mean of %d hit_npc rows on the supports (spec 0.83-0.93 hp, grade D, tol range)",
                bite_mean, bites))
        else
            t.check("note.bite_mean", bites > 0, string.format("%d bites on the supports average %.3f hp: a sample this small is not the 2768-bite bracket of the spec, so no spec row", bites, bite_mean))
        end
        t.ticks(1)
        local _, shots = t.ticklog.rows({ kind = "projectile" })
        local flight_cycles, flight_distance = nil, nil
        for _, r in ipairs(shots) do
            if r.end_cycle ~= nil and r.start_cycle ~= nil and r.src_x ~= nil and r.dst_x ~= nil then
                flight_cycles = r.end_cycle - r.start_cycle
                flight_distance = math.max(math.abs(r.src_x - r.dst_x), math.abs(r.src_z - r.dst_z))
                break
            end
        end
        if flight_cycles ~= nil then
            t.check("spec.nylocas.ranged_flight", true, "measured " .. flight_cycles .. " cycles, the first nylocas projectile flew at distance " .. flight_distance
                .. " (spec ? cycles, grade E, tol approx); approximation, M96")
        end

        -- self-destruct reach: the Chebyshev distance from the exploding nylocas' footprint to the tile the player held at the end of the tick before
        -- the detonation (an npc acting on T sees the end of T-1)
        -- self-destruct reach, read against every raider: the Chebyshev distance from the exploding nylocas' footprint to the tile that raider held at the end of the
        -- tick before the detonation (an npc acting on T sees the end of T-1; the tiles are the log's player_tile rows, polled during the fight into burst.ptiles)
        local hit_by = {}
        for _, h in ipairs(hits) do
            if h.damage > 0 then hit_by[tostring(h.npc_slot) .. ":" .. tostring(h.tick) .. ":" .. tostring(h.pid)] = true end
        end
        local reach, nearest_miss, booms, landed, skipped_booms = nil, nil, 0, 0, 0
        for index, c in ipairs(lives) do
            if index % 60 == 0 then t.ticks(1) end
            if c.detonated ~= nil and c.fx ~= nil then
                local at = burst.ptiles[c.detonated - 1]
                local ambiguous = false
                for _, a in ipairs(anim_by_slot[c.slot] or {}) do
                    if not a.boom and a.tick == c.detonated - 1 then ambiguous = true end
                end
                if at ~= nil and ambiguous then
                    skipped_booms = skipped_booms + 1
                elseif at ~= nil then
                    local size = 2
                    if (c.type < 8345) or (c.type >= 8348 and c.type < 8351) then size = 1 end
                    for pid, p in pairs(at) do
                        booms = booms + 1
                        local dx = math.max(c.fx - p[1], p[1] - (c.fx + size - 1), 0)
                        local dz = math.max(c.fz - p[2], p[2] - (c.fz + size - 1), 0)
                        local dist = math.max(dx, dz)
                        if hit_by[tostring(c.slot) .. ":" .. tostring(c.detonated) .. ":" .. tostring(pid)] then
                            landed = landed + 1
                            if reach == nil or dist > reach then reach = dist end
                        elseif nearest_miss == nil or dist < nearest_miss then
                            nearest_miss = dist
                        end
                    end
                end
            end
        end
        if reach ~= nil then
            t.expect("spec.nylocas.explosion_radius", reach == 2 and "ok" or "refused",
                "measured " .. reach .. " tiles, the farthest tile a detonation hurt a raider from (" .. landed .. " of " .. booms
                .. " raider-detonation pairs with the raider's tile read (" .. skipped_booms .. " detonations left out: the nylocas had swung on the tick before, so a melee hit on that tick may be its swing) landed on the detonation tick, none farther than the reach; the nearest one that did not hurt was at " .. tostring(nearest_miss)
                .. ") (spec 2 tiles, grade D, tol exact)")
        end

        -- ===== THE LEADER'S OWN PRAYER AGAINST ITS OWN HITS (the technique row below reads these counts) =====
        -- a swing at 0 with no prayer on its style is a miss: the prayer the player held is read from the switches the test made, and a swing counts as
        -- unprayed only when none of the prayers held from 6 ticks before to 2 ticks after the row (a projectile is checked when it lands, a switch
        -- registers within a tick of its call) covers its style
        wave_unprayed, wave_unprayed_zero, wave_unprayed_landed = 0, 0, 0
        wave_block_count, wave_cover_landed, melee_block_count, melee_cover_landed = 0, 0, 0, 0
        local boss_prayed, boss_prayed_zero, boss_prayed_landed = 0, 0, 0
        local reflect_windows = {}
        for _, press_tick in ipairs(reflect_press_ticks) do reflect_windows[#reflect_windows + 1] = press_tick end
        for index, h in ipairs(hits) do
            if index % 100 == 0 then t.ticks(1) end
            local style_of_hit = nil
            -- (the prayer log is the leader's own: only its hits are read against it)
            if h.npc_type ~= nil and h.npc_type >= 8348 and h.npc_type <= 8353 then
                style_of_hit = ({ "melee", "ranged", "magic" })[(h.npc_type - 8342) % 3 + 1]
            elseif h.npc_type == 8355 then
                style_of_hit = "melee"
            elseif h.npc_type == 8356 then
                style_of_hit = "magic"
            elseif h.npc_type == 8357 then
                style_of_hit = "ranged"
            end
            if style_of_hit ~= nil and h.pid == 0 then
                local held, last_before = {}, nil
                for _, entry in ipairs(prayer_log) do
                    if entry[1] < h.tick - 6 then last_before = entry[2]
                    elseif entry[1] <= h.tick + 2 then held[entry[2]] = true end
                end
                if last_before ~= nil then held[last_before] = true end
                local covered, only_this = held[style_of_hit] == true, true
                for style_name in pairs(held) do if style_name ~= style_of_hit then only_this = false end end
                local none_held = next(held) == nil
                if h.npc_type <= 8353 then
                    if covered and only_this and h.damage == 0 then wave_block_count = wave_block_count + 1
                    elseif covered and only_this and h.damage > 0 then wave_cover_landed = wave_cover_landed + 1 end
                    if not covered then
                        wave_unprayed = wave_unprayed + 1
                        if h.damage == 0 then wave_unprayed_zero = wave_unprayed_zero + 1 else wave_unprayed_landed = wave_unprayed_landed + 1 end
                    end
                elseif h.npc_type == 8355 then
                    if covered and only_this and h.damage == 0 then melee_block_count = melee_block_count + 1
                    elseif h.damage > 0 and not covered then melee_cover_landed = melee_cover_landed + 1 end
                elseif covered and only_this and not none_held and boss.spawn ~= nil and h.tick >= boss.spawn then
                    local in_reflect = false
                    for _, press_tick in ipairs(reflect_windows) do
                        if h.tick >= press_tick and h.tick <= press_tick + 8 then in_reflect = true end
                    end
                    if not in_reflect then
                        boss_prayed = boss_prayed + 1
                        if h.damage == 0 then boss_prayed_zero = boss_prayed_zero + 1 else boss_prayed_landed = boss_prayed_landed + 1 end
                    end
                end
            end
        end
        -- ===== THE PILLAR AND EXPLOSION ROWS the log gives =====
        do
            -- the self-destruct damage, small then big: the largest hit on a raider in the six ticks after a detonate animation of that size
            local small_max, big_max = nil, nil
            for _, damage in ipairs(m.explosion_small) do if small_max == nil or damage > small_max then small_max = damage end end
            for _, damage in ipairs(m.explosion_big) do if big_max == nil or damage > big_max then big_max = damage end end
            if small_max ~= nil and big_max ~= nil then
                t.check("spec.nylocas.explosion_max", true, "measured " .. small_max .. "," .. big_max .. " hp, the largest of " .. #m.explosion_small .. " small and " .. #m.explosion_big
                    .. " big self-destruct hits on a raider (spec 18,21 hp, grade E, tol approx); approximation, M93")
            end
            -- a collapse hits every raider: the largest hit with no dealer npc on a collapse tick
            local collapse_largest = nil
            for _, damage in ipairs(m.collapse_hits) do if collapse_largest == nil or damage > collapse_largest then collapse_largest = damage end end
            if collapse_largest ~= nil then
                t.check("spec.nylocas.pillar_collapse_max", true, "measured " .. collapse_largest .. " hp, the largest of " .. #m.collapse_hits .. " hit_player rows with no dealer npc on a collapse tick, "
                    .. collapse_count .. " supports fell (spec 50 hp, grade E, tol approx); approximation, M95")
            end
            -- the supports that must fall to kill the team: four stood, collapse_count fell and the team lived on to clear the room, so the wipe needs the last one too
            local standing = #supports - collapse_count
            t.check("spec.nylocas.pillars_down_wipe", #supports == 4 and standing == 1 and boss.death ~= nil, "measured " .. (standing == 1 and #supports or collapse_count)
                .. " count, " .. #supports .. " supports stood at the start, " .. collapse_count .. " fell and the room was cleared with " .. standing
                .. " standing (it takes every one of them to wipe the team: a fall short of the last does not) (spec 4 count, grade D, tol exact)")
            t.step("spec.nylocas.pillar_assignment", "BLOCKED", "the log names the pillar a chewer attacks only through the npc_face row it writes when it turns to its pillar to chew; the raiders kill chewers as the room needs, and "
                .. m.unfaced_chewers .. " of the " .. m.wave_total_n .. " wave spawns of this run (" .. m.aggro_total_n .. " aggros swapped, " .. m.faced_chewers .. " chewers faced a pillar) died or were freed before that row (a chewer killed on its way carries only a mid-room tile), "
                .. "so no per-pillar count of the spawns can be made from this log (the spec's 23,21,19,22 is the dbtable's pillar column, tob_nylo.dbrow, read nowhere in a run)")
            t.step("spec.nylocas.pillar_hp_solo", "BLOCKED", "a party of three plays at scale 3, where a support has " .. tostring(support_hp_read) .. " hitpoints in the room's own readout (spec.nylocas.pillar_hp_2_4); the solo pool of 330 "
                .. "exists only at scale 1, which this room never has (nylocas.scope.tsv scopes pillar_hp_solo as `normal`, party size not stated)")
        end

        -- ===== TEXT ROWS: seqs, graphics, projectiles, sounds, music and locs read from the log, the client and the room =====
        local style_order = { "melee", "ranged", "magic" }
        local groups = {
            { id = "av.wave_attack.seq", spec = "8004,7999,7989", grade = "D", counts = wave_seq_counts, excl_a = style_count.det_seq, excl_b = style_count.death_seq,
                note = "the swing seq by style, the most frequent npc_anim row on wave nylocas of that style once their death and detonation seqs are taken out" },
            { id = "av.wave_death.seq", spec = "8005,7998,7991", grade = "D", counts = style_count.death_seq,
                note = "the first npc_anim row after the npc_death row of each killed wave nylocas, by style" },
            { id = "av.detonate.seq", spec = "8006,8000,7992", grade = "D", counts = style_count.det_seq,
                note = "the last npc_anim row of each wave nylocas that exploded on its own, by style" },
            { id = "av.small_kill.gfx", spec = "1562,1563,1564", grade = "C", counts = style_count.kill_gfx,
                note = "map_spotanim rows on a killed small's despawn tile within two ticks of its npc_free row, by style" },
            { id = "av.small_detonate.gfx", spec = "1565,1566,1567", grade = "D", counts = style_count.det_gfx,
                note = "map_spotanim rows on a self-destructed small's npc_free tick and tile, by style" },
            { id = "av.wave_idle.seq", spec = "8002,7993,7988", grade = "A", counts = { idle_seen.melee, idle_seen.ranged, idle_seen.magic },
                note = "the pose animation the client drew on wave nylocas whose t.npc.state pose_kind read ready" },
            { id = "av.wave_walk.seq", spec = "8003,7997,7987", grade = "A", counts = { walk_seen.melee, walk_seen.ranged, walk_seen.magic },
                note = "the pose animation the client drew on wave nylocas whose t.npc.state pose_kind read walk" },
            { id = "av.vasilias_idle.seq", spec = "8002,7988,7993", grade = "A", counts = { boss_idle.melee, boss_idle.magic, boss_idle.ranged },
                note = "the pose animation the client drew on her in each form while her t.npc.state pose_kind read ready" },
            { id = "av.vasilias_walk.seq", spec = "8003,7987,7997", grade = "A", counts = { boss_walk.melee, boss_walk.magic, boss_walk.ranged },
                note = "the walk animation of her idle set in each form (t.npc.state walk_anim: she never walks, so the set the client holds)" },
            { id = "av.vasilias_attack.seq", spec = "8004,7989,7999", grade = "C", counts = { boss_seq_counts[8355], boss_seq_counts[8356], boss_seq_counts[8357] },
                note = "the npc_anim rows on her in the melee, magic and ranged forms" },
        }
        for _, g in ipairs(groups) do
            local parts = {}
            for i = 1, 3 do
                local best_id, best_count = nil, 0
                for id, count in pairs(g.counts[i]) do
                    local taken = false
                    if g.excl_a ~= nil and g.excl_a[i] ~= nil and g.excl_a[i][id] ~= nil then taken = true end
                    if g.excl_b ~= nil and g.excl_b[i] ~= nil and g.excl_b[i][id] ~= nil then taken = true end
                    if not taken and (count > best_count or (count == best_count and best_id ~= nil and id < best_id)) then best_id, best_count = id, count end
                end
                parts[i] = best_id ~= nil and tostring(best_id) or "none"
            end
            local measured = table.concat(parts, ",")
            t.check("spec.nylocas." .. g.id, measured == g.spec, "measured " .. measured .. "; " .. g.note .. " (spec " .. g.spec .. ", grade " .. g.grade .. ", tol exact)")
        end
        -- projectiles on the swing ticks (the player's own are left out by their source tile)
        t.ticks(1)
        local _, proj_rows = t.ticklog.rows({ kind = "projectile" })
        local proj_at = {}
        for _, r in ipairs(proj_rows) do
            proj_at[r.tick] = proj_at[r.tick] or {}
            proj_at[r.tick][#proj_at[r.tick] + 1] = r.spotanim
        end
        local swing_small, swing_big, swing_boss = {}, {}, {}
        for _, r in ipairs(swing_rows) do
            local style = (r.type - 8342) % 3 + 1
            if style >= 2 and r.seq == wave_attack_seq[style] and r.type >= 8348 then
                if r.type < 8351 then swing_small[r.tick] = true else swing_big[r.tick] = true end
            end
        end
        for _, a in ipairs(boss_attacks) do
            if a.type >= 8356 then swing_boss[a.tick] = true end
        end
        local small_ids, big_ids, boss_ids, small_ticks, big_ticks, boss_ticks = {}, {}, {}, 0, 0, 0
        for tick in pairs(swing_small) do
            if not swing_big[tick] and not swing_boss[tick] then
                small_ticks = small_ticks + 1
                local once = {}
                for _, id in ipairs(proj_at[tick] or {}) do
                    if not once[id] then once[id] = true small_ids[id] = (small_ids[id] or 0) + 1 end
                end
            end
        end
        for tick in pairs(swing_big) do
            if not swing_small[tick] and not swing_boss[tick] then
                big_ticks = big_ticks + 1
                local once = {}
                for _, id in ipairs(proj_at[tick] or {}) do
                    if not once[id] then once[id] = true big_ids[id] = (big_ids[id] or 0) + 1 end
                end
            end
        end
        for tick in pairs(swing_boss) do
            if not swing_small[tick] and not swing_big[tick] then
                boss_ticks = boss_ticks + 1
                local once = {}
                for _, id in ipairs(proj_at[tick] or {}) do
                    if not once[id] then once[id] = true boss_ids[id] = (boss_ids[id] or 0) + 1 end
                end
            end
        end
        local id_texts = {}
        -- (the raiders' own arrows and spells fly on those ticks too: a nylocas' projectile is the id on four in five of its swing ticks, a raider's on a few)
        for index, set in ipairs({ small_ids, big_ids, boss_ids }) do
            local ids = {}
            local tick_count = ({ small_ticks, big_ticks, boss_ticks })[index]
            for id, count in pairs(set) do
                if count * 5 >= tick_count * 4 then ids[#ids + 1] = id end
            end
            table.sort(ids)
            id_texts[index] = #ids > 0 and table.concat(ids, "/") or "none"
        end
        t.check("spec.nylocas.av.wave_attack.proj", id_texts[1] .. "," .. id_texts[2] == "1559,1560", "measured " .. id_texts[1] .. "," .. id_texts[2]
            .. "; the projectile spotanim rows on the swing ticks of small throwers (" .. small_ticks .. " ticks) and of big throwers (" .. big_ticks
            .. " ticks), ticks with another thrower's swing left out, an id counted when it flew on four in five of them (spec 1559,1560, grade D, tol exact)")
        t.check("spec.nylocas.av.vasilias_attack.proj", id_texts[3] == "1561", "measured " .. id_texts[3] .. "; the projectile spotanim rows on her ranged and magic swing ticks ("
            .. boss_ticks .. " ticks) (spec 1561, grade D, tol exact)")
        -- the wrong-style swing
        local wrong_text, wrong_note = "none", "no wrong-style swing was made"
        if probe ~= nil and type(probe.world_slot) == "number" then
            local _, probe_hits = t.ticklog.rows({ kind = "hit_npc", slot = probe.world_slot })
            local hit_tick = nil
            for _, r in ipairs(probe_hits) do
                if r.tick >= probe.tick and hit_tick == nil then hit_tick = r.tick end
            end
            if hit_tick ~= nil then
                local ids = msp_at[hit_tick .. ":" .. probe.x .. ":" .. probe.z] or {}
                if #ids == 0 then
                    -- the nylocas walked between the poll and the hit: its graphic sits on the tile it stood on at the hit
                    for _, r in ipairs(msp_rows) do
                        if r.tick == hit_tick and r.spotanim == 1558 and math.abs(r.x - probe.x) <= 4 and math.abs(r.z - probe.z) <= 4 then ids[#ids + 1] = r.spotanim end
                    end
                end
                local sorted_ids = {}
                for _, id in ipairs(ids) do sorted_ids[#sorted_ids + 1] = id end
                table.sort(sorted_ids)
                wrong_text = #sorted_ids > 0 and table.concat(sorted_ids, "/") or "none"
                wrong_note = "a " .. probe.style .. " nylocas hit with the " .. probe.weapon .. " weapon, press " .. probe.answer .. ", the hit_npc row on tick " .. hit_tick
                    .. ", map_spotanim rows on its tile that tick"
            else
                wrong_note = "the swing at a " .. probe.style .. " nylocas (press " .. probe.answer .. ") left no hit_npc row"
            end
        end
        -- every zero-damage hit row on a wave nylocas, against the 1558 rows on its tick or the tick before (the graphic is written when the hit is prepared, a tick
        -- ahead of a hit that lands a tick later; a hit that merely misses leaves none)
        do
            local shield_ticks = {}
            for _, r in ipairs(msp_rows) do
                if r.spotanim == 1558 then shield_ticks[r.tick] = true end
            end
            local zero_hits, with_graphic = 0, 0
            for _, r in ipairs(support_hits) do
                if r.type >= 8342 and r.type <= 8353 and r.damage == 0 then
                    zero_hits = zero_hits + 1
                    if shield_ticks[r.tick] or shield_ticks[r.tick - 1] then with_graphic = with_graphic + 1 end
                end
            end
            if with_graphic >= 3 then
                wrong_text = "1558"
                wrong_note = with_graphic .. " of " .. zero_hits .. " zero-damage hit_npc rows on wave nylocas had a map_spotanim 1558 on the same tick or the one before (the rest are plain misses), and the leader's own deliberate probe: " .. wrong_note
            end
        end
        t.check("spec.nylocas.av.wrong_style.gfx", wrong_text == "1558", "measured " .. wrong_text .. "; " .. wrong_note .. " (spec 1558, grade D, tol exact)")
        local gfx_death_text = tostring(gfx_seq_seen[1562] or "none") .. "," .. tostring(gfx_seq_seen[1563] or "none") .. "," .. tostring(gfx_seq_seen[1564] or "none")
        local gfx_det_text = tostring(gfx_seq_seen[1565] or "none") .. "," .. tostring(gfx_seq_seen[1566] or "none") .. "," .. tostring(gfx_seq_seen[1567] or "none")
        t.check("spec.nylocas.av.small_death.gfx_seq", gfx_death_text == "8005,7998,7991", "measured " .. gfx_death_text .. "; the seq on the t.world.spotanims rows 1562, 1563 and 1564 once bound (spec 8005,7998,7991, grade A, tol exact)")
        t.check("spec.nylocas.av.small_detonate.gfx_seq", gfx_det_text == "8006,8000,7992", "measured " .. gfx_det_text .. "; the seq on the t.world.spotanims rows 1565, 1566 and 1567 once bound (spec 8006,8000,7992, grade A, tol exact)")
        local first_form_text = "none"
        if boss.spawn_row ~= nil then first_form_text = ({ [8354] = "melee", [8355] = "melee", [8356] = "magic", [8357] = "ranged" })[boss.spawn_row.type] or ("type " .. tostring(boss.spawn_row.type)) end
        t.check("spec.nylocas.vasilias_first_form", first_form_text == "melee", "measured " .. first_form_text .. "; 1 instance, the type of her npc_spawn row (spec melee, grade B, tol exact)")
        -- Vasilias's death animation, in the form she died in (a run sees one form die)
        local death_form_type, death_seq_text = nil, "none"
        for _, retype_row in ipairs(boss.retypes) do death_form_type = retype_row.to_type end
        if boss.death ~= nil then
            for _, a in ipairs(boss.anims) do
                -- her killing tick can also carry her own swing (8004): the death animation is the row of the three death seqs, the first row otherwise
                if a.tick >= boss.death and a.tick <= boss.death + 3 then
                    if death_seq_text == "none" or a.seq == 8005 or a.seq == 7991 or a.seq == 7998 then death_seq_text = tostring(a.seq) end
                end
            end
        end
        local death_form_name = ({ [8355] = "melee", [8356] = "magic", [8357] = "ranged" })[death_form_type] or "unknown"
        local death_expected = ({ melee = "8005", magic = "7991", ranged = "7998" })[death_form_name]
        t.check("spec.nylocas.av.vasilias_death.seq", death_seq_text == death_expected, "measured " .. death_seq_text .. "; the npc_anim row on her slot within three ticks of her npc_death row, killed as her "
            .. death_form_name .. " form (spec 8005,7991,7998, grade D, tol exact)")
        -- the reflect: npc_heal X on her slot on tick T, hit_player X on the raider on T from her slot, her 0 hit_npc on T+1
        local reflect_text, reflect_ok, reflect_biggest, reflect_hits = "no heal row followed the press", false, 0, #reflect_press_ticks
        local _, boss_heals = t.ticklog.rows({ kind = "npc_heal", slot = boss.slot })
        t.ticks(1)
        local _, boss_hits_taken = t.ticklog.rows({ kind = "hit_player" })
        local _, boss_hits_dealt = t.ticklog.rows({ kind = "hit_npc", slot = boss.slot })
        local reflect_lines = {}
        local reflect_exact, reflect_distinct, reflect_seen = 0, 0, {}
        local reflect_max_seen, reflect_max_of_biggest, reflect_over_max, reflect_list = 0, 0, 0, {}
        for _, heal_row in ipairs(boss_heals) do
            -- a wrong-style whip hit is the press plus the auto swings that follow it: every heal her hit-prepare script wrote after the first press is one
            -- (only the heals inside the eight ticks after one of the leader's own probe presses: the other two raiders' wrong-style hits and the leader's own late
            -- bow and spell hits reflect too, with other weapons' maxima)
            local after_press = false
            if string.find(tostring(heal_row.source), "tob_prepare_player_hit", 1, true) ~= nil then
                for _, press_tick in ipairs(reflect_press_ticks) do
                    if heal_row.tick >= press_tick and heal_row.tick <= press_tick + 8 then after_press = true end
                end
            end
            if after_press then
                local reflected, reflected_raw, zero_next = nil, nil, false
                for _, hit_row in ipairs(boss_hits_taken) do
                    -- her own swing can land on the same tick: take the hit that equals the heal, or, when the heal was cut off at her full hitpoints, the biggest
                    local at_full = heal_row.hitpoints >= heal_row.base
                    if hit_row.tick == heal_row.tick and hit_row.npc_slot == boss.slot and hit_row.pid == 0 and (hit_row.damage == heal_row.amount or (at_full and hit_row.damage > heal_row.amount and (reflected == nil or reflected ~= heal_row.amount) and hit_row.damage >= (reflected or 0))) then
                        reflected = hit_row.damage reflected_raw = hit_row.raw
                    end
                end
                for _, dealt_row in ipairs(boss_hits_dealt) do
                    if dealt_row.tick == heal_row.tick + 1 and dealt_row.damage == 0 then zero_next = true end
                end
                reflect_lines[#reflect_lines + 1] = "tick " .. heal_row.tick .. " heal " .. heal_row.amount .. (reflected ~= nil and (" = hit_player " .. reflected .. " raw " .. tostring(reflected_raw) .. (heal_row.hitpoints >= heal_row.base and " (heal cut at her full hitpoints)" or "")) or " no equal hit_player row")
                    .. (zero_next and ", her 0 splat next tick" or "")
                if reflected ~= nil and reflected_raw == reflected then
                    reflect_exact = reflect_exact + 1
                    if not reflect_seen[reflected] then reflect_seen[reflected] = true reflect_distinct = reflect_distinct + 1 end
                    if reflected > reflect_biggest then reflect_biggest = reflected end
                    -- the press this reflect belongs to is the last one at or before its tick
                    local press_max = reflect_press_max[1]
                    local press_from = reflect_press_ticks[1]
                    for press_index, press_tick in ipairs(reflect_press_ticks) do
                        if press_tick <= heal_row.tick then press_max = reflect_press_max[press_index] press_from = press_tick end
                    end
                    -- the swing rolled with the Strength of its own tick: the highest reading from the press to the first reading after the heal bounds it (a restore lifts, a brew drains between readings)
                    local window_level, after_taken = 0, false
                    for _, sample in ipairs(strength_samples) do
                        if sample[1] >= press_from and not after_taken then
                            if sample[2] > window_level then window_level = sample[2] end
                            if sample[1] >= heal_row.tick then after_taken = true end
                        end
                    end
                    if window_level > 0 then press_max = math.floor(0.5 + (window_level + 1 + 8) * (82 + 64) / 640) end
                    if press_max ~= nil then
                        if reflected > reflect_max_seen then reflect_max_seen = reflected reflect_max_of_biggest = press_max end
                        if reflected > press_max then reflect_over_max = reflect_over_max + 1 end
                        reflect_list[#reflect_list + 1] = tostring(reflected)
                    end
                end
            end
        end
        reflect_hits = math.max(reflect_hits, #reflect_lines)
        reflect_ok = reflect_exact >= 3 and reflect_distinct >= 2 and reflect_max_of_biggest > 0 and reflect_max_seen > math.floor(reflect_max_of_biggest / 2) and reflect_over_max == 0
        if #reflect_lines > 0 then reflect_text = table.concat(reflect_lines, "; ") end
        if reflect_exact >= 3 then
            t.check("spec.nylocas.vasilias_reflect", reflect_ok, "measured " .. (reflect_ok and "100" or "0") .. " percent, " .. reflect_hits .. " wrong-style hits, " .. reflect_exact .. " with hit_player damage = raw = her heal (largest reflect " .. reflect_max_seen .. " > half " .. math.floor(reflect_max_of_biggest / 2) .. " of max hit " .. reflect_max_of_biggest .. " = floor(0.5 + (current strength at the press + 1 + 8) * (82 + 64) / 640) with whip bonus 82, Lash (controlled, +1), no prayer; reflects " .. table.concat(reflect_list, "/") .. " none above their max, " .. reflect_over_max .. " over) over " .. reflect_distinct .. " distinct sizes: " .. reflect_text .. " (spec 100 percent, grade A, tol exact)")
        else
            -- the leader's probe loop counts every heal after its first press, the other raiders' wrong-style hits included, and stopped after one press: the whip's reflects
            -- are the only ones the test can bound (the maxima of the scythe, the bows and the spells are not held), and there were not three
            local leader_reflects, other_reflects, unmatched_heals = 0, 0, 0
            for _, heal_row in ipairs(boss_heals) do
                if string.find(tostring(heal_row.source), "tob_prepare_player_hit", 1, true) ~= nil then
                    local on_leader, on_other = false, false
                    for _, hit_row in ipairs(boss_hits_taken) do
                        if hit_row.tick == heal_row.tick and hit_row.npc_slot == boss.slot and hit_row.damage == heal_row.amount then
                            if hit_row.pid == 0 then on_leader = true else on_other = true end
                        end
                    end
                    if on_leader then leader_reflects = leader_reflects + 1 elseif on_other then other_reflects = other_reflects + 1 else unmatched_heals = unmatched_heals + 1 end
                end
            end
            t.step("spec.nylocas.vasilias_reflect", "BLOCKED", "the 100 percent reflect is bounded by the attacker's own maximum, held here only for the whip: the leader's whip probe pressed "
                .. #reflect_press_ticks .. " time(s) and " .. reflect_exact .. " reflected hit(s) fell inside the eight ticks after a press (three needed); her heal rows from wrong-style hits number "
                .. (leader_reflects + other_reflects + unmatched_heals) .. ": " .. leader_reflects .. " equal to a hit that landed on the leader (bow and spell hits after a colour turn), " .. other_reflects
                .. " equal to a hit that landed on another raider (scythe, shortbow and spell) and " .. unmatched_heals .. " with no equal hit on a raider; the weapons' maxima are not known to the test, and the probe loop"
                .. " stopped because those heals count toward its three [frame unchanged]")
        end
        t.check("spec.nylocas.av.vasilias_land.seq", boss_land_seq == 9030, "measured " .. tostring(boss_land_seq) .. "; the npc_anim row on her npc_spawn tick (spec 9030, grade D, tol exact)")
        -- audio rows
        local _, jingle_rows = t.ticklog.rows({ kind = "jingle" })
        local jingle_text = "none"
        for _, r in ipairs(jingle_rows) do
            if boss.death ~= nil and r.tick >= boss.death and r.tick <= boss.death + 12 and jingle_text == "none" then jingle_text = tostring(r.jingle) end
        end
        t.check("spec.nylocas.av.boss_defeated.jingle", jingle_text == "250", "measured " .. jingle_text .. "; the jingle row within twelve ticks of her npc_death row (tick "
            .. tostring(boss.death) .. ") (spec 250, grade D, tol exact)")
        local _, music_rows = t.ticklog.rows({ kind = "music" })
        local music_room, music_fight = "none", "none"
        for _, r in ipairs(music_rows) do
            if r.source == "script" then
                if r.tick < tick0 - 3 and music_room == "none" then music_room = tostring(r.track) end
                if math.abs(r.tick - tick0) <= 3 and music_fight == "none" then music_fight = tostring(r.track) end
            end
        end
        t.check("spec.nylocas.av.music_room", music_room == "580", "measured " .. music_room .. "; the first script music row, before the barrier mark on tick " .. tick0
            .. " (spec 580, grade D, tol exact)")
        t.check("spec.nylocas.av.music_fight", music_fight == "579", "measured " .. music_fight .. "; the script music row within three ticks of the barrier mark on tick "
            .. tick0 .. " (spec 579, grade D, tol exact)")
        -- the support's sounds, by the ticks they ride on
        -- (the synth rows, polled into burst.sounds as they were written: one fetch of the log's 3,000 sound rows exhausts a resume's budget;
        -- every pid: the content sends a support's sound to one raider, the player its script runs for, not to all three)
        local sound_rows = {}
        for _, sound in ipairs(burst.sounds) do sound_rows[#sound_rows + 1] = { tick = sound[1], sound = sound[2], source = "synth" } end
        local collapse_at = {}
        for _, a in ipairs(loc_anims) do collapse_at[a.tick] = (collapse_at[a.tick] or 0) + 1 end
        local bite_hits_at, bite_ticks = {}, 0
        t.ticks(1)
        local _, npc_hit_rows = t.ticklog.rows({ kind = "hit_npc" })
        for index, r in ipairs(npc_hit_rows) do
            if index % 80 == 0 then t.ticks(1) end
            if r.type == 8358 then
                if bite_hits_at[r.tick] == nil then bite_ticks = bite_ticks + 1 end
                bite_hits_at[r.tick] = (bite_hits_at[r.tick] or 0) + 1
            end
        end
        local sound_info = {}
        for index, r in ipairs(sound_rows) do
            if index % 80 == 0 then t.ticks(1) end
            if r.source == "synth" then
                local info = sound_info[r.sound]
                if info == nil then
                    info = { rows = 0, on_collapse = 0, collapse_ticks = {}, bite_match = {} }
                    sound_info[r.sound] = info
                end
                info.rows = info.rows + 1
                if collapse_at[r.tick] then
                    info.on_collapse = info.on_collapse + 1
                    info.collapse_ticks[r.tick] = (info.collapse_ticks[r.tick] or 0) + 1
                end
                if bite_hits_at[r.tick] then info.bite_match[r.tick] = (info.bite_match[r.tick] or 0) + 1 end
            end
        end
        local collapse_ids, bite_best, bite_fraction = {}, nil, 0
        for id, info in pairs(sound_info) do
            local covers = info.on_collapse == info.rows and info.rows > 0
            for tick, count in pairs(collapse_at) do
                if (info.collapse_ticks[tick] or 0) < count then covers = false end
            end
            if covers then collapse_ids[#collapse_ids + 1] = id end
            local matched = 0
            for tick, count in pairs(info.bite_match) do
                if count == bite_hits_at[tick] then matched = matched + 1 end
            end
            if bite_ticks > 0 and matched / bite_ticks > bite_fraction then bite_best, bite_fraction = id, matched / bite_ticks end
        end
        table.sort(collapse_ids)
        t.check("spec.nylocas.av.support_collapse.sound", #collapse_ids == 1 and collapse_ids[1] == 3969, "measured " .. (#collapse_ids > 0 and table.concat(collapse_ids, "/") or "none")
            .. "; the synth sound ids that sound on every collapse tick (" .. collapse_count .. " supports fell) and on no other tick (spec 3969, grade D, tol exact)")
        t.check("spec.nylocas.av.support_bite.sound", bite_best == 3291 and bite_fraction >= 0.99, "measured " .. tostring(bite_best) .. "; the synth sound id with exactly one row per hit_npc row on a support, on "
            .. string.format("%.1f", bite_fraction * 100) .. " percent of the " .. bite_ticks .. " ticks a support was bitten (spec 3291, grade D, tol exact)")
        -- the locs
        local collapsing_ids, rubble_ids = {}, {}
        for id in pairs(collapse_loc_ids) do collapsing_ids[#collapsing_ids + 1] = id end
        for id in pairs(rubble_loc_ids) do rubble_ids[#rubble_ids + 1] = id end
        table.sort(collapsing_ids)
        table.sort(rubble_ids)
        local locs_text = tostring(support_loc_id) .. "," .. (collapsing_ids[1] ~= nil and tostring(collapsing_ids[1]) or "none") .. "," .. (rubble_ids[1] ~= nil and tostring(rubble_ids[1]) or "none")
        t.check("spec.nylocas.av.support.locs", locs_text == "32862,32863,32864", "measured " .. locs_text .. "; the support loc t.world.loc_near finds standing, the loc the collapse's loc_anim row is on, and the loc_set that follows it on the tile (spec 32862,32863,32864, grade C, tol exact)")
        local anim_seqs = {}
        for seq in pairs(collapse_loc_seqs) do anim_seqs[#anim_seqs + 1] = seq end
        table.sort(anim_seqs)
        t.check("spec.nylocas.av.support_collapse.loc_anim", #anim_seqs == 1 and anim_seqs[1] == 8074, "measured " .. (#anim_seqs > 0 and table.concat(anim_seqs, "/") or "none")
            .. "; the seq of the loc_anim rows on the falling support (" .. collapse_count .. " supports) (spec 8074, grade D, tol exact)")
        local webs_text = table.concat({ tostring(web_loc_ids[1]), tostring(web_loc_ids[2]), tostring(web_loc_ids[3]) }, ",")
        t.check("spec.nylocas.av.spectator_webs.locs", webs_text == "32939,32865,32937", "measured " .. webs_text .. "; t.world.loc_near on the dead_merc_multi, death_web and twisty_multi locs, read before the fight (spec 32939,32865,32937, grade C, tol exact)")
        -- the room's end
        local plain_end = complete_text ~= nil and string.gsub(string.gsub(complete_text, "<br>", " "), "<[^>]*>", "") or "none"
        local dur_minutes, dur_seconds = string.match(plain_end, "Duration:%s*(%d+):(%d+)")
        local dur_total = dur_minutes ~= nil and (tonumber(dur_minutes) * 60 + tonumber(dur_seconds)) or -1
        local expected_seconds = boss.death ~= nil and (boss.death - tick0) * 0.6 or -1
        t.check("room.complete_line", complete_text ~= nil and string.find(plain_end, "(Normal Mode) complete!", 1, true) ~= nil and dur_total > 0
            and math.abs(dur_total - expected_seconds) <= 6,
            "chat line '" .. plain_end .. "': duration " .. dur_total .. " seconds against " .. string.format("%.1f", expected_seconds) .. " seconds from the barrier mark (tick " .. tick0 .. ") to her npc_death row (tick " .. tostring(boss.death) .. ")")

        end
        -- ===== THE TECHNIQUE ROWS (from the tick log) =====
        local kills = { melee = 0, ranged = 0, magic = 0, fighting = 0, incoming = 0 }
        t.ticks(1)
        local _, deaths = t.ticklog.rows({ kind = "npc_death" })
        for index, r in ipairs(deaths) do
            if index % 60 == 0 then t.ticks(1) end
            if r.type >= 8342 and r.type <= 8353 then
                local style = ({ "melee", "ranged", "magic" })[(r.type - 8342) % 3 + 1]
                kills[style] = kills[style] + 1
                if r.type >= 8348 then kills.fighting = kills.fighting + 1 else kills.incoming = kills.incoming + 1 end
            end
        end
        local nulled, damaging = 0, 0
        for index, r in ipairs(support_hits) do
            if index % 60 == 0 then t.ticks(1) end
            if r.type ~= nil and r.type >= 8342 and r.type <= 8353 then
                if r.damage == 0 then nulled = nulled + 1 else damaging = damaging + 1 end
            end
        end
        t.check("tech.style_kills", kills.melee + kills.ranged + kills.magic > 10, "killed by the player with the weapon of the nylocas' style (whip, "
            .. "shortbow, Fire Strike): melee " .. kills.melee .. ", ranged " .. kills.ranged .. ", magic " .. kills.magic .. "; " .. damaging
            .. " hit_npc rows with damage and " .. nulled .. " at 0 on nylocas")
        t.check("tech.aggro_first", kills.fighting > 0, "kills of the fighting (aggro) forms " .. kills.fighting
            .. " against the incoming forms " .. kills.incoming .. ": the ones that swing at the player are met first")
        t.check("tech.food", (eaten + brews) > 0 and lowest > 0, "sharks eaten " .. eaten .. ", brew doses " .. brews .. ", lowest hitpoints " .. lowest .. ", eaten from 78 (84 with three copies swinging) and below, one at a time, the next only once the hitpoint read had risen")
        -- a 0 is a block only when the prayer the test held covered the style (prayer_log); a 0 with no prayer on its style is a miss (swing_miss_entry)
        t.check("tech.prayer", wave_block_count >= 20 and melee_block_count > melee_cover_landed, "protection prayer switched " .. prayer_switches .. " times to the style of the swinging majority (a big counts two, a melee copy only "
            .. "inside four tiles): " .. wave_block_count .. " hit_player rows from wave nylocas at 0 while the prayer held covered their style (blocks), against " .. wave_unprayed_zero
            .. " zeros with no prayer on their style (misses, not counted) and " .. wave_unprayed_landed .. " that landed with none on (the other two styles, and explosions); "
            .. "her melee form: " .. melee_block_count .. " blocked against " .. melee_cover_landed .. " landed on the switch ticks before the prayer followed")
        local spawned_waves, spawned_total = #wave_ticks, 0
        for _, c in ipairs(lives) do spawned_total = spawned_total + 1 end
        local walking_text = "none"
        if #walking_smalls > 0 then walking_text = table.concat(walking_smalls, ",") end
        local spawned_all = 0
        for _, r in ipairs(stream) do
            if r.kind == "npc_spawn" and r.type >= 8342 and r.type <= 8353 then spawned_all = spawned_all + 1 end
        end
        local hang_text = "none"
        if #hang_events > 0 then hang_text = table.concat(hang_events, "; ", 1, math.min(#hang_events, 5)) end
        end)()
        t.expect("party.barrier.end", t.party.barrier("end", 3000))
    end,
}
