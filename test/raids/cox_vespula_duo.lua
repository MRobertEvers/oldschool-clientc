-- cox_vespula_duo: Vespula, a party of two (--bots 2), played by the v2
-- solver's INTENDED solve (script/plugins/quest_driver/cox2_vespula.lua,
-- opts.method = "intended"; Synq 1:36:20; the solo test, cox_vespula.lua,
-- plays Synq's solo Redemption method): her shot down to 20% until she lands,
-- the portal shot while she is grounded. Seat 1 keeps her enraged between
-- (a portal hit in each renewal window: enraged she tags no grub); seat 2
-- feeds every grub she tagged back to full from the roots. Each seat in the
-- solo test's kit less the magic set (see setup).
--
-- GREEN: the portal dead and the boils burst (the exit open), every raider
-- alive, no enraged-sting hit, no soldier hatched -- on every seat.
--
--   ./src/build_cox_vespula_opt/torirsserver --scriptrun test/raids/cox_vespula_duo.lua --bots 2 \
--       --name vespula0 --ticks 3000 --session /tmp/vsp --fixture test/raids/fixtures/fresh_lumbridge.ini
--   (the run name's number picks the n-th raid holding Vespula: variant and turn)

local function run(t)
    local role, size = t.party.role(), t.party.size()
    -- (the tick log is the host's: a live party's member clients have no
    -- embedded server, and its start answers "unsupported" there --
    -- cox_olm_party.lua does the same)
    if role == 1 then
        t.check("vespula.ticklog", t.ticklog.start() == "ok", "")
    end
    -- (the live lane's user is the run name with "_p1" on it)
    local user = string.gsub(t.session._user() or "", "_p%d+$", "")
    local n = tonumber(string.match(user, "(%d+)$") or "0") or 0
    -- the leader builds the raid, the others join it on the leader's tile
    -- (::coxtestjoin), and all start together
    local er, at = "ok", nil
    if role == 1 then
        er, at = t.raid.cox_test_enter("vespula", n)
    end
    t.check("vespula.enter", er == "ok", tostring(er) .. " " .. tostring(at and at.seed))
    if er ~= "ok" then t.finish(1) return end
    if size > 1 then
        local br, bd = t.party.barrier("vespula_built", 600)
        t.check("vespula.built", br == "ok", tostring(bd))
        if br ~= "ok" then t.finish(1) return end
        if role ~= 1 then
            local jr, jd = t.raid.cox_test_join()
            t.check("vespula.join", jr == "ok", tostring(jd))
            if jr ~= "ok" then t.finish(1) return end
        end
        local ir, id = t.party.barrier("vespula_in", 300)
        t.check("vespula.in", ir == "ok", tostring(id))
        if ir ~= "ok" then t.finish(1) return end
    end
    t.ticks(2)
    t.ticklog.mark("room start")
    local result, detail, S = t.raid.cox_vespula_solve({ max_ticks = 1500, method = "intended",
                                                          role = role == 2 and "feed" or "enrage" })
    t.check("vespula.solve", result == "ok", tostring(detail))
    local v = S and S.v or {}
    t.check("vespula.exit_open", v.exit_open == true, "boils burst: " .. tostring(v.exit_open))
    t.check("vespula.alive", result ~= "died", tostring(result))
    t.check("vespula.no_sting", (v.sting_hits or 0) == 0, "enraged-sting hits " .. tostring(v.sting_hits))
    t.check("vespula.no_soldier", (v.soldiers_n or 0) == 0, "soldiers hatched " .. tostring(v.soldiers_n))
    t.check("vespula.landings", true, "landings " .. tostring(v.landings or 0) .. ", tags " .. tostring(v.tags))
    t.finish((result == "ok" and (v.sting_hits or 0) == 0 and (v.soldiers_n or 0) == 0) and 0 or 1)
end

return {
    id = "cox_vespula_duo",
    fixture = "fresh_lumbridge.ini",
    party = 2,
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
        -- the whole raid's kit (test/raids/cox_raid_solo.lua): the Olm kit and
        -- the raid's (+) potions
        "::blowpipe dragon_dart 2000 2000",
        -- (less the magic set -- trident, occult, imbued cape, mystic robes --
        -- which this room's solver never wears: their five slots carry the
        -- feed seat's blossoms, four grubs drain 4 hp a tick when she tags
        -- them all before the first enrage, duo seed 10)
        "::give avas_assembler 1",
        "::give xbows_crossbow_dragon 1", "::give black_dragonhide_body 1",
        -- the prep relay's kit (test/raids/cox_raid_solo_prep.lua): no raid
        -- potion (none can be brought in) -- the wiki's minimum supplies,
        -- seven Saradomin brews and three super restores, the antidote, the
        -- salve; the swords and pickaxe other rooms need
        "::give dragon_shortsword 1", "::give dragon_pickaxe 1",
        "::give 4dosepotionofsaradomin 7", "::give 4dose2restore 3",
        "::give antidote++4 1", "::give nzone_salve_amulet_e 1",
        "::give bh_rune_pouch 1",
    },
    run = run,
}
