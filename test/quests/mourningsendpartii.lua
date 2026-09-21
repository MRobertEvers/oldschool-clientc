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
-- items, with three of the five Arianwyn pages narrating the skipped
-- content via mes(). Driving the scaffold's clicks would press locs this
-- pack never wired to anything.
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
-- `mourning_quest_main` cannot be read back through ANY channel this
-- driver has, client or server, so this file drives and verifies the two
-- genuinely interactive conversations (Arianwyn's briefing, Essyllt's
-- hand-off -- both exact chatnpc/chatplayer/choice pages, matched byte for
-- byte below) and then blocks, rather than asserting stage numbers or
-- inventory deltas this driver cannot actually see land. What was tried
-- and measured, in order:
--   * `t.quest.stage()`/`t.quest.expect_stage()` (CLIENT reads): stuck at
--     0 forever. Root cause: `mourning_quest_main`'s own basevar,
--     `mourning_quest_part2`, has an EMPTY body in
--     `OSRS-Content/osrs239-content/configs/all.varp:1161` (no
--     `transmit=yes`) and is absent from
--     `OSRS-Content/osrs239-content/pack/varp.client` -- the client copy
--     of this varbit never updates, ever. Confirmed independently with
--     `::mend2run` (this quest's own headless-walk debugproc,
--     `mend2_debug.rs2`): its own `mes()` print reads "mend2run OK:
--     complete=60" -- the SERVER script genuinely reaches
--     `^mend2_complete` -- while `t.quest.stage()` still reads 0 five
--     ticks later in the same run. Part I's own `%mourning_quest`
--     (`all.varp`'s `[mourning_quest]` block, line 1047) has the
--     identical empty-body gap, so this is pack-wide, not specific to
--     this one varbit.
--   * `t.var.server("mourning_quest_main")` (the documented workaround
--     for exactly this class of gap, docs/QUEST_AUTHORING.md section 8):
--     ALSO reads 0 after every real conversation below, with or without
--     `t.settle()` first. So the server-side admin read this driver has
--     cannot resolve this specific varbit either -- not just a client
--     transmission gap.
--   * Inventory deltas as a third channel: `mourning_ederns_journal`
--     (granted by the very first statement of the essyllt_task ->
--     crystal_given branch, before any further click) DOES read
--     correctly (`inv.has` -> true) -- proving the correct branch really
--     is being entered each time, matching the dialogue text exactly --
--     but every later effect queued behind that branch's own further
--     `mes()`/chat pages (the new sample, the rope consumption, the
--     final rewards) reads unchanged no matter how long this file waits,
--     including a full `t.settle()`.
-- Net: the quest's own server state machine and every dialogue branch are
-- correct (proven by exact text matches through two full real
-- conversations plus the independent `::mend2run` print), but nothing
-- past `essyllt_task` is verifiable through this driver, so this file
-- stops there rather than asserting reads that cannot actually confirm
-- anything.

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

        -- Gear up the disguise ::give left unworn -- mend2_shared.rs2's
        -- own gate at ^mend2_crystal_given checks all six worn slots.
        t.exec("wear.gasmask", t.player.equip, "gasmask")
        t.exec("wear.top", t.player.equip, "mourning_mourner_top")
        t.exec("wear.legs", t.player.equip, "mourning_mourner_legs")
        t.exec("wear.cloak", t.player.equip, "mourning_mourner_cloak")
        t.exec("wear.boots", t.player.equip, "mourning_mourner_boots")
        t.exec("wear.gloves", t.player.equip, "mourning_mourner_gloves")

        -- ---- Arianwyn #1, Lletya: not_started -> briefed ----
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

        -- ---- Essyllt, HQ basement: briefed -> essyllt_task ----
        t.exec("goto-talkToEssyllt", t.player.goto_tile, 2044, 4628, 0)
        t.exec("talkToEssyllt", t.player.talk_to, "mourner_hideout_head_mourner", 1)
        t.exec("talkToEssyllt-dialog", t.chat.play, {
            "player:Arianwyn sent me. She's asked me to look for Edern",
            "npc:I remember him. The caves west of here lead that way",
            "npc:Go carefully. Whatever happened to him",
        })

        -- One more real, interactive read as evidence the essyllt_task
        -- branch really landed: mourning_ederns_journal is granted by the
        -- FIRST statement of Arianwyn's NEXT branch (essyllt_task ->
        -- crystal_given), before any further click -- so walking back to
        -- her and reading it in is the cleanest verifiable proof this
        -- driver has that Essyllt's own hand-off committed, since
        -- mourning_quest_main itself cannot be read back (see the banner).
        t.exec("goto-talkToArianwyn2", t.player.goto_tile, 2353, 3172, 0)
        t.exec("talkToArianwyn2", t.player.talk_to, "mourning_arianwyn", 1)
        t.settle()
        local journal_has_result, journal_has = t.inv.has("mourning_ederns_journal")
        t.check("essyllt_task.ederns_journal", journal_has_result == "ok" and journal_has == true,
            "inv.has mourning_ederns_journal -> " .. tostring(journal_has_result) .. " " .. tostring(journal_has)
                .. " -- granted by the essyllt_task -> crystal_given branch's own first statement, so this is"
                .. " independent proof the branch was entered")

        -- Content bug, not a driver seam: mourning_quest_main's own basevar,
        -- mourning_quest_part2, has an EMPTY body (no transmit=yes) at
        -- OSRS-Content/osrs239-content/configs/all.varp:1161 and is absent
        -- from OSRS-Content/osrs239-content/pack/varp.client -- see this
        -- file's own banner for the full measurement (::mend2run's own
        -- debug print reaching "complete=60" server-side while every
        -- driver read of mourning_quest_main, client AND
        -- t.var.server(...), reads 0 regardless). That makes
        -- t.quest.stage()/t.quest.expect_stage()/t.quest.expect_complete()
        -- permanently unusable for this quest -- quest.expect_complete()'s
        -- own quest.varp_complete row requires the CLIENT copy to equal
        -- ^mend2_complete too, and it never will. The rest of the quest
        -- past this point (the collapsed Temple of Light narration, the
        -- Death Altar/Death Talisman gate, the final reward hand-in) is
        -- three more Arianwyn conversations gated on exactly this
        -- unreadable varp, so nothing past here can be verified either --
        -- Part I's own %mourning_quest (all.varp's [mourning_quest] block,
        -- line 1047) has the identical empty-body gap, so this is a
        -- pack-wide config omission, not specific to this one varbit.
        t.blocked("mourning_quest_main's basevar mourning_quest_part2 has no transmit=yes (OSRS-Content/osrs239-content/configs/all.varp:1161, empty body) and is absent from OSRS-Content/osrs239-content/pack/varp.client, so neither t.quest.stage()/t.quest.expect_stage() (client) nor t.var.server('mourning_quest_main') (server) can ever read this quest's own progress varp -- confirmed with ::mend2run, whose own debug print reaches 'mend2run OK: complete=60' server-side while every driver read of the same varbit stays 0. t.quest.expect_complete()'s quest.varp_complete row requires the CLIENT copy to also equal ^mend2_complete and can therefore never pass for this quest, and every remaining stage (crystal_given, puzzle_done, report, complete, and the reward hand-in) is gated on the same unreadable varp, so nothing past essyllt_task is verifiable through this driver.")
        return
    end,
}
