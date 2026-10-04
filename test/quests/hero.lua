-- Heroes' Quest -- driven for real through the Phoenix Gang route
-- (Achietties -> Straven -> Alfonse -> Charlie the cook -> the misc key on
-- the side door -> ::hero_partner_lure, Grip shot through the arrow slit
-- from inside the secret room -> ::hero_partner_candlestick -> Straven's
-- candlestick hand-in for the armband), then the solo-collectible arms
-- (Ice Queen -> ice gloves; Gerrant -> Blamish oil -> oily rod -> the
-- Jailer -> lava eel -> cook it; then, every armed fight done, the weapons
-- and armour banked at Draynor and the Entrana firebird killed unarmed ->
-- feather), then the real hand-in to Achietties. Never ::setvar on
-- %heroquest itself past `hero_started` -- every later value is written by
-- the content this file actually clicks/fights through.
--
-- Travel (owner rule 2026-10-03: a goto into or out of ANY closed space is
-- a cheat). Every goto below departs from and lands on an open outdoor tile;
-- everything closed is crossed by its own click, in and out:
--   * the Taverley members' wall, on all five trips (Lumbridge -> guild,
--     guild -> Varrock, Varrock -> rockslide, Port Sarim -> Taverley
--     Dungeon, Port Sarim -> guild): membergater 2935,3450 (east gate) or
--     2933,3320 (south gate), a gates.rs2 walk-through, via taverley_gate();
--   * the Phoenix hideout: fai_varrock_poor_door 3241,3382 (the WEST room
--     holds the ladder; the north door 3246,3386 leads to the other room),
--     fai_varrock_ladder_deep 3244,3383 down (maplink src 3243,3383) and
--     phoenixladder up (src 3243,9783 -> 3244,3382), the door again out;
--   * Karamja is an island: seaman_lorris's paid crossing to the deck at
--     Musa Point (sailors.rs2 karamja_sailor_pay, 30 coins), the gangplank
--     ashore, the Karamja members' gate (membergatel 2816,3182, gates.rs2
--     walk-through), the restaurant's doorway (its poshdooropen stands open
--     in the map), herokitchendoor, herokitchenpanel, poordoor 2782,3194 and
--     pete_sidedoor; Varrock Teleport leaves the island from the secret room;
--   * White Wolf Mountain: herorockslide mined from the mainland (west) side,
--     then the guide's five ladders (takeLadder1Down..takeLadder5Down) on
--     their maplink src tiles, the two tunnels walked; Falador Teleport out;
--   * Entrana is an island: the monk's crossing (no weapon or armour
--     carried: banked at Draynor, whose doorway the map leaves open),
--     ship_from_entrana_off ashore, shipmonk2's crossing back,
--     ship_to_entrana_off ashore;
--   * Taverley Dungeon: the ladder, cauldrondoor (three presses: two suits of
--     armour, then the walk-through), metalgateclosedl, the Black Knights'
--     castledoubledoorl, dungeonjail in and out, castledoubledoorl back,
--     deepdungeondoor with the dusty key; Lumbridge Teleport out.
-- Each teleport is a spellbook click graded on three rows (TELEPORTED, the
-- exact runes, the landing); levels and runes are staged in setup.
--
-- Prerequisites cheated in setup, never the quest's own deliverable (trap
-- 16): 55 QP, Lost City (%zanaris), Dragon Slayer I (%dragonquest),
-- Merlin's Crystal (%arthur) and the Phoenix side of Shield of Arrav
-- (%phoenixgang) are FOUR OTHER quests achietties.rs2's own
-- ~has_hero_quest_requirements gate demands before Heroes' Quest can even
-- be accepted (docs/quests/heroes_quest.md section 2). ::complete has no
-- arm for quest_lostcity/quest_merlinscrystal/quest_dragonslayer1 in this
-- checkout's quest_cheat.rs2 (grepped -- Shield of Arrav's own arm
-- (quest_shieldofarrav) only ever writes %blackarmgang, never %phoenixgang
-- either), so the varps are set directly with ::setvar, the same documented
-- cheat ladder ::give/::setlevel/::setvar/::kill/::spawn/::tele/::goto
-- (QUEST_AUTHORING.md section 3) blackknight.lua already uses for its own
-- %qp prerequisite. phoenixkey2 is the tradeable key Straven's own
-- straven_gangmember label (areas/varrock/scripts/straven.rs2:97-102)
-- hands out on joining the Phoenix Gang -- a Shield-of-Arrav bring-along,
-- not this quest's own deliverable, so it is given rather than earned by
-- replaying that quest.
--
-- Combat/skill LEVELS and plain bring-along tools/food/ingredients are all
-- prerequisites, the same "gear is a prerequisite" idiom hunt.lua/
-- mortton.lua/rovingelves.lua already use for a quest fight -- never the
-- quest's own deliverable, which HeroesQuest.java's own ItemRequirement set
-- confirms: only `iceGloves` carries `.canBeObtainedDuringQuest()` (grepped
-- /Users/matthewevers/Documents/git_repos/quest-helper/.../HeroesQuest.java)
-- -- fishingRod, fishingBait, harralanderUnf (harralandervial, the
-- unfinished potion Blamish Slime mixes into) and pickaxe are NOT marked
-- that way, so they are given directly. blamish_snail_slime, blamish_oil,
-- oily_fishing_rod, raw_lava_eel, lava_eel, hot_feather and
-- master_thief_armband ARE the quest's own deliverables and are driven for
-- real below. petecandlestick is the partner's half: Grip's keyring and the
-- treasure room lie behind the Black Arm-only garvdoor, so the partner loots
-- the chest and trades one over (Quest Helper getCandlestick, "Get your
-- candlestick from your partner"); ::hero_partner_candlestick stands in for
-- that trade, at the player's own kill credit only (docs/QUEST_SERVER_CHEATS.md
-- section D, beside ::hero_partner and ::hero_partner_lure).
--
-- The candlestick-chest content bug this file used to stop at
-- (brimhaven_scarface_mansion.rs2's opencandlechest write clobbering a
-- Phoenix player's %varp188_heroquest with the Black Arm route's own checkpoint,
-- past hero_phoenix_obtained_armband) is FIXED as of the committed source
-- (brimhaven_scarface_mansion.rs2:134, gated on
-- `%heroquest >= ^hero_blackarm_gangmember_spoken`, which a Phoenix player
-- sitting at hero_phoenix_killed_grip(5) never satisfies). A solo Phoenix
-- player no longer opens that chest (the partner does): this file asserts
-- the stage at phoenix_killed_grip right after the kill
-- (quest.stage.phoenix_killed_grip), takes the partner's candlestick, and
-- drives on to Straven for the armband for real.

return {
    id = "hero",
    fixture = "fresh_lumbridge.ini",
    max_frames = 360000, -- three dungeons, two islands and the Phoenix cellar twice, all on foot between the gotos
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::setvar varp101_qp 55", -- prerequisite quest points (hero_required_questpoints), not this quest's own reward
        "::setvar varp147_zanaris 6", -- Lost City complete (zanaris_complete) -- no ::complete arm for quest_lostcity exists
        "::setvar varp176_dragonquest 10", -- Dragon Slayer I complete (dragon_complete) -- no ::complete arm for quest_dragonslayer1 exists
        "::setvar varp14_arthur 7", -- Merlin's Crystal complete (arthur_complete) -- no ::complete arm for quest_merlinscrystal exists
        "::setvar varp145_phoenixgang 10", -- Shield of Arrav, Phoenix side, complete (phoenixgang_complete) -- ::complete quest_shieldofarrav only ever writes %blackarmgang
        "::complete quest_druidicritual", -- Druidic Ritual, the DBROW name (all.dbrow.compack:35), not the quest_druid folder name (QUEST_AUTHORING.md docs/quests notes) -- ~herblore_unlocked (quest_druid.rs2:35-39) gates ~attempt_brew_potion on %druidquest >= ^druid_complete, and Heroes' Quest's own Blamish-oil mix (brew_potion.rs2:513-516) needs Herblore unlocked; this is a bring-along prerequisite (trap 16), not the quest's own deliverable
        "::give phoenixkey2 1", -- Shield-of-Arrav bring-along Straven's own script hands a joined Phoenix member, not Heroes' Quest's own deliverable
        -- Combat levels: prerequisite for BOTH real fights below (Grip,
        -- level 22; Ice Queen, level 111, hp104/atk95/str94/def95,
        -- combat_stats.generated.npc:14496-14516) -- same idiom
        -- rovingelves.lua uses for its own level-111-class moss guardian.
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::give rune_mace 1", -- crush weapon: Ice Queen's own lowest defence stat is crushdefence=20 (combat_stats.generated.npc:14515), vs slashdefence=40/stabdefence=30 -- dragon_mace is unusable here, levelrequire.rs2:171-176 refuses to Wear it until %varp188_heroquest >= ^hero_complete, which is this quest's OWN completion; rune_mace carries no such gate
        "::give rune_platebody 1",
        "::give rune_platelegs 1",
        "::give rune_full_helm 1",
        "::give rune_kiteshield 1",
        "::give shark 8", -- food for the Ice Queen fight and the dungeon walks, same idiom as mortton.lua/rovingelves.lua's own "::give shark" food
        -- Fire-arm + lava-eel-arm bring-alongs -- none of these carry
        -- .canBeObtainedDuringQuest() in HeroesQuest.java, unlike iceGloves.
        "::give fishing_rod 1",
        "::give fishing_bait 20",
        "::give harralandervial 1", -- "Harralander potion (unf)" -- the solvent brew.dbrow's herblore_blamish_oil row names
        "::give logs 1",
        "::give tinderbox 1",
        "::give rune_pickaxe 1", -- QH mineEntranceRocks: the rockslide needs a pickaxe (white_wolf_mountain.rs2:23-32)
        "::give magic_shortbow 1", -- QH rangedMage bring-along (HeroesQuest.java:214)
        "::give rune_arrow 100",
        "::setlevel ranged 99", -- killGrip with the shortbow (HeroesQuest.java:214)
        "::setlevel mining 50", -- the rockslide's own gate (white_wolf_mountain.rs2:29)
        "::setlevel fishing 99", -- lava eel fishing gate is level 53 (lavafish.rs2:90)
        "::setlevel cooking 99", -- lava eel cooking gate is level 53 (cooking_generic.dbrow's cooking_generic_lava_eel row, never burns)
        "::setlevel herblore 99", -- Blamish oil mixing gate is level 25 (brew.dbrow's herblore_blamish_oil row)
        "::setlevel firemaking 99", -- lighting the cooking fire is a roll (stat_random(firemaking,64,512), firemaking.rs2:80) -- near-certain first swing at 99, never a level GATE (logs need only level 1)
        -- Travel a player uses: the Karamja fare and three standard
        -- teleports (magic_spells.dbrow: Varrock 25 fire1/air3/law1,
        -- Lumbridge 31 earth1/air3/law1, Falador 37 water1/air3/law1).
        "::give coins 30", -- seaman_lorris's fare to Musa Point (sailors.rs2 karamja_sailor_pay)
        "::setlevel magic 37",
        "::give lawrune 3",
        "::give airrune 9",
        "::give firerune 1",
        "::give earthrune 1",
        "::give waterrune 1",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp188_heroquest",
            constants = {
                not_started = 0,
                started = 1,
                phoenix_gangmember_spoken = 2,
                phoenix_talked_alfonse = 3,
                phoenix_talked_charlie = 4,
                phoenix_killed_grip = 5,
                phoenix_obtained_armband = 6,
                blackarm_gangmember_spoken = 7,
                blackarm_hq_door_unlocked = 8,
                blackarm_id_papers_obtained = 9,
                blackarm_mansion_unlocked = 10,
                blackarm_id_papers_given = 11,
                blackarm_looted_chest = 12,
                blackarm_obtained_armband = 13,
                complete = 15,
            },
            row = "quest_heroes", -- docs/quests/heroes_quest.md section 2: "Cache row | quest_heroes"
            display = "Heroes' Quest",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        t.ticks(3) -- the ::setvar/::give cheats above are not client-side yet
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ===============================================================
        -- Route helpers.
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

        -- Hitpoints are sampled after every walk hop, crossing and fight
        -- round; below EAT_BELOW a shark is eaten. A fight's margin row
        -- reads the lowest sample since fight_begin().
        local EAT_BELOW = 60
        local hp_low, hp_eaten = nil, 0
        local function vitals()
            local hr, hp = t.skill.read("hitpoints")
            if hr == "ok" and type(hp) == "table" and hp.level then
                if hp_low == nil or hp.level < hp_low then
                    hp_low = hp.level
                end
                if hp.level < EAT_BELOW then
                    local er = t.player.inv_op("shark", 1)
                    if er == "ok" then
                        hp_eaten = hp_eaten + 1
                    end
                    t.ticks(1)
                end
            end
        end
        local function fight_begin()
            hp_low = nil
            vitals()
        end
        local function margin_row(name, fight)
            vitals()
            local fr, food = t.inv.count("shark")
            t.check(name, hp_low ~= nil and hp_low >= 25 and fr == "ok" and food >= 1,
                fight .. ": lowest hp " .. tostring(hp_low) .. "/99 (sampled every round), sharks staged 8, eaten so far "
                    .. hp_eaten .. ", left " .. tostring(food) .. " (" .. tostring(fr)
                    .. ") (margin: lowest hp >= 25, a quarter of 99, AND food left)")
        end

        -- A walk hop graded on the tile it reached.
        local function hop(name, x, z, ticks, tol)
            tol = tol or 1
            local wr, wd = t.player.walk_to(x, z, ticks)
            local r, tt = t.world.tile()
            if not (r == "ok" and math.abs(tt.x - x) <= tol and math.abs(tt.z - z) <= tol) then
                t.ticks(2)
                wr, wd = t.player.walk_to(x, z, ticks)
                r, tt = t.world.tile()
            end
            t.check(name, r == "ok" and math.abs(tt.x - x) <= tol and math.abs(tt.z - z) <= tol and tt.level == 0,
                "walk_to " .. x .. "," .. z .. " -> " .. tostring(wr) .. " (" .. tostring(wd) .. "); at " .. tile_text(r, tt)
                    .. " (want within " .. tol .. ", level 0)")
            vitals()
        end
        local function hops(prefix, list, ticks)
            for i, p in ipairs(list) do
                hop(prefix .. "." .. i, p[1], p[2], ticks or 40, p[3])
            end
        end

        -- A door that swings (next_loc_stage pair): walk to the near side;
        -- if the closed leaf stands on the door tile on this level, click
        -- THAT copy; otherwise an earlier press left it open, so assert the
        -- open leaf stands within a tile of the door tile (a row that fails
        -- when neither leaf is there) and walk through without pressing.
        local function pass_door(prefix, closed_sym, open_sym, door_x, door_z, near_x, near_z, far_x, far_z, far_ok, far_desc)
            t.player.walk_to(near_x, near_z, 40)
            local nr, nt = t.world.tile()
            t.check(prefix .. ".atDoor", nr == "ok" and math.abs(nt.x - near_x) <= 1 and math.abs(nt.z - near_z) <= 1,
                "walked to " .. near_x .. "," .. near_z .. " beside the door at " .. door_x .. "," .. door_z .. " -> " .. tile_text(nr, nt))
            local lvl = (nr == "ok") and nt.level or 0
            local cr, cd = t.world.loc_near(closed_sym, 3)
            if cr == "ok" and cd.tile_x == door_x and cd.tile_z == door_z and cd.level == lvl then
                t.exec(prefix .. ".openDoor", t.player.click_loc, closed_sym, 1, { at = { door_x, door_z } })
                t.ticks(1)
            else
                local orr, od = t.world.loc_near(open_sym, 3)
                t.check(prefix .. ".doorStandsOpen", orr == "ok" and od.level == lvl
                        and math.abs(od.tile_x - door_x) <= 1 and math.abs(od.tile_z - door_z) <= 1,
                    closed_sym .. " at " .. door_x .. "," .. door_z .. ": "
                        .. (cr == "ok" and ("nearest closed copy at " .. cd.tile_x .. "," .. cd.tile_z .. "," .. cd.level) or tostring(cr))
                        .. "; " .. open_sym .. ": " .. (orr == "ok" and ("open leaf at " .. od.tile_x .. "," .. od.tile_z .. "," .. od.level) or tostring(orr))
                        .. " (want the open leaf within 1 of the door tile on level " .. lvl .. ": already open, so it is walked through, not pressed again)")
            end
            t.player.walk_to(far_x, far_z, 40)
            local fr, ft = t.world.tile()
            t.check(prefix .. ".throughDoor", fr == "ok" and far_ok(ft),
                "walked through to " .. far_x .. "," .. far_z .. " -> " .. tile_text(fr, ft) .. " (want " .. far_desc .. ")")
            vitals()
        end

        -- A doorway whose door the MAP places open (no closed copy exists):
        -- assert the open leaf really stands on its map tile, then walk
        -- through it and check the far tile.
        local function open_doorway(prefix, open_sym, door_x, door_z, near_x, near_z, far_x, far_z, far_ok, far_desc)
            hop(prefix .. ".atDoorway", near_x, near_z, 40)
            local orr, od = t.world.loc_near(open_sym, 3)
            t.check(prefix .. ".doorStandsOpen", orr == "ok" and od.tile_x == door_x and od.tile_z == door_z and od.level == 0,
                open_sym .. " -> " .. (orr == "ok" and (od.tile_x .. "," .. od.tile_z .. "," .. od.level) or tostring(orr))
                    .. " (want the map's open leaf at " .. door_x .. "," .. door_z .. ",0)")
            t.player.walk_to(far_x, far_z, 40)
            local fr, ft = t.world.tile()
            t.check(prefix .. ".throughDoorway", fr == "ok" and far_ok(ft),
                "walked through to " .. far_x .. "," .. far_z .. " -> " .. tile_text(fr, ft) .. " (want " .. far_desc .. ")")
        end

        -- A scripted walk-through (a door, gate, panel or gangplank whose
        -- script p_teleports the player across): clicked on every crossing
        -- and graded on the tile before (not yet across) and after (across).
        -- A short pushed hop can answer `timeout settle_after_click` although
        -- it landed, so the row stands on the two tiles.
        local function cross(name, sym, lx, lz, far_ok, far_desc)
            local br, bt = t.world.tile()
            local cr, cd = t.player.click_loc(sym, 1, { at = { lx, lz } })
            await_tile(far_ok, 10, name)
            local wr, wt = t.world.tile()
            t.check(name, (cr == "ok" or cr == "timeout") and br == "ok" and not far_ok(bt) and wr == "ok" and far_ok(wt),
                "from " .. tile_text(br, bt) .. " click_loc(" .. sym .. " at " .. lx .. "," .. lz .. ") -> " .. tostring(cr) .. " " .. tostring(cd)
                    .. "; world.tile -> " .. tile_text(wr, wt) .. " (want " .. far_desc .. ")")
            vitals()
        end

        -- A ladder with maplink rows (ladders_stairs/configs/maplink.dbrow
        -- keys ~maplink_try on the PLAYER's coord): walk to the src tile,
        -- click that copy, wait for the landing, grade it on the dest tile.
        local function climb(name, sym, lx, lz, src_x, src_z, dest_x, dest_z)
            t.player.walk_to(src_x, src_z, 40)
            local sr, st = t.world.tile()
            t.check(name .. ".atLadder", sr == "ok" and st.x == src_x and st.z == src_z and st.level == 0,
                "walked to the maplink src " .. src_x .. "," .. src_z .. ",0 beside " .. sym .. " " .. lx .. "," .. lz .. " -> " .. tile_text(sr, st))
            local cr, cd = t.player.click_loc(sym, 1, { at = { lx, lz } })
            await_tile(function(tt) return tt.x == dest_x and tt.z == dest_z end, 8, name)
            local lr, lt = t.world.tile()
            -- Graded on the two tiles: only the climb moves the player from
            -- the src tile onto the dest 6400 tiles away (a press can answer
            -- `refused I can't reach that!` from a re-press after the first
            -- one already climbed: takeLadder1Down, run 1).
            t.check(name, sr == "ok" and st.x == src_x and st.z == src_z and lr == "ok" and lt.x == dest_x and lt.z == dest_z and lt.level == 0,
                "from " .. tile_text(sr, st) .. " click_loc(" .. sym .. " at " .. lx .. "," .. lz .. ") -> " .. tostring(cr) .. " " .. tostring(cd)
                    .. "; landed " .. tile_text(lr, lt) .. " (want the maplink dest " .. dest_x .. "," .. dest_z .. ",0)")
            vitals()
        end

        -- A standard-spellbook teleport pressed in the spellbook
        -- (t.player.cast with no target), graded on three rows: the cast
        -- answered TELEPORTED, the exact runes left the pack, and the
        -- landing is within 2 of the spell's tele_coord (teleport.rs2
        -- map_findsquare radius 2).
        local function teleport(name, spell, runes, want_x, want_z, where)
            local before = {}
            for _, rn in ipairs(runes) do
                local rr, c = t.inv.count(rn[1])
                before[rn[1]] = (rr == "ok") and c or nil
            end
            local br, bt = t.world.tile()
            local cr, cd = t.player.cast(spell)
            await_tile(function(tt) return tt.level == 0 and math.abs(tt.x - want_x) <= 2 and math.abs(tt.z - want_z) <= 2 end, 10, name)
            local wr, wt = t.world.tile()
            t.check(name .. ".cast", cr == "ok" and string.find(tostring(cd), "TELEPORTED", 1, true) ~= nil,
                "from " .. tile_text(br, bt) .. " cast " .. spell .. " -> " .. tostring(cr) .. " " .. tostring(cd))
            local paid, paid_text = true, ""
            for _, rn in ipairs(runes) do
                local rr, c = t.inv.count(rn[1])
                local b = before[rn[1]]
                local after = (rr == "ok") and c or nil
                if b == nil or after == nil or b - after ~= rn[2] then
                    paid = false
                end
                paid_text = paid_text .. string.format("%s %s -> %s (want -%d); ", rn[1], tostring(b), tostring(after), rn[2])
            end
            t.check(name .. ".runes", paid, paid_text)
            t.check(name .. ".landed", wr == "ok" and wt.level == 0 and math.abs(wt.x - want_x) <= 2 and math.abs(wt.z - want_z) <= 2,
                "landed " .. tile_text(wr, wt) .. " (want within 2 of " .. want_x .. "," .. want_z .. ",0, " .. where .. ")")
        end

        -- The Phoenix hideout: the ladder room is the house's WEST room
        -- (x 3242-3244 z 3382-3383, 8 tiles), entered by
        -- fai_varrock_poor_door 3241,3382 (east wall of that tile: outside
        -- x <= 3241). The street tile 3241,3387 is in Varrock's open map
        -- (reach.py floods it to Varrock square with every door shut).
        local function in_ladder_room(tt)
            return tt.level == 0 and tt.x >= 3242 and tt.x <= 3244 and tt.z >= 3382 and tt.z <= 3383
        end
        local function phoenix_in(pfx)
            t.exec("goto-" .. pfx .. ".phoenixStreet", t.player.goto_tile, 3241, 3387, 0)
            pass_door(pfx .. ".houseDoorIn", "fai_varrock_poor_door", "fai_varrock_poor_door_open", 3241, 3382, 3241, 3383, 3243, 3383,
                in_ladder_room, "inside the ladder room, x 3242-3244 z 3382-3383")
            climb(pfx .. ".ladderDown", "fai_varrock_ladder_deep", 3244, 3383, 3243, 3383, 3243, 9783)
        end
        local function phoenix_out(pfx)
            climb(pfx .. ".ladderUp", "phoenixladder", 3244, 9783, 3243, 9783, 3244, 3382)
            pass_door(pfx .. ".houseDoorOut", "fai_varrock_poor_door", "fai_varrock_poor_door_open", 3241, 3382, 3242, 3382, 3241, 3385,
                function(tt) return tt.level == 0 and tt.x <= 3241 end, "back in the street, x <= 3241")
        end

        -- The Taverley members' wall. Owner ruling (sampler-findings.md,
        -- "Sample matthew-mbp-m4-b59" (a)): a gate that is the only way on
        -- foot between two regions is clicked on every crossing, however
        -- large the regions. comp.py floods from Lumbridge, Varrock and Port
        -- Sarim never join the Taverley/Burthorpe side. Both gates are
        -- gates.rs2 [label,member_fencegate_try] walk-throughs (membergatel/r
        -- have no opened variant: nothing stays open after a crossing, so
        -- every crossing is a press, graded on the tiles before and after):
        --   * east: membergater 2935,3450 (rot 2, the east wall of its tile):
        --     Taverley is x <= 2935, the Ice Mountain side x >= 2936;
        --   * south: membergater 2933,3320 (rot 3, the south wall of its
        --     tile): Taverley is z >= 3320, west of Falador z <= 3319.
        -- ~check_axis (door_procs.rs2:112): on the gate's own column/row the
        -- press p_teleports through to the far side; from the other side it
        -- p_teleports onto the gate tile. Every trip goes through this one
        -- helper: goto two tiles short, walk to the tile beside the gate,
        -- press it.
        local TAVERLEY_GATES = {
            east_in = { 2935, 3450, 2938, 3450, 2936, 3450,
                function(tt) return tt.level == 0 and tt.x <= 2935 end, "through the east gate into Taverley, x <= 2935" },
            east_out = { 2935, 3450, 2932, 3450, 2934, 3450,
                function(tt) return tt.level == 0 and tt.x >= 2936 end, "through the east gate out of Taverley, x >= 2936" },
            south_in = { 2933, 3320, 2933, 3317, 2933, 3319,
                function(tt) return tt.level == 0 and tt.z >= 3320 end, "through the south gate into Taverley, z >= 3320" },
        }
        local function taverley_gate(prefix, which)
            local g = TAVERLEY_GATES[which]
            t.exec("goto-" .. prefix .. ".memberGate", t.player.goto_tile, g[3], g[4], 0)
            hop(prefix .. ".memberGate.approach", g[5], g[6], 10, 0)
            cross(prefix .. ".memberGate.cross", "membergater", g[1], g[2], g[7], g[8])
        end

        -- Equip the fight gear now, before anything else -- frees five
        -- backpack slots the fishing-bait/harralander/logs stack still needs,
        -- and both real fights below want it worn from the first swing.
        t.exec("equipMace", t.player.equip, "rune_mace")
        t.exec("equipPlatebody", t.player.equip, "rune_platebody")
        t.exec("equipPlatelegs", t.player.equip, "rune_platelegs")
        t.exec("equipFullHelm", t.player.equip, "rune_full_helm")
        t.exec("equipKiteshield", t.player.equip, "rune_kiteshield")

        -- ---------------------------------------------------------------
        -- Achietties: accept the quest for real. She stands in the guild's
        -- open stone arch (stone_arched_doorbase 2903,3509-3512, no door
        -- loc), past the Taverley members' east gate.
        -- ---------------------------------------------------------------
        taverley_gate("achietties", "east_in")
        t.exec("goto-achietties", t.player.goto_tile, 2903, 3510, 0)
        t.exec("talkToAchietties", t.player.talk_to, "achietties")
        t.exec("talkToAchietties-dialog", t.chat.play, {
            "npc:Greetings. Welcome to the Heroes",
            "npc:Only the greatest heroes of this land",
            "choose:I'm a hero, may I apply to join?",
            "player:I'm a hero. May I apply to join?",
            "npc:Well, you have a lot of quest points",
            "npc:for entrance are:",
            "choose:I'll start looking for all those things then.",
            "player:I'll start looking for all those things then.",
            "npc:Good luck with that.",
        })
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- ---------------------------------------------------------------
        -- Straven: join the Phoenix side of the armband chain. Out of
        -- Taverley by the east gate, then overland to Varrock.
        -- ---------------------------------------------------------------
        taverley_gate("enterPhoenixBase", "east_out")
        phoenix_in("enterPhoenixBase")
        t.exec("talkToStraven-1", t.player.talk_to, "straven")
        t.exec("talkToStraven-1-dialog", t.chat.play, {
            "npc:Greetings fellow gang member.",
            "choose:Is there any way I can get the rank of master thief?",
            "player:Is there any way I can get the rank of master thief?",
            "npc:As it happens, yes. Head to our restaurant front in Brimhaven",
        })
        t.expect("quest.stage.phoenix_gangmember_spoken", t.quest.expect_stage("phoenix_gangmember_spoken"))
        phoenix_out("leavePhoenixBase")

        -- ---------------------------------------------------------------
        -- To Brimhaven: Karamja is an island. seaman_lorris's paid crossing
        -- (sailors.rs2 karamja_sailor_talk -> karamja_sailor_pay: 30 coins,
        -- p_delay(2), p_telejump(1_46_49_12_7) = the ship's deck at Musa
        -- Point, 2956,3143,1, then its own mesbox), the gangplank ashore.
        -- %dragonquest is complete, so the menu is the plain p_choice2.
        -- ---------------------------------------------------------------
        t.exec("goto-seaman", t.player.goto_tile, 3028, 3221, 0)
        local coins0_r, coins0 = t.inv.count("coins")
        t.exec("talkToSeaman", t.player.talk_to, "seaman_lorris", 1)
        t.exec("talkToSeaman-dialog", t.chat.play, {
            "npc:Do you want to go on a trip to Karamja?",
            "npc:The trip will cost you 30 coins.",
            "options",
            "choose:Yes please.",
            "player:Yes please.",
        })
        t.expect("seaman.paidMessage", t.msg.expect("pay the 30 coins and board the ship"))
        local sail_r, sail_d = t.await({
            level = function()
                return t.chat.kind() == "mesbox"
            end,
            note = "seaman: the arrival mesbox after p_delay(2) + telejump",
        }, 15)
        t.step("seaman.sailReopen", sail_r == "ok" and "PASS" or "FAIL",
            "await(chat.kind() == mesbox) -> " .. tostring(sail_r) .. " " .. tostring(sail_d))
        t.exec("seaman.arrive", t.chat.play, { "mesbox:The ship arrives at Karamja." })
        local deck_r, deck = t.world.tile()
        local coins1_r, coins1 = t.inv.count("coins")
        t.check("seaman.onDeckAtMusaPoint", deck_r == "ok" and deck.level == 1 and math.abs(deck.x - 2956) <= 2 and math.abs(deck.z - 3143) <= 2
                and coins0_r == "ok" and coins1_r == "ok" and coins0 - coins1 == 30,
            "tile " .. tile_text(deck_r, deck) .. " (want the deck 2956,3143,1), coins " .. tostring(coins0) .. " -> " .. tostring(coins1) .. " (want -30)")
        cross("musaPoint.disembark", "sarimshipplank_off", 2956, 3144,
            function(tt) return tt.level == 0 and math.abs(tt.x - 2956) <= 3 and tt.z >= 3145 and tt.z <= 3150 end, "ashore on the Musa Point jetty, level 0")

        -- Musa Point -> the Karamja members' gate: overland across the open
        -- jungle path (reach.py: 177 tiles with every door shut). The gate
        -- (membergatel 2816,3182, a west-wall leaf: east of it x >= 2816)
        -- is gates.rs2's member_fencegate_try walk-through.
        t.exec("goto-karamjaGate", t.player.goto_tile, 2818, 3182, 0)
        hop("karamjaGate.approach", 2817, 3182, 10, 0)
        cross("karamjaGate.cross", "membergatel", 2816, 3182,
            function(tt) return tt.level == 0 and tt.x <= 2815 end, "west of the gate in Brimhaven, x <= 2815")

        -- The Shrimp and Parrot: its door is placed OPEN by the map
        -- (poshdooropen 2794,3180; wall line between z 3180 and 3181).
        hop("restaurant.approach", 2801, 3180, 30)
        open_doorway("restaurant.in", "poshdooropen", 2794, 3180, 2794, 3179, 2794, 3183,
            function(tt) return tt.level == 0 and tt.z >= 3181 end, "inside the restaurant, z >= 3181")

        -- ---------------------------------------------------------------
        -- Alfonse: the gherkin password.
        -- ---------------------------------------------------------------
        t.exec("talkToAlfonse", t.player.talk_to, "alfonse_the_waiter")
        t.exec("talkToAlfonse-dialog", t.chat.play, {
            "npc:Welcome to the Shrimp and Parrot.",
            "choose:Do you sell Gherkins?",
            "player:Do you sell Gherkins?",
            "npc:Hmmmm Gherkins eh? Ask Charlie the cook",
            "mesbox:Alfonse winks at you.",
        })
        t.expect("quest.stage.phoenix_talked_alfonse", t.quest.expect_stage("phoenix_talked_alfonse"))

        -- ---------------------------------------------------------------
        -- Into the kitchen (herokitchendoor opens once heroquest >=
        -- phoenix_talked_alfonse & %phoenixgang = phoenixgang_complete,
        -- brimhaven_restaurant.rs2:8-13; a north-wall leaf at 2788,3189,
        -- the kitchen is z >= 3190).
        -- ---------------------------------------------------------------
        cross("openKitchenDoor", "herokitchendoor", 2788, 3189,
            function(tt) return tt.level == 0 and tt.z >= 3190 and tt.x >= 2787 end, "in the kitchen, z >= 3190")

        -- ---------------------------------------------------------------
        -- Charlie the cook: the secret door into Mr Olbors' garden.
        -- ---------------------------------------------------------------
        t.exec("talkToCharlie", t.player.talk_to, "charlie_the_cook")
        t.exec("talkToCharlie-dialog", t.chat.play, {
            "npc:Hey! What are you doing back here?",
            "choose:I'm looking for a gherkin...",
            "player:I'm looking for a gherkin...",
            "npc:Aaaaaah... a fellow Phoenix! So, tell me compadre",
            "choose:I want to steal Scarface Pete's candlesticks.",
            "player:I want to steal Scarface Pete's candlesticks.",
            "npc:Ah yes, of course. The candlesticks.",
            "npc:a little assistance. The setting up of this restaurant",
            "npc:Now, at the other side of Mr Olbors",
            "npc:and we can't seem to find a way through.",
            "player:Mind if I check it out for myself?",
            "npc:Not at all! The more minds we have",
        })
        t.expect("quest.stage.phoenix_talked_charlie", t.quest.expect_stage("phoenix_talked_charlie"))

        -- ---------------------------------------------------------------
        -- Into Mr Olbors' garden (herokitchenpanel writes no state, plain
        -- walk-through, brimhaven_restaurant.rs2:16-22; a west-wall panel
        -- at 2787,3190, the garden is x <= 2786).
        -- ---------------------------------------------------------------
        cross("openKitchenPanel", "herokitchenpanel", 2787, 3190,
            function(tt) return tt.level == 0 and tt.x <= 2786 and tt.z >= 3188 end, "in Mr Olbors' garden, x <= 2786")

        -- ---------------------------------------------------------------
        -- pete_sidedoor: [oploc1] now ALWAYS refuses with "This door is
        -- locked." (2026-09-23 content parity, c8b3e2fede) -- only
        -- [oplocu,pete_sidedoor] with misc_key opens it
        -- (brimhaven_scarface_mansion.rs2:12-58). misc_key is the real
        -- two-player Black-Arm-partner hand-off (Grip's own dialogue,
        -- grip.rs2:50-58) -- this server drives one account, so
        -- ::hero_partner is the documented TEST AFFORDANCE standing in for
        -- exactly that partner action (docs/QUEST_SERVER_CHEATS.md section
        -- D, quest_hero.rs2:266-307), gated the same way the real hand-off
        -- would be (%phoenixgang >= phoenixgang_joined, %heroquest >=
        -- hero_phoenix_talked_charlie -- both already true here). Never a
        -- silent grant; the real click through the door is still driven.
        -- ---------------------------------------------------------------
        local partner_result, partner_detail = t.cheat("::hero_partner")
        t.step("hero_partner.cheat", partner_result == "ok" and "PASS" or "FAIL",
            "t.cheat(::hero_partner) -> " .. tostring(partner_result) .. " " .. tostring(partner_detail))
        -- The cheat's chat reply outruns its own inv_add by up to a tick
        -- (the same shape trap 23 documents for a setup ::give) -- poll,
        -- never a bare count read on the line right after.
        local misckey_result, misckey_detail = t.inv.await("misc_key", 1, 10)
        t.check("misc_key.granted", misckey_result == "ok",
            "inv.await(misc_key, 1, 10) -> " .. tostring(misckey_result) .. " " .. tostring(misckey_detail))

        -- The side door sits on the north wall of a 9-tile lobby
        -- (x 2780-2782 z 3193-3196) the garden reaches only through
        -- poordoor 2782,3194 (an east-wall leaf: the garden is x >= 2783).
        -- The old file's goto onto 2781,3196 jumped that door.
        pass_door("useKeyOnDoor.lobbyDoor", "poordoor", "poordooropen", 2782, 3194, 2783, 3194, 2781, 3195,
            function(tt) return tt.level == 0 and tt.x >= 2780 and tt.x <= 2782 and tt.z >= 3193 and tt.z <= 3196 end,
            "in the lobby, x 2780-2782 z 3193-3196")
        -- pete_sidedoor (m43_49.jl2, rot 3) at 2781,3197: [oploc1]'s own
        -- coordz(coord)=coordz(loc_coord) test wants the z-row approach,
        -- so the key is used from the lobby tile straight below it.
        hop("useKeyOnDoor.belowSideDoor", 2781, 3196, 10, 0)

        -- trap 298: use_on's backpack-tab press is not settled before its
        -- own arming, so an arm issued right after another action can
        -- silently fail and the click degrades to a bare "Walk here".
        t.check("sideDoor.tabInventory", t.ui.tab("inventory") == "ok", "t.ui.tab(inventory)")
        t.ticks(2)
        -- [oplocu,pete_sidedoor]'s own success path is a BARE
        -- ~hero_pete_walk_door -> p_teleport, no chat line and no mesbox,
        -- so this is graded on the tile the walk-door proc moved the player
        -- to, not on the click verb's own settle word.
        local predoor_result, predoor_tile = t.world.tile()
        local sidedoor_target = t.player.by_symbol("loc", "pete_sidedoor")
        local sidedoor_click_result, sidedoor_click_detail = t.player.use_on("misc_key", sidedoor_target)
        await_tile(function(tt) return tt.z >= 3197 end, 6, "useKeyOnSideDoor")
        local postdoor_result, postdoor_tile = t.world.tile()
        t.check("useKeyOnSideDoor", predoor_result == "ok" and postdoor_result == "ok" and predoor_tile.z <= 3196
                and postdoor_tile.z >= 3197 and postdoor_tile.x >= 2780 and postdoor_tile.x <= 2782,
            "use_on(misc_key, pete_sidedoor) -> " .. tostring(sidedoor_click_result) .. " (" .. tostring(sidedoor_click_detail)
                .. "), t.world.tile() " .. tile_text(predoor_result, predoor_tile) .. " -> " .. tile_text(postdoor_result, postdoor_tile)
                .. " (want lobby z <= 3196 -> secret room z >= 3197: hero_pete_walk_door's own bare p_teleport)")

        -- ---------------------------------------------------------------
        -- SEAM hero_partner_lures_grip (matthew-mbp-m4-b51-seam2).
        -- QH secretRoom zone = 2780..2782 x 3197..3198 (HeroesQuest.java:258);
        -- the side door (2781,3197, south edge) opens it from the lobby side.
        -- ---------------------------------------------------------------
        local room_result, room_tile = t.world.tile()
        local in_room = room_result == "ok" and room_tile ~= nil and room_tile.level == 0
            and room_tile.x >= 2780 and room_tile.x <= 2782 and room_tile.z >= 3197 and room_tile.z <= 3198
        t.check("inSecretRoom", in_room, "t.world.tile() -> " .. tile_text(room_result, room_tile)
            .. " (QH secretRoom 2780..2782 x 3197..3198)")

        -- Stand at the arrow slit (snipable_wall 2780,3198, blockrange=0): a
        -- plain walk inside the room, never a goto.
        local slit_walk = t.player.walk_to(2780, 3198, 10)
        local slit_r, slit_tile = t.world.tile()
        t.check("walkToSlit", slit_r == "ok" and slit_tile.x == 2780 and slit_tile.z == 3198,
            "walk_to(2780,3198) -> " .. tostring(slit_walk) .. ", tile " .. tile_text(slit_r, slit_tile))

        t.exec("equipShortbow", t.player.equip, "magic_shortbow")
        t.exec("equipArrows", t.player.equip, "rune_arrow")

        -- BEFORE the lure: Grip at his spawn (2774,3192) is behind walls,
        -- outside the cabinet room the lure walks him into.
        local g0r, g0 = t.npc.nearest("grip", 15)
        t.check("grip.unlured.where", g0r == "ok" and g0 ~= nil
                and not (g0.z >= 3196 and g0.z <= 3198 and g0.x >= 2770 and g0.x <= 2779),
            "npc.nearest(grip,15) -> " .. tostring(g0r) .. " " .. (g0 and (tostring(g0.x) .. "," .. tostring(g0.z)) or "nil")
                .. " (want Grip present and NOT yet in the cabinet room 2770..2779 x 3196..3198)")

        -- The seam, reproduced: with no partner's lure Grip is out of sight
        -- of the slit and the server refuses the swing.
        local u_r, u_d = t.player.attack("grip", 2, 10)
        t.check("killGrip.unlured.refused", u_r == "refused",
            "attack(grip) before any lure -> " .. tostring(u_r) .. " " .. tostring(u_d))
        t.player.walk_to(2780, 3198, 10)

        -- The gate holds before the kill: the partner has no candlestick to
        -- trade until THIS player has killed Grip.
        local pre_r = t.cheat("::hero_partner_candlestick")
        t.ticks(2)
        local pre_c_r, pre_c = t.inv.count("petecandlestick")
        local pre_msg_r, pre_msg = t.msg.expect("HERO_PARTNER_CANDLESTICK FAILED")
        t.check("candlestick.refusedBeforeKill", pre_c_r == "ok" and pre_c == 0 and pre_msg_r == "ok",
            "t.cheat(::hero_partner_candlestick) at stage 4 -> " .. tostring(pre_r) .. ", petecandlestick " .. tostring(pre_c)
            .. ", msg.expect -> " .. tostring(pre_msg_r) .. " " .. tostring(pre_msg))

        -- The partner's lure: ::hero_partner_lure (quest_hero.rs2
        -- [debugproc,hero_partner_lure], mirrors [label,summon_grip] case 1).
        -- One cabinet search moves Grip for the 6-tick hold; a partner whose
        -- Grip has not reached the cabinet room searches again (the cabinet
        -- stays searchable, [oploc1,gripcbopen] -> @summon_grip).
        local lured, g1, lure_tries, lure_trail = false, nil, 0, ""
        for attempt = 1, 3 do
            lure_tries = attempt
            local lure_r, lure_d = t.cheat("::hero_partner_lure")
            local said_r = t.msg.await("Grip lured", 12)
            local g1r
            g1r, g1 = t.npc.nearest("grip", 15)
            lure_trail = lure_trail .. string.format(" #%d cheat=%s done=%s grip=%s", attempt, tostring(lure_r), tostring(said_r),
                g1 and (tostring(g1.x) .. "," .. tostring(g1.z)) or tostring(g1r))
            if g1r == "ok" and g1 ~= nil and g1.z >= 3196 and g1.z <= 3198 and g1.x >= 2770 and g1.x <= 2779 then
                lured = true
                break
            end
        end
        t.check("hero_partner_lure", lured,
            "::hero_partner_lure x" .. lure_tries .. ":" .. lure_trail .. " (want Grip in the cabinet room next to the secret room, 2770..2779 x 3196..3198, walk target 2777,3198)")

        -- killGrip: ranged through the slit, without leaving the room.
        fight_begin()
        local ga_r, ga_d = t.player.attack("grip", 2, 20)
        local gk_r, gk_d = t.npc.await_dead_engaged(60)
        t.check("killGrip", gk_r == "ok",
            "attack(grip) -> " .. tostring(ga_r) .. " " .. tostring(ga_d) .. "; npc.await_dead_engaged(60) -> " .. tostring(gk_r) .. " " .. tostring(gk_d))
        margin_row("killGrip.margin", "Grip, shot through the arrow slit")
        t.ticks(3)
        t.expect("quest.stage.phoenix_killed_grip", t.quest.expect_stage("phoenix_killed_grip"))
        local k_r, k_tile = t.world.tile()
        t.check("killGrip.stillInRoom", k_r == "ok" and k_tile.x >= 2780 and k_tile.x <= 2782 and k_tile.z >= 3197 and k_tile.z <= 3198,
            "t.world.tile() after the kill -> " .. tile_text(k_r, k_tile))

        -- getCandlestick (HeroesQuest.java:412): the partner's trade.
        -- PARTNER: getCandlestick ::hero_partner_candlestick the partner loots the chest behind garvdoor and trades one (HeroesQuest.java:412)
        local cs_r, cs_d = t.cheat("::hero_partner_candlestick")
        t.check("hero_partner_candlestick.cheat", cs_r == "ok", "t.cheat(::hero_partner_candlestick) -> " .. tostring(cs_r) .. " " .. tostring(cs_d))
        local cw_r, cw_d = t.inv.await("petecandlestick", 1, 10)
        t.check("getCandlestick", cw_r == "ok", "inv.await(petecandlestick, 1, 10) -> " .. tostring(cw_r) .. " " .. tostring(cw_d))

        t.exec("reequipMace", t.player.equip, "rune_mace")
        t.exec("reequipKiteshield", t.player.equip, "rune_kiteshield")

        -- ---------------------------------------------------------------
        -- Off the island: Varrock Teleport from the secret room, straight
        -- to the Phoenix hideout's city.
        -- ---------------------------------------------------------------
        teleport("enterPhoenixBaseAgain.varrockTeleport", "varrock_teleport",
            { { "firerune", 1 }, { "airrune", 3 }, { "lawrune", 1 } }, 3213, 3424, "Varrock square, tele_coord 0_50_53_13_32")

        -- ---------------------------------------------------------------
        -- Straven, second visit: hand in a candlestick for the armband
        -- (straven.rs2:97-141's straven_gangmember label, $option=5).
        -- ---------------------------------------------------------------
        phoenix_in("enterPhoenixBaseAgain")
        t.exec("talkToStraven-2", t.player.talk_to, "straven")
        t.exec("talkToStraven-2-dialog", t.chat.play, {
            "npc:Greetings fellow gang member.",
            "choose:I have a candlestick now.",
            "player:I have a candlestick now.",
            "npc:Excellent work. Here",
        })
        t.expect("quest.stage.phoenix_obtained_armband", t.quest.expect_stage("phoenix_obtained_armband"))
        local armband_result, armband_count = t.inv.count("master_thief_armband")
        t.check("straven.armbandGranted", armband_result == "ok" and armband_count == 1,
            "inv.count(master_thief_armband) -> " .. tostring(armband_result) .. " " .. tostring(armband_count))
        phoenix_out("leavePhoenixBaseAgain")

        -- ---------------------------------------------------------------
        -- Ice Queen: real fight for ice_gloves (fire_feather.rs2:20's own
        -- gate on the firebird feather pickup below), level 111,
        -- hp104/atk95/str94/def95 (combat_stats.generated.npc:14496-14516).
        -- She is quest-state-blind (ice_queen.rs2's own ai_queen3 drop has
        -- no %heroquest check at all).
        --
        -- mineEntranceRocks (ObjectID.HEROROCKSLIDE): the 2x2 rockslide at
        -- 2838,3517 (covers x 2838-2839 z 3517-3518, so the guide's
        -- 2839,3518 and the old goto's 2838,3518 are ON the rock) closes a
        -- 79-tile pocket east of it; its WEST side (2837,3518) is the open
        -- mainland (comp.py floods it to Taverley), reached from Varrock
        -- through the Taverley members' east gate.
        -- [oploc2,herorockslide] (white_wolf_mountain.rs2:13-46), gated on
        -- a held pickaxe + Mining 50, has no success mes() -- the tell is
        -- the forcemove into the pocket (coordx not past the rock's 2838:
        -- +2,0 then +1,-1, 2837,3518 -> 2840,3517), read back off
        -- t.world.tile().
        -- ---------------------------------------------------------------
        taverley_gate("mineEntranceRocks", "east_in")
        t.exec("goto-rockslide", t.player.goto_tile, 2837, 3518, 0)
        local rockslide_before_result, rockslide_before = t.world.tile()
        local mine_click_result, mine_click_detail = t.player.click_loc("herorockslide", 2)
        await_tile(function(tt) return tt.x >= 2840 end, 8, "mineRockslide")
        local rockslide_after_result, rockslide_after = t.world.tile()
        t.check("mineRockslide", rockslide_before_result == "ok" and rockslide_after_result == "ok"
                and rockslide_before.x <= 2837 and rockslide_after.x >= 2840 and rockslide_after.level == 0,
            "click_loc(herorockslide, 2) -> " .. tostring(mine_click_result) .. " (" .. tostring(mine_click_detail) .. "), t.world.tile() "
                .. tile_text(rockslide_before_result, rockslide_before) .. " -> " .. tile_text(rockslide_after_result, rockslide_after)
                .. " (want west of the rock x <= 2837 -> the pocket x >= 2840: herorockslide op2's own forcemove after clearing)")

        -- The guide's five ladders, each from its maplink src tile
        -- (maplink.dbrow rows: +-6400 z, same x).
        climb("takeLadder1Down", "ladder_outside_to_underground", 2848, 3513, 2849, 3513, 2849, 9913)
        hops("takeLadder2Up.tunnel", { { 2837, 9907 }, { 2823, 9901 } }, 40)
        climb("takeLadder2Up", "ladder_from_cellar", 2824, 9907, 2825, 9907, 2825, 3507)
        climb("takeLadder3Down", "ladder_outside_to_underground", 2827, 3510, 2828, 3510, 2828, 9910)
        -- The long tunnel to the fourth ladder (231 tiles: reach.py with a
        -- margin of 80; it loops north to z 9972 and back). Hops stay
        -- under ~15 tiles: walk_to answers `refused (move_to)` on a 21-tile
        -- hop here (run 1).
        hops("takeLadder4Up.tunnel", {
            { 2827, 9924 }, { 2826, 9938 }, { 2826, 9953 }, { 2829, 9965 }, { 2839, 9970 },
            { 2851, 9971 }, { 2865, 9972 }, { 2878, 9970 }, { 2886, 9963 }, { 2891, 9953 },
            { 2888, 9941 }, { 2882, 9932 }, { 2876, 9923 }, { 2871, 9913 }, { 2858, 9911 },
        }, 50)
        climb("takeLadder4Up", "ladder_from_cellar", 2857, 9917, 2858, 9917, 2858, 3517)
        climb("takeLadder5Down", "ladder_outside_to_underground", 2859, 3519, 2858, 3519, 2858, 9919)
        hops("killIceQueen.approach", { { 2866, 9931 }, { 2865, 9942, 2 } }, 40)

        fight_begin()
        local icequeen_rounds = 0
        local icequeen_dead = false
        local icequeen_trail = ""
        while not icequeen_dead and icequeen_rounds < 40 do
            icequeen_rounds = icequeen_rounds + 1
            vitals()
            local qa_r = t.player.attack("ice_queen", 2, 20)
            local await_result = t.npc.await_dead("ice_queen", 15)
            if icequeen_rounds <= 6 then
                icequeen_trail = icequeen_trail .. string.format(" #%d attack=%s dead=%s", icequeen_rounds, tostring(qa_r), tostring(await_result))
            end
            if await_result == "ok" then
                icequeen_dead = true
            end
            vitals()
            local alive_result = t.player.alive()
            if alive_result ~= "ok" then
                break -- the driver's own terminal player.died row ends the run right after this
            end
        end
        t.check("killIceQueen.await_dead", icequeen_dead,
            "hunted " .. tostring(icequeen_rounds) .. " round(s):" .. icequeen_trail .. " -- t.npc.await_dead(ice_queen, 15) per round -> "
                .. tostring(icequeen_dead and "ok" or "not dead within the round budget"))
        margin_row("killIceQueen.margin", "Ice Queen (level 111)")
        t.expect("player.aliveAfterIceQueen", t.player.alive())

        -- ice_gloves is a real ground drop (ai_queue3,ice_queen -- obj_add
        -- at npc_coord, ice_queen.rs2:16), not a chat grant.
        local gloves_visible_result = t.await({
            level = function()
                return t.world.obj_near("ice_gloves", 10) == "ok"
            end,
            note = "waiting for the Ice Queen's dropped ice gloves to reach the client's entity pool",
        }, 30)
        t.step("iceGloves.visible", gloves_visible_result == "ok" and "PASS" or "FAIL",
            "t.world.obj_near(ice_gloves, 10) polled up to 30 ticks -> " .. tostring(gloves_visible_result))

        local gloves_before_result, gloves_before = t.inv.count("ice_gloves")
        local gloves_click_result, gloves_click_detail = t.player.click_obj("ice_gloves")
        if gloves_click_result ~= "ok" then
            t.ticks(3)
            gloves_click_result, gloves_click_detail = t.player.click_obj("ice_gloves")
        end
        t.inv.await("ice_gloves", 1, 10)
        local gloves_after_result, gloves_after = t.inv.count("ice_gloves")
        local gloves_pass = gloves_click_result == "ok" and gloves_after_result == "ok"
            and gloves_after > (gloves_before_result == "ok" and gloves_before or 0)
        t.step("pickUpIceGloves", gloves_pass and "PASS" or "FAIL",
            string.format("click_obj ice_gloves -> %s (%s), count %s -> %s",
                tostring(gloves_click_result), tostring(gloves_click_detail), tostring(gloves_before), tostring(gloves_after)))
        t.exec("equipIceGloves", t.player.equip, "ice_gloves")

        -- Out of the lair: Falador Teleport (the walk back is five ladders),
        -- then overland to Gerrant in Port Sarim. Every armed fight (Grip,
        -- the Ice Queen, the Jailer) comes before Entrana, so the gear is
        -- banked for good before the monk's boat (see goToEntrana below).
        teleport("leaveIceQueenLair.faladorTeleport", "falador_teleport",
            { { "waterrune", 1 }, { "airrune", 3 }, { "lawrune", 1 } }, 2965, 3378, "Falador square, tele_coord 0_46_52_21_50")

        -- ---------------------------------------------------------------
        -- Gerrant: Blamish snail slime for the lava-proof rod.
        -- gerrant.rs2:3-45 straven-style p_choice menu, option 3, real
        -- click. *.spawn tile areas/world/configs/m47_50.spawn:10. His
        -- shop's door is placed OPEN by the map (poordooropen 3014,3220;
        -- the wall line is the north edge of z 3219).
        -- ---------------------------------------------------------------
        t.exec("goto-gerrant", t.player.goto_tile, 3018, 3218, 0)
        open_doorway("talkToGerrant.shopIn", "poordooropen", 3014, 3220, 3014, 3219, 3013, 3223,
            function(tt) return tt.level == 0 and tt.z >= 3220 end, "inside Gerrant's shop, z >= 3220")
        t.exec("talkToGerrant", t.player.talk_to, "gerrant")
        t.exec("talkToGerrant-dialog", t.chat.play, {
            "npc:Welcome! You can buy fishing equipment",
            "choose:I want to find out how to catch a lava eel.",
            "player:I want to find out how to catch a lava eel.",
            "npc:Lava eels eh?",
            "npc:You know... thinking about it",
            "mesbox:Gerrant searches around a bit.",
            "npc:Aha! Here it is!",
        })
        local slime_result, slime_count = t.inv.count("blamish_snail_slime")
        t.check("gerrant.slimeGranted", slime_result == "ok" and slime_count >= 1,
            "inv.count(blamish_snail_slime) -> " .. tostring(slime_result) .. " " .. tostring(slime_count))
        open_doorway("talkToGerrant.shopOut", "poordooropen", 3014, 3220, 3013, 3222, 3016, 3218,
            function(tt) return tt.level == 0 and tt.z <= 3219 end, "back in the street, z <= 3219")

        -- ---------------------------------------------------------------
        -- Mix Blamish oil (brew.dbrow's herblore_blamish_oil row, level 25,
        -- [opheldu,blamish_snail_slime] -> ~attempt_brew_potion), then oil
        -- the fishing rod (oily_fishing_rod.rs2:10-21).
        -- ---------------------------------------------------------------
        local vial0_r, vial0 = t.inv.count("harralandervial")
        t.exec("mixBlamishOil", t.player.use_item_on_item, "blamish_snail_slime", "harralandervial")
        local oil_result, oil_count = t.inv.count("blamish_oil")
        local slime1_r, slime1 = t.inv.count("blamish_snail_slime")
        local vial1_r, vial1 = t.inv.count("harralandervial")
        t.check("mixBlamishOil.oilMade", oil_result == "ok" and oil_count >= 1 and slime1_r == "ok" and slime1 == 0
                and vial0_r == "ok" and vial1_r == "ok" and vial0 - vial1 == 1,
            "blamish_oil " .. tostring(oil_count) .. " (want >= 1), blamish_snail_slime left " .. tostring(slime1)
                .. " (want 0), harralandervial " .. tostring(vial0) .. " -> " .. tostring(vial1) .. " (want -1)")

        t.exec("oilTheRod", t.player.use_item_on_item, "blamish_oil", "fishing_rod")
        local oilyrod_result, oilyrod_count = t.inv.count("oily_fishing_rod")
        local oil1_r, oil1 = t.inv.count("blamish_oil")
        local rod1_r, rod1 = t.inv.count("fishing_rod")
        t.check("oilTheRod.rodMade", oilyrod_result == "ok" and oilyrod_count >= 1 and oil1_r == "ok" and oil1 == 0
                and rod1_r == "ok" and rod1 == 0,
            "oily_fishing_rod " .. tostring(oilyrod_count) .. " (want >= 1), blamish_oil left " .. tostring(oil1)
                .. " (want 0), fishing_rod left " .. tostring(rod1) .. " (want 0)")

        -- ---------------------------------------------------------------
        -- Taverley Dungeon: the deep section (where the lava-fishing spot
        -- sits) is behind deepdungeondoor, which needs dusty_key
        -- (jail_doors.rs2:17-24 -- HeroesQuest.java's own
        -- killJailerForKey/getDustyFromAdventurer/enterDeeperTaverley
        -- steps). dusty_key comes only from freeing Velrak
        -- (velrak_the_explorer.rs2), who sits behind dungeonjail, which
        -- needs jail_key (jail_doors.rs2:8-14) from killing the Jailer
        -- (drop_tables/jailer.rs2:5-13, a guaranteed jail_key drop) in the
        -- Black Knights' base. Everything is walked from the ladder; the
        -- hop lists are reach.py paths with every door shut between the
        -- named crossings (gaps-world.md "Taverley Dungeon deep area", and
        -- bearyoursoul.lua's proved hops to the metal gate).
        -- ---------------------------------------------------------------
        taverley_gate("enterTaverleyDungeon", "south_in")
        t.exec("goto-enterTaverleyDungeon", t.player.goto_tile, 2884, 3398, 0)
        climb("enterTaverleyDungeon", "ladder_outside_to_underground", 2884, 3397, 2884, 3398, 2884, 9798)
        hops("killJailerForKey.toCauldronDoor", { { 2884, 9818 }, { 2888, 9831 } }, 40)
        -- cauldrondoor (prison_doors.rs2 taverley_dungeon_open_doors): from
        -- outside, the first two presses animate the two suits of armour,
        -- the third is the walk-through (into x >= 2889).
        local cauldron_br, cauldron_bt = t.world.tile()
        local cauldron_trail, cauldron_through = "", false
        for press = 1, 5 do
            local pr, pd = t.player.click_loc("cauldrondoor", 1, { at = { 2889, 9831 } })
            t.ticks(3)
            local cr, ct = t.world.tile()
            cauldron_trail = cauldron_trail .. string.format(" #%d %s -> %s", press, tostring(pr), tile_text(cr, ct))
            vitals()
            if cr == "ok" and ct.x >= 2889 then
                cauldron_through = true
                break
            end
        end
        t.check("killJailerForKey.cauldronDoor", cauldron_br == "ok" and cauldron_bt.x <= 2888 and cauldron_through,
            "from " .. tile_text(cauldron_br, cauldron_bt) .. " cauldrondoor presses:" .. cauldron_trail
                .. " (want through to x >= 2889: two suits of armour, then the walk-through)")
        pass_door("killJailerForKey.metalGate", "metalgateclosedl", "metalgateopenl", 2898, 9831, 2897, 9831, 2899, 9831,
            function(tt) return tt.level == 0 and tt.x >= 2898 end, "east of the metal gate, x >= 2898")
        hops("killJailerForKey.toBlackKnights", {
            { 2919, 9833 }, { 2937, 9829 }, { 2941, 9811 }, { 2948, 9796 }, { 2951, 9777 },
            { 2935, 9777 }, { 2932, 9758 }, { 2921, 9747 }, { 2907, 9739 }, { 2907, 9717 },
        }, 50)
        -- The Black Knights' base: castledoubledoorl/r 2908/2907,9698, a
        -- south-wall pair (the base is z <= 9697).
        pass_door("killJailerForKey.baseDoorIn", "castledoubledoorl", "opencastledoubledoorl", 2908, 9698, 2908, 9699, 2908, 9696,
            function(tt) return tt.level == 0 and tt.z <= 9697 end, "inside the Black Knights' base, z <= 9697")
        hop("killJailerForKey.toJailer", 2925, 9693, 40, 2)

        fight_begin()
        local jailer_attack_result, jailer_attack_detail = t.player.attack("jailer", 2, 20)
        local jailer_dead_result, jailer_dead_detail = t.npc.await_dead("jailer", 60)
        t.check("killJailer", jailer_dead_result == "ok",
            "attack(jailer) -> " .. tostring(jailer_attack_result) .. " " .. tostring(jailer_attack_detail)
                .. "; npc.await_dead(jailer, 60) -> " .. tostring(jailer_dead_result) .. " " .. tostring(jailer_dead_detail))
        margin_row("killJailer.margin", "the Jailer (level 47)")

        -- jail_key is a real ground drop (ai_queue3,jailer -- obj_add at
        -- npc_coord), not a chat grant.
        local jailkey_visible_result = t.await({
            level = function()
                return t.world.obj_near("jail_key", 10) == "ok"
            end,
            note = "waiting for the Jailer's dropped jail_key to reach the client's entity pool",
        }, 30)
        t.step("jailKey.visible", jailkey_visible_result == "ok" and "PASS" or "FAIL",
            "t.world.obj_near(jail_key, 10) polled up to 30 ticks -> " .. tostring(jailkey_visible_result))

        local jailkey_before_result, jailkey_before = t.inv.count("jail_key")
        local jailkey_click_result, jailkey_click_detail = t.player.click_obj("jail_key")
        if jailkey_click_result ~= "ok" then
            t.ticks(3)
            jailkey_click_result, jailkey_click_detail = t.player.click_obj("jail_key")
        end
        t.inv.await("jail_key", 1, 10)
        local jailkey_after_result, jailkey_after = t.inv.count("jail_key")
        local jailkey_pass = jailkey_click_result == "ok" and jailkey_after_result == "ok"
            and jailkey_after > (jailkey_before_result == "ok" and jailkey_before or 0)
        t.step("pickUpJailKey", jailkey_pass and "PASS" or "FAIL",
            string.format("click_obj jail_key -> %s (%s), count %s -> %s",
                tostring(jailkey_click_result), tostring(jailkey_click_detail), tostring(jailkey_before), tostring(jailkey_after)))

        -- Velrak's cell (x 2928-2934 z 9683-9689): dungeonjail has two
        -- copies, 2931,9690 (this cell, a south-wall leaf) and 2931,9694;
        -- from 2931,9691 the nearest copy is the right one, and the row
        -- says which copy it resolved before the key is used on it.
        hop("getDustyFromAdventurer.atCellDoor", 2931, 9691, 20, 0)
        local jaildoor_near_result, jaildoor_row = t.world.loc_near("dungeonjail", 2)
        t.check("getDustyFromAdventurer.cellDoorCopy", jaildoor_near_result == "ok" and jaildoor_row.tile_x == 2931
                and jaildoor_row.tile_z == 9690 and jaildoor_row.level == 0,
            "t.world.loc_near(dungeonjail, 2) -> " .. tostring(jaildoor_near_result) .. " "
                .. (jaildoor_near_result == "ok" and (jaildoor_row.tile_x .. "," .. jaildoor_row.tile_z .. "," .. jaildoor_row.level) or "")
                .. " (want Velrak's cell door 2931,9690,0)")
        -- trap 298: settle the backpack tab before arming, or the arm can
        -- silently fail and the press degrades to a bare "Walk here".
        t.check("jaildoor.tabInventory", t.ui.tab("inventory") == "ok", "t.ui.tab(inventory)")
        t.ticks(2)
        local cell_br, cell_bt = t.world.tile()
        local unlock_r, unlock_d = t.player.use_on("jail_key", jaildoor_row)
        await_tile(function(tt) return tt.z <= 9689 end, 8, "unlockJailDoor")
        local cell_ar, cell_at = t.world.tile()
        local unlock_msg_r = t.msg.expect("You unlock the door.")
        t.check("unlockJailDoor", cell_br == "ok" and cell_bt.z >= 9690 and cell_ar == "ok" and cell_at.z <= 9689
                and cell_at.x >= 2928 and cell_at.x <= 2934 and unlock_msg_r == "ok",
            "use_on(jail_key, dungeonjail 2931,9690) -> " .. tostring(unlock_r) .. " " .. tostring(unlock_d) .. "; tile "
                .. tile_text(cell_br, cell_bt) .. " -> " .. tile_text(cell_ar, cell_at) .. "; msg 'You unlock the door.' -> "
                .. tostring(unlock_msg_r) .. " (want into the cell, z <= 9689)")

        t.exec("talkToVelrak", t.player.talk_to, "velrak_the_explorer")
        t.exec("talkToVelrak-dialog", t.chat.play, {
            "npc:Thank you for rescuing me!",
            "choose:So... do you know anywhere good to explore?",
            "player:So... do you know anywhere good to explore?",
            "npc:Well, this dungeon was quite good to explore",
            "npc:It's rather tough for me to get that far",
            "choose:Yes please!",
            "player:Yes please!",
            "mesbox:Velrak reaches somewhere mysterious",
        })
        local dustykey_result, dustykey_detail = t.inv.await("dusty_key", 1, 10)
        t.check("velrak.dustyKeyGranted", dustykey_result == "ok",
            "inv.await(dusty_key, 1, 10) -> " .. tostring(dustykey_result) .. " " .. tostring(dustykey_detail))

        -- Out of the cell: dungeonjail op1 from inside is the walk-through
        -- ("The door locks shut behind you.", jail_doors.rs2).
        -- From inside, the default camera (south of the player, looking
        -- north) sees no pickable face of the door: the press answers
        -- `covered ... menu has no row` from 2931,9689 and from 2932,9687
        -- (runs 1-2). Turn the camera to look at it from the corridor side
        -- (yaw 768 answered in run 3, 1024 was still covered), stepping the yaw if a press
        -- is still covered, then put the camera back. The row grades the
        -- tile before the press (in the cell, z <= 9689) and after.
        local lv_br, lv_bt = t.world.tile()
        local lv_trail, lv_out = "", false
        for _, yaw in ipairs({ 768, 1024, 1280, 512 }) do
            t.drive.camera(yaw, 383, 600)
            local pr, pd = t.player.click_loc("dungeonjail", 1, { at = { 2931, 9690 } })
            await_tile(function(tt) return tt.z >= 9690 end, 6, "leaveVelrakCell")
            local lr, lt = t.world.tile()
            lv_trail = lv_trail .. string.format(" yaw %d: %s %s -> %s;", yaw, tostring(pr), string.sub(tostring(pd), 1, 60), tile_text(lr, lt))
            if lr == "ok" and lt.z >= 9690 then
                lv_out = true
                break
            end
        end
        t.drive.camera(0, 383, 600)
        local lv_msg_r = t.msg.expect("The door locks shut behind you.")
        t.check("leaveVelrakCell", lv_br == "ok" and lv_bt.z <= 9689 and lv_out and lv_msg_r == "ok",
            "from " .. tile_text(lv_br, lv_bt) .. " click_loc(dungeonjail at 2931,9690):" .. lv_trail
                .. " msg 'The door locks shut behind you.' -> " .. tostring(lv_msg_r) .. " (want out of the cell, z >= 9690)")
        vitals()
        -- Out of the Black Knights' base the way in.
        hop("enterDeeperTaverley.toBaseDoor", 2910, 9691, 40)
        pass_door("enterDeeperTaverley.baseDoorOut", "castledoubledoorl", "opencastledoubledoorl", 2908, 9698, 2908, 9697, 2908, 9699,
            function(tt) return tt.level == 0 and tt.z >= 9698 end, "out of the base, z >= 9698")
        hops("enterDeeperTaverley.toGate", {
            { 2907, 9720 }, { 2908, 9741 }, { 2923, 9748 }, { 2924, 9755 }, { 2927, 9770 },
            { 2934, 9785 }, { 2930, 9801 }, { 2925, 9803, 0 },
        }, 50)

        -- The dusty key on deepdungeondoor from its EAST side (check_axis
        -- loc_west: x >= 2924 is outside): "You unlock the gate." and the
        -- walk-through lands at 2923,9803.
        local deepdoor_near_result, deepdoor_row = t.world.loc_near("deepdungeondoor", 3)
        t.check("enterDeeperTaverley.gateCopy", deepdoor_near_result == "ok" and deepdoor_row.tile_x == 2924
                and deepdoor_row.tile_z == 9803 and deepdoor_row.level == 0,
            "t.world.loc_near(deepdungeondoor, 3) -> " .. tostring(deepdoor_near_result) .. " "
                .. (deepdoor_near_result == "ok" and (deepdoor_row.tile_x .. "," .. deepdoor_row.tile_z .. "," .. deepdoor_row.level) or "")
                .. " (want the gate at 2924,9803,0)")
        t.check("deepdoor.tabInventory", t.ui.tab("inventory") == "ok", "t.ui.tab(inventory)")
        t.ticks(2)
        local gate_br, gate_bt = t.world.tile()
        local deep_r, deep_d = t.player.use_on("dusty_key", deepdoor_row)
        await_tile(function(tt) return tt.x <= 2923 end, 8, "unlockDeepDungeonDoor")
        local gate_ar, gate_at = t.world.tile()
        local gate_msg_r = t.msg.expect("You unlock the gate.")
        t.check("unlockDeepDungeonDoor", gate_br == "ok" and gate_bt.x >= 2924 and gate_ar == "ok" and gate_at.x <= 2923
                and gate_at.level == 0 and gate_msg_r == "ok",
            "use_on(dusty_key, deepdungeondoor) -> " .. tostring(deep_r) .. " " .. tostring(deep_d) .. "; tile "
                .. tile_text(gate_br, gate_bt) .. " -> " .. tile_text(gate_ar, gate_at) .. "; msg 'You unlock the gate.' -> "
                .. tostring(gate_msg_r) .. " (want through to x <= 2923)")

        -- ---------------------------------------------------------------
        -- Fish a lava eel at Taverley Dungeon's own lava-fishing spot
        -- (skill_fishing/scripts/fishing_spots/lavafish.rs2, category 1313,
        -- spots 2889-2891,9766 on the lava, m45_152.spawn:8-10), from the
        -- walkable bank 2890,9767. op1 resolves to `@attempt_fish_lava_eel`.
        -- ---------------------------------------------------------------
        hops("fishLavaEel.walk", { { 2901, 9803 }, { 2892, 9790 }, { 2892, 9772 }, { 2890, 9767, 0 } }, 40)
        t.exec("fish.lavaeel", t.player.talk_to, "0_45_152_lavafish")
        local raweel_result, raweel_detail = t.inv.await("raw_lava_eel", 1, 30)
        t.step("fish.lavaeel_caught", raweel_result == "ok" and "PASS" or "FAIL",
            "inv.await(raw_lava_eel, 1, 30) -> " .. tostring(raweel_result) .. " " .. tostring(raweel_detail))

        -- Out of the dungeon: Lumbridge Teleport.
        teleport("cookLavaEel.lumbridgeTeleport", "lumbridge_teleport",
            { { "earthrune", 1 }, { "airrune", 3 }, { "lawrune", 1 } }, 3221, 3218, "Lumbridge, tele_coord 0_50_50_21_18")

        -- ---------------------------------------------------------------
        -- Cook it: light a fire on open ground (the fixture's own start
        -- tile, the same real click sequence seaslug.lua uses), then
        -- use_on the raw eel -- cooking_generic_lava_eel always succeeds
        -- (cooking_generic.dbrow's own successchance,1,1).
        -- ---------------------------------------------------------------
        t.exec("fire.goto", t.player.goto_tile, 3206, 3233, 0)
        t.exec("lightFire", t.player.use_item_on_item, "tinderbox", "logs")
        -- The verb itself can settle on "The fire catches..." (run heroalt2:
        -- a msg.await after it then waits for a SECOND line and times out),
        -- so the line is read from the recent chat first, then awaited.
        local fire_msg_result, fire_msg_detail = t.msg.expect("The fire catches")
        if fire_msg_result ~= "ok" then
            fire_msg_result, fire_msg_detail = t.msg.await("The fire catches", 15)
        end
        local logs_r, logs_left = t.inv.count("logs")
        t.step("fire.lit", fire_msg_result == "ok" and logs_r == "ok" and logs_left == 0 and "PASS" or "FAIL",
            "'The fire catches' -> " .. tostring(fire_msg_result) .. " " .. tostring(fire_msg_detail)
                .. "; logs left " .. tostring(logs_left) .. " (want 0: the one staged log burned)")

        -- by_symbol's own search can resolve some OTHER "fire" already in
        -- the pool, not the one just lit -- loc_near pins the specific fire
        -- we made, but the zone packet carrying the loc_add lands a couple
        -- of ticks behind the chat line msg.await already settled on.
        t.ticks(2)
        local fire_lookup_result, fire_row = t.world.loc_near("fire", 3)
        t.check("lookup.fire", fire_lookup_result == "ok" and fire_row ~= nil,
            "world.loc_near(fire, 3) -> " .. tostring(fire_lookup_result)
                .. (fire_lookup_result == "ok" and (" at " .. fire_row.tile_x .. "," .. fire_row.tile_z) or "") .. " (want the fire just lit beside the player)")
        local raweel0_r, raweel0 = t.inv.count("raw_lava_eel")
        t.exec("cookLavaEel", t.player.use_on, "raw_lava_eel", fire_row)
        local eel_result, eel_detail = t.inv.await("lava_eel", 1, 10)
        local raweel1_r, raweel1 = t.inv.count("raw_lava_eel")
        t.step("cookLavaEel.cooked", eel_result == "ok" and raweel0_r == "ok" and raweel1_r == "ok" and raweel0 - raweel1 == 1 and "PASS" or "FAIL",
            "inv.await(lava_eel, 1, 10) -> " .. tostring(eel_result) .. " " .. tostring(eel_detail)
                .. "; raw_lava_eel " .. tostring(raweel0) .. " -> " .. tostring(raweel1) .. " (want -1)")

        -- ---------------------------------------------------------------
        -- goToEntrana, first: leave the weaponry and armour behind. The
        -- monk's own line is "you must leave your weaponry and armour
        -- behind"; his search (LostCity area_port_sarim/scripts/
        -- monk_of_entrana.rs2:26-50, ~has_entrana_restricted_items: every
        -- armour_* and weapon_* category, worn OR carried -- maces, bows,
        -- pickaxes included) is only DEFERRED in this pack
        -- (port_sarim/scripts/monk_of_entrana.rs2:11), so the test carries
        -- none of it, as a player must, and holds once the search exists.
        -- Every armed fight (Grip, the Ice Queen, the Jailer) is done, so the
        -- gear is banked for good at Draynor Village, on the way from
        -- Lumbridge to Port Sarim. The bank's doorway (3092-3093,3246) holds
        -- only the map's inactive open leaves (bankdoor_l/r_inactive, no op;
        -- reach.py walks in with every door shut), so in and out are walks
        -- from an outdoor tile. The ice gloves stay worn: the feather needs
        -- them (fire_feather.rs2:20) and OSRS lets them onto Entrana.
        -- ---------------------------------------------------------------
        -- The backpack has about 7 free slots here (run r2.1: 1 left after
        -- six unequips), so the two CARRIED pieces go in first, then the
        -- six worn ones are taken off and banked.
        local ENTRANA_CARRIED = { "magic_shortbow", "rune_pickaxe" }
        local ENTRANA_WORN = { "rune_mace", "rune_kiteshield", "rune_full_helm", "rune_platebody", "rune_platelegs", "rune_arrow" }
        local ENTRANA_FORBIDDEN = { "rune_mace", "rune_kiteshield", "rune_full_helm", "rune_platebody", "rune_platelegs",
            "rune_arrow", "magic_shortbow", "rune_pickaxe" }
        t.exec("goto-goToEntrana.draynorBank", t.player.goto_tile, 3092, 3250, 0)
        hop("goToEntrana.bankIn", 3092, 3243, 20, 0)
        t.exec("goToEntrana.bankOpen.carried", t.bank.open, "bankbooth", 2, { at = { 3091, 3243 } })
        for _, item in ipairs(ENTRANA_CARRIED) do
            t.exec("goToEntrana.deposit." .. item, t.bank.deposit, item, "all")
        end
        t.check("goToEntrana.bankClose.carried", t.bank.close())
        for _, item in ipairs(ENTRANA_WORN) do
            t.exec("goToEntrana.unequip." .. item, t.player.unequip, item)
        end
        t.exec("goToEntrana.bankOpen.worn", t.bank.open, "bankbooth", 2, { at = { 3091, 3243 } })
        for _, item in ipairs(ENTRANA_WORN) do
            t.exec("goToEntrana.deposit." .. item, t.bank.deposit, item, "all")
        end
        t.check("goToEntrana.bankClose.worn", t.bank.close())
        -- The monk's search, read off the player: none of it carried, none
        -- worn (unequip answers not_found, with no press, when nothing of
        -- the item is worn). The ice gloves are not in the pack: still worn
        -- since equipIceGloves (the feather pickup below needs them worn).
        local carried, carry_text = false, ""
        for _, item in ipairs(ENTRANA_FORBIDDEN) do
            local cr, c = t.inv.count(item)
            local ur = t.player.unequip(item)
            if not (cr == "ok" and c == 0 and ur == "not_found") then
                carried = true
            end
            carry_text = carry_text .. string.format("%s pack %s(%s) worn %s; ", item, tostring(c), tostring(cr), tostring(ur))
        end
        local gl_r, gl = t.inv.count("ice_gloves")
        t.check("goToEntrana.noWeaponOrArmour", not carried and gl_r == "ok" and gl == 0,
            carry_text .. "ice_gloves pack " .. tostring(gl) .. " (" .. tostring(gl_r)
                .. ") (want every weapon/armour 0 in the pack and not_found worn; the gloves not in the pack, worn)")
        hop("goToEntrana.bankOut", 3092, 3250, 20, 0)

        -- ---------------------------------------------------------------
        -- Entrana: the Quest Helper guide's own goToEntrana step is the
        -- Port Sarim monk's boat (monk_of_entrana.rs2's shipmonk_talk/
        -- shipmonk_ready labels, *.spawn tile
        -- areas/world/configs/m47_50.spawn:32), boarded with no weapon or
        -- armour (above).
        -- The crossing ends on the ship's deck (p_telejump(1_44_52_18_3) =
        -- 2834,3331,1); ship_from_entrana_off (2834,3333) is the way ashore.
        -- ---------------------------------------------------------------
        t.exec("goto-shipmonk", t.player.goto_tile, 3045, 3236, 0)
        t.exec("talkToShipmonk", t.player.talk_to, "shipmonk")
        t.exec("talkToShipmonk-dialog", t.chat.play, {
            "npc:Do you seek passage to holy Entrana?",
            "choose:Yes, okay, I'm ready to go.",
            "player:Yes, okay, I'm ready to go.",
            "npc:Very well. One moment please.",
            "mesbox:The monk quickly searches you.",
        })
        await_tile(function(tt) return tt.level == 1 and tt.x < 2900 end, 10, "shipmonk")
        local entrana_deck_r, entrana_deck = t.world.tile()
        t.check("shipmonk.onDeckAtEntrana", entrana_deck_r == "ok" and entrana_deck.level == 1
                and math.abs(entrana_deck.x - 2834) <= 2 and math.abs(entrana_deck.z - 3331) <= 2,
            "t.world.tile() -> " .. tile_text(entrana_deck_r, entrana_deck) .. " (want the deck, p_telejump(1_44_52_18_3) = 2834,3331,1)")
        cross("entrana.disembark", "ship_from_entrana_off", 2834, 3333,
            function(tt) return tt.level == 0 and tt.x < 2900 and tt.z > 3300 end, "ashore on Entrana, level 0")

        -- Entrana firebird: real fight (hp5/atk1, trivial) for hot_feather,
        -- UNARMED (the weapons are in the Draynor bank), with a margin row.
        -- fire_bird *.spawn tile areas/world/configs/m44_52.spawn:20, an
        -- overland hop from the jetty (reach.py 88 tiles, every door shut).
        -- No quest-state check on the fight itself (entrana_firebird.rs2's
        -- ai_queue3 only gates the FEATHER drop on %heroquest < hero_complete,
        -- true here).
        t.exec("goto-firebird", t.player.goto_tile, 2847, 3386, 0)
        fight_begin()
        local firebird_attack_result, firebird_attack_detail = t.player.attack("fire_bird", 2, 20)
        local firebird_dead_result, firebird_dead_detail = t.npc.await_dead("fire_bird", 30)
        t.check("killFirebird", firebird_dead_result == "ok",
            "attack(fire_bird) -> " .. tostring(firebird_attack_result) .. " " .. tostring(firebird_attack_detail)
                .. "; npc.await_dead(fire_bird, 30) -> " .. tostring(firebird_dead_result) .. " " .. tostring(firebird_dead_detail))
        margin_row("killFirebird.margin", "Entrana firebird, unarmed")

        -- hot_feather is a real ground drop (entrana_firebird.rs2:13) --
        -- pickup needs ice_gloves WORN (fire_feather.rs2:20's op3 gate),
        -- already equipped above.
        local feather_visible_result = t.await({
            level = function()
                return t.world.obj_near("hot_feather", 10) == "ok"
            end,
            note = "waiting for the firebird's dropped feather to reach the client's entity pool",
        }, 10)
        t.step("hotFeather.visible", feather_visible_result == "ok" and "PASS" or "FAIL",
            "t.world.obj_near(hot_feather, 10) polled up to 10 ticks -> " .. tostring(feather_visible_result))

        local feather_before_result, feather_before = t.inv.count("hot_feather")
        local feather_click_result, feather_click_detail = t.player.click_obj("hot_feather")
        if feather_click_result ~= "ok" then
            t.ticks(3)
            feather_click_result, feather_click_detail = t.player.click_obj("hot_feather")
        end
        t.inv.await("hot_feather", 1, 10)
        local feather_after_result, feather_after = t.inv.count("hot_feather")
        local feather_pass = feather_click_result == "ok" and feather_after_result == "ok"
            and feather_after > (feather_before_result == "ok" and feather_before or 0)
        t.step("pickUpHotFeather", feather_pass and "PASS" or "FAIL",
            string.format("click_obj hot_feather (worn ice_gloves) -> %s (%s), count %s -> %s",
                tostring(feather_click_result), tostring(feather_click_detail), tostring(feather_before), tostring(feather_after)))

        -- Off Entrana the way it was reached: shipmonk2 (spawn 2830,3335,
        -- areas/entrana/scripts/monk_of_entrana.rs2 shipmonk2_ready ->
        -- p_telejump(1_47_50_40_31) = the deck at Port Sarim, 3048,3231,1),
        -- then ship_to_entrana_off (3048,3232) ashore.
        t.exec("goto-leaveEntrana", t.player.goto_tile, 2832, 3336, 0)
        t.exec("leaveEntrana", t.player.talk_to, "shipmonk2", 1)
        t.exec("leaveEntrana-dialog", t.chat.play, {
            "npc:Do you wish to leave holy Entrana?",
            "choose:Yes, I'm ready to go.",
            "player:Yes, I'm ready to go.",
            "npc:Okay, let's board",
        })
        await_tile(function(tt) return tt.level == 1 and tt.x > 3000 end, 12, "leaveEntrana")
        local sarim_deck_r, sarim_deck = t.world.tile()
        t.check("leaveEntrana.onDeckAtPortSarim", sarim_deck_r == "ok" and sarim_deck.level == 1
                and math.abs(sarim_deck.x - 3048) <= 2 and math.abs(sarim_deck.z - 3231) <= 2,
            "t.world.tile() -> " .. tile_text(sarim_deck_r, sarim_deck) .. " (want the deck, p_telejump(1_47_50_40_31) = 3048,3231,1)")
        cross("portSarim.disembark", "ship_to_entrana_off", 3048, 3232,
            function(tt) return tt.level == 0 and tt.x > 3000 and tt.z > 3232 end, "ashore on the Port Sarim jetty, level 0")

        -- ---------------------------------------------------------------
        -- All three deliverables in hand: snapshot every skill now, right
        -- before the hand-in, so the reward rows below measure ONLY the
        -- completion's own stat_advance calls -- not the XP the fights or
        -- the eel arm already granted as ordinary side effects.
        -- ---------------------------------------------------------------
        local feather_have_result, feather_have = t.inv.count("hot_feather")
        local armband_have_result, armband_have = t.inv.count("master_thief_armband")
        local eel_have_result, eel_have = t.inv.count("lava_eel")
        t.check("finalItems.allThreeHeld",
            feather_have_result == "ok" and feather_have >= 1
                and armband_have_result == "ok" and armband_have >= 1
                and eel_have_result == "ok" and eel_have >= 1,
            string.format("hot_feather=%s(%s) master_thief_armband=%s(%s) lava_eel=%s(%s)",
                tostring(feather_have_result), tostring(feather_have),
                tostring(armband_have_result), tostring(armband_have),
                tostring(eel_have_result), tostring(eel_have)))

        local _, reward_snapshot = t.skill.snapshot()

        -- ---------------------------------------------------------------
        -- Achietties: the real hand-in (achietties.rs2:20-32). The
        -- item-complete branch fires because all three are held; it queues
        -- hero_quest_complete and deletes the three items in the caller.
        -- From the Port Sarim jetty through the Taverley members' south
        -- gate, then overland to the guild's open arch.
        -- ---------------------------------------------------------------
        taverley_gate("achietties-handin", "south_in")
        t.exec("goto-achietties-handin", t.player.goto_tile, 2903, 3510, 0)
        t.exec("achietties.handIn", t.player.talk_to, "achietties")
        t.exec("achietties.handIn-dialog", t.chat.play, {
            -- achietties.rs2:18 opens EVERY talk_to with this greeting,
            -- unconditionally, before branching on hero_in_progress.
            "npc:Greetings. Welcome to the Heroes",
            "npc:How goes thy quest",
            "player:I have all the required items.",
            "npc:I see that you have. Well done",
            "player:W-what? What do you mean?",
            "npc:I'm sorry, I was just having a little fun",
            "npc:Congratulations! You have completed",
        })
        t.ticks(3) -- queue(hero_quest_complete, 0, 0) is not client-side yet

        t.quest.expect_complete() -- writes quest.varp_complete/quest.scroll_title/quest.points/quest.journal

        -- ---------------------------------------------------------------
        -- Every reward Quest Helper/quest_hero.rs2:49-60's own
        -- stat_advance list grants (twelve skills, docs/quests/
        -- heroes_quest.md section 2's XP table).
        -- ---------------------------------------------------------------
        t.check("reward.attack", t.skill.expect_gain("attack", 3075, reward_snapshot) == "ok",
            "t.skill.expect_gain(attack, 3075)")
        t.check("reward.defence", t.skill.expect_gain("defence", 3075, reward_snapshot) == "ok",
            "t.skill.expect_gain(defence, 3075)")
        t.check("reward.strength", t.skill.expect_gain("strength", 3075, reward_snapshot) == "ok",
            "t.skill.expect_gain(strength, 3075)")
        t.check("reward.hitpoints", t.skill.expect_gain("hitpoints", 3075, reward_snapshot) == "ok",
            "t.skill.expect_gain(hitpoints, 3075)")
        t.check("reward.ranged", t.skill.expect_gain("ranged", 2075, reward_snapshot) == "ok",
            "t.skill.expect_gain(ranged, 2075)")
        t.check("reward.fishing", t.skill.expect_gain("fishing", 2725, reward_snapshot) == "ok",
            "t.skill.expect_gain(fishing, 2725)")
        t.check("reward.cooking", t.skill.expect_gain("cooking", 2825, reward_snapshot) == "ok",
            "t.skill.expect_gain(cooking, 2825)")
        t.check("reward.woodcutting", t.skill.expect_gain("woodcutting", 1575, reward_snapshot) == "ok",
            "t.skill.expect_gain(woodcutting, 1575)")
        t.check("reward.firemaking", t.skill.expect_gain("firemaking", 1575, reward_snapshot) == "ok",
            "t.skill.expect_gain(firemaking, 1575)")
        t.check("reward.smithing", t.skill.expect_gain("smithing", 2275, reward_snapshot) == "ok",
            "t.skill.expect_gain(smithing, 2275)")
        t.check("reward.mining", t.skill.expect_gain("mining", 2575, reward_snapshot) == "ok",
            "t.skill.expect_gain(mining, 2575)")
        t.check("reward.herblore", t.skill.expect_gain("herblore", 1325, reward_snapshot) == "ok",
            "t.skill.expect_gain(herblore, 1325)")

        -- achietties.rs2:26-29 deletes all three final items in the caller
        -- (the completion "consumes hot_feather+lava_eel+master_thief_armband
        -- exactly once" -- quest_hero.rs2's own ::herorun HERORUN treats a
        -- surviving final item as FAIL).
        t.check("reward.hotFeatherConsumed", t.inv.expect_absent("hot_feather") == "ok",
            "t.inv.expect_absent(hot_feather)")
        t.check("reward.lavaEelConsumed", t.inv.expect_absent("lava_eel") == "ok",
            "t.inv.expect_absent(lava_eel)")
        t.check("reward.armbandConsumed", t.inv.expect_absent("master_thief_armband") == "ok",
            "t.inv.expect_absent(master_thief_armband)")

        t.finish(0)
        return
    end,
}
