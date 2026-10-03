-- Twilight's Promise. Relay file: one leg per author, legs table of
-- docs/quest_authoring/relay.md. Scaffold of tools/quest_gate/new_quest.py replaced.
-- Guide: Quest Helper helpers/quests/twilightspromise (ladder.py twilightspromise).
-- Parity notes: docs/quests/ladders/twilightspromise.notes.md.

return {
    id = "twilightspromise",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::twilightspromise", -- debugproc: reset quest state, stand beside Regulus outside Varrock
        "::complete quest_childrenofthesun", -- guide requirement: Children of the Sun
        -- Children of the Sun's own side effect (childrenofthesun.rs2:109): Varrock Regulus is a multinpc
        -- shell hidden at first_travel 0 (all.npc vmq2_quetzal_keeper_varrock). Not this quest's var.
        "::setvar varb9652_vmq2_first_travel 1",
        -- LEG 2 brought-along kit: Quest Helper lists "Two combat styles" for the Colosseum knight (guide enterColosseum)
        "::setlevel hitpoints 80",
        "::setlevel attack 60", -- melee style
        "::setlevel strength 70",
        "::setlevel defence 60",
        "::setlevel magic 40", -- second style: Wind Strike
        "::give rune_scimitar 1",
        "::give air_rune 200",
        "::give mind_rune 200",
        "::give shark 12", -- food for Mezan (lvl 81, max 8)
        "::give rune_full_helm 1",
        "::give rune_chainbody 1",
        "::give rune_platelegs 1",
    },
    bind = {
        varp = "varb9649_vmq2",
        constants = {
            not_started = 0, start = 2, metzli = 4, metzli2 = 6, prince = 8, prince2 = 10,
            ennius2 = 12, knights = 14, complete = 50,
        },
        row = "quest_twilightspromise",
        display = "Twilight's Promise",
        points = 1,
    },

    legs = {
        {
            name = "temple",
            run = function(t)
                t.ticks(3)
                t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

                -- LEG 1 BEGIN: talkToRegulusStart
                t.exec("goto-talkToRegulusStart", t.player.goto_tile, 3281, 3413, 0)
                -- Talk to Regulus Cento outside Varrock's East Gate (twilightspromise.rs2:147).
                t.exec("talkToRegulusStart", t.player.talk_to, "vmq2_quetzal_keeper_varrock", 1)
                t.exec("talkToRegulusStart-dialog", t.chat.play, {
                    "npc:Nilsal, adventurer. Do you wis",
                    "choose:Let's do it!",
                    "player:Let's do it!",
                    "npc:Then hold on tight. Varlamore ",
                })
                t.ticks(5)
                local _, arrive = t.world.tile()
                t.check("talkToRegulusStart.arrived", arrive ~= nil and arrive.x < 2000,
                    "flew to Civitas illa Fortis, tile " .. tostring(arrive and (arrive.x .. "," .. arrive.z)))

                t.exec("goto-talkToEnnius", t.player.goto_tile, 1687, 3141, 0)
                t.exec("talkToEnnius", t.player.talk_to, "vmq2_ennius_outer_palace", 1)
                t.exec("talkToEnnius-dialog", t.chat.play, {
                    "npc:Is this the one?",
                    "npc:I believe so.",
                    "npc:Doesn't look like much.",
                    "choose:Yes.",
                    "player:Hey I recognise you two!",
                    "npc:Indeed we were.",
                    "npc:More than part of.",
                    "npc:As was our duty.",
                    "npc:The Prince could have contributed.",
                    "npc:Do not air your dirty laundry",
                    "npc:Hmph!",
                    "npc:You. The Prince wishes",
                    "player:Where do I find this temple?",
                    "npc:On the other side of the bazaar",
                    "player:Okay. Guess I should get going",
                    "npc:Indeed.",
                })
                t.ticks(2)
                t.expect("quest.stage.metzli", t.quest.expect_stage("metzli"))

                t.exec("goto-talkToMetzli", t.player.goto_tile, 1697, 3087, 0)
                t.exec("talkToMetzli", t.player.talk_to, "vmq2_metzli_fortis", 1)
                t.exec("talkToMetzli-dialog", t.chat.play, {
                    "npc:I know your face.",
                    "player:Er... I think you must have me confused",
                    "npc:Yes... and no.",
                    "player:Your only companion is death?",
                    "npc:You jest, as your kind often do.",
                    "player:Well things got deep very quickly.",
                    "npc:Few seek an introduction",
                    "player:Right... So who actually are you?",
                    "npc:I am Metzli, Teokan of Ranul.",
                    "player:Teokan? So there are two of you?",
                    "npc:Yes. As Teokan of Ranul",
                    "choose:I'm meant to be meeting Prince Itzla here.",
                    "player:I'm meant to be meeting Prince Itzla here.",
                    "npc:Yes you are.",
                    "choose:I'd better head down there.",
                    "player:I'd better head down there.",
                    "npc:Farewell.",
                })
                t.ticks(2)
                t.expect("quest.stage.prince", t.quest.expect_stage("prince"))

                t.exec("enterCrypt", t.player.click_loc, "vmq2_temple_stairs_top", 1, { at = { x = 1692, z = 3088 } })
                t.ticks(3)
                local _, crypt = t.world.tile()
                t.check("enterCrypt.arrived", crypt ~= nil and crypt.z > 9000,
                    "in the crypt at " .. tostring(crypt and (crypt.x .. "," .. crypt.z)))

                -- The crypt gate (twilightspromise.rs2:~493) opens from stage 8: open it for real.
                t.exec("goto-cryptGate", t.player.goto_tile, 1698, 9493, 0)
                local camr = t.drive.camera(0, 383, 700)
                t.check("cryptGate.camera", camr ~= nil, "camera turned to yaw 0 pitch 383 zoom 700 before the gate click, verb answered " .. tostring(camr))
                t.ticks(2)
                t.exec("openCryptGate", t.player.click_loc, "vmq2_temple_gate", 1)
                t.ticks(2)
                t.exec("goto-talkToPrince", t.player.goto_tile, 1685, 9512, 0)
                t.exec("talkToPrince", t.player.talk_to, "vmq2_itzla_fortis", 1)
                t.exec("talkToPrince-dialog", t.chat.play, {
                    "npc:Ah, there you are.",
                    "player:Your father?",
                    "npc:Yes, very fun stuff.",
                    "npc:Nilsal, my child.",
                    "npc:Well let's not start celebrating",
                    "npc:Sorry about that, by the way.",
                    "player:It's not a problem.",
                    "npc:Ah, the Tullus twins.",
                    "player:Actually, I was talking about Metzli.",
                    "npc:Oh, you met her!",
                    "npc:Metzli and her sect",
                    "npc:You're quite right",
                    "player:Sounds like a plan.",
                    "npc:Kuani! So we have quite",
                    "npc:Someone wants our dear Teokan dead",
                    "player:A Varlamorian who had access",
                    "npc:Right you are,",
                    "npc:Now I don't think it was me",
                    "npc:I'd like to think",
                    "npc:Too right.",
                    "player:So that leaves the Tullus twins",
                    "npc:Ennius and Furia are loyal",
                    "npc:Those knights should also",
                    "npc:Servius, until we have",
                    "npc:Nonsense!",
                    "npc:The Teomat does not offer",
                    "npc:I am the chosen messenger",
                    "npc:Well this is a terrible idea",
                    "npc:My child, I would be delighted",
                    "npc:, since Servius here seems keen",
                    "player:I'm sure I can manage that.",
                    "npc:Kuani! Head up to the palace",
                    "player:I'll get to it right away.",
                    "npc:Then it sounds like we're all set.",
                    "npc:Timoiva, my child.",
                })
                t.ticks(2)
                t.expect("quest.stage.ennius2", t.quest.expect_stage("ennius2"))

                t.exec("goto-cryptGateBack", t.player.goto_tile, 1698, 9499, 0)
                -- the gate loc is swapped for its open form (no closed 50861 in the pool): walk back through it
                local closed_r = t.world.loc_near("vmq2_temple_gate", 12)
                t.check("cryptGate.stillOpen", closed_r == "not_found",
                    "closed vmq2_temple_gate lookup near 1698,9499: " .. tostring(closed_r) .. " (open form in place)")
                t.exec("goto-leaveCrypt", t.player.goto_tile, 1694, 9493, 0)
                t.exec("leaveCrypt", t.player.click_loc, "vmq2_temple_stairs_bottom", 1, { at = { x = 1691, z = 9492 } })
                t.ticks(3)
                local _, up = t.world.tile()
                t.check("leaveCrypt.arrived", up ~= nil and up.z < 9000,
                    "back in the temple at " .. tostring(up and (up.x .. "," .. up.z)))

                t.exec("goto-talkToEnnius2", t.player.goto_tile, 1684, 3156, 0)
                t.exec("talkToEnnius2", t.player.talk_to, "vmq2_ennius_inner_palace", 1)
                t.exec("talkToEnnius2-dialog", t.chat.play, {
                    "npc:It seems the Prince has new chores",
                    "npc:You do not know that.",
                    "npc:Why else would he have sent",
                    "player:You two are quite the pair",
                    "npc:Thank you.",
                    "npc:I do not believe that was a compliment",
                    "npc:Oh... Well let's get to it then.",
                    "player:He's asked me to investigate the knights",
                    "npc:Has he forgotten that he himself",
                    "player:He's staying with Servius",
                    "npc:It is wise to keep the Teokan",
                    "npc:If you say so.",
                    "npc:The delegation consisted of six knights.",
                    "npc:Arrun and Claudia should be",
                    "player:What if any refuse to come?",
                    "npc:This crest will mark you",
                    "npc:Not unless they fancy",
                })
                -- the crest box (objbox) is not a chat.play entry: drain it to the question menu
                local box_r, box_d = t.chat.drain({ stop_at = "options" })
                t.expect("talkToEnnius2.crest_box", box_r, box_d)
                t.exec("talkToEnnius2-leave", t.chat.play, {
                    "choose:I'd better get going.",
                    "player:I'd better get going.",
                })
                t.ticks(2)
                t.expect("quest.stage.knights", t.quest.expect_stage("knights"))
                t.expect("talkToEnnius2.crest", t.inv.expect_has("vmq2_crest", 1))
                -- LEG 1 END
                t.ticks(2)
                local _, lx = t.world.tile()
                local _, stage = t.var.server("varb9649_vmq2")
                t.check("leg.1.state", lx ~= nil and lx.z > 3000 and lx.z < 9000 and stage == 14, "tile " .. tostring(lx and (lx.x .. "," .. lx.z .. "," .. lx.level))
                    .. " stage=" .. tostring(stage))
            end,
        },
        {
            name = "knights",
            run = function(t)
                t.ticks(2)
                -- LEG 2 BEGIN: talkToBazaarKnight
                t.exec("goto-talkToBazaarKnight", t.player.goto_tile, 1682, 3106, 0)
                t.exec("talkToBazaarKnight", t.player.talk_to, "vmq2_knight_1_vis", 1)
                t.exec("talkToBazaarKnight-dialog", t.chat.play, {
                    "player:Hello there.",
                    "npc:Nilsal. Can we help",
                    "player:Ennius and Furia Tullus need",
                    "npc:If that were so",
                    "player:Well it just so happens",
                    "*", -- objbox: the crest shown
                    "npc:Hmm... Well we're not going",
                    "player:I can do that",
                    "npc:A band of thieves",
                    "npc:Find the amulet",
                    "player:How will I find it",
                    "npc:Beat them at their own game",
                    "player:Well this doesn't sound",
                })
                t.ticks(2)
                t.expect("bazaar.asked", t.var.expect("varb9829_vmq2_bazaar_knights", 1))

                t.exec("goto-pickpocketCitizen", t.player.goto_tile, 1686, 3107, 0)
                local amulet = false
                for attempt = 1, 25 do
                    local pr, pd = t.player.press("vmq2_citizen_vis", 3, 6)
                    t.ticks(3)
                    local _, n = t.inv.count("vmq2_amulet")
                    if (n or 0) > 0 then
                        amulet = true
                        t.check("pickpocketCitizen", true, "attempt " .. attempt .. ": press " .. tostring(pr) .. ", stolen amulet in backpack x" .. tostring(n))
                        break
                    end
                    t.chat.drain({})
                    t.ticks(4)
                end
                if not amulet then t.check("pickpocketCitizen", false, "no amulet after 25 pickpocket attempts") end
                t.chat.drain({})
                t.expect("bazaar.amulet_taken", t.var.expect("varb9829_vmq2_bazaar_knights", 2))

                t.exec("goto-returnAmulet", t.player.goto_tile, 1682, 3106, 0)
                t.exec("returnAmulet", t.player.talk_to, "vmq2_knight_1_vis", 1)
                t.exec("returnAmulet-dialog", t.chat.play, {
                    "npc:Do you have the amulet?",
                    "player:I have it right here",
                    "npc:Good work",
                    "player:So you'll return",
                    "npc:We'll be there once",
                    "player:Well good luck",
                    "*", -- objbox: the amulet handed over
                })
                t.ticks(2)
                t.expect("quest.stage.bazaar_done", t.quest.expect_stage(16))

                t.exec("goto-talkToCothonKnight", t.player.goto_tile, 1746, 3118, 0)
                t.exec("talkToCothonKnight", t.player.talk_to, "vmq2_knight_3_vis", 1)
                t.exec("talkToCothonKnight-dialog", t.chat.play, {
                    "player:Hello there. I'm here on behalf",
                    "npc:You're working for the Tullus",
                    "*", -- objbox: the crest shown
                    "npc:Huh... Fair enough",
                    "player:What is it you're doing?",
                    "npc:With the recent opening",
                    "npc:We've had a tip-off",
                    "npc:If you can find",
                    "player:Sounds easy enough",
                    "npc:You say that now",
                    "npc:The crate should be",
                    "player:Good to know",
                })
                t.ticks(2)
                t.expect("cothon.asked", t.var.expect("varb9830_vmq2_cothon_knight", 1))

                t.exec("goto-searchCrate", t.player.goto_tile, 1776, 3149, 0)
                t.exec("searchCrate", t.player.click_loc, "vmq2_cothon_crate", 1, { at = { x = 1778, z = 3149 } })
                t.ticks(2)
                t.exec("searchCrate-box", t.chat.drain, {})
                t.ticks(2)
                t.expect("cothon.crate_found", t.var.expect("varb9830_vmq2_cothon_knight", 2))

                t.exec("goto-returnToCothonKnight", t.player.goto_tile, 1746, 3118, 0)
                t.exec("returnToCothonKnight", t.player.talk_to, "vmq2_knight_3_vis", 1)
                t.exec("returnToCothonKnight-dialog", t.chat.play, {
                    "npc:Any luck finding that crate?",
                    "player:Yes, I found it",
                    "npc:Perfect!",
                    "player:Great!",
                })
                t.ticks(2)
                t.expect("quest.stage.cothon_done", t.quest.expect_stage(18))

                t.exec("goto-talkToPubKnights", t.player.goto_tile, 1723, 3074, 0)
                t.exec("talkToPubKnights", t.player.talk_to, "vmq2_knight_5_drunk", 1)
                t.exec("talkToPubKnights-dialog", t.chat.play, {
                    "player:Hello!",
                    "npc:Well look who it is!",
                    "player:Er... I don't know you",
                    "npc:Oh...",
                    "npc:I told you not to have",
                    "npc:Stop whining!",
                    "player:Right... The two of you",
                    "npc:Report?",
                    "player:Sorry, but I have this crest",
                    "*", -- objbox: the crest shown
                    "npc:Ah, now that's going",
                    "player:How so?",
                    "npc:Azali here can't",
                    "player:Hmm... Where's the nearest",
                    "npc:There's one outside",
                    "player:Well there's nothing like",
                    "npc:Ohh... We're going somewhere new",
                })
                t.ticks(2)
                t.expect("pub.following", t.var.expect("varb9834_vmq2_drunk_knight_vis", 2))

                -- walk her to the fountain: she stops now and then (knights.rs2:364), talk to her again
                local led = false
                for leg_step = 1, 30 do
                    t.player.walk_to(1757, 3068, 40)
                    t.ticks(6)
                    local _, pub = t.var.server("varb9831_vmq2_pub_knights")
                    if (pub or 0) >= 2 then led = true; break end
                    local nr = t.player.talk_to("vmq2_knight_5_follower", 1)
                    t.chat.drain({})
                    t.ticks(2)
                end
                local _, pubv = t.var.server("varb9831_vmq2_pub_knights")
                local _, wt = t.world.tile()
                t.check("takePubKnightsToFountain", led, "walked from the pub, pub group var " .. tostring(pubv) .. ", standing at " .. tostring(wt and (wt.x .. "," .. wt.z)))

                t.exec("talkToPubKnightAtFountain", t.player.talk_to, "vmq2_knight_5_vis", 1)
                t.exec("talkToPubKnightAtFountain-dialog", t.chat.play, {
                    "npc:Oh boy...",
                    "player:Feeling a bit better",
                    "npc:Not exactly.",
                    "player:Well it will have to do",
                    "npc:This day is the worst",
                })
                t.ticks(2)
                t.expect("quest.stage.pub_done", t.quest.expect_stage(20))

                t.exec("wield-scimitar", t.player.equip, "rune_scimitar")
                t.exec("wield-helm", t.player.equip, "rune_full_helm")
                t.exec("wield-chainbody", t.player.equip, "rune_chainbody")
                t.exec("wield-platelegs", t.player.equip, "rune_platelegs")
                t.exec("goto-enterColosseum", t.player.goto_tile, 1795, 3106, 0)
                t.exec("enterColosseum", t.player.click_loc, "colosseum_entrance_outside", 1)
                t.ticks(3)
                local _, lobby = t.world.tile()
                t.check("enterColosseum.arrived", lobby ~= nil and lobby.z > 9000, "in the lobby at " .. tostring(lobby and (lobby.x .. "," .. lobby.z)))

                t.exec("talkToColosseumKnight", t.player.talk_to, "vmq2_knight_6_colosseum", 1)
                t.exec("talkToColosseumKnight-dialog", t.chat.play, {
                    "player:Hi there.",
                    "npc:Sorry, but I'm waiting",
                    "player:Well you might need",
                    "npc:You don't work for the palace",
                    "player:I'm working directly",
                    "npc:If you are, then prove it",
                    "*", -- objbox: the crest shown
                    "npc:Brand new here",
                    "player:Maybe, but like I say",
                    "npc:And I'll return there",
                    "npc:Hmm... You look like",
                    "player:I can?",
                    "npc:Yes! Join me",
                    "npc:Oh, and bring two attack styles",
                    "options",
                    "choose:I'm ready. Let's do this.",
                    "player:I'm ready",
                    "npc:Kuani!",
                })
                t.ticks(4)
                local _, arena = t.world.tile()
                t.check("talkToColosseumKnight.arena", arena ~= nil and arena.x > 6000 and arena.x ~= lobby.x, "in the arena copy at " .. tostring(arena and (arena.x .. "," .. arena.z .. "," .. arena.level)))

                -- Mezan prays against a style used four swings in a row (colosseum.rs2:102): never more than three
                -- melee swings before one Wind Strike, so the streak never reaches four.
                local _, food0 = t.inv.count("shark")
                local lowest = 999
                local dead = false
                t.exec("defeatColosseumKnight", t.player.attack, "vmq2_knight_6_combat", 2, 30)
                for cycle = 1, 40 do
                    local wr, wd = t.npc.await_dead_engaged(9, 1, { eat = { item = "shark", below = 40 } })
                    local lo = tonumber(tostring(wd):match("lowest hp (%d+)/"))
                    if lo and lo < lowest then lowest = lo end
                    if wr == "ok" then dead = true; break end
                    local cr = t.player.cast("wind_strike", "vmq2_knight_6_combat", 8)
                    local cr2, cd2 = t.npc.await_dead_engaged(3, 1, { eat = { item = "shark", below = 40 } })
                    if cr2 == "ok" then dead = true; break end
                    t.player.attack("vmq2_knight_6_combat", 2, 8)
                end
                local _, food1 = t.inv.count("shark")
                t.check("defeatColosseumKnight.dead", dead, "Mezan beaten (loop of 3 melee swings then one Wind Strike), lowest hp " .. tostring(lowest) .. "/80")
                t.check("defeatColosseumKnight.dead.margin", (food1 or 0) >= 1 and lowest >= 25, "sharks before " .. tostring(food0) .. ", left " .. tostring(food1) .. ", lowest hp " .. tostring(lowest) .. "/80 (margin: food left >= 1 AND lowest hp >= 25)")
                t.ticks(10)
                t.msg.expect("You leave the battle")
                t.expect("colosseum.beaten", t.var.expect("varb9832_vmq2_colosseum_knight", 2))

                t.exec("talkToColosseumKnight2", t.player.talk_to, "vmq2_knight_6_colosseum", 1)
                t.exec("talkToColosseumKnight2-dialog", t.chat.play, {
                    "npc:You fought well in there",
                    "player:So you'll return",
                    "npc:Yes, I'll head over",
                    "player:Perfect! Thanks.",
                })
                t.ticks(2)
                t.expect("quest.stage.knights_done", t.quest.expect_stage(22))
                -- LEG 2 END
                t.ticks(2)
                local _, ex = t.world.tile()
                local _, stage2 = t.var.server("varb9649_vmq2")
                t.check("leg.2.end", ex ~= nil and stage2 == 22 and ex.z > 9000, "tile " .. tostring(ex and (ex.x .. "," .. ex.z .. "," .. ex.level))
                    .. " stage=" .. tostring(stage2) .. "; carrying crest, sharks, runes, scimitar worn")
            end,
        },
        {
            name = "headquarters",
            run = function(t)
                t.ticks(2)
                -- LEG 3 BEGIN: climbStairs
                t.exec("climbStairs", t.player.click_loc, "colosseum_exit_lobby", 1)
                t.ticks(3)
                local _, out = t.world.tile()
                t.check("climbStairs.arrived", out ~= nil and out.z < 9000,
                    "out of the lobby at " .. tostring(out and (out.x .. "," .. out.z)))

                t.exec("goto-talkToEnniusAfterKnights", t.player.goto_tile, 1684, 3156, 0)
                t.exec("talkToEnniusAfterKnights", t.player.talk_to, "vmq2_ennius_inner_palace", 1)
                t.exec("talkToEnniusAfterKnights-dialog", t.chat.play, {
                    "npc:Have you spoken to all the knights",
                    "player:Yes. They should be here soon",
                    "npc:Six knights",
                    "player:Well I'm going to guess",
                    "npc:After they were picked",
                    "npc:We'll keep the knights busy",
                    "player:Okay, I'll be back soon",
                })
                t.ticks(2)
                t.expect("quest.stage.letter", t.quest.expect_stage(24))

                -- Walk from the palace square to the HQ's open arches at x 1652 (no door, no teleport inside).
                t.player.walk_to(1660, 3155, 30)
                t.ticks(10)
                t.player.walk_to(1652, 3155, 20)
                t.ticks(8)
                t.player.walk_to(1645, 3155, 12)
                t.ticks(8)
                local _, inside = t.world.tile()
                t.check("hq.inside", inside ~= nil and inside.x < 1655, "inside the HQ at " .. tostring(inside and (inside.x .. "," .. inside.z)))
                t.exec("goUpHQ", t.player.click_loc, "civitas_stairs_spiral", 1, { at = { x = 1638, z = 3155 } })
                t.ticks(3)
                local _, f1 = t.world.tile()
                t.check("goUpHQ.arrived", f1 ~= nil and f1.level == 1, "on floor " .. tostring(f1 and f1.level) .. " at " .. tostring(f1 and (f1.x .. "," .. f1.z)))
                t.player.walk_to(1649, 3155, 15)
                t.ticks(8)
                t.exec("goUpHQ2", t.player.click_loc, "civitas_stairs_spiral", 1, { at = { x = 1650, z = 3155 } })
                t.ticks(3)
                local _, f2 = t.world.tile()
                t.check("goUpHQ2.arrived", f2 ~= nil and f2.level == 2, "on floor " .. tostring(f2 and f2.level) .. " at " .. tostring(f2 and (f2.x .. "," .. f2.z)))
                t.player.walk_to(1648, 3144, 15)
                t.ticks(8)
                t.exec("searchHQChest", t.player.click_loc, "vmq2_knight_4_chest", 1)
                t.ticks(2)
                -- The letter is in one of two chests, random per player (twilightspromise_knights.rs2:452-460).
                local ck = t.chat.kind()
                if ck == "mesbox" then
                    t.chat.drain({ max_pages = 3 })
                    t.player.walk_to(1648, 3144, 6)
                    t.ticks(4)
                    t.exec("searchHQChest-other", t.player.click_loc, "vmq2_knight_1_chest", 1)
                    t.ticks(2)
                    ck = t.chat.kind()
                end
                t.check("searchHQChest.page", ck == "objbox", "found the letter, chat kind " .. tostring(ck) .. ": " .. tostring(t.chat.text()))
                t.chat.drain({ max_pages = 3 })
                t.ticks(2)
                t.expect("searchHQChest.letter", t.inv.expect_has("vmq2_incriminating_letter", 1))
                t.expect("quest.stage.letter2", t.quest.expect_stage(26))

                t.exec("readLetter", t.player.inv_op, "vmq2_incriminating_letter", 1)
                t.exec("readLetter-dialog", t.chat.play, {
                    "mesbox:Velam,",
                    "mesbox:Long have you waited",
                    "mesbox:Only with his demise will we see",
                    "end",
                })
                t.ticks(2)
                t.expect("quest.stage.bring", t.quest.expect_stage(28))
                t.ticks(2)
                local _, endt = t.world.tile()
                local _, st3 = t.var.server("varb9649_vmq2")
                t.check("leg.3.state", endt ~= nil and st3 == 28, "tile " .. tostring(endt and (endt.x .. "," .. endt.z .. "," .. endt.level))
                    .. " stage=" .. tostring(st3) .. "; carrying the incriminating letter (read), crest, sharks, runes")
                -- LEG 3 END
            end,
        },
        {
            name = "teomat",
            run = function(t)
                t.ticks(2)
                -- LEG 4 BEGIN: goDownHQ2To1
                t.exec("goDownHQ2To1", t.player.click_loc, "civitas_stairs_spiral_down", 1, { at = { x = 1651, z = 3155 } })
                t.ticks(3)
                local _, d1 = t.world.tile()
                t.check("goDownHQ2To1.arrived", d1 ~= nil and d1.level == 1, "on floor " .. tostring(d1 and d1.level) .. " at " .. tostring(d1 and (d1.x .. "," .. d1.z)))
                t.player.walk_to(1639, 3155, 15)
                t.ticks(8)
                t.exec("goDownHQ1To0", t.player.click_loc, "civitas_stairs_spiral_down", 1, { at = { x = 1638, z = 3155 } })
                t.ticks(3)
                local _, d0 = t.world.tile()
                t.check("goDownHQ1To0.arrived", d0 ~= nil and d0.level == 0, "on floor " .. tostring(d0 and d0.level) .. " at " .. tostring(d0 and (d0.x .. "," .. d0.z)))
                t.player.walk_to(1660, 3150, 25)
                t.ticks(10)

                t.exec("goto-returnToEnniusAfterLetter", t.player.goto_tile, 1684, 3156, 0)
                t.exec("returnToEnniusAfterLetter", t.player.talk_to, "vmq2_ennius_inner_palace", 1)
                t.exec("returnToEnniusAfterLetter-dialog", t.chat.drain, { max_pages = 40 })
                t.ticks(3)
                t.expect("quest.stage.regulus", t.quest.expect_stage(34))

                t.exec("goto-talkToRegulusForTransport", t.player.goto_tile, 1699, 3141, 0)
                t.exec("talkToRegulusForTransport", t.player.talk_to, "vmq2_quetzal_keeper_fortis", 1)
                t.exec("talkToRegulusForTransport.menu", t.chat.drain, { stop_at = "options" })
                t.exec("talkToRegulusForTransport.ask", t.chat.choose, "I was told to ask you about getting to the Teomat.")
                t.exec("talkToRegulusForTransport.tail", t.chat.drain, { max_pages = 20 })
                t.ticks(3)
                t.expect("quest.stage.feed", t.quest.expect_stage(36))
                t.expect("regulus.feed", t.inv.expect_has("vmq2_quetzal_feed", 1))

                -- Renu stands in a pen: reach her from the south (notes).
                t.exec("goto-feedRenu", t.player.goto_tile, 1703, 3141, 0)
                t.exec("feedRenu", t.player.talk_to, "quetzal_fortis", 1)
                t.chat.continue_()
                t.ticks(2)
                t.exec("feedRenu-dialog", t.chat.play, { "npc:Kuani! I reckon she" })
                t.ticks(3)
                t.expect("quest.stage.teomat", t.quest.expect_stage(38))
                local _, uiopen = t.ui.await_open("quetzal_menu", 8)
                t.check("feedRenu.menu", uiopen ~= nil, "quetzal map opened after the feed: " .. tostring(uiopen))
                t.key("escape")
                t.ticks(2)

                t.exec("travelToTeomat", t.player.talk_to, "quetzal_child_green", 1)
                t.ticks(3)
                t.ui.await_open("quetzal_menu", 8)
                local wr, wid = t.ui.widget("quetzal_menu:icons", 1)
                t.check("travelToTeomat.widget", wr == "ok" and wid ~= nil, "Teomat destination icon (slot 1 = id 2): " .. tostring(wr) .. " " .. tostring(wid))
                t.ui.invoke(wid, 1)
                t.ticks(6)
                local _, fly = t.world.tile()
                t.check("travelToTeomat.arrived", fly ~= nil and fly.x < 1500 and fly.x > 1400, "landed at " .. tostring(fly and (fly.x .. "," .. fly.z .. "," .. fly.level)))

                t.exec("goto-talkToPrinceInTemple", t.player.goto_tile, 1452, 3173, 0)
                t.exec("talkToPrinceInTemple", t.player.talk_to, "vmq2_itzla_teomat", 1)
                t.exec("talkToPrinceInTemple-dialog", t.chat.drain, { max_pages = 80 })
                t.ticks(3)
                t.expect("quest.stage.temple", t.quest.expect_stage(42))

                t.exec("goto-talkToMetzliNearTemple", t.player.goto_tile, 1448, 3194, 0)
                t.exec("talkToMetzliNearTemple", t.player.talk_to, "vmq2_metzli_teomat", 1)
                t.exec("talkToMetzliNearTemple-dialog", t.chat.drain, { max_pages = 80 })
                t.ticks(4)
                t.expect("quest.stage.cultists", t.quest.expect_stage(44))

                local cultists = { "vmq2_cultist_m_1", "vmq2_cultist_f_1", "vmq2_cultist_m_2", "vmq2_cultist_f_2", "vmq2_cultist_m_3", "vmq2_cultist_f_3" }
                local _, food0 = t.inv.count("shark")
                local lowest, kills = 999, 0
                for round = 1, 40 do
                    local _, k = t.var.server("varp7291_tp_battle_kills")
                    kills = tonumber(k) or kills
                    local _, stg = t.var.server("varb9649_vmq2")
                    if kills >= 8 or stg ~= 44 then break end
                    for _, sym in ipairs(cultists) do
                        local ar = t.player.attack(sym, 2, 12)
                        if ar == "ok" then
                            local dr, dd = t.npc.await_dead_engaged(15, 1, { eat = { item = "shark", below = 40 } })
                            local lo = tonumber(tostring(dd):match("lowest hp (%d+)/"))
                            if lo and lo < lowest then lowest = lo end
                            break
                        end
                    end
                end
                local _, kfin = t.var.server("varp7291_tp_battle_kills")
                local _, food1 = t.inv.count("shark")
                t.check("defeat8Cultists.kills", tonumber(kfin) == 8, "cultists felled " .. tostring(kfin) .. " of 8 (varp7291_tp_battle_kills)")
                t.check("defeat8Cultists.margin", (food1 or 0) >= 1 and lowest >= 25, "sharks before " .. tostring(food0) .. ", left " .. tostring(food1) .. ", lowest hp " .. tostring(lowest) .. "/80 (margin: food left >= 1 AND lowest hp >= 25)")
                t.ticks(8)
                t.msg.expect("You leave the battle")
                t.exec("defeat8Cultists-aftermath", t.chat.drain, { max_pages = 30 })
                t.ticks(3)
                t.expect("quest.stage.finish2", t.quest.expect_stage(48))

                local snapr, snap = t.skill.snapshot()
                t.check("skill.snapshot", snapr == "ok", "snapshot before the hand-in: " .. tostring(snapr))
                t.exec("goto-finishQuest", t.player.goto_tile, 1454, 3173, 0)
                t.exec("finishQuest", t.player.talk_to, "vmq2_itzla_teomat", 1)
                t.exec("finishQuest-dialog", t.chat.drain, { max_pages = 30 })
                t.ticks(3)
                t.quest.expect_complete()
                local tr, td = t.skill.expect_gain("thieving", 3000, snap)
                t.check("reward.thieving", tr == "ok", "thieving +3000 xp documented: " .. tostring(tr) .. " " .. tostring(td))
                local _, ft = t.var.server("varb9652_vmq2_first_travel")
                t.check("reward.quetzal", ft == 3, "Quetzal Transport System unlocked (varb9652_vmq2_first_travel " .. tostring(ft) .. " == 3, twilightspromise.rs2:86)")
                local _, visited = t.var.server("varb9650_varlamore_visited")
                t.check("reward.teleport", visited == 1, "Civitas illa Fortis Teleport unlock carrier varb9650_varlamore_visited " .. tostring(visited) .. " == 1 (twilightspromise.rs2:84, scroll line 'Civitas illa Fortis Teleport spell')")
                -- LEG 4 END
                t.finish(0)
            end,
        },
    },
}
