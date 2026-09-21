-- Mourning's End Part I, driven through to a DRIVER SEAM (not the original
-- content bug -- that one is fixed).
--
-- RETRY after OSRS-Content 4420b02611: this file's earlier revision t.blocked
-- here because mend1_gnome.rs2:33's [oploc1,mourning_gnome_rack] ->
-- mend1_gnome_rack_shared gate read `if (%mourning_quest <
-- ^mend1_gnome_task) { mes("Nothing interesting happens."); return; }`, and
-- no live (non-debug) trigger in this quest's own scripts ever wrote
-- %mourning_quest = ^mend1_gnome_task(5) -- Essyllt's own hand-over
-- (mend1_disguise.rs2:164-176, mend1_essyllt_talk) only reaches
-- ^mend1_assignment(4). 4420b02611 fixed this from the content side: the
-- gate now reads `%mourning_quest < ^mend1_assignment`, and
-- [label,mend1_gnome_first_talk] itself writes ^mend1_gnome_task once the
-- player has a feather and toad crunchies in hand. This file now plays the
-- whole gnome-cage ladder (talk/tickle/talk_again/release/give_items/
-- ask_toads), confirms the quest genuinely advances past ^mend1_assignment,
-- dyes two toads and fires the first one at a sheep -- then hits a real
-- DRIVER seam: mend1_sheep.rs2 requires the fixed device to be WORN to fire
-- but back in the BACKPACK to load the next colour, and no verb in the
-- table can use an item on a worn item or take a worn item back off. See
-- the t.blocked() at the end of run() for the exact seam.

return {
    id = "mourningsendparti",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::give fur 1",
        "::give silk 2",
        "::give bucket_water 1",
        -- mend1_gnome.rs2's own gnome-cage ladder (talk/tickle/talk_again/
        -- give_items): feather (tickled away), toad_crunchies (checked
        -- twice, consumed once at give_items), magic_logs + leather
        -- (checked at talk_again, consumed at give_items) -- every one of
        -- these is on quest-helper's own getItemRequirements() list for
        -- this quest (bearFur/silk2/waterBucket/feather/toadCrunchies/
        -- magicLogs/leather/ogreBellows/redDye/.../coal20OrNaphtha), none
        -- flagged canBeObtainedDuringQuest(), so every one is a legitimate
        -- setup grant, same as fur/silk/bucket_water above.
        "::give feather 1",
        "::give toad_crunchies 1",
        "::give magic_logs 1",
        "::give leather 1",
        -- mend1_sheep.rs2's dye-the-bellows step: only red and green are
        -- driven (the seam below stops the run before a third colour would
        -- matter), the bellows itself is reused (isNotConsumed() per
        -- quest-helper).
        "::give reddye 1",
        "::give greendye 1",
        "::give empty_ogre_bellows 1",
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

        -- ---- FIXED (RETRY after OSRS-Content 4420b02611): the caged gnome
        -- (mourning_gnome_rack) used to gate entry on %mourning_quest >=
        -- ^mend1_gnome_task(5), which nothing in this quest's own live
        -- scripts ever wrote -- a genuine content bug, blocked here in this
        -- file's earlier revision. mend1_gnome.rs2's entry gate now reads
        -- %mourning_quest < ^mend1_assignment(4) instead (the stage
        -- mend1_essyllt_talk actually leaves the player in), and
        -- [label,mend1_gnome_first_talk] itself writes
        -- %mourning_quest = ^mend1_gnome_task once the player has a feather
        -- and toad crunchies in hand -- so the cage is reachable from a real
        -- playthrough now. Driven onward from the exact point the old
        -- t.blocked() stood. ----
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

        -- ---- Click 1: mend1_gnome_first_talk (mend1_gnome.rs2:78-88) --
        -- feather + toad crunchies already in hand (setup), so this opens
        -- the "weaknesses" mesbox and, on dismiss, writes
        -- %mourning_gnome = ^mend1_gnome_weakness and
        -- %mourning_quest = ^mend1_gnome_task. THIS is the row that proves
        -- the RETRY fix: the cage no longer answers "Nothing interesting
        -- happens." at ^mend1_assignment, and the quest genuinely advances
        -- past the point the old blocked row asserted it never could. ----
        t.exec("gnomeCage.firstTalk", t.player.click_loc, "mourning_gnome_rack", 1)
        t.exec("gnomeCage.firstTalk-dialog", t.chat.play, {
            "mesbox:You talk with the caged gnome about toad crunchies and being tickled.",
        })
        t.chat.close()
        t.ticks(3)
        local journal_task_r, journal_task = t.ui.journal_open("Mourning's End Part I")
        local journal_task_line = journal_task and journal_task.first_line
        t.check("quest.stage.gnome_task",
            journal_task_r == "ok" and journal_task_line ~= nil
                and journal_task_line:find("I'm working on the caged gnome", 1, true) ~= nil,
            "journal_open(Mourning's End Part I) -> " .. tostring(journal_task_r) .. " first_line="
                .. tostring(journal_task_line)
                .. " -- the cage advanced the quest past ^mend1_assignment (OSRS-Content 4420b02611's fix confirmed live)")
        t.ui.journal_close()

        -- ---- Click 2: mend1_gnome_tickle (mend1_gnome.rs2:90-96) --
        -- consumes the feather -> gnome = tortured. ----
        t.exec("gnomeCage.tickle", t.player.click_loc, "mourning_gnome_rack", 1)
        t.exec("gnomeCage.tickle-dialog", t.chat.play, {
            "mesbox:You reach through the bars and tickle the gnome mercilessly with the feather.",
        })
        t.chat.close()
        t.ticks(2)
        local feather_r, feather_n = t.inv.count("feather")
        t.check("gnomeCage.tickle.result", feather_r == "ok" and feather_n == 0,
            "feather=" .. tostring(feather_n) .. " (" .. tostring(feather_r) .. ")")

        -- ---- Click 3: mend1_gnome_talk_again (mend1_gnome.rs2:98-104) --
        -- toad crunchies/magic logs/leather CHECKED, not consumed yet ->
        -- gnome = talked_item. ----
        t.exec("gnomeCage.talkAgain", t.player.click_loc, "mourning_gnome_rack", 1)
        t.exec("gnomeCage.talkAgain-dialog", t.chat.play, {
            "mesbox:Bring me those things and let me out of here",
        })
        t.chat.close()

        -- ---- Click 4: mend1_gnome_release (mend1_gnome.rs2:106-112) --
        -- consumes the key; a plain mes(), not a page. ----
        t.exec("gnomeCage.release", t.player.click_loc, "mourning_gnome_rack", 1)
        t.ticks(2)
        t.expect("gnomeCage.release.message", t.msg.expect("You unlock the cage and release the gnome."))
        local key_after_r, key_after_n = t.inv.count("mourning_gnome_key")
        t.check("gnomeCage.release.result", key_after_r == "ok" and key_after_n == 0,
            "mourning_gnome_key=" .. tostring(key_after_n) .. " (" .. tostring(key_after_r) .. ")")

        -- ---- Click 5: mend1_gnome_give_items (mend1_gnome.rs2:114-123) --
        -- consumes toad crunchies, magic logs, leather and the broken
        -- device; produces the fixed device -> gnome = repaired. The
        -- inv_del/inv_add calls run BEFORE the trailing mesbox (immediate),
        -- so the counts are safe to read right after dismiss. Measured live:
        -- mend1_gnome_release's own mes() transforms the rack loc's own
        -- form the moment the gnome is released (its menu now reads only
        -- "Examine Empty rack" / "Walk here", no Talk-to row at all) -- the
        -- header comment's "player who clicks the now-walking gnome instead
        -- of the rack still gets a response" is describing exactly this:
        -- from here on the shared label is reached through the NPC
        -- (m31_72.spawn:33's mourner_hideout_gnome, standing on the same
        -- tile, [opnpc1,mourner_hideout_gnome] -> the identical
        -- mend1_gnome_rack_shared), not the loc. ----
        t.exec("gnomeCage.giveItems", t.player.talk_to, "mourner_hideout_gnome", 1)
        t.exec("gnomeCage.giveItems-dialog", t.chat.play, {
            "mesbox:The gnome sets to work with practised hands",
        })
        t.chat.close()
        t.ticks(2)
        local gun_r, gun_n = t.inv.count("mourning_paint_gun")
        local logs_r, logs_n = t.inv.count("magic_logs")
        local leather_r, leather_n = t.inv.count("leather")
        local crunchies_r, crunchies_n = t.inv.count("toad_crunchies")
        t.check("gnomeCage.giveItems.result",
            gun_r == "ok" and gun_n == 1 and logs_r == "ok" and logs_n == 0
                and leather_r == "ok" and leather_n == 0 and crunchies_r == "ok" and crunchies_n == 0,
            string.format("mourning_paint_gun=%s magic_logs=%s leather=%s toad_crunchies=%s",
                tostring(gun_n), tostring(logs_n), tostring(leather_n), tostring(crunchies_n)))

        -- ---- Click 6: mend1_gnome_ask_toads (mend1_gnome.rs2:125-128) --
        -- a chatplayer opener plus two mesbox pages -> %mourning_dye_chat
        -- = 1, no item change. The rack is still the empty form from click
        -- 5 onward, so this is the NPC too. ----
        t.exec("gnomeCage.askToads", t.player.talk_to, "mourner_hideout_gnome", 1)
        t.exec("gnomeCage.askToads-dialog", t.chat.play, {
            "player:What does this thing actually do?",
            "mesbox:'It fires toads,' the gnome explains",
            "mesbox:Find a mourner ogre-bellows seller somewhere near Feldip Hills",
        })
        t.chat.close()
        t.ticks(3)
        local journal_dye_r, journal_dye = t.ui.journal_open("Mourning's End Part I")
        local journal_dye_line = journal_dye and journal_dye.first_line
        t.check("quest.stage.gnome_dye_learned",
            journal_dye_r == "ok" and journal_dye_line ~= nil
                and journal_dye_line:find("The gnome explained that the device fires dyed, inflated toads", 1, true) ~= nil,
            "journal_open(Mourning's End Part I) -> " .. tostring(journal_dye_r) .. " first_line=" .. tostring(journal_dye_line))
        t.ui.journal_close()

        -- ---- Dye the bellows and inflate two toads (mend1_sheep.rs2:9-24
        -- -- the bellows must be armed first: only [opheldu,
        -- empty_ogre_bellows] is declared, not the reverse, so item_a is
        -- the bellows. No location gate on this interaction at all; the
        -- bellows is not consumed and is reused for both colours. Only two
        -- colours are driven -- see the seam recorded below, which stops
        -- the run before a third colour would matter. One retry on a bare
        -- arming miss, same shape as the cleanTop retry above (use_on
        -- re-arms before every retry press per docs/QUEST_AUTHORING.md
        -- section 6) -- measured live: the backpack tab was not yet
        -- repainted from the journal_close() immediately before this. ----
        local dye_red_result, dye_red_detail = t.player.use_item_on_item("empty_ogre_bellows", "reddye")
        if dye_red_result ~= "ok" then
            t.ticks(3)
            dye_red_result, dye_red_detail = t.player.use_item_on_item("empty_ogre_bellows", "reddye")
        end
        t.step("dyeBellows.red", dye_red_result == "ok" and "PASS" or "FAIL",
            "use_item_on_item(empty_ogre_bellows, reddye) -> " .. tostring(dye_red_result) .. " " .. tostring(dye_red_detail))
        t.exec("dyeBellows.green", t.player.use_item_on_item, "empty_ogre_bellows", "greendye")
        local redtoad_r, redtoad_n = t.inv.count("mourning_bloated_toad_red")
        local greentoad_r, greentoad_n = t.inv.count("mourning_bloated_toad_green")
        t.check("dyeBellows.result", redtoad_r == "ok" and redtoad_n == 1 and greentoad_r == "ok" and greentoad_n == 1,
            "mourning_bloated_toad_red=" .. tostring(redtoad_n) .. " mourning_bloated_toad_green=" .. tostring(greentoad_n))

        -- ---- Load the red toad (device unworn, still a backpack cell),
        -- equip the device, and fire it at the sheep herd mend1_sheep.rs2's
        -- own mend1_try_fire_sheep maps to herder_plaguesheep_1. The LIVE
        -- entity is the map's own base symbol, plaguesheep_1
        -- (areas/world/configs/m40_52.spawn), which diseased_sheep.rs2's
        -- [opnpc1,plaguesheep_1] -> prod_sheep(herder_plaguesheep_1) routes
        -- into the same shared label (trap 19/20's base-symbol shape). ----
        t.exec("loadToad.red", t.player.use_item_on_item, "mourning_bloated_toad_red", "mourning_paint_gun")
        t.exec("equip.paintgun", t.player.equip, "mourning_paint_gun")
        -- by_symbol/walk_near need the npc loaded into the CLIENT's nearby
        -- region first -- we are still in the HQ basement, two whole
        -- regions away, so goto_tile to the sheep field before either
        -- (m40_52.spawn's plaguesheep_1 cluster: 2609-2610,3343-3345).
        t.exec("goto-sheepField", t.player.goto_tile, 2610, 3344, 0)
        local sheep1, sheep1_r = t.player.by_symbol("npc", "plaguesheep_1")
        t.step("sheep1.locate", sheep1_r == "ok" and "PASS" or "FAIL", "by_symbol(npc, plaguesheep_1) -> " .. tostring(sheep1_r))
        if sheep1_r == "ok" then
            t.exec("walk-sheep1", t.player.walk_near, sheep1, 15)
        end
        t.exec("fireSheep.red", t.player.talk_to, "plaguesheep_1", 1)
        t.ticks(2)
        t.expect("fireSheep.red.message", t.msg.expect("the red toad splats across the sheep"))

        -- ---- SEAM: reload the device for the second colour. mend1_sheep.rs2's
        -- mend1_try_fire_sheep requires mourning_paint_gun to be WORN to
        -- fire (`inv_total(worn, mourning_paint_gun) < 1` refuses), but
        -- loading a new toad is an [opheldu,...] item-on-item interaction,
        -- and t.player.use_item_on_item resolves BOTH cells through
        -- QD.player._inv_cell (script/plugins/quest_driver/pointer.lua),
        -- which walks QD._inv_container() -- the BACKPACK -- only. No verb
        -- in docs/QUEST_AUTHORING.md section 3 can use a backpack item on a
        -- worn/equipped item, or take a worn item back off into the
        -- backpack for a fresh load (t.player.equip only WEARS, via the
        -- same backpack-only cell lookup, so it cannot re-select an item
        -- that is already worn either). Recorded live below: the identical
        -- load call that worked for red before it was equipped now fails
        -- once the device is worn for the shot above. ----
        local reload_result, reload_detail = t.player.use_item_on_item("mourning_bloated_toad_green", "mourning_paint_gun")
        t.check("loadToad.green.seam", reload_result ~= "ok",
            "use_item_on_item(mourning_bloated_toad_green, mourning_paint_gun) -> " .. tostring(reload_result) .. " " .. tostring(reload_detail)
                .. " -- mourning_paint_gun is worn (equipped for the red shot above) and use_item_on_item only resolves cells in the backpack container")

        t.blocked("script/plugins/quest_driver/pointer.lua's QD.player._inv_cell (used by both halves of " ..
            "use_item_on_item, and by equip's own dispatch) resolves only QD._inv_container() -- the backpack. " ..
            "OSRS-Content/osrs239-content/server/scripts/quests/quest_mourningsendparti/scripts/mend1_sheep.rs2's own " ..
            "load/fire cycle requires mourning_paint_gun to be WORN to fire " ..
            "(mend1_try_fire_sheep: 'if (inv_total(worn, mourning_paint_gun) < 1) { return(0); }') but back in the " ..
            "BACKPACK to load the next colour's toad ([opheldu,mourning_bloated_toad_green]/[opheldu,mourning_paint_gun], " ..
            "both backpack-only cell lookups). No verb in the table removes a worn item back into the backpack, and " ..
            "t.player.equip cannot re-select an item that is already worn (same backpack-only lookup). Confirmed live " ..
            "above: loading and firing the red toad worked with the device unworn-then-worn exactly once, and the " ..
            "identical load call for the green toad then failed because mourning_paint_gun is no longer a backpack " ..
            "cell -- the four-colour sheep-marking step this quest's own gnome_task stage requires cannot be completed " ..
            "by any combination of the verbs in docs/QUEST_AUTHORING.md section 3.")
        return
    end,
}
