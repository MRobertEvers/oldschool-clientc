-- Ghosts Ahoy, re-driven 2026-11-02 along RSProx index 4462 (rev 241), which plays the whole
-- quest: docs/rsprox-analysis/quests/ghosts_ahoy.md. Previously authored against OSRS-Content quest_ghostsahoy/scripts/
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
        "::give needle 1", -- the boat's flag: silk, needle, thread and a knife (RSProx 4462 [4706])
        "::give thread 1",
        "::give knife 1",
        "::give spade 1",
        "::give oak_longbow 1",
        "::give bucket_ectoplasm 1", -- dyes the bedsheet green
        "::give bucket_milk 1", -- nettle tea
        "::give logs 1",
        "::give tinderbox 1", -- lights the fire the nettle-water is boiled over
        "::give bowl_water 1", -- nettle tea (a filled bowl is a brought-along supply, not quest deliverable)
        "::give leather_gloves 1", -- worn before picking nettles
        "::give reddye 1",
        "::give bluedye 1",
        "::give yellowdye 1",
        "::give orangedye 1",
        "::give greendye 1",
        "::give purpledye 1",
        "::give coins 2000", -- Runedraw stakes, 25 a game (4462: 7 games to four wins)
                        "::give rune_scimitar 1", -- worn for the giant lobster
        "::give lobster 4", -- food for the giant lobster: it swings back now (max hit 4)
        "::setlevel agility 25",
        "::setlevel firemaking 50", -- lights the logs at once (the fire is only the tea's heat)
        "::setlevel cooking 60", -- the nettle tea boils over below this sometimes (fitted odds); the quest needs 20
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
        t.ticklog.start() -- the wire audit against RSProx index 4462

        -- Both prerequisites PAID (1 qp each, Quest Helper GhostsAhoy.java requirements:
        -- Priest in Peril + The Restless Ghost): qp 2 before the quest, and neither
        -- ::complete took its "already complete" branch.
        local qp0_res, qp0 = t.var.varp("varp101_qp")
        t.expect("prereq.qp_before", (qp0_res == "ok" and tonumber(qp0) == 2) and "ok" or "refused",
            "varp101_qp after setup = " .. tostring(qp0) .. " (want 2: Restless Ghost 1 + Priest in Peril 1)")
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
            "npc:Into Morytania? Yes, you can.",
            "npc:I must warn you though, Morytania is an evil land.",
            "npc:Worst of all are the vampyres",
            "player:I'll be careful. Anything else I should know?",
            "npc:The first settlement you'll likely come across is Canifis",
            "npc:You should keep that dagger I gave you to hand.",
            "player:I see. Thanks.",
            "npc:One last thing.",
            "player:The Myreque?",
            "npc:Yes. They're a band of freedom fighters.",
            "npc:Veliaf, the leader of their Mort Myre group",
            "player:I'll keep an eye out for them.",
            "npc:Right, I think that's everything.",
            "mesbox:Drezel blesses you",
            "npc:Good luck out there. Stay safe.",
            "choose:And you. See you later.",
            "player:And you. See you later.",
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

        -- Pages and choices below are RSProx index 4462's, in its order:
        -- Velorina [957..1002], Necrovarus [1025..1033], Velorina [1067..1090].
        enter_phas("enterPhas", 2)
        t.exec("goto-talkToVelorina", t.player.goto_tile, 3678, 3510, 0)
        t.exec("talkToVelorina", t.player.talk_to, "ahoy_velorina", 1)
        t.exec("talkToVelorina-dialog", t.chat.play, {
            "npc:Take pity on me, please",
            "choose:Why, what is the matter?",
            "player:Why, what is the matter?",
            "npc:Oh, I'm sorry - I was just wailing",
            "player:No, that's okay",
            "npc:Do you know the history of our town?",
            "choose:Yes, I do. It is a very sad story.",
            "player:Yes, I do. It is a very sad story.",
            "npc:Would you help us obtain our release",
            "choose:Yes.",
            "player:Yes, of course I will.",
            "npc:Oh, thank you!",
            "npc:Necrovarus will not listen",
            "npc:If he declines, there may yet be another way.",
        })
        t.exec("quest.stage.talked_velorina", t.quest.expect_stage, "talked_velorina")

        exit_phas("exitPhas")
        t.exec("talkToNecrovarus", t.player.talk_to, "ahoy_necrovarus", 1)
        t.exec("talkToNecrovarus-dialog", t.chat.play, {
            "player:I must speak with you on behalf of Velorina.",
            "npc:You dare to speak that name",
            "player:She wants to pass-",
            "npc:Silence!!",
            "player:But she-",
            "npc:Get out of my sight!!",
        })
        t.exec("quest.stage.talked_necrovarus", t.quest.expect_stage, "talked_necrovarus")

        enter_phas("enterPhasAfterNecro", 2)
        t.exec("goto-talkToVelorinaAfterNecro", t.player.goto_tile, 3678, 3510, 0)
        t.exec("talkToVelorinaAfterNecro", t.player.talk_to, "ahoy_velorina", 1)
        t.exec("talkToVelorinaAfterNecro-dialog", t.chat.play, {
            "player:I'm sorry, but Necrovarus will not let you go.",
            "npc:I feared as much.",
            "player:You spoke of another way.",
            "npc:It is only a small chance.",
            "npc:She fled before the Ectofuntus",
            "choose:If it was such a long time ago, won't she be dead already?",
            "player:If it was such a long time ago",
            "npc:She was a friend of mine.",
            "choose:Do you know where this woman can be found?",
            "player:Do you know where this woman can be found?",
            "npc:I have a vision of a small wooden shack",
        })
        t.exec("quest.stage.told_of_crone", t.quest.expect_stage, "told_of_crone")

        -- The crone [2015..2043]: she asks for nettle tea.
        exit_phas("exitPhasForCrone")
        crone_in("talkToCrone")
        t.exec("talkToCrone", t.player.talk_to, "ahoy_crone", 1)
        t.exec("talkToCrone-dialog", t.chat.play, {
            "player:Were you once a disciple of Necrovarus",
            "npc:I don't remember;",
            "player:Is there anything that can help",
            "npc:Yes, I would love a nice hot cup of nettle tea.",
            "player:Do you know where I can find nettles",
            "npc:I believe they grow wild in the Haunted Forest.",
        })
        t.exec("var.tea_asked", t.var.expect, "varb211_ahoy_subquest_nettletea", 1)
        crone_out("talkToCrone")

        -- Nettles (gloves worn), the bowl, the fire.
        t.exec("goto-pickNettles", t.player.goto_tile, 3621, 3511, 0)
        t.exec("pickNettles", t.player.click_loc, "nettles", 1)
        t.exec("inv.nettles_picked", t.inv.await, "nettles_picked", 1, 6)
        t.exec("nettles.steep", t.player.use_item_on_item, "bowl_water", "nettles_picked")
        t.exec("inv.nettlewater", t.inv.expect_has, "bowl_nettlewater", 1)
        t.exec("lightFire", t.player.use_item_on_item, "tinderbox", "logs")
        local fire, fire_d = "not_found", nil
        local fire_tries = 0
        while fire ~= "ok" and fire_tries < 40 do
            fire, fire_d = t.world.loc_near("fire", 10)
            if fire ~= "ok" then
                t.ticks(1)
                fire_tries = fire_tries + 1
            end
        end
        t.step("world.fire_near", fire == "ok" and "PASS" or "FAIL", tostring(fire_d and fire_d.tile_x))
        t.exec("useTeaOnFire", t.player.use_on, "bowl_nettlewater", fire_d)
        t.exec("inv.nettletea", t.inv.await, "bowl_nettletea", 1, 5)

        -- [2091..2120]: tea not in her cup -> her special cup.
        crone_in("talkToCroneTea")
        t.exec("talkToCroneTea", t.player.talk_to, "ahoy_crone", 1)
        t.exec("talkToCroneTea-dialog", t.chat.play, {
            "player:Were you once a disciple of Necrovarus",
            "npc:I don't remember;",
            "player:Is there anything that can help",
            "npc:Yes, I would love a nice hot cup of nettle tea.",
            "player:Here's some tea for you, like you asked.",
            "npc:Yes, but it's not in my special cup!",
            "player:I see. Can I have this special cup, then?",
            "objbox:The old crone gives you her special cup.",
        })
        t.exec("inv.chinacup", t.inv.expect_has, "chinacup_empty", 1)
        t.exec("pourTea", t.player.use_item_on_item, "bowl_nettletea", "chinacup_empty")
        t.exec("inv.tea_in_cup", t.inv.expect_has, "chinacup_of_nettletea", 1)
        t.exec("useMilkOnTea", t.player.use_item_on_item, "bucket_milk", "chinacup_of_nettletea")
        t.exec("inv.milky_tea", t.inv.expect_has, "chinacup_of_nettletea_milky", 1)

        -- [2309..2357] the milky tea, then the menu and the toy boat [4335..4364].
        t.exec("talkToCroneMilky", t.player.talk_to, "ahoy_crone", 1)
        t.exec("talkToCroneMilky-dialog", t.chat.play, {
            "player:Were you once a disciple of Necrovarus",
            "npc:I don't remember;",
            "player:Is there anything that can help",
            "npc:Yes, I would love a nice hot cup of nettle tea.",
            "player:Here's a lovely cup of milky tea",
            "mesbox:As the old woman drinks the cup of milky tea",
            "npc:Ah, that's better.",
            "player:I have come from Velorina.",
            "npc:Velorina? That name sounds familiar...",
            "npc:Yes, Velorina was once a very good friend!",
            "player:She's a ghost, I'm afraid.",
            "npc:They are all dead, then.",
            "player:She and many others want to pass on",
            "npc:There might be one way...",
            "npc:Do you have such a thing as a ghostspeak amulet?",
            "player:Yes, I'm wearing one right now.",
            "npc:Well, that's a stroke of luck.",
            "npc:There is an enchantment",
            "player:What do you need to perform the enchantment?",
            "npc:Necrovarus had a magical robe",
            "npc:All his rituals came from a book",
            "npc:I cannot read the strange letters",
        })
        t.exec("quest.stage.gathering_items", t.quest.expect_stage, "gathering_items")
        t.exec("talkToCroneSon", t.player.talk_to, "ahoy_crone", 1)
        t.exec("talkToCroneSon-dialog", t.chat.play, {
            "choose:You are doing so much for me - is there anything I can do for you?",
            "player:You are doing so much for me",
            "npc:I have lived here on my own",
            "npc:He was about twelve years old",
            "player:That's the second saddest story",
            "npc:If you ever see him, please give him this",
            "objbox:The old woman gives you a toy boat.",
            "player:Was this his boat?",
            "npc:Yes, he made it himself.",
            "player:If I find him, I will pass it on.",
        })
        t.exec("inv.toyboat", t.inv.expect_has, "ahoy_toy_boat", 1)
        crone_out("talkToCroneSon")

        -- The manual: Ak-Haranu [2823..2852], Robin's Runedraw [2870..3097].
        enter_phas("enterPhasForManual", 2)
        t.exec("goto-talkToAkHaranu", t.player.goto_tile, 3689, 3499, 0)
        t.exec("talkToAkHaranu", t.player.talk_to, "ahoy_akharanu_multi", 1)
        t.exec("talkToAkHaranu-dialog", t.chat.play, {
            "player:It's nice to see a human face around here.",
            "npc:My name Ak-Haranu.",
            "player:You come from the lands of the East?",
            "npc:Ak-Haranu may help you.",
            "player:How much do you want for it?",
            "npc:Ak-Haranu not want money",
            "player:What would you like me to do?",
            "npc:Please get Master Bowman sign an oak longbow",
            "choose:Okay, wait here - I'll get you your bow.",
            "player:Okay, wait here",
        })
        t.exec("var.bow_agreed", t.var.expect, "varb212_ahoy_subquest_bow", 1)

        t.exec("goto-talkToRobin", t.player.goto_tile, 3669, 3497, 0)
        t.exec("talkToRobin.doorIn", t.player.pass_door, INN_IN)
        t.exec("talkToRobin", t.player.talk_to, "ahoy_robin", 1)
        t.exec("talkToRobin-dialog", t.chat.play, {
            "player:Would you sign this oak longbow for me?",
            "npc:I'm sorry, I don't sign autographs.",
            "npc:While you're here, though",
            "choose:Yes, I'll give you a game.",
            "player:Yes, I'll give you a game.",
        })
        -- Runedraw (interface 9): draw until the game ends, then the pages,
        -- and play again from Robin's menu until he signs (bow 7).
        local games = 0
        while games < 60 do
            local br, bow = t.var.server("varb212_ahoy_subquest_bow")
            if br == "ok" and bow ~= nil and bow >= 7 then
                break
            end
            games = games + 1
            local orr = t.ui.await_open("ahoy_runedraw", 10)
            if orr ~= "ok" then
                t.blocked("runedraw: the board never opened (game " .. games .. ")")
                return
            end
            -- Draw every five ticks while the board is up (Robin answers within
            -- four, [2904..2919]); a game ends on a Death rune.
            local draws = 0
            while draws < 12 do
                local mr, open = t.ui.is_modal()
                if mr ~= "ok" or open ~= true then
                    break
                end
                local wr, w = t.ui.widget("ahoy_runedraw:runedraw_btn_draw")
                t.ui.invoke(w, 1)
                draws = draws + 1
                t.ticks(5)
            end
            t.note("runedraw game " .. games .. ": " .. draws .. " draw press(es)")
            -- The board closes four ticks after the last rune; then the pages.
            local guard = 0
            while guard < 30 do
                guard = guard + 1
                local k = t.chat.kind()
                local _, kt = t.chat.text()
                t.note("runedraw after game " .. games .. ": " .. tostring(k) .. " " .. tostring(kt):sub(1, 60))
                if k == "options" then
                    local cr = t.chat.choose("Yes, I'll give you a game.")
                    if cr ~= "ok" then
                        t.blocked("runedraw: no 'Yes, I'll give you a game.' row")
                        return
                    end
                    t.chat.continue_()
                    break
                elseif k == "npc" or k == "player" or k == "objbox" or k == "mesbox" then
                    t.chat.continue_()
                else
                    local b2r, b2 = t.var.server("varb212_ahoy_subquest_bow")
                    if b2r == "ok" and b2 ~= nil and b2 >= 7 then
                        break
                    end
                    t.ticks(1)
                end
            end
        end
        t.exec("var.bow_signed", t.var.expect, "varb212_ahoy_subquest_bow", 7)
        t.exec("inv.signed_bow", t.inv.expect_has, "oak_longbow_signed", 1)
        t.step("runedraw.games", "PASS", games .. " game(s) to four wins")
        t.exec("talkToRobin.doorOut", t.player.pass_door, INN_OUT)

        t.exec("goto-bringBowToAkHaranu", t.player.goto_tile, 3689, 3499, 0)
        t.exec("bringBowToAkHaranu", t.player.talk_to, "ahoy_akharanu_multi", 1)
        t.exec("bringBowToAkHaranu-dialog", t.chat.play, {
            "player:I have your signed longbow for you.",
            "npc:Ah, can it be true?",
            "player:He was more than happy to oblige (cough).",
            "objbox:Ak-Haranu gives you a translation manual",
            "npc:May honour be bestowed upon you",
        })
        t.exec("var.bow_traded", t.var.expect, "varb212_ahoy_subquest_bow", 8)
        t.exec("inv.manual", t.inv.expect_has, "ahoy_translation_manual", 1)

        -- The robes: the innkeeper's sheet [3252..3271], the ectoplasm [5751],
        -- Gravingas [3404..3466] and ten signatures [5765..5913].
        t.exec("goto-talkToInnkeeper", t.player.goto_tile, 3669, 3497, 0)
        t.exec("talkToInnkeeper.doorIn", t.player.pass_door, INN_IN)
        t.exec("talkToInnkeeper", t.player.talk_to, "ahoy_ghost_innkeeper", 1)
        t.exec("talkToInnkeeper-dialog", t.chat.play, {
            "player:Hello there!",
            "npc:Greetings, traveller.",
            "choose:Do you have any jobs I can do?",
            "player:Do you have any jobs I can do?",
            "npc:Yes, actually, I do.",
            "choose:Yes, I'd be delighted.",
            "player:Yes, I'd be delighted.",
            "npc:Oh, thank you.",
        })
        t.exec("inv.bedsheet", t.inv.expect_has, "ahoy_bedsheet", 1)
        t.exec("talkToInnkeeper.doorOut", t.player.pass_door, INN_OUT)
        t.exec("useSlimeOnSheet", t.player.use_item_on_item, "bucket_ectoplasm", "ahoy_bedsheet")
        t.exec("inv.bedsheetgreen", t.inv.expect_has, "ahoy_bedsheetgreen", 1)

        t.exec("goto-talkToGravingas", t.player.goto_tile, 3660, 3500, 0)
        t.exec("talkToGravingas", t.player.talk_to, "protester_standardspeak_multi", 1)
        t.exec("talkToGravingas-dialog", t.chat.play, {
            "npc:Will you join with me and protest",
            "choose:After hearing Velorina's story I will be happy to help out.",
            "player:After hearing Velorina's story",
            "npc:Excellent, excellent!!",
        })
        t.exec("inv.petition", t.inv.expect_has, "ahoy_petition", 1)
        t.exec("equip.bedsheet", t.player.equip, "ahoy_bedsheetgreen")

        local asks = 0
        local tried = {}
        while asks < 80 do
            local cr, cnt = t.var.server("varb209_ahoy_signaturecounter")
            if cr == "ok" and cnt ~= nil and cnt >= 11 then
                break
            end
            asks = asks + 1
            local nr, _, rows = t.npc.tiles("ahoy_ghost_villager", 15)
            local pick = nil
            if nr == "ok" and rows then
                for _, row in ipairs(rows) do
                    if tried[row.slot] == nil or tried[row.slot] < asks - 3 then
                        pick = row
                        break
                    end
                end
            end
            if pick == nil then
                t.note("petition ask " .. asks .. ": no villager (" .. tostring(nr) .. ")")
                t.ticks(3)
            else
                tried[pick.slot] = asks
                local tr, td = t.player.talk_to("ahoy_ghost_villager", 1, { slot = pick.slot })
                t.note("petition ask " .. asks .. ": slot " .. tostring(pick.slot) .. " -> " .. tostring(tr) .. " " .. tostring(td):sub(1, 80))
                local guard = 0
                while tr == "ok" and guard < 12 do
                    guard = guard + 1
                    local k = t.chat.kind()
                    local _, kt = t.chat.text()
                    t.note("  page " .. tostring(k) .. " " .. tostring(kt):sub(1, 70))
                    if k == "options" then
                        t.chat.choose("Okay, if you insist.")
                    elseif k == "npc" or k == "player" or k == "objbox" or k == "mesbox" then
                        t.chat.continue_()
                    else
                        break
                    end
                end
            end
        end
        t.exec("var.petition_full", t.var.expect, "varb209_ahoy_signaturecounter", 11)
        t.step("petition.asks", "PASS", asks .. " ask(s) for ten signatures (4462: 22)")
        t.exec("unequip.bedsheet", t.player.unequip, "ahoy_bedsheetgreen")

        -- [5931..5952]: the petition burnt, the key dropped; the door [6013..6016];
        -- the coffin [6018..6021].
        exit_phas("exitPhasForNecro")
        t.exec("showPetitionToNecro", t.player.talk_to, "ahoy_necrovarus", 1)
        t.exec("showPetitionToNecro-dialog", t.chat.play, {
            "player:Necrovarus, I am presenting you with a petition",
            "npc:A petition you say?",
            "player:It says that the citizens",
            "npc:I see.",
            "player:So you will let them pass over",
            "npc:Oh yes.",
            "player:Really?",
            "npc:NO!!!!!",
            "mesbox:The petition form turns to ashes in your hand.",
            "mesbox:In his rage, Necrovarus drops a key",
        })
        t.exec("var.petition_burnt", t.var.expect, "varb209_ahoy_signaturecounter", 31)
        -- The recorder drops the ashes ([5959]); the pack is full here, so the
        -- empties go too.
        t.exec("dropAshes", t.player.drop, "ashes")
        t.exec("dropBowl", t.player.drop, "bowl_empty")
        t.exec("dropBucket", t.player.drop, "bucket_empty")
        local onr, ond = t.world.obj_near("ahoy_bone_key", 10)
        t.note("bone key on the ground: " .. tostring(onr) .. " " .. tostring(type(ond) == "table" and (tostring(ond.tile_x) .. "," .. tostring(ond.tile_z)) or ond))
        local key_res, key_detail = t.player.click_obj("ahoy_bone_key")
        t.step("takeKey", key_res == "ok" and "PASS" or "FAIL", tostring(key_detail))
        t.exec("inv.bonekey", t.inv.expect_has, "ahoy_bone_key", 1)
        t.exec("goUpFromNecro", t.player.climb, { loc = "ahoy_tower_stairs_lv1", op = 1, op_name = "Climb-up",
            at = { 3666, 3518, 0 }, src = { 3666, 3517 }, dest = { 3666, 3522, 1 } })
        local door_target = t.player.by_symbol("loc", "ahoy_harbour_door")
        t.exec("useKeyOnDoor", t.player.use_on, "ahoy_bone_key", door_target, { at = { 3656, 3514, 1 } })
        t.exec("var.templedoor", t.var.await_server, "varb213_ahoy_templedoor_unlocked", 1, 5)
        t.exec("templeDoorIn", t.player.cross_gate, { loc = "ahoy_harbour_door", at = { 3656, 3514, 1 }, near = { 3655, 3514 },
            far_ok = function(tile) return tile.x >= 3656 end, far_desc = "in Necrovarus's room, x >= 3656" })
        t.exec("openCoffin", t.player.click_loc, "ahoy_coffin", 1)
        t.ticks(2)
        t.exec("searchCoffin", t.player.click_loc, "ahoy_coffin_open", 1)
        t.exec("searchCoffin-dialog", t.chat.play, { "objbox:You take the Robes of Necrovarus" })
        t.exec("inv.robes", t.inv.expect_has, "ahoy_robes_of_necrovarus", 1)
        t.exec("templeDoorOut", t.player.cross_gate, { loc = "ahoy_harbour_door", at = { 3656, 3514, 1 }, near = { 3656, 3514 },
            far_ok = function(tile) return tile.x <= 3655 end, far_desc = "out of Necrovarus's room, x <= 3655" })
        t.exec("goDownFromNecro", t.player.climb, { loc = "ahoy_tower_stairs_lv1_top", op = 1, op_name = "Climb-down",
            at = { 3666, 3520, 1 }, src = { 3666, 3522 }, dest = { 3666, 3517, 0 } })

        -- The book: the boat mended [4706], the mast [4938..5206], the dyes
        -- [5070..5213], the old man [5224..5252], the chests and the lobster
        -- [4738..4828], [5263..5280], the rocks [5296..5394], the map [5341],
        -- the captain [5514..5551], the dig [5629..5632].
        t.exec("repairShip", t.player.inv_op, "ahoy_toy_boat", 1)
        t.exec("repairShip-dialog", t.chat.play, { "objbox:You replace the toy boat's missing flag." })
        t.exec("inv.repaired", t.inv.expect_has, "ahoy_toy_boat_repaired", 1)

        t.exec("goto-lobsterChest", t.player.goto_tile, 3617, 3541, 0)
        t.exec("openLobsterChest", t.player.click_loc, "ahoy_chest_closed", 1, { at = { 3618, 3542, 0 } })
        t.ticks(2)
        t.exec("searchLobsterChest", t.player.click_loc, "ahoy_chest_open", 1, { at = { 3618, 3542, 0 } })
        local lob_res, lob_row = t.npc.await_present("giant_lobster", 10, 15)
        t.step("lobster.found", lob_res == "ok" and "PASS" or "FAIL", tostring(lob_res) .. " " .. tostring(lob_row))
        if lob_res ~= "ok" then
            t.blocked("killLobster: no giant_lobster after the chest search")
            return
        end
        t.exec("killLobster.attack", t.player.attack, "giant_lobster", 2, 20)
        t.exec("killLobster.dead", t.npc.await_dead_engaged, 150, 6, { eat = { item = "lobster", below = 16 } })
        t.exec("var.killed_lobster", t.var.expect, "varb215_ahoy_killed_lobster", 1)
        t.exec("searchLobsterChestAgain", t.player.click_loc, "ahoy_chest_open", 1, { at = { 3618, 3542, 0 } })
        t.exec("searchLobsterChestAgain-dialog", t.chat.play, { "objbox:You find a piece of a map inside the chest." })
        t.exec("inv.scrap2", t.inv.expect_has, "ahoy_map_scrap_2", 1)

        t.exec("goto-goUpToDeckForMast", t.player.goto_tile, 3612, 3543, 0)
        t.exec("goUpToDeck", t.player.climb, { loc = "ahoy_ghostship_ladder", op = 1, op_name = "Climb-up",
            at = { 3613, 3543, 0 }, src = { 3612, 3543 }, dest = { 3614, 3543, 1 } })
        t.exec("goUpToMast", t.player.climb, { loc = "ahoy_ghostship_ladder", op = 1, op_name = "Climb-up",
            at = { 3615, 3541, 1 }, src = { 3614, 3541 }, dest = { 3616, 3541, 2 } })
        local colour_index = { red = 1, blue = 2, yellow = 3, orange = 4, green = 5, purple = 6 }
        local seen = {}
        local mast_presses, windy = 0, 0
        while (seen.top == nil or seen.bottom == nil or seen.skull == nil) and mast_presses < 200 do
            mast_presses = mast_presses + 1
            t.player.click_loc("ahoy_mast", 1, { at = { 3619, 3543, 2 } })
            t.await({ level = function() return t.chat.kind() == "mesbox" end, note = "the mast's box" }, 3)
            local xr, text = t.chat.text()
            local flat = string.lower(tostring(text)):gsub("<[^>]*>", " "):gsub("%s+", " ")
            seen.top = seen.top or flat:match("top half of the flag is coloured (%a+)")
            seen.bottom = seen.bottom or flat:match("bottom half of the flag is coloured (%a+)")
            seen.skull = seen.skull or flat:match("skull emblem is coloured (%a+)")
            if flat:find("too hard", 1, true) then
                windy = windy + 1
            end
            t.chat.continue_()
            t.ticks(1)
        end
        local top_val, bot_val, sku_val = colour_index[seen.top or ""], colour_index[seen.bottom or ""], colour_index[seen.skull or ""]
        t.check("searchMast", top_val ~= nil and bot_val ~= nil and sku_val ~= nil,
            mast_presses .. " look(s), " .. windy .. " in the wind: top=" .. tostring(seen.top) .. " bottom="
                .. tostring(seen.bottom) .. " skull=" .. tostring(seen.skull))
        if top_val == nil or bot_val == nil or sku_val == nil then
            t.blocked("searchMast: the three colours were not all read")
            return
        end
        local dyes = { "reddye", "bluedye", "yellowdye", "orangedye", "greendye", "purpledye" }
        local need = {}
        for _, v in ipairs({ top_val, bot_val, sku_val }) do
            need[v] = (need[v] or 0) + 1
        end
        for v, n in pairs(need) do
            if n > 1 then
                -- lint: kit-give dye repeat: the flag is rolled per player and may repeat a colour
                t.cheat("::give " .. dyes[v] .. " " .. (n - 1))
                t.exec("supply.dye." .. dyes[v], t.inv.await, dyes[v], n, 5)
            end
        end
        local words = { "red", "blue", "yellow", "orange", "green", "purple" }
        t.exec("dyeTop", t.player.use_item_on_item, dyes[top_val], "ahoy_toy_boat_repaired")
        t.exec("dyeTop.dialog", t.chat.play, { "choose:Top half", "objbox:You dye the top of the flag " .. words[top_val] })
        t.exec("dyeBottom", t.player.use_item_on_item, dyes[bot_val], "ahoy_toy_boat_repaired")
        t.exec("dyeBottom.dialog", t.chat.play, { "choose:Bottom half", "objbox:You dye the bottom of the flag " .. words[bot_val] })
        t.exec("dyeSkull", t.player.use_item_on_item, dyes[sku_val], "ahoy_toy_boat_repaired")
        t.exec("dyeSkull.dialog", t.chat.play, { "choose:Skull emblem", "objbox:You dye the skull emblem " .. words[sku_val] })
        t.exec("goDownToMan", t.player.climb, { loc = "ahoy_ghostship_laddertop", op = 1, op_name = "Climb-down",
            at = { 3615, 3541, 2 }, src = { 3616, 3541 }, dest = { 3614, 3541, 1 } })
        local CAPTAIN_IN = harbour_door({ 3615, 3543, 1 }, { 3615, 3543 }, { 3617, 3544 })
        local CAPTAIN_OUT = harbour_door({ 3615, 3543, 1 }, { 3616, 3543 }, { 3614, 3543 })
        -- He wanders into the Captain's room too ([5224] at 3619,3541): its door open first.
        t.exec("captainRoom.doorIn", t.player.pass_door, CAPTAIN_IN)
        t.exec("talkToMan", t.player.talk_to, "ahoy_oldman", 1)
        t.exec("talkToMan-dialog", t.chat.play, {
            "choose:Is this your toy boat?",
            "player:Is this your toy boat?",
            "objbox:The old man inspects the toy boat.",
            "npc:My word - so it is!!!",
            "player:Your mother gave it to me",
            "npc:My mother? She still lives?",
            "player:Yes, in a shack to the west of here.",
            "npc:After all these years ...",
            "player:Can I have the key to the chest, then?",
            "npc:Hang on, let me ask the Captain ...",
            "mesbox:The old man cocks an ear",
            "npc:The Captain says yes.",
            "objbox:The old man gives you the chest key.",
        })
        t.exec("inv.chestkey", t.inv.expect_has, "ahoy_chest_key", 1)
        local chest_target = t.player.by_symbol("loc", "ahoy_chest_locked")
        t.exec("useKeyOnChest", t.player.use_on, "ahoy_chest_key", chest_target, { at = { 3619, 3545, 1 } })
        t.exec("useKeyOnChest-dialog", t.chat.play, { "objbox:You unlock the chest." })
        t.exec("var.chest_unlocked", t.var.expect, "varb214_ahoy_subquest_toyboat", 3)
        t.exec("openCaptainChest", t.player.click_loc, "ahoy_chest_locked", 1, { at = { 3619, 3545, 1 } })
        t.exec("openCaptainChest-dialog", t.chat.play, { "objbox:You find a piece of a map inside the chest." })
        t.exec("inv.scrap1", t.inv.expect_has, "ahoy_map_scrap_1", 1)
        t.exec("captainRoom.doorOut", t.player.pass_door, CAPTAIN_OUT)

        t.exec("goAcrossPlank", t.player.climb, { loc = "ahoy_gangplank_shipwreck_on", op = 1, op_name = "Cross",
            at = { 3605, 3546, 1 }, dest = { 3605, 3548, 0 } })
        local function jump(name, from, to)
            -- Graded on the landing below: the approach jump settles after
            -- click_loc's own window.
            t.player.click_loc("ahoy_rock_invisible", 1, { at = { from[1], from[2] } })
            await_tile(function(tt) return tt.x == to[1] and tt.z == to[2] end, 8, name)
            local tr, tile = t.world.tile()
            t.check(name .. ".landed", tr == "ok" and tile.x == to[1] and tile.z == to[2],
                "from the rock at " .. from[1] .. "," .. from[2] .. " -> " .. tile_text(tile) .. " (want " .. to[1] .. "," .. to[2] .. ")")
        end
        local PAIRS = { { { 3604, 3550 }, { 3602, 3550 } }, { { 3599, 3552 }, { 3597, 3552 } }, { { 3595, 3554 }, { 3595, 3556 } },
            { { 3597, 3559 }, { 3597, 3561 } }, { { 3599, 3564 }, { 3601, 3564 } } }
        for i, p in ipairs(PAIRS) do
            jump("rockjump." .. i, p[1], p[2])
        end
        t.exec("openRockChest", t.player.click_loc, "ahoy_chest_closed", 1, { at = { 3606, 3564 } })
        t.ticks(2)
        t.exec("searchRockChest", t.player.click_loc, "ahoy_chest_open", 1, { at = { 3606, 3564 } })
        t.exec("searchRockChest-dialog", t.chat.play, { "objbox:You find a piece of a map inside the chest." })
        t.exec("inv.scrap3", t.inv.expect_has, "ahoy_map_scrap_3", 1)
        for i = #PAIRS, 1, -1 do
            jump("rockback." .. i, PAIRS[i][2], PAIRS[i][1])
        end
        -- The off-plank is a raw level-1 bridge copy pressed from plane 0
        -- (4462 [5397..5400]): graded on the two tiles.
        t.player.click_loc("ahoy_gangplank_shipwreck_off", 1)
        await_tile(function(tt) return tt.level == 1 and tt.x == 3605 and tt.z == 3545 end, 8, "goAcrossPlankBack")
        local pb_r, pb_tile = t.world.tile()
        t.check("goAcrossPlankBack", pb_r == "ok" and pb_tile.level == 1 and pb_tile.x == 3605 and pb_tile.z == 3545,
            "landed " .. tile_text(pb_tile) .. " (want 3605,3545,1)")
        t.exec("goDownFromDeck", t.player.climb, { loc = "ahoy_ghostship_laddertop", op = 1, op_name = "Climb-down",
            at = { 3609, 3543, 1 }, src = { 3608, 3543 }, dest = { 3610, 3543, 0 } })
        t.exec("useMapsTogether", t.player.use_item_on_item, "ahoy_map_scrap_2", "ahoy_map_scrap_3")
        t.exec("useMapsTogether-dialog", t.chat.play, { "objbox:You piece the three map scraps together" })
        t.exec("inv.map_complete", t.inv.expect_has, "ahoy_map_complete", 1)

        enter_phas("enterPhasForBoat", 2)
        t.exec("goto-takeBoat", t.player.goto_tile, 3702, 3487, 0)
        t.exec("takeBoat", t.player.talk_to, "ahoy_ghost_captain_1", 1)
        t.exec("takeBoat-dialog", t.chat.play, {
            "npc:Would you like to visit Dragontooth Island?",
            "choose:Okay, here's 25 ectotokens. Let's go.",
            "player:Okay, here's 25 ectotokens. Let's go.",
        })
        await_tile(function(tt) return tt.x == 3792 and tt.z == 3559 end, 10, "takeBoat")
        local dt_r, dt_tile = t.world.tile()
        t.check("world.dragontooth", dt_r == "ok" and dt_tile.x == 3792 and dt_tile.z == 3559, "landed " .. tile_text(dt_tile)
            .. " (want 3792,3559,0, [5548])")
        local walk_res = t.player.walk_to(3803, 3530)
        local dig_r, dig_tile = t.world.tile()
        t.check("walk.digspot", dig_r == "ok" and dig_tile.x == 3803 and dig_tile.z == 3530, "walk_to " .. tostring(walk_res)
            .. "; at " .. tile_text(dig_tile))
        t.exec("digForBook", t.player.inv_op, "spade", 1)
        t.exec("digForBook-dialog", t.chat.play, { "objbox:You unearth the Book of Haricanto." })
        t.exec("inv.book", t.inv.expect_has, "ahoy_book_of_haricanto", 1)
        t.player.walk_to(3792, 3558)
        t.exec("returnToPhas", t.player.talk_to, "ahoy_ghost_captain_1", 3)
        await_tile(function(tt) return tt.x == 3702 and tt.z == 3487 end, 10, "returnToPhas")
        local rp_r, rp_tile = t.world.tile()
        t.check("returnToPhas.landed", rp_r == "ok" and rp_tile.x == 3702 and rp_tile.z == 3487, "landed " .. tile_text(rp_tile))

        -- [6154..6163]: everything handed in, the ritual on the worn amulet.
        exit_phas("exitPhasForCroneAgain")
        crone_in("returnToCrone")
        t.exec("returnToCrone", t.player.talk_to, "ahoy_crone", 1)
        t.exec("returnToCrone-dialog", t.chat.play, {
            "player:I have something for you.",
            "npc:Good - the robes of Necrovarus.",
            "npc:The Book of Haricanto!",
            "npc:A translation manual - yes, this should do the job.",
            "npc:Wonderful; that's everything I need.",
            "npc:I will now perform the ritual of enchantment.",
            "objbox:The ghostspeak amulet emits a green glow",
        })
        t.exec("quest.stage.amulet_enchanted", t.quest.expect_stage, "amulet_enchanted")
        local wr, wn = t.ui._worn_count("amulet_of_ghostspeak_enchanted")
        t.check("worn.enchanted_amulet", wr == "ok" and wn == 1, "worn enchanted amulets: " .. tostring(wn) .. " (" .. tostring(wr) .. ")")
        crone_out("returnToCrone")

        -- [6418..6482]: the command.
        t.exec("goto-talkToNecroAfterCurse", t.player.goto_tile, 3660, 3510, 0)
        t.exec("talkToNecroAfterCurse", t.player.talk_to, "ahoy_necrovarus", 1)
        t.exec("talkToNecroAfterCurse-dialog", t.chat.play, {
            "npc:You dare to face me again",
            "player:No, Necrovarus, I am not insane.",
            "choose:Let any ghost who so wishes pass on into the next world.",
            "player:Let any ghost who so wishes",
            "objbox:A beam of intense green light",
            "npc:I - will - let ...",
            "player:Carry on...",
            "npc:... any ...",
            "player:Yes?",
            "npc:... ghost who so wishes ...",
            "player:I think we're almost getting there...",
            "npc:... pass into the next world.",
        })
        t.exec("quest.stage.necrovarus_defeated", t.quest.expect_stage, "necrovarus_defeated")

        local snap_res, snap = t.skill.snapshot()
        t.step("skill.snapshot", (snap_res == "ok" and type(snap) == "table") and "PASS" or "FAIL", "before the reward")
        enter_phas("enterPhasFinal", 0)
        t.exec("goto-talkToVelorinaFinal", t.player.goto_tile, 3678, 3510, 0)
        t.exec("talkToVelorinaFinal", t.player.talk_to, "ahoy_velorina", 1)
        t.exec("talkToVelorinaFinal-dialog", t.chat.play, {
            "npc:You don't need to tell me",
            "player:Only happy to help out.",
            "npc:Here, take this as a thank you",
            "objbox:Velorina gives you a vial of bright green ectoplasm.",
            "npc:This is an Ectophial.",
            "npc:Remember that once the Ectophial has been used",
        })
        t.quest.expect_complete()
        t.exec("reward.prayer_xp", t.skill.expect_gain, "prayer", 2400, snap)
        t.exec("reward.ectophial", t.inv.expect_has, "ectophial", 1)
        t.finish(0)
    end,
}
