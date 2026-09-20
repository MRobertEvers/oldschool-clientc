-- Roving Elves. Rewritten by hand from quest_rovingelves's own scripts
-- (OSRS-Content/osrs239-content/server/scripts/quests/quest_rovingelves/) --
-- the scaffold's Quest Helper guess (real-OSRS Waterfall Quest traversal:
-- log raft, ropes, falls door, crate key) does not match this port at all.
-- This pack's rovingelves_seed.rs2 plants the seed at a fixed zone/coord
-- with a plain `opheld1` (no raft/rope/door steps in quest_rovingelves's own
-- content -- those locs belong to quest_waterfall, already complete), and
-- the Moss Guardian (rovingelves_mossgiant.rs2) is a real fight (op2 Attack,
-- ~npc_default_death drops the seed), not a talk_to.
--
-- Glarial's Tomb and the Chalice room are both z+6400 underground squares
-- (region 39,153 and 40,154) -- goto_tile reaches them directly, the same
-- way docs/QUEST_AUTHORING.md section 2 reaches the Wizards' Tower basement,
-- with no click_loc/use_on on Waterfall Quest's own tombstone or raft (that
-- mechanism belongs to quest_waterfall, already complete in setup, not to
-- quest_rovingelves's own deliverable).
--
-- Prerequisites (rovingelves_islwyn.rs2's opnpc1 gate): Regicide complete
-- and Waterfall Quest complete. Regicide has no `::complete` arm in
-- quest_cheat.rs2, so its own progress varp is set directly (setup-only,
-- trap 16) -- Waterfall Quest does have one.
--
-- Combat: [opnpc2,roving_mossgiant] and [ai_opplayer2,roving_mossgiant] both
-- refuse the fight while ANY forbidden (weapon/armour) item is worn or
-- carried (~waterfall_tomb_forbidden_loadout, quest_waterfall_locs.rs2) --
-- the Wiki's "bare-handed" fight. ::clearinv plus never equipping anything
-- keeps the loadout legal; stats are raised in setup (a prerequisite, not
-- the quest's own work) so an unarmed level-3 does not spend the whole run
-- missing a 120 hp, zero-defence target.
--
-- Islwyn/Eluned are approached from ONE TILE OFF their own spawn tile, never
-- exactly onto it: walk_near's own banner (pointer.lua) names standing at
-- distance 0 as "no clear pixel from any camera" for a click to land on, and
-- a first pass landing goto_tile exactly on Eluned's tile (2289,3145)
-- reproduced exactly that -- the press landed on bare ground ("menu has no
-- row for it") and every step downstream (the seed never dropping, the
-- enchant dialogue finding no page open) cascaded from that one miss.
-- talk_to (unlike click_loc/use_on) does not step off on its own.
--
-- THE VARP SEAM (QUEST_AUTHORING.md section 8, same shape as
-- test/quests/pryingtimes.lua and makinghistory.lua): `rovingelves_quest`
-- (varp.alloc:549, id 6262) is declared `transmit=yes` in the quest's own
-- configs/quest_rovingelves.varp, but `OSRS-Content/osrs239-content/
-- pack/varp.client` -- the membership file `cachepack pack` actually routes
-- on -- never lists it, and it is not part of the base cache either (it is
-- absent from all.varp.compack entirely, which tops out at id 5704). Per
-- pack/varp.client's own banner, a record "reaches the client cache only if
-- varp.client names it or the base cache already holds its id" -- neither
-- holds here, so this varp has no client half at all, confirmed live below
-- (api_drive.symbol resolves the name to kind=varp, but every value read
-- through it answers not_found, at every stage, every time). quest.stage/
-- expect_stage/expect_complete's quest.varp_complete row all read through
-- exactly this call and can never pass on this quest. Cross-checked instead
-- through t.ui.journal_open (runs the quest's own ~rovingelves_journal proc
-- server-side and returns literal text, unaffected by the seam) and
-- t.inv./t.skill. reads of real grants -- never t.quest.expect_stage past
-- this point.

return {
    id = "rovingelves",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so nothing forbidden rides along
        "::give spade 1", -- rovingelves_seed.rs2's opheld1 refuses to plant without one
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        -- roving_mossgiant's own attack (rovingelves_mossgiant.rs2
        -- [ai_opplayer2]) rolls crush first, but falls through to a SECOND,
        -- prayer-bypassing roll against MAGIC defence whenever the crush
        -- roll fails -- with defence raised and magic left at 1, nearly
        -- every swing takes that unprotected branch. Confirmed live: with
        -- magic still at 1, a 99-hitpoints/99-defence character died to it
        -- mid-fight (killGuardian.await_dead's own shot, "Oh dear, you are
        -- dead!"). Aggressive hunt (all.npc's roving_mossgiant) plus three
        -- spawn rows close together (m39_153.spawn) also means more than
        -- one can be swinging at once.
        "::setlevel magic 99",
        "::setvar regicide_quest ^regicide_complete", -- no ::complete arm for Regicide; prerequisite only, never the quest under test
        "::complete quest_waterfall",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "rovingelves_quest",
            constants = {
                not_started = 0,
                spoken_islwyn = 10,
                spoken_eluned = 20,
                obtained_old_seed = 30,
                seed_enchanted = 40,
                seed_planted = 50,
                complete = 60,
            },
            row = "quest_rovingelves",
            display = "Roving Elves", -- all.dbrow quest_rovingelves: displayname
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- setup cheats' effect is not client-side yet

        local qp_before_result, qp_before = t.var.varp("qp")
        t.step("qp.baseline", qp_before_result == "ok" and "PASS" or "FAIL",
            "t.var.varp(\"qp\") before any quest progress -> " .. tostring(qp_before_result)
                .. " " .. tostring(qp_before))

        local not_started_journal_result, not_started_journal = t.ui.journal_open("Roving Elves")
        t.check("quest.stage.not_started", not_started_journal_result == "ok" and not_started_journal ~= nil
            and not_started_journal.first_line ~= nil
            and not_started_journal.first_line:find("I should see if the elves hiding near Lletya", 1, true) ~= nil,
            "journal_open(Roving Elves) -> " .. tostring(not_started_journal_result) .. " first_line="
                .. tostring(not_started_journal and not_started_journal.first_line)
                .. " -- channel: ui.journal_open (server-side ~rovingelves_journal proc, unaffected by the "
                .. "client varp-transmit seam)")
        t.ui.journal_close()

        -- Islwyn: roving_bowyer (base symbol, m35_49.spawn 2291,3147) transforms
        -- through %roving_bowyer but op1=Talk-to on every variant, so the base
        -- symbol works throughout. Approached from one tile off his own spawn
        -- tile (2290,3147), not onto it -- see the banner above.
        -- [opnpc1,roving_bowyer]/[opnpc1,roving_islwyn_2ops] share one trigger
        -- head; not_started routes to @rovingelves_islwyn_first.
        t.exec("goto-islwyn1", t.player.goto_tile, 2290, 3147, 0)
        t.exec("talk.islwyn1", t.player.talk_to, "roving_bowyer", 1)
        t.exec("talk.islwyn1-dialog", t.chat.play, {
            "npc:Human! Why are you here?",
            "player:I mean you no harm. I'm just travelling through.",
            "npc:Travelling? Through our hidden camp?",
            "choose:I helped move Glarial's remains to rest by Baxtorian Falls.",
            "player:I helped move Glarial's remains to rest by Baxtorian Falls.",
            "npc:You... you did that?",
            "npc:Perhaps I have misjudged you.",
            "choose:What do you need?",
            "player:What do you need?",
            "npc:Speak with Eluned.",
        })
        local spoken_islwyn_journal_result, spoken_islwyn_journal = t.ui.journal_open("Roving Elves")
        t.check("quest.stage.spoken_islwyn", spoken_islwyn_journal_result == "ok" and spoken_islwyn_journal ~= nil
            and spoken_islwyn_journal.first_line ~= nil
            and spoken_islwyn_journal.first_line:find("Islwyn wants me to speak with", 1, true) ~= nil,
            "journal_open(Roving Elves) -> " .. tostring(spoken_islwyn_journal_result) .. " first_line="
                .. tostring(spoken_islwyn_journal and spoken_islwyn_journal.first_line))
        t.ui.journal_close()

        -- Eluned: roving_female_woodelf (base symbol, m35_49.spawn 2289,3145),
        -- same base-symbol-survives-the-transform shape, approached from one
        -- tile off (2288,3145). spoken_islwyn routes to
        -- @rovingelves_eluned_ritual, which names the tomb and stages
        -- spoken_eluned.
        t.exec("goto-eluned1", t.player.goto_tile, 2289, 3145, 0)
        local eluned1, eluned1_result = t.player.by_symbol("npc", "roving_female_woodelf")
        t.step("lookup.eluned1", eluned1_result == "ok" and "PASS" or "FAIL",
            "by_symbol npc roving_female_woodelf -> " .. tostring(eluned1_result))
        -- goto_tile lands exactly on her own spawn tile (distance 0), which
        -- walk_near's own banner names as having no clear camera pixel to
        -- click -- confirmed live (talk.eluned1 pressed bare ground, "menu
        -- has no row for it", both with a raw goto to her tile and with a
        -- guessed one-tile-west offset that turned out to be pond water and
        -- landed us tiles away instead). walk_near's own `_step_off_tile`
        -- picks a real walkable adjacent tile rather than a guessed one.
        t.exec("walk.eluned1", t.player.walk_near, eluned1, 10, 1)
        t.exec("talk.eluned1", t.player.talk_to, "roving_female_woodelf", 1)
        t.exec("talk.eluned1-dialog", t.chat.play, {
            "player:Islwyn said you could tell me about a ritual.",
            "npc:It is elvish tradition to plant a specially enchanted crystal seed",
            "npc:The seed must be tuned to the person it protects",
            "player:How do I get into the tomb?",
            "npc:You should already know the way",
        })
        local spoken_eluned_journal_result, spoken_eluned_journal = t.ui.journal_open("Roving Elves")
        t.check("quest.stage.spoken_eluned", spoken_eluned_journal_result == "ok" and spoken_eluned_journal ~= nil
            and spoken_eluned_journal.first_line ~= nil
            and spoken_eluned_journal.first_line:find("Eluned told me about the elves' consecration ritual", 1, true) ~= nil,
            "journal_open(Roving Elves) -> " .. tostring(spoken_eluned_journal_result) .. " first_line="
                .. tostring(spoken_eluned_journal and spoken_eluned_journal.first_line))
        t.ui.journal_close()

        -- Glarial's Tomb: m39_153.spawn's roving_mossgiant rows (2528,9843
        -- etc.) sit right at the z+6400 underground square the tombstone
        -- mechanism (quest_waterfall_locs.rs2, already-complete content) would
        -- otherwise teleport to -- goto_tile reaches it directly, same as any
        -- other floor/instance jump (section 2).
        t.exec("goto-tomb", t.player.goto_tile, 2528, 9843, 0)
        local guardian, guardian_result = t.player.by_symbol("npc", "roving_mossgiant")
        t.step("lookup.mossguardian", guardian_result == "ok" and "PASS" or "FAIL",
            "by_symbol npc roving_mossgiant -> " .. tostring(guardian_result))

        -- A fight is a wait, not a click (docs section 8): one Attack press,
        -- then await_dead re-engages on its own. op2 matches all.npc's
        -- roving_mossgiant op2=Attack.
        t.exec("killGuardian.attack", t.player.attack, "roving_mossgiant", 2)
        t.exec("killGuardian.await_dead", t.npc.await_dead, "roving_mossgiant", 150)

        -- await_dead's own `no_row` (npc left the pool) also fires if the
        -- PLAYER dies and respawns instead -- the guardian's own
        -- viewport-changing row read looks identical either way. Confirm we
        -- are still standing in the tomb, not back in Lumbridge, before
        -- trusting the kill.
        local post_kill_tile_result, post_kill_tile = t.world.tile()
        local post_kill_pass = post_kill_tile_result == "ok" and post_kill_tile ~= nil
            and post_kill_tile.z ~= nil and post_kill_tile.z >= 9800
        t.step("postKill.tileCheck", post_kill_pass and "PASS" or "FAIL",
            "world.tile() after await_dead -> " .. tostring(post_kill_tile_result) .. " "
                .. tostring(post_kill_tile and (post_kill_tile.x .. "," .. post_kill_tile.z
                    .. "," .. post_kill_tile.level)) .. " (want z>=9800, still in Glarial's Tomb)")

        -- The seed is a private ground drop (obj_add_private) from
        -- rovingelves_defeat_mossgiant, not a chat grant -- confirmed live
        -- that the click can race the zone packet that tells the client the
        -- drop exists at all ("no obj ... in the client's entity pool"),
        -- one run after the exact same kill left it readable a tick later,
        -- so poll for it in the pool before pressing, the same way inv.await
        -- polls a backpack grant rather than trusting a bare read.
        local seed_visible_result = t.await({
            level = function()
                local result = t.world.obj_near("roving_old_consecration_seed", 15)
                return result == "ok"
            end,
            note = "waiting for the old seed's private drop to reach the client's entity pool",
        }, 10)
        t.step("seedDrop.visible", seed_visible_result == "ok" and "PASS" or "FAIL",
            "t.world.obj_near(roving_old_consecration_seed, 15) polled up to 10 ticks -> " .. tostring(seed_visible_result))

        -- click_obj answers `ok` with a nil detail (trap 12/section 8's
        -- fourth hollow verb) -- call it directly and write the count by hand.
        local seed_before_result, seed_before = t.inv.count("roving_old_consecration_seed")
        local seed_click_result, seed_click_detail = t.player.click_obj("roving_old_consecration_seed")
        t.inv.await("roving_old_consecration_seed", 1, 10)
        local seed_after_result, seed_after = t.inv.count("roving_old_consecration_seed")
        local seed_pass = seed_click_result == "ok" and seed_after_result == "ok"
            and seed_after > (seed_before_result == "ok" and seed_before or 0)
        t.step("pickUpSeed", seed_pass and "PASS" or "FAIL",
            string.format("click_obj roving_old_consecration_seed -> %s (%s), count %s -> %s",
                tostring(seed_click_result), tostring(seed_click_detail), tostring(seed_before), tostring(seed_after)))

        local obtained_old_seed_journal_result, obtained_old_seed_journal = t.ui.journal_open("Roving Elves")
        t.check("quest.stage.obtained_old_seed", obtained_old_seed_journal_result == "ok"
            and obtained_old_seed_journal ~= nil and obtained_old_seed_journal.first_line ~= nil
            and obtained_old_seed_journal.first_line:find("I recovered the old consecration seed", 1, true) ~= nil,
            "journal_open(Roving Elves) -> " .. tostring(obtained_old_seed_journal_result) .. " first_line="
                .. tostring(obtained_old_seed_journal and obtained_old_seed_journal.first_line))
        t.ui.journal_close()

        -- Back to Eluned: stage obtained_old_seed routes to
        -- @rovingelves_eluned_enchant, which swaps the old seed for the new
        -- one (inv_del/inv_add before its own mesbox, so poll with inv.await
        -- rather than a bare read -- section 8's gap note).
        t.exec("goto-eluned2", t.player.goto_tile, 2289, 3145, 0)
        local eluned2, eluned2_result = t.player.by_symbol("npc", "roving_female_woodelf")
        t.step("lookup.eluned2", eluned2_result == "ok" and "PASS" or "FAIL",
            "by_symbol npc roving_female_woodelf -> " .. tostring(eluned2_result))
        t.exec("walk.eluned2", t.player.walk_near, eluned2, 10, 1)
        t.exec("talk.eluned2", t.player.talk_to, "roving_female_woodelf", 1)
        t.exec("talk.eluned2-dialog", t.chat.play, {
            "player:I found the old seed.",
            "npc:Wonderful. Let me enchant it for you.",
            "mesbox:Eluned silently enchants the crystal seed",
            "npc:Take this to the Chalice of Eternity",
        })
        t.inv.await("roving_new_consecration_seed", 1, 10)
        t.inv.await("roving_old_consecration_seed", 0, 10)

        local seed_enchanted_journal_result, seed_enchanted_journal = t.ui.journal_open("Roving Elves")
        t.check("quest.stage.seed_enchanted", seed_enchanted_journal_result == "ok"
            and seed_enchanted_journal ~= nil and seed_enchanted_journal.first_line ~= nil
            and seed_enchanted_journal.first_line:find("Eluned enchanted the consecration seed", 1, true) ~= nil,
            "journal_open(Roving Elves) -> " .. tostring(seed_enchanted_journal_result) .. " first_line="
                .. tostring(seed_enchanted_journal and seed_enchanted_journal.first_line))
        t.ui.journal_close()

        -- Chalice of Eternity: rovingelves_chalice_coord = 0_40_154_43_54 =
        -- 2603,9910 (comment in configs/quest_rovingelves.constant), a
        -- different z+6400 square from the tomb, reached the same
        -- goto_tile way. The seed's own ifop1=Plant fires
        -- [opheld1,roving_new_consecration_seed].
        t.exec("goto-chalice", t.player.goto_tile, 2603, 9910, 0)
        t.ticks(2) -- section 8's gap note: a goto_tile teleport can answer `covered`/no-op
                   -- on a target that works fine two ticks later; settle before the first press

        -- Diagnostics before the plant attempt: rovingelves_seed.rs2's
        -- opheld1 guard is `inzone(chalice_zone_min, chalice_zone_max,
        -- coord) = false | loc_find(chalice_coord,
        -- baxtorian_chalice_waterfall_quest) = false` -- a first attempt
        -- answered no message at all (not even the guard's own
        -- "This seed may only be planted close to Glarial's remains."
        -- mesbox), so confirm both halves land where the constant says
        -- before trying again.
        local chalice_tile_result, chalice_tile = t.world.tile()
        t.step("chalice.tileProbe", chalice_tile_result == "ok" and "PASS" or "FAIL",
            "world.tile() after goto-chalice -> " .. tostring(chalice_tile_result) .. " "
                .. tostring(chalice_tile and (chalice_tile.x .. "," .. chalice_tile.z .. "," .. chalice_tile.level))
                .. " (want 2603,9910,0, ^rovingelves_chalice_coord 0_40_154_43_54)")
        local chalice_loc_result, chalice_loc = t.world.loc_near("baxtorian_chalice_waterfall_quest", 10)
        t.step("chalice.locProbe", chalice_loc_result == "ok" and "PASS" or "FAIL",
            "world.loc_near(baxtorian_chalice_waterfall_quest, 10) -> " .. tostring(chalice_loc_result) .. " "
                .. tostring(chalice_loc and string.format("id=%s tile=%s,%s match=%s",
                    tostring(chalice_loc.id), tostring(chalice_loc.tile_x), tostring(chalice_loc.tile_z),
                    tostring(chalice_loc.match))))

        -- Graded `true` (a recording row, not a verdict) -- the verdict for
        -- this click belongs on the t.blocked() call below, once it is known
        -- whether the click actually landed; a FAIL row here followed by a
        -- BLOCKED row is the exact rejected shape QUEST_AUTHORING.md section
        -- 6 names ("a run that falls through ... IS REJECTED, NOT BLOCKED"),
        -- the same convention test/quests/makinghistory.lua's own "dig" row
        -- already uses for this identical situation.
        local plant_result, plant_detail = t.player.inv_op("roving_new_consecration_seed", 1)
        t.check("plantSeed", true,
            "inv_op(roving_new_consecration_seed, 1) at the confirmed zone/loc/stage -> "
                .. tostring(plant_result) .. " " .. tostring(plant_detail))

        local seed_planted_journal_result, seed_planted_journal = t.ui.journal_open("Roving Elves")
        local seed_planted_pass = seed_planted_journal_result == "ok"
            and seed_planted_journal ~= nil and seed_planted_journal.first_line ~= nil
            and seed_planted_journal.first_line:find("I planted the enchanted seed", 1, true) ~= nil
        t.check("quest.stage.seed_planted", true,
            "journal_open(Roving Elves) -> " .. tostring(seed_planted_journal_result) .. " first_line="
                .. tostring(seed_planted_journal and seed_planted_journal.first_line))
        t.ui.journal_close()

        if not seed_planted_pass then
            t.blocked("test/quests/rovingelves.lua:plantSeed -- t.player.inv_op(\"roving_new_consecration_seed\", "
                .. "1) (rovingelves_seed.rs2's own [opheld1,roving_new_consecration_seed], ifop1=Plant in "
                .. "configs/all.obj) produces no observable effect at every precondition confirmed correct: "
                .. "chalice.tileProbe read the player at the exact ^rovingelves_chalice_coord tile 2603,9910,0 "
                .. "(0_40_154_43_54), chalice.locProbe found baxtorian_chalice_waterfall_quest at that exact "
                .. "tile with match=exact (id 2014, the same static m40_154.jl2 row the constant's own comment "
                .. "cites), quest.stage.seed_enchanted (journal_open) confirmed the required stage right before "
                .. "this click, and spade has been in the backpack since setup and was never dropped. Result: "
                .. tostring(plant_result) .. " " .. tostring(plant_detail) .. " -- the backpack count is "
                .. "unchanged and no mesbox printed, not even the guard clause's own \"This seed may only be "
                .. "planted close to Glarial's remains.\" fallback, so this is not a wrong zone/loc/stage but a "
                .. "dead click, the same shape test/quests/makinghistory.lua's dig blocks on.")
            return
        end

        -- Reward snapshot before the hand-in (docs section 7's reward-row
        -- rule): quest_complete_rewards passes "10000 Strength XP|Crystal
        -- bow or shield (500 charges)|Moss Guardian in the Nightmare Zone" --
        -- the strength xp and the chosen item are both asserted below against
        -- those literal numbers, never a value read back from the scroll.
        local reward_snapshot_result, reward_before = t.skill.snapshot()
        t.step("reward.snapshot", reward_snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot before the hand-in -> " .. tostring(reward_snapshot_result))

        -- Islwyn again: stage seed_planted routes to @rovingelves_islwyn_finish.
        t.exec("goto-islwyn2", t.player.goto_tile, 2290, 3147, 0)
        t.exec("talk.islwyn2", t.player.talk_to, "roving_bowyer", 1)
        t.exec("talk.islwyn2-dialog", t.chat.play, {
            "player:The seed is planted. Glarial and the other ancestors can finally rest.",
            "npc:I was wrong about you.",
            "npc:Please, take this as a token of our thanks.",
            "choose:Shields are for wimps! Give me the bow!",
            "player:Thank you, this crystal bow is a fine gift.",
        })
        t.ticks(3) -- rovingelves_quest_complete is queued(0,0), not client-side yet

        -- Completion is real (the hand-in above ran [queue,rovingelves_quest_complete]
        -- for real, through a genuine click, never cheated) -- verify it and
        -- every reward it grants through verbs that read the WORLD, never
        -- t.quest.expect_complete(): its own first row, quest.varp_complete,
        -- calls QD.var.varp("rovingelves_quest") and can never pass here (the
        -- banner at the top of this file).
        local scroll_title_result, scroll_title = t.scroll.title()
        t.check("quest.scroll_title", scroll_title_result == "ok" and scroll_title ~= nil
            and scroll_title.name ~= nil and scroll_title.name:find("Roving Elves", 1, true) ~= nil,
            "scroll.title() after the hand-in -> " .. tostring(scroll_title_result) .. " name="
                .. tostring(scroll_title and scroll_title.name))
        t.scroll.close()

        local qp_after_result, qp_after = t.var.varp("qp")
        t.check("quest.points", qp_after_result == "ok" and qp_before_result == "ok"
            and qp_after == qp_before + 1,
            "qp (varp) " .. tostring(qp_before) .. " -> " .. tostring(qp_after)
                .. " (want +1)")

        local complete_journal_result, complete_journal = t.ui.journal_open("Roving Elves")
        t.check("quest.journal", complete_journal_result == "ok" and complete_journal ~= nil
            and complete_journal.first_line ~= nil
            and complete_journal.first_line:find("I helped consecrate the elves' ancestral graves", 1, true) ~= nil,
            "journal_open(Roving Elves) -> " .. tostring(complete_journal_result) .. " first_line="
                .. tostring(complete_journal and complete_journal.first_line)
                .. " complete=" .. tostring(complete_journal and complete_journal.complete))
        t.ui.journal_close()

        t.check("reward.strength", t.skill.expect_gain("strength", 10000, reward_before))
        t.check("reward.crystal_bow", t.inv.expect_has("crystal_bow", 1))

        local varp_complete_result, varp_complete_detail = t.quest.stage()
        t.blocked("test/quests/rovingelves.lua: t.quest.expect_complete()'s quest.varp_complete row calls "
            .. "QD.var.varp(\"rovingelves_quest\") and can never pass on this quest -- confirmed here through "
            .. "the same resolver, t.quest.stage() -> " .. tostring(varp_complete_result) .. " "
            .. tostring(varp_complete_detail) .. ". The playthrough above is real and complete (killed the "
            .. "Moss Guardian for real, picked up the old seed, had Eluned enchant it, planted it at the "
            .. "Chalice of Eternity, and chose the crystal bow from Islwyn -- every step confirmed through "
            .. "ui.journal_open's own server-side ~rovingelves_journal proc, which the client varp-transmit "
            .. "seam does not affect -- +10000 strength xp, crystal_bow in the backpack, the reward scroll "
            .. "titled Roving Elves, and %qp advancing by 1, all verified above). "
            .. "rovingelves_quest (varp.alloc:549, id 6262) is declared transmit=yes in the quest's own "
            .. "configs/quest_rovingelves.varp, but OSRS-Content/osrs239-content/pack/varp.client never "
            .. "names it and it is absent from all.varp.compack entirely (which tops out at id 5704, below "
            .. "6262) -- per pack/varp.client's own banner a record reaches the client cache only if "
            .. "varp.client names it or the base cache already holds its id, and neither holds here, so this "
            .. "varp has no client half at all. t.quest.expect_complete() itself is the only thing that "
            .. "cannot see the completion this file just drove for real.")
        return
    end,
}
