-- Tree Gnome Village -- hand-written against the quest's own scripts
-- (OSRS-Content/osrs239-content/server/scripts/quests/quest_tree/,
-- areas/area_gnome/scripts/{king_bolren,commander_montai,elkoy}.rs2) and
-- Quest Helper's TreeGnomeVillage.java step ladder. Tier 2.
--
-- RE-AUTHOR after 08782520b [seam23]: King Bolren's PoG hub now falls
-- through for an unqualified player (OSRS-Content 33c8b2ac6a), so the
-- quest starts from Bolren normally -- no more content_bug block there.
--
-- Resumed from build/seam_state/seam23/tree_fix/tree_full.lua (the 66/66
-- reference), with one change: that file's single goto_tile from the
-- ballista straight to (2505,3258,1) skipped BOTH the crumbled wall and the
-- ladder the guide names (TreeGnomeVillage.java's cRetrieveOrb: "Enter the
-- tower by the Crumbled wall and climb the ladder to retrieve the first
-- orb from chest."). "wall" is a GATE_WORDS hit and rule (b) applies --
-- click it for real. The wall's own [oploc1,khazzacklowwall] trigger
-- (quest_tree_locs.rs2) requires %treequest >= ^tree_ballista_fired and is
-- approached from the SOUTH (it refuses "You can't get over the wall from
-- this side." from the north/inside), plays a mesbox and a forced climb
-- animation, and lands the player across it. The ladder itself has no
-- quest-specific trigger anywhere in quest_tree/ or area_gnome/ (grepped),
-- so it is plain TRAVEL_WORDS travel per QUEST_AUTHORING.md section 2 --
-- goto_tile onto ITS OWN tile at its level (2503,3252,1) is legitimate
-- driving, not a second cheat.
--
-- Four guide steps are alternate ways to a leg this file already drives
-- another way (helper_coverage.py's ANY-OF vocabulary):
-- ANY-OF: goThroughMaze talkToKingBolren king_bolren.rs2:18 ([opnpc1,king_bolren]'s dispatch reads only %treequest, no maze/lever/door state -- the same pure-navigation precedent QUEST_AUTHORING.md section 8 gives Ernest the Chicken's maze) -- goto-bolren + bolren.greet reach and talk to him directly instead of walking the marked path
-- ANY-OF: elkoySkip talkToKingBolrenFirstOrb elkoy.rs2:79 (Elkoy's own "Yes please" -> p_telejump is the identical centre-of-maze shortcut a goto_tile is) -- bolren.first_orb (TreeGnomeVillage.java:267-269's insideGnomeVillage branch) drives the same first-orb hand-in
-- ANY-OF: elkoySkip2 returnOrbs elkoy.rs2:146 (same Elkoy "Yes please" shortcut as elkoySkip, offered again once the orbs are recovered) -- bolren.orbs (TreeGnomeVillage.java:283's insideGnomeVillage branch) drives the same orbs hand-in
-- ANY-OF: pickupOrb warlord.satchel khazard_warlord.rs2:110 (the kill's own inv_add(inv, orbs_of_protection, 1) grants the orbs straight into the backpack -- they never land on the ground, so orbsOfProtectionNearby never trips in this port)

return {
    id = "tree",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        -- Attack is left at its fresh-character level on purpose: the
        -- quest's own reward is stat_advance(attack, 114500) --
        -- khazard_warlord.rs2:244 -- a FLOOR, not an add (measured run 1:
        -- an already-::setlevel'd 99 Attack reads delta=0, the same
        -- "before==after" a stat_advance no-op always would). Quest
        -- Helper's own combatGear hint is "magic is best" -- fire_bolt vs
        -- this npc's magic=1 (quest_tree.npc) is effectively unmissable,
        -- so the warlord is fought with magic and Attack XP stays
        -- observable for the reward row.
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::setlevel magic 99",
        "::give logs 6", -- brought-along prerequisite for Commander Montai (Quest Helper sixLogs)
        "::give rune_full_helm 1", -- combat prerequisite (Quest Helper combatGear)
        "::give rune_chainbody 1", -- rune_platebody needs Dragon Slayer complete (real OSRS mechanic, measured run 1) -- chainbody does not
        "::give rune_platelegs 1",
        "::give rune_kiteshield 1",
        "::give chaosrune 60", -- fire_bolt: 1 chaos + 4 fire + 3 air per cast (magic_combat_spells.dbrow)
        "::give firerune 200",
        "::give airrune 150",
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

        -- Gear up before travelling -- ::give is not equip (trap 177). No
        -- weapon: the warlord is fought with magic (see setup banner), and
        -- an equipped weapon's auto-retaliate would land unrelated melee
        -- hits at the untouched Attack level, muddying the fight's own
        -- evidence for no benefit -- the runes carry the damage.
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
        -- king_bolren.rs2 ^tree_not_started: TGV's own opener (seam23: the
        -- PoG hub now falls through for an unqualified player).
        t.exec("bolren.accept", t.chat.play, {
            "player:Hello.",
            "npc:Well hello stranger.",
            "npc:I'm surprised you made it in",
            "player:Maybe.",
            "npc:I'm afraid I have more serious concerns",
            "choose:Can I help at all?",
            "player:Can I help at all?",
            "npc:I'm glad you asked.",
            "npc:The truth is my people are in grave danger.",
            "npc:We are not a violent race",
            "npc:We became desperate",
            "npc:Khazard troops seized the orb.",
            "player:How can I help?",
            "npc:You would be a huge benefit",
            "choose:I would be glad to help.",
            "player:I would be glad to help.",
            "npc:Thank you. The battlefield is to the north",
            "npc:That is if he's still alive.",
            "npc:My assistant shall guide you out.",
        })
        t.exec("stage.started", t.var.await_server, "treequest", 1, 10)

        t.exec("goto-montai", t.player.goto_tile, 2523, 3207, 0)
        t.exec("montai.talk", t.player.talk_to, "commander_montai", 1)
        t.exec("montai.accept", t.chat.play, {
            "player:Hello.",
            "npc:Hello traveller, are you here to help",
            "player:I've been sent by King Bolren",
            "npc:Excellent we need all the help",
            "npc:I'm commander Montai.",
            "player:What can I do?",
            "npc:Firstly we need to strengthen",
            "choose:Ok, I'll gather some wood.",
            "player:Ok, I'll gather some wood.",
            "npc:Please be as quick as you can",
        })
        t.exec("stage.spoken_montai", t.var.await_server, "treequest", 2, 10)
        t.exec("montai.logs_talk", t.player.talk_to, "commander_montai", 1)
        t.exec("montai.logs", t.chat.play, {
            "player:Hello.",
            "npc:Hello again, we're still desperate for wood soldier.",
            "player:I have some here.",
            "npc:That's excellent",
        })
        t.exec("stage.given_logs", t.var.await_server, "treequest", 3, 10)
        t.exec("logs.gone", t.inv.expect_absent, "logs")
        t.exec("montai.trackers_talk", t.player.talk_to, "commander_montai", 1)
        t.exec("montai.trackers", t.chat.play, {
            "player:How are you doing Montai?",
            "npc:We're hanging in there soldier.",
            "npc:The ballista can break through",
            "player:So what's the problem?",
            "npc:From this distance",
            "player:Have they returned?",
            "npc:I'm afraid not",
            "npc:Do you think you can do it?",
            "choose:I'll try my best.",
            "player:I'll try my best.",
            "npc:Thank you, you're braver than most.",
            "npc:I don't know how long",
            "npc:If you can retrieve the orb",
        })
        t.exec("stage.finding_trackers", t.var.await_server, "treequest", 4, 10)

        -- Trackers (Quest Helper firstTracker/secondTracker/thirdTracker).
        t.exec("goto-tracker1", t.player.goto_tile, 2501, 3260, 0)
        t.exec("tracker1.talk", t.player.talk_to, "tracker1", 1)
        t.exec("tracker1.height", t.chat.play, {
            "player:Do you know the coordinates",
            "npc:I managed to get one",
            "npc:The height coordinate is 4.",
            "player:Well done.",
            "npc:The other two tracker gnomes",
            "player:OK, take care.",
        })
        t.exec("goto-tracker2", t.player.goto_tile, 2524, 3255, 0)
        t.exec("tracker2.talk", t.player.talk_to, "tracker2", 1)
        t.exec("tracker2.y", t.chat.play, {
            "player:Are you OK?",
            "npc:They caught me spying",
            "npc:But I didn't crack.",
            "player:I'm sorry little man.",
            "npc:Don't be. I have the position",
            "npc:The y coordinate is 5.",
            "player:Well done.",
            "npc:Now leave before they find you",
            "player:Hang in there.",
            "npc:Go!",
        })
        t.exec("goto-tracker3", t.player.goto_tile, 2497, 3233, 0)
        t.exec("tracker3.talk", t.player.talk_to, "tracker3", 1)
        t.exec("tracker3.riddle", t.chat.play, {
            "player:Are you OK?",
            "npc:OK? Who's OK?",
            "player:What's wrong?",
            "npc:You can't see me",
            "player:What do you mean?",
            "npc:They're dancing",
            "mesbox:He's clearly lost the plot.",
            "player:Do you have the coordinate",
            "npc:Who holds the stronghold?",
            "player:What?",
            "npc:More than me, less than our feet.",
            "player:You're mad.",
            "npc:More than we, and Khazard's men are beat.",
            "mesbox:The toll of war",
            "player:I'll pray for you little man.",
            "npc:All day we pray in the hay",
        })

        -- Ballista: height 4, x 3 (tracker3's riddle), y 5.
        t.exec("goto-ballista", t.player.goto_tile, 2509, 3209, 0)
        t.exec("ballista.fire", t.player.click_loc, "catabow", 2)
        t.exec("ballista.coords", t.chat.play, {
            "mesbox:To fire the ballista",
            "choose:0004",
            "choose:0003",
            "choose:0005",
            "mesbox:You fire the ballista.",
        })
        local hit_r = t.await({ level = function() return t.chat.kind() == "mesbox" end }, 10)
        t.check("ballista.hit_page", hit_r == "ok", "await chat.kind()==mesbox after if_close + p_delay -> " .. tostring(hit_r) .. ", kind now " .. tostring(t.chat.kind()))
        t.exec("ballista.hit", t.chat.play, { "mesbox:screams down directly" })
        t.exec("stage.ballista_fired", t.var.await_server, "treequest", 5, 10)

        -- Stronghold: the crumbled wall is a GATE_WORDS loc the guide names
        -- ("Enter the tower by the Crumbled wall and climb the ladder..." --
        -- TreeGnomeVillage.java's cRetrieveOrb) -- climb it for real from
        -- the south, the only side its own script accepts
        -- (quest_tree_locs.rs2's [oploc1,khazzacklowwall]: "You can't get
        -- over the wall from this side." when coordz(coord) > loc's).
        t.exec("goto-wall", t.player.goto_tile, 2509, 3245, 0)
        t.exec("wall.climb", t.player.click_loc, "khazzacklowwall", 1)
        t.exec("wall.mesbox", t.chat.play, { "mesbox:reduced to rubble" })
        t.ticks(3) -- the forced climb animation (forcewalk2 + agility_exactmove) runs before the landing teleport
        local wall_tile_r, wall_tile_v = t.world.tile()
        t.check("wall.crossed", wall_tile_r == "ok" and wall_tile_v and wall_tile_v.z > 3253,
            "tile after the climb: " .. tostring(wall_tile_r) .. " " ..
            (wall_tile_v and (wall_tile_v.x .. "," .. wall_tile_v.z .. "," .. wall_tile_v.level) or "?"))

        -- The ladder itself (TreeGnomeVillage.java's climbTheLadder,
        -- WorldPoint(2503,3252,0)) has no quest-specific trigger anywhere
        -- in quest_tree/ or area_gnome/ -- a bare TRAVEL_WORDS object, so
        -- goto_tile onto its own tile at its level is legitimate travel
        -- (QUEST_AUTHORING.md section 2), not a second cheat past it.
        t.exec("goto-ladder", t.player.goto_tile, 2503, 3252, 1)
        t.exec("chest.open", t.player.click_loc, "chestclosed_khazard", 1)
        t.ticks(2)
        t.exec("chest.search", t.player.click_loc, "chestopen_khazard", 1)
        t.exec("chest.orb_page", t.chat.play, { "mesbox:Inside you find the gnomes' stolen orb" })
        t.exec("orb.held", t.inv.await, "orb_of_protection", 1, 10)
        t.exec("stage.retrieved_orb", t.var.await_server, "treequest", 6, 10)

        t.exec("goto-bolren2", t.player.goto_tile, 2541, 3170, 0)
        t.exec("bolren.orb_talk", t.player.talk_to, "king_bolren", 1)
        t.exec("bolren.first_orb", t.chat.play, {
            "player:I have the orb.",
            "npc:Oh my... The misery",
            "player:King Bolren, are you OK?",
            "npc:Thank you traveller, but it's too late.",
            "player:What happened?",
            "npc:They came in the night.",
            "player:Who?",
            "npc:Khazard troops.",
            "player:I'm sorry.",
            "npc:They took the other orbs",
            "player:Where did they take them?",
            "npc:They headed north of the stronghold.",
            "choose:I will find the warlord and bring back the orbs.",
            "player:I will find the warlord",
            "npc:You are brave",
            "npc:I will safeguard this orb",
        })
        t.exec("stage.returned_first_orb", t.var.await_server, "treequest", 7, 10)
        t.chat.close()

        -- The warlord: Talk-to arms the combat form, then a real fight.
        t.exec("goto-warlord", t.player.goto_tile, 2459, 3302, 0)
        t.exec("warlord.talk", t.player.talk_to, "khazard_warlord", 1)
        t.exec("warlord.dialog", t.chat.play, {
            "player:You there, stop!",
            "npc:Go back to your pesky little green friends.",
            "player:I've come for the orbs.",
            "npc:You're out of your depth traveller.",
            "player:They're stolen goods,",
            "npc:Ha, you really think you stand a chance?",
        })
        -- Magic (Quest Helper's combatGear hint: "magic is best"), not
        -- melee -- fire_bolt (level 35, magic_combat_spells.dbrow) against
        -- this npc's magic=1 (quest_tree.npc) lands almost every cast.
        -- Cast directly in a loop (section 8's retry-loop rule: the outcome
        -- row is what's graded, not one row per attempt -- a cast that
        -- only closes the distance or whose hit didn't land is not a
        -- verb failure), then let await_dead_engaged both re-cast on any
        -- stall and give the corroborated kill.
        t.exec("warlord.cast1", t.player.cast, "fire_bolt", "khazard_warlord_combat", 14)
        local warlord_casts = 1
        while warlord_casts < 40 and t.npc.nearest("khazard_warlord_combat", 20) == "ok" do
            t.player.cast("fire_bolt", "khazard_warlord_combat", 14)
            warlord_casts = warlord_casts + 1
        end
        t.check("warlord.cast_loop", true, "cast fire_bolt " .. warlord_casts .. " time(s) total")
        t.exec("warlord.dead", t.npc.await_dead_engaged, 400, 6)
        t.exec("warlord.satchel", t.chat.play, { "mesbox:You search his satchel and find the orbs of protection." })
        t.exec("stage.defeated_warlord", t.var.await_server, "treequest", 8, 10)
        t.exec("orbs.held", t.inv.await, "orbs_of_protection", 1, 10)

        -- king_bolren.rs2's [queue,tree_quest_complete] (armed inside the
        -- SAME "bolren.orbs" dialogue below, well before the chant
        -- cutscene's own mesboxes) is what runs stat_advance(attack,
        -- 114500) -- so the reward snapshot has to be taken before THIS
        -- dialogue, not merely before quest.expect_complete() (measured
        -- run 2: a snapshot taken after stage.complete/reward.amulet had
        -- already passed read the POST-grant value both times and reported
        -- delta=0 -- the grant runs in the same script pass that sets
        -- %treequest=^tree_complete and adds the amulet, all three ahead of
        -- where that snapshot sat).
        local snap_r, snap_v = t.skill.snapshot()
        t.check("reward.snapshot", snap_r == "ok", "skill.snapshot before the hand-in dialogue -> " .. tostring(snap_r))

        t.exec("goto-bolren3", t.player.goto_tile, 2541, 3170, 0)
        t.exec("bolren.orbs_talk", t.player.talk_to, "king_bolren", 1)
        t.exec("bolren.orbs", t.chat.play, {
            "player:Bolren, I have returned.",
            "npc:You made it back!",
            "player:I have them here.",
            "npc:Hooray, you're amazing.",
            "npc:Once the orbs are replaced",
            "player:What does the ceremony involve?",
            "npc:The spirit tree has looked over us",
            "mesbox:The gnomes begin to chant.",
        })
        local rest_r = t.await({ level = function() return t.chat.kind() == "mesbox" end }, 40)
        t.check("ceremony.rest_page", rest_r == "ok", "await chat.kind()==mesbox across the chant cutscene -> " .. tostring(rest_r) .. ", kind now " .. tostring(t.chat.kind()))
        t.exec("ceremony.end", t.chat.play, {
            "mesbox:The orbs of protection come to rest",
            "npc:Now at last my people are safe",
            "player:I'm pleased I could help.",
            "npc:You are modest brave traveller.",
            "npc:Please, for your efforts take this amulet.",
            "player:Thank you King Bolren.",
            "npc:The tree has many other powers",
        })
        t.exec("stage.complete", t.var.await_server, "treequest", 9, 10)
        t.exec("reward.amulet", t.inv.await, "gnome_amulet", 1, 10)
        t.ticks(3)

        t.quest.expect_complete()
        t.exec("reward.attack_xp", t.skill.expect_gain, "attack", 11450, snap_v)
        t.finish(0)
    end,
}
