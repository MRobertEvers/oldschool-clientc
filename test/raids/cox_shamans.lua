-- cox_shamans: the Lizardman shamans, solo, played by the v2 solver
-- (script/plugins/quest_driver/cox2_shamans.lua; plan
-- docs/minigames/cox/rooms/shamans_solver.md, spec shamans_spec.md) in the
-- We Do Raids minimum CoX kit (QD.COX.KIT: the Olm solver's) with the head
-- phase's dragon crossbow and ruby dragon bolts (e) on rapid, and exactly the
-- whole raid's kit (cox_raid_solo.lua): no anti-poison, which the wiki says
-- to bring ("bring a sanfew serum"); the solver drinks one if carried.
--
-- The raid: t.raid.cox_test_enter("shamans", n), n the run name's number
-- (--name shamans12 -> n = 12), so a sweep sees every variant and turn.
--
-- GREEN (shamans_solver.md PASS): both shamans dead, the exit spawns gone,
-- the raider alive, and ZERO damage from a shaman (Shayzien Specialist: the
-- acid, the jump, melee and any unprayed ranged; poison is not theirs), the
-- spawns' bursts at most 30 in all.
--
--   ./src/build_cox_shamans_opt/torirsserver --scriptrun test/raids/cox_shamans.lua --bots 1 \
--       --name shamans0 --ticks 3000 --session /tmp/sh --fixture test/raids/fixtures/fresh_lumbridge.ini

local function run(t)
    t.check("sh.ticklog", t.ticklog.start() == "ok", "")
    local names = t.party.names()
    local n = tonumber(string.match(tostring(names[1] or ""), "(%d+)")) or 0
    local er, at = t.raid.cox_test_enter("shamans", n)
    t.check("sh.enter", er == "ok", tostring(er) .. " " .. tostring(at and at.seed))
    if er ~= "ok" then
        t.finish(1)
        return
    end
    t.ticks(2)
    -- A DRAINED ARRIVAL (raid16 came to this room at 89 hp, 27 prayer, 7
    -- brews and its 3 super restores in storage): no restore in the pack,
    -- two brews down, the prayer burnt to ~25 before the gate
    t.player.drink("4dosepotionofsaradomin")
    t.ticks(3)
    t.player.drink({ "3dosepotionofsaradomin", "4dosepotionofsaradomin" })
    t.prayer.set("rigour", true)
    t.prayer.set("protectfrommissiles", true)
    t.ticks(170)
    t.prayer.set("rigour", false)
    t.prayer.set("protectfrommissiles", false)
    t.ticks(2)
    local hr, room = t.raid.cox_here()
    t.check("sh.room", hr == "ok", tostring(hr) .. " " .. tostring(room and room.name) .. " v" .. tostring(room and room.variant)
        .. " turn " .. tostring(room and room.turn) .. " seed " .. tostring(at.seed))
    local result, detail, S = t.raid.cox_shamans_solve({ max_ticks = 1500 })
    t.check("sh.solve", result == "ok", tostring(detail))
    -- the damage by source, from the tick log, by what each hit IS (the
    -- dealer label is not trusted: a hit dealt from a queue reads as mine):
    -- green 18+ the acid, green under it poison or a tendril re-pass; red
    -- from a spawn its burst; red of 10 or less on a tick I swung my ruby
    -- bolt (e)'s Blood Forfeit; any other red a shaman's (auto, jump, melee)
    local shaman_dmg, spawn_dmg, ruby, poison, by = 0, 0, 0, 0, {}
    local mine = {}
    local nr, nrows = t.ticklog.rows({ kind = "player_anim" })
    if nr == "ok" then
        for _, r in ipairs(nrows) do mine[r.tick] = true; mine[r.tick + 1] = true end
    end
    local total = 0
    local lr, rows = t.ticklog.rows({ kind = "hit_player" })
    if lr == "ok" then
        for _, r in ipairs(rows) do
            local ty, d, sp = r.npc_type, r.damage or 0, r.hitsplat
            if d > 0 and r.tick >= (S and S.sh and S.sh.arr_tick or 0) then
                total = total + d
                local k
                if sp == 68 and d >= 18 then k = "acid"
                elseif sp == 68 then k = "poison"
                elseif ty == 6768 then k = "spawn"
                elseif ty == 7575 then k = "exit-burst"
                elseif d <= 12 and mine[r.tick] then k = "ruby"
                else k = "shaman" end
                by[k] = (by[k] or 0) + d
                if k == "acid" or k == "shaman" then shaman_dmg = shaman_dmg + d
                elseif k == "spawn" then spawn_dmg = spawn_dmg + d
                elseif k == "ruby" then ruby = ruby + d
                else poison = poison + d end
            end
        end
    end
    local parts = {}
    for k, v in pairs(by) do parts[#parts + 1] = "splat" .. k .. "=" .. v end
    t.check("sh.no_shaman_damage", shaman_dmg == 0, "shaman damage " .. shaman_dmg .. " {" .. table.concat(parts, " ") .. "}")
    t.check("sh.spawn_chip", spawn_dmg <= 30, "spawn damage " .. spawn_dmg .. ", my ruby bolts " .. ruby)
    -- THE COST (round 4): what the room took, from the arrival
    t.check("sh.cost", true, "taken " .. total .. " (poison " .. poison .. ") " .. tostring(detail and string.match(detail, "arrival[^;]*; cost[^;]*")))
    local exit_open = S and S.sh and S.sh.seen_blocker and #S.sh.blockers == 0
    t.check("sh.exit_open", exit_open == true, "blockers seen " .. tostring(S and S.sh and S.sh.seen_blocker))
    t.finish((result == "ok" and shaman_dmg == 0 and spawn_dmg <= 30 and exit_open) and 0 or 1)
end

return {
    id = "cox_shamans",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
    setup = {
        "::clearinv",
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99", "::setlevel hitpoints 99",
        "::setlevel prayer 99", "::setlevel magic 99", "::setlevel ranged 99", "::setlevel agility 99",
        "::setvar varb3888_kr_quest ^kr_complete",
        "::setvar varb3909_kr_knightwaves_state 8",
        "::setvar varb5451_prayer_rigour_unlocked 1",
        "::setvar varb5452_prayer_augury_unlocked 1",
        "::setvar varb5453_prayer_preserve_unlocked 1",
        "::setvar varp7348_barbarian_vial_smash 1",
        "::give fris_kingly_helm 1", "::wield fris_kingly_helm",
        "::give barbassault_penance_fighter_torso 1", "::wield barbassault_penance_fighter_torso",
        "::give black_dragonhide_chaps 1", "::wield black_dragonhide_chaps",
        "::give hundred_gauntlets_level_10 1", "::wield hundred_gauntlets_level_10",
        "::give abyssal_tentacle 1", "::wield abyssal_tentacle",
        "::give dragon_parryingdagger 1", "::wield dragon_parryingdagger",
        "::give enchanted_onyx_amulet 1", "::wield enchanted_onyx_amulet",
        "::give tzhaar_cape_fire 1", "::wield tzhaar_cape_fire",
        "::give dragon_boots 1", "::wield dragon_boots",
        "::give nzone_berzerker_ring 1", "::wield nzone_berzerker_ring",
        "::~charge abyssal_tentacle 10000",
        "::give dragon_bolts_enchanted_ruby 300", "::wield dragon_bolts_enchanted_ruby",
        "::clearinv",
        "::blowpipe dragon_dart 2000 2000",
        "::give avas_assembler 1",
        "::give xbows_crossbow_dragon 1", "::give black_dragonhide_body 1",
        -- the relay's kit as it reaches a later room (cox_raid_solo_prep.lua:
        -- 7 Saradomin brews, 3 super restores, antidote++, no raid potion);
        -- the run drains it on the way in (DRAIN below)
        "::give 4dosepotionofsaradomin 7", "::give antidote++4 1",
        "::give 4dosestamina 1",
    },
    run = run,
}
