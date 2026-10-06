-- Family Crest. Hand-authored against OSRS-Content/osrs239-content/server/
-- scripts/quests/quest_crest/scripts/*.rs2 (crest_dimintheis.rs2,
-- crest_caleb.rs2, crest_avan.rs2, crest_boot.rs2, crest_witchaven.rs2,
-- crest_johnathon.rs2, crest_chronozon.rs2, crest_quest.rs2),
-- areas/alkharid/scripts/gem_trader.rs2 (the Family Crest branch),
-- quests/quest_theslugmenace/scripts/slugmenace_witchaven.rs2 (the Witchaven
-- ruin entrance, open to all since seam25), skill_smithing smelting.rs2 and
-- skill_crafting jewellery.rs2 / jewellery_if.rs2 (perfect gold).
--
-- Setup gives only Quest Helper getItemRequirements(): the five cooked
-- fish, a pickaxe, two rubies, ring + necklace moulds, antipoison and the
-- runes for the four blast spells; plus the skill requirements (Mining,
-- Smithing, Crafting 40, Magic 59 -- raised to 99 with Hitpoints/Defence 99,
-- rune armour and sharks for the level-170 Chronozon fight) and the law
-- runes for the five teleports below.
--
-- Travel (b60 door rule: no goto_tile into or out of a closed space).
--   * Dimintheis lives in Varrock's members' south-east quarter, whose only
--     way in on foot is the east gate guidorgatel/rclosed 3264,3405-3406
--     (east_gate.rs2 [label,varrock_east_gate], a walk-through p_teleport;
--     comp.py: the quarter is 473 tiles walled by that gate and house
--     doors). Every visit crosses it by click, in and out, and his house's
--     west door 3278,3404 (standing open in the map) by pass_door.
--   * Catherby, Witchaven and the Ice Mountain hut are on the far side of
--     the members' wall from Varrock and Al Kharid: the long legs are real
--     spellbook teleports (Camelot 5 air + 1 law, Varrock 1 fire + 3 air +
--     1 law, magic_spells.dbrow), each graded TELEPORTED / runes / landing,
--     then an overland goto between open tiles (reach.py REACH closed-doors).
--   * Caleb's house door poordoor 2815,3448 is passed by pass_door both ways;
--     so is the Al Kharid furnace room's east door 3279,3185 (standing open).
--   * Avan's spawn tile 3295,3284 is inside the rockslide: both visits land
--     on the open tile 3295,3286 beside it.
--   * The Witchaven dungeon's three selfstage room doors and the gold-room
--     gate are crossed by pass_door on every visit, in and out.
--   * The Dwarven Mine is entered by the hut's trapdoor (maplink
--     3018,3450 -> 3018,9850) and left by a teleport; the Witchaven
--     dungeon by the old ruin entrance (2696,3283 -> 2696,9683) and a
--     teleport; the Edgeville dungeon by the ruin doorway (elfdooropen
--     3092,3470, standing open), the trapdoor 3097,3468 opened then climbed,
--     the dungeon gate metalgateclosedl 3103,9910 and the members' gate
--     membergatel 3131,9917, then a teleport out.
--   * The Jolly Boar's ground floor is open from the north doorway (no door
--     loc); its upper floor is reached and left by the staircase
--     fai_varrock_stairs_taller / _top 3285,3493 (no maplink: ~climb's +/-1
--     plane on the tile the player stands on), each way a t.player.climb
--     row; the trapdoors and the ruin entrance are climb rows too.

return {
    id = "crest",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000,
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        -- Bring-along items: Quest Helper getItemRequirements().
        "::give shrimp 1",
        "::give salmon 1",
        "::give tuna 1",
        "::give bass 1",
        "::give swordfish 1",
        "::give adamant_pickaxe 1",
        "::give ruby 2",
        "::give ring_mould 1",
        "::give necklace_mould 1",
        "::give 3doseantipoison 1",
        "::give airrune 200",
        "::give waterrune 60",
        "::give earthrune 60",
        "::give firerune 80",
        "::give deathrune 60",
        -- Five teleports: Camelot x2 and Varrock x3 (one law rune each).
        "::give lawrune 8",
        -- Chronozon (level 170) gear and food.
        "::give rune_full_helm 1",
        "::give rune_chainbody 1",
        "::give rune_platelegs 1",
        "::give rune_kiteshield 1",
        "::give shark 8",
        "::setlevel mining 40",
        "::setlevel smithing 40",
        "::setlevel crafting 40",
        "::setlevel magic 99",
        "::setlevel hitpoints 99",
        "::setlevel defence 99",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp148_crestquest",
            constants = {
                not_started = 0,
                spoken_dimintheis = 1,
                spoken_caleb = 2,
                caleb_piece = 3,
                caleb_where = 4,
                spoken_gem_trader = 5,
                spoken_avan = 6,
                spoken_boot = 7,
                avan_piece = 8,
                spoken_johnathon = 9,
                cured_johnathon = 10,
                complete = 11,
            },
            row = "quest_familycrest",
            display = "Family Crest",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- setup cheats' effect is not client-side yet
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        t.exec("equip.rune_full_helm", t.player.equip, "rune_full_helm")
        t.exec("equip.rune_chainbody", t.player.equip, "rune_chainbody")
        t.exec("equip.rune_platelegs", t.player.equip, "rune_platelegs")
        t.exec("equip.rune_kiteshield", t.player.equip, "rune_kiteshield")

        local function tile_text(r, tt)
            if r == "ok" and type(tt) == "table" then
                return tt.x .. "," .. tt.z .. "," .. tt.level
            end
            return tostring(r)
        end

        -- Exact backpack counts after a "use X on Y" or a hand-over: the item
        -- used must have LEFT the pack and the product arrived.
        local function counts(name, want, why)
            local parts, good = {}, true
            for _, w in ipairs(want) do
                local r, n = t.inv.count(w[1])
                if r ~= "ok" or n ~= w[2] then
                    good = false
                end
                parts[#parts + 1] = w[1] .. " " .. tostring(n) .. " (" .. tostring(r) .. ", want " .. w[2] .. ")"
            end
            t.check(name, good, why .. ": " .. table.concat(parts, ", "))
        end

        -- Varrock's members' south-east quarter: the east gate, walk-through,
        -- pressed on every crossing (east_gate.rs2: from the west the press
        -- lands on the gate tile 3264, from the east one tile west, 3263).
        local function east_gate_in(name)
            t.exec(name, t.player.cross_gate, { loc = "guidorgatelclosed", at = { 3264, 3405, 0 },
                near = { 3263, 3405 }, far_ok = function(tile) return tile.x >= 3264 end,
                far_desc = "inside Varrock's south-east quarter, x >= 3264" })
        end
        local function east_gate_out(name)
            t.exec(name, t.player.cross_gate, { loc = "guidorgatelclosed", at = { 3264, 3405, 0 },
                near = { 3264, 3405 }, far_ok = function(tile) return tile.x <= 3263 end,
                far_desc = "back in Varrock, x <= 3263" })
        end
        -- Dimintheis' house (x 3278-3283, z 3401-3406): its one way in from
        -- the quarter's street is the west door 3278,3404, which the map
        -- places standing open (fai_varrock_castle_door_open; comp.py: no
        -- other opening). pass_door asserts the open leaf on every visit,
        -- or presses the closed one if the door was shut since.
        local function dimintheis_house_in(name)
            t.exec(name, t.player.pass_door, { closed = "fai_varrock_castle_door", open = "fai_varrock_castle_door_open",
                at = { 3278, 3404, 0 }, near = { 3277, 3404 }, far = { 3279, 3404 } })
        end
        local function dimintheis_house_out(name)
            t.exec(name, t.player.pass_door, { closed = "fai_varrock_castle_door", open = "fai_varrock_castle_door_open",
                at = { 3278, 3404, 0 }, near = { 3279, 3404 }, far = { 3276, 3404 } })
        end
        local VARROCK_RUNES = { { "firerune", 1 }, { "airrune", 3 }, { "lawrune", 1 } }
        local CAMELOT_RUNES = { { "airrune", 5 }, { "lawrune", 1 } }

        -- ---------------------------------------------------------------
        -- Dimintheis, south east Varrock (crest_dimintheis.rs2 spawn row
        -- m51_53.spawn:21, 3279,3404,0). %crestquest=0 falls to the bottom
        -- default branch: "Hello, My name is Dimintheis..." -> a 3-way
        -- choice -> "Hi, I am a bold adventurer." -> crest_dimintheis_
        -- adventurer's own 3-way -> "So where is this crest?" ->
        -- crest_dimintheis_where's reveal (3 npc pages) -> a 2-way ->
        -- "Ok, I will help you." -> crest_dimintheis_accept sets
        -- %crestquest=spoken_dimintheis. Lumbridge -> the east gate is open
        -- ground (reach.py REACH closed-doors len=336); the gate is clicked.
        -- ---------------------------------------------------------------
        t.exec("goto-dimintheis.eastGate", t.player.goto_tile, 3262, 3405, 0)
        east_gate_in("dimintheis.eastGateIn")
        dimintheis_house_in("dimintheis.houseDoorIn")
        t.exec("talkToDimintheis", t.player.talk_to, "dimintheis")
        t.exec("talkToDimintheis-dialog", t.chat.play, {
            "npc:My name is Dimintheis, of the noble family Fitzharmon.",
            "choose:Hi, I am a bold adventurer.",
            "player:Hi, I am a bold adventurer.",
            "npc:An adventurer hmmm?",
            "choose:So where is this crest?",
            "player:So where is this crest?",
            "npc:my three sons took it with them",
            "npc:the battle to save Varrock",
            "npc:Caleb is alive and well",
            "choose:Ok, I will help you.",
            "player:Ok, I will help you.",
            "npc:I thank you greatly adventurer",
        })
        t.chat.close()
        t.expect("quest.stage.spoken_dimintheis", t.quest.expect_stage("spoken_dimintheis"))

        -- ---------------------------------------------------------------
        -- Caleb Fitzharmon, Catherby (crest_caleb.rs2 spawn row
        -- m44_53.spawn:10, 2819,3451,0). %crestquest=spoken_dimintheis ->
        -- caleb_fitzharmon_start: "Who are you?" -> 3-way -> "Are you Caleb
        -- Fitzharmon?" -> caleb_fitzharmon_areyou (npc+player narrative,
        -- no choice) -> a 2-way -> "So can I have your bit?" ->
        -- caleb_fitzharmon_bit (npc+player narrative) -> a 2-way ->
        -- "Ok, I will get those." sets %crestquest=spoken_caleb.
        -- Out of the quarter by the gate, Camelot Teleport, the overland walk
        -- to Caleb's door (reach.py 2757,3478 -> 2815,3447 REACH len=89),
        -- and the door poordoor 2815,3448 by click.
        -- ---------------------------------------------------------------
        dimintheis_house_out("dimintheis.houseDoorOut")
        east_gate_out("dimintheis.eastGateOut")
        t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = "caleb.camelotTeleport",
            runes = CAMELOT_RUNES, where = "Camelot, for Catherby" })
        t.exec("goto-caleb.door", t.player.goto_tile, 2815, 3446, 0)
        t.exec("caleb.doorIn", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 2815, 3448, 0 }, near = { 2815, 3447 }, far = { 2815, 3449 } })
        t.exec("talkToCaleb", t.player.talk_to, "caleb_fitzharmon")
        t.exec("talkToCaleb-dialog", t.chat.play, {
            "npc:Who are you? What are you after?",
            "choose:Are you Caleb Fitzharmon?",
            "player:Are you Caleb Fitzharmon?",
            "npc:Why... yes I am",
            "player:I have been sent by your father",
            "npc:Ah... well... hmmm",
            "choose:So can I have your bit?",
            "player:So can I have your bit?",
            "npc:I am the oldest son",
            "player:It's not really much use",
            "npc:Well that is true",
            "npc:so if you will assist me",
            "player:So what ingredients are you missing?",
            "npc:I require the following cooked fish",
            "choose:Ok, I will get those.",
            "player:Ok, I will get those.",
            "npc:You will? It would help me a lot!",
        })
        t.chat.close()
        t.expect("quest.stage.spoken_caleb", t.quest.expect_stage("spoken_caleb"))

        -- ---------------------------------------------------------------
        -- talkToCalebWithFish: back to Caleb with the five fish.
        -- caleb_fitzharmon_fish (crest_caleb.rs2) hands over avan_crest and
        -- sets %crestquest=caleb_piece, then a 2-way; "Thank you very much!"
        -- ends this visit so the guide's talkToCalebOnceMore is its own talk.
        -- The fish are staged in setup, so this is the same visit: no goto.
        -- ---------------------------------------------------------------
        t.exec("talkToCalebWithFish", t.player.talk_to, "caleb_fitzharmon")
        t.exec("talkToCalebWithFish-dialog", t.chat.play, {
            "npc:How is the fish collecting going?",
            "player:Got them all with me.",
            "mesbox:You exchange the fish",
            "choose:Thank you very much!",
            "player:Thank you very much.",
            "npc:You're welcome.",
        })
        t.chat.close()
        counts("caleb.gotPiece", { { "shrimp", 0 }, { "salmon", 0 }, { "tuna", 0 }, { "bass", 0 }, { "swordfish", 0 },
            { "avan_crest", 1 } }, "the five fish handed to Caleb and his crest piece (avan_crest) received")
        t.expect("quest.stage.caleb_piece", t.quest.expect_stage("caleb_piece"))

        -- talkToCalebOnceMore: caleb_fitzharmon_salad -> "Uh... what happened
        -- to the rest of it?" -> caleb_fitzharmon_rest, which at caleb_piece
        -- names Avan and sets %crestquest=caleb_where.
        t.exec("talkToCalebOnceMore", t.player.talk_to, "caleb_fitzharmon")
        t.exec("talkToCalebOnceMore-dialog", t.chat.play, {
            "npc:finishing touches to my masterful salad",
            "choose:Uh... what happened to the rest of it?",
            "player:Uh... what happened to the rest of it?",
            "npc:my brothers and I had a slight disagreement",
            "npc:None of us wanted to give up",
            "npc:We each went our seperate ways",
            "player:So do you know where I could find any of your brothers?",
            "npc:we haven't really kept in touch",
            "npc:He said he was on some kind of search for treasure",
            "npc:Avan always did have expensive tastes",
        })
        t.chat.close()
        t.expect("quest.stage.caleb_where", t.quest.expect_stage("caleb_where"))

        -- ---------------------------------------------------------------
        -- Al Kharid Gem Trader (gem_trader.rs2 spawn m51_50.spawn:14,
        -- 3288,3212,0). At %crestquest = caleb_where the menu has a third
        -- row; [label,gem_trader_crest] sets spoken_gem_trader. Out of
        -- Caleb's door, Varrock Teleport, then the open path south past the
        -- Al Kharid mine (reach.py 3213,3424 -> 3288,3212 REACH len=297, no
        -- toll gate).
        -- ---------------------------------------------------------------
        t.exec("caleb.doorOut", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 2815, 3448, 0 }, near = { 2815, 3449 }, far = { 2815, 3447 } })
        t.player.teleport_cast("varrock_teleport", { 3213, 3424, 0 }, { name = "gemTrader.varrockTeleport",
            runes = VARROCK_RUNES, where = "Varrock, for Al Kharid by the mine path" })
        t.exec("goto-gemTrader", t.player.goto_tile, 3288, 3212, 0)
        t.exec("talkToGemTrader", t.player.talk_to, "gem_trader")
        t.exec("talkToGemTrader-dialog", t.chat.play, {
            "npc:Good day to you traveller.",
            "choose:I'm in search of a man named Avan Fitzharmon.",
            "player:I'm in search of a man named Avan Fitzharmon.",
            "npc:Fitzharmon eh?",
            "npc:persuasion around here recently",
            "npc:from 'perfect gold'",
            "npc:theres gold out there",
            "npc:Maybe we'll all get lucky",
        })
        t.chat.close()
        t.expect("quest.stage.spoken_gem_trader", t.quest.expect_stage("spoken_gem_trader"))

        -- ---------------------------------------------------------------
        -- Avan (crest_avan.rs2, spawn m51_51.spawn:9, 3295,3284,0):
        -- avan_intro's second option, then crest_man_avan sets spoken_avan
        -- before the closing player line. Avan's spawn tile is inside the
        -- rockslide (rockslide3 3294,3283 / 3296,3285 cover 3295,3284): the
        -- goto lands on the open tile north of it, 3295,3286 (reach.py
        -- 3288,3212 -> 3295,3286 REACH closed-doors len=97), and talk_to
        -- walks the last step.
        -- ---------------------------------------------------------------
        t.exec("goto-avan", t.player.goto_tile, 3295, 3286, 0)
        t.exec("talkToMan", t.player.talk_to, "avan")
        t.exec("talkToMan-dialog", t.chat.play, {
            "options",
            "choose:I'm looking for a man named Avan Fitzharmon.",
            "player:I'm looking for a man... his name is Avan Fitzharmon.",
            "npc:Then you have found him.",
            "player:You have a part of your family crest.",
            "npc:Ha! I suppose one of my worthless brothers",
            "player:No, it was your father",
            "npc:My... my father wishes this?",
            "npc:There is a certain lady",
            "npc:is a golden ring",
            "npc:not just any old gold",
            "npc:None of the gold around here",
            "npc:in finding it I am afraid",
            "npc:gladly hand over my fragment",
            "player:Can you give me any help on finding this 'perfect gold'?",
            "npc:I thought I had found a solid lead",
            "npc:Unfortunately he has apparently returned to his home",
            "player:Well, I'll see what I can do.",
        })
        t.chat.close()
        t.expect("quest.stage.spoken_avan", t.quest.expect_stage("spoken_avan"))

        -- ---------------------------------------------------------------
        -- Boot the dwarf (crest_boot.rs2, spawn m46_153.spawn:14,
        -- 2985,9812,0). enterDwarvenMine: the trapdoor fai_dwarf_trapdoor_down
        -- 3019,3450 in the open-sided hut on Ice Mountain (maplink.dbrow
        -- 0_47_53_10_58 -> 0_47_153_10_58: 3018,3450 -> 3018,9850), then the
        -- mine walked to Boot (reach.py REACH closed-doors len=75).
        -- ---------------------------------------------------------------
        t.exec("goto-enterDwarvenMine", t.player.goto_tile, 3017, 3450, 0)
        t.exec("enterDwarvenMine", t.player.climb, { loc = "fai_dwarf_trapdoor_down", op = 1, op_name = "Climb-down",
            at = { 3019, 3450, 0 }, src = { 3018, 3450 }, dest = { 3018, 9850, 0 } })
        t.exec("walkToBoot", t.player.walk_route, { { 3018, 9842 }, { 3018, 9834 }, { 3018, 9826 }, { 3018, 9818 },
            { 3014, 9814 }, { 3007, 9813 }, { 3000, 9812 }, { 2993, 9811 }, { 2986, 9810 }, { 2986, 9813 } })
        t.exec("talkToBoot", t.player.talk_to, "boot_the_dwarf")
        t.exec("talkToBoot-dialog", t.chat.play, {
            "npc:Hello tall person.",
            "choose:Hello. I'm in search of very high quality gold.",
            "player:Hello. I'm in search of very high quality gold.",
            "npc:High quality gold eh?",
            "npc:I don't believe it's exactly easy to get to though",
        })
        t.chat.close()
        t.expect("quest.stage.spoken_boot", t.quest.expect_stage("spoken_boot"))

        -- ---------------------------------------------------------------
        -- enterWitchavenDungeon: the old ruin entrance west of Witchaven
        -- (open to everyone since seam25; lands at 2696,9683). The
        -- dungeon's aggressive types (m42_151.spawn hobgoblins at the
        -- ladder, ogres at the levers, hellhounds on the gold rocks) are
        -- made passive with the documented ::passive cheat (QUEST_AUTHORING
        -- trap 26) so a Mining-40 character survives the walk. Out of the
        -- mine by Camelot Teleport, then overland to the ruin (reach.py
        -- 2757,3478 -> 2697,3283 REACH closed-doors len=259).
        -- ---------------------------------------------------------------
        t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = "witchaven.camelotTeleport",
            runes = CAMELOT_RUNES, where = "Camelot, out of the Dwarven Mine, for Witchaven" })
        t.exec("goto-witchavenRuin", t.player.goto_tile, 2697, 3283, 0)
        t.ticks(2)
        local pr1, pd1 = t.cheat("::passive rimmington_hobgoblin_armed_1")
        t.check("passive.hobgoblin", pr1 == "ok", "::passive rimmington_hobgoblin_armed_1 -> " .. tostring(pr1) .. " " .. tostring(pd1))
        local pr2, pd2 = t.cheat("::passive ogre")
        t.check("passive.ogre", pr2 == "ok", "::passive ogre -> " .. tostring(pr2) .. " " .. tostring(pd2))
        local pr3, pd3 = t.cheat("::passive hellhound")
        t.check("passive.hellhound", pr3 == "ok", "::passive hellhound -> " .. tostring(pr3) .. " " .. tostring(pd3))
        -- slugmenace_witchaven.rs2 [oploc1,slug2_ruin_entrance]: p_teleport
        -- to the entrance's own coord + 6400 (2696,9683).
        t.exec("enterWitchavenDungeon", t.player.climb, { loc = "slug2_ruin_entrance", op = 1, op_name = "Climb-down",
            at = { 2696, 3283, 0 }, src = { 2697, 3283 }, dest = { 2696, 9683, 0 } })

        -- ---------------------------------------------------------------
        -- The lever puzzle (crest_witchaven.rs2): each [oploc1,lever*]
        -- swaps the lever for its pair on its own coord; [oploc1,
        -- famcrest_doori2h1] opens only with leverh DOWN and leveri2 UP.
        -- Quest Helper / wiki order. The room doors -- famcrest_doorg2h1
        -- 2722,9671 (south room, east), famcrest_doorh2 2719,9671 (south
        -- room, west), famcrest_doorh2g1 2723,9711 (north room) -- are
        -- door_selfstage doors (doors_selfstage.loc:239-246): opened, each
        -- is the SAME symbol one tile over for 500 ticks
        -- (doors_selfstage.rs2:76-89). Every visit crosses its door by
        -- pass_door, in and out (reach.py: the south room is NEEDS-DOOR
        -- from the lever corridor); a door still open from an earlier
        -- visit is asserted standing open, not pressed shut.
        -- ---------------------------------------------------------------
        local function room_door(name, sym, at, near, far)
            t.exec(name, t.player.pass_door, { closed = sym, open = sym, at = at, near = near, far = far })
        end
        -- A lever pull, graded on the swapped lever standing on the pulled
        -- lever's own tile.
        local function lever(name, sym, swapped, at)
            t.exec(name, t.player.click_loc, sym, 1, { at = at })
            t.ticks(2)
            local lr, l = t.world.loc_near(swapped, 1, { at = at })
            t.check(name .. ".pulled", lr == "ok",
                "world.loc_near(" .. swapped .. ", at " .. at[1] .. "," .. at[2] .. "," .. at[3] .. ") -> " .. tostring(lr) .. " "
                    .. tostring(type(l) == "table" and (tostring(l.tile_x) .. "," .. tostring(l.tile_z) .. "," .. tostring(l.level)) or l))
        end
        local function walk_lever_corridor(name)
            local wr, wd = t.player.walk_to(2722, 9709, 60)
            local tr, tt = t.world.tile()
            t.check(name, wr == "ok" and tr == "ok" and tt.x == 2722 and tt.z == 9709 and tt.level == 0,
                "walk_to 2722,9709 -> " .. tostring(wr) .. " " .. tostring(wd) .. "; at " .. tile_text(tr, tt))
        end
        local LEVER_G = { 2722, 9710, 0 }
        local LEVER_H = { 2724, 9669, 0 }
        local LEVER_I = { 2722, 9718, 0 }
        local SOUTH_EAST_DOOR = { 2722, 9671, 0 }
        local SOUTH_WEST_DOOR = { 2719, 9671, 0 }
        local NORTH_DOOR = { 2723, 9711, 0 }
        -- followPathAroundEast's first leg: from the ladder's foot round to
        -- the north wall lever (pressed from beside it: a press from the
        -- landing hunted 99 pixels under the chatbox, run 1 row 54).
        t.ticks(3)
        walk_lever_corridor("walk-pullNorthLever")
        lever("pullNorthLever", "leverg", "leverg2", LEVER_G)
        -- pull leverh up: into the south room by its east door, out by its west.
        room_door("enterSouthRoomEast", "famcrest_doorg2h1", SOUTH_EAST_DOOR, { 2722, 9672 }, { 2722, 9670 })
        lever("pullSouthRoomLever", "leverh", "leverh2", LEVER_H)
        room_door("exitSouthRoomWest", "famcrest_doorh2", SOUTH_WEST_DOOR, { 2719, 9670 }, { 2719, 9672 })
        walk_lever_corridor("walk-pullNorthLeverAgain")
        lever("pullNorthLeverAgain", "leverg2", "leverg", LEVER_G)
        -- pull leveri up: into the north room and back out.
        room_door("enterNorthRoom", "famcrest_doorh2g1", NORTH_DOOR, { 2723, 9710 }, { 2723, 9712 })
        lever("pullNorthRoomLever", "leveri", "leveri2", LEVER_I)
        room_door("exitNorthRoom", "famcrest_doorh2g1", NORTH_DOOR, { 2723, 9712 }, { 2723, 9709 })
        walk_lever_corridor("walk-pullNorthLever3")
        lever("pullNorthLever3", "leverg", "leverg2", LEVER_G)
        -- pull leverh down: into the south room by its west door, out by its east.
        room_door("enterSouthRoomWest", "famcrest_doorh2", SOUTH_WEST_DOOR, { 2719, 9672 }, { 2719, 9670 })
        lever("pullSouthRoomLever2", "leverh2", "leverh", LEVER_H)
        room_door("exitSouthRoomEast", "famcrest_doorg2h1", SOUTH_EAST_DOOR, { 2722, 9670 }, { 2722, 9672 })

        -- followPathAroundEast (2721,9700), then the goldrock2 gate
        -- famcrest_doori2h1 (2727,9690; the only way into the gold room,
        -- reach.py NEEDS-DOOR), which reads the lever state and swings by
        -- ~door_open_active to famcrest_doori2h1_open.
        local er, ed = t.player.walk_to(2721, 9700, 60)
        local etr, east = t.world.tile()
        t.check("followPathAroundEast", er == "ok" and etr == "ok" and east.x == 2721 and east.z == 9700 and east.level == 0,
            "walk_to 2721,9700 -> " .. tostring(er) .. " " .. tostring(ed) .. "; at " .. tile_text(etr, east))
        t.exec("openGoldGate", t.player.pass_door, { closed = "famcrest_doori2h1", open = "famcrest_doori2h1_open",
            at = { 2727, 9690, 0 }, near = { 2727, 9690 }, far = { 2728, 9690 } })
        t.expect("openGoldGate.open", t.msg.expect("The gate swings open"))

        -- mineGold: two 'perfect' gold ore from goldrock2 (2732,9680).
        -- Swing until each ore lands, re-pressing the rock whenever the
        -- swinging stops: a gem find ends the mining loop ("You just found
        -- an Opal!") and the old single press then waited 250 ticks on a
        -- player standing still. Every swing is a roll on the player's own
        -- stream (seam28); on it the gems came first.
        t.exec("mineGold1", t.player.click_loc, "goldrock2", 1)
        local gold1_r = t.inv.await("perfect_gold_ore", 1, 60)
        for again = 1, 12 do
            if gold1_r == "ok" then
                break
            end
            t.exec("mineGold1.again." .. again, t.player.click_loc, "goldrock2", 1)
            gold1_r = t.inv.await("perfect_gold_ore", 1, 60)
        end
        t.exec("mineGold1.ore", t.inv.await, "perfect_gold_ore", 1, 1)
        t.exec("mineGold2", t.player.click_loc, "goldrock2", 1)
        local gold2_r = t.inv.await("perfect_gold_ore", 2, 60)
        for again = 1, 12 do
            if gold2_r == "ok" then
                break
            end
            t.exec("mineGold2.again." .. again, t.player.click_loc, "goldrock2", 1)
            gold2_r = t.inv.await("perfect_gold_ore", 2, 60)
        end
        t.exec("mineGold2.ore", t.inv.await, "perfect_gold_ore", 2, 1)

        -- ---------------------------------------------------------------
        -- smeltGold: Quest Helper's furnace WorldPoint 3273,3186 (Al
        -- Kharid) is fai_falador_furnace; smelting.rs2's use_furnace has
        -- `case perfect_gold_ore : @smelt_ore_single(perfect_gold_bar)`.
        -- Out of the dungeon by Varrock Teleport, overland to the street
        -- east of the furnace room (reach.py 3213,3424 -> 3282,3185 REACH
        -- closed-doors len=318). The room's one way in is its east door
        -- 3279,3185, which the map places standing open (poordooropen; the
        -- walk from the street to the furnace crosses that tile and no
        -- other opening): pass_door asserts the open leaf in and out.
        -- ---------------------------------------------------------------
        t.player.teleport_cast("varrock_teleport", { 3213, 3424, 0 }, { name = "smeltGold.varrockTeleport",
            runes = VARROCK_RUNES, where = "Varrock, out of the Witchaven dungeon, for Al Kharid" })
        t.exec("goto-alkharidFurnace", t.player.goto_tile, 3282, 3185, 0)
        t.exec("alkharidFurnace.doorIn", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 3279, 3185, 0 }, near = { 3280, 3185 }, far = { 3275, 3186 } })
        t.ticks(2)
        t.exec("smeltGold1", t.player.use_on, "perfect_gold_ore", t.player.by_symbol("loc", "fai_falador_furnace"))
        t.exec("smeltGold1.bar", t.inv.await, "perfect_gold_bar", 1, 15)
        t.exec("smeltGold2", t.player.use_on, "perfect_gold_ore", t.player.by_symbol("loc", "fai_falador_furnace"))
        t.exec("smeltGold2.bar", t.inv.await, "perfect_gold_bar", 2, 15)
        counts("smeltGold.used", { { "perfect_gold_ore", 0 }, { "perfect_gold_bar", 2 } },
            "both perfect gold ores used on the furnace and smelted")

        -- makeNecklace / makeRing: a perfect gold bar on the furnace
        -- opens crafting_gold (jewellery.rs2 craft_gold_menu); its
        -- ruby_necklace / ruby_ring cells (jewellery_if.rs2) run
        -- craft_gold_once, which swaps in perfect_ruby_necklace/_ring
        -- while a perfect_gold_bar is held.
        t.exec("makeNecklace", t.player.use_on, "perfect_gold_bar", t.player.by_symbol("loc", "fai_falador_furnace"))
        local n_open = t.ui.await_open("crafting_gold", 10)
        local n_wr, n_widget = t.ui.widget("crafting_gold:ruby_necklace")
        t.check("makeNecklace.menu", n_open == "ok" and n_wr == "ok",
            "await_open crafting_gold -> " .. tostring(n_open) .. "; widget crafting_gold:ruby_necklace -> " .. tostring(n_wr) .. " " .. tostring(n_widget))
        local n_ir, n_id = "not_visible", "no crafting_gold:ruby_necklace widget"
        if n_widget then
            n_ir, n_id = t.ui.invoke(n_widget, 1)
        end
        local n_ar, n_ad = t.inv.await("perfect_ruby_necklace", 1, 10)
        t.check("makeNecklace.made", n_ar == "ok",
            "invoke ruby_necklace -> " .. tostring(n_ir) .. " " .. tostring(n_id) .. "; inv.await perfect_ruby_necklace -> " .. tostring(n_ar) .. " " .. tostring(n_ad))
        t.key("escape")
        t.ticks(2)
        counts("makeNecklace.used", { { "perfect_gold_bar", 1 }, { "ruby", 1 }, { "necklace_mould", 1 },
            { "perfect_ruby_necklace", 1 } }, "one perfect gold bar and one ruby used, the mould kept")

        t.exec("makeRing", t.player.use_on, "perfect_gold_bar", t.player.by_symbol("loc", "fai_falador_furnace"))
        local r_open = t.ui.await_open("crafting_gold", 10)
        local r_wr, r_widget = t.ui.widget("crafting_gold:ruby_ring")
        t.check("makeRing.menu", r_open == "ok" and r_wr == "ok",
            "await_open crafting_gold -> " .. tostring(r_open) .. "; widget crafting_gold:ruby_ring -> " .. tostring(r_wr) .. " " .. tostring(r_widget))
        local r_ir, r_id = "not_visible", "no crafting_gold:ruby_ring widget"
        if r_widget then
            r_ir, r_id = t.ui.invoke(r_widget, 1)
        end
        local r_ar, r_ad = t.inv.await("perfect_ruby_ring", 1, 10)
        t.check("makeRing.made", r_ar == "ok",
            "invoke ruby_ring -> " .. tostring(r_ir) .. " " .. tostring(r_id) .. "; inv.await perfect_ruby_ring -> " .. tostring(r_ar) .. " " .. tostring(r_ad))
        t.key("escape")
        t.ticks(2)
        counts("makeRing.used", { { "perfect_gold_bar", 0 }, { "ruby", 0 }, { "ring_mould", 1 },
            { "perfect_ruby_ring", 1 } }, "the second perfect gold bar and ruby used, the mould kept")

        -- ---------------------------------------------------------------
        -- returnToMan: crest_avan.rs2 @avan_jewelry at spoken_boot takes
        -- the ring and necklace, sets avan_piece, hands over caleb_crest,
        -- then @crest_avan_johnathon.
        -- ---------------------------------------------------------------
        t.exec("alkharidFurnace.doorOut", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 3279, 3185, 0 }, near = { 3278, 3185 }, far = { 3281, 3185 } })
        t.exec("goto-avanReturn", t.player.goto_tile, 3295, 3286, 0)
        t.exec("returnToMan", t.player.talk_to, "avan")
        t.exec("returnToMan-dialog", t.chat.play, {
            "npc:So how are you doing getting me my perfect gold jewelry?",
            "player:I have the ring and necklace right here.",
            "mesbox:You hand Avan the perfect gold ring and necklace.",
            "npc:These... these are exquisite!",
            "npc:Now, I suppose you will be wanting to find my brother Johnathon",
            "player:That's correct.",
            "npc:he was studying the magical arts",
            "npc:Unsurprisingly, I do not believe",
            "npc:some tavern or other near the edge of The Wilderness",
            "player:Thanks Avan.",
        })
        t.chat.close()
        counts("returnToMan.piece", { { "perfect_ruby_ring", 0 }, { "perfect_ruby_necklace", 0 }, { "caleb_crest", 1 } },
            "ring and necklace handed to Avan and his crest piece (caleb_crest) received")
        t.expect("quest.stage.avan_piece", t.quest.expect_stage("avan_piece"))

        -- ---------------------------------------------------------------
        -- goUpToJohnathon / talkToJohnathon: upstairs in the Jolly Boar
        -- (johnathon_fitzharmon, m51_54.spawn:26, 3279,3503,1). Overland to
        -- the inn's north doorway (reach.py REACH closed-doors len=304; the
        -- doorway 3280-3281,3506 has no door loc), walked in to the stairs,
        -- climbed by click.
        -- ---------------------------------------------------------------
        local function in_inn_ground(tt)
            return tt.level == 0 and tt.x >= 3275 and tt.x <= 3286 and tt.z >= 3486 and tt.z <= 3506
        end
        local function in_inn_upstairs(tt)
            return tt.level == 1 and tt.x >= 3275 and tt.x <= 3286 and tt.z >= 3486 and tt.z <= 3506
        end
        t.exec("goto-jollyBoar", t.player.goto_tile, 3280, 3512, 0)
        t.exec("jollyBoar.walkIn", t.player.walk_route, { { 3281, 3505 }, { 3281, 3497 }, { 3282, 3490 }, { 3284, 3492 } })
        -- The staircase has no maplink.dbrow row: ladders.rs2 [proc,climb]
        -- lands the player in the upper room in front of the stair top
        -- (run b60: 3285,3496,1), graded by t.player.climb on the level and
        -- the landing.
        t.exec("goUpToJohnathon", t.player.climb, { loc = "fai_varrock_stairs_taller", op = 1, op_name = "Climb-up",
            at = { 3285, 3493, 0 }, dest = { 3285, 3496, 1 }, slack = 2, landed_ok = in_inn_upstairs,
            landed_desc = "in the Jolly Boar's upper room x 3275-3286 z 3486-3506" })
        local jw_r, jw_d = t.player.walk_to(3279, 3501, 30)
        local jt_r, jt = t.world.tile()
        t.check("jollyBoar.toJohnathon", jt_r == "ok" and in_inn_upstairs(jt) and math.abs(jt.x - 3279) <= 1 and math.abs(jt.z - 3501) <= 1,
            "walk_to 3279,3501,1 -> " .. tostring(jw_r) .. " " .. tostring(jw_d) .. "; at " .. tile_text(jt_r, jt))
        t.exec("talkToJohnathon", t.player.talk_to, "johnathon_fitzharmon")
        t.exec("talkToJohnathon-dialog", t.chat.play, {
            "player:Greetings. Would you happen to be Johnathon Fitzharmon?",
            "npc:That... I am...",
            "player:I am here to retrieve your fragment",
            "npc:The.. poison.. it is all.. too much",
            "mesbox:Sweat is pouring down",
        })
        t.chat.close()
        t.expect("quest.stage.spoken_johnathon", t.quest.expect_stage("spoken_johnathon"))

        -- giveJohnathonAntipoison: [opnpcu,johnathon_fitzharmon] with any
        -- antipoison dose at spoken_johnathon -> cured_johnathon.
        t.exec("giveJohnathonAntipoison", t.player.use_on, "3doseantipoison", t.player.by_symbol("npc", "johnathon_fitzharmon"))
        t.exec("giveJohnathonAntipoison-dialog", t.chat.play, {
            "npc:That's completely cured me!",
            "npc:How can I reward you?",
            "player:I've come here for your piece of the Fitzharmon family crest.",
            "npc:Unfortunately I don't have it any more",
            "npc:our last battle when he bested me",
            "choose:Where can I find Chronozon?",
            "player:Where can I find Chronozon?",
            "npc:The fiend has made his lair in the Wilderness below the Obelisk of Air.",
            "choose:I will be on my way now.",
            "player:I will be on my way now.",
            "npc:My thanks for the assistance adventurer.",
        })
        t.chat.close()
        t.expect("quest.stage.cured_johnathon", t.quest.expect_stage("cured_johnathon"))
        counts("giveJohnathonAntipoison.used", { { "3doseantipoison", 0 } },
            "the antipoison used on Johnathon left the pack (crest_johnathon.rs2 inv_del last_useitem)")

        -- Down the stairs and out of the inn's north doorway.
        local sd_r, sd_d = t.player.walk_to(3284, 3492, 30)
        local sd_tr, sd_t = t.world.tile()
        t.check("jollyBoar.toStairsTop", sd_tr == "ok" and in_inn_upstairs(sd_t) and math.abs(sd_t.x - 3284) <= 1 and math.abs(sd_t.z - 3492) <= 1,
            "walk_to 3284,3492,1 -> " .. tostring(sd_r) .. " " .. tostring(sd_d) .. "; at " .. tile_text(sd_tr, sd_t))
        t.exec("jollyBoar.stairsDown", t.player.climb, { loc = "fai_varrock_stairs_top", op = 1, op_name = "Climb-down",
            at = { 3285, 3493, 1 }, dest = { 3285, 3492, 0 }, slack = 2, landed_ok = in_inn_ground,
            landed_desc = "on the Jolly Boar's ground floor x 3275-3286 z 3486-3506" })
        t.exec("jollyBoar.walkOut", t.player.walk_route, { { 3282, 3490 }, { 3281, 3497 }, { 3281, 3505 }, { 3280, 3512 } })

        -- ---------------------------------------------------------------
        -- killChronizon (m48_155.spawn:9, 3087,9937,0; the guide's
        -- goDownToChronizon is the Edgeville trapdoor -- plain travel).
        -- player_magic.rs2:285 calls ~chronozon_spell on every LANDED
        -- blast, which prints "Chronozon weakens..."; [ai_queue3,chronozon]
        -- regenerates him unless all four bits are set. Each blast is cast
        -- until its own new "weakens" line, then fire blast finishes him.
        -- ---------------------------------------------------------------
        -- Edgeville (reach.py 3280,3512 -> 3096,3466 REACH len=248): the
        -- ruined house's doorway (elfdooropen 3092,3470, the map leaves it
        -- open), the trapdoor 3097,3468 Open then Climb-down (trapdoors.rs2:
        -- p_telejump(coord + 6400)), then the dungeon on foot: the gate
        -- metalgateclosedl 3103,9910 and the members' gate membergatel
        -- 3131,9917 (gates.rs2 [label,member_fencegate_try], walk-through)
        -- are the only way to the lair (comp.py: 367 / 555 / 910-tile
        -- pockets, reach.py NEEDS-DOOR without them).
        local sp1, spd1 = t.cheat("::passive poisonspider")
        t.check("passive.poisonspider", sp1 == "ok", "::passive poisonspider -> " .. tostring(sp1) .. " " .. tostring(spd1))
        t.exec("goto-goDownToChronizon", t.player.goto_tile, 3090, 3470, 0)
        t.exec("edgeville.ruinDoorway", t.player.pass_door, { closed = "elfdoor", open = "elfdooropen",
            at = { 3092, 3470, 0 }, near = { 3090, 3470 }, far = { 3093, 3470 } })
        t.exec("edgeville.toTrapdoor", t.player.walk_route, { { 3096, 3468 } })
        local op_r, op_d = t.player.click_loc("trapdoor", 1, { at = { 3097, 3468, 0 } })
        -- loc_change(trapdoor_open) lands a tick or two after the press's
        -- chat line (run 1: the first Climb-down press found no open copy).
        local td_r, td = "not_found", nil
        local tdo_r = 0
        for wait = 1, 6 do
            t.ticks(1)
            tdo_r = wait
            td_r, td = t.world.loc_near("trapdoor_open", 3, { at = { 3097, 3468, 0 } })
            if td_r == "ok" then
                break
            end
        end
        t.check("goDownToChronizon.openTrapdoor", td_r == "ok",
            "click_loc(trapdoor, Open, at 3097,3468,0) -> " .. tostring(op_r) .. " " .. tostring(op_d) .. "; read after "
                .. tostring(tdo_r) .. " tick(s): trapdoor_open at 3097,3468,0 -> " .. tostring(td_r)
                .. " " .. tostring(type(td) == "table" and (td.tile_x .. "," .. td.tile_z .. "," .. td.level) or td))
        t.exec("goDownToChronizon", t.player.climb, { loc = "trapdoor_open", op = 1, op_name = "Climb-down",
            at = { 3097, 3468, 0 }, src = { 3096, 3468 }, dest = { 3096, 9868, 0 } })
        t.exec("chronozon.walkToGate", t.player.walk_route, { { 3096, 9876 }, { 3095, 9883 }, { 3095, 9891 },
            { 3095, 9899 }, { 3096, 9906 }, { 3100, 9910 }, { 3102, 9910 } })
        t.exec("chronozon.dungeonGate", t.player.pass_door, { closed = "metalgateclosedl", open = "metalgateopenl",
            at = { 3103, 9910, 0 }, near = { 3102, 9910 }, far = { 3105, 9910 } })
        t.exec("chronozon.walkToMemberGate", t.player.walk_route, { { 3112, 9911 }, { 3120, 9911 }, { 3128, 9911 },
            { 3131, 9916 } })
        t.exec("chronozon.memberGate", t.player.cross_gate, { loc = "membergatel", at = { 3131, 9917, 0 },
            near = { 3131, 9916 }, far_ok = function(tile) return tile.z >= 9918 end,
            far_desc = "north of the dungeon's members' gate, z >= 9918" })
        t.exec("chronozon.walkToLair", t.player.walk_route, { { 3132, 9925 }, { 3132, 9933 }, { 3132, 9941 },
            { 3130, 9947 }, { 3123, 9948 }, { 3117, 9950 }, { 3116, 9957 }, { 3110, 9957 }, { 3106, 9955 },
            { 3098, 9955 }, { 3092, 9953 }, { 3090, 9947 }, { 3090, 9939 }, { 3091, 9934 } },
            { vitals = { eat = "shark", below = 60 } })
        local cz_r, cz = t.npc.nearest("chronozon", 15)
        t.check("chronozon.present", cz_r == "ok",
            "npc.nearest chronozon -> " .. tostring(cz_r) .. " " .. tostring(type(cz) == "table" and ("slot " .. tostring(cz.slot) .. " at " .. tostring(cz.x) .. "," .. tostring(cz.z)) or cz))
        local hp_low = nil
        local function sample_hp()
            local hr, hp = t.skill.read("hitpoints")
            if hr == "ok" and type(hp) == "table" and hp.level and (hp_low == nil or hp.level < hp_low) then
                hp_low = hp.level
            end
            return hr, hp
        end
        for _, blast in ipairs({ "wind_blast", "water_blast", "earth_blast", "fire_blast" }) do
            local weakened = false
            local casts = 0
            local last = "none"
            for attempt = 1, 12 do
                local hr, hp = sample_hp()
                if hr == "ok" and type(hp) == "table" and hp.level < 60 then
                    t.player.inv_op("shark", 1)
                    t.ticks(2)
                end
                local mark = -1
                local mr, mlist = t.msg.last(100)
                if mr == "ok" and type(mlist) == "table" then
                    for i = 1, #mlist do
                        if string.find(tostring(mlist[i].text), "Chronozon weakens", 1, true) and mlist[i].serial > mark then
                            mark = mlist[i].serial
                        end
                    end
                end
                local cr, cd = t.player.cast(blast, "chronozon", 14)
                casts = attempt
                last = tostring(cr) .. " " .. tostring(cd)
                t.ticks(2)
                sample_hp()
                local newest = -1
                local nr, nlist = t.msg.last(100)
                if nr == "ok" and type(nlist) == "table" then
                    for i = 1, #nlist do
                        if string.find(tostring(nlist[i].text), "Chronozon weakens", 1, true) and nlist[i].serial > newest then
                            newest = nlist[i].serial
                        end
                    end
                end
                if newest > mark then
                    weakened = true
                    break
                end
            end
            t.check("killChronizon." .. blast, weakened, "new 'Chronozon weakens...' line after " .. casts .. " cast(s); last cast: " .. last)
        end
        local bits_r, bits = t.var.server("varp6189_crest_spells_levers_gauntlets")
        t.check("killChronizon.allFourBlasts", bits_r == "ok" and type(bits) == "number" and bits % 16 == 15,
            "crest_spells_levers_gauntlets=" .. tostring(bits) .. " (low four bits = ^crest_all_spells_cast 15)")
        -- With all four bits set, one fire blast engages him and
        -- await_dead_engaged re-casts it on every stall and eats a shark
        -- whenever hitpoints fall under 70 (seam27: a cast fight stalls on
        -- health alone, and opts.eat). The kill is corroborated by the
        -- johnathon_crest drop below, not by absence.
        t.exec("killChronizon.cast", t.player.cast, "fire_blast", "chronozon", 14)
        local kd_r, kd_d = t.exec("killChronizon", t.npc.await_dead_engaged, 240, 40, { eat = { item = "shark", below = 70 } })
        local fight_low = tonumber(string.match(tostring(kd_d), "lowest hp (%d+)/"))
        if fight_low ~= nil and (hp_low == nil or fight_low < hp_low) then
            hp_low = fight_low
        end
        local sk_r, sharks = t.inv.count("shark")
        t.check("killChronizon.margin", kd_r == "ok" and hp_low ~= nil and hp_low >= 25 and sk_r == "ok" and sharks >= 1,
            "Chronozon: lowest hp " .. tostring(hp_low) .. "/99 (sampled every blast and by the kill's eater: "
                .. tostring(fight_low) .. "), sharks left " .. tostring(sharks) .. " (" .. tostring(sk_r)
                .. ") of 8 (margin: lowest hp >= 25, a quarter of 99, AND food left)")

        -- pickUpCrest3: the drop is obj_add(npc_coord, johnathon_crest)
        -- at cured_johnathon.
        -- Two pieces are not a crest (crest_quest.rs2 [label,combine_crest_parts]
        -- needs all three). This press also retires the fire blast the kill's
        -- last re-cast left armed: with target mode live every later world
        -- press reads `covered ... menu rows: <Cancel>` (run 1 rows 138, 147).
        t.ticks(3)
        local nm_since = 0
        do
            local mr, ml = t.msg.last(20)
            if mr == "ok" and type(ml) == "table" then
                for i = 1, #ml do
                    if ml[i].serial > nm_since then
                        nm_since = ml[i].serial
                    end
                end
            end
        end
        local nr_r, nr_d = t.player.use_item_on_item("avan_crest", "caleb_crest")
        t.ticks(2)
        local said = false
        do
            local mr, ml = t.msg.last(20)
            if mr == "ok" and type(ml) == "table" then
                for i = 1, #ml do
                    if ml[i].serial > nm_since and string.find(tostring(ml[i].text), "You still need one more piece of the crest.", 1, true) then
                        said = true
                    end
                end
            end
        end
        local nf_r, nf = t.inv.count("family_crest")
        local na_r, na = t.inv.count("avan_crest")
        t.check("repairCrest.needsThree", said and nf_r == "ok" and nf == 0 and na_r == "ok" and na == 1,
            "use avan_crest on caleb_crest with two pieces -> " .. tostring(nr_r) .. " " .. tostring(nr_d)
                .. "; 'You still need one more piece of the crest.' seen: " .. tostring(said)
                .. "; family_crest " .. tostring(nf) .. " (want 0), avan_crest " .. tostring(na) .. " (want 1)")
        local cc_r, cc = t.world.obj_near("johnathon_crest", 8)
        t.check("pickUpCrest3.seen", cc_r == "ok",
            "world.obj_near johnathon_crest -> " .. tostring(cc_r) .. " " .. tostring(type(cc) == "table" and (tostring(cc.tile_x) .. "," .. tostring(cc.tile_z)) or cc))
        local tk_r, tk_d = t.player.click_obj("johnathon_crest", 3)
        local pk_r, pk_d = t.inv.await("johnathon_crest", 1, 20)
        t.check("pickUpCrest3", pk_r == "ok", "click_obj johnathon_crest -> " .. tostring(tk_r) .. " " .. tostring(tk_d) .. "; inv.await -> " .. tostring(pk_r) .. " " .. tostring(pk_d))

        -- repairCrest: [opheldu,avan_crest] with johnathon_crest while all
        -- three pieces are held -> family_crest (crest_quest.rs2).
        t.exec("repairCrest", t.player.use_item_on_item, "avan_crest", "johnathon_crest")
        t.exec("repairCrest.crest", t.inv.await, "family_crest", 1, 10)
        counts("repairCrest.used", { { "avan_crest", 0 }, { "caleb_crest", 0 }, { "johnathon_crest", 0 }, { "family_crest", 1 } },
            "the three crest pieces combined into the family crest")

        -- ---------------------------------------------------------------
        -- returnCrest: crest_dimintheis.rs2 at %crestquest > spoken_caleb
        -- with family_crest: steel_gauntlets, crest_complete,
        -- ~quest_complete_rewards(quest_familycrest, "Steel gauntlets").
        -- ---------------------------------------------------------------
        local sg_r, sg_before = t.inv.count("steel_gauntlets")
        t.check("reward.steel_gauntlets.before", sg_r == "ok" and sg_before == 0,
            "steel_gauntlets before hand-in: " .. tostring(sg_before) .. " (" .. tostring(sg_r) .. ", want 0: setup gives none)")
        -- Out of the lair by Varrock Teleport (wilderness level ~3, under
        -- teleport.rs2's level-20 gate), then the east gate again.
        t.player.teleport_cast("varrock_teleport", { 3213, 3424, 0 }, { name = "returnCrest.varrockTeleport",
            runes = VARROCK_RUNES, where = "Varrock, out of the Edgeville dungeon" })
        t.exec("goto-dimintheisReturn.eastGate", t.player.goto_tile, 3262, 3405, 0)
        east_gate_in("returnCrest.eastGateIn")
        dimintheis_house_in("returnCrest.houseDoorIn")
        t.exec("returnCrest", t.player.talk_to, "dimintheis")
        t.exec("returnCrest-dialog", t.chat.play, {
            "player:I have retrieved your crest.",
            "npc:Adventurer... I can only thank you",
            "npc:You are truly a hero",
            "npc:I know not how I can adequately reward you",
            "npc:I do have these mystical gauntlets",
            "npc:whenever lost, or if the owner has died",
            "npc:They can also be granted extra powers",
        })
        t.exec("quest.stage.complete", t.var.await_server, "varp148_crestquest", 11, 10)
        t.ticks(3)
        t.quest.expect_complete()
        -- Rewards (wiki / crest_dimintheis.rs2 ~quest_complete_rewards): 1
        -- quest point (quest.points above, delta 1) and one pair of steel
        -- gauntlets, 0 -> 1.
        local sa_r, sg_after = t.inv.count("steel_gauntlets")
        t.check("reward.steel_gauntlets", sa_r == "ok" and sg_before == 0 and sg_after == 1,
            "scroll reward 'Steel gauntlets': steel_gauntlets " .. tostring(sg_before) .. " before hand-in, now "
                .. tostring(sg_after) .. " (" .. tostring(sa_r) .. "; want exactly 0 -> 1)")
        t.finish(0)
        return
    end,
}
