-- _play_nylocas: the Nylocas, Entry solo, played through the PLAY LIBRARY
-- (t.raid.play, raid_play.lua) and its room plan (raid_play_tob_nylocas.lua,
-- raid seam30 play_tob_nylocas; docs/minigames/raid_loop/PLAY_NOTES.md).
-- An underscore harness, not a kept room: the room's setup and entry as
-- test/raids/tob_nylocas.lua does them, the fight as ONE call, then the kept
-- test's room-complete and technique rows (tob_nylocas.lua :1892-1925)
-- copied unchanged, their inputs read from the tick log and the record.
return {
    id = "_play_nylocas",
    fixture = "fresh_lumbridge.ini",
    max_frames = 150000,
    setup = {
        "::clearinv",
        -- tob_nylocas.lua's own bring-alongs: 99s, one weapon per colour, Ancient
        -- Magicks and the runes of Ice Rush / Ice Burst (water, chaos, death)
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel ranged 99",
        "::setlevel magic 99",
        "::setlevel hitpoints 99",
        "::setlevel prayer 99",
        -- the Entry page's recommended Entry-mode equipment, worn (no backpack
        -- slot): "Void melee helm, Amulet of glory, Elite void top, Elite void
        -- robe, Void knight gloves, Dragon boots, Berserker ring (i)" (Entry page
        -- :78-91 {{Recommended equipment|style = Entry mode}}; its dragon
        -- defender is left out: the shortbow is two-handed, and its imbued god
        -- cape needs the Mage Arena).  The kept test wore nothing.  Given and worn
        -- first, while the backpack has room.
        "::give game_pest_melee_helm 1", "::wield game_pest_melee_helm",
        "::give amulet_of_glory 1", "::wield amulet_of_glory",
        "::give elite_void_knight_top 1", "::wield elite_void_knight_top",
        "::give elite_void_knight_robes 1", "::wield elite_void_knight_robes",
        "::give pest_void_knight_gloves 1", "::wield pest_void_knight_gloves",
        "::give dragon_boots 1", "::wield dragon_boots",
        "::give nzone_berzerker_ring 1", "::wield nzone_berzerker_ring",
        "::give abyssal_whip 1",
        "::give magic_shortbow 1",
        "::give rune_arrow 800",
        "::give lava_battlestaff 1",
        "::setvar varb4070_spellbook 1",
        "::give water_rune 2000",
        "::give chaos_rune 1000",
        "::give death_rune 1000",
        -- the library's supplies (raid_play.lua QD.RAID_PLAY_BREWS / RESTORES:
        -- the Theatre's own brew and restore): 28 slots with the above
        "::give br_4dosepotionofsaradomin 10",
        "::give br_4dose2restore 4",
        -- (raid seam31 play_tob_nylocas_green) the Bloat chest's bandages in
        -- place of the sharks: "After defeating the Pestilent Bloat, players
        -- will have access to the first supply chest. During Entry Mode this
        -- will always contain 10 bandages" and "Due to these bandages boosting
        -- the player's stats, combat potions and ranging potions are not
        -- necessary except for the first two bosses" (Entry page, sources/
        -- wiki_Theatre_of_Blood_Entry_Mode.wikitext :151, :33); the chest hands
        -- over as many as the backpack holds (tob_chest.rs2 tob_chest_bandages).
        -- Heals 20 like the shark it replaces, and boosts (tob_spectate.rs2
        -- [opheld1,tob_bandages]); the plan eats them (_play_nylocas_supplies).
        "::give tob_bandages 7",
    },
    run = function(t)
        t.check("spec.scope", true, "mode=entry party=1")
        t.ticklog.start()
        local enter_result, enter_detail = t.raid.enter("tob", "nylocas", { mode = "entry" })
        t.check("enter", enter_result == "ok", tostring(enter_detail))
        local state_result, state = t.raid.state()
        t.check("state", state_result == "ok" and state.room == "nylocas" and state.mode == "entry" and state.started == false,
            type(state) == "table" and tostring(state.line) or tostring(state))
        local tile_result, fight, fight_text = t.raid.start_tile()
        t.check("start_tile", tile_result == "ok", tostring(fight_text))
        -- the bow on, rapid, auto-retaliate off (tob_nylocas.lua :70-91), before
        -- the barrier: the plan swaps weapons itself and must never swing back
        -- with the wrong one ("a wrong-style swing nulls that nylocas for good")
        t.player.equip("rune_arrow")
        t.player.equip("magic_shortbow")
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
        local retaliate_before_result, retaliate_before = t.var.varp("varp172_option_nodef")
        local retaliate_widget_result, retaliate_widget = t.ui.widget("combat_interface:retaliate")
        local retaliate_press_result = "skipped"
        if retaliate_before == 0 then retaliate_press_result = t.ui.invoke(retaliate_widget, 1) end
        t.ticks(2)
        local retaliate_after_result, retaliate_after = t.var.varp("varp172_option_nodef")
        t.check("setup.retaliate_off", retaliate_after == 1, "combat tab auto-retaliate button: varp172_option_nodef read " .. tostring(retaliate_before)
            .. " before, press " .. tostring(retaliate_press_result) .. ", " .. tostring(retaliate_after) .. " after (1 is off)")
        t.player.walk_to(fight.x + 1, fight.z, 20)
        local click_result, click_detail = t.player.click_loc("tob_arena_barrier", 1)
        t.check("barrier.click", click_result == "ok", tostring(click_detail))
        local play_result, play_detail = t.chat.play({ "options", "choose:Yes, begin the fight." })
        t.check("barrier.confirm", play_result == "ok", tostring(play_detail))
        t.ticklog.mark("room start")
        -- the kept test's camera (tob_nylocas.lua :69): high and far, the whole room in frame
        t.drive.camera(0, 512, 1100)

        -- THE FIGHT: the library and the room's plan, nothing else
        local result, detail, rec = t.raid.play("tob_nylocas", { mode = "entry", max_ticks = 2000 })
        local ny = rec.ny or {}
        local res = {}
        for k, c in pairs(ny.results or {}) do res[#res + 1] = k .. " " .. c end
        table.sort(res)
        t.check("play.fight", result == "ok", tostring(detail) .. "; waves seen " .. tostring(ny.waves) .. ", presses " .. tostring(ny.presses)
            .. " (" .. table.concat(res, ", ") .. "), casts " .. tostring(ny.casts) .. " (bursts " .. tostring(ny.bursts) .. "), swaps "
            .. tostring(ny.swaps) .. ", blast escapes " .. tostring(ny.escapes) .. ", turn holds " .. tostring(ny.holds) .. ", flicker cancels "
            .. tostring(ny.flicker_cancels) .. ", her turns " .. tostring(#(ny.turns or {})))

        -- the room's end: the real chat line reaches the raider a few ticks after
        -- her death (tob_nylocas.lua :816-826)
        local complete_text = nil
        if result == "ok" then
            t.ticks(8)
            t.shot("fight.wave_complete_line")
            local _, recent = t.msg.last(80)
            for _, line in ipairs(recent) do
                if string.find(line.text, "complete!", 1, true) ~= nil then complete_text = line.text break end
            end
        end
        local leave_result, leave_detail = t.raid.leave()
        t.check("fight.left", leave_result == "ok", tostring(leave_detail))
        t.ticks(1)

        -- THE TICK LOG: the reads tob_nylocas.lua's ANALYSIS makes for these rows
        ;(function()
        local tick0 = nil
        local _, mark_rows = t.ticklog.rows({ kind = "mark" })
        for i = 1, #mark_rows do
            if mark_rows[i].label == "room start" then tick0 = mark_rows[i].tick end
        end
        tick0 = tick0 or rec.start_tick
        t.ticks(1)
        local boss = { slot = nil, death = nil }
        local _, deaths = t.ticklog.rows({ kind = "npc_death" })
        for _, r in ipairs(deaths) do
            if r.type >= 10786 and r.type <= 10789 and boss.death == nil then boss.death, boss.slot = r.tick, r.slot end
        end
        t.ticks(1)
        local _, support_hits = t.ticklog.rows({ kind = "hit_npc" })
        t.ticks(1)
        local _, hits = t.ticklog.rows({ kind = "hit_player" })
        t.ticks(1)
        local heals = 0
        if boss.slot ~= nil then
            local _, heal_rows = t.ticklog.rows({ kind = "npc_heal", slot = boss.slot })
            heals = #heal_rows
        end
        t.ticks(1)
        -- the prayer the player READ lit, per tick (the library's record): one
        -- entry per change of the lit protection prayer (the kept test's
        -- prayer_log: {tick, style} per accepted switch)
        local prayer_log, prayer_switches = {}, 0
        local style_of_prayer = { protectfrommelee = "melee", protectfrommagic = "magic", protectfrommissiles = "ranged" }
        local last_style = nil
        for tk = rec.start_tick, (rec.end_tick or rec.start_tick) do
            local lit = rec.prayer_at[tk]
            if lit ~= nil then
                local on = nil
                for name, style in pairs(style_of_prayer) do
                    if lit[name] == true then on = style end
                end
                if on ~= nil and on ~= last_style then
                    prayer_log[#prayer_log + 1] = { tk, on }
                    prayer_switches = prayer_switches + 1
                    last_style = on
                end
            end
        end
        local eaten, brews, lowest = 0, 0, 99
        local bandages = 0
        for _, e in ipairs(rec.eats) do
            eaten = eaten + 1
            if e.item == "tob_bandages" then bandages = bandages + 1 end
        end
        for _, d in ipairs(rec.drinks) do
            if string.find(d.item, "saradomin", 1, true) ~= nil then brews = brews + 1 end
        end
        for _, hp in pairs(rec.hp_at) do
            if hp < lowest then lowest = hp end
        end
        -- tob_nylocas.lua :1519-1566, unchanged: a 0 is a block only when the
        -- prayer held covered the style; her melee form's blocks and landings
        local wave_unprayed, wave_unprayed_zero, wave_unprayed_landed = 0, 0, 0
        local wave_block_count, wave_cover_landed, melee_block_count, melee_cover_landed = 0, 0, 0, 0
        for index, h in ipairs(hits) do
            if index % 100 == 0 then t.ticks(1) end
            local style_of_hit = nil
            if h.npc_type ~= nil and h.npc_type >= 10780 and h.npc_type <= 10785 then
                style_of_hit = ({ "melee", "ranged", "magic" })[(h.npc_type - 10774) % 3 + 1]
            elseif h.npc_type == 10787 then
                style_of_hit = "melee"
            elseif h.npc_type == 10788 then
                style_of_hit = "magic"
            elseif h.npc_type == 10789 then
                style_of_hit = "ranged"
            end
            if style_of_hit ~= nil then
                local held, last_before = {}, nil
                for _, entry in ipairs(prayer_log) do
                    if entry[1] < h.tick - 6 then last_before = entry[2]
                    elseif entry[1] <= h.tick + 2 then held[entry[2]] = true end
                end
                if last_before ~= nil then held[last_before] = true end
                local covered, only_this = held[style_of_hit] == true, true
                for style_name in pairs(held) do if style_name ~= style_of_hit then only_this = false end end
                if h.npc_type <= 10785 then
                    if covered and only_this and h.damage == 0 then wave_block_count = wave_block_count + 1
                    elseif covered and only_this and h.damage > 0 then wave_cover_landed = wave_cover_landed + 1 end
                    if not covered then
                        wave_unprayed = wave_unprayed + 1
                        if h.damage == 0 then wave_unprayed_zero = wave_unprayed_zero + 1 else wave_unprayed_landed = wave_unprayed_landed + 1 end
                    end
                elseif h.npc_type == 10787 then
                    if covered and only_this and h.damage == 0 then melee_block_count = melee_block_count + 1
                    elseif h.damage > 0 and not covered then melee_cover_landed = melee_cover_landed + 1 end
                end
            end
        end
        -- ===== COPIED UNCHANGED from test/raids/tob_nylocas.lua :1887-1894 (the room's end) =====
        -- the room's end
        local plain_end = complete_text ~= nil and string.gsub(string.gsub(complete_text, "<br>", " "), "<[^>]*>", "") or "none"
        local dur_minutes, dur_seconds = string.match(plain_end, "Duration:%s*(%d+):(%d+)")
        local dur_total = dur_minutes ~= nil and (tonumber(dur_minutes) * 60 + tonumber(dur_seconds)) or -1
        local expected_seconds = boss.death ~= nil and (boss.death - tick0) * 0.6 or -1
        t.check("room.complete_line", complete_text ~= nil and string.find(plain_end, "(Entry Mode) complete!", 1, true) ~= nil and dur_total > 0
            and math.abs(dur_total - expected_seconds) <= 6,
            "chat line '" .. plain_end .. "': duration " .. dur_total .. " seconds against " .. string.format("%.1f", expected_seconds) .. " seconds from the barrier mark (tick " .. tick0 .. ") to her npc_death row (tick " .. tostring(boss.death) .. ")")
        -- ===== THE TECHNIQUE ROWS (from the tick log) =====
        local kills = { melee = 0, ranged = 0, magic = 0, fighting = 0, incoming = 0 }
        local _, deaths = t.ticklog.rows({ kind = "npc_death" })
        for index, r in ipairs(deaths) do
            if index % 60 == 0 then t.ticks(1) end
            if r.type >= 10774 and r.type <= 10785 then
                local style = ({ "melee", "ranged", "magic" })[(r.type - 10774) % 3 + 1]
                kills[style] = kills[style] + 1
                if r.type >= 10780 then kills.fighting = kills.fighting + 1 else kills.incoming = kills.incoming + 1 end
            end
        end
        local nulled, damaging = 0, 0
        for index, r in ipairs(support_hits) do
            if index % 60 == 0 then t.ticks(1) end
            if r.type ~= nil and r.type >= 10774 and r.type <= 10785 then
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
        -- ===== the plan's own measurements (PLAY_NOTES.md "Nylocas, Entry solo") =====
        -- the reflect: every wrong-colour hit on her heals her (DMG :278-300);
        -- the play never sends one, so her npc_heal rows are none
        t.check("play.no_reflect", boss.slot ~= nil and heals == 0, "npc_heal rows on her slot " .. tostring(boss.slot) .. ": " .. heals
            .. "; presses on her " .. #(ny.vas_presses or {}) .. ", turns " .. #(ny.turns or {}) .. ", turn holds " .. tostring(ny.holds))
        local taken, by_kind = 0, {}
        for _, h in ipairs(hits) do
            if h.damage > 0 then
                taken = taken + h.damage
                local key = tostring(h.npc_type)
                by_kind[key] = (by_kind[key] or 0) + h.damage
            end
        end
        local kinds = {}
        for k, d in pairs(by_kind) do kinds[#kinds + 1] = k .. ":" .. d end
        table.sort(kinds)
        t.check("note.damage", true, "taken " .. taken .. " (by npc type " .. table.concat(kinds, " ") .. "), food eaten " .. eaten .. " (bandages " .. bandages .. ", the interlude one at tick " .. tostring(ny.boosted) .. "), brew doses " .. brews
            .. ", lowest " .. lowest .. ", room " .. tostring(boss.death and (boss.death - tick0) or "none") .. " ticks from the mark to her death")
        end)()
    end,
}
