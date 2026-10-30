-- Holy Grail. Hand-authored against OSRS-Content/osrs-content/server/
-- scripts/quests/quest_grail/scripts/*.rs2 (quest_grail.rs2, king_arthur.rs2,
-- merlin.rs2, high_priest_of_entrana.rs2/grail_crone.rs2, brother_galahad.rs2,
-- black_knight_titan.rs2, grail_realm_npcs.rs2, fisher_king.rs2,
-- sir_percival.rs2) -- every branch below was read from the .rs2 source.
-- Since 2026-10-30 the pages, menus, whistle, bell, sacks and stairs follow
-- RSProx index 4435 (docs/rsprox-analysis/quests/holy_grail.md); the route
-- notes below predate it where they say otherwise (the bell now lands inside
-- the ground floor, 2761,4692, and the Fisher King is reached by the stairs).
--
-- Route (state machine on %grail, quest_grail.constant):
--   king_arthur (grailstart, needs %arthur=arthur_complete) -> grail_started
--   Camelot, upstairs: open merlinworkshop door (spawns merlin2, the ONLY
--   npc_add for him) -> talk merlin2 -> grail_spoken_merlin
--   Port Sarim monk -> Entrana: high_priest_of_entrana chains into
--   grail_crone.rs2's [label,grail_crone] in the same conversation ->
--   grail_spoken_crone
--   brother_galahad: "I seek an item from the realm of the Fisher King."
--   -> holy_table_napkin
--   Draynor Manor whistle room: [oploc1,whistledoor] with the napkin held
--   drops magic_whistle on the room's own tile -> pick up two
--   Excalibur out of the bank (the guide's goGetExcalibur)
--   blow a whistle under the tower NW of Brimhaven -> the corrupted realm
--   black_knight_titan: real fight, killing blow with Excalibur WORN
--   grail_fisherman: choice 2 drops grail_bell outside the castle wall
--   ring the held bell on its tile -> inside, 2761,4692; spiral stairs up
--   fisher_king "You don't look too well." -> grail_finding_percival
--   whistle out; king_arthur again -> magic_golden_feather
--   Goblin Village percy_sacks op2 (feather held) -> sir_percival ->
--   "Your father wishes to speak to you." -> grail_given_whistle
--   whistle again -> the RESTORED realm; up the castle to the holy_grail
--   whistle out; king_arthur with the grail -> grail_quest_complete
--   Rewards: 11000 Prayer XP, 15300 Defence XP, 2 quest points.
--
-- THE WHOLE ROUTE IS WALKED (docs/QUEST_ORCHESTRATOR.md standing rule,
-- owner 2026-10-03: a goto into or out of any closed space is a cheat).
-- goto_tile is used only for overland hops between open tiles (reach.py,
-- every door shut, recorded in build/orchestrator/fix_b61/grail.progress.md).
-- Everything else is a click, every visit, both ways:
--   * Camelot: the courtyard gate (kr_camelot_metalgateclosedl, north wall
--     of 2757,3482), the castle's large doors (kr_cam_doubledoorl, north
--     wall of 2757,3503), kr_cam_woodenstairs (maplink 2750,3508,0 <->
--     2750,3513,1), Merlin's workshop door (merlinworkshop 2764,3503,1,
--     east wall, door_selfstage).
--   * Entrana is an island: shipmonk's crossing (no weapon or armour carried
--     or worn: the port_sarim monk searches, monk_of_entrana.rs2
--     ~has_entrana_restricted_items) to the deck 2834,3331,1 and
--     ship_from_entrana_off ashore; shipmonk2 back to the deck at Port Sarim
--     3048,3231,1 and ship_to_entrana_off ashore.
--   * Galahad's house: castledoubledoorl (south wall of 2613,3481).
--   * Draynor Manor: the front doors (haunteddoorr 3109,3353, a walk-through
--     that refuses from inside), the hall door D1 3109,3358, the stairs
--     (3108,3361,0 <-> 3108,3366,1), the spiral stairs (3106,3362,1 ->
--     3105,3364,2 -> 3106,3363,1), the whistle room's door (whistledoor
--     3106,3361,2, north wall, door_selfstage); out through D2 3106,3368,
--     the kitchen door D6 3120,3356 and hauntedbackdoor 3123,3361.
--   * Falador west bank: its doorway holds only the map's inactive open
--     leaves (fai_falador_bank_door_inactive_l/r, no op), so in and out are
--     walks. (Not Draynor's bank: see goGetExcalibur below.)
--   * Karamja is an island: seaman_lorris's paid crossing to the deck at
--     Musa Point 2956,3143,1, sarimshipplank_off ashore, the members' gate
--     (membergatel 2816,3182, gates.rs2 walk-through) every crossing. Off
--     the island by the realm's whistle (it lands under the tower) and a
--     Camelot Teleport.
--   * Goblin Village: the sacks' hut door (goblin_outpost_poordoor
--     2958,3506, east wall).
--   * The corrupted realm is walked: the whistle lands on the EAST bank,
--     the titan's side of the bridge (^grail_realm_entry_coord
--     0_43_73_54_43 = 2806,4715, as LostCity quest_grail.rs2:44; OSRS-Content
--     690a24bc57); a walk to the bridge's east end 2793,4722 (reach.py
--     len=20) and the titan is fought from there. The bridge's middle tile
--     2791,4722 is the titan's own (f1), so it is crossed only by
--     defeat_titan's p_teleport (black_knight_titan.rs2:52-56), which lands
--     the player at 2790,4722, the far end. From there the fisherman
--     (2800,4706) is a walk south along the river (reach.py len=28; wiki
--     Holy_Grail: "cross the bridge and then head south along the river").
--   * The corrupted realm's castle is never walked into: the held bell is
--     rung from the tile the bell was picked up on (the guide's ringBell,
--     2762,4694), outside the wall, and its own p_teleport lands the player
--     beside the Fisher King.
--   * The restored castle: castledoubledoorr (south wall of 2634,4693), the
--     diagonal poordoor 2638,4688, poordoor 2645,4684 (east wall), the
--     spiralstairs 2648,4683,0 (no maplink: ladders.rs2 [proc,climb] is one
--     plane on the tile you stand on) and the ladder 2651,4684,1.
--   * Long trips are standard-spellbook teleports cast by click
--     (t.player.teleport_cast: TELEPORTED, exact runes, landing):
--     magic_spells.dbrow Camelot 45 air5/law1 (x4), Falador 37
--     water1/air3/law1 (x2), Lumbridge 31 earth1/air3/law1 (x1).
--
-- Gear: Excalibur and the rune armour are staged in the BANK in setup
-- (::bankgive), and taken out by click at Falador's west bank after the manor --
-- the guide's own goGetExcalibur ("Go retrieve Excalibur from your bank").
-- Excalibur itself is Merlin's Crystal's reward (::complete
-- quest_merlinscrystal in setup); the armour and sharks are the Titan
-- fight's bring-alongs.

return {
    id = "grail",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000, -- two Karamja crossings, Entrana both ways, seven teleports, every door on foot
    setup = {
        "::clearinv",
        "::bankgive excalibur 1",
        "::bankgive rune_chainbody 1",
        "::bankgive rune_platelegs 1",
        "::bankgive rune_full_helm 1",
        "::bankgive rune_kiteshield 1",
        "::give shark 10",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::complete quest_merlinscrystal",
        -- Travel a player uses: two seaman_lorris fares (sailors.rs2
        -- karamja_sailor_pay, 30 coins each) and seven teleports
        -- (magic_spells.dbrow: Camelot x4 = air 20 law 4, Falador x2 =
        -- air 6 water 2 law 2, Lumbridge x1 = air 3 earth 1 law 1).
        "::give coins 60",
        "::setlevel magic 45",
        "::give airrune 29",
        "::give lawrune 7",
        "::give waterrune 2",
        "::give earthrune 1",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp5_grail",
            constants = {
                complete = 10,
                failed_defeat_titan = 7,
                finding_percival = 8,
                given_whistle = 9,
                not_started = 0,
                spoken_crone = 4,
                spoken_merlin = 3,
                started = 2,
            },
            row = "quest_holygrail",
            display = "Holy Grail",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- the setup cheats are not client-side yet
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        -- The wire audit's tick log (docs/rsprox-analysis/quests/holy_grail.md).
        t.ticklog.start()

        -- ===============================================================
        -- Helpers: a walk graded on its tile, the Camelot doors, the
        -- teleports, the boats.
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

        -- A walk (the client's pathfinder, never a teleport) graded on the
        -- tile it reached, on the level it started on.
        local function walk(name, x, z, level, tol)
            tol = tol or 0
            local wr, wd = t.player.walk_to(x, z)
            local r, tt = t.world.tile()
            if not (r == "ok" and math.abs(tt.x - x) <= tol and math.abs(tt.z - z) <= tol) then
                t.ticks(2)
                wr, wd = t.player.walk_to(x, z)
                r, tt = t.world.tile()
            end
            t.check(name, r == "ok" and math.abs(tt.x - x) <= tol and math.abs(tt.z - z) <= tol and tt.level == level,
                "walk_to " .. x .. "," .. z .. " -> " .. tostring(wr) .. " (" .. tostring(wd) .. "); at "
                    .. tile_text(r, tt) .. " (want within " .. tol .. " on level " .. level .. ")")
        end

        local function camelot_teleport(name)
            t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = name,
                runes = { { "airrune", 5 }, { "lawrune", 1 } }, where = "Camelot, tele_coord 0_43_54_5_22" })
        end
        local function falador_teleport(name)
            t.player.teleport_cast("falador_teleport", { 2965, 3378, 0 }, { name = name,
                runes = { { "waterrune", 1 }, { "airrune", 3 }, { "lawrune", 1 } }, where = "Falador, tele_coord 0_46_52_21_50" })
        end
        local function lumbridge_teleport(name)
            t.player.teleport_cast("lumbridge_teleport", { 3221, 3218, 0 }, { name = name,
                runes = { { "earthrune", 1 }, { "airrune", 3 }, { "lawrune", 1 } }, where = "Lumbridge, tele_coord 0_50_50_21_18" })
        end

        -- Camelot: the courtyard gate (north wall of 2757,3482) and the
        -- castle's large doors (north wall of 2757,3503), both ways.
        local function camelot_in(prefix)
            walk(prefix .. ".toGate", 2757, 3481, 0)
            t.exec(prefix .. ".courtyardGateIn", t.player.pass_door, { closed = "kr_camelot_metalgateclosedl",
                open = "kr_camelot_metalgateopenl", at = { 2757, 3482, 0 }, near = { 2757, 3482 }, far = { 2757, 3484 } })
            t.exec(prefix .. ".castleDoorIn", t.player.pass_door, { closed = "kr_cam_doubledoorl",
                open = "kr_cam_doubledoorl_open", at = { 2757, 3503, 0 }, near = { 2757, 3503 }, far = { 2757, 3505 } })
            -- Across the hall to the king's end of the round table: from the
            -- door, the client's viewport often has him under the side panel.
            walk(prefix .. ".toArthur", 2764, 3513, 0, 1)
        end
        local function camelot_out(prefix)
            t.exec(prefix .. ".castleDoorOut", t.player.pass_door, { closed = "kr_cam_doubledoorl",
                open = "kr_cam_doubledoorl_open", at = { 2757, 3503, 0 }, near = { 2757, 3504 }, far = { 2757, 3502 } })
            t.exec(prefix .. ".courtyardGateOut", t.player.pass_door, { closed = "kr_camelot_metalgateclosedl",
                open = "kr_camelot_metalgateopenl", at = { 2757, 3482, 0 }, near = { 2757, 3483 }, far = { 2757, 3480 } })
        end

        -- Karamja: seaman_lorris's paid crossing (sailors.rs2
        -- karamja_sailor_talk -> karamja_sailor_pay: 30 coins, p_delay(2),
        -- p_telejump(1_46_49_12_7) = the deck at Musa Point 2956,3143,1, then
        -- its own mesbox), the gangplank ashore (gangplank.rs2
        -- gangplank_disembark, a north plank: 2956,3146,0), overland to the
        -- members' gate, through it, overland to the tower.
        local function to_brimhaven_tower(prefix)
            t.exec("goto-" .. prefix .. ".seaman", t.player.goto_tile, 3028, 3221, 0)
            local coins0_r, coins0 = t.inv.count("coins")
            t.exec(prefix .. ".talkToSeaman", t.player.talk_to, "seaman_lorris", 1)
            t.exec(prefix .. ".talkToSeaman-dialog", t.chat.play, {
                "npc:Do you want to go on a trip to Karamja?",
                "npc:The trip will cost you 30 coins.",
                "options",
                "choose:Yes please.",
                "player:Yes please.",
            })
            local sail_r, sail_d = t.await({
                level = function()
                    return t.chat.kind() == "mesbox"
                end,
                note = prefix .. ": the arrival mesbox after p_delay(2) + telejump",
            }, 15)
            t.step(prefix .. ".sail", sail_r == "ok" and "PASS" or "FAIL",
                "await(chat.kind() == mesbox) -> " .. tostring(sail_r) .. " " .. tostring(sail_d))
            t.exec(prefix .. ".arrive", t.chat.play, { "mesbox:The ship arrives at Karamja." })
            local deck_r, deck = t.world.tile()
            local coins1_r, coins1 = t.inv.count("coins")
            t.check(prefix .. ".onDeckAtMusaPoint", deck_r == "ok" and deck.level == 1
                    and math.abs(deck.x - 2956) <= 2 and math.abs(deck.z - 3143) <= 2
                    and coins0_r == "ok" and coins1_r == "ok" and coins0 - coins1 == 30,
                "tile " .. tile_text(deck_r, deck) .. " (want the deck 2956,3143,1), coins " .. tostring(coins0)
                    .. " -> " .. tostring(coins1) .. " (want -30)")
            t.exec(prefix .. ".disembark", t.player.climb, { loc = "sarimshipplank_off", op_name = "Cross",
                at = { 2956, 3144, 1 }, src = { 2956, 3143 }, dest = { 2956, 3146, 0 }, slack = 1 })
            t.exec("goto-" .. prefix .. ".memberGate", t.player.goto_tile, 2818, 3182, 0)
            t.exec(prefix .. ".memberGate", t.player.cross_gate, { loc = "membergatel", at = { 2816, 3182, 0 },
                near = { 2817, 3182 }, far_ok = function(tile) return tile.x <= 2815 end,
                far_desc = "west of the gate in Brimhaven, x <= 2815" })
            -- Under the tower's four pillars (2740..2743, 3234..3237): RSProx
            -- index 4435 blew from 2742,3236 [2086] and 2742,3235 [3267].
            t.exec("goto-" .. prefix, t.player.goto_tile, 2742, 3236, 0)
        end

        -- ---------------------------------------------------------------
        -- startQuest: Camelot Teleport from Lumbridge, through the courtyard
        -- gate and the castle's large doors to King Arthur. Only reached once
        -- %arthur = arthur_complete (king_arthur.rs2:58-59).
        -- ---------------------------------------------------------------
        camelot_teleport("startQuest.camelotTeleport")
        camelot_in("startQuest")
        t.exec("startQuest", t.player.talk_to, "king_arthur", 1)
        t.exec("startQuest-dialog", t.chat.play, {
            "player:Now I am a knight of the round table",
            "npc:Aha! I'm glad you are here!",
            "choose:Tell me of this quest.",
            "player:Tell me of this quest.",
            "npc:Well, we recently found out that the Holy Grail",
            "npc:This is most fortuitous!",
            "npc:None of my knights ever did return with it",
            "choose:Yes.",
            "player:I'd enjoy trying that.",
            "npc:Go speak to Merlin.",
            "npc:He has set up his workshop",
        })
        t.check("quest.stage.started", t.quest.expect_stage("started"))
        t.expect("quest.started_msg", t.msg.expect("Holy Grail</col>"))

        -- ---------------------------------------------------------------
        -- Merlin's workshop, upstairs: kr_cam_woodenstairs (maplink
        -- 0_42_54_62_52 -> 1_42_54_62_57), then [oploc1,merlinworkshop],
        -- the ONLY npc_add for merlin2 (quest_grail.rs2:27), pressed from
        -- outside ($entering) -- door_selfstage: open, it is the same symbol
        -- one tile over.
        -- ---------------------------------------------------------------
        walk("goUpStairsCamelot.toStairs", 2750, 3508, 0)
        t.exec("goUpStairsCamelot", t.player.climb, { loc = "kr_cam_woodenstairs", op_name = "Climb-up",
            at = { 2750, 3509, 0 }, src = { 2750, 3508 }, dest = { 2750, 3513, 1 } })
        t.exec("openMerlinDoor", t.player.pass_door, { closed = "merlinworkshop", open = "merlinworkshop",
            at = { 2764, 3503, 1 }, near = { 2764, 3503 }, far = { 2765, 3503 } })
        t.exec("talkToMerlin", t.player.talk_to, "merlin2", 1)
        t.exec("talkToMerlin-dialog", t.chat.play, {
            "player:Hello. King Arthur has sent me",
            "npc:Ah yes... the Holy Grail...",
            "npc:That is a powerful artefact in",
            "npc:Due to its nature the Holy Gra",
            "player:Any suggestions?",
            "npc:I believe there is a holy isla",
            "npc:I suppose you could also try s",
            "npc:He returned from the quest man",
            "choose:Where can I find Sir Galahad?",
            "player:Where can I find Sir Galahad?",
            "npc:Galahad now lives a life of re",
        })
        t.check("quest.stage.spoken_merlin", t.quest.expect_stage("spoken_merlin"))
        t.exec("talkToMerlin.workshopDoorOut", t.player.pass_door, { closed = "merlinworkshop", open = "merlinworkshop",
            at = { 2764, 3503, 1 }, near = { 2765, 3503 }, far = { 2763, 3503 } })
        walk("talkToMerlin.toStairsTop", 2750, 3513, 1)
        t.exec("talkToMerlin.stairsDown", t.player.climb, { loc = "kr_cam_woodenstairstop", op_name = "Climb-down",
            at = { 2750, 3511, 1 }, src = { 2750, 3513 }, dest = { 2750, 3508, 0 } })
        camelot_out("talkToMerlin")

        -- ---------------------------------------------------------------
        -- goToEntrana: "Bank all combat gear." The gear was never taken out
        -- of the bank; the monk's search (port_sarim/scripts/
        -- monk_of_entrana.rs2 ~has_entrana_restricted_items, pack AND worn)
        -- is read off the player first.
        -- ---------------------------------------------------------------
        falador_teleport("goToEntrana.faladorTeleport")
        local FORBIDDEN = { "excalibur", "rune_chainbody", "rune_platelegs", "rune_full_helm", "rune_kiteshield" }
        local carried, carry_text = false, ""
        for _, item in ipairs(FORBIDDEN) do
            local cr, c = t.inv.count(item)
            local ur = t.player.unequip(item)
            if not (cr == "ok" and c == 0 and ur == "not_found") then
                carried = true
            end
            carry_text = carry_text .. string.format("%s pack %s(%s) worn %s; ", item, tostring(c), tostring(cr), tostring(ur))
        end
        t.check("goToEntrana.noWeaponOrArmour", not carried,
            carry_text .. "(want every one 0 in the pack and not_found worn: the gear is still in the bank)")
        t.exec("goto-goToEntrana", t.player.goto_tile, 3045, 3236, 0)
        t.exec("goToEntrana", t.player.talk_to, "shipmonk", 1)
        t.exec("goToEntrana-dialog", t.chat.play, {
            "npc:Do you seek passage to holy Entrana?",
            "choose:Yes, okay, I'm ready to go.",
            "player:Yes, okay, I'm ready to go.",
            "npc:Very well. One moment please.",
            "mesbox:The monk quickly searches you.",
            "npc:All is satisfactory. You may board the boat now.",
        })
        -- ship_journey holds the player at sea for 14 ticks (^journey_ticks_entrana).
        await_tile(function(tt) return tt.level == 1 and tt.x < 2900 end, 25, "goToEntrana")
        local entrana_deck_r, entrana_deck = t.world.tile()
        t.check("goToEntrana.onDeck", entrana_deck_r == "ok" and entrana_deck.level == 1
                and math.abs(entrana_deck.x - 2834) <= 2 and math.abs(entrana_deck.z - 3331) <= 2,
            "t.world.tile() -> " .. tile_text(entrana_deck_r, entrana_deck)
                .. " (want the deck, p_telejump(1_44_52_18_3) = 2834,3331,1)")
        t.exec("goToEntrana.disembark", t.player.climb, { loc = "ship_from_entrana_off", op_name = "Cross",
            at = { 2834, 3333, 1 }, src = { 2834, 3332 }, dest = { 2834, 3335, 0 }, slack = 1 })

        -- ---------------------------------------------------------------
        -- High Priest of Entrana: with %grail in [spoken_merlin,
        -- finding_percival) the same conversation chains into grail_crone.rs2
        -- (high_priest_of_entrana.rs2:55-64). Open ground from the jetty
        -- (reach.py 2834,3335 -> 2851,3348: 42 tiles, every door shut).
        -- ---------------------------------------------------------------
        t.exec("goto-talkToHighPriest", t.player.goto_tile, 2851, 3348, 0)
        t.exec("talkToHighPriest", t.player.talk_to, "high_priest_of_entrana", 1)
        t.exec("talkToHighPriest-dialog", t.chat.play, {
            "npc:Many greetings. Welcome to our",
            "choose:Ask about the Holy Grail Quest.",
            "player:Hello, I am in search of the Holy Grail.",
            "npc:The object of which you speak did once pass",
            "npc:Nor do I really care.",
            "npc:Did you say the Grail?",
            "player:Well I would, but I don't know where I am going!",
            "npc:Go to where the six heads face",
            "choose:Ok, I will go searching.",
            "player:Ok, I will go searching.",
            "npc:Good luck with that.",
        })
        t.check("quest.stage.spoken_crone", t.quest.expect_stage("spoken_crone"))

        -- Off Entrana the way it was reached: shipmonk2 (spawn 2830,3335,
        -- areas/entrana/scripts/monk_of_entrana.rs2 shipmonk2_ready ->
        -- p_telejump(1_47_50_40_31) = the deck at Port Sarim 3048,3231,1),
        -- then ship_to_entrana_off (3048,3232) ashore.
        t.exec("goto-leaveEntrana", t.player.goto_tile, 2832, 3336, 0)
        t.exec("leaveEntrana", t.player.talk_to, "shipmonk2", 1)
        t.exec("leaveEntrana-dialog", t.chat.play, {
            "npc:Do you wish to leave holy Entrana?",
            "choose:Yes, I'm ready to go.",
            "player:Yes, I'm ready to go.",
            "npc:Okay, let's board",
        })
        await_tile(function(tt) return tt.level == 1 and tt.x > 3000 and tt.x < 3060 end, 25, "leaveEntrana")
        local sarim_deck_r, sarim_deck = t.world.tile()
        t.check("leaveEntrana.onDeckAtPortSarim", sarim_deck_r == "ok" and sarim_deck.level == 1
                and math.abs(sarim_deck.x - 3048) <= 2 and math.abs(sarim_deck.z - 3231) <= 2,
            "t.world.tile() -> " .. tile_text(sarim_deck_r, sarim_deck)
                .. " (want the deck, p_telejump(1_47_50_40_31) = 3048,3231,1)")
        t.exec("leaveEntrana.disembark", t.player.climb, { loc = "ship_to_entrana_off", op_name = "Cross",
            at = { 3048, 3232, 1 }, src = { 3048, 3231 }, dest = { 3048, 3234, 0 }, slack = 1 })

        -- ---------------------------------------------------------------
        -- Brother Galahad, west of McGrubor's Wood: choice 4 needs
        -- %grail=spoken_crone and no napkin held (brother_galahad.rs2:43-49,
        -- 61-65). Camelot to his front door is open ground (reach.py
        -- 2757,3478 -> 2613,3481: 196 tiles, every door shut); his house's
        -- double door (south wall of 2612-2613,3481) is pressed both ways.
        -- ---------------------------------------------------------------
        camelot_teleport("talkToGalahad.camelotTeleport")
        t.exec("goto-talkToGalahad", t.player.goto_tile, 2613, 3483, 0)
        t.exec("talkToGalahad.houseDoorIn", t.player.pass_door, { closed = "castledoubledoorl",
            open = "opencastledoubledoorl", at = { 2613, 3481, 0 }, near = { 2613, 3481 }, far = { 2613, 3479 } })
        t.exec("talkToGalahad", t.player.talk_to, "brother_galahad", 1)
        t.exec("talkToGalahad-dialog", t.chat.play, {
            "npc:Welcome to my home.",
            "mesbox:Brother Galahad hangs a kettle",
            "choose:I seek an item from the realm of the Fisher King.",
            "player:I seek an item from the realm of the Fisher King.",
            "npc:Funny you should mention that",
            "player:I don't suppose I could borrow that",
            "mesbox:Galahad reluctantly passes you a small cloth.",
        })
        t.check("talkToGalahad.napkin", t.inv.expect_has("holy_table_napkin", 1))
        t.exec("talkToGalahad.houseDoorOut", t.player.pass_door, { closed = "castledoubledoorl",
            open = "opencastledoubledoorl", at = { 2613, 3481, 0 }, near = { 2613, 3480 }, far = { 2613, 3483 } })

        -- ---------------------------------------------------------------
        -- Draynor Manor (Lumbridge Teleport, then open ground: reach.py
        -- 3221,3218 -> 3108,3351, 261 tiles). The front doors are a
        -- walk-through pressed from the south (quest_haunted.rs2
        -- open_manor_entrance refuses from inside), the hall door, both
        -- staircases by click, then the whistle room's door: with the napkin
        -- held [oploc1,whistledoor] drops magic_whistle on the room's own
        -- tile (^grail_whistle_room_coord 3107,3359,2).
        -- ---------------------------------------------------------------
        lumbridge_teleport("goToDraynorManor.lumbridgeTeleport")
        t.exec("goto-goToDraynorManor", t.player.goto_tile, 3109, 3351, 0)
        t.exec("enterDraynorManor", t.player.pass_door, { closed = "haunteddoorr", open = "haunteddoorr_inactive",
            at = { 3109, 3353, 0 }, near = { 3109, 3352 }, far = { 3109, 3354 } })
        t.exec("goUpStairsDraynor1.hallDoor", t.player.pass_door, { closed = "draynor_panelled_door",
            open = "draynor_panelled_door_open", at = { 3109, 3358, 0 }, near = { 3109, 3357 }, far = { 3109, 3359 } })
        t.exec("goUpStairsDraynor1", t.player.climb, { loc = "draynor_manor_stairs_up", op_name = "Climb-up",
            at = { 3108, 3362, 0 }, src = { 3108, 3361 }, dest = { 3108, 3366, 1 } })
        t.exec("goUpStairsDraynor2", t.player.climb, { loc = "draynor_spiralstairs", op_name = "Climb-up",
            at = { 3104, 3362, 1 }, src = { 3106, 3362 }, dest = { 3105, 3364, 2 } })
        t.exec("openWhistleDoor", t.player.pass_door, { closed = "whistledoor", open = "whistledoor",
            at = { 3106, 3361, 2 }, near = { 3106, 3362 }, far = { 3106, 3360 } })
        -- takeWhistles: the door's two obj_add calls put two whistles on the
        -- room's tile; Take until two are held (a Take that answers before
        -- the second obj reaches the client's pool is tried again a tick
        -- later). One row for the loop's outcome; the door is not pressed
        -- again (runs 1-8 and accounts grail_b/grail_c: two Takes, every time).
        local whistle_log = ""
        local function whistle_count()
            local r, n = t.inv.count("magic_whistle")
            return (r == "ok") and n or -1
        end
        for attempt = 1, 4 do
            if whistle_count() >= 2 then
                break
            end
            local cr = t.player.click_obj("magic_whistle", 3)
            whistle_log = whistle_log .. string.format("take %d: %s -> held %d; ", attempt, tostring(cr), whistle_count())
            t.ticks(1)
        end
        local held = whistle_count()
        local wr, wt = t.world.tile()
        t.check("takeWhistles", held >= 2 and wr == "ok" and wt.level == 2,
            whistle_log .. "magic_whistle held " .. held .. " (want 2), at " .. tile_text(wr, wt) .. " (the room, level 2)")

        -- Out of the manor: the whistle room's door (left open), both
        -- staircases down, D2 3106,3368 to the north corridor, round to the
        -- kitchen door D6 3120,3356 and out of hauntedbackdoor 3123,3361
        -- (door_selfstage: open, the same symbol one tile south).
        t.exec("takeWhistles.roomDoorOut", t.player.pass_door, { closed = "whistledoor", open = "whistledoor",
            at = { 3106, 3361, 2 }, near = { 3106, 3361 }, far = { 3106, 3363 } })
        t.exec("takeWhistles.spiralDown", t.player.climb, { loc = "sarim_spiralstairstop", op_name = "Climb-down",
            at = { 3105, 3363, 2 }, src = { 3105, 3364 }, dest = { 3106, 3363, 1 } })
        t.exec("takeWhistles.stairsDown", t.player.climb, { loc = "draynor_manor_stairs_down", op_name = "Climb-down",
            at = { 3108, 3364, 1 }, src = { 3108, 3366 }, dest = { 3108, 3361, 0 } })
        t.exec("takeWhistles.wingDoor", t.player.pass_door, { closed = "draynor_panelled_door",
            open = "draynor_panelled_door_open", at = { 3106, 3368, 0 }, near = { 3106, 3368 }, far = { 3106, 3369 } })
        t.exec("takeWhistles.corridor", t.player.walk_route,
            { { 3106, 3369 }, { 3113, 3370 }, { 3114, 3362 }, { 3113, 3356 }, { 3119, 3356 } }, { level = 0 })
        t.exec("takeWhistles.kitchenDoor", t.player.pass_door, { closed = "draynor_panelled_door",
            open = "draynor_panelled_door_open", at = { 3120, 3356, 0 }, near = { 3119, 3356 }, far = { 3121, 3356 } })
        walk("takeWhistles.toBackDoor", 3123, 3360, 0)
        t.exec("takeWhistles.backDoorOut", t.player.pass_door, { closed = "hauntedbackdoor", open = "hauntedbackdoor",
            at = { 3123, 3361, 0 }, near = { 3123, 3360 }, far = { 3123, 3362 } })

        -- ---------------------------------------------------------------
        -- goGetExcalibur: "Go retrieve Excalibur from your bank." Falador's
        -- west bank (reach.py 3123,3362 -> 2945,3373: 295 tiles, every door
        -- shut); its doorway holds only the map's inactive open leaves
        -- (fai_falador_bank_door_inactive_l/r, no op), so in and out are
        -- walks. Not Draynor's bank: arriving in map square 48,50 runs the
        -- engine's region-music unlock, whose table carries the music
        -- VARIABLE index (5, "Unknown Land") as a raw varp id and sets bit 5
        -- of varp 5 = %varp5_grail (spoken_crone 4 -> 36; run 4 of this
        -- fixer, src/torirsserver/torirs_server_world.c music unlock +
        -- torirs_server_music_regions.gen.h row 12338) -- an engine seam,
        -- reported, not a game rule.
        -- ---------------------------------------------------------------
        local GEAR = { "excalibur", "rune_chainbody", "rune_platelegs", "rune_full_helm", "rune_kiteshield" }
        t.exec("goto-goGetExcalibur", t.player.goto_tile, 2945, 3376, 0)
        walk("goGetExcalibur.bankIn", 2946, 3368, 0)
        t.exec("goGetExcalibur.bankOpen", t.bank.open, "fai_falador_bankbooth", 2, { at = { 2946, 3367 } })
        for _, item in ipairs(GEAR) do
            t.exec("goGetExcalibur.withdraw." .. item, t.bank.withdraw, item, 1)
        end
        t.check("goGetExcalibur.bankClose", t.bank.close())
        t.exec("goGetExcalibur", t.player.equip, "excalibur")
        t.exec("goGetExcalibur.chainbody", t.player.equip, "rune_chainbody")
        t.exec("goGetExcalibur.platelegs", t.player.equip, "rune_platelegs")
        t.exec("goGetExcalibur.fullhelm", t.player.equip, "rune_full_helm")
        t.exec("goGetExcalibur.kiteshield", t.player.equip, "rune_kiteshield")
        walk("goGetExcalibur.bankOut", 2945, 3376, 0)

        -- ---------------------------------------------------------------
        -- To the tower NW of Brimhaven and blow a whistle: %grail <
        -- given_whistle routes to the CORRUPTED realm entry
        -- (quest_grail.rs2:113-132).
        -- ---------------------------------------------------------------
        to_brimhaven_tower("goToTeleportLocation1")
        -- A bare teleport on the click: no seq, no sound, no line (4435 [2086]).
        t.exec("blowWhistle1", t.player.inv_op, "magic_whistle", 1)
        t.ticks(2)
        -- The whistle lands on the EAST bank, the titan's side of the bridge
        -- (quest_grail.constant ^grail_realm_entry_coord 0_43_73_54_43, as
        -- LostCity quest_grail.rs2:44).
        local realm1_result, realm1_tile = t.world.tile()
        t.check("blowWhistle1.arrived", realm1_result == "ok" and realm1_tile.level == 0
                and math.abs(realm1_tile.x - 2806) <= 1 and math.abs(realm1_tile.z - 4715) <= 1,
            "t.world.tile() -> " .. tile_text(realm1_result, realm1_tile)
                .. " (grail_realm_entry_coord 0_43_73_54_43 = 2806,4715,0)")

        -- ---------------------------------------------------------------
        -- Black Knight Titan (combat_stats.generated.npc: 142 hp, attack 91,
        -- strength 100; docs/bosses/quest_combat_manifest.json
        -- quest-holy-grail). The killing blow must land with Excalibur WORN
        -- (black_knight_titan.rs2 defeat_titan; worn since goGetExcalibur).
        -- Margin row: lowest hp >= 25 (a quarter of 99) AND sharks left.
        -- ---------------------------------------------------------------
        -- Walked from the landing to the titan's east end (reach.py
        -- 2806,4715 -> 2793,4722 REACH closed-doors len=20).
        walk("attackTitan.toEastEnd", 2793, 4722, 0)
        t.exec("attackTitan-talk", t.player.talk_to, "black_knight_titan", 1)
        t.exec("attackTitan-dialog", t.chat.play, {
            "npc:I am the Black Knight Titan!",
            "choose:Okay, have at ye oh evil knight!",
            "player:Okay, have at ye oh evil knight!",
        })
        t.exec("attackTitan", t.player.attack, "black_knight_titan", 2, 20)
        local titan_r, titan_d = t.npc.await_dead_engaged(300, 12, { eat = { item = "shark", below = 60 } })
        t.step("attackTitan.dead", titan_r == "ok" and "PASS" or "FAIL", tostring(titan_r) .. " " .. tostring(titan_d))
        local titan_low = tonumber(string.match(tostring(titan_d), "lowest hp (%d+)/") or "")
        local shark_r, sharks = t.inv.count("shark")
        t.check("attackTitan.margin", titan_low ~= nil and titan_low >= 25 and shark_r == "ok" and sharks >= 1,
            "Black Knight Titan with Excalibur: lowest hp " .. tostring(titan_low) .. "/99, sharks left "
                .. tostring(sharks) .. " of 10 staged (" .. tostring(shark_r)
                .. ") (margin: lowest hp >= 25, a quarter of 99, AND food left)")
        t.ticks(2)
        -- black_knight_titan.rs2: [ai_queue3] queues queue_defeat_titan on the
        -- player; the npc is held until that queue decides (engine seam
        -- matthew-mbp-m4-b61-seam1). [label,defeat_titan] reads
        -- inv_total(worn, excalibur): worn -> mes "Well done! You have
        -- defeated the Black Knight Titan!" and the npc stays dead, %grail
        -- untouched; not worn -> heal, "Maybe you need something more to
        -- beat the titan?" and spoken_crone -> failed_defeat_titan.
        local well_r, well_d = t.msg.expect("Well done! You have defeated the Black Knight Titan!")
        t.check("attackTitan.defeated", well_r, well_d)
        local puny_r, puny_d = t.msg.expect("Maybe you need something more to beat the titan?")
        t.check("attackTitan.notHealed", puny_r == "refused",
            "Excalibur-missing branch line: msg.expect -> " .. tostring(puny_r) .. " " .. tostring(puny_d)
                .. " (want refused: defeat_titan took the worn-Excalibur branch)")
        t.check("attackTitan.withExcalibur", t.quest.expect_stage("spoken_crone"))
        -- defeat_titan p_teleports the player to the far side of the titan
        -- (black_knight_titan.rs2:52-56, same as LostCity's): fought from the
        -- east end of the bridge, that is 2790,4722, the bridge's west end.
        local landed_r, landed = t.world.tile()
        t.check("attackTitan.crossedBridge", landed_r == "ok" and landed.level == 0
                and landed.x == 2790 and landed.z == 4722,
            "t.world.tile() -> " .. tile_text(landed_r, landed)
                .. " (want 2790,4722,0: movecoord(npc_coord 2791,4722, -1, 0, 0))")


        -- ---------------------------------------------------------------
        -- grail_fisherman: choice 2 drops grail_bell at 0_43_73_10_22 =
        -- 2762,4694, outside the castle's north wall (grail_realm_npcs.rs2).
        -- ---------------------------------------------------------------
        -- South along the river on foot (reach.py 2790,4722 -> 2800,4706
        -- REACH closed-doors len=28; wiki: "head south along the river").
        -- An unhappy peasant on the way (4435 [2567]).
        t.exec("talkToUnhappyPeasant", t.player.talk_to, "unhappy_peasant", 1)
        t.exec("talkToUnhappyPeasant-dialog", t.chat.play, {
            "npc:Woe is me! Our crops are all failing",
        })
        walk("talkToFisherman.walk", 2800, 4706, 0)
        t.exec("talkToFisherman", t.player.talk_to, "grail_fisherman", 1)
        t.exec("talkToFisherman-dialog", t.chat.play, {
            "npc:Hi! I don't get many visitors ",
            "choose:Any idea how to get into the castle?",
            "player:Any idea how to get into the c",
            "npc:Why, that's easy!",
            "npc:Just ring one of the bells out",
            "player:...I didn't see any bells.",
            "npc:You must be blind then. There'",
        })

        -- ---------------------------------------------------------------
        -- pickupBell / ringBell: the bell is the player's own, laid on
        -- 2762,4694 outside the castle's north wall; the ground menu carries
        -- only Take, so it is picked up and rung HELD from that tile, as the
        -- recorder did (4435 [2722..2733]): "Ting-a-ling-a-ling!", the
        -- maiden's welcome, "Somehow you are now inside the castle." and the
        -- move to 2761,4692,0. The Fisher King is upstairs: the spiral stairs
        -- at 2761,4680 land on 2762,4682,1 (4435 [2807..2809]).
        -- ---------------------------------------------------------------
        t.exec("goto-pickupBell", t.player.goto_tile, 2762, 4695, 0)
        local take_bell_result, take_bell_detail = t.player.click_obj("grail_bell", 3)
        local bell_r, bell_n = t.inv.count("grail_bell")
        t.step("pickupBell", take_bell_result == "ok" and bell_r == "ok" and bell_n == 1 and "PASS" or "FAIL",
            "click_obj(grail_bell) -> " .. tostring(take_bell_result) .. " " .. tostring(take_bell_detail)
                .. "; grail_bell held " .. tostring(bell_n))
        walk("ringBell.onBellTile", 2762, 4694, 0)
        t.exec("ringBell", t.player.inv_op, "grail_bell", 1)
        t.exec("ringBell-dialog", t.chat.play, {
            "mesbox:Ting-a-ling-a-ling!",
            "npc:Welcome to the Grail castle. You should come",
            "mesbox:Somehow you are now inside the castle.",
        })
        local inside_r, inside = t.world.tile()
        t.check("ringBell.inside", inside_r == "ok" and inside.level == 0 and inside.x == 2761 and inside.z == 4692,
            "landed " .. tile_text(inside_r, inside) .. " (want 2761,4692,0: 4435 [2733])")
        -- The recorder's way to those stairs: round the hall and through two
        -- poordoors, 2763,4676 and 2758,4681 (4435 [2741..2806]).
        -- A maiden inside (4435 [2738]).
        t.exec("talkToGrailMaiden", t.player.talk_to, "grail_maiden", 1)
        t.exec("talkToGrailMaiden-dialog", t.chat.play, { "npc:Welcome to the Grail castle." })
        walk("goUpBrokenCastleStairs.toDoor1", 2763, 4677, 0)
        t.exec("goUpBrokenCastleStairs.door1", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 2763, 4676, 0 }, near = { 2763, 4676 }, far = { 2761, 4676 } })
        t.exec("goUpBrokenCastleStairs.door2", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 2758, 4681, 0 }, near = { 2758, 4680 }, far = { 2759, 4681 } })
        t.exec("goUpBrokenCastleStairs", t.player.climb, { loc = "spiralstairs", op_name = "Climb-up",
            at = { 2761, 4680, 0 }, src = { 2760, 4681 }, dest = { 2762, 4682, 1 } })
        -- Upstairs, the door east of the landing and the diagonal door by the
        -- Fisher King's room (4435 [2814..2833]).
        t.exec("toFisherKing.door1", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 2766, 4684, 1 }, near = { 2765, 4684 }, far = { 2767, 4684 } })
        t.exec("toFisherKing.door2", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 2766, 4688, 1 }, near = { 2766, 4687 }, far = { 2767, 4687 } })

        -- ---------------------------------------------------------------
        -- Fisher King: "You don't look too well." sets grail_finding_
        -- percival (fisher_king.rs2 @fisher_king_well).
        -- ---------------------------------------------------------------
        t.exec("talkToFisherKing", t.player.talk_to, "fisher_king", 1)
        t.exec("talkToFisherKing-dialog", t.chat.play, {
            "npc:Ah! You got inside at last!",
            "choose:You don't look too well.",
            "player:You don't look too well.",
            "npc:Nope, I don't feel so good eit",
            "npc:I fear my life is running shor",
            "npc:If you could find my son, that",
            "player:Who is your son?",
            "npc:He is known as Percival.",
            "npc:I believe he is a knight of th",
            "player:I shall go and see if I can fi",
        })
        t.check("quest.stage.finding_percival", t.quest.expect_stage("finding_percival"))

        -- Exit the realm: blowing inside the bounding box always routes back
        -- to the Brimhaven tower (quest_grail.rs2:114-118).
        t.exec("blowWhistleExit1", t.player.inv_op, "magic_whistle", 1)
        t.ticks(2)
        local exit1_result, exit1_tile = t.world.tile()
        t.check("blowWhistleExit1.arrived", exit1_result == "ok" and exit1_tile.level == 0
                and exit1_tile.x == 2741 and exit1_tile.z == 3235,
            "t.world.tile() -> " .. tile_text(exit1_result, exit1_tile)
                .. " (want 2741,3235,0: 4435 [3427])")

        -- ---------------------------------------------------------------
        -- King Arthur again: %grail=finding_percival hands out
        -- magic_golden_feather (king_arthur.rs2:24-41).
        -- ---------------------------------------------------------------
        camelot_teleport("talkToKingArthur2.camelotTeleport")
        camelot_in("talkToKingArthur2")
        t.exec("talkToKingArthur2", t.player.talk_to, "king_arthur", 1)
        t.exec("talkToKingArthur2-dialog", t.chat.play, {
            "player:Hello, do you have a knight na",
            "npc:Ah yes. I remember young Perci",
            "npc:He was going to try and recove",
            "player:Any idea which way that would ",
            "npc:Not exactly.",
            "npc:They certainly point somewhere",
            "npc:Just blowing gently on them",
            "objbox:King Arthur gives you a feather.",
        })
        t.check("talkToKingArthur2.feather", t.inv.expect_has("magic_golden_feather", 1))
        camelot_out("talkToKingArthur2")

        -- ---------------------------------------------------------------
        -- Goblin Village (Falador Teleport, then open ground: reach.py
        -- 2965,3378 -> 2958,3506): the sacks stand in a hut whose door is
        -- goblin_outpost_poordoor (east wall of 2958,3506). op2 Open, with
        -- the feather held and grail=finding_percival, adds sir_percival on
        -- 2961,3505 (sir_percival.rs2 [oploc2,percy_sacks]).
        -- ---------------------------------------------------------------
        falador_teleport("openSack.faladorTeleport")
        t.exec("goto-openSack", t.player.goto_tile, 2956, 3506, 0)
        t.exec("openSack.hutDoorIn", t.player.pass_door, { closed = "goblin_outpost_poordoor",
            open = "goblin_outpost_openpoordoor", at = { 2958, 3506, 0 }, near = { 2958, 3506 }, far = { 2960, 3506 } })
        -- Open answers with a mesbox; on its click Percival climbs out on
        -- 2961,3505 and his first page opens (4435 [3069..3071]).
        t.exec("openSack", t.player.click_loc, "percy_sacks", 2)
        t.exec("talkToPercival-dialog", t.chat.play, {
            "mesbox:You hear muffled noises from the sack. You open the sack.",
            "npc:Wow, thank you! I could hardly breathe",
            "choose:Your father wishes to speak to you.",
            "player:Your father wishes to speak to you.",
            "npc:My father? You have spoken to him recently?",
            "player:He is dying and wishes you to be his heir.",
            "npc:I have been told that before.",
            "npc:I have not been able to find that castle again though",
            "player:Well, I do have the means to get us there",
            "mesbox:You give a whistle to Sir Percival.",
            "npc:Ok, I will see you there then!",
        })
        t.check("quest.stage.given_whistle", t.quest.expect_stage("given_whistle"))
        t.exec("talkToPercival.hutDoorOut", t.player.pass_door, { closed = "goblin_outpost_poordoor",
            open = "goblin_outpost_openpoordoor", at = { 2958, 3506, 0 }, near = { 2959, 3506 }, far = { 2956, 3506 } })

        -- ---------------------------------------------------------------
        -- Brimhaven again (Goblin Village -> Port Sarim is open ground:
        -- reach.py 2956,3506 -> 3028,3220, 366 tiles); %grail >=
        -- given_whistle now routes straight to the RESTORED realm.
        -- ---------------------------------------------------------------
        to_brimhaven_tower("goToTeleportLocation2")
        t.exec("blowWhistle2", t.player.inv_op, "magic_whistle", 1)
        t.ticks(2)
        -- The restored realm's landing (quest_grail.constant
        -- ^grail_realm_restored_coord 0_41_73_54_43, as LostCity
        -- quest_grail.rs2:42); the castle's south door is an open-ground hop
        -- from there (reach.py 2678,4715 -> 2634,4695 at margin 80).
        local realm2_result, realm2_tile = t.world.tile()
        t.check("blowWhistle2.arrived", realm2_result == "ok" and realm2_tile.level == 0
                and math.abs(realm2_tile.x - 2678) <= 1 and math.abs(realm2_tile.z - 4715) <= 1,
            "t.world.tile() -> " .. tile_text(realm2_result, realm2_tile)
                .. " (grail_realm_restored_coord 0_41_73_54_43 = 2678,4715,0)")

        -- ---------------------------------------------------------------
        -- The restored castle (maps/m41_73.jl2): the guide's
        -- openFisherKingCastleDoor (castledoubledoorr, south wall of
        -- 2634,4693), the diagonal poordoor 2638,4688 between the entrance
        -- rooms and the east hall, poordoor 2645,4684 (east wall) into the
        -- tower, the spiralstairs 2648,4683 (goUpNewCastleStairs; no
        -- maplink: [proc,climb] moves one plane on the approach tile) and
        -- the ladder 2651,4684,1 (goUpNewCastleLadder). The holy_grail
        -- spawns at 2649,4684,2 (m41_73.spawn); opobj3 needs %grail >=
        -- given_whistle.
        -- ---------------------------------------------------------------
        t.exec("goto-openFisherKingCastleDoor", t.player.goto_tile, 2634, 4695, 0)
        t.exec("openFisherKingCastleDoor", t.player.pass_door, { closed = "castledoubledoorr",
            open = "opencastledoubledoorr", at = { 2634, 4693, 0 }, near = { 2634, 4693 }, far = { 2634, 4691 } })
        walk("goUpNewCastleStairs.toHallDoor", 2637, 4688, 0)
        t.exec("goUpNewCastleStairs.hallDoor", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 2638, 4688, 0 }, near = { 2637, 4688 }, far = { 2639, 4688 } })
        walk("goUpNewCastleStairs.toTowerDoor", 2645, 4684, 0)
        t.exec("goUpNewCastleStairs.towerDoor", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 2645, 4684, 0 }, near = { 2645, 4684 }, far = { 2646, 4684 } })
        -- The spiral stairs land on 2649,4685,1 (4435 [3406..3408]); the
        -- ladder climbs on the player's tile (4435 [3410..3413]).
        t.exec("goUpNewCastleStairs", t.player.climb, { loc = "spiralstairs", op_name = "Climb-up",
            at = { 2648, 4683, 0 }, src = { 2647, 4684 }, dest = { 2649, 4685, 1 } })
        walk("goUpNewCastleLadder.toLadder", 2651, 4685, 1)
        t.exec("goUpNewCastleLadder", t.player.climb, { loc = "ladder", op_name = "Climb-up",
            at = { 2651, 4684, 1 }, src = { 2651, 4685 }, dest = { 2651, 4685, 2 } })
        local take_grail_result, take_grail_detail = t.player.click_obj("holy_grail", 3)
        t.step("takeGrail", take_grail_result == "ok" and "PASS" or "FAIL",
            tostring(take_grail_result) .. " " .. tostring(take_grail_detail))
        t.check("takeGrail.held", t.inv.expect_has("holy_grail", 1))

        -- Exit the realm one last time (lands under the Brimhaven tower).
        t.exec("blowWhistleExit2", t.player.inv_op, "magic_whistle", 1)
        t.ticks(2)
        local exit2_result, exit2_tile = t.world.tile()
        t.check("blowWhistleExit2.arrived", exit2_result == "ok" and exit2_tile.level == 0
                and exit2_tile.x == 2741 and exit2_tile.z == 3235,
            "t.world.tile() -> " .. tile_text(exit2_result, exit2_tile)
                .. " (want 2741,3235,0: 4435 [3427])")

        -- ---------------------------------------------------------------
        -- Return to King Arthur: holding holy_grail + grail=given_whistle
        -- queues grail_quest_complete (king_arthur.rs2:44-49). The skill
        -- snapshot is taken after the teleport so the reward rows measure
        -- only the completion's own stat_advance calls.
        -- ---------------------------------------------------------------
        camelot_teleport("finishQuest.camelotTeleport")
        camelot_in("finishQuest")
        local snapshot_result, snapshot = t.skill.snapshot()
        t.step("reward.snapshot", snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot before hand-in -> " .. tostring(snapshot_result))
        t.exec("finishQuest", t.player.talk_to, "king_arthur", 1)
        t.exec("finishQuest-dialog", t.chat.play, {
            "npc:How goes thy quest?",
            "player:I have retrieved the Grail!",
            "npc:Wow! Incredible!",
        })
        t.ticks(3)

        t.quest.expect_complete()
        t.expect("finishQuest.completed_msg", t.msg.expect("Congratulations, you've completed a quest: @q_mes@Holy Grail</col>"))
        t.check("reward.prayer", t.skill.expect_gain("prayer", 11000, snapshot))
        t.check("reward.defence", t.skill.expect_gain("defence", 15300, snapshot))
        t.check("finishQuest.grailHandedIn", t.inv.expect_absent("holy_grail"))

        t.finish(0)
    end,
}
