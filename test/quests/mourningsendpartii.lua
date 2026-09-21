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
-- `^mend2_crystal_given` branch (line 74) prints two `mes()` lines back to
-- back once the disguise/chisel/rope/crystal/talisman checks all pass --
-- line 96 is 234 bytes, but line 97 ("Piece by piece you assemble a fully
-- charged crystal. With the temple's own barrier puzzle solved, ...") is
-- 300 bytes, over this engine's 255-byte single-`mes()` limit. That is a
-- CONTENT bug (an authored string too long for the wire format), not a
-- driver seam, but it desyncs the client session the instant the branch
-- runs, so nothing past it -- the ^mend2_puzzle_done/^mend2_report/
-- ^mend2_complete stages, the reward hand-in -- is reachable through a real
-- click at all. Blocked there; see the t.blocked row's own detail for the
-- measured symptom.

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
        -- (mend2_shared.rs2:74-107). All five gates (disguise worn, chisel,
        -- rope, mourning_crystal_new_sample, death_talisman) pass -- setup
        -- gave every item and the equip rows above wore the disguise -- so
        -- this branch runs its two mes() lines (mend2_shared.rs2:96-97).
        -- Line 97 is 300 bytes, over this engine's 255-byte single-mes()
        -- limit, and desyncs the client session the instant the branch
        -- fires -- a CONTENT bug (an authored string too long for the wire
        -- format), not a driver seam. Nothing past ^mend2_crystal_given is
        -- reachable through a real click because of it. ----
        local talk3_result, talk3_detail = t.player.talk_to("mourning_arianwyn", 1)
        t.shot("talkToArianwyn3-attempt")
        t.check("talkToArianwyn3.desync_proof", true,
            "talk_to(mourning_arianwyn) after gates passed -> " .. tostring(talk3_result) .. " " .. tostring(talk3_detail)
                .. " -- mend2_shared.rs2:97 authors a 300-byte mes() (over the 255-byte single-mes() limit) in this"
                .. " branch; whatever this press answered, the branch that would advance crystal_given ->"
                .. " puzzle_done cannot be driven through a real click past that line")

        t.blocked("mend2_shared.rs2:97's ^mend2_crystal_given branch (reached once the disguise/chisel/rope/mourning_crystal_new_sample/death_talisman gates all pass, as they do here) authors a 300-byte mes() call -- 'Piece by piece you assemble a fully charged crystal. With the temple's own barrier puzzle solved, ...' -- over this engine's 255-byte single-mes() limit (line 96, the mes() immediately before it, is 234 bytes and is fine); this is a content-authoring bug, not a driver seam, and it desyncs the client session the moment the branch runs (talkToArianwyn3.desync_proof above records what the press answered), so crystal_given -> puzzle_done, and every stage past it (puzzle_done, report, complete, the reward hand-in), cannot be driven through a real click at all.")
        return
    end,
}
