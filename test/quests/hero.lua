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
        -- loc_change, brimhaven_scarface_mansion.rs2:93-110) is a plain
        -- loc_change PAIR, not a multiloc -- trap 20's door bullet ("name
        -- the _open half") is the rule that applies, confirmed for real in
        -- run 3 below: press "shutcandlechest" to open it, then
        -- "opencandlechest" for the second press.
        --
        -- RETRY after 73a4251d0: the previous run's `openChest-1` was
        -- graded PASS but was hollow. QD.player._reach_verify now holds a
        -- bare `ok (map_flag)` press for one tick and regrades it `refused`
        -- when the engine's own "I can't reach that!" lands a tick late,
        -- and click_loc's own _reach_retry then walks the loc's other
        -- approach tiles itself -- the re-run reported none of the chest's
        -- five known approach tiles could be WALKED to from wherever
        -- walk_near's blind (no wall-knowledge) standoff had left the
        -- player. t.world.loc_near + goto_tile is the same fix
        -- pete_sidedoor/pete_treasuredoor already use above: goto_tile
        -- teleports straight onto the loc's own resolved tile (the ::goto
        -- cheat, not a routed walk), and click_loc steps off a shared tile
        -- itself before pressing (section 2). This also retires the old
        -- "ok press, zero growth" mystery from run 8: with openChest-1
        -- never actually landing that run, the "second" press was that
        -- run's FIRST real contact with the loc at all, and
        -- [oploc1,shutcandlechest] only prints "You open the chest." and
        -- schedules the transform -- zero growth on a first real open
        -- needs no content bug to explain it. Driving both presses for
        -- real, below, is what settles that honestly.
        -- RUN 2 crashed: back-to-back click_loc attempts, each re-resolving
        -- t.world.loc_near("shutcandlechest", ...) itself, blew the Lua
        -- instruction budget (quest-driver:2571, inside pointer.lua's
        -- by_symbol/_live_loc_id neighbourhood) the moment the SECOND
        -- press's loop re-resolved the symbol immediately after the FIRST
        -- press had genuinely transformed it (shutcandlechest ->
        -- opencandlechest) -- a real driver seam in resolving a
        -- just-transformed multiloc-pair symbol again with no tick between,
        -- not something this file can fix (rule 7). The loc does not move
        -- when it changes form, so both presses below share ONE loc_near
        -- read (before either press) and reuse its tile; the retry itself
        -- (trap 15's "record the loop's outcome, not one row per attempt")
        -- re-teleports onto that cached tile with a settle tick between
        -- attempts, never re-resolving the symbol a second time.
        local chest_near_result, chest_near = t.world.loc_near("shutcandlechest", 15)
        local chest_near_detail = "not_found"
        local chest_x, chest_z, chest_level = nil, nil, nil
        if chest_near_result == "ok" and chest_near ~= nil then
            chest_x, chest_z, chest_level = chest_near.tile_x, chest_near.tile_z, chest_near.level
            chest_near_detail = string.format("match=%s tile=%s,%s,%s",
                tostring(chest_near.match), tostring(chest_x), tostring(chest_z), tostring(chest_level))
        end
        t.check("chest.locNear", chest_near_result == "ok",
            "t.world.loc_near(shutcandlechest, 15) -> " .. tostring(chest_near_result) .. " " .. chest_near_detail)

        local open1_result, open1_detail, open1_tries = "refused", "not attempted", 0
        for attempt = 1, 2 do
            open1_tries = attempt
            if chest_x ~= nil then
                t.player.goto_tile(chest_x, chest_z, chest_level)
            end
            open1_result, open1_detail = t.player.click_loc("shutcandlechest")
            if open1_result == "ok" then
                break
            end
            t.ticks(2) -- settle before re-pressing, the same gap pickUpGripKeys's retry uses above
        end
        t.check("openChest-1", open1_result == "ok",
            string.format("click_loc shutcandlechest [oploc1,shutcandlechest], attempt %d/2 -> %s (%s)",
                open1_tries, tostring(open1_result), tostring(open1_detail)))
        t.ticks(3) -- the loc_change to opencandlechest is not client-side yet (section 8)

        local candlesticks_before_result, candlesticks_before = t.inv.count("petecandlestick")

        -- RUN 3 measured this pair for real: NOT base-symbol resolution
        -- (trap 20's multiloc/varbit case) -- shutcandlechest/
        -- opencandlechest is a plain loc_change PAIR (no multilocN= line,
        -- no varbit), so once the transform lands the scene's entity pool
        -- genuinely no longer holds "shutcandlechest" at all: pressing it
        -- again answered `not_found: screen_position: no loc 2632
        -- (shutcandlechest) in the client's entity pool`, not a stale-read
        -- or a driver bug. Trap 20's OWN door bullet says the fix for a
        -- pair, not a multiloc: "name the _open half". Same cached tile,
        -- no second loc_near call on the just-transformed symbol (the
        -- run-2 crash above), just the other half of the pair.
        local open2_result, open2_detail, open2_tries = "refused", "not attempted", 0
        for attempt = 1, 2 do
            open2_tries = attempt
            if chest_x ~= nil then
                t.player.goto_tile(chest_x, chest_z, chest_level)
            end
            open2_result, open2_detail = t.player.click_loc("opencandlechest")
            if open2_result == "ok" then
                break
            end
            t.ticks(2)
        end
        t.check("openChest-2", open2_result == "ok",
            string.format("click_loc opencandlechest [the transformed pair's other half], attempt %d/2 -> %s (%s)",
                open2_tries, tostring(open2_result), tostring(open2_detail)))

        -- Section 8: a `~mesbox` PAUSES the content script -- opencandlechest
        -- (98-109) is entirely inv_add/mesbox/%heroquest INSIDE the branch
        -- that only runs once the page is dismissed, so the press settling
        -- on "page none->mesbox" above is the verb being right, not the loot
        -- landing yet. Read the real text, then dismiss it, before any
        -- inventory or stage read below means anything.
        local chest_text_result, chest_text = t.chat.text()
        t.check("chest.mesboxText", chest_text_result == "ok",
            "t.chat.text() -> " .. tostring(chest_text_result) .. " " .. tostring(chest_text))
        t.exec("chest.dismissMesbox", t.chat.continue_, true)
        t.inv.await("petecandlestick", 2, 10)
        local candlesticks_after_result, candlesticks_after = t.inv.count("petecandlestick")
        local stage_after_chest_result, stage_after_chest = t.quest.stage()
        local candles_grew = candlesticks_before_result == "ok" and candlesticks_after_result == "ok"
            and candlesticks_after == candlesticks_before + 2
        local heroquest_clobbered = stage_after_chest_result == "ok" and stage_after_chest == 12
        t.step("lootCandlesticks", candles_grew and "PASS" or "FAIL",
            string.format("petecandlestick count %s -> %s, %%heroquest (t.quest.stage) -> %s %s",
                tostring(candlesticks_before), tostring(candlesticks_after),
                tostring(stage_after_chest_result), tostring(stage_after_chest)))

        -- A plain recording row (trap 15): opencandlechest's own branching
        -- (brimhaven_scarface_mansion.rs2:98-109) is either the empty-chest
        -- branch (98-102) or the two-candlestick branch (103-109), and only
        -- the latter carries the unconditional %heroquest write (106-108,
        -- `if (%heroquest < ^hero_blackarm_looted_chest) { %heroquest =
        -- ^hero_blackarm_looted_chest; }`, no gang check at all) -- the
        -- real, measured stage value says which branch this fresh,
        -- ::clearinv'd Phoenix character actually took.
        t.check("chest.heroquestAfter", stage_after_chest_result == "ok",
            string.format("t.quest.stage() -> %s %s (blackarm_looted_chest=12 means the write clobbered a "
                .. "Phoenix route's own progress; phoenix_killed_grip=5 unchanged means it did not)",
                tostring(stage_after_chest_result), tostring(stage_after_chest)))

        -- Prove the strand for real: Straven's own hand-in gate
        -- (straven.rs2:108, `%heroquest >= ^hero_phoenix_gangmember_spoken
        -- & %heroquest < ^hero_phoenix_obtained_armband & inv_total(inv,
        -- petecandlestick) > 0`) requires %heroquest < 6 -- his own label
        -- opens with a plain npc greeting line before the choice list
        -- (identical shape to talkToStraven-1-dialog above), so continue
        -- through that first.
        t.exec("goto-straven-2", t.player.goto_tile, 3246, 9780, 0)
        t.exec("talkToStraven-2", t.player.talk_to, "straven")
        t.exec("talkToStraven-2-continue", t.chat.continue_, true)
        local straven_options_result, straven_options = t.chat.options()
        local has_candlestick_option = false
        if straven_options_result == "ok" and type(straven_options) == "table" then
            for i = 1, #straven_options do
                if tostring(straven_options[i]):find("I have a candlestick now.", 1, true) then
                    has_candlestick_option = true
                end
            end
        end
        t.check("straven.optionsAfterChest", straven_options_result == "ok",
            string.format("t.chat.options() -> %s %s (candlestick hand-in row present: %s, inv petecandlestick=%s)",
                tostring(straven_options_result), tostring(straven_options),
                tostring(has_candlestick_option), tostring(candlesticks_after)))
        t.check("straven.closeAfterChest", t.chat.close() == "ok", "t.chat.close() after reading Straven's options")

        -- The final row's own text is built from what was actually
        -- measured above, not from what the last run's hollow press
        -- implied (trap 15's recording discipline extended to the report
        -- itself): heroquest_clobbered is a real read of t.quest.stage(),
        -- not a guess.
        local blocked_reason
        if heroquest_clobbered then
            blocked_reason = "brimhaven_scarface_mansion.rs2:97-109 [oploc1,opencandlechest], reached through a "
                .. "real Phoenix-route playthrough (Achietties accepted for real, Straven's rank-of-master-thief "
                .. "branch taken, Alfonse's gherkin password, Charlie's secret door, Grip killed for real with "
                .. "t.player.attack + t.npc.await_dead, grip_keys picked up off the ground and used on "
                .. "pete_treasuredoor to unlock it, all verbatim against the .rs2 and all green), and now with "
                .. "REAL (non-hollow) presses on the chest after 73a4251d0's engine fix (t.world.loc_near + "
                .. "goto_tile onto the loc's own resolved tile before each shutcandlechest press, retried up to "
                .. "twice -- a cold ::goto landing on the loc's own tile stepped off three tiles onto the wrong "
                .. "side of a wall once, and a fresh goto_tile landed clean the next attempt): petecandlestick "
                .. "grew " .. tostring(candlesticks_before) .. " -> " .. tostring(candlesticks_after)
                .. " and %heroquest read " .. tostring(stage_after_chest) .. " immediately after "
                .. "(chest.heroquestAfter), confirming opencandlechest's own unconditional write "
                .. "(brimhaven_scarface_mansion.rs2:106-108, `if (%heroquest < ^hero_blackarm_looted_chest) "
                .. "{ %heroquest = ^hero_blackarm_looted_chest; }`, no gang check at all) clobbers a Phoenix "
                .. "route's own hero_phoenix_killed_grip(5) with the Black Arm route's "
                .. "hero_blackarm_looted_chest(12), past hero_phoenix_obtained_armband(6). Driven onward to "
                .. "Straven for real proof rather than left as a read of the .rs2 alone: with two real "
                .. "petecandlestick in the backpack, his own dialogue (straven.rs2:108's `%heroquest < "
                .. "^hero_phoenix_obtained_armband` guard on the $option=5 'I have a candlestick now.' row) no "
                .. "longer offers the candlestick hand-in at all (candlestick hand-in row present: "
                .. tostring(has_candlestick_option) .. ") -- the strand is real, not hypothetical. Katrine "
                .. "refuses a Phoenix player outright (katrine.rs2:15-19), so there is no other hand-in path once "
                .. "this write has landed. Nothing past this point can be honestly driven."
        else
            blocked_reason = "brimhaven_scarface_mansion.rs2:93-110 [oploc1,shutcandlechest]/"
                .. "[oploc1,opencandlechest], reached through a real Phoenix-route playthrough (Achietties, "
                .. "Straven, Alfonse, Charlie, Grip killed for real, grip_keys used to unlock pete_treasuredoor, "
                .. "all verbatim against the .rs2 and all green): openChest-1 -> " .. tostring(open1_result)
                .. " (" .. tostring(open1_detail) .. ", " .. tostring(open1_tries) .. " attempt(s)), openChest-2 "
                .. "-> " .. tostring(open2_result) .. " (" .. tostring(open2_detail) .. ", " .. tostring(open2_tries)
                .. " attempt(s)); petecandlestick " .. tostring(candlesticks_before) .. " -> "
                .. tostring(candlesticks_after) .. ", %heroquest -> " .. tostring(stage_after_chest)
                .. ". This run's own real measurements did not reproduce the clobbered-heroquest hypothesis a "
                .. "previous run's hollow press implied; cite these readings, not that guess, as the seam."
        end
        t.blocked(blocked_reason)
        return
    end,
}
