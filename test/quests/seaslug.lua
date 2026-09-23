-- Sea Slug (1 QP). Content:
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_seaslug/
-- OSRS-Content/osrs239-content/server/scripts/areas/ardougne_east/scripts/caroline.rs2
-- OSRS-Content/osrs239-content/server/scripts/areas/area_fishing_platform/scripts/
--     holgart.rs2, kennith.rs2, kent.rs2, bailey.rs2
-- OSRS-Content/osrs239-content/server/scripts/skill_cooking/scripts/dough.rs2
--
-- RETRY after b44ce7a2d (queue.py show seaslug): both prior blockers are
-- gone. The cook fix (033d83f61f) added skill_cooking/configs/
-- cooking_generic.dbrow's cooking_generic_swamp_paste row (uncooked=
-- rawswamppaste, cooked=swamppaste, fire only, always succeeds), so this
-- file now asserts the FIXED cook instead of "You can't cook that.", and the
-- platform Holgart (holgart.rs2's [label,holgartplatform_talk], reached
-- through the base spawn symbol slug2_holgart_jeb per trap 19's fix)
-- answers. Two things the retry note called out: the committed setup gave
-- ONE log at Firemaking 1 (the light roll timed out on a full run) -- this
-- file stages ::setlevel firemaking 50 with five logs; and Kennith's spawn
-- (areas/world/configs/m43_51.spawn:42) is 2766,3288 on PLANE 1, reached
-- with goto_tile's own level=1 climb (doc section 2), never a click on the
-- seaslug_ladder loc (its own [oploc1,seaslug_ladder] gate is content the
-- goto bypass is FOR).
--
-- Chain driven for real, nothing cheated (trap 16): gather swamp tar (m49_49
-- ground spawns) -> mix with a bought pot of flour -> heat on a real fire ->
-- give Holgart the swamp paste -> sail to the Fishing Platform -> find
-- Kennith -> sail to the island and free Kent (who pulls a sea slug off the
-- player) -> sail back -> Bailey hands over an unlit torch -> gather damp
-- sticks + broken glass on the platform, dry the sticks with the glass, rub
-- them alight (needs the Firemaking level staged in setup) -> Kennith won't
-- go near the slugs even with a lit torch, so kick the loose panel open ->
-- Kennith needs a way down, so work the crane -> sail back to shore ->
-- Caroline's thanks (Oyster pearls + Fishing xp), driven end to end.
--
-- Prerequisites given in setup, none of them the quest's own deliverable:
-- a pot of flour (a bought good), a tinderbox and several logs (the generic
-- Firemaking tools the journal's own "heating the mixture on a Fire" step
-- and the torch-lighting step both need), and the Firemaking level itself
-- (raised, never the fire lit for us -- every fire/torch light below is
-- still a real click).
return {
    id = "seaslug",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel firemaking 50",
        "::give pot_flour 1",
        "::give tinderbox 1",
        "::give logs 5",
    },

    run = function(t)
        t.quest.bind({
            varp = "seaslugquest",
            constants = {
                not_started = 0,
                started = 1,
                spoken_holgart = 2,
                boat_repaired = 3,
                spoken_kennith = 4,
                sailed_kent = 5,
                spoken_kent = 6,
                lit_torch = 7,
                kennith_need_escape = 8,
                panel_opened = 9,
                need_kennith_path = 10,
                saved_kennith = 11,
                complete = 12,
            },
            row = "quest_seaslug", -- all.dbrow.compack:128
            display = "Sea Slug",
            points = 1,
        })
        t.ticks(3) -- setup's ::give/::setlevel cheats are not client-side yet
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ---------------------------------------------------- accept, Caroline
        -- caroline.rs2 [opnpc1,caroline] at %seaslugquest=not_started jumps
        -- straight to [label,caroline_help] (no separate accept branch), which
        -- opens with the PLAYER's own line (trap 18) -- drain walks the whole
        -- alternating player/npc run up to the p_choice2 options page without
        -- needing each line spelled out.
        t.exec("caroline.goto", t.player.goto_tile, 2716, 3302, 0)
        t.exec("caroline.greet", t.player.talk_to, "caroline")
        local drain1_result, drain1_detail = t.chat.drain({ stop_at = "options" })
        t.expect("caroline.drain_to_choice", drain1_result, drain1_detail)
        t.shot("caroline-choice-menu")

        t.exec("caroline.choose_help", t.chat.choose, "I suppose so, how do I get there?")
        local drain2_result, drain2_detail = t.chat.drain({ stop_at = "none" })
        t.expect("caroline.drain_close", drain2_result, drain2_detail)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- ---------------------------------------------------- gather swamp tar
        -- Swamp tar ground spawns south of Lumbridge (areas/world/configs/
        -- m49_49.spawn:71-87), NOT the m53_5x squares (those are Mort'ton's
        -- ghast swamp -- same obj symbol reused on a different map square,
        -- confirmed by the ghast_invis/willothewisp/mmsnail neighbours there).
        t.exec("swamp.goto", t.player.goto_tile, 3183, 3180, 0)
        local tar_before_result, tar_before_count = t.inv.count("swamp_tar")
        -- click_obj answers ok with a nil detail (trap 12's hollow rule) --
        -- call it directly and write the before/after count ourselves.
        local tar_pickup_result, tar_pickup_detail = t.player.click_obj("swamp_tar")
        local tar_after_result, tar_after_detail = t.inv.await("swamp_tar", 1, 10)
        local tar_read, tar_count = t.inv.count("swamp_tar")
        t.check("seaslug.gather_swamp_tar",
            tar_after_result == "ok" and tar_count >= 1,
            string.format(
                "click_obj(swamp_tar) -> %s (%s); before=%s after=%s(%s), await=%s %s",
                tostring(tar_pickup_result), tostring(tar_pickup_detail),
                tostring(tar_before_count), tostring(tar_read), tostring(tar_count),
                tostring(tar_after_result), tostring(tar_after_detail)))

        -- --------------------------------------------- mix flour + swamp tar
        -- dough.rs2's [opheldu,swamp_tar]/[opheldu,pot_flour] make_swamp_paste
        -- (dough.rs2:96-102): consumes both, hands back an empty pot and
        -- `rawswamppaste` -- the intermediate item, not the deliverable
        -- Holgart actually wants (heated on a fire, below).
        t.exec("seaslug.mix_swamp_paste", t.player.use_item_on_item, "swamp_tar", "pot_flour")
        local raw_read, raw_count = t.inv.count("rawswamppaste")
        t.check("seaslug.have_rawswamppaste",
            raw_read == "ok" and raw_count >= 1,
            "inv.count(rawswamppaste) -> " .. tostring(raw_read) .. " " .. tostring(raw_count)
                .. " -- dough.rs2:102 inv_add(inv, rawswamppaste, 1)")

        -- ---------------------------------------------------------- light a fire
        -- Back on open ground (the fixture's own start tile, beside Hans) --
        -- firemaking.rs2's [opheldu,tinderbox] with a held log arms tinderbox
        -- (item_a) and clicks the inventory log (item_b) -> @light_logs_inv,
        -- which drops the log at the player's own coord and, on a successful
        -- roll, loc_adds a `fire` (firemaking.rs2:139) a few ticks later.
        -- Firemaking 50 (staged in setup) plus five logs: retry the light up
        -- to three times against a missed roll rather than pin the run to one
        -- attempt, and record the OUTCOME once, not one row per try (trap 15).
        t.exec("fire.goto", t.player.goto_tile, 3206, 3233, 0)
        local fire_lit = false
        local fire_attempts = 0
        local fire_last_detail = nil
        while not fire_lit and fire_attempts < 3 do
            fire_attempts = fire_attempts + 1
            local logs_read, logs_count = t.inv.count("logs")
            if logs_read ~= "ok" or (logs_count or 0) < 1 then
                fire_last_detail = "no logs left (" .. tostring(logs_read) .. " " .. tostring(logs_count) .. ")"
                break
            end
            local light_result, light_detail = t.player.use_item_on_item("tinderbox", "logs")
            local msg_result, msg_detail = t.msg.await("The fire catches", 15)
            fire_last_detail = "attempt " .. fire_attempts .. ": use_item_on_item -> "
                .. tostring(light_result) .. " (" .. tostring(light_detail) .. "); msg.await -> "
                .. tostring(msg_result) .. " " .. tostring(msg_detail)
            if msg_result == "ok" then
                fire_lit = true
            end
        end
        t.step("seaslug.fire_lit", fire_lit and "PASS" or "FAIL", fire_last_detail)

        local fire_target, fire_sym_result = t.player.by_symbol("loc", "fire")
        t.step("seaslug.find_fire_loc",
            fire_sym_result == "ok" and "PASS" or "FAIL",
            "by_symbol(loc, fire) -> " .. tostring(fire_sym_result))

        -- ----------------------------------------- heat rawswamppaste: FIXED
        -- 033d83f61f added skill_cooking/configs/cooking_generic.dbrow's
        -- cooking_generic_swamp_paste row (rawswamppaste -> swamppaste, fire
        -- only, levelrequired=1, successchance always-passes idiom), so
        -- use_on now settles on the BACKPACK DIFF (trap 24: use_on is the one
        -- click verb that waits for it) rather than a "can't cook" mesbox.
        local heat_result, heat_detail = t.player.use_on("rawswamppaste", fire_target)
        t.step("seaslug.cook_swamppaste",
            heat_result == "ok" and "PASS" or "FAIL",
            "use_on(rawswamppaste, fire) -> " .. tostring(heat_result)
                .. " (" .. tostring(heat_detail) .. ")")

        local paste_after_result, paste_after_count = t.inv.await("swamppaste", 1, 10)
        local raw_after_read, raw_after_count = t.inv.count("rawswamppaste")
        local paste_read, paste_count = t.inv.count("swamppaste")
        t.check("seaslug.have_swamppaste",
            paste_after_result == "ok" and paste_count >= 1 and raw_after_count == 0,
            string.format(
                "inv.await(swamppaste, 1, 10) -> %s; rawswamppaste %s->%s(%s), swamppaste=%s(%s) -- " ..
                "skill_cooking/configs/cooking_generic.dbrow cooking_generic_swamp_paste",
                tostring(paste_after_result),
                tostring(raw_count), tostring(raw_after_read), tostring(raw_after_count),
                tostring(paste_read), tostring(paste_count)))

        -- --------------------------------------------------- give Holgart the paste
        -- Two separate conversations, by content design: the FIRST talk to
        -- Holgart (at any stage <= started) only asks for swamp paste and
        -- advances %seaslugquest to spoken_holgart (holgart.rs2's
        -- [label,holgart_wantboat]) -- it does not read the backpack at all.
        -- Only the SECOND talk (now that the stage is spoken_holgart) reaches
        -- [label,holgart_paste], which checks inv_total(inv, swamppaste) and
        -- hands it over.
        t.exec("holgart1.goto", t.player.goto_tile, 2720, 3306, 0)
        t.exec("holgart1.greet", t.player.talk_to, "holgartland")
        local hdrain1_result, hdrain1_detail = t.chat.drain({ stop_at = "none" })
        t.expect("holgart1.drain_close", hdrain1_result, hdrain1_detail)
        t.expect("quest.stage.spoken_holgart", t.quest.expect_stage("spoken_holgart"))

        t.exec("holgart2.greet", t.player.talk_to, "holgartland")
        local hdrain2_result, hdrain2_detail = t.chat.drain({ stop_at = "options" })
        t.expect("holgart2.drain_to_choice", hdrain2_result, hdrain2_detail)
        t.shot("holgart2-choice-menu")
        t.exec("holgart2.choose_board", t.chat.choose, "Okay, lets do it.")
        -- holgart_paste's own board-out (~board_ardougne_to_fishing_platform)
        -- is if_close then a few ticks later a fresh mesbox arrival page --
        -- the same reopen shape section 8 describes -- so drain twice: once
        -- for whatever is still open right after the choose, then again after
        -- a short buffer for the delayed arrival mesbox, rather than trust a
        -- single drain call not to stop on the transient close in between.
        local hdrain3_result, hdrain3_detail = t.chat.drain({ stop_at = "none" })
        t.expect("holgart2.drain_board", hdrain3_result, hdrain3_detail)
        t.ticks(3)
        local hdrain4_result, hdrain4_detail = t.chat.drain({ stop_at = "none" })
        t.check("holgart2.drain_arrival",
            hdrain4_result == "ok",
            "post-board drain -> " .. tostring(hdrain4_result) .. " " .. tostring(hdrain4_detail))
        t.expect("quest.stage.boat_repaired", t.quest.expect_stage("boat_repaired"))

        -- ---------------------------------------------- find Kennith, platform
        -- Kennith's own spawn (areas/world/configs/m43_51.spawn:42) is plane
        -- 1. Measured this run: a direct goto_tile(2766,3288,1) from the
        -- boat's arrival tile (2782,3273,0, ~22 tiles away across the whole
        -- structure) never moves at all (still at the arrival tile ten ticks
        -- later). world.loc_near(seaslug_ladder,...) found the ladder itself
        -- at 2784,3286,1 once (close to the arrival tile) but answered
        -- not_found on a later identical call -- a flaky scene-dependent
        -- read, not something to gate navigation on -- so the fix is a
        -- hardcoded closer intermediate hop at that same measured tile
        -- (an ordinary stairs plane-shift the pathfinder climbs on its own
        -- once the distance is short enough), then a second goto_tile the
        -- rest of the way. A click on the ladder from the top answers
        -- `covered` (its menu has no row at all from that side -- only
        -- Cancel/Walk here), so the ladder loc itself is never pressed.
        t.exec("kennith1.goto_ladder", t.player.goto_tile, 2784, 3286, 1)
        t.exec("kennith1.goto", t.player.goto_tile, 2766, 3288, 1)
        t.exec("kennith1.greet", t.player.talk_to, "kennith")
        local kdrain1_result, kdrain1_detail = t.chat.drain({ stop_at = "none" })
        t.expect("kennith1.drain_close", kdrain1_result, kdrain1_detail)
        t.expect("quest.stage.spoken_kennith", t.quest.expect_stage("spoken_kennith"))

        -- ------------------------------------------------- Holgart, platform
        -- Reached through the base spawn symbol (slug2_holgart_jeb,
        -- m43_51.spawn:25) resolving its own multinpc1=holgartplatform child
        -- (trap 19's fix -- slugmenace_pages.rs2 hands %slug2_npc_track1=0 to
        -- holgart.rs2's own [label,holgartplatform_talk]).
        -- slug2_holgart_jeb's own spawn (areas/world/configs/m43_51.spawn:25)
        -- is plane 0 -- seaslug_ladder_top's down-climb IS the generic
        -- climb_down_ladder category (quest_seaslug.rs2's own banner), so a
        -- plain goto_tile back down works, unlike the up-climb above.
        t.exec("holgart3.goto", t.player.goto_tile, 2782, 3276, 0)
        t.exec("holgart3.greet", t.player.talk_to, "holgartplatform")
        local hdrain5_result, hdrain5_detail = t.chat.drain({ stop_at = "none" })
        t.expect("holgart3.drain_board", hdrain5_result, hdrain5_detail)
        t.ticks(3)
        local hdrain6_result, hdrain6_detail = t.chat.drain({ stop_at = "none" })
        t.check("holgart3.drain_arrival",
            hdrain6_result == "ok",
            "post-board drain -> " .. tostring(hdrain6_result) .. " " .. tostring(hdrain6_detail))
        t.expect("quest.stage.sailed_kent", t.quest.expect_stage("sailed_kent"))

        -- --------------------------------------------------------- Kent, island
        -- kent.rs2's own branch (%seaslugquest=sailed_kent) opens with the
        -- NPC's line, not the player's (kent.rs2:11, the exception trap 18
        -- itself flags as "almost always") and closes with if_close, a
        -- p_delay(2), then a SEPARATE reopened dialogue ("Traveller wait!")
        -- that pulls the sea slug off the player -- section 8's reopen shape,
        -- driven as two chat.play calls either side of an explicit await.
        t.exec("kent.goto", t.player.goto_tile, 2793, 3321, 0)
        t.exec("kent.greet", t.player.talk_to, "kent")
        local kent1_result, kent1_detail = t.exec("kent.rescue_offer", t.chat.play, {
            "npc:Oh thank Saradomin",
            "player:Your wife sent me out",
            "npc:I knew the row boat wasn't sea worthy",
            "player:What's going on here",
            "npc:Five days ago we pulled in a huge catch",
            "npc:That's when the fishermen began to act strange",
            "npc:they attach themselves to your body",
            "npc:I told Kennith to hide until I returned",
            "npc:Please go back and get my boy",
            "end",
        })
        t.expect("quest.stage.spoken_kent", t.quest.expect_stage("spoken_kent"))

        -- kent.rs2:28-29: "Traveller wait!" ITSELF is if_close'd right after
        -- its own continue too (a second, nested reopen boundary), so
        -- t.chat.play's trailing readiness wait -- which resolves on the
        -- page CHANGING, and closing to "none" counts as a change -- can
        -- report "ok" the instant that transient close lands, well before
        -- the real reopen ("A few more minutes...") three ticks later
        -- mounts. Measured this run: a single list spanning both reopens
        -- read "the dialogue closed after 1 page(s)" at entry 2. Split every
        -- reopen into its own chat.play call either side of an explicit
        -- await (doc section 8's own worked pattern), never one list across
        -- an if_close.
        local reopen1_result, reopen1_detail = t.await({
            level = function() return t.chat.kind() ~= "none" end,
            note = "kent.slug_removal_reopen1",
        }, 10)
        t.step("kent.slug_removal_reopen1",
            reopen1_result == "ok" and "PASS" or "FAIL",
            "await chat reopen -> " .. tostring(reopen1_result) .. " " .. tostring(reopen1_detail))

        t.exec("kent.slug_removal_wait", t.chat.play, {
            "npc:Traveller wait",
            "end",
        })

        local reopen2_result, reopen2_detail = t.await({
            level = function() return t.chat.kind() ~= "none" end,
            note = "kent.slug_removal_reopen2",
        }, 10)
        t.step("kent.slug_removal_reopen2",
            reopen2_result == "ok" and "PASS" or "FAIL",
            "await chat reopen -> " .. tostring(reopen2_result) .. " " .. tostring(reopen2_detail))

        t.exec("kent.slug_removal", t.chat.play, {
            "npc:A few more minutes",
            "player:Yuck",
            "end",
        })

        -- ---------------------------------------- back to the platform, Bailey
        -- holgartsunkboat_talk's own branch: at any stage other than
        -- sailed_kent (we are spoken_kent now) it re-boards straight back to
        -- the platform -- the same reused board proc, if_close then a
        -- delayed reopen for the arrival mesbox.
        t.exec("holgartsunkboat.goto", t.player.goto_tile, 2799, 3320, 0)
        t.exec("holgartsunkboat.greet", t.player.talk_to, "holgartsunkboat")
        local sdrain1_result, sdrain1_detail = t.chat.drain({ stop_at = "none" })
        t.expect("holgartsunkboat.drain_board", sdrain1_result, sdrain1_detail)
        t.ticks(3)
        local sdrain2_result, sdrain2_detail = t.chat.drain({ stop_at = "none" })
        t.check("holgartsunkboat.drain_arrival",
            sdrain2_result == "ok",
            "post-board drain -> " .. tostring(sdrain2_result) .. " " .. tostring(sdrain2_detail))

        t.exec("bailey1.goto", t.player.goto_tile, 2765, 3276, 0)
        t.exec("bailey1.greet", t.player.talk_to, "bailey")
        local bdrain1_result, bdrain1_detail = t.chat.drain({ stop_at = "none" })
        t.expect("bailey1.drain_close", bdrain1_result, bdrain1_detail)
        -- trap 24: drain's own "ok" is the server's sentence, not the
        -- container update -- poll rather than read torch_unlit bare.
        -- inv.await's own second return is a detail string (ok/timeout),
        -- never a count (unlike inv.count) -- read the count back with a
        -- separate inv.count call.
        local torch_await_result, torch_await_detail = t.inv.await("torch_unlit", 1, 10)
        local torch_read, torch_count = t.inv.count("torch_unlit")
        t.check("bailey1.gave_torch",
            torch_await_result == "ok" and torch_read == "ok" and torch_count >= 1,
            "inv.await(torch_unlit, 1, 10) -> " .. tostring(torch_await_result) .. " "
                .. tostring(torch_await_detail) .. "; inv.count -> " .. tostring(torch_read) .. " "
                .. tostring(torch_count) .. " -- bailey.rs2 'Bailey gives you a torch.'")

        -- ------------------------------------------------- dry and light the torch
        -- Ground spawns on the platform itself (areas/world/configs/
        -- m43_51.spawn:61-63): broken_glass twice, damp_sticks once.
        t.exec("sticks.goto", t.player.goto_tile, 2784, 3289, 0)
        local sticks_pickup_result, sticks_pickup_detail = t.player.click_obj("damp_sticks")
        local sticks_await_result, sticks_await_detail = t.inv.await("damp_sticks", 1, 10)
        local sticks_after_result, sticks_after_count = t.inv.count("damp_sticks")
        t.check("seaslug.gather_damp_sticks",
            sticks_await_result == "ok" and sticks_after_result == "ok" and sticks_after_count >= 1,
            "click_obj(damp_sticks) -> " .. tostring(sticks_pickup_result) .. " (" ..
                tostring(sticks_pickup_detail) .. "); await -> " .. tostring(sticks_await_result)
                .. " " .. tostring(sticks_await_detail) .. "; count -> " .. tostring(sticks_after_result)
                .. " " .. tostring(sticks_after_count))

        t.exec("glass.goto", t.player.goto_tile, 2766, 3289, 0)
        local glass_pickup_result, glass_pickup_detail = t.player.click_obj("broken_glass")
        local glass_await_result, glass_await_detail = t.inv.await("broken_glass", 1, 10)
        local glass_after_result, glass_after_count = t.inv.count("broken_glass")
        t.check("seaslug.gather_broken_glass",
            glass_await_result == "ok" and glass_after_result == "ok" and glass_after_count >= 1,
            "click_obj(broken_glass) -> " .. tostring(glass_pickup_result) .. " (" ..
                tostring(glass_pickup_detail) .. "); await -> " .. tostring(glass_await_result)
                .. " " .. tostring(glass_await_detail) .. "; count -> " .. tostring(glass_after_result)
                .. " " .. tostring(glass_after_count))

        -- [opheldu,damp_sticks] arms damp_sticks (item_a) and clicks
        -- broken_glass (item_b) -- quest_seaslug.rs2:36-40.
        t.exec("seaslug.dry_sticks", t.player.use_item_on_item, "damp_sticks", "broken_glass")
        local dry_read, dry_count = t.inv.count("dry_sticks")
        t.check("seaslug.have_dry_sticks",
            dry_read == "ok" and dry_count >= 1,
            "inv.count(dry_sticks) -> " .. tostring(dry_read) .. " " .. tostring(dry_count)
                .. " -- quest_seaslug.rs2:46 inv_add(inv, dry_sticks, 1)")

        -- [opheld1,dry_sticks]: a held op (rub together), gated on Firemaking
        -- >= 30 (staged at 50) and a stat_random(firemaking, 64, 512) roll --
        -- retried up to three times against a miss, one OUTCOME row (trap 15).
        local torch_lit_ok = false
        local torch_attempts = 0
        local torch_last_detail = nil
        while not torch_lit_ok and torch_attempts < 3 do
            torch_attempts = torch_attempts + 1
            local dry_now_read, dry_now_count = t.inv.count("dry_sticks")
            if dry_now_read ~= "ok" or (dry_now_count or 0) < 1 then
                torch_last_detail = "no dry_sticks left (" .. tostring(dry_now_read) .. " "
                    .. tostring(dry_now_count) .. ")"
                break
            end
            local rub_result, rub_detail = t.player.inv_op("dry_sticks", 1)
            local lit_await_result, lit_await_detail = t.inv.await("torch_lit", 1, 10)
            local lit_result, lit_count = t.inv.count("torch_lit")
            torch_last_detail = "attempt " .. torch_attempts .. ": inv_op(dry_sticks,1) -> "
                .. tostring(rub_result) .. " (" .. tostring(rub_detail) .. "); torch_lit await -> "
                .. tostring(lit_await_result) .. " " .. tostring(lit_await_detail)
                .. "; count -> " .. tostring(lit_result) .. " " .. tostring(lit_count)
            if lit_await_result == "ok" and lit_result == "ok" and lit_count >= 1 then
                torch_lit_ok = true
            end
        end
        t.step("seaslug.light_torch", torch_lit_ok and "PASS" or "FAIL", torch_last_detail)
        t.expect("quest.stage.lit_torch", t.quest.expect_stage("lit_torch"))

        -- ---------------------------------------------------- Kennith, second talk
        -- Down at ground level for Bailey/sticks/glass since -- same
        -- too-far-for-one-hop seam as kennith1 above, so the same two-hop
        -- approach by the ladder tile again.
        t.exec("kennith2.goto_ladder", t.player.goto_tile, 2784, 3286, 1)
        t.exec("kennith2.goto", t.player.goto_tile, 2766, 3288, 1)
        t.exec("kennith2.greet", t.player.talk_to, "kennith")
        local kdrain2_result, kdrain2_detail = t.chat.drain({ stop_at = "none" })
        t.expect("kennith2.drain_close", kdrain2_result, kdrain2_detail)
        t.expect("quest.stage.kennith_need_escape", t.quest.expect_stage("kennith_need_escape"))

        -- ------------------------------------------------------- kick the panel
        -- quest_seaslug.rs2:69-79's own branch is all mes() log lines (no
        -- page at all) -- click_loc's settle reads the chat line itself.
        -- Measured this run: pressing straight from Kennith's own tile
        -- refused "I can't reach that!" from every approach tile click_loc's
        -- own retry tried -- find the panel's actual live tile first and
        -- stand right by it before pressing, the same fix the crane below
        -- needed.
        local panel_loc_result, panel_loc = t.world.loc_near("slug_breakable_panel", 15)
        t.step("seaslug.find_panel",
            panel_loc_result == "ok" and "PASS" or "FAIL",
            "world.loc_near(slug_breakable_panel, 15) -> " .. tostring(panel_loc_result) .. " "
                .. (panel_loc_result == "ok"
                    and (tostring(panel_loc.tile_x) .. "," .. tostring(panel_loc.tile_z) .. ","
                        .. tostring(panel_loc.level))
                    or "n/a"))
        if panel_loc_result == "ok" then
            t.exec("panel.goto", t.player.goto_tile, panel_loc.tile_x, panel_loc.tile_z, panel_loc.level)
        end
        -- Measured this run: pressing right after the goto_tile teleport
        -- answered `covered` with no row at all, the pixel hunt exhausted --
        -- the doc's own "covered right after a teleport on a target that
        -- worked twice already: t.ticks(2) first" (a scene that has not
        -- rebuilt yet, trap 21's Rockslide-in-Ardougne shape).
        t.ticks(2)
        local panel_before_result, panel_before_stage = t.quest.stage()
        local panel_click_result, panel_click_detail = t.player.click_loc("slug_breakable_panel", 1)
        local panel_msg_result, panel_msg_detail = t.msg.await("opening big enough for Kennith", 10)
        local panel_bypass_note = ""
        -- Measured across two runs: the normal press failed identically
        -- (`covered`, the full pixel hunt exhausted, 0 never hittested) from
        -- every approach tile click_loc itself tried -- doc section 8's own
        -- threshold for the last-resort bypass ("failed twice from two
        -- tiles"). This is never evidence a player could have pressed it
        -- from here (section 3's own banner on t.drive.op), so the detail
        -- says so.
        if panel_msg_result ~= "ok" and panel_loc_result == "ok" then
            local bypass_result, bypass_detail = t.drive.op(panel_loc, 1)
            panel_msg_result, panel_msg_detail = t.msg.await("opening big enough for Kennith", 10)
            panel_bypass_note = "; drive.op bypass (NOT evidence of reachability) -> "
                .. tostring(bypass_result) .. " " .. tostring(bypass_detail)
        end
        t.step("seaslug.kick_panel",
            panel_msg_result == "ok" and "PASS" or "FAIL",
            "click_loc(slug_breakable_panel,1) -> " .. tostring(panel_click_result) .. " ("
                .. tostring(panel_click_detail) .. "); msg.await -> " .. tostring(panel_msg_result)
                .. " " .. tostring(panel_msg_detail) .. "; stage before=" .. tostring(panel_before_stage)
                .. panel_bypass_note)
        t.expect("quest.stage.panel_opened", t.quest.expect_stage("panel_opened"))

        -- ---------------------------------------------------- Kennith, third talk
        -- Back to Kennith's own tile -- the panel click above walked us off
        -- to its own approach tile, and Kennith fell out of the client's
        -- entity pool from there on one run.
        t.exec("kennith3.goto", t.player.goto_tile, 2766, 3288, 1)
        t.exec("kennith3.greet", t.player.talk_to, "kennith")
        local kdrain3_result, kdrain3_detail = t.chat.drain({ stop_at = "none" })
        t.expect("kennith3.drain_close", kdrain3_result, kdrain3_detail)
        t.expect("quest.stage.need_kennith_path", t.quest.expect_stage("need_kennith_path"))

        -- ------------------------------------------------------ work the crane
        -- quest_seaslug.rs2:102-123: also all mes() log lines, but gated on a
        -- standing-distance check (coordz(coord) < coordz(movecoord(loc_coord,
        -- 0, 0, 3)), "I need to get closer to use that.") that click_loc's own
        -- approach-tile retry does not know about (that retry is for
        -- "I can't reach that!" pathing, not a content distance gate) -- find
        -- the crane's own live tile first and stand a few tiles off it on the
        -- side the check wants before pressing.
        local crane_loc_result, crane_loc = t.world.loc_near("seaslug_crane", 15)
        t.step("seaslug.find_crane",
            crane_loc_result == "ok" and "PASS" or "FAIL",
            "world.loc_near(seaslug_crane, 15) -> " .. tostring(crane_loc_result) .. " "
                .. (crane_loc_result == "ok"
                    and (tostring(crane_loc.tile_x) .. "," .. tostring(crane_loc.tile_z) .. ","
                        .. tostring(crane_loc.level))
                    or "n/a"))
        -- SEAM FIX seaslug_crane_standing_gate (2026-09-21): the gate at
        -- quest_seaslug.rs2:103 is a proximity test now, so the crane is
        -- worked the ordinary way -- stand on the deck SOUTH of it and press
        -- Rotate for real.  2772,3286 level 1 is a deck tile that touches the
        -- crane's footprint (x 2770-2773, z 3287-3290 on the server's plane
        -- 1); no drive.op bypass anywhere in this step, and the t.blocked the
        -- draft ended on here is gone.
        t.exec("crane.goto_deck", t.player.goto_tile, 2772, 3286, 1)
        t.exec("crane.rotate", t.player.click_loc, "seaslug_crane", 1)
        -- t.expect, not t.exec: msg.await answers ("ok", nil) and the hollow
        -- rule would FAIL an ok with no detail.
        t.expect("crane.kennith_lowered", t.msg.await("lower Kennith to the row boat", 12),
            "msg.await(\"lower Kennith to the row boat\")")

        t.expect("quest.stage.saved_kennith", t.quest.expect_stage("saved_kennith"))

        -- ----------------------------------------------------- back to shore
        t.exec("holgart4.goto", t.player.goto_tile, 2782, 3276, 0)
        t.exec("holgart4.greet", t.player.talk_to, "holgartplatform")
        local hdrain7_result, hdrain7_detail = t.chat.drain({ stop_at = "none" })
        t.expect("holgart4.drain_board", hdrain7_result, hdrain7_detail)
        t.ticks(3)
        local hdrain8_result, hdrain8_detail = t.chat.drain({ stop_at = "none" })
        t.check("holgart4.drain_arrival",
            hdrain8_result == "ok",
            "post-board drain -> " .. tostring(hdrain8_result) .. " " .. tostring(hdrain8_detail))

        -- ------------------------------------------------------- hand-in, Caroline
        local fishing_snapshot_result, fishing_snapshot = t.skill.snapshot()
        t.step("seaslug.fishing_xp_before_read",
            fishing_snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot before hand-in -> " .. tostring(fishing_snapshot_result))

        t.exec("caroline2.goto", t.player.goto_tile, 2716, 3302, 0)
        t.exec("caroline2.greet", t.player.talk_to, "caroline")
        local cdrain_result, cdrain_detail = t.chat.drain({ stop_at = "none" })
        t.expect("caroline2.drain_close", cdrain_result, cdrain_detail)

        -- Completion is asynchronous: caroline_savedkennith's own
        -- queue(seaslug_quest_complete, 0, 0) lands behind the dialogue's own
        -- closing lines (doc section 8's own load-bearing t.ticks(3)).
        t.ticks(3)

        t.quest.expect_complete() -- writes quest.varp_complete/quest.scroll_title/quest.points/quest.journal

        -- caroline.rs2's [queue,seaslug_quest_complete]: stat_advance(fishing,
        -- 71750) (7175 Fishing XP at the pack's 10x fixed-point scale) and
        -- inv_add(inv, bigoysterpearls, 1) -- the two rewards the scroll
        -- string itself lists, asserted literally (not read back from the
        -- scroll).
        t.expect("reward.fishing_xp",
            t.skill.expect_gain("fishing", 7175, fishing_snapshot))
        t.exec("reward.oyster_pearls", t.inv.expect_has, "bigoysterpearls", 1)

        t.finish(0)
    end,
}
