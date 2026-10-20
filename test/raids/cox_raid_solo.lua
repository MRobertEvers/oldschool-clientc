-- cox_raid_solo: the Chambers of Xeric, solo, end to end, on every layout
-- (RAID_SOLO_RELAY.md; COX_FINISH_PLAN.md F5). From Mount Quidamortem: the
-- steps, the raid's start room, Start raid on the raiding party panel, then
-- every room the layout deals, each played by its own solver
-- (script/plugins/quest_driver/cox2_<room>.lua) from where the room before
-- left me, the holes between floors, the Great Olm (cox2_olm.lua) and the
-- Ancient chest.
--
-- THE OLM PREP is played, not given: no raid potion is brought in. Seeds come
-- from the Guardians, Shamans and Mystics and the floor ends' weeds;
-- secondaries and planks from the scavengers; the resource rooms farm both
-- patches and brew (cox2_supplies.lua); the storage unit is BUILT (the kit's
-- dragon warhammer, the scavengers' planks) and holds the gear while the prep brews and
-- the potions between preps; the last floor's end restocks the Olm kit.
-- Floor 1 is fought on the outside supplies the wiki's Minimum setup brings
-- (brews, restores, a sanfew, a super combat and a ranging potion to pre-pot).
-- NOTHING brought in is ever dropped (owner, 2026-10-19): every CoX drop
-- asserts QD.COX.is_raid_item.
--
-- The layout is the server's draw for the run (its random stream is the run
-- name's): a sweep of names is a sweep of layouts.
--
-- GREEN: the chest looted, the player never dead.
--
--   ./src/build_cox_relay_opt/torirsserver --scriptrun test/raids/cox_raid_solo.lua --bots 1 \
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
-- (round 4 tried aid 12 / revit 6 / enhance 4: raid16 met Olm with 50 heal
-- doses and no restore and lost, raid15 lost earlier -- Olm spends both;
-- the clears r15/r16 spent 29-30 heal and 16 restore doses)
local PLAN = { aid = 7, revit = 9, enhance = 7, overload = 2 }
-- What Olm does not use, left in storage at the last floor's end.
local OLM_LEAVE = { "dragon_shortsword", "rune_pickaxe", "nzone_salve_amulet_e", "dragon_warhammer", "hammer",
    "sanfew_salve_4_dose", "sanfew_salve_3_dose", "sanfew_salve_2_dose", "sanfew_salve_1_dose",
    "4dose2combat", "3dose2combat", "2dose2combat", "1dose2combat",
    "4doserangerspotion", "3doserangerspotion", "2doserangerspotion", "1doserangerspotion",
    "iron_pickaxe", "iron_axe", "lockpick", "tinderbox", "fishing_rod", "butterfly_net" }

-- THE KIT: the wiki's Minimum setup, slot for slot (Chambers_of_Xeric/
-- Strategies "Setups", the budget column; RAID_SOLO_RELAY.md section 2), the
-- mirror of QD.COX.SOLO_KIT (cox2_raid.lua) -- keep the two the same. One
-- deviation: a dragon warhammer for the lockpick (the raider builds the
-- storage unit with it, and it smashes the crabs, wielded).
-- No raid potion (none can be brought in); no max set (owner, 2026-10-19).
local function kit()
    local lines = { "::clearinv" }
    for _, s in ipairs({ "attack", "strength", "defence", "hitpoints", "prayer", "magic", "ranged", "agility",
                         "woodcutting", "firemaking", "mining", "thieving", "herblore", "farming", "fishing", "hunter",
                         "construction", "cooking" }) do
        lines[#lines + 1] = "::setlevel " .. s .. " 99"
    end
    for _, line in ipairs({
        "::setvar varb3888_kr_quest ^kr_complete", "::setvar varb3909_kr_knightwaves_state 8",
        "::setvar varb5451_prayer_rigour_unlocked 1", "::setvar varb5452_prayer_augury_unlocked 1",
        "::setvar varb5453_prayer_preserve_unlocked 1", "::setvar varp7348_barbarian_vial_smash 1",
    }) do lines[#lines + 1] = line end
    for _, obj in ipairs({ "fris_kingly_helm", "barbassault_penance_fighter_torso", "black_dragonhide_chaps",
            "hundred_gauntlets_level_10", "abyssal_tentacle", "dragon_parryingdagger", "enchanted_onyx_amulet",
            "tzhaar_cape_fire", "dragon_boots", "lightbearer" }) do
        lines[#lines + 1] = "::give " .. obj .. " 1"
        lines[#lines + 1] = "::wield " .. obj
    end
    for _, line in ipairs({
        "::~charge abyssal_tentacle 10000",
        "::give dragon_bolts_enchanted_ruby 300", "::wield dragon_bolts_enchanted_ruby",
        "::clearinv",
        "::blowpipe dragon_dart 2000 2000",
    }) do lines[#lines + 1] = line end
    -- the 28: the switches, the tools, the outside supplies
    for _, obj in ipairs({ "xbows_crossbow_dragon", "anma_50_reward", "toxic_tots_charged", "occult_necklace",
            "black_dragonhide_body", "mystic_robe_top", "mystic_robe_bottom", "bgs", "anglerfish",
            "bh_rune_pouch", "ma2_saradomin_cape", "rune_pickaxe", "nzone_salve_amulet_e", "dragon_shortsword",
            "dragon_warhammer", "sanfew_salve_4_dose" }) do
        lines[#lines + 1] = "::give " .. obj .. " 1"
    end
    for _, line in ipairs({
        "::~charge toxic_tots_charged 2500",
        -- (a stamina for Olm in one brew's slot: the owner, and the wiki's
        -- "one stamina potion is highly recommended for Olm" for a solo;
        -- stored at the first unit, back in the Olm pack)
        -- (no super combat / ranging pre-pot: the owner chose the two slots
        -- for the scavengers' secondaries, 2026-10-19)
        "::give 4dosepotionofsaradomin 6", "::give 4dose2restore 2", "::give 4dosestamina 1",
        -- the spares in the bank, packed per layout from the start room's
        -- chest (Synq 0:08:21: "for Vespula ... an extra one to two
        -- restores to use the redemption method")
        "::bankgive 4dose2restore 2",
        "::setvar varb29_rune_pouch_type_1 1", "::setvar varb1624_rune_pouch_quantity_1 500",
        "::setvar varb1622_rune_pouch_type_2 2", "::setvar varb1625_rune_pouch_quantity_2 500",
        "::setvar varb1623_rune_pouch_type_3 5", "::setvar varb1626_rune_pouch_quantity_3 500",
        "::tele 0_19_55_18_50",
    }) do lines[#lines + 1] = line end
    return lines
end

local function run(t)
    t.raid.cox_trace(true)
    -- what the raider brought in (the tools a room also hands out: only the
    -- copies over this may ever go down)
    t.raid.cox_snapshot_brought()
    t.check("raid.ticklog", t.ticklog.start() == "ok", "")
    t.ticks(2)
    -- in, and the raid begun
    -- in, and SCOUTED (the owner, 2026-10-19: Vespula needs a prayer enhance;
    -- a raid that cannot give one before her is left by the start room's
    -- exit steps and made again -- a new layout each entry, nothing spent)
    local sr = t.player.click_loc("raids_entrance_steps", 1)
    local cr, cd = t.chat.play({ "mesbox:*", "options", "choose:Accept warning and proceed." })
    t.ticks(3)
    local seen_path = ""
    -- (a completable raid is about 1 in 80: the five 3-combat patterns on
    -- ring 1's Tekton-Muttadiles-Guardians run; a reload is ~20 ticks)
    for scout = 1, 1500 do
        local verdict, seen = t.raid.cox_scout()
        seen_path = tostring(seen)
        t.check("raid.scout." .. scout, true, verdict .. ": " .. seen_path)
        if verdict == "ok" then break end
        -- Reload: the start room's steps, the leader's second op
        local xr, xd = t.player.click_loc("raids_exit_steps_reload", 2)
        t.ticks(4)
        t.check("raid.reload." .. scout, xr == "ok", tostring(xr) .. " " .. tostring(xd))
    end
    t.check("raid.enter", cr == "ok", tostring(cd))
    -- PACKED FOR THE LAYOUT, from the start room's bank chest while it is
    -- open (it shuts at Start raid): Vespula seen -> the two spare restores
    if string.find(seen_path, "vespula", 1, true) then
        local br = t.bank.open("raids_bank_chest_lobby_working", 1)
        local wr, wd = t.bank.withdraw("4dose2restore", 2)
        t.bank.close()
        t.check("raid.pack.vespula", wr == "ok", tostring(br) .. " " .. tostring(wr) .. " " .. tostring(wd))
        -- and kept for her: the rooms before leave the last 8 super
        -- restore doses alone (QD.COX.reserved)
        t.raid.cox_reserve("vespula", "#2restore", 8)
    end
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
        t.check("supplies." .. i .. "." .. name .. ".weeds", wr == "ok" or wr == "no_tool" or wr == "no_room", tostring(wr) .. " " .. tostring(wd))
    end
    local last = lobby
    -- THE FLOOR'S PATH, for a trip back to its resource room to farm and
    -- brew (the owner, 2026-10-19: "you can farm and grow and make potions.
    -- Do that when possible"): the rooms in the order walked, the resource
    -- room among them, and the trips taken
    local path, resource, trips = { lobby }, nil, 0
    local function farm_trip(i, name)
        if resource == nil or trips >= 2 or #path < 2 then return end
        local cur = path[#path]
        if cur.ox == resource.ox and cur.oz == resource.oz and cur.level == resource.level then return end
        local due, n = t.raid.cox_farm_due(PLAN, resource)
        if not due then return end
        local back = {}
        for k = #path, 1, -1 do
            back[#back + 1] = path[k]
            if path[k] == resource then break end
        end
        if back[#back] ~= resource then return end
        trips = trips + 1
        local br, bd = t.raid.cox_walk_path(back, true)
        t.check("farm." .. i .. "." .. name .. ".back", br == "ok", tostring(br) .. " crossed " .. tostring(bd) .. ", " .. n .. " to make")
        if br ~= "ok" then
            -- (stopped part way: on again over the rooms crossed, to where
            -- the relay left off)
            local on = {}
            for k = (bd or 0) + 1, 1, -1 do on[#on + 1] = back[k] end
            local fr, fd = t.raid.cox_walk_path(on, false)
            t.check("farm." .. i .. "." .. name .. ".return", fr == "ok", tostring(fr) .. " crossed " .. tostring(fd))
            return
        end
        local _, me = t.world.tile()
        local pr, pd = t.raid.cox_prep_solve({ max_ticks = 3000, make = PLAN, final = false, take_tools = { "rake" } })
        t.check("farm." .. i .. "." .. name .. ".prep", pr == "ok", tostring(pd))
        local on = {}
        for k = #back, 1, -1 do on[#on + 1] = back[k] end
        local fr, fd = t.raid.cox_walk_path(on, false)
        t.check("farm." .. i .. "." .. name .. ".on", fr == "ok", tostring(fr) .. " crossed " .. tostring(fd))
        -- (back in a floor end: past its own Passage again, where the hole
        -- is -- r26: three descents refused from its south side)
        if cur.name == "floor_end" then
            t.check("farm." .. i .. "." .. name .. ".into", t.raid.cox_into_floor_end(cur) == "ok", "")
        end
    end
    for i = 1, 24 do
        local _, room = t.raid.cox_here()
        local play = room
        if room == nil or done[key(room)] then
            local from = room or last
            play = from and t.raid.cox_room_ahead(from) or nil
        end
        local name = play and play.name or "?"
        t.ticklog.mark("raid room " .. i .. " " .. name)
        if play ~= nil and path[#path] ~= play then
            local top = path[#path]
            if not (top and top.ox == play.ox and top.oz == play.oz and top.level == play.level) then path[#path + 1] = play end
        end
        if play ~= nil and (name == "resource" or name == "resource_b") then resource = path[#path] end
        -- poisoned from the room before (the Shamans' acid, Vespula's
        -- soldiers): the antidote, before the next fight
        t.raid.cox_cure()
        -- and every prayer off: the next room lights what it needs
        t.raid.cox_prayers_off()
        -- one slot free, as every room test plays (a two-handed switch, the
        -- room's own pick-ups, a scavenger's drop): made by eating or
        -- drinking, never by dropping
        t.check("free." .. i .. "." .. name, true, tostring(t.raid.cox_make_free(1)))
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
                local sr1, sd1 = t.raid.cox_stash()
                t.check("stash." .. i .. "." .. name, sr1 == "ok", tostring(sd1))
                farm_trip(i, name)
                t.check("energy." .. i .. "." .. name, true, t.raid.cox_energy())
                local r = t.player.click_loc("raids_descentto2", 1)
                t.ticks(6)
                local down = select(2, t.world.tile()).level == 1
                t.check("raid.down." .. i, down, tostring(r))
                if not down then t.finish(1) return end
                last = nil
                path, resource, trips = {}, nil, 0
                -- floor 2 seen from its own start: Vespula ahead keeps her
                -- restores (r30: every relay Vespula loss was a floor-2 room
                -- the start-room scout never saw, met with 1.7 restore doses)
                local _, ahead = t.raid.cox_scout_here()
                t.check("raid.scout.floor2", true, tostring(ahead))
                if string.find(tostring(ahead), "vespula", 1, true) then
                    t.raid.cox_reserve("vespula", "#2restore", 8)
                    t.raid.cox_reserve("vespula", "raids_vial_revitalisation_strong_", 8)
                end
            else
                -- (one slot free, as the Olm test plays: the blowpipe's
                -- switch -- r10 raid5 met Olm with a full pack)
                farm_trip(i, name)
                -- Olm entered whole (r14 raid15 met it at 54 hp and 5 prayer):
                -- the top-up drinks what is carried, then the Olm pack
                -- refills his quota from storage
                t.check("topup." .. i .. ".olm", true, t.raid.cox_top_up(95, 85))
                t.check("energy." .. i .. ".olm", true, t.raid.cox_energy())
                local pr, pd = t.raid.cox_olm_pack({ leave = OLM_LEAVE, keep_free = 1 })
                t.check("supplies." .. i .. ".olm_pack", pr == "ok", tostring(pd))
                local r = t.player.click_loc("raids_bossentrance", 1)
                t.ticks(6)
                local olm = select(2, t.world.tile()).level == 2
                t.check("raid.olm_door", olm, tostring(r))
                if not olm then t.finish(1) return end
                break
            end
        elseif SOLVED[name] then
            -- the wiki's prep list: the room's styles pre-potted while no
            -- raid overload is carried
            t.check("topup." .. i .. "." .. name, true, t.raid.cox_top_up(nil, nil, name))
            t.check("prepot." .. i .. "." .. name, true, t.raid.cox_prepot(name))
            local fn = t.raid["cox_" .. name .. "_solve"]
            t.check("raid.solver." .. name, fn ~= nil, "")
            if fn == nil then t.finish(1) return end
            local r, d = fn({ max_ticks = 4000 })
            t.check("raid.room." .. i .. "." .. name, r == "ok", tostring(d))
            if r ~= "ok" then t.finish(1) return end
            done[key(play)] = true
            -- (a combat room never comes twice: what only it used may wait
            -- in storage from now on -- the pack plan, cox2_supplies.lua)
            t.raid.cox_mark_done(name)
            last = play
            local lr0, ld0 = t.raid.cox_supplies_loot({ keep_free = 1 })
            t.check("supplies." .. i .. "." .. name, lr0 == "ok", tostring(ld0))
            -- a room with a storage unit (the thieving room, the ice demon):
            -- the raid's stacks stashed there
            local sr0, sd0 = t.raid.cox_stash()
            t.check("stash." .. i .. "." .. name, sr0 == "ok", tostring(sd0))
            farm_trip(i, name)
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
            if name == "scavenger_small" or name == "scavenger_large" then farm_trip(i, name) end
            t.check("energy." .. i .. "." .. name, true, t.raid.cox_energy())
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
    id = "cox_raid_solo",
    fixture = "fresh_lumbridge.ini",
    max_frames = 2400000, -- (scouting: up to 1,500 reloads of ~18 ticks before the raid) -- the gate's ceiling: 16,000 ticks at 30 frames a tick (raid3 whole raid: 5,321 ticks)
    setup = kit(),
    run = run,
}
