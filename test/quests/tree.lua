-- Tree Gnome Village -- hand-written against the quest's own scripts
-- (OSRS-Content/osrs239-content/server/scripts/quests/quest_tree/,
-- areas/area_gnome/scripts/{king_bolren,commander_montai,elkoy}.rs2) and
-- Quest Helper's TreeGnomeVillage.java step ladder. Tier 2.
--
-- CONTENT BUG (run 1, 2026-09-28): every single King Bolren interaction is
-- unreachable for a fresh character. The Path of Glouphrie's own additive
-- hub, wired into King Bolren by
-- OSRS-Content/osrs239-content/server/scripts/areas/area_gnome/scripts/
-- king_bolren.rs2:12-15 (`if (~pog_king_bolren_hub = 1) { return; }`, the
-- FIRST lines of [opnpc1,king_bolren]), calls
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_pathofglouphrie/
-- scripts/pog_quest.rs2's [proc,pog_king_bolren_hub] (lines 11-66). That
-- proc's own `%pog = ^pog_not_started` branch (line 44-49, pog_not_started
-- = 0, the default for a fresh character, per
-- quest_pathofglouphrie/configs/pathofglouphrie.constant:198) is:
--     if (%pog = ^pog_not_started) {
--         if (~pog_qualify_fail_reason ! 0) {
--             ~pog_show_qualify_fail;
--             return(1);
--         }
--         ... POG's own Dumpling/Advisor start dialogue, also return(1) ...
--     }
-- `pog_qualify_fail_reason` (pog_quest.rs2:69-93) returns 1 whenever
-- %eyeglo_quest < ^eyeglo_complete -- true for every fresh character -- so
-- EVERY branch of pog_king_bolren_hub for %pog=0 ends in return(1) ("handled
-- by POG"), and the proc's only `return(0)` (fall through to Tree Gnome
-- Village's own switch) is gated on `%pog >= ^pog_complete` (50). There is
-- no path from %pog=0 back to king_bolren.rs2's own %treequest switch at
-- all. So talking to King Bolren always opens the mesbox "You need to
-- complete The Eyes of Glouphrie before starting The Path of Glouphrie."
-- instead of any Tree Gnome Village dialogue -- reproduced live below
-- (bolren.greet/bolren.pog_gate_text), blocking the quest's own start (and,
-- by the same gate, its first-orb hand-in and completion dialogue, both of
-- which also talk to king_bolren) end to end, for any character who has not
-- separately finished Path of Glouphrie's own prerequisite chain. This is
-- not fixable from a quest test file (script/plugins/, src/, tools/ and
-- OSRS-Content/ are all off limits here) -- the fix belongs in
-- pog_king_bolren_hub, which needs a guard (e.g. on %treequest, since Tree
-- Gnome Village is itself one of Path of Glouphrie's prerequisites and a
-- player mid-Tree-Gnome-Village cannot yet be "in" Path of Glouphrie at
-- all) before it unconditionally swallows every King Bolren interaction.
--
-- The rest of this file (setup, the maze-is-pure-navigation goto_tile, the
-- gnome_amulet/logs/tracker plan) is written against the quest's real
-- scripts and Quest Helper's TreeGnomeVillage.java step ladder, ready to
-- resume once the King Bolren gate above is fixed.

return {
    id = "tree",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::give logs 6", -- brought-along prerequisite for Commander Montai (Quest Helper sixLogs)
        "::give rune_scimitar 1", -- combat prerequisite (Quest Helper combatGear)
        "::give rune_full_helm 1",
        "::give rune_chainbody 1", -- rune_platebody needs Dragon Slayer complete (real OSRS mechanic, measured run 1) -- chainbody does not
        "::give rune_platelegs 1",
        "::give rune_kiteshield 1",
        "::give shark 6", -- recommended food
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "treequest",
            constants = {
                not_started = 0,
                started = 1,
                spoken_montai = 2,
                given_logs_montai = 3,
                finding_trackers = 4,
                ballista_fired = 5,
                retrieved_orb = 6,
                returned_first_orb = 7,
                defeated_warlord = 8,
                complete = 9,
            },
            display = "Tree Gnome Village",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- a setup cheat's effect is not client-side yet
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Gear up before travelling -- ::give is not equip (trap 177).
        t.exec("equip.weapon", t.player.equip, "rune_scimitar")
        t.exec("equip.helm", t.player.equip, "rune_full_helm")
        t.exec("equip.body", t.player.equip, "rune_chainbody")
        t.exec("equip.legs", t.player.equip, "rune_platelegs")
        t.exec("equip.shield", t.player.equip, "rune_kiteshield")

        -- talkToBolrenAtCentreOfMaze: the Tree Gnome maze is pure navigation
        -- (king_bolren.rs2's own switch reads only %treequest/inv, no
        -- lever/door/maze-progress state), the same precedent
        -- QUEST_AUTHORING.md section 8 gives for Ernest the Chicken's
        -- six-lever maze -- so goto_tile straight to the centre is legal
        -- evidence, not a puzzle bypass.
        t.exec("goto-bolren", t.player.goto_tile, 2541, 3170, 0)
        t.exec("bolren.greet", t.player.talk_to, "king_bolren", 1)

        -- CONTENT BUG reproduction (see file banner): the page that opened
        -- is Path of Glouphrie's qualify-fail mesbox, not Tree Gnome
        -- Village's own "Well hello stranger" opener.
        t.exec("bolren.pog_gate_text", t.chat.expect_text, "Eyes of Glouphrie")
        t.exec("bolren.pog_gate_dismiss", t.chat.continue_, true)

        t.blocked("content_bug: OSRS-Content/osrs239-content/server/scripts/quests/quest_pathofglouphrie/scripts/pog_quest.rs2:44-49 ([proc,pog_king_bolren_hub]'s %pog=^pog_not_started branch always ends return(1), never falling through) + areas/area_gnome/scripts/king_bolren.rs2:12-15 (the additive `if (~pog_king_bolren_hub = 1) { return; }` gate) -- every King Bolren interaction for a fresh character (%pog defaults to 0) shows 'You need to complete The Eyes of Glouphrie before starting The Path of Glouphrie.' instead of any Tree Gnome Village dialogue, so the quest cannot be started (or, downstream, handed in) through King Bolren at all; not fixable from this test file")
        return
    end,
}
