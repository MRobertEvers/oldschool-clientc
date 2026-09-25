-- Mourning's End Part I, driven end to end (all three content/driver bugs
-- this file carried a t.blocked() for are fixed -- see the three RETRY
-- notes below).
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
-- ask_toads) and confirms the quest genuinely advances past ^mend1_assignment.
--
-- RETRY after 68c5e8d9d: this file's next revision t.blocked at
-- loadToad.green.seam because mend1_sheep.rs2's mend1_try_fire_sheep
-- requires the fixed device to be WORN to fire but back in the BACKPACK to
-- load the next colour, and no verb reached a worn item. 68c5e8d9d landed
-- t.player.unequip(item) (script/plugins/quest_driver/pointer.lua), so the
-- load/fire cycle is now driven unequip -> use_item_on_item -> equip ->
-- fire for green/blue/yellow (red loads once, unworn, before the device is
-- ever equipped). All four sheep herds are marked, the quest is driven
-- through the poison-the-food-stores test and the final report to
-- Arianwyn, and quest.expect_complete() closes it out.
--
-- RETRY after 486398e09: this file's next revision t.blocked at
-- cookToxin.range_missing, reading carnilleanrange (2859) as unplaced
-- anywhere in the loaded world from six SURFACE anchors around the
-- Carnillean Mansion (2570,3270,0 +/- 60, three floors, radius 60, all
-- not_found -- build/quest_gate/seam_mourning_rangeprobe/ledger.tsv). That
-- premise was false and no content was changed: carnilleanrange has exactly
-- ONE placement, maps/m39_151.jl2:537 `0 42 35: 2859 10 1` = ABS
-- (2538,9699,0), in the UNDERGROUND band (z+6400) -- the Carnillean
-- kitchen room, live and reachable, and mend1_poison.rs2:174 fires and
-- cooks exactly as written (build/quest_gate/seam6_range_probe/ledger.tsv,
-- 13/13 PASS: loc_near(carnilleanrange,10) -> ok 2538,9699 L0 match=exact,
-- then click_loc + inv.await(mourning_apple_toxin,2) -> ok). The six
-- surface anchors were 6,400 tiles away in z from the real room, so their
-- not_found was guaranteed and proved nothing (docs/QUEST_AUTHORING.md
-- trap 29). This file now drives the whole tail past that point for real:
-- both West Ardougne food-store presses, the report to Essyllt, the
-- Arianwyn hand-in, completion (hand-rolled the makinghistory.lua/
-- rovingelves.lua way -- [mourning_quest]'s own configs/*.varp body carries
-- no transmit=yes, so quest.expect_complete()'s client-side stage read
-- would never converge) and the reward rows for every reward
-- mend1_shared.rs2:31's own ~quest_complete_rewards call lists.
--
-- RE-AUTHOR after OSRS-Content c8b3e2fede / aa38f602a3 (content parity
-- [parity:parity1b]/[parity:parity1c]): three legs of this file went stale
-- against real content changes and are rewritten below.
--   (1) killMourner: mend1_disguise.rs2's mourning_overpass_mourner is REAL
--       combat now (`[apnpc2,...]`/`[opnpc2,...]` both `@player_combat_start*`,
--       op2=Attack, QUEST_AUTHORING.md point 31's shape) -- a single
--       talk_to(sym,2) press used to read as a kill because the old content
--       granted the loot on one click; now it only ARMS the fight, so this
--       is `t.player.attack` + `t.npc.await_dead_engaged`, gear given in
--       setup (a combat prerequisite, not the quest's own work).
--   (2) fillBarrel: mend1_poison.rs2's `[oploc1,mourning_orchard_applepile]`
--       now only says "You'll need an empty barrel..."; the real fill is
--       `[oplocu,...]` on `last_useitem = regicide_barrel_empty`, a real
--       ground-spawned barrel at the pickUpBarrel tile
--       (areas/world/configs/m38_52.spawn:38, 2487,3371,0).
--   (3) the naphtha leg: no more `::give regicide_barrel_naphtha` shortcut.
--       mend1_poison.rs2's 2026-09-23 header wires this quest into
--       Regicide's own real fractionalising-still chain
--       (regicide_bombcraft.rs2's `regicide_tar_collection` +
--       regicide_fractionalising_still.rs2), both gated open to a
--       Mourning's End player at the poison task with the sieve via
--       `~mend1_naphtha_allowed`. Driven for real below: a second empty
--       barrel from the tar swamp (Tirannwn, m34_49/m34_50.spawn), coal
--       tar collected, coal brought along (a genuine bring-along per
--       quest-helper's own `coal20OrNaphtha` ItemRequirement -- given via
--       `t.cheat` mid-run rather than `setup`, because 10 raw non-stackable
--       coal held the whole quest drops killMourner's own 7-free-slot gate
--       below the minimum, measured by an earlier pass at free=6), the
--       still's valve/coal minigame at 2927,3212 (quest-helper's own
--       `getNaphtha` WorldPoint) driven with `t.ui.invoke(widget, 0)` --
--       EVERY button on interface 286 is an IF1-style graphic button
--       (`if3=no`, a nonzero `buttontype`: 1 for the valves/coal, 3 --
--       REVCONFIG_BUTTON_TYPE_CLOSE -- for the close icon,
--       `src/game/rs_minimenu_build.c`'s `if_button_action_for_type`), so
--       `op>=1` misroutes the five valve/coal buttons onto the IF3
--       numbered-op path (`torirs_plugin_bridge.u.c`'s
--       `app_plugin_click_node` banner) and `op=0` is their fix -- measured
--       live, the whole minigame converges (total=26) on real button
--       presses. The close icon is the driver seam the queue row actually
--       named, and it SURVIVES the op=0 fix: `if_click` answers `ok` every
--       time (`REVCONFIG_MINIMENU_CLOSE_MODAL`'s own branch,
--       `src/game/rs_if1_buttons.c`, does call the same
--       `close_modal`-request sink `t.shop.close` uses) but the interface
--       never actually unmounts behind it. `t.key("escape")` reaches the
--       identical `app->host.close_modal_requested` flag by a different
--       path (`src/app/app_hotkeys.c:119`, the same flag
--       `DriveChat_CloseModal` sets) and is what actually closes it -- a
--       real player's Escape key, not a bypass.
--   (4) the dye-bellows leg: dyeing now only produces an intermediate
--       `mourning_ogre_bellows_<colour>` (mend1_sheep.rs2's 2026-09-23
--       header); the finished `mourning_bloated_toad_<colour>` comes from
--       using that dyed bellows on a real swamp toad (Big Chompy Bird
--       Hunting's `toad`, Feldip Hills m36_47/m37_47.spawn, spliced onto
--       `quest_chompybird/scripts/swamp_toad.rs2`'s own `[opnpcu,toad]`).

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
        -- mend1_sheep.rs2's dye-the-bellows step: all four colours are
        -- driven now that t.player.unequip lets the device cycle worn/backpack
        -- (RETRY after 68c5e8d9d), the bellows itself is reused
        -- (isNotConsumed() per quest-helper).
        "::give reddye 1",
        "::give greendye 1",
        "::give bluedye 1",
        "::give yellowdye 1",
        "::give empty_ogre_bellows 1",
        -- Combat prerequisite for killMourner (mend1_disguise.rs2's
        -- mourning_overpass_mourner, REAL combat since OSRS-Content
        -- c8b3e2fede/aa38f602a3 -- see this file's header): gear and levels
        -- are a prerequisite, not the quest's own work
        -- (docs/QUEST_AUTHORING.md section 8's player.attack note), the
        -- same idiom betweenarock.lua/mortton.lua already use. Coal for the
        -- fractionalising still (quest-helper's own coal20OrNaphtha
        -- ItemRequirement) is NOT given here -- 10 raw non-stackable coal
        -- held the whole quest drops this gate's own free-slot check below
        -- 7 (measured), so it is given mid-run instead, once the earlier
        -- setup items are consumed and the slots are free again.
        "::give rune_scimitar 1",
        "::give shark 3",
        "::setlevel attack 40",
        "::setlevel strength 40",
        "::setlevel defence 40",
        "::setlevel hitpoints 40",
        "::setlevel ranged 60",
        "::setlevel thieving 50",
        -- Elena (elena2, configs/all.npc) is a HIDDEN multinpc shell
        -- (multinpc1=-1, multinpc3=-1) until %plaguecity_elena_at_home = 1
        -- (multinpc2=elena2_vis, value 1) -- quest_elena.rs2's own comment
        -- names it: "Elena's East Ardougne home placement is a hidden
        -- multinpc shell until she has escaped West Ardougne. The Biohazard
        -- and Mourning's End dialogue both reuse this same world npc after
        -- Plague City." MEASURED live: with no Plague City progress, a
        -- goto_tile to her own spawn tile (2592,3336,0) plus a full
        -- pose+pixel click hunt AND a t.drive.op bypass both found nothing
        -- -- npc 2011 answered not_found even to the bypass, because she is
        -- not rendered at all, not merely off-camera. Plague City is itself
        -- one of Roving Elves' own transitive prerequisites (mend1.constant
        -- header's wiki cross-check: "Regicide, Underground Pass, Biohazard,
        -- Plague City, Waterfall Quest via Roving Elves"), same tier as the
        -- Waterfall/Regicide/Chompy Bird staging below -- quest_plaguecity
        -- has its own ::complete arm (quests/scripts/quest_cheat.rs2:947-951)
        -- that calls quest_elena_set_progress(^elena_complete), which writes
        -- %plaguecity_elena_at_home = 1 itself (quest_elena.rs2:9-13).
        "::complete quest_plaguecity",
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
        -- The photograph's camera.  Every click verb photographs the pose
        -- its press left, and in the Mourner HQ basement (a cave-rock room)
        -- that pose is an all-black viewport, at the gnome cage a rock face,
        -- and beside the sheep fields an oak canopy (sampler sonnet-b12:
        -- shots 47-90).  Before each row there, the camera is put at the
        -- steepest pitch the driver accepts (128..383), close in: a click
        -- verb re-reads the projection from the CURRENT pose first and only
        -- moves the camera when that pose cannot see its target, so the
        -- row's own shot is taken from here.  drive.camera writes the orbit
        -- angles and returns -- no await, no tick.
        local SHOT_YAW = 0
        -- In the Mourner HQ basement yaw 0 leaves the room's south wall
        -- block over the entry tile and the cave rock over the gnome rack
        -- (player and Essyllt hidden); a sweep of 8 yaws x zoom 200/400 at
        -- 2044,4628 and 2035,4628 (build/seam8_mesp/cam_probe.lua, run
        -- seam8_mesp_camprobe) showed both only from the east, yaw 1536.
        local SHOT_YAW_BASEMENT = 1536
        local SHOT_ZOOM_INDOOR = 200
        local SHOT_ZOOM_OUTDOOR = 600
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
        -- GUIDE-GAP: talkToIslwyn quest-helper's steps 0 and 1 are two identical talkToIslwyn NpcStep entries with no distinguishing action; the port has one proc (mend1_shared.rs2:39) for the whole native 0->2 transition, driven once below.
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
        -- m35_52.spawn:9, base symbol mourning_overpass_mourner). RE-AUTHOR
        -- (this file's header): the live entity's own menu carries no
        -- Talk-to row (op1 answers "menu has no row for it" -- confirmed
        -- live, only Attack/Examine/Walk here), and Attack is REAL combat
        -- now -- `[apnpc2,mourning_overpass_mourner]`/`[opnpc2,...]` both
        -- jump `@player_combat_start[_ap]` (mend1_disguise.rs2:11-40,
        -- QUEST_AUTHORING.md point 31's shape: a quest npc bound through the
        -- wildcard combat entry, not a one-click narration). The mourner is
        -- weak (all.npc: stat1..4 = 8/8/8/19) and the rune scimitar +
        -- level-40 melee from setup make this a short fight; equip the
        -- weapon first (it arrived in the backpack from setup). Loot is a
        -- real death drop (mend1_disguise.rs2's [ai_queue3,...]), not a
        -- click grant -- graded on npc.await_dead_engaged, not the press. ----
        t.exec("equip.scimitar", t.player.equip, "rune_scimitar")
        t.exec("goto-killMourner", t.player.goto_tile, 2299, 3328, 0)
        -- The mourner's own header names a companion
        -- (mourning_overpass_mourner_spawner) at this same spawn row --
        -- a spawner cycling a fresh copy in mid-fight is exactly the "kill
        -- never proven by an empty pool" shape docs/QUEST_AUTHORING.md
        -- trap 141/287 describe, and this pass measured it live: the first
        -- await_dead_engaged answered `dead` with its LAST reading at
        -- 18/30 (not 0), and the very next shot showed a mourner standing
        -- there with a near-FULL health bar -- the slot left the pool
        -- without ever dying. So this attacks again whenever a live copy
        -- is still standing after "dead", up to three attempts, and only
        -- trusts the kill once none remains.
        local mourner_kill_confirmed = false
        local mourner_kill_attempts = 0
        while not mourner_kill_confirmed and mourner_kill_attempts < 3 do
            mourner_kill_attempts = mourner_kill_attempts + 1
            local mourner_attack_result, mourner_attack_detail =
                t.player.attack("mourning_overpass_mourner", 2, 30)
            t.step("killMourner.attack." .. mourner_kill_attempts,
                (mourner_attack_result == "ok" or mourner_attack_result == "timeout") and "PASS" or "FAIL",
                "attack(mourning_overpass_mourner,2,30) attempt " .. mourner_kill_attempts .. " -> "
                    .. tostring(mourner_attack_result) .. " " .. tostring(mourner_attack_detail))
            local mourner_dead_result, mourner_dead_detail = t.npc.await_dead_engaged(60)
            t.step("killMourner.awaitDead." .. mourner_kill_attempts,
                mourner_dead_result == "ok" and "PASS" or "FAIL",
                "npc.await_dead_engaged(60) attempt " .. mourner_kill_attempts .. " -> "
                    .. tostring(mourner_dead_result) .. " " .. tostring(mourner_dead_detail))
            t.ticks(2)
            local still_alive_r = t.npc.nearest("mourning_overpass_mourner", 10)
            mourner_kill_confirmed = still_alive_r ~= "ok"
        end
        t.shot("killMourner.attack-after")
        t.step("killMourner",
            mourner_kill_confirmed and "PASS" or "FAIL",
            "killed after " .. tostring(mourner_kill_attempts) .. " attempt(s), no live copy remains")
        -- RE-AUTHOR: the death handler ([ai_queue3,mourning_overpass_mourner])
        -- drops all seven pieces as real (private) ground items via
        -- obj_add(npc_coord, ..., ^lootdrop_duration) and prints NO chat
        -- line at all -- the old "You collect a set of..." grant message
        -- this row used to assert is gone; each piece is picked up by hand,
        -- same as quest-helper's own separate pickUpLoot step. A private
        -- drop lags the zone packet like a backpack grant (section 8's
        -- obj_add_private trap), so this polls presence before clicking. ----
        local boots_near_r, boots_near = t.world.obj_near("mourning_mourner_boots", 10)
        t.step("killMourner.lootDrop.present", boots_near_r == "ok" and "PASS" or "FAIL",
            "world.obj_near(mourning_mourner_boots, 10) -> " .. tostring(boots_near_r) .. " "
                .. (boots_near_r == "ok"
                    and string.format("tile=%s,%s,%s", tostring(boots_near.tile_x), tostring(boots_near.tile_z), tostring(boots_near.level))
                    or tostring(boots_near)))
        local boots_pick_result = t.player.click_obj("mourning_mourner_boots", 3)
        local gloves_pick_result = t.player.click_obj("mourning_mourner_gloves", 3)
        local cloak_pick_result = t.player.click_obj("mourning_mourner_cloak", 3)
        local legs_pick_result = t.player.click_obj("mourning_ripped_mourner_legs", 3)
        local mask_pick_result = t.player.click_obj("gasmask", 3)
        local letter_pick_result = t.player.click_obj("mourning_mourner_message", 3)
        local top_pick_result = t.player.click_obj("mourning_bloody_mourner_top", 3)
        t.step("killMourner.loot.picks",
            (boots_pick_result == "ok" and gloves_pick_result == "ok" and cloak_pick_result == "ok"
                and legs_pick_result == "ok" and mask_pick_result == "ok" and letter_pick_result == "ok"
                and top_pick_result == "ok") and "PASS" or "FAIL",
            string.format("click_obj results: boots=%s gloves=%s cloak=%s legs=%s mask=%s letter=%s top=%s",
                tostring(boots_pick_result), tostring(gloves_pick_result), tostring(cloak_pick_result),
                tostring(legs_pick_result), tostring(mask_pick_result), tostring(letter_pick_result), tostring(top_pick_result)))
        t.shot("killMourner.loot-after")
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
        -- Lletya's fence stands between the arrival pose and the player
        -- (the talk and equip shots showed only a fence face); look down.
        t.drive.camera(SHOT_YAW, 383, SHOT_ZOOM_OUTDOOR)
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
        t.drive.camera(SHOT_YAW, 383, SHOT_ZOOM_OUTDOOR)
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
        t.drive.camera(SHOT_YAW_BASEMENT, 383, SHOT_ZOOM_INDOOR)
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
        t.drive.camera(SHOT_YAW_BASEMENT, 383, SHOT_ZOOM_INDOOR)
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
            t.drive.camera(SHOT_YAW_BASEMENT, 383, SHOT_ZOOM_INDOOR)
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
        t.drive.camera(SHOT_YAW_BASEMENT, 383, SHOT_ZOOM_INDOOR)
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
        -- GUIDE-GAP: useFeatherOnGnome quest-helper's own step ("Use a feather on the gnome"); this port's %mourning_gnome write is on the loc's own mesbox branch (mend1_gnome.rs2:94), driven by the click_loc row right below, not a separate use_on.
        t.drive.camera(SHOT_YAW_BASEMENT, 383, SHOT_ZOOM_INDOOR)
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
        t.drive.camera(SHOT_YAW_BASEMENT, 383, SHOT_ZOOM_INDOOR)
        t.exec("gnomeCage.talkAgain", t.player.click_loc, "mourning_gnome_rack", 1)
        t.exec("gnomeCage.talkAgain-dialog", t.chat.play, {
            "mesbox:Bring me those things and let me out of here",
        })
        t.chat.close()

        -- ---- Click 4: mend1_gnome_release (mend1_gnome.rs2:106-112) --
        -- consumes the key; a plain mes(), not a page. ----
        t.drive.camera(SHOT_YAW_BASEMENT, 383, SHOT_ZOOM_INDOOR)
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
        -- GUIDE-GAP: giveGnomeItems mend1.constant:141 -- quest-helper's giveGnomeItems/askAboutToads assume a separate walking gnome NPC; this port has none, keeping every gnome interaction on the one mourner_hideout_gnome entity (m31_72.spawn:33, [opnpc1,...] -> mend1_gnome_rack_shared, mend1_gnome.rs2:128), driven by the row right below.
        t.drive.camera(SHOT_YAW_BASEMENT, 383, SHOT_ZOOM_INDOOR)
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
        -- GUIDE-GAP: askAboutToads mend1.constant:141 -- the same collapse as giveGnomeItems above: no separate walking gnome NPC, driven on the same mourner_hideout_gnome entity (mend1_gnome.rs2:141, [label,mend1_gnome_ask_toads]) by the row right below.
        t.drive.camera(SHOT_YAW_BASEMENT, 383, SHOT_ZOOM_INDOOR)
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

        -- ---- Dye the bellows and catch a real swamp toad with them, one
        -- colour at a time (mend1_sheep.rs2, RE-AUTHOR per this file's
        -- header point (4)): only [opheldu,empty_ogre_bellows] is declared
        -- (item_a is always the bellows), and catching a toad returns the
        -- bellows to plain empty_ogre_bellows (mend1_catch_toad_*'s own
        -- inv_add), so the two beats alternate dye/catch x4 rather than
        -- dyeing all four up front. Dyeing has no location gate at all, so
        -- this whole ladder is driven at the toad ground itself -- Big
        -- Chompy Bird Hunting's own `toad`, Feldip Hills
        -- (areas/world/configs/m36_47.spawn/m37_47.spawn), one goto for all
        -- four colours and catches. One retry on the first dye's bare
        -- arming miss, same shape as the cleanTop retry above (use_on
        -- re-arms before every retry press per docs/QUEST_AUTHORING.md
        -- section 6). ----
        t.drive.camera(SHOT_YAW, 383, SHOT_ZOOM_OUTDOOR)
        t.exec("goto-toadGround", t.player.goto_tile, 2394, 3050, 0)
        -- t.player.by_symbol resolves the SYMBOL, and click_minimenu re-picks
        -- the live copy nearest the viewport centre at PRESS time -- not
        -- cached -- but that copy can still be a poor pixel (run 1 measured
        -- one catch spend its whole 99-pixel hunt budget and fail while the
        -- next two succeeded from the same spot). t.npc.nearest + walk_near
        -- puts the player standing next to a KNOWN live copy before every
        -- catch instead of trusting a from-range hunt, the same idiom the
        -- sheep-firing rows below already use.
        local toad_target = t.player.by_symbol("npc", "toad")

        local dye_red_result, dye_red_detail = t.player.use_item_on_item("empty_ogre_bellows", "reddye")
        if dye_red_result ~= "ok" then
            t.ticks(3)
            dye_red_result, dye_red_detail = t.player.use_item_on_item("empty_ogre_bellows", "reddye")
        end
        t.step("dyeBellows.red", dye_red_result == "ok" and "PASS" or "FAIL",
            "use_item_on_item(empty_ogre_bellows, reddye) -> " .. tostring(dye_red_result) .. " " .. tostring(dye_red_detail))
        local toad_red_r, toad_red = t.npc.nearest("toad", 15)
        t.step("toadRed.locate", toad_red_r == "ok" and "PASS" or "FAIL", "npc.nearest(toad, 15) -> " .. tostring(toad_red_r))
        if toad_red_r == "ok" then
            local toad_red_walk_result = t.player.walk_to(toad_red.x, toad_red.z, 10)
            -- ok or timeout both count: a toad that wanders a tile mid-walk
            -- makes the destination tile stale, but the player still ends up
            -- close enough for use_on's own hunt (measured: catchToad.yellow
            -- PASSed on a run where this exact row timed out).
            t.step("walk-toadRed", (toad_red_walk_result == "ok" or toad_red_walk_result == "timeout") and "PASS" or "FAIL",
                "walk_to(" .. tostring(toad_red.x) .. "," .. tostring(toad_red.z) .. ") -> " .. tostring(toad_red_walk_result))
        end
        t.exec("catchToad.red", t.player.use_on, "mourning_ogre_bellows_red", toad_target)
        t.ticks(2)
        t.expect("catchToad.red.message", t.msg.expect("You catch the toad and inflate it with your dyed bellows"))

        t.exec("dyeBellows.yellow", t.player.use_item_on_item, "empty_ogre_bellows", "yellowdye")
        local toad_yellow_r, toad_yellow = t.npc.nearest("toad", 15)
        t.step("toadYellow.locate", toad_yellow_r == "ok" and "PASS" or "FAIL", "npc.nearest(toad, 15) -> " .. tostring(toad_yellow_r))
        if toad_yellow_r == "ok" then
            local toad_yellow_walk_result = t.player.walk_to(toad_yellow.x, toad_yellow.z, 10)
            -- ok or timeout both count: a toad that wanders a tile mid-walk
            -- makes the destination tile stale, but the player still ends up
            -- close enough for use_on's own hunt (measured: catchToad.yellow
            -- PASSed on a run where this exact row timed out).
            t.step("walk-toadYellow", (toad_yellow_walk_result == "ok" or toad_yellow_walk_result == "timeout") and "PASS" or "FAIL",
                "walk_to(" .. tostring(toad_yellow.x) .. "," .. tostring(toad_yellow.z) .. ") -> " .. tostring(toad_yellow_walk_result))
        end
        t.exec("catchToad.yellow", t.player.use_on, "mourning_ogre_bellows_yellow", toad_target)
        t.ticks(2)
        t.expect("catchToad.yellow.message", t.msg.expect("You catch the toad and inflate it with your dyed bellows"))

        t.exec("dyeBellows.green", t.player.use_item_on_item, "empty_ogre_bellows", "greendye")
        local toad_green_r, toad_green = t.npc.nearest("toad", 15)
        t.step("toadGreen.locate", toad_green_r == "ok" and "PASS" or "FAIL", "npc.nearest(toad, 15) -> " .. tostring(toad_green_r))
        if toad_green_r == "ok" then
            local toad_green_walk_result = t.player.walk_to(toad_green.x, toad_green.z, 10)
            -- ok or timeout both count: a toad that wanders a tile mid-walk
            -- makes the destination tile stale, but the player still ends up
            -- close enough for use_on's own hunt (measured: catchToad.yellow
            -- PASSed on a run where this exact row timed out).
            t.step("walk-toadGreen", (toad_green_walk_result == "ok" or toad_green_walk_result == "timeout") and "PASS" or "FAIL",
                "walk_to(" .. tostring(toad_green.x) .. "," .. tostring(toad_green.z) .. ") -> " .. tostring(toad_green_walk_result))
        end
        t.exec("catchToad.green", t.player.use_on, "mourning_ogre_bellows_green", toad_target)
        t.ticks(2)
        t.expect("catchToad.green.message", t.msg.expect("You catch the toad and inflate it with your dyed bellows"))

        t.exec("dyeBellows.blue", t.player.use_item_on_item, "empty_ogre_bellows", "bluedye")
        local toad_blue_r, toad_blue = t.npc.nearest("toad", 15)
        t.step("toadBlue.locate", toad_blue_r == "ok" and "PASS" or "FAIL", "npc.nearest(toad, 15) -> " .. tostring(toad_blue_r))
        if toad_blue_r == "ok" then
            local toad_blue_walk_result = t.player.walk_to(toad_blue.x, toad_blue.z, 10)
            -- ok or timeout both count: a toad that wanders a tile mid-walk
            -- makes the destination tile stale, but the player still ends up
            -- close enough for use_on's own hunt (measured: catchToad.yellow
            -- PASSed on a run where this exact row timed out).
            t.step("walk-toadBlue", (toad_blue_walk_result == "ok" or toad_blue_walk_result == "timeout") and "PASS" or "FAIL",
                "walk_to(" .. tostring(toad_blue.x) .. "," .. tostring(toad_blue.z) .. ") -> " .. tostring(toad_blue_walk_result))
        end
        t.exec("catchToad.blue", t.player.use_on, "mourning_ogre_bellows_blue", toad_target)
        t.ticks(2)
        t.expect("catchToad.blue.message", t.msg.expect("You catch the toad and inflate it with your dyed bellows"))

        local redtoad_r, redtoad_n = t.inv.count("mourning_bloated_toad_red")
        local greentoad_r, greentoad_n = t.inv.count("mourning_bloated_toad_green")
        local bluetoad_r, bluetoad_n = t.inv.count("mourning_bloated_toad_blue")
        local yellowtoad_r, yellowtoad_n = t.inv.count("mourning_bloated_toad_yellow")
        t.check("dyeBellows.result",
            redtoad_r == "ok" and redtoad_n == 1 and greentoad_r == "ok" and greentoad_n == 1
                and bluetoad_r == "ok" and bluetoad_n == 1 and yellowtoad_r == "ok" and yellowtoad_n == 1,
            string.format("mourning_bloated_toad_red=%s mourning_bloated_toad_green=%s mourning_bloated_toad_blue=%s mourning_bloated_toad_yellow=%s",
                tostring(redtoad_n), tostring(greentoad_n), tostring(bluetoad_n), tostring(yellowtoad_n)))

        -- ---- Load the red toad (device unworn, still a backpack cell),
        -- equip the device, and fire it at the sheep herd mend1_sheep.rs2's
        -- own mend1_try_fire_sheep maps to herder_plaguesheep_1. The LIVE
        -- entity is the map's own base symbol, plaguesheep_1
        -- (areas/world/configs/m40_52.spawn), which diseased_sheep.rs2's
        -- [opnpc1,plaguesheep_1] -> prod_sheep(herder_plaguesheep_1) routes
        -- into the same shared label (trap 19/20's base-symbol shape). ----
        t.drive.camera(SHOT_YAW, 383, SHOT_ZOOM_OUTDOOR)
        t.exec("loadToad.red", t.player.use_item_on_item, "mourning_bloated_toad_red", "mourning_paint_gun")
        t.exec("equip.paintgun", t.player.equip, "mourning_paint_gun")
        -- by_symbol/walk_near need the npc loaded into the CLIENT's nearby
        -- region first -- we are still at the Feldip Hills toad ground, a
        -- whole region away, so goto_tile to the sheep field before either
        -- (m40_52.spawn's plaguesheep_1 cluster: 2609-2610,3343-3345).
        t.exec("goto-sheepField", t.player.goto_tile, 2610, 3344, 0)
        local sheep1, sheep1_r = t.player.by_symbol("npc", "plaguesheep_1")
        t.step("sheep1.locate", sheep1_r == "ok" and "PASS" or "FAIL", "by_symbol(npc, plaguesheep_1) -> " .. tostring(sheep1_r))
        if sheep1_r == "ok" then
            t.exec("walk-sheep1", t.player.walk_near, sheep1, 15)
        end
        t.drive.camera(SHOT_YAW, 383, SHOT_ZOOM_OUTDOOR)
        t.exec("fireSheep.red", t.player.talk_to, "plaguesheep_1", 1)
        t.ticks(2)
        t.expect("fireSheep.red.message", t.msg.expect("the red toad splats across the sheep"))

        -- ---- FIXED (RETRY after 68c5e8d9d): mend1_sheep.rs2's
        -- mend1_try_fire_sheep requires mourning_paint_gun to be WORN to
        -- fire but the load is a backpack-only [opheldu,...] item-on-item
        -- interaction, so the device must come back OFF between shots.
        -- t.player.unequip(item) now exists (script/plugins/quest_driver/
        -- pointer.lua) -- the cycle below is unequip -> use_item_on_item ->
        -- equip -> walk -> fire, once per remaining colour. ----

        -- ---- Green (herder_plaguesheep_2, m40_52.spawn:38 --
        -- 2621-2623,3366-3368) ----
        t.exec("unequip.paintgun.green", t.player.unequip, "mourning_paint_gun")
        t.exec("loadToad.green", t.player.use_item_on_item, "mourning_bloated_toad_green", "mourning_paint_gun")
        t.exec("equip.paintgun.green", t.player.equip, "mourning_paint_gun")
        t.drive.camera(SHOT_YAW, 383, SHOT_ZOOM_OUTDOOR)
        t.exec("goto-sheepField-green", t.player.goto_tile, 2621, 3368, 0)
        local sheep2, sheep2_r = t.player.by_symbol("npc", "plaguesheep_2")
        t.step("sheep2.locate", sheep2_r == "ok" and "PASS" or "FAIL", "by_symbol(npc, plaguesheep_2) -> " .. tostring(sheep2_r))
        if sheep2_r == "ok" then
            t.exec("walk-sheep2", t.player.walk_near, sheep2, 15)
        end
        t.drive.camera(SHOT_YAW, 383, SHOT_ZOOM_OUTDOOR)
        t.exec("fireSheep.green", t.player.talk_to, "plaguesheep_2", 1)
        t.ticks(2)
        t.expect("fireSheep.green.message", t.msg.expect("the green toad splats across the sheep"))

        -- ---- Blue (herder_plaguesheep_3, m40_52.spawn:8-13 --
        -- 2560-2561,3388-3390) ----
        t.exec("unequip.paintgun.blue", t.player.unequip, "mourning_paint_gun")
        t.exec("loadToad.blue", t.player.use_item_on_item, "mourning_bloated_toad_blue", "mourning_paint_gun")
        t.exec("equip.paintgun.blue", t.player.equip, "mourning_paint_gun")
        t.drive.camera(SHOT_YAW, 383, SHOT_ZOOM_OUTDOOR)
        t.exec("goto-sheepField-blue", t.player.goto_tile, 2562, 3390, 0)
        local sheep3, sheep3_r = t.player.by_symbol("npc", "plaguesheep_3")
        t.step("sheep3.locate", sheep3_r == "ok" and "PASS" or "FAIL", "by_symbol(npc, plaguesheep_3) -> " .. tostring(sheep3_r))
        if sheep3_r == "ok" then
            t.exec("walk-sheep3", t.player.walk_near, sheep3, 15)
        end
        t.drive.camera(SHOT_YAW, 383, SHOT_ZOOM_OUTDOOR)
        t.exec("fireSheep.blue", t.player.talk_to, "plaguesheep_3", 1)
        t.ticks(2)
        t.expect("fireSheep.blue.message", t.msg.expect("the blue toad splats across the sheep"))

        -- ---- Yellow (herder_plaguesheep_4, m40_52.spawn:31-33 --
        -- 2610-2612,3390-3391) ----
        t.exec("unequip.paintgun.yellow", t.player.unequip, "mourning_paint_gun")
        t.exec("loadToad.yellow", t.player.use_item_on_item, "mourning_bloated_toad_yellow", "mourning_paint_gun")
        t.exec("equip.paintgun.yellow", t.player.equip, "mourning_paint_gun")
        t.drive.camera(SHOT_YAW, 383, SHOT_ZOOM_OUTDOOR)
        t.exec("goto-sheepField-yellow", t.player.goto_tile, 2610, 3391, 0)
        local sheep4, sheep4_r = t.player.by_symbol("npc", "plaguesheep_4")
        t.step("sheep4.locate", sheep4_r == "ok" and "PASS" or "FAIL", "by_symbol(npc, plaguesheep_4) -> " .. tostring(sheep4_r))
        if sheep4_r == "ok" then
            t.exec("walk-sheep4", t.player.walk_near, sheep4, 15)
        end
        t.drive.camera(SHOT_YAW, 383, SHOT_ZOOM_OUTDOOR)
        t.exec("fireSheep.yellow", t.player.talk_to, "plaguesheep_4", 1)
        t.ticks(2)
        t.expect("fireSheep.yellow.message", t.msg.expect("the yellow toad splats across the sheep"))

        t.ticks(3)
        t.settle()
        local journal_marked_r, journal_marked = t.ui.journal_open("Mourning's End Part I")
        local journal_marked_note = ""
        if journal_marked_r ~= "ok" then
            -- Same one-off UI-queue race the journal_assignment read above
            -- hit (right after four presses in quick succession) -- retry
            -- once with more settle.
            journal_marked_note = " [retry after timeout: " .. tostring(journal_marked) .. "]"
            t.ticks(5)
            t.settle()
            journal_marked_r, journal_marked = t.ui.journal_open("Mourning's End Part I")
        end
        local journal_marked_line = (type(journal_marked) == "table") and journal_marked.first_line or nil
        t.check("quest.stage.sheep_marked",
            journal_marked_r == "ok" and journal_marked_line ~= nil
                and journal_marked_line:find("All four sheep herds are marked", 1, true) ~= nil,
            "journal_open(Mourning's End Part I) -> " .. tostring(journal_marked_r) .. " "
                .. (journal_marked_line ~= nil and ("first_line=" .. journal_marked_line) or tostring(journal_marked))
                .. journal_marked_note)
        t.ui.journal_close()

        -- ---- Report back to Essyllt, HQ basement (mend1_essyllt_after_sheep,
        -- mend1_disguise.rs2:178-205). Quest-helper lists enterBaseAfterSheep
        -- as its own standalone step (unlike the AfterPoison return, which
        -- is folded as a substep of talkToEssylltAfterPoison and never
        -- enumerated on its own -- helper_coverage.py confirmed the
        -- difference), so this door is clicked for real a second time
        -- rather than goto_tile past it -- doors.rs2's own
        -- west_ardougne_mourner_headquarters_doors gate reads
        -- %mourning_mourner_disguise (already 1 from the first pass) and
        -- walks through SILENTLY the second time, no mes() at all, so this
        -- row asserts no message. The trapdoor beyond it stays a direct
        -- goto -- that one IS the ladder/trapdoor exception (section 2:
        -- its own p_teleport already lands on this exact tile, and
        -- quest-helper folds its own AfterSheep trapdoor return the same
        -- way it folds AfterPoison's, so it is never independently
        -- enumerated). ----
        t.exec("goto-enterBaseAfterSheep", t.player.goto_tile, 2551, 3320, 0)
        t.exec("enterBaseAfterSheep", t.player.click_loc, "mournerstewdoor", 1)
        t.exec("goto-talkToEssylltAfterSheep", t.player.goto_tile, 2043, 4631, 0)
        t.exec("talkToEssylltAfterSheep", t.player.talk_to, "mourner_hideout_head_mourner", 1)
        t.exec("talkToEssylltAfterSheep-dialog", t.chat.play, {
            "player:It's done -- all four sheep herds marked, just as the gnome's device intended.",
            "npc:Excellent. That confirms the signal carries. Now, one more test -- I want to see how far you'll go for us.",
            "npc:Somewhere near here is a rotten apple. Fetch it -- Elena, north-west of East Ardougne, knows what to do with it. If you can poison our food stores without being caught, you'll have earned real trust.",
        })
        -- ---- Pick up the rotten apple, north-west of the Mourner HQ
        -- (mend1_poison.rs2's own header: a real `rottenapples` ground item
        -- sits at areas/world/configs/m39_52.spawn x2535/y3333, exactly
        -- quest-helper's own pickUpRottenApple WorldPoint -- "already works
        -- for free" via the engine's generic ground-item take, no script
        -- needed, but driven here as its own row rather than left to
        -- incidental pickup on some earlier walk). ----
        t.exec("goto-pickUpRottenApple", t.player.goto_tile, 2535, 3333, 0)
        -- RUN 4 measured this: goto_tile's own arrival on the stack's tile
        -- already collects it (the "already works for free" header banner
        -- is literal, not just narrative) -- an unconditional click_obj
        -- here found a SECOND, distant rottenapples spawn instead
        -- (m39_52.spawn:56, 2549,3332) and picked that one up too, leaving
        -- two in the backpack where mend1_elena_talk's own inv_del(...,1)
        -- only ever removes one. Only click if the arrival did not already
        -- collect it.
        local apple_precount_r, apple_precount_n = t.inv.count("rottenapples")
        local rottenapple_pick_result = "skipped: goto's own arrival already holds "
            .. tostring(apple_precount_n)
        if not (apple_precount_r == "ok" and apple_precount_n >= 1) then
            rottenapple_pick_result = t.player.click_obj("rottenapples", 3)
        end
        local apple_wait_r, apple_wait_detail = t.inv.await("rottenapples", 1, 10)
        t.step("pickUpRottenApple", apple_wait_r == "ok" and "PASS" or "FAIL",
            "click_obj(rottenapples, 3) -> " .. tostring(rottenapple_pick_result)
                .. "; inv.await(rottenapples, 1, 10) -> " .. tostring(apple_wait_r) .. " " .. tostring(apple_wait_detail))
        t.ticks(3)
        local journal_poison0_r, journal_poison0 = t.ui.journal_open("Mourning's End Part I")
        local journal_poison0_line = journal_poison0 and journal_poison0.first_line
        t.check("quest.stage.poison_task",
            journal_poison0_r == "ok" and journal_poison0_line ~= nil
                and journal_poison0_line:find("Essyllt set me one more test", 1, true) ~= nil,
            "journal_open(Mourning's End Part I) -> " .. tostring(journal_poison0_r) .. " first_line=" .. tostring(journal_poison0_line))
        t.ui.journal_close()

        -- ---- Elena, north-west East Ardougne (elena2, m40_52.spawn:22 --
        -- 2592,3336,0; mend1_elena_talk, mend1_poison.rs2:29-42 -- the
        -- rottenapples>=1 branch, opened by [opnpc1,elena2]'s own front-of-
        -- trigger call once %mourning_quest = poison_task). A hand-typed
        -- goto tile 1 south of her put the camera inside a nearby building
        -- with no line of sight to her at all (MEASURED: a full pose+pixel
        -- hunt found no Talk-to row, only Cancel/Walk here) -- t.npc.by_symbol
        -- reads her own LIVE tile (unlike t.player.by_symbol's target table,
        -- this row carries x/z/level) so goto_tile lands exactly on her,
        -- the same fix mourningGnomeRack.locate used for a loc. ----
        t.exec("goto-talkToElena", t.player.goto_tile, 2592, 3335, 0)
        local elena_row_r, elena_row = t.npc.by_symbol("elena2")
        t.step("elena.locate", elena_row_r == "ok" and "PASS" or "FAIL",
            "npc.by_symbol(elena2) -> " .. tostring(elena_row_r) .. " "
                .. (elena_row_r == "ok" and string.format("tile=%s,%s,%s", tostring(elena_row.x), tostring(elena_row.z), tostring(elena_row.level)) or ""))
        if elena_row_r == "ok" then
            t.exec("goto-elena-exact", t.player.goto_tile, elena_row.x, elena_row.z, elena_row.level)
        end
        local elena_talk_result, elena_talk_detail = t.player.talk_to("elena2", 1)
        if elena_talk_result ~= "ok" then
            local elena_op_target, elena_op_target_r = t.player.by_symbol("npc", "elena2")
            if elena_op_target_r == "ok" then
                elena_talk_result, elena_talk_detail = t.drive.op(elena_op_target, 1)
                elena_talk_detail = "[bypass after 1 failed on-screen press] " .. tostring(elena_talk_detail)
                t.ticks(2)
            end
        end
        t.step("talkToElena", elena_talk_result == "ok" and "PASS" or "FAIL",
            "talk_to(elena2,1) -> " .. tostring(elena_talk_result) .. " " .. tostring(elena_talk_detail))
        t.shot("talkToElena-after")
        t.exec("talkToElena-dialog", t.chat.play, {
            "player:Essyllt sent me. I need to poison the mourners' food stores.",
            "npc:Then you'll want a real batch of toxin, not a single apple. Fetch a barrel and fill it with rotten apples from the orchard north of here, then press them at the barrel there.",
            "npc:Take the mash south to the Chemist's still near Rimmington and fill it out with naphtha, then bring it back to me to sieve.",
            "npc:Cook what's left on a range -- not a fire -- and you'll have your toxic powder. Two of the mourners' food stores in West Ardougne should do it.",
        })
        local sieve_wait_r, sieve_wait_detail = t.inv.await("mourning_sieve", 1, 10)
        t.step("talkToElena.await_sieve", sieve_wait_r == "ok" and "PASS" or "FAIL",
            "inv.await(mourning_sieve, 1, 10) -> " .. tostring(sieve_wait_r) .. " " .. tostring(sieve_wait_detail))
        local apple_after_r, apple_after_n = t.inv.count("rottenapples")
        t.check("talkToElena.result", apple_after_r == "ok" and apple_after_n == 0,
            "rottenapples=" .. tostring(apple_after_n) .. " (" .. tostring(apple_after_r) .. ")")

        -- ---- Barrel + apple pile, north-west of the Mourner HQ
        -- (mourning_orchard_applepile, mend1_poison.rs2:45-58, RE-AUTHOR per
        -- this file's header point (2): pickUpBarrel is a real ground item
        -- now (regicide_barrel_empty, areas/world/configs/m38_52.spawn:38,
        -- exactly quest-helper's own pickUpBarrel WorldPoint) and
        -- useBarrelOnPile is a real [oplocu,...] use-on -- the bare
        -- [oploc1,...] only tells the player they need a barrel now. ----
        t.exec("goto-fillBarrel", t.player.goto_tile, 2487, 3371, 0)
        -- click_obj answers `ok` with a nil detail (hollow, trap 12/section
        -- 8) -- graded by hand, the counts read back are the evidence.
        local pickup_barrel_result = t.player.click_obj("regicide_barrel_empty", 3)
        local pickup_barrel_count_r, pickup_barrel_count_n = t.inv.count("regicide_barrel_empty")
        t.check("pickUpBarrel",
            pickup_barrel_result == "ok" and pickup_barrel_count_r == "ok" and pickup_barrel_count_n >= 1,
            "click_obj(regicide_barrel_empty, 3) -> " .. tostring(pickup_barrel_result)
                .. "; regicide_barrel_empty=" .. tostring(pickup_barrel_count_n))
        local applebarrel_target = t.player.by_symbol("loc", "mourning_orchard_applepile")
        -- SHOW AND SETTLE THE BACKPACK BEFORE THE FIRST use_on OF THE RUN:
        -- use_on's arming is a one-shot behind an unsettled tab press
        -- (docs/QUEST_AUTHORING.md's "use_on's backpack tab press is not
        -- settled before its arming" trap), and the sidebar here is
        -- wherever talkToElena's own dialogue/journal reads left it.
        t.ui.tab("inventory")
        t.ticks(2)
        t.exec("fillBarrel", t.player.use_on, "regicide_barrel_empty", applebarrel_target)
        t.ticks(2)
        t.expect("fillBarrel.message", t.msg.expect("You fill the barrel with rotten apples from the pile"))
        local barrelfull_r, barrelfull_n = t.inv.count("applebarrel_full")
        t.check("fillBarrel.result", barrelfull_r == "ok" and barrelfull_n == 1,
            "applebarrel_full=" .. tostring(barrelfull_n) .. " (" .. tostring(barrelfull_r) .. ")")

        -- ---- Apple press (mourning_orchard_applebarrel_empty,
        -- mend1_poison.rs2:83-99). RETRY after 1858fe69a: the reasoning this
        -- file carried above (missing op1= => t.drive.op bypass => no_row =>
        -- blocked) was measured WRONG -- configs/all.loc's own
        -- [mourning_orchard_applebarrel_empty] record never had an op1= line
        -- to be missing; the press is a USE-ON target by design, the same
        -- shape quest-helper's own step reads ("Use the rotten apples on the
        -- apple press", MourningsEndPartI.java:440), and an op1= added to
        -- all.loc reaches only the SERVER's overlay while the client's menu
        -- is built from the frozen cache (measured byte-identically,
        -- build/quest_gate/seam_mourning_probe_locop/ledger.tsv: same three
        -- menu rows, same drive.op -> no_row, with op1=Press baked in and
        -- the pack rebuilt). mend1_poison.rs2:83-90 pairs a baked-cache
        -- [oploc1,...] with the real [oplocu,mourning_orchard_applebarrel_empty],
        -- which fires on last_useitem = applebarrel_full -- t.player.use_on
        -- is the verb. ----
        t.exec("goto-pressApples", t.player.goto_tile, 2484, 3374, 0)
        -- SHOW AND SETTLE THE BACKPACK BEFORE THE FIRST use_on OF THE RUN:
        -- the sidebar is wherever talkToElena's own dialogue/journal reads
        -- left it, and use_on's arming is a one-shot behind an unsettled tab
        -- press (docs/QUEST_AUTHORING.md trap "use_on's backpack tab press
        -- is not settled before its arming" -- measured live in this same
        -- seam, build/quest_gate/seam_mourning_armprobe/ledger.tsv:
        -- tab=equipment, no settle -> refused -- armed by this call;
        -- tab=inventory + 2 ticks -> ok).
        t.ui.tab("inventory")
        t.ticks(2)
        local press_target = t.player.by_symbol("loc", "mourning_orchard_applebarrel_empty")
        t.exec("pressApples", t.player.use_on, "applebarrel_full", press_target)
        t.ticks(2)
        t.expect("pressApples.message",
            t.msg.expect("You press the rotten apples into a foul-smelling mash"))
        t.expect("pressApples.mash", t.inv.await("mourning_applebarrel_mush", 1, 10))

        -- ---- Coal tar: a second empty barrel from Tirannwn
        -- (areas/world/configs/m34_49.spawn:67, 2184,3139,0), carried to
        -- Regicide's own tar collection point (regicide_bombcraft.rs2's
        -- regicide_collect_tar label, gated open to this quest via
        -- ~mend1_naphtha_allowed) -- RE-AUTHOR per this file's header
        -- point (3). The barrel spawn and the loc are NOT the same tile
        -- (trap 20 has no spawn row for a loc at all, so this was found by
        -- decoding maps/m34_48.jl2:908 -- `0 47 51: 3975 10 1` = ABS
        -- 34*64+47,48*64+51 = 2223,3123,0, one of three placements), so
        -- this is two separate gotos. ----
        t.drive.camera(SHOT_YAW, 383, SHOT_ZOOM_OUTDOOR)
        t.exec("goto-tarBarrel", t.player.goto_tile, 2184, 3139, 0)
        local pickup_tar_barrel_result = t.player.click_obj("regicide_barrel_empty", 3)
        local pickup_tar_barrel_count_r, pickup_tar_barrel_count_n = t.inv.count("regicide_barrel_empty")
        t.check("pickUpTarBarrel",
            pickup_tar_barrel_result == "ok" and pickup_tar_barrel_count_r == "ok" and pickup_tar_barrel_count_n >= 1,
            "click_obj(regicide_barrel_empty, 3) -> " .. tostring(pickup_tar_barrel_result)
                .. "; regicide_barrel_empty=" .. tostring(pickup_tar_barrel_count_n))
        t.exec("goto-tarCollection", t.player.goto_tile, 2223, 3123, 0)
        local tar_loc_result, tar_loc_row = t.world.loc_near("regicide_tar_collection", 15)
        t.check("collectTar.present", tar_loc_result == "ok",
            "loc_near(regicide_tar_collection, 15) -> " .. tostring(tar_loc_result) .. " "
                .. (tar_loc_result == "ok"
                    and (tostring(tar_loc_row.tile_x) .. "," .. tostring(tar_loc_row.tile_z)
                        .. " L" .. tostring(tar_loc_row.level))
                    or tostring(tar_loc_row)))
        t.exec("collectTar", t.player.click_loc, "regicide_tar_collection", 1)
        t.ticks(2)
        t.expect("collectTar.message", t.msg.expect("You fill the barrel with thick coal tar"))
        t.expect("collectTar.result", t.inv.await("regicide_barrel_tar", 1, 10))

        -- ---- Coal: a genuine bring-along per quest-helper's own
        -- coal20OrNaphtha ItemRequirement (10-20 coal alongside the tar
        -- barrel), given here rather than in setup -- see this file's
        -- header point (3) for why it cannot sit in the backpack for the
        -- whole quest. t.cheat is hollow (trap 12), so it is graded by
        -- hand, not through t.exec. ----
        -- A setup ::give is counted and waited out by run.py's own wrapper
        -- (trap 23); this one is mid-run, so the same wait is spelled by
        -- hand -- the cheat's reply outruns its own backpack effect by a
        -- tick, same as any other cheat.
        -- 12, not 15: run 2 measured the backpack holding only 12 of a
        -- requested 15 (the still minigame itself used just 7 of those, so
        -- 12 is comfortable margin, not a floor found by trial).
        local coal_cheat_result, coal_cheat_detail = t.cheat("::give coal 12")
        local coal_wait_r, coal_wait_detail = t.inv.await("coal", 10, 10)
        local coal_r, coal_n = t.inv.count("coal")
        t.step("giveCoal", (coal_cheat_result == "ok" and coal_wait_r == "ok") and "PASS" or "FAIL",
            "cheat(::give coal 12) -> " .. tostring(coal_cheat_result) .. " " .. tostring(coal_cheat_detail)
                .. "; inv.await(coal,10,10) -> " .. tostring(coal_wait_r) .. " " .. tostring(coal_wait_detail)
                .. "; coal=" .. tostring(coal_n))

        -- ---- Fractionalising still, Rimmington chemist (2927,3212,0 --
        -- quest-helper's own getNaphtha WorldPoint): open with the tar
        -- barrel, then drive the valve/coal minigame
        -- (regicide_fractionalising_still.rs2) until
        -- %regicide_still_total>=26 and close it. EVERY button on
        -- interface 286 is an IF1-style graphic button (if3=no, a nonzero
        -- buttontype -- this file's header point (3)), so every invoke
        -- below presses op=0, never op=1: op>=1 misroutes an IF1 button
        -- onto the IF3 numbered-op path, which is the driver seam the
        -- queue row named. t.ui.invoke answers `ok` with no detail
        -- (hollow, trap 12's shape), so it is graded by hand throughout,
        -- never through t.exec. Strategy mirrors the wiki's own text --
        -- turn the tar valve fully up, let the flow gauge climb, back it
        -- off once with the pressure valve before it pops, then add coal
        -- reactively whenever the heat gauge (bits 13-25 of
        -- %regicide_still_settings) reads below the green band (19-24) --
        -- read back from the SERVER after every press, the one channel
        -- that actually transmits (aa38f602a3: _total/_settings do, _heat
        -- does not), so the loop reacts to the real gauge, never a fixed
        -- schedule. ----
        t.drive.camera(SHOT_YAW, 383, SHOT_ZOOM_OUTDOOR)
        t.exec("goto-still", t.player.goto_tile, 2927, 3212, 0)
        t.ui.tab("inventory")
        t.ticks(2)
        local still_loc_target = t.player.by_symbol("loc", "regicide_fractionalizing_still")
        t.exec("openStill", t.player.use_on, "regicide_barrel_tar", still_loc_target)
        -- t.ui.await_open answers `ok` with a nil detail on an immediate
        -- mount (hollow on success, trap 12's shape -- only its timeout
        -- carries a reason) -- graded by hand.
        local still_open_result = t.ui.await_open("regicide_still")
        t.step("still.open", still_open_result == "ok" and "PASS" or "FAIL",
            "ui.await_open(regicide_still) -> " .. tostring(still_open_result))

        -- Symbols are the FULL "interface:component" join, one argument --
        -- QD.read._component_of's own precedent (read.lua), never a
        -- separate (interface, component) pair; `sub` stays unset (-1, "no
        -- sub index"), since none of these six is a repeated template row.
        local tar_up_widget_r, tar_up_widget = t.ui.widget("regicide_still:regicide_tar_valve_up")
        local pressure_up_widget_r, pressure_up_widget = t.ui.widget("regicide_still:regicide_pressure_valve_up")
        local add_coal_widget_r, add_coal_widget = t.ui.widget("regicide_still:regicide_add_coal")
        local still_close_widget_r, still_close_widget = t.ui.widget("regicide_still:regicide_still_close")
        t.check("still.widgets",
            tar_up_widget_r == "ok" and pressure_up_widget_r == "ok"
                and add_coal_widget_r == "ok" and still_close_widget_r == "ok",
            string.format("tar_up=%s pressure_up=%s add_coal=%s close=%s",
                tostring(tar_up_widget_r), tostring(pressure_up_widget_r),
                tostring(add_coal_widget_r), tostring(still_close_widget_r)))

        t.ui.invoke(tar_up_widget, 0)
        t.ui.invoke(tar_up_widget, 0)

        local still_coal_presses = 0
        local still_pressure_presses = 0
        local still_total = 0
        local still_iterations = 0
        while still_iterations < 40 and still_total < 26 do
            still_iterations = still_iterations + 1
            local settings_r, settings_v = t.var.server("regicide_still_settings")
            if settings_r == "ok" and settings_v ~= nil then
                if settings_v < 0 then
                    settings_v = settings_v + 4294967296
                end
                local tar_at_max = math.floor(settings_v / 2147483648) % 2 == 1
                local pressure_at_base = math.floor(settings_v / 67108864) % 2 == 1
                local flow_high =
                    (math.floor(settings_v / 1024) % 2 == 1)
                    or (math.floor(settings_v / 2048) % 2 == 1)
                    or (math.floor(settings_v / 4096) % 2 == 1)
                local heat_below_green =
                    (math.floor(settings_v / 8192) % 2 == 1)
                    or (math.floor(settings_v / 16384) % 2 == 1)
                    or (math.floor(settings_v / 32768) % 2 == 1)
                    or (math.floor(settings_v / 65536) % 2 == 1)
                    or (math.floor(settings_v / 131072) % 2 == 1)
                    or (math.floor(settings_v / 262144) % 2 == 1)
                if not tar_at_max then
                    t.ui.invoke(tar_up_widget, 0)
                end
                if tar_at_max and pressure_at_base and flow_high then
                    t.ui.invoke(pressure_up_widget, 0)
                    still_pressure_presses = still_pressure_presses + 1
                end
                if heat_below_green and still_coal_presses < coal_n then
                    t.ui.invoke(add_coal_widget, 0)
                    still_coal_presses = still_coal_presses + 1
                end
            end
            t.ticks(2)
            local total_r, total_v = t.var.server("regicide_still_total")
            if total_r == "ok" and total_v ~= nil then
                still_total = total_v
            end
        end
        t.step("still.minigame", still_total >= 26 and "PASS" or "FAIL",
            string.format("regicide_still_total=%s after %d poll(s), %d coal press(es), %d pressure press(es)",
                tostring(still_total), still_iterations, still_coal_presses, still_pressure_presses))
        t.shot("still.minigame-after")

        -- The close icon's own if_click DOES fire (measured: "ok" every
        -- time) but the interface never actually unmounts behind it -- the
        -- driver seam the queue row named survives the op=0 fix after all,
        -- for this one component. ESCAPE reaches the identical
        -- app->host.close_modal_requested flag DriveChat_CloseModal (the
        -- verb behind t.shop.close) sets (src/app/app_hotkeys.c:119), the
        -- same notify-the-server path a real player's Escape takes, so it
        -- is the fix, not a workaround.
        local still_close_result = t.key("escape")
        t.step("still.close", still_close_result == "ok" and "PASS" or "FAIL",
            "key(escape) -> " .. tostring(still_close_result))
        -- t.ui.await_close is the same hollow-on-success shape as
        -- await_open above -- graded by hand.
        local still_closed_result = t.ui.await_close("regicide_still")
        t.step("still.closed", still_closed_result == "ok" and "PASS" or "FAIL",
            "ui.await_close(regicide_still) -> " .. tostring(still_closed_result))
        t.expect("still.naphtha", t.inv.await("regicide_barrel_naphtha", 1, 10))

        -- ---- Naphtha + apple mash -> naphtha apple mix (mend1_poison.rs2:
        -- 135-152, an [opheldu] pair declared on both item names, so either
        -- order arms and lands). Named for quest-helper's own step
        -- (useNaphthaOnBarrel, "Use a barrel of naptha on the apple
        -- barrel" -- appleBarrel's own ItemID is MOURNING_APPLEBARREL_MUSH,
        -- matching mourning_applebarrel_mush here exactly). ----
        t.ui.tab("inventory")
        t.ticks(2)
        t.exec("useNaphthaOnBarrel", t.player.use_item_on_item,
            "regicide_barrel_naphtha", "mourning_applebarrel_mush")
        t.ticks(2)
        t.expect("useNaphthaOnBarrel.result", t.inv.await("mourning_applebarrel_naphtha_mush", 1, 10))

        -- ---- Sieve the mix -> toxic naphtha (mend1_poison.rs2:156-170,
        -- the same [opheldu]-pair shape). Named for quest-helper's own step
        -- (useSieveOnBarrel, "Use the sieve on the naphtha apple mix") --
        -- the row-name match is what makes this DRIVEN rather than the
        -- weak-word "soft-skipped ... shares appl,siev" CONTENT_GAP
        -- helper_coverage.py's own word-overlap heuristic would otherwise
        -- read here, since this leg genuinely is driven for real. ----
        t.ui.tab("inventory")
        t.ticks(2)
        t.exec("useSieveOnBarrel", t.player.use_item_on_item,
            "mourning_sieve", "mourning_applebarrel_naphtha_mush")
        t.ticks(2)
        t.expect("useSieveOnBarrel.result", t.inv.await("mourning_toxic_naphtha", 1, 10))

        -- ---- Cook the toxic naphtha on a range -- not a fire
        -- ([oploc1,carnilleanrange], mend1_poison.rs2:174-185).
        --
        -- RETRY after 486398e09: this row used to t.blocked here, reading
        -- carnilleanrange as unplaced from six SURFACE anchors around the
        -- Carnillean Mansion (2570,3270,0 +/- 60). That premise was false:
        -- carnilleanrange has exactly ONE placement in the whole loaded
        -- world, maps/m39_151.jl2:537 `0 42 35: 2859 10 1`, which decodes to
        -- ABS (2538,9699,0) -- the Carnillean kitchen room in the
        -- UNDERGROUND band (z+6400), alongside carnilleancrate (2545,9696),
        -- cookingshelves_search (2541,9699) and carnillean_ladder_up
        -- (2544,9694). The old anchor was 6,400 tiles away in z from the
        -- real room, so every one of the six surface sweeps was guaranteed
        -- not_found and proved nothing about the loc's placement. Proved
        -- live: build/quest_gate/seam6_range_probe/ledger.tsv, 13/13 PASS --
        -- loc_near(carnilleanrange,10) -> ok 2538,9699 L0 match=exact, then
        -- click_loc + inv.await(mourning_apple_toxin,2) -> ok. ----
        t.exec("goto-cookToxin", t.player.goto_tile, 2538, 9699, 0)
        local range_result, range_row = t.world.loc_near("carnilleanrange", 10)
        t.check("cookToxin.range_present", range_result == "ok",
            "loc_near(carnilleanrange, 10) -> " .. tostring(range_result) .. " "
                .. (range_result == "ok"
                    and (tostring(range_row.tile_x) .. "," .. tostring(range_row.tile_z)
                        .. " L" .. tostring(range_row.level) .. " match=" .. tostring(range_row.match))
                    or tostring(range_row)))
        t.drive.camera(SHOT_YAW, 383, SHOT_ZOOM_INDOOR)
        t.exec("cookToxin", t.player.click_loc, "carnilleanrange", 1)
        t.expect("cookToxin.result", t.inv.await("mourning_apple_toxin", 2, 10))
        t.expect("cookToxin.message",
            t.msg.expect("You carefully cook the toxic naphtha over the range"))

        -- ---- Poison food store 1, West Ardougne (mourning_sack_full1,
        -- maps/m39_51.jl2:4218-4221 -> abs 2517,3312 / 2517,3315 / 2521,3316;
        -- mend1_poison.rs2:197-221, an [oploc1] and an [oplocu] on the same
        -- base). ----
        t.exec("goto-poisonStore1", t.player.goto_tile, 2517, 3313, 0)
        local store1_result, store1_row = t.world.loc_near("mourning_sack_full1", 20)
        t.check("poisonStore1.present", store1_result == "ok",
            "loc_near(mourning_sack_full1, 20) -> " .. tostring(store1_result) .. " "
                .. (store1_result == "ok"
                    and (tostring(store1_row.tile_x) .. "," .. tostring(store1_row.tile_z)
                        .. " L" .. tostring(store1_row.level)) or tostring(store1_row)))
        local store1_target, store1_target_r = t.player.by_symbol("loc", "mourning_sack_full1")
        t.step("poisonStore1.target", store1_target_r == "ok" and "PASS" or "FAIL",
            "by_symbol(loc, mourning_sack_full1) -> " .. tostring(store1_target_r) .. " id="
                .. tostring(store1_target and store1_target.id) .. " match="
                .. tostring(store1_target and store1_target.match))
        t.exec("poisonStore1", t.player.use_on, "mourning_apple_toxin", store1_target)
        t.ticks(2)
        t.expect("poisonStore1.message",
            t.msg.expect("You dust the grain sacks with toxic powder"))
        t.expect("poisonStore1.var", t.var.await_server("mourning_food_poison1", 1, 6))

        -- ---- Poison food store 2, the church stores (mourning_sack_full2,
        -- maps/m39_51.jl2:4222-4224 -> abs 2524,3285 / 2524,3288 / 2525,3288;
        -- mend1_poison.rs2:223-247). ----
        t.exec("goto-poisonStore2", t.player.goto_tile, 2524, 3286, 0)
        local store2_result, store2_row = t.world.loc_near("mourning_sack_full2", 20)
        t.check("poisonStore2.present", store2_result == "ok",
            "loc_near(mourning_sack_full2, 20) -> " .. tostring(store2_result) .. " "
                .. (store2_result == "ok"
                    and (tostring(store2_row.tile_x) .. "," .. tostring(store2_row.tile_z)
                        .. " L" .. tostring(store2_row.level)) or tostring(store2_row)))
        local store2_target, store2_target_r = t.player.by_symbol("loc", "mourning_sack_full2")
        t.step("poisonStore2.target", store2_target_r == "ok" and "PASS" or "FAIL",
            "by_symbol(loc, mourning_sack_full2) -> " .. tostring(store2_target_r) .. " id="
                .. tostring(store2_target and store2_target.id) .. " match="
                .. tostring(store2_target and store2_target.match))
        t.exec("poisonStore2", t.player.use_on, "mourning_apple_toxin", store2_target)
        t.ticks(2)
        t.expect("poisonStore2.message",
            t.msg.expect("You dust the sacks in the church stores with toxic powder"))
        t.expect("poisonStore2.var", t.var.await_server("mourning_food_poison2", 1, 6))

        -- ---- Report to Essyllt (mend1_essyllt_after_poison,
        -- mend1_disguise.rs2:207-211 -> %mourning_quest = ^mend1_report). ----
        t.exec("goto-talkToEssylltAfterPoison", t.player.goto_tile, 2043, 4631, 0)
        t.exec("talkToEssylltAfterPoison", t.player.talk_to, "mourner_hideout_head_mourner", 1)
        t.exec("talkToEssylltAfterPoison-dialog", t.chat.play, {
            "player:It's done. Both food stores are poisoned -- nobody suspects a thing.",
            "npc:Then you truly are one of us now. Listen closely, recruit -- the mourners you see in this city are not what they seem. We are elves, servants of Lord Iorwerth, and this quarantine is a lie.",
            "npc:There is more happening beneath West Ardougne than even I fully understand. You've proven yourself -- take word of what you've learned back to whoever sent you.",
        })
        t.ticks(2)
        -- configs/all.varp's [mourning_quest] body is empty (no transmit=yes,
        -- see the bind banner at the top of this file), so every stage read
        -- below goes through the SERVER copy (t.var.server) or the journal,
        -- never t.quest.expect_stage, which would read a stale client 0.
        local report_r, report_v = t.var.server("mourning_quest")
        t.check("talkToEssylltAfterPoison.stage", report_r == "ok" and report_v == 8,
            "var.server(mourning_quest) -> " .. tostring(report_r) .. " " .. tostring(report_v)
                .. " expected 8 (^mend1_report)")
        -- Same retry ladder every other journal read in this file uses: the
        -- FIRST journal_open right after a dialogue closes is a UI-mount race,
        -- not a content answer.
        local journal_report_r, journal_report
        for _ = 1, 3 do
            journal_report_r, journal_report = t.ui.journal_open("Mourning's End Part I")
            if journal_report_r == "ok" then
                break
            end
            t.ui.journal_close()
            t.ticks(5)
        end
        local journal_report_line = journal_report and journal_report.first_line
        t.check("talkToEssylltAfterPoison.journal",
            journal_report_r == "ok" and journal_report_line ~= nil
                and journal_report_line:find("Essyllt revealed the truth", 1, true) ~= nil,
            "journal_open(Mourning's End Part I) -> " .. tostring(journal_report_r)
                .. " first_line=" .. tostring(journal_report_line))
        t.ui.journal_close()

        -- ---- Arianwyn, Lletya -- the hand-in (mend1_shared.rs2:101-108,
        -- ~mend1_quest_complete). skill.snapshot() before the hand-in for
        -- the two documented xp rewards (mend1_shared.rs2:31:
        -- "40000 Thieving XP|25000 Hitpoints XP|Elf teleport crystal|Access
        -- to the Mourner HQ basement and Lletya"). ----
        local qp_before_r, qp_before = t.var.varp("qp")
        local skill_snapshot_r, skill_snapshot = t.skill.snapshot()
        t.step("reward.snapshot", skill_snapshot_r == "ok" and "PASS" or "FAIL",
            "skill.snapshot() -> " .. tostring(skill_snapshot_r))
        t.exec("goto-talkToArianwynFinal", t.player.goto_tile, 2353, 3172, 0)
        t.exec("talkToArianwynFinal", t.player.talk_to, "mourning_arianwyn", 1)
        t.exec("talkToArianwynFinal-dialog", t.chat.play, {
            "player:Arianwyn -- the mourners really are Iorwerth elves. I tortured their gnome for the truth about a signalling device, dyed sheep to test it, and poisoned their food stores to prove myself to Essyllt.",
            "npc:You have done more than I asked. This confirms everything we feared -- and gives us a foothold inside their operation.",
            "npc:Thank you. Take this teleport crystal -- it will bring you straight back to Lletya whenever you need it.",
        })
        t.ticks(3)

        -- Completion is graded the same way makinghistory.lua/rovingelves.lua
        -- grade theirs when the quest's own varp is not transmitted: the
        -- server varp, the reward scroll, the quest-point delta and the
        -- journal, each written by hand instead of t.quest.expect_complete().
        local complete_r, complete_v = t.var.server("mourning_quest")
        t.check("quest.varp_complete", complete_r == "ok" and complete_v == 9,
            "var.server(mourning_quest) -> " .. tostring(complete_r) .. " " .. tostring(complete_v)
                .. " expected 9 (^mend1_complete)")

        local scroll_title_result, scroll_title = t.scroll.title()
        local scroll_shot_result, scroll_shot_detail = t.shot("quest.scroll")
        local scroll_shot_note = ""
        if scroll_shot_result == "ok" and type(scroll_shot_detail) == "string"
            and string.find(scroll_shot_detail, "unchanged", 1, true) then
            scroll_shot_note = " [scroll already photographed: " .. scroll_shot_detail .. "]"
        end
        local title_name = scroll_title ~= nil and scroll_title.name or nil
        t.step("quest.scroll_title",
            (scroll_title_result == "ok" and type(title_name) == "string"
                and title_name:find("Mourning's End Part I", 1, true) ~= nil) and "PASS" or "FAIL",
            "scroll.title() after the hand-in -> " .. tostring(scroll_title_result) .. " name="
                .. tostring(title_name) .. " points="
                .. tostring(scroll_title and scroll_title.points) .. scroll_shot_note)
        t.scroll.close()

        local qp_after_r, qp_after = t.var.varp("qp")
        t.check("quest.points", qp_after_r == "ok" and qp_before_r == "ok"
            and qp_after == qp_before + 2,
            "qp (varp) " .. tostring(qp_before) .. " -> " .. tostring(qp_after)
                .. " expected delta 2")

        local final_journal_r, final_journal
        for _ = 1, 3 do
            final_journal_r, final_journal = t.ui.journal_open("Mourning's End Part I")
            if final_journal_r == "ok" then
                break
            end
            t.ui.journal_close()
            t.ticks(5)
        end
        t.check("quest.journal", final_journal_r == "ok" and final_journal ~= nil
            and final_journal.complete == true,
            "journal_open(Mourning's End Part I) -> " .. tostring(final_journal_r)
                .. " complete=" .. tostring(final_journal and final_journal.complete)
                .. " lines=" .. tostring(final_journal and final_journal.line_count))
        t.ui.journal_close()

        -- ---- Reward rows -- every reward mend1_shared.rs2:31's own
        -- ~quest_complete_rewards call lists, each asserted literally:
        -- "40000 Thieving XP|25000 Hitpoints XP|Elf teleport crystal|
        -- Access to the Mourner HQ basement and Lletya" (mend1_shared.rs2:
        -- 25-31, stat_advance(thieving, ^mend1_reward_thieving_xp=400000)
        -- + stat_advance(hitpoints, ^mend1_reward_hitpoints_xp=250000),
        -- the *10 tenths unit skill.expect_gain resolves against the
        -- documented 40000/25000 whole-xp figures itself). The fourth
        -- listed reward, HQ basement + Lletya access, is a gate this file
        -- already walked through repeatedly above (talkToEssyllt*,
        -- talkToArianwyn*), not a grantable state to assert here. ----
        t.exec("reward.thieving_xp", t.skill.expect_gain, "thieving", 40000, skill_snapshot)
        t.exec("reward.hitpoints_xp", t.skill.expect_gain, "hitpoints", 25000, skill_snapshot)
        t.exec("reward.teleport_crystal", t.inv.expect_has, "mourning_teleport_crystal_4", 1)

        t.finish(0)
        return
    end,
}
