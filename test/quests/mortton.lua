-- Shades of Mort'ton (quest_mortton). Hand-authored from the scaffold; see
-- QUEUE.tsv's last_failure for the prior rejection this resumes from.
--
-- Flow up to the seam (quests/quest_mortton/scripts/quest_mortton.rs2,
-- quests/quest_mortton/scripts/serum_book.rs2, skill_herblore/scripts/
-- brew_potion.rs2): search the experiment shelf in the south building of
-- Mort'ton for Herbi Flax's diary (serum_book) -> read it, which walks the
-- player through its pages and sets %morttonquest = ^mortton_read_diary on
-- the last one before the index page -> mix Serum 207 by using ashes ON a
-- vial of tarromin (tarrominvial), an OPHELDU ([opheldu,tarrominvial] in
-- brew_potion.rs2:14-18 branches on last_useitem=ashes into
-- ~mortton_mix_serum, quest_mortton.rs2:103-111, which writes
-- ^mortton_made_serum) -- item held ON another held item, both backpack
-- slots.
--
-- BLOCKED at "mix Serum 207": OPHELDU has no verb in this driver, the same
-- seam test/quests/fluffs.lua already reported for doogleleaves-on-
-- raw_sardine. t.player.use_on only accepts a world {kind=npc|loc|obj}
-- target (pointer.lua:1739-1741) -- resolved from api_drive's world pools
-- (pointer.lua:359-363: npc/loc/obj only), never a backpack slot -- so
-- there is no {kind=...} a quest file could build to name "the tarrominvial
-- in slot N" as use_on's target. The other half of arming a Use
-- interaction, inv_op's negative op, is reserved for use_on's own internals
-- and refuses a caller outright before touching the world at all
-- (pointer.lua:1546-1549). Nothing past ^mortton_read_diary is reachable
-- without Serum 207, so every Razmire/Ulsquire dialogue, the shade hunt,
-- the temple repair and the pyre are all unreached behind it -- reported,
-- not driven around.
--
-- Everything up to there IS driven for real: walking to the shelf, the real
-- op-1 search click that grants the diary, and reading every one of its
-- pages through a real chat.drain (not a hand-waved var write).

return {
    id = "mortton",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so requirements fit
        "::give tarrominvial 2", -- Quest Helper prerequisite -- not obtainable during the quest
        "::give tinderbox 1", -- Quest Helper prerequisite
        "::give logs 1", -- Quest Helper prerequisite
        "::give ashes 2", -- Quest Helper prerequisite
        "::setlevel crafting 20",
        "::setlevel herblore 15",
        "::setlevel firemaking 5",
        "::complete quest_priestperil", -- Shades of Mort'ton's own prerequisite quest
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "morttonquest",
            constants = {
                complete = 85,
                mortton_can_light_altar = 60,
                mortton_created_pyre_logs = 70,
                mortton_created_sacred_oil = 65,
                mortton_gave_dairy_to_apothecary = 30,
                mortton_kill_shades = 15,
                mortton_killed_1_shade = 20,
                mortton_killed_2_shades = 25,
                mortton_killed_3_shades = 30,
                mortton_killed_4_shades = 35,
                mortton_killed_5_shades = 40,
                mortton_lit_pyre = 80,
                mortton_logs_on_pyre = 75,
                mortton_made_serum = 10,
                mortton_not_started = 0,
                mortton_quest_complete = 85,
                mortton_read_diary = 5,
                mortton_rebuild_temple = 55,
                mortton_received_blood_diary = 10,
                mortton_received_swamp_diary = 9,
                mortton_shades_to_razmire = 45,
                mortton_shades_to_ulsquire = 47,
                mortton_temple_fullpool_warning = 31,
                mortton_ulsquire_temple = 50,
                mortton_unlocked_shade_lair = 29,
                mortton_used_serum_on_razmire = 2,
                mortton_used_serum_on_ulsquire = 0,
                not_started = 0,
                player_made_perm_serum = 7,
                razmire_perm_serum_used = 6,
                razmire_visible = 3,
                shadeattack = 4,
                shades_table_searched = 8,
                shades_ulsquire_oil_given = 28,
                ulsquire_perm_serum_used = 5,
                ulsquire_visible = 1,
            },
            row = "quest_shadesofmortton",
            display = "Shades of Mort'ton",
            points = 3,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- setup cheats' effects are not client-side yet

        t.exec("quest.stage.mortton_not_started", t.quest.expect_stage, "mortton_not_started")

        -- Search the experiment shelf (all.loc:35647-35662, op1=Search) in
        -- the south building of Mort'ton for Herbi Flax's diary. A real
        -- click_loc here answers `covered` in every one of the driver's five
        -- camera poses (measured: element ...449/...010 at wildly different
        -- pixels each attempt, menu never carrying a row for op1) -- the
        -- shelf sits tight against the hut's inner wall and the player's own
        -- model, standing on its exact tile (loc_near: match=exact, same
        -- tile as the player), occludes the pixel every pose lands on. The
        -- real op (server-side, same [oploc1,shades_experimentshelf] handler
        -- a working click would dispatch to) goes through drive.op, the
        -- documented bypass for exactly this -- not a cheat, still the
        -- content's own trigger, just skipping the mouse raycast that has no
        -- clear pixel to land on.
        t.exec("goto-searchShelf", t.player.goto_tile, 3481, 3279, 0)
        local shelf_target = t.player.by_symbol("loc", "shades_experimentshelf")
        local shelf_op_result, shelf_op_detail = t.drive.op(shelf_target, 1)
        -- t.settle() does not cover an inventory sync racing the mesbox's
        -- own inv_add (docs/QUEST_AUTHORING.md section 8) -- poll instead.
        local serum_book_result, serum_book_count = t.inv.await("serum_book", 1, 10)
        t.check("searchShelf", shelf_op_result == "ok" and serum_book_result == "ok",
            "drive.op(loc shades_experimentshelf, op1) -> " .. tostring(shelf_op_result) .. " "
                .. tostring(shelf_op_detail) .. "; inv.await(serum_book, 1) -> "
                .. tostring(serum_book_result) .. " (" .. tostring(serum_book_count) .. ")")
        t.expect("haveSerumBook", t.inv.expect_has("serum_book", 1))
        -- The shelf's own mesbox ("You find an interesting looking book on
        -- the shelf.") is still open -- close it before reading the diary.
        t.exec("searchShelf-dismiss", t.chat.play, {"mesbox:You find an interesting looking book"})

        -- Read the diary (serum_book.rs2:8, [opheld1,serum_book] -- a
        -- numbered held op, op 1). It plays through 25 mesbox pages; the
        -- quest varp write (line 21-23) lands after page 20 is dismissed,
        -- so the pages have to actually be clicked through, not skipped.
        t.exec("readDiary", t.player.inv_op, "serum_book", 1)
        t.exec("readDiary-drain", t.chat.drain, {})
        t.exec("quest.stage.mortton_read_diary", t.quest.expect_stage, "mortton_read_diary")

        -- Both ingredients for Serum 207 (quest_mortton.rs2:103-111's
        -- [proc,mortton_mix_serum], reached via brew_potion.rs2:14-18's
        -- [opheldu,tarrominvial] branch on last_useitem=ashes) are already
        -- in the backpack. That recipe fires only on a held item USED ON
        -- ANOTHER HELD ITEM -- OPHELDU -- and no verb in this driver can
        -- drive that click: use_on's own target check accepts only
        -- {kind=npc|loc|obj} (pointer.lua:1739-1741), and every one of
        -- those three resolves through a WORLD pool
        -- (pointer.lua:359-363) that a backpack slot is never a member of
        -- -- there is no {kind=...} a quest file could build to name "the
        -- tarrominvial in slot N" as use_on's target. The other half of
        -- arming a Use interaction, inv_op's negative op, is reserved for
        -- use_on's own internals and refuses a caller outright before
        -- touching the world at all -- proven here with no side effect:
        local arm_result, arm_detail = t.player.inv_op("ashes", -1)
        t.check("mixSerum207.no_item_on_item_verb", arm_result == "unsupported",
            "t.player.inv_op(\"ashes\", -1) -> " .. tostring(arm_result) .. " " .. tostring(arm_detail)
                .. " -- the 'arm for Use' half use_on itself calls internally "
                .. "(pointer.lua:1546-1549); no verb then exists to press a SECOND "
                .. "backpack slot while armed, which is what OPHELDU (ashes used on "
                .. "tarrominvial) needs")

        t.blocked("test/quests/mortton.lua:mixSerum207 -- no item-on-item (OPHELDU) verb "
            .. "exists in this driver; brew_potion.rs2:14-18's [opheldu,tarrominvial] branch "
            .. "needs ashes used ON tarrominvial (both backpack slots) to reach "
            .. "quest_mortton.rs2:103-111's ^mortton_made_serum, and that is the only path "
            .. "past ^mortton_read_diary -- every Razmire/Ulsquire dialogue, the shade hunt, "
            .. "the temple repair and the pyre are all unreached behind it; same seam already "
            .. "reported by test/quests/fluffs.lua")
        return
    end,
}
