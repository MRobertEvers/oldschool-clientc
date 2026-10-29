-- Fishing Contest, end to end through the real client, in the order RSProx
-- index 4404 (rev 241, 2026-10-29) plays it: Vestri's start, Morris and the
-- gate, Bonzo's fee and the willow spot, quitting through the gate, Grandpa
-- Jack's hints and rod, McGrubor's Wood (the forester, the railing, the red
-- vines), the gate again on Morris's word, garlic in the pipe, the pipes' spot
-- (Bonzo moves the stranger), the carp, the trophy, Vestri's hand-in. Every
-- page below is the recording's; docs/rsprox-analysis/quests/fishing_contest.md
-- has the ticks.
--
-- Content: areas/area_white_wolf_mountain/scripts/mountain_dwarf.rs2,
-- areas/area_seers/scripts/hemenster/{bonzo,fishermen,grandpa_jack,
-- sinister_stranger}.rs2, areas/area_seers/scripts/mcgrubors_wood.rs2,
-- quests/quest_fishingcompo/scripts/*.rs2.
--
-- The spade and the coins are bring-alongs (the recorder banked for both);
-- the garlic is the Seers' kitchen ground spawn (m42_54.spawn, 2714,3478),
-- taken through kr_poordoor as the recorder did [426..428].


return {
    id = "fishingcompo",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::give spade 1", -- a bring-along TOOL (getItemRequirements), never the deliverable: dig_red_vine
                          -- (mcgrubors_wood.rs2) only checks inv_total(inv, spade) > 0, it is not consumed
        "::give coins 10", -- 5gp for Bonzo's entrance fee + 5gp for Jack's rod
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

        -- A page that comes after a long recorded pause (the stranger's
        -- walk, "You wait": 9 ticks), past chat.play's own 8-tick settle.
        local function await_page(name, kind, ticks)
            local r, d = t.await({
                level = function() return t.chat.kind() == kind end,
                note = "fishingcompo." .. name,
            }, ticks)
            t.check(name, r == "ok", "a " .. kind .. " page within " .. ticks .. " tick(s) -> "
                .. tostring(r) .. " " .. tostring(d))
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
        -- [241..362]: the first menu, the stairs menu, "Why not?", the friend
        -- branch, the start confirm; the stage, the line and the pass with the
        -- page after the pass's box.
        t.ticklog.start()
        t.ticklog.mark("leg.vestri_start")
        t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = "startQuest.camelotTeleport",
            runes = { { "airrune", 5 }, { "lawrune", 1 } }, where = "Camelot, tele_coord 0_43_54_5_22" })
        t.exec("goto-vestri-start", t.player.goto_tile, 2820, 3486, 0)
        -- [245..257]: the stairs first; the dwarf halts the player, and his
        -- "Well, it is." falls through to his own first menu.
        t.exec("stairs.halt", t.player.click_loc, "tunnelstairstop", 1)
        t.exec("dwarf.start", t.chat.play, {
            "npc:Hoi there! Halt!",
            "npc:You can't come in here!",
            "options", "choose:Oh sorry, I hadn't realised it was private.",
            "player:Oh, sorry, I hadn't realised it was private.",
            "npc:Well, it is.",
            "options", "choose:I was wondering what was down those stairs?",
            "player:I was just wondering what was down those stairs?",
            "npc:You can't go down there!",
            "options", "choose:Why not?",
            "player:Why not?",
            "npc:This is the home of the Mountain Dwarves.",
            "options", "choose:If you were my friend I wouldn't mind it.",
            "player:If you were my friend I wouldn't mind.",
            "npc:Yes, but I don't even know you.",
            "options", "choose:Well, let's be friends!",
            "player:Well, let's be friends!",
            "npc:I don't make friends easily.",
            "options", "choose:Yes.",
            "player:And how am I meant to do that?",
            "npc:My, we are the persistent one aren't we?",
            "npc:Well, there's a certain gold artefact we're after.",
            "npc:Fortunately we have acquired a pass",
            "npc:Unfortunately Dwarves don't make good fishermen.",
            "player:Fortunately I'm alright at fishing!",
            "npc:Okay. I entrust you with our competition pass.",
            "objbox:You got the Fishing Contest Pass!",
            "npc:Go to Hemenster and do us proud!",
            "end",
        })
        t.expect("quest.stage.started", t.quest.expect_stage("started"))
        local pass_result, pass_detail = t.inv.await("fishing_competition_pass", 1, 10)
        t.step("dwarf.pass_granted", pass_result == "ok" and "PASS" or "FAIL",
            "fishing_competition_pass await 1 -> " .. tostring(pass_result) .. " " .. tostring(pass_detail))

        -- ------------------------------------------------ the garlic
        -- [426..428]: the Seers' kitchen's ground garlic, through kr_poordoor.
        t.ticklog.mark("leg.garlic")
        t.exec("goto-seers-door-outside", t.player.goto_tile, 2713, 3485, 0)
        t.exec("garlic.doorIn", t.player.pass_door, { closed = "kr_poordoor", open = "kr_poordooropen",
            at = { 2713, 3483, 0 }, near = { 2713, 3483 }, far = { 2713, 3482 } })
        t.exec("walk-garlic-table", t.player.walk_to, 2715, 3478, 15)
        local pick_result = t.player.click_obj("garlic")
        local garlic_result, garlic_detail = t.inv.await("garlic", 1, 10)
        t.check("garlic.picked_up", pick_result == "ok" and garlic_result == "ok",
            "click_obj -> " .. tostring(pick_result) .. "; garlic await 1 -> "
                .. tostring(garlic_result) .. " " .. tostring(garlic_detail))
        t.exec("walk-seers-door-inside", t.player.walk_to, 2713, 3482, 15)
        t.exec("garlic.doorOut", t.player.pass_door, { closed = "kr_poordoor", open = "kr_poordooropen",
            at = { 2713, 3483, 0 }, near = { 2713, 3482 }, far = { 2713, 3484 } })

        -- ------------------------------------------------ Morris, the gate
        -- [547..553]: the player asks Morris, shows the pass (passed = 1), and
        -- the gate lets the player straight through.
        t.ticklog.mark("leg.morris")
        t.exec("goto-hemenster-gate-outside", t.player.goto_tile, 2644, 3441, 0)
        t.exec("morris.talk", t.player.talk_to, "morris")
        t.exec("morris.pass", t.chat.play, {
            "player:What are you sitting around here for?",
            "npc:I'm making sure only those with a competition pass",
            "player:I have one here.",
            "objbox:You show Morris your pass.",
            "npc:Move on through. Talk to Bonzo to enter the competition.",
            "end",
        })
        t.exec("enterHemenster.gateIn", t.player.cross_gate, { loc = "fishinggateclosedr", at = { 2642, 3441, 0 },
            near = { 2643, 3441 }, far_ok = function(tile) return tile.x <= 2642 end,
            far_desc = "inside the Hemenster contest fence, x <= 2642" })

        -- ------------------------------------------------ Bonzo's fee, the willow
        -- [561..601]: the fee in a box, a tick's pause, the places; the
        -- stranger walks to the pipes and the stage goes out with the box.
        t.ticklog.mark("leg.bonzo_fee")
        t.exec("walk-bonzo-pay", t.player.walk_to, 2641, 3438, 15)
        t.exec("bonzo.greet", t.player.talk_to, "bonzo")
        t.exec("bonzo.enter", t.chat.play, {
            "npc:Roll up, roll up!",
            "npc:Enter the great Hemenster fishing competition!",
            "npc:Only 5gp entrance fee!",
            "options", "choose:I'll enter the competition please.",
            "player:I'll enter the competition please.",
            "npc:Marvelous!",
            "objbox:You pay Bonzo 5 coins.",
            "npc:Ok, we've got all the fishermen! It's time to roll!",
            "npc:Ok, nearly everyone is in their place already.",
            "end",
        })
        await_page("bonzo.willow_box", "mesbox", 14)
        t.exec("bonzo.willow", t.chat.play, {
            "mesbox:Your fishing competition spot is beside the willow tree.",
            "end",
        })
        t.expect("quest.stage.in_comp", t.quest.expect_stage("in_comp"))
        count_is("bonzo.fee_paid", "coins", 5, 10, "Bonzo's 5gp entrance fee from the setup's 10")
        t.shot("bonzo-spot-assigned")

        -- [665]: the pipe, searched.
        t.exec("walk-garlicpipe", t.player.walk_to, 2638, 3445, 15)
        t.exec("pipe.search", t.player.click_loc, "garlicpipe", 1)
        t.exec("pipe.search-dialog", t.chat.play, { "player:Ewww - it's a smelly sewage pipe.", "end" })

        -- ------------------------------------------------ quit through the gate
        -- [703..716]: Bonzo stops the player a tile short; the stage back to 1
        -- on the last click, a step onto the gate and through.
        t.ticklog.mark("leg.quit")
        t.exec("walk-quit", t.player.walk_to, 2641, 3439, 15)
        t.exec("quit.gate", t.player.click_loc, "fishinggateclosedr", 1)
        t.exec("quit.dialog", t.chat.play, {
            "npc:So you're calling it quits here for now?",
            "options", "choose:Yes I'll compete again another day.",
            "player:Yes I'll compete again another day.",
            "npc:Ok, I'll see you again.",
            "end",
        })
        t.ticks(4)
        t.expect("quest.stage.quit", t.quest.expect_stage("started"))

        -- ------------------------------------------------ Grandpa Jack
        -- [746..807] the hints, [810..836] the rod.
        t.ticklog.mark("leg.jack")
        t.exec("goto-jack-door-outside", t.player.goto_tile, 2649, 3447, 0)
        t.exec("jack.doorIn", t.player.pass_door, { closed = "poshdoor", open = "poshdooropen",
            at = { 2649, 3448, 0 }, near = { 2649, 3448 }, far = { 2649, 3449 } })
        t.exec("jack.greet", t.player.talk_to, "grandpa_jack")
        t.exec("jack.hints", t.chat.play, {
            "npc:Hello young",
            "options", "choose:Are you entering the fishing competition?",
            "player:Are you entering the fishing competition?",
            "npc:Ah... the Hemenster fishing competition...",
            "npc:I know all about that...",
            "options", "choose:I don't suppose you could give me any hints?",
            "player:I don't suppose you could give me any hints?",
            "npc:Well, you sometimes get these really big fish",
            "npc:I think they're some kind of carp...",
            "npc:Try to get a spot round there.",
            "npc:I used to get those from McGrubor's wood",
            "player:There's this weird creepy guy",
            "npc:Ahh well, I'm sure you'll find something",
            "npc:to be scared of garlic!",
            "player:You're right! Thanks Jack!",
            "end",
        })
        t.exec("jack.greet2", t.player.talk_to, "grandpa_jack")
        t.exec("jack.buy_rod", t.chat.play, {
            "npc:Hello young",
            "options", "choose:Can I buy one of your fishing rods?",
            "player:Can I buy one of your fishing rods?",
            "npc:Of course you can young",
            "options", "choose:Very fair, I'll buy that rod!",
            "player:Very fair, I'll buy that rod!",
            "npc:Excellent choice!",
            "objbox:You hand over the money and receive a fishing rod.",
            "player:Thanks!",
            "npc:You're welcome young 'un.",
            "end",
        })
        local rod_result, rod_coins = t.inv.await("fishing_rod", 1, 10)
        t.step("jack.rod_bought", rod_result == "ok" and "PASS" or "FAIL",
            "fishing_rod await 1 -> " .. tostring(rod_result) .. " " .. tostring(rod_coins))
        count_is("jack.rod_paid", "coins", 0, 10, "Jack's 5gp")
        t.exec("jack.doorOut", t.player.pass_door, { closed = "poshdoor", open = "poshdooropen",
            at = { 2649, 3448, 0 }, near = { 2649, 3449 }, far = { 2649, 3447 } })

        -- ------------------------------------------------ McGrubor's Wood
        -- [851..860]: the front gate from outside is the forester's.
        t.ticklog.mark("leg.mcgrubor")
        t.exec("goto-mcgrubor-gate", t.player.goto_tile, 2650, 3469, 0)
        t.exec("mcgrubor.gate", t.player.click_loc, "mcgruborgatel", 1)
        t.exec("mcgrubor.forester", t.chat.play, {
            "npc:Hey! You can't come through here! This is private land!",
            "mesbox:You will need to find another way in.",
            "end",
        })
        -- [949..952] in by the railing, [1017..1018] a worm, [1085..1092] out.
        t.exec("goto-railing-outside", t.player.goto_tile, 2661, 3500, 0)
        t.exec("railing.squeezeIn", t.player.cross_trap, { loc = "mcgruborlooserailing", op_name = "Squeeze-through",
            at = { 2662, 3500, 0 }, src = { 2661, 3500 }, dest = { 2662, 3500 }, attempts = 1 })
        t.exec("walk-red-vine", t.player.walk_to, 2632, 3497, 100)
        local vine_target, vine_lookup = t.player.by_symbol("loc", "red_worm_junction")
        t.step("vine.lookup", vine_target ~= nil and "PASS" or "FAIL",
            "by_symbol(loc, red_worm_junction) -> " .. tostring(vine_lookup)
                .. " id=" .. tostring(vine_target and vine_target.id))
        t.exec("redVine.useSpade", t.player.use_on, "spade", vine_target)
        local worm_result, worm_detail = t.inv.await("red_vine_worm", 1, 10)
        t.check("redVine.wormDug", worm_result == "ok",
            "red_vine_worm await 1 -> " .. tostring(worm_result) .. " " .. tostring(worm_detail))
        -- [1053..1060]: the front gate from inside is locked (the line and
        -- `locked` in the tick log).
        t.exec("walk-mcgrubor-gate-inside", t.player.walk_to, 2649, 3471, 100)
        t.exec("mcgrubor.gateLocked", t.player.click_loc, "mcgruborgater", 1)
        t.ticks(3)
        t.exec("walk-railing-inside", t.player.walk_to, 2662, 3499, 100)
        t.exec("railing.squeezeOut", t.player.cross_trap, { loc = "mcgruborlooserailing", op_name = "Squeeze-through",
            at = { 2662, 3500, 0 }, src = { 2662, 3499 }, dest = { 2661, 3500 }, attempts = 1 })

        -- ------------------------------------------------ back in on Morris's word
        -- [1154..1160]: the gate asks for the pass and lets the player through
        -- on the click of Morris's last page.
        t.ticklog.mark("leg.gate_again")
        t.exec("goto-hemenster-gate-outside2", t.player.goto_tile, 2644, 3441, 0)
        t.exec("enterHemenster.gateIn2", t.player.cross_gate, { loc = "fishinggateclosedr", at = { 2642, 3441, 0 },
            near = { 2643, 3441 }, far_ok = function(tile) return tile.x <= 2642 end,
            far_desc = "inside the Hemenster contest fence, x <= 2642",
            chat = { "npc:Competition pass please.", "objbox:You show Morris your pass.",
                "npc:Move on through. Talk to Bonzo to enter the competition." } })

        -- ------------------------------------------------ the garlic, the pipes
        -- [1186..1188]: the garlic in the pipe, both varbits on the box's click.
        t.ticklog.mark("leg.garlic_pipe")
        local pipe_target, pipe_lookup = t.player.by_symbol("loc", "garlicpipe")
        t.step("garlicpipe.lookup", pipe_target ~= nil and "PASS" or "FAIL",
            "by_symbol(loc, garlicpipe) -> " .. tostring(pipe_lookup))
        t.exec("walk-garlicpipe2", t.player.walk_to, 2638, 3445, 15)
        t.exec("putGarlicInPipe.use", t.player.use_on, "garlic", pipe_target)
        t.exec("putGarlicInPipe.box", t.chat.play, { "objbox:You stash the garlic in the pipe.", "end" })
        count_is("putGarlicInPipe.garlicGone", "garlic", 0, 5, "the garlic, on the box's click")
        local gp_result, gp = t.var.server("varb2051_fishingcompo_garlicpipe")
        t.check("putGarlicInPipe.varbit", gp_result == "ok" and gp == 1, "garlicpipe -> " .. tostring(gp))

        -- [1190..1210]: the pipes' spot, paid and out of the competition:
        -- Bonzo's places, the stranger's complaint and walk to the willow, the
        -- stage 3 with Bonzo's next page.
        t.exec("walk-pipes-spot", t.player.walk_to, 2638, 3444, 15)
        t.exec("pipes.takeSpot", t.player.talk_to, "0_41_53_sinisterfishspot")
        t.exec("pipes.place", t.chat.play, {
            "npc:Ok, nearly everyone is in their place already.",
            "npc:Arrgh! WHAT is that GHASTLY smell???",
            "end",
        })
        await_page("pipes.bonzo_page", "npc", 14)
        t.exec("pipes.place2", t.chat.play, {
            "npc:Hmm. You'd better go and take the area by the pipes then.",
            "mesbox:Your fishing competition spot is now beside the pipes.",
            "end",
        })
        t.expect("quest.stage.garlic_comp", t.quest.expect_stage("garlic_comp"))
        t.shot("garlicpipe-stranger-moved")

        -- [1212..1215]: one cast, the carp, a worm spent.
        t.ticklog.mark("leg.carp")
        t.ticks(2)
        t.exec("fish.carp", t.player.talk_to, "0_41_53_sinisterfishspot")
        local carp_result, carp_detail = t.inv.await("raw_giant_carp", 1, 10)
        t.check("fish.carp_landed", carp_result == "ok",
            "raw_giant_carp await 1 -> " .. tostring(carp_result) .. " " .. tostring(carp_detail))

        -- [1221..1249]: the hand-in.
        t.ticklog.mark("leg.handin_bonzo")
        t.exec("walk-bonzo-handin", t.player.walk_to, 2641, 3438, 15)
        t.exec("bonzo.howdoing", t.player.talk_to, "bonzo")
        t.exec("bonzo.accept_carp", t.chat.play, {
            "npc:So how are you doing so far?",
            "options", "choose:I have this big fish. Is it enough to win?",
            "player:I have this big fish.",
            "player:Is it enough to win?",
            "npc:Well, we'll just wait till time is up.",
            "mesbox:You wait",
            "end",
        })
        await_page("bonzo.handover_box", "mesbox", 14)
        t.exec("bonzo.handover", t.chat.play, {
            "mesbox:You hand over your catch.",
            "npc:We have a new winner!",
            "objbox:You are given the Hemenster fishing trophy!",
            "end",
        })
        t.expect("quest.stage.won_comp", t.quest.expect_stage("won_comp"))
        local trophy_result, trophy_detail = t.inv.await("hemenster_fishing_trophy", 1, 10)
        local _, carp_left = t.inv.count("raw_giant_carp")
        t.check("bonzo.trophy_granted", trophy_result == "ok" and carp_left == 0,
            "hemenster_fishing_trophy await 1 -> " .. tostring(trophy_result) .. " " .. tostring(trophy_detail)
                .. "; raw_giant_carp left=" .. tostring(carp_left))

        -- [1263..1265]: out by the gate's op, no Bonzo.
        t.exec("walk-hemenster-gate-inside", t.player.walk_to, 2642, 3441, 15)
        t.exec("speaktoVestri.gateOut", t.player.cross_gate, { loc = "fishinggateclosedr", at = { 2642, 3441, 0 },
            near = { 2642, 3441 }, far_ok = function(tile) return tile.x >= 2643 end,
            far_desc = "outside the Hemenster contest fence, x >= 2643" })

        -- ------------------------------------------------ Vestri's hand-in
        -- [1340..1357]: complete on the click of "Oh! It's even more shiny";
        -- the dwarf goes on once the scroll is closed.
        t.ticklog.mark("leg.vestri_handin")
        local skill_snap_result, skill_snap = t.skill.snapshot()
        t.step("quest.skill_snapshot", skill_snap_result == "ok" and "PASS" or "FAIL",
            "snapshot before hand-in -> " .. tostring(skill_snap_result))
        t.exec("goto-vestri-handin", t.player.goto_tile, 2820, 3486, 0)
        t.exec("dwarf.trophy_handin", t.player.talk_to, "tunnel_dwarf1")
        t.exec("dwarf.won_dialogue", t.chat.play, {
            "npc:Have you won yet?",
            "player:Yes I have!",
            "npc:Well done! That's brilliant! Do you have the trophy with you?",
            "player:Yep, I have it right here!",
            "npc:Oh! It's even more shiny and gold than I thought possible...",
        })
        t.quest.expect_complete()
        t.check("reward.fishing", t.skill.expect_gain("fishing", 2437, skill_snap))
        local _, trophy_left = t.inv.count("hemenster_fishing_trophy")
        t.check("dwarf.trophy_taken", trophy_left == 0,
            "hemenster_fishing_trophy left after the hand-in = " .. tostring(trophy_left))
        t.exec("dwarf.afterScroll", t.chat.play, {
            "npc:You've done us proud. Thank you.",
            "player:In where?",
            "npc:Why the tunnel of course!",
            "player:Excellent. That will come in most handy.",
            "end",
        })
        t.finish(0)
    end,
}
