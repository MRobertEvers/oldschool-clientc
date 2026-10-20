-- cox_raid_solo_prep: cox_raid_solo (rebuilt from it, the same loop) with the
-- raid's own potions MADE in the raid by the supplies unit's prep
-- (script/plugins/quest_driver/cox2_supplies.lua, docs/minigames/cox/rooms/
-- supplies_solver.md sections 7-8). The kit brings no raid potion (none can
-- be brought in); it brings what a solo raider brings for the first floor,
-- Saradomin brews and super restores (the wiki's minimum setup), and every
-- solver drinks those after the raid's own (QD.COX.SUPPLY, cox2_raid.lua).
-- The supplies hooks over cox_raid_solo's loop:
--   * after a solver's room: what its boss left (seeds, secondaries, potions)
--     is picked up (cox_supplies_loot) before the way on;
--   * a scavenger room loots the secondaries the whole plan needs;
--   * a resource room preps (cox_prep_solve): gear switches parked in private
--     storage, the plan brewed, the gear taken back; the first floor's takes
--     the rake and carries it on, the last floor's drops the tools;
--   * the first floor's end and the second floor's start: the weeds raked for
--     the seeds the plan still needs;
--   * the last floor's end: what Olm does not use into storage, the stored
--     potions back (cox_olm_pack), then the hole to Olm.
-- Every room logs `supplies.<i>.<room>` with the counts carried (+ stored).
--
-- (cox_raid_solo's own header follows.)
-- cox_raid_solo: the Chambers of Xeric, solo, end to end (COX_FINISH_PLAN.md F5).
-- From Mount Quidamortem: the steps, the raid's start room, Start raid on the
-- raiding party panel, then every room the layout deals, each played by its
-- own solver (script/plugins/quest_driver/cox2_<room>.lua) from where the
-- room before left me (its Passage, or its way-out edge), the holes between
-- floors, the Great Olm (cox2_olm.lua) and the Ancient chest.
--
-- The layout is the server's draw for the run (its random stream is the run
-- name's): a sweep of names is a sweep of layouts.
--
-- GREEN: the chest looted, the player never dead.
--
--   ./src/build_cox_opt/torirsserver --scriptrun test/raids/cox_raid_solo_prep.lua --bots 1 \
--       --name raid1 --ticks 40000 --session /tmp/raid --fixture test/raids/fixtures/fresh_lumbridge.ini

local SOLVED = {
    tekton = true, guardians = true, vespula = true, icedemon = true, tightrope = true, crabs = true,
    thieving = true, shamans = true, mystics = true, vasa = true, vanguards = true, muttadiles = true,
}

-- THE PLAN: the Olm kit (cox_olm_solo.lua: aid 6, revit 4, enhance 4,
-- overload 2) and, past it, what the rooms on the way drink. Its secondaries
-- are what the scavengers loot.
-- (round 5, measured over raid1..7: the rooms drink restores and prayer,
-- not heals -- 1-6 restore doses a room against 0-16 heal doses, most rooms
-- 0 heal -- so the plan leans to revitalisation (buchu + stinkhorn) and
-- prayer enhance (buchu + cicely); the Olm kit's aid 6 stays)
local PLAN = { aid = 7, revit = 9, enhance = 7, overload = 2 }
-- What Olm does not use, left in storage at the last floor's end.
local OLM_LEAVE = { "dragon_shortsword", "dragon_pickaxe", "nzone_salve_amulet_e",
    "antidote++4", "antidote++3", "antidote++2", "antidote++1", "hammer", "iron_pickaxe", "iron_axe" }

-- The kit: the union of what the room tests carry (the Olm kit, We Do Raids'
-- minimum) WITHOUT the raid's own potions: a raider brings none in.
local function kit()
    local lines = { "::clearinv" }
    for _, s in ipairs({ "attack", "strength", "defence", "hitpoints", "prayer", "magic", "ranged", "agility",
                         "woodcutting", "firemaking", "mining", "thieving", "herblore", "farming", "fishing", "hunter" }) do
        lines[#lines + 1] = "::setlevel " .. s .. " 99"
    end
    for _, line in ipairs({
        "::setvar varb3888_kr_quest ^kr_complete", "::setvar varb3909_kr_knightwaves_state 8",
        "::setvar varb5451_prayer_rigour_unlocked 1", "::setvar varb5452_prayer_augury_unlocked 1",
        "::setvar varb5453_prayer_preserve_unlocked 1", "::setvar varp7348_barbarian_vial_smash 1",
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
        -- (Vasa's crystals take stab: the We Do Raids list's dragon sword;
        -- the Guardians take a pickaxe of the raider's own: the wiki's
        -- "players will need a pickaxe")
        "::give dragon_shortsword 1", "::give dragon_pickaxe 1",
        -- the first floor's supplies, brought in from outside (raid potions
        -- cannot be): the wiki's minimum setup (Chambers_of_Xeric/Strategies
        -- "Setups") carries seven Saradomin brews and two super restores and
        -- a sanfew serum; the sanfew's antipoison is the antidote below, its
        -- restore a third super restore. 24 in the pack: four free for the
        -- rake and the three secondaries' stacks
        "::give 4dosepotionofsaradomin 7", "::give 4dose2restore 3",
        -- the Shamans' poison: an antidote, as Synq and the wiki say to bring
        -- (the Shamans solver drinks it at the first poison); the Mystics'
        -- salve amulet (ei), on the We Do Raids minimum
        "::give antidote++4 1", "::give nzone_salve_amulet_e 1",
        "::give bh_rune_pouch 1",
        "::setvar varb29_rune_pouch_type_1 1", "::setvar varb1624_rune_pouch_quantity_1 500",
        "::setvar varb1622_rune_pouch_type_2 2", "::setvar varb1625_rune_pouch_quantity_2 500",
        "::setvar varb1623_rune_pouch_type_3 5", "::setvar varb1626_rune_pouch_quantity_3 500",
        "::tele 0_19_55_18_50",
    }) do lines[#lines + 1] = line end
    return lines
end

local function run(t)
    t.raid.cox_trace(true)
    t.check("raid.ticklog", t.ticklog.start() == "ok", "")
    t.ticks(2)
    -- in, and the raid begun
    local sr = t.player.click_loc("raids_entrance_steps", 1)
    local cr, cd = t.chat.play({ "mesbox:*", "options", "choose:Accept warning and proceed." })
    t.check("raid.enter", cr == "ok", tostring(cd))
    t.ticks(3)
    t.ui.tab(2)
    t.ticks(1)
    local _, start = t.ui.widget("raids_sidepanel:start")
    t.check("raid.start", t.ui.invoke(start, 1) == "ok", "")
    t.ticks(2)
    local done = {}
    local lr, lobby = t.raid.cox_leave_room(40)
    t.check("raid.lobby_out", lr == "ok", tostring(lr))
    if lobby ~= nil then done[lobby.ox .. "," .. lobby.oz .. "," .. lobby.level] = true end
    -- the rooms: each played from where the room before left me -- past its
    -- Passage, or on its way-out edge -- so the room to play is the one I
    -- stand in unless that one is done (or I am in a corridor), then the one
    -- ahead. A solver's room is entered by its solver (tendrils and all);
    -- an open room or a floor's end by the walker.
    local function key(r) return r.ox .. "," .. r.oz .. "," .. r.level end
    -- a floor room's weeds, raked for the seeds the plan still needs
    local function rake(i, name)
        local wr, wd = t.raid.cox_weeds_solve({ plan = PLAN, max_ticks = 150 })
        t.check("supplies." .. i .. "." .. name .. ".weeds", wr == "ok" or wr == "no_tool", tostring(wr) .. " " .. tostring(wd))
    end
    local last = lobby
    for i = 1, 20 do
        local _, room = t.raid.cox_here()
        local play = room
        if room == nil or done[key(room)] then
            local from = room or last
            play = from and t.raid.cox_room_ahead(from) or nil
        end
        local name = play and play.name or "?"
        t.ticklog.mark("raid room " .. i .. " " .. name)
        -- poisoned from the room before (the Shamans' acid, Vespula's
        -- soldiers): the antidote, before the next fight
        t.raid.cox_cure()
        -- and every prayer off: the next room lights what it needs
        t.raid.cox_prayers_off()
        -- the state the room is entered with (the supplies' measure)
        t.check("arrive." .. i .. "." .. name, true, t.raid.cox_supplies_text())
        t.check("raid.next." .. i, play ~= nil, room and room.name or "corridor")
        if play == nil then t.finish(1) return end
        local inside = room ~= nil and room.ox == play.ox and room.oz == play.oz and room.level == play.level
        if not SOLVED[name] and not inside then
            local er, after = t.raid.cox_enter_next(room or last, 80)
            t.check("raid.walk." .. i .. "." .. name, er == "ok", tostring(after and after.name))
            if er ~= "ok" then t.finish(1) return end
        end
        if name == "floor_end" then
            -- past its own Passage (a Passage before it can leave me in its
            -- cell, short of that)
            local fr = t.raid.cox_into_floor_end(play)
            t.check("raid.floor_end_in." .. i, fr == "ok", tostring(fr))
            if fr ~= "ok" then t.finish(1) return end
            local _, me = t.world.tile()
            if me.level == 0 then
                rake(i, name)
                local r = t.player.click_loc("raids_descentto2", 1)
                t.ticks(6)
                local down = select(2, t.world.tile()).level == 1
                t.check("raid.down." .. i, down, tostring(r))
                if not down then t.finish(1) return end
                last = nil
            else
                local pr, pd = t.raid.cox_olm_pack({ leave = OLM_LEAVE })
                t.check("supplies." .. i .. ".olm_pack", pr == "ok", tostring(pd))
                local r = t.player.click_loc("raids_bossentrance", 1)
                t.ticks(6)
                local olm = select(2, t.world.tile()).level == 2
                t.check("raid.olm_door", olm, tostring(r))
                if not olm then t.finish(1) return end
                break
            end
        elseif SOLVED[name] then
            local fn = t.raid["cox_" .. name .. "_solve"]
            t.check("raid.solver." .. name, fn ~= nil, "")
            if fn == nil then t.finish(1) return end
            local r, d = fn({ max_ticks = 4000 })
            t.check("raid.room." .. i .. "." .. name, r == "ok", tostring(d))
            if r ~= "ok" then t.finish(1) return end
            done[key(play)] = true
            last = play
            local lr0, ld0 = t.raid.cox_supplies_loot({ keep_free = 1 })
            t.check("supplies." .. i .. "." .. name, lr0 == "ok", tostring(ld0))
            t.check("raid.out." .. i, t.raid.cox_leave_room(60) == "ok", name)
        else
            -- the open rooms: the scavengers cleared (they follow a raider
            -- who walks past), the resource rooms prepped, by the supplies
            -- solver; then to the way-out edge
            local _, me = t.world.tile()
            local pr, pd = nil, nil
            if name == "scavenger_small" or name == "scavenger_large" then
                pr, pd = t.raid.cox_scavengers_solve({ max_ticks = 3000, make = PLAN })
            elseif name == "resource" or name == "resource_b" then
                -- the first floor's: the rake taken and carried on; the
                -- last floor's (the Olm kit's): the tools dropped after
                local final = me.level >= 1
                pr, pd = t.raid.cox_prep_solve({ max_ticks = 3000, make = PLAN, final = final,
                    take_tools = final and {} or { "rake" } })
            elseif name == "floor_start" and me.level == 1 then
                rake(i, name)
            end
            if pr ~= nil then
                t.check("raid.prep." .. i .. "." .. name, pr == "ok", tostring(pd))
                if pr ~= "ok" then t.finish(1) return end
            end
            t.check("supplies." .. i .. "." .. name, true, t.raid.cox_supplies_text())
            done[key(play)] = true
            last = play
            local lr2 = t.raid.cox_leave_room(80)
            t.check("raid.out." .. i, lr2 == "ok", name)
            if lr2 ~= "ok" then t.finish(1) return end
        end
    end
    -- Olm
    local hr, hd = t.player.click_loc("raids_olm_barrier", 1)
    local cr2 = t.chat.play({ "options", "choose:Step through the mystical barrier." })
    t.check("raid.olm_in", cr2 == "ok", "")
    local result, detail = t.raid.cox_olm_solve({ max_ticks = 2400 })
    t.check("raid.olm", result == "ok", tostring(detail) .. "; kit left " .. t.raid.cox_supplies_text())
    if result ~= "ok" then t.finish(1) return end
    local chest = nil
    for _ = 1, 8 do
        local _c
        _c, chest = t.world.loc_near("raids_reward_chest", 40)
        if chest ~= nil then break end
        t.ticks(1)
    end
    t.ticks(4)
    local lr2 = t.raid.tob_loc_op("raids_reward_chest", 1)
    t.check("raid.chest", t.ui.await_open("raids_rewards", 40) == "ok", tostring(lr2))
    local _, bank_all = t.ui.widget("raids_rewards:bank")
    t.check("raid.looted", t.ui.invoke(bank_all, 6) == "ok" and t.ui.await_close("raids_rewards", 10) == "ok", "")
    t.finish(0)
end

return {
    id = "cox_raid_solo_prep",
    fixture = "fresh_lumbridge.ini",
    max_frames = 480000, -- the gate's ceiling: 16,000 ticks at 30 frames a tick (raid3 whole raid: 5,321 ticks)
    setup = kit(),
    run = run,
}
