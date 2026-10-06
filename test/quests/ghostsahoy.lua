-- Ghosts Ahoy. Authored against OSRS-Content quest_ghostsahoy/scripts/
-- ahoy_hub.rs2, ahoy_book.rs2, ahoy_manual.rs2, ahoy_robes.rs2, ahoy_shared.rs2
-- and configs/ghostsahoy.constant, read in full (2026-09-28). Guide:
-- docs/quests/ghosts_ahoy.md (Gate D closed, lobster/dye/Rune-Draw all real).
--
-- Fixture start: fresh_lumbridge.ini, beside Hans at 3206,3233,0.
--
-- Coordinates below are decoded straight from ghostsahoy.constant's
-- ^ahoy_*_coord fields (plane_zoneX_zoneZ_localX_localZ -> x=zoneX*64+localX,
-- z=zoneZ*64+localZ) or read off m56_55.jl2 for the rock-jump chain and
-- gangplank locs.
--
-- WALLS (door rule, owner 2026-10-03; re-driven in b61). Every goto departs
-- from and lands on open ground; every door, gate, barrier, stair, ladder and
-- trapdoor between the player and a target is pressed on every visit, in and
-- out. Checked with test/quests/orchestrator/matthew-mbp-m4/reports/
-- sample_tools/{reach,comp,locs_near}.py (doors closed):
--   * Lumbridge -> the Salve: only through the Varrock members' gate
--     fai_varrock_member_gatel/r 3319,3467-3468 (reach margins 30/80/160 all
--     NEEDS-DOOR via it); a double door that opens in place (doubledoors.loc).
--   * Morytania: in the way Priest in Peril opens it -- the Paterdomus
--     trapdoor 3405,3507 (down to 3405,9906), pip_underground_door1
--     3405,9895 and door2 3431,9897 (walk-through, gates.rs2), Drezel's
--     advice (60 -> 61) and the holy barrier 3440,9886 (p_telejump to
--     3423,3485, mausoleum_interactions.rs2). Morytania is never left.
--   * Port Phasmatys is walled (barrier 3659,3508 faces south; inside is
--     z < 3508): op4 Pay-toll (2 tokens) in, op1 Pass (free) out, on every
--     crossing (ahoy_hub.rs2 [label,ahoy_barrier_pass]).
--   * The Old Crone's house x 3460-3465 z 3556-3560 behind ahoy_harbour_door
--     on the north edge of 3461,3555 (maps/m54_55.jl2).
--   * The inn (Innkeeper, Robin) x >= 3671 behind ahoy_harbour_door on the
--     east edge of 3670,3497 (m57_54.jl2).
--   * The Ectofuntus: Necrovarus's ground floor is open from the north side
--     (reach len 7); upstairs by ahoy_tower_stairs_lv1 3666,3518 (maplink
--     3666,3517,0 -> 3666,3522,1, down 3666,3522,1 -> 3666,3517,0); the coffin
--     room x 3656-3663 behind the bone-key door 3656,3514,1 (west edge).
--   * The wreck: its hull (level 0) is open ground (reach len 98 from the
--     barrier); deck by ahoy_ghostship_ladder 3613,3543,0 (maplink
--     3612,3543,0 -> 3614,3543,1, back 3614,3543,1 -> 3612,3543,0); the mast
--     by the ladder 3615,3541,1 (3614,3541,1 -> 3616,3541,2, back down the
--     same); the Captain's Room x 3616-3622 behind ahoy_harbour_door on the
--     east edge of 3615,3543,1; the rocks by the gangplank (on 3605,3546,1 ->
--     3604,3550,0; off -> 3605,3546,1, ghostsahoy.constant).
--   * Dragontooth Island by the Ghost captain's boat both ways.

return {
    id = "ghostsahoy",
    fixture = "fresh_lumbridge.ini",
    max_frames = 160000, -- Rune-Draw (~12 ticks a game, up to 100 games) plus the walk in through Paterdomus
    setup = {
        "::clearinv",
        -- The two prerequisites FIRST: ~ahoy_reset (ahoy_shared.rs2:240-241) writes varp107/varp302
        -- complete without paying their quest points, so a ::complete after ::ghostsahoy took the
        -- "already complete" branch (quest_cheat.rs2:73-75) and the scroll read Total Quest Points 2
        -- (b56 shot sampler, 955-quest.scroll). Arms: quest_cheat.rs2:999 and :1068.
        "::complete quest_restlessghost",
        "::complete quest_priestinperil",
        "::ghostsahoy", -- resets ahoy_*, re-asserts prieststart/priestperil, worn amulet + 40 ecto-tokens
        "::give dagger_wolfbane 1", -- Priest in Peril's own reward (::complete grants no items); Drezel's holy-barrier advice needs it held (mausoleum_drezel.rs2:29-34)
        "::give amulet_of_ghostspeak 1", -- spare, handed to the Crone for enchanting
        "::give ectotoken 100",
        "::give silk 1",
        "::give costumeneedle 1", -- covers both needle and thread for the boat repair
        "::give knife 1",
        "::give spade 1",
        "::give oak_longbow 1",
        "::give bucket_ectoplasm 1", -- dyes the bedsheet green
        "::give bucket_milk 1", -- nettle tea
        "::give bowl_water 1", -- nettle tea (a filled bowl is a brought-along supply, not quest deliverable)
        "::give leather_gloves 1", -- worn before picking nettles
        "::give reddye 1",
        "::give bluedye 1",
        "::give yellowdye 1",
        "::give orangedye 1",
        "::give greendye 1",
        "::give purpledye 1",
        "::give coins 1000", -- Rune-Draw stakes, 25/game
        "::give logs 1",
        "::give tinderbox 1", -- lights the fire the nettle-water is boiled over
        "::give rune_scimitar 1", -- worn for the giant lobster
        "::give lobster 4", -- food for the giant lobster: it swings back now (max hit 4)
        "::setlevel agility 25",
        "::setlevel cooking 20",
        "::setlevel attack 40",
        "::setlevel strength 40",
        "::setlevel defence 40",
        "::setlevel hitpoints 40",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb217_ahoy_questvar",
            constants = {
                not_started = 0,
                talked_velorina = 1,
                talked_necrovarus = 2,
                told_of_crone = 3,
                gathering_items = 4,
                need_amulet = 5,
                amulet_enchanted = 6,
                necrovarus_defeated = 7,
                complete = 8,
            },
            row = "quest_ghostsahoy",
            display = "Ghosts Ahoy",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        -- Both prerequisites PAID (1 qp each, Quest Helper GhostsAhoy.java requirements:
        -- Priest in Peril + The Restless Ghost): qp 2 before the quest, and neither
        -- ::complete took its "already complete" branch.
        local qp0_res, qp0 = t.var.varp("varp101_qp")
        t.expect("prereq.qp_before", (qp0_res == "ok" and tonumber(qp0) == 2) and "ok" or "refused",
            "varp101_qp after setup = " .. tostring(qp0) .. " (want 2: Restless Ghost 1 + Priest in Peril 1)")
        local pip_res, pip_line = t.msg.expect("Priest in Peril complete.")
        local rg_res, rg_line = t.msg.expect("Restless Ghost complete.")
        local al_res, al_line = t.msg.expect("already complete")
        t.expect("prereq.paid", (pip_res == "ok" and rg_res == "ok" and al_res ~= "ok") and "ok" or "refused",
            "pip=" .. tostring(pip_res) .. " [" .. tostring(pip_line) .. "] rg=" .. tostring(rg_res) .. " ["
                .. tostring(rg_line) .. "] already=" .. tostring(al_res) .. " [" .. tostring(al_line) .. "]")

        -- Wear the tools that must be worn: frees two backpack slots and
        -- satisfies the nettle-picking glove check and arms the lobster fight.
        t.exec("equip.gloves", t.player.equip, "leather_gloves")
        t.exec("equip.scimitar", t.player.equip, "rune_scimitar")

        t.exec("quest.stage.not_started", t.quest.expect_stage, "not_started")

        local function tile_text(tile)
            if type(tile) ~= "table" then
                return tostring(tile)
            end
            return tostring(tile.x) .. "," .. tostring(tile.z) .. "," .. tostring(tile.level)
        end
        local function await_tile(pred, ticks, what)
            return t.await({
                level = function()
                    local r, tt = t.world.tile()
                    return r == "ok" and pred(tt)
                end,
                note = what .. ": waiting for the landing",
            }, ticks)
        end
        local function free_slots()
            local free = 0
            for i = 0, 27 do
                local sr, slot = t.inv.slot(i)
                if sr ~= "ok" then
                    return nil
                end
                if slot.count == 0 then
                    free = free + 1
                end
            end
            return free
        end

        -- ---------------------------------------------------------------
        -- Into Morytania (WALLS in the header): the Varrock members' gate,
        -- the Paterdomus trapdoor, the two mausoleum gates, Drezel's advice
        -- and the holy barrier.
        -- ---------------------------------------------------------------
        t.exec("goto-enterMorytania.varrockGate", t.player.goto_tile, 3318, 3468, 0)
        t.exec("enterMorytania.varrockGate", t.player.pass_door, { closed = "fai_varrock_member_gatel",
            open = "fai_varrock_member_gatel_open", at = { 3319, 3468, 0 }, near = { 3318, 3468 }, far = { 3321, 3468 } })
        t.exec("goto-enterMorytania.trapdoor", t.player.goto_tile, 3405, 3506, 0)
        t.exec("enterMorytania.openTrapdoor", t.player.click_loc, "trapdoor", 1, { at = { 3405, 3507, 0 } })
        t.await({
            level = function()
                return t.world.loc_near("trapdoor_open", 3, { at = { 3405, 3507, 0 } }) == "ok"
            end,
            note = "enterMorytania: the trapdoor opens",
        }, 6)
        local tdo_r, tdo = t.world.loc_near("trapdoor_open", 3, { at = { 3405, 3507, 0 } })
        local tdc_r = t.world.loc_near("trapdoor", 3, { at = { 3405, 3507, 0 } })
        t.check("enterMorytania.trapdoorOpen", tdo_r == "ok" and tdc_r ~= "ok",
            "trapdoor_open on 3405,3507,0 -> " .. tostring(tdo_r) .. " "
                .. (tdo_r == "ok" and (tdo.tile_x .. "," .. tdo.tile_z .. "," .. tdo.level) or tostring(tdo))
                .. "; closed trapdoor there -> " .. tostring(tdc_r) .. " (want the open leaf and no closed one)")
        t.exec("enterMorytania.descend", t.player.cross_trap, { loc = "trapdoor_open", op = 1, op_name = "Climb-down",
            at = { 3405, 3507, 0 }, src = { 3405, 3506 }, dest = { 3405, 9906 }, attempts = 2 })
        t.exec("enterMorytania.gate1", t.player.cross_gate, { loc = "pip_underground_door1", at = { 3405, 9895, 0 },
            near = { 3405, 9896 }, far_ok = function(tile) return tile.z > 6400 and tile.z <= 9894 end,
            far_desc = "south of the golden-key gate, z <= 9894", ticks = 30 })
        t.exec("enterMorytania.gate2", t.player.cross_gate, { loc = "pip_underground_door2", at = { 3431, 9897, 0 },
            near = { 3430, 9897 }, far_ok = function(tile) return tile.z > 6400 and tile.x >= 3432 end,
            far_desc = "Drezel's side of the second gate, x >= 3432", ticks = 60 })
        -- Priest in Peril's farewell advice (mausoleum_drezel.rs2:145-154,
        -- LostCity drezel.rs2:138-147): 60 -> 61, the holy barrier opens.
        t.exec("enterMorytania.talkToDrezel", t.player.talk_to, "priestperiltrappedmonk2", 1)
        t.exec("enterMorytania.talkToDrezel-dialog", t.chat.play, {
            "player:So can I pass through that barrier now?",
            "npc:Ah, ",
            "npc:Morytania is an evil land",
            "npc:You should take some basic precautions",
            "npc:In many ways Werewolves",
            "npc:and it is a holy relic",
            "npc:wolf form is incredibly powerful",
            "player:Okay, I will keep it equipped",
        })
        t.exec("enterMorytania.drezelAdvice", t.var.await_server, "varp302_priestperil", 61, 8)
        t.exec("enterMorytania.holyBarrier", t.player.cross_gate, { loc = "pip_underground_wall_side_withportal",
            at = { 3440, 9886, 0 }, near = { 3440, 9887 },
            far_ok = function(tile) return tile.x == 3423 and tile.z == 3485 end,
            far_desc = "east of the Salve at 3423,3485 (mausoleum_interactions.rs2 p_telejump(0_53_54_31_29))" })

        -- Port Phasmatys' north Energy Barrier: maps/m57_54.jl2 row
        -- `0 11 52: 57722 10 3` -- ahoy_town_barrier_multi at 3659,3508,
        -- facing south, spanning 3659-3660. The town (Velorina, the inn,
        -- Gravingas, the docks) is SOUTH of it (z < 3508), the Ectofuntus
        -- NORTH. op4 "Pay-toll(2-Ecto)" pays without the guard's talk; from
        -- inside op1 "Pass" lets you out free (ahoy_hub.rs2
        -- [label,ahoy_barrier_pass]); after Necrovarus is commanded op4 is
        -- free both ways. Every crossing is graded on the tiles either side.
        local function enter_phas(step, toll)
            t.exec("goto-" .. step, t.player.goto_tile, 3660, 3509, 0)
            local r0, before = t.inv.count("ectotoken")
            t.exec(step, t.player.cross_gate, { loc = "ahoy_town_barrier_multi", op = 4, at = { 3659, 3508, 0 },
                near = { 3660, 3509 }, far_ok = function(tile) return tile.z < 3508 end,
                far_desc = "inside Port Phasmatys, z < 3508" })
            local r1, after = t.inv.count("ectotoken")
            t.check(step .. ".toll", r0 == "ok" and r1 == "ok" and before ~= nil and after ~= nil
                and before - after == toll,
                "ecto-tokens " .. tostring(before) .. " -> " .. tostring(after) .. " (want -" .. toll .. ")")
        end
        local function exit_phas(step)
            t.exec("goto-" .. step, t.player.goto_tile, 3660, 3507, 0)
            t.exec(step, t.player.cross_gate, { loc = "ahoy_town_barrier_multi", op = 1, at = { 3659, 3508, 0 },
                near = { 3660, 3507 }, far_ok = function(tile) return tile.z > 3508 end,
                far_desc = "outside Port Phasmatys, north of the barrier, z > 3508" })
        end

        -- The Old Crone's house and the inn: ahoy_harbour_door copies that
        -- open like any door (ahoy_robes.rs2 [oploc1,ahoy_harbour_door]).
        local function harbour_door(at, near, far)
            return { closed = "ahoy_harbour_door", open = "ahoy_harbour_door_open", at = at, near = near, far = far }
        end
        local CRONE_IN = harbour_door({ 3461, 3555, 0 }, { 3461, 3554 }, { 3461, 3557 })
        local CRONE_OUT = harbour_door({ 3461, 3555, 0 }, { 3461, 3556 }, { 3461, 3554 })
        local INN_IN = harbour_door({ 3670, 3497, 0 }, { 3669, 3497 }, { 3672, 3497 })
        local INN_OUT = harbour_door({ 3670, 3497, 0 }, { 3671, 3497 }, { 3669, 3497 })
        local function crone_in(step)
            t.exec("goto-" .. step, t.player.goto_tile, 3461, 3554, 0)
            t.exec(step .. ".doorIn", t.player.pass_door, CRONE_IN)
        end
        local function crone_out(step)
            t.exec(step .. ".doorOut", t.player.pass_door, CRONE_OUT)
        end

        -- ---------------------------------------------------------------
        -- talkToVelorina: accept the quest.
        -- ---------------------------------------------------------------
        enter_phas("enterPhas", 2)
        t.exec("goto-talkToVelorina", t.player.goto_tile, 3678, 3510, 0)
        t.exec("talkToVelorina", t.player.talk_to, "ahoy_velorina", 1)
        t.exec("talkToVelorina-dialog", t.chat.play, {
            "player:Why, what is the matter?",
            "npc:I am trapped here, unable to pass on",
            "choose:Yes, I will help.",
            "player:Yes, I will help.",
            "npc:Will you speak to Necrovarus for me",
            "player:Yes.",
        })
        t.exec("quest.stage.talked_velorina", t.quest.expect_stage, "talked_velorina")

        -- talkToNecrovarus: refused. His ground floor is open from the
        -- barrier's north side: talk_to walks there.
        exit_phas("exitPhas")
        t.exec("talkToNecrovarus", t.player.talk_to, "ahoy_necrovarus", 1)
        t.exec("talkToNecrovarus-dialog", t.chat.play, {
            "player:Velorina asked me to speak to you",
            "npc:Pass on? Never!",
        })
        t.exec("quest.stage.talked_necrovarus", t.quest.expect_stage, "talked_necrovarus")

        -- talkToVelorinaAfterNecro: sent to the Old Crone.
        enter_phas("enterPhasAfterNecro", 2)
        t.exec("goto-talkToVelorinaAfterNecro", t.player.goto_tile, 3678, 3510, 0)
        t.exec("talkToVelorinaAfterNecro", t.player.talk_to, "ahoy_velorina", 1)
        t.exec("talkToVelorinaAfterNecro-dialog", t.chat.play, {
            "player:Necrovarus refused to let anyone pass on.",
            "npc:There is an old woman who once served him",
        })
        t.exec("quest.stage.told_of_crone", t.quest.expect_stage, "told_of_crone")

        -- talkToCrone x1: cup handed over.
        exit_phas("exitPhasForCrone")
        crone_in("talkToCrone")
        t.exec("talkToCrone", t.player.talk_to, "ahoy_crone", 1)
        t.exec("talkToCrone-dialog", t.chat.play, {
            "player:I'm here about Necrovarus.",
            "npc:Bring me a cup, with a little milk.",
        })
        t.exec("inv.cup", t.inv.expect_has, "chinacup_empty", 1)
        crone_out("talkToCrone")

        -- Pick nettles (m56_54.jl2 local 37,55 -- the nearest clump to the
        -- Ectofuntus/Crone area the map actually has), steep, boil, pour,
        -- add milk.
        t.exec("goto-pickNettles", t.player.goto_tile, 3621, 3511, 0)
        t.exec("pickNettles", t.player.click_loc, "nettles", 1)
        -- click_loc's `ok` is the server's sentence, not the container update
        -- (QUEST_AUTHORING trap 24) -- wait for the backpack, not a bare read.
        t.exec("inv.nettles_picked", t.inv.await, "nettles_picked", 1, 5)
        t.exec("nettles.steep", t.player.use_item_on_item, "bowl_water", "nettles_picked")
        t.exec("inv.nettlewater", t.inv.expect_has, "bowl_nettlewater", 1)

        t.exec("lightFire", t.player.use_item_on_item, "tinderbox", "logs")
        -- Firemaking is a self-continuing action (p_opobj(4) re-arms the
        -- attempt every action cycle until the stat_random roll lands) --
        -- poll a few ticks for the resulting `fire` loc rather than a bare
        -- read right after the first "You attempt to light the logs." page.
        local fire, fire_d = "not_found", nil
        local fire_tries = 0
        -- 40 ticks, not 8: the attempt re-rolls every cycle and a low
        -- Firemaking level can miss for many in a row (seam27 s27gh_full1:
        -- 8 ticks ran out once the barrier crossings shifted the rolls).
        while fire ~= "ok" and fire_tries < 40 do
            fire, fire_d = t.world.loc_near("fire", 10)
            if fire ~= "ok" then
                t.ticks(1)
                fire_tries = fire_tries + 1
            end
        end
        t.step("world.fire_near", fire == "ok" and "PASS" or "FAIL",
            fire == "ok" and ("fire at " .. tostring(fire_d.tile_x) .. "," .. tostring(fire_d.tile_z)) or tostring(fire_d))
        t.exec("useTeaOnFire", t.player.use_on, "bowl_nettlewater", fire_d)
        t.exec("inv.nettletea", t.inv.expect_has, "bowl_nettletea", 1)

        t.exec("useTeaOnCup", t.player.use_item_on_item, "chinacup_empty", "bowl_nettletea")
        t.exec("inv.tea_in_cup", t.inv.expect_has, "chinacup_of_nettletea", 1)
        t.exec("useMilkOnTea", t.player.use_item_on_item, "chinacup_of_nettletea", "bucket_milk")
        t.exec("inv.milky_tea", t.inv.expect_has, "chinacup_of_nettletea_milky", 1)

        -- talkToCroneAgainForShip: tea delivered, toy boat handed over,
        -- questvar advances to gathering_items.
        crone_in("talkToCroneAgainForShip")
        t.exec("talkToCroneAgainForShip", t.player.talk_to, "ahoy_crone", 1)
        t.exec("talkToCroneAgainForShip-dialog", t.chat.play, {
            "player:I'm here about Necrovarus.",
            "npc:It all comes back to me now.",
            "npc:you'll need three things",
            "choose:I'll get started.",
        })
        t.exec("quest.stage.gathering_items", t.quest.expect_stage, "gathering_items")
        t.exec("inv.toyboat", t.inv.expect_has, "ahoy_toy_boat", 1)
        crone_out("talkToCroneAgainForShip")

        -- =================================================================
        -- Robes branch: bedsheet, petition (10 signatures), bone key,
        -- harbour door, coffin.
        -- =================================================================
        enter_phas("enterPhasForRobe", 2)
        t.exec("goto-talkToInnkeeper", t.player.goto_tile, 3669, 3497, 0)
        t.exec("talkToInnkeeper.doorIn", t.player.pass_door, INN_IN)
        t.exec("talkToInnkeeper", t.player.talk_to, "ahoy_ghost_innkeeper", 1)
        t.exec("talkToInnkeeper-dialog", t.chat.play, {
            "player:Do you have any jobs I can do?",
            "npc:I've a spare bedsheet",
        })
        t.exec("inv.bedsheet", t.inv.expect_has, "ahoy_bedsheet", 1)
        t.exec("talkToInnkeeper.doorOut", t.player.pass_door, INN_OUT)

        t.exec("useSlimeOnSheet", t.player.use_item_on_item, "bucket_ectoplasm", "ahoy_bedsheet")
        t.exec("inv.bedsheetgreen", t.inv.expect_has, "ahoy_bedsheetgreen", 1)
        t.check("useSlimeOnSheet.consumed", t.inv.expect_absent("bucket_ectoplasm") == "ok"
            and t.inv.expect_absent("ahoy_bedsheet") == "ok",
            "bucket_ectoplasm and the white bedsheet left the pack (want both gone: the slime dyed the sheet)")
        t.exec("equip.bedsheet", t.player.equip, "ahoy_bedsheetgreen")

        t.exec("goto-talkToGravingas", t.player.goto_tile, 3660, 3499, 0)
        t.exec("talkToGravingas", t.player.talk_to, "protester_ghostspeak_multi", 1)
        t.exec("talkToGravingas-dialog", t.chat.play, {
            "player:I've heard Velorina's sad story",
            "npc:Take this petition and get the townsfolk to sign it",
        })
        t.exec("inv.petition", t.inv.expect_has, "ahoy_petition", 1)

        -- talkToVillagers x10: signaturecounter 1 -> 11 (10 successful signs).
        t.exec("goto-talkToVillagers", t.player.goto_tile, 3661, 3497, 0)
        local sign_n = 0
        while sign_n < 10 do
            sign_n = sign_n + 1
            local vr, vd = t.player.talk_to("ahoy_ghost_villager", 1)
            t.step("talkToVillagers." .. sign_n, vr == "ok" and "PASS" or "FAIL", vd)
            if vr ~= "ok" then
                t.blocked("talkToVillagers: ahoy_ghost_villager talk_to answered " .. tostring(vr) .. " -- " .. tostring(vd))
                return
            end
            local pr, pd = t.chat.play({
                "player:Would you sign this petition",
                "npc:Why, of course.",
            })
            t.step("talkToVillagers.dialog." .. sign_n, pr == "ok" and "PASS" or "FAIL", pd)
            if pr ~= "ok" then
                t.blocked("talkToVillagers: signing dialog #" .. sign_n .. " answered " .. tostring(pr) .. " -- " .. tostring(pd))
                return
            end
        end
        local sig_res, sig_val = t.var.server("varb209_ahoy_signaturecounter")
        t.check("petition.full", sig_res == "ok" and sig_val ~= nil and sig_val >= 11,
            "ahoy_signaturecounter = " .. tostring(sig_val) .. " (" .. tostring(sig_res) .. ")")
        -- A villager can wander into the inn while its door stands open, and
        -- talk_to follows him in on foot (run 3: the last signature was taken
        -- at 3672,3497). The inn's walkable tiles are exactly x 3671-3681
        -- z 3489-3499 (flooded from both sides, doors closed: no outside tile
        -- in that box): if the player is in there, out by the door.
        local vt_r, vt = t.world.tile()
        if vt_r == "ok" and vt.level == 0 and vt.x >= 3671 and vt.x <= 3681 and vt.z >= 3489 and vt.z <= 3499 then
            t.exec("talkToVillagers.innDoorOut", t.player.pass_door, INN_OUT)
        end

        -- showPetitionToNecro: ashes, bone key drops on the ground.
        exit_phas("exitPhasForNecro")
        t.exec("showPetitionToNecro", t.player.talk_to, "ahoy_necrovarus", 1)
        t.exec("showPetitionToNecro-dialog", t.chat.play, {
            "player:The townsfolk have signed this petition",
            "npc:How DARE you incite my flock against me!",
        })
        -- click_obj answers `ok` with a NIL detail (QUEST_AUTHORING trap 12/
        -- section 8's hollow list) -- call it directly, never through
        -- t.exec, and it already waits for the backpack count to rise.
        local key_res, key_detail = t.player.click_obj("ahoy_bone_key")
        t.step("takeKey", key_res == "ok" and "PASS" or "FAIL", tostring(key_detail))
        t.exec("inv.bonekey", t.inv.expect_has, "ahoy_bone_key", 1)

        -- goUpFromNecro + useKeyOnDoor + takeRobes: upstairs at the
        -- Ectofuntus, the coffin room behind the bone-key door. The content
        -- has no use-on for that door: its op1 Open takes the key from the
        -- pack and unlocks it (ahoy_robes.rs2 [oploc1,ahoy_harbour_door],
        -- 1_57_54_8_58), so the door is pressed with the key carried and
        -- graded on the key leaving the pack and the unlock bit.
        t.exec("goUpFromNecro", t.player.climb, { loc = "ahoy_tower_stairs_lv1", op = 1, op_name = "Climb-up",
            at = { 3666, 3518, 0 }, src = { 3666, 3517 }, dest = { 3666, 3522, 1 } })
        local bk0_res, bk0_n = t.inv.count("ahoy_bone_key")
        t.exec("useKeyOnDoor", t.player.pass_door, harbour_door({ 3656, 3514, 1 }, { 3655, 3514 }, { 3657, 3514 }))
        local tdu_res, tdu_val = t.var.server("varb213_ahoy_templedoor_unlocked")
        local bk_res, bk_n = t.inv.count("ahoy_bone_key")
        local bk_diff = (bk0_res == "ok" and bk_res == "ok" and bk_n < bk0_n)
            and (" [backpack: lost ahoy_bone_key " .. bk0_n .. "->" .. bk_n .. "]") or ""
        t.check("useKeyOnDoor.unlocked", tdu_res == "ok" and tdu_val == 1 and bk0_res == "ok" and bk0_n == 1
            and bk_res == "ok" and bk_n == 0,
            "ahoy_templedoor_unlocked = " .. tostring(tdu_val) .. " (" .. tostring(tdu_res) .. "), bone keys "
                .. tostring(bk0_n) .. " -> " .. tostring(bk_n) .. " across the door press (want 1 and 1 -> 0: the key"
                .. " opened the door and was used up)" .. bk_diff)
        t.exec("takeRobes", t.player.click_loc, "ahoy_coffin", 1)
        -- click_loc's `ok` is the server's sentence, not the container
        -- update (trap 24) -- poll instead of a bare read.
        t.exec("inv.robes", t.inv.await, "ahoy_robes_of_necrovarus", 1, 5)
        t.exec("takeRobes.doorOut", t.player.pass_door, harbour_door({ 3656, 3514, 1 }, { 3657, 3514 }, { 3655, 3514 }))
        t.exec("takeRobes.downstairs", t.player.climb, { loc = "ahoy_tower_stairs_lv1_top", op = 1, op_name = "Climb-down",
            at = { 3666, 3520, 1 }, src = { 3666, 3522 }, dest = { 3666, 3517, 0 } })

        -- =================================================================
        -- Manual branch: Ak-Haranu, Robin's Rune-Draw (real game, drawn
        -- until Robin's debt reaches 100), manual.
        -- =================================================================
        enter_phas("enterPhasForManual", 2)
        t.exec("goto-talkToAkHaranu", t.player.goto_tile, 3689, 3499, 0)
        t.exec("talkToAkHaranu", t.player.talk_to, "ahoy_akharanu_multi", 1)
        t.exec("talkToAkHaranu-dialog", t.chat.play, {
            "player:I have an oak longbow.",
            "npc:Okay, wait here",
        })
        local bow_res0, bow_val0 = t.var.server("varb212_ahoy_subquest_bow")
        t.check("bow.talked_akharanu", bow_res0 == "ok" and bow_val0 ~= nil and bow_val0 >= 1,
            "ahoy_subquest_bow = " .. tostring(bow_val0) .. " (" .. tostring(bow_res0) .. ")")

        t.exec("goto-talkToRobin", t.player.goto_tile, 3669, 3497, 0)
        t.exec("talkToRobin.doorIn", t.player.pass_door, INN_IN)

        -- Robin's Rune-Draw, played for real until he owes 100 and signs the
        -- bow (ahoy_manual.rs2 [proc,ahoy_runedraw_round]).  t.game.runedraw
        -- plays ONE game from the page talk_to leaves up and picks every
        -- Draw/Hold by the exact best reply to Robin's fixed rule: +0.094 a
        -- game, 15.4 games to a debt of 100 on average, P(>100 games) 0.0002
        -- (QD.game banner, chat.lua).  The cap is that tail, not a hope.
        local rd_games = 0
        while true do
            local bow_res, bow_val = t.var.server("varb212_ahoy_subquest_bow")
            if bow_res == "ok" and bow_val ~= nil and bow_val >= 2 then
                break
            end
            rd_games = rd_games + 1
            if rd_games > 100 then
                t.blocked("Rune-Draw: Robin's debt never reached 100 in 100 games under the exact policy (P = 0.0002)")
                return
            end
            local tr, td = t.player.talk_to("ahoy_robin", 1)
            t.step("talkToRobin." .. rd_games, tr == "ok" and "PASS" or "FAIL", td)
            if tr ~= "ok" then
                t.blocked("talkToRobin: talk_to answered " .. tostring(tr) .. " -- " .. tostring(td))
                return
            end
            local gr, game = t.game.runedraw()
            t.step("runedraw.game." .. rd_games, gr == "ok" and "PASS" or "FAIL",
                gr == "ok" and game.text or tostring(game))
            if gr ~= "ok" then
                t.blocked("Rune-Draw: game " .. rd_games .. " answered " .. tostring(gr) .. " -- " .. tostring(game))
                return
            end
            local debt_res, debt_val = t.quest._read_content("varp7172_ahoy_robin_debt")
            t.step("runedraw.debt." .. rd_games, (debt_res == "ok" and tonumber(debt_val) ~= nil) and "PASS" or "FAIL",
                "ahoy_robin_debt = " .. tostring(debt_val) .. " (" .. tostring(debt_res) .. ") after game " .. rd_games)
        end
        t.exec("quest.stage.bow_signed", t.var.expect, "varb212_ahoy_subquest_bow", 2)
        t.exec("talkToRobin.doorOut", t.player.pass_door, INN_OUT)

        t.exec("goto-bringBowToAkHaranu", t.player.goto_tile, 3689, 3499, 0)
        t.exec("bringBowToAkHaranu", t.player.talk_to, "ahoy_akharanu_multi", 1)
        t.exec("bringBowToAkHaranu-dialog", t.chat.play, {
            "player:Here's your signed oak longbow.",
            "npc:As promised, here is the translation manual.",
        })
        t.exec("inv.manual", t.inv.expect_has, "ahoy_translation_manual", 1)
        t.check("bringBowToAkHaranu.bowGiven", t.inv.expect_absent("oak_longbow_signed") == "ok",
            "oak_longbow_signed left the pack (want it handed to Ak-Haranu)")

        -- =================================================================
        -- Book branch: repair the toy boat, dye its flag, trade for the
        -- chest key, open the captain's chest (scrap 1), fight the giant
        -- lobster in the hull and loot its chest (scrap 3), jump the rocks
        -- to the third chest (scrap 2), combine the map, sail to
        -- Dragontooth Island, dig.
        -- =================================================================
        t.exec("repairShip", t.player.use_item_on_item, "silk", "ahoy_toy_boat")
        t.exec("inv.repaired", t.inv.expect_has, "ahoy_toy_boat_repaired", 1)
        t.check("repairShip.silkUsed", t.inv.expect_absent("silk") == "ok", "the silk left the pack (want it sewn into the sail)")

        -- The wreck's hull is open ground west of the town (WALLS); every
        -- deck above it is a ladder.
        exit_phas("exitPhasForWreck")
        local free_ship = free_slots()
        t.check("ship.freeSlots", free_ship ~= nil and free_ship >= 4,
            "free backpack slots before the wreck: " .. tostring(free_ship)
                .. " (want >= 4: chest key, scrap 1, scrap 3, scrap 2, and a second dye pot)")
        t.exec("goto-goUpToDeckForMast", t.player.goto_tile, 3612, 3543, 0)
        t.exec("goUpToDeckForMast", t.player.climb, { loc = "ahoy_ghostship_ladder", op = 1, op_name = "Climb-up",
            at = { 3613, 3543, 0 }, src = { 3612, 3543 }, dest = { 3614, 3543, 1 } })
        t.exec("goUpToMast", t.player.climb, { loc = "ahoy_ghostship_ladder", op = 1, op_name = "Climb-up",
            at = { 3615, 3541, 1 }, src = { 3614, 3541 }, dest = { 3616, 3541, 2 } })

        -- searchMast: "Search the Mast repeatedly until you've found out all
        -- the colours" (ahoy_book.rs2 [oploc1,ahoy_mast]): a low wind shows
        -- ONE random part's colour in a mesbox, a high wind shows nothing.
        -- The colours are read off those boxes, then checked against the
        -- per-player roll.
        local colour_index = { red = 1, blue = 2, yellow = 3, orange = 4, green = 5, purple = 6 }
        local seen = {}
        local mast_presses, mast_trail = 0, {}
        while (seen.top == nil or seen.bottom == nil or seen.skull == nil) and mast_presses < 80 do
            mast_presses = mast_presses + 1
            local mr, md = t.player.click_loc("ahoy_mast", 1, { at = { 3619, 3543, 2 } })
            if mr ~= "ok" and mr ~= "timeout" then
                mast_trail[#mast_trail + 1] = mast_presses .. ":" .. tostring(mr) .. " " .. tostring(md)
                break
            end
            t.await({ level = function() return t.chat.kind() == "mesbox" end, note = "searchMast: a colour box" }, 3)
            if t.chat.kind() == "mesbox" then
                local xr, text = t.chat.text()
                local flat = string.lower(tostring(text)):gsub("<[^>]*>", " "):gsub("%s+", " ")
                local top = flat:match("top half is coloured (%a+)")
                local bottom = flat:match("bottom half is coloured (%a+)")
                local skull = flat:match("skull emblem is coloured (%a+)")
                seen.top = seen.top or top
                seen.bottom = seen.bottom or bottom
                seen.skull = seen.skull or skull
                mast_trail[#mast_trail + 1] = mast_presses .. ":" .. (top and ("top " .. top) or bottom and ("bottom " .. bottom)
                    or skull and ("skull " .. skull) or ("? " .. tostring(xr) .. " " .. flat:sub(1, 60)))
                t.chat.continue_()
            else
                -- High wind: nothing to read. The wind re-rolls on every
                -- search and on its own 8-15 tick timer (ahoy_book.rs2
                -- [timer,ahoy_wind]); account ghostsahoy_x saw 11 high in a
                -- row, so the cap is 80 searches, not 40.
                mast_trail[#mast_trail + 1] = mast_presses .. ":wind"
                t.ticks(2)
            end
        end
        local top_val, bot_val, sku_val = colour_index[seen.top or ""], colour_index[seen.bottom or ""],
            colour_index[seen.skull or ""]
        t.check("searchMast", top_val ~= nil and bot_val ~= nil and sku_val ~= nil,
            "read off the mast in " .. mast_presses .. " search(es): top=" .. tostring(seen.top) .. " bottom="
                .. tostring(seen.bottom) .. " skull=" .. tostring(seen.skull) .. " [" .. table.concat(mast_trail, "; ") .. "]")
        if top_val == nil or bot_val == nil or sku_val == nil then
            t.blocked("searchMast: the three flag colours were not all shown in " .. mast_presses .. " searches")
            return
        end
        local top_res, top_srv = t.quest._read_content("varp6828_ahoy_flag_top")
        local bot_res, bot_srv = t.quest._read_content("varp6829_ahoy_flag_bottom")
        local sku_res, sku_srv = t.quest._read_content("varp6830_ahoy_flag_skull")
        t.check("searchMast.matchesRoll", top_res == "ok" and bot_res == "ok" and sku_res == "ok"
            and tonumber(top_srv) == top_val and tonumber(bot_srv) == bot_val and tonumber(sku_srv) == sku_val,
            "read " .. top_val .. "/" .. bot_val .. "/" .. sku_val .. " vs the per-player roll top="
                .. tostring(top_srv) .. " bottom=" .. tostring(bot_srv) .. " skull=" .. tostring(sku_srv))

        local ahoy_colour_dye = { "reddye", "bluedye", "yellowdye", "orangedye", "greendye", "purpledye" }
        local ahoy_colour_word = { "red", "blue", "yellow", "orange", "green", "purple" }

        -- The mast's three colours are rolled per player and may repeat
        -- (top=5 bottom=1 skull=1 in s24gh_run1), and setup brings ONE of
        -- each dye: bring the repeats the way a player buys a second pot.
        local dye_need = {}
        for _, v in ipairs({ top_val, bot_val, sku_val }) do
            dye_need[v] = (dye_need[v] or 0) + 1
        end
        for v, n in pairs(dye_need) do
            if n > 1 then
                -- lint: kit-give dye repeat: the flag colours are rolled per player at the first mast search (ahoy_book.rs2 ~ahoy_flag_init) and may repeat; setup cannot know which pot is needed twice, and six spare pots would overflow the pack
                t.cheat("::give " .. ahoy_colour_dye[v] .. " " .. (n - 1))
                t.exec("supply.dye." .. ahoy_colour_dye[v], t.inv.await, ahoy_colour_dye[v], n, 5)
            end
        end

        t.exec("dyeTop", t.player.use_item_on_item, ahoy_colour_dye[top_val], "ahoy_toy_boat_repaired")
        t.exec("dyeTop.dialog", t.chat.play, {
            "choose:Top half",
            "mesbox:You dye the top of the flag " .. ahoy_colour_word[top_val],
        })
        t.exec("dyeBottom", t.player.use_item_on_item, ahoy_colour_dye[bot_val], "ahoy_toy_boat_repaired")
        t.exec("dyeBottom.dialog", t.chat.play, {
            "choose:Bottom half",
            "mesbox:You dye the bottom of the flag " .. ahoy_colour_word[bot_val],
        })
        t.exec("dyeSkull", t.player.use_item_on_item, ahoy_colour_dye[sku_val], "ahoy_toy_boat_repaired")
        t.exec("dyeSkull.dialog", t.chat.play, {
            "choose:Skull emblem",
            "mesbox:You dye the skull emblem " .. ahoy_colour_word[sku_val],
        })

        -- goDownToMan + talkToMan: down to the deck, into the Captain's Room.
        t.exec("goDownToMan", t.player.climb, { loc = "ahoy_ghostship_laddertop", op = 1, op_name = "Climb-down",
            at = { 3615, 3541, 2 }, src = { 3616, 3541 }, dest = { 3614, 3541, 1 } })
        local CAPTAIN_IN = harbour_door({ 3615, 3543, 1 }, { 3615, 3543 }, { 3617, 3544 })
        local CAPTAIN_OUT = harbour_door({ 3615, 3543, 1 }, { 3616, 3543 }, { 3614, 3543 })
        t.exec("talkToMan.doorIn", t.player.pass_door, CAPTAIN_IN)
        t.exec("talkToMan", t.player.talk_to, "ahoy_oldman", 1)
        t.exec("talkToMan-dialog", t.chat.play, {
            "player:Is this your toy boat?",
            "npc:Here -- take the key to my chest",
        })
        t.exec("inv.chestkey", t.inv.expect_has, "ahoy_chest_key", 1)
        t.check("talkToMan.boatGiven", t.inv.expect_absent("ahoy_toy_boat_repaired") == "ok",
            "the dyed toy boat left the pack (want it traded to the Old Man)")

        local chest_target, chest_target_r = t.player.by_symbol("loc", "ahoy_chest_locked")
        t.step("world.chest_locked_symbol", chest_target ~= nil and "PASS" or "FAIL", tostring(chest_target_r))
        -- [oplocu,ahoy_chest_locked] is the unlock trigger (last_useitem =
        -- ahoy_chest_key), a USE-ON, never a plain click_loc.
        t.exec("useKeyOnChest", t.player.use_on, "ahoy_chest_key", chest_target, { at = { 3619, 3545, 1 } })
        local cu_res, cu_val = t.quest._read_content("varp6835_ahoy_captain_chest_unlocked")
        local ck_res, ck_n = t.inv.count("ahoy_chest_key")
        t.check("useKeyOnChest.unlocked", cu_res == "ok" and tonumber(cu_val) == 1 and ck_res == "ok" and ck_n == 0,
            "ahoy_captain_chest_unlocked = " .. tostring(cu_val) .. ", chest keys left " .. tostring(ck_n)
                .. " (want 1 and 0: ahoy_book.rs2 takes the key)")
        t.exec("openSecondChest", t.player.click_loc, "ahoy_chest_locked", 1, { at = { 3619, 3545, 1 } })
        t.exec("inv.scrap1", t.inv.await, "ahoy_map_scrap_1", 1, 5)
        t.exec("openSecondChest.doorOut", t.player.pass_door, CAPTAIN_OUT)

        -- searchChestForLobster + killLobster + searchChestAfterLobster: the
        -- hull chest answers only once the captain's chest is open
        -- (ahoy_book.rs2 [oploc1,ahoy_chest_closed] toyboat_chest2_open).
        t.exec("goDownToHull", t.player.climb, { loc = "ahoy_ghostship_laddertop", op = 1, op_name = "Climb-down",
            at = { 3613, 3543, 1 }, src = { 3614, 3543 }, dest = { 3612, 3543, 0 } })
        t.exec("searchChestForLobster", t.player.click_loc, "ahoy_chest_closed", 1, { at = { 3618, 3542, 0 } })
        -- The lobster is npc_add'ed after the mesbox (ahoy_book.rs2
        -- ~ahoy_spawn_lobster): play the page, then wait for the spawn.
        t.exec("searchChestForLobster.mes", t.chat.play, { "mesbox:You are attacked by a giant lobster!" })
        local lob_res, lob_row = t.npc.await_present("giant_lobster", 10, 15)
        t.step("lobster.found", lob_res == "ok" and "PASS" or "FAIL", tostring(lob_res) .. " " .. tostring(lob_row))
        if lob_res ~= "ok" then
            t.blocked("killLobster: giant_lobster not found in the pool after the chest search")
            return
        end
        local function player_hp()
            local hr, hs = t.skill.read("hitpoints")
            return hr == "ok" and hs and (hs.current or hs.level) or nil
        end
        local lob_hp0 = player_hp()
        t.exec("killLobster.attack", t.player.attack, "giant_lobster", 2, 20)
        -- 30 hp at attack/strength 40 with a rune scimitar: ~75 ticks
        -- (build/quest_gate/s26gh_lob2: dead after 76). 60 was too short.
        local _, lob_detail = t.exec("killLobster.dead", t.npc.await_dead_engaged, 150, 6,
            { eat = { item = "lobster", below = 16 } })
        local lob_hp1 = player_hp()
        local _, food_left = t.inv.count("lobster")
        local eaten = 4 - (tonumber(food_left) or 4)
        -- Graded (seam ghostsahoy_giant_lobster_never_swings): the lobster is an aggressive
        -- melee monster that keeps swinging until it dies (wiki Giant_lobster_(Ghosts_Ahoy)
        -- oldid 15272821: Stab, speed 4, max hit 4). Before the fix it hit once and stopped
        -- (b56 sampler, hp 23/40 flat).
        t.expect("killLobster.player_hp",
            (lob_hp0 and lob_hp1 and (lob_hp0 - lob_hp1 >= 2 or eaten >= 1)) and "ok" or "refused",
            "player hp across the lobster fight " .. tostring(lob_hp0) .. " -> " .. tostring(lob_hp1)
                .. " /40, lobsters eaten " .. eaten .. " (eaten below 16) -- want a drop of >= 2 or a meal: the lobster keeps swinging")
        -- The fight's margin: the lowest hitpoints the eat watch saw, at
        -- least a quarter of 40, AND food left.
        local lob_low = tonumber(tostring(lob_detail):match("lowest hp (%d+)/"))
        t.check("killLobster.margin", lob_low ~= nil and lob_low >= 10 and tonumber(food_left) ~= nil and tonumber(food_left) >= 1,
            "lowest hp " .. tostring(lob_low) .. "/40 across the fight, lobsters left " .. tostring(food_left)
                .. " of 4 (margin: lowest hp >= 10, a quarter of 40, AND food left)")

        t.exec("searchChestAfterLobster", t.player.click_loc, "ahoy_chest_open", 1)
        t.exec("inv.scrap3", t.inv.await, "ahoy_map_scrap_3", 1, 5)

        -- goUpToDeck + goAcrossPlank: back up, over the gangplank onto the
        -- rocks (^ahoy_gangplank_rock_coord 0_56_55_20_30 = 3604,3550,0).
        t.exec("goUpToDeck", t.player.climb, { loc = "ahoy_ghostship_ladder", op = 1, op_name = "Climb-up",
            at = { 3613, 3543, 0 }, src = { 3612, 3543 }, dest = { 3614, 3543, 1 } })
        t.exec("goAcrossPlank", t.player.climb, { loc = "ahoy_gangplank_shipwreck_on", op = 1, op_name = "Cross",
            at = { 3605, 3546, 1 }, dest = { 3604, 3550, 0 } })
        -- openThirdChest: jump the rock chain (Quest Helper setLinePoints
        -- 3604,3550 -> ... -> 3605,3564; the copies are m56_55.jl2's 16115
        -- rows). Each rock is pressed by its tile and the jump graded on the
        -- player landing ON it ([aploc1,ahoy_rock_invisible] p_teleport
        -- loc_coord). Not t.player.cross_trap: the rocks sit on raw cache
        -- level 1 over a bridge column while the player stands on plane 0,
        -- and cross_trap presses and grades on one level.
        local rocks_out = {
            { 3602, 3550 }, { 3599, 3552 }, { 3597, 3552 }, { 3595, 3554 }, { 3595, 3556 },
            { 3597, 3559 }, { 3597, 3561 }, { 3599, 3564 }, { 3601, 3564 },
        }
        local function jump_chain(label, chain)
            for i, rock in ipairs(chain) do
                local name = label .. "." .. i
                local br, before = t.world.tile()
                t.exec(name, t.player.click_loc, "ahoy_rock_invisible", 1, { at = { rock[1], rock[2] } })
                -- p_teleport(loc_coord) lands a tick behind the jump's chat line.
                await_tile(function(tt) return tt.x == rock[1] and tt.z == rock[2] end, 4, name)
                local tr, tile = t.world.tile()
                local landed = tr == "ok" and tile.x == rock[1] and tile.z == rock[2]
                t.check(name .. ".landed", landed and br == "ok" and (before.x ~= rock[1] or before.z ~= rock[2]),
                    "from " .. tile_text(before) .. " onto the rock at " .. rock[1] .. "," .. rock[2] .. " -> " .. tile_text(tile))
                if not landed then
                    return false
                end
            end
            return true
        end
        if not jump_chain("rockjump", rocks_out) then
            t.blocked("rockjump: a jump did not land on its named rock")
            return
        end
        t.exec("openThirdChest", t.player.click_loc, "ahoy_chest_closed", 1)
        t.exec("inv.scrap2", t.inv.await, "ahoy_map_scrap_2", 1, 5)

        -- Back the same way: the chain in reverse to the plank's rock.
        local rocks_back = {
            { 3599, 3564 }, { 3597, 3561 }, { 3597, 3559 }, { 3595, 3556 }, { 3595, 3554 },
            { 3597, 3552 }, { 3599, 3552 }, { 3602, 3550 }, { 3604, 3550 },
        }
        if not jump_chain("rockback", rocks_back) then
            t.blocked("rockback: a jump did not land on its named rock")
            return
        end
        -- The off-plank is a raw level-1 loc pressed from plane 0 (the same
        -- bridge column as the rocks): graded on the two tiles.
        local pb_r, pb_before = t.world.tile()
        local pc_r, pc_d = t.player.click_loc("ahoy_gangplank_shipwreck_off", 1)
        await_tile(function(tt) return tt.level == 1 and tt.x == 3605 and tt.z == 3546 end, 8, "goAcrossPlankBack")
        local pa_r, pa_after = t.world.tile()
        t.check("goAcrossPlankBack", pb_r == "ok" and pb_before.x == 3604 and pb_before.z == 3550 and pb_before.level == 0
            and pa_r == "ok" and pa_after.x == 3605 and pa_after.z == 3546 and pa_after.level == 1,
            "from " .. tile_text(pb_before) .. " click_loc(ahoy_gangplank_shipwreck_off) -> " .. tostring(pc_r) .. " "
                .. tostring(pc_d) .. "; landed " .. tile_text(pa_after)
                .. " (want 3604,3550,0 -> 3605,3546,1, ^ahoy_gangplank_ship_coord)")
        t.exec("goDownFromDeck", t.player.climb, { loc = "ahoy_ghostship_laddertop", op = 1, op_name = "Climb-down",
            at = { 3613, 3543, 1 }, src = { 3614, 3543 }, dest = { 3612, 3543, 0 } })

        t.exec("useMapsTogether", t.player.use_item_on_item, "ahoy_map_scrap_1", "ahoy_map_scrap_2")
        t.exec("inv.map_complete", t.inv.expect_has, "ahoy_map_complete", 1)
        t.check("useMapsTogether.scrapsUsed", t.inv.expect_absent("ahoy_map_scrap_1") == "ok"
            and t.inv.expect_absent("ahoy_map_scrap_2") == "ok" and t.inv.expect_absent("ahoy_map_scrap_3") == "ok",
            "the three scraps left the pack (want them joined into the treasure map)")

        -- Out of the hull, into the town, to the Phasmatys dock, sail.
        enter_phas("enterPhasForDigging", 2)
        t.exec("goto-takeRowingBoat", t.player.goto_tile, 3703, 3487, 0)
        local fare_r0, fare0 = t.inv.count("ectotoken")
        t.exec("takeRowingBoat", t.player.talk_to, "ahoy_ghost_captain_1", 1)
        t.exec("takeRowingBoat-dialog", t.chat.play, {
            "choose:Pay 25 ecto-tokens for a return trip.",
            "player:Take me to Dragontooth Island,",
            "npc:Hold on tight.",
        })
        await_tile(function(tt) return tt.x == 3791 and tt.z == 3559 end, 4, "takeRowingBoat")
        local dt_res, dt_tile = t.world.tile()
        local fare_r1, fare1 = t.inv.count("ectotoken")
        t.check("world.dragontooth", dt_res == "ok" and dt_tile.x == 3791 and dt_tile.z == 3559 and dt_tile.level == 0
            and fare_r0 == "ok" and fare_r1 == "ok" and fare0 - fare1 == 25,
            "landed " .. tile_text(dt_tile) .. " (want 3791,3559,0 ^ahoy_dragontooth_dock_coord); ecto-tokens "
                .. tostring(fare0) .. " -> " .. tostring(fare1) .. " (want -25)")

        -- Walk the island (Quest Helper digForBook DigStep 3803,3530, south
        -- of the dock), not a ::goto. walk_to answers a bare ok (trap f):
        -- check the tile it reached.
        local walk_res = t.player.walk_to(3803, 3530)
        local dig_res, dig_tile = t.world.tile()
        t.check("walk.digspot", walk_res == "ok" and dig_res == "ok" and dig_tile ~= nil
            and dig_tile.x == 3803 and dig_tile.z == 3530,
            "walk_to " .. tostring(walk_res) .. "; at " .. tile_text(dig_tile))
        -- The spade dig has no named driver verb (DigStep) -- drive it
        -- through the real held-item op on the spade itself (general_use/
        -- spade.rs2's dispatch chain reaches ~ahoy_try_dig).
        t.exec("digForBook", t.player.inv_op, "spade", 1)
        t.exec("inv.book", t.inv.await, "ahoy_book_of_haricanto", 1, 5)

        -- Free return: walk back to the Ghost captain at the island's dock
        -- (m59_55.spawn 3792,3560; Quest Helper returnToPhas 3791,3559).
        local back_res = t.player.walk_to(3791, 3558)
        local back_tr, back_tile = t.world.tile()
        t.check("walk.dragontooth_dock", back_res == "ok" and back_tr == "ok" and back_tile ~= nil
            and back_tile.x == 3791 and back_tile.z == 3558,
            "walk_to " .. tostring(back_res) .. "; at " .. tile_text(back_tile))
        t.exec("returnToPhas", t.player.talk_to, "ahoy_ghost_captain_1", 1)
        t.exec("returnToPhas-dialog", t.chat.play, {
            "player:Take me back to Port Phasmatys, please.",
            "npc:Righto.",
        })
        await_tile(function(tt) return tt.x == 3703 and tt.z == 3487 end, 4, "returnToPhas")
        local rp_res, rp_tile = t.world.tile()
        t.check("returnToPhas.landed", rp_res == "ok" and rp_tile.x == 3703 and rp_tile.z == 3487 and rp_tile.level == 0,
            "landed " .. tile_text(rp_tile) .. " (want 3703,3487,0 ^ahoy_phas_dock_coord, inside the town)")

        -- =================================================================
        -- Independent hand-ins (robes, manual, book all carried at once),
        -- ghostspeak amulet, enchantment, Necrovarus, completion.
        -- =================================================================
        -- Landed on the Phasmatys dock, inside the town: out by the barrier.
        exit_phas("exitPhasForCroneAgain")
        crone_in("returnToCrone")
        t.exec("returnToCrone", t.player.talk_to, "ahoy_crone", 1)
        t.exec("returnToCrone-dialog", t.chat.play, {
            "npc:The Book of Haricanto",
            "npc:The translation manual will let me read the rite.",
            "npc:Necrovarus's own robes.",
            "npc:Now bring me an ordinary ghostspeak amulet",
        })
        t.exec("quest.stage.need_amulet", t.quest.expect_stage, "need_amulet")

        t.exec("bringCroneAmulet", t.player.talk_to, "ahoy_crone", 1)
        t.exec("bringCroneAmulet-dialog", t.chat.play, {
            "mesbox:The Old Crone dons the robes",
            "npc:your amulet is enchanted",
        })
        t.exec("quest.stage.amulet_enchanted", t.quest.expect_stage, "amulet_enchanted")
        t.exec("inv.enchanted_amulet", t.inv.expect_has, "amulet_of_ghostspeak_enchanted", 1)
        crone_out("bringCroneAmulet")

        t.exec("equip.enchanted_amulet", t.player.equip, "amulet_of_ghostspeak_enchanted")

        -- talkToNecroAfterCurse: from open ground north of the barrier;
        -- talk_to walks onto his open ground floor.
        t.exec("goto-talkToNecroAfterCurse", t.player.goto_tile, 3660, 3509, 0)
        t.exec("talkToNecroAfterCurse", t.player.talk_to, "ahoy_necrovarus", 1)
        t.exec("talkToNecroAfterCurse-dialog", t.chat.play, {
            "player:Let any ghost who so wishes pass on",
            "mesbox:A beam of green light radiates out from your amulet",
            "npc:My power over this town",
            "mesbox:Necrovarus's hold over Port Phasmatys shatters.",
        })
        t.exec("quest.stage.necrovarus_defeated", t.quest.expect_stage, "necrovarus_defeated")

        local snap_res, snap = t.skill.snapshot()
        t.step("skill.snapshot", (snap_res == "ok" and type(snap) == "table") and "PASS" or "FAIL", "before hand-in")

        -- enterPhasFinal: free once Necrovarus is commanded (Quest Helper
        -- GhostsAhoy.java:421, ahoy_hub.rs2 necrovarus_defeated branch).
        enter_phas("enterPhasFinal", 0)
        t.exec("goto-talkToVelorinaFinal", t.player.goto_tile, 3678, 3510, 0)
        t.exec("talkToVelorinaFinal", t.player.talk_to, "ahoy_velorina", 1)
        t.exec("talkToVelorinaFinal-dialog", t.chat.play, {
            "player:Necrovarus's curse is broken",
            "npc:Thank you, thank you a thousand times over.",
            "npc:Please, take this Ectophial",
        })
        -- The scroll's own total: 2 prerequisite points + Ghosts Ahoy's 2 (read, no shot --
        -- expect_complete photographs this same scroll as quest.scroll).
        local sc_res, sc = t.scroll.title()
        local sc_points = type(sc) == "table" and tostring(sc.points) or tostring(sc)
        t.expect("quest.scroll_total", (sc_res == "ok" and sc_points:find("Total Quest Points: 4", 1, true)) and "ok" or "refused",
            "scroll " .. tostring(sc_res) .. ": " .. sc_points .. " (want Total Quest Points: 4)")
        t.quest.expect_complete()
        local qp1_res, qp1 = t.var.varp("varp101_qp")
        t.expect("quest.qp_total", (qp1_res == "ok" and tonumber(qp1) == 4) and "ok" or "refused",
            "varp101_qp after completion = " .. tostring(qp1) .. " (want 4)")

        t.exec("reward.prayer_xp", t.skill.expect_gain, "prayer", 2400, snap)
        t.exec("reward.ectophial", t.inv.expect_has, "ectophial", 1)

        -- Postquest: the completed barrier resolves to
        -- ahoy_town_barrier_post_quest, whose one op is op4 "Pass" (all.loc;
        -- wiki Energy Barrier oldid 15134513): out free. (Back in by op4
        -- opens the guard's thanks first -- a dialogue cross_gate cannot
        -- answer -- so the test leaves by the barrier and comes home by the
        -- Ectophial.)
        local tok_res4, tok_before3 = t.inv.count("ectotoken")
        t.exec("goto-barrierPostquest", t.player.goto_tile, 3660, 3507, 0)
        t.exec("barrierPostquest", t.player.cross_gate, { loc = "ahoy_town_barrier_multi", op = 4, at = { 3659, 3508, 0 },
            near = { 3660, 3507 }, far_ok = function(tile) return tile.z > 3508 end,
            far_desc = "outside Port Phasmatys, north of the barrier, z > 3508" })
        local tok_res5, tok_after3 = t.inv.count("ectotoken")
        t.check("barrierPostquest.free", tok_res4 == "ok" and tok_res5 == "ok" and tok_before3 == tok_after3,
            "ecto-tokens " .. tostring(tok_before3) .. " -> " .. tostring(tok_after3))

        t.exec("ectophial.empty", t.player.inv_op, "ectophial", 1)
        t.exec("inv.ectophial_empty", t.inv.await, "ectophial_empty", 1, 5)
        await_tile(function(tt) return math.abs(tt.x - 3660) <= 2 and math.abs(tt.z - 3522) <= 2 end, 10, "ectophial")
        local ec_res, ec_tile = t.world.tile()
        t.check("world.ectophial_arrival", ec_res == "ok" and ec_tile.level == 0 and math.abs(ec_tile.x - 3660) <= 2
            and math.abs(ec_tile.z - 3522) <= 2,
            "after the Ectophial: " .. tile_text(ec_tile) .. " (want within 2 of 3660,3522,0 ^ahoy_ectophial_coord)")
        t.exec("inv.ectophial_refilled", t.inv.await, "ectophial", 1, 5)

        t.finish(0)
    end,
}
