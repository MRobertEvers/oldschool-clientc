-- Mourning's End Part I, driven through to a CONTENT BUG, not a driver seam.
--
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_mourningsendparti/
-- scripts/mend1_disguise.rs2's own [opnpc1,mourner_hideout_head_mourner]
-- handler (mend1_essyllt_talk, ~line 164-176) advances %mourning_quest from
-- ^mend1_gathering(3) to ^mend1_assignment(4) once Essyllt hands over the
-- gnome key and the broken device -- and NEVER advances it any further. No
-- other live (non-debug) trigger in this quest's own scripts writes
-- %mourning_quest = ^mend1_gnome_task(5) anywhere
-- (`grep -rn "%mourning_quest =" quests/quest_mourningsendparti/scripts/*.rs2`
-- shows the only such write at mend1_debug.rs2:69, inside the `mend1run`
-- debugproc's own soft-skip walk -- a test-only cheat, not something a real
-- click can reach). mend1_gnome.rs2:33's [oploc1,mourning_gnome_rack] ->
-- mend1_gnome_rack_shared gate reads `if (%mourning_quest <
-- ^mend1_gnome_task) { mes("Nothing interesting happens."); return; }`, so
-- the caged gnome -- and with it the dye/toad/sheep branch the rest of the
-- quest depends on -- is unreachable from a fresh, non-cheated playthrough.
-- This file plays every beat up to the point Essyllt hands over the key and
-- the device, proves the gnome cage answers "Nothing interesting happens."
-- with the quest genuinely stuck at ^mend1_assignment, and t.blocked()s
-- right there.

return {
    id = "mourningsendparti",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::give fur 1",
        "::give silk 2",
        "::give bucket_water 1",
        "::setlevel ranged 60",
        "::setlevel thieving 50",
        -- rovingelves_islwyn.rs2's shared [opnpc1,roving_bowyer/roving_islwyn_2ops]
        -- trigger checks %regicide_quest and %waterfall_quest directly, on top
        -- of %rovingelves_quest. Waterfall Quest has its own ::complete arm;
        -- Regicide has none in quest_cheat.rs2 (grep-confirmed), so it is
        -- staged with the ladder cheat instead -- a genuine further
        -- prerequisite of Roving Elves (quest_rovingelves.constant's own
        -- header), never this quest's own work.
        "::complete quest_waterfall",
        "::setvar regicide_quest ^regicide_complete",
        "::complete quest_rovingelves",
        -- Big Chompy Bird Hunting has no ::complete arm in quest_cheat.rs2
        -- either (grep-confirmed) -- staged with the ladder cheat, same as
        -- Regicide above.
        "::setvar chompybird ^chompybird_complete",
        "::complete quest_sheepherder",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "mourning_quest",
            constants = {
                not_started = 0,
                briefed = 2,
                gathering = 3,
                assignment = 4,
                gnome_task = 5,
                poison_task = 6,
                learn_secret = 7,
                report = 8,
                complete = 9,
            },
            display = "Mourning's End Part I",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        -- configs/all.varp's own [mourning_quest] body is EMPTY (no
        -- transmit=yes), the same shape docs/QUEST_AUTHORING.md section 8
        -- names for [makinghistory]: every quest.expect_stage read would
        -- stay client=0 forever regardless of real server progress. Every
        -- stage check below reads t.ui.journal_open's own first_line instead
        -- (mend1_journal.rs2's ~mend1_journal proc runs server-side and is
        -- unaffected), the same channel makinghistory.lua and rovingelves.lua
        -- already use for the identical seam.
        local journal0_r, journal0 = t.ui.journal_open("Mourning's End Part I")
        t.check("quest.stage.not_started", journal0_r == "ok" and journal0 ~= nil
            and journal0.first_line ~= nil
            and journal0.first_line:find("I should talk to Islwyn in Isafdar", 1, true) ~= nil,
            "journal_open(Mourning's End Part I) -> " .. tostring(journal0_r) .. " first_line="
                .. tostring(journal0 and journal0.first_line))
        t.ui.journal_close()

        -- ---- Islwyn, Isafdar (2291,3147,0 --
        -- areas/world/configs/m35_49.spawn:81, spawned as base symbol
        -- roving_bowyer; rovingelves_islwyn_login's own %roving_bowyer
        -- multivarp transform only runs on [login,_], which fires before
        -- this file's setup cheats complete Roving Elves, so the live
        -- entity is still the untransformed roving_bowyer id, not
        -- roving_islwyn_2ops. [opnpc1,roving_bowyer] and
        -- [opnpc1,roving_islwyn_2ops] share the identical trigger body
        -- (rovingelves_islwyn.rs2:24-25), so talking to the untransformed id
        -- reaches ~mend1_islwyn_start all the same) ----
        t.exec("goto-talkToIslwyn", t.player.goto_tile, 2291, 3147, 0)
        t.exec("talkToIslwyn", t.player.talk_to, "roving_bowyer", 1)
        -- mend1_islwyn_start (mend1_shared.rs2:39-59): with %rovingelves_quest
        -- complete, ranged>=60 and thieving>=50 (mend1_meets_requirements
        -- true), the branch is the "there is one more thing" one.
        t.exec("talkToIslwyn-dialog", t.chat.play, {
            "npc:There is one more thing, human. Arianwyn has need of someone with your particular talents -- someone who can pass unnoticed among humans.",
            "player:What does she need?",
            "npc:I cannot say more here. Go to her in Lletya, and she will explain.",
            "choose:I'm ready now.",
            "player:I'm ready now.",
            "npc:Then go to her at once.",
        })
        -- The script's own %mourning_quest write happens once the last page's
        -- continue_ click (chat.play's own dismiss) lands server-side; the
        -- CLIENT'S mirror never carries this varp at all (see the banner
        -- above the quest.bind step) -- read server-side via the journal.
        t.ticks(3)
        local journal_briefed_r, journal_briefed = t.ui.journal_open("Mourning's End Part I")
        t.check("quest.stage.briefed", journal_briefed_r == "ok" and journal_briefed ~= nil
            and journal_briefed.first_line ~= nil
            and journal_briefed.first_line:find("Islwyn told me Arianwyn has a task for me", 1, true) ~= nil,
            "journal_open(Mourning's End Part I) -> " .. tostring(journal_briefed_r) .. " first_line="
                .. tostring(journal_briefed and journal_briefed.first_line))
        t.ui.journal_close()

        -- ---- Arianwyn, Lletya (2353,3172,0 -- m36_49.spawn:37, base symbol
        -- mourning_arianwyn) ----
        t.exec("goto-talkToArianwyn", t.player.goto_tile, 2353, 3172, 0)
        t.exec("talkToArianwyn", t.player.talk_to, "mourning_arianwyn", 1)
        -- mend1_arianwyn_talk (mend1_shared.rs2:83-92), %mourning_quest =
        -- briefed branch -- a plain npc-opened info dump, no choose.
        t.exec("talkToArianwyn-dialog", t.chat.play, {
            "player:Islwyn said you wanted to speak to me.",
            "npc:Indeed. Those that you know as mourners are in fact elves in service to Lord Iorwerth.",
            "npc:They wear plague-doctor robes and patrol West Ardougne under the pretence of quarantine, but that is a lie -- the plague is their cover to move freely through the city.",
            "player:Why are they really there?",
            "npc:That, I do not know. Lord Iorwerth allied himself with King Lathas, and I suspect it is to reach something valuable beneath West Ardougne. We need you to infiltrate their ranks and find out.",
            "npc:A mourner crosses the Arandar pass regularly. If you can obtain a disguise from one, you may be able to walk straight into their headquarters.",
        })
        t.ticks(3)
        local journal_gathering_r, journal_gathering = t.ui.journal_open("Mourning's End Part I")
        t.check("quest.stage.gathering", journal_gathering_r == "ok" and journal_gathering ~= nil
            and journal_gathering.first_line ~= nil
            and journal_gathering.first_line:find("Arianwyn revealed that the West Ardougne mourners", 1, true) ~= nil,
            "journal_open(Mourning's End Part I) -> " .. tostring(journal_gathering_r) .. " first_line="
                .. tostring(journal_gathering and journal_gathering.first_line))
        t.ui.journal_close()

        -- ---- Kill the overpass mourner, Arandar pass (2299,3328,0 --
        -- m35_52.spawn:9, base symbol mourning_overpass_mourner). The
        -- server script listens on both [opnpc1,...] and [opnpc2,...]
        -- (mend1_disguise.rs2:11-12), but the live entity's own menu carries
        -- NO Talk-to row at all -- op1 answers "menu has no row for it"
        -- (confirmed live: the pressed menu lists only Attack/Examine/Walk
        -- here, no Talk-to), so this is [opnpc2,mourning_overpass_mourner].
        -- mend1_kill_mourner is a plain mes() chat-log chain either way, not
        -- a dialogue page -- no chat.play here, the loot and the message
        -- are read back directly. ----
        t.exec("goto-killMourner", t.player.goto_tile, 2299, 3328, 0)
        t.exec("killMourner", t.player.talk_to, "mourning_overpass_mourner", 2)
        t.ticks(2)
        t.expect("killMourner.message", t.msg.expect("You collect a set of mourner's clothes, a mask, and a letter"))
        local boots_r, boots_n = t.inv.count("mourning_mourner_boots")
        local gloves_r, gloves_n = t.inv.count("mourning_mourner_gloves")
        local cloak_r, cloak_n = t.inv.count("mourning_mourner_cloak")
        local legs_r, legs_n = t.inv.count("mourning_ripped_mourner_legs")
        local mask_r, mask_n = t.inv.count("gasmask")
        local letter_r, letter_n = t.inv.count("mourning_mourner_message")
        local top_r, top_n = t.inv.count("mourning_bloody_mourner_top")
        t.check("killMourner.loot",
            boots_n == 1 and gloves_n == 1 and cloak_n == 1 and legs_n == 1
                and mask_n == 1 and letter_n == 1 and top_n == 1,
            string.format(
                "boots=%s gloves=%s cloak=%s ripped_legs=%s gasmask=%s message=%s bloody_top=%s",
                tostring(boots_n), tostring(gloves_n), tostring(cloak_n),
                tostring(legs_n), tostring(mask_n), tostring(letter_n), tostring(top_n)))

        -- ---- Tegid's laundry basket, south Taverley (eadgar_laundry_basket
        -- -- op1 search, no chat.play; [oploc1,eadgar_laundry_basket]
        -- (mend1_disguise.rs2:40-54) writes a plain mes() line too) ----
        t.exec("goto-searchLaundry", t.player.goto_tile, 2912, 3418, 0)
        t.exec("searchLaundry", t.player.click_loc, "eadgar_laundry_basket", 1)
        t.ticks(2)
        t.expect("searchLaundry.message", t.msg.expect("You search Tegid's laundry basket and steal a bar of soap"))
        local soap_r, soap_n = t.inv.count("mourning_soap")
        t.check("searchLaundry.soap", soap_r == "ok" and soap_n == 1,
            "mourning_soap count=" .. tostring(soap_n) .. " (" .. tostring(soap_r) .. ")")

        -- ---- Clean the bloodied top: soap on the bloody top (opheldu on
        -- either item, mend1_disguise.rs2:59-79) -- settles on the new chat
        -- (mes) line and the backpack change use_item_on_item watches for.
        -- One retry on a bare arming miss (use_on re-arms before every
        -- retry press per docs/QUEST_AUTHORING.md section 6). ----
        t.ticks(2)
        local clean_result, clean_detail = t.player.use_item_on_item("mourning_soap", "mourning_bloody_mourner_top")
        if clean_result ~= "ok" then
            t.ticks(3)
            clean_result, clean_detail = t.player.use_item_on_item("mourning_soap", "mourning_bloody_mourner_top")
        end
        t.step("cleanTop", clean_result == "ok" and "PASS" or "FAIL",
            "use_item_on_item(mourning_soap, mourning_bloody_mourner_top) -> " .. tostring(clean_result) .. " " .. tostring(clean_detail))
        t.shot("cleanTop-after")
        local clean_top_r, clean_top_n = t.inv.count("mourning_mourner_top")
        local bloody_r, bloody_n = t.inv.count("mourning_bloody_mourner_top")
        t.check("cleanTop.result", clean_top_n == 1 and bloody_n == 0,
            "mourning_mourner_top=" .. tostring(clean_top_n) .. " mourning_bloody_mourner_top=" .. tostring(bloody_n))

        -- ---- Oronwen, Lletya (2324,3179,0 -- m36_49.spawn:13, mourning_seamstress).
        -- We already carry 2 silk and 1 fur (setup), so mend1_oronwen_talk
        -- (mend1_disguise.rs2:86-115) runs both halves of the exchange in
        -- the one dialogue. ----
        t.exec("goto-talkToOronwen", t.player.goto_tile, 2324, 3179, 0)
        t.exec("talkToOronwen", t.player.talk_to, "mourning_seamstress", 1)
        t.exec("talkToOronwen-dialog", t.chat.play, {
            "player:Do you mend clothes?",
            "npc:Of course -- but those trousers are in a sorry state. I'll need two lengths of silk and some bear fur to patch them properly.",
            "player:I have all I need to mend my trousers.",
            "npc:There -- good as new. Mind the stitching doesn't show under that cloak.",
        })
        -- inv_del(silk)/inv_del(fur)/inv_add(mourning_mourner_legs) are the
        -- server's own reaction to the dialogue's LAST continue_ click, which
        -- lands a tick or more after chat.play's own return -- a bare
        -- t.inv.count read right here can still see the pre-grant counts
        -- (docs/QUEST_AUTHORING.md section 8, first bullet). Poll for the
        -- grant instead of reading once.
        local legs_wait_r, legs_wait_detail = t.inv.await("mourning_mourner_legs", 1, 10)
        t.step("talkToOronwen.await_legs", legs_wait_r == "ok" and "PASS" or "FAIL",
            "inv.await(mourning_mourner_legs, 1, 10) -> " .. tostring(legs_wait_r) .. " " .. tostring(legs_wait_detail))
        local mourner_legs_r, mourner_legs_n = t.inv.count("mourning_mourner_legs")
        local silk_r, silk_n = t.inv.count("silk")
        local fur_r, fur_n = t.inv.count("fur")
        t.check("talkToOronwen.result",
            mourner_legs_n == 1 and silk_n == 0 and fur_n == 0,
            "mourning_mourner_legs=" .. tostring(mourner_legs_n) .. " silk=" .. tostring(silk_n) .. " fur=" .. tostring(fur_n))

        -- ---- Wear the full disguise (mend1_wearing_full_disguise,
        -- mend1_disguise.rs2:125-130 -- all six pieces WORN, the letter
        -- stays in the backpack) ----
        t.exec("equip.gasmask", t.player.equip, "gasmask")
        t.exec("equip.top", t.player.equip, "mourning_mourner_top")
        t.exec("equip.legs", t.player.equip, "mourning_mourner_legs")
        t.exec("equip.cloak", t.player.equip, "mourning_mourner_cloak")
        t.exec("equip.boots", t.player.equip, "mourning_mourner_boots")
        t.exec("equip.gloves", t.player.equip, "mourning_mourner_gloves")

        -- ---- Mourners' HQ front door, West Ardougne (mournerstewdoor) --
        -- areas/area_ardougne_west/scripts/doors.rs2's own
        -- [oploc1,mournerstewdoor] extends into the mourning_quest branch
        -- that this quest owns: full disguise worn + the letter in the
        -- backpack lets the guard wave the player through. ----
        t.exec("goto-enterMournerBase", t.player.goto_tile, 2551, 3320, 0)
        t.exec("enterMournerBase", t.player.click_loc, "mournerstewdoor", 1)
        t.ticks(2)
        t.expect("enterMournerBase.message", t.msg.expect("You slip past the guards in your disguise"))

        -- ---- Basement trapdoor (mourning_hideout_trap_door,
        -- mend1_disguise.rs2:136-143 -- p_teleport to mend1_hq_basement_coord,
        -- 2044,4628,0, the same tile the basement npcs spawn on) ----
        t.exec("goto-enterBasement", t.player.goto_tile, 2542, 3327, 0)
        t.exec("enterBasement", t.player.click_loc, "mourning_hideout_trap_door", 1)
        t.expect("enterBasement.message", t.msg.expect("You climb down into the basement"))
        -- p_teleport's client-side region reload needs a tick or two before
        -- the basement's npcs populate the entity pool.
        t.ticks(3)

        -- ---- Essyllt, HQ basement (mourner_hideout_head_mourner,
        -- 2044,4628,0 -- m31_72.spawn:41, the base symbol; the trap door's
        -- own p_teleport lands the player on this same tile). At
        -- %mourning_quest = gathering, mend1_essyllt_talk
        -- (mend1_disguise.rs2:164-176) opens with an npc line (chatnpc_anim),
        -- not a player one -- this handler has no chatplayer_anim opener the
        -- way trap 18's "almost always" case does. ----
        t.exec("talkToEssyllt", t.player.talk_to, "mourner_hideout_head_mourner", 1)
        t.exec("talkToEssyllt-dialog", t.chat.play, {
            "npc:New face. Good -- we can always use another set of hands.",
            "player:What do you need me to do?",
            "npc:There's a gnome chained up in the cage behind me -- an old prisoner of ours. He knows things about a device we've never managed to repair. Get it out of him.",
            "npc:Take this key to his cage, and the broken device -- see if you can get him talking.",
        })
        -- Same shape as the Oronwen exchange above: inv_add(mourning_gnome_key)/
        -- inv_add(mourning_paint_gun_broken) are the server's reaction to the
        -- dialogue's last continue_ click, so poll rather than read once.
        local key_wait_r, key_wait_detail = t.inv.await("mourning_gnome_key", 1, 10)
        t.step("talkToEssyllt.await_key", key_wait_r == "ok" and "PASS" or "FAIL",
            "inv.await(mourning_gnome_key, 1, 10) -> " .. tostring(key_wait_r) .. " " .. tostring(key_wait_detail))
        local key_r, key_n = t.inv.count("mourning_gnome_key")
        local gun_r, gun_n = t.inv.count("mourning_paint_gun_broken")
        t.check("talkToEssyllt.items", key_n == 1 and gun_n == 1,
            "mourning_gnome_key=" .. tostring(key_n) .. " mourning_paint_gun_broken=" .. tostring(gun_n))
        t.settle()
        t.ticks(5)
        local journal_assignment_r, journal_assignment = t.ui.journal_open("Mourning's End Part I")
        local journal_assignment_note = ""
        if journal_assignment_r ~= "ok" then
            -- Measured live: the FIRST journal_open right after this dialogue's
            -- own last continue_ can time out with the row genuinely clicked
            -- but questjournal never mounting within 20 ticks, while a SECOND
            -- attempt moments later (further down, after the gnome-rack click)
            -- reads cleanly -- the same one-off UI-queue race
            -- docs/QUEST_AUTHORING.md trap 11 documents for chat.drain/
            -- continue_. Retry once with more settle, same shape as this
            -- file's own cleanTop retry above.
            journal_assignment_note = " [retry after timeout: " .. tostring(journal_assignment) .. "]"
            t.ticks(5)
            t.settle()
            journal_assignment_r, journal_assignment = t.ui.journal_open("Mourning's End Part I")
        end
        local journal_assignment_line = (type(journal_assignment) == "table") and journal_assignment.first_line or nil
        t.check("quest.stage.assignment", journal_assignment_r == "ok" and journal_assignment_line ~= nil
            and journal_assignment_line:find(
                "I infiltrated the Mourners' Headquarters in my disguise", 1, true) ~= nil,
            "journal_open(Mourning's End Part I) -> " .. tostring(journal_assignment_r) .. " "
                .. (journal_assignment_line ~= nil and ("first_line=" .. journal_assignment_line) or tostring(journal_assignment))
                .. journal_assignment_note)
        t.ui.journal_close()

        -- ---- CONTENT BUG: the caged gnome (mourning_gnome_rack) is gated
        -- on %mourning_quest >= ^mend1_gnome_task(5)
        -- (mend1_gnome.rs2:33-37, mend1_gnome_rack_shared), but nothing in
        -- this quest's own live scripts ever advances %mourning_quest past
        -- ^mend1_assignment(4) -- see the file banner. The stage is proven
        -- stuck immediately above (quest.stage.assignment PASSed with the
        -- key and the broken device already in hand, which is exactly the
        -- state mend1_essyllt_talk leaves the player in permanently). This
        -- click is the recording row: it reads the cage's own refusal text
        -- live, with the quest genuinely at ^mend1_assignment, not a guess.
        -- world.loc_near first, to goto the cage's real tile rather than
        -- guessing a second one. ----
        local rack_near_result, rack_near = t.world.loc_near("mourning_gnome_rack", 15)
        t.step("mourningGnomeRack.locate",
            rack_near_result == "ok" and "PASS" or "FAIL",
            "world.loc_near(mourning_gnome_rack, 15) -> " .. tostring(rack_near_result) .. " "
                .. (rack_near_result == "ok" and string.format("tile=%s,%s,%s match=%s",
                    tostring(rack_near.tile_x), tostring(rack_near.tile_z), tostring(rack_near.level), tostring(rack_near.match))
                    or tostring(rack_near)))
        if rack_near_result == "ok" then
            t.exec("goto-mourningGnomeRack", t.player.goto_tile, rack_near.tile_x, rack_near.tile_z, rack_near.level)
        end
        -- Not t.exec: pointer.lua's own QD.player.CLICK_REFUSAL_LINES
        -- classifies the exact live reply ("Nothing interesting happens.")
        -- as `refused` -- content REACHED mourning_gnome_rack and no branch
        -- claimed it, the precise signature of the content bug this row
        -- exists to record, not a driver failure -- so grading it through
        -- t.exec (which FAILs any non-"ok") would leave a FAIL row right
        -- before t.blocked(), which QUEST_AUTHORING.md section 8's own
        -- "grade this row true" convention (makinghistory.lua/pryingtimes.lua)
        -- rejects. Called directly and recorded with t.check instead.
        t.ticks(2)
        local rack_click_result, rack_click_detail = t.player.click_loc("mourning_gnome_rack", 1)
        local journal_after_r, journal_after = t.ui.journal_open("Mourning's End Part I")
        local journal_after_line = journal_after and journal_after.first_line
        t.check("mourningGnomeRack.blocked_by_stage",
            rack_click_result == "refused" and rack_click_detail == "Nothing interesting happens."
                and journal_after_r == "ok" and journal_after_line ~= nil
                and journal_after_line:find(
                    "I infiltrated the Mourners' Headquarters in my disguise", 1, true) ~= nil,
            "click_loc(mourning_gnome_rack) -> " .. tostring(rack_click_result) .. " " .. tostring(rack_click_detail)
                .. " -- journal_open(Mourning's End Part I) still reads first_line=" .. tostring(journal_after_line)
                .. " (still the assignment-stage line, gnome untouched) -- channel: ui.journal_open, server-side")
        t.ui.journal_close()

        t.blocked("OSRS-Content/osrs239-content/server/scripts/quests/quest_mourningsendparti/scripts/mend1_disguise.rs2:164-176 " ..
            "(mend1_essyllt_talk) sets %mourning_quest = ^mend1_assignment after handing over the gnome key and the broken " ..
            "device, and no other live trigger in quests/quest_mourningsendparti/scripts/*.rs2 ever writes " ..
            "%mourning_quest = ^mend1_gnome_task -- the only such write is mend1_debug.rs2:69, inside the mend1run " ..
            "debugproc's own soft-skip walk. mend1_gnome.rs2:33-37's [oploc1,mourning_gnome_rack] -> mend1_gnome_rack_shared " ..
            "gate reads 'if (%mourning_quest < ^mend1_gnome_task) { mes(\"Nothing interesting happens.\"); return; }', so the " ..
            "caged gnome -- and the dye/toad/sheep branch and the poison-task branch that depend on torturing it -- is " ..
            "unreachable from a real playthrough. Confirmed live above: quest.stage.assignment PASSed right after Essyllt " ..
            "handed over the key and the device, and the very next click on mourning_gnome_rack answers 'Nothing " ..
            "interesting happens.' with the stage still at assignment(4).")
        return
    end,
}
