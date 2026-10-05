-- Zogre Flesh Eaters. Content: OSRS-Content/osrs239-content/server/scripts/
-- quests/quest_zogreflesheaters/scripts/{zogreflesheaters,zogre_finish}.rs2.
-- Guide: quest-helper ZogreFleshEaters.java (getPanels() is the step ladder
-- driven below; the guide text is paraphrase, the .rs2 dialogue quoted here
-- is verbatim). Progress varp %zogre (0..9, complete=14), side bits
-- %thzfe_prismsearch (coffin 0 locked/1 unlocked/3 open),
-- %thzfe_sithik_transformed, %thzfe_makecompozogrebow.
--
-- Slash Bash (npc 882) takes 25% damage from ordinary attacks, 50% (capped
-- 7) from Crumble Undead, and full damage from brutal arrows fired from a
-- comp ogre bow (zogre_finish.rs2 zfe_slash_bash_prepare_hit; wiki). Crumble
-- Undead alone drove him 30/30 -> 3/30 in 490 ticks and he left at his
-- 500-tick stay (seam25 runs s25sithik2/s25sithik4), so this file fletches
-- the bow and brutal arrows live after Grish's "easier way" unlock, from
-- pre-quest materials given in setup (Quest Helper combatGear: "Either
-- brutal arrows or Crumble Undead for fighting Slash Bash").
--
-- Travel (b61 door rule: no goto_tile into or out of a closed space; every
-- door, stair, ladder and barricade clicked on every visit, both ways):
--  * Lumbridge (the fixture) -> Jiggig, and the tomb -> Grish after Slash
--    Bash, are real Camelot Teleports (magic_spells.dbrow
--    [magic_spell_teleport_camelot]: level 45, 5 air + 1 law, 2757,3478)
--    plus an overland goto: reach.py finds Camelot -> Grish 2448,3049
--    (len 750) and Yanille 2594,3101 -> Grish (len 276) with every door
--    closed, at margins 30/80/160/300. Yanille <-> Jiggig is the same open
--    overland hop. (2447,3049, the old landing, is a solid map tile.)
--  * Jiggig's ceremonial ground east of the barricade is a 250-tile pocket
--    (comp.py 2485,3045: no door, no other way out), so the crushed
--    barricade (zogreflesheaters.rs2 [oploc1,ogre_barricade_collapsedl]:
--    2455,3048 <-> 2457,3048) is climbed by t.player.cross_trap every
--    crossing, in and out.
--  * The tomb is entered and left only by its stairs (zogreflesheaters.rs2
--    [oploc1,ogre_stairs_down]/[oploc1,ogre_stairs]: 2485,3042,0 ->
--    2477,9437,2 and 2478,9437,2 -> 2485,3045,0), t.player.climb both ways.
--    The tomb doors (zogre_finish.rs2 [proc,zfe_tomb_door]) teleport to
--    ^zfe_tomb_past_door 2480,9446,0 from either pair and either side, and
--    the floor beyond (comp.py: 853 tiles) leaves only by the stairs
--    2443,9417,0 to a 122-tile pocket whose only exits are those doors (back
--    to 2480,9446,0) and the stairs down: there is no walk out after Slash
--    Bash, so the player teleports, as a player would.
--  * Yanille: the Magic Guild's east door (magic_guild.rs2
--    [label,open_mageguild_door], a walk-through door) by cross_gate both
--    ways; Sithik's house door (xbows_castle_door 2594,3102), the ladder
--    (2597,3107: ladder/laddertop2, on-tile climb), Sithik's room door
--    (poordoor 2591,3105,1) and the Dragon Inn door (poshdoor 2551,3082) by
--    pass_door / climb on every visit.

-- Camelot Teleport cost, magic_spells.dbrow [magic_spell_teleport_camelot].
local CAMELOT_RUNES = { { "airrune", 5 }, { "lawrune", 1 } }
local CAMELOT = { 2757, 3478, 0 }

return {
    id = "zogreflesheaters",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- fourteen tutorial slots, so the quest's own drops fit
        "::setlevel smithing 4", -- getGeneralRequirements()
        "::setlevel herblore 8",
        "::setlevel ranged 75", -- 30 is the quest's; the comp ogre bow fight (seam25)
        "::setlevel fletching 30", -- comp ogre bow (ogre_arrows.rs2 make_unstrung_comp_bow)
        "::setlevel magic 70", -- Yanille guild door needs 66 to enter (magic_guild.rs2:16); Camelot Teleport 45
        "::setlevel hitpoints 99", -- Slash Bash: wiki infobox atk100/str120 -- survive the grind
        "::setlevel defence 75",
        "::setlevel attack 60",
        "::setlevel strength 60",
        -- quest_chompybird has no `[debugproc,quest_chompybird]` arm in
        -- quests/scripts/quest_cheat.rs2's ~170-quest completion table (grepped;
        -- absent), so `::complete quest_chompybird` answers questcheat_unknown
        -- and does nothing -- a content gap in that cheat table, not this file.
        -- %chompybird is an ordinary varp (chompybird_complete=65), so ::setvar
        -- reaches it directly; this is setup-phase prerequisite staging, not the
        -- run()-time varp write rule (e) forbids.
        "::setvar varp293_chompybird 65",
        "::complete quest_junglepotion",
        "::give rune_scimitar 1", -- combat prerequisite for the Brentle zombie fight
        "::give shark 6", -- combat prerequisite: food for the Slash Bash fight (few: the backpack must also hold the quest's clue items)
        -- seam25: Crumble Undead alone drove Slash Bash to 3/30 in 490 ticks and
        -- he left at his 500-tick stay (s25sithik2/4). The wiki's full-damage
        -- weapon is the comp ogre bow + brutal arrows, which the player may
        -- only fletch after Grish's "easier way" line (%thzfe_makecompozogrebow).
        -- These are its pre-quest materials (achey logs + wolf bones + bow
        -- string; headless ogre arrows from Big Chompy Bird Hunting; iron nails);
        -- the bow and the arrows are fletched live below.
        "::give achey_tree_logs 1",
        "::give wolf_bones 1",
        "::give bow_string 1",
        "::give ogre_headless_arrow 60",
        "::give nails_iron 60",
        -- Two Camelot Teleports (Lumbridge -> Jiggig, the sealed tomb -> Grish).
        "::give airrune 10",
        "::give lawrune 2",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb487_zogre",
            constants = {
                not_started = 0,
                investigate = 1,
                crypt = 2,
                zavistic = 3,
                sithik = 4,
                potion = 5,
                potion_tea = 6,
                sithik_ogre = 7,
                grish_key = 8,
                slash_bash = 9,
                complete = 14,
            },
            row = "quest_zogreflesheaters",
            display = "Zogre Flesh Eaters",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.check("quest.stage.not_started", t.quest.expect_stage("not_started"))

        local function count(sym)
            local r, n = t.inv.count(sym)
            if r ~= "ok" then
                return nil
            end
            return n or 0
        end

        -- A walk on one floor, graded on the exact tile it ends on.
        local function walk(name, x, z, ticks)
            local wr, wd = t.player.walk_to(x, z, ticks)
            local tr, tile = t.world.tile()
            local on = tr == "ok" and type(tile) == "table" and tile.x == x and tile.z == z
            t.check(name, on, "walk_to " .. x .. "," .. z .. " -> " .. tostring(wr) .. " " .. tostring(wd) .. "; at "
                .. (type(tile) == "table" and (tile.x .. "," .. tile.z .. "," .. tostring(tile.level)) or tostring(tr)))
        end

        -- ---- The crossings, each one verb call per crossing -------------
        local function camelot(name)
            t.player.teleport_cast("camelot_teleport", CAMELOT, { name = name, runes = CAMELOT_RUNES,
                where = "Camelot" })
        end
        -- The crushed barricade: [oploc1,ogre_barricade_collapsedl] teleports
        -- west of x 2457 to ^zfe_barricade_east_s 2457,3048, else to
        -- ^zfe_barricade_west_s 2455,3048.
        local function barricade_east(name)
            t.exec(name, t.player.cross_trap, { loc = "zogre_multi_blocking_barricade_l", op_name = "Climb-over",
                at = { 2456, 3048, 0 }, src = { 2455, 3048 }, dest = { 2457, 3048 } })
        end
        local function barricade_west(name)
            t.exec(name, t.player.cross_trap, { loc = "zogre_multi_blocking_barricade_l", op_name = "Climb-over",
                at = { 2456, 3048, 0 }, src = { 2457, 3048 }, dest = { 2455, 3048 } })
        end
        local function tomb_down(name)
            walk(name .. ".walk", 2485, 3045, 50)
            t.exec(name, t.player.climb, { loc = "ogre_stairs_down", op = 1, op_name = "Climb-down",
                at = { 2485, 3042, 0 }, dest = { 2477, 9437, 2 } })
        end
        local function tomb_up(name)
            walk(name .. ".walk", 2477, 9437, 90)
            t.exec(name, t.player.climb, { loc = "ogre_stairs", op = 1, op_name = "Climb-up",
                at = { 2478, 9437, 2 }, dest = { 2485, 3045, 0 } })
        end
        -- The Magic Guild's east door is a walk-through door (no open leaf):
        -- entering lands one tile west of the door tile, leaving lands on it.
        local function guild_in(name)
            t.exec(name, t.player.cross_gate, { loc = "magicguild_door_r", at = { 2597, 3088, 0 },
                near = { 2598, 3088 }, far_ok = function(tile) return tile.x <= 2596 end,
                far_desc = "inside the Magic Guild, x <= 2596" })
        end
        local function guild_out(name)
            t.exec(name, t.player.cross_gate, { loc = "magicguild_door_r", at = { 2597, 3088, 0 },
                near = { 2596, 3088 }, far_ok = function(tile) return tile.x >= 2597 end,
                far_desc = "outside the Magic Guild, x >= 2597" })
        end
        local HOUSE_DOOR = { 2594, 3102, 0 }
        local function house_in(name)
            t.exec(name, t.player.pass_door, { closed = "xbows_castle_door", open = "xbowscastledoor_open",
                at = HOUSE_DOOR, near = { 2594, 3102 }, far = { 2594, 3104 }, ticks = 40 })
        end
        local function house_out(name)
            t.exec(name, t.player.pass_door, { closed = "xbows_castle_door", open = "xbowscastledoor_open",
                at = HOUSE_DOOR, near = { 2594, 3103 }, far = { 2594, 3101 } })
        end
        -- ladder/laddertop2 at 2597,3107 have no maplink row: ~climb_ladder
        -- moves the player one plane on the tile he stands on (measured
        -- 2596,3107,0 <-> 2596,3107,1).
        local function ladder_up(name)
            t.exec(name, t.player.climb, { loc = "ladder", op = 1, op_name = "Climb-up",
                at = { 2597, 3107, 0 }, src = { 2596, 3107 }, dest = { 2596, 3107, 1 } })
        end
        local function ladder_down(name)
            t.exec(name, t.player.climb, { loc = "laddertop2", op = 1, op_name = "Climb-down",
                at = { 2597, 3107, 1 }, src = { 2596, 3107 }, dest = { 2596, 3107, 0 } })
        end
        local ROOM_DOOR = { 2591, 3105, 1 }
        local function room_in(name)
            t.exec(name, t.player.pass_door, { closed = "poordoor", open = "poordooropen",
                at = ROOM_DOOR, near = { 2591, 3106 }, far = { 2591, 3105 } })
        end
        local function room_out(name)
            t.exec(name, t.player.pass_door, { closed = "poordoor", open = "poordooropen",
                at = ROOM_DOOR, near = { 2591, 3105 }, far = { 2591, 3106 } })
        end
        -- Sithik's room from the guild's door step and back out to the street.
        local function up_to_sithik(prefix, climb_name)
            house_in(prefix .. ".houseDoor")
            ladder_up(climb_name)
            room_in(prefix .. ".roomDoor")
        end
        local function out_of_sithiks(prefix)
            room_out(prefix .. ".roomDoor")
            ladder_down(prefix .. ".ladderDown")
            house_out(prefix .. ".houseDoor")
        end

        t.exec("prep.equip_scimitar", t.player.equip, "rune_scimitar")

        -- ---- Starting off: Grish, the guard, the barricade -------------
        camelot("talkToGrish.camelotTeleport")
        t.exec("goto-talkToGrish", t.player.goto_tile, 2448, 3049, 0)
        t.exec("talkToGrish", t.player.talk_to, "zogre_ogre_shaman", 1)
        t.exec("talkToGrish-dialog", t.chat.play, {
            "player:Hello there, what's going on here?",
            "npc:Hey yous creature...wha's yous doing here?",
            "choose:What do you mean sickies?",
            "player:What do you mean sickies?",
            "npc:Da zogries comin wiv da sickies",
            "player:Sorry, I just don't understand",
            "npc:Da sickies is when yous creature goes like orange",
            "choose:Can I help in any way?",
            "player:Can I help in any way?",
            "npc:Yes creatures...yous does good fings for Grish",
            "player:Oh, so you want me to find out why the Zogres have appeared",
            "npc:Is what Grish says!",
            "choose:Ok, I'll check things out then and report back.",
            "player:Ok, I'll check things out then and report back.",
            "npc:Is yous creatures really, really sure",
            "choose:Yes, I'm really sure!",
            "player:Yes, I'm really sure!",
            "npc:Dats da good fing yous creature",
            "mesbox:Grish hands you some food and two potions",
            "npc:Der's yous go creatures",
            "end",
        })
        t.check("quest.stage.investigate", t.quest.expect_stage("investigate"))

        t.exec("goto-talkToGuard", t.player.goto_tile, 2454, 3048, 0)
        t.exec("talkToGuard", t.player.talk_to, "zogre_ogre_guard", 1)
        t.exec("talkToGuard-dialog", t.chat.play, {
            "npc:Yous needs ta stay away from dis place",
            "player:But Grish has asked me to look into this place",
            "npc:Ok, dat is da big, big scary, danger fing!",
            "player:Yes, I'm sure.",
            "npc:Ok, I opens da stoppa's for yous creature.",
            "npc:Ok der' yous goes!",
            "end",
        })
        t.check("quest.stage.crypt", t.quest.expect_stage("crypt"))

        barricade_east("climbBarricade")
        tomb_down("goDownStairs")

        -- ---- Starting off: crypt (tomb F2) clues ------------------------
        walk("searchSkeleton.walk", 2442, 9457, 90)
        t.exec("searchSkeleton", t.player.click_loc, "zogre_brentle_skeleton", 1)
        local kz_present = t.npc.await_present("zogre_human_brentle_vahn", 5, 10)
        t.check("killZombie.present", kz_present == "ok", tostring(kz_present))
        t.exec("killZombie.attack", t.player.attack, "zogre_human_brentle_vahn", 2, 15)
        local kz_r, kz_d = t.exec("killZombie", t.npc.await_dead_engaged, 200, 10, { eat = { item = "shark", below = 50 } })
        local kz_low = tonumber(string.match(tostring(kz_d), "lowest hp (%d+)/"))
        local kz_sharks = count("shark")
        t.check("killZombie.margin", kz_r == "ok" and kz_low ~= nil and kz_low >= 25 and kz_sharks ~= nil and kz_sharks >= 1,
            "Brentle zombie (zogreflesheaters.npc: 50 hp, atk 30, str 30, def 30): lowest hp " .. tostring(kz_low) .. "/99 (the kill's eater), sharks left " .. tostring(kz_sharks)
                .. " of 6 (margin: lowest hp >= 25, a quarter of 99, AND food left)")

        t.exec("openBackpack", t.player.inv_op, "zogre_brentle_vahn_backpack", 1)
        t.exec("openBackpack-dialog", t.chat.play, {
            "mesbox:Just before you open the backpack",
            "mesbox:You find an interesting looking tankard",
            "mesbox:You find a knife and some rotten food",
            "end",
        })
        t.exec("openBackpack.knife", t.inv.await, "knife", 1, 10)
        t.exec("openBackpack.tankard", t.inv.await, "zogre_dragon_tankard", 1, 10)
        -- rotten food is dead weight; the backpack must hold the later clue items
        t.exec("dropRottenFood", t.player.drop, "rotten_food")

        t.exec("searchLectern", t.player.click_loc, "zogre_lecturn", 1)
        t.exec("searchLectern-dialog", t.chat.play, { "mesbox:You find a half torn page", "end" })

        t.exec("searchCoffin", t.player.click_loc, "zogre_coffin_special_entity", 1)
        t.exec("searchCoffin-dialog", t.chat.play, {
            "mesbox:You search the coffin and find a small geometrically",
            "mesbox:The lock looks quite crude",
            "end",
        })

        -- [oplocu,zogre_coffin_special] (zogreflesheaters.rs2:234-240): the
        -- knife springs the lock (%thzfe_prismsearch 0 -> 1) and is kept.
        t.exec("useKnifeOnCoffin", t.player.use_on, "knife", t.player.by_symbol("loc", "zogre_coffin_special_entity"))
        t.exec("useKnifeOnCoffin-dialog", t.chat.play, {
            "mesbox:With some skill you manage to slide the blade",
            "end",
        })
        local ps_r, ps_v = t.var.server("varb488_thzfe_prismsearch")
        local knife_after = count("knife")
        t.check("useKnifeOnCoffin.unlocked", ps_r == "ok" and ps_v == 1 and knife_after == 1,
            "thzfe_prismsearch=" .. tostring(ps_v) .. " (want 1, unlocked); knife held " .. tostring(knife_after)
                .. " (the content keeps it)")

        t.exec("openCoffin", t.player.click_loc, "zogre_coffin_special_entity", 1)
        t.exec("openCoffin-dialog", t.chat.play, {
            "player:Urrrgggg.",
            "player:Aarrrgghhh!",
            "player:Raarrrggggg! Yes!",
            "mesbox:You eventually manage to lift the lid",
            "end",
        })

        t.exec("searchCoffinProperly", t.player.click_loc, "zogre_coffin_special_entity", 1)
        t.exec("searchCoffinProperly-dialog", t.chat.play, {
            "mesbox:You find a creepy looking black prism",
            "end",
        })
        t.exec("searchCoffinProperly.prism", t.inv.await, "zogre_black_prism", 1, 10)

        -- ---- Investigating: out of the tomb, Zavistic -------------------
        tomb_up("leaveTomb.stairs")
        walk("leaveTomb.walkToBarricade", 2457, 3048, 50)
        barricade_west("leaveTomb.barricade")
        -- Overland: Jiggig's open ground west of the barricade to the street
        -- outside the Magic Guild's east door (reach.py: REACH, no door).
        t.exec("goto-talkToZavistic", t.player.goto_tile, 2599, 3088, 0)
        guild_in("talkToZavistic.guildDoor")
        t.exec("talkToZavistic", t.player.talk_to, "zogre_human_zavistic_rarve", 1)
        t.exec("talkToZavistic-dialog", t.chat.play, {
            "player:There's some undead ogre activity over at Jiggig",
            "mesbox:You show the prism and the necromantic half page",
            "npc:Hmmm, now this is interesting",
            "player:I got them from a nearby Ogre tomb",
            "npc:This is very troubling",
            "player:Do you have any leads",
            "npc:Well a wizard by the name of 'Sithik Ints'",
            "npc:Why not go and talk to him",
            "end",
        })
        t.check("quest.stage.zavistic", t.quest.expect_stage("zavistic"))
        guild_out("talkToZavistic.leaveGuild")

        -- ---- Sithik's house: door, ladder, room door -------------------
        up_to_sithik("goUpToSith", "goUpToSith")
        -- zogre_sithik_bed_entity is placed at 2591,3103,1 (maps/m40_48.jl2
        -- "1 31 31: 6887 10 2"); seam24: [oploc1,ogre_bedman_loc] names
        -- Sithik (~chatnpc_specific), so the click opens his page.
        t.exec("talkToSith.press", t.player.click_loc, "zogre_sithik_bed_entity", 1)
        t.exec("talkToSith", t.chat.play, {
            "npc:who gave you permission",
            "player:Zavistic Rarve said",
            "npc:why would he send you to me",
            "choose:Do you mind if I look around?",
            "player:Do you mind if I look around",
            "npc:actually yes I do mind",
            "player:going to have a look around anyway",
        })
        t.check("quest.stage.sithik", t.quest.expect_stage("sithik"))

        -- ---- Investigating: the three searches, the sketch, the Dragon Inn
        -- searchWardrobe/searchCupboard/searchDrawers grant only at
        -- %zogre 3..4 (zogre_finish.rs2 sithiks_*); stage is 4 here.
        t.exec("searchCupboard", t.player.click_loc, "sithiks_cupboard", 1)
        t.exec("searchCupboard-dialog", t.chat.play, { "mesbox:You find a book on Necromancy", "end" })
        t.exec("searchCupboard.book", t.inv.await, "zogre_necrobook", 1, 10)

        t.exec("searchWardrobe", t.player.click_loc, "sithiks_wardrobe", 1)
        t.exec("searchWardrobe-dialog", t.chat.play, { "mesbox:You find a book on Philosophy", "end" })
        t.exec("searchWardrobe.book", t.inv.await, "zogre_hambook", 1, 10)

        t.exec("searchDrawers", t.player.click_loc, "sithiks_drawers", 1)
        t.exec("searchDrawers-dialog", t.chat.play, {
            "mesbox:You find some papyrus",
            "mesbox:You find some charcoal",
            "mesbox:You also find a book on portraiture",
            "end",
        })
        t.exec("searchDrawers.papyrus", t.inv.await, "papyrus", 1, 10)
        t.exec("searchDrawers.charcoal", t.inv.await, "charcoal", 1, 10)

        -- [oplocu,ogre_bedman_loc] (zogre_finish.rs2:255-270): the papyrus is
        -- used up, the charcoal kept, a portrait added.
        t.exec("usePapyrusOnSith", t.player.use_on, "papyrus", t.player.by_symbol("loc", "zogre_sithik_bed_entity"))
        t.exec("usePapyrusOnSith-dialog", t.chat.play, {
            "npc:Oh lovely",
            "mesbox:You begin sketching",
            "mesbox:You get a portrait of Sithik",
            "end",
        })
        t.exec("usePapyrusOnSith.portrait", t.inv.await, "zogre_sithik_portrait_good", 1, 10)
        local papyrus_left = count("papyrus")
        t.check("usePapyrusOnSith.papyrusUsed", papyrus_left == 0, "papyrus held " .. tostring(papyrus_left) .. " (want 0)")

        out_of_sithiks("useTankardOnBartender.leaveSith")
        walk("useTankardOnBartender.walk", 2551, 3083, 80)
        t.exec("useTankardOnBartender.innDoor", t.player.pass_door, { closed = "poshdoor", open = "poshdooropen",
            at = { 2551, 3082, 0 }, near = { 2551, 3083 }, far = { 2551, 3081 } })
        -- [opnpcu,dragon_bartender] (zogre_finish.rs2:291-305): the tankard is
        -- shown, not given (%zfe_asked_tankard 0 -> 1).
        t.exec("useTankardOnBartender", t.player.use_on, "zogre_dragon_tankard", t.player.by_symbol("npc", "dragon_bartender"))
        t.exec("useTankardOnBartender-dialog", t.chat.play, {
            "player:I found this tankard",
            "npc:this is Brentle's mug",
            "player:Brentle you say",
            "npc:Brentle Vahn",
            "player:Brentle Vahn is dead",
            "npc:Noooo",
            "end",
        })
        local at_r, at_v = t.var.server("varp5977_zfe_asked_tankard")
        t.check("useTankardOnBartender.asked", at_r == "ok" and at_v == 1,
            "zfe_asked_tankard=" .. tostring(at_v) .. " (want 1); tankard held " .. tostring(count("zogre_dragon_tankard"))
                .. " (the content keeps it)")
        -- The good portrait is swapped for the signed one (zogre_finish.rs2:307-316).
        t.exec("usePortraitOnBartender", t.player.use_on, "zogre_sithik_portrait_good", t.player.by_symbol("npc", "dragon_bartender"))
        t.exec("usePortraitOnBartender-dialog", t.chat.play, {
            "mesbox:You show the portrait to the Inn keeper",
            "npc:that's the guy",
            "player:bring him to justice",
            "npc:I can and I will",
            "mesbox:The Dragon Inn bartender signs the portrait",
            "end",
        })
        t.exec("usePortraitOnBartender.signed", t.inv.await, "zogre_sithik_portrait_signed", 1, 10)
        local good_left = count("zogre_sithik_portrait_good")
        t.check("usePortraitOnBartender.goodUsed", good_left == 0, "zogre_sithik_portrait_good held " .. tostring(good_left) .. " (want 0)")
        t.exec("usePortraitOnBartender.leaveInn", t.player.pass_door, { closed = "poshdoor", open = "poshdooropen",
            at = { 2551, 3082, 0 }, near = { 2551, 3081 }, far = { 2551, 3083 } })

        -- ---- Zavistic again: the four pieces of evidence -> the potion ---
        walk("bringSignedPortraitToZavistic.walk", 2598, 3088, 80)
        guild_in("bringSignedPortraitToZavistic.guildDoor")
        local bs_r, bs_d = t.player.talk_to("zogre_human_zavistic_rarve", 1)
        t.check("bringSignedPortraitToZavistic", bs_r == "ok", tostring(bs_r) .. " " .. tostring(bs_d))
        t.exec("bringSignedPortraitToZavistic-dialog", t.chat.play, {
            "options",
            "choose:I have evidence I'd like you to look at.",
            "player:I have evidence",
            "mesbox:You show all the evidence",
            "npc:starting to think that Sithik may be involved",
            "mesbox:Zavistic hands you a strange looking potion",
            "end",
        })
        t.exec("bringSignedPortraitToZavistic.potion", t.inv.await, "zogre_ogre_trans_potion", 1, 10)
        t.check("quest.stage.potion", t.quest.expect_stage("potion"))
        guild_out("bringSignedPortraitToZavistic.leaveGuild")

        -- ---- Discover the truth: potion in the tea, leave, come back ----
        up_to_sithik("goUpToSithAgain", "goUpToSithAgain")
        -- [opobju,zogre_cup_of_tea_sithix] (zogre_finish.rs2:326-336): the
        -- potion becomes an empty bottle.
        t.exec("usePotionOnTea", t.player.use_on, "zogre_ogre_trans_potion", t.player.by_symbol("obj", "zogre_cup_of_tea_sithix"))
        t.exec("usePotionOnTea-dialog", t.chat.play, { "mesbox:You pour some of the potion into the cup", "end" })
        t.check("quest.stage.potion_tea", t.quest.expect_stage("potion_tea"))
        local potion_left = count("zogre_ogre_trans_potion")
        t.check("usePotionOnTea.potionUsed", potion_left == 0, "zogre_ogre_trans_potion held " .. tostring(potion_left) .. " (want 0)")

        -- The guide's "go down the ladder and back up" (docs/quests/
        -- zogre_flesh_eaters.md stage 6 -> 7): [oploc1,ladder] at
        -- ^zfe_sithik_ladder turns %zogre potion_tea -> sithik_ogre.
        room_out("goDownstairsFromSith.roomDoor")
        ladder_down("goDownstairsFromSith")
        t.check("goDownstairsFromSith.stage_still_potion_tea", t.quest.expect_stage("potion_tea"))
        ladder_up("goUpToOgreSith")
        t.check("quest.stage.sithik_ogre", t.quest.expect_stage("sithik_ogre"))
        local tr_r, tr_v = t.var.server("varb495_thzfe_sithik_transformed")
        t.check("goUpToOgreSith.transformed", tr_r == "ok" and tr_v == 1, "thzfe_sithik_transformed=" .. tostring(tr_v))
        room_in("goUpToOgreSith.roomDoor")

        -- ---- askSithQuestions / askAboutDiseaseAndOgres -----------------
        t.exec("talkToOgreSith.press", t.player.click_loc, "zogre_sithik_bed_entity", 1)
        t.exec("talkToOgreSith", t.chat.play, {
            "npc:what's happened to me? You must help me!",
            "player:The potion won't wear off",
            "npc:Alright! Alright!",
            "options",
            "choose:How do I remove the effects of the spell from the area?",
            "player:How do I remove the effects",
            "npc:The spell is permanent",
            "options",
            "choose:How do I get rid of the undead ogres?",
            "player:How do I get rid of the undead ogres?",
            "npc:Brutal arrows work well against zogres.",
            "options",
            "choose:How do I get rid of the disease?",
            "player:How do I get rid of the disease?",
            "npc:Two jungle based herbs",
            "options",
            "choose:Sorry, I have to go.",
            "player:Sorry, I have to go.",
            "player:I'll tell Grish.",
            "end",
        })
        local ba_r, ba_v = t.var.server("varb499_thzfe_makebrutalarrow")
        local cd_r, cd_v = t.var.server("varb498_thzfe_makecuredisease")
        t.check("askAboutDiseaseAndOgres.bits", ba_r == "ok" and cd_r == "ok" and ba_v == 1 and cd_v == 1,
            "thzfe_makebrutalarrow=" .. tostring(ba_v) .. " thzfe_makecuredisease=" .. tostring(cd_v))
        out_of_sithiks("talkToGrishAgain.leaveSith")

        -- ---- Tell Grish; the key; the easier way -------------------------
        -- Overland: the street outside Sithik's house to Grish (open ground).
        t.exec("goto-talkToGrishAgain", t.player.goto_tile, 2448, 3049, 0)
        t.exec("talkToGrishAgain", t.player.talk_to, "zogre_ogre_shaman", 1)
        t.exec("talkToGrishAgain-dialog", t.chat.play, {
            "npc:Yous creature dun da fing yet?",
            "player:I found who's responsible",
            "npc:Where is da creature?",
            "player:The person responsible is a wizard",
            "player:I'm sorry to say",
            "npc:Dat is da bad fing creature",
            "player:Yes, that's right",
            "npc:Urghhh...not good fing creature",
            "mesbox:Grish gives you a crudely crafted key.",
            "player:Oh, so you want me to go back in there",
            "npc:Yeah creature",
            "end",
        })
        t.exec("talkToGrishAgain.key", t.inv.await, "zogre_tomb_artefact_key", 1, 10)
        t.check("quest.stage.grish_key", t.quest.expect_stage("grish_key"))
        t.exec("talkToGrishForBow", t.player.talk_to, "zogre_ogre_shaman", 1)
        t.exec("talkToGrishForBow-dialog", t.chat.play, {
            "npc:Hey, you's creature got da old fings?",
            "choose:There must be an easier way to kill these zogres!",
            "player:There must be an easier way to kill these zogres!",
            "npc:Yeah creature, yous needs da comp'zit bow",
            "mesbox:You can now fletch composite ogre bows and brutal arrows.",
            "end",
        })
        local bow_r, bow_v = t.var.server("varb500_thzfe_makecompozogrebow")
        t.check("talkToGrishForBow.bit", bow_r == "ok" and bow_v == 1, "thzfe_makecompozogrebow=" .. tostring(bow_v))

        -- ---- Fletch the comp ogre bow + brutal arrows (now unlocked) -----
        t.exec("fletchBow", t.player.use_item_on_item, "achey_tree_logs", "wolf_bones")
        t.exec("fletchBow.inv", t.inv.await, "unstrung_zogre_bow", 1, 5)
        t.exec("stringBow", t.player.use_item_on_item, "bow_string", "unstrung_zogre_bow")
        t.exec("stringBow.inv", t.inv.await, "zogre_bow", 1, 5)
        for i = 1, 10 do
            t.exec("fletchBrutal." .. i, t.player.use_item_on_item, "ogre_headless_arrow", "nails_iron")
            t.ticks(2)
        end
        local br_c = count("zogre_brutal_iron")
        t.check("fletchBrutal.count", br_c ~= nil and br_c >= 30, "zogre_brutal_iron=" .. tostring(br_c))
        t.exec("equip.bow", t.player.equip, "zogre_bow")
        t.exec("equip.brutal", t.player.equip, "zogre_brutal_iron")

        -- ---- goKillBash: the barricade, the stairs, the locked tomb door --
        t.exec("goto-climbBarricadeForBoss", t.player.goto_tile, 2455, 3048, 0)
        barricade_east("climbBarricadeForBoss")
        tomb_down("goDownStairsForBoss")
        -- North of the first pair of tomb doors (m38_147.jl2:6408/6410,
        -- ogre_cavedoorr/l at 2441/2442,9433 level 2): the key opens them.
        -- [proc,zfe_tomb_door] teleports straight to ^zfe_tomb_past_door
        -- 2480,9446,0 (the guide's goDownToBoss stairs are folded into it).
        walk("enterDoors.walk", 2442, 9434, 90)
        local door_r, door_d = t.player.click_loc("ogre_cavedoorl", 1, { at = { 2442, 9433, 2 } })
        t.ticks(3)
        local _, door_tile = t.world.tile()
        t.check("enterDoors", door_tile.level == 0 and door_tile.x == 2480 and door_tile.z == 9446,
            tostring(door_r) .. " " .. tostring(door_d) .. " -- tile " .. door_tile.x .. "," .. door_tile.z .. "," .. door_tile.level
                .. " (want ^zfe_tomb_past_door 2480,9446,0)")
        t.exec("openTombDoor.mes", t.msg.expect, "You use the Ogre Tomb Key to unlock the door.")

        t.exec("searchStand", t.player.click_loc, "zogre_stand", 1)
        t.exec("searchStand.mes", t.msg.expect, "Something stirs behind you!")
        local sb_r, sb_d = t.npc.await_present("zogre_slash_bash", 5, 10)
        t.check("slashBash.present", sb_r == "ok", "zogre_slash_bash within 5: " .. tostring(sb_r) .. " " .. tostring(sb_d))
        local atk_r, atk_d
        for _ = 1, 6 do
            atk_r, atk_d = t.player.attack("zogre_slash_bash", 2, 15)
            if atk_r == "ok" then break end
            t.ticks(5)
        end
        t.check("slashBash.attack", atk_r == "ok", "comp ogre bow + brutal: " .. tostring(atk_r) .. " " .. tostring(atk_d))
        local sharks_before = count("shark")
        local kd_r, kd_d = t.exec("slashBash.dead", t.npc.await_dead_engaged, 480, 60, { eat = { item = "shark", below = 50 } })
        local fight_low = tonumber(string.match(tostring(kd_d), "lowest hp (%d+)/"))
        local sharks_left = count("shark")
        t.check("slashBash.margin", kd_r == "ok" and fight_low ~= nil and fight_low >= 25 and sharks_left ~= nil and sharks_left >= 1,
            "Slash Bash (zogreflesheaters.npc: 100 hp, atk 100, str 120, def 60): lowest hp " .. tostring(fight_low)
                .. "/99 (the kill's eater), sharks " .. tostring(sharks_before) .. " -> " .. tostring(sharks_left)
                .. " (margin: lowest hp >= 25, a quarter of 99, AND food left)")
        t.check("quest.stage.slash_bash", t.quest.expect_stage("slash_bash"))
        -- He drops the artefact (zogre_finish.rs2 [ai_queue3,zogre_slash_bash],
        -- Quest Helper pickUpOgreArtefact): take it off the floor.
        t.ticks(2)
        local art0 = count("zogre_artifacts") or 0
        local pk_r, pk_d = t.player.click_obj("zogre_artifacts")
        t.ticks(1)
        local art1 = count("zogre_artifacts") or 0
        t.check("pickUpOgreArtefact", art0 == 0 and art1 == 1,
            string.format("click_obj(zogre_artifacts) -> %s (%s); zogre_artifacts %d -> %d",
                tostring(pk_r), tostring(pk_d), art0, art1))

        -- ---- returnRelic: no walk out of the sealed boss floor (see the
        -- header), so a real teleport, then the overland hop to Grish.
        camelot("returnRelic.camelotTeleport")
        t.exec("goto-returnRelic", t.player.goto_tile, 2448, 3049, 0)
        -- Reward rows measure from here: zogreflesheaters.rs2 [queue,zfe_quest_complete]
        -- stat_advance(ranged|fletching|herblore, 20000) (tenths: 2000 XP each),
        -- inv_add(zogre_bones, 2), inv_add(zogre_ancestral_bones_ourg, 3).
        local snapshot_result, snapshot = t.skill.snapshot()
        local zbones_before = count("zogre_bones")
        local obones_before = count("zogre_ancestral_bones_ourg")
        t.check("reward.snapshot", snapshot_result == "ok" and zbones_before == 0 and obones_before == 0,
            "skill.snapshot before hand-in -> " .. tostring(snapshot_result)
                .. "; zogre_bones " .. tostring(zbones_before) .. ", zogre_ancestral_bones_ourg " .. tostring(obones_before)
                .. "; ranged xp=" .. tostring(snapshot and snapshot.ranged and snapshot.ranged.experience)
                .. " fletching xp=" .. tostring(snapshot and snapshot.fletching and snapshot.fletching.experience)
                .. " herblore xp=" .. tostring(snapshot and snapshot.herblore and snapshot.herblore.experience))
        t.exec("returnRelic", t.player.talk_to, "zogre_ogre_shaman", 1)
        t.exec("returnRelic-dialog", t.chat.play, {
            "npc:Hey, you's creature got da old fings?",
            "player:Yeah, I have them here!",
            "npc:Dat is da goodly fing",
            "player:Thanks, that's very nice of you!",
            "end",
        })
        t.ticks(3)
        t.quest.expect_complete()
        t.check("reward.ranged_xp", t.skill.expect_gain("ranged", 2000, snapshot))
        t.check("reward.fletching_xp", t.skill.expect_gain("fletching", 2000, snapshot))
        t.check("reward.herblore_xp", t.skill.expect_gain("herblore", 2000, snapshot))
        local zbones_after = count("zogre_bones")
        local obones_after = count("zogre_ancestral_bones_ourg")
        t.check("reward.zogre_bones", zbones_after ~= nil and zbones_before ~= nil and zbones_after - zbones_before == 2,
            "zogre_bones " .. tostring(zbones_before) .. " -> " .. tostring(zbones_after) .. " (expected +2)")
        t.check("reward.ourg_bones", obones_after ~= nil and obones_before ~= nil and obones_after - obones_before == 3,
            "zogre_ancestral_bones_ourg " .. tostring(obones_before) .. " -> " .. tostring(obones_after) .. " (expected +3)")
        t.finish(0)
        return
    end,
}
