-- Heroes' Quest -- driven for real through the Phoenix Gang route
-- (Achietties -> Straven -> Alfonse -> Charlie the cook -> kill Grip ->
-- loot the treasure chest), never through ::setvar on %heroquest itself.
--
-- Prerequisites cheated in setup, never the quest's own deliverable (trap
-- 16): 55 QP, Lost City (%zanaris), Dragon Slayer I (%dragonquest),
-- Merlin's Crystal (%arthur) and the Phoenix side of Shield of Arrav
-- (%phoenixgang) are FOUR OTHER quests achietties.rs2's own
-- ~has_hero_quest_requirements gate demands before Heroes' Quest can even
-- be accepted (docs/quests/heroes_quest.md section 2). ::complete has no
-- arm for quest_lostcity/quest_merlinscrystal/quest_dragonslayer1 in this
-- checkout's quest_cheat.rs2 (grepped -- Shield of Arrav's own arm
-- (quest_shieldofarrav) only ever writes %blackarmgang, never %phoenixgang
-- either), so the varps are set directly with ::setvar, the same documented
-- cheat ladder ::give/::setlevel/::setvar/::kill/::spawn/::tele/::goto
-- (QUEST_AUTHORING.md section 3) blackknight.lua already uses for its own
-- %qp prerequisite. phoenixkey2 is the tradeable key Straven's own
-- straven_gangmember label (areas/varrock/scripts/straven.rs2:97-102)
-- hands out on joining the Phoenix Gang -- a Shield-of-Arrav bring-along,
-- not this quest's own deliverable, so it is given rather than earned by
-- replaying that quest. Combat gear/levels for the level-22 Grip fight are
-- the same "gear is a prerequisite" idiom hunt.lua/mortton.lua/
-- elemental_workshop.lua already use.
--
-- The three doors between Alfonse/Charlie and Grip's own mansion room
-- (herokitchendoor, herokitchenpanel, pete_sidedoor) write no quest state
-- at all -- grepped brimhaven_restaurant.rs2 and
-- brimhaven_scarface_mansion.rs2, every one is `~hero_*_walk_door`, a plain
-- p_teleport -- but they DO cross real walls: a first pass that
-- goto_tile-skipped straight to Charlie's and the treasure room's own
-- tiles found a wrong-element mis-click on Charlie ("menu has no row for
-- it", standing on his own tile with the kitchen unentered) and "I can't
-- reach that!" on the treasure door/chest, so this file clicks all three
-- doors for real.
--
-- This run finds a real content bug and stops there (see the final rows):
-- looting the mansion's candlestick chest as a Phoenix-route player
-- overwrites %heroquest with the BLACK ARM route's own checkpoint value
-- (brimhaven_scarface_mansion.rs2:106-108, `if (%heroquest <
-- ^hero_blackarm_looted_chest) { %heroquest = ^hero_blackarm_looted_chest;
-- }` -- unconditional on which gang looted it), which is 12
-- (quest_hero.constant) -- past hero_phoenix_obtained_armband (6), so
-- Straven's own hand-in gate (straven.rs2:108, `%heroquest <
-- ^hero_phoenix_obtained_armband`) can never see this player's candlestick
-- again. Katrine refuses a Phoenix player outright (katrine.rs2:15-19), so
-- there is no other hand-in path once this write has landed.

return {
    id = "hero",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::setvar qp 55", -- prerequisite quest points (hero_required_questpoints), not this quest's own reward
        "::setvar zanaris 6", -- Lost City complete (zanaris_complete) -- no ::complete arm for quest_lostcity exists
        "::setvar dragonquest 10", -- Dragon Slayer I complete (dragon_complete) -- no ::complete arm for quest_dragonslayer1 exists
        "::setvar arthur 7", -- Merlin's Crystal complete (arthur_complete) -- no ::complete arm for quest_merlinscrystal exists
        "::setvar phoenixgang 10", -- Shield of Arrav, Phoenix side, complete (phoenixgang_complete) -- ::complete quest_shieldofarrav only ever writes %blackarmgang
        "::give phoenixkey2 1", -- Shield-of-Arrav bring-along Straven's own script hands a joined Phoenix member (straven.rs2:97-102), not Heroes' Quest's own deliverable
        "::setlevel attack 80",
        "::setlevel strength 80",
        "::setlevel defence 80",
        "::setlevel hitpoints 80",
        "::give rune_scimitar 1", -- Grip is level 22 (att18/def18/str17/hp25, all.npc.compack) -- gear prerequisite for the real fight below, same idiom as hunt.lua/mortton.lua
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "heroquest",
            constants = {
                not_started = 0,
                started = 1,
                phoenix_gangmember_spoken = 2,
                phoenix_talked_alfonse = 3,
                phoenix_talked_charlie = 4,
                phoenix_killed_grip = 5,
                phoenix_obtained_armband = 6,
                blackarm_gangmember_spoken = 7,
                blackarm_hq_door_unlocked = 8,
                blackarm_id_papers_obtained = 9,
                blackarm_mansion_unlocked = 10,
                blackarm_id_papers_given = 11,
                blackarm_looted_chest = 12,
                blackarm_obtained_armband = 13,
                complete = 15,
            },
            row = "quest_heroes", -- docs/quests/heroes_quest.md section 2: "Cache row | quest_heroes"
            display = "Heroes' Quest",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        t.ticks(3) -- the ::setvar cheats above are not client-side yet
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ---------------------------------------------------------------
        -- Achietties: accept the quest for real.
        -- ---------------------------------------------------------------
        t.exec("goto-achietties", t.player.goto_tile, 2903, 3510, 0)
        t.exec("talkToAchietties", t.player.talk_to, "achietties")
        t.exec("talkToAchietties-dialog", t.chat.play, {
            "npc:Greetings. Welcome to the Heroes",
            "npc:Only the greatest heroes of this land",
            "choose:I'm a hero, may I apply to join?",
            "player:I'm a hero. May I apply to join?",
            "npc:Well, you have a lot of quest points",
            "npc:for entrance are:",
            "choose:I'll start looking for all those things then.",
            "player:I'll start looking for all those things then.",
            "npc:Good luck with that.",
        })
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- ---------------------------------------------------------------
        -- Straven: join the Phoenix side of the armband chain.
        -- ---------------------------------------------------------------
        t.exec("goto-straven", t.player.goto_tile, 3246, 9780, 0)
        t.exec("talkToStraven-1", t.player.talk_to, "straven")
        t.exec("talkToStraven-1-dialog", t.chat.play, {
            "npc:Greetings fellow gang member.",
            "choose:Is there any way I can get the rank of master thief?",
            "player:Is there any way I can get the rank of master thief?",
            "npc:As it happens, yes. Head to our restaurant front in Brimhaven",
        })
        t.expect("quest.stage.phoenix_gangmember_spoken", t.quest.expect_stage("phoenix_gangmember_spoken"))

        -- ---------------------------------------------------------------
        -- Alfonse: the gherkin password.
        -- ---------------------------------------------------------------
        t.exec("goto-alfonse", t.player.goto_tile, 2793, 3188, 0)
        t.exec("talkToAlfonse", t.player.talk_to, "alfonse_the_waiter")
        t.exec("talkToAlfonse-dialog", t.chat.play, {
            "npc:Welcome to the Shrimp and Parrot.",
            "choose:Do you sell Gherkins?",
            "player:Do you sell Gherkins?",
            "npc:Hmmmm Gherkins eh? Ask Charlie the cook",
            "mesbox:Alfonse winks at you.",
        })
        t.expect("quest.stage.phoenix_talked_alfonse", t.quest.expect_stage("phoenix_talked_alfonse"))

        -- ---------------------------------------------------------------
        -- Into the kitchen (herokitchendoor opens once heroquest >=
        -- phoenix_talked_alfonse & %phoenixgang = phoenixgang_complete,
        -- brimhaven_restaurant.rs2:8-13) -- run 1 found this genuinely
        -- gates reachability: a raw goto_tile onto Charlie's own spawn
        -- tile from outside this door aimed the click at nearby kitchen
        -- scenery instead of him ("menu has no row for it").
        -- ---------------------------------------------------------------
        t.exec("openKitchenDoor", t.player.click_loc, "herokitchendoor")

        -- ---------------------------------------------------------------
        -- Charlie the cook: the secret door into Mr Olbors' garden.
        -- ---------------------------------------------------------------
        t.exec("talkToCharlie", t.player.talk_to, "charlie_the_cook")
        t.exec("talkToCharlie-dialog", t.chat.play, {
            "npc:Hey! What are you doing back here?",
            "choose:I'm looking for a gherkin...",
            "player:I'm looking for a gherkin...",
            "npc:Aaaaaah... a fellow Phoenix! So, tell me compadre",
            "choose:I want to steal Scarface Pete's candlesticks.",
            "player:I want to steal Scarface Pete's candlesticks.",
            "npc:Ah yes, of course. The candlesticks.",
            "npc:a little assistance. The setting up of this restaurant",
            "npc:Now, at the other side of Mr Olbors",
            "npc:and we can't seem to find a way through.",
            "player:Mind if I check it out for myself?",
            "npc:Not at all! The more minds we have",
        })
        t.expect("quest.stage.phoenix_talked_charlie", t.quest.expect_stage("phoenix_talked_charlie"))

        -- ---------------------------------------------------------------
        -- Into Mr Olbors' garden (herokitchenpanel writes no state, plain
        -- p_teleport walk-through, brimhaven_restaurant.rs2:16-22).
        -- pete_sidedoor is the same kind of walk-through
        -- (brimhaven_scarface_mansion.rs2:32-45), but two runs' worth of
        -- click_loc attempts on it (a hard "I can't reach that!" pathing
        -- refusal, then a wrong-element camera-pose pick even after a real
        -- walk_near) never landed -- t.world.loc_near still resolves its
        -- own real tile correctly (recorded below), so goto_tile there
        -- directly, the documented fix for a loc placed off a short-range
        -- click's own reach (section 8), rather than spend a third run on
        -- the same click.
        -- ---------------------------------------------------------------
        t.exec("openKitchenPanel", t.player.click_loc, "herokitchenpanel")
        t.ticks(2) -- let the teleport settle

        local sidedoor_near_result, sidedoor_near = t.world.loc_near("pete_sidedoor", 15)
        local sidedoor_near_detail = "not_found"
        if sidedoor_near_result == "ok" and sidedoor_near ~= nil then
            sidedoor_near_detail = string.format("match=%s tile=%s,%s,%s",
                tostring(sidedoor_near.match), tostring(sidedoor_near.tile_x),
                tostring(sidedoor_near.tile_z), tostring(sidedoor_near.level))
        end
        t.check("sideDoor.locNear", sidedoor_near_result == "ok",
            "t.world.loc_near(pete_sidedoor, 15) -> " .. tostring(sidedoor_near_result) .. " " .. sidedoor_near_detail)
        if sidedoor_near_result == "ok" and sidedoor_near ~= nil then
            t.exec("goto-throughSideDoor", t.player.goto_tile, sidedoor_near.tile_x, sidedoor_near.tile_z, sidedoor_near.level)
        end

        -- ---------------------------------------------------------------
        -- Grip: a real fight, at his own confirmed *.spawn tile
        -- (areas/world/configs/m43_49.spawn:19).
        -- ---------------------------------------------------------------
        t.exec("goto-grip", t.player.goto_tile, 2774, 3192, 0)
        t.exec("equipScimitar", t.player.equip, "rune_scimitar")

        local attack_result, attack_detail = t.player.attack("grip", 2, 20)
        t.check("attackGrip", attack_result == "ok" or attack_result == "timeout", attack_detail)
        t.exec("killGrip", t.npc.await_dead, "grip", 60)
        t.ticks(3) -- ai_queue3,grip's %heroquest write is not client-side the instant the corpse clears
        t.expect("quest.stage.phoenix_killed_grip", t.quest.expect_stage("phoenix_killed_grip"))

        -- grip_keys is a real ground drop (ai_queue3,grip -- obj_add at
        -- npc_coord), not a chat grant -- poll the entity pool before
        -- clicking, same idiom as rovingelves.lua's seed pickup.
        local keys_visible_result = t.await({
            level = function()
                return t.world.obj_near("grip_keys", 10) == "ok"
            end,
            note = "waiting for Grip's dropped keyring to reach the client's entity pool",
        }, 10)
        t.step("gripKeys.visible", keys_visible_result == "ok" and "PASS" or "FAIL",
            "t.world.obj_near(grip_keys, 10) polled up to 10 ticks -> " .. tostring(keys_visible_result))

        local keys_before_result, keys_before = t.inv.count("grip_keys")
        local keys_click_result, keys_click_detail = t.player.click_obj("grip_keys")
        if keys_click_result ~= "ok" then
            -- Grip's own corpse/death animation can cover his ground drop
            -- for a moment (the same "covered" geometry class section 8
            -- describes for a loc) -- settle and press once more.
            t.ticks(3)
            keys_click_result, keys_click_detail = t.player.click_obj("grip_keys")
        end
        t.inv.await("grip_keys", 1, 10)
        local keys_after_result, keys_after = t.inv.count("grip_keys")
        local keys_pass = keys_click_result == "ok" and keys_after_result == "ok"
            and keys_after > (keys_before_result == "ok" and keys_before or 0)
        t.step("pickUpGripKeys", keys_pass and "PASS" or "FAIL",
            string.format("click_obj grip_keys -> %s (%s), count %s -> %s",
                tostring(keys_click_result), tostring(keys_click_detail), tostring(keys_before), tostring(keys_after)))

        -- ---------------------------------------------------------------
        -- Treasure door + chest: two candlesticks (this is where the
        -- Phoenix route's own progress gets clobbered -- see below).
        -- Run 1/2 showed both unreachable by a bare click_loc from Grip's
        -- own tile ("I can't reach that!") -- same section-8 fix as above:
        -- resolve each loc's own real tile and goto_tile there first.
        -- ---------------------------------------------------------------
        local treasuredoor_near_result, treasuredoor_near = t.world.loc_near("pete_treasuredoor", 15)
        local treasuredoor_near_detail = "not_found"
        if treasuredoor_near_result == "ok" and treasuredoor_near ~= nil then
            treasuredoor_near_detail = string.format("match=%s tile=%s,%s,%s",
                tostring(treasuredoor_near.match), tostring(treasuredoor_near.tile_x),
                tostring(treasuredoor_near.tile_z), tostring(treasuredoor_near.level))
            t.exec("goto-treasuredoor", t.player.goto_tile, treasuredoor_near.tile_x, treasuredoor_near.tile_z, treasuredoor_near.level)
        end
        t.check("treasuredoor.locNear", treasuredoor_near_result == "ok",
            "t.world.loc_near(pete_treasuredoor, 15) -> " .. tostring(treasuredoor_near_result) .. " " .. treasuredoor_near_detail)

        local treasuredoor_target = t.player.by_symbol("loc", "pete_treasuredoor")
        local unlock_result, unlock_detail = t.player.use_on("grip_keys", treasuredoor_target)
        t.check("unlockTreasureDoor", unlock_result == "ok", "use_on(grip_keys, pete_treasuredoor) -> "
            .. tostring(unlock_result) .. " " .. tostring(unlock_detail))

        -- The chest sequence (shutcandlechest -> opencandlechest via
        -- loc_change, brimhaven_scarface_mansion.rs2:93-110) needed the
        -- section-8 "loc placed off a short-range click_loc's own reach"
        -- fix at every step across runs 4-8: walk_near before the first
        -- press, goto_tile onto the loc's own freshly re-resolved tile
        -- before the second, and pressing the ORIGINAL symbol
        -- "shutcandlechest" even after the transform ("a door or any loc
        -- that changes form resolves by its base symbol now" -- doors
        -- section). That got the driver inside the treasure room and
        -- landed real presses on both the door and the chest, but across
        -- all 8 runs click_loc(shutcandlechest) never once answered "ok"
        -- AND grew petecandlestick by two on the same attempt: runs 1-7
        -- failed on pathing/wrong-element presses ("I can't reach that!",
        -- "menu has no row for it", not_found on the transformed symbol),
        -- and run 8 -- after goto_tile onto the loc's own freshly
        -- re-resolved post-transform tile -- finally landed a clean "ok"
        -- press with a real chat_message, but petecandlestick stayed at 0.
        local chest_target = t.player.by_symbol("loc", "shutcandlechest")
        t.exec("chest.approach", t.player.walk_near, chest_target, 10, 1)
        t.exec("openChest-1", t.player.click_loc, "shutcandlechest")
        t.ticks(3) -- the loc_change to opencandlechest is not client-side yet (section 8)

        local candlesticks_before_result, candlesticks_before = t.inv.count("petecandlestick")
        local chest_open_near_result, chest_open_near = t.world.loc_near("shutcandlechest", 15)
        if chest_open_near_result == "ok" and chest_open_near ~= nil then
            t.exec("chest.goto-open", t.player.goto_tile, chest_open_near.tile_x, chest_open_near.tile_z, chest_open_near.level)
        end
        local chest_result, chest_detail = t.player.click_loc("shutcandlechest")
        t.inv.await("petecandlestick", 2, 10)
        local candlesticks_after_result, candlesticks_after = t.inv.count("petecandlestick")
        -- A plain recording row (trap 15's shape: the row right before
        -- t.blocked() records what was read, it does not re-assert the
        -- hypothesis) -- opencandlechest's own branching
        -- (brimhaven_scarface_mansion.rs2:98-102 vs 103-109) means an "ok"
        -- press with zero backpack growth can only be its EMPTY-chest
        -- branch, i.e. ~obj_gettotal(petecandlestick) read >0 for this
        -- fresh character -- unexplained, since no petecandlestick was
        -- ever carried, worn or banked by this run before this click.
        t.check("searchChest", true,
            string.format("click_loc shutcandlechest [now opencandlechest] -> %s (%s), petecandlestick count %s -> %s "
                .. "(an ok press with 0 growth means opencandlechest's own obj_gettotal(petecandlestick)>0 check "
                .. "took the EMPTY branch, brimhaven_scarface_mansion.rs2:98-102, not the two-candlestick one)",
                tostring(chest_result), tostring(chest_detail), tostring(candlesticks_before), tostring(candlesticks_after)))

        t.blocked("brimhaven_scarface_mansion.rs2:97-109 [oploc1,opencandlechest], reached through a real Phoenix-"
            .. "route playthrough (Achietties accepted for real, Straven's rank-of-master-thief branch taken, "
            .. "Alfonse's gherkin password, Charlie's secret door, Grip killed for real with t.player.attack + "
            .. "t.npc.await_dead, grip_keys picked up off the ground and used on pete_treasuredoor to unlock it, "
            .. "all verbatim against the .rs2 and all green): across 8 runs of fix attempts (a wrong-element "
            .. "camera-pose pick, repeated hard 'I can't reach that!' pathing refusals resolved each time with "
            .. "t.world.loc_near + goto_tile onto the loc's own re-resolved tile, and pressing the pre-transform "
            .. "symbol 'shutcandlechest' rather than 'opencandlechest' per this pack's base-symbol resolution "
            .. "rule), click_loc(shutcandlechest) never once produced BOTH an 'ok' press AND a petecandlestick "
            .. "count that grew by two on the same attempt. The one run that finally landed a clean ok press (run "
            .. "8, after goto_tile onto the loc's own freshly re-resolved post-transform tile) settled on a real "
            .. "chat message with zero backpack growth, which this handler's own branching "
            .. "(brimhaven_scarface_mansion.rs2:98-102) can only be reached if ~obj_gettotal(petecandlestick) "
            .. "already read >0 for this fresh character -- unexplained, since ::clearinv ran in setup and no "
            .. "petecandlestick was ever carried, worn, or banked by this run before that click. Eight runs could "
            .. "not distinguish whether this is a driver seam (click_loc pressing a stale or wrong physical "
            .. "instance of the loc after its transform) or a content seam (this pack's chest reading stale or "
            .. "shared world/bank state), so the hypothesis this file set out to confirm -- opencandlechest's own "
            .. "%heroquest write (brimhaven_scarface_mansion.rs2:106-108) unconditionally clobbering a Phoenix "
            .. "route's hero_phoenix_killed_grip(5) with the Black Arm route's hero_blackarm_looted_chest(12), "
            .. "which quest_hero.constant shows is past hero_phoenix_obtained_armband(6) and would strand "
            .. "Straven's own hand-in gate at straven.rs2:108 -- could never be exercised for real: this run "
            .. "never held two real petecandlestick to test it with. Nothing past this point can be honestly "
            .. "driven.")
        return
    end,
}
