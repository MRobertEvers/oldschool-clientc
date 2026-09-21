-- Tears of Guthix: talk to Juna to accept, mine/craft a stone bowl, hand it
-- in, collect the Crafting XP reward.
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
-- RETRY after the queue's "Juna is bound before ~chatnpc_specific so the
-- resume resolves" fix: `tearsofguthix.rs2` now carries
-- `[proc,tog_bind_speaker]`, which `npc_find`s (or `npc_add`s, the same
-- fallback `dttd_bmp.rs2`'s `[proc,dttdbmp_bind]` uses for this exact
-- `tog_juna_dummy` symbol) a live speaker BEFORE every `~chatnpc_specific`
-- call `[proc,tog_juna]` makes -- so the `~chatnpc_specific` page's first
-- `p_pausebutton` now has an active npc to run `facesquare`/`npc_facesquare`
-- against and resolves normally. The previously committed file's
-- `tog.dummy_npc_probe` / `tog.accept_stuck_after_15_ticks` /
-- `tog.accept_continue_stuck` rows and its trailing BLOCKED row (a t dot
-- blocked call) all asserted that now-fixed hang; they are gone, replaced
-- by driving the
-- dialogue, the craft and the hand-in through to a real completion.
--
-- WHICH ROWS SHOOT. t.exec and t.check shoot the row they write; plain
-- t.expect/t.step rows do not.

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
        -- world read yet. tog_juna_bowl is a VARBIT (all.varbit.compack
        -- id 451, no all.varp row of its own) -- quest.stage/expect_stage
        -- resolve that transparently.
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
        -- setlevel cheats to be visible client-side before reading anything
        -- (trap 23's own settle -- the ladder's reply outruns the container
        -- update by a tick).
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        t.expect("setup.have_stone", t.inv.expect_has("tog_stone", 1))
        t.expect("setup.have_chisel", t.inv.expect_has("chisel", 1))

        -- ------------------------------------------------- greet Juna
        -- [oploc1,tog_juna] -> @tog_juna_talk (not_started branch, requirements
        -- already met via the debugproc's %qp + this file's setlevel cheats).
        t.exec("tog.greet", t.player.click_loc, "tog_juna", 1)

        -- The whole first conversation, page by page, copied byte-for-byte
        -- from tearsofguthix.rs2's @tog_juna_talk not-started branch. The
        -- "Okay..." choice (p_choice3, option 1) falls through into the
        -- accept text; %tog_juna_bowl is set to need_bowl silently at the
        -- very end (no further page), so the list closes on "end".
        t.exec("tog.accept_dialog", t.chat.play, {
            "npc:Tell me... a story...",
            "player:A story?",
            "npc:I have been waiting here three thousand years, guarding the Tears of Guthix.",
            "npc:An adventurer such as yourself must have many tales to tell.",
            "npc:Then you can drink of the power of balance, which will make you stronger",
            "choose:Okay...",
            "player:Okay...",
            "mesbox:You tell Juna some stories of your adventures.",
            "npc:Your stories have entertained me. I will let you into the cave for a short time.",
            "npc:But first you will need to make a bowl in which to collect the tears.",
            "npc:There is a cave on the south side of the chasm that is similarly infused",
            "npc:Mine some stone from that cave, make it into a bowl, and bring it to me",
            "end",
        })

        t.expect("quest.stage.need_bowl", t.quest.expect_stage("need_bowl"))

        -- ------------------------------------------------- craft the bowl
        -- [opheldu,tog_stone] checks last_useitem = chisel: arm the chisel
        -- (item_a, the "Use" cell) and click tog_stone (item_b). mes("You
        -- make a stone bowl.") is a chat-log line and the backpack swap
        -- (tog_stone -1, tog_bowl +1) lands in the same tick, so
        -- use_item_on_item's own settle (new chat line OR backpack total
        -- change) catches it either way.
        t.exec("tog.make_bowl", t.player.use_item_on_item, "chisel", "tog_stone")

        local bowl_wait_result, bowl_wait_detail = t.inv.await("tog_bowl", 1, 10)
        local bowl_read, bowl_count = t.inv.count("tog_bowl")
        local stone_read, stone_count = t.inv.count("tog_stone")
        t.check("tog.bowl_crafted",
            bowl_wait_result == "ok" and bowl_count == 1 and stone_count == 0,
            "inv.await(tog_bowl,1) -> " .. tostring(bowl_wait_result) .. " " .. tostring(bowl_wait_detail)
                .. " tog_bowl=" .. tostring(bowl_count) .. "(" .. tostring(bowl_read) .. ")"
                .. " tog_stone=" .. tostring(stone_count) .. "(" .. tostring(stone_read) .. ")")

        -- ------------------------------------------------- hand in the bowl
        -- Read the skill snapshot before the hand-in click that awards it
        -- (state.lua's skill.expect_gain banner: a "before" reading taken
        -- one call ahead of the commit).
        local craft_snap_result, craft_snap = t.skill.snapshot()
        t.step("reward.snapshot", craft_snap_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot() -> " .. tostring(craft_snap_result))

        -- Second click_loc on the same loc: %tog_juna_bowl is still
        -- need_bowl, but inv_total(inv, tog_bowl) > 0 now, so @tog_juna_talk
        -- takes the "I have a bowl" branch and commits completion.
        t.exec("tog.handin", t.player.click_loc, "tog_juna", 1)

        t.exec("tog.handin_dialog", t.chat.play, {
            "npc:Before you can collect the Tears of Guthix you must make a bowl",
            "player:I have a bowl.",
            "npc:I will keep your bowl for you, so that you may collect the tears many times",
            "npc:Now... tell me another story, and I will let you collect the tears for the first time.",
            "end",
        })

        -- Completion (inv_del the bowl, %tog_juna_bowl=complete,
        -- stat_advance, ~quest_complete_rewards) commits silently behind
        -- that last page and the reward scroll mounts asynchronously
        -- (section 8) -- give it real time before reading anything.
        t.ticks(3)

        t.quest.expect_complete()

        -- Reward: stat_advance(crafting, 10000) is 1000 Crafting XP
        -- (tenths) -- ~quest_complete_rewards(quest_tearsofguthix, "1000
        -- Crafting XP|Access to the Tears of Guthix minigame", coins), the
        -- literal amount the quest documents. No item/coin reward -- the
        -- minigame access is an unlock, not a testable delta.
        t.exec("reward.crafting_xp", t.skill.expect_gain, "crafting", 1000, craft_snap)

        t.finish(0)
    end,
}
