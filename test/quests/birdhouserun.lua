-- A Fossil Island bird house run, from Varrock centre, every tier.
-- Content: skill_hunter/scripts/birdhouse.rs2 (craft, place, seed, empty) and
-- quest_bonevoyage/scripts/bonevoyage.rs2 (the barge guard's Quick-Travel and
-- the camp rowboat home).
--
-- Start: Varrock centre (3213,3424) with Bone Voyage done, 99 Crafting and
-- Hunter, a hammer, a chisel, nine clockworks, one log of each of the nine
-- tiers and 90 potato seeds. The account crafts all nine houses there, walks
-- east to the Digsite barge (through the vm_fencegate, the only door on the
-- way: reach.py), Quick-Travels to Fossil Island, and lays three runs over
-- the four spaces -- place and seed; 50 minutes later empty, place the next
-- tier and seed -- until every tier has been placed, filled and emptied,
-- then walks back to the camp and rows home to the Digsite barge.
--
-- Expected numbers are the OSRS wiki's (Bird house trapping, read 2026-10-15),
-- not the content's table: crafting levels/xp from Bird_house_trapping/Table;
-- Hunter xp per house is the table BEFORE the 19 August 2026 "Summer Sweep
-- Up" cut, which is the era birdhouse.dbrow implements (280 .. 1200).
-- Quick-Travel's landing: 3724,3808,0 (Near-Reality TransportLoader), and the
-- message is Transcript:Barge_guard's "You row to the expedition camp.".
--
-- Spaces (birdhouse.rs2 header, cache maps) and the tile each is worked from:
--   a 3677,3882 Mushroom Meadow (from 3677,3881)
--   b 3679,3815 Tar Swamp entrance (from 3679,3814)
--   c 3768,3761 Verdant Valley north (from 3768,3760)
--   d 3763,3755 Verdant Valley south (from 3763,3754)
return {
    id = "birdhouserun",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::complete quest_bonevoyage",
        "::setlevel crafting 99",
        "::setlevel hunter 99",
        "::give hammer 1",
        "::give chisel 1",
        "::give poh_clockwork_mechanism 9",
        "::give logs 1",
        "::give oak_logs 1",
        "::give willow_logs 1",
        "::give teak_logs 1",
        "::give maple_logs 1",
        "::give mahogany_logs 1",
        "::give yew_logs 1",
        "::give magic_logs 1",
        "::give redwood_logs 1",
        "::give potato_seed 90",
    },

    run = function(t)
        -- tier: logs, house, crafting xp, Hunter xp per emptied house.
        local TIERS = {
            { key = "normal", logs = "logs", house = "birdhouse_normal", craft_xp = 15, hunter_xp = 280 },
            { key = "oak", logs = "oak_logs", house = "birdhouse_oak", craft_xp = 20, hunter_xp = 420 },
            { key = "willow", logs = "willow_logs", house = "birdhouse_willow", craft_xp = 25, hunter_xp = 560 },
            { key = "teak", logs = "teak_logs", house = "birdhouse_teak", craft_xp = 30, hunter_xp = 700 },
            { key = "maple", logs = "maple_logs", house = "birdhouse_maple", craft_xp = 35, hunter_xp = 820 },
            { key = "mahogany", logs = "mahogany_logs", house = "birdhouse_mahogany", craft_xp = 40, hunter_xp = 960 },
            { key = "yew", logs = "yew_logs", house = "birdhouse_yew", craft_xp = 45, hunter_xp = 1020 },
            { key = "magic", logs = "magic_logs", house = "birdhouse_magic", craft_xp = 50, hunter_xp = 1140 },
            { key = "redwood", logs = "redwood_logs", house = "birdhouse_redwood", craft_xp = 55, hunter_xp = 1200 },
        }
        local BY_KEY = {}
        for i = 1, #TIERS do
            BY_KEY[TIERS[i].key] = TIERS[i]
        end

        local SITES = {
            a = { at = { 3677, 3882 }, stand = { 3677, 3881 }, var = "varp1626_birdhouse_transmit_a" },
            b = { at = { 3679, 3815 }, stand = { 3679, 3814 }, var = "varp1627_birdhouse_transmit_b" },
            c = { at = { 3768, 3761 }, stand = { 3768, 3760 }, var = "varp1628_birdhouse_transmit_c" },
            d = { at = { 3763, 3755 }, stand = { 3763, 3754 }, var = "varp1629_birdhouse_transmit_d" },
        }
        -- Walking legs (reach.py's shortest walk, a waypoint every <= 10 tiles).
        local CAMP_TO_B = { { 3714, 3808 }, { 3704, 3808 }, { 3702, 3800 }, { 3693, 3801 }, { 3687, 3805 },
            { 3682, 3810 }, { 3679, 3814 } }
        local B_TO_A = { { 3681, 3822 }, { 3681, 3832 }, { 3681, 3842 }, { 3681, 3852 }, { 3682, 3861 },
            { 3679, 3868 }, { 3678, 3875 }, { 3677, 3881 } }
        local A_TO_C = { { 3683, 3877 }, { 3682, 3868 }, { 3682, 3858 }, { 3682, 3848 }, { 3683, 3839 },
            { 3688, 3834 }, { 3692, 3828 }, { 3694, 3820 }, { 3701, 3817 }, { 3707, 3813 }, { 3717, 3813 },
            { 3727, 3813 }, { 3732, 3808 }, { 3731, 3799 }, { 3731, 3791 }, { 3733, 3783 }, { 3730, 3776 },
            { 3728, 3768 }, { 3725, 3761 }, { 3724, 3752 }, { 3729, 3747 }, { 3735, 3743 }, { 3744, 3742 },
            { 3753, 3743 }, { 3763, 3743 }, { 3773, 3743 }, { 3781, 3745 }, { 3784, 3752 }, { 3783, 3761 },
            { 3780, 3768 }, { 3773, 3765 }, { 3768, 3760 } }
        local C_TO_D = { { 3764, 3754 }, { 3763, 3754 } }

        -- A leg walked backwards: its own points reversed, ending on `to`.
        local function back(leg, to)
            local out = {}
            for i = #leg - 1, 1, -1 do
                out[#out + 1] = leg[i]
            end
            out[#out + 1] = to
            return out
        end

        local NESTS = { "bird_nest_seeds", "bird_nest_ring", "bird_nest_egg_red", "bird_nest_egg_green",
            "bird_nest_egg_blue", "bird_nest_empty", "wc_clue_nest_easy", "wc_clue_nest_medium",
            "wc_clue_nest_hard", "wc_clue_nest_elite" }

        local function tile_str(tl)
            return tl and (tostring(tl.x) .. "," .. tostring(tl.z) .. "," .. tostring(tl.level)) or "nil"
        end
        local function count(name)
            local _, n = t.inv.count(name)
            return n or 0
        end
        local function server(name)
            local r, v = t.var.server(name)
            return r == "ok" and v or -1
        end
        -- The space's multiloc value (birdhouse.rs2: 0 empty, then built /
        -- seeded / ready in groups of three per tier).
        local function state_of(tier_index, phase)
            return (tier_index - 1) * 3 + phase
        end
        local function tier_index(key)
            for i = 1, #TIERS do
                if TIERS[i].key == key then
                    return i
                end
            end
            return nil
        end

        -- ---------------------------------------------------------------
        -- Varrock centre: craft every tier from logs + clockwork.
        -- ---------------------------------------------------------------
        t.exec("goto-varrock", t.player.goto_tile, 3213, 3424, 0)
        for i = 1, #TIERS do
            local tier = TIERS[i]
            local _, snap = t.skill.snapshot()
            t.exec("craft." .. tier.key, t.player.use_item_on_item, tier.logs, "poh_clockwork_mechanism")
            t.expect("craft." .. tier.key .. ".house", t.inv.await(tier.house, 1, 5))
            t.check("craft." .. tier.key .. ".spent", count(tier.logs) == 0 and count("poh_clockwork_mechanism") == 9 - i,
                tier.logs .. " " .. count(tier.logs) .. " (want 0), clockwork " .. count("poh_clockwork_mechanism")
                    .. " (want " .. (9 - i) .. ")")
            t.expect("craft." .. tier.key .. ".xp", t.skill.expect_gain("crafting", tier.craft_xp, snap))
        end
        local _, here0 = t.world.tile()
        t.check("craft.still_in_varrock", here0 ~= nil and math.abs(here0.x - 3213) <= 2 and math.abs(here0.z - 3424) <= 2,
            "crafted all nine at " .. tile_str(here0) .. " (Varrock centre 3213,3424)")

        -- ---------------------------------------------------------------
        -- On foot to the Digsite barge, then Quick-Travel.
        -- ---------------------------------------------------------------
        t.exec("walk-to-fencegate", t.player.walk_route, { { 3218, 3429 }, { 3228, 3429 }, { 3238, 3429 },
            { 3248, 3429 }, { 3258, 3429 }, { 3268, 3429 }, { 3278, 3429 }, { 3288, 3429 }, { 3294, 3429 } })
        t.exec("fencegate", t.player.pass_door, { closed = "vm_fencegate_r", open = "vm_fencegate_r",
            at = { 3296, 3429, 0 }, near = { 3294, 3429 }, far = { 3297, 3429 } })
        t.exec("walk-to-barge", t.player.walk_route, { { 3299, 3437 }, { 3307, 3439 }, { 3314, 3442 },
            { 3321, 3445 }, { 3331, 3445 }, { 3341, 3445 }, { 3351, 3445 }, { 3361, 3445 }, { 3363, 3446 } })
        t.exec("quick-travel", t.player.talk_to, "fossilquest_barge_guard_port_postquest", 4)
        t.ticks(3)
        local _, landed = t.world.tile()
        t.check("quick-travel.landed", landed ~= nil and landed.level == 0 and landed.x == 3724 and landed.z == 3808,
            "after Quick-Travel at " .. tile_str(landed) .. " (want the camp landing 3724,3808,0)")
        t.expect("quick-travel.message", t.msg.expect("You row to the expedition camp."))

        -- ---------------------------------------------------------------
        -- The run: at each space, empty what is there, place, seed.
        -- ---------------------------------------------------------------
        local function walk(step, leg)
            t.exec("walk-" .. step, t.player.walk_route, leg)
        end

        local function place_and_seed(step, site, key)
            local s = SITES[site]
            local tier = BY_KEY[key]
            local ti = tier_index(key)
            local space = t.player.by_symbol("loc", "birdhouse_not_built")
            t.exec(step .. ".place", t.player.use_on, tier.house, space, { at = { s.at[1], s.at[2] } })
            t.expect(step .. ".built", t.var.await(s.var, state_of(ti, 1), 5))
            t.check(step .. ".house_spent", count(tier.house) == 0, tier.house .. " left: " .. count(tier.house))
            local seeds_before = count("potato_seed")
            local built = t.player.by_symbol("loc", "birdhouse_" .. key .. "_built")
            t.exec(step .. ".seed", t.player.use_on, "potato_seed", built, { at = { s.at[1], s.at[2] } })
            t.expect(step .. ".seeded", t.var.await(s.var, state_of(ti, 2), 5))
            t.check(step .. ".seeds_spent", seeds_before - count("potato_seed") == 10,
                "potato_seed " .. seeds_before .. " -> " .. count("potato_seed") .. " (want 10 used)")
        end

        local function empty(step, site, key)
            local s = SITES[site]
            local tier = BY_KEY[key]
            local ti = tier_index(key)
            -- The space turns to the full house on its own (the poll softtimer).
            t.expect(step .. ".ready", t.var.await(s.var, state_of(ti, 3), 120))
            local _, snap = t.skill.snapshot()
            local clock_before = count("poh_clockwork_mechanism")
            local feathers_before = count("feather")
            t.exec(step .. ".empty", t.player.click_loc, "birdhouse_" .. key .. "_bird", 3, { at = { s.at[1], s.at[2], 0 } })
            t.expect(step .. ".space_clear", t.var.await(s.var, 0, 5))
            t.expect(step .. ".hunter_xp", t.skill.expect_gain("hunter", tier.hunter_xp, snap))
            t.check(step .. ".clockwork_back", count("poh_clockwork_mechanism") == clock_before + 1,
                "clockwork " .. clock_before .. " -> " .. count("poh_clockwork_mechanism"))
            local got = count("feather") - feathers_before
            t.check(step .. ".feathers", got == 30 or got == 40 or got == 50 or got == 60,
                "+" .. got .. " feathers (wiki: 30, 40, 50 or 60)")
            local mr, meat = t.world.obj_near("spit_raw_bird_meat", 2)
            local meat_n = (mr == "ok" and type(meat) == "table") and meat.count or 0
            t.check(step .. ".meat", meat_n == 10,
                "raw bird meat on the ground: " .. tostring(meat_n) .. " (" .. tostring(mr) .. "; wiki: 10, always dropped)")
            -- Nests do not stack: a run with no bank trip between houses
            -- would fill the backpack and the next empty would be refused
            -- for want of a slot for the clockwork. Drop them here.
            local held = {}
            for i = 1, #NESTS do
                local n = count(NESTS[i])
                if n > 0 then
                    held[#held + 1] = NESTS[i] .. " x" .. n
                end
                for k = 1, n do
                    t.exec(step .. ".drop." .. NESTS[i] .. "." .. k, t.player.drop, NESTS[i])
                end
            end
            t.note(step .. " nests: " .. (#held > 0 and table.concat(held, ", ") or "none"))
        end

        -- Run 1: the four best tiers.
        local RUN1 = { b = "redwood", a = "magic", c = "yew", d = "mahogany" }
        local RUN2 = { d = "maple", c = "teak", a = "willow", b = "oak" }
        local RUN3 = { b = "normal" }

        walk("camp-to-b", CAMP_TO_B)
        place_and_seed("run1.b", "b", RUN1.b)
        walk("b-to-a", B_TO_A)
        place_and_seed("run1.a", "a", RUN1.a)
        walk("a-to-c", A_TO_C)
        place_and_seed("run1.c", "c", RUN1.c)
        walk("c-to-d", C_TO_D)
        place_and_seed("run1.d", "d", RUN1.d)

        t.expect("wait.run1", t.clock.skip(50))

        -- Run 2, walked back the way it came: empty run 1, lay the next four.
        empty("run2.d", "d", RUN1.d)
        place_and_seed("run2.d", "d", RUN2.d)
        walk("d-to-c", back(C_TO_D, SITES.c.stand))
        empty("run2.c", "c", RUN1.c)
        place_and_seed("run2.c", "c", RUN2.c)
        walk("c-to-a", back(A_TO_C, SITES.a.stand))
        empty("run2.a", "a", RUN1.a)
        place_and_seed("run2.a", "a", RUN2.a)
        walk("a-to-b", back(B_TO_A, SITES.b.stand))
        empty("run2.b", "b", RUN1.b)
        place_and_seed("run2.b", "b", RUN2.b)

        t.expect("wait.run2", t.clock.skip(50))

        -- Run 3: empty run 2, the last tier goes on b.
        empty("run3.b", "b", RUN2.b)
        place_and_seed("run3.b", "b", RUN3.b)
        walk("b-to-a.3", B_TO_A)
        empty("run3.a", "a", RUN2.a)
        walk("a-to-c.3", A_TO_C)
        empty("run3.c", "c", RUN2.c)
        walk("c-to-d.3", C_TO_D)
        empty("run3.d", "d", RUN2.d)

        t.expect("wait.run3", t.clock.skip(50))
        walk("d-to-c.4", back(C_TO_D, SITES.c.stand))
        walk("c-to-a.4", back(A_TO_C, SITES.a.stand))
        walk("a-to-b.4", back(B_TO_A, SITES.b.stand))
        empty("run4.b", "b", RUN3.b)

        t.check("all.clockworks", count("poh_clockwork_mechanism") == 9,
            "clockwork " .. count("poh_clockwork_mechanism") .. " after every house was emptied (want all 9 back)")

        -- Home: back to the camp and the rowboat to the Digsite barge
        -- (3724,3808 -> 3362,3445,0, Near-Reality TransportLoader).
        walk("b-to-camp", { { 3684, 3809 }, { 3690, 3805 }, { 3697, 3802 }, { 3704, 3801 }, { 3708, 3807 },
            { 3718, 3807 }, { 3724, 3807 } })
        t.exec("rowboat", t.player.click_loc, "fossil_rowboat_camp", 1, { at = { 3723, 3805, 0 } })
        t.ticks(3)
        local _, home = t.world.tile()
        t.check("rowboat.landed", home ~= nil and home.level == 0 and home.x == 3362 and home.z == 3445,
            "after the camp rowboat at " .. tile_str(home) .. " (want the Digsite barge 3362,3445,0)")
        t.finish(0)
    end,
}
