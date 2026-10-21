-- _thieving_tells: the thieving room sends what Jagex's server sends.
-- RSProx 3389 (solo, Challenge Mode), the only recording of the room, in
-- instance tiles (docs/rsprox-analysis/cox/thieving.md,
-- tools/cox/rsprox_thieving.py):
--   a search (RH-C1, C2, C4): human_openchest (536) the tick before the
--     result; on the result chest_open (52) round the chest and the chest
--     hatched (29745); closed (29742) again 11-15 ticks later;
--   a deposit (RH-T1): human_pickuptable (832) with dream_squish_illusive
--     (3637) round the raider; an empty trough shows full the tick after;
--   the scavenger (RH-S1, S3): a meal (7496) every 3 ticks; with E its last,
--     the reward (stinkhorn, cicely, endarkened juice) under it on E+1, its
--     yawn ("Yawwwwwn!", 7497, howl1 1734) on E+7, asleep (7603) on E+14,
--     "Zzz" (277) with troll_snore (869) on E+21.
-- Played by the room solver (cox2_thieving.lua) in the room test's kit.
local function kit()
    local lines = { "::clearinv" }
    for _, s in ipairs({ "attack", "strength", "defence", "hitpoints", "prayer", "magic", "ranged", "agility",
                         "woodcutting", "firemaking", "mining", "thieving", "herblore", "farming", "fishing", "hunter" }) do
        lines[#lines + 1] = "::setlevel " .. s .. " 99"
    end
    for _, line in ipairs({
        "::setvar varb3888_kr_quest ^kr_complete", "::setvar varb3909_kr_knightwaves_state 8",
        "::setvar varb5451_prayer_rigour_unlocked 1", "::setvar varb5452_prayer_augury_unlocked 1",
        "::setvar varb5453_prayer_preserve_unlocked 1", "::setvar varp8023_barbarian_vial_smash 1",
    }) do lines[#lines + 1] = line end
    for _, obj in ipairs({ "fris_kingly_helm", "barbassault_penance_fighter_torso", "black_dragonhide_chaps",
            "hundred_gauntlets_level_10", "abyssal_tentacle", "dragon_parryingdagger", "enchanted_onyx_amulet",
            "tzhaar_cape_fire", "dragon_boots", "nzone_berzerker_ring" }) do
        lines[#lines + 1] = "::give " .. obj .. " 1"
        lines[#lines + 1] = "::wield " .. obj
    end
    for _, line in ipairs({
        "::~charge abyssal_tentacle 10000",
        "::give dragon_bolts_enchanted_ruby 300", "::wield dragon_bolts_enchanted_ruby",
        "::clearinv",
        "::blowpipe dragon_dart 2000 2000",
        "::give avas_assembler 1", "::give toxic_tots_charged 1", "::~charge toxic_tots_charged 2500",
        "::give occult_necklace 1", "::give ma2_saradomin_cape 1",
        "::give xbows_crossbow_dragon 1", "::give black_dragonhide_body 1",
        "::give mystic_robe_top 1", "::give mystic_robe_bottom 1",
        -- (the raid's kit, cox_raid_solo: 27 in the pack, and its one free
        -- slot the scavengers' juice by the time a puzzle room comes)
        "::give dragon_shortsword 1", "::give dragon_pickaxe 1",
        "::give raids_vial_overload_strong_4 2", "::give raids_vial_xericaid_strong_4 5",
        "::give raids_vial_revitalisation_strong_4 3", "::give raids_vial_prayer_strong_4 3",
        "::give antidote++4 1", "::give nzone_salve_amulet_e 1",
        "::give bh_rune_pouch 1",
        "::give raids_endarkened_juice 14",
        "::setvar varb29_rune_pouch_type_1 1", "::setvar varb1624_rune_pouch_quantity_1 500",
        "::setvar varb1622_rune_pouch_type_2 2", "::setvar varb1625_rune_pouch_quantity_2 500",
        "::setvar varb1623_rune_pouch_type_3 5", "::setvar varb1626_rune_pouch_quantity_3 500",
    }) do lines[#lines + 1] = line end
    return lines
end


local function by_tick(rows)
    local out = {}
    for _, r in ipairs(rows) do
        out[r.tick] = out[r.tick] or {}
        table.insert(out[r.tick], r)
    end
    return out
end

return {
    id = "_thieving_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
    setup = kit(),
    run = function(t)
        t.ticklog.start()
        local er, ed = t.raid.cox_test_enter_long("thieving", t.raid.cox_run_n(), 80)
        t.check("tells.enter", er == "ok", tostring(er) .. " " .. tostring(ed and ed.seed))
        if er ~= "ok" then t.finish(1) return end
        t.ticks(2)
        local wr = t.raid.cox_walk_into("thieving", ed.cx, ed.cz, 60)
        if wr ~= "ok" then t.finish(1) return end
        local result, detail = t.raid.cox_thieving_solve({ max_ticks = 900 })
        t.check("tells.solve", result == "ok", tostring(detail))
        if result ~= "ok" then t.finish(1) return end
        t.ticks(24)
        local rows = {}
        for _, k in ipairs({ "loc_set", "player_anim", "sound", "npc_anim", "npc_say", "npc_retype", "npc_spotanim", "obj_add" }) do
            local r, got = t.ticklog.rows({ kind = k })
            t.check("tells.rows." .. k, r == "ok", tostring(r))
            rows[k] = r == "ok" and got or {}
            t.ticks(1)
        end
        local snd, panim = by_tick(rows.sound), by_tick(rows.player_anim)
        local bad = {}
        local function has(list, pred)
            for _, x in ipairs(list or {}) do if pred(x) then return true end end
            return false
        end
        -- SEARCHES
        local searches, closes = 0, {}
        for _, l in ipairs(rows.loc_set) do
            if l.loc == 29745 then
                searches = searches + 1
                local R = l.tick
                if not has(panim[R - 1], function(a) return a.seq == 536 end) then bad[#bad + 1] = string.format("t%d: no human_openchest on R-1", R) end
                if has(panim[R], function(a) return a.seq == 536 end) then bad[#bad + 1] = string.format("t%d: human_openchest on the result", R) end
                if not has(snd[R], function(s) return s.sound == 52 and s.x == l.x and s.z == l.z and s.radius == 5 end) then
                    bad[#bad + 1] = string.format("t%d: no chest_open round the chest", R)
                end
                for _, c in ipairs(rows.loc_set) do
                    if c.loc == 29742 and c.coord == l.coord and c.tick > R and closes[R] == nil then closes[R] = c.tick - R end
                end
                local d = closes[R]
                if d ~= nil and (d < 11 or d > 15) then bad[#bad + 1] = string.format("t%d: closed %d later", R, d) end
            end
        end
        -- DEPOSITS
        local deposits = 0
        for _, a in ipairs(rows.player_anim) do
            if a.seq == 832 then
                deposits = deposits + 1
                if not has(snd[a.tick], function(s) return s.sound == 3637 and s.radius == 5 end) then
                    bad[#bad + 1] = string.format("t%d: a deposit without dream_squish_illusive", a.tick)
                end
            end
        end
        -- THE SCAVENGER
        local eats = {}
        for _, a in ipairs(rows.npc_anim) do if a.seq == 7496 then eats[#eats + 1] = a.tick end end
        for i = 2, #eats do
            if eats[i] - eats[i - 1] < 3 or (eats[i] - eats[i - 1]) % 3 ~= 0 then
                bad[#bad + 1] = string.format("meals t%d and t%d", eats[i - 1], eats[i])
            end
        end
        local E = eats[#eats]
        if E == nil then
            bad[#bad + 1] = "no meal"
        else
            if not has(rows.obj_add, function(o) return o.tick == E + 1 and o.obj == 20910 end) then bad[#bad + 1] = "no reward on E+1" end
            if not has(rows.npc_say, function(r) return r.tick == E + 7 and r.text == "Yawwwwwn!" end) then bad[#bad + 1] = "no yawn on E+7" end
            if not has(rows.npc_anim, function(a) return a.tick == E + 7 and a.seq == 7497 end) then bad[#bad + 1] = "no tired seq on E+7" end
            if not has(rows.npc_retype, function(r) return r.tick == E + 14 and r.to_type == 7603 end) then bad[#bad + 1] = "not asleep on E+14" end
            if not has(rows.npc_spotanim, function(g) return g.tick == E + 21 and g.spotanim == 277 end) then bad[#bad + 1] = "no Zzz on E+21" end
        end
        t.check("tells.counts", searches > 0 and deposits > 0 and #eats > 0,
            string.format("%d searches, %d deposits, %d meals", searches, deposits, #eats))
        t.check("tells.recorded", #bad == 0, #bad == 0 and "every tell as recorded" or table.concat(bad, "; "))
        t.finish((#bad == 0 and searches > 0 and deposits > 0) and 0 or 1)
    end,
}
