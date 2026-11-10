-- The Giant Dwarf: driven from the Quest Helper ladder (32 steps, 3 legs).
-- Coal and every ore are mined, sapphires bought, the bars smelted at Keldagrim's
-- furnace (no ::give of ores or bars).
--
-- Door rule (re-driven b62, docs/QUEST_ORCHESTRATOR.md standing rules): no goto_tile
-- enters or leaves a closed space. Keldagrim is a cave city: it is entered and left
-- only by what a player uses, every time, and every door, stair, tunnel, boat and
-- cart between the player and the target is pressed through the driver's verbs
-- (pass_door / cross_gate / climb / teleport_cast) or graded on the landing tile.
-- What stays a goto_tile is a hop between two open tiles of one walkable area:
-- the city's streets, the surface outdoors.
--   * The way in at the start: a Camelot Teleport from Lumbridge (staged: magic 45,
--     5 air + 1 law), the open hillside east of Rellekka,
--     `trollromance_stronghold_exit_tunnel` (betweenarock_travel.rs2:18) into the
--     troll room, `dwarf_cavewall_tunnel` (:42) onto the ferry bank 2838,10124, then
--     the Dwarven Boatman (gdwarf_start.rs2:64: the first trip lands in Veldaban's HQ).
--   * The way back in from the surface: the Grand Exchange trapdoor
--     (forget_keldagrim.rs2:9-19, lands in Veldaban's HQ), the HQ door out.
--   * Closed rooms in the city, each door pressed in and out: Veldaban's HQ
--     (dwarf_keldagrim_door 2827,10218), Blasidar's house with Riki
--     (dwarf_keldagrim_door_ornate 2906,10200), the library (dwarf_keldagrim_door
--     2863,10229), Dromund's house (dwarf_keldagrim_door_ornate 2837,10219; the right
--     boot is grabbed through the window from the street north of it).
--   * No ore rock stands in the city. Coal and copper are on the ferry bank (x
--     2835-2877, z 10116-10143, closed on every side): the city boatman
--     (keldagrim_travel.rs2:54, lands 2838,10127) and back with the mines boatman.
--     Tin and iron are in the dwarf-mine alcove: the ferry bank's Dwarven Ferryman
--     (5 coins, betweenarock_travel.rs2:63, lands 2823,10165) and back to the city
--     with the second ferryman (:90, lands 2865,10195).
--   * Out to the surface (Reldo): the Grand Exchange cart on track 3
--     (keldagrim_travel.rs2 keldagrim_cart_ride, 2923,10171 -> 3141,3504); Varrock
--     palace's east door (fai_varrock_castle_door 3217,3492) and Reldo's library
--     door (3210,3490), in and out.
--   * The consortium floor: the west-south wide stairs (dwarf_keldagrim_wide_stairs_lower
--     2863,10209,0 / _upper 2863,10209,1) by climb on every visit, up and down, including
--     every ore run. gdwarf_consortium.rs2:240-251 call ~climb(1)/~climb(-1), which lands on
--     the stair's maplink row: 2865,10209,0 <-> 2862,10209,1 (maplink.dbrow
--     maplink_0_44_159_49_33_up / maplink_1_44_159_46_33_down). The foot of the stairs is
--     the open street, walked off and graded after every descent.
--   * Runes: 5 air + 1 law for the one Camelot Teleport, 1 air + 1 law for each of at
--     most 12 Telekinetic Grab casts at the right boot.
return {
    id = "giantdwarf",
    fixture = "fresh_lumbridge.ini",
    max_frames = 360000, -- every ore run is a boat (and ferry) round trip plus the consortium stairs
    setup = {
        "::clearinv",
        "::setlevel crafting 12",
        "::setlevel firemaking 16",
        "::setlevel magic 45",
        "::setlevel thieving 20",
        "::setlevel mining 60",
        "::setlevel smithing 20",
        "::give rune_pickaxe 1",
        "::setlevel hitpoints 99",
        "::give logs 1",
        "::give tinderbox 1",
        "::give coins 2000",
        "::give lawrune 13",
        "::give airrune 17",
        "::give redberry_pie 1",
    },

    run = function(t)
        t.ticklog.start()
        t.quest.bind({
            varp = "varb571_giantdwarf_quest",
            constants = {
                not_started = 0, boat_ride = 5, see_blasidar = 10, items = 20,
                consortium = 30, meeting = 40, complete = 50,
            },
            row = "quest_thegiantdwarf",
            display = "The Giant Dwarf",
            points = 2,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        local function tile_text(r, tl)
            if r == "ok" and type(tl) == "table" then
                return tl.x .. "," .. tl.z .. "," .. tl.level
            end
            return tostring(r)
        end
        local function count(sym)
            local r, n = t.inv.count(sym)
            if r ~= "ok" then return nil end
            return n or 0
        end

        -- One walk inside one walkable area, graded on the tile it reached.
        local function walk(name, x, z, level, slack)
            slack = slack or 0
            local function there(tl)
                return tl.level == level and math.abs(tl.x - x) <= slack and math.abs(tl.z - z) <= slack
            end
            local wr, wd = t.player.walk_to(x, z, 140)
            local r, tl = t.world.tile()
            if not (r == "ok" and there(tl)) then
                t.ticks(2)
                wr, wd = t.player.walk_to(x, z, 140)
                r, tl = t.world.tile()
            end
            t.check(name, r == "ok" and there(tl),
                "walk_to " .. x .. "," .. z .. " -> " .. tostring(wr) .. " (" .. tostring(wd) .. "); at "
                    .. tile_text(r, tl) .. " (want within " .. slack .. " of " .. x .. "," .. z .. "," .. level .. ")")
        end

        -- A ride an npc gives (a boat, a ferry): talk, play the page, then the
        -- landing tile is the verdict.
        local function ride(name, npc, chat, landed_ok, want)
            local br, bt = t.world.tile()
            t.exec(name, t.player.talk_to, npc, 1)
            t.exec(name .. ".chat", t.chat.play, chat)
            t.await({
                level = function()
                    local r, tl = t.world.tile()
                    return r == "ok" and landed_ok(tl)
                end,
                note = name .. ": waiting for the landing",
            }, 12)
            local ar, at = t.world.tile()
            t.check(name .. ".landed", br == "ok" and not landed_ok(bt) and ar == "ok" and landed_ok(at),
                "from " .. tile_text(br, bt) .. " landed " .. tile_text(ar, at) .. " (want " .. want .. ")")
        end
        local function at_tile(x, z)
            return function(tl) return tl.level == 0 and tl.x == x and tl.z == z end
        end

        local ferry1_trips, ferry2_trips = 0, 0
        -- City dock -> the ferry bank (coal, copper).
        local function boat_to_bank(p)
            t.exec("goto-" .. p .. ".dock", t.player.goto_tile, 2888, 10225, 0)
            ride(p .. ".boatToBank", "dwarf_city_boatman_city", {
                "npc:Want me to take you back to the mines?",
                "choose:Yes, please take me.",
                "player:Yes, please take me.",
            }, at_tile(2838, 10127), "the ferry bank 2838,10127,0: keldagrim_travel.rs2:54")
        end
        -- The ferry bank -> the city dock (the mines boatman after the first trip).
        local function bank_to_city(p)
            walk(p .. ".toBoatman", 2840, 10128, 0, 1)
            ride(p .. ".boatToCity", "dwarf_city_boatman_mines", {
                "npc:Hello again",
                "choose:Yes, please take me.",
                "player:Yes, please take me.",
            }, at_tile(2892, 10225), "the Keldagrim dock 2892,10225,0: gdwarf_start.rs2:42")
        end
        -- The ferry bank -> the dwarf-mine alcove (tin, iron), 5 coins.
        local function bank_to_alcove(p)
            walk(p .. ".toFerryman", 2838, 10128, 0, 1)
            local c0 = count("coins")
            local chat
            if ferry1_trips == 0 then
                chat = { "player:Can you take me across the water?", "npc:Aye, but it'll cost you 5 coins", "choose:Yes please." }
            else
                chat = { "npc:Back again? That'll be another 5 coins", "choose:Yes please." }
            end
            ride(p .. ".ferryToAlcove", "dwarfrock_ferryman1", chat, at_tile(2823, 10165),
                "the alcove 2823,10165,0: betweenarock_travel.rs2:75")
            local c1 = count("coins")
            t.check(p .. ".ferryToAlcove.fare", c0 ~= nil and c1 ~= nil and c0 - c1 == 5,
                "coins " .. tostring(c0) .. " -> " .. tostring(c1) .. " (want -5, ^dwarfrock_ferry_toll)")
            ferry1_trips = ferry1_trips + 1
        end
        -- The alcove -> the city (the second ferryman on the jetty).
        local function alcove_to_city(p)
            walk(p .. ".toJetty", 2855, 10146, 0, 2)
            local chat
            if ferry2_trips == 0 then
                chat = { "player:Can you take me to Keldagrim?", "npc:Climb aboard" }
            else
                chat = { "npc:Heading back to Keldagrim?" }
            end
            ride(p .. ".ferryToCity", "dwarfrock_ferryman2", chat, at_tile(2865, 10195),
                "Keldagrim 2865,10195,0: betweenarock_travel.rs2:90")
            ferry2_trips = ferry2_trips + 1
        end

        -- The rocks a player can walk to: coal and copper on the ferry bank, tin and
        -- iron in the alcove (maps/m44_158.jl2).
        local ROCK = {
            coal = { a = "coalrock1", b = "coalrock2", x = 2864, z = 10121, bank = true },
            copper_ore = { a = "copperrock1", b = "copperrock2", x = 2872, z = 10116, bank = true },
            tin_ore = { a = "tinrock1", b = "tinrock2", x = 2856, z = 10161, bank = false },
            iron_ore = { a = "ironrock1", b = "ironrock2", x = 2830, z = 10152, bank = false },
        }
        local function mine(name, ore, want)
            local rock = ROCK[ore]
            walk(name .. ".toRocks", rock.x, rock.z, 0, 1)
            local got = 0
            for att = 1, 120 do
                got = count(ore) or 0
                if got >= want then break end
                t.player.click_loc((att % 2 == 1) and rock.a or rock.b, 1)
                t.inv.await(ore, got + 1, 12)
            end
            t.check(name, got >= want, "mined " .. ore .. ": " .. tostring(got) .. " in the pack (want " .. want .. ")")
        end
        -- From a city street: boat to the bank, mine what is there, then either the
        -- ferry on to the alcove and back by the second ferryman, or the boat back.
        local function gather(p, needs)
            boat_to_bank(p)
            for _, ore in ipairs({ "coal", "copper_ore" }) do
                if needs[ore] and (count(ore) or 0) < needs[ore] then
                    mine(p .. ".mine-" .. ore, ore, needs[ore])
                end
            end
            local alcove = false
            for _, ore in ipairs({ "tin_ore", "iron_ore" }) do
                if needs[ore] and (count(ore) or 0) < needs[ore] then alcove = true end
            end
            if alcove then
                bank_to_alcove(p)
                for _, ore in ipairs({ "iron_ore", "tin_ore" }) do
                    if needs[ore] and (count(ore) or 0) < needs[ore] then
                        mine(p .. ".mine-" .. ore, ore, needs[ore])
                    end
                end
                alcove_to_city(p)
            else
                bank_to_city(p)
            end
        end

        -- The way into Keldagrim from the surface: Camelot Teleport, the hillside
        -- east of Rellekka, the two tunnels onto the ferry bank.
        local function camelot_teleport(name)
            -- Graded on the landing: the spell's hop is several ticks after the cast.
            t.exec(name, t.player.cast, "camelot_teleport")
            t.await({
                level = function()
                    local r, tl = t.world.tile()
                    return r == "ok" and math.abs(tl.x - 2757) <= 2 and math.abs(tl.z - 3478) <= 2
                end,
                note = name .. ": waiting for the landing",
            }, 12)
            local r, tl = t.world.tile()
            t.check(name .. ".landed", r == "ok" and math.abs(tl.x - 2757) <= 2 and math.abs(tl.z - 3478) <= 2, tile_text(r, tl))
        end
        local function tunnels_to_bank(p)
            camelot_teleport(p .. ".camelotTeleport")
            t.exec("goto-" .. p .. ".hillside", t.player.goto_tile, 2730, 3712, 0)
            t.exec(p == "start" and "enterDwarfCave" or (p .. ".enterDwarfCave"), t.player.cross_gate, {
                loc = "trollromance_stronghold_exit_tunnel", at = { 2731, 3712, 0 }, near = { 2730, 3712 },
                far_ok = function(tl) return tl.z > 9000 and tl.x >= 2762 and tl.x <= 2804 end,
                far_desc = "the troll room under the mountain (betweenarock_travel.rs2:18)" })
            t.exec(p == "start" and "enterDwarfCave2" or (p .. ".enterDwarfCave2"), t.player.cross_gate, {
                loc = "dwarf_cavewall_tunnel", at = { 2781, 10161, 0 }, near = { 2780, 10161 },
                far_ok = function(tl) return tl.x == 2838 and tl.z == 10124 end,
                far_desc = "the ferry bank 2838,10124 (betweenarock_travel.rs2:45)" })
        end

        -- Closed rooms: one door each, in and out.
        local DOORS = {
            veldaban = { closed = "dwarf_keldagrim_door", open = "dwarf_keldagrim_door_open", at = { 2827, 10218, 0 },
                out_tile = { 2827, 10218 }, in_tile = { 2827, 10217 }, in_far = { 2827, 10216 }, out_far = { 2827, 10219 } },
            blasidar = { closed = "dwarf_keldagrim_door_ornate", open = "dwarf_keldagrim_door_ornate_open", at = { 2906, 10200, 0 },
                out_tile = { 2906, 10200 }, in_tile = { 2906, 10201 }, in_far = { 2906, 10202 }, out_far = { 2906, 10199 } },
            library = { closed = "dwarf_keldagrim_door", open = "dwarf_keldagrim_door_open", at = { 2863, 10229, 0 },
                out_tile = { 2863, 10229 }, in_tile = { 2863, 10228 }, in_far = { 2863, 10227 }, out_far = { 2863, 10230 } },
            dromund = { closed = "dwarf_keldagrim_door_ornate", open = "dwarf_keldagrim_door_ornate_open", at = { 2837, 10219, 0 },
                out_tile = { 2837, 10219 }, in_tile = { 2837, 10220 }, in_far = { 2837, 10221 }, out_far = { 2837, 10218 } },
            palace = { closed = "fai_varrock_castle_door", open = "fai_varrock_castle_door_open", at = { 3217, 3492, 0 },
                out_tile = { 3218, 3492 }, in_tile = { 3217, 3492 }, in_far = { 3216, 3492 }, out_far = { 3219, 3492 } },
            reldo = { closed = "fai_varrock_castle_door", open = "fai_varrock_castle_door_open", at = { 3210, 3490, 0 },
                out_tile = { 3210, 3489 }, in_tile = { 3210, 3490 }, in_far = { 3210, 3491 }, out_far = { 3210, 3488 } },
        }
        local function door_in(name, which)
            local d = DOORS[which]
            t.exec(name, t.player.pass_door, { closed = d.closed, open = d.open, at = d.at,
                near = d.out_tile, far = d.in_far })
        end
        local function door_out(name, which)
            local d = DOORS[which]
            t.exec(name, t.player.pass_door, { closed = d.closed, open = d.open, at = d.at,
                near = d.in_tile, far = d.out_far })
        end

        -- The consortium floor by the west-south wide stairs: ~climb on the maplink row
        -- (gdwarf_consortium.rs2:240-251; maplink.dbrow maplink_0_44_159_49_33_up lands
        -- 2862,10209,1, maplink_1_44_159_46_33_down lands 2865,10209,0, the open street).
        -- The palace floor by the east wide stairs, the recorder's route to the Green
        -- Gemstone's office (index 4495 [361], [412]).
        local function up_to_consortium(name)
            t.exec("goto-" .. name, t.player.goto_tile, 2893, 10209, 0)
            t.exec(name, t.player.climb, { loc = "dwarf_keldagrim_wide_stairs_lower", op = 1, op_name = "Climb-up",
                at = { 2894, 10209, 0 }, src = { 2893, 10209 }, dest = { 2896, 10209, 1 } })
        end
        local function in_hq(tl) return tl.level == 0 and tl.x >= 2825 and tl.x <= 2829 and tl.z >= 10208 and tl.z <= 10217 end
        -- The Grand Exchange trapdoor's Travel: a forced ride along the rails that
        -- ends at the station (keldagrim_travel.rs2, index 4494 [2816..2838]).
        local function trapdoor_to_station(name)
            t.exec("goto-" .. name, t.player.goto_tile, 3141, 3504, 0)
            t.exec(name, t.player.click_loc, "ge_keldagrim_trapdoor", 2, { at = { 3140, 3504, 0 } })
            t.await({
                level = function()
                    local r, tl = t.world.tile()
                    return r == "ok" and tl.x == 2909 and tl.z == 10174
                end,
                note = name .. ": waiting for the ride to end",
            }, 40)
            local r, tl = t.world.tile()
            t.check(name .. ".landed", r == "ok" and tl.x == 2909 and tl.z == 10174, "landed " .. tile_text(r, tl))
        end
        -- Page through a conversation, pressing the first listed row a menu has.
        local function drive(name, rows)
            for _ = 1, 60 do
                local kind = t.chat.kind()
                if kind == "none" then return end
                if kind == "options" then
                    local done = false
                    for _, row in ipairs(rows) do
                        if t.chat.choose(row) == "ok" then done = true; break end
                    end
                    t.check(name .. ".row", done, "none of the rows is on this menu")
                    if not done then return end
                else
                    t.chat.continue_()
                end
                t.ticks(2)
            end
        end
        local function await_chat(name)
            for _ = 1, 12 do
                if t.chat.kind() ~= "none" then return end
                t.ticks(1)
            end
        end
        local function talk(name, npc, rows)
            t.exec(name, t.player.talk_to, npc, 1)
            await_chat(name)
            drive(name, rows)
            t.ticks(2)
        end
        local function server(var)
            local _, v = t.var.server(var)
            return v
        end
        local function down_from_consortium(name)
            walk(name .. ".toStairs", 2896, 10209, 1, 1)
            t.exec(name, t.player.climb, { loc = "dwarf_keldagrim_wide_stairs_upper", op = 1, op_name = "Climb-down",
                at = { 2895, 10209, 1 }, src = { 2896, 10209 }, dest = { 2893, 10209, 0 } })
            walk(name .. ".offStairs", 2889, 10209, 0, 1)
        end

        -- ============================================================
        -- The start, index 4491 [285..574]
        -- ============================================================
        tunnels_to_bank("start")
        walk("talkToBoatman.walk", 2840, 10128, 0, 1)
        t.exec("talkToBoatman.present", t.npc.await_present, "dwarf_city_boatman_mines", 10, 15)
        talk("talkToBoatman", "dwarf_city_boatman_mines", { "That's a deal!", "Yes." })
        t.expect("quest.stage.boat_ride", t.quest.expect_stage("boat_ride"))
        t.await({
            level = function()
                local r, tl = t.world.tile()
                return r == "ok" and in_hq(tl) and t.chat.kind() ~= "none"
            end,
            note = "the ride, the crash, the arrest: waiting for Veldaban's first page",
        }, 300)
        local ar, at = t.world.tile()
        t.check("arrested.landed", ar == "ok" and in_hq(at), "landed " .. tile_text(ar, at))
        drive("talkToVeldaban", { "Yes, I will do this." })
        t.ticks(2)
        t.expect("quest.stage.see_blasidar", t.quest.expect_stage("see_blasidar"))
        door_out("talkToVeldaban.doorOut", "veldaban")

        t.exec("goto-talkToBlasidar", t.player.goto_tile, 2906, 10199, 0)
        door_in("talkToBlasidar.doorIn", "blasidar")
        talk("talkToBlasidar", "dwarf_city_shop_sculpture", { "Yes, I will do this." })
        t.expect("quest.stage.items", t.quest.expect_stage("items"))
        door_out("talkToBlasidar.doorOut", "blasidar")

        -- ============================================================
        -- The clothes, index 4492 and 4494 [1141..1868]
        -- ============================================================
        t.exec("goto-talkToVermundi", t.player.goto_tile, 2887, 10188, 0)
        talk("talkToVermundi", "dwarf_city_shop_cloth_poor", { "Yes, I'm looking for some special clothes." })
        t.check("vermundi.asked", server("varp9553_gdwarf_vermundi_asked") == 1, "asked about the clothes")

        t.exec("goto-talkToLibrarian", t.player.goto_tile, 2863, 10230, 0)
        door_in("talkToLibrarian.doorIn", "library")
        talk("talkToLibrarian", "dwarf_city_librarian", { "Do you know anything about King Alvis' clothes?" })
        t.check("librarian.hint", server("varp9554_gdwarf_book_hint") == 1, "told about the book")

        t.exec("climbBookcase", t.player.click_loc, "dwarf_keldagrim_bookcase_ladder", 1, { at = { 2859, 10228 } })
        await_chat("climbBookcase")
        drive("climbBookcase.page", {})
        t.check("climbBookcase.book", count("dwarf_library_book") == 1, "Scholars Guide in backpack")
        door_out("climbBookcase.doorOut", "library")

        t.exec("goto-talkToVermundiAfterBook", t.player.goto_tile, 2887, 10188, 0)
        talk("talkToVermundiWithBook", "dwarf_city_shop_cloth_poor", { "Yes, about those special clothes again..." })
        t.check("vermundi.book", server("varb584_giantdwarf_vermundi_givenbook") == 1, "the book went to Vermundi")

        -- Coal for the machine is on the ferry bank; the same trip takes the ferry on
        -- to the alcove for the iron Thurgo's bar is smelted from (iron smelts half the
        -- time without a ring of forging, so ten ores).
        gather("mineCoal", { coal = 1, iron_ore = 10 })
        t.exec("goto-smeltIronBar", t.player.goto_tile, 2873, 10210, 0)
        local furnace0 = t.player.by_symbol("loc", "dwarf_keldagrim_furnace")
        local bar_have = 0
        for _ = 1, 30 do
            bar_have = count("iron_bar") or 0
            if bar_have >= 1 then break end
            t.player.use_on("iron_ore", furnace0)
            t.chat.close()
            t.inv.await("iron_bar", 1, 8)
        end
        t.check("smeltIronBar", bar_have >= 1, "smelted iron bar for Thurgo, count=" .. tostring(bar_have))

        t.exec("goto-useCoalOnMachine", t.player.goto_tile, 2885, 10187, 0)
        local machine = t.player.by_symbol("loc", "dwarf_keldagrim_spinning_machine")
        t.exec("useCoalOnMachine", t.player.use_on, "coal", machine)
        t.ticks(3)
        t.check("machine.loaded", server("varp9552_gdwarf_machine") == 1, "coal and logs went in")
        for i = 1, 25 do
            if server("varp9552_gdwarf_machine") == 2 then break end
            machine = t.player.by_symbol("loc", "dwarf_keldagrim_spinning_machine")
            t.player.use_on("tinderbox", machine)
            t.ticks(10)
        end
        t.check("machine.lit", server("varp9552_gdwarf_machine") == 2, "the machine runs")

        talk("talkToVermundiWithMachine", "dwarf_city_shop_cloth_poor", { "Yes, about those special clothes again...", "I'll pay." })
        t.check("clothes.received", count("dwarf_clothes") == 1, "the clothes are in the pack")

        -- ============================================================
        -- The boots, index 4492 [1396..2518]
        -- ============================================================
        t.exec("goto-talkToSaro", t.player.goto_tile, 2827, 10197, 0)
        talk("talkToSaro", "dwarf_city_shop_armour", { "Yes, I'm looking for a pair of special boots." })
        t.check("saro.asked", server("varp9557_gdwarf_saro_asked") == 1, "Saro named Dromund")

        t.exec("goto-talkToDromund", t.player.goto_tile, 2837, 10218, 0)
        door_in("talkToDromund.doorIn", "dromund")
        talk("talkToDromund", "dwarf_city_excentric_dwarf", {})
        t.check("dromund.asked", server("varp9551_gdwarf_dromund_asked") == 1, "Dromund refused")

        -- Dromund paces a 22-tick loop and sees what is ahead of him: press with his
        -- back turned (going north up the west wall), as the recorder did (index 4492 [1537]).
        local function dromund_at(want_x, want_z, ticks)
            for _ = 1, ticks do
                local _, _, rows = t.npc.tiles("dwarf_city_excentric_dwarf", 30)
                if type(rows) == "table" and rows[1] and rows[1].x == want_x and rows[1].z == want_z then return true end
                t.ticks(1)
            end
            return false
        end
        local got, tries = false, 0
        walk("takeLeftBoot.tile", 2838, 10221, 0, 0)
        for i = 1, 6 do
            tries = i
            t.check("takeLeftBoot.timed-" .. i, dromund_at(2835, 10222, 60), "Dromund came round the west wall")
            t.player.click_obj("dwarf_perfect_left_boot", 3)
            t.ticks(2)
            local n = count("dwarf_perfect_left_boot")
            if n and n > 0 then got = true break end
            drive("leftBootCaught-" .. i, {})
        end
        t.check("takeLeftBoot", got, "left boot in backpack after " .. tries .. " press(es)")
        door_out("takeLeftBoot.doorOut", "dromund")

        -- The right boot: through the window, from the tile under it (2836,10227 worked, 2836,10228
        -- is 'try moving a little closer', index 4492 [1704], [1722]).
        t.exec("goto-takeRightBoot", t.player.goto_tile, 2836, 10227, 0)
        -- From the street he is timed on the north wall, walking away from the window
        -- (index 4492 [1722], [1890], [2088]: 2838..2839,10225).
        local pair, casts = false, 0
        for i = 1, 6 do
            casts = i
            t.check("takeRightBoot.timed-" .. i, dromund_at(2837, 10225, 60), "Dromund on the north wall")
            local r, d = t.player.cast("telegrab", { kind = "obj", id = "dwarf_perfect_right_boot" })
            t.ticks(5)
            local nn = count("dwarf_perfect_pair_of_boots")
            if nn and nn > 0 then pair = true break end
            drive("rightBootCaught-" .. i, {})
        end
        t.check("takeRightBoot", pair, "the pair is in the pack after " .. casts .. " telegrab press(es)")
        drive("takeRightBoot.page", {})

        -- ============================================================
        -- The axe, index 4494 [2438..4369]; Reldo is the wiki's route for a
        -- player without The Knight's Sword, which this fixture is
        -- ============================================================
        t.exec("goto-talkToSantiri", t.player.goto_tile, 2828, 10228, 0)
        talk("talkToSantiri", "dwarf_city_shop_weapons", { "Yes, I'm looking for a particular battleaxe.",
            "Blasidar the sculptor needs it for his statue.", "Perhaps I can repair the axe?" })
        t.check("santiri.axe", count("dwarf_battleaxe_old") == 1, "Santiri handed over the axe")

        t.exec("goto-buySapphires", t.player.goto_tile, 2888, 10209, 0)
        t.exec("buySapphires.open", t.shop.open, "dwarf_city_shop_gems", 3, "keldagrim_gem_stall")
        t.exec("buySapphires.buy", t.shop.buy, "sapphire", 3)
        t.shop.close()
        t.check("buySapphires.count", count("sapphire") == 3, "three sapphires bought")

        t.exec("useSapphires", t.player.use_item_on_item, "sapphire", "dwarf_battleaxe_old")
        t.ticks(3)
        t.check("useSapphires.axe", count("sapphire") == 0 and count("dwarf_battleaxe_sapphires") == 1, "the sapphires are set")

        t.exec("goto-talkToLibrarianAboutImcando", t.player.goto_tile, 2863, 10230, 0)
        door_in("talkToLibrarianAboutImcando.doorIn", "library")
        talk("talkToLibrarianAboutImcando", "dwarf_city_librarian", { "Can you help me find an Imcando dwarf?" })
        door_out("talkToLibrarianAboutImcando.doorOut", "library")

        -- Out to Varrock on the Grand Exchange cart (index 4495 [1294..1316]).
        t.exec("goto-talkToReldo.cart", t.player.goto_tile, 2923, 10170, 0)
        t.exec("talkToReldo.cartClick", t.player.click_loc, "keldagrim_train_cart", 1, { at = { 2923, 10171, 0 } })
        t.await({
            level = function()
                local r, tl = t.world.tile()
                return r == "ok" and tl.x == 3141 and tl.z == 3504
            end,
            note = "the cart ride to the Grand Exchange",
        }, 60)
        local gr, gt = t.world.tile()
        t.check("talkToReldo.cartLanded", gr == "ok" and gt.x == 3141 and gt.z == 3504, "landed " .. tile_text(gr, gt))
        t.exec("goto-talkToReldo", t.player.goto_tile, 3219, 3492, 0)
        door_in("talkToReldo.palaceDoorIn", "palace")
        walk("talkToReldo.hall", 3210, 3489, 0, 0)
        door_in("talkToReldo.libraryDoorIn", "reldo")
        talk("talkToReldo", "reldo_normal", {})
        door_out("talkToReldo.libraryDoorOut", "reldo")
        walk("talkToReldo.hallOut", 3217, 3492, 0, 0)
        door_out("talkToReldo.palaceDoorOut", "palace")

        t.exec("goto-talkToThurgo", t.player.goto_tile, 3001, 3145, 0)
        talk("thurgo.pie", "thurgo", { "Would you like a redberry pie?" })
        t.check("thurgo.pie.given", server("varb580_giantdwarf_pie_given") == 1, "Thurgo ate the pie")
        talk("thurgo.ask", "thurgo", { "Can you help me with this ancient axe?" })
        talk("thurgo.repair", "thurgo", { "Can you repair that axe now?", "Return to Keldagrim immediately." })
        t.ticks(4)
        t.check("thurgo.axe", count("dwarf_battleaxe_new") == 1, "the axe is restored")
        local tr, tt = t.world.tile()
        t.check("thurgo.returned", tr == "ok" and tt.x == 2906 and tt.z == 10202, "landed " .. tile_text(tr, tt))

        -- Riki, inside Blasidar's house already (the return lands there).
        talk("giveBoots", "dwarf_city_shop_sculpture_model_multi", {})
        talk("giveClothes", "dwarf_city_shop_sculpture_model_multi", {})
        talk("giveAxe", "dwarf_city_shop_sculpture_model_multi", {})
        t.check("model.finished", server("varb576_giantdwarf_model_state") == 7, "Riki has all three")
        talk("talkToBlasidarHead", "dwarf_city_shop_sculpture", {})
        t.expect("quest.stage.consortium", t.quest.expect_stage("consortium"))
        door_out("talkToBlasidarHead.doorOut", "blasidar")

        -- The trapdoor way in (keldagrim_travel.rs2): one ride, down from the Grand Exchange.
        -- ============================================================
        -- The Consortium, index 4495..4501
        -- ============================================================
        up_to_consortium("enterConsortium")

        local ORE_OF = { [2] = "copper_ore", [3] = "tin_ore", [4] = "iron_ore", [8] = "coal" }
        local SEC, DIR = "dwarf_city_secretary_green_gemstone", "dwarf_city_director_green_gemstone"
        local function points() return server("varp9543_gdwarf_points_green_gemstone") or 0 end
        local function task() return server("varp9548_gdwarf_task_item") or 0, server("varp9549_gdwarf_task_amount") or 0 end
        -- Read the offer's page, refuse what no rock or furnace here can supply (the
        -- recorded -2), and take the rest.
        local WANT = { ["copper ore"] = "copper_ore", ["tin ore"] = "tin_ore", ["iron ore"] = "iron_ore", ["coal"] = "coal",
            ["bronze"] = "bronze_bar", ["iron"] = "iron_bar" }
        local function offer(npc, name, ask)
            t.exec(name .. ".present", t.npc.await_present, npc, 20, 15)
            t.exec(name, t.player.talk_to, npc, 1)
            await_chat(name)
            local wanted
            for _ = 1, 40 do
                local kind = t.chat.kind()
                if kind == "none" then break end
                local _, text = t.chat.text()
                text = tostring(text or ""):gsub("<br>", " ")
                local item = text:match("pieces of ([a-z ]-) in the next") or text:match("bars of ([a-z]-) in the next")
                if item then wanted = item end
                if kind == "options" then
                    local pressed = false
                    if wanted then
                        local row = WANT[wanted] and "I'll take it." or "No thanks."
                        pressed = t.chat.choose(row) == "ok"
                        wanted = nil
                    end
                    if not pressed then
                        for _, row in ipairs(ask) do
                            if t.chat.choose(row) == "ok" then pressed = true; break end
                        end
                    end
                    t.check(name .. ".row", pressed, "no expected row")
                    if not pressed then break end
                else
                    t.chat.continue_()
                end
                t.ticks(2)
            end
            t.ticks(2)
        end
        local BAR_OF = { [11] = "bronze_bar", [12] = "iron_bar" }
        local function smelt(p, bar, n)
            t.exec("goto-" .. p .. ".furnace", t.player.goto_tile, 2873, 10210, 0)
            local furnace = t.player.by_symbol("loc", "dwarf_keldagrim_furnace")
            local made = 0
            for _ = 1, 60 do
                made = count(bar) or 0
                if made >= n then break end
                t.player.use_on((bar == "bronze_bar") and "copper_ore" or "iron_ore", furnace)
                t.chat.close()
                t.inv.await(bar, made + 1, 8)
            end
            t.check(p .. ".smelted", made >= n, "smelted " .. tostring(made) .. "/" .. tostring(n) .. " " .. bar)
        end

        -- A refusal or a lapsed task costs 2, and below 75 the director will not
        -- talk: a dropped score goes back through the secretary, as a player's would.
        -- Refused or lapsed tasks leave ore behind: a full pack stops the next mine.
        local function drop_ores(keep)
            for _, ore in ipairs({ "copper_ore", "tin_ore", "iron_ore", "coal", "bronze_bar", "iron_bar" }) do
                if not keep[ore] then
                    for _ = 1, 28 do
                        if (count(ore) or 0) == 0 then break end
                        t.player.inv_op(ore, 5)
                        t.ticks(1)
                    end
                end
            end
        end
        local function secretary_round(i)
            local p = "secretary-" .. i
            offer(SEC, p, { "Is there anything I can help you with?", "Do you have another task for me?", "I have to go!" })
            local item, n = task()
            local ore = ORE_OF[item]
            if ore then
                drop_ores({ [ore] = true })
                if (count(ore) or 0) < n then
                    down_from_consortium(p .. ".down")
                    gather(p, { [ore] = n })
                    up_to_consortium(p .. ".up")
                end
                t.check(p .. ".carrying", (count(ore) or 0) >= n, ore .. " x" .. n)
                talk(p .. ".handin", SEC, { "I have to go!" })
            end
        end
        local function director_round(i)
            local p = "director-" .. i
            offer(DIR, p, { "Do you have any more tasks for me?", "Thank you for your time." })
            local item, n = task()
            local bar = BAR_OF[item]
            if bar then
                drop_ores({ [bar] = true, copper_ore = (bar == "bronze_bar"), tin_ore = (bar == "bronze_bar"), iron_ore = (bar == "iron_bar") })
                if (count(bar) or 0) < n then
                    down_from_consortium(p .. ".down")
                    if bar == "bronze_bar" then
                        gather(p, { copper_ore = n, tin_ore = n })
                    else
                        gather(p, { iron_ore = n * 2 + 2 })
                    end
                    smelt(p, bar, n)
                    up_to_consortium(p .. ".up")
                end
                t.check(p .. ".carrying", (count(bar) or 0) >= n, bar .. " x" .. n)
                talk(p .. ".handin", DIR, { "Thank you for your time." })
            end
        end
        for i = 1, 80 do
            if points() >= 100 then break end
            if points() < 75 then secretary_round(i) else director_round(i) end
        end
        t.check("points.director", points() >= 100, "points " .. points())

        talk("joinCompany", DIR, { "I'd like to officially join your company.", "Thank you for your time." })
        t.check("joined", server("varb578_giantdwarf_current_company") == 4, "the Green Gemstone")
        talk("pledge", DIR, { "Blasidar the sculptor has sent me.", "I would support you.", "Yes! Long live the Green Gemstone!" })
        t.expect("quest.stage.meeting", t.quest.expect_stage("meeting"))
        down_from_consortium("leaveConsortium")

        local _, before = t.skill.snapshot()
        t.exec("goto-talkToVeldabanAfterJoining", t.player.goto_tile, 2827, 10219, 0)
        door_in("talkToVeldabanAfterJoining.doorIn", "veldaban")
        t.exec("talkToVeldabanAfterJoining.present", t.npc.await_present, "dwarf_city_black_guard_leader", 8, 15)
        talk("talkToVeldabanAfterJoining", "dwarf_city_black_guard_leader", { "I'm ready.", "I want to see the meeting." })
        t.await({
            level = function() return server("varb571_giantdwarf_quest") == 50 end,
            note = "the Consortium meeting",
        }, 400)
        t.ticks(3)
        t.quest.expect_complete()
        t.check("reward.mining", t.skill.expect_gain("mining", 2500, before))
        t.check("reward.smithing", t.skill.expect_gain("smithing", 2500, before))
        t.check("reward.crafting", t.skill.expect_gain("crafting", 2500, before))
        t.check("reward.magic", t.skill.expect_gain("magic", 1500, before))
        t.check("reward.thieving", t.skill.expect_gain("thieving", 1500, before))
        t.check("reward.firemaking", t.skill.expect_gain("firemaking", 1500, before))
        t.check("notes", count("dwarf_meeting_notes1") == 1, "the minutes are in the pack")
        t.finish(0)
    end,
}
