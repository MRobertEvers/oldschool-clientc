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
--   2. There is no loc/stairs click anywhere in this quest. `goto_tile`
--      with the destination's own level climbs Juliet's house stairs by
--      itself (docs/QUEST_AUTHORING.md section 2's floors-and-ladders
--      rule); the scaffold inserted an extra approach-tile goto plus a
--      guessed `click_loc("fai_varrock_stairs_taller")` and a guessed
--      dialog choice before EVERY plane change, none of which the real
--      quest needs or the .rs2 has anything to do with.
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
-- (Lumbridge, beside Hans). Every `goto_tile` below is an ABSOLUTE tile
-- read off a `*.spawn` row (romeo/father_lawrence/apothecary) or the
-- `juliet_multi_visible` wrapper's own spawn row (note 3 above).

return {
    id = "romeojuliet",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::give cadavaberries 1", -- Quest-Helper prerequisite; the quest's own deliverable (the cadava POTION) is earned from the Apothecary by a real click below, never given
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "rjquest",
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
        -- Varrock, upper floor) -- goto_tile's own level climbs the stairs,
        -- no click_loc needed (docs/QUEST_AUTHORING.md section 2).
        t.exec("goto-talkToJuliet", t.player.goto_tile, 3158, 3425, 1)
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
        t.exec("goto-talkToLawrence", t.player.goto_tile, 3254, 3484, 0) -- father_lawrence.spawn: 3254 3484 0
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

        -- ------------------------------------------------ the Apothecary
        t.exec("goto-talkToApothecary", t.player.goto_tile, 3195, 3404, 0) -- apothecary.spawn: 3195 3404 0
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

        -- --------------------------------------------- give the potion to Juliet
        t.exec("goto-givePotionToJuliet", t.player.goto_tile, 3158, 3425, 1)
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
