-- Prince Ali Rescue -- REAUTHORED (RETRY after b44ce7a2d), not resumed.
--
-- The previous committed file proved the quest wholly BLOCKED: Hassan,
-- Osman's own princequest switch, Lady Keli, Joe and Prince Ali all had
-- zero live *.spawn rows, so acceptance itself was unreachable. Two fixes
-- landed since:
--   * OSRS-Content 033d83f61f added quest_prince/configs/quest_prince.spawn
--     (hassan 3302,3163,0 / joe 3123,3245,0 / prince_ali_prison 3123,3242,0
--     / lady_keli 3128,3244,0) -- all four now spawn.
--   * osman.rs2's princequest switch moved into [label,osman_talk], reached
--     from contact_osman.rs2's own [opnpc1,contact_osman_multi] once
--     %contact < ^contact_met_maisa (docs/QUEST_AUTHORING.md trap 19) --
--     Osman's princequest lines are reachable now too.
-- So this file drives the quest all the way through to hand-in, rather
-- than reasserting seams that no longer exist.
--
-- Items from Quest Helper's getItemRequirements() (PrinceAliRescue.java) --
-- everything the player BRINGS, never a step the quest itself walks you
-- through -- given in setup, never cheated mid-run (trap 16):
--   softClay, ballsOfWool3(3), yellowDye, redberries, ashes, bucketOfWater,
--   potOfFlour, bronzeBar, pinkSkirt, beers3(3), rope, coins100.
-- Everything else (plainwig/blondwig, keyprint, princeskey, skinpaste) is
-- the quest's own deliverable and is obtained through real clicks below:
-- Ned makes the wig from the 3 balls of wool, it is dyed with the yellow
-- dye already carried, Aggie mixes the paste from the carried ingredients,
-- Lady Keli's key-print branch touches the carried soft clay, and the
-- bronze bar is smelted with the print at a furnace (the wiki's 14 Jan
-- 2026 change -- quest_prince.rs2's [label,prince_make_key], dispatched
-- from smelting.rs2's [label,use_furnace] case keyprint).
--
-- Door rule (b65 re-drive; docs/QUEST_ORCHESTRATOR.md standing rules,
-- owner 2026-10-03 / 2026-10-05). Every goto leaves from and lands on an
-- open street tile; every door between it and the npc is pressed, in and
-- out, by the driver's verbs (static map checks: reach.py --root the
-- worktree, margins 30/80/160):
--   * Al Kharid palace: the palace hall is open to Osman's courtyard
--     (3290,3182 -> 3293,3168 REACH closed-doors 21); Hassan's south hall
--     is behind the double door bankdoor_l/bankdoor_r 3293,3167/3292,3167
--     (r3, south wall; 3293,3168 -> 3302,3163 NEEDS-DOOR via bankdoor_l),
--     a double door that stays open (doubledoors.rs2
--     ~open_double_door_left, openbankdoor_l): pass_door in and out.
--   * Lumbridge <-> Al Kharid: the toll gate kharidmetalgateclosedl/r
--     3268,3227-3228 is NOT the only way (reach.py 3206,3233 -> 3290,3182:
--     UNREACHABLE at 30/80, REACH closed-doors len=377 at 160 -- round by
--     the north, the guard's own "No thank you, I'll walk around."
--     border_gate.rs2:63); a goto between open tiles there is travel.
--   * Draynor: Ned's house poordoor 3101,3258 (r2: outside 3102,3258),
--     Aggie's house poordoor 3088,3258 (r2: outside 3089,3258), the jail's
--     elfdoor 3128,3246 (r1: outside 3128,3247; Lady Keli and Joe are in
--     its guard room), and the cell's alidoor 3123,3243 (unlocked with the
--     key from the guard-room side; quest_prince.rs2 [oplocu,alidoor]
--     p_teleports onto the door tile, [oploc1,alidoor] from the cell side
--     walks out onto 3123,3244: ~prince_walk_alidoor).
--   * Furnace: the Lumbridge furnace fai_falador_furnace 3226,3256
--     (Quest Helper's makeKey WorldPoint 3227,3256; category
--     smithing_furnace -> smelting.rs2 [oplocu,_smithing_furnace]); its
--     smithy is open on the west (3227,3254 -> 3223,3254 REACH 4), so the
--     goto lands outside it on 3223,3254 and the use walks in.

local LUMBRIDGE_FURNACE = { 3226, 3256, 0 }
local EAT_BELOW = 15 -- hitpoints (of 30): a lobster is eaten below this in the jail

return {
    id = "prince",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::give softclay 1",     -- bring-along: Lady Keli's key-print step
        "::give ball_of_wool 3", -- bring-along: Ned's wig (ballsOfWool3)
        "::give yellowdye 1",    -- bring-along: dyeing the wig
        "::give redberries 1",   -- bring-along: Aggie's skin paste
        "::give pot_flour 1",    -- bring-along: Aggie's skin paste
        "::give bucket_water 1", -- bring-along: Aggie's skin paste
        "::give ashes 1",        -- bring-along: Aggie's skin paste
        "::give bronze_bar 1",   -- bring-along: smelting the key print
        "::give pink_skirt 1",   -- bring-along: the disguise (Varrock's Fancy Clothes Store sells it; no in-quest step makes one)
        "::give beer 3",         -- bring-along: getting Joe drunk (beers3)
        "::give rope 1",         -- bring-along: tying up Lady Keli
        "::give coins 100",      -- bring-along: coins100 (spare; this run's happy path never spends any)
        -- Quest Helper getCombatRequirements(): "Able to survive jail guards
        -- (level 26) attacking you" (PrinceAliRescue.java:248). jailguard.npc
        -- makes them aggressive (huntrange 5; one stands at 3127,3248, beside
        -- the jail door), and the fresh character's 10 hitpoints died to them
        -- in the guard room (b65 run1, player.died at 3130,3242). Staged here
        -- like blackknight.lua's fortress guards: hitpoints, defence, food.
        "::setlevel hitpoints 30",
        "::setlevel defence 20",
        "::give lobster 5",      -- food for the jail visits, eaten below EAT_BELOW
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp273_princequest",
            constants = {
                not_started = 0,
                started = 10,
                spoken_osman = 20,
                prep_finished = 30,
                guard_drunk = 40,
                tied_keli = 50,
                saved = 100,
                complete = 110,
                questpoints = 3,
                keymade = 1,
                keyclaimed = 2,
            },
            row = "quest_princealirescue",
            display = "Prince Ali Rescue",
            points = 3,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        local setup_ok, setup_detail = t.inv.await_all({
            softclay = 1,
            ball_of_wool = 3,
            yellowdye = 1,
            redberries = 1,
            pot_flour = 1,
            bucket_water = 1,
            ashes = 1,
            bronze_bar = 1,
            pink_skirt = 1,
            beer = 3,
            rope = 1,
            coins = 100,
        }, 10)
        t.check("setup.items", setup_ok == "ok",
            "inv.await_all(setup items) -> " .. tostring(setup_ok) .. " " .. tostring(setup_detail))

        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Hitpoints are sampled after every row near and in the jail (its
        -- guards are aggressive); below EAT_BELOW a lobster is eaten. The
        -- margin rows read these (lowest hp >= a quarter of 30 AND food left).
        local hp_low, hp_eaten = nil, 0
        local function vitals()
            local hr, hp = t.skill.read("hitpoints")
            if hr == "ok" and type(hp) == "table" and hp.level then
                if hp_low == nil or hp.level < hp_low then
                    hp_low = hp.level
                end
                if hp.level < EAT_BELOW then
                    local er = t.player.inv_op("lobster", 1)
                    if er == "ok" then
                        hp_eaten = hp_eaten + 1
                    end
                    t.ticks(1)
                end
            end
        end
        local function margin_row(name, visit)
            vitals()
            local fr, food = t.inv.count("lobster")
            local hr, hp = t.skill.read("hitpoints")
            t.check(name, hp_low ~= nil and hp_low * 4 >= 30 and fr == "ok" and food >= 1,
                visit .. ": lowest hp " .. tostring(hp_low) .. "/30 (sampled after every row), hp now "
                    .. tostring(hr == "ok" and type(hp) == "table" and hp.level or hr) .. ", lobsters staged 5, eaten "
                    .. hp_eaten .. ", left " .. tostring(food) .. " (" .. tostring(fr)
                    .. ") (margin: lowest hp >= a quarter of 30 AND food left)")
            hp_low = nil
        end

        -- Door helpers: one row per crossing, graded by pass_door / cross_gate
        -- on the loc reads and the tiles (docs/quest_authoring/verbs-pointer.md).
        local function palace_in(tag, open_leaf)
            -- From the open courtyard into the palace hall (no door), then
            -- through the south double door into Hassan's hall. open_leaf is
            -- nil on the run's first visit only: the door is certainly shut
            -- then (fresh world), and pass_door reads the open leaf ONCE, the
            -- tick the closed leaf leaves (script/plugins/quest_driver/
            -- world.lua:430) -- b65 run1/run2 pressed bankdoor_l, the closed
            -- leaf left, and openbankdoor_l (doubledoors.rs2's loc_add, seen
            -- at 3293,3166 by the very next row) was not in the client's pool
            -- yet. Unnamed, the row is graded on the closed leaf leaving
            -- 3293,3167 and the player reaching z <= 3166.
            t.exec(tag .. ".toPalaceHall", t.player.walk_to, 3293, 3168)
            t.exec(tag .. ".palaceDoorIn", t.player.pass_door, { closed = "bankdoor_l", open = open_leaf,
                at = { 3293, 3167, 0 }, near = { 3293, 3168 }, far = { 3293, 3165 },
                far_ok = function(tile) return tile.z <= 3166 end, far_desc = "in Hassan's hall, z <= 3166" })
        end
        local function palace_out(tag)
            t.exec(tag .. ".palaceDoorOut", t.player.pass_door, { closed = "bankdoor_l", open = "openbankdoor_l",
                at = { 3293, 3167, 0 }, near = { 3293, 3166 }, far = { 3293, 3168 },
                far_ok = function(tile) return tile.z >= 3167 end, far_desc = "in the palace hall, z >= 3167" })
        end
        local function jail_in(tag)
            t.exec(tag .. ".jailDoorIn", t.player.pass_door, { closed = "elfdoor", open = "elfdooropen",
                at = { 3128, 3246, 0 }, near = { 3128, 3247 }, far = { 3127, 3245 },
                far_ok = function(tile) return tile.z <= 3246 end, far_desc = "in the jail's guard room, z <= 3246" })
        end
        local function jail_out(tag)
            t.exec(tag .. ".jailDoorOut", t.player.pass_door, { closed = "elfdoor", open = "elfdooropen",
                at = { 3128, 3246, 0 }, near = { 3128, 3246 }, far = { 3128, 3248 },
                far_ok = function(tile) return tile.z >= 3247 end, far_desc = "outside the jail, z >= 3247" })
        end

        -- ------------------------------------------------------- Hassan: accept
        -- hassan.rs2:2-10, quest_prince.spawn's own row (3302,3163,0). The
        -- start: Lumbridge (fixture 3206,3233) -> the open courtyard north of
        -- the palace, round by the north (the toll gate is not the only way:
        -- see the header).
        t.exec("goto-palaceCourtyard", t.player.goto_tile, 3290, 3182, 0)
        palace_in("talkToHassan", nil)
        t.exec("hassan.talk", t.player.talk_to, "hassan", 1)
        t.exec("hassan.accept", t.chat.play, {
            "npc:Greetings I am Hassan",
            "choose:Can I help you? You must need some help here in the desert.",
            "player:Can I help you? You must need some help here in the desert.",
            "npc:I need the services of someone",
        })
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- ------------------------------------------------------- Osman: instructions
        -- osman.rs2's [label,osman_talk] (reached now through contact_osman.rs2's
        -- own hand-back), contact_osman_multi's spawn (m51_49.spawn, 3286,3180,0),
        -- in the open courtyard north of the palace.
        palace_out("talkToOsman")
        t.exec("talkToOsman.toCourtyard", t.player.walk_to, 3288, 3181)
        t.exec("osman.talk", t.player.talk_to, "osman", 1)
        t.exec("osman.instructions", t.chat.play, {
            "player:The chancellor trusts me",
            "npc:Our prince is captive by the Lady Keli",
            "choose:What is the first thing I must do?",
            "player:What is the first thing I must do?",
            "npc:guarded by some stupid guards",
            "npc:tie her up. One coil of rope",
            "player:How good must the disguise be?",
            "npc:fool the guards at a distance",
            "npc:Get a blonde wig, too",
            "npc:My daughter and top spy, Leela",
            "npc:near Draynor Village",
            "choose:What is the second thing you need?",
            "player:What is the second thing you need?",
            "npc:We need the key, or we need a copy made",
            "npc:convince Lady Keli to show it to you",
            "npc:Bring the imprint to me, with a bar of bronze",
            "choose:Okay, I better go find some things.",
            "player:Okay, I had better go find some things.",
            "npc:May good luck travel with you",
        })
        t.expect("quest.stage.spoken_osman", t.quest.expect_stage("spoken_osman"))

        -- ------------------------------------------------------- Ned: the wig
        -- ned.rs2:20-43/64-74/138-158, areas/world/configs/m48_50.spawn.
        -- Al Kharid's courtyard -> the street outside Ned's door (reach.py
        -- 3290,3182 -> 3102,3258: REACH closed-doors 438 at margin 80), then
        -- in by his door.
        t.exec("goto-nedsDoor", t.player.goto_tile, 3102, 3258, 0)
        t.exec("talkToNed.doorIn", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 3101, 3258, 0 }, near = { 3102, 3258 }, far = { 3100, 3258 },
            far_ok = function(tile) return tile.x <= 3101 end, far_desc = "in Ned's house, x <= 3101" })
        t.exec("ned.talk", t.player.talk_to, "ned", 1)
        t.exec("ned.wig", t.chat.play, {
            "npc:me friends call me Ned",
            "choose:Ned, could you make other things from wool?",
            "player:Ned, could you make other things from wool?",
            "npc:Aye, that I can.",
            "choose:How about some sort of wig?",
            "player:How about some sort of wig?",
            "npc:Give me 3 balls of wool",
            "choose:I have that now. Please, make me a wig.",
            "player:I have that now. Please, make me a wig.",
            "mesbox:You hand Ned 3 balls of wool",
            "mesbox:Ned gives you a pretty good wig",
            "npc:There you go, that should fool anyone",
        })
        local wig_have_result = t.inv.await("plainwig", 1, 10)
        t.check("ned.wig.have", wig_have_result == "ok",
            "inv.await(plainwig,1) after Ned -> " .. tostring(wig_have_result))
        t.check("ned.wig.woolGone", t.inv.expect_absent("ball_of_wool"))

        -- Dye it blonde: quest_prince.rs2's [opheldu,plainwig] fires on
        -- last_useitem=yellowdye -- plainwig is the armed half.
        t.exec("wig.dye", t.player.use_item_on_item, "plainwig", "yellowdye")
        local dyed_result = t.inv.await("blondwig", 1, 10)
        t.check("wig.dyed", dyed_result == "ok",
            "inv.await(blondwig,1) after dyeing -> " .. tostring(dyed_result))
        t.check("wig.dye.dyeGone", t.inv.expect_absent("yellowdye"))

        -- ------------------------------------------------------- Aggie: the paste
        -- aggie.rs2's princequest-gated 5th option, areas/world/configs/m48_50.spawn.
        -- Out of Ned's door, 13 tiles west along the street, in by Aggie's door.
        t.exec("talkToAggie.nedDoorOut", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 3101, 3258, 0 }, near = { 3101, 3258 }, far = { 3103, 3258 },
            far_ok = function(tile) return tile.x >= 3102 end, far_desc = "outside Ned's house, x >= 3102" })
        t.exec("talkToAggie.toDoor", t.player.walk_route, { { 3095, 3260 }, { 3089, 3258 } }, { level = 0 })
        t.exec("talkToAggie.doorIn", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 3088, 3258, 0 }, near = { 3089, 3258 }, far = { 3087, 3258 },
            far_ok = function(tile) return tile.x <= 3088 end, far_desc = "in Aggie's house, x <= 3088" })
        t.exec("aggie.talk", t.player.talk_to, "aggie", 1)
        t.exec("aggie.paste", t.chat.play, {
            "npc:What can I help you with?",
            "choose:Could you think of a way to make skin paste?",
            "player:Could you think of a way to make skin paste?",
            "npc:I see you already have the ingredients",
            "choose:Yes please. Mix me some skin paste.",
            "player:Yes please. Mix me some skin paste.",
            "npc:That should be simple",
            "mesbox:You hand the ash, flour, water and redberries",
            "npc:Tourniquet, Fenderbaum",
            "mesbox:Aggie hands you the skin paste",
            "npc:There you go dearie",
        })
        local paste_result = t.inv.await("skinpaste", 1, 10)
        t.check("aggie.paste.have", paste_result == "ok",
            "inv.await(skinpaste,1) after Aggie -> " .. tostring(paste_result))
        t.exec("talkToKeli.aggieDoorOut", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 3088, 3258, 0 }, near = { 3088, 3258 }, far = { 3090, 3258 },
            far_ok = function(tile) return tile.x >= 3089 end, far_desc = "outside Aggie's house, x >= 3089" })

        -- ------------------------------------------------------- Lady Keli: key print
        -- lady_keli.rs2, quest_prince.spawn's own row (3128,3244,0), in the
        -- jail's guard room behind elfdoor 3128,3246. The softclay-gated
        -- "touch the key" option only appears while %princequest =
        -- ^prince_spoken_osman exactly, which still holds. Aggie's door ->
        -- the street outside the jail door (reach.py 3090,3258 -> 3128,3247
        -- REACH closed-doors).
        t.exec("goto-jailDoor", t.player.goto_tile, 3128, 3247, 0)
        vitals()
        jail_in("talkToKeli")
        vitals()
        t.exec("keli.talk", t.player.talk_to, "lady_keli", 1)
        t.exec("keli.keyprint", t.chat.play, {
            "player:Are you the famous Lady Keli",
            "npc:I am Keli, you have heard of me",
            "choose:Heard of you? You are famous in RuneScape!",
            "player:The great Lady Keli, of course I have heard of you",
            "npc:That's very kind of you to say",
            "choose:What is your latest plan then?",
            "player:What is your latest plan then?",
            "npc:I can tell you I have a valuable prisoner",
            "npc:I can expect a high reward",
            "choose:Can you be sure they will not try to get him out?",
            "player:Can you be sure they will not try",
            "npc:There is no way to release him",
            "npc:There is not another key",
            "choose:Could I see the key please?",
            "player:Could I see the key please?",
            "npc:As you put it that way",
            "mesbox:Keli shows you a small key",
            "choose:Could I touch the key for a moment?",
            "player:Could I touch the key a moment please?",
            "npc:Only for a moment then.",
            "mesbox:You put a piece of your soft clay",
            "player:Thank you so much, you are too kind",
            "npc:You are welcome, run along now",
        })
        local keyprint_result = t.inv.await("keyprint", 1, 10)
        t.check("keli.keyprint.have", keyprint_result == "ok",
            "inv.await(keyprint,1) after Keli -> " .. tostring(keyprint_result))
        t.check("keli.keyprint.clayGone", t.inv.expect_absent("softclay"))
        vitals()
        jail_out("makeKey")
        margin_row("makeKey.jailMargin", "first jail visit (Lady Keli)")

        -- ------------------------------------------------------- Furnace: forge the key
        -- smelting.rs2's [oplocu,_smithing_furnace] -> [label,use_furnace] case
        -- keyprint -> quest_prince.rs2's [label,prince_make_key] -- the wiki's
        -- 14 Jan 2026 change (player smelts it at any furnace, no longer handed
        -- to Osman). Quest Helper's makeKey furnace is Lumbridge's
        -- fai_falador_furnace (3227,3256; the map's copy sits at 3226,3256).
        -- The goto lands on the open ground west of its smithy (no door: the
        -- west side is open), and the use walks in.
        t.exec("goto-lumbridgeSmithy", t.player.goto_tile, 3223, 3254, 0)
        local furnace_target = t.player.by_symbol("loc", "fai_falador_furnace")
        t.exec("furnace.smelt", t.player.use_on, "keyprint", furnace_target, { at = LUMBRIDGE_FURNACE })
        local key_result = t.inv.await("princeskey", 1, 10)
        t.check("furnace.smelt.key", key_result == "ok",
            "inv.await(princeskey,1) after smelting -> " .. tostring(key_result))
        t.check("furnace.smelt.printGone", t.inv.expect_absent("keyprint"))
        t.check("furnace.smelt.barGone", t.inv.expect_absent("bronze_bar"))

        -- ------------------------------------------------------- Leela: prep finished
        -- leela.rs2's [label,leela_help] -- all four items held while
        -- %princequest = ^prince_spoken_osman advances it to prep_finished
        -- in the same click, areas/world/configs/m48_50.spawn (3113,3263,0),
        -- open ground east of Draynor (reach.py 3223,3254 -> 3113,3264 REACH
        -- closed-doors 120).
        t.exec("goto-leela", t.player.goto_tile, 3113, 3264, 0)
        t.exec("leela.talk", t.player.talk_to, "leela", 1)
        t.exec("leela.prep", t.chat.play, {
            "npc:Good, you have all the basic equipment",
        })
        t.expect("quest.stage.prep_finished", t.quest.expect_stage("prep_finished"))

        -- ------------------------------------------------------- Joe: three beers
        -- joe.rs2's [label,joe_distract]/[label,joe_beer], quest_prince.spawn
        -- (3123,3245,0), in the jail's guard room. Leela -> the jail door is
        -- open street (reach.py 3113,3264 -> 3128,3247 REACH closed-doors).
        t.exec("goto-jailDoorAgain", t.player.goto_tile, 3128, 3247, 0)
        vitals()
        jail_in("talkToJoe")
        vitals()
        t.exec("joe.talk", t.player.talk_to, "joe", 1)
        t.exec("joe.beer", t.chat.play, {
            "choose:I have some beer here, fancy one?",
            "player:I have some beer here, fancy one?",
            "npc:that would be lovely",
            "player:it must be tough being here without a drink",
            "mesbox:You hand a beer to the guard",
            "npc:That was perfect",
            "player:How are you? Still ok?",
            "player:Would you care for another",
            "npc:I better not",
            "player:Here, just keep these for later",
            "mesbox:You hand two more beers",
            "npc:Franksh, that wash just what I need",
            "mesbox:The guard is drunk",
        })
        t.expect("quest.stage.guard_drunk", t.quest.expect_stage("guard_drunk"))
        t.check("joe.beer.beersGone", t.inv.expect_absent("beer"))
        vitals()

        -- ------------------------------------------------------- Tie up Lady Keli
        -- quest_prince.rs2's [opnpcu,lady_keli] -- rope on Keli, npc_del's her.
        -- Same guard room as Joe: the use walks to her.
        local keli_tie_target = t.player.by_symbol("npc", "lady_keli")
        t.exec("keli.tie", t.player.use_on, "rope", keli_tie_target)
        -- The mesbox SUSPENDS the [opnpcu,lady_keli] branch (trap 22) --
        -- npc_del and the princequest write both sit AFTER it, so it must
        -- be dismissed with a real continue, not just closed.
        t.exec("keli.tie.dismiss", t.chat.play, { "mesbox:You overpower Keli, tie her up" })
        t.ticks(1)
        t.check("keli.tie.ropeGone", t.inv.expect_absent("rope"))
        t.expect("quest.stage.tied_keli", t.quest.expect_stage("tied_keli"))
        vitals()

        -- ------------------------------------------------------- Unlock the jail door
        -- quest_prince.rs2's [oplocu,alidoor]:
        --   if (last_useitem ! princeskey | coordz(coord) <= coordz(loc_coord)) { refuse }
        -- REFUSES when the player's z is <= the door's own z (loc_coord,
        -- 3123,3243,0 -- OSRS-Content maps/m48_50.jl2:1458), so the key
        -- must be used from the GUARD-ROOM side (z > 3243), never from the
        -- prince's side. Also requires the quest past guard_drunk
        -- (tied_keli=50 is) and Lady Keli gone (npc_del'd above). On success
        -- it prints "You unlock the door." and p_teleports the player onto
        -- the door's own tile, the cell side of its north wall
        -- (~prince_walk_alidoor(false)).
        t.exec("useKeyOnDoor.toDoor", t.player.walk_to, 3123, 3244)
        local alidoor_target = t.player.by_symbol("loc", "alidoor")
        t.exec("prince.unlock", t.player.use_on, "princeskey", alidoor_target)
        local unlock_msg_result = t.msg.expect("You unlock the door.")
        t.check("prince.unlock.msg", unlock_msg_result == "ok",
            "msg.expect(You unlock the door.) -> " .. tostring(unlock_msg_result))
        local cell_r, cell_tile = t.world.tile()
        t.check("prince.unlock.inCell", cell_r == "ok" and cell_tile.z <= 3243 and cell_tile.level == 0,
            string.format("after the unlock at %s,%s,%s (want the cell, z <= 3243, level 0)",
                tostring(cell_tile and cell_tile.x), tostring(cell_tile and cell_tile.z),
                tostring(cell_tile and cell_tile.level)))
        vitals()

        -- ------------------------------------------------------- Free the prince
        t.exec("prince.talk", t.player.talk_to, "prince_ali_prison", 1)
        t.exec("prince.rescue", t.chat.play, {
            "player:Prince, I come to rescue you",
            "npc:That is very very kind of you",
            "player:With a disguise. I have removed the Lady Keli",
            "player:Take this disguise, and this key",
            "mesbox:You hand over the disguise and key",
            "npc:Thank you my friend, I must leave you now",
            "player:Go to Leela, she is close to here",
            "mesbox:The prince has escaped, well done",
        })
        t.expect("quest.stage.saved", t.quest.expect_stage("saved"))

        -- ------------------------------------------------------- Hand in to Hassan
        -- Out of the cell by the prison gate ([oploc1,alidoor] from z <= 3243
        -- walks the player through onto 3123,3244: ~prince_walk_alidoor(true)),
        -- out of the jail, and back to the palace by its courtyard.
        t.exec("returnToHassan.cellOut", t.player.cross_gate, { loc = "alidoor", at = { 3123, 3243, 0 },
            near = { 3123, 3243 }, far_ok = function(tile) return tile.z >= 3244 end,
            far_desc = "in the guard room, z >= 3244" })
        vitals()
        t.exec("returnToHassan.toJailDoor", t.player.walk_to, 3128, 3246)
        vitals()
        jail_out("returnToHassan")
        margin_row("returnToHassan.jailMargin", "second jail visit (Joe, Lady Keli, the cell)")

        local reward_coins_before_result, reward_coins_before = t.inv.count("coins")

        t.exec("goto-palaceCourtyardReturn", t.player.goto_tile, 3290, 3182, 0)
        palace_in("returnToHassan", "openbankdoor_l")
        t.exec("hassan.return.talk", t.player.talk_to, "hassan", 1)
        t.exec("hassan.return.reward", t.chat.play, {
            "npc:You have the eternal gratitude of the Emir",
        })
        t.ticks(3) -- queue(prince_complete) is queued behind this page, not synchronous

        t.quest.expect_complete()

        local reward_coins_after_result, reward_coins_after = t.inv.count("coins")
        t.check("reward.coins", reward_coins_before_result == "ok" and reward_coins_after_result == "ok"
            and reward_coins_after == reward_coins_before + 700,
            string.format("coins %s -> %s (want +700), reads %s/%s",
                tostring(reward_coins_before), tostring(reward_coins_after),
                tostring(reward_coins_before_result), tostring(reward_coins_after_result)))

        t.finish(0)
    end,
}
