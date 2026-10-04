-- Sea Slug (1 QP). Content:
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_seaslug/
-- OSRS-Content/osrs239-content/server/scripts/areas/ardougne_east/scripts/caroline.rs2
-- OSRS-Content/osrs239-content/server/scripts/areas/area_fishing_platform/scripts/
--     holgart.rs2, kennith.rs2, kent.rs2, bailey.rs2
-- OSRS-Content/osrs239-content/server/scripts/skill_cooking/scripts/dough.rs2
--
-- RETRY after b44ce7a2d (queue.py show seaslug): both prior blockers are
-- gone. The cook fix (033d83f61f) added skill_cooking/configs/
-- cooking_generic.dbrow's cooking_generic_swamp_paste row (uncooked=
-- rawswamppaste, cooked=swamppaste, fire only, always succeeds), so this
-- file now asserts the FIXED cook instead of "You can't cook that.", and the
-- platform Holgart (holgart.rs2's [label,holgartplatform_talk], reached
-- through the base spawn symbol slug2_holgart_jeb per trap 19's fix)
-- answers. The committed retry staged ::setlevel firemaking 50 with five
-- logs (one log at Firemaking 1 timed out the light roll on a full run).
--
-- RE-DRIVEN b59 (door rule, owner 2026-10-03): the Fishing Platform is an
-- island reached only by Holgart's boat and every room on it is behind a
-- door, so nothing on it is a goto_tile any more. The ladder is climbed by
-- its own op both ways, Kennith is talked to from inside his cabin through
-- slug2_village_poordoor 2767,3285 (level 1), Bailey and the broken glass
-- are in the room behind 2768,3276, the panel is kicked and the crane turned
-- from the deck tile 2769,3289 (no drive.op), and Kent's island and the
-- Witchaven jetty are walked. The only gotos left are the overland hops
-- between open tiles on the mainland.
--
-- BLOCKED (content_bug) at talkToKennith: walked into the cabin, the talk
-- answers "I can't reach that!" -- Kennith is two tiles back behind crates
-- and kennith.rs2 has no approach-range trigger (talk_kennith below names
-- the fix). The whole route after it was proven with a throwaway probe in
-- b59 fixer run 3 (134 PASS; build/orchestrator/fix_b59/seaslug.run3.ledger.tsv).
--
-- Chain driven for real, nothing cheated (trap 16): gather swamp tar (m49_49
-- ground spawns) -> mix with a bought pot of flour -> heat on a real fire ->
-- give Holgart the swamp paste -> sail to the Fishing Platform -> find
-- Kennith -> sail to the island and free Kent (who pulls a sea slug off the
-- player) -> sail back -> Bailey hands over an unlit torch -> gather damp
-- sticks + broken glass on the platform, dry the sticks with the glass, rub
-- them alight (needs the Firemaking level staged in setup) -> Kennith won't
-- go near the slugs even with a lit torch, so kick the loose panel open ->
-- Kennith needs a way down, so work the crane -> sail back to shore ->
-- Caroline's thanks (Oyster pearls + Fishing xp), driven end to end.
--
-- Prerequisites given in setup, none of them the quest's own deliverable:
-- a pot of flour (a bought good), a tinderbox and several logs (the generic
-- Firemaking tools the journal's own "heating the mixture on a Fire" step
-- and the torch-lighting step both need), and the Firemaking level itself
-- (raised, never the fire lit for us -- every fire/torch light below is
-- still a real click).
return {
    id = "seaslug",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel firemaking 50",
        "::give pot_flour 1",
        "::give tinderbox 1",
        "::give logs 5",
    },

    run = function(t)
        t.quest.bind({
            varp = "varp159_seaslugquest",
            constants = {
                not_started = 0,
                started = 1,
                spoken_holgart = 2,
                boat_repaired = 3,
                spoken_kennith = 4,
                sailed_kent = 5,
                spoken_kent = 6,
                lit_torch = 7,
                kennith_need_escape = 8,
                panel_opened = 9,
                need_kennith_path = 10,
                saved_kennith = 11,
                complete = 12,
            },
            row = "quest_seaslug", -- all.dbrow.compack:128
            display = "Sea Slug",
            points = 1,
        })
        t.ticks(3) -- setup's ::give/::setlevel cheats are not client-side yet
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ---------------------------------------------------- accept, Caroline
        -- caroline.rs2 [opnpc1,caroline] at %seaslugquest=not_started jumps
        -- straight to [label,caroline_help] (no separate accept branch), which
        -- opens with the PLAYER's own line (trap 18) -- drain walks the whole
        -- alternating player/npc run up to the p_choice2 options page without
        -- needing each line spelled out.
        t.exec("caroline.goto", t.player.goto_tile, 2716, 3302, 0)
        t.exec("caroline.greet", t.player.talk_to, "caroline")
        local drain1_result, drain1_detail = t.chat.drain({ stop_at = "options" })
        t.expect("caroline.drain_to_choice", drain1_result, drain1_detail)
        t.shot("caroline-choice-menu")

        t.exec("caroline.choose_help", t.chat.choose, "I suppose so, how do I get there?")
        local drain2_result, drain2_detail = t.chat.drain({ stop_at = "none" })
        t.expect("caroline.drain_close", drain2_result, drain2_detail)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- ---------------------------------------------------- gather swamp tar
        -- Swamp tar ground spawns south of Lumbridge (areas/world/configs/
        -- m49_49.spawn:71-87), NOT the m53_5x squares (those are Mort'ton's
        -- ghast swamp -- same obj symbol reused on a different map square,
        -- confirmed by the ghast_invis/willothewisp/mmsnail neighbours there).
        t.exec("swamp.goto", t.player.goto_tile, 3183, 3180, 0)
        local tar_before_result, tar_before_count = t.inv.count("swamp_tar")
        -- click_obj answers ok with a nil detail (trap 12's hollow rule) --
        -- call it directly and write the before/after count ourselves.
        local tar_pickup_result, tar_pickup_detail = t.player.click_obj("swamp_tar")
        local tar_after_result, tar_after_detail = t.inv.await("swamp_tar", 1, 10)
        local tar_read, tar_count = t.inv.count("swamp_tar")
        t.check("seaslug.gather_swamp_tar",
            tar_after_result == "ok" and tar_count >= 1,
            string.format(
                "click_obj(swamp_tar) -> %s (%s); before=%s after=%s(%s), await=%s %s",
                tostring(tar_pickup_result), tostring(tar_pickup_detail),
                tostring(tar_before_count), tostring(tar_read), tostring(tar_count),
                tostring(tar_after_result), tostring(tar_after_detail)))

        -- --------------------------------------------- mix flour + swamp tar
        -- dough.rs2's [opheldu,swamp_tar]/[opheldu,pot_flour] make_swamp_paste
        -- (dough.rs2:96-102): consumes both, hands back an empty pot and
        -- `rawswamppaste` -- the intermediate item, not the deliverable
        -- Holgart actually wants (heated on a fire, below).
        t.exec("seaslug.mix_swamp_paste", t.player.use_item_on_item, "swamp_tar", "pot_flour")
        local raw_read, raw_count = t.inv.count("rawswamppaste")
        t.check("seaslug.have_rawswamppaste",
            raw_read == "ok" and raw_count >= 1,
            "inv.count(rawswamppaste) -> " .. tostring(raw_read) .. " " .. tostring(raw_count)
                .. " -- dough.rs2:102 inv_add(inv, rawswamppaste, 1)")

        -- ---------------------------------------------------------- light a fire
        -- Back on open ground (the fixture's own start tile, beside Hans) --
        -- firemaking.rs2's [opheldu,tinderbox] with a held log arms tinderbox
        -- (item_a) and clicks the inventory log (item_b) -> @light_logs_inv,
        -- which drops the log at the player's own coord and, on a successful
        -- roll, loc_adds a `fire` (firemaking.rs2:139) a few ticks later.
        -- Firemaking 50 (staged in setup) plus five logs: retry the light up
        -- to three times against a missed roll rather than pin the run to one
        -- attempt, and record the OUTCOME once, not one row per try (trap 15).
        t.exec("fire.goto", t.player.goto_tile, 3206, 3233, 0)
        local fire_lit = false
        local fire_attempts = 0
        local fire_last_detail = nil
        while not fire_lit and fire_attempts < 3 do
            fire_attempts = fire_attempts + 1
            local logs_read, logs_count = t.inv.count("logs")
            if logs_read ~= "ok" or (logs_count or 0) < 1 then
                fire_last_detail = "no logs left (" .. tostring(logs_read) .. " " .. tostring(logs_count) .. ")"
                break
            end
            local light_result, light_detail = t.player.use_item_on_item("tinderbox", "logs")
            local msg_result, msg_detail = t.msg.await("The fire catches", 15)
            fire_last_detail = "attempt " .. fire_attempts .. ": use_item_on_item -> "
                .. tostring(light_result) .. " (" .. tostring(light_detail) .. "); msg.await -> "
                .. tostring(msg_result) .. " " .. tostring(msg_detail)
            if msg_result == "ok" then
                fire_lit = true
            end
        end
        t.step("seaslug.fire_lit", fire_lit and "PASS" or "FAIL", fire_last_detail)

        local fire_target, fire_sym_result = t.player.by_symbol("loc", "fire")
        t.step("seaslug.find_fire_loc",
            fire_sym_result == "ok" and "PASS" or "FAIL",
            "by_symbol(loc, fire) -> " .. tostring(fire_sym_result))

        -- ----------------------------------------- heat rawswamppaste: FIXED
        -- 033d83f61f added skill_cooking/configs/cooking_generic.dbrow's
        -- cooking_generic_swamp_paste row (rawswamppaste -> swamppaste, fire
        -- only, levelrequired=1, successchance always-passes idiom), so
        -- use_on now settles on the BACKPACK DIFF (trap 24: use_on is the one
        -- click verb that waits for it) rather than a "can't cook" mesbox.
        local heat_result, heat_detail = t.player.use_on("rawswamppaste", fire_target)
        t.step("seaslug.cook_swamppaste",
            heat_result == "ok" and "PASS" or "FAIL",
            "use_on(rawswamppaste, fire) -> " .. tostring(heat_result)
                .. " (" .. tostring(heat_detail) .. ")")

        local paste_after_result, paste_after_count = t.inv.await("swamppaste", 1, 10)
        local raw_after_read, raw_after_count = t.inv.count("rawswamppaste")
        local paste_read, paste_count = t.inv.count("swamppaste")
        t.check("seaslug.have_swamppaste",
            paste_after_result == "ok" and paste_count >= 1 and raw_after_count == 0,
            string.format(
                "inv.await(swamppaste, 1, 10) -> %s; rawswamppaste %s->%s(%s), swamppaste=%s(%s) -- " ..
                "skill_cooking/configs/cooking_generic.dbrow cooking_generic_swamp_paste",
                tostring(paste_after_result),
                tostring(raw_count), tostring(raw_after_read), tostring(raw_after_count),
                tostring(paste_read), tostring(paste_count)))

        -- ------------------------------------------------- platform helpers
        -- The Fishing Platform is an island reached only by Holgart's boat,
        -- and every room on it is behind a door: no goto_tile lands on it or
        -- leaves it (owner rule 2026-10-03). Everything on the platform and
        -- on Kent's island is walked, every door and ladder clicked.
        --
        -- The platform column is LINK_BELOW (quest_seaslug.rs2's crane
        -- banner): maps/m43_51.jl2 places every platform loc one level ABOVE
        -- the plane the player stands on, and t.world.loc_near reports that
        -- placed level (the committed run read the panel at 2768,3289,2 while
        -- the player stood on level 1). So a platform loc's copy is matched
        -- at the player's level + 1, and a press names that level in `at`.
        local function tile_text(r, tt)
            return (r == "ok" and tt) and (tt.x .. "," .. tt.z .. "," .. tt.level) or tostring(r)
        end

        -- Cross one platform door on foot (the b56 pass_door pattern). Every
        -- door here is slug2_village_poordoor, a SELFSTAGE door
        -- (doors/configs/doors_selfstage.loc:50): opened, it stands as the
        -- SAME symbol one tile over. Walk to the near tile; if the closed
        -- leaf is on the exact door tile at this floor's placed level, click
        -- that copy; otherwise an earlier press left it open, so assert the
        -- open leaf stands within 1 of the door tile (a check that fails when
        -- neither is there) and do not press it again. Then walk through and
        -- check the far tile and the floor.
        local DOOR = "slug2_village_poordoor"
        local function pass_door(prefix, door_x, door_z, near_x, near_z, far_x, far_z, far_ok, far_desc)
            t.player.walk_to(near_x, near_z, 40)
            local nr, nt = t.world.tile()
            t.check(prefix .. ".atDoor", nr == "ok" and nt.x == near_x and nt.z == near_z,
                "walked to " .. near_x .. "," .. near_z .. " beside the door at " .. door_x .. ","
                    .. door_z .. " -> " .. tile_text(nr, nt))
            local here = (nr == "ok") and nt.level or -1
            local placed = here + 1
            local cr, cd = t.world.loc_near(DOOR, 2)
            if cr == "ok" and cd.tile_x == door_x and cd.tile_z == door_z and cd.level == placed then
                t.exec(prefix .. ".openDoor", t.player.click_loc, DOOR, 1, { at = { door_x, door_z, placed } })
                t.ticks(1)
            else
                local open_ok = cr == "ok" and cd.level == placed
                    and not (cd.tile_x == door_x and cd.tile_z == door_z)
                    and math.abs(cd.tile_x - door_x) <= 1 and math.abs(cd.tile_z - door_z) <= 1
                t.check(prefix .. ".doorStandsOpen", open_ok,
                    DOOR .. " at " .. door_x .. "," .. door_z .. "," .. placed .. ": nearest copy "
                        .. (cr == "ok" and (cd.tile_x .. "," .. cd.tile_z .. "," .. cd.level) or tostring(cr))
                        .. " (want the open leaf within 1 of the door tile: an earlier press left it"
                        .. " open, so it is walked through, not pressed again)")
            end
            t.player.walk_to(far_x, far_z, 40)
            local fr, ft = t.world.tile()
            t.check(prefix .. ".throughDoor", fr == "ok" and ft.level == here and far_ok(ft),
                "walked through to " .. far_x .. "," .. far_z .. " -> " .. tile_text(fr, ft)
                    .. " (want " .. far_desc .. ", level " .. here .. ")")
        end

        -- Climb the platform ladder by its own op. Both halves stand on
        -- 2784,3286 (seaslug_ladder below, seaslug_ladder_top above) with
        -- forceapproach=30 (all.loc): approached from the north, 2784,3287.
        -- ~climb_ladder (ladders_stairs/scripts/ladders.rs2:137) moves the
        -- player one plane on its own tile, and the click can answer before
        -- the climb lands, so wait for the level, then check the tile.
        local function climb(name, sym, from_level, to_level)
            t.player.walk_to(2784, 3287, 40)
            local sr, st = t.world.tile()
            t.check(name .. ".atLadder", sr == "ok" and st.x == 2784 and st.z == 3287 and st.level == from_level,
                "walked to 2784,3287," .. from_level .. " north of " .. sym .. " 2784,3286 -> " .. tile_text(sr, st))
            t.exec(name, t.player.click_loc, sym, 1, { at = { 2784, 3286, from_level + 1 } })
            local lr, ld = t.await({
                level = function()
                    local r, tt = t.world.tile()
                    return r == "ok" and tt.level == to_level
                end,
                note = name .. " reaches level " .. to_level,
            }, 12)
            local tr, tt = t.world.tile()
            t.check(name .. ".landed",
                lr == "ok" and tr == "ok" and tt.level == to_level
                    and math.abs(tt.x - 2784) <= 1 and math.abs(tt.z - 3287) <= 1,
                sym .. " climb -> " .. tile_text(tr, tt) .. " (" .. tostring(lr) .. " " .. tostring(ld)
                    .. "; want level " .. to_level .. " beside 2784,3287)")
        end

        -- After each boat: the board proc teleports the player; check the
        -- arrival tile against the destination's walkable area.
        local function arrived(name, ok_fn, want)
            local ar, ad = t.await({
                level = function()
                    local r, tt = t.world.tile()
                    return r == "ok" and ok_fn(tt)
                end,
                note = name,
            }, 30)
            local r, tt = t.world.tile()
            t.check(name, ar == "ok" and r == "ok" and ok_fn(tt),
                "after the boat -> " .. tile_text(r, tt) .. " (" .. tostring(ar) .. " " .. tostring(ad)
                    .. "; want " .. want .. ")")
        end

        -- walk_to answers a bare ok (hollow under t.exec): walk, then grade
        -- the row on the tile actually reached.
        local function walk(name, x, z, level)
            local wr, wd = t.player.walk_to(x, z, 40)
            local r, tt = t.world.tile()
            t.check(name, r == "ok" and tt.x == x and tt.z == z and tt.level == level,
                "walk_to " .. x .. "," .. z .. " -> " .. tostring(wr) .. " " .. tostring(wd)
                    .. "; at " .. tile_text(r, tt) .. " (want " .. x .. "," .. z .. "," .. level .. ")")
        end
        local function on_platform(tt)
            return tt.level == 0 and tt.x >= 2762 and tt.x <= 2794 and tt.z >= 3266 and tt.z <= 3292
        end

        -- Kennith (spawn m43_51.spawn:42, 2766,3288 level 1) hides behind the
        -- stacked crates (slug2_crate_stack, width 2, 2765-2766,3287) at the
        -- back of the cabin on the west side of the first floor; the guide's
        -- talkToKennith is "from inside the cabin". The cabin's door is
        -- slug2_village_poordoor 2767,3285 (west edge): stand outside on
        -- 2767,3285, go in to 2766,3285, step to 2766,3286 (straight across
        -- the crates from Kennith) and talk; then out through the same door.
        local function cabin_in(prefix)
            pass_door(prefix .. ".cabinIn", 2767, 3285, 2767, 3285, 2766, 3285,
                function(tt) return tt.x <= 2766 and tt.x >= 2764 and tt.z >= 3284 and tt.z <= 3286 end,
                "inside Kennith's cabin, x 2764-2766 z 3284-3286")
            t.player.walk_to(2766, 3286, 20)
            local r, tt = t.world.tile()
            t.check(prefix .. ".besideCrates", r == "ok" and tt.x == 2766 and tt.z == 3286 and tt.level == 1,
                "walked to 2766,3286,1 across the crates from Kennith -> " .. tile_text(r, tt))
        end
        local function cabin_out(prefix)
            pass_door(prefix .. ".cabinOut", 2767, 3285, 2766, 3285, 2767, 3286,
                function(tt) return tt.x >= 2767 end, "back on the deck east of the cabin, x >= 2767")
        end

        -- Talk to Kennith from the cabin. He is two tiles back, behind the
        -- crates, and kennith.rs2 binds only [opnpc1] (adjacent reach): the
        -- server answers "I can't reach that!" from every tile a player can
        -- stand on (b59 fixer runs 2-3, rows talkToKennith/-Again/-AfterKicking).
        -- A refusal ends the run at an honest content_bug; once the content
        -- has the approach twin named below, the same call talks and the run
        -- continues down the route already proven in b59 run 3
        -- (build/orchestrator/fix_b59/seaslug.run3.ledger.tsv: 134 PASS, the
        -- only 3 FAILs being these talks).
        local function talk_kennith(name)
            local kr, kd = t.player.talk_to("kennith")
            if kr ~= "ok" then
                t.blocked("content_bug: " .. name .. ": talk_to(kennith) from 2766,3286,1 inside the cabin -> "
                    .. tostring(kr) .. " " .. tostring(kd) .. ". Kennith (areas/world/configs/m43_51.spawn:42, "
                    .. "2766,3288 level 1) stands in a 5-tile pocket (x 2765-2767, z 3288-3289) closed by "
                    .. "slug2_crate_stack 2765,3287 (width 2), seaslug_wall_window 2767,3287, seaslug_wall "
                    .. "2768,3288 and the panel slug_breakable_panel/boardgames_barstool_invis 2768,3289 "
                    .. "(maps/m43_51.jl2, level 2 placements); the nearest tile a player reaches is 2766,3286 "
                    .. "in the cabin, two tiles back. area_fishing_platform/scripts/kennith.rs2:10-12 binds only "
                    .. "[opnpc1,kennith*] (adjacent reach), and its [oploc1,kennithwall] (kennith.rs2:14, "
                    .. "LostCity's 'Shout-across' crates, LostCity maps/m43_51.jm2:5932,5955) has no placement in "
                    .. "this cache. Needed (content): an approach twin for the guide's 'talk to Kennith from inside "
                    .. "the cabin', [apnpc1,kennith_platform] (and kennith) if (npc_range(coord) > 2) "
                    .. "{ p_aprange(2); return; } @kennith_chat; -- the At First Light Verity fix, "
                    .. "quest_atfirstlight/scripts/atfirstlight.rs2:166-171.")
                return false
            end
            local kind = t.chat.kind()
            t.check(name, kind ~= "none",
                "talk_to(kennith) from 2766,3286,1 inside the cabin -> " .. tostring(kr) .. " "
                    .. tostring(kd) .. "; dialogue open: " .. tostring(kind))
            return true
        end

        -- --------------------------------------------------- give Holgart the paste
        -- Two separate conversations, by content design: the FIRST talk to
        -- Holgart (at any stage <= started) only asks for swamp paste and
        -- advances %seaslugquest to spoken_holgart (holgart.rs2's
        -- [label,holgart_wantboat]) -- it does not read the backpack at all.
        -- Only the SECOND talk (now that the stage is spoken_holgart) reaches
        -- [label,holgart_paste], which checks inv_total(inv, swamppaste) and
        -- hands it over.
        t.exec("holgart1.goto", t.player.goto_tile, 2720, 3306, 0)
        t.exec("holgart1.greet", t.player.talk_to, "holgartland")
        local hdrain1_result, hdrain1_detail = t.chat.drain({ stop_at = "none" })
        t.expect("holgart1.drain_close", hdrain1_result, hdrain1_detail)
        t.expect("quest.stage.spoken_holgart", t.quest.expect_stage("spoken_holgart"))

        t.exec("holgart2.greet", t.player.talk_to, "holgartland")
        local hdrain2_result, hdrain2_detail = t.chat.drain({ stop_at = "options" })
        t.expect("holgart2.drain_to_choice", hdrain2_result, hdrain2_detail)
        t.shot("holgart2-choice-menu")
        t.exec("holgart2.choose_board", t.chat.choose, "Okay, lets do it.")
        -- holgart_paste's own board-out (~board_ardougne_to_fishing_platform)
        -- is if_close then a few ticks later a fresh mesbox arrival page --
        -- the same reopen shape section 8 describes -- so drain twice: once
        -- for whatever is still open right after the choose, then again after
        -- a short buffer for the delayed arrival mesbox, rather than trust a
        -- single drain call not to stop on the transient close in between.
        local hdrain3_result, hdrain3_detail = t.chat.drain({ stop_at = "none" })
        t.expect("holgart2.drain_board", hdrain3_result, hdrain3_detail)
        t.ticks(3)
        local hdrain4_result, hdrain4_detail = t.chat.drain({ stop_at = "none" })
        t.check("holgart2.drain_arrival",
            hdrain4_result == "ok",
            "post-board drain -> " .. tostring(hdrain4_result) .. " " .. tostring(hdrain4_detail))
        t.expect("quest.stage.boat_repaired", t.quest.expect_stage("boat_repaired"))
        -- talkToHolgartWithSwampPaste: the paste left the pack.
        local paste_gone_read, paste_gone_count = t.inv.count("swamppaste")
        t.check("talkToHolgartWithSwampPaste.paste_taken",
            paste_gone_read == "ok" and paste_gone_count == 0,
            "inv.count(swamppaste) after Holgart's repair -> " .. tostring(paste_gone_read) .. " "
                .. tostring(paste_gone_count) .. " (was " .. tostring(paste_count) .. ")")
        arrived("travelWithHolgart.arrived", on_platform, "the platform deck, level 0")

        -- ---------------------------------------------- find Kennith, platform
        -- climbLadder: at stage boat_repaired the up-climb has no torch gate
        -- (quest_seaslug.rs2 [oploc1,seaslug_ladder] reads it only from
        -- spoken_kent on).
        climb("climbLadder", "seaslug_ladder", 0, 1)
        cabin_in("talkToKennith")
        if not talk_kennith("talkToKennith") then
            return
        end
        local kdrain1_result, kdrain1_detail = t.chat.drain({ stop_at = "none" })
        t.expect("kennith1.drain_close", kdrain1_result, kdrain1_detail)
        t.expect("quest.stage.spoken_kennith", t.quest.expect_stage("spoken_kennith"))
        cabin_out("talkToKennith")
        climb("goDownLadder", "seaslug_ladder_top", 1, 0)

        -- ------------------------------------------------- Holgart, platform
        -- Reached through the base spawn symbol (slug2_holgart_jeb,
        -- m43_51.spawn:25, 2782,3276 level 0) resolving its own
        -- multinpc1=holgartplatform child (trap 19's fix --
        -- slugmenace_pages.rs2 hands %slug2_npc_track1=0 to holgart.rs2's own
        -- [label,holgartplatform_talk]). Walked from the ladder on the deck.
        walk("goToIsland.walk", 2782, 3277, 0)
        t.exec("goToIsland", t.player.talk_to, "holgartplatform")
        local hdrain5_result, hdrain5_detail = t.chat.drain({ stop_at = "none" })
        t.expect("holgart3.drain_board", hdrain5_result, hdrain5_detail)
        t.ticks(3)
        local hdrain6_result, hdrain6_detail = t.chat.drain({ stop_at = "none" })
        t.check("holgart3.drain_arrival",
            hdrain6_result == "ok",
            "post-board drain -> " .. tostring(hdrain6_result) .. " " .. tostring(hdrain6_detail))
        t.expect("quest.stage.sailed_kent", t.quest.expect_stage("sailed_kent"))
        arrived("goToIsland.arrived", function(tt)
            return tt.level == 0 and tt.x >= 2785 and tt.x <= 2810 and tt.z >= 3310 and tt.z <= 3330
        end, "Kent's island, level 0")

        -- --------------------------------------------------------- Kent, island
        -- kent.rs2's own branch (%seaslugquest=sailed_kent) opens with the
        -- NPC's line, not the player's (kent.rs2:11, the exception trap 18
        -- itself flags as "almost always") and closes with if_close, a
        -- p_delay(2), then a SEPARATE reopened dialogue ("Traveller wait!")
        -- that pulls the sea slug off the player -- section 8's reopen shape,
        -- driven as two chat.play calls either side of an explicit await.
        -- The island is one open patch: walked from the boat (static reach
        -- 2800,3320 -> 2793,3321 is 8 tiles, no door).
        walk("kent.walk", 2793, 3321, 0)
        t.exec("kent.greet", t.player.talk_to, "kent")
        local kent1_result, kent1_detail = t.exec("kent.rescue_offer", t.chat.play, {
            "npc:Oh thank Saradomin",
            "player:Your wife sent me out",
            "npc:I knew the row boat wasn't sea worthy",
            "player:What's going on here",
            "npc:Five days ago we pulled in a huge catch",
            "npc:That's when the fishermen began to act strange",
            "npc:they attach themselves to your body",
            "npc:I told Kennith to hide until I returned",
            "npc:Please go back and get my boy",
            "end",
        })
        t.expect("quest.stage.spoken_kent", t.quest.expect_stage("spoken_kent"))

        -- kent.rs2:28-29: "Traveller wait!" ITSELF is if_close'd right after
        -- its own continue too (a second, nested reopen boundary), so
        -- t.chat.play's trailing readiness wait -- which resolves on the
        -- page CHANGING, and closing to "none" counts as a change -- can
        -- report "ok" the instant that transient close lands, well before
        -- the real reopen ("A few more minutes...") three ticks later
        -- mounts. Measured this run: a single list spanning both reopens
        -- read "the dialogue closed after 1 page(s)" at entry 2. Split every
        -- reopen into its own chat.play call either side of an explicit
        -- await (doc section 8's own worked pattern), never one list across
        -- an if_close.
        local reopen1_result, reopen1_detail = t.await({
            level = function() return t.chat.kind() ~= "none" end,
            note = "kent.slug_removal_reopen1",
        }, 10)
        t.step("kent.slug_removal_reopen1",
            reopen1_result == "ok" and "PASS" or "FAIL",
            "await chat reopen -> " .. tostring(reopen1_result) .. " " .. tostring(reopen1_detail))

        t.exec("kent.slug_removal_wait", t.chat.play, {
            "npc:Traveller wait",
            "end",
        })

        local reopen2_result, reopen2_detail = t.await({
            level = function() return t.chat.kind() ~= "none" end,
            note = "kent.slug_removal_reopen2",
        }, 10)
        t.step("kent.slug_removal_reopen2",
            reopen2_result == "ok" and "PASS" or "FAIL",
            "await chat reopen -> " .. tostring(reopen2_result) .. " " .. tostring(reopen2_detail))

        t.exec("kent.slug_removal", t.chat.play, {
            "npc:A few more minutes",
            "player:Yuck",
            "end",
        })

        -- ---------------------------------------- back to the platform, Bailey
        -- holgartsunkboat_talk's own branch: at any stage other than
        -- sailed_kent (we are spoken_kent now) it re-boards straight back to
        -- the platform -- the same reused board proc, if_close then a
        -- delayed reopen for the arrival mesbox.
        walk("returnFromIsland.walk", 2800, 3320, 0)
        t.exec("returnFromIsland", t.player.talk_to, "holgartsunkboat")
        local sdrain1_result, sdrain1_detail = t.chat.drain({ stop_at = "none" })
        t.expect("holgartsunkboat.drain_board", sdrain1_result, sdrain1_detail)
        t.ticks(3)
        local sdrain2_result, sdrain2_detail = t.chat.drain({ stop_at = "none" })
        t.check("holgartsunkboat.drain_arrival",
            sdrain2_result == "ok",
            "post-board drain -> " .. tostring(sdrain2_result) .. " " .. tostring(sdrain2_detail))
        arrived("returnFromIsland.arrived", on_platform, "the platform deck, level 0")

        -- Bailey (m43_51.spawn:9, 2765,3276) is in the south-west room, door
        -- slug2_village_poordoor 2768,3276 (west edge): stand on 2768,3276,
        -- go in to 2767,3276. The room (x 2763-2767, z 3274-3277) also holds
        -- one of the two broken_glass spawns (m43_51.spawn:61, 2766,3277),
        -- which is the guide's pickupGlass "in the room".
        pass_door("talkToBaileyForTorch.roomIn", 2768, 3276, 2768, 3276, 2767, 3276,
            function(tt) return tt.x >= 2763 and tt.x <= 2767 and tt.z >= 3274 and tt.z <= 3277 end,
            "inside Bailey's room, x 2763-2767 z 3274-3277")
        t.exec("talkToBaileyForTorch", t.player.talk_to, "bailey")
        local bdrain1_result, bdrain1_detail = t.chat.drain({ stop_at = "none" })
        t.expect("bailey1.drain_close", bdrain1_result, bdrain1_detail)
        -- trap 24: drain's own "ok" is the server's sentence, not the
        -- container update -- poll rather than read torch_unlit bare.
        -- inv.await's own second return is a detail string (ok/timeout),
        -- never a count (unlike inv.count) -- read the count back with a
        -- separate inv.count call.
        local torch_await_result, torch_await_detail = t.inv.await("torch_unlit", 1, 10)
        local torch_read, torch_count = t.inv.count("torch_unlit")
        t.check("bailey1.gave_torch",
            torch_await_result == "ok" and torch_read == "ok" and torch_count >= 1,
            "inv.await(torch_unlit, 1, 10) -> " .. tostring(torch_await_result) .. " "
                .. tostring(torch_await_detail) .. "; inv.count -> " .. tostring(torch_read) .. " "
                .. tostring(torch_count) .. " -- bailey.rs2 'Bailey gives you a torch.'")

        -- ------------------------------------------------- dry and light the torch
        local glass_before_read, glass_before_count = t.inv.count("broken_glass")
        local glass_pickup_result, glass_pickup_detail = t.player.click_obj("broken_glass")
        local glass_await_result, glass_await_detail = t.inv.await("broken_glass", 1, 10)
        local glass_after_result, glass_after_count = t.inv.count("broken_glass")
        t.check("pickupGlass",
            glass_await_result == "ok" and glass_after_result == "ok" and glass_after_count >= 1
                and glass_before_read == "ok" and glass_before_count == 0,
            "click_obj(broken_glass) in Bailey's room -> " .. tostring(glass_pickup_result) .. " (" ..
                tostring(glass_pickup_detail) .. "); before=" .. tostring(glass_before_count)
                .. "; await -> " .. tostring(glass_await_result)
                .. " " .. tostring(glass_await_detail) .. "; count -> " .. tostring(glass_after_result)
                .. " " .. tostring(glass_after_count))
        pass_door("talkToBaileyForTorch.roomOut", 2768, 3276, 2767, 3276, 2769, 3276,
            function(tt) return tt.x >= 2768 end, "back on the deck east of Bailey's room, x >= 2768")

        -- Ground spawn in the platform's north-east corner (m43_51.spawn:63,
        -- 2784,3289 level 0), walked to on the open deck.
        walk("pickupDampSticks.walk", 2784, 3289, 0)
        local sticks_pickup_result, sticks_pickup_detail = t.player.click_obj("damp_sticks")
        local sticks_await_result, sticks_await_detail = t.inv.await("damp_sticks", 1, 10)
        local sticks_after_result, sticks_after_count = t.inv.count("damp_sticks")
        t.check("pickupDampSticks",
            sticks_await_result == "ok" and sticks_after_result == "ok" and sticks_after_count >= 1,
            "click_obj(damp_sticks) -> " .. tostring(sticks_pickup_result) .. " (" ..
                tostring(sticks_pickup_detail) .. "); await -> " .. tostring(sticks_await_result)
                .. " " .. tostring(sticks_await_detail) .. "; count -> " .. tostring(sticks_after_result)
                .. " " .. tostring(sticks_after_count))

        -- useGlassOnDampSticks: [opheldu,damp_sticks] arms damp_sticks
        -- (item_a) and clicks broken_glass (item_b) -- quest_seaslug.rs2:36-46
        -- deletes the damp sticks and adds dry sticks; the glass is kept.
        t.exec("useGlassOnDampSticks", t.player.use_item_on_item, "damp_sticks", "broken_glass")
        local dry_read, dry_count = t.inv.count("dry_sticks")
        local damp_read, damp_count = t.inv.count("damp_sticks")
        t.check("seaslug.have_dry_sticks",
            dry_read == "ok" and dry_count >= 1 and damp_read == "ok" and damp_count == 0,
            "inv.count(dry_sticks) -> " .. tostring(dry_read) .. " " .. tostring(dry_count)
                .. ", damp_sticks -> " .. tostring(damp_read) .. " " .. tostring(damp_count)
                .. " -- quest_seaslug.rs2 inv_del(inv, damp_sticks, 1); inv_add(inv, dry_sticks, 1)")

        -- rubSticks: [opheld1,dry_sticks], gated on Firemaking >= 30 (staged
        -- at 50) and a stat_random(firemaking, 64, 512) roll -- retried up to
        -- three times against a miss, one OUTCOME row (trap 15).
        local torch_lit_ok = false
        local torch_attempts = 0
        local torch_last_detail = nil
        while not torch_lit_ok and torch_attempts < 3 do
            torch_attempts = torch_attempts + 1
            local dry_now_read, dry_now_count = t.inv.count("dry_sticks")
            if dry_now_read ~= "ok" or (dry_now_count or 0) < 1 then
                torch_last_detail = "no dry_sticks left (" .. tostring(dry_now_read) .. " "
                    .. tostring(dry_now_count) .. ")"
                break
            end
            local rub_result, rub_detail = t.player.inv_op("dry_sticks", 1)
            local lit_await_result, lit_await_detail = t.inv.await("torch_lit", 1, 10)
            local lit_result, lit_count = t.inv.count("torch_lit")
            torch_last_detail = "attempt " .. torch_attempts .. ": inv_op(dry_sticks,1) -> "
                .. tostring(rub_result) .. " (" .. tostring(rub_detail) .. "); torch_lit await -> "
                .. tostring(lit_await_result) .. " " .. tostring(lit_await_detail)
                .. "; count -> " .. tostring(lit_result) .. " " .. tostring(lit_count)
            if lit_await_result == "ok" and lit_result == "ok" and lit_count >= 1 then
                torch_lit_ok = true
            end
        end
        t.step("rubSticks", torch_lit_ok and "PASS" or "FAIL", torch_last_detail)
        t.expect("quest.stage.lit_torch", t.quest.expect_stage("lit_torch"))

        -- ---------------------------------------------------- Kennith, second talk
        -- goBackUpLadder: from spoken_kent on, the up-climb needs the lit
        -- torch (quest_seaslug.rs2 [oploc1,seaslug_ladder]); without it the
        -- fishermen hit for 4 and the climb is refused.
        climb("goBackUpLadder", "seaslug_ladder", 0, 1)
        cabin_in("talkToKennithAgain")
        if not talk_kennith("talkToKennithAgain") then
            return
        end
        local kdrain2_result, kdrain2_detail = t.chat.drain({ stop_at = "none" })
        t.expect("kennith2.drain_close", kdrain2_result, kdrain2_detail)
        t.expect("quest.stage.kennith_need_escape", t.quest.expect_stage("kennith_need_escape"))
        cabin_out("talkToKennithAgain")

        -- ------------------------------------------------------- kick the panel
        -- kickWall: slug_breakable_panel 2768,3289 (placed level 2 = the
        -- player's level 1) is a multiloc shell whose stage 1-9 slot is
        -- seaslug_wall_closed ("Badly repaired wall", op1=Kick,
        -- forceapproach=29: approached from the east). Stand on 2769,3289,1,
        -- the deck tile east of it, and press Kick for real -- no drive.op.
        walk("kickWall.atPanel", 2769, 3289, 1)
        t.exec("kickWall", t.player.click_loc, "slug_breakable_panel", 1, { at = { 2768, 3289, 2 } })
        t.expect("kickWall.opening", t.msg.await("opening big enough for Kennith", 12),
            "msg.await(\"opening big enough for Kennith\") -- quest_seaslug.rs2 [oploc1,slug_breakable_panel]")
        t.expect("quest.stage.panel_opened", t.quest.expect_stage("panel_opened"))

        -- ---------------------------------------------------- Kennith, third talk
        cabin_in("talkToKennithAfterKicking")
        if not talk_kennith("talkToKennithAfterKicking") then
            return
        end
        local kdrain3_result, kdrain3_detail = t.chat.drain({ stop_at = "none" })
        t.expect("kennith3.drain_close", kdrain3_result, kdrain3_detail)
        t.expect("quest.stage.need_kennith_path", t.quest.expect_stage("need_kennith_path"))
        cabin_out("talkToKennithAfterKicking")

        -- ------------------------------------------------------ work the crane
        -- activateCrane: seaslug_crane is a 4x4 at 2770,3287 (x 2770-2773,
        -- z 3287-3290 on level 1). Its gate (quest_seaslug.rs2
        -- [oploc1,seaslug_crane]) is distance(coord, loc_coord) <= 4. The deck
        -- south of it (x 2770-2780, z 3284-3287) is railed off from the rest
        -- of the floor (seaslug_railing 2781,3285 west edge), so it is not
        -- stood on; 2769,3289 touches the footprint's west side and is 2 from
        -- its corner.
        walk("activateCrane.atCrane", 2769, 3289, 1)
        t.exec("activateCrane", t.player.click_loc, "seaslug_crane", 1, { at = { 2770, 3287, 2 } })
        -- t.expect, not t.exec: msg.await answers ("ok", nil) and the hollow
        -- rule would FAIL an ok with no detail.
        t.expect("crane.kennith_lowered", t.msg.await("lower Kennith to the row boat", 12),
            "msg.await(\"lower Kennith to the row boat\")")
        t.expect("quest.stage.saved_kennith", t.quest.expect_stage("saved_kennith"))

        -- ----------------------------------------------------- back to shore
        climb("goDownLadderAgain", "seaslug_ladder_top", 1, 0)
        walk("returnWithHolgart.walk", 2782, 3277, 0)
        t.exec("returnWithHolgart", t.player.talk_to, "holgartplatform")
        local hdrain7_result, hdrain7_detail = t.chat.drain({ stop_at = "none" })
        t.expect("holgart4.drain_board", hdrain7_result, hdrain7_detail)
        t.ticks(3)
        local hdrain8_result, hdrain8_detail = t.chat.drain({ stop_at = "none" })
        t.check("holgart4.drain_arrival",
            hdrain8_result == "ok",
            "post-board drain -> " .. tostring(hdrain8_result) .. " " .. tostring(hdrain8_detail))
        arrived("returnWithHolgart.arrived", function(tt)
            return tt.level == 0 and tt.x >= 2705 and tt.x <= 2730 and tt.z >= 3295 and tt.z <= 3315
        end, "the Witchaven jetty, level 0")

        -- ------------------------------------------------------- hand-in, Caroline
        local fishing_snapshot_result, fishing_snapshot = t.skill.snapshot()
        local fishing_before = (fishing_snapshot_result == "ok" and fishing_snapshot and fishing_snapshot.fishing)
            and fishing_snapshot.fishing.experience or nil
        t.check("seaslug.fishing_xp_before_read",
            fishing_snapshot_result == "ok" and fishing_before == 0,
            "skill.snapshot before hand-in -> " .. tostring(fishing_snapshot_result)
                .. ", fishing experience " .. tostring(fishing_before)
                .. " (fresh account: none before the reward)")

        -- Caroline stands on the open jetty path (static reach from the boat's
        -- landing is 9 tiles, no door): walked.
        walk("finishQuest.walk", 2716, 3302, 0)
        t.exec("finishQuest", t.player.talk_to, "caroline")
        local cdrain_result, cdrain_detail = t.chat.drain({ stop_at = "none" })
        t.expect("caroline2.drain_close", cdrain_result, cdrain_detail)

        -- Completion is asynchronous: caroline_savedkennith's own
        -- queue(seaslug_quest_complete, 0, 0) lands behind the dialogue's own
        -- closing lines (doc section 8's own load-bearing t.ticks(3)).
        t.ticks(3)

        t.quest.expect_complete() -- writes quest.varp_complete/quest.scroll_title/quest.points/quest.journal

        -- caroline.rs2's [queue,seaslug_quest_complete]: stat_advance(fishing,
        -- 71750) (7175 Fishing XP at the pack's 10x fixed-point scale) and
        -- inv_add(inv, bigoysterpearls, 1) -- the two rewards the scroll
        -- string itself lists, asserted literally (not read back from the
        -- scroll).
        t.expect("reward.fishing_xp",
            t.skill.expect_gain("fishing", 7175, fishing_snapshot))
        t.exec("reward.oyster_pearls", t.inv.expect_has, "bigoysterpearls", 1)

        t.finish(0)
    end,
}
