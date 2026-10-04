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
--
-- RE-AUTHOR after OSRS-Content 70487ebee8 (on top of parity1m's 565711aa90):
-- two content-parity passes landed since this file was last green, and
-- almost every dialogue below went stale against them.
--   parity1m (565711aa90): the quest now STARTS WITH ELUNED, not Islwyn --
--       her cache dbrow startnpc (1116 = roving_female_woodelf) matches the
--       wiki's own |start=; Islwyn (rovingelves_islwyn.rs2's own hook) now
--       only ever answers "Ahh... good to see you back. Are you after more
--       crystal equipment?" once Roving Elves is complete, and starts
--       nothing. Arianwyn's briefing (mend1_arianwyn_briefing) is now the
--       FULL wiki transcript with a story/skip-backstory branch (driven
--       here via the shorter "Let's skip the backstory" choice). The caged
--       gnome (mend1_gnome.rs2) was rebuilt onto two entities -- the rack
--       (`mourning_gnome_rack`, states 0..6) and, once released, a second
--       walking npc (`mourner_hideout_gnome`/`_head`, states 7..9) -- with
--       new verbatim dialogue at every beat; critically the feather/toad
--       crunchies are NOT consumed any more (Tarnished key page: "can be
--       freely disposed of after the quest"), and holding leather+logs+
--       device+crunchies all at once collapses tickle->talk_again->release->
--       give_items into two clicks (feather use-on, then Release op2).
--   parity1o (70487ebee8): Tegid's soap is now gated behind actually
--       TALKING to him first (`%mourning_tegid_chat`, eadgar_druid_washing.rs2
--       calls `~mend1_tegid_talk`); Oronwen's dialogue is the real
--       has_all/owed-item branch tree; Essyllt's "New recruit" now reads and
--       TAKES the letter of recommendation (`mourning_mourner_message`,
--       inv_del'd) before the long Death-Guard/sheep-dye briefing, and the
--       mourner-HQ door (`doors.rs2`'s `west_ardougne_mourner_headquarters_
--       doors`) no longer prints any message at all on a disguised
--       walk-through -- the letter is Essyllt's to read, not the guard's;
--       Essyllt's "New orders"/poison-task briefing is one continuous
--       dialogue naming three (not two) food supply points; Elena's "An old
--       friend" visit is read in the FULL-DISGUISE branch (this file never
--       takes the mourner gear off before visiting her) and the barrel-fill
--       message changed ("You scoop up a barrel full of the rotten apples.");
--       the toxic naphtha now cooks on ANY range (driven here at the Mourner
--       HQ's own range, 2547,3322 -- `skill_cooking/scripts/cooking.rs2`'s
--       `[oplocu,_cooking_oven]` hook), not Carnillean's; and Essyllt's
--       "The mourners' plan" / Arianwyn's final report are the full verbatim
--       Temple-of-Light closes. A third food store (`mourning_sack_full3`)
--       exists but has no Quest Helper `ObjectStep` at all (mend1.constant's
--       own header: "deferred, not a missed critical-path branch") -- left
--       undriven, same as the previous author's revision.
--
-- RE-DRIVE b59 (door rule, docs/QUEST_ORCHESTRATOR.md 2026-10-03): no goto_tile
-- into or out of a closed space. Every crossing is pressed on foot, going in
-- and coming out, on every visit: the members' gate west of Falador
-- (membergatel 2934,3320: the only on-foot link between Lumbridge/Rimmington
-- and Kandarin; out of Lumbridge, and both ways to Rimmington's still), the
-- Arandar pass's Huge Gate (Tirannwn is
-- sealed from Kandarin but for it: regicide_arandar_gate_guard.rs2), Lletya's
-- elf_village_treegate trees (2305,3191; Pass both ways,
-- quest_mourningsendparti/scripts/mend1_lletya_access.rs2, OSRS-Content
-- 3e8fbb4d30), the West Ardougne city wall door ardougnedoor_l/_r
-- (2557-2558,3299; the Biohazard walk-through, area_ardougne_west/scripts/
-- doors.rs2, same commit), the Mourner HQ's front door, its trapdoor room's
-- poordoor, the trapdoor down and the basement ladder mourner_hideout_ladder1
-- back up (it climbs to the trapdoor room, 2542,3326: mend1_hideout_ladder.rs2,
-- same commit), the basement's two room doors, Elena's door, the orchard
-- gate, and the food-store hall's double doors and store room door. No
-- teleport is used: the round-1 revision's four Camelot Teleports (out of
-- Lletya and three times out of the basement) stood in for the trees and the
-- ladder while those had no handler, and are gone with the magic level and
-- runes they were staged for.
-- The tar swamp at 2223,3123 and the Tyras-camp barrels lie behind the dense
-- forest (Agility 56), so the second barrel is the orchard's own respawn
-- (200 ticks) and the tar is the 2263,3127 copy, both in the open.

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
        "::setvar varp328_regicide_quest ^regicide_complete",
        "::complete quest_rovingelves",
        -- Big Chompy Bird Hunting has no ::complete arm in quest_cheat.rs2
        -- either (grep-confirmed) -- staged with the ladder cheat, same as
        -- Regicide above.
        "::setvar varp293_chompybird ^chompybird_complete",
        "::complete quest_sheepherder",
        -- Biohazard is the next link of the same chain (Roving Elves ->
        -- Regicide -> Underground Pass -> Biohazard); West Ardougne's city
        -- doors open on it (area_ardougne_west/scripts/doors.rs2:36-49).
        "::complete quest_biohazard",
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

        local function free_slots()
            local free = 0
            for i = 0, 27 do
                local sr, slot = t.inv.slot(i)
                if sr ~= "ok" then
                    return sr, nil
                end
                if slot.count == 0 then
                    free = free + 1
                end
            end
            return "ok", free
        end

        local function tile_text(r, w)
            if r ~= "ok" or type(w) ~= "table" then
                return tostring(r) .. " " .. tostring(w)
            end
            return w.x .. "," .. w.z .. "," .. w.level
        end

        -- Cross one door on foot (the b56 pass_door pattern). Walk to the near
        -- side and check the tile; press the CLOSED leaf on the exact door
        -- tile on the player's own level, or -- when an earlier press left it
        -- open -- assert the open leaf stands within open_reach of the door
        -- tile; walk through; check the far tile.
        local function pass_door(prefix, closed_sym, open_sym, door_x, door_z, near_x, near_z, far_x, far_z, far_ok, far_desc, open_reach)
            open_reach = open_reach or 1
            t.player.walk_to(near_x, near_z, 30)
            local nr, nt = t.world.tile()
            t.check(prefix .. ".atDoor", nr == "ok" and math.abs(nt.x - near_x) <= 1 and math.abs(nt.z - near_z) <= 1,
                "walked to " .. near_x .. "," .. near_z .. " beside " .. closed_sym .. " at " .. door_x .. "," .. door_z
                    .. " -> " .. tile_text(nr, nt))
            local here = (nr == "ok") and nt.level or 0
            local cr, cd = t.world.loc_near(closed_sym, 3)
            local press = "not pressed: it stands open"
            if cr == "ok" and cd.tile_x == door_x and cd.tile_z == door_z and cd.level == here then
                -- A door that swings the player's way can answer `timeout
                -- settle_after_click` on a press that opened it (the Elena
                -- exit did, scratch run mesp_tail1 row 194): the press's
                -- answer goes in the .through detail, and the far tile -- a
                -- walk no closed door lets through -- grades it.
                local pr, pd = t.player.click_loc(closed_sym, 1, { at = { door_x, door_z } })
                press = "pressed " .. closed_sym .. " at " .. door_x .. "," .. door_z .. " -> " .. tostring(pr) .. " " .. tostring(pd)
                t.ticks(1)
            else
                local orr, od = t.world.loc_near(open_sym, 3)
                t.check(prefix .. ".standsOpen", orr == "ok" and od.level == here
                        and math.abs(od.tile_x - door_x) <= open_reach and math.abs(od.tile_z - door_z) <= open_reach,
                    closed_sym .. " at " .. door_x .. "," .. door_z .. ": "
                        .. (cr == "ok" and ("nearest closed copy at " .. cd.tile_x .. "," .. cd.tile_z .. "," .. cd.level) or tostring(cr))
                        .. "; " .. open_sym .. ": "
                        .. (orr == "ok" and ("open leaf at " .. od.tile_x .. "," .. od.tile_z .. "," .. od.level) or tostring(orr))
                        .. " (an earlier press left it open: walked through, not pressed again)")
            end
            t.player.walk_to(far_x, far_z, 30)
            local fr, ft = t.world.tile()
            t.check(prefix .. ".through", fr == "ok" and far_ok(ft),
                press .. "; walked through to " .. far_x .. "," .. far_z .. " -> " .. tile_text(fr, ft) .. " (want " .. far_desc .. ")")
        end

        -- The Arandar pass's Huge Gate (overpass_gate_right 2386,3334;
        -- maplink.dbrow 0_37_52_18_5 <-> 0_37_52_18_7 = 2386,3333 <-> 2386,3335)
        -- is Tirannwn's only way in from Kandarin on foot (the Isafdar flood
        -- from 2386,3333 is 7,176 tiles and closed). regicide_arandar_gate_guard.rs2
        -- opens it once Regicide is past ^regicide_killed_tyras and nudges the
        -- player; probe2 measured the nudge landing part-way (2385,3335 after a
        -- southward press) with the gate standing open, so the row walks on to
        -- the far tile and is graded on the tiles before and after.
        local function pass_arandar(prefix, southward)
            local near_z = southward and 3335 or 3333
            local far_z = southward and 3331 or 3337
            t.player.walk_to(2386, near_z, 30)
            local nr, nt = t.world.tile()
            t.check(prefix .. ".atGate", nr == "ok" and nt.level == 0 and math.abs(nt.x - 2386) <= 1 and nt.z == near_z,
                "walked to 2386," .. near_z .. " beside overpass_gate_right 2386,3334 -> " .. tile_text(nr, nt))
            local cr, cd = t.player.click_loc("overpass_gate_right", 1, { at = { 2386, 3334 } })
            t.ticks(1)
            t.player.walk_to(2386, far_z, 20)
            local fr, ft = t.world.tile()
            local crossed = fr == "ok" and ft.level == 0 and math.abs(ft.x - 2386) <= 2
                and ((southward and ft.z <= 3333) or ((not southward) and ft.z >= 3335))
            t.check(prefix, crossed,
                "from " .. tile_text(nr, nt) .. ": click_loc(overpass_gate_right) -> " .. tostring(cr) .. " " .. tostring(cd)
                    .. "; walked on to 2386," .. far_z .. " -> " .. tile_text(fr, ft)
                    .. (southward and " (want z <= 3333, inside the pass)" or " (want z >= 3335, Kandarin side)"))
        end

        -- The members' gate west of Falador (membergatel 2934,3320 with
        -- membergater 2933,3320, south-edge leaves). With every door shut,
        -- Kandarin's component (132,142 tiles from the gate tile, box
        -- x 2250-3330 z 2950-3800) holds the gate tile, the Arandar pass,
        -- Taverley, the Feldip toads and East Ardougne, while Lumbridge and
        -- Rimmington's still lie south of it (reach.py 2934,3321 -> 2934,3318:
        -- NEEDS-DOOR via membergatel at margins 30, 80 and 160): on foot it is
        -- the only way between the two, so it is clicked on every crossing
        -- (sampler-findings.md, "Sample matthew-mbp-m4-b59" (a)).
        -- gates.rs2's [label,member_fencegate_try] walks the player through
        -- (onto the gate tile 2934,3320 from the south; from the north onto
        -- the gate tile, then one south to 2934,3319) and leaves no opened
        -- loc, so it is graded on the tiles before and after.
        local function member_gate(prefix, northward)
            local near_z = northward and 3318 or 3321
            t.player.walk_to(2934, near_z, 30)
            local nr, nt = t.world.tile()
            t.check(prefix .. ".atGate", nr == "ok" and nt.level == 0 and math.abs(nt.x - 2934) <= 1 and nt.z == near_z,
                "walked to 2934," .. near_z .. " beside membergatel 2934,3320 -> " .. tile_text(nr, nt))
            local function across(w)
                return w.level == 0 and math.abs(w.x - 2934) <= 2
                    and ((northward and w.z >= 3320) or ((not northward) and w.z <= 3319))
            end
            local cr, cd = t.player.click_loc("membergatel", 1, { at = { 2934, 3320 } })
            local ar, ad = t.await({ level = function()
                local r, w = t.world.tile()
                return r == "ok" and across(w)
            end, note = prefix .. ".across" }, 10)
            local fr, ft = t.world.tile()
            t.check(prefix, nr == "ok" and nt.z == near_z and fr == "ok" and across(ft),
                "from " .. tile_text(nr, nt) .. ": click_loc(membergatel 2934,3320) -> " .. tostring(cr) .. " " .. tostring(cd)
                    .. "; await -> " .. tostring(ar) .. " " .. tostring(ad) .. "; at " .. tile_text(fr, ft)
                    .. (northward and " (want z >= 3320, the Kandarin side)" or " (want z <= 3319, the Falador side)"))
        end

        -- Lletya (x 2306-2358 z 3148-3197) is walled in; its only walkable
        -- gaps are the elf_village_treegate trees (2305,3191 and 2305,3195,
        -- maps/m36_49.jl2, op1 Pass). mend1_lletya_access.rs2's
        -- [oploc1,elf_village_treegate] opens the tree and forcemoves the
        -- player two tiles across on the row they stand on (both tree states
        -- block, so only the press carries anyone through). Graded on the
        -- tiles before and after; the press's answer goes in the detail.
        local function lletya_gate(prefix, inward)
            local near_x = inward and 2304 or 2306
            local far_x = inward and 2306 or 2304
            t.player.walk_to(near_x, 3193, 40)
            local nr, nt = t.world.tile()
            t.check(prefix .. ".atGate", nr == "ok" and nt.level == 0 and nt.x == near_x and math.abs(nt.z - 3193) <= 1,
                "walked to " .. near_x .. ",3193 beside the tree at 2305,3191 -> " .. tile_text(nr, nt))
            local cr, cd = t.player.click_loc("elf_village_treegate", 1, { at = { 2305, 3191 } })
            t.ticks(3)
            local mr, mt = t.world.tile()
            t.player.walk_to(far_x, 3193, 10)
            local fr, ft = t.world.tile()
            local crossed = nr == "ok" and nt.x == near_x and fr == "ok" and ft.level == 0
                and ((inward and ft.x >= 2306) or ((not inward) and ft.x <= 2304))
            t.check(prefix, crossed, "from " .. tile_text(nr, nt) .. ": click_loc(elf_village_treegate 2305,3191) -> "
                .. tostring(cr) .. " " .. tostring(cd) .. "; 3 ticks later at " .. tile_text(mr, mt)
                .. "; walked to " .. far_x .. ",3193 -> " .. tile_text(fr, ft)
                .. (inward and " (want x >= 2306, inside Lletya)" or " (want x <= 2304, Isafdar)"))
            return crossed
        end

        -- West Ardougne is walled in (4,528 tiles from 2551,3320, closed);
        -- the city doors are ardougnedoor_l/_r, a 2x2 block at 2557-2558,
        -- 3299-3300 (maps/m39_51.jl2). doors.rs2's
        -- [label,west_ardougne_open_city_doors] (Biohazard complete) prints
        -- "You pull on the large wooden doors..." and forcemoves the player
        -- two tiles across; the doors themselves never change, so the leaf
        -- on the player's side is pressed on every crossing. Graded on the
        -- tiles before and after.
        local function ardougne_wall(prefix, westward)
            local near_x = westward and 2559 or 2556
            local far_x = westward and 2555 or 2560
            local door_x = westward and 2558 or 2557
            local door_sym = westward and "ardougnedoor_l" or "ardougnedoor_r"
            t.player.walk_to(near_x, 3299, 40)
            local nr, nt = t.world.tile()
            t.check(prefix .. ".atDoor", nr == "ok" and nt.level == 0 and nt.x == near_x and math.abs(nt.z - 3299) <= 1,
                "walked to " .. near_x .. ",3299 beside " .. door_sym .. " " .. door_x .. ",3299 -> " .. tile_text(nr, nt))
            local cr, cd = t.player.click_loc(door_sym, 1, { at = { door_x, 3299 } })
            t.ticks(3)
            local mr, mt = t.world.tile()
            t.player.walk_to(far_x, 3299, 10)
            local fr, ft = t.world.tile()
            local crossed = nr == "ok" and nt.x == near_x and fr == "ok" and ft.level == 0
                and ((westward and ft.x <= 2556) or ((not westward) and ft.x >= 2559))
            t.check(prefix, crossed, "from " .. tile_text(nr, nt) .. ": click_loc(" .. door_sym .. " " .. door_x .. ",3299) -> "
                .. tostring(cr) .. " " .. tostring(cd) .. "; 3 ticks later at " .. tile_text(mr, mt)
                .. "; walked to " .. far_x .. ",3299 -> " .. tile_text(fr, ft)
                .. (westward and " (want x <= 2556, West Ardougne)" or " (want x >= 2559, East Ardougne)"))
            return crossed
        end

        -- The Mourner HQ, West Ardougne: the front door mournerstewdoor
        -- (2551,3320, north edge; doors.rs2:19-24 walks a disguised player
        -- through), the trapdoor room's poordoor (2546,3325, east edge), and
        -- mourning_hideout_trap_door (2542,3327; mend1_disguise.rs2:346-353
        -- p_teleports to the basement, 2044,4628). From the street at 2551,3320.
        local function hq_down(door_row, trap_row)
            local br, bt = t.world.tile()
            t.exec(door_row, t.player.click_loc, "mournerstewdoor", 1, { at = { 2551, 3320 } })
            t.ticks(4)
            local ar, at = t.world.tile()
            t.check(door_row .. ".walked_through", br == "ok" and ar == "ok" and at.z >= 3321 and at.x >= 2547 and at.x <= 2555,
                "tile before=" .. tile_text(br, bt) .. " after=" .. tile_text(ar, at) .. " (want inside the HQ front room, z >= 3321)")
            pass_door(trap_row .. ".poordoor", "poordoor", "poordooropen", 2546, 3325, 2547, 3325, 2545, 3326,
                function(w) return w.x <= 2546 and w.z >= 3324 and w.z <= 3327 end, "inside the trapdoor room, x <= 2546")
            t.drive.camera(SHOT_YAW_BASEMENT, 383, SHOT_ZOOM_INDOOR)
            t.exec(trap_row, t.player.click_loc, "mourning_hideout_trap_door", 1, { at = { 2542, 3327 } })
            local landed_r, landed_detail = t.await({
                level = function()
                    local tile_result, tile = t.world.tile()
                    return tile_result == "ok" and tile and tile.x and tile.z
                        and math.abs(tile.x - 2044) <= 8 and math.abs(tile.z - 4628) <= 8
                end,
                note = "trapdoor.landed_basement",
            }, 10)
            t.step(trap_row .. ".landed", landed_r == "ok" and "PASS" or "FAIL",
                "await(world.tile() within 8 of mend1_hq_basement_coord 2044,4628) after the trap door -> "
                    .. tostring(landed_r) .. " " .. tostring(landed_detail))
            t.ticks(3)
        end

        -- Out of the basement on foot: from Essyllt's room (door2, 2042,4633)
        -- or the gnome's (door3, 2037,4633) onto the corridor, north to the
        -- ladder's approach tile, and up mourner_hideout_ladder1 (2044,4650):
        -- mend1_hideout_ladder.rs2 climbs to the trapdoor room, 2542,3326,0.
        -- Then out of the HQ by the poordoor (2546,3325, the room's east
        -- edge) and the front door (mournerstewdoor 2551,3320) to the street.
        local function hq_up(prefix)
            local br, bt = t.world.tile()
            t.drive.camera(SHOT_YAW_BASEMENT, 383, SHOT_ZOOM_INDOOR)
            if br == "ok" and bt.x <= 2038 then
                pass_door(prefix .. ".gnomeDoor", "mourner_hideout_door3", "mourner_hideout_door3_open", 2037, 4633, 2037, 4633, 2037, 4635,
                    function(w) return w.z >= 4634 end, "in the corridor, z >= 4634")
            else
                pass_door(prefix .. ".essylltDoor", "mourner_hideout_door2", "mourner_hideout_door2_open", 2042, 4633, 2042, 4633, 2042, 4635,
                    function(w) return w.z >= 4634 end, "in the corridor, z >= 4634")
            end
            t.player.walk_to(2044, 4649, 40)
            local lr, lt = t.world.tile()
            t.check(prefix .. ".atLadder", lr == "ok" and lt.x == 2044 and lt.z == 4649 and lt.level == 0,
                "walked to 2044,4649 below mourner_hideout_ladder1 (2044,4650) -> " .. tile_text(lr, lt))
            local cr, cd = t.player.click_loc("mourner_hideout_ladder1", 1, { at = { 2044, 4650 } })
            local ar, ad = t.await({ level = function()
                local r, w = t.world.tile()
                return r == "ok" and w.x == 2542 and w.z == 3326 and w.level == 0
            end, note = prefix .. ".landing" }, 10)
            local wr, wt = t.world.tile()
            t.check(prefix, lr == "ok" and lt.z == 4649 and wr == "ok" and wt.x == 2542 and wt.z == 3326 and wt.level == 0,
                "from " .. tile_text(lr, lt) .. ": click_loc(mourner_hideout_ladder1) -> " .. tostring(cr) .. " " .. tostring(cd)
                    .. "; await landing -> " .. tostring(ar) .. " " .. tostring(ad) .. "; at " .. tile_text(wr, wt)
                    .. " (want 2542,3326,0: the HQ trapdoor room)")
            t.drive.camera(SHOT_YAW, 383, SHOT_ZOOM_INDOOR)
            pass_door(prefix .. ".poordoor", "poordoor", "poordooropen", 2546, 3325, 2546, 3325, 2548, 3325,
                function(w) return w.x >= 2547 and w.z >= 3321 and w.z <= 3327 end, "in the HQ front room, x >= 2547")
            t.player.walk_to(2551, 3321, 10)
            local hr, ht = t.world.tile()
            local pr, pd = t.player.click_loc("mournerstewdoor", 1, { at = { 2551, 3320 } })
            t.ticks(4)
            local sr, st = t.world.tile()
            t.check(prefix .. ".frontDoor", hr == "ok" and ht.z >= 3321 and sr == "ok" and st.z <= 3320 and st.level == 0,
                "from " .. tile_text(hr, ht) .. ": click_loc(mournerstewdoor 2551,3320) -> " .. tostring(pr) .. " " .. tostring(pd)
                    .. "; at " .. tile_text(sr, st) .. " (want the street, z <= 3320)")
        end

        do
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp517_mourning_quest",
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
            and journal0.first_line:find("I should talk to Eluned in Isafdar", 1, true) ~= nil,
            "journal_open(Mourning's End Part I) -> " .. tostring(journal0_r) .. " first_line="
                .. tostring(journal0 and journal0.first_line))
        t.ui.journal_close()

        end
        do
        -- ---- Eluned, Isafdar (2289,3145,0 --
        -- areas/world/configs/m35_49.spawn:79, spawned as base symbol
        -- roving_female_woodelf). RE-AUTHOR (this file's header, parity1m):
        -- the quest now starts here, not at Islwyn -- the cache dbrow's own
        -- startnpc (1116) resolves to this symbol, and Islwyn's own hook
        -- (rovingelves_islwyn.rs2) no longer starts anything once Roving
        -- Elves is complete ("Ahh... good to see you back. Are you after
        -- more crystal equipment?"). rovingelves_eluned.rs2's own
        -- [opnpc1,roving_female_woodelf] calls ~mend1_eluned_start once
        -- %rovingelves_quest is complete and %mourning_quest < ^mend1_briefed. ----
        -- OBSOLETE: talkToIslwyn the live game starts Mourning's End Part I at Eluned (https://oldschool.runescape.wiki/w/Mourning%27s_End_Part_I?oldid=15292327#Starting_the_quest: "Start point: Talk to Eluned inside the elven woods of Isafdar"); quest-helper's Islwyn start predates Song of the Elves' 2019 rework, and Islwyn's hook starts nothing (quests/quest_mourningsendparti/scripts/mend1_shared.rs2:103-105) -- driven below as talkToEluned.
        -- From the Lumbridge fixture overland to the members' gate west of
        -- Falador (first goto of the run; reach.py REACH 387, every door
        -- shut), through it, overland to Kandarin's side of the Arandar pass,
        -- the Huge Gate on foot, then Isafdar's open forest to Eluned
        -- (2289,3145 is in the 7,176-tile component the gate opens onto).
        t.exec("goto-memberGate.eluned", t.player.goto_tile, 2934, 3318, 0)
        member_gate("memberGateIn.eluned", true)
        t.exec("goto-arandarNorth", t.player.goto_tile, 2386, 3337, 0)
        pass_arandar("arandarIn.eluned", true)
        t.exec("goto-talkToEluned", t.player.goto_tile, 2289, 3145, 0)
        t.exec("talkToEluned", t.player.talk_to, "roving_female_woodelf", 1)
        -- mend1_eluned_start (mend1_shared.rs2:59-92): meets_requirements is
        -- true (ranged>=60, thieving>=50, Roving Elves/Chompy/Sheep Herder
        -- all complete from setup), so this opens with the escort offer.
        t.exec("talkToEluned-dialog", t.chat.play, {
            "npc:Arianwyn, our leader in Lletya, has asked to meet you. I can take you to him now if you like.",
            "choose:Yes, I should go see him now.",
            "player:Yes, I should go see him now.",
        })
        -- The teleport + mesbox + crystal objbox + three more chathead pages
        -- all run in the SAME script call, past the p_choice2's own
        -- resolve; "*" takes every continuable page (mesbox/objbox) without
        -- checking its text (docs section 3's "* (any one continuable
        -- page)"), so one list carries the whole exchange through to
        -- %mourning_quest = ^mend1_briefed.
        t.exec("talkToEluned-dialog2", t.chat.play, {
            "*", -- mesbox: "Eluned takes you to Lletya."
            "*", -- objbox: "Eluned hands you a tiny crystal seed."
            "npc:Here you go... If you ever need to get back to Lletya, you can use this.",
            "npc:Use it sparingly as it only has a few uses. If you need, I can re-enchant it, but I am not as talented as the singer who originally made this one.",
            "player:Thanks a lot Eluned!",
            "npc:Now, I had better get going or Islwyn will get worried.",
        })
        t.chat.close()
        local crystal_r, crystal_n = t.inv.count("mourning_teleport_crystal_4")
        t.check("talkToEluned.crystal", crystal_r == "ok" and crystal_n == 1,
            "mourning_teleport_crystal_4=" .. tostring(crystal_n) .. " (" .. tostring(crystal_r) .. ")")
        -- The script's own %mourning_quest write happens once the last
        -- page's dismiss lands server-side; the CLIENT'S mirror never
        -- carries this varp at all (see the banner above the quest.bind
        -- step) -- read server-side via the journal.
        t.ticks(3)
        local journal_briefed_r, journal_briefed = t.ui.journal_open("Mourning's End Part I")
        t.check("quest.stage.briefed", journal_briefed_r == "ok" and journal_briefed ~= nil
            and journal_briefed.first_line ~= nil
            and journal_briefed.first_line:find("Eluned took me to Lletya, where Arianwyn has a task for me", 1, true) ~= nil,
            "journal_open(Mourning's End Part I) -> " .. tostring(journal_briefed_r) .. " first_line="
                .. tostring(journal_briefed and journal_briefed.first_line))
        t.ui.journal_close()

        end
        do
        -- ---- Arianwyn, Lletya (2353,3172,0 -- m36_49.spawn:37, base symbol
        -- mourning_arianwyn). RE-AUTHOR (parity1m): mend1_arianwyn_briefing
        -- is now the full wiki transcript, verbatim, with a story/skip
        -- choice -- driven here via the shorter "Let's skip the backstory."
        -- branch; both branches converge on the same tail. ----
        -- Eluned's own p_teleport (mend1_shared.rs2:85) set the player down
        -- inside Lletya at 2352,3170, two tiles from Arianwyn: no goto.
        local lletya_r, lletya_tile = t.world.tile()
        t.check("talkToEluned.inLletya", lletya_r == "ok" and lletya_tile.level == 0
                and lletya_tile.x >= 2306 and lletya_tile.x <= 2358 and lletya_tile.z >= 3148 and lletya_tile.z <= 3197,
            "after Eluned's escort: " .. tile_text(lletya_r, lletya_tile) .. " (want inside Lletya, x 2306-2358 z 3148-3197; "
                .. "^mend1_lletya_arrival_coord is 2352,3170)")
        t.exec("talkToArianwyn", t.player.talk_to, "mourning_arianwyn", 1)
        t.exec("talkToArianwyn-dialog", t.chat.play, {
            "npc:Welcome to Lletya, home to the Elven Resistance.",
            "player:Thank you. It wasn't particularly easy to get here, it took some work to convince Islwyn I could be trusted.",
            "npc:That's not surprising, he is not particularly fond of humans. But if you've earnt his trust, it proves to me that I was right to take a chance on you.",
            "player:I'm glad you did. I hate the thought that I might still be blindly working for King Lathas and Lord Iorwerth right now.",
            "npc:Well you're here now and you still have a chance to right your previous wrongs.",
            "player:I hope so. There's a lot I still don't understand though. Why are King Lathas and Lord Iorwerth working together? Where does the plague fit in?",
            "npc:I'm afraid that a lot is still unknown to us as well. However, I will explain what I can. Be aware that this is only a brief overview. You'll find some books around the village that explain more if you wish though.",
            "choose:Let's skip the backstory.",
            "player:Let's skip the backstory.",
            "npc:I'd argue that understanding is essential to what comes next. But as you wish.",
            "npc:The bit that is relevant to you is the recent alliance between King Lathas and Lord Iorwerth.",
            "npc:From what we can tell, it seems that Lord Iorwerth has promised to support King Lathas in reclaiming some land his family once owned.",
            "player:But why? What's in it for Lord Iorwerth.",
            "npc:That's what we want you to find out. According to our spies, Lord Iorwerth has used the alliance with King Lathas as an opportunity to infiltrate West Ardougne. The city must contain something he needs.",
            "player:Infiltrate? How?",
            "npc:Those that you know as mourners are in fact elves in service to Lord Iorwerth. We regularly see them crossing the Arandar mountain pass and into the east.",
            "player:Mourners are elves?",
            "npc:That they are. We assume that the plague is a cover up for them to enter the city unchallenged. We don't know why though. That's why we need you.",
            "npc:You know the city and can freely travel there. You are the perfect person for this task. We need you to go to West Ardougne, infiltrate the mourners and find out what they are doing in the city.",
            "player:So you want me to infiltrate the mourners? How am I supposed to achieve that?",
            "npc:I don't have an exact plan for you I'm afraid. You know the city better than any of us so are best placed to make a plan yourself.",
            "npc:However as I said before, there have been sightings of mourners crossing the Arandar mountain pass. I'm sure you can take advantage of that.",
            "player:Alright, I'll see what I can do.",
        })
        t.chat.close()
        t.ticks(3)
        local journal_gathering_r, journal_gathering = t.ui.journal_open("Mourning's End Part I")
        t.check("quest.stage.gathering", journal_gathering_r == "ok" and journal_gathering ~= nil
            and journal_gathering.first_line ~= nil
            and journal_gathering.first_line:find("Arianwyn revealed that the West Ardougne mourners", 1, true) ~= nil,
            "journal_open(Mourning's End Part I) -> " .. tostring(journal_gathering_r) .. " first_line="
                .. tostring(journal_gathering and journal_gathering.first_line))
        t.ui.journal_close()

        end
        do
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
        -- Out of Lletya on foot: Arianwyn's square to the trees in two walks
        -- (a walk_to reaches ~18 tiles), through them, and over Isafdar's
        -- open forest to the overpass mourner (2300,3328 and the trees'
        -- outside, 2302-2304,3193, are one 7,176-tile component).
        t.player.walk_to(2340, 3172, 80)
        t.player.walk_to(2324, 3178, 80)
        if not lletya_gate("lletyaOut.mourner", false) then
            return
        end
        t.exec("goto-killMourner", t.player.goto_tile, 2300, 3328, 0)
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
        -- Every await eats a staged shark under 20 hp (half of 40); the
        -- margin row reads the waits' own "lowest hp" / "never needed to eat".
        local mourner_kill_confirmed = false
        local mourner_kill_attempts = 0
        local mourner_lowest_hp = nil
        local mourner_never_ate = true
        while not mourner_kill_confirmed and mourner_kill_attempts < 3 do
            mourner_kill_attempts = mourner_kill_attempts + 1
            local mourner_attack_result, mourner_attack_detail =
                t.player.attack("mourning_overpass_mourner", 2, 30)
            t.step("killMourner.attack." .. mourner_kill_attempts,
                mourner_attack_result == "ok" and "PASS" or "FAIL",
                "attack(mourning_overpass_mourner,2,30) attempt " .. mourner_kill_attempts .. " -> "
                    .. tostring(mourner_attack_result) .. " " .. tostring(mourner_attack_detail))
            local mourner_dead_result, mourner_dead_detail =
                t.npc.await_dead_engaged(60, nil, { eat = { item = "shark", below = 20 } })
            t.step("killMourner.awaitDead." .. mourner_kill_attempts,
                mourner_dead_result == "ok" and "PASS" or "FAIL",
                "npc.await_dead_engaged(60) attempt " .. mourner_kill_attempts .. " -> "
                    .. tostring(mourner_dead_result) .. " " .. tostring(mourner_dead_detail))
            local low = string.match(tostring(mourner_dead_detail), "lowest hp (%d+)/")
            if low ~= nil then
                low = tonumber(low)
                if mourner_lowest_hp == nil or low < mourner_lowest_hp then
                    mourner_lowest_hp = low
                end
            end
            if string.find(tostring(mourner_dead_detail), "never needed to eat", 1, true) == nil then
                mourner_never_ate = false
            end
            t.ticks(2)
            local still_alive_r = t.npc.nearest("mourning_overpass_mourner", 10)
            mourner_kill_confirmed = still_alive_r ~= "ok"
        end
        t.shot("killMourner.attack-after")
        t.step("killMourner",
            mourner_kill_confirmed and "PASS" or "FAIL",
            "killed after " .. tostring(mourner_kill_attempts) .. " attempt(s), no live copy remains")
        -- Margin: lowest hp at least a quarter of 40 (10) AND food left. A
        -- wait that never ate never saw hp under its 20 threshold.
        local margin_food_r, margin_food_n = t.inv.count("shark")
        local margin_hp_r, margin_hp = t.skill.read("hitpoints")
        -- 10 = a quarter of the staged 40 hitpoints; the waits' detail always
        -- carries "lowest hp N/", so a missing reading fails the row.
        local margin_low_ok = mourner_lowest_hp ~= nil and mourner_lowest_hp >= 10
        t.check("killMourner.margin", margin_low_ok and margin_food_r == "ok" and margin_food_n >= 1,
            "lowest hp " .. tostring(mourner_lowest_hp) .. "/40 from the waits' eat readings"
                .. (mourner_never_ate and " (never needed to eat: hp stayed >= 20)" or "")
                .. ", hp now " .. tostring(margin_hp_r == "ok" and type(margin_hp) == "table" and margin_hp.level or margin_hp_r)
                .. ", shark left " .. tostring(margin_food_n) .. " of 3 staged (margin: lowest hp >= 10 AND food left)")
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

        end
        do
        -- ---- Tegid, south Taverley (eadgar_druid_washing). RE-AUTHOR
        -- (parity1o): the basket's soap is now gated on having actually
        -- talked to Tegid first (%mourning_tegid_chat -- mend1_disguise.rs2
        -- ~mend1_tegid_talk, called from the front of Eadgar's Ruse's own
        -- [opnpc1,eadgar_druid_washing] after its four standard lines). ----
        -- Out of Tirannwn by the same gate, then overland: Tegid's riverbank
        -- (2912,3417) is in the open Kandarin/Taverley component with the
        -- pass's north side.
        t.exec("goto-arandarSouth.out", t.player.goto_tile, 2386, 3331, 0)
        pass_arandar("arandarOut.tegid", false)
        t.exec("goto-talkToTegid", t.player.goto_tile, 2912, 3417, 0)
        t.exec("talkToTegid", t.player.talk_to, "eadgar_druid_washing", 1)
        t.exec("talkToTegid-dialog", t.chat.play, {
            "player:So, you're doing laundry, eh?",
            "npc:Yes. What is it to you?",
            "player:Nice day for it.",
            "npc:I suppose it is.",
            "player:Do you know any way to remove blood stains?",
            "npc:Blood stains is it... Well, the soap I use can clean almost any stain.",
            "player:Really?!? That sounds like just the thing I need, can I use some?",
            "npc:No, I don't have very much soap left and I still have lots to get clean.",
            "player:Alright can you tell me where I can buy some?",
            "npc:You can't... I make it to my own secret recipe.",
        })
        t.chat.close()
        t.expect("talkToTegid.chat_var", t.var.await_server("varb9149_mourning_tegid_chat", 1, 6))

        -- ---- Tegid's laundry basket (eadgar_laundry_basket, op1=Search) --
        -- gated on the talk above; the soap is offered while the bloody top
        -- is still held. ----
        t.exec("searchLaundry", t.player.click_loc, "eadgar_laundry_basket", 1)
        t.exec("searchLaundry-dialog", t.chat.play, {
            "mesbox:It's full of dirty robes, On top you see a bar of soap.",
            "choose:Steal the soap.",
            "*",
        })
        t.chat.close()
        t.expect("searchLaundry.soap", t.inv.await("mourning_soap", 1, 10))

        end
        do
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
        -- The objbox settles the click a tick ahead of the inv_del/inv_add
        -- it announces (docs/QUEST_AUTHORING.md's "a click verb's ok is the
        -- server's sentence, not the container update") -- poll, never a
        -- bare count on the line below. inv.await(name, 0, ticks) is never a
        -- real wait ("total >= 0" is always true, trap 12), so the mourner
        -- top's own arrival is the wait, and the bloody top's loss is read
        -- (bare, safe now) off the SAME inv_del/inv_add transaction.
        local clean_top_wait_r, clean_top_wait_detail = t.inv.await("mourning_mourner_top", 1, 6)
        local bloody_r, bloody_n = t.inv.count("mourning_bloody_mourner_top")
        t.check("cleanTop.result",
            clean_top_wait_r == "ok" and bloody_r == "ok" and bloody_n == 0,
            "mourning_mourner_top: " .. tostring(clean_top_wait_r) .. " " .. tostring(clean_top_wait_detail)
                .. "; mourning_bloody_mourner_top=" .. tostring(bloody_n))

        end
        do
        -- ---- Oronwen, Lletya (2324,3179,0 -- m36_49.spawn:13, mourning_seamstress).
        -- We already carry 2 silk and 1 fur (setup), so mend1_oronwen_talk
        -- (mend1_disguise.rs2:86-115) runs both halves of the exchange in
        -- the one dialogue. ----
        -- Back into Tirannwn by the gate, overland to the trees at Lletya's
        -- west edge, and through them on foot.
        t.exec("goto-arandarNorth.oronwen", t.player.goto_tile, 2386, 3337, 0)
        pass_arandar("arandarIn.oronwen", true)
        t.exec("goto-lletyaGate.oronwen", t.player.goto_tile, 2303, 3193, 0)
        if not lletya_gate("lletyaIn.oronwen", true) then
            return
        end
        t.player.walk_to(2324, 3178, 40)
        -- Lletya's fence stands between the arrival pose and the player
        -- (the talk and equip shots showed only a fence face); look down.
        t.drive.camera(SHOT_YAW, 383, SHOT_ZOOM_OUTDOOR)
        t.exec("talkToOronwen", t.player.talk_to, "mourning_seamstress", 1)
        -- mend1_oronwen_talk (mend1_disguise.rs2:183-247): needs_trousers is
        -- true and trousers_chat=0, so this is the first-visit branch;
        -- has_all is ALSO already true (2 silk + 1 fur from setup), so it
        -- falls straight through into the auto-mend tail in the same call.
        t.exec("talkToOronwen-dialog", t.chat.play, {
            "npc:Hello, can I help?",
            "choose:Do you mend clothes?",
            "player:Do you mend clothes?",
            "npc:I do, but human clothes are too hard to mend. They do not have the finesse of elven garments.",
            "player:I need you to repair some elven clothing as it goes.",
            "npc:Let me take a look at it then.",
            "*", -- objbox: "You show the seamstress the mourner trousers."
            "npc:There's something disturbingly familiar about the design of these trousers, they are made for an elf.",
            "player:But can you fix them?",
            "npc:Of course I can, but I will need two pieces of silk and some bear fur.",
            "player:Great, I have those here.",
            "npc:Huh, it's almost like you knew what I needed.",
            "*", -- objbox: "Oronwen hands you the mourner trousers, they look as good as new."
            "player:Thanks a lot.",
            "npc:Any time.",
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

        end
        do
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
        -- that this quest owns: the full disguise alone lets the player
        -- walk straight through. RE-AUTHOR (parity1o): the letter is no
        -- longer checked at the door (it is Essyllt's to read, in the
        -- basement) and west_ardy_walk_door prints no mes() line at all --
        -- graded on the player's tile actually changing, not a message. ----
        -- Out of Lletya through its trees, out of Tirannwn by the Arandar
        -- gate, overland to East Ardougne's side of the city wall, through the
        -- wall door, and on foot to the HQ's front door.
        if not lletya_gate("lletyaOut.hq", false) then
            return
        end
        t.exec("goto-arandarSouth.hq", t.player.goto_tile, 2386, 3331, 0)
        pass_arandar("arandarOut.hq", false)
        t.exec("goto-ardougneWall.hq", t.player.goto_tile, 2561, 3299, 0)
        if not ardougne_wall("ardougneWallIn.hq", true) then
            return
        end
        t.exec("goto-enterMournerBase", t.player.goto_tile, 2551, 3320, 0)
        -- The door also sets the disguise bit (doors.rs2:68); the trapdoor's
        -- p_teleport lands on 2044,4628,0 (mend1_disguise.rs2:346-353).
        hq_down("enterMournerBase", "enterBasement")
        t.expect("enterMournerBase.disguise_bit", t.var.await_server("varb798_mourning_mourner_disguise", 1, 6))
        t.expect("enterBasement.message", t.msg.expect("You climb down into the basement"))

        end
        do
        -- ---- Essyllt, HQ basement (mourner_hideout_head_mourner,
        -- 2044,4628,0 -- m31_72.spawn:41, the base symbol; the trap door's
        -- own p_teleport lands the player on this same tile). RE-AUTHOR
        -- (parity1o): "New recruit", verbatim -- Essyllt reads and TAKES the
        -- letter of recommendation before the long Death-Guard briefing
        -- (mend1_disguise.rs2:416-470). ----
        t.drive.camera(SHOT_YAW_BASEMENT, 383, SHOT_ZOOM_INDOOR)
        t.exec("talkToEssyllt", t.player.talk_to, "mourner_hideout_head_mourner", 1)
        t.exec("talkToEssyllt-dialog", t.chat.play, {
            "player:Hello, I'm...",
            "npc:Ah... I take it you are one of the new recruits?",
            "player:Well, I'm...",
            "npc:I'm Essyllt and I'm in charge here. Now come on, let us have a look at your paper work.",
            "*", -- objbox: "You hand over the letter of recommendation."
            "npc:This seems to be all in order. Welcome to the Death Guard. Now, for your first assignment...",
            "npc:As you may know, part of what we do here is keep the people believing in the plague...",
            "player:Why?",
            "npc:Did they not tell you? Hmm... I'd better fill you in.",
            "npc:Well as you undoubtedly know, we were sent here by Lord Iorwerth to secure the city. The reason for this is so that we can access the caves below...",
            "player:What's important about the caves?",
            "npc:I'm afraid that information is classified until you prove your commitment to the Death Guard.",
            "player:Oh, fair enough.",
            "npc:Now to keep our presence in the city secret, Lord Iorwerth arranged an alliance with King Lathas, the ruler in the east.",
            "npc:Apparently the fool thinks we will help him dispose of the Knights of Camelot once our work here is done. I expect he'll be disappointed.",
            "npc:Anyway, the plague serves two purposes. First, it allows us to move around the city in secret thanks to our mourner disguises.",
            "npc:Second, it means we can abduct citizens to mine the caves below with ease. We just pretend they've fallen victim to the plague.",
            "player:Doesn't anyone suspect the truth?",
            "npc:Well we have had a few near misses. Recently we had some girl from East Ardougne poking around asking too many questions, so we locked her up while we decided what to do with her.",
            "npc:Annoyingly, she managed to escape with the help of some adventurer. Lucky for us, they were so stupid that we managed to use the adventurer to deal with King Tyras. It all worked out quite well in the end.",
            "npc:Things like that is why we must ensure the people keep believing in the plague though. One of the things we do to keep up the lie is to fool old Farmer Brumty into believing his sheep are infected.",
            "npc:The old halfwit thinks just because his sheep are an abnormal colour, that they are all ill with plague. It's amazing what a bit of dye can do.",
            "player:You dyed them?",
            "npc:Well you don't think they end up those ridiculous colours naturally do you?",
            "npc:Anyway, shame of it is that we have yet to find a way to stop the dye from washing out. So we need someone, that someone being you, to go and re-dye them.",
            "player:Simple enough, you want me to give a blue rinse to a load of old sheep?",
            "npc:Not quite, for starters the sheep need to be dyed red, yellow, green and blue. But most important is that you are not seen doing this by anyone.",
            "npc:Also you will need to re-dye them the colours they already are or the farmer may notice the change.",
            "player:That sounds a little more tricky. How did you do it before?",
            "npc:We have a gnomic device that fires fat dye parcels that rupture on impact.",
            "npc:Unfortunately, we have run out of the parcels and the device is broken.",
            "player:Can I take a look at it?",
            "npc:Sure. We have a gnome inventor here too. Sadly, he is not being very helpful about fixing it, but you can talk to him if you like. Here is the key, he is in the next room. And here's the device as well.",
            "*", -- doubleobjbox: "Essyllt hands you a strange object and a tarnished key."
        })
        t.chat.close()
        t.expect("talkToEssyllt.letter_gone", t.inv.expect_absent("mourning_mourner_message"))
        t.expect("talkToEssyllt.stage4", t.var.await_server("varp517_mourning_quest", 4, 10))
        t.expect("talkToEssyllt.items", t.inv.await_all({mourning_gnome_key = 1, mourning_paint_gun_broken = 1}, 10))
        t.ticks(3)
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

        end
        do
        -- ---- The caged gnome, rebuilt onto two entities as of parity1m
        -- (mend1_gnome.rs2's own header): the RACK (mourning_gnome_rack,
        -- states 0..6) answers while he is strapped down, and a WALKING
        -- gnome npc (mourner_hideout_gnome/_head) answers from 7 on. Entry
        -- gates on %mourning_quest < ^mend1_assignment(4), the stage
        -- talkToEssyllt actually leaves the player in. ----
        -- Essyllt's room (x 2039-2045 z 4628-4633) and the gnome's (x
        -- 2034-2038 z 4628-4633) both open north onto the basement corridor:
        -- mourner_hideout_door2 (2042,4633) out, mourner_hideout_door3
        -- (2037,4633) in (maps/m31_72.jl2, generic door_closed).
        pass_door("gnomeRoom.essylltDoor", "mourner_hideout_door2", "mourner_hideout_door2_open", 2042, 4633, 2042, 4633, 2042, 4635,
            function(w) return w.z >= 4634 end, "in the corridor, z >= 4634")
        pass_door("gnomeRoom.gnomeDoor", "mourner_hideout_door3", "mourner_hideout_door3_open", 2037, 4633, 2037, 4634, 2036, 4631,
            function(w) return w.z <= 4633 and w.x >= 2034 and w.x <= 2038 end, "in the gnome's room, x 2034-2038 z <= 4633")
        local rack_near_result, rack_near = t.world.loc_near("mourning_gnome_rack", 6)
        t.step("mourningGnomeRack.locate",
            rack_near_result == "ok" and "PASS" or "FAIL",
            "world.loc_near(mourning_gnome_rack, 6) -> " .. tostring(rack_near_result) .. " "
                .. (rack_near_result == "ok" and string.format("tile=%s,%s,%s match=%s",
                    tostring(rack_near.tile_x), tostring(rack_near.tile_z), tostring(rack_near.level), tostring(rack_near.match))
                    or tostring(rack_near)))

        -- ---- Click 1: mend1_gnome_bargains (mend1_gnome.rs2:112-161) --
        -- "Bargains and bluffs", verbatim; quest-helper's talkToGnome picks
        -- the third option ("You said about toad crunchies and being
        -- tickled."), which is the correct one -> gnome = weakness(3),
        -- %mourning_quest = ^mend1_gnome_task. ----
        t.drive.camera(SHOT_YAW_BASEMENT, 383, SHOT_ZOOM_INDOOR)
        t.exec("gnomeCage.bargains", t.player.click_loc, "mourning_gnome_rack", 1)
        t.exec("gnomeCage.bargains-dialog", t.chat.play, {
            "player:Hello, will you help me fix this... err... thing?",
            "npc:I'm not helping you fix that, as if it hasn't caused enough trouble as it is. Your friends have already tried every torture in the book!",
            "npc:I'm still not telling anyone squat.",
            "player:Have they tried... err... Stretching your eyelids yet?",
            "npc:Yes, it didn't work.",
            "player:How about... feeding you nail and prune stew?",
            "npc:That's all I've been living on since I got here.",
            "player:Set fire to your nostril hair? Let rabid rabbits nibble your toes? Given you a twisted arm?",
            "npc:Yes, yes and yes. Tried them all, quite liked the toe nibbling.",
            "player:Extracted your wisdom teeth?",
            "npc:Us gnomes aren't wise so we don't get them. Face it, you'll never put me in enough pain that I'll tell you what you wanna know.",
            "npc:I used to play gnomeball as a kid. This is a walk in the park in comparison.",
            "player:Alright I get the picture. What will work then?",
            "npc:Ha! You think I'm stupid enough to tell you that I've been craving toad crunchies or that I can't stand having my feet tickled!",
            "player:Err... But you just told me?!?",
            "npc:I did? What did I say?",
            "choose:You said about toad crunchies and being tickled.",
            "player:You said about toad crunchies and being tickled.",
            "npc:Oops... I mean erm... No that must have been some other err... gnome...",
            "player:You're not fooling me... So you'll help me in exchange for toad crunchies?",
            "npc:If you think you can just buy my co-operation you're a bigger imbecile than I thought.",
            "player:You will tell me, sooner or later.",
        })
        t.chat.close()
        t.ticks(3)
        local journal_task_r, journal_task = t.ui.journal_open("Mourning's End Part I")
        local journal_task_line = journal_task and journal_task.first_line
        t.check("quest.stage.gnome_task",
            journal_task_r == "ok" and journal_task_line ~= nil
                and journal_task_line:find("I'm working on the caged gnome", 1, true) ~= nil,
            "journal_open(Mourning's End Part I) -> " .. tostring(journal_task_r) .. " first_line="
                .. tostring(journal_task_line))
        t.ui.journal_close()

        -- ---- Click 2: use FEATHER on the rack (mend1_gnome.rs2:186-233,
        -- [oplocu,mourning_gnome_rack]). Neither the feather nor the toad
        -- crunchies are consumed (parity1m's header: the wiki's Tarnished
        -- key page). With magic_logs + leather ALSO already held (setup),
        -- this single use-on collapses "tickle" and "talk again" into one
        -- interaction -> gnome = talked_item(6) directly. ----
        t.ui.tab("inventory")
        t.ticks(2)
        local gnome_rack_target = t.player.by_symbol("loc", "mourning_gnome_rack")
        t.exec("gnomeCage.tickle", t.player.use_on, "feather", gnome_rack_target)
        t.exec("gnomeCage.tickle-dialog", t.chat.play, {
            "*", -- doubleobjbox: "You dangle the toad crunchie above the gnome's nose..."
            "player:Now will you help me or would you prefer it if I tickle your feet?",
            "npc:Alright... alright! I'm beaten! Bring me some soft leather and some magic logs and I'll see what I can do.",
            "player:I have all of that here.",
            "npc:Well let me up off of this rack and I'll get started, I can't do anything while I'm all tied up.",
        })
        t.chat.close()
        local feather_r, feather_n = t.inv.count("feather")
        t.check("gnomeCage.tickle.result", feather_r == "ok" and feather_n == 1,
            "feather=" .. tostring(feather_n) .. " (" .. tostring(feather_r) .. ", not consumed)")

        -- ---- Click 3: Release, op2 (mend1_gnome.rs2:247-274). The key is
        -- checked, never consumed (wiki Tarnished key: "can be freely
        -- disposed of after the quest"); with every item already held, the
        -- release, the item hand-over and the device fix all land in the
        -- same click -> gnome = repaired(9). ----
        t.drive.camera(SHOT_YAW_BASEMENT, 383, SHOT_ZOOM_INDOOR)
        t.exec("gnomeCage.release", t.player.click_loc, "mourning_gnome_rack", 2)
        t.exec("gnomeCage.release-dialog", t.chat.play, {
            "npc:What do you think you are doing? You are meant to be getting information out of him, not making friends!",
            "player:I need to let him up if he's to fix this advice.",
            "npc:Okay, but without the proper paperwork he stays in this room.",
            "*", -- mesbox: "You release the gnome and hand him..."
            "npc:Right, here you go. Now leave me to eat my crunchies in peace.",
            "*", -- objbox: "The gnome gives you a fixed device."
        })
        t.chat.close()
        local key_after_r, key_after_n = t.inv.count("mourning_gnome_key")
        local gun_r, gun_n = t.inv.count("mourning_paint_gun")
        local logs_r, logs_n = t.inv.count("magic_logs")
        local leather_r, leather_n = t.inv.count("leather")
        local crunchies_r, crunchies_n = t.inv.count("toad_crunchies")
        t.check("gnomeCage.release.result",
            key_after_r == "ok" and key_after_n == 1
                and gun_r == "ok" and gun_n == 1 and logs_r == "ok" and logs_n == 0
                and leather_r == "ok" and leather_n == 0 and crunchies_r == "ok" and crunchies_n == 0,
            string.format("mourning_gnome_key=%s (kept) mourning_paint_gun=%s magic_logs=%s leather=%s toad_crunchies=%s",
                tostring(key_after_n), tostring(gun_n), tostring(logs_n), tostring(leather_n), tostring(crunchies_n)))

        -- ---- Ask about toads (mend1_gnome.rs2:371-392, the released
        -- gnome's own [opnpc1,mourner_hideout_gnome]/[opnpc1,...
        -- _head] -- gnome = repaired already, so this is
        -- mend1_gnome_ask_toads, verbatim). ----
        t.drive.camera(SHOT_YAW_BASEMENT, 383, SHOT_ZOOM_INDOOR)
        t.exec("gnomeCage.askToads", t.player.talk_to, "mourner_hideout_gnome", 1)
        t.exec("gnomeCage.askToads-dialog", t.chat.play, {
            "npc:What are you after now?",
            "player:Do you know where I can get the dye parcels for his thing?",
            "npc:You mourners are really not very good at this are you? Fine, since you gave me some crunchies, the parcels are actually toads.",
            "player:Toads?",
            "npc:Yes, toads. Get some bellows and fill them with the dye you want. Then just find a toad and use the bellows to fill it up.",
            "player:Poor toads.",
            "npc:Ahh they're fine, a bit of dye never hurt anything... It's the firing them out the device that kills them.",
            "player:Ewww... That's nasty.",
            "npc:Oh and torturing gnomes is perfectly fine? Hypocrite.",
            "player:You make a fair point. Where do I find the toads?",
            "npc:You get loads of them down in the Feldip Hills, especially near the ponds.",
            "player:Ah yes, I've used them to catch chompy birds before. I have some bellows right here as well.",
            "npc:Catching chompy birds? You mourners are weird. Anyway, if you don't need anything else I'd like to be left in peace.",
        })
        t.chat.close()
        t.ticks(3)
        -- Same one-off UI-queue race the journal_assignment/journal_marked
        -- reads elsewhere in this file hit (right after a long dialogue's
        -- own last continue_ click) -- retry once with more settle.
        local journal_dye_r, journal_dye = t.ui.journal_open("Mourning's End Part I")
        local journal_dye_note = ""
        if journal_dye_r ~= "ok" then
            journal_dye_note = " [retry after timeout: " .. tostring(journal_dye) .. "]"
            t.ticks(5)
            t.settle()
            journal_dye_r, journal_dye = t.ui.journal_open("Mourning's End Part I")
        end
        local journal_dye_line = (type(journal_dye) == "table") and journal_dye.first_line or nil
        t.check("quest.stage.gnome_dye_learned",
            journal_dye_r == "ok" and journal_dye_line ~= nil
                and journal_dye_line:find("The gnome explained that the device fires dyed, inflated toads", 1, true) ~= nil,
            "journal_open(Mourning's End Part I) -> " .. tostring(journal_dye_r) .. " first_line="
                .. tostring(journal_dye_line) .. journal_dye_note)
        t.ui.journal_close()

        end
        do
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
        -- Out of the basement by its ladder, out of the HQ, across West
        -- Ardougne to the city wall, and through it; East Ardougne's side and
        -- the Feldip toad ground are in one open Kandarin component.
        hq_up("leaveBasement.gnome")
        t.exec("goto-ardougneWallWest.toads", t.player.goto_tile, 2555, 3299, 0)
        if not ardougne_wall("ardougneWallOut.toads", false) then
            return
        end
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

        -- One colour at a time: dye (the dye takes the empty bellows,
        -- mend1_sheep.rs2's mend1_bellows_*), walk up to a live toad, use the
        -- dyed bellows on it, and wait for the bloated toad -- the catch
        -- hands the empty bellows back, and the next colour's dye needs it,
        -- so a catch still walking when the next dye is pressed would find
        -- no bellows (run1 of this round: green's catch landed late and
        -- blue's dye answered "empty_ogre_bellows: not in the backpack").
        -- A toad that wanders between aim and press gets up to two more
        -- presses, each graded by the toad item arriving.
        local function dye_and_catch(colour, label)
            local dye = colour .. "dye"
            local dyed = "mourning_ogre_bellows_" .. colour
            local toad_item = "mourning_bloated_toad_" .. colour
            local dr, dd = t.player.use_item_on_item("empty_ogre_bellows", dye)
            if dr ~= "ok" then
                t.ticks(3)
                dr, dd = t.player.use_item_on_item("empty_ogre_bellows", dye)
            end
            local held_r, held_n = t.inv.count(dyed)
            t.check("dyeBellows." .. colour, dr == "ok" and held_r == "ok" and held_n == 1,
                "use_item_on_item(empty_ogre_bellows, " .. dye .. ") -> " .. tostring(dr) .. " " .. tostring(dd)
                    .. "; " .. dyed .. "=" .. tostring(held_n))
            local caught_r, caught_d, cr, cd
            local presses = 0
            repeat
                presses = presses + 1
                local nr, toad = t.npc.nearest("toad", 15)
                if presses == 1 then
                    t.step("toad" .. label .. ".locate", nr == "ok" and "PASS" or "FAIL", "npc.nearest(toad, 15) -> " .. tostring(nr))
                end
                if nr == "ok" then
                    local wr = t.player.walk_to(toad.x, toad.z, 10)
                    if presses == 1 then
                        local ar, at = t.world.tile()
                        t.check("walk-toad" .. label, ar == "ok" and math.abs(at.x - toad.x) <= 3 and math.abs(at.z - toad.z) <= 3,
                            "walk_to(" .. tostring(toad.x) .. "," .. tostring(toad.z) .. ") -> " .. tostring(wr)
                                .. "; standing at " .. tile_text(ar, at) .. " (want within 3 of the toad)")
                    end
                end
                cr, cd = t.player.use_on(dyed, t.player.by_symbol("npc", "toad"))
                caught_r, caught_d = t.inv.await(toad_item, 1, 8)
            until caught_r == "ok" or presses >= 3
            t.check("catchToad." .. colour, caught_r == "ok",
                "use_on(" .. dyed .. ", toad) press " .. presses .. " -> " .. tostring(cr) .. " " .. tostring(cd)
                    .. "; inv.await(" .. toad_item .. ", 1, 8) -> " .. tostring(caught_r) .. " " .. tostring(caught_d))
            t.expect("catchToad." .. colour .. ".message", t.msg.expect("You now have a " .. colour .. " toad."))
        end
        dye_and_catch("red", "Red")
        dye_and_catch("yellow", "Yellow")
        dye_and_catch("green", "Green")
        dye_and_catch("blue", "Blue")

        local redtoad_r, redtoad_n = t.inv.count("mourning_bloated_toad_red")
        local greentoad_r, greentoad_n = t.inv.count("mourning_bloated_toad_green")
        local bluetoad_r, bluetoad_n = t.inv.count("mourning_bloated_toad_blue")
        local yellowtoad_r, yellowtoad_n = t.inv.count("mourning_bloated_toad_yellow")
        -- A crowded toad pond (docs/QUEST_AUTHORING.md: "a hunted press can
        -- fail 100% reproducibly ... on one npc in a cramped cluster") can
        -- leave one colour's catch pressed on a copy that never fires the
        -- trigger, with no item change and the message check above only
        -- catching a stale ring line -- retry a missing colour once more
        -- against a freshly re-resolved copy before grading it a real miss.
        local retoad_attempts = 0
        while retoad_attempts < 2 and not (redtoad_n == 1 and greentoad_n == 1 and bluetoad_n == 1 and yellowtoad_n == 1) do
            retoad_attempts = retoad_attempts + 1
            local retoad_target = t.player.by_symbol("npc", "toad")
            if redtoad_n ~= 1 then
                t.player.use_on("mourning_ogre_bellows_red", retoad_target)
                t.ticks(2)
            end
            if yellowtoad_n ~= 1 then
                t.player.use_on("mourning_ogre_bellows_yellow", retoad_target)
                t.ticks(2)
            end
            if greentoad_n ~= 1 then
                t.player.use_on("mourning_ogre_bellows_green", retoad_target)
                t.ticks(2)
            end
            if bluetoad_n ~= 1 then
                t.player.use_on("mourning_ogre_bellows_blue", retoad_target)
                t.ticks(2)
            end
            redtoad_r, redtoad_n = t.inv.count("mourning_bloated_toad_red")
            greentoad_r, greentoad_n = t.inv.count("mourning_bloated_toad_green")
            bluetoad_r, bluetoad_n = t.inv.count("mourning_bloated_toad_blue")
            yellowtoad_r, yellowtoad_n = t.inv.count("mourning_bloated_toad_yellow")
        end
        t.check("dyeBellows.result",
            redtoad_r == "ok" and redtoad_n == 1 and greentoad_r == "ok" and greentoad_n == 1
                and bluetoad_r == "ok" and bluetoad_n == 1 and yellowtoad_r == "ok" and yellowtoad_n == 1,
            string.format("mourning_bloated_toad_red=%s mourning_bloated_toad_green=%s mourning_bloated_toad_blue=%s mourning_bloated_toad_yellow=%s (retry round(s)=%d)",
                tostring(redtoad_n), tostring(greentoad_n), tostring(bluetoad_n), tostring(yellowtoad_n), retoad_attempts))
        -- Each dye takes its colour and the empty bellows (mend1_sheep.rs2's
        -- mend1_bellows_*: inv_del empty_ogre_bellows), each catch gives the
        -- empty bellows back: four cycles end with the one staged bellows and
        -- no dye.
        local eb_r, eb_n = t.inv.count("empty_ogre_bellows")
        local rd_r, rd_n = t.inv.count("reddye")
        local yd_r, yd_n = t.inv.count("yellowdye")
        local gd_r, gd_n = t.inv.count("greendye")
        local bd_r, bd_n = t.inv.count("bluedye")
        t.check("dyeBellows.consumed", eb_r == "ok" and eb_n == 1 and rd_r == "ok" and rd_n == 0 and yd_r == "ok" and yd_n == 0
                and gd_r == "ok" and gd_n == 0 and bd_r == "ok" and bd_n == 0,
            string.format("empty_ogre_bellows=%s (want 1) reddye=%s yellowdye=%s greendye=%s bluedye=%s (want 0 each)",
                tostring(eb_n), tostring(rd_n), tostring(yd_n), tostring(gd_n), tostring(bd_n)))

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

        end
        do
        -- ---- Report back to Essyllt, HQ basement (mend1_essyllt_after_sheep,
        -- mend1_disguise.rs2:178-205). The guide's enterBaseAfterSheep /
        -- enterBasementAfterSheep: from the sheep fields overland to East
        -- Ardougne's side of the city wall, through the wall door, and in by
        -- the front door, the trapdoor room's door and the trapdoor
        -- (doors.rs2's mourner-HQ gate reads %mourning_mourner_disguise,
        -- already 1, and walks through silently). ----
        t.exec("goto-ardougneWall.afterSheep", t.player.goto_tile, 2561, 3299, 0)
        if not ardougne_wall("ardougneWallIn.afterSheep", true) then
            return
        end
        t.exec("goto-enterBaseAfterSheep", t.player.goto_tile, 2551, 3320, 0)
        hq_down("enterBaseAfterSheep", "enterBasementAfterSheep")
        t.exec("talkToEssylltAfterSheep", t.player.talk_to, "mourner_hideout_head_mourner", 1)
        -- mend1_essyllt_after_sheep (mend1_disguise.rs2:543-566): "New
        -- orders", verbatim, falling straight into mend1_essyllt_poison_
        -- orders in the same call (no return between them) -- one
        -- continuous dialogue from the sheep report to the food-store task.
        t.exec("talkToEssylltAfterSheep-dialog", t.chat.play, {
            "npc:Have you finished with those sheep yet?",
            "player:Yes, it is done. What next?",
            "npc:It is good to see your enthusiasm. I was going to get one of the others to do this job, but as you are here...",
            "npc:It has been quite some time since anyone got ill from the plague, so I would like you to see to it that people do!",
            "player:Err... But the plague doesn't exist, how am I meant to do that?",
            "npc:We have never tried this before, but some joker put something in our food not too long ago that gave us all symptoms akin to those of the plague.",
            "npc:If you can find out what it was that we were poisoned with, you could reproduce the effects. If done right the poison should not be fatal and could help restock our dwindling supply of cheap labour.",
            "npc:We can have our men remove the 'infected' citizens and sent into the mines.",
            "npc:Distribution should be easy enough. Because of the city walls no one can grow their own food. Instead, all the food here comes from one of three supply points.",
            "player:Let me get this clear, you want me to find out who poisoned you, find out what they poisoned you with, find out how to make the poison and produce enough poison to affect a lot of people?",
            "npc:Don't forget the part where you use the poison to contaminate the food supply. Two of the three supply points should be enough.",
            "player:How am I meant to do all of that?",
            "npc:You seem resourceful so I'm sure you will come up with something. I would help you but I am not a biologist.",
            "player:Luckily for me I know a biologist nearby.",
            "npc:Someone trustworthy I hope?",
            "player:Oh yes, definitely.",
            "npc:Very well. Perform this task and then return to me.",
        })
        t.chat.close()
        t.expect("talkToEssylltAfterSheep.stage6", t.var.await_server("varp517_mourning_quest", 6, 10))
        -- ---- Pick up the rotten apple, north-west of the Mourner HQ
        -- (mend1_poison.rs2's own header: a real `rottenapples` ground item
        -- sits at areas/world/configs/m39_52.spawn x2535/y3333, exactly
        -- quest-helper's own pickUpRottenApple WorldPoint -- "already works
        -- for free" via the engine's generic ground-item take, no script
        -- needed, but driven here as its own row rather than left to
        -- incidental pickup on some earlier walk). ----
        -- Out of the basement by a teleport (its ladder leads nowhere), back
        -- overland to the city wall and on foot to the apple, two tiles north
        -- of where the goto stops (2535,3331: one tile off, the player's own
        -- model covered the stack in every hunted pose, scratch runs
        -- mesp_tail1/2). The radius-3 pick cannot reach the second spawn at
        -- 2549,3332 (m39_52.spawn:56), so exactly one is held.
        -- Up the ladder and out of the HQ; the apple barrel's street is in
        -- the same walled West Ardougne component.
        hq_up("leaveBasement.afterSheep")
        t.exec("goto-pickUpRottenApple", t.player.goto_tile, 2535, 3331, 0)
        local apple_before_r, apple_before_n = t.inv.count("rottenapples")
        -- The stack shares its tile with wandering rats: a press can answer
        -- `covered` (scratch run mesp_tail1 row 180, the Take row under an
        -- Attack Rat row). Look straight down and press again, at most three.
        local rottenapple_pick_result, rottenapple_pick_detail, apple_wait_r, apple_wait_detail
        local apple_presses = 0
        repeat
            apple_presses = apple_presses + 1
            t.drive.camera((apple_presses - 1) * 512, 383, SHOT_ZOOM_INDOOR)
            rottenapple_pick_result, rottenapple_pick_detail = t.player.click_obj("rottenapples", 3)
            apple_wait_r, apple_wait_detail = t.inv.await("rottenapples", 1, 6)
        until apple_wait_r == "ok" or apple_presses >= 3
        rottenapple_pick_detail = tostring(rottenapple_pick_detail) .. " (press " .. apple_presses .. ")"
        local apple_after_pick_r, apple_after_pick_n = t.inv.count("rottenapples")
        t.check("pickUpRottenApple", apple_before_r == "ok" and apple_before_n == 0 and apple_wait_r == "ok"
                and apple_after_pick_r == "ok" and apple_after_pick_n == 1,
            "rottenapples " .. tostring(apple_before_n) .. " -> " .. tostring(apple_after_pick_n)
                .. "; click_obj(rottenapples, 3) -> " .. tostring(rottenapple_pick_result) .. " " .. tostring(rottenapple_pick_detail)
                .. "; inv.await -> " .. tostring(apple_wait_r) .. " " .. tostring(apple_wait_detail))
        t.ticks(3)
        -- the same one-off UI-mount race the other journal reads retry
        local journal_poison0_r, journal_poison0
        for _ = 1, 3 do
            journal_poison0_r, journal_poison0 = t.ui.journal_open("Mourning's End Part I")
            if journal_poison0_r == "ok" then
                break
            end
            t.ui.journal_close()
            t.ticks(5)
        end
        local journal_poison0_line = type(journal_poison0) == "table" and journal_poison0.first_line or nil
        t.check("quest.stage.poison_task",
            journal_poison0_r == "ok" and journal_poison0_line ~= nil
                and journal_poison0_line:find("Essyllt wants me to make people ill as if from the plague", 1, true) ~= nil,
            "journal_open(Mourning's End Part I) -> " .. tostring(journal_poison0_r) .. " first_line=" .. tostring(journal_poison0_line))
        t.ui.journal_close()

        end
        do
        -- ---- Elena, north-west East Ardougne (elena2, m40_52.spawn:22 --
        -- 2592,3336,0; mend1_elena_talk, mend1_poison.rs2:29-42 -- the
        -- rottenapples>=1 branch, opened by [opnpc1,elena2]'s own front-of-
        -- trigger call once %mourning_quest = poison_task). Her house (x
        -- 2590-2592 z 3334-3338) is walled in with elenadoor2 on 2592,3339's
        -- south edge: out of West Ardougne by the wall door, overland to her
        -- door, through it on foot. ----
        t.exec("goto-ardougneWallWest.elena", t.player.goto_tile, 2555, 3299, 0)
        if not ardougne_wall("ardougneWallOut.elena", false) then
            return
        end
        t.exec("goto-talkToElena", t.player.goto_tile, 2592, 3340, 0)
        pass_door("elenaHouseIn", "elenadoor2", "elenadoor2open", 2592, 3339, 2592, 3339, 2592, 3337,
            function(w) return w.z <= 3338 and w.x >= 2590 and w.x <= 2592 end, "inside Elena's house, z <= 3338")
        t.drive.camera(SHOT_YAW, 383, SHOT_ZOOM_INDOOR)
        local elena_talk_result, elena_talk_detail = t.player.talk_to("elena2", 1)
        t.step("talkToElena", elena_talk_result == "ok" and "PASS" or "FAIL",
            "talk_to(elena2,1) -> " .. tostring(elena_talk_result) .. " " .. tostring(elena_talk_detail))
        t.shot("talkToElena-after")
        -- mend1_elena_old_friend (mend1_poison.rs2:127-176), the FULL-
        -- DISGUISE branch -- this file never takes the mourner gear off
        -- before visiting her, so Elena mistakes the player for a mourner
        -- first. Both branches converge on the same backstory tail, which
        -- ends with "Bring me a sample of rotten apple..."; the apple is
        -- already held (pickUpRottenApple above), so the SAME call hands it
        -- over and falls straight into mend1_elena_tests (elena=apple_given
        -- then =sieve, both silent) -- one continuous dialogue from "old
        -- friend" through the sieve hand-over. ----
        t.exec("talkToElena-dialog", t.chat.play, {
            "player:Hello again Elena.",
            "npc:How dare you enter my house, Mourner! Get out!",
            "player:Elena, it's me...",
            "npc:I didn't recognise you in all that Mourner gear!",
            "player:It's a long story.",
            "npc:Well I'm not going anywhere. What's going on?",
            "player:Well I'll start from when we found out that the plague was a hoax. When I confronted the king, he told me that he faked the plague to keep people safe from his brother, King Tyras.",
            "player:He said that Tyras was taken by the Dark Lord while exploring the lands to the west. He claimed that the Dark Lord corrupted him by forcing him to drink from the Chalice of Eternity.",
            "npc:But that was a lie?",
            "player:Indeed, but I didn't find out until much later. On the kings orders, I travelled through the Underground Pass and into the western lands of Tirannwn.",
            "player:Once there, I met up with Lord Iorwerth, the leader of some elves that had allied with the king. He helped me kill King Tyras.",
            "player:That was when I discovered the truth. While returning to King Lathas, I was confronted by another elf named Arianwyn, the leader of a group of rebel elves opposing Lord Iorwerth.",
            "player:At the time, I was carrying a letter from Lord Iorwerth to King Lathas. Arianwyn magically broke the seal on the letter so I could read it.",
            "npc:What did it say?",
            "player:It revealed that it's actually King Lathas and Lord Iorwerth who serve the Dark Lord. According to the letter, King Lathas wants to reclaim Camelot from King Arthur and believes the Dark Lord can help him.",
            "npc:And Lord Iorwerth?",
            "player:I'm not exactly sure yet. West Ardougne seems to be key to his plans though. I met with Arianwyn who revealed to me that the mourners are actually elves in service to Lord Iorwerth.",
            "player:I've infiltrated the mourners and I'm trying to work out what they are doing. From what I've learnt so far, there's something important to them in the caves below West Ardougne.",
            "player:I'm currently working with them to earn their trust. Hopefully they'll reveal their plans to me soon.",
            "npc:I see, that's quite the reveal. If you need anything from me just ask.",
            "player:Well, I was hoping you could help me with something.",
            "npc:What's the problem?",
            "player:I've been asked to produce a poison based on rotten apples.",
            "npc:I doubt any poison based solely on apples, rotten or not, would be very effective.",
            "player:Well I put a rotten apple in the mourners' stew, I'm told that the effect was much like the symptoms of the plague.",
            "npc:Hmm... That sounds like they were ill from some sort of toxin, I should think it was a mould of some sort that had the effect, not the rotten apple itself. What do you need it for?",
            "player:I need to poison a large food supply in order to get the mourners' trust.",
            "npc:That's awful, I can't help you with that.",
            "player:If I don't gain the trust of the mourners, then the people of West Ardougne will have a much worse time than the effects of that toxin. Elena trust me.",
            "npc:You'd better be right about this, and I'd better make sure you get this toxin right so no one dies!",
            "npc:Bring me a sample of rotten apple to examine. I'll also see if I can make a counteractant for the toxin.",
            "player:I have one right here.",
            "*", -- objbox: "You hand Elena the rotten apple."
            "npc:Ick... Alright then let's get started.",
            "*", -- doubleobjbox: "Elena starts some tests on the apple."
            "npc:Right.. lets see...",
            "npc:Okay, I've managed to isolate a small sample of the toxin. It's a byproduct of the mould that grows on these apples, it's not fatal so a counteractant shouldn't be necessary.",
            "npc:How big is the store that you're going to affect?",
            "player:Well I was instructed to contaminate two of the three supply points in West Ardougne.",
            "npc:That's over half of the food in the city! You're going to need a huge amount of the toxin to do that. You're also going to need to refine it too, or people will notice rotten apples in amongst the food.",
            "player:This is starting to sound a little tricky, can't you make it for me?",
            "npc:I would, but I don't have the right equipment here to do anything in bulk.",
            "player:Alright then, tell me the process and I'll get started.",
            "npc:Right then, the first thing to do is mash up a lot of rotten apples, then you will need to dissolve the toxin into a liquid that has a very low evaporation point, some form of solvent.",
            "npc:I don't know as much about those kinds of chemicals but I believe naphtha will be perfect for this.",
            "player:Ah yes, I've worked with naphtha before. The chemist in Rimmington helped me make it from coal tar.",
            "npc:Perfect. Well get yourself some naphtha and some mashed rotten apples and mix them together. Once you've done that, strain out any solids.",
            "npc:Finally, heat the mixture to evaporate off the solvent. Be careful of naked flames as the solvent will be highly flammable.",
            "player:And that's all is it? Why isn't anything ever easy?",
            "npc:Sorry, I never said it would be simple. Here, you'll need this to strain out the solids.",
            "*", -- objbox: "Elena hands you a large sieve."
            "npc:You may want to check out the orchard just north of the city. I hear no one has tended it since the blight infected the trees there. I imagine it will have plenty of rotten apples.",
        })
        t.chat.close()
        local sieve_wait_r, sieve_wait_detail = t.inv.await("mourning_sieve", 1, 10)
        t.step("talkToElena.await_sieve", sieve_wait_r == "ok" and "PASS" or "FAIL",
            "inv.await(mourning_sieve, 1, 10) -> " .. tostring(sieve_wait_r) .. " " .. tostring(sieve_wait_detail))
        local apple_after_r, apple_after_n = t.inv.count("rottenapples")
        t.check("talkToElena.result", apple_after_r == "ok" and apple_after_n == 0,
            "rottenapples=" .. tostring(apple_after_n) .. " (" .. tostring(apple_after_r) .. ")")
        t.expect("talkToElena.var", t.var.await_server("varb805_mourning_elena", 4, 6))
        pass_door("elenaHouseOut", "elenadoor2", "elenadoor2open", 2592, 3339, 2592, 3338, 2592, 3340,
            function(w) return w.z >= 3339 end, "outside Elena's house, z >= 3339")

        end
        do
        -- ---- Barrel + apple pile, north-west of the Mourner HQ
        -- (mourning_orchard_applepile, mend1_poison.rs2:45-58, RE-AUTHOR per
        -- this file's header point (2): pickUpBarrel is a real ground item
        -- now (regicide_barrel_empty, areas/world/configs/m38_52.spawn:38,
        -- exactly quest-helper's own pickUpBarrel WorldPoint) and
        -- useBarrelOnPile is a real [oplocu,...] use-on -- the bare
        -- [oploc1,...] only tells the player they need a barrel now. ----
        -- The orchard (x 2466-2494 z 3365-3387) is fenced in; its gate
        -- mourning_orchard_fencegate_l/_r stands on 2474-2475,3364's north
        -- edge. Overland to the gate, through it, and on to the barrel.
        t.exec("goto-fillBarrel", t.player.goto_tile, 2474, 3363, 0)
        pass_door("orchardIn", "mourning_orchard_fencegate_l", "mourning_orchard_fencegate_open_l", 2474, 3364, 2474, 3364, 2474, 3366,
            function(w) return w.z >= 3365 end, "inside the orchard, z >= 3365", 2)
        t.player.walk_to(2487, 3372, 30)
        local pickup_barrel_before_r, pickup_barrel_before_n = t.inv.count("regicide_barrel_empty")
        local pickup_barrel_result, pickup_barrel_detail = t.player.click_obj("regicide_barrel_empty", 3)
        local pickup_barrel_wait_r = t.inv.await("regicide_barrel_empty", 1, 10)
        local pickup_barrel_count_r, pickup_barrel_count_n = t.inv.count("regicide_barrel_empty")
        t.check("pickUpBarrel",
            pickup_barrel_before_r == "ok" and pickup_barrel_before_n == 0 and pickup_barrel_wait_r == "ok"
                and pickup_barrel_count_r == "ok" and pickup_barrel_count_n == 1,
            "regicide_barrel_empty " .. tostring(pickup_barrel_before_n) .. " -> " .. tostring(pickup_barrel_count_n)
                .. "; click_obj(regicide_barrel_empty, 3) -> " .. tostring(pickup_barrel_result) .. " " .. tostring(pickup_barrel_detail))
        local applebarrel_target = t.player.by_symbol("loc", "mourning_orchard_applepile")
        -- SHOW AND SETTLE THE BACKPACK BEFORE THE FIRST use_on OF THE RUN:
        -- use_on's arming is a one-shot behind an unsettled tab press
        -- (docs/QUEST_AUTHORING.md's "use_on's backpack tab press is not
        -- settled before its arming" trap), and the sidebar here is
        -- wherever talkToElena's own dialogue/journal reads left it.
        t.ui.tab("inventory")
        t.ticks(2)
        t.exec("fillBarrel", t.player.use_on, "regicide_barrel_empty", applebarrel_target)
        -- mend1_poison.rs2's own objbox ("You scoop up a barrel full of the
        -- rotten apples.") -- a popup, not a chat-log line.
        t.expect("fillBarrel.message", t.chat.expect_text("You scoop up a barrel full of the rotten apples."))
        t.chat.close()
        local barrelfull_r, barrelfull_n = t.inv.count("applebarrel_full")
        local barrelempty_r, barrelempty_n = t.inv.count("regicide_barrel_empty")
        t.check("fillBarrel.result", barrelfull_r == "ok" and barrelfull_n == 1 and barrelempty_r == "ok" and barrelempty_n == 0,
            "applebarrel_full=" .. tostring(barrelfull_n) .. ", regicide_barrel_empty=" .. tostring(barrelempty_n)
                .. " (the empty barrel went into the pile)")

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
        t.player.walk_to(2484, 3373, 20)
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
        -- mend1_press_apples's own mesbox ("You use the apple press..."),
        -- then an objbox for the mash it produces -- both popups, neither a
        -- chat-log line t.msg.* can see (docs section 3's kind list).
        t.expect("pressApples.message",
            t.chat.expect_text("You use the apple press to crush your rotten apples."))
        t.exec("pressApples.continue", t.chat.continue_, true)
        t.chat.close()
        t.expect("pressApples.mash", t.inv.await("mourning_applebarrel_mush", 1, 10))
        local press_full_r, press_full_n = t.inv.count("applebarrel_full")
        t.check("pressApples.barrelUsed", press_full_r == "ok" and press_full_n == 0,
            "applebarrel_full=" .. tostring(press_full_n) .. " after the press (want 0: crushed into the mush)")

        -- ---- A second empty barrel for the coal tar (getNaphtha: "Grab
        -- another barrel"). The orchard's own spawn (m38_52.spawn:39) is a
        -- RESPAWN obj: ToriRSServer_WorldGroundTake re-arms it
        -- ^lootdrop_duration (200) ticks after the take
        -- (src/torirsserver/torirs_server_world.c:5911), so the player waits
        -- beside it and takes the new one. ----
        t.player.walk_to(2487, 3372, 20)
        local barrel_back_r, barrel_back_detail = t.await({
            level = function()
                local obj_r = t.world.obj_near("regicide_barrel_empty", 2)
                return obj_r == "ok"
            end,
            note = "orchard barrel respawn",
        }, 260)
        t.step("pickUpBarrel2.respawned", barrel_back_r == "ok" and "PASS" or "FAIL",
            "await(world.obj_near(regicide_barrel_empty, 2)) -> " .. tostring(barrel_back_r) .. " " .. tostring(barrel_back_detail))
        local barrel2_result, barrel2_detail = t.player.click_obj("regicide_barrel_empty", 3)
        local barrel2_wait_r = t.inv.await("regicide_barrel_empty", 1, 10)
        local barrel2_r, barrel2_n = t.inv.count("regicide_barrel_empty")
        t.check("pickUpBarrel2", barrel2_wait_r == "ok" and barrel2_r == "ok" and barrel2_n == 1,
            "click_obj(regicide_barrel_empty, 3) -> " .. tostring(barrel2_result) .. " " .. tostring(barrel2_detail)
                .. "; regicide_barrel_empty=" .. tostring(barrel2_n))
        pass_door("orchardOut", "mourning_orchard_fencegate_l", "mourning_orchard_fencegate_open_l", 2474, 3364, 2474, 3365, 2474, 3362,
            function(w) return w.z <= 3364 end, "outside the orchard, z <= 3364", 2)

        end
        do
        -- ---- Coal tar (regicide_bombcraft.rs2's regicide_collect_tar,
        -- gated open to this quest via ~mend1_naphtha_allowed). Of the three
        -- regicide_tar_collection placements (2223,3123 / 2263,3127 /
        -- 2294,3115) only 2263,3127 stands in the open Isafdar component the
        -- Arandar gate opens onto (2263,3128 beside it); 2223,3123 is behind
        -- the dense forest (regicide_cross_over*, Agility 56). Into Tirannwn
        -- by the gate and on to the tar with the orchard's second barrel. ----
        t.drive.camera(SHOT_YAW, 383, SHOT_ZOOM_OUTDOOR)
        t.exec("goto-arandarNorth.tar", t.player.goto_tile, 2386, 3337, 0)
        pass_arandar("arandarIn.tar", true)
        t.exec("goto-tarCollection", t.player.goto_tile, 2263, 3128, 0)
        local tar_loc_result, tar_loc_row = t.world.loc_near("regicide_tar_collection", 3)
        t.check("collectTar.present", tar_loc_result == "ok" and tar_loc_row.tile_x == 2263 and tar_loc_row.tile_z == 3127,
            "loc_near(regicide_tar_collection, 3) -> " .. tostring(tar_loc_result) .. " "
                .. (tar_loc_result == "ok"
                    and (tostring(tar_loc_row.tile_x) .. "," .. tostring(tar_loc_row.tile_z)
                        .. " L" .. tostring(tar_loc_row.level))
                    or tostring(tar_loc_row)) .. " (want the copy at 2263,3127)")
        t.exec("collectTar", t.player.click_loc, "regicide_tar_collection", 1, { at = { 2263, 3127 } })
        t.ticks(2)
        t.expect("collectTar.message", t.msg.expect("You fill the barrel with thick coal tar"))
        t.expect("collectTar.result", t.inv.await("regicide_barrel_tar", 1, 10))
        local tar_empty_r, tar_empty_n = t.inv.count("regicide_barrel_empty")
        t.check("collectTar.barrelUsed", tar_empty_r == "ok" and tar_empty_n == 0,
            "regicide_barrel_empty=" .. tostring(tar_empty_n) .. " after the tar (want 0: it is the tar barrel now)")

        -- ---- Coal: a genuine bring-along per quest-helper's own
        -- coal20OrNaphtha ItemRequirement (10-20 coal alongside the tar
        -- barrel). t.cheat is hollow (trap 12), so it is graded by hand. The
        -- free slots are counted first: a full pack drops a give silently. ----
        local coal_free_r, coal_free = free_slots()
        t.check("giveCoal.room", coal_free_r == "ok" and coal_free >= 9,
            "free backpack slots " .. tostring(coal_free) .. " (" .. tostring(coal_free_r) .. "; want >= 9 for 9 coal)")
        -- 9 coal: the still burned 7 in every scratch run of this route. The
        -- pack peaks above 28 if the coal is held from setup (mourner loot
        -- before the disguise is worn, the gnome's items, four toads), so it
        -- is still handed over here.
        -- lint: kit-give coal20OrNaphtha bring-along: non-stackable coal held from setup overflows the 28-slot pack before the HQ (mourner loot, gnome items, four toads), so it is handed over at the leg that burns it
        local coal_cheat_result, coal_cheat_detail = t.cheat("::give coal 9")
        local coal_wait_r, coal_wait_detail = t.inv.await("coal", 9, 10)
        local coal_r, coal_n = t.inv.count("coal")
        t.check("giveCoal", coal_cheat_result == "ok" and coal_wait_r == "ok" and coal_r == "ok" and coal_n == 9,
            "cheat(::give coal 9) -> " .. tostring(coal_cheat_result) .. " " .. tostring(coal_cheat_detail)
                .. "; inv.await(coal,9,10) -> " .. tostring(coal_wait_r) .. " " .. tostring(coal_wait_detail)
                .. "; coal=" .. tostring(coal_n))

        -- Out of Tirannwn by the gate; Rimmington (2927,3211) lies past the
        -- members' gate west of Falador, pressed below (member_gate).
        t.exec("goto-arandarSouth.still", t.player.goto_tile, 2386, 3331, 0)
        pass_arandar("arandarOut.still", false)

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
        t.exec("goto-memberGate.still", t.player.goto_tile, 2934, 3321, 0)
        member_gate("memberGateOut.still", false)
        t.exec("goto-still", t.player.goto_tile, 2927, 3213, 0)
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
            local settings_r, settings_v = t.var.server("varp331_regicide_still_settings")
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
            local total_r, total_v = t.var.server("varp330_regicide_still_total")
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
        -- t.ui.await_close is the same hollow-on-success shape as
        -- await_open above -- graded by hand; the Escape press's own status
        -- is only carried in the detail.
        local still_closed_result = t.ui.await_close("regicide_still")
        t.step("still.closed", still_closed_result == "ok" and "PASS" or "FAIL",
            "key(escape) -> " .. tostring(still_close_result) .. "; ui.await_close(regicide_still) -> "
                .. tostring(still_closed_result))
        t.expect("still.naphtha", t.inv.await("regicide_barrel_naphtha", 1, 10))

        end
        do
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
        local naphtha_left_r, naphtha_left_n = t.inv.count("regicide_barrel_naphtha")
        local mush_left_r, mush_left_n = t.inv.count("mourning_applebarrel_mush")
        t.check("useNaphthaOnBarrel.used", naphtha_left_r == "ok" and naphtha_left_n == 0 and mush_left_r == "ok" and mush_left_n == 0,
            "regicide_barrel_naphtha=" .. tostring(naphtha_left_n) .. " mourning_applebarrel_mush=" .. tostring(mush_left_n)
                .. " (want both 0: mixed into the naphtha apple mix)")

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
        local mix_left_r, mix_left_n = t.inv.count("mourning_applebarrel_naphtha_mush")
        t.check("useSieveOnBarrel.used", mix_left_r == "ok" and mix_left_n == 0,
            "mourning_applebarrel_naphtha_mush=" .. tostring(mix_left_n) .. " (want 0: strained; the sieve itself is kept, "
                .. "mend1_poison.rs2:459-460 deletes only the mix)")

        -- ---- Cook the toxic naphtha on a range -- not a fire. RE-AUTHOR
        -- (parity1o): any range works now (skill_cooking/scripts/
        -- cooking.rs2's [oplocu,_cooking_oven] hook calls
        -- ~mend1_heat_toxic_naphtha), driven here at the Mourner HQ's own
        -- range (2547,3322 -- symbol "range", the client's cooking-range
        -- category) instead of the underground Carnillean one. ----
        -- ANY-OF: cookNaphtha cookToxin the guide targets only carnilleanrange, but the content cooks the naphtha on ANY range via skill_cooking/scripts/cooking.rs2:11's [oplocu,_cooking_oven] hook (mend1_poison.rs2's ~mend1_heat_toxic_naphtha) -- cooked below at the Mourner HQ range as cookToxin.
        -- From Rimmington back through the members' gate, overland to East
        -- Ardougne's side of the city wall, through the wall door, in at the
        -- HQ's front door; the range (2547,3322-3323) stands in the front
        -- room, cooked from 2548,3323.
        t.exec("goto-memberGate.cook", t.player.goto_tile, 2934, 3318, 0)
        member_gate("memberGateIn.cook", true)
        t.exec("goto-ardougneWall.cook", t.player.goto_tile, 2561, 3299, 0)
        if not ardougne_wall("ardougneWallIn.cook", true) then
            return
        end
        t.exec("goto-enterMournerBaseForCook", t.player.goto_tile, 2551, 3320, 0)
        local cook_door_before_r, cook_door_before = t.world.tile()
        t.exec("enterMournerBaseForCook", t.player.click_loc, "mournerstewdoor", 1, { at = { 2551, 3320 } })
        t.ticks(4)
        local cook_door_after_r, cook_door_after = t.world.tile()
        t.check("enterMournerBaseForCook.walked_through",
            cook_door_before_r == "ok" and cook_door_after_r == "ok" and cook_door_after.z >= 3321,
            "tile before=" .. tile_text(cook_door_before_r, cook_door_before) .. " after=" .. tile_text(cook_door_after_r, cook_door_after)
                .. " (want inside the HQ front room, z >= 3321)")
        t.player.walk_to(2548, 3323, 10)
        local range_result, range_row = t.world.loc_near("range", 6)
        t.check("cookToxin.range_present", range_result == "ok",
            "loc_near(range, 6) -> " .. tostring(range_result) .. " "
                .. (range_result == "ok"
                    and (tostring(range_row.tile_x) .. "," .. tostring(range_row.tile_z)
                        .. " L" .. tostring(range_row.level) .. " match=" .. tostring(range_row.match))
                    or tostring(range_row)))
        t.drive.camera(SHOT_YAW, 383, SHOT_ZOOM_INDOOR)
        t.ui.tab("inventory")
        t.ticks(2)
        local range_target = t.player.by_symbol("loc", "range")
        t.exec("cookToxin", t.player.use_on, "mourning_toxic_naphtha", range_target)
        t.expect("cookToxin.message",
            t.chat.expect_text("You evaporate the naphtha and you're left with a powdery residue"))
        t.chat.close()
        t.expect("cookToxin.result", t.inv.await("mourning_apple_toxin", 2, 10))
        t.expect("cookToxin.barrel_back", t.inv.await("regicide_barrel_empty", 1, 4))
        local toxic_left_r, toxic_left_n = t.inv.count("mourning_toxic_naphtha")
        t.check("cookToxin.used", toxic_left_r == "ok" and toxic_left_n == 0,
            "mourning_toxic_naphtha=" .. tostring(toxic_left_n) .. " (want 0: evaporated on the range)")

        -- Out of the HQ by its front door (doors.rs2:20-21 walks the player
        -- out), along the street to the food-store hall (x 2522-2529 z
        -- 3312-3319, double doors w_ardougnedoubledoorl/r on 2525-2526,3311's
        -- north edge) and its store room (x 2517-2521 z 3312-3316, poordoor on
        -- 2521,3314's east edge).
        t.player.walk_to(2551, 3321, 10)
        local hq_out_before_r, hq_out_before = t.world.tile()
        -- the walk-through answers `timeout settle_after_click` (mesp_tail3
        -- row 248): graded on the tiles, the press's answer in the detail
        local hq_out_pr, hq_out_pd = t.player.click_loc("mournerstewdoor", 1, { at = { 2551, 3320 } })
        t.ticks(4)
        local hq_out_r, hq_out = t.world.tile()
        t.check("leaveHQ.cook", hq_out_r == "ok" and hq_out.z <= 3320,
            "click_loc(mournerstewdoor) -> " .. tostring(hq_out_pr) .. " " .. tostring(hq_out_pd) .. "; tile before="
                .. tile_text(hq_out_before_r, hq_out_before) .. " after=" .. tile_text(hq_out_r, hq_out)
                .. " (want the street, z <= 3320)")

        -- ---- Poison food store 1, West Ardougne (mourning_sack_full1,
        -- maps/m39_51.jl2:4218-4221 -> abs 2517,3312 / 2517,3315 / 2521,3316;
        -- mend1_poison.rs2:534-596, an [oploc1] and an [oplocu] on the same
        -- base). RE-AUTHOR: both stores share the SAME objbox line now
        -- ("You add the toxin to the grain, after a few seconds of mixing
        -- you can't tell the difference."). ----
        t.exec("goto-poisonStore1", t.player.goto_tile, 2525, 3310, 0)
        pass_door("storeHallIn", "w_ardougnedoubledoorl", "w_ardougnedoubledoorlopen", 2525, 3311, 2525, 3311, 2525, 3313,
            function(w) return w.z >= 3312 and w.x >= 2522 end, "inside the store hall, z >= 3312")
        pass_door("storeRoomIn", "poordoor", "poordooropen", 2521, 3314, 2522, 3314, 2520, 3314,
            function(w) return w.x <= 2521 and w.z >= 3312 and w.z <= 3316 end, "inside the store room, x <= 2521")
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
        t.ui.tab("inventory")
        t.ticks(2)
        local toxin1_before_r, toxin1_before = t.inv.count("mourning_apple_toxin")
        t.exec("poisonStore1", t.player.use_on, "mourning_apple_toxin", store1_target)
        t.expect("poisonStore1.message",
            t.chat.expect_text("You add the toxin to the grain"))
        t.chat.close()
        t.expect("poisonStore1.var", t.var.await_server("varb806_mourning_food_poison1", 1, 6))
        local toxin1_after_r, toxin1_after = t.inv.count("mourning_apple_toxin")
        t.check("poisonStore1.used", toxin1_before_r == "ok" and toxin1_after_r == "ok" and toxin1_before == 2 and toxin1_after == 1,
            "mourning_apple_toxin " .. tostring(toxin1_before) .. " -> " .. tostring(toxin1_after) .. " (want 2 -> 1)")
        pass_door("storeRoomOut", "poordoor", "poordooropen", 2521, 3314, 2521, 3314, 2523, 3314,
            function(w) return w.x >= 2522 end, "back in the store hall, x >= 2522")
        pass_door("storeHallOut", "w_ardougnedoubledoorl", "w_ardougnedoubledoorlopen", 2525, 3311, 2525, 3312, 2525, 3309,
            function(w) return w.z <= 3311 end, "on the street, z <= 3311")

        -- ---- Poison food store 2, the church stores (mourning_sack_full2,
        -- maps/m39_51.jl2:4222-4224 -> abs 2524,3285 / 2524,3288 / 2525,3288;
        -- mend1_poison.rs2:534-596). The second store poisoned writes
        -- ^mend1_learn_secret. The church is open onto West Ardougne's
        -- streets (one walkable component with the HQ's door). ----
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
        t.ui.tab("inventory")
        t.ticks(2)
        local toxin2_before_r, toxin2_before = t.inv.count("mourning_apple_toxin")
        t.exec("poisonStore2", t.player.use_on, "mourning_apple_toxin", store2_target)
        t.expect("poisonStore2.message",
            t.chat.expect_text("You add the toxin to the grain"))
        t.chat.close()
        t.expect("poisonStore2.var", t.var.await_server("varb807_mourning_food_poison2", 1, 6))
        t.expect("poisonStore2.stage7", t.var.await_server("varp517_mourning_quest", 7, 6))
        local toxin2_after_r, toxin2_after = t.inv.count("mourning_apple_toxin")
        t.check("poisonStore2.used", toxin2_before_r == "ok" and toxin2_after_r == "ok" and toxin2_before == 1 and toxin2_after == 0,
            "mourning_apple_toxin " .. tostring(toxin2_before) .. " -> " .. tostring(toxin2_after) .. " (want 1 -> 0)")

        end
        do
        -- ---- Report to Essyllt (mend1_essyllt_after_poison,
        -- mend1_disguise.rs2:570-592 -> %mourning_quest = ^mend1_report).
        -- "The mourners' plan", verbatim. ----
        -- Guide steps enterMournerBaseAfterPoison / enterMournerBasement-
        -- AfterPoison (Quest Helper MourningsEndPartI.java:462-468, sub-steps
        -- of talkToEssylltAfterPoison: ObjectID.MOURNERSTEWDOOR at
        -- 2551,3320,0 then ObjectID.MOURNING_HIDEOUT_TRAP_DOOR at
        -- 2542,3327,0). Both are real clicks, as on the first visit
        -- (enterMournerBase / enterBasement above): the door gate reads
        -- %mourning_mourner_disguise, already 1, and walks through silently;
        -- the trap door (mend1_disguise.rs2:136-143) p_teleports to
        -- mend1_hq_basement_coord, 2044,4628,0.
        t.exec("goto-enterMournerBaseAfterPoison", t.player.goto_tile, 2551, 3320, 0)
        hq_down("enterMournerBaseAfterPoison", "enterMournerBasementAfterPoison")
        t.exec("talkToEssylltAfterPoison", t.player.talk_to, "mourner_hideout_head_mourner", 1)
        t.exec("talkToEssylltAfterPoison-dialog", t.chat.play, {
            "npc:You are back already? How is the epidemic going?",
            "player:The epidemic? Oh, you mean the poisoning?",
            "npc:Subtle as a brick... Yes that, how is it going?",
            "player:It's all done.",
            "npc:This is good news, give it a few days and the slave pens will be full again.",
            "player:So have I proved my commitment now?",
            "npc:Yes I think you have. I guess you want to know what we're doing in the mines?",
            "player:Yes please.",
            "npc:A long time ago, Seren herself ordered the construction of a great temple deep beneath the earth. This temple guards a great power, one that is key to our plans.",
            "npc:We believe the temple lies beneath this city. That is why we are here.",
            "npc:Unfortunately, progress has been slower than we'd have liked. The slaves accidentally mined into some old caverns infested with beasts. They have caused us significant issues.",
            "player:I see. But how close are we to finding it?",
            "npc:It shouldn't be long now. We have started to see signs that we are near.",
            "npc:Anyway, now that you know about the temple, I have a new task for you to perform, deep within the mines.",
            "npc:Alas, one of the guards has taken the key to the mines to be copied. Report in regularly and I will see that you get a copy as soon as he gets back.",
            "player:Will do.",
            "npc:Now be on your way, I have other things to attend to.",
            "player:I should probably report this to Arianwyn.",
            "npc:What was that?",
            "player:Oh nothing.",
            "npc:Hmm, fair enough.",
        })
        t.chat.close()
        t.ticks(2)
        -- configs/all.varp's [mourning_quest] body is empty (no transmit=yes,
        -- see the bind banner at the top of this file), so every stage read
        -- below goes through the SERVER copy (t.var.server) or the journal,
        -- never t.quest.expect_stage, which would read a stale client 0.
        local report_r, report_v = t.var.server("varp517_mourning_quest")
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
                and journal_report_line:find("Essyllt revealed that the mourners are searching for a temple", 1, true) ~= nil,
            "journal_open(Mourning's End Part I) -> " .. tostring(journal_report_r)
                .. " first_line=" .. tostring(journal_report_line))
        t.ui.journal_close()

        end
        do
        -- ---- Arianwyn, Lletya -- the hand-in (mend1_shared.rs2:101-108,
        -- ~mend1_quest_complete). skill.snapshot() before the hand-in for
        -- the two documented xp rewards (mend1_shared.rs2:31:
        -- "40000 Thieving XP|25000 Hitpoints XP|Elf teleport crystal|Access
        -- to the Mourner HQ basement and Lletya"). ----
        -- Up the ladder, out of the HQ, out through the city wall, then the
        -- pass's Huge Gate and the trees into Lletya.
        hq_up("leaveBasement.afterPoison")
        t.exec("goto-ardougneWallWest.final", t.player.goto_tile, 2555, 3299, 0)
        if not ardougne_wall("ardougneWallOut.final", false) then
            return
        end
        t.exec("goto-arandarNorth.final", t.player.goto_tile, 2386, 3337, 0)
        pass_arandar("arandarIn.final", true)
        t.exec("goto-lletyaGate.final", t.player.goto_tile, 2303, 3193, 0)
        if not lletya_gate("lletyaIn.final", true) then
            return
        end
        -- walk_to presses the minimap, which reaches ~18 tiles: three hops
        -- (2324,3178 -> 2352,3171 in one press never moved, mesp_tail4 row 294)
        t.player.walk_to(2324, 3178, 80)
        t.player.walk_to(2340, 3172, 80)
        t.player.walk_to(2352, 3171, 80)
        local ari_r, ari_tile = t.world.tile()
        t.check("walk.arianwynFinal", ari_r == "ok" and math.abs(ari_tile.x - 2352) <= 2 and math.abs(ari_tile.z - 3171) <= 2,
            "walked from the trees to Arianwyn -> " .. tile_text(ari_r, ari_tile) .. " (want within 2 of 2352,3171)")
        local qp_before_r, qp_before = t.var.varp("varp101_qp")
        local crystal_before_r, crystal_before = t.inv.count("mourning_teleport_crystal_4")
        local skill_snapshot_r, skill_snapshot = t.skill.snapshot()
        t.exec("talkToArianwynFinal", t.player.talk_to, "mourning_arianwyn", 1)
        -- mend1_arianwyn_talk's ^mend1_report branch (mend1_shared.rs2:
        -- 143-156), "Reporting back to Arianwyn", verbatim -> ~mend1_quest_
        -- complete (the reward scroll/xp/crystal, no chat line of its own).
        t.exec("talkToArianwynFinal-dialog", t.chat.play, {
            "npc:How goes it",
            "player:I think I have the information you're after. It seems Iorwerth elves are searching for an ancient temple of some sort deep beneath West Ardougne.",
            "npc:Temple? That must be the Temple of Light!",
            "player:Temple of Light? What's the Temple of Light?",
            "npc:It was built by Seren herself to guard a dark and ancient power. This is not good friend, I fear Lord Iorwerth intends to use that power to summon the Dark Lord!",
            "player:I don't think they've found this temple yet, but they know it's down there.",
            "npc:Well then we still have time. Thank you for your help so far, we would never have discovered this without you. Now we must prepare for what comes next.",
        })
        t.chat.close()
        t.ticks(3)

        -- Completion is graded the same way makinghistory.lua/rovingelves.lua
        -- grade theirs when the quest's own varp is not transmitted: the
        -- server varp, the reward scroll, the quest-point delta and the
        -- journal, each written by hand instead of t.quest.expect_complete().
        local complete_r, complete_v = t.var.server("varp517_mourning_quest")
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

        local qp_after_r, qp_after = t.var.varp("varp101_qp")
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
        -- ~mend1_quest_complete (mend1_shared.rs2:28-30) adds a fresh
        -- mourning_teleport_crystal_4 beside Eluned's (whose Lletya op does
        -- nothing yet, so it is still a (4)).
        local crystal_after_r, crystal_after = t.inv.count("mourning_teleport_crystal_4")
        t.check("reward.teleport_crystal", crystal_before_r == "ok" and crystal_after_r == "ok"
                and crystal_after == crystal_before + 1,
            "mourning_teleport_crystal_4 " .. tostring(crystal_before) .. " -> " .. tostring(crystal_after) .. " (want +1)"
                .. "; skill.snapshot() -> " .. tostring(skill_snapshot_r))

        end
        t.finish(0)
        return
    end,
}
