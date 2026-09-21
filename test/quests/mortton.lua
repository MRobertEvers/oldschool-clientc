-- Shades of Mort'ton (quest_mortton). Hand-authored from the scaffold; see
-- QUEUE.tsv's last_failure for the prior rejections this resumes from.
--
-- RETRY after b5b720b49 cleared two driver seams: t.player.use_item_on_item
-- now exists for OPHELDU (mixSerum207), and t.player.click_loc steps off a
-- loc's own tile and walks its far sides, so the shelf search no longer
-- needs the drive.op bypass. Both are driven for real below, and both work.
--
-- CONTENT_BUG at mixSerum207, once those two driver seams were out of the
-- way: mixing Serum 207 (ashes used ON tarrominvial -- OPHELDU,
-- brew_potion.rs2:14-18 -> quest_mortton.rs2:103-111's
-- ~mortton_mix_serum -> brew_potion.rs2:503-548's ~attempt_brew_potion)
-- answers `ok` with the backpack UNCHANGED and the mesbox "You need
-- another ingredient to make this potion." (run 1's ledger row 10) instead
-- of producing mort_serum3 -- so ^mortton_made_serum is never written and
-- nothing past ^mortton_read_diary is reachable (confirmed: run 1 drove
-- the Razmire/Ulsquire/shade/temple flow anyway with the item that was
-- supposed to exist and every single row from there failed against the
-- unchanged ^morttonquest=5).
--
-- Root cause, traced through the real engine, not guessed: herblore_serum207
-- (skill_herblore/configs/brewing/brew.dbrow:495-502) is a plain two-
-- ingredient row (ingredient=ashes, solvent=tarrominvial, product=
-- mort_serum3) with NO `data=ingredient2` line -- optional per
-- herblore.dbtable:24 (`column=ingredient2,obj`, no REQUIRED). ~get_brew_data
-- (skill_herblore/scripts/herblore.rs2:18-35) finds this row correctly (its
-- own $message, "You mix the ashes into your potion.", is what a working
-- mix would show; the "need another ingredient" text is attempt_brew_potion's
-- OWN line at brew_potion.rs2:540, reachable only when
-- `$ingredient2 ! null`). `null` compiles to -1 for every namedobj/obj
-- literal (src/serverscript/ssc_symbols.c:1366), but
-- SS_OP_DB_GETFIELD's fallback for a column no row (and no table default=)
-- ever set pushes a plain zero, not -1 (src/torirsserver/
-- torirs_server_ops_db.c:242-267: `source_count == 0` -> `column->defaults`,
-- itself empty, so `offset >= source_count` -> `SSVM_PushInt(state, 0)`).
-- `def_namedobj $ingredient2 = db_getfield(...)` therefore reads 0, and
-- `0 ~= -1` is true -- every optional-ingredient2 dbrow row in this whole
-- content pack (herblore_sara_brew at brew.dbrow:446-454 is the only row
-- that ever sets ingredient2 at all) hits this the same way. Not a driver
-- seam -- use_item_on_item arms and clicks the right two backpack slots
-- (ledger row 10: "ashes (slot 4) on tarrominvial (slot 0)") and the
-- server answers for real; the content/engine's own optional-column
-- default is what is wrong. Outside test/quests/mortton.lua entirely (a
-- fix is either an engine default-typing change in
-- torirs_server_ops_db.c or a `data=ingredient2,null`-shaped default in
-- brew.dbrow), so reported rather than worked around.
--
-- Everything up to there IS driven for real: walking to the shelf, the
-- real op-1 search click that grants the diary (click_loc, no bypass),
-- reading every one of its pages through a real chat.drain, and the real
-- OPHELDU click that mixes the serum -- it is the CONTENT behind that
-- click, not the click, that is broken.

return {
    id = "mortton",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so requirements fit
        "::give tarrominvial 2", -- Quest Helper prerequisite -- not obtainable during the quest
        "::give tinderbox 1", -- Quest Helper prerequisite
        "::give logs 1", -- Quest Helper prerequisite
        "::give ashes 2", -- Quest Helper prerequisite
        "::give hammer 1", -- Quest Helper prerequisite -- needed at the temple wall (trap 16: a tool, not the quest's own deliverable)
        "::give rune_scimitar 1", -- combat gear prerequisite for the five shades (trap 16), same idiom as hunt.lua
        "::setlevel crafting 20",
        "::setlevel herblore 15",
        "::setlevel firemaking 5",
        -- Loar shades (level 40) and the Afflicted NPCs scattered through
        -- Mort'ton killed an earlier probe twice at fresh-character combat
        -- stats -- level up before ever entering the town, same as the
        -- rune scimitar above (prerequisite gear, not the quest's work).
        "::setlevel hitpoints 99",
        "::setlevel defence 40",
        "::setlevel attack 40",
        "::setlevel strength 40",
        "::complete quest_priestperil", -- Shades of Mort'ton's own prerequisite quest
        -- Herblore itself is locked behind Druidic Ritual (brew_potion.rs2:
        -- 513's ~herblore_unlocked, quest_druid.rs2:33) regardless of level
        -- -- measured run 1: mixSerum207 answered ok with the backpack
        -- unchanged and "You need to complete the Druidic Ritual quest
        -- before you can use the Herblore skill." A skill unlock, not the
        -- deliverable of THIS quest.
        "::complete quest_druidicritual",
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
        -- the south building of Mort'ton for Herbi Flax's diary. click_loc
        -- now steps off the loc's own tile and walks its far sides before
        -- projecting, so the real op-1 click lands (queue.py last_failure:
        -- build/quest_gate/q2_proof, click.shelf PASS + haveBook PASS).
        t.exec("goto-searchShelf", t.player.goto_tile, 3481, 3279, 0)
        local shelf_click_result, shelf_click_detail = t.player.click_loc("shades_experimentshelf", 1)
        -- t.settle() does not cover an inventory sync racing the mesbox's
        -- own inv_add (docs/QUEST_AUTHORING.md section 8) -- poll instead.
        local serum_book_result, serum_book_count = t.inv.await("serum_book", 1, 10)
        t.check("searchShelf", shelf_click_result == "ok" and serum_book_result == "ok",
            "click_loc(shades_experimentshelf, op1) -> " .. tostring(shelf_click_result) .. " "
                .. tostring(shelf_click_detail) .. "; inv.await(serum_book, 1) -> "
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

        -- Mix Serum 207: ashes used ON the tarrominvial (both backpack
        -- slots) -- OPHELDU -- brew_potion.rs2:14-18 branches on
        -- last_useitem=ashes into quest_mortton.rs2:103-111's
        -- ~mortton_mix_serum, which (past ^mortton_read_diary) runs
        -- ~attempt_brew_potion and should write ^mortton_made_serum on
        -- success. The click itself lands (verb answers ok, both slots
        -- correctly identified) -- see the file header for what the
        -- content does with it instead.
        t.exec("mixSerum207", t.player.use_item_on_item, "ashes", "tarrominvial")

        -- Evidence, not a guess: the varp never moves off
        -- ^mortton_read_diary and no mort_serum3 lands in the backpack,
        -- which is attempt_brew_potion returning false from inside its own
        -- ingredient2 gate (brew_potion.rs2:538-542) rather than reaching
        -- ~brew_potion at all.
        local stage_after_mix_result, stage_after_mix_value = t.quest.stage()
        t.check("mixSerum207.stageUnmoved", stage_after_mix_result == "ok" and stage_after_mix_value == 5,
            "quest.stage() -> " .. tostring(stage_after_mix_result) .. " (" .. tostring(stage_after_mix_value)
                .. ") -- still mortton_read_diary(5), not mortton_made_serum(10), after an OPHELDU click "
                .. "that answered ok; attempt_brew_potion's own ingredient2 gate (brew_potion.rs2:538-542) "
                .. "misfired -- see the file header for the traced engine root cause")
        local serum_after_mix_result, serum_after_mix_count = t.inv.count("mort_serum3")
        t.check("mixSerum207.noSerumProduced", serum_after_mix_result == "ok" and serum_after_mix_count == 0,
            "inv.count(mort_serum3) -> " .. tostring(serum_after_mix_result) .. " (" .. tostring(serum_after_mix_count)
                .. ") -- the ashes/tarrominvial pair is still unconsumed too (mixSerum207's own detail: "
                .. "'backpack unchanged')")

        t.blocked("content_bug: quest_mortton.rs2:103-111 (~mortton_mix_serum) -> "
            .. "brew_potion.rs2:503-548 (~attempt_brew_potion) never writes ^mortton_made_serum -- "
            .. "herblore_serum207 (skill_herblore/configs/brewing/brew.dbrow:495-502) is found "
            .. "correctly by ~get_brew_data but has no `data=ingredient2` line (optional per "
            .. "herblore.dbtable:24), and SS_OP_DB_GETFIELD's no-default fallback "
            .. "(src/torirsserver/torirs_server_ops_db.c:242-267) pushes a plain 0 for that unset "
            .. "column instead of -1, the compiled value of the `null` literal "
            .. "(src/serverscript/ssc_symbols.c:1366) -- so brew_potion.rs2:538's "
            .. "`$ingredient2 ! null` reads true and returns false before ~brew_potion ever runs. "
            .. "Every optional-ingredient2 row in this content pack hits the same defect (only "
            .. "herblore_sara_brew, brew.dbrow:446-454, ever sets that column); it is engine/content, "
            .. "not this driver -- use_item_on_item armed and clicked the right two backpack slots for "
            .. "real (see mixSerum207's own detail) and the two driver seams this file already worked "
            .. "around (the shelf's click_loc bypass, the missing item-on-item verb) are both gone. "
            .. "Nothing past ^mortton_read_diary is reachable without Serum 207 -- every Razmire/"
            .. "Ulsquire dialogue, the shade hunt, the temple repair and the pyre are all unreached "
            .. "behind it.")
        return
    end,
}
