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
--   * Out of the consortium stairs' sealed landing (CONTENT SEAM, see
--     down_from_consortium): Varrock Teleport (1 fire + 3 air + 1 law each), the
--     street to the GE trapdoor, the trapdoor back into Veldaban's HQ.
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
--   * The consortium floor: dwarf_keldagrim_wide_stairs_lower / _upper by climb on
--     every visit (gdwarf_consortium.rs2:229 / :240), including every ore run.
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
        "::give lawrune 30",
        "::give airrune 70",
        "::give firerune 14",
        "::give redberry_pie 1",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb571_giantdwarf_quest",
            constants = {
                not_started = 0, arrived = 1, veldaban_done = 2, blasidar_done = 3,
                vermundi_asked = 4, librarian_asked = 5, got_book = 6, showed_book = 7,
                machine_loaded = 8, machine_started = 9, clothes_done = 10,
                saro_asked = 11, dromund_asked = 12, left_boot = 13, boots_done = 14,
                santiri_asked = 15, sapphires_used = 16, imcando_asked = 17,
                reldo_told = 18, axe_done = 19, items_given = 20, blasidar_after = 21,
                entered_consortium = 22, secretary_done = 23, director_done = 24,
                joined_company = 25, director_after = 26, ready_to_finish = 28,
                complete = 50,
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
            t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = name,
                runes = { { "airrune", 5 }, { "lawrune", 1 } }, where = "Camelot" })
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

        -- The consortium floor: up any lower wide stair lands 2869,10205,1, down any
        -- upper one lands 2895,10210,0 (gdwarf_consortium.rs2:229-245).
        local function up_to_consortium(name)
            t.exec("goto-" .. name, t.player.goto_tile, 2893, 10209, 0)
            t.exec(name, t.player.climb, { loc = "dwarf_keldagrim_wide_stairs_lower", op = 1, op_name = "Climb-up",
                at = { 2894, 10209, 0 }, src = { 2893, 10209 }, dest = { 2869, 10205, 1 } })
        end
        local function in_hq(tl) return tl.level == 0 and tl.x >= 2825 and tl.x <= 2829 and tl.z >= 10208 and tl.z <= 10217 end
        -- The Grand Exchange trapdoor (forget_keldagrim.rs2:9-19) lands in Veldaban's HQ.
        local function trapdoor_to_hq(name)
            t.exec("goto-" .. name, t.player.goto_tile, 3141, 3504, 0)
            t.exec(name, t.player.cross_gate, { loc = "ge_keldagrim_trapdoor", at = { 3140, 3504, 0 }, near = { 3141, 3504 },
                far_ok = in_hq, far_desc = "Veldaban's HQ x 2825-2829 z 10208-10217 (forget_keldagrim.rs2:19 ^forget_veldaban_coord)",
                chat = { "mesbox:The trapdoor leads down", "choose:Yes please.", "player:Yes please." } })
        end
        -- CONTENT SEAM: gdwarf_consortium.rs2:240-242 ([oploc1,dwarf_keldagrim_wide_stairs_upper])
        -- lands every descent on ^gdwarf_consortium_lower_coord = 0_45_159_15_34 = 2895,10210,0
        -- (giantdwarf.constant:148), a tile of the 2x2 dwarf_keldagrim_wide_stairs_lower footprint
        -- (2894,10209) boxed in by the chopping board (2894-2895,10211), crates and sacks (2896,
        -- 10209-10211) (maps/m45_159.jl2): comp.py size 1, and run 2 timed out walking off it on
        -- all seven descents. A goto out of it would leave a sealed pocket, so the player does what
        -- a player there does: Varrock Teleport, the street to the Grand Exchange trapdoor, and the
        -- trapdoor back down into Veldaban's HQ. The walk off is still tried first and noted.
        local function down_from_consortium(name)
            walk(name .. ".toStairs", 2865, 10209, 1, 0)
            t.exec(name, t.player.climb, { loc = "dwarf_keldagrim_wide_stairs_upper", op = 1, op_name = "Climb-down",
                at = { 2863, 10209, 1 }, src = { 2865, 10209 }, dest = { 2895, 10210, 0 } })
            local wr, wd = t.player.walk_to(2893, 10210, 12)
            local r, tl = t.world.tile()
            t.note(name .. ": stepping off the stairs' landing: walk_to 2893,10210 -> " .. tostring(wr) .. " "
                .. tostring(wd) .. "; at " .. tile_text(r, tl) .. " (content seam: gdwarf_consortium.rs2:242 landing is sealed)")
            t.player.teleport_cast("varrock_teleport", { 3213, 3424, 0 }, { name = name .. ".varrockTeleport",
                runes = { { "firerune", 1 }, { "airrune", 3 }, { "lawrune", 1 } }, where = "Varrock" })
            trapdoor_to_hq(name .. ".trapdoor")
        end

        -- ============================================================
        -- Leg 1
        -- ============================================================
        tunnels_to_bank("start")
        -- the prequest symbol has no spawn row; the placed boatman (m44_158.spawn:12) routes to it
        -- the tunnel's landing rebuilds the scene: the boatman joins the npc pool late
        walk("talkToBoatman.walk", 2840, 10128, 0, 1)
        t.exec("talkToBoatman.present", t.npc.await_present, "dwarf_city_boatman_mines", 10, 15)
        local br, bt = t.world.tile()
        t.exec("talkToBoatman", t.player.talk_to, "dwarf_city_boatman_mines", 1)
        t.exec("talkToBoatman.chat", t.chat.play, {
            "npc:For a human like you",
            "choose:That's a deal!",
            "player:That's a deal!",
            "npc:Yes, I'm ready",
            "player:Yes.",
        })
        t.ticks(4)
        t.expect("quest.stage.arrived", t.quest.expect_stage("arrived"))
        local ar, at = t.world.tile()
        t.check("talkToBoatman.landed", br == "ok" and bt.z < 10150 and ar == "ok" and in_hq(at),
            "from " .. tile_text(br, bt) .. " the boat landed " .. tile_text(ar, at)
                .. " (want Veldaban's HQ x 2825-2829 z 10208-10217: gdwarf_start.rs2:64 ^gdwarf_veldaban_coord)")

        t.exec("talkToVeldaban", t.player.talk_to, "dwarf_city_black_guard_leader", 1)
        t.exec("talkToVeldaban.chat", t.chat.play, {
            "npc:Welcome to Keldagrim",
            "npc:I want you to go",
            "npc:I need you to go to Blasidar",
            "choose:Yes, I will do this.",
            "player:Yes, I will do this.",
        })
        t.ticks(3)
        t.expect("quest.stage.veldaban_done", t.quest.expect_stage("veldaban_done"))
        door_out("talkToVeldaban.doorOut", "veldaban")

        t.exec("goto-talkToBlasidar", t.player.goto_tile, 2906, 10199, 0)
        door_in("talkToBlasidar.doorIn", "blasidar")
        t.exec("talkToBlasidar", t.player.talk_to, "dwarf_city_shop_sculpture", 1)
        t.exec("talkToBlasidar.chat", t.chat.play, {
            "npc:Ah, a human!",
            "npc:I'm building a grand statue",
            "choose:Yes, I will do this.",
            "player:Yes, I will do this.",
            "npc:Wonderful! Vermundi",
        })
        t.ticks(3)
        t.expect("quest.stage.blasidar_done", t.quest.expect_stage("blasidar_done"))
        door_out("talkToBlasidar.doorOut", "blasidar")

        t.exec("goto-talkToVermundi", t.player.goto_tile, 2887, 10188, 0)
        t.exec("talkToVermundi", t.player.talk_to, "dwarf_city_shop_cloth_poor", 1)
        t.exec("talkToVermundi.chat", t.chat.play, {
            "player:Yes, I'm looking for some special clothes.",
            "npc:Clothes fit for King Alvis",
        })
        t.ticks(3)
        t.expect("quest.stage.vermundi_asked", t.quest.expect_stage("vermundi_asked"))

        t.exec("goto-talkToLibrarian", t.player.goto_tile, 2863, 10230, 0)
        door_in("talkToLibrarian.doorIn", "library")
        t.exec("talkToLibrarian", t.player.talk_to, "dwarf_city_librarian", 1)
        t.exec("talkToLibrarian.chat", t.chat.play, {
            "player:Do you know anything about King Alvis' clothes?",
            "npc:There is a book on dwarven costumes",
        })
        t.ticks(3)
        t.expect("quest.stage.librarian_asked", t.quest.expect_stage("librarian_asked"))

        t.exec("climbBookcase", t.player.click_loc, "dwarf_keldagrim_bookcase_ladder", 1, { at = { 2859, 10228 } })
        t.ticks(3)
        t.expect("quest.stage.got_book", t.quest.expect_stage("got_book"))
        local book = count("dwarf_library_book")
        t.check("climbBookcase.book", book == 1, "Scholars Guide in backpack count=" .. tostring(book))
        door_out("climbBookcase.doorOut", "library")

        t.exec("goto-talkToVermundiAfterBook", t.player.goto_tile, 2887, 10188, 0)
        t.exec("talkToVermundiWithBook", t.player.talk_to, "dwarf_city_shop_cloth_poor", 1)
        t.exec("talkToVermundiWithBook.chat", t.chat.play, {
            "player:Yes, about those special clothes again...",
            "npc:Ah, the Scholars Guide",
        })
        t.ticks(3)
        t.expect("quest.stage.showed_book", t.quest.expect_stage("showed_book"))

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
        local logs0, coal0 = count("logs"), count("coal")
        local machine = t.player.by_symbol("loc", "dwarf_keldagrim_spinning_machine")
        t.exec("useCoalOnMachine", t.player.use_on, "coal", machine)
        t.ticks(3)
        t.expect("quest.stage.machine_loaded", t.quest.expect_stage("machine_loaded"))
        local logs1, coal1 = count("logs"), count("coal")
        t.check("useCoalOnMachine.spent", logs0 ~= nil and coal0 ~= nil and logs1 == logs0 - 1 and coal1 == coal0 - 1,
            "logs " .. tostring(logs0) .. " -> " .. tostring(logs1) .. ", coal " .. tostring(coal0) .. " -> "
                .. tostring(coal1) .. " (want one of each into the machine: gdwarf_clothes.rs2:126)")
        machine = t.player.by_symbol("loc", "dwarf_keldagrim_spinning_machine")
        t.exec("startMachine", t.player.use_on, "tinderbox", machine)
        t.ticks(3)
        t.expect("quest.stage.machine_started", t.quest.expect_stage("machine_started"))

        t.exec("talkToVermundiWithMachine", t.player.talk_to, "dwarf_city_shop_cloth_poor", 1)
        t.exec("talkToVermundiWithMachine.chat", t.chat.play, {
            "player:Yes, about those special clothes again...",
            "npc:The machine's running now",
            "choose:I'll pay.",
            "player:I'll pay.",
            "mesbox:Vermundi spins you",
        })
        t.ticks(3)
        t.expect("quest.stage.clothes_done", t.quest.expect_stage("clothes_done"))
        local clothes = count("dwarf_clothes")
        t.check("clothes.received", clothes == 1, "exquisite clothes count=" .. tostring(clothes))

        t.exec("goto-talkToSaro", t.player.goto_tile, 2827, 10197, 0)
        t.exec("talkToSaro", t.player.talk_to, "dwarf_city_shop_armour", 1)
        t.exec("talkToSaro.chat", t.chat.play, {
            "player:Yes, I'm looking for a pair of special boots.",
            "npc:Special boots?",
        })
        t.ticks(3)
        t.expect("quest.stage.saro_asked", t.quest.expect_stage("saro_asked"))

        t.exec("goto-talkToDromund", t.player.goto_tile, 2837, 10218, 0)
        door_in("talkToDromund.doorIn", "dromund")
        t.exec("talkToDromund", t.player.talk_to, "dwarf_city_excentric_dwarf")
        t.exec("talkToDromund.chat", t.chat.play, { "player:Saro said", "npc:Get out you pesky human" })
        t.ticks(2)
        t.expect("quest.stage.dromund_asked", t.quest.expect_stage("dromund_asked"))

        local got = false
        for i = 1, 12 do
            local r, d = t.player.click_obj("dwarf_perfect_left_boot", 3)
            t.step("leftBootAttempt-" .. i, (r == "ok" or r == "timeout" or r == "refused") and "PASS" or "FAIL", tostring(r) .. " " .. tostring(d))
            t.ticks(2)
            local n = count("dwarf_perfect_left_boot")
            if n and n > 0 then got = true break end
            t.chat.close()
            t.ticks(6)
        end
        t.check("takeLeftBoot", got, "left boot in backpack after waiting for Dromund to look away")
        t.ticks(2)
        t.expect("quest.stage.left_boot", t.quest.expect_stage("left_boot"))
        door_out("takeLeftBoot.doorOut", "dromund")

        -- the right boot: through the window, from the street north of the house
        t.exec("goto-takeRightBoot", t.player.goto_tile, 2836, 10229, 0)
        local pair = false
        for i = 1, 12 do
            local r, d = t.player.cast("telegrab", { kind = "obj", id = "dwarf_perfect_right_boot" })
            t.step("rightBootAttempt-" .. i, (r == "ok" or r == "refused" or r == "timeout") and "PASS" or "FAIL", tostring(r) .. " " .. tostring(d))
            t.ticks(4)
            local nn = count("dwarf_perfect_pair_of_boots")
            if nn and nn > 0 then pair = true break end
            t.chat.close()
            t.ticks(4)
        end
        t.check("takeRightBoot", pair, "exquisite pair in backpack after telekinetic grab")
        t.expect("quest.stage.boots_done", t.quest.expect_stage("boots_done"))

        t.exec("goto-talkToSantiri", t.player.goto_tile, 2828, 10228, 0)
        t.exec("talkToSantiri", t.player.talk_to, "dwarf_city_shop_weapons", 1)
        t.exec("talkToSantiri.chat", t.chat.play, {
            "player:Yes, I'm looking for a particular battleaxe.",
            "npc:A battleaxe, you say?",
            "player:Blasidar the sculptor needs it",
            "npc:Ah, I see.",
            "player:Perhaps I can repair the axe?",
        })
        t.ticks(3)
        t.expect("quest.stage.santiri_asked", t.quest.expect_stage("santiri_asked"))

        t.exec("goto-buySapphires", t.player.goto_tile, 2888, 10209, 0)
        t.exec("buySapphires.open", t.shop.open, "dwarf_city_shop_gems", 3, "keldagrim_gem_stall")
        t.exec("buySapphires.buy", t.shop.buy, "sapphire", 3)
        t.shop.close()
        local sapph = count("sapphire")
        t.check("buySapphires.count", sapph == 3, "bought sapphires, count=" .. tostring(sapph))

        t.exec("useSapphires", t.player.use_item_on_item, "sapphire", "dwarf_battleaxe_old")
        t.ticks(3)
        t.expect("quest.stage.sapphires_used", t.quest.expect_stage("sapphires_used"))
        local sapph1, axe1 = count("sapphire"), count("dwarf_battleaxe_sapphires")
        t.check("useSapphires.axe", sapph1 == 0 and axe1 == 1,
            "sapphires left " .. tostring(sapph1) .. ", dwarf_battleaxe_sapphires " .. tostring(axe1)
                .. " (want the three sapphires set into the old axe)")

        t.exec("goto-talkToLibrarianAboutImcando", t.player.goto_tile, 2863, 10230, 0)
        door_in("talkToLibrarianAboutImcando.doorIn", "library")
        t.exec("talkToLibrarianAboutImcando", t.player.talk_to, "dwarf_city_librarian", 1)
        t.exec("talkToLibrarianAboutImcando.chat", t.chat.play, {
            "player:Can you help me find an Imcando dwarf?",
            "npc:Imcando dwarves?",
        })
        t.ticks(3)
        t.expect("quest.stage.imcando_asked", t.quest.expect_stage("imcando_asked"))
        door_out("talkToLibrarianAboutImcando.doorOut", "library")

        -- Out to Varrock: the Grand Exchange cart from track 3 (free once the quest
        -- is started: keldagrim_travel.rs2 keldagrim_cart_ride).
        t.exec("goto-talkToReldo.cart", t.player.goto_tile, 2923, 10172, 0)
        t.exec("talkToReldo.cartToGrandExchange", t.player.cross_gate, {
            loc = "keldagrim_train_cart", at = { 2923, 10171, 0 }, near = { 2923, 10172 },
            far_ok = function(tl) return math.abs(tl.x - 3141) <= 1 and math.abs(tl.z - 3504) <= 1 end,
            far_desc = "the Grand Exchange 3141,3504 (keldagrim_travel.rs2 p_teleport(0_49_54_5_48))" })
        t.exec("goto-talkToReldo", t.player.goto_tile, 3219, 3492, 0)
        door_in("talkToReldo.palaceDoorIn", "palace")
        walk("talkToReldo.hall", 3210, 3489, 0, 0)
        door_in("talkToReldo.libraryDoorIn", "reldo")
        t.exec("talkToReldo", t.player.talk_to, "reldo_normal", 1)
        t.exec("talkToReldo.chat", t.chat.play, {
            "player:Ask about Imcando dwarves.",
            "npc:Imcando dwarves?",
        })
        t.ticks(3)
        t.expect("quest.stage.reldo_told", t.quest.expect_stage("reldo_told"))
        door_out("talkToReldo.libraryDoorOut", "reldo")
        walk("talkToReldo.hallOut", 3217, 3492, 0, 0)
        door_out("talkToReldo.palaceDoorOut", "palace")

        -- ============================================================
        -- Leg 3
        -- ============================================================
        t.exec("goto-talkToThurgo", t.player.goto_tile, 3001, 3145, 0)
        t.exec("talkToThurgo", t.player.talk_to, "thurgo", 1)
        t.exec("talkToThurgo.chat", t.chat.play, {
            "player:Would you like a redberry pie?",
            "npc:You make excellent redberry pies",
            "player:Can you repair that axe now?",
            "mesbox:Thurgo hammers",
            "player:Return to Keldagrim immediately.",
        })
        t.ticks(3)
        t.expect("quest.stage.axe_done", t.quest.expect_stage("axe_done"))

        -- Back to Keldagrim: the street to the Grand Exchange trapdoor, Veldaban's HQ door out.
        trapdoor_to_hq("returnToKeldagrim.trapdoor")
        door_out("returnToKeldagrim.hqDoorOut", "veldaban")

        t.exec("goto-giveItemsToRiki", t.player.goto_tile, 2906, 10199, 0)
        door_in("giveItemsToRiki.doorIn", "blasidar")
        t.exec("giveItemsToRiki", t.player.talk_to, "dwarf_city_shop_sculpture_model_multi", 1)
        t.exec("giveItemsToRiki.chat", t.chat.play, {
            "npc:Thank...you.",
            "npc:Thank...you.",
            "npc:Thank...you.",
            "mesbox:Riki the model stands dressed",
        })
        t.ticks(3)
        t.expect("quest.stage.items_given", t.quest.expect_stage("items_given"))

        t.exec("talkToBlasidarAfterItems", t.player.talk_to, "dwarf_city_shop_sculpture", 1)
        t.exec("talkToBlasidarAfterItems.chat", t.chat.play, {
            "player:I've given Riki the clothes",
            "npc:Wonderful! Let me take a look",
            "npc:Now, to win the Black Guard's trust",
        })
        t.ticks(3)
        t.expect("quest.stage.blasidar_after", t.quest.expect_stage("blasidar_after"))
        door_out("talkToBlasidarAfterItems.doorOut", "blasidar")

        -- Leg 3: the consortium
        up_to_consortium("enterConsortium")
        t.expect("quest.stage.entered_consortium", t.quest.expect_stage("entered_consortium"))

        -- The tasks are random; the ores are MINED for real on the ferry bank and in
        -- the alcove, and a task we cannot mine (clay, silver, gold, mithril) is
        -- turned down with "No thanks." (the quest's own refusal, -2 points).
        local ORE_OF = { [2] = "copper_ore", [3] = "tin_ore", [4] = "iron_ore", [8] = "coal" }

        local function task_vars()
            local _, task = t.var.server("varp7199_gdwarf_task")
            local _, n = t.var.server("varp7200_gdwarf_task_count")
            local _, points = t.var.server("varp7198_gdwarf_points")
            local _, st = t.quest.stage()
            return task, n, points, st
        end
        local function task_dialog(accept)
            for _ = 1, 12 do
                local kind = t.chat.kind()
                if kind == "options" then
                    local _, tk = t.var.server("varp7199_gdwarf_task")
                    if accept(tk) then
                        if t.chat.choose("I'll take it.") ~= "ok" then t.chat.choose("I'll keep looking.") end
                    else
                        if t.chat.choose("No thanks.") ~= "ok" then t.chat.choose("I'll keep looking.") end
                    end
                elseif kind == "none" then break
                else t.chat.continue_() end
                t.ticks(2)
            end
            t.ticks(2)
        end

        for i = 1, 40 do
            -- after a climb the floor's npcs join the client's pool a few ticks late
            t.exec("talkToSecretary-" .. i .. ".present", t.npc.await_present, "dwarf_city_secretary_blue_opal", 15, 15)
            t.exec("talkToSecretary-" .. i, t.player.talk_to, "dwarf_city_secretary_blue_opal")
            task_dialog(function(tk) return tk ~= nil and ORE_OF[tk] ~= nil end)
            local task, n, points, st = task_vars()
            if st and st >= 23 then
                t.check("talkToSecretary-done-" .. i, points ~= nil and points >= 75,
                    "secretary finished, points=" .. tostring(points) .. " (want >= 75, ^gdwarf_points_secretary_limit)")
                break
            end
            local ore = task and ORE_OF[task]
            if ore and n and n > 0 then
                if (count(ore) or 0) < n then
                    local p = "secretaryTrip-" .. i
                    down_from_consortium(p .. ".down")
                    door_out(p .. ".hqDoorOut", "veldaban")
                    gather(p, { [ore] = n })
                    up_to_consortium(p .. ".up")
                end
                local have = count(ore) or 0
                t.check("mine-" .. ore .. "-" .. i, have >= n,
                    tostring(have) .. "/" .. tostring(n) .. " " .. ore .. " in the pack for the secretary, points=" .. tostring(points))
            else
                t.check("talkToSecretary-refused-" .. i, task == 0 and n == 0,
                    "task turned down: varp7199 task=" .. tostring(task) .. " count=" .. tostring(n)
                        .. " (want 0/0 after ~gdwarf_refuse_points), points=" .. tostring(points))
            end
        end
        t.expect("quest.stage.secretary_done", t.quest.expect_stage("secretary_done"))

        for i = 1, 40 do
            t.exec("talkToDirector-" .. i .. ".present", t.npc.await_present, "dwarf_city_director_blue_opal", 20, 15)
            t.exec("talkToDirector-" .. i, t.player.talk_to, "dwarf_city_director_blue_opal")
            task_dialog(function(tk) return tk == 1 or tk == 2 end)
            local task, n, points, st = task_vars()
            if st and st >= 24 then
                t.check("talkToDirector-done-" .. i, points ~= nil and points >= 100,
                    "director finished, points=" .. tostring(points) .. " (want >= 100, ^gdwarf_points_total)")
                break
            end
            if (task == 1 or task == 2) and n and n > 0 then
                local bar = (task == 1) and "bronze_bar" or "iron_bar"
                if (count(bar) or 0) < n then
                    local p = "directorTrip-" .. i
                    down_from_consortium(p .. ".down")
                    door_out(p .. ".hqDoorOut", "veldaban")
                    if task == 1 then
                        gather(p, { copper_ore = n, tin_ore = n })
                    else
                        gather(p, { iron_ore = n * 2 + 2 })
                    end
                    t.exec("goto-" .. p .. ".furnace", t.player.goto_tile, 2873, 10210, 0)
                    local furnace = t.player.by_symbol("loc", "dwarf_keldagrim_furnace")
                    local made = 0
                    for _ = 1, 60 do
                        made = count(bar) or 0
                        if made >= n then break end
                        t.player.use_on((task == 1) and "copper_ore" or "iron_ore", furnace)
                        t.chat.close()
                        t.inv.await(bar, made + 1, 8)
                    end
                    t.check("smelt-" .. bar .. "-" .. i, made >= n,
                        "smelted " .. tostring(made) .. "/" .. tostring(n) .. " " .. bar .. " at the furnace")
                    up_to_consortium(p .. ".up")
                end
            else
                t.check("talkToDirector-refused-" .. i, task == 0 and n == 0,
                    "task turned down: varp7199 task=" .. tostring(task) .. " count=" .. tostring(n)
                        .. " (want 0/0 after ~gdwarf_refuse_points), points=" .. tostring(points))
            end
        end
        t.expect("quest.stage.director_done", t.quest.expect_stage("director_done"))

        t.exec("joinCompany", t.player.talk_to, "dwarf_city_director_blue_opal")
        t.exec("joinCompany.chat", t.chat.play, {
            "npc:Have you ever considered joining",
            "choose:I'd like to officially join your company.",
            "player:I'd like to officially join your company.",
            "npc:It is agreed then!",
        })
        t.ticks(2)
        t.expect("quest.stage.joined_company", t.quest.expect_stage("joined_company"))

        t.exec("talkToDirectorAfterJoining", t.player.talk_to, "dwarf_city_director_blue_opal")
        t.exec("talkToDirectorAfterJoining.chat", t.chat.play, {
            "npc:Blasidar the sculptor has sent you?",
            "player:Blasidar the sculptor has sent me.",
            "npc:Then I will remember it",
            "player:Yes! Long live",
        })
        t.ticks(2)
        t.expect("quest.stage.director_after", t.quest.expect_stage("director_after"))

        down_from_consortium("leaveConsortium")
        t.expect("quest.stage.ready_to_finish", t.quest.expect_stage("ready_to_finish"))

        local _, before = t.skill.snapshot()
        -- the trapdoor out of the stairs' pocket lands in Veldaban's HQ itself
        t.exec("talkToVeldabanAfterJoining.present", t.npc.await_present, "dwarf_city_black_guard_leader", 8, 15)
        t.exec("talkToVeldabanAfterJoining", t.player.talk_to, "dwarf_city_black_guard_leader")
        t.exec("talkToVeldabanAfterJoining.chat", t.chat.play, {
            "player:I've been accepted into the consortium",
            "npc:Excellent work, human.",
        })
        t.ticks(3)
        t.quest.expect_complete()
        t.check("reward.mining", t.skill.expect_gain("mining", 2500, before))
        t.check("reward.smithing", t.skill.expect_gain("smithing", 2500, before))
        t.check("reward.crafting", t.skill.expect_gain("crafting", 2500, before))
        t.check("reward.magic", t.skill.expect_gain("magic", 1500, before))
        t.check("reward.thieving", t.skill.expect_gain("thieving", 1500, before))
        t.check("reward.firemaking", t.skill.expect_gain("firemaking", 1500, before))
        t.finish(0)
    end,
}
