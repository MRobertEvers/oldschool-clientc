-- Mourning's End Part II -- hand-written, NOT the new_quest.py scaffold.
--
-- Why: the generator's own output (161 steps drawn from Quest Helper's
-- MourningsEndPartII.java) walks the REAL osrs Temple of Light mirror
-- maze tile by tile. This content pack does not implement that maze --
-- `configs/mend2.constant`'s own header says so explicitly ("no rs2
-- precedent anywhere in this tree for modelling a beam-propagation
-- puzzle ... the entire multi-floor puzzle chain ... is narrated as a
-- single interaction at Arianwyn (30 -> 40) rather than modelled
-- tile-by-tile"), and `scripts/mend2_shared.rs2` bears this out: there is
-- no [oploc*] trigger anywhere under quest_mourningsendpartii/, no
-- Temple-of-Light pillar/dispenser script, and no Death Altar/Underground
-- Pass route at all. The whole quest is five Arianwyn conversations plus
-- one Essyllt conversation, state-gated on `%mourning_quest_main`
-- (0/10/20/30/40/50/60, `configs/mend2.constant`) and on held/worn
-- items, with the fourth Arianwyn page narrating the skipped content via
-- mes().
--
-- Route: `mourning_arianwyn` is already spawned and triggered by Part I's
-- own `mend1_shared.rs2` ([opnpc1,mourning_arianwyn] -> ^mend1_complete
-- branch -> ~mend2_arianwyn_talk); `mourner_hideout_head_mourner` the same
-- way via Part I's `mend1_disguise.rs2`. Both resolve through the
-- `sote`-keyed multinpc/_vis pair the same as any other roving elf (trap
-- 19/pointer.lua) -- talk to the BASE symbol, never `_vis`.
--
-- Setup: `::mend2` (this quest's own debug cheat, `mend2_debug.rs2`,
-- mirroring `::mend1`/`::cook`'s idiom) sets %mourning_quest to
-- ^mend1_complete, resets %mourning_quest_main to ^mend2_not_started and
-- teleports beside Arianwyn in Lletya (2353,3172,0) -- run last so it is
-- authoritative. `::complete quest_mourningsendparti` alongside it matches
-- every other chained-quest file's own convention (druid.lua's
-- `::complete quest_troll`, mortton.lua's `::complete quest_priestperil`,
-- ...), even though it is redundant with what `::mend2` already writes.
-- The mourner disguise (gasmask + 5 pieces), chisel, rope and a death
-- talisman are all Quest Helper's own `getItemRequirements()`
-- (mournersOutfit/chisel/deathTalismanHeader/rope) -- bring-along
-- prerequisites this content pack only CHECKS (`inv_total(worn, ...)` /
-- `inv_total(inv, ...)`), never asks the player to craft or fetch, so
-- `::give` is the right tool per docs/QUEST_AUTHORING.md trap 16 (this is
-- not the quest's own deliverable).
--
-- RESUMED 2026-09-21 after the RETRY fix
-- (OSRS-Content 4420b02611, `quest_mourningsendpartii/configs/mend2.varp`):
-- `[mourning_quest_part2]` (`configs/all.varp:1161`) now declares
-- `transmit=yes`, so `t.quest.expect_stage()` reads this quest's own
-- progress for the first time -- the earlier `t.blocked` naming that gap
-- (all.varp:1161 having an empty body) is gone, and every conversation
-- below is now graded on the live stage instead of the earlier file's
-- indirect `mourning_ederns_journal` inventory proxy. Driven three real
-- stage transitions further than before (not_started -> briefed ->
-- essyllt_task -> crystal_given, each with a passing `quest.stage.*` row)
-- before hitting the next wall: `mend2_shared.rs2`'s own
-- `^mend2_crystal_given` branch (line 97 at that time) printed a 300-byte
-- `mes()` call, over this engine's 255-byte single-`mes()` limit, and
-- desynced the client session the instant the branch ran.
--
-- RESUMED AGAIN 2026-09-21 after a SECOND RETRY fix (`RETRY after
-- 68c5e8d9d`): `mend2_shared.rs2:97`'s over-length line is now split across
-- two `mes()` calls (line 102/103 of the current file, both under the
-- 252-byte var-u8 ceiling -- confirmed by reading the file fresh, not by
-- trusting the queue row's own numbers), and `ToriRSServer_Send` refuses an
-- over-length var-u8 frame out loud rather than desyncing the session, so
-- this is no longer a live hazard even if a future line crept back over the
-- limit. The former `t.blocked` row and its `talkToArianwyn3.desync_proof`
-- probe are gone; `talkToArianwyn3` now drives the real
-- `^mend2_crystal_given -> ^mend2_puzzle_done` transition (the branch opens
-- with a `mes()` line, not a page -- trap 22/section 2 -- so it is read
-- with `t.msg.expect`, never `t.chat.play`), then `talkToArianwyn4`
-- (`^mend2_puzzle_done -> ^mend2_report`, a real three-page dialogue
-- opening on the PLAYER's line per trap 18) and `talkToArianwyn5`
-- (`^mend2_report -> ^mend2_complete`, one npc page whose dismissal fires
-- `~mend2_quest_complete` -- the agility xp, the `mourning_crystal_trinket`
-- reward and the extra `death_talisman`, all per `configs/mend2.constant`'s
-- own `stat_xp_awarded`/reward-string documentation) drive the quest to
-- `quest.expect_complete()`.
--
-- `quest.journal` gap (unrelated to the above, named in the RETRY row):
-- there is no `[proc,mend2_journal]` anywhere in this tree and
-- `quest_journal.rs2` has no part-2 dispatch row at all (grepped fresh),
-- so `t.ui.journal_open("Mourning's End Part II")` would fall through to
-- the file's own "This world does not run this quest yet." default. This
-- is exactly the gap section 7's minimum shape exists for:
-- `quest.expect_complete()`'s own `quest.journal` row is not required for
-- green, so the journal is not probed here at all -- the varp/scroll/points
-- rows below are the completion evidence.

return {
    id = "mourningsendpartii",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::give gasmask 1",
        "::give mourning_mourner_top 1",
        "::give mourning_mourner_legs 1",
        "::give mourning_mourner_cloak 1",
        "::give mourning_mourner_boots 1",
        "::give mourning_mourner_gloves 1",
        "::give chisel 1",
        "::give rope 1",
        "::give death_talisman 1",
        "::complete quest_mourningsendparti",
        "::mend2", -- last: resets mourning_quest_main and teleports beside Arianwyn
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "mourning_quest_main",
            constants = {
                not_started = 0,
                briefed = 10,
                essyllt_task = 20,
                crystal_given = 30,
                puzzle_done = 40,
                report = 50,
                complete = 60,
            },
            row = "quest_mourningsendpart2", -- all.dbrow.compack id 100
            display = "Mourning's End Part II",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        -- ::mend2's own varp write + teleport are server-side -- give the
        -- client a couple of ticks before reading anything.
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Gear up the disguise ::give left unworn -- mend2_shared.rs2's
        -- own gate at ^mend2_crystal_given checks all six worn slots.
        t.exec("wear.gasmask", t.player.equip, "gasmask")
        t.exec("wear.top", t.player.equip, "mourning_mourner_top")
        t.exec("wear.legs", t.player.equip, "mourning_mourner_legs")
        t.exec("wear.cloak", t.player.equip, "mourning_mourner_cloak")
        t.exec("wear.boots", t.player.equip, "mourning_mourner_boots")
        t.exec("wear.gloves", t.player.equip, "mourning_mourner_gloves")

        -- ---- Arianwyn #1, Lletya: not_started -> briefed
        -- (mend2_shared.rs2:33-47) ----
        t.exec("talkToArianwyn1", t.player.talk_to, "mourning_arianwyn", 1)
        t.exec("talkToArianwyn1-dialog", t.chat.play, {
            "npc:There is more you can do for us, if you're willing.",
            "player:What happened to him?",
            "npc:I don't know. I need someone to go into the Mourner Caves",
            "npc:Essyllt still has your contact in the Headquarters basement",
            "choose:I'll help.",
            "player:I'll help.",
            "npc:Thank you. Speak to Essyllt",
        })
        t.ticks(3)
        t.expect("quest.stage.briefed", t.quest.expect_stage("briefed"))

        -- ---- Essyllt, HQ basement: briefed -> essyllt_task
        -- (mend2_shared.rs2:132-138) ----
        t.exec("goto-talkToEssyllt", t.player.goto_tile, 2044, 4628, 0)
        t.exec("talkToEssyllt", t.player.talk_to, "mourner_hideout_head_mourner", 1)
        t.exec("talkToEssyllt-dialog", t.chat.play, {
            "player:Arianwyn sent me. She's asked me to look for Edern",
            "npc:I remember him. The caves west of here lead that way",
            "npc:Go carefully. Whatever happened to him",
        })
        t.ticks(3)
        t.expect("quest.stage.essyllt_task", t.quest.expect_stage("essyllt_task"))

        -- ---- Arianwyn #2, Lletya: essyllt_task -> crystal_given
        -- (mend2_shared.rs2:52-73). The branch opens with a plain mes()
        -- line (not a page -- trap 22/section 2) that grants
        -- mourning_ederns_journal and mourning_crystal_sample before the
        -- first real page, then five pages: player, npc, npc, npc, player
        -- (^chat_sad/^chat_shock anims -- ~chatplayer_anim opens first,
        -- trap 18). ----
        t.exec("goto-talkToArianwyn2", t.player.goto_tile, 2353, 3172, 0)
        t.exec("talkToArianwyn2", t.player.talk_to, "mourning_arianwyn", 1)
        t.exec("talkToArianwyn2-dialog", t.chat.play, {
            "player:Arianwyn -- Edern didn't make it. I found his journal",
            "npc:I feared as much. Let me see the sample.",
            "npc:This crystal is unlike any I've seen",
            "npc:Eluned tells me the Temple of Light's mirrors can charge a crystal",
            "player:I'll see what I can do.",
        })
        t.ticks(3)
        t.expect("quest.stage.crystal_given", t.quest.expect_stage("crystal_given"))

        local journal_has_result, journal_has = t.inv.has("mourning_ederns_journal")
        t.check("crystal_given.ederns_journal", journal_has_result == "ok" and journal_has == true,
            "inv.has mourning_ederns_journal -> " .. tostring(journal_has_result) .. " " .. tostring(journal_has))
        local crystal_has_result, crystal_has = t.inv.has("mourning_crystal_new_sample")
        t.check("crystal_given.crystal_new_sample", crystal_has_result == "ok" and crystal_has == true,
            "inv.has mourning_crystal_new_sample -> " .. tostring(crystal_has_result) .. " " .. tostring(crystal_has))

        -- ---- Arianwyn #3, Lletya: crystal_given -> puzzle_done
        -- (mend2_shared.rs2:75-113). All five gates (disguise worn, chisel,
        -- rope, mourning_crystal_new_sample, death_talisman) pass -- setup
        -- gave every item and the equip rows above wore the disguise -- so
        -- this branch runs straight through its five mes() lines (now all
        -- under the var-u8 ceiling, the RETRY fix). The branch opens with a
        -- plain mes(), not a page (trap 22/section 2), so talk_to settles on
        -- that opening line and the lines are read with t.msg.expect, never
        -- t.chat.play. ----
        t.exec("talkToArianwyn3", t.player.talk_to, "mourning_arianwyn", 1)
        local msg3a_result = t.msg.expect("You make your way back into the Temple of Light")
        t.check("talkToArianwyn3.mesOpening", msg3a_result == "ok",
            "msg.expect('You make your way back into the Temple of Light') -> " .. tostring(msg3a_result))
        local msg3b_result = t.msg.expect("Piece by piece you assemble a fully charged crystal")
        t.check("talkToArianwyn3.mesSplitLine", msg3b_result == "ok",
            "msg.expect('Piece by piece you assemble a fully charged crystal') -> " .. tostring(msg3b_result)
                .. " -- mend2_shared.rs2:102 (formerly a single 300-byte mes() at line 97, over the"
                .. " 255-byte single-mes() limit) is now split across two mes() calls and lands intact")
        local msg3c_result = t.msg.expect("Light floods back into the Temple of Light")
        t.check("talkToArianwyn3.mesClosing", msg3c_result == "ok",
            "msg.expect('Light floods back into the Temple of Light') -> " .. tostring(msg3c_result))
        t.ticks(3)
        t.expect("quest.stage.puzzle_done", t.quest.expect_stage("puzzle_done"))

        -- ---- Arianwyn #4, Lletya: puzzle_done -> report
        -- (mend2_shared.rs2:114-120). Three pages, opening on the PLAYER's
        -- own line (trap 18: ~chatplayer_anim is the branch's first call). ----
        t.exec("talkToArianwyn4", t.player.talk_to, "mourning_arianwyn", 1)
        t.exec("talkToArianwyn4-dialog", t.chat.play, {
            "player:Arianwyn -- it's done. The Temple of Light is lit again, and the Death Altar answers to us.",
            "npc:You've done something remarkable. Lord Iorwerth's plans just suffered a real setback.",
            "npc:Return to me once you've had a moment to catch your breath",
        })
        t.ticks(3)
        t.expect("quest.stage.report", t.quest.expect_stage("report"))

        -- ---- Reward snapshot, before the hand-in (docs sec H2 convention) ----
        local reward_snapshot_result, reward_before = t.skill.snapshot()
        t.step("reward.snapshot", reward_snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot before the hand-in -> " .. tostring(reward_snapshot_result))
        local qp_before_result, qp_before = t.var.varp("qp")
        t.step("reward.qpBefore", qp_before_result == "ok" and "PASS" or "FAIL",
            "var.varp(qp) before hand-in -> " .. tostring(qp_before_result) .. " " .. tostring(qp_before))
        local talisman_before_result, talisman_before = t.inv.count("death_talisman")
        t.step("reward.talismanBefore", talisman_before_result == "ok" and "PASS" or "FAIL",
            "inv.count(death_talisman) before hand-in -> " .. tostring(talisman_before_result)
                .. " " .. tostring(talisman_before))

        -- ---- Arianwyn #5, Lletya: report -> complete
        -- (mend2_shared.rs2:121-125). One npc page; its dismissal fires
        -- ~mend2_quest_complete (proc lines 12-21), which is where the
        -- reward scroll, the agility xp and the two item grants actually
        -- happen -- trap 22, the page SUSPENDS the branch until dismissed. ----
        t.exec("talkToArianwyn5", t.player.talk_to, "mourning_arianwyn", 1)
        t.exec("talkToArianwyn5-dialog", t.chat.play, {
            "npc:Thank you, truly. Elven-kind owes you a debt for this.",
        })
        t.ticks(3) -- completion is asynchronous (docs sec 8) -- not padding

        -- ---- Completion, hand-rolled: quest.expect_complete()'s own
        -- quest.journal row would FAIL here on purpose, not flakily --
        -- there is no [proc,mend2_journal] anywhere in this tree and
        -- quest_journal.rs2 has no part-2 dispatch row at all (grepped
        -- fresh against the current tree), so ui.journal_open("Mourning's
        -- End Part II") falls through to the file's own "This world does
        -- not run this quest yet." default and never reports complete=true.
        -- Docs sec 8's own rule for exactly this shape: drive the
        -- completion through the rows that DO land -- quest.varp_complete,
        -- quest.scroll_title, quest.points, the reward rows -- each written
        -- by hand with t.check, and never ship a row measured to time out. ----
        local stage_result, stage_value = t.quest.stage()
        t.check("quest.varp_complete", stage_result == "ok" and stage_value == 60,
            "quest.stage() -> " .. tostring(stage_result) .. " " .. tostring(stage_value) .. " complete=60")

        local title_result, title_detail = t.scroll.title()
        local title_name = type(title_detail) == "table" and title_detail.name or nil
        local title_pass = title_result == "ok" and type(title_name) == "string"
            and string.find(title_name, "Mourning's End Part II", 1, true) ~= nil
        -- Owner's rule (docs sec 7): the completion scroll must be
        -- photographed on this row -- quest.varp_complete's own auto-shot
        -- above already caught the frame, so this row's attempt reads
        -- "unchanged" (trap 4); fold that into the detail exactly the way
        -- quest.expect_complete()'s own quest.scroll_title row does, so
        -- gate.py's scroll-shot check accepts it without a duplicate PNG.
        local scroll_shot_result, scroll_shot_detail = t.shot("quest.scroll")
        local scroll_shot_note = ""
        if scroll_shot_result == "ok" and type(scroll_shot_detail) == "string"
            and string.find(scroll_shot_detail, "unchanged", 1, true) then
            scroll_shot_note = " [scroll already photographed: " .. scroll_shot_detail .. "]"
        end
        t.check("quest.scroll_title", title_pass,
            "scroll.title() -> " .. tostring(title_result) .. " name=" .. tostring(title_name)
                .. " expected to contain 'Mourning's End Part II'" .. scroll_shot_note)
        t.scroll.close()

        local qp_after_result, qp_after = t.var.varp("qp")
        local qp_delta = (qp_after_result == "ok" and qp_before_result == "ok") and (qp_after - qp_before) or nil
        t.check("quest.points", qp_delta == 2,
            "qp " .. tostring(qp_before) .. " -> " .. tostring(qp_after)
                .. " delta=" .. tostring(qp_delta) .. " expected=2"
                .. " -- quest.journal has no row here: no [proc,mend2_journal] and no part-2 dispatch"
                .. " in quest_journal.rs2 (grepped fresh), so ui.journal_open falls through to the"
                .. " default 'This world does not run this quest yet.' text and never reports complete")

        -- ---- Rewards: literal values configs/mend2.constant and
        -- mend2_shared.rs2's own ~mend2_quest_complete document (60000
        -- Agility XP tenths-scale=600000, Crystal trinket, an extra Death
        -- Talisman), never a number read back from the scroll ----
        t.check("reward.agility", t.skill.expect_gain("agility", 60000, reward_before))
        local trinket_result, trinket_detail = t.inv.expect_has("mourning_crystal_trinket", 1)
        t.check("reward.crystal_trinket", trinket_result, trinket_detail)
        local talisman_expect = (talisman_before_result == "ok" and talisman_before or 0) + 1
        local talisman_result, talisman_detail = t.inv.await("death_talisman", talisman_expect, 10)
        t.check("reward.death_talisman", talisman_result == "ok",
            "inv.await(death_talisman," .. talisman_expect .. ",10) -> " .. tostring(talisman_result)
                .. " " .. tostring(talisman_detail))

        t.finish(0)
    end,
}
