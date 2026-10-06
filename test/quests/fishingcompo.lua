-- Fishing Contest, end to end through the real client.
--
-- The scaffold (tools/quest_gate/new_quest.py, from Quest Helper's guide)
-- guessed a dialogue tree that does not match this content pack at all --
-- Quest Helper's own dialog text ("I was wondering what was down those
-- stairs?", no "just") never matches areas/area_white_wolf_mountain/scripts/
-- mountain_dwarf.rs2's actual rows. Rewritten by hand against the quest's
-- own .rs2 files:
--   quests/quest_fishingcompo/scripts/{quest_fishingcompo,
--     quest_fishingcompo_gate,hemenster_fishing}.rs2
--   areas/area_white_wolf_mountain/scripts/mountain_dwarf.rs2
--   areas/area_seers/scripts/hemenster/{bonzo,grandpa_jack}.rs2
--   areas/area_seers/scripts/mcgrubors_wood.rs2
--
-- An earlier attempt (batch sonnet-b10) read `kr_seers_table2` and
-- `red_worm_junction` having no [oploc*] handler as "gathering is not
-- wired" and ::gave garlic/the worm/the rod outright -- rejected
-- (helper_coverage.py: "getGarlic is CHEAT"). Both ARE driveable, just not
-- through the loc the guide's WorldPoint happens to sit next to:
--   * garlic is a spawned GROUND OBJ beside kr_seers_table2
--     (areas/world/configs/m42_54.spawn:45, 2714,3478,0, the exact
--     WorldPoint) -- a click_obj pickup, never a loc click.
--   * the red vine worm is a real spade-dig: [oplocu,_red_vine] /
--     [oploc1,_red_vine] (category 216, mcgrubors_wood.rs2) on any of the
--     eight red_worm_* vine locs, entered through the ONLY working passage,
--     mcgruborlooserailing's agility squeeze (mcgruborgatel/r are always
--     "The gate is locked.").
--   * the fishing rod is a real 5gp purchase from Grandpa Jack
--     (grandpa_jack.rs2 [label,grandpa_jack_buy_rod]), which is what the
--     guide's own coins tooltip ("10 if you buy a fishing rod from Jack")
--     was already telling the setup grant.
-- The spade is the one genuine bring-along here (a TOOL Jack's dig checks
-- for but never consumes), same as the coins Jack and Bonzo both spend.
--
-- Door rule (b65 re-drive): every goto departs from and lands on an open,
-- walkable tile OUTSIDE (reach.py, doors closed). The run opens with a real
-- Camelot Teleport from the Lumbridge fixture (magic 45 and the runes are
-- setup staging), and the guide's dwarf is VESTRI (tunnel_dwarf1, 2820,3487,
-- north of Catherby) -- the Taverley dwarf the old first goto stood beside
-- was past the members' gate. Every closed space is entered and left by its
-- own crossing, on every visit:
--   * McGrubor's Woods: mcgruborlooserailing 2662,3500 (Squeeze-through),
--     2661,3500 outside <-> 2662,3500 inside, through cross_trap both ways;
--   * Grandpa Jack's house: poshdoor 2649,3448, pass_door in and out;
--   * the Seers' house with the garlic: kr_poordoor 2713,3483, pass_door in
--     and out; the garlic is taken from beside kr_seers_table2, not off it;
--   * Hemenster: fishinggateclosedr 2642,3441, cross_gate in (Morris checks
--     the pass) and out (after the win the gate lets the player straight out).
-- Inside the woods, the houses and Hemenster the player walks (walk_to).
--
-- Fishing level 10 is Quest Helper's own stated requirement to even be
-- offered the quest (mountain_dwarf.rs2's tunnel_dwarf_friends branch
-- returns before the accept prompt under it) -- a prerequisite skill, set
-- once in setup, never touched again.


return {
    id = "fishingcompo",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::give spade 1", -- a bring-along TOOL (getItemRequirements), never the deliverable: dig_red_vine
                          -- (mcgrubors_wood.rs2) only checks inv_total(inv, spade) > 0, it is not consumed
        "::give coins 10", -- 5gp for Jack's rod (grandpa_jack.rs2) + 5gp for Bonzo's entrance fee
        "::setlevel fishing 10", -- mountain_dwarf.rs2's own accept-prompt gate
        "::setlevel magic 45", -- travel staging: one Camelot Teleport from Lumbridge (teleport.rs2)
        "::give airrune 5", -- Camelot Teleport's cost (magic_spells.dbrow): 5 air + 1 law
        "::give lawrune 1",
        "::passive guarddog", -- McGrubor's Woods is thick with aggressive level-44 guard dogs
                              -- (areas/world/configs/m41_54.spawn) between the railing and the red
                              -- vine patch; this is setup housekeeping around the quest's own work,
                              -- never the quest's own work (trap "::passive").
    },

    run = function(t)
        -- Poll a backpack count until it is EXACTLY n (t.inv.await is ">= n",
        -- so it cannot see a count fall).
        local function count_is(name, item, n, ticks, why)
            local r, d = t.await({
                level = function()
                    local cr, count = t.inv.count(item)
                    return cr == "ok" and count == n
                end,
                note = "fishingcompo." .. name,
            }, ticks)
            local _, left = t.inv.count(item)
            t.check(name, r == "ok" and left == n,
                why .. ": " .. item .. " == " .. n .. " within " .. ticks .. " tick(s) (" .. tostring(r) .. ") "
                    .. tostring(d) .. " count=" .. tostring(left))
        end

        local bind_result, bind_detail = t.quest.bind({
            varp = "varp11_fishingcompo",
            constants = {
                not_started = 0,
                started = 1,
                in_comp = 2,
                garlic_comp = 3,
                won_comp = 4,
                complete = 5,
            },
            row = "quest_fishingcontest", -- configs/all.dbrow:2839
            display = "Fishing Contest", -- all.dbrow:2847 displayname, confirmed against the live row
            points = 1, -- configs/quest_fishingcompo.constant: ^fishingcompo_questpoints = 1
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ------------------------------------------------ start the quest
        -- talkToVestriStep: Vestri (tunnel_dwarf1, m44_54.spawn:10, 2820,3487)
        -- is the guide's dwarf, "just north of Catherby"; both dwarves share
        -- [label,tunnel_dwarf_talk] (mountain_dwarf.rs2:13). A real Camelot
        -- Teleport from the Lumbridge fixture, then overland (reach.py
        -- 2757,3478 -> 2820,3486: REACH closed-doors len=85).
        -- The "friend" branch (stairs -> why not? -> if you were my friend ->
        -- let's be friends -> how do I earn that) is the ONLY branch
        -- mountain_dwarf.rs2 wires to the accept prompt.
        t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = "startQuest.camelotTeleport",
            runes = { { "airrune", 5 }, { "lawrune", 1 } }, where = "Camelot, tele_coord 0_43_54_5_22" })
        t.exec("goto-vestri-start", t.player.goto_tile, 2820, 3486, 0)
        t.exec("dwarf.greet", t.player.talk_to, "tunnel_dwarf1")
        local d1r, d1d = t.chat.drain({ stop_at = "options" })
        t.expect("dwarf.drain_to_stairs_choice", d1r, d1d)
        t.shot("dwarf-greet-options")

        t.exec("dwarf.choose_stairs", t.chat.choose, "I was just wondering what was down those stairs?")
        local d2r, d2d = t.chat.drain({ stop_at = "options" })
        t.expect("dwarf.drain_to_whynot_choice", d2r, d2d)

        t.exec("dwarf.choose_whynot", t.chat.choose, "Why not?")
        local d3r, d3d = t.chat.drain({ stop_at = "options" })
        t.expect("dwarf.drain_to_friend_choice", d3r, d3d)

        t.exec("dwarf.choose_friend", t.chat.choose, "If you were my friend I wouldn't mind.")
        local d4r, d4d = t.chat.drain({ stop_at = "options" })
        t.expect("dwarf.drain_to_letsbefriends_choice", d4r, d4d)

        t.exec("dwarf.choose_letsbefriends", t.chat.choose, "Well, let's be friends!")
        local d5r, d5d = t.chat.drain({ stop_at = "options" })
        t.expect("dwarf.drain_to_howearn_choice", d5r, d5d)

        t.exec("dwarf.choose_howearn", t.chat.choose, "And how am I meant to do that?")
        local d6r, d6d = t.chat.drain({ stop_at = "options" })
        t.expect("dwarf.drain_to_accept_choice", d6r, d6d)
        t.shot("dwarf-accept-options")

        t.exec("dwarf.choose_yes_start", t.chat.choose, "Yes.")
        local d7r, d7d = t.chat.drain({ stop_at = "none" })
        t.expect("dwarf.drain_accept_close", d7r, d7d)

        t.expect("quest.stage.started", t.quest.expect_stage("started"))
        local pass_result, pass_detail = t.inv.await("fishing_competition_pass", 1, 10)
        t.step("dwarf.pass_granted", pass_result == "ok" and "PASS" or "FAIL",
            "fishing_competition_pass await 1 -> " .. tostring(pass_result) .. " " .. tostring(pass_detail))

        -- ------------------------------------------------ dig up a red vine worm
        -- goToMcGruborWood: the northern entrance is mcgruborlooserailing
        -- 2662,3500 (maps/m41_54.jl2; mcgrubors_wood.rs2:29 [oploc1,...], an
        -- agility side-step across the railing's x; mcgruborgatel/r always
        -- answer "The gate is locked."). reach.py: 2661,3500 is OUTSIDE (REACH
        -- closed-doors to Camelot, len=146), 2662,3500 INSIDE (only via the
        -- locked mcgruborgatel 2650,3470). Pressed by its own op both ways.
        t.exec("goto-railing-outside", t.player.goto_tile, 2661, 3500, 0)
        t.exec("railing.squeezeIn", t.player.cross_trap, { loc = "mcgruborlooserailing", op_name = "Squeeze-through",
            at = { 2662, 3500, 0 }, src = { 2661, 3500 }, dest = { 2662, 3500 }, attempts = 1 })
        t.shot("railing-squeezed-in")

        -- goToRedVine: "Use your spade on the red vines" -- [oplocu,_red_vine]
        -- with last_useitem = spade -> [label,dig_red_vine] (mcgrubors_wood.rs2:44).
        -- The vines are walk-over ground decorations with an op (red_worm_*),
        -- so stand on the plain tile 2632,3497 beside red_worm_junction
        -- (2631,3497; reach.py from 2662,3500: REACH closed-doors len=81).
        t.exec("walk-red-vine", t.player.walk_to, 2632, 3497, 100)
        local vine_target, vine_lookup = t.player.by_symbol("loc", "red_worm_junction")
        t.step("vine.lookup", vine_target ~= nil and "PASS" or "FAIL",
            "by_symbol(loc, red_worm_junction) -> " .. tostring(vine_lookup)
                .. " id=" .. tostring(vine_target and vine_target.id))
        t.exec("redVine.useSpade", t.player.use_on, "spade", vine_target)
        local worm_result, worm_detail = t.inv.await("red_vine_worm", 1, 10)
        local _, spade_kept = t.inv.count("spade")
        t.check("redVine.wormDug", worm_result == "ok" and spade_kept == 1,
            "red_vine_worm await 1 -> " .. tostring(worm_result) .. " " .. tostring(worm_detail)
                .. "; spade kept=" .. tostring(spade_kept) .. " (dig_red_vine only checks it)")
        t.shot("vine-worm-dug")

        -- runToJack: "You can leave McGrubor's Woods via the northern entrance."
        t.exec("walk-railing-inside", t.player.walk_to, 2662, 3500, 100)
        t.exec("railing.squeezeOut", t.player.cross_trap, { loc = "mcgruborlooserailing", op_name = "Squeeze-through",
            at = { 2662, 3500, 0 }, src = { 2662, 3500 }, dest = { 2661, 3500 }, attempts = 1 })

        -- ------------------------------------------------ buy a rod from Grandpa Jack
        -- grandpa_jack.rs2 [opnpc1,grandpa_jack]: with %fishingcompo=started
        -- the greeting opens a 5-row menu; "Can I buy a fishing rod?" ->
        -- [label,grandpa_jack_buy_rod] -> "Yes please." grants the rod and
        -- deducts 5gp. Jack stands in his house (2650,3452), walled in with
        -- poshdoor 2649,3448 on its south wall (room x 2646-2652 z 3449-3454):
        -- through the door in and out.
        t.exec("goto-jack-door-outside", t.player.goto_tile, 2649, 3447, 0)
        t.exec("jack.doorIn", t.player.pass_door, { closed = "poshdoor", open = "poshdooropen",
            at = { 2649, 3448, 0 }, near = { 2649, 3448 }, far = { 2649, 3449 } })
        t.exec("jack.greet", t.player.talk_to, "grandpa_jack")
        t.exec("jack.buy_rod", t.chat.play, {
            "npc:Hello young",
            "options",
            "choose:Can I buy a fishing rod?",
            "player:Can I buy a fishing rod?",
            "npc:I can sell you one of my old rods for 5 coins.",
            "options",
            "choose:Yes please.",
            "npc:There you go. Look after it.",
            "end",
        })
        local rod_result, rod_coins = t.inv.await("fishing_rod", 1, 10)
        t.step("jack.rod_bought", rod_result == "ok" and "PASS" or "FAIL",
            "fishing_rod await 1 -> " .. tostring(rod_result) .. " " .. tostring(rod_coins))
        count_is("jack.rod_paid", "coins", 5, 10, "Jack's 5gp from the setup's 10")
        t.shot("rod-bought")
        t.exec("jack.doorOut", t.player.pass_door, { closed = "poshdoor", open = "poshdooropen",
            at = { 2649, 3448, 0 }, near = { 2649, 3449 }, far = { 2649, 3447 } })

        -- ------------------------------------------------ pick up the garlic
        -- OBSOLETE: getGarlic the guide's kr_seers_table2 has no op in the real game (configs/all.loc:286189, LostCity kr_seers.rs2); the wiki has you "pick up a piece off the table" (https://oldschool.runescape.wiki/w/Fishing_Contest?oldid=15302643#Help_from_the_champion) -- the ground spawn on the table's tile (areas/world/configs/m42_54.spawn:45, grandpa_jack.rs2:34), taken below with click_obj.
        -- The table stands in a Seers' house walled in with kr_poordoor
        -- 2713,3483 on its north wall (room x 2709-2716 z 3476-3482). The
        -- garlic lies on the table's tile; take it from the open tile beside
        -- it (2715,3478; reach.py from 2713,3482: REACH len=6).
        t.exec("goto-seers-door-outside", t.player.goto_tile, 2713, 3485, 0)
        t.exec("garlic.doorIn", t.player.pass_door, { closed = "kr_poordoor", open = "kr_poordooropen",
            at = { 2713, 3483, 0 }, near = { 2713, 3483 }, far = { 2713, 3482 } })
        t.exec("walk-garlic-table", t.player.walk_to, 2715, 3478, 15)
        -- click_obj answers `ok` with a nil detail (section 8's hollow list) --
        -- call it directly and read the backpack back.
        local pick_result = t.player.click_obj("garlic")
        local garlic_result, garlic_detail = t.inv.await("garlic", 1, 10)
        t.check("garlic.picked_up", pick_result == "ok" and garlic_result == "ok",
            "click_obj -> " .. tostring(pick_result) .. "; garlic await 1 -> "
                .. tostring(garlic_result) .. " " .. tostring(garlic_detail))
        t.shot("garlic-picked-up")
        t.exec("walk-seers-door-inside", t.player.walk_to, 2713, 3482, 15)
        t.exec("garlic.doorOut", t.player.pass_door, { closed = "kr_poordoor", open = "kr_poordooropen",
            at = { 2713, 3483, 0 }, near = { 2713, 3482 }, far = { 2713, 3484 } })

        -- ------------------------------------------------ show Morris the pass, enter Hemenster
        -- quest_fishingcompo_gate.rs2 [oploc1,fishinggateclosedr] ->
        -- [label,hemenster_gate_open]: from OUTSIDE (player x > the gate's
        -- x=2642) [proc,fishingcompo_gate_admit] -- with the pass carried and
        -- Morris within 12 tiles, three pages ("Competition pass please." /
        -- "You show Morris your pass." / "Move on through."), then if_close +
        -- p_teleport(loc_coord) lands the player on the gate tile, inside.
        t.exec("goto-hemenster-gate-outside", t.player.goto_tile, 2644, 3441, 0)
        t.exec("enterHemenster.gateIn", t.player.cross_gate, { loc = "fishinggateclosedr", at = { 2642, 3441, 0 },
            near = { 2643, 3441 }, far_ok = function(tile) return tile.x <= 2642 end,
            far_desc = "inside the Hemenster contest fence, x <= 2642",
            chat = { "npc:Competition pass please.", "mesbox:You show Morris your pass.", "npc:Move on through." },
            chat_optional = "Morris speaks only within 12 tiles (fishingcompo_gate_admit); without him the line is "
                .. "'You show your competition pass and go through.'" })
        t.shot("hemenster-gate-entered")

        -- ------------------------------------------------ pay Bonzo, enter the competition
        -- bonzo.rs2 [label,bonzo_talk] -> started & paid=0 -> bonzo_waiting_entry.
        t.exec("walk-bonzo-pay", t.player.walk_to, 2641, 3438, 15)
        t.exec("bonzo.greet", t.player.talk_to, "bonzo")
        local b1r, b1d = t.chat.drain({ stop_at = "options" })
        t.expect("bonzo.drain_to_enter_choice", b1r, b1d)
        t.shot("bonzo-entry-options")

        t.exec("bonzo.choose_enter", t.chat.choose, "I'll enter the competition please.")
        local b2r, b2d = t.chat.drain({ stop_at = "none" })
        t.expect("bonzo.drain_entry_close", b2r, b2d)
        t.shot("bonzo-spot-assigned")

        t.expect("quest.stage.in_comp", t.quest.expect_stage("in_comp"))
        -- Trap 24: the coins delta lands a tick behind the dialogue's close.
        count_is("bonzo.fee_paid", "coins", 0, 10, "Bonzo's 5gp entrance fee")

        -- ------------------------------------------------ scare off the stranger
        -- putGarlicInPipe: [oplocu,garlicpipe] switches on last_useitem =
        -- garlic -> @stash_garlic (quest_fishingcompo_gate.rs2:4) -- a real
        -- use_on, never a plain click_loc ("The pipe smells of sewage."). It
        -- is the ONLY way into %fishingcompo=garlic_comp.
        local pipe_target, pipe_lookup = t.player.by_symbol("loc", "garlicpipe")
        t.step("garlicpipe.lookup", pipe_target ~= nil and "PASS" or "FAIL",
            "by_symbol(loc, garlicpipe) -> " .. tostring(pipe_lookup)
                .. " id=" .. tostring(pipe_target and pipe_target.id)
                .. " match=" .. tostring(pipe_target and pipe_target.match))

        t.exec("walk-garlicpipe", t.player.walk_to, 2638, 3445, 15)
        t.exec("putGarlicInPipe.use", t.player.use_on, "garlic", pipe_target)
        count_is("putGarlicInPipe.garlicGone", "garlic", 0, 5, "[label,stash_garlic] inv_del(inv, garlic, 1)")

        -- [label,stash_garlic]: with %fishingcompo_paid=1 already the stash
        -- fires the Sinister Stranger's and Bonzo's lines, then Bonzo's mesbox.
        t.exec("garlicpipe.stranger_reacts", t.chat.play, {
            "npc:Arrgh! WHAT is that GHASTLY smell",
            "npc:Hmm. You'd better go and take the area by the pipes then.",
            "mesbox:Your fishing competition spot is now beside the pipes.",
            "end",
        })
        t.expect("quest.stage.garlic_comp", t.quest.expect_stage("garlic_comp"))
        t.shot("garlicpipe-stashed")

        -- ------------------------------------------------ catch the giant carp
        -- hemenster_fishing.rs2 [opnpc1,0_41_53_sinisterfishspot] ->
        -- attempt_fish_hemenster -> ~get_hemenster_bait picks the carried
        -- red_vine_worm -> [label,hemenster_catch] grants raw_giant_carp
        -- (no roll). The spot (2637,3444) sits in the water beside the pipe
        -- tile the player already stands on.
        t.ticks(2)
        t.exec("fish.carp", t.player.talk_to, "0_41_53_sinisterfishspot")
        local carp_result, carp_detail = t.inv.await("raw_giant_carp", 1, 10)
        local _, worm_left = t.inv.count("red_vine_worm")
        t.check("fish.carp_landed", carp_result == "ok" and worm_left == 0,
            "raw_giant_carp await 1 -> " .. tostring(carp_result) .. " " .. tostring(carp_detail)
                .. "; red_vine_worm left=" .. tostring(worm_left) .. " (the bait is spent)")
        t.shot("carp-caught")

        -- ------------------------------------------------ hand the carp to Bonzo
        -- bonzo.rs2 -> %fishingcompo=garlic_comp -> @bonzo_howdoing -> the
        -- "enough to win" choice -> @bonzo_handover_catch.
        t.exec("walk-bonzo-handin", t.player.walk_to, 2641, 3438, 15)
        t.exec("bonzo.howdoing", t.player.talk_to, "bonzo")
        t.exec("bonzo.accept_carp", t.chat.play, {
            "npc:So how are you doing so far?",
            "options",
            "choose:I have this big fish. Is it enough to win?",
            "player:I have this big fish. Is it enough to win?",
            "mesbox:You hand over your catch.",
            "npc:We have a new winner!",
            "mesbox:You are given the Hemenster fishing trophy!",
            "end",
        })
        t.expect("quest.stage.won_comp", t.quest.expect_stage("won_comp"))
        local trophy_result, trophy_detail = t.inv.await("hemenster_fishing_trophy", 1, 10)
        local _, carp_left = t.inv.count("raw_giant_carp")
        t.check("bonzo.trophy_granted", trophy_result == "ok" and carp_left == 0,
            "hemenster_fishing_trophy await 1 -> " .. tostring(trophy_result) .. " " .. tostring(trophy_detail)
                .. "; raw_giant_carp left=" .. tostring(carp_left))
        t.shot("trophy-won")

        -- Leave Hemenster by its gate: from inside (x <= 2642) with
        -- %fishingcompo >= won_comp, [label,hemenster_gate_open] skips
        -- Bonzo's quit prompt and p_teleports the player to 2643,3441.
        t.exec("walk-hemenster-gate-inside", t.player.walk_to, 2642, 3441, 15)
        t.exec("speaktoVestri.gateOut", t.player.cross_gate, { loc = "fishinggateclosedr", at = { 2642, 3441, 0 },
            near = { 2642, 3441 }, far_ok = function(tile) return tile.x >= 2643 end,
            far_desc = "outside the Hemenster contest fence, x >= 2643" })

        -- ------------------------------------------------ hand the trophy to Vestri
        -- mountain_dwarf.rs2 [label,tunnel_dwarf_won]: the trophy is handed
        -- over and queue(fishingcompo_quest_complete, 0, 0) runs
        -- stat_advance(fishing, 24370) (the scroll's "2,437 Fishing XP").
        -- reach.py 2643,3441 -> 2820,3486: REACH closed-doors len=230.
        local skill_snap_result, skill_snap = t.skill.snapshot()
        t.step("quest.skill_snapshot", skill_snap_result == "ok" and "PASS" or "FAIL",
            "snapshot before hand-in -> " .. tostring(skill_snap_result))

        t.exec("goto-vestri-handin", t.player.goto_tile, 2820, 3486, 0)
        t.exec("dwarf.trophy_handin", t.player.talk_to, "tunnel_dwarf1")
        t.exec("dwarf.won_dialogue", t.chat.play, {
            "npc:Have you won yet?",
            "player:Yes I have!",
            "npc:Well done! So where is the trophy?",
            "player:I have it right here!",
            "mesbox:You give the trophy to the dwarf.",
            "npc:That's a mighty fine trophy!",
            "npc:You can use the tunnel under White Wolf Mountain",
            "player:Thanks!",
            "end",
        })
        local _, trophy_left = t.inv.count("hemenster_fishing_trophy")
        t.check("dwarf.trophy_taken", trophy_left == 0,
            "hemenster_fishing_trophy left after the hand-in = " .. tostring(trophy_left))
        -- Completion is asynchronous (section 8): the queued proc above runs
        -- behind this dialogue's own close, not inside it.
        t.ticks(3)

        t.quest.expect_complete()
        -- Literal rewards (wiki Fishing Contest: 1 Quest Point, 2,437 Fishing
        -- XP; quest_fishingcompo.rs2 stat_advance(fishing, 24370)). The quest
        -- point delta is expect_complete's quest.points row (bind points = 1).
        t.check("reward.fishing", t.skill.expect_gain("fishing", 2437, skill_snap))
        t.finish(0)
    end,
}
