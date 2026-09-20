-- Prince Ali Rescue -- BLOCKED before the quest can even be accepted, and
-- proved live below rather than left as a grep claim.
--
-- Two independent, verified breaks, in the order a real playthrough hits
-- them:
--
-- 1. Hassan -- who grants the quest (`^prince_not_started` ->
--    `^prince_started`, `[opnpc1,hassan]`,
--    OSRS-Content/osrs239-content/server/scripts/areas/alkharid/scripts/
--    hassan.rs2:2-31) -- has ZERO `*.spawn` rows anywhere in this content
--    pack: `grep -rn -i hassan --include='*.spawn' server/scripts` returns
--    nothing across all 984 `*.spawn` files, and he is not wired as a
--    `multinpc*=hassan` child of any spawned entity either (`grep -n
--    'multinpc.*=hassan\b' configs/all.npc` -- zero hits). A live
--    `goto_tile(3298,3163,0)` (Quest Helper's own WorldPoint) then
--    `talk_to("hassan",1)` below answers `screen_position: no npc 4285
--    (hassan) in the client's entity pool` -- proved, not assumed.
--
-- 2. Osman IS spawned (`contact_osman_multi`,
--    areas/world/configs/m51_49.spawn:3286,3180,0, a `multinpc1..10=osman`
--    parent), so `talk_to("osman",1)` below genuinely lands -- but the
--    live text it opens on is not Prince Ali Rescue's dialogue at all:
--    `quest_contact/scripts/contact_osman.rs2`'s OWN
--    `[opnpc1,contact_osman_multi]` (lines 11-14) fires on that same
--    spawned entity ahead of `osman.rs2`'s `[opnpc1,osman]` princequest
--    switch, and while `%contact < ^contact_met_maisa` (true for any
--    character that has not progressed the unrelated "Contact!" quest,
--    which this fixture has not) it unconditionally answers "Osman has
--    business to attend to." and returns -- osman.rs2's own
--    `osman_prequest`/`osman_instructions` branches are unreachable from a
--    fresh character. Checked live below with `t.chat.expect_text`, not
--    guessed from the source alone.
--
-- Together: Hassan cannot be reached to accept the quest, and Osman's own
-- Prince Ali Rescue lines are gated behind a different quest's progress on
-- the one npc that IS spawned -- princequest cannot leave `not_started` (0)
-- through real play at all, so every later step (Lady Keli's key print,
-- Joe's three beers, the rope-tie, the rescue) is unreachable a fortiori.
-- Checked those three anyway, live, for the same static claim ([opnpc1,
-- lady_keli]/[opnpc1,joe]/[opnpc1,prince_ali_prison], areas/draynor/scripts/
-- {lady_keli,joe,prince_ali}.rs2): none of lady_keli/lady_keli_vis
-- (multinpc1-10), joe/joe_vis, or prince_ali_prison/prince_ali_vis(_blackeye)
-- has a single `*.spawn` row either, while this same quest's OTHER Draynor
-- npcs -- ned, aggie, leela -- do, all three in
-- areas/world/configs/m48_50.spawn (so this is not a goto aimed wrong,
-- trap 13's usual fix).

return {
    id = "prince",
    fixture = "fresh_lumbridge.ini",
    setup = {},

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "princequest",
            constants = {
                not_started = 0,
                started = 10,
                spoken_osman = 20,
                prep_finished = 30,
                guard_drunk = 40,
                tied_keli = 50,
                saved = 100,
                complete = 110,
                questpoints = 3,
                keymade = 1,
                keyclaimed = 2,
            },
            row = "quest_princealirescue",
            display = "Prince Ali Rescue",
            points = 3,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- --------------------------------------------- Hassan: not reachable
        -- Quest Helper's own WorldPoint (3298,3163,0). goto succeeds (a
        -- bare tile teleport); the npc does not.
        t.exec("goto-hassan", t.player.goto_tile, 3298, 3163, 0)
        t.ticks(2)

        -- player.by_symbol resolves the SYMBOL's definition (it answers ok
        -- for any valid npc type, spawned or not -- proved on lady_keli
        -- below), so live presence is checked with npc.nearest instead,
        -- which walks the actual entity pool within radius.
        local hassan_nearest_result, hassan_nearest_row = t.npc.nearest("hassan", 15)
        t.check("hassan.nearest", hassan_nearest_result ~= "ok",
            "npc.nearest(hassan, 15) at 3298,3163,0 -> "
                .. tostring(hassan_nearest_result) .. " " .. tostring(hassan_nearest_row)
                .. " -- expected not ok: hassan has zero *.spawn rows"
                .. " anywhere in this content pack, and no multinpc*=hassan"
                .. " child of any spawned entity either")

        local hassan_talk_result, hassan_talk_detail = t.player.talk_to("hassan", 1)
        t.check("hassan.talk_to", hassan_talk_result ~= "ok",
            "talk_to(hassan, op 1) at Quest Helper's own WorldPoint -> "
                .. tostring(hassan_talk_result) .. " " .. tostring(hassan_talk_detail)
                .. " -- expected not ok: the quest cannot be accepted"
                .. " (^prince_not_started -> ^prince_started needs this exact"
                .. " talk, hassan.rs2:2-10) if hassan is never in the world")

        -- --------------------------------------------- Osman: wrong dialogue
        -- Osman IS spawned (contact_osman_multi, areas/world/configs/
        -- m51_49.spawn, a multinpc1..10=osman parent) so this click lands
        -- for real -- but quest_contact/scripts/contact_osman.rs2's own
        -- [opnpc1,contact_osman_multi] answers first, ahead of osman.rs2's
        -- own [opnpc1,osman] princequest switch, while %contact hasn't
        -- reached ^contact_met_maisa (true here). Proved on the live text,
        -- not assumed from the source.
        t.exec("goto-osman", t.player.goto_tile, 3286, 3180, 0)
        t.exec("osman.talk", t.player.talk_to, "osman", 1)
        t.check("osman.gated_by_contact", t.chat.expect_text("Osman has business to attend to."),
            "contact_osman.rs2 [opnpc1,contact_osman_multi] answers before"
                .. " osman.rs2's own princequest switch can -- osman_prequest/"
                .. "osman_instructions are unreachable while %contact <"
                .. " ^contact_met_maisa, so talking to the one Osman entity"
                .. " that exists cannot progress princequest either")

        t.expect("quest.stage.not_started.still", t.quest.expect_stage("not_started"))

        -- --------------------------------------------- Lady Keli: not there
        -- Quest Helper's own WorldPoint for talkToKeli (3127,3244,0) --
        -- driven to and checked live, not assumed from the file-tree grep
        -- in the banner above.
        t.exec("goto-keli", t.player.goto_tile, 3127, 3244, 0)
        t.ticks(2)

        local keli_nearest_result, keli_nearest_row = t.npc.nearest("lady_keli", 15)
        t.check("keli.nearest", keli_nearest_result ~= "ok",
            "npc.nearest(lady_keli, 15) at 3127,3244,0 -> "
                .. tostring(keli_nearest_result) .. " " .. tostring(keli_nearest_row)
                .. " -- expected not ok: lady_keli has zero *.spawn rows"
                .. " anywhere in this content pack")

        local talk_result, talk_detail = t.player.talk_to("lady_keli", 1)
        t.check("keli.talk_to", talk_result ~= "ok",
            "talk_to(lady_keli, op 1) at Quest Helper's own WorldPoint -> "
                .. tostring(talk_result) .. " " .. tostring(talk_detail)
                .. " -- expected not ok: lady_keli/lady_keli_vis (multinpc1-10),"
                .. " joe/joe_vis and prince_ali_prison/prince_ali_vis(_blackeye)"
                .. " each resolve in configs/all.npc but have zero *.spawn rows"
                .. " across all 984 *.spawn files under server/scripts -- ned,"
                .. " aggie and leela, this same quest's other Draynor npcs, DO"
                .. " have rows in areas/world/configs/m48_50.spawn, so this is"
                .. " not a goto aimed wrong")

        t.blocked("hassan [opnpc1,hassan in OSRS-Content/osrs239-content/server/"
            .. "scripts/areas/alkharid/scripts/hassan.rs2] has zero *.spawn rows "
            .. "anywhere in this content pack and no multinpc*=hassan child of "
            .. "any spawned entity either, confirmed live above (goto 3298,3163,0"
            .. " then talk_to -> screen_position: no npc in the client's entity"
            .. " pool) -- the quest cannot be accepted. The one Osman entity that"
            .. " IS spawned (contact_osman_multi) answers through"
            .. " quest_contact/scripts/contact_osman.rs2's own"
            .. " [opnpc1,contact_osman_multi] ('Osman has business to attend to.',"
            .. " confirmed live above) before osman.rs2's own princequest switch"
            .. " ever runs, while %contact < ^contact_met_maisa -- so princequest"
            .. " cannot leave not_started (0) through real play at all. Lady Keli"
            .. " [lady_keli.rs2], Joe [joe.rs2] and prince_ali_prison"
            .. " [prince_ali.rs2], needed for every step from spoken_osman (20)"
            .. " to saved (100), have zero *.spawn rows either (checked live"
            .. " above too) -- so this is blocked at the very first step and"
            .. " every step after it, not one seam but the whole quest ladder")
        return
    end,
}
