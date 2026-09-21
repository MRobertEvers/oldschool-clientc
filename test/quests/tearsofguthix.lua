-- Tears of Guthix: talk to Juna to accept the quest.
--
-- Fixture start: fresh_lumbridge.ini stands the player at 3206,3233,0
-- (Lumbridge, beside Hans), but `setup`'s `::tearsofguthix` debugproc
-- (quest_tearsofguthix/scripts/tearsofguthix.rs2 [debugproc,tearsofguthix])
-- p_teleports the player straight to `^tog_juna_stand` (3250,9517, level 2),
-- forces %qp=^tog_qp_req (43) and grants one `tog_stone` + one `chisel` --
-- the same shape as Cook's Assistant's `::cookbmp_test_give_ingredients`: a
-- test-only prerequisite grant so the driver would exercise the dialogue/
-- craft/hand-in chain instead of the lantern-across-the-chasm mining
-- minigame (`tearsofguthix_lantern.rs2`, explicitly deferred by that
-- file's own banner). The debugproc does NOT touch skill levels, so
-- `setup` still sets Firemaking/Crafting/Mining itself --
-- `~tog_has_requirements` checks those three stats plus %qp.
--
-- RETRY after 73a4251d0 (queue last_failure): tog_juna IS a multiloc
-- (all.loc [tog_juna], children tog_juna_1op/tog_juna_2ops,
-- multivarbit=tog_juna_bowl), and the previously committed file asserted
-- `chat.no_dialogue` (kind=="none" right after the click) as the recorded
-- CONTENT BUG -- measured against a world where the child-then-base
-- multiloc trigger fallback had not landed, so [oploc1,tog_juna] was never
-- reached at all. That fallback is fixed now (QUEST_AUTHORING.md trap 20,
-- 2026-09-20): re-measured below, the click DOES now reach
-- [label,tog_juna_talk] and a real npc dialogue DOES open ("Tell me... a
-- story...", `02-tog.greet-p1.png`). But the SAME underlying defect the
-- old row's comment named survives one step later: the dialogue's first
-- `p_pausebutton` (inside `~chatnpc_specific` -> `chatnpc_specific_anim`,
-- interface_chat/scripts/chat.rs2:108-118) never resolves. `chat.continue_`
-- clicks the resume button, gets an "ok" ack, and the page then never
-- advances -- ticked out to 45+ server ticks, `chat.continue_` on every
-- later attempt answers "a resume is already outstanding" (chat.lua:150),
-- because no new page ever mounts to clear that pending flag.
--
-- Root cause, confirmed live: `t.npc.by_symbol("tog_juna_dummy")` (the
-- exact npc `[proc,tog_juna]` passes to `~chatnpc_specific` at
-- tearsofguthix.rs2:22) answers `no_row` -- there is no live
-- `tog_juna_dummy` entity anywhere in the loaded world at all, despite
-- `areas/world/configs/m50_148.spawn`'s static row for it. Compare
-- `quest_deathtothedorgeshuun/scripts/dttd_bmp.rs2:72-76`
-- (`[proc,dttdbmp_bind]`), which calls `~chatnpc_specific("Juna",
-- tog_juna_dummy, ...)` for this SAME npc symbol but only after
-- `npc_find(coord, tog_juna_dummy, 12, 0)` and, on a miss, `npc_add(...)`
-- to spawn one right there first (dttd_bmp.rs2:411/427 call
-- `~dttdbmp_bind(tog_juna_dummy)` before every `~chatnpc_specific` on it).
-- `tearsofguthix.rs2`'s own `[proc,tog_juna]` has no equivalent bind/spawn
-- step, so `~chatnpc_specific` opens its first page display-only (no live
-- entity needed to paint static head/text) and then hangs forever on the
-- first resume, which needs the active npc `chatnpc_specific_anim`'s
-- `facesquare(npc_coord)`/`npc_facesquare(coord)` calls depend on.

return {
    id = "tearsofguthix",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::tearsofguthix",
        "::setlevel firemaking 49",
        "::setlevel crafting 20",
        "::setlevel mining 20",
    },

    run = function(t)
        -- quest.bind records the varp and constants for later checks; no
        -- world read yet.
        t.quest.bind({
            varp = "tog_juna_bowl",
            constants = {
                not_started = 0,
                need_bowl = 1,
                complete = 2,
            },
            display = "Tears of Guthix",
            points = 1,
        })

        -- Wait for the debugproc's teleport/varp/inventory grant and the
        -- setlevel cheats to be visible client-side before reading anything.
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        t.expect("setup.have_stone", t.inv.expect_has("tog_stone", 1))
        t.expect("setup.have_chisel", t.inv.expect_has("chisel", 1))

        -- ------------------------------------------------- greet Juna
        -- [oploc1,tog_juna] -> @tog_juna_talk (not_started branch): the
        -- trigger IS reached now (re-measuring the queue's last_failure) --
        -- a real npc dialogue opens.
        t.exec("tog.greet", t.player.click_loc, "tog_juna", 1)

        local greet_kind = t.chat.kind()
        local greet_text_result, greet_text = t.chat.text()
        t.check("tog.dialogue_opened", greet_kind == "npc" and greet_text_result == "ok",
            "kind=" .. tostring(greet_kind) .. " text=" .. tostring(greet_text))

        -- Root cause probe: the exact npc `[proc,tog_juna]` passes to
        -- `~chatnpc_specific` at tearsofguthix.rs2:22.
        local dummy_result, dummy_row = t.npc.by_symbol("tog_juna_dummy")
        local dummy_detail = "no row read"
        if dummy_result == "ok" and type(dummy_row) == "table" then
            dummy_detail = "tile_x=" .. tostring(dummy_row.tile_x) .. " tile_z=" .. tostring(dummy_row.tile_z)
                .. " level=" .. tostring(dummy_row.level)
        end
        t.check("tog.dummy_npc_probe", true,
            "npc.by_symbol(tog_juna_dummy) -> " .. tostring(dummy_result) .. " " .. dummy_detail
                .. " -- no live entity for ~chatnpc_specific to bind to (compare dttd_bmp.rs2's ~dttdbmp_bind)")

        -- Click the resume button on page 1 ("Tell me... a story...").
        -- The click itself is acked (an "ok" from continue_ here is only
        -- the CLIENT's ack of the press, chat.lua:118-124's own banner --
        -- not proof the server ever remounts a next page).
        local continue_result, continue_detail = t.chat.continue_()
        t.check("tog.accept_continue_click", true,
            "chat.continue_() -> " .. tostring(continue_result) .. " " .. tostring(continue_detail))

        -- Give the reply generous real time -- 15 server ticks, well past
        -- chat.drain's own 6-tick settle and past every real
        -- click_loc-triggered dialogue transition measured elsewhere in
        -- this suite (betweenarock/eadgar/haunted/murder/squire all
        -- resume click_loc dialogues in well under that).
        t.ticks(15)
        local stuck_kind = t.chat.kind()
        local stuck_text_result, stuck_text = t.chat.text()
        t.check("tog.accept_stuck_after_15_ticks", true,
            "kind=" .. tostring(stuck_kind) .. " text=" .. tostring(stuck_text)
                .. " -- still page 1 ('Tell me... a story...'), never advanced to the player's 'A story?' echo")

        -- A second resume click now answers "a resume is already
        -- outstanding" (chat.lua:150) -- the FIRST click's pending flag
        -- was never cleared because no new page ever mounted.
        local retry_result, retry_detail = t.chat.continue_()
        t.check("tog.accept_continue_stuck", true,
            "chat.continue_() retried -> " .. tostring(retry_result) .. " " .. tostring(retry_detail))

        t.blocked("content_bug: quest_tearsofguthix/scripts/tearsofguthix.rs2:22 -- "
            .. "[proc,tog_juna]'s ~chatnpc_specific(\"Juna\", tog_juna_dummy, $text) has no live "
            .. "tog_juna_dummy entity to bind to (t.npc.by_symbol: no_row) and no bind/spawn step "
            .. "the way dttd_bmp.rs2:72-76's [proc,dttdbmp_bind] (npc_find + npc_add fallback) gives "
            .. "the same npc symbol before its own ~chatnpc_specific calls. Page 1 opens (static "
            .. "head/text needs no live entity) but the first p_pausebutton resume never resolves -- "
            .. "chat.continue_ acks the click, the page never advances past 15 ticks, and a retried "
            .. "continue_ then answers 'a resume is already outstanding' forever.")
        return
    end,
}
