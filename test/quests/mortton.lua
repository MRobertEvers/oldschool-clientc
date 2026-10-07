-- Shades of Mort'ton (quest_mortton). Hand-authored from the scaffold; see
-- QUEUE.tsv's last_failure for the prior rejections this resumes from.
--
-- RETRY after 1858fe69a (queue.py show mortton): the shade hunt was losing
-- to four AGGRESSIVE Afflicted types (mort_afflicted_man/_man2/_woman/
-- _woman2) wandering Mort'ton's SINGLE-WAY street -- every Attack on a Loar
-- Shadow refused "I'm already under attack." because an Afflicted had
-- claimed the player. Fixed with four ::passive setup lines
-- (docs/QUEST_SERVER_CHEATS.md section F). The shop blocker from the prior
-- rejection is gone too: t.shop.* landed, so Razmire's builders' store
-- (razmirebuildingstore, op 4) is reachable for real and this file now
-- drives the whole rest of the quest instead of stopping at the temple
-- wall's "not enough resources" mesbox.
--
-- Coins are a bring-along prerequisite for the shop (Quest Helper's own
-- buyTimberLimeAndSwamp step) -- setup has none without an explicit
-- ::give. Razmire's and Ulsquire's cure from Serum 207 lasts only 200
-- ticks (npc_changetype(..., 200) in both razmire_keelgan.rs2 and
-- ulsquire_shauncy.rs2) and the shade hunt alone runs past that, so every
-- return visit re-checks whether the npc is still the afflicted type and,
-- if so, re-cures with the lightest Serum 207 vial already carried before
-- talking -- setup carries five ashes/tarrominvial pairs: one each for the
-- two first cures, and a fresh 3-dose mort_serum3 for the sacred flame
-- (Serum 208: one dose for Razmire, one for Ulsquire, and the last vial in
-- hand for Ulsquire's "already used" answer).
--
-- The temple wall (flamtaer_temple.rs2's [oploc3,_temple_wall]) and the
-- fire altar/funeral pyre (mortton_pyre.rs2's [oploc4,_pyre_remains_loaded]
-- try_light_altar/light_funeral_pyre) are both SELF-RE-ARMING interactions
-- -- every successful roll ends with its own p_oploc(3)/p_oploc(4), the
-- same idiom player.attack's auto-swing uses -- so one press each drives
-- repeated server-side rolls on its own; this file presses once and polls
-- quest.stage() rather than re-pressing per round.

return {
    id = "mortton",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so requirements fit
        "::give tarrominvial 5", -- Quest Helper prerequisite -- first cures x2, the flame's Serum 208 x1, spares
        "::give tinderbox 1", -- Quest Helper prerequisite
        "::give logs 1", -- Quest Helper prerequisite
        "::give ashes 5", -- Quest Helper prerequisite -- paired with tarrominvial above
        "::give hammer 1", -- Quest Helper prerequisite -- needed at the temple wall (trap 16: a tool, not the quest's own deliverable)
        "::give rune_scimitar 1", -- combat gear prerequisite for the five shades (trap 16), same idiom as hunt.lua
        "::give shark 5", -- food prerequisite (trap 26/section 8's player.attack note): "carry food and WEAR the weapon you were given"
        "::give coins 5000", -- bring-along prerequisite for Razmire's builders' store (t.shop.buy needs coins in setup)
        -- Crafting 20 is the quest's OWN requirement (flamtaer_temple.rs2's
        -- ::mortton_repairtemple guard: "if (stat(crafting) < 20)"), not 99:
        -- ~mortton_temple_build_step spends resource POOL per repair scaled
        -- by crafting level (measured ~9 pool/repair at 20, ~31 at 99), so a
        -- higher level than the quest asks for makes the wall repair burn
        -- through Razmire's 5-per-restock materials three times as fast for
        -- no benefit -- keep it at what the quest itself requires.
        "::setlevel crafting 20",
        "::setlevel herblore 15",
        "::setlevel firemaking 99", -- prerequisite gearing for the fire altar and funeral pyre stat_random rolls
        -- Loar shades (level 40, mortton_shades.npc: attack 45 / defence 26 /
        -- strength 30 / hitpoints 38) killed an earlier probe at fresh-
        -- character combat stats -- level up before ever entering the town,
        -- same as the rune scimitar and food above (prerequisite gear, not
        -- the quest's own work).
        "::setlevel hitpoints 99",
        "::setlevel defence 40",
        "::setlevel attack 40",
        "::setlevel strength 40",
        "::complete quest_priestinperil", -- Shades of Mort'ton's own prerequisite quest
        "::give dagger_wolfbane 1", -- Priest in Peril's own reward (::complete grants no items); Drezel's holy-barrier advice needs it held (mausoleum_drezel.rs2:28-33)
        -- Herblore itself is locked behind Druidic Ritual (brew_potion.rs2:
        -- 513's ~herblore_unlocked, quest_druid.rs2:33) regardless of level.
        "::complete quest_druidicritual",
        -- Mort'ton's shade street is SINGLE-WAY and four Afflicted types
        -- wander it aggressively (docs/QUEST_SERVER_CHEATS.md section F):
        -- one that aggresses claims the player for
        -- TORIRSSERVER_SINGLEWAY_COMBAT_TICKS past every swing, and every
        -- Attack on a Loar Shadow inside that window is refused "I'm
        -- already under attack." -- measured 20 rounds for 2 of 5 kills
        -- without this, 7 rounds for 5 of 5 with it.
        "::passive mort_afflicted_man",
        "::passive mort_afflicted_man2",
        "::passive mort_afflicted_woman",
        "::passive mort_afflicted_woman2",
        -- shadeshadow_level1 ITSELF is huntmode=aggressive, huntrange 3
        -- (mortton_shades.npc), 31 copies packed along this same street --
        -- runs 2-4 all measured EVERY round's own t.player.attack refused
        -- "I'm already under attack." from the very first press (no prior
        -- engagement to abandon), so something is claiming the player before
        -- our own Attack registers even with the four Afflicted held. Hold
        -- this type passive too: it is still attackable, still takes real
        -- damage from our own t.player.attack presses, still dies for real
        -- and still drops shade_bones1 (docs/QUEST_SERVER_CHEATS.md section
        -- F) -- only its own unprompted aggression/retaliation is removed,
        -- the same driver affordance as the four Afflicted lines above, not
        -- a shortcut around the kill itself.
        "::passive shadeshadow_level1",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp339_morttonquest",
            constants = {
                complete = 85,
                mortton_can_light_altar = 60,
                mortton_created_pyre_logs = 70,
                mortton_created_sacred_oil = 65,
                mortton_gave_dairy_to_apothecary = 30,
                mortton_kill_shades = 15,
                mortton_killed_1_shade = 20,
                mortton_killed_2_shades = 25,
                mortton_killed_3_shades = 30,
                mortton_killed_4_shades = 35,
                mortton_killed_5_shades = 40,
                mortton_lit_pyre = 80,
                mortton_logs_on_pyre = 75,
                mortton_made_serum = 10,
                mortton_not_started = 0,
                mortton_quest_complete = 85,
                mortton_read_diary = 5,
                mortton_rebuild_temple = 55,
                mortton_received_blood_diary = 10,
                mortton_received_swamp_diary = 9,
                mortton_shades_to_razmire = 45,
                mortton_shades_to_ulsquire = 47,
                mortton_temple_fullpool_warning = 31,
                mortton_ulsquire_temple = 50,
                mortton_unlocked_shade_lair = 29,
                mortton_used_serum_on_razmire = 2,
                mortton_used_serum_on_ulsquire = 0,
                not_started = 0,
                player_made_perm_serum = 7,
                razmire_perm_serum_used = 6,
                razmire_visible = 3,
                shadeattack = 4,
                shades_table_searched = 8,
                shades_ulsquire_oil_given = 28,
                ulsquire_perm_serum_used = 5,
                ulsquire_visible = 1,
            },
            row = "quest_shadesofmortton",
            display = "Shades of Mort'ton",
            points = 3,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- setup cheats' effects are not client-side yet

        t.exec("quest.stage.mortton_not_started", t.quest.expect_stage, "mortton_not_started")

        -- DOORS (docs/QUEST_ORCHESTRATOR.md standing rule, 2026-10-03): no goto lands in or leaves a
        -- closed space. Razmire's shop and Ulsquire's house are walled rooms, each with one
        -- `poordoor` (doors.loc: next_loc_stage poordooropen) -- maps/m54_51.jl2:
        --   Razmire  door 3488,3294 (north edge), street tile 3488,3294, room x 3487-3490 z 3295-3297
        --   Ulsquire door 3494,3289 (east edge),  street tile 3494,3289, room x 3495-3497 z 3288-3291
        -- Every entry and exit walks through the door: click the closed leaf, or -- when an earlier
        -- press left it open (a door swings back after 500 ticks) -- assert the open leaf stands
        -- beside the door tile and walk through without pressing it again.
        local function tile_text(r, tt)
            if r == "ok" and type(tt) == "table" then
                return tostring(tt.x) .. "," .. tostring(tt.z) .. "," .. tostring(tt.level)
            end
            return tostring(r) .. " " .. tostring(tt)
        end
        local function loc_text(r, d)
            if r == "ok" and type(d) == "table" then
                return "at " .. tostring(d.tile_x) .. "," .. tostring(d.tile_z) .. "," .. tostring(d.level)
            end
            return tostring(r) .. " " .. tostring(d)
        end
        -- A live-pool presence read (player.by_symbol only resolves a symbol and answers ok
        -- whether or not the npc is spawned, so it cannot fail).
        local function check_npc_present(name, sym, radius)
            local r, row = t.npc.nearest(sym, radius)
            local where = tostring(row)
            if type(row) == "table" then
                where = "slot " .. tostring(row.slot) .. " at " .. tostring(row.x) .. "," .. tostring(row.z)
            end
            t.check(name, r == "ok", "npc.nearest(" .. sym .. ", " .. tostring(radius) .. ") -> " .. tostring(r) .. " " .. where)
            return r == "ok"
        end
        -- Razmire's and Ulsquire's cure is npc_changetype(<cured>, 200) plus a 200-tick queue that
        -- clears the *_visible bit (ulsquire_shauncy.rs2:15-17,43-44; razmire_keelgan.rs2 the
        -- same). While the cured form is live, op1 Talk-to reaches @*_talk; once the AFFLICTED
        -- form is back the bit is gone and op1 falls to @afflicted_talk (ulsquire_shauncy.rs2:
        -- 53-58). So read the live pool: the afflicted form gets a fresh dose of Serum 207
        -- (its use path ends in the same @*_talk label), the cured one a plain Talk-to.
        -- A re-cure spends the smallest Serum 207 already carried (every cure leaves the
        -- vial one dose lighter: mort_serum3 -> 2 -> 1, quest_mortton.obj next_obj_stage)
        -- and mixes a fresh pair only when none is left, so the ashes/tarrominvial pairs
        -- stay in reserve for the 3-dose Serum 207 the sacred flame turns into Serum 208.
        local function talk_or_recure(talk_row, mix_row, recure_row, cured_sym, afflicted_sym)
            local afflicted_result, afflicted_row = t.npc.nearest(afflicted_sym, 8)
            if afflicted_result == "ok" then
                t.note(afflicted_sym .. " is the live form (slot " .. tostring(afflicted_row.slot)
                    .. ") -- the 200-tick cure lapsed, re-curing")
                local dose_item = nil
                for _, dose_name in ipairs({ "mort_serum1", "mort_serum2", "mort_serum3" }) do
                    local dose_result, dose_count = t.inv.count(dose_name)
                    if dose_item == nil and dose_result == "ok" and type(dose_count) == "number" and dose_count > 0 then
                        dose_item = dose_name
                    end
                end
                if dose_item == nil then
                    t.exec(mix_row, t.player.use_item_on_item, "ashes", "tarrominvial")
                    dose_item = "mort_serum3"
                else
                    t.note("re-curing with a carried " .. dose_item .. " (no fresh mix)")
                end
                local afflicted_target = t.player.by_symbol("npc", afflicted_sym)
                t.exec(recure_row, t.player.use_on, dose_item, afflicted_target)
            else
                t.note(afflicted_sym .. ": " .. tostring(afflicted_result) .. " -- the cured form is live")
                check_npc_present(talk_row .. ".find", cured_sym, 8)
                t.exec(talk_row, t.player.talk_to, cured_sym, 1)
            end
        end
        local RAZMIRE = {
            name = "Razmire's shop", tag = "Razmire", door_x = 3488, door_z = 3294,
            street_x = 3488, street_z = 3294, room_x = 3488, room_z = 3295,
            inside = function(tt) return tt.x >= 3487 and tt.x <= 3490 and tt.z >= 3295 and tt.z <= 3297 end,
        }
        local ULSQUIRE = {
            name = "Ulsquire's house", tag = "Ulsquire", door_x = 3494, door_z = 3289,
            street_x = 3494, street_z = 3289, room_x = 3495, room_z = 3289,
            inside = function(tt) return tt.x >= 3495 and tt.x <= 3497 and tt.z >= 3288 and tt.z <= 3291 end,
        }
        local inside_house = nil
        local function pass_door(prefix, house, entering)
            local near_x, near_z, far_x, far_z = house.street_x, house.street_z, house.room_x, house.room_z
            if not entering then
                near_x, near_z, far_x, far_z = house.room_x, house.room_z, house.street_x, house.street_z
            end
            -- An npc op inside the room can walk the player out through the OPEN door by itself
            -- (use_on's walk_near steps off the npc's tile, and Razmire likes the inside door
            -- tile): then he is on the far side already, and the open leaf is what he crossed --
            -- assert it, check the side, do not walk back in to walk out again.
            local sr, st = t.world.tile()
            if sr == "ok" and st.level == 0 and house.inside(st) == entering
                and math.abs(st.x - far_x) <= 2 and math.abs(st.z - far_z) <= 2 then
                local cr0, cd0 = t.world.loc_near("poordoor", 3)
                local or0, od0 = t.world.loc_near("poordooropen", 3)
                t.check(prefix .. ".alreadyThrough",
                    or0 == "ok" and math.abs(od0.tile_x - house.door_x) <= 1 and math.abs(od0.tile_z - house.door_z) <= 1
                        and not (cr0 == "ok" and cd0.tile_x == house.door_x and cd0.tile_z == house.door_z),
                    "already " .. (entering and "inside " or "outside ") .. house.name .. " at " .. tile_text(sr, st)
                        .. " (an npc op walked the player through); poordooropen " .. loc_text(or0, od0)
                        .. ", closed leaf " .. loc_text(cr0, cd0) .. " (want the open leaf within 1 of "
                        .. house.door_x .. "," .. house.door_z .. " and no closed leaf on it)")
                inside_house = entering and house or nil
                return
            end
            t.player.walk_to(near_x, near_z)
            local nr, nt = t.world.tile()
            t.check(prefix .. ".atDoor",
                nr == "ok" and nt.level == 0 and math.abs(nt.x - near_x) <= 1 and math.abs(nt.z - near_z) <= 1
                    and house.inside(nt) == (not entering),
                "walked to " .. near_x .. "," .. near_z .. " on the " .. (entering and "street" or "room")
                    .. " side of " .. house.name .. "'s door at " .. house.door_x .. "," .. house.door_z
                    .. " -> " .. tile_text(nr, nt))
            local cr, cd = t.world.loc_near("poordoor", 3)
            if cr == "ok" and cd.tile_x == house.door_x and cd.tile_z == house.door_z then
                t.exec(prefix .. ".openDoor", t.player.click_loc, "poordoor", 1, { at = { house.door_x, house.door_z } })
                t.ticks(1)
            else
                local orr, od = t.world.loc_near("poordooropen", 3)
                t.check(prefix .. ".doorStandsOpen",
                    orr == "ok" and math.abs(od.tile_x - house.door_x) <= 1 and math.abs(od.tile_z - house.door_z) <= 1,
                    "poordoor at " .. house.door_x .. "," .. house.door_z .. ": nearest closed leaf " .. loc_text(cr, cd)
                        .. "; poordooropen " .. loc_text(orr, od)
                        .. " (want the open leaf within 1 of the door tile: an earlier press left it open, so it is"
                        .. " walked through, not pressed again)")
            end
            t.player.walk_to(far_x, far_z)
            local fr, ft = t.world.tile()
            t.check(prefix .. ".throughDoor",
                fr == "ok" and ft.level == 0 and math.abs(ft.x - far_x) <= 1 and math.abs(ft.z - far_z) <= 1
                    and house.inside(ft) == entering,
                "walked through to " .. far_x .. "," .. far_z .. " -> " .. tile_text(fr, ft) .. " (want "
                    .. (entering and "inside " or "outside ") .. house.name .. ")")
            if entering then
                inside_house = house
            else
                inside_house = nil
            end
        end
        -- Walk out of whichever room the player is in, then in through `house`'s door.
        local function enter_house(prefix, house)
            if inside_house == house then
                return
            end
            if inside_house ~= nil then
                pass_door(prefix .. ".leave" .. inside_house.tag, inside_house, false)
            end
            pass_door(prefix .. ".enter" .. house.tag, house, true)
        end
        local function leave_house(prefix)
            if inside_house ~= nil then
                pass_door(prefix .. ".leave" .. inside_house.tag, inside_house, false)
            end
        end

        -- DIAGNOSTIC (run 3): the shade hunt's own screenshot (42-shade.hunt-FAIL.png,
        -- run 2) showed "I'm already under attack." refusing every Attack press,
        -- exactly the single-way symptom the four setup ::passive lines are
        -- supposed to prevent -- but client.log carries no chat/say() text at
        -- all (grepped: neither that line nor any "is passive"/"Passive:"
        -- confirmation appears there), so there is no way to tell from the
        -- log alone whether setup's four ::passive cheats actually registered.
        -- Read the held list back directly.
        -- t.cheat's own detail is hollow here (api_drive.cheat's synchronous
        -- reply, not the async chat line) -- REVERTED last_failure (sampler
        -- sonnet-b13): this row PASSed as "ok (nil)" without ever reading
        -- whether the five setup ::passive lines actually registered (trap
        -- 12). Bare "::passive" prints one "Passive: <name> (<id>)." line
        -- PER held type -- read the ring back and require all five.
        local passive_list_result = t.cheat("::passive")
        local passive_msgs_result, passive_msgs = t.msg.last(8)
        local passive_lines = {}
        if passive_msgs_result == "ok" then
            for i = 1, #passive_msgs do
                if string.find(passive_msgs[i].text, "Passive:", 1, true) then
                    table.insert(passive_lines, passive_msgs[i].text)
                end
            end
        end
        t.check("diagnostic.passiveListHeld",
            passive_list_result == "ok" and #passive_lines == 5,
            "t.cheat(::passive) -> " .. tostring(passive_list_result) .. "; held (" .. tostring(#passive_lines)
                .. "/5 expected -- mort_afflicted_man/_man2/_woman/_woman2/shadeshadow_level1): "
                .. table.concat(passive_lines, " | "))

        -- Search the experiment shelf (all.loc:35647-35662, op1=Search) in
        -- the south building of Mort'ton for Herbi Flax's diary. click_loc
        -- steps off the loc's own tile and walks its far sides before
        -- projecting, so the real op-1 click lands. The building is a ruin (crumblywall, no door:
        -- reach.py walks in with every door closed), so the overland hop lands on the open
        -- street outside it, 3485,3282, and the click walks the player in.
        --
        -- INTO MORYTANIA (fix_b66; owner ruling 2026-10-05: the run's first goto obeys the door
        -- rule). The fixture stands in Lumbridge (3206,3233); no flood from there crosses the
        -- Salve (reach 3206,3233 -> 3485,3282 UNREACHABLE at margin 600), and no spell this pack
        -- implements lands in Morytania (skill_magic/scripts/spells/teleport.rs2: the standard
        -- book stops at Trollheim; Kharyrll is Desert Treasure's, the Ectophial Ghosts Ahoy's).
        -- So the way in is the one Priest in Peril opens, walked like makinghistory.lua's
        -- enterMorytania (checked with sample_tools/reach.py --root <worktree>, doors closed):
        --   * Lumbridge 3206,3233 -> 3318,3468, west of the Varrock members' gate: open ground
        --     (REACH closed-doors len=389 at margins 30/80/160).
        --   * fai_varrock_member_gatel 3319,3468 (the only walk to the temple), then
        --     3321,3468 -> 3405,3506 beside the Paterdomus trapdoor (REACH len=122).
        --   * The trapdoor (3405,3507), the two mausoleum gates, Drezel's advice
        --     (mausoleum_drezel.rs2:145-154, 60 -> 61; needs dagger_wolfbane held, :28-33) and the
        --     holy barrier (mausoleum_interactions.rs2:26, p_telejump out at 3423,3485).
        --   * 3423,3485 -> 3510,3470 on the Canifis road, east of the Mort Myre gate
        --     (REACH closed-doors len=110 at margin 30).
        --   * 3510,3470 -> 3485,3282: south through the Haunted Woods into the swamp's open
        --     east edge and down to Mort'ton (REACH closed-doors len=779 at margin 80). The
        --     shorter walk west of it goes through mortmyre_metalgateclosed_r/l 3443-3444,3458,
        --     which only opens once Nature Spirit is started (quest_druidspirit.rs2:16-20), so
        --     the route never uses that gate.
        t.exec("goto-enterMorytania.varrockGate", t.player.goto_tile, 3318, 3468, 0)
        t.exec("enterMorytania.varrockGate", t.player.pass_door, { closed = "fai_varrock_member_gatel",
            open = "fai_varrock_member_gatel_open", at = { 3319, 3468, 0 }, near = { 3318, 3468 }, far = { 3321, 3468 } })
        t.exec("goto-enterMorytania.trapdoor", t.player.goto_tile, 3405, 3506, 0)
        t.exec("enterMorytania.openTrapdoor", t.player.click_loc, "trapdoor", 1, { at = { 3405, 3507, 0 } })
        t.await({
            level = function()
                return t.world.loc_near("trapdoor_open", 3, { at = { 3405, 3507, 0 } }) == "ok"
            end,
            note = "enterMorytania: the trapdoor opens",
        }, 6)
        local tdo_r, tdo = t.world.loc_near("trapdoor_open", 3, { at = { 3405, 3507, 0 } })
        local tdc_r = t.world.loc_near("trapdoor", 3, { at = { 3405, 3507, 0 } })
        t.check("enterMorytania.trapdoorOpen", tdo_r == "ok" and tdc_r ~= "ok",
            "trapdoor_open on 3405,3507,0 -> " .. tostring(tdo_r) .. " "
                .. (tdo_r == "ok" and (tdo.tile_x .. "," .. tdo.tile_z .. "," .. tdo.level) or tostring(tdo))
                .. "; closed trapdoor there -> " .. tostring(tdc_r) .. " (want the open leaf and no closed one)")
        t.exec("enterMorytania.descend", t.player.climb, { loc = "trapdoor_open", op = 1, op_name = "Climb-down",
            at = { 3405, 3507, 0 }, src = { 3405, 3506 }, dest = { 3405, 9906, 0 } })
        t.exec("enterMorytania.gate1", t.player.cross_gate, { loc = "pip_underground_door1", at = { 3405, 9895, 0 },
            near = { 3405, 9896 }, far_ok = function(tile) return tile.z > 6400 and tile.z <= 9894 end,
            far_desc = "south of the golden-key gate, z <= 9894", ticks = 30 })
        t.exec("enterMorytania.gate2", t.player.cross_gate, { loc = "pip_underground_door2", at = { 3431, 9897, 0 },
            near = { 3430, 9897 }, far_ok = function(tile) return tile.z > 6400 and tile.x >= 3432 end,
            far_desc = "Drezel's side of the second gate, x >= 3432", ticks = 60 })
        -- Priest in Peril's farewell advice (mausoleum_drezel.rs2:145-154,
        -- LostCity drezel.rs2:138-147): 60 -> 61, the holy barrier opens. The
        -- advice has no combat-level branch, so the staged combat stats change nothing here.
        t.exec("enterMorytania.talkToDrezel", t.player.talk_to, "priestperiltrappedmonk2", 1)
        t.exec("enterMorytania.talkToDrezel-dialog", t.chat.play, {
            "player:So can I pass through that barrier now?",
            "npc:Ah, ",
            "npc:Morytania is an evil land",
            "npc:You should take some basic precautions",
            "npc:In many ways Werewolves",
            "npc:and it is a holy relic",
            "npc:wolf form is incredibly powerful",
            "player:Okay, I will keep it equipped",
        })
        t.exec("enterMorytania.drezelAdvice", t.var.await_server, "varp302_priestperil", 61, 8)
        t.exec("enterMorytania.holyBarrier", t.player.cross_gate, { loc = "pip_underground_wall_side_withportal",
            at = { 3440, 9886, 0 }, near = { 3440, 9887 },
            far_ok = function(tile) return tile.x == 3423 and tile.z == 3485 end,
            far_desc = "east of the Salve at 3423,3485 (mausoleum_interactions.rs2 p_telejump(0_53_54_31_29))" })
        t.exec("enterMorytania.holyBarrier-msg", t.msg.expect, "You pass through the holy barrier")
        t.exec("goto-enterMorytania.canifisRoad", t.player.goto_tile, 3510, 3470, 0)
        t.exec("goto-searchShelf", t.player.goto_tile, 3485, 3282, 0)
        local shelf_click_result, shelf_click_detail = t.player.click_loc("shades_experimentshelf", 1)
        -- t.settle() does not cover an inventory sync racing the mesbox's
        -- own inv_add (docs/QUEST_AUTHORING.md section 8) -- poll instead.
        local serum_book_result, serum_book_count = t.inv.await("serum_book", 1, 10)
        t.check("searchShelf", shelf_click_result == "ok" and serum_book_result == "ok",
            "click_loc(shades_experimentshelf, op1) -> " .. tostring(shelf_click_result) .. " "
                .. tostring(shelf_click_detail) .. "; inv.await(serum_book, 1) -> "
                .. tostring(serum_book_result) .. " (" .. tostring(serum_book_count) .. ")")
        t.expect("haveSerumBook", t.inv.expect_has("serum_book", 1))
        -- The shelf's own mesbox ("You find an interesting looking book on
        -- the shelf.") is still open -- close it before reading the diary.
        t.exec("searchShelf-dismiss", t.chat.play, {"mesbox:You find an interesting looking book"})

        -- Read the diary (serum_book.rs2:8, [opheld1,serum_book] -- a
        -- numbered held op, op 1). It plays through 25 mesbox pages; the
        -- quest varp write lands after page 20 is dismissed, so the pages
        -- have to actually be clicked through, not skipped.
        t.exec("readDiary", t.player.inv_op, "serum_book", 1)
        t.exec("readDiary-drain", t.chat.drain, {})
        t.exec("quest.stage.mortton_read_diary", t.quest.expect_stage, "mortton_read_diary")

        -- Mix Serum 207 for Razmire's FIRST cure: ashes used ON the
        -- tarrominvial (OPHELDU), brew_potion.rs2:14-18 ->
        -- quest_mortton.rs2:103-111's ~mortton_mix_serum ->
        -- brew_potion.rs2:503-548's ~attempt_brew_potion. A fresh mix always
        -- comes out named "mort_serum3" at dose_count 3
        -- (quest_mortton.obj), so every cure below mixes its OWN fresh pair
        -- right before use rather than tracking a partially-drunk dose
        -- under a different name (mort_serum2/mort_serum1).
        t.exec("addAshes", t.player.use_item_on_item, "ashes", "tarrominvial")
        local stage_after_mix1_result, stage_after_mix1_value = t.quest.stage()
        t.check("mixSerum207.stageAdvanced", stage_after_mix1_result == "ok" and stage_after_mix1_value == 10,
            "quest.stage() -> " .. tostring(stage_after_mix1_result) .. " (" .. tostring(stage_after_mix1_value)
                .. ") -- mortton_made_serum(10) after addAshes's OPHELDU click, the fixed behaviour")
        t.expect("mixSerum207.firstDose", t.inv.expect_has("mort_serum3", 1))
        t.exec("quest.stage.mortton_made_serum", t.quest.expect_stage, "mortton_made_serum")

        -- Gear up for real -- ::give put the scimitar in the backpack, it
        -- did not wear it (section 8's player.attack note).
        t.exec("gearUp.scimitar", t.player.equip, "rune_scimitar")

        -- Use Serum 207 on the afflicted Razmire Keelgan (spawn row
        -- m54_51.spawn:35, 3489,3296,0) -- razmire_keelgan.rs2's
        -- razmire_use_item label (oc_category 108, quest_mortton.obj's
        -- serum_207 category) temporarily cures him and opens razmire_talk.
        -- On foot from the shelf to his door, and in through it (no goto: the shop is walled).
        enter_house("razmire", RAZMIRE)
        local razmire_afflicted = t.player.by_symbol("npc", "razmire_keelgan_afflicted")
        check_npc_present("razmire.find", "razmire_keelgan_afflicted", 6)
        t.exec("razmire.cure", t.player.use_on, "mort_serum3", razmire_afflicted)
        -- razmire_talk (first visit): "Ah, excellent..." -> since
        -- %morttonquest < mortton_kill_shades -> razmire_questions ->
        -- choose "What are all these shadow creatures?" -> razmire_creatures
        -- -> "Yes, I'll dispatch..." sets %morttonquest = mortton_kill_shades.
        -- Chat text copied verbatim from razmire_keelgan.rs2 (row text for
        -- choose:, the ECHOED player line differs by one letter -- "shadow"
        -- vs "shadowy" -- and is spelled exactly as the .rs2 has it).
        t.exec("razmire.acceptKillShades", t.chat.play, {
            "npc:you've made your own serum",
            "choose:What are all these shadow creatures?",
            "player:What are all these shadowy creatures?",
            "npc:Those disgusting entities are the filth",
            "npc:I've heard that they jealously guard",
            "choose:Yes, I'll dispatch those dark and evil creatures.",
            "player:Yes, I'll dispatch those dark and evil creatures.",
            "npc:Great, that's what I wanted to hear",
        })
        t.exec("quest.stage.mortton_kill_shades", t.quest.expect_stage, "mortton_kill_shades")

        -- Mix Serum 207 for Ulsquire's FIRST cure now, while the two are
        -- close together and before the hunt eats the backpack -- one fresh
        -- pair, used on him further down.
        t.exec("mixSerum207-2", t.player.use_item_on_item, "ashes", "tarrominvial")
        t.expect("mixSerum207.secondDose", t.inv.expect_has("mort_serum3", 1))

        -- Kill five Loar shades (mortton_shades.npc's shadeshadow_level1:
        -- hitpoints 38, attack 45, defence 26 -- well within the setup's
        -- 40/40/40/99 combat stats) and loot their remains
        -- (mortton_shades.npc's death_drop=shade_bones1).
        --
        -- t.player.attack takes the npc SYMBOL directly (combat.lua's
        -- QD.player.attack resolves it itself through by_symbol/
        -- npc.nearest) -- not a resolved {kind,id} table the way click_loc/
        -- use_on's target argument works; handing it one raised `bad
        -- argument #2 to 'symbol' (string expected, got table)` on the
        -- first attempt (see queue history).
        --
        -- The first real attempt filled the chat ring edge-to-edge with
        -- "I can't reach that!" on every single one of 41 Attack presses --
        -- not an accuracy roll at all. Razmire's afflicted spawn (and the
        -- cure) sit on his shop's raised porch behind a picket fence
        -- (shade.attack-1.png/44-blocked.png); t.player.walk_near answered
        -- "within 2" from there because it is a bare tile-distance check,
        -- not a path/line-of-sight one, so it never caught the fence. Fixed
        -- by leaving the shop through its door and walking to shadeshadow_level1's own spawn
        -- tile (m54_51.spawn, clear street) -- on foot, not a goto out of the walled shop.
        leave_house("razmire")
        t.player.walk_to(3488, 3288)
        local hunt_start_result, hunt_start_tile = t.world.tile()
        t.check("walkToShadeStreet",
            hunt_start_result == "ok" and hunt_start_tile.level == 0 and math.abs(hunt_start_tile.x - 3488) <= 1
                and math.abs(hunt_start_tile.z - 3288) <= 1,
            "walked from the shop door to the shade street 3488,3288 -> " .. tile_text(hunt_start_result, hunt_start_tile))

        -- Real combat off the porch still lands hits at a low rate (one
        -- probe kill needed 51+40 real ticks before a still-live read), so
        -- this is a genuine retry loop, not five independent one-shot
        -- fights -- section 8's rule: "a retry loop that writes a t.step
        -- row per ATTEMPT fails the every-row-PASS rule over attempts that
        -- were only a walk... record the loop's OUTCOME row only." Each
        -- round re-finds the nearest live shadow (a prior round's target may
        -- already be dead or may have moved), presses Attack, gives the
        -- fight a real window to land hits, and picks up whatever remains
        -- are on the ground; the loop's own exit condition is shade_bones1
        -- actually reaching 5 in the backpack, read back after every round.
        -- Eat through the kill wait's own opts.eat (verbs-combat, "Eating"):
        -- a shark whenever stated hitpoints fall under 60.
        -- shadeshadow_level1 is ITSELF huntmode=aggressive, huntrange 3
        -- (mortton_shades.npc) -- 31 copies packed along this street, so
        -- several aggro at once regardless of the four ::passive Afflicted
        -- types. A loop that re-resolves-and-attacks fresh every round can
        -- abandon a shade it is still mid-fight with (docs/QUEST_AUTHORING.md
        -- section 3's npc.await_dead_engaged note: "a loop that re-resolves
        -- its symbol each attempt abandons the half-killed npc it already
        -- engaged") -- run 2/3 measured exactly that signature (0 kills in 20
        -- rounds, "I'm already under attack." on repeat, 42-shade.hunt-FAIL.png)
        -- because a fresh attack() call every round can pick a DIFFERENT
        -- nearest copy than the one still holding the fight claim on the
        -- player. Track engagement across rounds instead: only resolve+press
        -- a NEW target when not currently engaged, and on a timeout keep
        -- polling await_dead_engaged (it re-issues Attack on the same slot)
        -- rather than re-resolving.
        -- Each kill gets a MARGIN row: the lowest hitpoints the eat wait read over that fight
        -- (opts.eat's own "lowest hp x/y", carried across a timed-out round on the same slot)
        -- must be >= 25 AND a shark must still be in the pack -- both, never either. Eating is
        -- the wait's own (shark below 60, at most once per 3 ticks), so it holds whether or
        -- not an eat delays a queued hit, and nothing here leans on prayer.
        local shade_rounds = 0
        local shade_bones_count = 0
        local shade_engaged = false
        local shade_kills = 0
        local fight_lowest = nil
        local fight_eats = {}
        while shade_bones_count < 5 and shade_rounds < 30 do
            shade_rounds = shade_rounds + 1
            if not shade_engaged then
                local shade_target, shade_target_status = t.player.by_symbol("npc", "shadeshadow_level1")
                t.note("round " .. tostring(shade_rounds) .. " by_symbol: " .. tostring(shade_target_status))
                if shade_target_status == "ok" then
                    t.player.walk_near(shade_target, 20, 1)
                    local attack_result, attack_detail = t.player.attack("shadeshadow_level1", 2, 30)
                    t.note("round " .. tostring(shade_rounds) .. " attack: " .. tostring(attack_result)
                        .. " (" .. tostring(attack_detail) .. ")")
                    shade_engaged = true
                end
            end
            if shade_engaged then
                local dead_result, dead_detail = t.npc.await_dead_engaged(80, 15,
                    { eat = { item = "shark", below = 60 } })
                t.note("round " .. tostring(shade_rounds) .. " await_dead_engaged: " .. tostring(dead_result)
                    .. " (" .. tostring(dead_detail) .. ")")
                local round_lowest = tonumber(string.match(tostring(dead_detail), "lowest hp (%d+)/"))
                if round_lowest ~= nil and (fight_lowest == nil or round_lowest < fight_lowest) then
                    fight_lowest = round_lowest
                end
                local round_eats = string.match(tostring(dead_detail), "; eat shark below 60: ([^,;]*)")
                if round_eats ~= nil then
                    table.insert(fight_eats, round_eats)
                end
                if dead_result == "ok" then
                    shade_engaged = false
                    shade_kills = shade_kills + 1
                    local shark_left_result, shark_left = t.inv.count("shark")
                    t.check("shade.fight" .. tostring(shade_kills) .. ".margin",
                        fight_lowest ~= nil and fight_lowest >= 25 and shark_left_result == "ok"
                            and type(shark_left) == "number" and shark_left > 0,
                        "kill " .. tostring(shade_kills) .. " of 5: lowest hp " .. tostring(fight_lowest)
                            .. " (want >= 25) AND sharks left " .. tostring(shark_left) .. " (want > 0); eats: "
                            .. table.concat(fight_eats, " | "))
                    fight_lowest = nil
                    fight_eats = {}
                    t.player.click_obj("shade_bones1", 3)
                elseif dead_result == "no_row" then
                    -- the stamped engagement is gone (the attack above never
                    -- really landed) -- let the next round resolve fresh.
                    shade_engaged = false
                end
                -- on timeout, shade_engaged stays true and the next round
                -- polls await_dead_engaged again on the SAME slot instead of
                -- abandoning it for a freshly-resolved target.
            end
            local bones_result, bones_value = t.inv.count("shade_bones1")
            if bones_result == "ok" and type(bones_value) == "number" then
                shade_bones_count = bones_value
            end
        end
        t.check("shade.hunt", shade_bones_count >= 5,
            "hunted " .. tostring(shade_rounds) .. " round(s) off " .. "3488,3288,0"
                .. ", shade_bones1 in backpack: " .. tostring(shade_bones_count))
        t.expect("player.aliveAfterShades", t.player.alive())
        t.exec("quest.stage.mortton_killed_5_shades", t.quest.expect_stage, "mortton_killed_5_shades")
        t.expect("shade.remainsCollected", t.inv.expect_has("shade_bones1", 5))

        -- Hand two remains to Razmire (razmire_talk's killed_5_shades
        -- branch, razmire_keelgan.rs2:99-112). His cure from mixSerum207-1
        -- lasts only 200 ticks and the hunt above almost certainly ran past
        -- that (npc_changetype(razmire_keelgan_afflicted,200) reverts him
        -- and clears the razmire_visible bit on the same timer) -- check
        -- which type is actually live and, if he has reverted, mix a fresh
        -- dose and re-cure through the afflicted symbol instead of talking
        -- to a base symbol that is not the one spawned right now.
        -- The hunt ends wherever the last shade fell on the open street; the goto lands on the
        -- street south of the shop (3488,3290, open) and the player walks in through the door.
        t.exec("goto-razmire-2", t.player.goto_tile, 3488, 3290, 0)
        enter_house("razmire-2", RAZMIRE)
        -- player.by_symbol answers ok for any content symbol; the live pool says which form
        -- is spawned right now.
        talk_or_recure("razmire.giveRemains", "mixSerum207-3", "razmire.recure",
            "razmire_keelgan", "razmire_keelgan_afflicted")
        -- razmire_talk's opening line is now ALWAYS "Ah, it's you, how's it
        -- going?" (testbit(mortton_used_serum_on_razmire) was set true on
        -- the very first cure above and never clears), whichever of the two
        -- branches just opened the dialogue -- then the killed_5_shades body
        -- below it. Decline the store offer here with "Ok, thanks." and open
        -- the builders' store separately through t.shop.open (a direct
        -- numbered press, not a menu choice).
        t.exec("razmire.giveRemains-dialogue", t.chat.play, {
            "npc:it's you",
            "npc:have you killed the five shades yet",
            "player:Yes, I have actually!",
            "mesbox:Razmire takes two Shade remains.",
            "npc:I'll experiment on these",
            "npc:Now that you've slayed",
            "choose:Ok, thanks.",
            "player:Ok, thanks",
        })
        t.exec("quest.stage.mortton_shades_to_razmire", t.quest.expect_stage, "mortton_shades_to_razmire")

        -- Free space before shopping, keeping ONE ashes/tarrominvial pair in
        -- reserve for a later re-cure (mixSerum207-4 and on) -- run 5's own
        -- 59-razmire.buyLimestone.png showed the backpack already full of
        -- the leftover pairs plus five shade_bones1/timberbeam/limestonebrick
        -- (none of limestonebrick/timberbeam/swamppaste/ashes/tarrominvial
        -- stack -- each unit is its own slot), and swamppaste then answered
        -- "You don't have enough inventory space." for every one of the 25
        -- asked.
        -- The hunt is over and the fights never needed food: keep one shark and drop the rest
        -- for shop space (the trip loop below does the same per trip). Keep TWO serum pairs,
        -- not one: the shop trips and the oil visit can each find a cure lapsed
        -- (talk_or_recure above), and the pyre hand-in needs one more.
        local first_shark_result, first_shark_have = t.inv.count("shark")
        local first_shark_drops = 0
        while first_shark_result == "ok" and type(first_shark_have) == "number" and first_shark_have > 1
            and first_shark_drops < 4 do
            t.player.drop("shark")
            first_shark_drops = first_shark_drops + 1
            first_shark_result, first_shark_have = t.inv.count("shark")
        end
        t.check("razmire.freeSharkSlots",
            first_shark_result == "ok" and type(first_shark_have) == "number" and first_shark_have == 1,
            "dropped " .. tostring(first_shark_drops) .. " shark(s) before the builders' store, "
                .. tostring(first_shark_have) .. " left (want exactly 1 kept)")
        local ashes_before_drop_result, ashes_before_drop_count = t.inv.count("ashes")
        if ashes_before_drop_result == "ok" and type(ashes_before_drop_count) == "number"
            and ashes_before_drop_count > 2 then
            t.player.drop("ashes")
        end
        local tarrominvial_before_drop_result, tarrominvial_before_drop_count = t.inv.count("tarrominvial")
        if tarrominvial_before_drop_result == "ok" and type(tarrominvial_before_drop_count) == "number"
            and tarrominvial_before_drop_count > 2 then
            t.player.drop("tarrominvial")
        end

        -- Buy the temple's building materials for real: t.shop.* landed
        -- 2026-09-22 (docs/QUEST_AUTHORING.md section 3's shop table).
        -- razmire_keelgan.rs2's [opnpc4,razmire_keelgan] opens
        -- razmire_building_open (~openshop(razmirebuildingstore, ...))
        -- directly once %morttonquest >= mortton_shades_to_razmire, no
        -- dialogue needed. Quest Helper's own buyTimberLimeAndSwamp step:
        -- "Buy 5 timber beams, 5 limestone bricks, and 25 swamp paste from
        -- Razmire's builders' store" -- but none of the three stack, so
        -- swamp paste's count is capped to whatever backpack space is
        -- actually free after the other two, rather than hard-coding 25 and
        -- failing on "not enough inventory space" again.
        t.exec("razmire.shopOpen", t.shop.open, "razmire_keelgan", 4, "razmirebuildingstore")
        t.exec("razmire.buyTimber", t.shop.buy, "timberbeam", 5)
        t.exec("razmire.buyLimestone", t.shop.buy, "limestonebrick", 5)
        local free_slots_before_paste = 0
        for slot_index = 0, 27 do
            local slot_result, slot_cell = t.inv.slot(slot_index)
            if slot_result == "ok" and (slot_cell.name == "" or slot_cell.count == 0) then
                free_slots_before_paste = free_slots_before_paste + 1
            end
        end
        local swamppaste_target = 25
        if free_slots_before_paste - 1 < swamppaste_target then
            swamppaste_target = free_slots_before_paste - 1
        end
        if swamppaste_target < 5 then
            swamppaste_target = 5
        end
        t.check("razmire.spaceForSwamppaste", free_slots_before_paste > 0,
            tostring(free_slots_before_paste) .. " free slot(s) after timber/limestone -- buying "
                .. tostring(swamppaste_target) .. " swamppaste (25 asked, capped to fit)")
        t.exec("razmire.buySwamppaste", t.shop.buy, "swamppaste", swamppaste_target)
        local shop_close_result = t.shop.close()
        t.check("razmire.shopClose", shop_close_result == "ok", "shop.close() -> " .. tostring(shop_close_result))

        -- Use the second Serum 207 dose on the afflicted Ulsquire Shauncy
        -- (spawn row m54_51.spawn:44, 3496,3289,0) -- ulsquire_shauncy.rs2's
        -- ulsquire_talk, at %morttonquest = mortton_shades_to_razmire,
        -- takes the remaining shade_bones1 straight away (no menu).
        -- Out of Razmire's shop through its door, along the street and in through Ulsquire's.
        enter_house("ulsquire", ULSQUIRE)
        local ulsquire_afflicted = t.player.by_symbol("npc", "ulsquire_shauncy_afflicted")
        check_npc_present("ulsquire.find", "ulsquire_shauncy_afflicted", 6)
        t.exec("ulsquire.cure", t.player.use_on, "mort_serum3", ulsquire_afflicted)
        t.exec("talkToUlsquire", t.chat.play, {
            "npc:you've made your own serum",
            "player:Razmire said to come and talk to you.",
            "npc:Oh yes, well, that's very nice.",
            "player:I just slayed 5 shades for Razmire",
            "npc:Oh yes, that would be interesting!",
            "mesbox:You show the shade remains to the priest",
        })
        t.exec("quest.stage.mortton_shades_to_ulsquire", t.quest.expect_stage, "mortton_shades_to_ulsquire")

        -- Talk to Ulsquire again and ask about the temple -- ulsquire.rs2's
        -- ulsquire_temple label sets %morttonquest = mortton_ulsquire_temple
        -- and names exactly what the temple needs (limestone bricks, swamp
        -- paste, wooden planks) "Thankfully Razmire stocks all these items."
        -- Same visit as the cure above (mortton_used_serum_on_ulsquire is
        -- now true), so the opening line is "Ah, hello again, what can I do
        -- for you now?" before the questions_post_shades menu opens.
        t.exec("ulsquire.askTemple", t.player.talk_to, "ulsquire_shauncy", 1)
        t.exec("ulsquire.askTemple-dialogue", t.chat.play, {
            "npc:hello again",
            "choose:What can you tell me about that temple?",
            "player:What can you tell me about that temple?",
            "npc:Hmm, interesting question",
            "player:How can you rebuild a totally destroyed temple?",
            "npc:I couldn't do it alone",
            "choose:Ok thanks",
            "player:Ok, thanks",
        })
        t.exec("quest.stage.mortton_ulsquire_temple", t.quest.expect_stage, "mortton_ulsquire_temple")

        -- The temple wall (flamtaer_temple.rs2's self-re-arming
        -- [oploc3,_temple_wall], 150 separate repair actions across fifteen
        -- wall locs) cannot be driven by clicking and polling inside this
        -- run's ~2,000-server-tick budget (docs/QUEST_AUTHORING.md section
        -- 9) -- ::mortton_repairtemple ([debugproc], landed 2026-09-22,
        -- docs/QUEST_SERVER_CHEATS.md section A) is the sanctioned
        -- fast-forward: it walks the SAME real ~mortton_temple_build_step
        -- against a real wall loc in a guarded loop, spending the real
        -- backpack materials through ~add_temple_resources, and needs no
        -- click at all -- only %morttonquest>=mortton_ulsquire_temple
        -- (already true here), Crafting 20 (setup) and a hammer (setup).
        -- What binds it is MATERIALS, not ticks: 800 resource pool per 5
        -- swamp paste + 1 limestone brick + 1 timber beam, and Razmire's
        -- store restocks only 5 of each -- so run the hook against the load
        -- already bought above, and if the quest has not yet reached
        -- mortton_can_light_altar(60), Razmire's Serum 207 cure has almost
        -- certainly lapsed again (200-tick npc_changetype), re-cure, buy
        -- another load, and run the hook again.
        -- t.cheat's own detail is hollow (nil) here too -- the debugproc's
        -- real answer is a say() line ("The temple is repaired after N
        -- repair(s)." / "Temple repair stopped at N% after M repair(s) -
        -- bring more limestone bricks, wooden planks and swamp paste."), not
        -- a packet reply. REVERTED last_failure: this shape PASSed twice
        -- with 0 repairs actually made (trap 12) -- read the reply back and
        -- FAIL a call that repaired nothing.
        local repair_result_1, repair_detail_1 = t.cheat("::mortton_repairtemple")
        local repair_msg_result_1, repair_msg_1 = t.msg.expect("repair(s)")
        local repair_count_1 = nil
        if repair_msg_result_1 == "ok" then
            repair_count_1 = tonumber(repair_msg_1:match("after (%d+) repair"))
        end
        t.check("temple.repair-1",
            repair_result_1 == "ok" and repair_msg_result_1 == "ok" and repair_count_1 ~= nil and repair_count_1 > 0,
            "t.cheat(::mortton_repairtemple) -> " .. tostring(repair_result_1) .. " (" .. tostring(repair_detail_1)
                .. "); reply: " .. tostring(repair_msg_result_1) .. " (" .. tostring(repair_msg_1)
                .. "); repairs made: " .. tostring(repair_count_1))
        local temple_stage_result, temple_stage_now = t.quest.stage()
        if temple_stage_result ~= "ok" or type(temple_stage_now) ~= "number" then
            temple_stage_now = 0
        end

        local temple_trip = 1
        while temple_stage_now < 60 and temple_trip < 8 do
            temple_trip = temple_trip + 1
            local trip_suffix = "-" .. tostring(temple_trip)

            -- Which form is live decides talk vs re-cure (talk_or_recure above). On foot, through the doors: out of Ulsquire's house (trip 2) and into Razmire's
            -- shop; a later trip starts inside the shop already and stays.
            enter_house("razmire-temple" .. trip_suffix, RAZMIRE)

            -- Run 2 measured the real bottleneck: NOT shop stock, backpack
            -- SPACE -- none of timberbeam/limestonebrick/swamppaste stack,
            -- buying a flat 5 more of each every trip regardless of what is
            -- already carried fills the pack with unspent timber/limestone
            -- while swamppaste (spent 5-per-refill, five times faster) never
            -- gets a slot free, and shop.buy answered "You don't have enough
            -- inventory space." on every later trip. Two fixes: only top
            -- timber/limestone up to 5 TOTAL (never re-buy what is already
            -- held), and the shade hunt is over by this point in the run, so
            -- the five non-stacking sharks (trap 26's food requirement,
            -- setup) are no longer load-bearing at their original count --
            -- drop down to one kept for safety and spend the reclaimed
            -- slots on the ingredient that actually runs out. Dropped HERE,
            -- before the shop opens: REVERTED last_failure (sampler
            -- sonnet-b13) -- run with this drop done AFTER shop.attach read
            -- "dropped 4 shark(s), 4 left" every trip (t.player.drop failing
            -- silently with the shop interface open, 23 ticks wasted a
            -- trip), and the row still graded true against that reading.
            local shark_result, shark_have = t.inv.count("shark")
            local shark_drops = 0
            while shark_result == "ok" and type(shark_have) == "number" and shark_have > 1 and shark_drops < 4 do
                t.player.drop("shark")
                shark_drops = shark_drops + 1
                shark_result, shark_have = t.inv.count("shark")
            end
            t.check("temple.freeSharkSlots" .. trip_suffix,
                shark_result == "ok" and type(shark_have) == "number" and shark_have <= 1,
                "dropped " .. tostring(shark_drops) .. " shark(s), " .. tostring(shark_have)
                    .. " left (kept for trap 26's food requirement)")

            talk_or_recure("razmire.talk" .. trip_suffix, "mixSerum207" .. trip_suffix, "razmire.recure" .. trip_suffix,
                "razmire_keelgan", "razmire_keelgan_afflicted")
            -- %morttonquest is mortton_rebuild_temple(55) by now (crossed by
            -- the first ::mortton_repairtemple call above), which routes
            -- razmire_talk to a DIFFERENT branch than the
            -- shades_to_razmire-era razmire_store_selection menu used at the
            -- hand-in above: "Ah, it's you, how's it going?" (the
            -- mortton_used_serum_on_razmire bit is already set from the
            -- first cure, so this is never the first-time "made your own
            -- serum" line again) / "Hello there, I've started repairing the
            -- temple." / "That's great, carry on the good work." ->
            -- razmire_questions_post_rebuild's own p_choice5, where case 4
            -- "Can you open a store for me?" goes through
            -- razmire_store_post_oil ("Sure, which store do you wanna see?")
            -- into a SECOND menu (razmire_store_selection_post_oil) before
            -- razmire_building_open actually fires. That label is
            -- ~openshop(razmirebuildingstore,...) directly -- it opens
            -- shopmain with no page of its own, so the list ends at the
            -- "choose:" row (trap 30) and shop.attach binds the screen the
            -- click opened rather than shop.open pressing a closed npc menu
            -- a second time (the fix behind this whole trip loop: shop.buy
            -- answers "this shop was not opened through shop.open" without
            -- it).
            t.exec("razmire.reopenDialogue" .. trip_suffix, t.chat.play, {
                "npc:it's you",
                "player:I've started repairing the temple",
                "npc:carry on the good work",
                "choose:Can you open a store for me?",
                "player:Can you open a store for me?",
                "npc:which store",
                "choose:Can I see the building store please?",
            })
            t.exec("razmire.shopAttach" .. trip_suffix, t.shop.attach, "razmirebuildingstore")

            local timber_result, timber_have = t.inv.count("timberbeam")
            if timber_result ~= "ok" or type(timber_have) ~= "number" then
                timber_have = 0
            end
            if timber_have < 5 then
                t.exec("razmire.buyTimber" .. trip_suffix, t.shop.buy, "timberbeam", 5 - timber_have)
            else
                -- a skip is not a row (it could not fail); the note rides on the next row
                t.note("trip" .. trip_suffix .. ": already holding " .. tostring(timber_have)
                    .. " timberbeam -- not bought")
            end

            local limestone_result, limestone_have = t.inv.count("limestonebrick")
            if limestone_result ~= "ok" or type(limestone_have) ~= "number" then
                limestone_have = 0
            end
            if limestone_have < 5 then
                t.exec("razmire.buyLimestone" .. trip_suffix, t.shop.buy, "limestonebrick", 5 - limestone_have)
            else
                t.note("trip" .. trip_suffix .. ": already holding " .. tostring(limestone_have)
                    .. " limestonebrick -- not bought")
            end

            local swamppaste_have_result, swamppaste_have = t.inv.count("swamppaste")
            if swamppaste_have_result ~= "ok" or type(swamppaste_have) ~= "number" then
                swamppaste_have = 0
            end
            local free_slots = 0
            for slot_index = 0, 27 do
                local slot_result, slot_cell = t.inv.slot(slot_index)
                if slot_result == "ok" and (slot_cell.name == "" or slot_cell.count == 0) then
                    free_slots = free_slots + 1
                end
            end
            -- Mirror the initial purchase's floor (razmire.buySwamppaste
            -- above, lines 432-437): the repair debugproc spends 5 swamp
            -- paste per repair action, so capping THIS trip's buy to
            -- whatever free_slots happens to be -- with no floor -- can
            -- leave fewer than 5 held total. REVERTED last_failure (sampler
            -- sonnet-b13): temple.repair-2/-4 called the debugproc sitting
            -- on <5 swamp paste and it silently repaired 0, and the old row
            -- here PASSed on the cheat's bare "ok" without reading that.
            local paste_target = 25 - swamppaste_have
            if paste_target > free_slots then
                paste_target = free_slots
            end
            local paste_floor = 5 - swamppaste_have
            if paste_floor > 0 and paste_target < paste_floor then
                paste_target = paste_floor
            end
            if paste_target < 0 then
                paste_target = 0
            end
            t.check("razmire.spaceForPaste" .. trip_suffix, free_slots > 0 or swamppaste_have >= 5,
                tostring(free_slots) .. " free slot(s), " .. tostring(swamppaste_have)
                    .. " swamppaste already held -- buying " .. tostring(paste_target)
                    .. " more (floor 5 total, cap 25 total)")
            if paste_target > 0 then
                t.exec("razmire.buySwamppaste" .. trip_suffix, t.shop.buy, "swamppaste", paste_target)
            else
                t.note("trip" .. trip_suffix .. ": already holding " .. tostring(swamppaste_have)
                    .. " swamppaste -- not bought")
            end
            local shop_close_result = t.shop.close()
            t.check("razmire.shopClose" .. trip_suffix, shop_close_result == "ok",
                "shop.close() -> " .. tostring(shop_close_result))

            -- t.cheat's own detail is hollow (nil) here too -- read the
            -- debugproc's real say() reply back and FAIL a call that
            -- repaired nothing (trap 12; same shape as temple.repair-1
            -- above).
            local repair_result, repair_detail = t.cheat("::mortton_repairtemple")
            local repair_msg_result, repair_msg = t.msg.expect("repair(s)")
            local repair_count = nil
            if repair_msg_result == "ok" then
                repair_count = tonumber(repair_msg:match("after (%d+) repair"))
            end
            t.check("temple.repair" .. trip_suffix,
                repair_result == "ok" and repair_msg_result == "ok" and repair_count ~= nil and repair_count > 0,
                "t.cheat(::mortton_repairtemple) -> " .. tostring(repair_result) .. " (" .. tostring(repair_detail)
                    .. "); reply: " .. tostring(repair_msg_result) .. " (" .. tostring(repair_msg)
                    .. "); repairs made: " .. tostring(repair_count))

            local stage_result, stage_value = t.quest.stage()
            if stage_result == "ok" and type(stage_value) == "number" then
                temple_stage_now = stage_value
            end
        end

        t.check("temple.wallsRepaired", temple_stage_now >= 60,
            "after " .. tostring(temple_trip) .. " materials trip(s) and ::mortton_repairtemple call(s), "
                .. "morttonquest reached " .. tostring(temple_stage_now)
                .. " (55=mortton_rebuild_temple, 60=mortton_can_light_altar)")

        if temple_stage_now < 60 then
            t.blocked("the temple wall repair (flamtaer_temple.rs2's ::mortton_repairtemple debugproc) did not "
                .. "reach %varp339_morttonquest=mortton_can_light_altar(60) after " .. tostring(temple_trip)
                .. " materials trip(s) (5 timberbeam/5 limestonebrick/up-to-25 swamppaste per trip, capped by "
                .. "Razmire's own restock and by backpack space) -- morttonquest last read "
                .. tostring(temple_stage_now) .. ". Razmire's builders' store restocks only 5 timberbeam/5 "
                .. "limestonebrick per visit, which bounds how much resource pool each trip can refill.")
            return
        end

        -- Ulsquire gives sacred olive oil once the temple is built
        -- (ulsquire_shauncy.rs2's ulsquire_temple_built branch, reached
        -- through questions_post_rebuild). His cure from earlier has almost
        -- certainly lapsed across the temple-repair wait -- same
        -- afflicted-check-and-recure idiom again.
        -- On foot: out of Razmire's shop through its door, in through Ulsquire's.
        enter_house("ulsquire-2", ULSQUIRE)
        -- ulsquire_temple_built's free-oil grant is gated on
        -- `inv_freespace(inv) != 0` -- the two shop trips carried real
        -- weight (run 6/7's own screenshots showed the pack near full), so
        -- guarantee at least one slot before the dialogue reaches that
        -- check, or it falls to the "buy it from Razmire instead" branch.
        -- The temple is built, so a leftover building material is what goes (the serum
        -- pairs are kept for a lapsed cure).
        local function count_free_slots()
            local free = 0
            for slot_index = 0, 27 do
                local slot_result_3, slot_cell_3 = t.inv.slot(slot_index)
                if slot_result_3 == "ok" and (slot_cell_3.name == "" or slot_cell_3.count == 0) then
                    free = free + 1
                end
            end
            return free
        end
        local free_slots_before_oil = count_free_slots()
        local oil_slot_dropped = "nothing"
        if free_slots_before_oil < 1 then
            for _, leftover in ipairs({ "swamppaste", "limestonebrick", "timberbeam" }) do
                local leftover_result, leftover_count = t.inv.count(leftover)
                if leftover_result == "ok" and type(leftover_count) == "number" and leftover_count > 0 then
                    t.player.drop(leftover)
                    t.ticks(1)
                    oil_slot_dropped = leftover
                    break
                end
            end
        end
        local free_slots_for_oil = count_free_slots()
        t.check("ulsquire.oilSlotFree", free_slots_for_oil >= 1,
            tostring(free_slots_before_oil) .. " free slot(s), dropped " .. oil_slot_dropped .. " -> "
                .. tostring(free_slots_for_oil) .. " free (ulsquire_temple_built needs inv_freespace != 0)")
        talk_or_recure("ulsquire.askOil", "mixSerum207-oil", "ulsquire.recure-oil",
            "ulsquire_shauncy", "ulsquire_shauncy_afflicted")
        -- ulsquire_talk's universal opener (bit already true) is "Ah, hello
        -- again, what can I do for you now?", THEN the PLAYER line "Hello
        -- there, I've started repairing the temple." (not an npc line --
        -- run 7's own ulsquire.askOil-dialogue never ran this far, but the
        -- razmire.recure-2-dialogue FAIL at the same universal-opener shape
        -- caught the same mistake there). ulsquire_temple_built loops back
        -- to @ulsquire_questions_post_rebuild's own menu after the mesbox,
        -- so a second "Ok, thanks." is needed to close out.
        t.exec("ulsquire.askOil-dialogue", t.chat.play, {
            "npc:hello again",
            "player:I've started repairing the temple",
            "npc:carry on the good work",
            "choose:What should I do when the temple is built?",
            "player:What should I do when the temple is built?",
            "npc:it seems that the temple was",
            "npc:The pagans believed that sacred oil sanctified",
            "mesbox:Ulsquire gives you some olive oil",
            "player:Thanks!",
            "choose:Ok, thanks.",
            "player:Ok, thanks",
        })
        t.expect("haveOliveOil", t.inv.expect_has("oliveoil3", 1))

        -- Light the fire altar (flamtaer_temple.rs2's
        -- [oploc1,templefire_altar_nofire]) -- crafting/tinderbox/sanctity
        -- are already satisfied from the repair above; another
        -- self-re-arming roll (p_oploc(3) again), poll instead of re-press.
        -- The debugproc-driven repair loop above never walks the player to
        -- the temple at all (it needs no click), so run 3's altarClick
        -- answered "covered ... the world is not picking" while standing at
        -- Ulsquire's tile, 3496,3289 -- go back to the temple first: out of
        -- Ulsquire's house through its door, then the hop to the open ground
        -- south of the temple (3506,3311). The temple ring (templewall locs,
        -- x 3504-3508 z 3314-3318, maps/m54_51.jl2) has an open gap at
        -- 3506,3314 with no door loc, and the altar sits in the middle at
        -- 3506,3316 -- the click walks the player in through the gap.
        leave_house("ulsquire-temple")
        t.exec("goto-temple-altar", t.player.goto_tile, 3506, 3311, 0)
        local altar_click_result, altar_click_detail = t.player.click_loc("templefire_altar_nofire", 1)
        t.check("temple.altarClick", altar_click_result == "ok",
            "click_loc(templefire_altar_nofire, op1) -> " .. tostring(altar_click_result) .. " "
                .. tostring(altar_click_detail))
        -- flamtaer_temple.rs2:284 loc_change(templefire_altar, 99): the lit altar is a real loc
        -- on the altar's tile for 99 ticks -- poll the scene for it, not a symbol resolve.
        local altar_lit = false
        local altar_rounds = 0
        local altar_near_result, altar_near = nil, nil
        while not altar_lit and altar_rounds < 10 do
            altar_rounds = altar_rounds + 1
            t.ticks(5)
            altar_near_result, altar_near = t.world.loc_near("templefire_altar", 6)
            if altar_near_result == "ok" and altar_near.tile_x == 3506 and altar_near.tile_z == 3316 then
                altar_lit = true
            end
        end
        t.check("temple.altarLit", altar_lit,
            "polled " .. tostring(altar_rounds) .. " round(s) of 5 ticks: loc templefire_altar (lit) "
                .. loc_text(altar_near_result, altar_near) .. " (want 3506,3316)")

        -- Sanctity (%varp6753_temple_sanctity, 3000 = 100%) is what the flame spends:
        -- every repair action added its resource amount (flamtaer_temple.rs2:137-139),
        -- [timer,sanctity_drain] takes 30 per 100 ticks (:164-168). The guide's
        -- repairTo20Sanctity: Serum 208 needs >= 600 (20%) before the flame takes a
        -- Serum 207 (:438-441), and the oil costs 81 first (:413-422).
        local function read_sanctity()
            local sr, sv = t.var.server("varp6753_temple_sanctity")
            if sr == "ok" and type(sv) == "number" then
                return sv
            end
            return nil
        end
        local function read_runtime_bit(bit)
            local rr, rv = t.var.server("varp6752_mortton_runtime")
            if rr == "ok" and type(rv) == "number" then
                return math.floor(rv / (2 ^ bit)) % 2, rv
            end
            return nil, tostring(rr) .. " " .. tostring(rv)
        end
        local function count_of(item)
            local cr, cv = t.inv.count(item)
            if cr == "ok" and type(cv) == "number" then
                return cv
            end
            return nil
        end
        local sanctity_at_light = read_sanctity()
        t.check("repairTo20Sanctity", sanctity_at_light ~= nil and sanctity_at_light >= 600 + 81,
            "varp6753_temple_sanctity = " .. tostring(sanctity_at_light) .. " after the repairs (want >= 681: "
                .. "600 for Serum 208 (flamtaer_temple.rs2:438) plus the oil's 81 spent first)")

        -- Sanctify the olive oil in the lit flame (oc_category 110,
        -- oplocu,templefire_altar) -> sacred_oil3, %morttonquest =
        -- mortton_created_sacred_oil.
        local altar_target_2 = t.player.by_symbol("loc", "templefire_altar")
        t.exec("temple.sanctifyOil", t.player.use_on, "oliveoil3", altar_target_2)
        t.exec("quest.stage.mortton_created_sacred_oil", t.quest.expect_stage, "mortton_created_sacred_oil")

        -- use207OnFlame: a Serum 207 used on the lit flame becomes Serum 208
        -- (flamtaer_temple.rs2:432-466, oc_category 108): sanctity >= 600 first, then
        -- 150 sanctity per dose spent, the vial set to next_obj_stage_temple
        -- (mort_serum3 -> mort_serum_perm3, quest_mortton.obj:18-21), bit
        -- player_made_perm_serum (7) of %varp6752_mortton_runtime set, and a mesbox the
        -- first time. A FRESH 3-dose mix from the reserve pair (the guide's addAshes
        -- again): three doses of Serum 208 -- Razmire, Ulsquire, and one left in hand for
        -- Ulsquire's "already used" answer (ulsquire_shauncy.rs2:22-26 consumes nothing).
        local serum3_before_mix = count_of("mort_serum3")
        t.exec("mixSerum207-flame", t.player.use_item_on_item, "ashes", "tarrominvial")
        t.ticks(1)
        local serum3_before = count_of("mort_serum3")
        local perm3_before = count_of("mort_serum_perm3")
        local sanctity_before_208 = read_sanctity()
        t.check("mixSerum207-flame.fresh207",
            serum3_before_mix ~= nil and serum3_before == serum3_before_mix + 1 and perm3_before == 0,
            "mort_serum3 " .. tostring(serum3_before_mix) .. " -> " .. tostring(serum3_before)
                .. " (want +1, a fresh 3-dose Serum 207); mort_serum_perm3 held " .. tostring(perm3_before)
                .. " (want 0 before the flame); sanctity now " .. tostring(sanctity_before_208))
        local altar_target_3 = t.player.by_symbol("loc", "templefire_altar")
        t.exec("use207OnFlame", t.player.use_on, "mort_serum3", altar_target_3)
        t.exec("use207OnFlame-dialogue", t.chat.play, {
            "mesbox:You sanctify Serum 207 in the sacred flame and turn it into Serum 208.",
        })
        t.ticks(1)
        local serum3_after = count_of("mort_serum3")
        local perm3_after = count_of("mort_serum_perm3")
        local sanctity_after_208 = read_sanctity()
        local made_bit, made_raw = read_runtime_bit(7)
        local sanctity_spent = nil
        if sanctity_before_208 ~= nil and sanctity_after_208 ~= nil then
            sanctity_spent = sanctity_before_208 - sanctity_after_208
        end
        t.check("use207OnFlame.serum208",
            serum3_before ~= nil and serum3_after == serum3_before - 1 and perm3_after == 1
                and (sanctity_spent == 450 or sanctity_spent == 480) and made_bit == 1,
            "USED mort_serum3 " .. tostring(serum3_before) .. " -> " .. tostring(serum3_after)
                .. "; RESULT mort_serum_perm3 (Serum 208, 3 doses) " .. tostring(perm3_before) .. " -> "
                .. tostring(perm3_after) .. "; sanctity " .. tostring(sanctity_before_208) .. " -> "
                .. tostring(sanctity_after_208) .. " (spent " .. tostring(sanctity_spent)
                .. ", want 450 = 150 x 3 doses, or 480 if a drain tick of 30 fell between)"
                .. "; mortton_runtime " .. tostring(made_raw) .. " bit 7 player_made_perm_serum = "
                .. tostring(made_bit))

        -- Sacred oil on the logs setup carried (mortton_pyre.rs2's
        -- [opheldu,_sacred_oil], oc_category(logs)=22) -> logs_pyre,
        -- %morttonquest = mortton_created_pyre_logs. pyre_logs needs 2
        -- doses and sacred_oil3 has 3 (quest_mortton.obj), so one use is
        -- enough.
        -- mortton_pyre.rs2's [opheldu,_sacred_oil] used to check
        -- oc_category(last_item) on both branches, and last_item is ALWAYS
        -- the sacred oil itself for every dispatch rung that can reach this
        -- handler (torirs_server_scripts.c's opheldu_orient invariant:
        -- "last_item is the item the script is bound to, last_useitem is
        -- the other one") -- so the pyre-logs branch was unreachable and
        -- this row reproduced it live ("Nothing interesting happens.", the
        -- prior REVERTED attempt's own finding, reported as a content bug
        -- against mortton_pyre.rs2:2,6,11). OSRS-Content has since fixed it
        -- (read fresh this session: lines 7 and 11 now read
        -- oc_category(last_useitem), with a comment there naming the same
        -- invariant) -- assert the real, now-reachable behaviour instead of
        -- re-reporting the old bug (trap: "Rows that ASSERT THE OLD BUG are
        -- now FAIL and must be rewritten to assert the fixed behaviour").
        t.exec("temple.makePyreLogs", t.player.use_item_on_item, "sacred_oil3", "logs")
        t.expect("havePyreLogs", t.inv.expect_has("logs_pyre", 1))
        t.exec("quest.stage.mortton_created_pyre_logs", t.quest.expect_stage, "mortton_created_pyre_logs")

        -- Build the funeral pyre (mortton_pyre.rs2's [oploc1,temple_pyre] /
        -- [oploc1,_pyre_loaded] / [oploc1,_pyre_remains_loaded]): op1 on the
        -- empty pyre uses whatever pyre logs are carried
        -- (~mortton_best_pyre_logs, only logs_pyre held here), op1 again on
        -- the loaded pyre adds the best shade remains carried
        -- (~mortton_best_pyre_remains) -- two shade_bones1 remain after the
        -- Razmire (2) and Ulsquire (1) hand-ins above, out of the five the
        -- shade hunt dropped. The funeral pyres sit on the shade street,
        -- not at the temple altar -- nearest copy of loc 4093
        -- (all.loc.compack) to the player decoded from maps/m54_50.jl2
        -- (trap 29): level 0, local 51,12 in map square 54,50 -> 3507,3276.
        --
        -- The four pyres stand as a cross around 3507,3275 (each 1x3: 3504-3506,3275 /
        -- 3507,3272-3274 / 3507,3276-3278 / 3508-3510,3275), so that centre tile is a pocket
        -- no walk reaches. The player walks out of the temple ring through its gap, hops to the
        -- open ground north-west of the cross (3505,3278), and every pyre click names the copy
        -- at 3507,3276, approached from its open west side.
        t.player.walk_to(3506, 3311)
        local temple_out_result, temple_out_tile = t.world.tile()
        t.check("temple.walkOut",
            temple_out_result == "ok" and temple_out_tile.level == 0 and temple_out_tile.z <= 3313
                and math.abs(temple_out_tile.x - 3506) <= 1,
            "walked out of the temple ring through its gap (3506,3314) to 3506,3311 -> "
                .. tile_text(temple_out_result, temple_out_tile) .. " (want z <= 3313, outside the ring)")
        t.exec("goto-pyre", t.player.goto_tile, 3505, 3278, 0)
        local pyre_near_result, pyre_near_detail = t.world.loc_near("temple_pyre", 6)
        t.check("temple.pyreFind", pyre_near_result == "ok",
            "world.loc_near(temple_pyre, 6) -> " .. loc_text(pyre_near_result, pyre_near_detail))

        t.exec("temple.pyreAddLogs", t.player.click_loc, "temple_pyre", 1, { at = { 3507, 3276 } })
        t.exec("quest.stage.mortton_logs_on_pyre", t.quest.expect_stage, "mortton_logs_on_pyre")
        -- Section 8: "give a loc_change 2-3 ticks before a second
        -- click_loc can see the new form."
        t.ticks(2)
        local pyre_logs_near_result, pyre_logs_near_detail = t.world.loc_near("temple_pyre_logs", 5)
        t.check("temple.pyreLogsPlaced",
            pyre_logs_near_result == "ok" and pyre_logs_near_detail.tile_x == 3507 and pyre_logs_near_detail.tile_z == 3276,
            "world.loc_near(temple_pyre_logs, 5) -> " .. loc_text(pyre_logs_near_result, pyre_logs_near_detail)
                .. " (want 3507,3276)")

        local shade_bones_before_result, shade_bones_before = t.inv.count("shade_bones1")
        t.check("temple.shadeBonesBeforePyre",
            shade_bones_before_result == "ok" and type(shade_bones_before) == "number" and shade_bones_before >= 1,
            "inv.count(shade_bones1) -> " .. tostring(shade_bones_before_result) .. " ("
                .. tostring(shade_bones_before) .. "; want >= 1: five dropped, two to Razmire, one to Ulsquire)")
        t.exec("temple.pyreAddRemains", t.player.click_loc, "temple_pyre_logs", 1, { at = { 3507, 3276 } })
        -- Trap 24: a click verb's `ok` is the server's sentence, one tick
        -- before the container update reaches the client -- a bare
        -- t.inv.count on the line right below read 2 -> 2 (run 2's own
        -- FAIL) though add_remains_to_funeral_pyre's inv_del(inv,
        -- $remains, 1) already ran. Spend the tick first.
        t.ticks(1)
        local shade_bones_after_result, shade_bones_after = t.inv.count("shade_bones1")
        t.check("temple.shadeBonesAfterPyre",
            shade_bones_after_result == "ok" and type(shade_bones_before) == "number"
                and type(shade_bones_after) == "number" and shade_bones_after == shade_bones_before - 1,
            "inv.count(shade_bones1): " .. tostring(shade_bones_before) .. " -> " .. tostring(shade_bones_after)
                .. " (add_remains_to_funeral_pyre's own inv_del)")
        t.ticks(2)
        local pyre_bones_near_result, pyre_bones_near_detail = t.world.loc_near("temple_pyre_bones_logs", 5)
        t.check("temple.pyreRemainsPlaced",
            pyre_bones_near_result == "ok" and pyre_bones_near_detail.tile_x == 3507
                and pyre_bones_near_detail.tile_z == 3276,
            "world.loc_near(temple_pyre_bones_logs, 5) -> " .. loc_text(pyre_bones_near_result, pyre_bones_near_detail)
                .. " (want 3507,3276)")

        -- Light it ([oploc1,_pyre_remains_loaded] -> label light_funeral_pyre ->
        -- [oploc4,_pyre_remains_loaded], mortton_pyre.rs2:228-282): "You attempt to light the
        -- funeral pyre." now, the stat_random roll 3 ticks later. Only the SUCCESS branch
        -- sets mortton_lit_pyre, plays temple_pyre_fire on the pyre, advances firemaking 505
        -- (tenths; flamtaer_pyre.struct pyre_logs) and prayer 250 (shade_bones1
        -- pyre_shade_exp, +0 bonus), and npc_adds the shade's spirit (shade_heaven) beside the
        -- pyre -- which funeral_pyre_reward deletes 2 ticks later, when the pyre reverts to an
        -- empty temple_pyre. The fire and the spirit are on screen together for about a
        -- second, so the click is NOT a shot row (its settle would eat the window): the
        -- spirit wait right behind it is, and shoots the frame the spirit stands in.
        local pyre_snapshot_result, pyre_snapshot = t.skill.snapshot()
        local pyre_light_result, pyre_light_detail = t.player.click_loc("temple_pyre_bones_logs", 1, { at = { 3507, 3276 } })
        t.exec("temple.pyreLit", t.npc.await_present, "shade_heaven", 8, 60)
        t.check("temple.pyreLight", pyre_light_result == "ok",
            "click_loc(temple_pyre_bones_logs, op1 Light, at 3507,3276) -> " .. tostring(pyre_light_result) .. " "
                .. tostring(pyre_light_detail))
        t.ticks(1)
        t.exec("quest.stage.mortton_lit_pyre", t.quest.expect_stage, "mortton_lit_pyre")
        t.exec("temple.pyreLit.prayerXp", t.skill.expect_gain, "prayer", 25, pyre_snapshot)
        local firemaking_after_result, firemaking_after = t.skill.read("firemaking")
        local firemaking_delta = nil
        if pyre_snapshot_result == "ok" and type(pyre_snapshot) == "table" and type(pyre_snapshot.firemaking) == "table"
            and firemaking_after_result == "ok" and type(firemaking_after) == "table" then
            firemaking_delta = firemaking_after.experience - pyre_snapshot.firemaking.experience
        end
        t.check("temple.pyreLit.firemakingXp", firemaking_delta == 50 or firemaking_delta == 51,
            "firemaking whole-xp delta " .. tostring(firemaking_delta)
                .. " (want 50.5 xp = stat_advance(firemaking, 505), read as 50 or 51 in whole units)")
        -- funeral_pyre_reward (queued 2 ticks after the light) turns the burnt pyre back into an
        -- empty temple_pyre and deletes the spirit: the remains are gone from the world.
        local pyre_burnt = false
        local pyre_burnt_rounds = 0
        local burnt_bones_result, burnt_bones = nil, nil
        while not pyre_burnt and pyre_burnt_rounds < 10 do
            pyre_burnt_rounds = pyre_burnt_rounds + 1
            t.ticks(1)
            burnt_bones_result, burnt_bones = t.world.loc_near("temple_pyre_bones_logs", 6)
            pyre_burnt = burnt_bones_result ~= "ok"
        end
        local empty_pyre_result, empty_pyre = t.world.loc_near("temple_pyre", 3)
        t.check("temple.pyreBurnedOut",
            pyre_burnt and empty_pyre_result == "ok" and empty_pyre.tile_x == 3507 and empty_pyre.tile_z == 3276,
            "after " .. tostring(pyre_burnt_rounds) .. " tick(s): temple_pyre_bones_logs "
                .. loc_text(burnt_bones_result, burnt_bones) .. "; temple_pyre " .. loc_text(empty_pyre_result, empty_pyre)
                .. " (want the loaded pyre gone and an empty temple_pyre back at 3507,3276)")

        -- The reward queue (funeral_pyre_reward, 2-tick delay) drops loot
        -- and clears %pyre_loc -- settle before talking to Ulsquire.
        t.ticks(3)

        -- Reward snapshot before the hand-in (section 7's rule).
        local _, skill_snapshot = t.skill.snapshot()

        -- Serum 208 is used on whichever form is spawned: [opnpcu] binds both the cured
        -- and the afflicted symbol to the same label (razmire_keelgan.rs2:24-28,
        -- ulsquire_shauncy.rs2:1-5), so read the live pool for the target.
        local function live_form(cured_sym, afflicted_sym)
            local ar, arow = t.npc.nearest(afflicted_sym, 8)
            if ar == "ok" then
                t.note(afflicted_sym .. " is the live form (slot " .. tostring(arow.slot) .. ")")
                return t.player.by_symbol("npc", afflicted_sym)
            end
            t.note(afflicted_sym .. ": " .. tostring(ar) .. " -- " .. cured_sym .. " is the live form")
            return t.player.by_symbol("npc", cured_sym)
        end

        -- use208OnRazmire (razmire_keelgan.rs2:49-63, oc_category 109): "Well done - I
        -- feel this serum is really working on me", bit razmire_perm_serum_used (6) set,
        -- the vial one dose lighter (mort_serum_perm3 -> perm2), 180-220 coins
        -- (~random_range) with a mesbox, then @razmire_talk: at mortton_lit_pyre the
        -- shade-lair page (:122-128) and the store menu, closed with "Ok, thanks.".
        -- On foot from the pyres to his door and in.
        enter_house("razmire-208", RAZMIRE)
        local razmire_208_target = live_form("razmire_keelgan", "razmire_keelgan_afflicted")
        local razmire_coins_before = count_of("coins")
        local razmire_perm_bit_before = read_runtime_bit(6)
        t.exec("use208OnRazmire", t.player.use_on, "mort_serum_perm3", razmire_208_target)
        t.exec("use208OnRazmire-dialogue", t.chat.play, {
            "npc:Well done - I feel this serum is really working on me.",
            "mesbox:Razmire gives you",
            "npc:it's you",
            "player:Hey, I've laid a Shade to rest",
            "npc:Yeah, great job on putting that Shade to rest.",
            "player:Can you open a store for me?",
            "npc:which store",
            "choose:Ok, thanks.",
            "player:Ok, thanks",
        })
        t.ticks(1)
        local razmire_coins_after = count_of("coins")
        local razmire_coins_gain = nil
        if razmire_coins_before ~= nil and razmire_coins_after ~= nil then
            razmire_coins_gain = razmire_coins_after - razmire_coins_before
        end
        local razmire_perm_bit, razmire_runtime = read_runtime_bit(6)
        local perm3_after_razmire = count_of("mort_serum_perm3")
        local perm2_after_razmire = count_of("mort_serum_perm2")
        t.check("use208OnRazmire.cured",
            razmire_perm_bit_before == 0 and razmire_perm_bit == 1 and perm3_after_razmire == 0
                and perm2_after_razmire == 1 and razmire_coins_gain ~= nil and razmire_coins_gain >= 180
                and razmire_coins_gain <= 220,
            "USED mort_serum_perm3 1 -> " .. tostring(perm3_after_razmire) .. ", mort_serum_perm2 -> "
                .. tostring(perm2_after_razmire) .. "; mortton_runtime " .. tostring(razmire_runtime)
                .. " bit 6 razmire_perm_serum_used " .. tostring(razmire_perm_bit_before) .. " -> "
                .. tostring(razmire_perm_bit) .. "; coins " .. tostring(razmire_coins_before) .. " -> "
                .. tostring(razmire_coins_after) .. " (+" .. tostring(razmire_coins_gain) .. ", want 180-220)")

        -- use208OnUlsquire (ulsquire_shauncy.rs2:22-38): the same page, bit
        -- ulsquire_perm_serum_used (5), perm2 -> perm1, 180-220 coins -- then
        -- @ulsquire_talk, whose mortton_lit_pyre branch (:105-110) is the guide's
        -- talkToUlsquireToFinish: "I've put the Shade's spirit to rest!" -> "Great! Well
        -- done my friend!" -> queue(mortton_quest_complete). The use's own dialogue is the
        -- finishing talk (the content routes every ulsquire op through that label), so it
        -- is played under the guide's name. Out of Razmire's shop and in through
        -- Ulsquire's door.
        enter_house("ulsquire-208", ULSQUIRE)
        local ulsquire_208_target = live_form("ulsquire_shauncy", "ulsquire_shauncy_afflicted")
        local ulsquire_coins_before = count_of("coins")
        local ulsquire_perm_bit_before = read_runtime_bit(5)
        t.exec("use208OnUlsquire", t.player.use_on, "mort_serum_perm2", ulsquire_208_target)
        t.exec("talkToUlsquireToFinish", t.chat.play, {
            "npc:Well done - I feel this serum is really working on me.",
            "mesbox:Ulsquire gives you",
            "npc:hello again",
            "player:I've put the Shade's spirit to rest",
            "npc:Great! Well done my friend!",
        })
        t.ticks(1)
        local ulsquire_coins_after = count_of("coins")
        local ulsquire_coins_gain = nil
        if ulsquire_coins_before ~= nil and ulsquire_coins_after ~= nil then
            ulsquire_coins_gain = ulsquire_coins_after - ulsquire_coins_before
        end
        local ulsquire_perm_bit, ulsquire_runtime = read_runtime_bit(5)
        local perm2_after_ulsquire = count_of("mort_serum_perm2")
        local perm1_after_ulsquire = count_of("mort_serum_perm1")
        t.check("use208OnUlsquire.cured",
            ulsquire_perm_bit_before == 0 and ulsquire_perm_bit == 1 and perm2_after_ulsquire == 0
                and perm1_after_ulsquire == 1 and ulsquire_coins_gain ~= nil and ulsquire_coins_gain >= 180
                and ulsquire_coins_gain <= 220,
            "USED mort_serum_perm2 1 -> " .. tostring(perm2_after_ulsquire) .. ", mort_serum_perm1 -> "
                .. tostring(perm1_after_ulsquire) .. "; mortton_runtime " .. tostring(ulsquire_runtime)
                .. " bit 5 ulsquire_perm_serum_used " .. tostring(ulsquire_perm_bit_before) .. " -> "
                .. tostring(ulsquire_perm_bit) .. "; coins " .. tostring(ulsquire_coins_before) .. " -> "
                .. tostring(ulsquire_coins_after) .. " (+" .. tostring(ulsquire_coins_gain) .. ", want 180-220)")
        -- Completion is asynchronous (section 8): the queue(0,0) call fires
        -- behind this dialogue's own close.
        t.ticks(3)
        t.quest.expect_complete()

        -- A second Serum 208 on Ulsquire: bit 5 is set, so "You've already used the
        -- permanent serum on me" and `return` (ulsquire_shauncy.rs2:24-26) -- no dose
        -- spent, no coins.
        local again_target = live_form("ulsquire_shauncy", "ulsquire_shauncy_afflicted")
        local again_coins_before = count_of("coins")
        t.exec("use208OnUlsquire.again", t.player.use_on, "mort_serum_perm1", again_target)
        t.exec("use208OnUlsquire.again-dialogue", t.chat.play, {
            "npc:You've already used the permanent serum on me",
        })
        t.ticks(1)
        local again_perm1 = count_of("mort_serum_perm1")
        local again_coins_after = count_of("coins")
        t.check("use208OnUlsquire.againNothingSpent",
            again_perm1 == 1 and again_coins_before ~= nil and again_coins_after == again_coins_before,
            "mort_serum_perm1 held " .. tostring(again_perm1) .. " (want 1: the refusal returns before"
                .. " inv_setslot); coins " .. tostring(again_coins_before) .. " -> " .. tostring(again_coins_after)
                .. " (want unchanged)")

        -- Every reward quest_mortton.rs2's own ~quest_complete_rewards
        -- lists: "2000 Herblore XP|2000 Crafting XP|Access to the Shade
        -- Catacombs" (stat_advance(..., 20000) -- the internal *10 unit
        -- t.skill.expect_gain already accepts and names).
        t.exec("reward.herbloreXp", t.skill.expect_gain, "herblore", 2000, skill_snapshot)
        t.exec("reward.craftingXp", t.skill.expect_gain, "crafting", 2000, skill_snapshot)

        t.finish(0)
        return
    end,
}
