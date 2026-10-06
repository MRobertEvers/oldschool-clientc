-- Watchtower. Authored against OSRS-Content/osrs239-content/server/scripts/
-- quests/quest_itwatchtower/scripts/*.rs2 (watchtower_wizard.rs2, grew.rs2,
-- og.rs2, toban.rs2, gorad.rs2, ogre_guard.rs2, city_guard.rs2, skavid.rs2,
-- enclave_guard.rs2, ogre_potion.rs2, ogre_shaman.rs2, quest_itwatchtower.rs2)
-- and Quest Helper's helpers/quests/watchtower/Watchtower.java (step order
-- read off loadSteps()'s ConditionalStep chains, simulated state by state,
-- and cross-checked against getPanels()'s "Investigate" panel order, which
-- matches exactly: searchBush, talkToWizardAgain, talkToOg, useRopeOnBranch,
-- talkToGrew, leaveGrewIsland, enterHoleSouthOfGuTanoth, killGorad,
-- talkToToban, giveTobanDragonBones, searchChestForTobansGold, talkToOgAgain,
-- useRopeOnBranchAgain, talkToGrewAgain, talkToWizardWithRelic).
--
-- getItemRequirements() lists coins20, goldBar, deathRune, pickaxe,
-- dragonBones, rope2, guamUnf, lightSource, pestleAndMortar, batBones,
-- jangerberries as bring-along items (none are gathered/bought/looted by a
-- guide step), so setup ::give's them. Combat gear/levels are a prerequisite
-- (getCombatRequirements(): "Gorad (level 68)"), not the quest's own work.
--
-- The crystal hand-over, placement and lever are content since 39e774122
-- (watchtower_wizard.rs2 made_potion branch); this file drives the whole
-- quest to the completion scroll.

return {
    id = "itwatchtower",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000, -- every ladder, swing, gate and cave on foot (b62 round 3)
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so bring-along gear fits
        -- Magic 45 + 5 air / 1 law: the start is a real Camelot Teleport
        -- (magic_spells.dbrow [magic_spell_teleport_camelot]: level 45, airrune 5,
        -- lawrune 1, tele_coord 0_43_54_5_22 = 2757,3478). Lumbridge to Yanille
        -- has no walk on foot that does not pass a members' gate (owner ruling
        -- 2026-10-05: the first goto obeys the door rule). Combat level is
        -- unchanged (melee outweighs 45 Magic).
        "::setlevel magic 45",
        "::give airrune 5",
        "::give lawrune 1",
        "::setlevel thieving 15",
        "::setlevel agility 25",
        "::setlevel herblore 14",
        "::setlevel mining 40",
        -- Combat prerequisite (Quest Helper getCombatRequirements(): "Gorad
        -- (level 68)"); gear is armed in run(), not the quest's own work.
        "::setlevel attack 60",
        "::setlevel strength 70",
        "::setlevel defence 60",
        "::setlevel hitpoints 60",
        -- getItemRequirements() bring-along items (none has a gather/buy/loot
        -- step in the guide -- rule (c)).
        "::give rune_scimitar",
        "::give adamant_pickaxe", -- pickaxe req 31 <= mining 40
        "::give dragon_bones",    -- Toban's "prove your might" gift
        "::give rope 2",          -- tree_ropeswing4_norope, two outbound swings
        "::give guamvial",        -- Guam potion (unf)
        "::give torch_lit",       -- lit light source for the skavid caves
        "::give pestle_and_mortar",
        "::give bat_bones",
        "::give jangerberries",
        "::give gold_bar",        -- ogre_guard1's SE gate toll
        "::give deathrune",       -- city guard's riddle answer
        "::give coins 50",        -- tanothjump1's 20gp toll
        "::give shark 10",        -- Gorad + enclave shaman food (s25rc_wt2 died at the second shaman on 5)
        -- Herblore is locked behind Druidic Ritual (run 2: grindBatBones
        -- refused "You need to complete the Druidic Ritual quest..."); a
        -- prerequisite's state comes only from ::complete, never ::setvar.
        "::complete quest_druidicritual", -- quest_cheat.rs2's dispatch row (not quest_druid)
        -- Run 2 & 3: killGorad's own press was refused "I'm already under
        -- attack." every attempt (71 ticks straight) -- m40_47.spawn has
        -- four ogre2 + two plain ogre spawns within a handful of tiles of
        -- Gorad's own (2577,3021), any of which can wander over and aggro
        -- the (still fairly low combat-level) fixture character first,
        -- claiming HIM and refusing every Attack on Gorad in the meantime.
        -- Section F's Mort'ton precedent: ::passive the wandering type, not
        -- more waiting (whatever holds the claim can keep swinging
        -- indefinitely).
        "::passive ogre2",
        "::passive ogre",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp212_itwatchtower",
            constants = {
                complete = 13,
                gutanoth_found_gold = 2,
                gutanoth_looking_gold = 1,
                itwatchtower_complete = 13,
                itwatchtower_complete_read_scroll = 14,
                itwatchtower_fed_nightshade = 8,
                itwatchtower_found_all_crystals = 11,
                itwatchtower_given_fingernails = 2,
                itwatchtower_given_relic = 4,
                itwatchtower_given_riddle = 5,
                itwatchtower_helped_grew = 4,
                itwatchtower_helped_og = 6,
                itwatchtower_helped_toban = 2,
                itwatchtower_learned_ar = 13,
                itwatchtower_learned_cur = 15,
                itwatchtower_learned_ig = 14,
                itwatchtower_learned_nod = 16,
                itwatchtower_learned_potion = 9,
                itwatchtower_learning_skavid = 12,
                itwatchtower_looking_relic = 0,
                itwatchtower_made_potion = 10,
                itwatchtower_made_relic = 3,
                itwatchtower_market_lower = 10,
                itwatchtower_market_upper = 11,
                itwatchtower_not_started = 0,
                itwatchtower_relic1 = 7,
                itwatchtower_relic2 = 8,
                itwatchtower_relic3 = 9,
                itwatchtower_shaman_kills_lower = 17,
                itwatchtower_shaman_kills_upper = 19,
                itwatchtower_skavid_crystal = 7,
                itwatchtower_solved_riddle = 6,
                itwatchtower_spoken_grew = 3,
                itwatchtower_spoken_og = 5,
                itwatchtower_spoken_toban = 1,
                itwatchtower_started = 1,
                not_started = 0,
            },
            display = "Watchtower",
            points = 4,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.exec("gear.equip_scimitar", t.player.equip, "rune_scimitar")

        -- ===============================================================
        -- Route helpers (door rule, b62 round 3). Every floor change is a
        -- climb, every pocket is left by the loc the content gives it:
        --   Grew's island (134 tiles, x 2505-2519 z 3079-3092): IN by rope
        --     on tree_ropeswing4_norope (quest_itwatchtower.rs2:314-331,
        --     lands loc+6 = 2505,3087), OUT by tree_ropeswing3 (rope_swings
        --     .rs2:45, z axis 3089 -> 3096). Wiki: "Leave the island using
        --     the rope swing on the north side."
        --   Toban's island (125 tiles): IN by tobancave (p_teleport
        --     2576,3029), OUT by tobanladderdown (p_teleport 2500,2988).
        --   Gu'Tanoth (523 tiles): the NW gate ogreguardgate2/right (x 2504,
        --     z 3062-3063; the relic's guard teleports you in once, then the
        --     gate walks you through: ogre_guard.rs2:187-209), the battlement
        --     ganothbattlement 2507,3012 to the bridge, tanothjump1 over the
        --     gap and tanothjump2 back (the city guard's pocket has no other
        --     exit: wiki "Return back across the bridges, over the
        --     battlement, and through the north-west gate").
        --   The mad skavid's pocket (82 tiles) behind the SE gate
        --     ogreguardgate1/right (2549-2550,3028).
        -- ===============================================================
        local function tile_text(r, tt)
            if r == "ok" and type(tt) == "table" then
                return tt.x .. "," .. tt.z .. "," .. tt.level
            end
            return tostring(r)
        end
        local function await_tile(pred, ticks, what)
            return t.await({
                level = function()
                    local r, tt = t.world.tile()
                    return r == "ok" and pred(tt)
                end,
                note = what .. ": waiting for the landing",
            }, ticks)
        end
        local function near(tt, x, z, slack)
            return type(tt) == "table" and math.abs(tt.x - x) <= slack and math.abs(tt.z - z) <= slack
        end

        -- Hitpoints are sampled after every fight round, shaman and walk in
        -- the enclave; below EAT_BELOW a shark is eaten. A margin row reads
        -- the lowest sample since margin_begin().
        local EAT_BELOW = 35
        local hp_low, hp_eaten = nil, 0
        local function vitals()
            local hr, hp = t.skill.read("hitpoints")
            if hr == "ok" and type(hp) == "table" and hp.level then
                if hp_low == nil or hp.level < hp_low then
                    hp_low = hp.level
                end
                if hp.level < EAT_BELOW then
                    local er = t.player.inv_op("shark", 1) -- shark's own ifop1=Eat
                    if er == "ok" then
                        hp_eaten = hp_eaten + 1
                    end
                    t.ticks(3)
                end
            end
        end
        local function margin_begin()
            hp_low = nil
            vitals()
        end
        local function margin_row(name, fight)
            vitals()
            local fr, food = t.inv.count("shark")
            t.check(name, hp_low ~= nil and hp_low >= 15 and fr == "ok" and food ~= nil and food >= 1,
                fight .. ": lowest hp " .. tostring(hp_low) .. "/60 (sampled), sharks staged 10, eaten so far "
                    .. hp_eaten .. ", left " .. tostring(food) .. " (" .. tostring(fr)
                    .. ") (margin: lowest hp >= 15, a quarter of 60, AND food left)")
        end

        -- The Watchtower's ladders. No maplink row for any of them
        -- (ladders_stairs/configs/maplink.dbrow has none at 39_48_48_39 /
        -- 53_39): watchladderup/down p_teleport to 2549,3112 on the next
        -- floor (quest_itwatchtower.rs2:291-302); qip_watchtower_ladder_top
        -- and towerladder climb one plane on the tile stood on.
        local function climb_to_wizard(name)
            t.exec(name, t.player.climb, { loc = "watchladderup", op = 1, op_name = "Climb-up",
                at = { 2549, 3111, 1 }, src = { 2549, 3112 }, dest = { 2549, 3112, 2 } })
        end
        local function climb_down_from_wizard(name)
            t.exec(name, t.player.climb, { loc = "watchladderdown", op = 1, op_name = "Climb-down",
                at = { 2549, 3111, 2 }, src = { 2549, 3112 }, dest = { 2549, 3112, 1 } })
        end
        -- The first floor's Climb-down is qip_watchtower_ladder_top (17122,
        -- maps/m39_48.jl2 `1 48 39: 17122 10`), NOT towerladder -- that is
        -- the ground floor's Climb-up on the same square, under the floor.
        local function climb_down_to_ground(name)
            t.exec(name, t.player.climb, { loc = "qip_watchtower_ladder_top", op = 1, op_name = "Climb-down",
                at = { 2544, 3111, 1 }, src = { 2544, 3112 }, dest = { 2544, 3112, 0 } })
        end
        -- towerladder ([oploc1,towerladder] quest_itwatchtower.rs2:101-109)
        -- opens the tower guard's page ("It is the wizards' helping hand -
        -- let 'em up.") BEFORE ~climb_ladder(1), and the page holds the climb
        -- until it is continued. t.player.climb has no `chat` (cross_gate
        -- does), so this one climb is pressed, its page played and graded
        -- here on the level and the landing tile, never on the press.
        local function climb_tower_ladder(name)
            t.exec("walk-" .. name, t.player.walk_to, 2544, 3112)
            local br, before = t.world.tile()
            local pr, pd = t.player.click_loc("towerladder", 1, { at = { 2544, 3111, 0 } })
            local cr, cd = t.chat.play({ "npc:It is the wizards' helping hand" })
            await_tile(function(tt) return tt.level == 1 end, 10, name)
            local ar, after = t.world.tile()
            t.check(name, br == "ok" and before.level == 0 and ar == "ok" and after.level == 1
                    and near(after, 2544, 3112, 1),
                "towerladder 2544,3111,0 from " .. tile_text(br, before) .. ": click_loc -> " .. tostring(pr) .. " "
                    .. tostring(pd) .. "; tower guard page -> " .. tostring(cr) .. " " .. tostring(cd)
                    .. "; landed " .. tile_text(ar, after) .. " (want level 1 within 1 of 2544,3112)")
        end
        local function down_from_wizard_to_ground(name1, name2)
            climb_down_from_wizard(name1)
            t.exec("walk-" .. name2, t.player.walk_to, 2544, 3112)
            climb_down_to_ground(name2)
        end
        local function up_to_wizard(name1, name2)
            climb_tower_ladder(name1)
            t.exec("walk-" .. name2, t.player.walk_to, 2549, 3112)
            climb_to_wizard(name2)
        end

        -- Grew's island: in by the branch (Use rope), graded on the tiles
        -- and the rope; out by the north rope swing.
        local function swing_onto_island(name)
            t.exec("goto-" .. name, t.player.goto_tile, 2501, 3088, 0)
            t.exec("walk-" .. name, t.player.walk_to, 2501, 3087)
            local br, before = t.world.tile()
            local _, ropes_before = t.inv.count("rope")
            local branch = t.player.by_symbol("loc", "tree_ropeswing4_norope")
            local ur, ud = t.player.use_on("rope", branch)
            await_tile(function(tt) return tt.x == 2505 and tt.z == 3087 end, 12, name)
            local ar, after = t.world.tile()
            local _, ropes_after = t.inv.count("rope")
            t.check(name, br == "ok" and before.x <= 2501 and ar == "ok" and after.x == 2505 and after.z == 3087
                    and after.level == 0 and ropes_before ~= nil and ropes_after == ropes_before - 1,
                "rope on tree_ropeswing4_norope 2499,3087 from " .. tile_text(br, before) .. ": use_on -> "
                    .. tostring(ur) .. " " .. tostring(ud) .. "; landed " .. tile_text(ar, after)
                    .. " (want 2505,3087,0 on the island: loc+6, quest_itwatchtower.rs2:328); rope "
                    .. tostring(ropes_before) .. " -> " .. tostring(ropes_after))
        end
        local function swing_off_island(name)
            t.exec("walk-" .. name, t.player.walk_to, 2511, 3089)
            t.exec(name, t.player.cross_trap, { loc = "tree_ropeswing3", op = 1, op_name = "Swing-on",
                at = { 2511, 3090, 0 }, src = { 2511, 3089 }, dest = { 2511, 3096 }, attempts = 2 })
        end

        -- Gu'Tanoth's north-west gate, once the relic is given: a WALK-THROUGH
        -- double gate since seam b63-seam1 (ogre_guard.rs2:187-209
        -- open_gutanoth_gate -> ~itwatchtower_gate_walk, LostCity
        -- quest_itwatchtower.rs2:271-281 ~open_and_close_double_door2). The
        -- leaf's wall is on the west edge of 2504: from outside the approach
        -- stands the player on 2504 (the door row, "entering") and the press
        -- carries them one past it (2503); from inside (2503, across the edge)
        -- it carries them onto the leaf tile 2504 (outside). Both leaves are
        -- back 3 ticks later, so every crossing is a press.
        local function nw_gate_in(name)
            t.exec(name, t.player.cross_gate, { loc = "ogreguardgate2right", at = { 2504, 3063, 0 },
                near = { 2505, 3063 }, far_ok = function(tile) return tile.x <= 2503 end,
                far_desc = "inside Gu'Tanoth, x <= 2503 (entering: leaf 2504 then one past it)" })
        end
        local function nw_gate_out(name)
            t.exec(name, t.player.cross_gate, { loc = "ogreguardgate2right", at = { 2504, 3063, 0 },
                near = { 2503, 3063 }, far_ok = function(tile) return tile.x >= 2504 end,
                far_desc = "outside Gu'Tanoth, x >= 2504 (carried onto the leaf tile)", far = { 2505, 3063 } })
        end

        -- A skavid cave: Enter on the surface (frame 0) lands in the cave
        -- (frame 1) with the map and a lit torch held; Leave lands beside the
        -- entrance (quest_itwatchtower.rs2:142-209).
        local function cave_in(name, loc, at, src, dest)
            t.exec(name, t.player.climb, { loc = loc, op = 1, op_name = "Enter",
                at = { at[1], at[2], 0 }, src = src, dest = { dest[1], dest[2], 0 } })
            t.ticks(3) -- settle the scene after the underground teleport (trap 21)
        end
        local function cave_out(name, loc, at, dest)
            t.exec(name, t.player.climb, { loc = loc, op = 1, op_name = "Leave",
                at = { at[1], at[2], 0 }, dest = { dest[1], dest[2], 0 } })
        end

        -- ================= Starting off: reach the Watchtower Wizard =================
        -- From the fixture (Lumbridge) no walk reaches Kandarin except through a
        -- members' gate, so the run starts with what a player uses: Camelot
        -- Teleport cast from the spellbook (three graded rows: cast, runes,
        -- landing). Camelot to the open ground north of the tower is one
        -- Kandarin overland walk with every door shut (reach.py REACH
        -- closed-doors len 567 at margins 30/80/160).
        t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = "goUpTrellis.camelotTeleport",
            runes = { { "airrune", 5 }, { "lawrune", 1 } }, where = "Camelot" })
        t.exec("goto-goUpTrellis", t.player.goto_tile, 2548, 3120, 0)
        -- Agility-18 climb up the tower's north wall (loc_2299 wall climb,
        -- ported seam20; quest_itwatchtower.rs2:121-128 ~agility_climb_up to
        -- 1_39_48_52_45 = 2548,3117,1). agility=25 clears the level 18 gate.
        t.exec("goUpTrellis", t.player.climb, { loc = "qip_watchtower_trellis_base", op = 1, op_name = "Climb-up",
            at = { 2548, 3119, 0 }, src = { 2548, 3120 }, dest = { 2548, 3117, 1 }, ticks = 15 })
        t.exec("walk-goUpLadderToWizard", t.player.walk_to, 2549, 3112)
        climb_to_wizard("goUpLadderToWizard")

        t.exec("talkToWizard", t.player.talk_to, "watchtower_wizard", 1)
        t.exec("talkToWizard-dialog", t.chat.play, {
            "npc:Oh my, oh my",
            "choose:What's the matter?",
            "player:What's the matter",
            "npc:Oh dear, oh dear",
            "npc:We try hard to keep this town",
            "npc:But how can we do that",
            "player:What do you mean it isn't work",
            "npc:The Watchtower here works",
            "npc:The exact knowledge of the spe",
            "npc:Feldips.",
            "choose:So how come the spell doesn't work?",
            "player:So how come the spell doesn't",
            "npc:The crystals! The crystals hav",
            "player:Taken?",
            "npc:Stolen!",
            "player:Stolen?",
            "npc:Yes, yes! Do I have to repeat",
            "choose:Can I be of help?",
            "player:Can I be of help?",
            "npc:Help? Oh wonderful, dear trave",
            "npc:Yes I could do with an extra p",
            "player:???",
            "npc:There must be some evidence of",
            "player:I would be happy to.",
            "npc:Try searching the surrounding",
        })
        t.check("quest.stage.started", t.quest.expect_stage("itwatchtower_started"))

        -- ================= Search the bushes for the fingernails =================
        down_from_wizard_to_ground("goDownFromWizard", "goDownFromFirstFloor")
        -- The tower's ground floor is open to the north (no door loc).
        t.exec("walk-searchBush", t.player.walk_to, 2544, 3133)
        t.exec("searchBush", t.player.click_loc, "watchtowerbushnail", 1)
        t.exec("inv.fingernails", t.inv.await, "fingernails", 1, 5)

        -- ================= Return to the wizard with the fingernails =================
        up_to_wizard("goBackUpToFirstFloor", "goBackUpToWizard")
        t.exec("talkToWizardAgain", t.player.talk_to, "watchtower_wizard", 1)
        -- watchtower_wizard.rs2:119-123 (%itwatchtower=started, fingernails
        -- held) jumps straight into @watchwiz_give_fingernails (:242-268),
        -- never the "No, sorry, nothing yet" branch the scaffold guessed.
        t.exec("talkToWizardAgain-dialog", t.chat.play, {
            "npc:Hello again",
            "player:Have a look at these",
            "npc:Interesting, very interesting",
            "npc:Long nails",
            "npc:Of course! They belong to a skavid",
            "player:A skavid?",
            "npc:A servant race to the ogres",
            "npc:They inhabit the caves",
            "npc:They normally keep to themselves",
            "choose:What do you suggest I do?",
            "player:What do you suggest I do",
            "npc:It's no good searching the caves",
            "player:Why not?",
            "npc:They are deep and complex",
            "npc:It may be that the ogres have one",
            "player:And how do you know that?",
            "npc:Well... I don't",
            "choose:So what do I do?",
            "player:So what do I do?",
            "npc:You need to be fearless",
            "player:That sounds scary",
            "npc:Ogres are nasty creatures",
            "player:What do I need to do to get into",
            "npc:Well, the guards need to be dealt",
            "npc:Tribal ogres often dislike",
        })
        t.check("quest.stage.given_fingernails", t.quest.expect_stage("itwatchtower_given_fingernails"))

        -- ================= Talk to Og (first) -- get Toban's key =================
        down_from_wizard_to_ground("toOg.downFromWizard", "toOg.downFromFirstFloor")
        t.exec("goto-talkToOg", t.player.goto_tile, 2506, 3116, 0)
        t.exec("talkToOg", t.player.talk_to, "og", 1)
        t.exec("talkToOg-dialog", t.chat.play, {
            "npc:Why you here little rat?",
            "choose:I seek entrance to the city of ogres.",
            "player:I seek entrance to the city of",
            "npc:You got no business there!",
            "npc:Just a minute",
            "player:What can I do to help an ogre?",
            "npc:South-east of here der is more",
            "npc:Here is a key to the chest",
        })
        t.exec("inv.toban_key", t.inv.await, "toban_key", 1, 5)

        -- ================= Swing to Grew's island, talk to Grew (first) =================
        swing_onto_island("useRopeOnBranch")
        -- Grew stands at the island's east end; from the landing (2505,3087)
        -- he is off the viewport (run 1: "pose 2 ... none of 27 pixels").
        t.exec("walk-talkToGrew", t.player.walk_to, 2509, 3087)
        t.exec("talkToGrew", t.player.talk_to, "grew", 1)
        t.exec("talkToGrew-dialog", t.chat.play, {
            "npc:What do you want, little morsel",
            "player:I want to enter the city of ogr",
            "npc:Hah! I should eat you instead!",
            "choose:Don't eat me; I can help you.",
            "player:Don't eat me; I can help you.",
            "npc:What can a morsel like you do",
            "player:I am a mighty adventurer",
            "npc:Well, well, perhaps the morsel",
            "npc:If you t'ink you're tough",
        })

        -- ================= Leave Grew's island, enter the hole south of Gu'Tanoth =================
        swing_off_island("leaveGrewIsland")

        -- Overland round the west of Gu'Tanoth to the hole (all.loc.compack
        -- 2811=tobancave at 2499,2989); stand on 2500,2988, the open tile the
        -- island's ladder lands on, and Enter. [oploc1,tobancave]
        -- (quest_itwatchtower.rs2:259-262) p_teleports to 0_40_47_16_21 on
        -- the same level and map frame, then a player page.
        t.exec("goto-enterHoleSouthOfGuTanoth", t.player.goto_tile, 2500, 2988, 0)
        t.exec("enterHoleSouthOfGuTanoth", t.player.climb, { loc = "tobancave", op = 1, op_name = "Enter",
            at = { 2499, 2989, 0 }, src = { 2500, 2988 }, dest = { 2576, 3029, 0 },
            same_level = "quest_itwatchtower.rs2 [oploc1,tobancave] p_teleport(0_40_47_16_21)" })
        t.exec("enterHoleSouthOfGuTanoth-dialog", t.chat.play, { "player:Wow! That tunnel went a long way." })

        -- ================= Kill Gorad, pick up his tooth =================
        -- Run 2/3 (sonnet-b30): the press was refused "I'm already under
        -- attack." while another ogre held the claim (single-way combat);
        -- retry the press with a wait between attempts. ::passive ogre/ogre2
        -- in setup keeps the wanderers off.
        t.exec("walk-killGorad", t.player.walk_to, 2578, 3024)
        margin_begin()
        local gorad_pressed = false
        for attempt = 1, 8 do
            if not gorad_pressed then
                local atk_result, atk_detail = t.player.attack("gorad", 2, 30)
                if atk_result == "ok" then
                    t.check("killGorad", atk_result == "ok", atk_detail)
                    gorad_pressed = true
                elseif attempt == 8 then
                    t.step("killGorad", "FAIL", atk_detail)
                else
                    t.note("killGorad attempt " .. attempt .. ": " .. tostring(atk_detail))
                    t.ticks(10)
                end
            end
        end
        local gd_r, gd_d = t.npc.await_dead_engaged(250, 12, { eat = { item = "shark", below = 30 } })
        t.step("killGorad.dead", gd_r == "ok" and "PASS" or "FAIL", gd_d)
        local gorad_low = tonumber(string.match(tostring(gd_d), "lowest hp (%d+)/"))
        if gorad_low ~= nil and (hp_low == nil or gorad_low < hp_low) then
            hp_low = gorad_low
        end
        margin_row("killGorad.margin", "Gorad (level 68), rune scimitar, att 60 str 70 def 60")
        -- [queue,defeat_gorad] auto-grants ogretooth on death, given a free slot.
        t.exec("inv.goradstooth", t.inv.await, "ogretooth", 1, 10)

        -- ================= Talk to Toban (first) =================
        t.exec("talkToToban", t.player.talk_to, "toban", 1)
        t.exec("talkToToban-dialog", t.chat.play, {
            "npc:What do you want, small thing",
            "choose:I seek entrance to the city of ogres.",
            "player:I seek entrance to the city of",
            "npc:Hahaha! You'll never get in there",
            "player:I'll find a way, trust me.",
            "npc:Bold words for a t'ing so small",
            "choose:I could do something for you...",
            "player:I could do something for you",
            "npc:Hahaha! This creature t'inks",
            "npc:Prove to me your might",
        })

        -- ================= Give Toban the dragon bones =================
        -- toban.rs2:49-58 (@spoken_toban) opens with a chatnpc page BEFORE
        -- the inv_del/inv_add/setbit -- use_on's settle only proves that
        -- FIRST page opened (trap 22), so the reward needs the rest of the
        -- chain clicked through before it lands.
        local toban_t = t.player.by_symbol("npc", "toban")
        t.exec("giveTobanDragonBones", t.player.use_on, "dragon_bones", toban_t)
        t.exec("giveTobanDragonBones-drain", t.chat.drain, {stop_at = "none", max_pages = 10})
        t.exec("inv.relicpart3", t.inv.await, "relicpart3", 1, 5)
        t.exec("inv.dragon_bones_given", t.inv.expect_absent, "dragon_bones")

        -- ================= Open Toban's chest for Og's stolen gold =================
        t.exec("walk-searchChestForTobansGold", t.player.walk_to, 2575, 3032)
        t.exec("searchChestForTobansGold", t.player.click_loc, "tobanchest", 1)
        t.exec("inv.stolen_gold", t.inv.await, "stolen_gold", 1, 5)
        t.chat.drain({stop_at = "none", max_pages = 3})

        -- ================= Return the gold to Og =================
        -- Off Toban's island by its ladder ([oploc1,tobanladderdown]
        -- quest_itwatchtower.rs2:264-267: p_teleport 0_39_46_4_44, beside
        -- the hole). Wiki: "Leave the island via the ladder."
        t.exec("leaveTobanIsland", t.player.climb, { loc = "tobanladderdown", op = 1, op_name = "Climb-down",
            at = { 2575, 3029, 0 }, dest = { 2500, 2988, 0 },
            same_level = "quest_itwatchtower.rs2 [oploc1,tobanladderdown] p_teleport(0_39_46_4_44)" })
        t.exec("goto-talkToOgAgain", t.player.goto_tile, 2506, 3116, 0)
        local og_t = t.player.by_symbol("npc", "og")
        t.exec("talkToOgAgain", t.player.use_on, "stolen_gold", og_t)
        t.exec("talkToOgAgain-drain", t.chat.drain, {stop_at = "none", max_pages = 10})
        t.exec("inv.relicpart1", t.inv.await, "relicpart1", 1, 5)
        t.exec("inv.stolen_gold_given", t.inv.expect_absent, "stolen_gold")

        -- ================= Return the tooth to Grew =================
        swing_onto_island("useRopeOnBranchAgain")
        t.exec("walk-talkToGrewAgain", t.player.walk_to, 2509, 3087)
        local grew_t = t.player.by_symbol("npc", "grew")
        t.exec("talkToGrewAgain", t.player.use_on, "ogretooth", grew_t)
        t.exec("talkToGrewAgain-drain", t.chat.drain, {stop_at = "none", max_pages = 10})
        t.exec("inv.relicpart2", t.inv.await, "relicpart2", 1, 5)
        t.exec("inv.powering_crystal1", t.inv.await, "powering_crystal1", 1, 5)
        t.exec("inv.ogretooth_given", t.inv.expect_absent, "ogretooth")

        -- ================= Bring the assembled relic to the wizard =================
        swing_off_island("leaveGrewIsland2")
        t.exec("goto-bringRelicUpToFirstFloor", t.player.goto_tile, 2544, 3113, 0)
        up_to_wizard("bringRelicUpToFirstFloor", "bringRelicUpToWizard")

        -- Each relicpartN use_on assembles the statue; the third call fires
        -- @check_relic_parts (watchtower_wizard.rs2:201-211) which auto-
        -- advances the stage once all three bits are set -- no separate
        -- talk_to needed.
        local wizard_t = t.player.by_symbol("npc", "watchtower_wizard")
        t.exec("talkToWizardWithRelic-part1", t.player.use_on, "relicpart1", wizard_t)
        t.exec("talkToWizardWithRelic-part1-drain", t.chat.drain, {stop_at = "none", max_pages = 10})
        t.exec("talkToWizardWithRelic-part2", t.player.use_on, "relicpart2", wizard_t)
        t.exec("talkToWizardWithRelic-part2-drain", t.chat.drain, {stop_at = "none", max_pages = 10})
        t.exec("talkToWizardWithRelic-part3", t.player.use_on, "relicpart3", wizard_t)
        t.exec("talkToWizardWithRelic-part3-drain", t.chat.drain, {stop_at = "none", max_pages = 10})
        t.exec("inv.ogrerelic", t.inv.await, "ogrerelic", 1, 5)
        t.check("quest.stage.made_relic", t.quest.expect_stage("itwatchtower_made_relic"))

        -- ================= Enter Gu'Tanoth =================
        down_from_wizard_to_ground("toGuTanoth.downFromWizard", "toGuTanoth.downFromFirstFloor")
        -- Outside the north-west gate (the gate is x 2504; outside is x >= 2505).
        t.exec("goto-enterGuTanoth", t.player.goto_tile, 2506, 3063, 0)
        local guard2_t = t.player.by_symbol("npc", "ogre_guard2")
        t.exec("enterGuTanoth", t.player.use_on, "ogrerelic", guard2_t)
        -- @ogre_guard_friendship_proof (ogre_guard.rs2:65-77): the pages,
        -- then p_teleport(0_39_47_7_54) = 2503,3062, inside the gate.
        t.exec("enterGuTanoth-drain", t.chat.drain, {stop_at = "none", max_pages = 10})
        await_tile(function(tt) return tt.x <= 2503 and tt.z <= 3063 end, 10, "enterGuTanoth")
        local eg_r, eg_tile = t.world.tile()
        t.check("enterGuTanoth.landed", eg_r == "ok" and eg_tile.x == 2503 and eg_tile.z == 3062 and eg_tile.level == 0,
            "the guard let the player in: at " .. tile_text(eg_r, eg_tile) .. " (want 2503,3062,0, ogre_guard.rs2:74)")
        t.check("quest.stage.given_relic", t.quest.expect_stage("itwatchtower_given_relic"))

        -- ================= Market: rock cake for the battlement guard =================
        -- ogre_trader2 wanders beside this counter (m39_47.spawn:18: 2513,3034)
        -- and the steal is refused -- "Grr! Get your hands off those cakes!",
        -- and he attacks -- while he is within 3 tiles of the thief by line of
        -- walk (yanille_shop_stubs.rs2 [oploc2,rockcounter_withcakes], LC
        -- ogre_trader.rs2:26-30). A player steals from the side of the counter
        -- the trader is not on, so the counter blocks his line: try the north
        -- side, then the south; after a refusal step away until he loses
        -- interest and wanders. The counter's only op is op2 Steal-From
        -- (all.loc:24228). Thieving 15 comes from setup.
        local steal_sides = {{2513, 3037}, {2514, 3035}}
        local stolen = false
        for attempt = 1, 8 do
            if not stolen then
                local side = steal_sides[(attempt - 1) % 2 + 1]
                t.exec("goto-stealRockCake-" .. attempt, t.player.goto_tile, side[1], side[2], 0)
                local _, cakes_before = t.inv.count("rockcake")
                local r, d = t.player.click_loc("rockcounter_withcakes", 2)
                local _, page = t.chat.text()
                t.chat.drain({stop_at = "none", max_pages = 3})
                t.ticks(2)
                local _, cakes = t.inv.count("rockcake")
                if cakes and cakes > 0 then
                    stolen = true
                    t.check("stealRockCake", cakes > 0, "attempt " .. attempt .. " from " .. side[1] .. "," .. side[2]
                        .. ": click_loc -> " .. tostring(r) .. " " .. tostring(d) .. "; rockcake "
                        .. tostring(cakes_before) .. " -> " .. tostring(cakes))
                else
                    t.note("stealRockCake attempt " .. attempt .. ": " .. tostring(r) .. " " .. tostring(d)
                        .. " / page: " .. tostring(page))
                    if attempt == 8 then
                        t.step("stealRockCake", "FAIL", "no rock cake after 8 attempts: " .. tostring(d))
                    end
                    t.exec("goto-stealRockCake-away-" .. attempt, t.player.goto_tile, 2514, 3050, 0)
                    t.ticks(15)
                end
            end
        end
        t.exec("inv.rockcake", t.inv.await, "rockcake", 1, 5)

        t.exec("goto-talkToGuardBattlement", t.player.goto_tile, 2504, 3012, 0)
        t.exec("talkToGuardBattlement", t.player.talk_to, "ogre_guard3", 1)
        t.exec("talkToGuardBattlement-dialog", t.chat.play, {
            "npc:Oi! Where do you think you are",
            "choose:But I am a friend to ogres...",
            "player:But I am a friend to ogres",
            "npc:Prove it to us with a gift",
            "player:Like what?",
            "npc:Surprise us",
        })

        local guard3_t = t.player.by_symbol("npc", "ogre_guard3")
        t.exec("talkToGuardWithRockCake", t.player.use_on, "rockcake", guard3_t)
        -- ogre_guard.rs2:114-118 (battlements_rockcake) deletes rockcake and
        -- sets the market bits BEFORE its chat lines.
        t.exec("talkToGuardWithRockCake-drain", t.chat.drain, {stop_at = "none", max_pages = 5})
        t.exec("inv.rockcake_spent", t.inv.expect_absent, "rockcake")

        -- ================= Over the battlement, jump the broken bridge =================
        -- [oploc1,ganothbattlement] (quest_itwatchtower.rs2:344-357): with the
        -- market bits at 2 the player climbs from loc-1 to loc+1 on x.
        t.exec("walk-climbBattlement", t.player.walk_to, 2506, 3012)
        t.exec("climbBattlement", t.player.cross_trap, { loc = "ganothbattlement", op = 1, op_name = "Climb-over",
            at = { 2507, 3012, 0 }, src = { 2506, 3012 }, dest = { 2508, 3012 }, attempts = 2 })
        -- tanothjump1 is maps/m39_47.jl2 `1 34 18: 2830 10`: raw level 1 on
        -- the bridge deck the player walks on plane 0. [oploc1,tanothjump1]
        -- (quest_itwatchtower.rs2:418-444): ogre_guard4 (within 8) asks the
        -- toll first, then the jump p_teleports to 0_39_47_34_21 = 2530,3029
        -- and the player's "Phew!" page follows.
        t.exec("walk-jumpGap", t.player.walk_to, 2530, 3024)
        t.exec("jumpGap", t.player.cross_gate, { loc = "tanothjump1", op = 1, at = { 2530, 3026, 0 }, loc_level = 1,
            near = { 2530, 3025 },
            far_ok = function(tile) return tile.z >= 3029 and tile.x >= 2527 and tile.x <= 2545 end,
            far_desc = "over the gap on the city guard's side (2530,3029)",
            chat = {
                "npc:Oi! Little thing",
                "player:20 gold pieces to jump",
                "npc:That's what I said",
                "choose:Okay, I'll pay it.",
                "player:Okay, I'll pay it.",
                "npc:A wise choice",
                "player:Phew! I just made it.",
            } })
        local _, coins_after_jump = t.inv.count("coins")
        t.check("jumpGap.toll_paid", coins_after_jump == 30, "coins " .. tostring(coins_after_jump) .. " (50 - 20 toll)")

        -- ================= City guard's riddle =================
        t.exec("walk-talkToCityGuard", t.player.walk_to, 2542, 3031)
        t.exec("talkToCityGuard", t.player.talk_to, "city_guard", 1)
        t.exec("talkToCityGuard-dialog", t.chat.play, {
            "npc:Grrrr, what business you got",
            "player:I am on an errand.",
            "npc:So what you want with me?",
            "choose:I seek passage into the skavid caves.",
            "player:I seek passage into the skavid",
            "npc:Is that so",
            "npc:I want you to bring me an item",
            "npc:My first is in days",
            "npc:My fifth is in heaven",
            "npc:My eighth is in nine",
            "npc:My whole is an object",
        })
        t.check("quest.stage.given_riddle", t.quest.expect_stage("itwatchtower_given_riddle"))

        local guard_t = t.player.by_symbol("npc", "city_guard")
        t.exec("talkToCityGuardAgain", t.player.use_on, "deathrune", guard_t)
        t.exec("talkToCityGuardAgain-drain", t.chat.drain, {stop_at = "none", max_pages = 5})
        t.exec("inv.skavidmap", t.inv.await, "skavidmap", 1, 5)
        t.exec("inv.deathrune_given", t.inv.expect_absent, "deathrune")
        t.check("quest.stage.solved_riddle", t.quest.expect_stage("itwatchtower_solved_riddle"))

        -- ================= Out of the city to the skavid caves =================
        -- The guard's pocket has no way out but the gap: back over
        -- tanothjump2 ([oploc1,tanothjump2] :446-448, p_teleport 2531,3026,
        -- then a player page), back over the battlement, out of the
        -- north-west gate, east then south to the caves (wiki quick guide).
        t.exec("walk-jumpGapBack", t.player.walk_to, 2530, 3029)
        t.exec("jumpGapBack", t.player.cross_trap, { loc = "tanothjump2", op = 1, op_name = "Jump-Over",
            at = { 2531, 3029, 0 }, src = { 2530, 3029 }, dest = { 2531, 3026 }, attempts = 1 })
        t.exec("jumpGapBack-dialog", t.chat.play, { "player:I'm glad that was easier" })
        t.exec("walk-climbBattlementBack", t.player.walk_to, 2508, 3012, 60)
        t.exec("climbBattlementBack", t.player.cross_trap, { loc = "ganothbattlement", op = 1, op_name = "Climb-over",
            at = { 2507, 3012, 0 }, src = { 2508, 3012 }, dest = { 2506, 3012 }, attempts = 2 })
        t.exec("goto-leaveGuTanoth", t.player.goto_tile, 2502, 3062, 0)
        t.exec("walk-leaveGuTanoth", t.player.walk_to, 2503, 3063)
        nw_gate_out("leaveGuTanoth.northWestGate")

        -- ================= Skavid caves: the scared skavid =================
        t.exec("goto-enterScaredSkavidCave", t.player.goto_tile, 2552, 3034, 0)
        cave_in("enterScaredSkavidCave", "skavid_cave5", { 2553, 3033 }, { 2552, 3034 }, { 2504, 9441 })
        t.exec("talkToScaredSkavid", t.player.talk_to, "scared_skavid", 1)
        t.exec("talkToScaredSkavid-dialog", t.chat.play, {
            "npc:Tanath cur, tanath cur",
            "player:???",
            "npc:Don't hurt me, don't hurt me",
            "player:Stop moaning, creature",
            "npc:Please don't touch me",
            "player:You have something that belong",
            "npc:I don't have anything",
            "player:Somehow, I find your words",
            "npc:I'm begging your kindness",
            "choose:Okay, okay, I'm not going to hurt you.",
            "player:Okay, okay, I'm not going to h",
            "npc:Thank you, kind one",
            "npc:I'll tells you where",
            "npc:You will have to learn skavid",
            "npc:Let me tells you the most comm",
            "npc:Ar, nod, gor, ig, cur",
            "npc:Those will gets you started",
        })
        cave_out("leaveScaredSkavidRoom", "cave5exit", { 2504, 9442 }, { 2552, 3034 })

        -- ================= Room 1 (skavid_cave4): skavidtalker3, "Cur." =================
        t.exec("goto-enterSkavid1Cave", t.player.goto_tile, 2553, 3054, 0)
        cave_in("enterSkavid1Cave", "skavid_cave4", { 2552, 3052 }, { 2553, 3054 }, { 2498, 9451 })
        t.exec("talkToSkavid1", t.player.talk_to, "skavidtalker3", 1)
        t.exec("talkToSkavid1-dialog", t.chat.play, {
            "npc:Bidith tanath",
            "choose:Cur.",
            "player:Cur.",
            "npc:Cur",
        })
        cave_out("leaveSkavid1", "cave4exit", { 2497, 9451 }, { 2553, 3054 })

        -- ================= Room 2 (skavid_cave3): skavidtalker2, "Ar." =================
        t.exec("goto-enterSkavid2Cave", t.player.goto_tile, 2540, 3054, 0)
        cave_in("enterSkavid2Cave", "skavid_cave3", { 2539, 3052 }, { 2540, 3054 }, { 2518, 9455 })
        t.exec("talkToSkavid2", t.player.talk_to, "skavidtalker2", 1)
        t.exec("talkToSkavid2-dialog", t.chat.play, {
            "npc:Gor cur",
            "choose:Ar.",
            "player:Ar.",
            "npc:Ar",
        })
        cave_out("leaveSkavid2", "cave3exit", { 2518, 9456 }, { 2540, 3054 })

        -- ================= Room 3 (skavid_cave2): skavidtalker1, "Ig." + nightshade #1 =================
        t.exec("goto-enterSkavid3Cave", t.player.goto_tile, 2524, 3070, 0)
        cave_in("enterSkavid3Cave", "skavid_cave2", { 2522, 3068 }, { 2524, 3070 }, { 2532, 9469 })
        t.exec("talkToSkavid3", t.player.talk_to, "skavidtalker1", 1)
        t.exec("talkToSkavid3-dialog", t.chat.play, {
            "npc:Cur bidith",
            "choose:Ig.",
            "player:Ig.",
            "npc:Ig",
        })
        -- m39_147.spawn:49 -- a nightshade ground spawn sits in this room
        -- (2530,9462). Take is op 3 on a ground object (click_obj's
        -- default). Stand one square off the stack, on 2531,9462 (2530,9461,
        -- where the old goto put the player, is solid rock).
        t.exec("walk-nightshade1", t.player.walk_to, 2531, 9462)
        local ns1_r, ns1_d = t.player.click_obj("nightshade", 3)
        t.check("pickUp2Nightshade-1", ns1_r == "ok", "click_obj nightshade op3 -> " .. tostring(ns1_r) .. " " .. tostring(ns1_d))
        t.exec("inv.nightshade1", t.inv.await, "nightshade", 1, 5)
        cave_out("leaveSkavid3", "cave2exit", { 2532, 9470 }, { 2524, 3070 })

        -- ================= Room 4 (skavid_cave1): skavidtalker4, "Nod." =================
        t.exec("goto-enterSkavid4Cave", t.player.goto_tile, 2562, 3024, 0)
        cave_in("enterSkavid4Cave", "skavid_cave1", { 2560, 3022 }, { 2562, 3024 }, { 2498, 9418 })
        t.exec("talkToSkavid4", t.player.talk_to, "skavidtalker4", 1)
        t.exec("talkToSkavid4-dialog", t.chat.play, {
            "npc:Tanath gor",
            "choose:Nod.",
            "player:Nod.",
            "npc:Nod",
        })
        cave_out("leaveSkavid4", "cave1exit", { 2497, 9418 }, { 2562, 3024 })

        -- ================= The SE gate: ogre_guard1, gold bar toll =================
        -- open_gutanoth_gate (ogre_guard.rs2:187-202): while %gutanoth_gold <
        -- found_gold the gate only speaks, and only with ogre_guard1 within 6
        -- tiles of the player. The first press (state 0) sets looking_gold and
        -- the guard pushes the player out to ^gutanoth_hill (0_39_47_50_57 =
        -- 2546,3065). The second press (state 1, gold bar held) takes the bar
        -- and p_teleports through to 0_39_47_53_19 = 2549,3027.
        t.exec("goto-tryToGoThroughToInsaneSkavid", t.player.goto_tile, 2550, 3030, 0)
        t.exec("walk-tryToGoThroughToInsaneSkavid", t.player.walk_to, 2550, 3029)
        local pushed = false
        for attempt = 1, 5 do
            if not pushed then
                local br, before = t.world.tile()
                local r, d = t.player.click_loc("ogreguardgate1", 1, { at = { 2550, 3028, 0 } })
                local _, page = t.chat.text()
                if page ~= nil and string.find(tostring(page), "Halt", 1, true) then
                    local cr, cd = t.chat.play({
                        "npc:Halt! You cannot pass here.",
                        "player:I am a friend to ogres.",
                        "npc:You will be my friend only with gold.",
                        "npc:For now - begone!",
                    })
                    await_tile(function(tt) return tt.x == 2546 and tt.z == 3065 end, 8, "tryToGoThroughToInsaneSkavid")
                    local ar, after = t.world.tile()
                    t.check("tryToGoThroughToInsaneSkavid", ar == "ok" and after.x == 2546 and after.z == 3065
                            and after.level == 0,
                        "ogreguardgate1 2550,3028 from " .. tile_text(br, before) .. ": click_loc -> " .. tostring(r)
                            .. " " .. tostring(d) .. "; guard's pages -> " .. tostring(cr) .. " " .. tostring(cd)
                            .. "; pushed to " .. tile_text(ar, after) .. " (want ^gutanoth_hill 2546,3065,0)")
                    pushed = true
                else
                    t.note("tryToGoThroughToInsaneSkavid attempt " .. attempt .. ": click_loc -> " .. tostring(r)
                        .. " " .. tostring(d) .. ", no guard page (" .. tostring(page) .. ") -- ogre_guard1 not within 6")
                    t.chat.drain({stop_at = "none", max_pages = 3})
                    t.ticks(5)
                    if attempt == 5 then
                        t.step("tryToGoThroughToInsaneSkavid", "FAIL", "ogre_guard1 never spoke at the gate in 5 presses")
                    end
                end
            end
        end
        t.exec("walk-throughSEGate", t.player.walk_to, 2550, 3030, 60)
        t.exec("throughSEGate", t.player.cross_gate, { loc = "ogreguardgate1", op = 1, at = { 2550, 3028, 0 },
            near = { 2550, 3029 },
            far_ok = function(tile) return tile.z <= 3027 end,
            far_desc = "through the SE gate (2549,3027, ogre_guard.rs2:22)",
            chat = {
                "npc:Creature, did you bring me the gold?",
                "player:Here it is!",
                "npc:It's brought it! On your way.",
            } })
        t.exec("inv.gold_bar_spent", t.inv.expect_absent, "gold_bar")

        -- ================= The mad skavid's riddle-guess =================
        t.exec("walk-enterInsaneSkavidCave", t.player.walk_to, 2529, 3013, 60)
        cave_in("enterInsaneSkavidCave", "skavid_cave6", { 2527, 3011 }, { 2529, 3013 }, { 2522, 9411 })

        -- skavid.rs2:196-237 -- the mad skavid picks $mes_type = random(3)
        -- each visit and the correct word depends on it (0->"Gor.",
        -- 1->"Cur.", 2->"Bidith."); read the spoken line back and answer it,
        -- retrying (a wrong guess just reopens the same choice) until the
        -- crystal lands.
        local mad_answered = false
        for attempt = 1, 9 do
            if not mad_answered then
                t.exec("talkToInsaneSkavid-" .. attempt, t.player.talk_to, "mad_skavid", 1)
                local text_result, npc_line = t.chat.text()
                local answer = nil
                if text_result == "ok" and npc_line then
                    if string.find(npc_line, "Ar cur", 1, true) then
                        answer = "Gor."
                    elseif string.find(npc_line, "Bidith ig", 1, true) then
                        answer = "Cur."
                    elseif string.find(npc_line, "Cur tanath", 1, true) then
                        answer = "Bidith."
                    end
                end
                if answer then
                    t.exec("talkToInsaneSkavid-answer-" .. attempt, t.chat.play, {"npc:*", "choose:" .. answer})
                    -- mad_skavid_correct (skavid.rs2) opens a chatnpc page
                    -- THEN a ~mesbox BEFORE inv_add (trap 22): drain it.
                    t.exec("talkToInsaneSkavid-drain-" .. attempt, t.chat.drain, {stop_at = "none", max_pages = 5})
                else
                    t.check("talkToInsaneSkavid-close-" .. attempt, t.chat.close(), npc_line or "no page text read")
                end
                local _, crystal2_count = t.inv.count("powering_crystal2")
                if crystal2_count and crystal2_count > 0 then
                    mad_answered = true
                end
            end
        end
        t.exec("inv.powering_crystal2", t.inv.await, "powering_crystal2", 1, 10)
        t.check("quest.stage.skavid_crystal", t.quest.expect_stage("itwatchtower_skavid_crystal"))

        -- m39_147.spawn:48 -- the second nightshade ground spawn (2528,9415) is in this room.
        t.exec("walk-nightshade2", t.player.walk_to, 2528, 9414)
        local ns2_r, ns2_d = t.player.click_obj("nightshade", 3)
        t.check("pickUp2Nightshade-2", ns2_r == "ok", "click_obj nightshade op3 -> " .. tostring(ns2_r) .. " " .. tostring(ns2_d))
        t.exec("inv.nightshade2", t.inv.await, "nightshade", 2, 5)

        cave_out("leaveMadSkavid", "cave6exit", { 2521, 9411 }, { 2529, 3013 })

        -- ================= Infiltrate the enclave (nightshade #1) =================
        -- "Leave the way you came in ... Return to the Gu'Tanoth market"
        -- (wiki): out of the SE gate (open now the gold is paid), round to
        -- the north-west gate and in.
        t.exec("walk-leaveSouthPocket", t.player.walk_to, 2549, 3026, 60)
        -- A walk-through since b63-seam1 (ogre_guard.rs2 ~itwatchtower_gate_walk
        -- = LC open_and_close_double_door2): from the south the player is
        -- carried onto the leaf tile 2549,3028 (the north side) and the gate
        -- shuts 3 ticks later.
        t.exec("leaveSouthPocket.southEastGate", t.player.cross_gate, { loc = "ogreguardgate1right",
            at = { 2549, 3028, 0 }, near = { 2549, 3027 }, far_ok = function(tile) return tile.z >= 3028 end,
            far_desc = "out of the mad skavid's pocket, z >= 3028 (carried onto the leaf tile)", far = { 2549, 3029 } })
        t.exec("goto-returnToGuTanoth", t.player.goto_tile, 2506, 3063, 0)
        t.exec("walk-returnToGuTanoth", t.player.walk_to, 2505, 3063)
        nw_gate_in("returnToGuTanoth.northWestGate")
        t.exec("goto-useNightshadeOnGuard", t.player.goto_tile, 2507, 3037, 0)
        local guard_encl_t = t.player.by_symbol("npc", "enclave_guard")
        t.exec("useNightshadeOnGuard", t.player.use_on, "nightshade", guard_encl_t)
        -- enclave_guard.rs2 [opnpcu]: the chatnpc page comes first, then
        -- enter_skavid_cave waits p_delay(2) and p_teleport's into the
        -- enclave (0_40_147_28_2 = 2588,9410).
        t.exec("useNightshadeOnGuard-drain", t.chat.drain, {stop_at = "none", max_pages = 5})
        await_tile(function(tt) return tt.z > 9000 end, 8, "useNightshadeOnGuard")
        local _, enclave_tile = t.world.tile()
        t.check("enclave.entered", enclave_tile ~= nil and enclave_tile.x == 2588 and enclave_tile.z == 9410,
            "player at " .. tile_text("ok", enclave_tile) .. " (want 2588,9410,0, enclave_guard.rs2:47)")
        t.check("quest.stage.fed_nightshade", t.quest.expect_stage("itwatchtower_fed_nightshade"))

        -- ================= Back to the wizard: learn the potion recipe =================
        -- The exit cave is m40_147.jl2:4389 (2598,9468); it lands 2540,3054.
        t.exec("goto-leaveEnclave", t.player.goto_tile, 2598, 9467, 0)
        cave_out("leaveEnclave", "enclavecave", { 2598, 9468 }, { 2540, 3054 })
        t.exec("goto-goBackUpToFirstFloorAfterEnclave", t.player.goto_tile, 2544, 3113, 0)
        up_to_wizard("goBackUpToFirstFloorAfterEnclave", "goBackUpToWizardAfterEnclave")

        t.exec("talkToWizardAgainEnclave", t.player.talk_to, "watchtower_wizard", 1)
        -- watchtower_wizard.rs2's itwatchtower_fed_nightshade branch opens
        -- with ~chatplayer_anim, not ~chatnpc_anim.
        t.exec("talkToWizardAgainEnclave-dialog", t.chat.play, {
            "player:I have found the cave of ogre",
            "npc:That is because of their magic",
            "npc:Collect a guam leaf, add janger",
            "npc:Be very careful how you mix",
            "npc:I hope you've been brushing up",
        })
        t.check("quest.stage.learned_potion", t.quest.expect_stage("itwatchtower_learned_potion"))

        -- ================= Make the ogre potion =================
        -- brew_potion.rs2:207-214 [opheldu,jangerberries]; try the reverse
        -- arm/target order as well before giving up (runs 2/4/6/7 of
        -- sonnet-b30 read "Nothing interesting happens." one way round).
        local jj_r, jj_d = t.player.use_item_on_item("jangerberries", "guamvial")
        local _, jj_count = t.inv.count("guamjangervial")
        if not (jj_count and jj_count > 0) then
            t.note("useJangerberriesOnGuam (jangerberries on guamvial): " .. tostring(jj_d))
            jj_r, jj_d = t.player.use_item_on_item("guamvial", "jangerberries")
        end
        t.step("useJangerberriesOnGuam", jj_r == "ok" and "PASS" or "FAIL", jj_d)
        t.exec("inv.guamjangervial", t.inv.await, "guamjangervial", 1, 5)
        t.exec("inv.jangerberries_used", t.inv.expect_absent, "jangerberries")
        t.exec("grindBatBones", t.player.use_item_on_item, "pestle_and_mortar", "bat_bones")
        t.exec("inv.ground_bat_bones", t.inv.await, "ground_bat_bones", 1, 5)
        t.exec("inv.bat_bones_used", t.inv.expect_absent, "bat_bones")
        -- ogre_potion.rs2:35 declares [opheldu,guamjangervial]; give the
        -- reverse a chance too.
        local bp_r, bp_d = t.player.use_item_on_item("guamjangervial", "ground_bat_bones")
        local _, bp_count = t.inv.count("ogre_potion")
        if not (bp_count and bp_count > 0) then
            t.note("useBonesOnPotion (guamjangervial on ground_bat_bones): " .. tostring(bp_d))
            bp_r, bp_d = t.player.use_item_on_item("ground_bat_bones", "guamjangervial")
        end
        t.step("useBonesOnPotion", bp_r == "ok" and "PASS" or "FAIL", bp_d)
        t.exec("inv.ogre_potion", t.inv.await, "ogre_potion", 1, 5)
        t.exec("inv.ground_bat_bones_used", t.inv.expect_absent, "ground_bat_bones")

        -- ================= Return with the potion; the wizard enchants it =================
        -- The potion is made on the wizard's floor, so no ladder is needed
        -- (goUpToFirstFloorWithPotion / goUpToWizardWithPotion are travel).
        t.exec("talkToWizardWithPotion", t.player.talk_to, "watchtower_wizard", 1)
        -- make_magic_ogre_potion (watchtower_wizard.rs2:285-294): inv_del/
        -- inv_add/stage happen BEFORE any of these pages; an if_close then a
        -- bare mes() then a p_delay(3) before a REOPENED page.
        t.exec("talkToWizardWithPotion-dialog", t.chat.play, {
            "npc:Any more news",
            "player:I have made the potion",
            "npc:That's great news",
            "end",
        })
        t.exec("msg.wizard_mutters", t.msg.expect, "wizard mutters strange words")
        t.ticks(3)
        t.exec("talkToWizardWithPotion-enchant", t.chat.play, {"npc:Here it is - a dangerous"})
        t.exec("inv.magic_ogre_potion", t.inv.await, "magic_ogre_potion", 1, 5)
        t.check("quest.stage.made_potion", t.quest.expect_stage("itwatchtower_made_potion"))

        -- ================= Re-infiltrate the enclave (nightshade #2) =================
        down_from_wizard_to_ground("useNightshadeOnGuardAgain-down1", "useNightshadeOnGuardAgain-down2")
        t.exec("goto-useNightshadeOnGuardAgain-gate", t.player.goto_tile, 2506, 3063, 0)
        t.exec("walk-useNightshadeOnGuardAgain-gate", t.player.walk_to, 2505, 3063)
        nw_gate_in("useNightshadeOnGuardAgain.northWestGate")
        t.exec("goto-useNightshadeOnGuardAgain", t.player.goto_tile, 2507, 3037, 0)
        local guard_encl_t2 = t.player.by_symbol("npc", "enclave_guard")
        t.exec("useNightshadeOnGuardAgain", t.player.use_on, "nightshade", guard_encl_t2)
        t.exec("useNightshadeOnGuardAgain-drain", t.chat.drain, {stop_at = "none", max_pages = 5})
        await_tile(function(tt) return tt.z > 9000 end, 8, "useNightshadeOnGuardAgain")
        local _, enclave_tile2 = t.world.tile()
        t.check("enclave.entered_again", enclave_tile2 ~= nil and enclave_tile2.x == 2588 and enclave_tile2.z == 9410,
            "player at " .. tile_text("ok", enclave_tile2) .. " (want 2588,9410,0)")
        t.exec("inv.nightshade_spent", t.inv.expect_absent, "nightshade")

        -- ================= Use the potion on the six ogre shamans =================
        -- Base spawn symbols (areas/world/configs/m40_147.spawn); the
        -- "_normal" child QH names has no spawn row in this pack. The sixth
        -- grants powering_crystal3 (ogre_shaman.rs2:39-86).
        local shamans = {
            {sym = "qip_watchtower_ogre_shaman_01", x = 2592, z = 9436},
            {sym = "qip_watchtower_ogre_shaman_02", x = 2582, z = 9437},
            {sym = "qip_watchtower_ogre_shaman_03", x = 2577, z = 9451},
            {sym = "qip_watchtower_ogre_shaman_04", x = 2599, z = 9461},
            {sym = "qip_watchtower_ogre_shaman_05", x = 2607, z = 9451},
            {sym = "qip_watchtower_ogre_shaman_06", x = 2606, z = 9438},
        }
        margin_begin()
        for i, shaman in ipairs(shamans) do
            vitals()
            t.exec("goto-usePotionOnOgre" .. i, t.player.goto_tile, shaman.x, shaman.z, 0)
            local shaman_t = t.player.by_symbol("npc", shaman.sym)
            t.exec("usePotionOnOgre" .. i, t.player.use_on, "magic_ogre_potion", shaman_t)
            -- seam23 copy: the handler pages after its own p_delay(1); a
            -- page left up here holds the script and drops the next click.
            t.ticks(2)
            t.exec("usePotionOnOgre" .. i .. "-drain", t.chat.drain, {})
            vitals()
        end
        t.exec("inv.powering_crystal3", t.inv.await, "powering_crystal3", 1, 10)
        margin_row("usePotionOnOgre.margin", "the enclave's six shamans and its ogres")

        -- ================= Mine the Rock of Dalgroth for the fourth crystal =================
        t.exec("goto-mineRock", t.player.goto_tile, 2592, 9450, 0)
        -- [oploc2,rock_of_dalgroth] is the mining trigger; op1 is only the
        -- flavour "examine the rock" text (quest_itwatchtower.rs2).
        t.exec("mineRock", t.player.click_loc, "rock_of_dalgroth", 2)
        t.exec("inv.powering_crystal4", t.inv.await, "powering_crystal4", 1, 10)

        -- ================= Leave the enclave with all four crystals =================
        t.exec("goto-leaveEnclaveWithCrystals", t.player.goto_tile, 2598, 9467, 0)
        cave_out("leaveEnclaveWithCrystals", "enclavecave", { 2598, 9468 }, { 2540, 3054 })
        t.exec("goto-goUpToFirstFloorWithCrystals", t.player.goto_tile, 2544, 3113, 0)
        up_to_wizard("goUpToFirstFloorWithCrystals", "goUpToWizardWithCrystals")

        t.exec("talkToWizardWithCrystals", t.player.talk_to, "watchtower_wizard", 1)
        -- watchtower_wizard.rs2:29-37 (itwatchtower_made_potion branch, all
        -- six shamans down); seam24: the made_potion branch now hands over
        -- (wiki Transcript:Watchtower oldid 15263268).
        t.exec("talkToWizardWithCrystals-dialog", t.chat.play, {
            "npc:Hello again",
            "player:Indeed it did",
            "npc:Magnificent! At last you've brought all the crystals.",
            "npc:Now the shield generator can be activated",
            "npc:Put the crystals on the pillars there and throw the lever",
        })
        t.exec("inv.all_four_crystals", t.inv.await_all, {
            powering_crystal1 = 1,
            powering_crystal2 = 1,
            powering_crystal3 = 1,
            powering_crystal4 = 1,
        })
        t.check("quest.stage.found_all_crystals", t.quest.expect_stage("itwatchtower_found_all_crystals"))
        local pillars = {
            { n = 1, loc = "qip_watchtower_pillar_nocrystal_multi_yellow", item = "powering_crystal1", varbit = "varb3128_watchtower_pillar_2" },
            { n = 2, loc = "qip_watchtower_pillar_nocrystal_multi_magenta", item = "powering_crystal2", varbit = "varb3129_watchtower_pillar_3" },
            { n = 3, loc = "qip_watchtower_pillar_nocrystal_multi_cyan", item = "powering_crystal3", varbit = "varb3127_watchtower_pillar_1" },
            { n = 4, loc = "qip_watchtower_pillar_nocrystal_multi_white", item = "powering_crystal4", varbit = "varb3130_watchtower_pillar_4" },
        }
        for _, p in ipairs(pillars) do
            local target = t.player.by_symbol("loc", p.loc)
            t.exec("useCrystal" .. p.n, t.player.use_on, p.item, target)
            t.exec("var." .. p.varbit, t.var.await_server, p.varbit, 1, 5)
            t.exec("inv." .. p.item .. "_placed", t.inv.expect_absent, p.item)
        end
        -- Reward baselines, read just before the final hand-in (the lever):
        -- [oploc1,watchleverup] (quest_itwatchtower.rs2:583-586) runs
        -- stat_advance(magic, 152500) = 15250 xp, inv_add coins 5000,
        -- inv_add watchtowerspell 1, then ~quest_complete_rewards awards
        -- quest:questpoints = 4 (all.dbrow [quest_watchtower] column 17).
        -- Wiki Watchtower rewards: 4 QP, 15,250 Magic XP, 5,000 coins,
        -- Watchtower teleport scroll.
        local reward_snapshot_result, reward_before = t.skill.snapshot()
        t.step("reward.snapshot", reward_snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot before the lever -> " .. tostring(reward_snapshot_result))
        local coins_before_r, coins_before = t.inv.count("coins")
        local scroll_before_r, scroll_before = t.inv.count("watchtowerspell")
        local qp_before_r, qp_before = t.var.varp("varp101_qp")
        t.exec("pullLever", t.player.click_loc, "watchleverup", 1)
        t.exec("msg.force_field", t.msg.await, "The magic force field activates.", 10)
        t.ticks(10)
        t.exec("pullLever-dialog", t.chat.play, {
            "npc:Marvellous! It works!",
            "npc:Take this payment",
            "npc:improve your Magic level",
            "npc:Here is a special item",
        })
        t.quest.expect_complete()

        -- Reward rows: literal deltas across the lever, not just completion.
        local magic_r, magic_d = t.skill.expect_gain("magic", 15250, reward_before)
        t.check("reward.magic", magic_r == "ok",
            "magic +15250 xp literal (stat_advance(magic,152500)): " .. tostring(magic_r) .. " " .. tostring(magic_d))
        local coins_after_r, coins_after = t.inv.count("coins")
        t.check("reward.coins",
            coins_before_r == "ok" and coins_after_r == "ok"
                and type(coins_before) == "number" and type(coins_after) == "number"
                and coins_after - coins_before == 5000,
            string.format("coins %s -> %s (want +5000)", tostring(coins_before), tostring(coins_after)))
        local scroll_after_r, scroll_after = t.inv.count("watchtowerspell")
        t.check("reward.watchtowerspell",
            scroll_before_r == "ok" and scroll_after_r == "ok"
                and type(scroll_before) == "number" and type(scroll_after) == "number"
                and scroll_after - scroll_before == 1,
            string.format("watchtowerspell %s -> %s (want +1, Watchtower Teleport scroll)",
                tostring(scroll_before), tostring(scroll_after)))
        local qp_after_r, qp_after = t.var.varp("varp101_qp")
        t.check("reward.questpoints",
            qp_before_r == "ok" and qp_after_r == "ok"
                and type(qp_before) == "number" and type(qp_after) == "number"
                and qp_after - qp_before == 4,
            string.format("quest points %s -> %s (want +4)", tostring(qp_before), tostring(qp_after)))
        t.finish(0)
        return
    end,
}
