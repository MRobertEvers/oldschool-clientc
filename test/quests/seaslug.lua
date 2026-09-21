-- Sea Slug (1 QP). Content:
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_seaslug/
-- OSRS-Content/osrs239-content/server/scripts/areas/ardougne_east/scripts/caroline.rs2
-- OSRS-Content/osrs239-content/server/scripts/areas/area_fishing_platform/scripts/holgart.rs2
-- OSRS-Content/osrs239-content/server/scripts/skill_cooking/scripts/dough.rs2
--
-- REAUTHORED after REJECTION (reviewer sonnet-b3, queue.py show seaslug).
-- The rejected attempt reached the Holgart-platform content_bug
-- (slugmenace_pages.rs2's [opnpc1,slug2_holgart_jeb] shadowing holgart.rs2's
-- own [opnpc1,holgartplatform]) but got there by `::give swamppaste 1` in
-- setup -- skipping the quest's own documented craft chain
-- (seaslug_journal.rs2:59-71: gather Swamp Tar, mix with Flour, heat on a
-- Fire) in violation of trap 16. The reviewer traced what that cheat had
-- been hiding: `rawswamppaste` (dough.rs2:102) is never converted to
-- `swamppaste` anywhere in this tree -- no `cooking_generic.dbrow` row, no
-- special-cased `[oplocu,_cooking_fire]`/`[oplocu,_cooking_oven]` branch --
-- so heating it on a fire falls through `~attempt_cook` to "You can't cook
-- that." This file drives the REAL chain (gather -> mix -> heat) with no
-- cheated deliverable, hits that heating step for real, and BLOCKS there --
-- earlier than the Holgart-platform seam the previous attempt reported,
-- exactly as the review asked for.
--
-- Prerequisites given in setup, none of them the quest's own deliverable:
-- Pot of flour (a bought good, not gathered by this quest), a tinderbox and
-- a log (the generic Firemaking tools the journal's own "heating the
-- mixture on a Fire" step requires -- lighting the fire is still driven by
-- a real click below, just as the swamp tar gather and the mix are).
return {
    id = "seaslug",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::give pot_flour 1",
        "::give tinderbox 1",
        "::give logs 1",
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
        t.ticks(3) -- setup's ::give cheats are not client-side yet
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
        -- Holgart actually wants.
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
        t.exec("fire.goto", t.player.goto_tile, 3206, 3233, 0)
        t.exec("seaslug.light_fire", t.player.use_item_on_item, "tinderbox", "logs")
        local fire_msg_result, fire_msg_detail = t.msg.await("The fire catches", 15)
        t.step("seaslug.fire_lit",
            fire_msg_result == "ok" and "PASS" or "FAIL",
            "msg.await('The fire catches', 15) -> " .. tostring(fire_msg_result)
                .. " " .. tostring(fire_msg_detail))

        local fire_target, fire_sym_result = t.player.by_symbol("loc", "fire")
        t.step("seaslug.find_fire_loc",
            fire_sym_result == "ok" and "PASS" or "FAIL",
            "by_symbol(loc, fire) -> " .. tostring(fire_sym_result))

        -- --------------------------- heat rawswamppaste on the fire: THE SEAM
        -- use_on settles on the mesbox that opens (a dialogue page differing
        -- from the one before the click), so its own `ok`/detail already
        -- carries the answer -- read it, do not guess it.
        local heat_result, heat_detail = t.player.use_on("rawswamppaste", fire_target)
        t.step("seaslug.heat_rawswamppaste",
            heat_result == "ok" and "PASS" or "FAIL",
            "use_on(rawswamppaste, fire) -> " .. tostring(heat_result)
                .. " (" .. tostring(heat_detail) .. ")")

        local cant_cook_result, cant_cook_detail = t.exec(
            "seaslug.confirm_cant_cook", t.chat.expect_text, "You can't cook that.")

        local raw_after_read, raw_after_count = t.inv.count("rawswamppaste")
        local swamppaste_read, swamppaste_count = t.inv.count("swamppaste")
        t.check("seaslug.rawswamppaste_still_raw",
            cant_cook_result == "ok" and raw_after_count == raw_count
                and swamppaste_read == "ok" and swamppaste_count == 0,
            string.format(
                "chat.expect_text(\"You can't cook that.\") -> %s (%s); " ..
                "rawswamppaste %s->%s(%s), swamppaste=%s(%s) -- " ..
                "skill_cooking/scripts/cooking.rs2:238-244 [proc,attempt_cook] " ..
                "db_find(cooking_generic:uncooked, rawswamppaste) returns null",
                tostring(cant_cook_result), tostring(cant_cook_detail),
                tostring(raw_count), tostring(raw_after_read), tostring(raw_after_count),
                tostring(swamppaste_read), tostring(swamppaste_count)))

        -- CONTENT BUG, not a driver seam: dough.rs2's own make_swamp_paste
        -- (dough.rs2:96-102) manufactures `rawswamppaste`, and Holgart
        -- (areas/area_fishing_platform/scripts/holgart.rs2:58-79) checks
        -- `inv_total(inv, swamppaste)` -- a DIFFERENT item -- but nothing in
        -- this tree ever turns one into the other. `attempt_cook`
        -- (skill_cooking/scripts/cooking.rs2:238-244) is the only place a
        -- heat source can act on a held item, and it works from a single
        -- table, `cooking_generic:uncooked` (skill_cooking/configs/
        -- cooking_generic.dbrow); that table has no row for `rawswamppaste`,
        -- so `db_find` returns null and every heat source (fire or range,
        -- [oplocu,_cooking_fire]/[oplocu,_cooking_oven], cooking.rs2:161-190)
        -- answers "You can't cook that." with the item untouched. No click
        -- sequence in this content pack can produce `swamppaste`, so Sea
        -- Slug's own documented craft chain (seaslug_journal.rs2:59-71) is
        -- unfinishable, and Holgart's boat (holgart.rs2:58) can never be
        -- repaired -- a seam earlier than, and a precondition for, the
        -- previously-reported Holgart-platform dialogue shadow.
        t.blocked("skill_cooking/scripts/cooking.rs2:238-244 [proc,attempt_cook] " ..
            "(db_find(cooking_generic:uncooked, rawswamppaste) -> null, mesbox " ..
            "\"You can't cook that.\"): rawswamppaste, made by dough.rs2:96-102's " ..
            "make_swamp_paste, has no row in skill_cooking/configs/" ..
            "cooking_generic.dbrow and no special-cased [oplocu,_cooking_fire]/" ..
            "[oplocu,_cooking_oven] branch anywhere in this tree, so it can never " ..
            "become swamppaste -- the item holgart.rs2:70 actually checks for. " ..
            "Sea Slug's own craft chain (seaslug_journal.rs2:59-71) is " ..
            "unfinishable by any click sequence in this content pack.")
        return
    end,
}
