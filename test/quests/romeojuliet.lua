-- Romeo & Juliet, end to end through the real client, as OSRS plays it:
-- RSProx index 4361 (2026-10-24, rev 241, a normal world) for every page the
-- lists below name, the stage ticks, the berry bush and both scenes. The quest
-- was first ported from LostCity's 2004 scripts, and this file then followed
-- that text; OSRS rewrote all of it (docs/rsprox-analysis/quests/
-- romeo_and_juliet.md), so the lists are the recording's now.
--
-- Travel notes kept from the first version (each checked against the map):
--
--   2. Every closed space is entered and left on foot (docs/
--      QUEST_ORCHESTRATOR.md standing rule, 2026-10-03; start-and-travel.md
--      "The grader now catches the goto inside"). A goto departs only from
--      an open street tile and lands only on one; the walls and doors below
--      are read off the map squares (maps/m49_53.jl2, m50_53.jl2,
--      m50_54.jl2) and checked with reach.py's closed-door flood:
--      - Juliet's house (m49_53): the street door fai_varrock_castle_door
--        on the west edge of 3165,3433; the staircase
--        fai_varrock_stairs_taller (3156-3158,3435-3436, level 0) climbed
--        up with its own Climb-up op (the guide's goUpToJuliet/
--        goUpToJuliet2) from 3159,3435 to the stair top's WEST foot
--        3155,3435,1 in the stair room (maplink.dbrow
--        maplink_0_49_53_23_43_up, transports.tsv:921; the 2004 game agrees:
--        LostCity_Content2 scripts/ladders+stairs/scripts/stairs.rs2
--        [oploc1,loc_1722] "Julia house" telejumps 0_49_53_20_43 ->
--        1_49_53_19_43, and fai_varrock_stairs_top's forceapproach faces
--        west -- the tiles east of the top, 3158,3435-3436,1, are a sealed
--        two-tile pocket), and fai_varrock_stairs_top (3156,3435, level 1)
--        climbed down from 3155,3435 (maplink.dbrow
--        maplink_1_49_53_19_43_down -> 3159,3435,0; the guide's
--        goDownstairsTo* steps); upstairs the hall door 3157,3430 and the
--        balcony door 3158,3426 (Juliet stands at 3158,3425, level 1).
--      - The Apothecary's shop (x 3192-3198, z 3403-3406): its west door
--        stands open in the map (fai_varrock_door_open at 3192,3403), so it
--        is asserted open and walked through, both ways.
--      - Father Lawrence's church (x 3252-3259, z 3471-3488): its south
--        doorway is two permanently open leaves with no op
--        (fai_varrock_museum_door_inactive_r/_l at 3255,3471/3256,3471),
--        asserted on their tiles and walked through, both ways.
--      - Romeo stands in the open Varrock square; no door.
--   3. Juliet's own spawn is a multinpc wrapper, `juliet_multi_visible`
--      (grep -rn --include='*.spawn' juliet_multi_visible
--      .../areas/world/configs/m49_53.spawn -> 3158,3425,1), one tile off
--      the scaffold's guessed 3158,3427 -- the wrapper's multinpc1/3 both
--      resolve to the `juliet` symbol this file still targets.
--
-- The cadava berries are picked, not given: the south-western bush by the
-- south-east Varrock mine is the quest's own (one berry per player while
-- %varb6530_cadavabush is 0; 4361 [385..391]), and the Apothecary takes them
-- straight away when they are already in the pack ("Conveniently, I have some
-- here.", 4361 [752]).
--
-- Fixture start: fresh_lumbridge.ini stands the player at 3206,3233,0
-- (Lumbridge, beside Hans), in the open. Romeo's goto lands on his
-- `romeo.spawn` tile; every other goto lands on the open street tile
-- outside the building its npc stands in (note 2), and the npc's own
-- `*.spawn` tile (or the `juliet_multi_visible` wrapper's, note 3) is
-- reached on foot from there.

return {
    id = "romeojuliet",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp144_rjquest",
            constants = {
                complete = 100,
                juliet_crypt = 60,
                not_started = 0,
                passed_message = 30,
                questpoints = 5,
                spoken_apothecary = 50,
                spoken_father = 40,
                spoken_juliet = 20,
                spoken_romeo = 10,
            },
            row = "quest_romeoandjuliet",
            display = "Romeo & Juliet",
            points = 5,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        local function tile_text(r, tt)
            return r == "ok" and (tt.x .. "," .. tt.z .. "," .. tt.level) or tostring(r)
        end

        -- Cross one door on foot (the pattern wanted.lua proved). Walk to the
        -- tile on this side of it, then press the CLOSED leaf's copy on the
        -- door tile, on this level (click_loc's `at` selector presses nothing
        -- and answers `no_row` when no closed copy stands there). The level
        -- matters: the house has a fai_varrock_castle_door at 3165,3433 on
        -- BOTH floors, and a bare loc_near answers whichever copy the pool
        -- lists first. Either way the next row asserts the OPEN leaf stands on
        -- or beside the door tile on this level -- after the press, or (on
        -- `no_row`) because an earlier press left it open (an opened door
        -- re-closes after 500 ticks) or the map places it open, in which case
        -- it is walked through, not pressed again. Then walk to the far side
        -- and check the tile.
        local function pass_door(prefix, closed_sym, open_sym, door_x, door_z, near_x, near_z, far_x, far_z, far_ok, far_desc)
            t.player.walk_to(near_x, near_z, 30)
            local nr, nt = t.world.tile()
            t.check(prefix .. ".atDoor", nr == "ok" and math.abs(nt.x - near_x) <= 1 and math.abs(nt.z - near_z) <= 1,
                "walked to " .. near_x .. "," .. near_z .. " beside the door at " .. door_x .. "," .. door_z .. " -> " .. tile_text(nr, nt))
            local here = nr == "ok" and nt.level or -1
            local pr, pd = t.player.click_loc(closed_sym, 1, { at = { door_x, door_z, here } })
            if pr ~= "no_row" then
                t.ticks(1)
            end
            local orr, od = t.world.loc_near(open_sym, 2)
            local open_here = orr == "ok" and od.level == here and math.abs(od.tile_x - door_x) <= 1 and math.abs(od.tile_z - door_z) <= 1
            local leaf = open_sym .. ": " .. (orr == "ok" and ("open leaf at " .. od.tile_x .. "," .. od.tile_z .. "," .. tostring(od.level)) or tostring(orr))
                .. " (want within 1 of the door tile " .. door_x .. "," .. door_z .. "," .. tostring(here) .. ")"
            if pr == "no_row" then
                t.check(prefix .. ".doorStandsOpen", open_here,
                    "no closed " .. closed_sym .. " on the door tile (" .. tostring(pd) .. "); " .. leaf
                        .. " -- already standing open, so it is walked through, not pressed again")
            else
                t.check(prefix .. ".openDoor", pr == "ok" and open_here,
                    "click_loc " .. closed_sym .. " op1 at " .. door_x .. "," .. door_z .. "," .. tostring(here) .. " -> " .. tostring(pr) .. " " .. tostring(pd) .. "; " .. leaf)
            end
            t.player.walk_to(far_x, far_z, 30)
            local fr, ft = t.world.tile()
            t.check(prefix .. ".throughDoor", fr == "ok" and far_ok(ft),
                "walked through to " .. far_x .. "," .. far_z .. " -> " .. tile_text(fr, ft) .. " (want " .. far_desc .. ")")
        end

        -- Climb one staircase with its own op and grade the landing tile.
        local function climb(name, sym, want_level, want_ok, want_desc)
            t.exec(name, t.player.click_loc, sym, 1)
            local lr, ld = t.await({
                level = function()
                    local r, lv = t.world.level()
                    return r == "ok" and lv == want_level
                end,
                note = name .. ".level_settle",
            }, 10)
            local tr, tt = t.world.tile()
            t.check(name .. ".landed", lr == "ok" and tr == "ok" and tt.level == want_level and want_ok(tt),
                "after the " .. sym .. " climb: " .. tostring(lr) .. " " .. tostring(ld) .. "; tile " .. tile_text(tr, tt) .. " (want " .. want_desc .. ")")
        end

        -- Juliet's house, street door, stairs, hall door, balcony door
        -- (note 2). Lands on the balcony beside Juliet, level 1.
        local function up_to_juliet(pfx, guide_up)
            pass_door(pfx .. ".houseDoorIn", "fai_varrock_castle_door", "fai_varrock_castle_door_open", 3165, 3433, 3165, 3433, 3163, 3433,
                function(tt) return tt.level == 0 and tt.x >= 3156 and tt.x <= 3164 and tt.z >= 3432 and tt.z <= 3436 end,
                "the stair hall inside, x 3156-3164 z 3432-3436, level 0")
            t.player.walk_to(3159, 3435, 20)
            climb(guide_up, "fai_varrock_stairs_taller", 1,
                function(tt) return tt.x == 3155 and (tt.z == 3435 or tt.z == 3436) end,
                "level 1, 3155,3435|3436 -- maplink_0_49_53_23_43/44_up's dest, the stair room x 3151-3158 z 3431-3439")
            pass_door(pfx .. ".hallDoorIn", "fai_varrock_castle_door", "fai_varrock_castle_door_open", 3157, 3430, 3157, 3431, 3158, 3428,
                function(tt) return tt.level == 1 and tt.z >= 3427 and tt.z <= 3430 end,
                "the landing north of the balcony, z 3427-3430, level 1")
            pass_door(pfx .. ".balconyDoorIn", "fai_varrock_castle_door", "fai_varrock_castle_door_open", 3158, 3426, 3158, 3427, 3159, 3426,
                function(tt) return tt.level == 1 and tt.x >= 3155 and tt.x <= 3161 and tt.z >= 3425 and tt.z <= 3426 end,
                "the balcony, x 3155-3161 z 3425-3426, level 1")
        end

        -- Balcony back down to the street: the balcony door, the hall door,
        -- the staircase down from its maplink tile 3155,3435, the street door.
        local function down_from_juliet(pfx, guide_down)
            pass_door(pfx .. ".balconyDoorOut", "fai_varrock_castle_door", "fai_varrock_castle_door_open", 3158, 3426, 3158, 3426, 3158, 3428,
                function(tt) return tt.level == 1 and tt.z >= 3427 and tt.z <= 3430 end,
                "the landing north of the balcony, z 3427-3430, level 1")
            pass_door(pfx .. ".hallDoorOut", "fai_varrock_castle_door", "fai_varrock_castle_door_open", 3157, 3430, 3157, 3430, 3156, 3433,
                function(tt) return tt.level == 1 and tt.x >= 3151 and tt.x <= 3158 and tt.z >= 3431 and tt.z <= 3439 end,
                "the stair room x 3151-3158 z 3431-3439, level 1")
            t.player.walk_to(3155, 3435, 20)
            climb(guide_down, "fai_varrock_stairs_top", 0,
                function(tt) return tt.x == 3159 and (tt.z == 3435 or tt.z == 3436) end,
                "3159,3435|3436,0 -- maplink_1_49_53_19_43/44_down's dest")
            pass_door(pfx .. ".houseDoorOut", "fai_varrock_castle_door", "fai_varrock_castle_door_open", 3165, 3433, 3164, 3433, 3167, 3433,
                function(tt) return tt.level == 0 and tt.x >= 3165 end,
                "the street east of the house, x >= 3165, level 0")
        end

        -- Click through a scene's pages as they come, including the ones that open by
        -- themselves after a closed chatbox (corsaircurse.lua's play_scene).
        local function play_scene(name, max_ticks)
            local clicks, idle, waited = 0, 0, 0
            while waited < max_ticks do
                if t.chat.kind() ~= "none" then
                    idle = 0
                    local r = t.chat.continue_()
                    if r == "ok" then
                        clicks = clicks + 1
                    end
                else
                    idle = idle + 1
                    if clicks > 0 and idle >= 8 then
                        break
                    end
                end
                t.ticks(1)
                waited = waited + 1
            end
            t.step(name, (clicks > 0 and t.chat.kind() == "none") and "PASS" or "FAIL",
                clicks .. " page(s) clicked over " .. waited .. " tick(s)")
        end
        local function page_has(note, want, ticks)
            t.await({ level = function()
                local r, text = t.chat.text()
                return r == "ok" and text ~= nil and string.find(text, want, 1, true) ~= nil
            end, note = note }, ticks or 15)
        end

        -- ---------------------------------------------------- talk to Romeo
        -- RSProx 4361 [168]: Romeo walks up to a player who has not started and
        -- opens his opener himself ("Romeo approaches you..."). If he has done so
        -- by the time the goto lands, the talk is already up; otherwise talk.
        t.exec("goto-talkToRomeo", t.player.goto_tile, 3211, 3425, 0) -- romeo.spawn: 3211 3425 0
        if t.chat.kind() == "none" then
            t.exec("talkToRomeo", t.player.talk_to, "romeo", 1)
        end
        -- The opener is one of six rolls; the rest is 4361 [302..340].
        t.exec("talkToRomeo-dialog", t.chat.play, {
            "npc:*",
            "choose:Yes, I have seen her actually!",
            "player:Yes, I have seen her actually!",
            "player:At least, I think it was her...",
            "npc:Golly...yes, yes",
            "npc:And I'll bet she's a bit of a fox!",
            "player:Well, I guess she could be",
            "npc:I'll bet she is!",
            "npc:Sorry, all that jubilation",
            "player:You were asking me about Juliet?",
            "npc:Oh yes, Juliet!",
            "npc:The fox...could you tell her",
            "player:What?",
            "npc:Oh yeah...what you said",
            "choose:Yes.",
            "player:Yes, okay. I'll let her know.",
            "npc:Oh great! And tell her",
            "player:You mean you want to give her a kiss!",
            "npc:Oh you're good",
            "npc:I see I've picked a true professional",
            "choose:Ok, thanks.",
            "player:Ok, thanks.",
        })
        t.expect("quest.stage.spoken_romeo", t.quest.expect_stage("spoken_romeo"))

        -- ---------------------------------------------------- talk to Juliet
        t.exec("goto-talkToJuliet", t.player.goto_tile, 3167, 3433, 0)
        up_to_juliet("juliet1", "goUpToJuliet")
        t.exec("talkToJuliet", t.player.talk_to, "juliet", 1)
        -- 4361 [489..495]: the letter comes with the box.
        t.exec("talkToJuliet-dialog", t.chat.play, {
            "player:Juliet, I come from Romeo.",
            "npc:Oh how my heart soars",
            "player:Well, I hope it's good news",
            "npc:He's quite often upset",
            "npc:Everything is explained in the letter",
            "player:Certainly, I'll do so straight away.",
            "npc:Many thanks! Oh, I'm so very grateful.",
            "*",
        })
        t.expect("quest.stage.spoken_juliet", t.quest.expect_stage("spoken_juliet"))
        t.exec("talkToJuliet.letter", t.inv.await, "julietmessage", 1, 5)

        -- ------------------------------------------ deliver the letter
        down_from_juliet("juliet1", "goDownstairsToGiveLetterToRomeo")
        t.exec("goto-giveLetterToRomeo", t.player.goto_tile, 3211, 3425, 0)
        t.exec("giveLetterToRomeo", t.player.talk_to, "romeo", 1)
        -- 4361 [508..561]: the letter goes on the "Father Lawrence?" page.
        t.exec("giveLetterToRomeo-dialog", t.chat.play, {
            "player:Romeo...great news...I've been in touch with Juliet!",
            "*",
            "npc:Oh, a message! A message!",
            "player:Really?",
            "npc:No, no, not one!",
            "npc:Oh, well, except for the occasional court summons.",
            "npc:But they're not really 'nice' messages.",
            "player:Well are you going to open it or not?",
            "npc:Oh yes, yes, of course!",
            "player:'Affection?'",
            "npc:Ahh yes...'still hold affection",
            "player:Oh dear...that doesn't sound too good.",
            "npc:What? '...great affection",
            "npc:'...marriage and will...",
            "npc:...will kill you if he sees you again!'",
            "player:I have to be honest",
            "npc:'Our only hope is that Father Lawrence",
            "*",
            "npc:Well, that's it then",
            "player:What about Father Lawrence?",
            "npc:...our love is over",
            "player:...or you could speak to Father Lawrence!",
            "npc:Oh, my aching, breaking, heart",
            "player:FATHER LAWRENCE!",
            "npc:Father Lawrence?",
            "npc:Oh yes, Father Lawrence...he's our long time confidant",
            "player:Where can I find Father Lawrence?",
            "npc:Lather Fawrence! Oh he's...",
            "npc:You know he's not my 'real' Father",
            "player:I think I suspected that he wasn't.",
            "npc:Well anyway...he tells these song",
            "choose:Ok, thanks.",
            "player:Ok, thanks.",
        })
        t.expect("quest.stage.passed_message", t.quest.expect_stage("passed_message"))
        t.exec("giveLetterToRomeo.letterGone", t.inv.expect_absent, "julietmessage")

        -- ---------------------------------------------- Father Lawrence
        -- father_lawrence.spawn: 3254 3484 0, inside the church; the goto
        -- lands on the street south of its doorway (note 2).
        t.exec("goto-talkToLawrence", t.player.goto_tile, 3255, 3468, 0)
        -- The doorway's two leaves have no op (configs/all.loc
        -- fai_varrock_museum_door_inactive_*): they stand open for good, so
        -- the row asserts the west leaf on its own tile and walks through.
        local function church_doorway(prefix, near_x, near_z, far_x, far_z, far_ok, far_desc)
            t.player.walk_to(near_x, near_z, 30)
            local lr, ld = t.world.loc_near("fai_varrock_museum_door_inactive_r", 4)
            local nr, nt = t.world.tile()
            t.check(prefix .. ".doorwayStandsOpen",
                lr == "ok" and ld.tile_x == 3255 and ld.tile_z == 3471 and nr == "ok" and math.abs(nt.x - near_x) <= 1 and math.abs(nt.z - near_z) <= 1,
                "at " .. tile_text(nr, nt) .. " (want within 1 of " .. near_x .. "," .. near_z .. "); fai_varrock_museum_door_inactive_r: "
                    .. (lr == "ok" and ("open leaf at " .. ld.tile_x .. "," .. ld.tile_z) or tostring(lr)) .. " (want 3255,3471)")
            t.player.walk_to(far_x, far_z, 30)
            local fr, ft = t.world.tile()
            t.check(prefix .. ".throughDoorway", fr == "ok" and far_ok(ft),
                "walked through to " .. far_x .. "," .. far_z .. " -> " .. tile_text(fr, ft) .. " (want " .. far_desc .. ")")
        end
        church_doorway("lawrence.churchIn", 3255, 3470, 3255, 3474,
            function(tt) return tt.level == 0 and tt.x >= 3252 and tt.x <= 3259 and tt.z >= 3471 and tt.z <= 3488 end,
            "inside the church, x 3252-3259 z 3471-3488")
        t.exec("talkToLawrence", t.player.talk_to, "father_lawrence", 1)
        local sermon_mark = t.cutscene.mark()
        -- 4361 [602..639]. The congregation's page stands five ticks with no
        -- continue and the player's answer opens by itself, so the list stops
        -- at the click before it and resumes once the answer is up.
        t.exec("talkToLawrence-dialog", t.chat.play, {
            "npc:''...and let Saradomin light the way for you...",
            "npc:Can't you see that I'm in the middle of a sermon?!",
            "player:But Romeo sent me!",
            "npc:But I'm busy delivering a sermon",
        })
        page_has("talkToLawrence.congregation", "captive audience", 15)
        t.exec("talkToLawrence-dialog2", t.chat.play, {
            "player:Yes, well, it certainly seems like you have a captive audience!",
            "npc:Ok, okay... what do you want",
            "player:Romeo sent me. He says you may be able to help.",
            "npc:Ah Romeo, yes. A fine lad",
            "player:Yes, very confused....",
            "npc:I agree, and I think I have an idea!",
            "player:Dead! Sounds a bit creepy to me...",
            "npc:The potion will only make Juliet 'appear' dead...",
            "player:Crypt! Again... very creepy!",
            "npc:Then Romeo can collect her from the crypt!",
            "player:Apart from the strong overtones of death",
        })
        t.exec("talkToLawrence.cutscene", t.cutscene.await, "talkToLawrence", { since = sermon_mark, quiet = 60, expect = {
            { op = "moveto", coord = "0_50_54_54_30", height = 325 },
            { op = "lookat", coord = "0_50_54_56_20", height = 25 },
            { op = "reset" },
        } })
        t.expect("quest.stage.spoken_father", t.quest.expect_stage("spoken_father"))
        church_doorway("lawrence.churchOut", 3255, 3472, 3255, 3468,
            function(tt) return tt.level == 0 and tt.z <= 3470 end,
            "the street south of the church, z <= 3470")

        -- ---------------------------------------------- the cadava berries
        -- The south-western bush is the quest's own (cadava_bush.rs2): one berry
        -- per player while %varb6530 is 0. RSProx 4361 [385..391].
        t.exec("goto-pickCadavaBerries", t.player.goto_tile, 3262, 3367, 0)
        t.exec("pickCadavaBerries", t.player.click_loc, "fai_varrock_cadavabush_tailored", 1, { at = { 3264, 3366, 0 } })
        t.exec("pickCadavaBerries.berry", t.inv.await, "cadavaberries", 1, 8)
        t.check("pickCadavaBerries.bushVar", t.var.expect("varb6530_cadavabush", 1))

        -- ------------------------------------------------ the Apothecary
        -- apothecary.spawn: 3195 3404 0, inside his shop; the goto lands on
        -- the street west of its door (note 2).
        t.exec("goto-talkToApothecary", t.player.goto_tile, 3190, 3403, 0)
        pass_door("apothecary.shopDoorIn", "fai_varrock_door", "fai_varrock_door_open", 3192, 3403, 3191, 3403, 3193, 3403,
            function(tt) return tt.level == 0 and tt.x >= 3192 and tt.x <= 3198 and tt.z >= 3402 and tt.z <= 3406 end,
            "inside the shop, x 3192-3198 z 3402-3406")
        t.exec("talkToApothecary", t.player.talk_to, "apothecary", 1)
        -- 4361 [740..759]: the stage on "I have all that", the berries handed
        -- straight over, a blank chatbox while he brews, then "Phew!".
        t.exec("talkToApothecary-dialog", t.chat.play, {
            "npc:I am the Apothecary. I brew potions.",
            "choose:Talk about something else.",
            "choose:Talk about Romeo & Juliet.",
            "player:Apothecary, Father Lawrence sent me.",
            "npc:Cadava potion. It's pretty nasty.",
            "npc:Wing of rat, tail of frog.",
            "npc:I have all that, but I need some Cadava berries.",
            "npc:You will have to find them while I get the rest ready.",
            "player:Conveniently, I have some here.",
            "*",
        })
        page_has("talkToApothecary.brewed", "Phew!", 10)
        t.exec("talkToApothecary-dialog2", t.chat.play, {
            "npc:Phew! Here is what you need.",
            "*",
        })
        t.expect("quest.stage.spoken_apothecary", t.quest.expect_stage("spoken_apothecary"))
        t.exec("talkToApothecary.potion", t.inv.await_all, { cadava = 1, cadavaberries = 0 }, 10)
        t.check("talkToApothecary.bushVar", t.var.expect("varb6530_cadavabush", 0))

        pass_door("apothecary.shopDoorOut", "fai_varrock_door", "fai_varrock_door_open", 3192, 3403, 3193, 3403, 3190, 3403,
            function(tt) return tt.level == 0 and tt.x <= 3191 end,
            "the street west of the shop, x <= 3191")

        -- --------------------------------------------- give the potion to Juliet
        t.exec("goto-givePotionToJuliet", t.player.goto_tile, 3167, 3433, 0)
        up_to_juliet("juliet2", "goUpToJuliet2")
        t.exec("givePotionToJuliet", t.player.talk_to, "juliet", 1)
        local potion_mark = t.cutscene.mark()
        -- 4361 [794..806], then the scene in a copy of the house ([806]..[837]).
        t.exec("givePotionToJuliet-dialog", t.chat.play, {
            "player:Hi Juliet! I have an interesting proposition",
            "npc:Go on....",
            "player:I have a Cadava potion here",
            "npc:Yes...",
            "player:And when you appear dead",
            "npc:Oooooh, a cold dark creepy crypt...",
            "npc:...sounds just peachy!",
            "player:Then...Romeo can steal into the crypt",
            "npc:...and this is the great idea",
            "player:To be fair, I can't take all the credit",
            "npc:Ok...if this is the best we can do",
            "*",
            "npc:Wonderful! I just hope Romeo can remember",
            "npc:Please go to Romeo and make sure he understands.",
        })
        page_has("givePotionToJuliet.scene", "Oh, here's Phillipa", 15)
        play_scene("givePotionToJuliet-scene", 120)
        t.exec("givePotionToJuliet.cutscene", t.cutscene.await, "givePotionToJuliet", { since = potion_mark, quiet = 60, expect = {
            { op = "moveto", height = 375 },
            { op = "lookat", height = 175 },
            { op = "moveto", height = 425 },
            { op = "lookat", height = 125 },
            { op = "reset" },
        } })
        t.expect("quest.stage.juliet_crypt", t.quest.expect_stage("juliet_crypt"))
        t.exec("givePotionToJuliet.potionGone", t.inv.expect_absent, "cadava")
        t.check("givePotionToJuliet.julietInCrypt", t.var.expect("varb1838_romjul_juliet_visible", 1))

        -- --------------------------------------------------- finish the quest
        down_from_juliet("juliet2", "goDownstairsToFinishQuest")
        t.exec("goto-finishQuest", t.player.goto_tile, 3211, 3425, 0)
        t.exec("finishQuest", t.player.talk_to, "romeo", 1)
        local crypt_mark = t.cutscene.mark()
        -- 4361 [847..853], then the crypt scene in a copy ([853]..[920]); the
        -- quest completes two ticks after the copy is left.
        t.exec("finishQuest-dialog", t.chat.play, {
            "player:Romeo, it's all set.",
            "npc:Ah right, the potion! Great...",
            "npc:What potion would that be then?",
            "player:The Cadava potion...you know",
            "npc:But I'm scared...will you come with me?",
            "player:Oh, ok...come on!",
        })
        page_has("finishQuest.scene", "This is pretty scary", 15)
        play_scene("finishQuest-scene", 160)
        t.exec("finishQuest.cutscene", t.cutscene.await, "finishQuest", { since = crypt_mark, quiet = 60, expect = {
            { op = "moveto", height = 400 },
            { op = "lookat", height = 25 },
            { op = "moveto", height = 375 },
            { op = "lookat", height = 250 },
            { op = "moveto", height = 250 },
            { op = "lookat", height = 200 },
            { op = "moveto", height = 300 },
            { op = "lookat", height = 25 },
            { op = "reset" },
        } })

        t.ticks(4)
        t.check("finishQuest.julietBack", t.var.expect("varb1838_romjul_juliet_visible", 2))
        t.quest.expect_complete() -- quest.varp_complete, quest.scroll_title, quest.points, quest.journal
        t.finish(0)
    end,
}
