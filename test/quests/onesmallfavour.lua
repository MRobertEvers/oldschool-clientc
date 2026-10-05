-- One Small Favour (tier 2). Driven against the real relay chain in
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_onesmallfavour/
-- (onesmallfavour_relay.rs2, onesmallfavour_puzzles.rs2, onesmallfavour.constant)
-- plus the shared-npc owner files each leg merges into (yanni_salika.rs2,
-- jungle_forester.rs2, brian.rs2, aggie.rs2, dttd_haminfiltrate.rs2,
-- fred_the_farmer.rs2, idesofmilk.rs2, horvik.rs2, apothecary.rs2,
-- sanfew.rs2, gnome_glider.rs2).
--
-- Travel (door rule, docs/QUEST_ORCHESTRATOR.md, owner 2026-10-03): a
-- goto_tile departs from and lands on open ground only. Everything closed
-- between is clicked, in and out, on every visit:
--   * Karamja is an island: seaman_lorris's paid crossing (sailors.rs2
--     karamja_sailor_talk/_pay) and the Musa Point gangplank; back by the
--     Musa Point customs officer (customs_officer.rs2 customs_pay ->
--     3032,3217,1) and the Port Sarim gangplank. The members' gate
--     (membergatel 2816,3182, gates.rs2 walk-through) is the only way on
--     foot between Musa Point and Brimhaven (reach.py: NEEDS-DOOR at
--     margins 80 and 160) and is pressed on every crossing.
--   * Shilo Village is walled; the outside of its metal gate is a 30-tile
--     pocket of timber defences (comp.py), so the village is reached the
--     way a player does: Hajedy's cart from Brimhaven (hajedy.rs2:42 ->
--     2834,2951) and Vigroy's cart back (vigroy.rs2:37 -> 2776,3214);
--     quest_shilovillage is completed in setup (a One Small Favour
--     requirement, and hajedy.rs2:25's own gate).
--   * Doors and gates by t.player.pass_door; the Taverley wall by
--     t.player.cross_gate; a ladder, stair or trapdoor that changes level
--     by t.player.climb (graded on the new level and the landing); the
--     ones that change only the map frame (the H.A.M. and Ice Mountain
--     trapdoors and ladders, the goblin cave mouth) and the gangplanks by
--     a click on the exact tile and level, graded on the landing (transit
--     below).
--
-- Captain Bleemadge: gnome_glider.rs2:27-29 hands the One Small Favour
-- window (stages 75..86 and 190) to onesmallfavour_relay.rs2's
-- [label,osf_bleemadge_talk].
--
-- The Seers' roof (osf_weathervane 2702,3476, plane 3): favour_seer_ladder
-- (2715,3472,1) climbs onto 2714,3472,3 and favour_roof_trapdoor
-- (2715,3472,3) drops onto 2714,3472,1 (onesmallfavour_puzzles.rs2:115-117,
-- ~climb_ladder_to); every roof trip climbs both by click with
-- t.player.climb, graded on the level and the landing tile.

-- Tile of a t.world.tile() read, or the status that came back instead.
local function tile_text(r, tt)
    if r == "ok" and type(tt) == "table" then
        return tt.x .. "," .. tt.z .. "," .. tt.level
    end
    return tostring(r)
end

-- A verb's detail for a row: strings and numbers as they are, never a
-- table address.
local function txt(v)
    local kind = type(v)
    if kind == "string" or kind == "number" or kind == "boolean" or kind == "nil" then
        return tostring(v)
    end
    return "<" .. kind .. ">"
end

-- ~calc_shilocart_cost (hajedy.rs2:49-56, vigroy.rs2 shares it): five per
-- cent of the coins carried, never under 10 or over 200.
local function cart_fare(coins)
    local fare = math.floor(coins * 5 / 100)
    if fare < 10 then
        fare = 10
    elseif fare > 200 then
        fare = 200
    end
    return fare
end

local function within(tt, x0, x1, z0, z1, level)
    return tt.level == level and tt.x >= x0 and tt.x <= x1 and tt.z >= z0 and tt.z <= z1
end

return {
    id = "onesmallfavour",
    fixture = "fresh_lumbridge.ini",
    max_frames = 420000,
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        -- Levels first: the rune gear below is wielded in setup.
        "::setlevel agility 36",
        "::setlevel crafting 25",
        "::setlevel herblore 18",
        "::setlevel smithing 30",
        "::setlevel attack 80",
        "::setlevel strength 80",
        "::setlevel defence 80",
        "::setlevel hitpoints 90",
        "::give steel_bar 4",
        "::give bronze_bar 1",
        "::give iron_bar 1",
        "::give chisel 1",
        "::give guam_leaf 2",
        "::give marentill 1",
        "::give harralander 1",
        "::give hammer 1",
        "::give cup_empty 1",
        "::give bowl_hot_water 1",
        -- Cut gems for the landing lights: Quest Helper getItemRecommended
        -- opal2/jade2/redTopaz2 (wiki: "Two of each of the following cut
        -- gems, or a chisel to cut the uncut gems received during the
        -- quest"); the lamps' own uncut sapphires are cut in the run
        -- (sapphires cannot be crushed). Cutting the uncut opal/jade/red
        -- topaz would crush at random (gem.dbrow success_rate), so the
        -- recommended cut ones are brought instead.
        "::give jade 2",
        "::give opal 2",
        "::give red_topaz 2",
        -- Slagilith (level 92) and three level-44 gang dwarves: a pickaxe
        -- (the guide's recommended weapon for Slagilith), armour and food.
        -- Worn in setup, before the food is given: five backpack slots the
        -- sharks and the fares need (twenty items above, so 4 sharks and
        -- the coins make 25 of 28 -- the forester's axe still fits).
        "::give rune_pickaxe 1",
        "::wield rune_pickaxe",
        "::give rune_full_helm 1",
        "::wield rune_full_helm",
        "::give rune_chainbody 1",
        "::wield rune_chainbody",
        "::give rune_platelegs 1",
        "::wield rune_platelegs",
        "::give rune_kiteshield 1",
        "::wield rune_kiteshield",
        "::give shark 4",
        -- Fares a player pays: seaman_lorris 30 twice, the customs officer
        -- 30, and three Shilo cart rides at ~calc_shilocart_cost (10 each
        -- while under 200 coins are carried): 120 of 150.
        "::give coins 150",
        "::complete quest_runemysteries",
        "::complete quest_druidicritual",
        "::complete quest_shilovillage",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp416_onesmallfavour",
            constants = {
                not_started = 0,
                forester_axe = 5,
                axe_to_brian = 10,
                aggie_agreed = 20,
                aggie_told = 22,
                johanhus_told = 25,
                fred_told = 45,
                seth_told = 50,
                horvik_told = 55,
                apoth_told = 60,
                tassie_told = 65,
                hammerspike_told = 70,
                sanfew_told = 75,
                brewing_tea = 80,
                bleemadge_wants_trash = 86,
                arhein_told = 88,
                phantuwti_told = 90,
                wall_found = 95,
                cromperty_told = 100,
                tindel_told = 105,
                rantz_told = 110,
                gnormadium_told = 115,
                lights_repairing = 120,
                lights_fixed = 122,
                gnormadium_done = 125,
                rantz_done = 130,
                tindel_done = 135,
                cromperty_done = 140,
                slagilith_fight = 145,
                slagilith_defeated = 150,
                petra_freed = 152,
                phantuwti_weather = 160,
                vane_search = 175,
                vane_searched = 176,
                vane_loosened = 177,
                vane_parts_taken = 178,
                vane_repaired = 180,
                phantuwti_vane_done = 185,
                arhein_done = 190,
                bleemadge_done = 195,
                sanfew_done = 200,
                hammerspike_gang = 205,
                hammerspike_done = 225,
                tassie_done = 230,
                pot_made = 235,
                horvik_medicine = 238,
                horvik_done = 240,
                seth_done = 250,
                johanhus_done = 255,
                aggie_done = 260,
                brian_done = 265,
                forester_done = 270,
                complete = 285,
            },
            row = "quest_onesmallfavour",
            display = "One Small Favour",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ---------------------------------------------------------------
        -- Travel helpers.
        -- ---------------------------------------------------------------
        local function coins_now()
            local r, n = t.inv.count("coins")
            if r == "ok" then
                return n
            end
            return nil
        end

        -- A use-item row's proof: await `await_sym` reaching `await_n` (nil:
        -- no wait), then grade the EXACT count of every item `want` lists
        -- ({ {sym, n}, ... }) -- the used item left the pack and the product
        -- came, not just that the click answered.
        local function pack_after(name, await_sym, await_n, want)
            local ar = "ok"
            if await_sym ~= nil then
                ar = t.inv.await(await_sym, await_n, 10)
            end
            local ok, parts = ar == "ok", {}
            for i = 1, #want do
                local r, n = t.inv.count(want[i][1])
                ok = ok and r == "ok" and n == want[i][2]
                parts[#parts + 1] = want[i][1] .. "=" .. tostring(n) .. " (want " .. want[i][2] .. ")"
            end
            t.check(name, ok, (await_sym and ("await " .. await_sym .. " -> " .. tostring(ar) .. "; ") or "")
                .. table.concat(parts, ", "))
        end

        -- Press a loc whose own op carries the player to another floor or
        -- map frame (a ladder, a staircase, a trapdoor, a cave mouth, a
        -- gangplank) on its exact tile AND level, then wait for the landing.
        local function press_and_land(sym, op, at, land_ok, ticks)
            local br, bt = t.world.tile()
            local cr, cd = t.player.click_loc(sym, op, { at = at })
            local ar = t.await({
                level = function()
                    local r, tt = t.world.tile()
                    return r == "ok" and land_ok(tt)
                end,
                note = sym .. ": waiting for the landing",
            }, ticks or 15)
            local tr, tt = t.world.tile()
            return {
                ok = br == "ok" and not land_ok(bt) and ar == "ok" and tr == "ok" and land_ok(tt),
                detail = "from " .. tile_text(br, bt) .. "; click_loc(" .. sym .. ", " .. op .. ", at "
                    .. at[1] .. "," .. at[2] .. "," .. at[3] .. ") -> " .. tostring(cr) .. " " .. txt(cd)
                    .. "; landing await -> " .. tostring(ar) .. "; landed " .. tile_text(tr, tt),
                tile_r = tr,
                tile = tt,
            }
        end

        -- One graded crossing: the tile before is NOT the landing, the tile
        -- after IS (the press's own answer is only in the detail).
        local function transit(name, sym, op, at, land_ok, land_desc, ticks)
            local landed = press_and_land(sym, op, at, land_ok, ticks)
            t.check(name, landed.ok, landed.detail .. " (want " .. land_desc .. ")")
        end

        local function door(name, closed, open, at, near, far)
            t.exec(name, t.player.pass_door, { closed = closed, open = open, at = at, near = near, far = far })
        end

        -- Port Sarim -> Musa Point: seaman_lorris (sailors.rs2
        -- karamja_sailor_talk; a fresh account gets the plain p_choice2),
        -- 30 coins, p_delay(2), p_telejump onto the deck, its own mesbox;
        -- then the gangplank ashore.
        local function sail_to_musa(p)
            t.exec("goto-" .. p .. ".seaman", t.player.goto_tile, 3028, 3221, 0)
            local coins0 = coins_now()
            t.exec(p .. ".talkToSeaman", t.player.talk_to, "seaman_lorris", 1)
            t.exec(p .. ".talkToSeaman-dialog", t.chat.play, {
                "npc:Do you want to go on a trip to Karamja?",
                "npc:The trip will cost you 30 coins.",
                "options",
                "choose:Yes please.",
                "player:Yes please.",
            })
            t.expect(p .. ".seaman.paid", t.msg.expect("pay the 30 coins and board the ship"))
            local sail_r, sail_d = t.await({
                level = function()
                    return t.chat.kind() == "mesbox"
                end,
                note = p .. ": the arrival mesbox after p_delay(2) + telejump",
            }, 15)
            t.step(p .. ".seaman.sailed", sail_r == "ok" and "PASS" or "FAIL",
                "await(chat.kind() == mesbox) -> " .. tostring(sail_r) .. " " .. txt(sail_d))
            t.exec(p .. ".seaman.arrive", t.chat.play, { "mesbox:The ship arrives at Karamja." })
            local deck_r, deck = t.world.tile()
            local coins1 = coins_now()
            t.check(p .. ".seaman.onDeck",
                deck_r == "ok" and deck.level == 1 and math.abs(deck.x - 2956) <= 2 and math.abs(deck.z - 3143) <= 2
                    and coins0 ~= nil and coins1 ~= nil and coins0 - coins1 == 30,
                "tile " .. tile_text(deck_r, deck) .. " (want the deck 2956,3143,1), coins " .. tostring(coins0)
                    .. " -> " .. tostring(coins1) .. " (want -30)")
            transit(p .. ".disembarkMusa", "sarimshipplank_off", 1, { 2956, 3144, 1 },
                function(tt) return tt.level == 0 and math.abs(tt.x - 2956) <= 3 and tt.z >= 3145 and tt.z <= 3150 end,
                "ashore on the Musa Point jetty, level 0, z 3145-3150")
        end

        -- Karamja's members' gate, membergatel 2816,3182 (a west-wall leaf:
        -- the gate tile and everything east of it is the Musa Point side).
        local function karamja_gate_west(p)
            t.exec("goto-" .. p .. ".karamjaGate", t.player.goto_tile, 2819, 3182, 0)
            t.exec(p .. ".karamjaGate", t.player.cross_gate, { loc = "membergatel", at = { 2816, 3182, 0 },
                near = { 2817, 3182 }, far_ok = function(tile) return tile.x <= 2815 end,
                far_desc = "west of the members' gate, the Brimhaven side, x <= 2815" })
        end
        local function karamja_gate_east(p)
            t.exec("goto-" .. p .. ".karamjaGate", t.player.goto_tile, 2813, 3182, 0)
            t.exec(p .. ".karamjaGate", t.player.cross_gate, { loc = "membergatel", at = { 2816, 3182, 0 },
                near = { 2815, 3182 }, far_ok = function(tile) return tile.x >= 2816 end,
                far_desc = "east of the members' wall, the Musa Point side, x >= 2816" })
        end

        -- Hajedy's cart, Brimhaven -> Shilo Village (hajedy.rs2:24-47).
        local function cart_to_shilo(p)
            t.exec("goto-" .. p .. ".hajedy", t.player.goto_tile, 2781, 3212, 0)
            local coins0 = coins_now()
            local fare = coins0 and cart_fare(coins0)
            t.exec(p .. ".talkToHajedy", t.player.talk_to, "brimhavencartdriver", 1)
            t.exec(p .. ".talkToHajedy-dialog", t.chat.play, {
                "player:Hello!",
                "npc:Hello Bwana!",
                "npc:I am offering a cart ride to Shilo Village",
                "options",
                "choose:Yes please, I'd like to go to Shilo Village.",
                "player:Yes please, I'd like to go to Shilo Village.",
                "npc:Great! Just hop into the cart",
                "mesbox:You hop into the cart",
                "mesbox:You pay the fare and hand",
                "mesbox:You feel tired from the journey",
            })
            local tr, tt = t.world.tile()
            local coins1 = coins_now()
            t.check(p .. ".cartToShilo",
                tr == "ok" and within(tt, 2832, 2836, 2949, 2953, 0) and fare ~= nil and coins1 ~= nil
                    and coins0 - coins1 == fare,
                "tile " .. tile_text(tr, tt) .. " (want inside Shilo Village by the cart, 2834,2951,0 +-2), coins "
                    .. tostring(coins0) .. " -> " .. tostring(coins1) .. " (want -" .. tostring(fare)
                    .. ", ~calc_shilocart_cost)")
        end

        -- Vigroy's cart, Shilo Village -> Brimhaven (vigroy.rs2:23-42).
        local function cart_to_brimhaven(p)
            t.exec("goto-" .. p .. ".vigroy", t.player.goto_tile, 2834, 2952, 0)
            local coins0 = coins_now()
            local fare = coins0 and cart_fare(coins0)
            t.exec(p .. ".talkToVigroy", t.player.talk_to, "shilocartdriver", 1)
            t.exec(p .. ".talkToVigroy-dialog", t.chat.play, {
                "player:Hello!",
                "npc:Hello Bwana!",
                "npc:I am offering a cart ride to Brimhaven",
                "options",
                "choose:Yes please, I'd like to go to Brimhaven.",
                "player:Yes please, I'd like to go to Brimhaven.",
                "npc:Great! Just hop into the cart",
                "mesbox:You hop into the cart",
                "mesbox:You pay the fare and hand",
                "mesbox:You feel tired from the journey",
            })
            local tr, tt = t.world.tile()
            local coins1 = coins_now()
            t.check(p .. ".cartToBrimhaven",
                tr == "ok" and within(tt, 2774, 2778, 3212, 3216, 0) and fare ~= nil and coins1 ~= nil
                    and coins0 - coins1 == fare,
                "tile " .. tile_text(tr, tt) .. " (want Brimhaven by the cart, 2776,3214,0 +-2), coins "
                    .. tostring(coins0) .. " -> " .. tostring(coins1) .. " (want -" .. tostring(fare) .. ")")
        end

        -- Musa Point -> Port Sarim: the customs officer's search and boarding
        -- charge (customs_officer.rs2:14-115), then the gangplank ashore.
        local function sail_to_sarim(p)
            t.exec("goto-" .. p .. ".customs", t.player.goto_tile, 2953, 3147, 0)
            local coins0 = coins_now()
            t.exec(p .. ".talkToCustoms", t.player.talk_to, "customs_officer", 1)
            t.exec(p .. ".talkToCustoms-dialog", t.chat.play, {
                "npc:Can I help you?",
                "options",
                "choose:Can I journey on this ship?",
                "player:Can I journey on this ship?",
                "npc:You need to be searched",
                "options",
                "choose:Search away, I have nothing to hide.",
                "player:Search away, I have nothing to hide.",
                "npc:it's all legal",
                "options",
                "choose:Ok.",
                "player:Ok.",
            })
            t.expect(p .. ".customs.paid", t.msg.expect("pay 30 coins and board the ship"))
            local sail_r, sail_d = t.await({
                level = function()
                    return t.chat.kind() == "mesbox"
                end,
                note = p .. ": the arrival mesbox after p_delay(2) + telejump",
            }, 15)
            t.step(p .. ".customs.sailed", sail_r == "ok" and "PASS" or "FAIL",
                "await(chat.kind() == mesbox) -> " .. tostring(sail_r) .. " " .. txt(sail_d))
            t.exec(p .. ".customs.arrive", t.chat.play, { "mesbox:ship arrives at Port Sarim" })
            local deck_r, deck = t.world.tile()
            local coins1 = coins_now()
            t.check(p .. ".customs.onDeck",
                deck_r == "ok" and deck.level == 1 and math.abs(deck.x - 3032) <= 2 and math.abs(deck.z - 3217) <= 2
                    and coins0 ~= nil and coins1 ~= nil and coins0 - coins1 == 30,
                "tile " .. tile_text(deck_r, deck) .. " (want the deck 3032,3217,1), coins " .. tostring(coins0)
                    .. " -> " .. tostring(coins1) .. " (want -30)")
            transit(p .. ".disembarkSarim", "karamjashipplank_off", 1, { 3031, 3217, 1 },
                function(tt) return tt.level == 0 and tt.x <= 3030 and math.abs(tt.z - 3217) <= 2 end,
                "on the Port Sarim pier, level 0, x <= 3030")
        end

        -- Brian's axe shop (poordoor 3031,3248, a west-wall leaf: the shop
        -- is x <= 3030).
        local function brian_in(p)
            t.exec("goto-" .. p, t.player.goto_tile, 3033, 3246, 0)
            door(p .. ".doorIn", "poordoor", "poordooropen", { 3031, 3248, 0 }, { 3032, 3248 }, { 3029, 3248 })
        end
        local function brian_out(p)
            door(p .. ".doorOut", "poordoor", "poordooropen", { 3031, 3248, 0 }, { 3030, 3248 }, { 3032, 3248 })
        end

        -- Aggie's house (poordoor 3088,3258, an east-wall leaf: x <= 3088).
        local function aggie_in(p)
            t.exec("goto-" .. p, t.player.goto_tile, 3091, 3258, 0)
            door(p .. ".doorIn", "poordoor", "poordooropen", { 3088, 3258, 0 }, { 3089, 3258 }, { 3087, 3258 })
        end
        local function aggie_out(p)
            door(p .. ".doorOut", "poordoor", "poordooropen", { 3088, 3258, 0 }, { 3087, 3258 }, { 3090, 3258 })
        end

        -- The H.A.M. hideout (m49_150): the entry pocket under the trapdoor,
        -- a diagonal poordoor (3158,9640, shape 9) into the main cavern, and
        -- Johanhus's room behind poordoor 3171,9621 (a north-wall leaf: the
        -- room is z <= 9621). Out by osf_ham_ladder (losttribe_ham.rs2:84:
        -- p_teleport(^lt_ham_ladder_out) = 3165,3251).
        local function on_ham_surface(tt) return tt.level == 0 and tt.z < 4000 end
        local function in_ham_lair(tt) return tt.level == 0 and tt.z > 9000 end
        local function johanhus_in(p)
            door(p .. ".lairDoorIn", "poordoor", "poordooropen", { 3158, 9640, 0 }, { 3157, 9641 }, { 3159, 9639 })
            t.exec(p .. ".walkToCell", t.player.walk_route, { { 3165, 9631 }, { 3171, 9623 } })
            door(p .. ".cellDoorIn", "poordoor", "poordooropen", { 3171, 9621, 0 }, { 3171, 9622 }, { 3171, 9620 })
        end
        local function johanhus_out(p)
            door(p .. ".cellDoorOut", "poordoor", "poordooropen", { 3171, 9621, 0 }, { 3171, 9621 }, { 3171, 9623 })
            t.exec(p .. ".walkToLairDoor", t.player.walk_route, { { 3165, 9631 }, { 3159, 9639 } })
            door(p .. ".lairDoorOut", "poordoor", "poordooropen", { 3158, 9640, 0 }, { 3159, 9639 }, { 3157, 9641 })
            t.exec(p .. ".walkToLadder", t.player.walk_route, { { 3152, 9647 }, { 3149, 9652 } })
            transit(p .. ".climbOut", "osf_ham_ladder", 1, { 3149, 9653, 0 },
                function(tt) return on_ham_surface(tt) and math.abs(tt.x - 3165) <= 2 and math.abs(tt.z - 3251) <= 2 end,
                "on the surface by the trapdoor, 3165,3251,0 +-2")
        end

        -- Fred's farm: the yard gate (qip_sheep_shearer_fencegate_l/_r
        -- 3188-3189,3279, north-wall leaves; pressed by the left leaf, whose
        -- open leaf stands beside it -- the right one swings two tiles out) and his house door (qip_sheep_shearer_poordoor
        -- 3189,3275, a north-wall leaf: the house is z <= 3275).
        local function fred_in(p)
            t.exec("goto-" .. p, t.player.goto_tile, 3189, 3282, 0)
            door(p .. ".gateIn", "qip_sheep_shearer_fencegate_l", "qip_sheep_shearer_openfencegate_l",
                { 3188, 3279, 0 }, { 3188, 3280 }, { 3188, 3278 })
            door(p .. ".doorIn", "qip_sheep_shearer_poordoor", "qip_sheep_shearer_poordooropen",
                { 3189, 3275, 0 }, { 3189, 3276 }, { 3189, 3274 })
        end
        local function fred_out(p)
            door(p .. ".doorOut", "qip_sheep_shearer_poordoor", "qip_sheep_shearer_poordooropen",
                { 3189, 3275, 0 }, { 3189, 3275 }, { 3189, 3277 })
            door(p .. ".gateOut", "qip_sheep_shearer_fencegate_l", "qip_sheep_shearer_openfencegate_l",
                { 3188, 3279, 0 }, { 3188, 3278 }, { 3188, 3281 })
        end

        -- Seth Groats's farm: the yard gate (fencegate_l/_r 3236,3296-3295,
        -- east-wall leaves; pressed by the left leaf), the farmhouse door (poordoor 3230,3291, east
        -- wall) and his own room (poordoor 3225,3293, west wall: x <= 3224).
        local function seth_in(p)
            t.exec("goto-" .. p, t.player.goto_tile, 3239, 3295, 0)
            door(p .. ".gateIn", "fencegate_l", "openfencegate_l", { 3236, 3296, 0 }, { 3237, 3296 }, { 3235, 3296 })
            door(p .. ".houseDoorIn", "poordoor", "poordooropen", { 3230, 3291, 0 }, { 3231, 3291 }, { 3229, 3291 })
            door(p .. ".roomDoorIn", "poordoor", "poordooropen", { 3225, 3293, 0 }, { 3225, 3293 }, { 3224, 3293 })
        end
        local function seth_out(p)
            door(p .. ".roomDoorOut", "poordoor", "poordooropen", { 3225, 3293, 0 }, { 3224, 3293 }, { 3226, 3293 })
            door(p .. ".houseDoorOut", "poordoor", "poordooropen", { 3230, 3291, 0 }, { 3229, 3291 }, { 3232, 3291 })
            door(p .. ".gateOut", "fencegate_l", "openfencegate_l", { 3236, 3296, 0 }, { 3235, 3296 }, { 3238, 3296 })
        end

        -- The Dwarven Mine from Ice Mountain: fai_dwarf_trapdoor_down
        -- (3019,3450; maplink 3018/3020,3450 -> 3018/3020,9850) and back up
        -- ladder_from_cellar_directional (3019,9850 -> 3018,3450). The
        -- Falador hut's stairs_cellar has no down row, so it climbs nowhere.
        local function mine_down(name)
            t.exec("goto-" .. name, t.player.goto_tile, 3016, 3446, 0)
            transit(name, "fai_dwarf_trapdoor_down", 1, { 3019, 3450, 0 },
                function(tt) return tt.level == 0 and tt.z >= 9845 and tt.z <= 9855 and math.abs(tt.x - 3019) <= 3 end,
                "in the Dwarven Mine under the trapdoor, 3018-3020,9850")
        end
        local function mine_up(name)
            t.exec("goto-" .. name, t.player.goto_tile, 3019, 9849, 0)
            transit(name, "ladder_from_cellar_directional", 1, { 3019, 9850, 0 },
                function(tt) return tt.level == 0 and tt.z < 4000 and math.abs(tt.x - 3019) <= 3 and math.abs(tt.z - 3450) <= 3 end,
                "back on Ice Mountain by the trapdoor, 3018,3450")
        end

        -- Taverley's members' wall, east gate (membergater 2935,3450, rot 2:
        -- Taverley is x <= 2935).
        local function taverley_in(p)
            t.exec("goto-" .. p .. ".memberGate", t.player.goto_tile, 2938, 3450, 0)
            t.exec(p .. ".memberGate", t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 },
                near = { 2936, 3450 }, far_ok = function(tile) return tile.x <= 2935 end,
                far_desc = "inside Taverley, x <= 2935" })
        end
        local function taverley_out(p)
            t.exec("goto-" .. p .. ".memberGate", t.player.goto_tile, 2932, 3450, 0)
            t.exec(p .. ".memberGate", t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 },
                near = { 2934, 3450 }, far_ok = function(tile) return tile.x >= 2936 end,
                far_desc = "out of Taverley, x >= 2936" })
        end

        -- Sanfew's tower: the west doorway's leaf is placed OPEN by the map
        -- (poordooropen 2895,3428), so pass_door reads it standing open and
        -- does not press it; the spiral staircase (2898,3428) climbs to
        -- 2898,3427,1 (maplink rows) and spiralstairstop comes back down to
        -- 2897,3428,0; both by t.player.climb.
        local function in_sanfew_upstairs(tt) return within(tt, 2895, 2901, 3424, 3432, 1) end
        local function in_sanfew_ground(tt) return within(tt, 2895, 2901, 3425, 3431, 0) end
        local function sanfew_up(p, climb_name)
            t.exec("goto-" .. p, t.player.goto_tile, 2892, 3428, 0)
            door(p .. ".doorIn", "poordoor", "poordooropen", { 2895, 3428, 0 }, { 2894, 3428 }, { 2896, 3428 })
            t.exec(climb_name, t.player.climb, { loc = "spiralstairs", op = 1, op_name = "Climb-up",
                at = { 2898, 3428, 0 }, dest = { 2898, 3427, 1 }, slack = 1, landed_ok = in_sanfew_upstairs,
                landed_desc = "upstairs in Sanfew's tower, level 1" })
        end
        local function sanfew_down(p)
            t.exec(p .. ".stairsDown", t.player.climb, { loc = "spiralstairstop", op = 1, op_name = "Climb-down",
                at = { 2898, 3428, 1 }, dest = { 2897, 3428, 0 }, slack = 1, landed_ok = in_sanfew_ground,
                landed_desc = "back on the ground floor of the tower, level 0" })
            door(p .. ".doorOut", "poordoor", "poordooropen", { 2895, 3428, 0 }, { 2896, 3428 }, { 2893, 3428 })
        end

        -- Phantuwti's house in Seers' Village (kr_poordoor 2701,3477, a
        -- south-wall leaf: the house is z <= 3476).
        local function phantuwti_in(p)
            t.exec("goto-" .. p, t.player.goto_tile, 2701, 3479, 0)
            door(p .. ".doorIn", "kr_poordoor", "kr_poordooropen", { 2701, 3477, 0 }, { 2701, 3477 }, { 2701, 3475 })
        end
        local function phantuwti_out(p)
            door(p .. ".doorOut", "kr_poordoor", "kr_poordooropen", { 2701, 3477, 0 }, { 2701, 3476 }, { 2701, 3479 })
        end

        -- The house's ladder (kr_ladder_directional 2699,3476, no maplink:
        -- one plane, landing on the approach tile 2699,3475), the two level-1 doors east (kr_poordoor 2706,3472 and
        -- 2709,3472) and the roof ladder (favour_seer_ladder 2715,3472,1) /
        -- trapdoor (favour_roof_trapdoor 2715,3472,3).
        local function on_roof(tt) return within(tt, 2700, 2714, 3470, 3475, 3) end
        local function in_house_upstairs(tt) return within(tt, 2698, 2705, 3470, 3477, 1) end
        local function in_house_ground(tt) return within(tt, 2698, 2706, 3469, 3476, 0) end
        local function by_roof_ladder(tt) return within(tt, 2710, 2715, 3469, 3473, 1) end
        local function house_up(name)
            t.exec(name, t.player.climb, { loc = "kr_ladder_directional", op = 1, op_name = "Climb-up",
                at = { 2699, 3476, 0 }, dest = { 2699, 3475, 1 }, slack = 1, landed_ok = in_house_upstairs,
                landed_desc = "upstairs in Phantuwti's house, level 1" })
        end
        local function house_down(name)
            t.exec(name, t.player.climb, { loc = "kr_laddertop_directional", op = 1, op_name = "Climb-down",
                at = { 2699, 3476, 1 }, dest = { 2699, 3475, 0 }, slack = 1, landed_ok = in_house_ground,
                landed_desc = "back on the ground floor of the house, level 0" })
        end
        local function to_roof_ladder(p)
            door(p .. ".eastDoor1", "kr_poordoor", "kr_poordooropen", { 2706, 3472, 1 }, { 2705, 3472 }, { 2707, 3472 })
            door(p .. ".eastDoor2", "kr_poordoor", "kr_poordooropen", { 2709, 3472, 1 }, { 2709, 3472 }, { 2711, 3472 })
            t.exec(p .. ".walkToRoofLadder", t.player.walk_route, { { 2714, 3472 } }, { level = 1 })
        end
        -- The roof ladder and trapdoor (onesmallfavour_puzzles.rs2:115-117,
        -- ~climb_ladder_to: both land on the ladder's west tile 2714,3472,
        -- the ladder onto plane 3, the trapdoor onto plane 1).
        local function roof_up(name)
            t.exec(name, t.player.climb, { loc = "favour_seer_ladder", op = 1, op_name = "Climb-up",
                at = { 2715, 3472, 1 }, dest = { 2714, 3472, 3 }, landed_ok = on_roof,
                landed_desc = "on the roof, plane 3, x 2700-2714 z 3470-3475" })
        end
        local function roof_down(name)
            t.exec(name, t.player.climb, { loc = "favour_roof_trapdoor", op = 1, op_name = "Climb-down",
                at = { 2715, 3472, 3 }, dest = { 2714, 3472, 1 }, landed_ok = by_roof_ladder,
                landed_desc = "back on level 1 by the roof ladder, x 2710-2715 z 3469-3473" })
        end
        local function from_roof_ladder(p)
            t.exec(p .. ".walkFromRoofLadder", t.player.walk_route, { { 2711, 3472 } }, { level = 1 })
            door(p .. ".westDoor2", "kr_poordoor", "kr_poordooropen", { 2709, 3472, 1 }, { 2710, 3472 }, { 2708, 3472 })
            door(p .. ".westDoor1", "kr_poordoor", "kr_poordooropen", { 2706, 3472, 1 }, { 2706, 3472 }, { 2704, 3472 })
        end

        -- The goblin cave (mcannon_cave_guard.rs2: mcannoncave ->
        -- 2620,9797; mcanmudpile 2621,9796 -> 2623,3391).
        local function in_goblin_cave(tt) return tt.level == 0 and tt.z > 9000 and math.abs(tt.x - 2620) <= 3 and math.abs(tt.z - 9797) <= 3 end
        local function by_cave_mouth(tt) return tt.level == 0 and tt.z < 4000 and math.abs(tt.x - 2623) <= 2 and math.abs(tt.z - 3391) <= 2 end
        local function cave_in(name)
            t.exec("goto-" .. name, t.player.goto_tile, 2623, 3389, 0)
            transit(name, "mcannoncave", 1, { 2622, 3392, 0 }, in_goblin_cave, "in the goblin cave, 2620,9797 +-3")
        end
        local function cave_out(name)
            t.exec("goto-" .. name, t.player.goto_tile, 2620, 9798, 0)
            transit(name, "mcanmudpile", 1, { 2621, 9796, 0 }, by_cave_mouth, "out by the cave mouth, 2623,3391 +-2")
        end

        -- Wizard Cromperty's building (castledoubledoorl 2678,3325, an
        -- east-wall leaf: the building is x >= 2679).
        local function cromperty_in(p)
            t.exec("goto-" .. p, t.player.goto_tile, 2676, 3325, 0)
            door(p .. ".doorIn", "castledoubledoorl", "opencastledoubledoorl", { 2678, 3325, 0 }, { 2678, 3325 }, { 2680, 3325 })
        end
        local function cromperty_out(p)
            door(p .. ".doorOut", "castledoubledoorl", "opencastledoubledoorl", { 2678, 3325, 0 }, { 2679, 3325 }, { 2677, 3325 })
        end

        -- Fight margin: the lowest stated hitpoints (await_dead_engaged's eat
        -- sampler) at least a quarter of the maximum AND food left.
        local function margin(name, details, staged)
            local low, base
            for i = 1, #details do
                local l, b = string.match(tostring(details[i]), "lowest hp (%d+)/(%d+)")
                l, b = tonumber(l), tonumber(b)
                if l ~= nil and (low == nil or l < low) then
                    low, base = l, b
                end
            end
            local fr, left = t.inv.count("shark")
            t.check(name, low ~= nil and base ~= nil and low * 4 >= base and fr == "ok" and (left or 0) >= 1,
                "lowest hp " .. tostring(low) .. "/" .. tostring(base) .. ", sharks left " .. tostring(left)
                    .. " (" .. tostring(fr) .. ") of " .. staged .. " staged -- margin: lowest hp >= a quarter of max AND food left")
        end
        local EAT = { eat = { item = "shark", below = 50 } }

        -- === Yanni Salika (Shilo Village) -- start the favour chain ======
        -- areas/area_shilo/scripts/yanni_salika.rs2:9-27 (osf_not_started
        -- branch, additive ahead of the antiques-trading fallback).
        sail_to_musa("talkToYanni")
        karamja_gate_west("talkToYanni")
        cart_to_shilo("talkToYanni")
        t.exec("goto-talkToYanni", t.player.goto_tile, 2835, 2984, 0)
        t.exec("talkToYanni", t.player.talk_to, "shiloantiques", 1)
        t.exec("talkToYanni-dialog", t.chat.play, {
            "player:Is there anything else interesting to do around here?",
            "npc:Interesting, you say?",
            "player:Yes.",
            "npc:Splendid! Go and see them",
        })
        t.expect("quest.stage.forester_axe", t.quest.expect_stage("forester_axe"))

        -- === Jungle Forester (Karamja jungle) =============================
        -- quests/quest_legends/scripts/jungle_forester.rs2:14-22
        -- ([opnpc1,jungleforester_m/f] @osf_jungleforester_check;).
        -- Real spawn row (areas/world/configs/m43_46.spawn): jungleforester_f
        -- 2759,2944,0 -- outside Shilo's walls, on foot from Brimhaven.
        cart_to_brimhaven("talkToJungleForester")
        t.exec("goto-talkToJungleForester", t.player.goto_tile, 2759, 2945, 0)
        t.exec("talkToJungleForester", t.player.talk_to, "jungleforester_f", 1)
        t.exec("talkToJungleForester-dialog", t.chat.play, {
            "player:I need to talk to you about red mahogany.",
            "npc:Red mahogany! Rare stuff around here.",
            "player:Okay, I'll take your axe to get it sharpened.",
            "npc:Bless you. Brian at the Port Sarim axe shop",
        })
        t.expect("quest.stage.axe_to_brian", t.quest.expect_stage("axe_to_brian"))
        t.exec("blunt_axe.received", t.inv.await, "favour_jungleforesteraxe_blunt", 1, 10)

        -- === Brian (Port Sarim axe shop) ===================================
        -- areas/port_sarim/scripts/brian.rs2:8-21 (osf_axe_to_brian branch,
        -- requires the blunt axe already in the backpack).
        karamja_gate_east("talkToBrian")
        sail_to_sarim("talkToBrian")
        brian_in("talkToBrian")
        t.exec("talkToBrian", t.player.talk_to, "brian", 1)
        t.exec("talkToBrian-dialog", t.chat.play, {
            "player:Do you sharpen axes?",
            "npc:That old thing?",
            "player:Look, can you sharpen this cursed axe or what?",
            "npc:Ok, ok, I'll do it! I'll go and see Aggie.",
        })
        t.expect("quest.stage.aggie_agreed", t.quest.expect_stage("aggie_agreed"))
        brian_out("talkToBrian")

        -- === Aggie (Draynor Village) ========================================
        -- areas/draynor/scripts/aggie.rs2 delegates to onesmallfavour_relay.rs2's
        -- [proc,osf_aggie_talk]: Brian's osf_aggie_agreed (20) is her stage, and
        -- "Oh, Ok, I'll see if I can find Jimmy." hands Johanhus osf_aggie_told
        -- (wiki Transcript "Asking Aggie to be a character witness"; Quest
        -- Helper talkToAggie's three dialogue steps).
        aggie_in("talkToAggie")
        t.exec("talkToAggie", t.player.talk_to, "aggie", 1)
        t.exec("talkToAggie-dialog", t.chat.play, {
            "npc:What can I help you with?",
            "choose:Could I ask you about being a character witness?",
            "player:Could I ask you about being a character witness?",
            "npc:Not at the minute I'm afraid",
            "choose:Let me guess, you're going to ask me to do you a favour?",
            "player:Let me guess, you're going to ask me to do you a favour?",
            "npc:Would you my dear",
            "player:Hmm, I seem to have heard that one before.",
            "npc:Could you go on and check out that abandoned building",
            "choose:Oh, Ok, I'll see if I can find Jimmy.",
            "player:Oh, Ok, I'll see if I can find Jimmy.",
            "npc:Oh, thanks ever so much",
        })
        t.expect("quest.stage.aggie_told", t.quest.expect_stage("aggie_told"))
        aggie_out("talkToAggie")

        -- === Johanhus Ulsbrecht (H.A.M. hideout) ============================
        -- quests/quest_deathtothedorgeshuun/scripts/dttd_haminfiltrate.rs2:74-83.
        -- The trapdoor is picked (op 5) and climbed (losttribe_ham.rs2: lands
        -- ^lt_ham_trapdoor_in, 3149,9652).
        t.exec("goto-hamTrapdoor", t.player.goto_tile, 3166, 3256, 0)
        t.exec("hamPickLock", t.player.click_loc, "osf_trapdoor_closed", 5)
        t.ticks(4)
        transit("goDownToJohanhus", "osf_trapdoor_open", 1, { 3166, 3252, 0 },
            function(tt) return in_ham_lair(tt) and tt.x == 3149 and tt.z == 9652 end,
            "in the hideout on ^lt_ham_trapdoor_in, 3149,9652,0")
        johanhus_in("talkToJohanhus")
        t.exec("talkToJohanhus", t.player.talk_to, "favour_johanhus_ulsbrecht", 1)
        t.exec("talkToJohanhus-dialog", t.chat.play, {
            "player:I'm looking for Jimmy the Chisel.",
            "npc:Jimmy? He owes us a debt he hasn't paid.",
            "player:And I suppose you need me to do you a favour?",
            "npc:As it happens",
            "player:Ok, Jimmy has to be worth more than a few scrawny chickens!",
            "npc:Take it or leave it.",
        })
        t.expect("quest.stage.johanhus_told", t.quest.expect_stage("johanhus_told"))
        johanhus_out("talkToJohanhus")

        -- === Fred the Farmer (north of Lumbridge) ==========================
        -- areas/lumbridge/scripts/fred_the_farmer.rs2:27-32.
        fred_in("talkToFred")
        t.exec("talkToFred", t.player.talk_to, "fred_the_farmer", 1)
        t.exec("talkToFred-dialog", t.chat.play, {
            "player:I need to talk to you about Jimmy.",
            "npc:Jimmy the dwarf? Not my business",
        })
        t.expect("quest.stage.fred_told", t.quest.expect_stage("fred_told"))
        fred_out("talkToFred")

        -- === Seth Groats (farm north east of Lumbridge) ====================
        -- quests/quest_idesofmilk/scripts/idesofmilk.rs2:142-151.
        seth_in("talkToSeth")
        t.exec("talkToSeth", t.player.talk_to, "favour_seth_groats", 1)
        t.exec("talkToSeth-dialog", t.chat.play, {
            "player:Fred said you might be able to help with some chickens.",
            "npc:Chickens? My coops are falling apart",
            "player:Oh, ok! I guess it's not that much further to Varrock!",
        })
        t.expect("quest.stage.seth_told", t.quest.expect_stage("seth_told"))
        seth_out("talkToSeth")

        -- === Horvik the armourer (Varrock) ==================================
        -- areas/varrock/scripts/horvik.rs2: Seth's three steel bars pay his
        -- debt (Quest Helper talkToHorvik requires steelBars3), then the
        -- medicine favour and the five pigeon cages (wiki Transcript "Asking
        -- Horvik about chicken cages"). His shop has no door (reach.py: REACH
        -- with every door closed).
        local bars0_r, bars0 = t.inv.count("steel_bar")
        t.exec("goto-talkToHorvik", t.player.goto_tile, 3229, 3438, 0)
        t.exec("talkToHorvik", t.player.talk_to, "horvik_the_armourer", 1)
        t.exec("talkToHorvik-dialog", t.chat.play, {
            "player:Hi, I need to talk to you about chicken cages!",
            "npc:Hmm, Seth eh!",
            "player:Ok, well I have three steel bars here",
            "npc:Ok then! Great",
            "player:Oh dear, you don't sound too well?",
            "npc:No, I'm not actually",
            "player:Well that's a shame",
            "npc:I'm sorry, but the most I can manage",
            "player:Oh I see, you need me to do you a favour?",
            "npc:Well, just one small favour",
            "choose:Ok, I guess one good turn deserves another.",
            "player:Ok, I guess one good turn deserves another.",
            "npc:Well that's jolly decent of you",
            "player:Well, hopefully, I'll be right back",
            "npc:it'll be a lot easier for me to simply adjust some existing pigeon cages",
            "npc:But first things first, bring me the medicine!",
        })
        t.expect("quest.stage.horvik_told", t.quest.expect_stage("horvik_told"))
        local bars1_r, bars1 = t.inv.count("steel_bar")
        t.check("talkToHorvik.bars", bars0_r == "ok" and bars1_r == "ok" and bars0 - bars1 == 3,
            "steel bars " .. tostring(bars0) .. " -> " .. tostring(bars1) .. " (want three handed to Horvik)")

        -- === Apothecary (west Varrock) ======================================
        -- areas/varrock/scripts/apothecary.rs2:18-24. (Not the Cadava-potion
        -- branch further down that file -- that is gated on %rjquest, a
        -- different quest's own stage, and never fires here.)
        t.exec("goto-talkToApoth", t.player.goto_tile, 3195, 3404, 0)
        t.exec("talkToApoth", t.player.talk_to, "apothecary", 1)
        t.exec("talkToApoth-dialog", t.chat.play, {
            "player:Talk about One Small Favour.",
            "npc:One Small Favour, is it?",
            "player:Oh, ok, I guess it's not that far to the Barbarian Village.",
            "player:I guess I can go to the Barbarian Village.",
        })
        t.expect("quest.stage.apoth_told", t.quest.expect_stage("apoth_told"))

        -- === Tassie Slipcast (Barbarian Village pottery) ====================
        -- onesmallfavour_relay.rs2:43-51.
        t.exec("goto-talkToTassie", t.player.goto_tile, 3084, 3408, 0)
        t.exec("talkToTassie", t.player.talk_to, "favour_tassie_slipcast", 1)
        t.exec("talkToTassie-dialog", t.chat.play, {
            "player:The Apothecary sent me. He says you're the one to speak to about the Dwarven Mine.",
            "npc:Ugh, don't remind me.",
            "player:Ok, I'll deal with Hammerspike!",
            "npc:Would you? He's in the west cavern",
        })
        t.expect("quest.stage.tassie_told", t.quest.expect_stage("tassie_told"))

        -- === Hammerspike Stoutbeard (Dwarven Mine, west cavern) =============
        -- onesmallfavour_relay.rs2:68-77. Down the Ice Mountain trapdoor
        -- (the guide's goDownToHammerspike), then one open passage to the
        -- west cavern (reach.py: REACH, every door closed).
        mine_down("goDownToHammerspike")
        t.exec("goto-talkToHammerspike", t.player.goto_tile, 2965, 9810, 0)
        t.exec("talkToHammerspike", t.player.talk_to, "favour_hammerspike_stoutbeard", 1)
        t.exec("talkToHammerspike-dialog", t.chat.play, {
            "player:Have you always been a gangster?",
            "npc:Ha! I run this cavern.",
            "player:She'd like you and your gang to leave the potters alone.",
            "npc:That's a lot to ask for nothing.",
            "player:Ok, another favour",
            "npc:Good. Sanfew up in Taverley owes me",
        })
        t.expect("quest.stage.hammerspike_told", t.quest.expect_stage("hammerspike_told"))

        -- === Sanfew (Taverley herblore store, upstairs) =====================
        -- areas/area_taverly/scripts/sanfew.rs2:12-19.
        mine_up("leaveMine")
        taverley_in("talkToSanfew")
        sanfew_up("talkToSanfew", "goUpToSanfew")
        t.exec("talkToSanfew", t.player.talk_to, "sanfew", 1)
        t.exec("talkToSanfew-dialog", t.chat.play, {
            "player:Are you taking any new initiates?",
            "npc:Perhaps, if the applicant showed promise.",
            "player:Do you accept dwarves?",
            "player:A dwarf I know wants to become an initiate.",
            "npc:Hmm. Tell you what",
            "npc:- brew him a cup of Guthix rest",
            "player:Yep, it's a deal.",
        })
        t.expect("quest.stage.sanfew_told", t.quest.expect_stage("sanfew_told"))
        sanfew_down("talkToSanfew")

        -- === Guthix rest tea + Captain Bleemadge (White Wolf Mountain) =====
        -- gnome_glider.rs2:27-29 hands %onesmallfavour 75..86 and 190 to
        -- onesmallfavour_relay.rs2's [label,osf_bleemadge_talk]. At 75 the
        -- first talk only sets osf_brewing_tea (relay.rs2:172-178); the tea
        -- is handed over on a SECOND talk (relay.rs2:180-191). Taverley to the
        -- glider is open mountain path (reach.py: REACH, 159 tiles).
        t.exec("goto-talkToBleemadge", t.player.goto_tile, 2846, 3497, 0)
        t.ticks(3)
        t.exec("meetBleemadge", t.player.talk_to, "pilot_white_wolf", 1)
        t.exec("meetBleemadge-dialog", t.chat.play, {
            "player:Right-o, Captain Bleemadge?",
            "npc:That's me! You after a lift, gnome-friend?",
            "player:Sanfew sent me.",
            "npc:Sanfew, eh?",
        })
        t.expect("quest.stage.brewing_tea", t.quest.expect_stage("brewing_tea"))

        -- onesmallfavour_puzzles.rs2:163-172 (bowl of hot water on the cup)
        -- then brew_potion.rs2:90 -> ~osf_brew_tea (puzzles.rs2:196-233): the
        -- bowl and the cup leave, the herbs leave, the tea arrives.
        t.exec("useBowlOnCup", t.player.use_item_on_item, "bowl_hot_water", "cup_empty")
        pack_after("useBowlOnCup.cup", "cup_hot_water", 1,
            { { "cup_hot_water", 1 }, { "bowl_empty", 1 }, { "cup_empty", 0 }, { "bowl_hot_water", 0 } })
        t.exec("useHerbsOnCup", t.player.use_item_on_item, "guam_leaf", "cup_hot_water")
        pack_after("makeGuthixRest", "cup_guthix_rest_3", 1, { { "cup_guthix_rest_3", 1 }, { "cup_hot_water", 0 },
            { "guam_leaf", 0 }, { "marentill", 0 }, { "harralander", 0 } })

        t.exec("talkToBleemadge", t.player.talk_to, "pilot_white_wolf", 1)
        t.exec("talkToBleemadge-dialog", t.chat.play, {
            "player:I have a special tea here for you from Sanfew!",
            "npc:Ah, lovely, just what the herblorist ordered.",
            "npc:Much better already!",
        })
        t.expect("quest.stage.bleemadge_wants_trash", t.quest.expect_stage("bleemadge_wants_trash"))
        pack_after("talkToBleemadge.tea", nil, nil, { { "cup_guthix_rest_3", 0 } })

        -- === Arhein (Catherby) -- arhein.rs2:21-26 ==========================
        t.exec("goto-talkToArhein", t.player.goto_tile, 2804, 3431, 0)
        t.exec("talkToArhein", t.player.talk_to, "arhein", 1)
        t.exec("talkToArhein-dialog", t.chat.play, {
            "player:I need to talk T.R.A.S.H. to you.",
            "npc:T.R.A.S.H.? Captain Bleemadge sent you",
            "player:Yes, Ok, I'll do it!",
        })
        t.expect("quest.stage.arhein_told", t.quest.expect_stage("arhein_told"))

        -- === Phantuwti Farsight (Seers' Village) -- relay.rs2:211-220 =======
        phantuwti_in("talkToPhantuwti")
        t.exec("talkToPhantuwti", t.player.talk_to, "favour_phantuwti_farsight", 1)
        t.exec("talkToPhantuwti-dialog", t.chat.play, {
            "player:Hi, can you give me a weather forecast?",
            "npc:I would, if my seeing-tools",
            "player:What can I do to help?",
            "npc:Find Petra",
            "player:Yes, Ok, I'll do it.",
        })
        t.expect("quest.stage.phantuwti_told", t.quest.expect_stage("phantuwti_told"))
        phantuwti_out("talkToPhantuwti")

        -- === Goblin cave sculpture -- mcannon_cave_guard.rs2:8-14 (enter),
        -- onesmallfavour_puzzles.rs2:11-16 (search) ==========================
        cave_in("enterGoblinCave")
        t.exec("goto-searchWall", t.player.goto_tile, 2620, 9834, 0)
        t.exec("searchWall", t.player.click_loc, "favour_lady_in_wall", 1)
        t.exec("searchWall-dialog", t.chat.play, { "mesbox:crude sculpture of a woman" })
        t.expect("quest.stage.wall_found", t.quest.expect_stage("wall_found"))
        cave_out("leaveGoblinCave")

        -- === Wizard Cromperty (East Ardougne) -- wizard_cromperty.rs2:14-20
        cromperty_in("talkToCromperty")
        t.exec("talkToCromperty", t.player.talk_to, "ardounge_wizard", 1)
        t.exec("talkToCromperty-dialog", t.chat.play, {
            "player:Chat.",
            "player:I need to talk to you about a girl stuck in some rock!",
            "npc:Stuck in rock?",
            "player:Oh! One more 'small favour'",
        })
        t.expect("quest.stage.cromperty_told", t.quest.expect_stage("cromperty_told"))
        cromperty_out("talkToCromperty")

        -- === Tindel Marchant (Port Khazard) -- relay.rs2:253-262 ============
        t.exec("goto-talkToTindel", t.player.goto_tile, 2678, 3152, 0)
        t.exec("talkToTindel", t.player.talk_to, "tindel_marchant", 1)
        t.exec("talkToTindel-dialog", t.chat.play, {
            "player:Wizard Cromperty sent me to get some iron oxide.",
            "npc:Iron oxide!",
            "player:Ask about iron oxide.",
            "npc:Bring me back a proper comfy mattress",
            "player:Okay, I'll do it!",
        })
        t.expect("quest.stage.tindel_told", t.quest.expect_stage("tindel_told"))

        -- === Rantz (Feldip Hills) -- relay.rs2:286-293 ======================
        t.exec("goto-talkToRantz", t.player.goto_tile, 2630, 2980, 0)
        t.exec("talkToRantz", t.player.talk_to, "rantz", 1)
        t.exec("talkToRantz-dialog", t.chat.play, {
            "player:I need to talk to you about a mattress.",
            "npc:A mattress!",
            "player:Ok, I'll see what I can do.",
        })
        t.expect("quest.stage.rantz_told", t.quest.expect_stage("rantz_told"))

        -- === Gnormadium Avlafrim (glider strip) -- onesmallfavour_relay.rs2 ==
        -- Wiki Transcript "Helping Gnormadium with the gnome glider": his
        -- "Yes, I'll take a look at them." is 115 -> 120.
        t.exec("goto-talkToGnormadium", t.player.goto_tile, 2544, 2972, 0)
        t.exec("talkToGnormadium", t.player.talk_to, "gnormadium_avlafrim", 1)
        t.exec("talkToGnormadium-dialog", t.chat.play, {
            "player:Rantz said I should help you finish this project.",
            "npc:Rantz? *gulp*",
            "npc:I expect that it's far too complex for you",
            "choose:Yes, I'll take a look at them.",
            "player:Yes, I'll take a look at them.",
            "npc:Ok then, just pop over",
            "player:We'll see!",
        })
        t.expect("quest.stage.lights_repairing", t.quest.expect_stage("lights_repairing"))

        -- fixAllLamps: Quest Helper's take1..take8 / cutSaph / put1..put8 on
        -- the eight osf_multi_landinglight_* copies (maps/m39_46.jl2; row 1 at
        -- z 2974, row 2 at z 2969). Search takes the uncut gem (the cache's
        -- checklandinglights bit), the cut gem used on the light places it
        -- (fixedlandinglights). The two uncut sapphires are cut here with the
        -- chisel; the jade/opal/red topaz are the recommended cut ones.
        local lights = {
            { "osf_multi_landinglight_jade_1", 2554, 2974, "uncut jade", "jade" },
            { "osf_multi_landinglight_redtopaz_1", 2551, 2974, "uncut red topaz", "red_topaz" },
            { "osf_multi_landinglight_opal_1", 2548, 2974, "uncut opal", "opal" },
            { "osf_multi_landinglight_sapphire_1", 2545, 2974, "uncut sapphire", "sapphire" },
            { "osf_multi_landinglight_jade_1", 2554, 2969, "uncut jade", "jade" },
            { "osf_multi_landinglight_redtopaz_1", 2551, 2969, "uncut red topaz", "red_topaz" },
            { "osf_multi_landinglight_opal_1", 2548, 2969, "uncut opal", "opal" },
            { "osf_multi_landinglight_sapphire_1", 2545, 2969, "uncut sapphire", "sapphire" },
        }
        for i = 1, #lights do
            local light = lights[i]
            local step = "take" .. i
            t.exec(step, t.player.click_loc, light[1], 1, { at = { light[2], light[3] } })
            t.exec(step .. "-gem", t.chat.expect_text, "You find an " .. light[4] .. " in the landing light.")
            t.exec(step .. "-dialog", t.chat.play, { "*" })
            if light[5] == "sapphire" then
                t.exec("cutSaph-" .. i, t.player.use_item_on_item, "chisel", "uncut_sapphire")
                t.exec("cutSaph-" .. i .. ".cut", t.inv.await, "sapphire", 1, 10)
            end
            local gems0_r, gems0 = t.inv.count(light[5])
            step = "put" .. i
            t.exec(step, t.player.use_on, light[5], t.player.by_symbol("loc", light[1]),
                { at = { light[2], light[3] } })
            t.exec(step .. "-placed", t.chat.expect_text, "It seems to look right.")
            local gems1_r, gems1 = t.inv.count(light[5])
            t.check(step .. ".gemUsed", gems0_r == "ok" and gems1_r == "ok" and gems0 - gems1 == 1,
                light[5] .. " in the pack " .. tostring(gems0) .. " -> " .. tostring(gems1) .. " (want one set into the light)")
            local tally = i == #lights and "mesbox:You've fixed all the landing lights!"
                or (i == 1 and "mesbox:You've fixed one landing light so far..."
                    or ("mesbox:You've fixed " .. i .. " landing lights so far..."))
            t.exec(step .. "-dialog", t.chat.play, { "*", tally })
        end
        t.expect("quest.stage.lights_fixed", t.quest.expect_stage("lights_fixed"))
        t.exec("fixAllLamps", t.var.await_server, "varb6241_fixedlandinglights", 255, 5)

        -- "I've fixed all the lights!": Gnormadium flicks all_lights_fixed.
        t.exec("talkToGnormadiumAgain", t.player.talk_to, "gnormadium_avlafrim", 1)
        t.exec("talkToGnormadiumAgain-dialog", t.chat.play, {
            "npc:Hello! Don't get in the way around here",
            "player:I've fixed all the lights!",
            "npc:Hmm. That seems a tad unlikely",
            "npc:I don't believe it - you fixed it!",
            "player:I know one ogre who'll be very pleased",
        })
        t.expect("quest.stage.gnormadium_done", t.quest.expect_stage("gnormadium_done"))
        t.exec("talkToGnormadiumAgain.lit", t.var.await_server, "varb256_all_lights_fixed", 1, 5)

        -- === Rantz, Tindel, Cromperty again =================================
        t.exec("goto-returnToRantz", t.player.goto_tile, 2630, 2980, 0)
        t.exec("returnToRantz", t.player.talk_to, "rantz", 1)
        t.exec("returnToRantz-dialog", t.chat.play, {
            "player:Ok, I've helped that Gnome",
            "npc:Splendid! Let's see to that mattress, then.",
        })
        t.expect("quest.stage.rantz_done", t.quest.expect_stage("rantz_done"))
        t.exec("returnToRantz.mattress", t.inv.await, "favour_matress_comfy", 1, 10)

        t.exec("goto-returnToTindel", t.player.goto_tile, 2678, 3152, 0)
        t.exec("returnToTindel", t.player.talk_to, "tindel_marchant", 1)
        t.exec("returnToTindel-dialog", t.chat.play, {
            "player:I have the mattress.",
            "npc:Ahh, lovely and comfy.",
        })
        t.expect("quest.stage.tindel_done", t.quest.expect_stage("tindel_done"))
        pack_after("returnToTindel.oxide", "favour_iron_oxide", 1, { { "favour_iron_oxide", 1 }, { "favour_matress_comfy", 0 } })

        cromperty_in("returnToCromperty")
        t.exec("returnToCromperty", t.player.talk_to, "ardounge_wizard", 1)
        t.exec("returnToCromperty-dialog", t.chat.play, {
            "player:I have that iron oxide you asked for!",
            "npc:Marvellous! Here's your animate rock scroll",
        })
        t.expect("quest.stage.cromperty_done", t.quest.expect_stage("cromperty_done"))
        pack_after("returnToCromperty.scroll", "favour_animate_rock", 1, { { "favour_animate_rock", 1 }, { "favour_iron_oxide", 0 } })
        cromperty_out("returnToCromperty")

        -- === Pigeon cages behind Jerico's house =============================
        -- Three `pigeons` ground spawns (areas/world/configs/m40_51.spawn:116-118);
        -- the guide wants five, so the loop waits for the respawn. Horvik
        -- converts them into the chicken cages (horvik.rs2, talkToHorvikFinal).
        -- Jerico's garden is open ground (reach.py: REACH from Cromperty's
        -- door, every door closed).
        t.exec("goto-getPigeonCages", t.player.goto_tile, 2619, 3324, 0)
        for cage = 1, 5 do
            local seen = t.await({
                level = function()
                    return t.world.obj_near("pigeons", 4) == "ok"
                end,
                note = "pigeon cage on the ground",
            }, 300)
            if seen == "ok" then
                t.player.click_obj("pigeons", 3)
                t.inv.await("pigeons", cage, 10)
            end
        end
        local cages_r, cages = t.inv.count("pigeons")
        t.check("getPigeonCages", cages_r == "ok" and cages >= 5,
            "pigeon cages carried after the pickup loop: " .. tostring(cages) .. " (need 5)")

        -- === Slagilith and Petra -- onesmallfavour_puzzles.rs2:27-64,
        -- onesmallfavour_relay.rs2:366-425 ====================================
        cave_in("enterGoblinCaveAgain")
        t.exec("goto-standNextToSculpture", t.player.goto_tile, 2617, 9835, 0)
        t.exec("standNextToSculpture", t.player.use_on, "favour_animate_rock",
            t.player.by_symbol("loc", "favour_lady_in_wall"))
        t.exec("standNextToSculpture-dialog", t.chat.play, { "mesbox:You read the animate rock scroll aloud." })
        t.expect("quest.stage.slagilith_fight", t.quest.expect_stage("slagilith_fight"))
        t.exec("killSlagilith", t.player.attack, "slagilith", 2, 15)
        local _, slag_detail = t.exec("killSlagilith.dead", t.npc.await_dead_engaged, 200, 8, EAT)
        t.exec("killSlagilith.stage", t.var.await_server, "varp416_onesmallfavour", 150, 15)
        margin("killSlagilith.margin", { slag_detail }, 4)
        t.exec("readScrollAgain", t.player.use_on, "favour_animate_rock",
            t.player.by_symbol("loc", "favour_lady_in_wall"))
        t.exec("readScrollAgain-dialog", t.chat.play, { "mesbox:This time the spell strikes the sculpture." })
        t.expect("quest.stage.petra_freed", t.quest.expect_stage("petra_freed"))
        t.ticks(2)
        t.exec("talkToPetra", t.player.talk_to, "favour_petra", 1)
        t.exec("talkToPetra-dialog", t.chat.play, {
            "player:Are you alright? Phantuwti sent me to find you.",
            "npc:Oh, thank the gods!",
            "player:It's dealt with now.",
            "npc:I will, right away.",
        })
        t.expect("quest.stage.phantuwti_weather", t.quest.expect_stage("phantuwti_weather"))
        cave_out("leaveGoblinCaveAgain")

        -- === Phantuwti, then the weathervane ================================
        phantuwti_in("returnToPhantuwti")
        t.exec("returnToPhantuwti", t.player.talk_to, "favour_phantuwti_farsight", 1)
        t.exec("returnToPhantuwti-dialog", t.chat.play, {
            "player:I've released Petra, she should have returned.",
            "npc:She has! Thank you.",
        })
        t.expect("quest.stage.vane_search", t.quest.expect_stage("vane_search"))

        -- Up to the roof (Quest Helper goUpLadder, goUpToRoof): the house
        -- ladder, the two level-1 doors, the roof ladder.
        house_up("goUpLadder")
        to_roof_ladder("goUpToRoof")
        roof_up("goUpToRoof")

        -- The weathervane (onesmallfavour_puzzles.rs2): search it (op 5,
        -- "Search"), use the hammer on it, search it again for the three
        -- broken parts -- Quest Helper searchVane / useHammerOnVane /
        -- searchVaneAgain, stages 175 / 176 / 177; texts from the wiki
        -- Transcript "Weather vane".
        t.exec("searchVane", t.player.click_loc, "osf_weathervane", 5)
        t.exec("searchVane-dialog", t.chat.play, { "mesbox:You search the weather vane..." })
        t.expect("quest.stage.vane_searched", t.quest.expect_stage("vane_searched"))
        t.exec("useHammerOnVane", t.player.use_on, "hammer", t.player.by_symbol("loc", "osf_weathervane"))
        t.exec("useHammerOnVane-dialog", t.chat.play, { "mesbox:You give the structure a good solid whack..." })
        t.expect("quest.stage.vane_loosened", t.quest.expect_stage("vane_loosened"))
        t.exec("searchVaneAgain", t.player.click_loc, "osf_weathervane", 5)
        t.exec("searchVaneAgain-ornament", t.chat.expect_text, "You find a broken ornament...")
        t.exec("searchVaneAgain-p1", t.chat.play, { "*" })
        t.exec("searchVaneAgain-directionals", t.chat.expect_text, "broken directionals...")
        t.exec("searchVaneAgain-p2", t.chat.play, { "*" })
        t.exec("searchVaneAgain-pillar", t.chat.expect_text, "and a broken rotating pillar.")
        t.exec("searchVaneAgain-p3", t.chat.play, { "*" })
        t.expect("quest.stage.vane_parts_taken", t.quest.expect_stage("vane_parts_taken"))
        t.exec("searchVaneAgain.parts", t.inv.await_all,
            { favour_ornament_broken = 1, favour_directionals_broken = 1, favour_pillar_broken = 1 }, 10)

        -- Down to Seers' anvils (Quest Helper goDownFromRoof,
        -- goDownLadderToSeers): the roof trapdoor, the doors, the house
        -- ladder, the front door.
        t.exec("goDownFromRoof.walk", t.player.walk_route, { { 2706, 3472 }, { 2714, 3472 } }, { level = 3 })
        roof_down("goDownFromRoof")
        from_roof_ladder("goDownFromRoof")
        t.exec("goDownLadderToSeers.walk", t.player.walk_route, { { 2700, 3475 } }, { level = 1 })
        house_down("goDownLadderToSeers")
        phantuwti_out("goDownLadderToSeers")

        -- Seers' anvils (2712,3495 and 2713,3492, open ground): each broken
        -- part on the anvil uses its own bar (puzzles.rs2:74-99).
        t.exec("goto-useVane123OnAnvil", t.player.goto_tile, 2712, 3494, 0)
        local anvil = t.player.by_symbol("loc", "anvil")
        t.exec("useVane123OnAnvil", t.player.use_on, "favour_directionals_broken", anvil)
        pack_after("useVane123OnAnvil.directionals", "favour_directionals_fixed", 1,
            { { "favour_directionals_fixed", 1 }, { "favour_directionals_broken", 0 }, { "steel_bar", 0 } })
        t.exec("useVane123OnAnvil-ornament", t.player.use_on, "favour_ornament_broken", anvil)
        pack_after("useVane123OnAnvil.ornament", "favour_ornament_fixed", 1,
            { { "favour_ornament_fixed", 1 }, { "favour_ornament_broken", 0 }, { "bronze_bar", 0 } })
        t.exec("useVane123OnAnvil-pillar", t.player.use_on, "favour_pillar_broken", anvil)
        pack_after("useVane123OnAnvil.pillar", "favour_pillar_fixed", 1,
            { { "favour_pillar_fixed", 1 }, { "favour_pillar_broken", 0 }, { "iron_bar", 0 } })

        -- Back up (Quest Helper goBackUpToRoof).
        phantuwti_in("goBackUpToRoof")
        house_up("goBackUpToRoof.houseLadder")
        to_roof_ladder("goBackUpToRoof")
        roof_up("goBackUpToRoof")
        t.exec("goBackUpToRoof.walk", t.player.walk_route, { { 2706, 3472 }, { 2702, 3474 } }, { level = 3 })
        local vane = t.player.by_symbol("loc", "osf_weathervane")
        t.exec("useVane1", t.player.use_on, "favour_ornament_fixed", vane)
        t.exec("useVane1-dialog", t.chat.play, { "mesbox:You slot the ornament back into the housing." })
        t.exec("useVane2", t.player.use_on, "favour_directionals_fixed", vane)
        t.exec("useVane2-dialog", t.chat.play, { "mesbox:You slot the directionals back into the housing." })
        t.exec("useVane3", t.player.use_on, "favour_pillar_fixed", vane)
        t.exec("useVane3-dialog", t.chat.play, {
            "mesbox:You slot the rotating pillar back into the housing.",
            "mesbox:With the last part in place",
        })
        t.expect("quest.stage.vane_repaired", t.quest.expect_stage("vane_repaired"))
        pack_after("useVane3.parts", nil, nil,
            { { "favour_ornament_fixed", 0 }, { "favour_directionals_fixed", 0 }, { "favour_pillar_fixed", 0 } })

        -- Down to Phantuwti (Quest Helper goFromRoofToPhantuwti,
        -- goDownLadderToPhantuwti).
        t.exec("goFromRoofToPhantuwti.walk", t.player.walk_route, { { 2706, 3472 }, { 2714, 3472 } }, { level = 3 })
        roof_down("goFromRoofToPhantuwti")
        from_roof_ladder("goFromRoofToPhantuwti")
        t.exec("goDownLadderToPhantuwti.walk", t.player.walk_route, { { 2700, 3475 } }, { level = 1 })
        house_down("goDownLadderToPhantuwti")
        t.exec("finishWithPhantuwti", t.player.talk_to, "favour_phantuwti_farsight", 1)
        t.exec("finishWithPhantuwti-dialog", t.chat.play, {
            "player:I've fixed the weather vane!",
            "npc:Marvellous! Here's your weather report",
        })
        t.expect("quest.stage.phantuwti_vane_done", t.quest.expect_stage("phantuwti_vane_done"))
        t.exec("finishWithPhantuwti.report", t.inv.await, "favour_weather_report", 1, 10)
        phantuwti_out("finishWithPhantuwti")

        -- === The return legs: Arhein, Bleemadge, Sanfew =====================
        t.exec("goto-returnToArhein", t.player.goto_tile, 2804, 3431, 0)
        t.exec("returnToArhein", t.player.talk_to, "arhein", 1)
        t.exec("returnToArhein-dialog", t.chat.play, {
            "player:What did you want me to do again?",
            "player:I have the weather report for you.",
            "npc:Splendid!",
        })
        t.expect("quest.stage.arhein_done", t.quest.expect_stage("arhein_done"))

        t.exec("goto-returnToBleemadge", t.player.goto_tile, 2846, 3497, 0)
        t.ticks(3)
        t.exec("returnToBleemadge", t.player.talk_to, "pilot_white_wolf", 1)
        t.exec("returnToBleemadge-dialog", t.chat.play, {
            "player:Hey there, did you get your T.R.A.S.H?",
            "npc:That I did!",
        })
        t.expect("quest.stage.bleemadge_done", t.quest.expect_stage("bleemadge_done"))

        -- White Wolf Mountain down into Taverley is open path (reach.py).
        sanfew_up("returnToSanfew", "returnUpToSanfew")
        t.exec("returnToSanfew", t.player.talk_to, "sanfew", 1)
        t.exec("returnToSanfew-dialog", t.chat.play, {
            "player:Hi there, the Gnome Pilot has agreed to take you to see the ogres!",
            "npc:Excellent! A deal's a deal",
        })
        t.expect("quest.stage.sanfew_done", t.quest.expect_stage("sanfew_done"))
        sanfew_down("returnToSanfew")

        -- === Hammerspike and his gang -- relay.rs2:79-162 ===================
        taverley_out("returnToHammerspike")
        mine_down("goDownToHammerspikeAgain")
        t.exec("goto-returnToHammerspike", t.player.goto_tile, 2965, 9810, 0)
        t.exec("returnToHammerspike", t.player.talk_to, "favour_hammerspike_stoutbeard", 1)
        t.exec("returnToHammerspike-dialog", t.chat.play, { "npc:Sanfew took you seriously, did he?" })
        t.expect("quest.stage.hammerspike_gang", t.quest.expect_stage("hammerspike_gang"))
        local sharks_gang = select(2, t.inv.count("shark"))
        t.exec("killGangMembers", t.player.attack, "favour_gangster_dwarf", 2, 15)
        local _, gang1 = t.exec("killGangMembers.dead1", t.npc.await_dead_engaged, 150, 8, EAT)
        t.exec("killGangMembers-2", t.player.attack, "favour_gangster_dwarf_2", 2, 15)
        local _, gang2 = t.exec("killGangMembers.dead2", t.npc.await_dead_engaged, 150, 8, EAT)
        t.exec("killGangMembers-3", t.player.attack, "favour_gangster_dwarf_3", 2, 15)
        local _, gang3 = t.exec("killGangMembers.dead3", t.npc.await_dead_engaged, 150, 8, EAT)
        margin("killGangMembers.margin", { gang1, gang2, gang3 }, tostring(sharks_gang) .. " left after Slagilith (4)")
        t.exec("quest.stage.hammerspike_done", t.var.await_server, "varp416_onesmallfavour", 225, 15)
        t.exec("talkToHammerspikeFinal", t.player.talk_to, "favour_hammerspike_stoutbeard", 1)
        t.exec("talkToHammerspikeFinal-dialog", t.chat.play, { "npc:Alright, alright! You've made your point." })
        mine_up("leaveMineAgain")

        -- === Tassie, the pot lid, the Apothecary ============================
        -- Tassie gives the soft clay and teaches pot lids (wiki Transcript
        -- "Tassie"; walkthrough "Tassie, who will give you some soft clay");
        -- the pot is the one in the Barbarian Village helmet shop (Quest
        -- Helper pickUpPot; areas/world/configs/m48_53.spawn pot_empty
        -- 3074,3431).
        t.exec("goto-returnToTassie", t.player.goto_tile, 3085, 3408, 0)
        t.exec("returnToTassie", t.player.talk_to, "favour_tassie_slipcast", 1)
        t.exec("returnToTassie-dialog", t.chat.play, {
            "player:Hey there, Hammerspike won't be bothering you anymore!",
            "npc:Really! Fantastic!",
            "player:Well you could make me an airtight pot!",
            "npc:I'll do better than that!",
            "npc:Now, while pots are quite easy to make",
        })
        t.exec("returnToTassie-clay", t.chat.expect_text, "Tassie gives you some clay!")
        t.exec("returnToTassie-dialog2", t.chat.play, { "*", "npc:Ok then, just use it on the wheel over there!" })
        t.exec("returnToTassie-lids", t.chat.expect_text, "Tassie shows you how to make pot lids.")
        t.exec("returnToTassie-dialog3", t.chat.play, { "*" })
        t.expect("quest.stage.tassie_done", t.quest.expect_stage("tassie_done"))
        t.exec("returnToTassie.clay", t.inv.await, "softclay", 1, 10)

        t.exec("spinPotLid", t.player.use_on, "softclay", t.player.by_symbol("loc", "potterywheel"))
        t.exec("spinPotLid.menu", t.ui.await_open, "skillmulti", 10)
        local cell_r, cell = t.ui.widget("skillmulti:f")
        -- The pot-lid cell's press answers no detail of its own; the row
        -- that proves it is spinPotLid.made (the clay left, the lid came).
        local press_r = t.ui.invoke(cell, 1)
        t.note("skillmulti cell f=" .. tostring(cell_r) .. "/" .. txt(cell) .. " invoke=" .. tostring(press_r))
        pack_after("spinPotLid.made", "potlid_unfired", 1, { { "potlid_unfired", 1 }, { "softclay", 0 } })
        t.exec("firePotLid", t.player.use_on, "potlid_unfired",
            t.player.by_symbol("loc", "fai_barbarian_pottery_oven"))
        pack_after("firePotLid.fired", "potlid", 1, { { "potlid", 1 }, { "potlid_unfired", 0 } })
        t.exec("goto-pickUpPot", t.player.goto_tile, 3074, 3430, 0)
        t.exec("pickUpPot", t.player.click_obj, "pot_empty", 3)
        t.exec("pickUpPot.pot", t.inv.await, "pot_empty", 1, 10)
        t.exec("usePotLidOnPot", t.player.use_item_on_item, "potlid", "pot_empty")
        pack_after("usePotLidOnPot.pot", "favour_airtight_pot", 1, { { "favour_airtight_pot", 1 }, { "potlid", 0 }, { "pot_empty", 0 } })
        t.expect("quest.stage.pot_made", t.quest.expect_stage("pot_made"))

        t.exec("goto-returnToApothecary", t.player.goto_tile, 3195, 3404, 0)
        t.exec("returnToApothecary", t.player.talk_to, "apothecary", 1)
        t.exec("returnToApothecary-dialog", t.chat.play, {
            "player:Talk about One Small Favour.",
            "npc:An airtight pot, wonderful!",
        })
        t.exec("returnToApothecary.items", t.inv.await_all,
            { favour_breathing_salts = 1, favour_herbal_tincture = 1 }, 10)

        -- === Horvik, Seth, Johanhus, Aggie, Brian, the forester, Yanni ======
        -- Horvik: the medicine first (Quest Helper returnToHorvik), then the
        -- five pigeon cages become chicken cages (talkToHorvikFinal) -- wiki
        -- Transcript "Giving the items to Horkiv" / "Getting the chicken cages".
        t.exec("goto-returnToHorvik", t.player.goto_tile, 3229, 3437, 0)
        t.exec("returnToHorvik", t.player.talk_to, "horvik_the_armourer", 1)
        t.exec("returnToHorvik-dialog", t.chat.play, {
            "player:I have the tincture and the breathing salts.",
            "npc:Wonderful! That's just great! I just need the pigeon cages now.",
        })
        t.expect("quest.stage.horvik_medicine", t.quest.expect_stage("horvik_medicine"))
        t.exec("talkToHorvikFinal", t.player.talk_to, "horvik_the_armourer", 1)
        t.exec("talkToHorvikFinal-dialog", t.chat.play, {
            "player:I have the five pigeon cages you asked for!",
            "npc:Great stuff",
        })
        t.exec("talkToHorvikFinal-handover", t.chat.expect_text, "You hand over the pigeon cages.")
        t.exec("talkToHorvikFinal-dialog2", t.chat.play, {
            "*",
            "mesbox:Horvik works for sometime on the pigeon cages",
            "npc:There you go then! There's your chicken cages!",
        })
        t.expect("quest.stage.horvik_done", t.quest.expect_stage("horvik_done"))
        t.exec("talkToHorvikFinal.cages", t.inv.await, "favour_chicken_cage", 5, 10)

        seth_in("returnToSeth")
        t.exec("returnToSeth", t.player.talk_to, "favour_seth_groats", 1)
        t.exec("returnToSeth-dialog", t.chat.play, {
            "player:I have the chicken cages Horvik made up for you.",
            "npc:Perfect fit!",
        })
        t.expect("quest.stage.seth_done", t.quest.expect_stage("seth_done"))
        seth_out("returnToSeth")

        -- The trapdoor stays unlocked after the first pick (no op 5 again).
        t.exec("goto-returnHamTrapdoor", t.player.goto_tile, 3166, 3256, 0)
        transit("returnDownToJohnahus", "osf_trapdoor_open", 1, { 3166, 3252, 0 },
            function(tt) return in_ham_lair(tt) and tt.x == 3149 and tt.z == 9652 end,
            "in the hideout on ^lt_ham_trapdoor_in, 3149,9652,0")
        johanhus_in("returnToJohnahus")
        t.exec("returnToJohnahus", t.player.talk_to, "favour_johanhus_ulsbrecht", 1)
        t.exec("returnToJohnahus-dialog", t.chat.play, {
            "player:I have the chickens Seth Groats promised you.",
            "npc:You're in luck",
        })
        t.expect("quest.stage.johanhus_done", t.quest.expect_stage("johanhus_done"))
        johanhus_out("returnToJohnahus")

        aggie_in("returnToAggie")
        t.exec("returnToAggie", t.player.talk_to, "aggie", 1)
        t.exec("returnToAggie-dialog", t.chat.play, {
            "player:Good news! Jimmy has been released!",
            "npc:Wonderful! I'll go have a word with Brian myself.",
        })
        t.expect("quest.stage.aggie_done", t.quest.expect_stage("aggie_done"))
        aggie_out("returnToAggie")

        brian_in("returnToBrian")
        t.exec("returnToBrian", t.player.talk_to, "brian", 1)
        t.exec("returnToBrian-dialog", t.chat.play, {
            "player:I've returned with good news.",
            "npc:Aggie spoke up for me!",
        })
        t.expect("quest.stage.brian_done", t.quest.expect_stage("brian_done"))
        brian_out("returnToBrian")

        sail_to_musa("returnToForester")
        karamja_gate_west("returnToForester")
        t.exec("goto-returnToForester", t.player.goto_tile, 2759, 2945, 0)
        t.exec("returnToForester", t.player.talk_to, "jungleforester_f", 1)
        t.exec("returnToForester-dialog", t.chat.play, {
            "player:Good news, I have your sharpened axe!",
            "npc:Wonderful, thank you!",
        })
        t.expect("quest.stage.forester_done", t.quest.expect_stage("forester_done"))
        t.exec("returnToForester.log", t.inv.await, "favour_mahogany_log", 1, 10)

        cart_to_shilo("returnToYanni")
        t.exec("goto-returnToYanni", t.player.goto_tile, 2835, 2984, 0)
        t.exec("returnToYanni", t.player.talk_to, "shiloantiques", 1)
        t.exec("returnToYanni-dialog", t.chat.play, {
            "player:Here's the red mahogany you asked for.",
            "npc:Ah, perfect!",
        })
        t.exec("quest.stage.complete", t.var.await_server, "varp416_onesmallfavour", 285, 15)
        t.quest.expect_complete()
        -- Rewards (yanni_salika.rs2:33-37): two reward lamps and the steel key ring.
        t.exec("reward.thosf_reward_lamp", t.inv.expect_has, "thosf_reward_lamp", 2)
        t.exec("reward.favour_key_ring", t.inv.expect_has, "favour_key_ring", 1)
        pack_after("reward.mahoganyHandedIn", nil, nil, { { "favour_mahogany_log", 0 } })
        t.finish(0)
        return
    end,
}
