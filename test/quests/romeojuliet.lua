-- Romeo & Juliet, end to end through the real client.
--
-- Regenerated from tools/quest_gate/new_quest.py's scaffold (Quest Helper's
-- helpers/quests/romeoandjuliet/), then corrected against this content
-- pack's own scripts -- OSRS-Content/osrs239-content/server/scripts/quests/
-- quest_romeojuliet/scripts/quest_romeojuliet.rs2 (state machine + the
-- selftest debugproc's own state-transition order) and areas/varrock/
-- scripts/{romeo,juliet,father_lawrence,apothecary}.rs2 (the real
-- `[opnpc1,...]` handlers, chat text and branch guards). Six corrections
-- the scaffold got wrong or left unresolved, each checked line-for-line
-- against the .rs2 above:
--
--   1. talkToRomeo's dialog is missing a SECOND options page. Choosing
--      "Yes, I have seen her." (p_choice3 option 1) plays two more player
--      lines and an npc line, then romeo.rs2 opens a second
--      `~p_choice2("Yes, I will tell her.", 1, ...)` before falling into
--      `[label,romeo_tell_her]` -- the scaffold's list jumped straight from
--      the npc line to `romeo_tell_her`'s own player line with no
--      `choose:` in between, which dies with "expected kind=player, got
--      options" on a live run.
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
--   4. The scaffold skips a whole quest step. quest_romeojuliet.rs2's own
--      selftest ("cadava potion brewed from berries, rjquest stays
--      romeojuliet_spoken_apothecary") and apothecary.rs2's own
--      `[label,apothecary_make_cadava]` both show the player must talk to
--      the Apothecary a SECOND time, with the cadava berries in the
--      backpack, before there is any Cadava potion to give Juliet -- the
--      scaffold went straight from "he told me to bring berries" to
--      "give the potion to Juliet" with no potion ever brewed. Driven for
--      real here (bringBerriesToApothecary), not cheated: the berries are
--      a Quest-Helper-listed prerequisite (`::give` in setup), the potion
--      itself is the quest's own deliverable and is earned by the click.
--   5. Every op-number/branch-guard marker the scaffold left unresolved is
--      resolved by reading the same .rs2 files: `[opnpc1,...]` is always
--      op 1; the %dov (Defender of Varrock) and the Apothecary's five
--      other-quest guards (twocats_quest, onesmallfavour x2, my2arm_status,
--      fossilquest_progress, ratcatch_var) all default unset/0 on a fresh
--      character, so every one of those branches is false and never
--      entered.
--   6. quest.bind's `display` ("Romeo & Juliet") and the dbrow's
--      questpoints (5) are confirmed against
--      OSRS-Content/osrs239-content/configs/all.dbrow's own
--      `[quest_romeoandjuliet]` block; `~quest_complete_rewards(
--      quest_romeoandjuliet, "", coins)` passes an EMPTY rewards string and
--      "coins" only as the scroll's icon obj, not a coin amount -- this
--      quest has no XP or item reward, only quest points, which
--      quest.expect_complete()'s own `quest.points` row already covers.
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
        "::give cadavaberries 1", -- Quest-Helper prerequisite; the quest's own deliverable (the cadava POTION) is earned from the Apothecary by a real click below, never given
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

        -- ---------------------------------------------------- talk to Romeo
        t.exec("goto-talkToRomeo", t.player.goto_tile, 3211, 3425, 0) -- romeo.spawn: 3211 3425 0
        t.exec("talkToRomeo", t.player.talk_to, "romeo", 1)
        -- [opnpc1,romeo]'s %dov guard defaults false on a fresh character --
        -- falls straight to `if (%rjquest = ^romeojuliet_not_started) @romeojuliet_start`.
        -- Option 1 ("Yes, I have seen her.") -> a second options page
        -- (p_choice2) -> option 1 ("Yes, I will tell her.") -> [label,romeo_tell_her].
        t.exec("talkToRomeo-dialog", t.chat.play, {
            "npc:Juliet, Juliet, Juliet!  Where",
            "npc:Kind friend, have you seen Jul",
            "npc:She's disappeared and I can't ",
            "choose:Yes, I have seen her.",
            "player:Yes, I have seen her.",
            "player:I think it was her... Blonde? ",
            "npc:Yes, that sounds like her. Ple",
            "choose:Yes, I will tell her.",
            "player:Yes, I will tell her how you f",
            "npc:You are the saviour of my hear",
            "player:Err, yes. Ok. Thats.... Nice.",
        })
        t.expect("quest.stage.spoken_romeo", t.quest.expect_stage("spoken_romeo"))

        -- ---------------------------------------------------- talk to Juliet
        -- juliet_multi_visible's own spawn row: 3158 3425 1 (west of
        -- Varrock, upper floor). The goto lands on the street outside the
        -- house's east door; the rest is on foot (note 2).
        t.exec("goto-talkToJuliet", t.player.goto_tile, 3167, 3433, 0)
        up_to_juliet("juliet1", "goUpToJuliet")
        t.exec("talkToJuliet", t.player.talk_to, "juliet", 1)
        -- %rjquest = spoken_romeo -> @juliet_from_romeo -> falls into
        -- @juliet_agree_message (no options page in this branch).
        t.exec("talkToJuliet-dialog", t.chat.play, {
            "player:Juliet, I come from Romeo.",
            "player:He begs I tell you he cares st",
            "npc:Please, take this message to h",
            "player:Certainly, I will deliver your",
            "npc:It may be our only hope.",
            "mesbox:Juliet gives you a message.",
        })
        t.expect("quest.stage.spoken_juliet", t.quest.expect_stage("spoken_juliet"))

        -- ------------------------------------------ deliver the letter
        down_from_juliet("juliet1", "goDownstairsToGiveLetterToRomeo")
        t.exec("goto-giveLetterToRomeo", t.player.goto_tile, 3211, 3425, 0)
        t.exec("giveLetterToRomeo", t.player.talk_to, "romeo", 1)
        -- %rjquest = spoken_juliet -> @romeo_messagefrom (no options page).
        t.exec("giveLetterToRomeo-dialog", t.chat.play, {
            "player:Romeo, I have a message from J",
            "mesbox:You pass Juliet's message to R",
            "npc:Tragic news. Her father is opp",
            "npc:If her father sees me, he will",
            "npc:I dare not go near his lands.",
            "npc:She says Father Lawrence can h",
            "npc:Please find him for me. Tell h",
        })
        t.expect("quest.stage.passed_message", t.quest.expect_stage("passed_message"))

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
        -- %rjquest = passed_message -> @father_lawrence_help (no options page).
        t.exec("talkToLawrence-dialog", t.chat.play, {
            "player:Romeo sent me. He says you can",
            "npc:Ah Romeo, yes. A fine lad, but",
            "player:Juliet must be rescued from he",
            "npc:I know just the thing. A potio",
            "npc:Then Romeo can collect her fro",
            "npc:Go to the Apothecary, tell him",
            "npc:You will need a Cadava potion.",
        })
        t.expect("quest.stage.spoken_father", t.quest.expect_stage("spoken_father"))
        church_doorway("lawrence.churchOut", 3255, 3472, 3255, 3468,
            function(tt) return tt.level == 0 and tt.z <= 3470 end,
            "the street south of the church, z <= 3470")

        -- ------------------------------------------------ the Apothecary
        -- apothecary.spawn: 3195 3404 0, inside his shop; the goto lands on
        -- the street west of its door (note 2).
        t.exec("goto-talkToApothecary", t.player.goto_tile, 3190, 3403, 0)
        pass_door("apothecary.shopDoorIn", "fai_varrock_door", "fai_varrock_door_open", 3192, 3403, 3191, 3403, 3193, 3403,
            function(tt) return tt.level == 0 and tt.x >= 3192 and tt.x <= 3198 and tt.z >= 3403 and tt.z <= 3406 end,
            "inside the shop, x 3192-3198 z 3403-3406")
        t.exec("talkToApothecary", t.player.talk_to, "apothecary", 1)
        -- [opnpc1,apothecary]'s five other-quest guards (twocats_quest,
        -- onesmallfavour x2, my2arm_status, fossilquest_progress,
        -- ratcatch_var) all default unset/0 -- none matches, falls to
        -- %rjquest = spoken_father -> @apothecary_lawrence_sent.
        t.exec("talkToApothecary-dialog", t.chat.play, {
            "player:Apothecary. Father Lawrence se",
            "player:I need a Cadava potion to help",
            "npc:Cadava potion. It's pretty nas",
            "npc:Wing of rat, tail of frog. Ear",
            "npc:I have all of that, but I need",
            "npc:You will have to find them whi",
            "npc:Bring them here when you have ",
        })
        t.expect("quest.stage.spoken_apothecary", t.quest.expect_stage("spoken_apothecary"))

        -- The quest's own missing step (note 4 above): hand in the berries
        -- for real, a second click on the same npc -- @apothecary_make_cadava
        -- (cadava=0, cadavaberries=1 from setup) brews the potion. %rjquest
        -- stays spoken_apothecary; the deliverable is the inventory swap.
        t.exec("bringBerriesToApothecary", t.player.talk_to, "apothecary", 1)
        t.exec("bringBerriesToApothecary-dialog", t.chat.play, {
            "npc:Well done. You have the berrie",
            "mesbox:You hand over the berries, whi",
            "npc:Here is what you need.",
            "mesbox:The Apothecary gives you a Ca",
        })
        -- The berries-for-potion swap is a server-side inv_del/inv_add inside
        -- the dialog's own mesbox line; the client-visible backpack reaches
        -- it a few ticks after the chat page closes (the same class of race
        -- cooks_assistant.lua's own commit_settle/ingredients_settle await
        -- for), so this is an honest bounded await rather than an instant read.
        local brewed_result, brewed_detail = t.await({
            level = function()
                local cr, cc = t.inv.count("cadava")
                local br, bc = t.inv.count("cadavaberries")
                return cr == "ok" and cc == 1 and br == "ok" and bc == 0
            end,
            note = "bringBerriesToApothecary.brewed_settle",
        }, 20)
        local cadava_read, cadava_count = t.inv.count("cadava")
        local berries_read, berries_count = t.inv.count("cadavaberries")
        t.check("bringBerriesToApothecary-brewed",
            brewed_result == "ok" and cadava_count == 1 and berries_count == 0,
            "brewed within 20 ticks (" .. tostring(brewed_result) .. ") " .. tostring(brewed_detail)
                .. " cadava=" .. tostring(cadava_count) .. "(" .. tostring(cadava_read) .. ")"
                .. " cadavaberries=" .. tostring(berries_count) .. "(" .. tostring(berries_read) .. ")")

        pass_door("apothecary.shopDoorOut", "fai_varrock_door", "fai_varrock_door_open", 3192, 3403, 3193, 3403, 3190, 3403,
            function(tt) return tt.level == 0 and tt.x <= 3191 end,
            "the street west of the shop, x <= 3191")

        -- --------------------------------------------- give the potion to Juliet
        t.exec("goto-givePotionToJuliet", t.player.goto_tile, 3167, 3433, 0)
        up_to_juliet("juliet2", "goUpToJuliet2")
        t.exec("givePotionToJuliet", t.player.talk_to, "juliet", 1)
        -- %rjquest = spoken_apothecary -> @juliet_potion_made; cadava=1 now
        -- so `if (inv_total(inv, cadava) = 0)` is false, falls into the
        -- hand-in body.
        t.exec("givePotionToJuliet-dialog", t.chat.play, {
            "player:I have a Cadava potion from Fa",
            "player:It should make you seem dead, ",
            "mesbox:You pass the potion to Juliet.",
            "npc:Wonderful. I just hope Romeo c",
            "npc:Many thanks kind friend.",
            "npc:Please go to Romeo, make sure ",
            "npc:He can be a bit dense sometime",
        })
        t.expect("quest.stage.juliet_crypt", t.quest.expect_stage("juliet_crypt"))

        -- --------------------------------------------------- finish the quest
        down_from_juliet("juliet2", "goDownstairsToFinishQuest")
        t.exec("goto-finishQuest", t.player.goto_tile, 3211, 3425, 0)
        t.exec("finishQuest", t.player.talk_to, "romeo", 1)
        -- %rjquest = juliet_crypt -> @romeo_allset (no options page); ends
        -- with `queue(romeo_and_juliet_complete, 0, 0)`.
        t.exec("finishQuest-dialog", t.chat.play, {
            "player:Romeo, it's all set. Juliet ha",
            "npc:Ah right, the potion! Great...",
            "player:The Cadava potion, the one whi",
            "npc:But I'm scared... will you com",
            "player:Oh, ok... come on!",
            "mesbox:You accompany Romeo down into ",
            "npc:This is pretty scary...",
            "player:We're here. Look, Juliet is ov",
            "npc:Hey... Juliet...? Juliet....? ",
            "mesbox:Phillipa, Juliet's cousin, ste",
            "npc:Wow! You're a fox!",
            "npc:Who's Juliet?",
        })

        -- The completion queue(...) is asynchronous -- not visible client-side
        -- the instant the dialog closes (docs/QUEST_AUTHORING.md section 8).
        t.ticks(3)

        t.quest.expect_complete() -- quest.varp_complete, quest.scroll_title, quest.points, quest.journal
        t.finish(0)
    end,
}
