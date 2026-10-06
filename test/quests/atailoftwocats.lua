-- A Tail of Two Cats (2 QP). Content:
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_atailoftwocats/scripts/twocats.rs2
-- (2052 lines) plus its delegation hosts:
--   quest_dragonslayer2/scripts/dragonslayer2.rs2 -- Bob's ONLY [opnpc1,...]
--     trigger (death_growncat_black) and the Sphinx's ([opnpc1,ics_little_sphinx]).
--   areas/varrock/scripts/gertrude.rs2 -- [opnpc1,gertrude] (the BASE symbol
--     the spawn row carries, trap 19); its ladder falls through to
--     ~gertrude_route_topics -> ~twocats_gertrude_after_bob.
--   areas/varrock/scripts/reldo.rs2 -- [opnpc1,reldo] owns %twocats_quest
--     25..30 and hands off to [proc,twocats_reldo_talk].
--
-- RE-AUTHOR after 3b41349334 [parity:parity1p] (queue's last_failure): the
-- ONLY thing wrong with the previous attempt (reverted -- see
-- `git show bf8b834ba:test/quests/atailoftwocats.lua`, 128 rows / 441 shots,
-- reverted by the b25 sampler) was its three "find Bob" legs -- they turned
-- the dial four times blind, read %twocats_locator_direction with no check
-- that the eyes had ever lit, and reached Bob with the content's own
-- ::twocats_gotobob debugproc, which twocats.rs2:440-445 spells out is "not
-- for a quest test" (the sampler's own words: "the guide's locate leg is
-- handed over by a debugproc"). Every OTHER leg below is that same rejected
-- file's content, unchanged: verbatim dialogue already checked line-for-line
-- against the live `.rs2` and driven end to end in that 128-row run, so it is
-- carried forward rather than re-derived. Only findBob/findBobAgain/
-- findBobToFinish (the dial) and talkToBob/talkToBobAgain/talkToBobToFinish
-- (reaching Bob) are new, built from the proven scratch driver
-- `build/parity_state/parity1p/scripts/twocats_locate.lua` (157/157 rows
-- through the real embedded client, its own header cites the wiki: Catspeak
-- amulet(e) "turn the compass clockwise or anti-clockwise by clicking on the
-- whiskers until the eyes are lit and the cat's mouth starts moving"; Bob
-- (cat) "When the eyes light up ... Bob can be found in the direction that
-- the dial is pointing"), now ONE local function `locate_bob` used at all
-- three sites.
--
-- RE-DRIVEN b58 (door rule, docs/QUEST_ORCHESTRATOR.md 2026-10-03): every
-- goto now leaves from and lands on an open, unroofed street tile, and every
-- door between is opened on foot in and out (`pass_door`); the search's moves
-- are walks while Bob is on screen and waypoint-to-waypoint travel otherwise;
-- the death runes are a setup bring-along. The goto audit is in
-- build/orchestrator/fix_b58/atailoftwocats.progress.md.
--
-- RE-DRIVEN b64 (gate_crossings, matthew-mbp-m4-b63-seam1's grader): Burthorpe
-- and Taverley sit behind the members' wall, whose east gate (membergater
-- 2935,3450) is the only way on foot to Lumbridge, Varrock and the Sphinx
-- (reach.py 2938,3450 -> 2932,3450 NEEDS-DOOR via membergater; a flood from
-- either side never meets the other). Every crossing -- the first trip from
-- Lumbridge, out to Gertrude, the second search's way back in, out to the
-- Sphinx, out to the Apothecary, back in for the cure -- presses the gate
-- (`taverley_gate`, t.player.cross_gate). The Sphinx trip also crosses the
-- Shantay Pass doorway with a pass bought from Shantay. Audit:
-- build/orchestrator/fix_b64/atailoftwocats.progress.md.
--
-- The locator itself (twocats.rs2:248-458, cache IF1 group 48
-- bob_locator_amulet): `[opheld3,twocats_amuletofcatspeak]` fires the Open op
-- while the amulet sits UNWORN in the backpack; worn, the same verb is
-- `[inv_button2,wornitems:slot2]` ("Locate", op 2 on the equipment tab).
-- Either way it calls `~twocats_locator_refresh`, which points the nose at
-- `%twocats_locator_direction` (the cache-native dial, id 1034 -- lint's
-- compack check covers it) and only lights the eyes/mouth/meow
-- (`%twocats_locator_found`, a pack/varp.alloc test-only mirror OUTSIDE that
-- check, id 7166, section 8's closing bullet) once the dial's sector matches
-- `~twocats_bob_sector`'s read of Bob's LIVE position (or his home tile when
-- the engine has not stood him up this far from a player). Bob himself wanders
-- the whole world now (`[ai_timer,death_growncat_black]`, LostCity's
-- wanderMode written into content, twocats.rs2:487-515) -- up to
-- `^twocats_bob_step_ahead` (16) tiles toward a re-rolled target every
-- `^twocats_bob_roll_max` (1-15) ticks, home to his spawn after 500 stuck
-- ticks -- so a scripted run turns the whiskers until `%twocats_locator_found`
-- reads 1 (READ BEFORE any extra turn -- the rejected file's bug), reads
-- `%twocats_locator_direction`, moves that bearing (a walk while he is on
-- screen, else a goto between checked street waypoints), and repeats until
-- Bob is beside the player, then talks to him for real. `talkToBob`/`talkToBobAgain`/`talkToBobToFinish` retry the click a
-- few times and re-approach when he has stepped away meanwhile, because he
-- keeps wandering while the dialogue opens.
--
-- Content facts for every OTHER leg (read from the CURRENT twocats.rs2, cited
-- by line, unchanged from the reverted file's own citations -- the only diff
-- between that file's content pin (70487ebee8) and this run's (3b41349334) is
-- the locator/Bob section above, `git -C OSRS-Content diff 70487ebee8
-- 3b41349334 -- .../twocats.rs2`):
--
--   * Unferth's start (twocats.rs2:49-108): one trigger for BOTH
--     `twocats_unferth`/`twocats_unferth_bald`. `~twocats_has_cat` needs a
--     cat/kitten CARRIED (`~ratcatch_has_cat`, growncatobject family) --
--     Quest Helper's own `cat` FollowerItemRequirement, brought along, never
--     the quest's own deliverable. `~twocats_catspeak` needs the (regular or
--     enchanted) amulet WORN (`~ratcatch_has_catspeak_equipped`) at every
--     later talk, not just this one. The accept ends in a real
--     `~p_choice2_header("Yes.",1,"No.",2,...)` (trap: chat.play's
--     "choose:Yes." entry).
--
--   * Hild (twocats.rs2:178-246): the first visit (stage 5) writes stage 10
--     and then checks `inv_total(inv,deathrune) < 5` (twocats.rs2:198-202);
--     the five death runes are a Quest Helper bring-along for talkToHild
--     ("items: Death runes"), staged in SETUP, so the same visit runs on into
--     the atomic exchange (worn-or-carried amulet + 5 death runes + a free
--     slot -> the enchanted `twocats_amuletofcatspeak`, unworn, in the
--     backpack), which writes stage 15.
--
--   * Gertrude (twocats.rs2:678-717ish): reached through the BASE symbol
--     "gertrude" (areas/varrock/scripts/gertrude.rs2's own [opnpc1,gertrude],
--     trap 19 -- `gertrude_post` in twocats.rs2 is dead code, the spawn row
--     never carries that child) -> falls through `~gertrude_route_topics`
--     (Ratcatchers ineligible here) straight to `~twocats_gertrude_after_bob`,
--     writing stage 25.
--
--   * Reldo (areas/varrock/scripts/reldo.rs2's own [opnpc1,reldo] for the
--     whole 25..30 window): the cat-speech gate is the amulet WORN; unworn,
--     the topic-choice branch still opens but the cat's own reply lines are
--     swapped for the "meow" refusal and the stage never advances past 25 --
--     driven below as a deliberate negative check before re-equipping and
--     driving the real exchange (writes stage 30, %twocats_reldo=1).
--
--   * The Sphinx (dragonslayer2.rs2's [opnpc1,ics_little_sphinx] hands off to
--     @twocats_talk_to_sphinx): the verbatim hypnosis-and-reveal cutscene,
--     opened by a real `choose:Ask the Sphinx for help for Bob.` topic pick,
--     ending in `choose:Yes, teleport me to Unferth's house.` and the chores
--     objbox (stage 40). `*` marks a page whose exact text carries no
--     symbol/stage write to verify (pure Bob/Sphinx/Neite banter).
--
--   * Chores, all gated on stage==40 and unchained (any order): rake
--     (`[oplocu,twocats_patch]`, last_useitem=rake, a real
--     `stat_random(farming,64,255)` loop to tidygarden=3 -- can take many
--     ticks, not a single click), then potato_seed (tidygarden 3->4, arms
--     `[softtimer,twocats_potato_grow]`); the bed (`[oploc1,twocats_bed]`,
--     op 1, tidyhouse 0->1); the fireplace (`[oplocu,twocats_fireplace]`,
--     logs then tinderbox -- lighting the fire is a bare `anim`+`p_delay`
--     with NO chat line, so `use_on`'s own settle times out on success and
--     the row is graded on the varbit read back, not the click result); the
--     table (`[oplocu,twocats_table]`, cake then milk, the bucket returns
--     empty); shears on Unferth himself (`[opnpcu,twocats_unferth]`+hair
--     children, a `stat_random(crafting,64,255)` loop to tidyhuman=8, same
--     silent-anim shape as the fire). `[queue,twocats_chores_finished]` is the
--     single place every chore's completion funnels through, and only fires
--     once ALL FIVE thresholds are met -- the cat's own announcement is what
--     carries stage 40 -> 45, not a click. `[debugproc,twocats_growpotatoes]`
--     (docs/QUEST_SERVER_CHEATS.md section A, sanctioned GRIND fast-forward)
--     walks the SAME `[proc,twocats_potato_advance]` body the real ~2,500-
--     tick softtimer calls, once per remaining stage -- read back below.
--     Unferth's patch is reached through his house's own north door
--     (`poordoor`, `click_loc` op 1) from just inside it.
--
--   * The Apothecary (stage 50): a real three-way menu ("Can you make
--     potions...?" / "Talk about A Tail of Two Cats." / "Talk about something
--     else."), then a real hat CHOICE (`~p_choice2("Doctor's hat.",1,"Nurse
--     hat.",2)`) -- the player's pick is the ONLY source of either hat, and
--     `[queue,twocats_quest_complete]` grants NEITHER at completion (parity1n
--     "dropped the second, unsourced hat"). This file picks "Nurse hat." to
--     exercise the choice.
--
--   * The cure: `~twocats_disguised` needs the hat WORN, a desert shirt AND
--     desert robe (or the druid/gown alternates) both worn, and BOTH weapon
--     and shield slots empty (`inv_getobj(worn,^wearpos_rhand/lhand) = null`);
--     a vial of water carried. Any of those missing (this file proves it
--     holding a bronze sword) reads the transcript's one shared refusal,
--     "No you're not! A Doctor wouldn't be holding that!", and the stage does
--     not move. Unequipping the sword and re-talking runs the real 30-page
--     cure scene and writes stage 60.
--
--   * Completion: `[label,twocats_done]` (stage 65->70) queues
--     `[queue,twocats_quest_complete]`, which grants the `twocats_present` --
--     the SAME container Quest Helper's item rewards list; `[opheld1,
--     twocats_present]` opens it into 2 antique lamps and a mouse toy. No
--     `~<abbr>_journal` proc exists anywhere in this file (grepped empty) --
--     section 8's documented case for a quest whose journal channel can never
--     pass, so `quest.expect_complete()` is not called; the three rows it
--     WOULD have produced are hand-rolled per that recipe (varp_complete,
--     scroll_title, points), plus the reward rows.
return {
    id = "atailoftwocats",
    fixture = "fresh_lumbridge.ini",
    max_frames = 300000, -- three locator searches, every door on foot, six gate crossings
    setup = {
        "::clearinv", -- fourteen tutorial slots would otherwise sit in the way
        "::give ics_little_amulet_of_catspeak 1", -- Icthlarin's Little
        -- Helper's own reward item, worn to accept (twocats.rs2:62's
        -- ~twocats_catspeak reads WORN) and handed to Hild worn-or-carried.
        "::give growncatobject 1", -- Quest Helper's `cat` FollowerItemRequirement:
        -- "You must have a cat or kitten with you" (~twocats_has_cat reads
        -- ~ratcatch_has_cat, which accepts any grown-cat colour carried).
        "::give deathrune 5", -- Quest Helper's deathRune5 for talkToHild:
        -- carried on the first visit, so Hild enchants the amulet in that
        -- same conversation (twocats.rs2:198-202 -> @twocats_hild_enchant).
        "::complete quest_icthlarinslittlehelper", -- Quest Helper's real
        -- prerequisite (getGeneralRequirements). The cheat only sets
        -- %ics_little_var -- it grants no item -- so the amulet above is
        -- given separately.
        "::complete quest_gertrudescat", -- required for Gertrude's ladder to
        -- fall through to ~gertrude_route_topics at all (gertrude.rs2's own
        -- header comment).
        -- The step-40 chore kit -- each trigger names its own item and
        -- count (twocats.rs2, read beside each chore row below).
        "::give rake 1",
        "::give dibber 1",
        "::give potato_seed 4",
        "::give logs 1",
        "::give tinderbox 1",
        "::give chocolate_cake 1",
        "::give bucket_milk 1",
        "::give shears 1",
        -- The medical disguise for step 55: white robes worn, a vial of
        -- water carried and consumed (twocats.rs2:1459-1478). Brought-along
        -- kit per Quest Helper's item requirements, not the quest's own
        -- deliverable.
        "::give desert_shirt 1",
        "::give desert_robe 1",
        "::give vial_water 1",
        -- Test aid only, no guide step names it: a weapon to prove
        -- ~twocats_disguised's own "no weapon/shield equipped" clause
        -- (twocats.rs2:1503) refuses the cure, before it is taken back off
        -- and the real disguise is worn.
        "::give bronze_sword 1",
        -- Five coins for the Shantay pass bought from Shantay on the way to
        -- the Sphinx (shantay.rs2: 5 gp): the doorway is the desert's only
        -- way in on foot, and it takes a pass (shantay_pass.rs2 oploc1).
        "::give coins 5",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb1028_twocats_quest", -- a VARBIT (id 1028) packed into the
            -- carrier varp `[twocats]` (protect=no/transmit=yes/scope=perm,
            -- atailoftwocats.varp) -- t.quest.bind resolves either kind
            -- transparently (QUEST_AUTHORING.md section 3).
            constants = {
                not_started = 0,
                accepted = 5, -- twocats.rs2:107, after ~p_choice2_header
                hild_asked = 10, -- twocats.rs2:198, first Hild visit
                amulet_enchanted = 15, -- twocats.rs2:241, the atomic exchange
                bob_found = 20, -- twocats.rs2:612 (bob_talk_1)
                gertrude_done = 25, -- twocats.rs2:714
                reldo_done = 30, -- twocats.rs2:797
                bob_found_again = 35, -- twocats.rs2:612 (bob_talk_2 -- see below)
                chores_ready = 40, -- twocats.rs2:991-ish, end of Sphinx scene
                chores_done = 45, -- twocats.rs2:1009, [queue,twocats_chores_finished]
                apothecary_needed = 50, -- twocats.rs2:1365
                hat_given = 55, -- twocats.rs2:1425
                cured = 60, -- twocats.rs2:1489
                bob_home = 65, -- twocats.rs2:656 (bob_talk_3)
                complete = 70, -- twocats.rs2:1540, [label,twocats_done]
            },
            row = "quest_tailoftwocats",
            display = "A Tail of Two Cats",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- setup cheats (::give, ::complete) are not client-side yet
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Captured here (not exposed by quest.bind) for the hand-rolled
        -- quest.points row at completion -- section 8's recipe.
        local qp_before_result, qp_before = t.var.varp("varp101_qp")

        -- ---------------------------------------------------------------
        -- Travel (docs/QUEST_ORCHESTRATOR.md standing rules, 2026-10-03):
        -- a goto_tile departs only from an open, unroofed street tile and
        -- lands only on one; every door between the player and a target is
        -- clicked on foot, going in AND coming out. Rooms on this route, all
        -- from the map squares (m45_55.jl2, m49_53.jl2, m50_54.jl2,
        -- m51_43.jl2):
        --   Unferth's room x2917-2922 z3557-3561, poshdoor 2922,3558 (east
        --     edge) to the street, poordoor 2920,3561 (north edge) to his
        --     patch garden x2917-2922 z3562-3565, which death_fencing walls in
        --     on every other side: the patch is reached only from the house.
        --   Hild's house x2929-2934 z3565-3570, poordoor 2932,3564 (north edge
        --     of 3564).
        --   Gertrude's house x3148-3153 z3404-3411, fai_varrock_door 3151,3412.
        --   Varrock Castle: open arch 3212-3213,3471, then
        --     fai_varrock_castle_door 3215,3477 (south edge), 3214,3486 (north
        --     edge) and 3210,3490 (south edge) into the library.
        --   The Apothecary's shop x3192-3198 z3402-3406: doorway on the west
        --     edge of 3192,3403, its leaf placed OPEN (fai_varrock_door_open).
        --   Sophanem's walls: north gate sophanem_gate_left/right 3283/3284,2809
        --     (north edge; doors_selfstage.loc).
        -- ---------------------------------------------------------------
        local function tile_text(r, tt)
            if r == "ok" and type(tt) == "table" then
                return tt.x .. "," .. tt.z .. "," .. tt.level
            end
            return tostring(r)
        end

        local function at_tile(r, tt, x, z)
            return r == "ok" and type(tt) == "table" and tt.level == 0 and tt.x == x and tt.z == z
        end

        -- Walk to x,z and check the exact tile.
        local function walk_check(name, x, z, why)
            local wr = t.player.walk_to(x, z, 40)
            local tr, tt = t.world.tile()
            t.check(name, at_tile(tr, tt, x, z),
                "walk_to " .. x .. "," .. z .. " (" .. why .. ") -> " .. tostring(wr) .. "; at " .. tile_text(tr, tt))
        end

        -- Cross one door on foot (sampler-findings b56/b57 pass_door). Walk
        -- to the near tile and check it; if the CLOSED leaf stands on the door
        -- tile on this level, click that copy (op 1 Open); otherwise an
        -- earlier press left it open (a door swings back after 500 ticks), so
        -- assert the OPEN leaf on this level within 1 of the door tile -- a
        -- row that fails when neither leaf is there -- and never press it
        -- again. Then walk to the far tile and check it exactly. A selfstage
        -- door (Sophanem's gate) passes the same symbol twice.
        local function pass_door(prefix, closed_sym, open_sym, door_x, door_z, near_x, near_z, far_x, far_z, far_desc)
            t.player.walk_to(near_x, near_z, 30)
            local nr, nt = t.world.tile()
            local level = (nr == "ok" and type(nt) == "table") and nt.level or 0
            t.check(prefix .. ".atDoor", at_tile(nr, nt, near_x, near_z),
                "walked to " .. near_x .. "," .. near_z .. ",0 beside " .. closed_sym .. " at " .. door_x .. "," .. door_z
                    .. " -> " .. tile_text(nr, nt))
            local cr, cd = t.world.loc_near(closed_sym, 1)
            if cr == "ok" and cd.tile_x == door_x and cd.tile_z == door_z and cd.level == level then
                t.exec(prefix .. ".openDoor", t.player.click_loc, closed_sym, 1, { at = { door_x, door_z } })
                t.ticks(1)
            else
                local orr, od = t.world.loc_near(open_sym, 2)
                t.check(prefix .. ".doorStandsOpen", orr == "ok" and od.level == level
                        and math.abs(od.tile_x - door_x) <= 1 and math.abs(od.tile_z - door_z) <= 1,
                    closed_sym .. " at " .. door_x .. "," .. door_z .. ": "
                        .. (cr == "ok" and ("nearest closed copy at " .. cd.tile_x .. "," .. cd.tile_z .. "," .. cd.level) or tostring(cr))
                        .. "; " .. open_sym .. ": "
                        .. (orr == "ok" and ("open leaf at " .. od.tile_x .. "," .. od.tile_z .. "," .. od.level) or tostring(orr))
                        .. " (want the open leaf on level " .. level .. " within 1 of the door tile: standing open, walked through, not pressed again)")
            end
            local wr = t.player.walk_to(far_x, far_z, 20)
            local fr, ft = t.world.tile()
            t.check(prefix .. ".throughDoor", at_tile(fr, ft, far_x, far_z),
                "walked through " .. closed_sym .. " to " .. far_x .. "," .. far_z .. " (" .. far_desc .. ") -> "
                    .. tile_text(fr, ft) .. " (walk " .. tostring(wr) .. ")")
        end

        -- The street tiles a travel goto may leave from or land on. Every one
        -- is a level-0 tile that a flood from Bob's home 2924,3565 or Varrock
        -- square 3212,3428 reaches with EVERY closed door shut, all eight
        -- neighbours too, none under a roof (jm2 f4 clear): a 16-tile lattice
        -- over x2840-3240 z3380-3640, the Wilderness (x >= 2945, z >= 3520)
        -- cut out, plus the door-exit tiles below. Generated by
        -- build/orchestrator/fix_b58/atailoftwocats_tools/flood.py (GRID=16
        -- SNAP=6) over reach.py's static collision.
        local WAYPOINTS = {}
        for _, line in ipairs({
            "2851:3427 2848:3440 2848:3456 2848:3472 2848:3488 2849:3504 2853:3557 2852:3572 2846:3582 ",
            "2867:3421 2865:3439 2864:3456 2862:3474 2863:3485 2866:3505 2862:3524 2869:3535 2859:3557 ",
            "2862:3566 2880:3394 2880:3408 2877:3421 2879:3442 2882:3458 2880:3472 2879:3488 2878:3505 ",
            "2877:3517 2878:3534 2880:3552 2880:3568 2886:3578 2895:3381 2895:3391 2895:3407 2896:3421 ",
            "2896:3440 2896:3456 2895:3471 2895:3487 2894:3502 2898:3517 2896:3536 2896:3552 2897:3571 ",
            "2895:3579 2907:3381 2908:3388 2907:3409 2910:3425 2908:3436 2912:3456 2915:3476 2910:3486 ",
            "2912:3504 2912:3520 2910:3534 2908:3556 2912:3568 2923:3381 2928:3408 2927:3424 2927:3439 ",
            "2928:3456 2928:3472 2927:3487 2926:3502 2925:3523 2928:3536 2926:3554 2927:3567 2939:3381 ",
            "2944:3390 2947:3410 2942:3422 2942:3438 2944:3456 2944:3472 2944:3488 2949:3504 2939:3514 ",
            "2955:3381 2960:3392 2960:3408 2959:3423 2957:3437 2961:3459 2960:3472 2963:3487 2957:3501 ",
            "2976:3392 2976:3408 2976:3424 2976:3440 2978:3454 2974:3470 2974:3487 2976:3504 2977:3519 ",
            "2995:3381 2990:3394 2992:3408 2992:3422 2988:3441 2996:3456 2989:3474 2994:3491 2992:3504 ",
            "2996:3518 3008:3381 3007:3391 3008:3408 3008:3423 3005:3442 3007:3456 3008:3472 3007:3490 ",
            "3009:3502 3023:3382 3024:3392 3024:3407 3024:3424 3024:3439 3025:3454 3026:3473 3024:3485 ",
            "3025:3503 3042:3390 3037:3409 3040:3424 3040:3440 3040:3456 3042:3471 3040:3488 3040:3503 ",
            "3038:3518 3056:3392 3055:3409 3056:3424 3058:3437 3057:3455 3055:3473 3054:3489 3057:3503 ",
            "3055:3519 3067:3381 3072:3392 3072:3408 3070:3422 3072:3440 3072:3456 3071:3470 3072:3486 ",
            "3072:3504 3073:3519 3088:3392 3089:3408 3086:3422 3087:3439 3087:3457 3089:3471 3088:3488 ",
            "3088:3504 3087:3519 3104:3393 3104:3406 3101:3426 3100:3439 3106:3459 3107:3469 3104:3488 ",
            "3104:3504 3103:3519 3123:3395 3120:3408 3122:3424 3119:3439 3121:3457 3118:3474 3117:3491 ",
            "3118:3503 3119:3519 3133:3381 3135:3393 3135:3407 3136:3424 3135:3439 3136:3456 3136:3472 ",
            "3135:3487 3136:3504 3136:3519 3152:3381 3152:3392 3148:3413 3152:3424 3149:3443 3151:3455 ",
            "3153:3474 3152:3488 3152:3504 3152:3519 3174:3381 3168:3392 3169:3407 3169:3424 3168:3440 ",
            "3168:3456 3168:3472 3168:3504 3167:3519 3179:3381 3180:3388 3183:3407 3183:3423 3178:3434 ",
            "3183:3455 3184:3472 3184:3488 3182:3506 3184:3519 3197:3381 3201:3391 3199:3408 3199:3423 ",
            "3199:3439 3200:3456 3198:3470 3199:3487 3200:3503 3199:3519 3211:3381 3215:3391 3219:3406 ",
            "3216:3424 3218:3442 3214:3454 3212:3466 3218:3502 3214:3519 3231:3391 3231:3409 3228:3420 ",
            "3234:3438 3231:3455 3236:3471 3231:3488 3233:3506 3231:3519 2925:3558 2923:3558 2932:3563 ",
            "2924:3565 3151:3414 3151:3413 3190:3403 ",
        }) do
            for wx, wz in line:gmatch("(%d+):(%d+)") do
                WAYPOINTS[#WAYPOINTS + 1] = { x = tonumber(wx), z = tonumber(wz) }
            end
        end
        local function dist(ax, az, bx, bz)
            return math.sqrt((ax - bx) * (ax - bx) + (az - bz) * (az - bz))
        end

        -- The Taverley members' wall splits the waypoints in two (owner ruling,
        -- sampler-findings "Sample matthew-mbp-m4-b59" (a): a gate that is the
        -- only way on foot between two regions is pressed on every crossing).
        -- A flood over reach.py's static collision with every door closed
        -- (x2780-3280 z3340-3680) from Bob's street 2925,3558 (16,152 tiles)
        -- and from Varrock square 3212,3428 (41,321 tiles) never meets; these
        -- 77 waypoints are on the Taverley/Burthorpe side, the other 179 on
        -- the Varrock side. Every walk between the two goes through
        -- membergater/membergatel 2935,3450-3451 (reach.py 2938,3450 ->
        -- 2932,3450: NEEDS-DOOR via membergater).
        local INSIDE = {}
        for wx, wz in ([[2851:3427 2848:3440 2848:3456 2848:3472 2848:3488 2849:3504 2853:3557 2852:3572
            2846:3582 2867:3421 2865:3439 2864:3456 2862:3474 2863:3485 2866:3505 2862:3524 2869:3535
            2859:3557 2862:3566 2880:3394 2880:3408 2877:3421 2879:3442 2882:3458 2880:3472 2879:3488
            2878:3505 2877:3517 2878:3534 2880:3552 2880:3568 2886:3578 2895:3381 2895:3391 2895:3407
            2896:3421 2896:3440 2896:3456 2895:3471 2895:3487 2894:3502 2898:3517 2896:3536 2896:3552
            2897:3571 2895:3579 2907:3381 2908:3388 2907:3409 2910:3425 2908:3436 2912:3456 2915:3476
            2910:3486 2912:3504 2912:3520 2910:3534 2908:3556 2912:3568 2923:3381 2928:3408 2927:3424
            2927:3439 2928:3456 2928:3472 2927:3487 2926:3502 2925:3523 2928:3536 2926:3554 2927:3567
            2942:3422 2942:3438 2925:3558 2923:3558 2932:3563 2924:3565]]):gmatch("(%d+):(%d+)") do
            INSIDE[wx .. ":" .. wz] = true
        end
        -- The side of the wall a tile is on: the side of its nearest waypoint
        -- (every departure and landing below IS a waypoint or a door tile
        -- beside one).
        local function inside_taverley(x, z)
            local best, best_d = nil, nil
            for _, w in ipairs(WAYPOINTS) do
                local d = dist(x, z, w.x, w.z)
                if best_d == nil or d < best_d then
                    best, best_d = w, d
                end
            end
            return best ~= nil and INSIDE[best.x .. ":" .. best.z] == true
        end

        -- Cross the Taverley members' wall by its east gate, pressed on every
        -- crossing (t.player.cross_gate; gates.rs2 [label,member_fencegate_try]
        -- walk-through: nothing stays open, so each crossing is a press graded
        -- on the tiles before and after). Travel to the open tile two short of
        -- it on this side (2938,3450 outside / 2932,3450 inside, both in their
        -- side's flood), press membergater 2935,3450 from the tile beside it,
        -- walk on to the open tile beyond.
        local TAVERLEY_GATE = {
            ["in"] = { stage = { 2938, 3450 }, near = { 2936, 3450 }, far = { 2933, 3450 },
                far_ok = function(tile) return tile.x <= 2935 end,
                far_desc = "inside Taverley's east wall, x <= 2935" },
            out = { stage = { 2932, 3450 }, near = { 2934, 3450 }, far = { 2938, 3450 },
                far_ok = function(tile) return tile.x >= 2936 end,
                far_desc = "outside Taverley's east wall, x >= 2936" },
        }
        local function taverley_gate(prefix, way)
            local g = TAVERLEY_GATE[way]
            t.exec("goto-" .. prefix .. ".memberGate", t.player.goto_tile, g.stage[1], g.stage[2], 0)
            t.exec(prefix .. ".memberGate", t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 },
                near = g.near, far = g.far, far_ok = g.far_ok, far_desc = g.far_desc })
        end

        -- Before a goto from wherever the last walk left the player (beside
        -- Bob, say): walk to the nearest waypoint (trying the three nearest)
        -- and check the tile exactly. A walk crosses no closed door, so a
        -- player who somehow stood indoors either walks out through an open
        -- doorway or fails this row.
        local function depart_open(name)
            local r, here = t.world.tile()
            local cands = {}
            if r == "ok" and type(here) == "table" then
                for _, w in ipairs(WAYPOINTS) do
                    cands[#cands + 1] = { w = w, d = dist(here.x, here.z, w.x, w.z) }
                end
                table.sort(cands, function(a, b) return a.d < b.d end)
            end
            local tr, tt = r, here
            local used = nil
            local walked = false
            for i = 1, math.min(3, #cands) do
                used = cands[i].w
                if not at_tile(tr, tt, used.x, used.z) then
                    t.player.walk_to(used.x, used.z, 30)
                    walked = true
                    tr, tt = t.world.tile()
                end
                if at_tile(tr, tt, used.x, used.z) then
                    break
                end
            end
            local ok = used ~= nil and at_tile(tr, tt, used.x, used.z)
            local detail = "departure on an open, unroofed street waypoint: from " .. tile_text(r, here) .. " to waypoint "
                .. (used and (used.x .. "," .. used.z) or "none") .. " -> " .. tile_text(tr, tt)
            if walked then
                t.check(name, ok, detail) -- the walk changed the screen: photograph it
            else
                t.step(name, ok and "PASS" or "FAIL", detail .. " (already standing on it)")
            end
        end

        -- The catspeak amulet (e)'s locator (twocats.rs2:248-458, IF1 group 48
        -- bob_locator_amulet), used for real: open it (Open, unworn
        -- [opheld3]; Locate, worn [inv_button2,wornitems:slot2]), press the
        -- right whisker until %twocats_locator_found reads 1 -- read BEFORE
        -- any extra turn -- read %twocats_locator_direction (1 = north,
        -- clockwise to 8 = north-west), one more turn tells "near" (lit in
        -- every direction), close it, and move that way. Bob wanders
        -- ([ai_timer,death_growncat_black]), so it repeats until he is
        -- beside the player. While he is on screen (the npc list, 15 tiles)
        -- the move is a WALK 8 tiles along the dial -- a walk crosses no
        -- closed door; off screen it is travel: a goto from a checked
        -- waypoint to the waypoint nearest a point 30 tiles along the dial.
        local DIRS = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 1, -1 }, { 0, -1 }, { -1, -1 }, { -1, 0 }, { -1, 1 } }
        local function locate_bob(step, worn)
            local in_view = false
            for hop = 1, 40 do
                if worn then
                    t.ui.tab("equipment")
                    t.ticks(2)
                    local _, slot = t.ui.widget("wornitems:slot2")
                    if slot then
                        t.ui.invoke(slot, 2)
                    end
                else
                    t.exec(step .. ".open." .. hop, t.player.inv_op, "twocats_amuletofcatspeak", 3)
                end
                local mounted = t.ui.await_open("bob_locator_amulet", 10)
                t.check(step .. ".mounted." .. hop, mounted == "ok",
                    (worn and "worn Locate (wornitems:slot2 op 2)" or "Open (opheld3)")
                        .. " -> await_open(bob_locator_amulet) " .. tostring(mounted))
                local _, r_whisker = t.ui.widget("bob_locator_amulet:bob_locator_r_whisker")
                local found, turns = 0, 0
                for i = 1, 17 do
                    t.ticks(2)
                    local fr, fv = t.var.server("varp7166_twocats_locator_found")
                    if fr == "ok" and fv == 1 then
                        found = 1
                        break
                    end
                    if r_whisker then
                        t.ui.invoke(r_whisker, 1)
                    end
                    turns = turns + 1
                end
                local _, dir = t.var.server("varb1034_twocats_locator_direction")
                local hr, here = t.world.tile()
                t.check(step .. ".dial_lit." .. hop, found == 1 and hr == "ok",
                    "turned the right whisker " .. turns .. " time(s); found=" .. found
                        .. " direction=" .. tostring(dir) .. " from " .. tile_text(hr, here))
                local near = false
                if found == 1 and r_whisker then
                    t.ui.invoke(r_whisker, 1)
                    t.ticks(2)
                    local nr, nv = t.var.server("varp7166_twocats_locator_found")
                    near = (nr == "ok" and nv == 1)
                end
                t.key("escape")
                local closed = t.ui.await_close("bob_locator_amulet", 10)
                t.check(step .. ".closed." .. hop, closed == "ok",
                    "await_close(bob_locator_amulet) -> " .. tostring(closed))
                if found ~= 1 or hr ~= "ok" or type(dir) ~= "number" or dir < 1 or dir > 8 then
                    break
                end
                if near then
                    t.note(step .. ": lit in every direction (near) after " .. (hop - 1) .. " move(s), at " .. tile_text(hr, here))
                    in_view = true
                    break
                end
                local vr, vrow = t.npc.nearest("death_growncat_black", 3)
                if vr == "ok" then
                    t.note(step .. ": Bob beside the player after " .. (hop - 1) .. " move(s), at "
                        .. tostring(vrow.x) .. "," .. tostring(vrow.z) .. ", dial " .. dir)
                    in_view = true
                    break
                end
                local dx, dz = DIRS[dir][1], DIRS[dir][2]
                local sr, srow = t.npc.nearest("death_growncat_black", 15)
                if sr == "ok" then
                    t.player.walk_to(here.x + dx * 8, here.z + dz * 8, 12)
                    local ar, at = t.world.tile()
                    local moved = (ar == "ok") and (math.abs(at.x - here.x) + math.abs(at.z - here.z)) or 0
                    if moved < 2 then
                        -- a wall that way: let the route find its own way
                        -- round to where he stands
                        t.player.walk_to(srow.x, srow.z, 12)
                        ar, at = t.world.tile()
                    end
                    t.note(step .. " move " .. hop .. ": walked along dial " .. dir .. " from " .. tile_text(hr, here)
                        .. " to " .. tile_text(ar, at) .. " (Bob on screen at " .. tostring(srow.x) .. "," .. tostring(srow.z) .. ")")
                else
                    depart_open(step .. ".depart." .. hop)
                    local fr, from = t.world.tile()
                    local fx = (fr == "ok") and from.x or here.x
                    local fz = (fr == "ok") and from.z or here.z
                    local px, pz = fx + dx * 30, fz + dz * 30
                    local best, best_d = nil, nil
                    for _, w in ipairs(WAYPOINTS) do
                        if dist(fx, fz, w.x, w.z) >= 12 then
                            local d = dist(px, pz, w.x, w.z)
                            if best_d == nil or d < best_d then
                                best, best_d = w, d
                            end
                        end
                    end
                    -- A hop to the other side of the Taverley wall goes
                    -- through its gate, pressed (Bob lives on the Taverley
                    -- side; a search begun in Varrock crosses it once).
                    local from_in = inside_taverley(fx, fz)
                    local to_in = INSIDE[best.x .. ":" .. best.z] == true
                    if from_in ~= to_in then
                        taverley_gate(step .. ".wall." .. hop, to_in and "in" or "out")
                    end
                    t.exec(step .. ".goto." .. hop, t.player.goto_tile, best.x, best.z, 0)
                end
            end
            t.check(step, in_view, "Bob beside the player: " .. tostring(in_view))
        end

        -- Reach Bob: talk_to routes to wherever he stands now; a press that
        -- lands where he has just left ("no dialogue") or a route a wall
        -- stops ("I can't reach that!") is retried after a WALK to his
        -- current tile (he wanders on every 1-15 ticks). Never a goto.
        local function talk_to_bob(step)
            local r, d
            local tries = 0
            for attempt = 1, 12 do
                tries = attempt
                r, d = t.player.talk_to("death_growncat_black", 1)
                if r == "ok" and not tostring(d):find("no dialogue") then
                    break
                end
                local vr, vrow = t.npc.nearest("death_growncat_black", 15)
                if vr == "ok" then
                    t.player.walk_to(vrow.x, vrow.z, 8)
                else
                    t.ticks(3)
                end
            end
            t.check(step, r == "ok" and not tostring(d):find("no dialogue"),
                "attempt " .. tries .. ": " .. tostring(r) .. " " .. tostring(d))
        end

        t.exec("amulet.equip", t.player.equip, "ics_little_amulet_of_catspeak")

        -- talkToUnferth: NE Burthorpe, Unferth in his room (2919,3559). The first
        -- goto of the run leaves the fixture's open field north of Lumbridge
        -- castle (3206,3233; comp.py: a 2,325-tile open component) for the
        -- open ground east of Taverley's members' gate (reach.py 3206,3233 ->
        -- 2936,3450 REACH closed-doors len=517); the gate is pressed, then
        -- travel inside the wall to the street east of his house (reach.py
        -- 2933,3450 -> 2925,3558 REACH len=136); poshdoor is opened on foot.
        -- Verbatim accept scene, ending in the real Yes/No confirm.
        taverley_gate("unferth", "in")
        t.exec("goto-unferth", t.player.goto_tile, 2925, 3558, 0)
        pass_door("unferthIn", "poshdoor", "poshdooropen", 2922, 3558, 2923, 3558, 2921, 3558, "inside Unferth's room")
        t.exec("talkToUnferth", t.player.talk_to, "twocats_unferth", 1)
        t.exec("talkToUnferth-dialog", t.chat.play, {
            "npc:Hello, I see you have a cat.",
            "npc:Meow.",
            "player:Indeed I do! Do you have a cat?",
            "npc:I do! His name is Bob but I can't find him!",
            "player:Oh dear...",
            "npc:I haven't seen him for a week! What am I to do?!",
            "npc:What a wet blanket, Bob can look after himself.",
            "player:I know puss, but he is in distress.",
            "npc:Whaa...",
            "player:Never mind.",
            "choose:Yes.",
            "player:Tell you what. I'll help you!",
            "npc:Would you? Oh that would be so great!",
            "npc:I don't want to leave the house in case Bob come",
            "npc:He must be hungry by now!",
            "npc:This guy gets on my nerves.",
            "player:Shh!",
            "npc:Huh?",
            "player:Er... Where did you see Bob last?",
            "npc:Well he usually comes back to me once a week. Us",
            "player:I wonder why it's on a Wednesday.",
            "npc:Hmph, well, this week he hasn't come back for hi",
            "player:So he could be anywhere?",
            "npc:I guess so.",
            "npc:I've just had an idea!",
            "npc:Oh my, you win... a biscuit!",
            "player:Puss!",
            "npc:Do you want to help or not?!",
            "player:I said I would, didn't I?",
            "npc:Ok.",
            "npc:My friend Hild is cleverer than me...",
            "npc:ANYONE would be 'cleverer'!",
            "player:Hehe!",
            "npc:This isn't funny!",
            "player:Sorry!",
            "npc:Where was I? Oh yes... my friend Hild might be a",
            "player:Ok. I'll go speak to her!",
            "npc:Please hurry!",
        })
        t.expect("quest.stage.accepted", t.quest.expect_stage("accepted"))

        -- talkToHild: out of Unferth's by poshdoor, up the street to Hild's
        -- door (poordoor 2932,3564) and in. The five death runes are carried
        -- (setup), so stage 5 -> 10 -> 15 in one conversation: twocats.rs2:198
        -- writes 10, :199 finds the runes, @twocats_hild_enchant writes 15.
        pass_door("unferthOut", "poshdoor", "poshdooropen", 2922, 3558, 2921, 3558, 2923, 3558, "the street east of Unferth's")
        pass_door("hildIn", "poordoor", "poordooropen", 2932, 3564, 2932, 3564, 2932, 3566, "inside Hild's house")
        t.exec("talkToHild", t.player.talk_to, "death_woman_indoors1", 1)
        t.exec("talkToHild-dialog", t.chat.play, {
            "npc:Greetings Adventurer, why have you come to me in",
            "player:Greetings Hild, I am trying to find Unferth's ca",
            "npc:Unferth's cat? Pfft! Unferth is Bob's human!",
            "player:What are you talking about?",
            "npc:Your cat speaks some truth, it is humans that mi",
            "player:You understand what my cat is saying?!",
            "npc:Indeed.",
            "player:Er...",
            "npc:Bob didn't come home this week.",
            "npc:Hmm. This is not like Bob, I wonder what could b",
            "npc:I don't think there is a problem, Bob is probabl",
            "player:Hello? I'm still here!",
            "npc:I am sorry ",
            "player:Ah... good! He could be anywhere though!",
            "npc:I see you have an Amulet of Catspeak. If you ope",
            "npc:then I will be able to perform the enchantment f",
            "player:I have the death runes with me now.",
            "npc:Good. Give me the amulet so that I may perform t",
            "*",
            "*",
            "npc:Using the enchanted amulet is easy; open up the ",
            "npc:direction that Bob is in, the eyes will light up",
            "npc:matter which way the nose is pointing. That's al",
            "player:Thanks!",
        })
        t.expect("quest.stage.amulet_enchanted", t.quest.expect_stage("amulet_enchanted"))
        t.expect("hild.amulet_e", t.inv.expect_has("twocats_amuletofcatspeak", 1))
        t.expect("hild.runes_taken", t.inv.expect_absent("deathrune"))

        -- Out of Hild's house on foot before the search begins.
        pass_door("hildOut", "poordoor", "poordooropen", 2932, 3564, 2932, 3565, 2932, 3563, "the street south of Hild's")

        -- findBob: the amulet (e) is still unworn in the backpack -> Open.
        locate_bob("findBob", false)

        -- Wear the enchanted amulet: every talk from here needs it WORN
        -- (~twocats_catspeak), and worn also switches the locator's own verb
        -- from inv_op (Open) to the worn-tab "Locate" used by the later legs.
        t.exec("amulet_e.equip", t.player.equip, "twocats_amuletofcatspeak")

        -- talkToBob
        talk_to_bob("talkToBob")
        t.exec("talkToBob-dialog", t.chat.play, {
            "player:Bob! I've found you at last!",
            "npc:Hi Bob!",
            "npc:Hi there son.",
            "player:Don't start that again!",
            "npc:The humans have been looking for you, they get w",
            "npc:Ah. I should've realised Unferth would miss me.",
            "npc:What's up?",
            "npc:Sigh. I can't believe it but I've fallen for Nei",
            "player:Neite...now, I'm sure I've heard that name befor",
            "npc:What a beauty she is! It's the way the shimmer f",
            "npc:would say, she's the cat's whiskers. Oh how I lo",
            "npc:Wow! He's got it bad! Real bad! I never thought ",
            "npc:I know but there is something about her. The way",
            "player:I'm starting to feel sick...",
            "npc:I haven't been home because I've wanted to be al",
            "npc:Does Neite feel the same way about you?",
            "npc:She said she has feelings for me but would never",
            "npc:Don't you know who your parents are?",
            "npc:All I know is that Gertrude found me on her doorstep",
            "player:The crazy cat lady?",
            "npc:Don't you have any memory of your parents?",
            "npc:Nothing! The furthest back I can remember is Ger",
            "npc:*",
            "player:Do I look like a match maker?!",
            "npc:Come on! We can help Bob!",
        })
        t.expect("quest.stage.bob_found", t.quest.expect_stage("bob_found"))

        -- talkToGertrude: west of Varrock, BASE symbol "gertrude" (trap 19), in
        -- her house behind fai_varrock_door 3151,3412. The goto leaves from a
        -- checked street waypoint beside wherever Bob was talked to (the
        -- Taverley side: Bob is a Burthorpe cat and an npc cannot work the
        -- members' gate), out through the gate, then travel to her doorstep
        -- (reach.py 2936,3450 -> 3151,3414 REACH len=267).
        depart_open("depart.gertrude")
        local _, gertrude_from = t.world.tile()
        if type(gertrude_from) ~= "table" or inside_taverley(gertrude_from.x, gertrude_from.z) then
            taverley_gate("gertrude", "out")
        end
        t.exec("goto-gertrude", t.player.goto_tile, 3151, 3414, 0)
        pass_door("gertrudeIn", "fai_varrock_door", "fai_varrock_door_open", 3151, 3412, 3151, 3413, 3151, 3411, "inside Gertrude's house")
        t.exec("talkToGertrude", t.player.talk_to, "gertrude", 1)
        t.exec("talkToGertrude-dialog", t.chat.play, {
            "choose:Ask about Bob's parents.",
            "player:Hello again Gertrude!",
            "npc:Welcome back adventurer! How is your cat?",
            "npc:I'm fine thanks.",
            "player:He says he's fine.",
            "npc:What can I do for you then? I hope it's not abou",
            "player:Death runes? No idea.... No, it's not that. I, e",
            "npc:Come on, spit it out.",
            "player:This is going to sound silly...",
            "npc:Why, are you going to talk in a stupid accent? G",
            "player:Ok... Well... Do you know who Bob's parents are?",
            "npc:Bob.... you mean the big tomcat who hangs around",
            "npc:a kitten inside it left on my doorstep. I brough",
            "npc:We don't have time for this, Bob could be in tro",
            "npc:Crumbs! Your cat's quite noisy isn't it? Is ever",
            "player:No, it's erm... It needs to be taken to the vets",
            "npc:I do hope you're joking. These claws are real yo",
            "player:See, it just can't stop howling. Allergic to cat",
            "npc:Oh was I... Oh yes, well, I'm afraid I don't kno",
            "npc:Hang on, maybe this is something to do with the ",
            "player:Vaguely?",
            "npc:Ask Gertrude if she knows anything.",
            "player:Hmm... Gertrude, do you know anything of the leg",
            "npc:No, sorry. If it's a legend, it's Reldo you need",
            "npc:Good old Gertrude! Come on, lets go.",
            "player:But...",
            "npc:I'll explain later!",
        })
        t.expect("quest.stage.gertrude_done", t.quest.expect_stage("gertrude_done"))
        pass_door("gertrudeOut", "fai_varrock_door", "fai_varrock_door_open", 3151, 3412, 3151, 3411, 3151, 3413, "the street south of Gertrude's")

        -- talkToReldo: Varrock Castle library (3209,3495). A deliberate
        -- negative check first -- unworn amulet -- content's own "meow"
        -- refusal and the stage staying 25 (twocats.rs2:775-778) -- then
        -- re-equip and drive the real exchange (writes 30).
        -- From Gertrude's doorstep (checked above) to the street south of the
        -- castle's open arch, then the three castle doors on foot.
        t.exec("goto-reldo", t.player.goto_tile, 3212, 3466, 0)
        pass_door("castleIn1", "fai_varrock_castle_door", "fai_varrock_castle_door_open", 3215, 3477, 3215, 3476, 3215, 3478, "the castle's east hall")
        pass_door("castleIn2", "fai_varrock_castle_door", "fai_varrock_castle_door_open", 3214, 3486, 3214, 3486, 3214, 3487, "the corridor south of the library")
        pass_door("castleIn3", "fai_varrock_castle_door", "fai_varrock_castle_door_open", 3210, 3490, 3210, 3489, 3210, 3491, "inside the library")
        t.exec("amulet_e.unequip", t.player.unequip, "twocats_amuletofcatspeak")
        t.exec("reldo.unworn", t.player.talk_to, "reldo", 1)
        t.exec("reldo.unworn-dialog", t.chat.play, {
            "npc:Hello there",
            "choose:I have a cat related question.",
            "player:I have a cat related question.",
            "npc:Yes?",
            "player:Well...",
            "npc:Meow.",
            "player:Hmm... My cat is trying to say something",
        })
        t.expect("reldo.unworn_stays_gertrude_done", t.var.await_server("varb1028_twocats_quest", 25, 3))
        t.exec("amulet_e.equip2", t.player.equip, "twocats_amuletofcatspeak")
        t.exec("talkToReldo", t.player.talk_to, "reldo", 1)
        t.exec("talkToReldo-dialog", t.chat.play, {
            "npc:Hello there",
            "choose:I have a cat related question.",
            "player:I have a cat related question.",
            "npc:Yes?",
            "player:Well...",
            "npc:Go on... ask him.",
            "player:Okay, okay. I'm on it.",
            "player:Do you know anything about Robert the Strong?",
            "npc:Ah, Robert the Strong. There's some details on h",
            "npc:Fourth Age... Popular Lore... Ah here we are, Ro",
            "npc:'Not much is known about the hero called Robert ",
            "npc:'He wields a six foot tall longbow and travels w",
            "player:Dragonkin?",
            "npc:I'm coming to that.",
            "npc:'The stories tell of the dragonkin being an inte",
            "npc:'Because of this, they became very afraid of dea",
            "npc:'Some even say the dragonkin made corrupted vers",
            "player:Hmm... Doesn't sound very believable.",
            "npc:There's no doubt that some elements of these tal",
            "npc:Robert the Strong is a name that often comes up ",
            "player:Thank you Reldo, you've been most... helpful.",
            "player:That was a nice tale for mothers to tell their c",
            "npc:Don't you think it's odd that no one knows where",
            "player:I guess, but he's just a cat... right?",
            "npc:I'd hoped you would've understood by now that th",
            "player:Ok, ok! I get your point, after all I'm being dr",
            "npc:I wouldn't say dragged... let's call it a partne",
            "player:Ok!",
            "player:So are you saying that Robert the Strong is... i",
            "player:Bob?!",
            "npc:Let's ask Bob!",
            "player:Let's go!",
        })
        t.expect("quest.stage.reldo_done", t.quest.expect_stage("reldo_done"))
        t.expect("reldo.withbook", t.var.await_server("varb1036_twocats_reldo", 1, 3))

        -- Out of the castle the way in, door by door, to the street.
        pass_door("castleOut3", "fai_varrock_castle_door", "fai_varrock_castle_door_open", 3210, 3490, 3210, 3491, 3210, 3489, "the corridor south of the library")
        pass_door("castleOut2", "fai_varrock_castle_door", "fai_varrock_castle_door_open", 3214, 3486, 3214, 3487, 3214, 3486, "the castle's east hall")
        pass_door("castleOut1", "fai_varrock_castle_door", "fai_varrock_castle_door_open", 3215, 3477, 3215, 3478, 3215, 3476, "the entrance hall")
        walk_check("castleOut.street", 3212, 3466, "the street south of the castle arch")

        -- findBobAgain: the WORN "Locate" verb (equipment tab -> wornitems
        -- slot2 op 2, [inv_button2,wornitems:slot2]), the amulet worn since
        -- the Reldo re-equip above.
        locate_bob("findBobAgain", true)

        -- talkToBobAgain
        talk_to_bob("talkToBobAgain")
        t.exec("talkToBobAgain-dialog", t.chat.play, {
            "player:Hi Bob!",
            "npc:Did you find out who my parents are?",
            "player:Not exactly...",
            "npc:We think you are... or used to be... Robert the ",
            "npc:Robert the who?",
            "player:I thought this was stupid.",
            "npc:Robert the Strong was a great hero, you have no ",
            "npc:No... I'm afraid not.",
            "npc:Does a black panther called Odysseus ring any be",
            "npc:No...",
            "npc:What about the dragonkin? A vicious race of bird",
            "npc:Nothing...",
            "player:Looks like theres nothing else we can do puss.",
            "npc:I'm not done yet!",
            "npc:Do you remember when you were hypnotised by the ",
            "player:Well no... I was hypnotised!",
            "npc:Hehe!",
            "npc:The Sphinx understood how you were hypnotised, i",
            "player:Sounds crazy... guess it might work!",
            "npc:Bye for now Bob, we're going to speak to the Sph",
            "npc:Bye.",
        })
        t.expect("quest.stage.bob_found_again", t.quest.expect_stage("bob_found_again"))

        -- talkToSphinx: Sophanem (3300,2784). The verbatim hypnosis-and-
        -- reveal cutscene, ending in the Burthorpe teleport offer and the
        -- chores objbox.
        -- The trip: out of Taverley by its members' gate (Bob was found on its
        -- side), travel to the Shantay Pass (reach.py 2936,3450 -> 3304,3123
        -- REACH len=717), a pass bought from Shantay, the pass's doorway --
        -- the desert's only way in on foot (comp.py from 3304,3123 at margin
        -- 80: the Al Kharid side, 5,113 tiles, never reaches the desert; from
        -- 3304,3110 the desert joins 3284,2812) -- then travel south (reach.py
        -- 3304,3115 -> 3284,2812 REACH len=443). Desert heat: the doorway
        -- starts a 150-tick timer (desert_heat.rs2) that only bites inside
        -- desert_zones (mapsquares z 46-48); Sophanem (z 43-44) is outside
        -- them, so the timer clears itself on its first fire.
        -- Sophanem is walled: the goto lands on the desert outside its north
        -- gate and the gate is opened on foot.
        depart_open("depart.sphinx")
        local _, sphinx_from = t.world.tile()
        if type(sphinx_from) ~= "table" or inside_taverley(sphinx_from.x, sphinx_from.z) then
            taverley_gate("sphinx", "out")
        end
        t.exec("goto-shantay", t.player.goto_tile, 3304, 3123, 0)
        local _, coins_before_pass = t.inv.count("coins")
        t.exec("buyShantayPass", t.player.talk_to, "shantay", 1)
        t.exec("buyShantayPass-dialog", t.chat.play, {
            "npc:Hello effendi, I am Shantay.",
            "npc:I see you're new.",
            "choose:I want to buy a shantay pass for 5 gold coins.",
            "player:I want to buy a shantay pass for",
            "mesbox:You purchase a Shantay Pass.",
        })
        local pass_await_r, pass_await_d = t.inv.await("shantay_pass", 1, 5)
        local _, coins_after_pass = t.inv.count("coins")
        t.check("buyShantayPass.paid", pass_await_r == "ok" and type(coins_before_pass) == "number"
                and type(coins_after_pass) == "number" and coins_before_pass - coins_after_pass == 5,
            "shantay_pass " .. tostring(pass_await_r) .. " " .. tostring(pass_await_d) .. "; coins "
                .. tostring(coins_before_pass) .. " -> " .. tostring(coins_after_pass) .. " (want -5, shantay.rs2)")
        -- shantay_pass.rs2 [oploc1,shantay_pass_henge_doorway] from the north:
        -- the poster pages (no disclaimer carried yet), the pass handed over,
        -- the disclaimer, then [queue,shantay_pass_enter] p_teleport to
        -- 3304,3118 and p_telejump 3 south.
        t.exec("shantayDoorway", t.player.cross_gate, { loc = "shantay_pass_henge_doorway", at = { 3302, 3116, 0 },
            near = { 3304, 3118 }, far_ok = function(tile) return tile.z <= 3115 end,
            far_desc = "south of the Shantay Pass doorway, z <= 3115",
            chat = {
                "mesbox:There is a large poster on the wall",
                "mesbox:The Desert is a VERY Dangerous place",
                "mesbox:That seems pretty scary!",
                "choose:Yeah, that poster doesn't scare me!",
                "npc:Can I see your Shantay Desert Pass",
                "mesbox:You hand over a Shantay Pass.",
                "player:Sure, here you go!",
                "npc:Here, have a disclaimer",
            } })
        t.expect("shantayDoorway.passHandedOver", t.inv.expect_absent("shantay_pass"))
        t.exec("goto-sphinx", t.player.goto_tile, 3284, 2812, 0)
        pass_door("sophanemGate", "sophanem_gate_right", "sophanem_gate_right", 3284, 2809, 3284, 2810, 3284, 2808, "inside Sophanem's north gate")
        walk_check("sphinx.approach", 3299, 2786, "the square before the Sphinx")
        t.exec("talkToSphinx", t.player.talk_to, "ics_little_sphinx", 1)
        t.exec("talkToSphinx-dialog", t.chat.play, {
            "choose:Ask the Sphinx for help for Bob.",
            "player:Good day.",
            "npc:What is it human?",
            "player:Sphinx, we need your help!",
            "npc:Yes, please help!",
            "npc:Very well, I see you have a close relationship w",
            "npc:What is the problem?",
            "player:Thank you!",
            "player:Bob is in love with Neite but we need to prove t",
            "npc:Slow down human!",
            "npc:Bob has fallen in love?",
            "npc:This I did not foresee!",
            "npc:Who is this Robert the Strong that you speak of?",
            "player:He was a mighty hero!",
            "npc:Human myths interest me not.",
            "npc:Robert the Strong was no ordinary human though!",
            "npc:It is true that there is something about Bob... ",
            "player:But Bob has no memory of this!",
            "npc:Then it is the time for action!",
            "npc:Come with me into the desert so we are not distu",
            "npc:Quiet please.... I shall summon Bob.",
            "npc:What..? What happened?",
            "npc:I was eating a particularly nice piece of tuna.",
            "npc:Greetings Bob.",
            "npc:Oh, hi there Sphinx.",
            "npc:Hi there ",
            "npc:I have brought you here to find out who you real",
            "npc:So you buy this Robert the Strong stuff?",
            "npc:I have long suspected that your appearance as an",
            "npc:Hey! I'm just this cat, you know?",
            "npc:Look into my eyes.",
            "npc:Sure... whatever.",
            "npc:You are now under my influence.",
            "npc:Let's start with something simple. What is your ",
            "npc:My name is Bob.",
            "npc:Good. Are you or have you ever been called Rober",
            "npc:I... I don't know.",
            "npc:Think back to your earliest memories.",
            "npc:I see a big cat...",
            "npc:Where are you now?",
            "npc:I am in front of a dark tower... the cat is call",
            "npc:What is your name?",
            "npc:My name is Robert.",
            "npc:I am walking towards the tower...",
            "npc:Come, Odysseus!",
            "npc:Hesente!",
            "npc:Crasortius!",
            "npc:Never!",
            "npc:You are no longer under my influence.",
            "npc:Wow Bob! You really are Robert the Strong!",
            "npc:I am?",
            "npc:Yes! When you were hypnotised you told us of whe",
            "npc:I did?",
            "npc:Yes! How can Neite refuse you now!",
            "npc:Really?",
            "npc:Sphinx, summon Neite please!",
            "npc:There are more important matters than match maki",
            "npc:That can wait! Bob has been love sick, we have t",
            "npc:Very well! I shall summon Neite!",
            "npc:Oh... furballs!",
            "npc:Hi Neite!",
            "npc:Hello, to what do I owe this pleasure?",
            "npc:Bob is Robert the Strong!",
            "npc:What are you talking about?",
            "npc:The Sphinx hypnotised Bob. Bob told us about whe",
            "npc:So you're supposed to be Robert the Strong?",
            "npc:So they tell me.",
            "npc:Hmph.",
            "npc:For too long I have ignored the fact that there ",
            "npc:Wow.",
            "npc:Come here you.",
            "npc:*",
            "npc:Neite!",
            "npc:Of course kitten.",
            "npc:Hey ",
            "player:Of course.",
            "npc:Myself and Neite are going away for a few days. ",
            "player:Er... sure.",
            "npc:I'll give you a list of what needs doing.",
            "player:OK, no problem Bob.",
            "npc:*",
            "choose:Yes, teleport me to Unferth's house.",
            "*",
        })
        t.expect("quest.stage.chores_ready", t.quest.expect_stage("chores_ready"))
        t.expect("chores.list_given", t.inv.expect_has("twocats_chores", 1))
        -- "Yes, teleport me to Unferth's house." is the content's own
        -- p_telejump(0_45_55_38_38) (twocats.rs2:974): 2918,3558, inside his room.
        t.await({
            level = function()
                local r, tt = t.world.tile()
                return at_tile(r, tt, 2918, 3558)
            end,
            note = "Sphinx teleport landing",
        }, 10)
        local sphinx_tr, sphinx_tt = t.world.tile()
        t.check("talkToSphinx.teleported", at_tile(sphinx_tr, sphinx_tt, 2918, 3558),
            "Sphinx's Burthorpe teleport (twocats.rs2:974 p_telejump 0_45_55_38_38) -> " .. tile_text(sphinx_tr, sphinx_tt))

        -- The findBobAgain leg left the sidebar on the equipment tab (trap
        -- 294: a use_on issued from another tab refuses on the ARM, printing
        -- "armed by this call" even though the press never landed --
        -- measured run 1, useRake). Every chore below is a use_on; paint the
        -- inventory tab back before the first one.
        local chores_tab_result = t.ui.tab("inventory")
        t.check("chores.tab_inventory", chores_tab_result == "ok",
            "ui.tab(inventory) -> " .. tostring(chores_tab_result))
        t.ticks(2)

        -- useRake / plantSeeds: Unferth's patch, in the garden north of his
        -- room, which the fence closes off from the street: out through the
        -- room's own north door (poordoor 2920,3561) and back in after.
        pass_door("patchOut", "poordoor", "poordooropen", 2920, 3561, 2920, 3561, 2920, 3562, "the patch garden")
        local patch = t.player.by_symbol("loc", "twocats_patch")
        local useRake_result, useRake_detail = t.player.use_on("rake", patch)
        local rake_r, rake_d = t.var.await_server("varb1033_twocats_chores_tidygarden", 3, 120)
        t.check("useRake", rake_r == "ok",
            "use_on(rake, twocats_patch) -> " .. tostring(useRake_result) .. " " .. tostring(useRake_detail)
                .. "; tidygarden -> 3: " .. tostring(rake_r) .. " " .. tostring(rake_d))
        t.expect("chore.weeds_collected", t.inv.await("weeds", 3, 10))
        t.expect("chore.rake_kept", t.inv.expect_has("rake", 1))
        t.exec("plantSeeds", t.player.use_on, "potato_seed", patch)
        t.expect("quest.stage.tidygarden_planted", t.var.await_server("varb1033_twocats_chores_tidygarden", 4, 10))
        t.expect("chore.seeds_used", t.inv.expect_absent("potato_seed"))
        pass_door("patchIn", "poordoor", "poordooropen", 2920, 3561, 2920, 3562, 2920, 3561, "inside Unferth's room")

        -- makeBed
        t.exec("makeBed", t.player.click_loc, "twocats_bed", 1)
        t.expect("quest.stage.bed_made", t.var.await_server("varb1029_twocats_chores_tidyhouse", 1, 10))

        -- useLogsOnFireplace / lightLogs (the lighting itself is a bare
        -- anim + p_delay, no chat line -- use_on's own settle times out on
        -- success, so it is graded on the varbit read back, not the click).
        local fireplace = t.player.by_symbol("loc", "twocats_fireplace")
        t.exec("useLogsOnFireplace", t.player.use_on, "logs", fireplace)
        t.expect("quest.stage.logs_placed", t.var.await_server("varb1030_twocats_chores_warmhuman", 1, 10))
        t.expect("chore.logs_used", t.inv.expect_absent("logs"))
        local lightLogs_result, lightLogs_detail = t.player.use_on("tinderbox", fireplace)
        local light_r, light_d = t.var.await_server("varb1030_twocats_chores_warmhuman", 2, 10)
        t.check("lightLogs", light_r == "ok",
            "use_on(tinderbox, twocats_fireplace) -> " .. tostring(lightLogs_result) .. " " .. tostring(lightLogs_detail)
                .. "; warmhuman -> 2: " .. tostring(light_r) .. " " .. tostring(light_d)
                .. " (silent trigger, twocats.rs2:1161-1174)")

        -- useChocolateCakeOnTable / useMilkOnTable
        local table_loc = t.player.by_symbol("loc", "twocats_table")
        t.exec("useChocolateCakeOnTable", t.player.use_on, "chocolate_cake", table_loc)
        t.expect("quest.stage.cake_placed", t.var.await_server("varb1031_twocats_chores_feedhuman", 3, 10))
        t.expect("chore.cake_used", t.inv.expect_absent("chocolate_cake"))
        t.exec("useMilkOnTable", t.player.use_on, "bucket_milk", table_loc)
        t.expect("quest.stage.milk_placed", t.var.await_server("varb1031_twocats_chores_feedhuman", 4, 10))
        t.expect("chore.milk_used", t.inv.expect_absent("bucket_milk"))
        t.expect("chore.bucket_returned_empty", t.inv.expect_has("bucket_empty", 1))

        -- useShearsOnUnferth (same silent-trigger shape as lightLogs)
        local unferth = t.player.by_symbol("npc", "twocats_unferth")
        local useShears_result, useShears_detail = t.player.use_on("shears", unferth)
        local shears_r, shears_d = t.var.await_server("varb1032_twocats_chores_tidyhuman", 8, 250)
        t.check("useShearsOnUnferth", shears_r == "ok",
            "use_on(shears, twocats_unferth) -> " .. tostring(useShears_result) .. " " .. tostring(useShears_detail)
                .. "; tidyhuman -> 8: " .. tostring(shears_r) .. " " .. tostring(shears_d))

        -- The potatoes grow: sanctioned GRIND fast-forward
        -- (docs/QUEST_SERVER_CHEATS.md:120), graded on the varbit reaching 8
        -- and then on the cat's own 40->45 announcement.
        local grow_result = t.cheat("::twocats_growpotatoes")
        local grown_r, grown_d = t.var.await_server("varb1033_twocats_chores_tidygarden", 8, 10)
        t.check("garden.grown", grown_r == "ok",
            "::twocats_growpotatoes -> " .. tostring(grow_result) .. "; tidygarden -> 8: " .. tostring(grown_r) .. " " .. tostring(grown_d))
        t.exec("chores.finished-dialog", t.chat.play, {
            "npc:Well done, that's all the chores finished!",
            "npc:Let's talk to Unferth to see if there's anything",
        })
        t.expect("quest.stage.chores_done", t.quest.expect_stage("chores_done"))

        -- reportToUnferth
        -- (still in his room since the chores)
        t.exec("reportToUnferth", t.player.talk_to, "twocats_unferth", 1)
        t.exec("reportToUnferth-dialog", t.chat.play, {
            "player:Hi Unferth, is there anything I can do for you?",
            "npc:Ugh...",
            "npc:Now what?!",
            "npc:I don't feel so good...",
            "player:Oh dear! What's up?",
            "npc:Nothing, I bet!",
            "player:Shh!",
            "npc:I think I need the Doctor...ugh...",
            "player:What Doctor is that?",
            "npc:Just a Doctor... please hurry... I don't think I",
            "npc:Snarl!",
            "player:We can't let Bob see him like this!",
            "npc:I guess you're right, there is the Apothecary in",
            "player:Good idea!",
        })
        t.expect("quest.stage.apothecary_needed", t.quest.expect_stage("apothecary_needed"))

        -- talkToApoth: SW Varrock, the Apothecary (3195,3404) in his shop. Out
        -- of Unferth's by poshdoor; the goto lands on the street west of the
        -- shop and the player walks in through its doorway (the leaf is placed
        -- open in m49_53.jl2: asserted, not pressed). A real three-way menu,
        -- then the Doctor's/Nurse hat choice -- "Nurse hat." here.
        pass_door("unferthOut2", "poshdoor", "poshdooropen", 2922, 3558, 2921, 3558, 2923, 3558, "the street east of Unferth's")
        taverley_gate("apothecary", "out")
        local _, hat_before = t.inv.count("twocats_nurses_hat")
        t.exec("goto-apothecary", t.player.goto_tile, 3190, 3403, 0)
        pass_door("apothecaryIn", "fai_varrock_door", "fai_varrock_door_open", 3192, 3403, 3191, 3403, 3193, 3403, "inside the Apothecary's shop")
        t.exec("talkToApoth", t.player.talk_to, "apothecary", 1)
        t.exec("talkToApoth-dialog", t.chat.play, {
            "npc:I am the Apothecary. I brew potions. Do you need",
            "choose:Talk about A Tail of Two Cats.",
            "player:Hello Apothecary, Unferth has fallen ill. Could ",
            "npc:That Unferth! Honestly, I have been to see him m",
            "npc:I never did like that guy!",
            "player:I'm beginning to agree with you!",
            "player:What can I do with Unferth?",
            "npc:I have a few suggestions!",
            "npc:It's quite simple. Unferth is what we call a hyp",
            "player:How do I make Unferth believe I am a Doctor or N",
            "npc:Easy, make sure you are dressed in white robes a",
            "choose:Nurse hat.",
            "*",
            "npc:Then all you need to do is give him a vial of wa",
            "player:Haha!",
            "player:I always wanted to be a Doctor!",
        })
        t.expect("quest.stage.hat_given", t.quest.expect_stage("hat_given"))
        local hat_r, hat_d = t.inv.await("twocats_nurses_hat", 1, 5)
        local _, hat_after = t.inv.count("twocats_nurses_hat")
        t.check("reward.nurses_hat_granted", hat_r == "ok" and hat_before == 0 and hat_after == 1,
            "twocats_nurses_hat " .. tostring(hat_before) .. " -> " .. tostring(hat_after)
                .. " (want exactly 0 -> 1, the picked hat; await " .. tostring(hat_r) .. " " .. tostring(hat_d) .. ")")
        pass_door("apothecaryOut", "fai_varrock_door", "fai_varrock_door_open", 3192, 3403, 3193, 3403, 3190, 3403, "the street west of the shop")

        -- talkToUnferthAsDoctor: first, deliberately refused holding a
        -- weapon (~twocats_disguised's own "no weapon/shield" clause), then
        -- the real cure (twocats.rs2:1459-1491, 30 pages).
        t.exec("cure.equip_hat", t.player.equip, "twocats_nurses_hat")
        t.exec("cure.equip_shirt", t.player.equip, "desert_shirt")
        t.exec("cure.equip_robe", t.player.equip, "desert_robe")
        t.exec("cure.equip_sword", t.player.equip, "bronze_sword")
        taverley_gate("cure", "in")
        t.exec("goto-unferth-cure", t.player.goto_tile, 2925, 3558, 0)
        pass_door("unferthIn2", "poshdoor", "poshdooropen", 2922, 3558, 2923, 3558, 2921, 3558, "inside Unferth's room")
        t.exec("cure.refused", t.player.talk_to, "twocats_unferth", 1)
        t.exec("cure.refused-dialog", t.chat.play, {
            "player:Good day Unferth, I am Doctor ",
            "npc:No you're not! A Doctor wouldn't be holding that",
        })
        t.expect("cure.refused_stays_hat_given", t.var.await_server("varb1028_twocats_quest", 55, 2))
        t.exec("cure.unequip_sword", t.player.unequip, "bronze_sword")
        t.exec("talkToUnferthAsDoctor", t.player.talk_to, "twocats_unferth", 1)
        t.exec("talkToUnferthAsDoctor-dialog", t.chat.play, {
            "player:Good day Unferth, I am Doctor ",
            "npc:I am so glad to see you Doctor!",
            "npc:I am not a well man!",
            "player:What seems to be the problem?",
            "npc:Theres something wrong with his head!",
            "npc:My back hurts!",
            "player:No problem...",
            "npc:I think my arm is broken!",
            "player:We can...",
            "npc:Then there's my spleen!",
            "player:Your what...?",
            "npc:My liver has packed in!",
            "player:All you need to do is take this potion.",
            "*",
            "npc:Is that it?! I'm much more ill than that!",
            "npc:We could offer to bonk him on the head with a bi",
            "player:Hehe!",
            "npc:Doctor Please take this seriously! I demand trea",
            "player:Of course sir! The potion I have given you is th",
            "npc:Wow! What can I say... I feel special!",
            "player:Do you feel better?",
            "npc:I'm not sure...",
            "npc:Argh!",
            "npc:Wait! I feel something! Yes! I can feel the poti",
            "player:That's... great! Phew!",
            "npc:Indeed. Bob should be back by now; let's go find",
        })
        t.expect("quest.stage.cured", t.quest.expect_stage("cured"))
        t.expect("cure.vial_given", t.inv.expect_absent("vial_water"))

        -- Out of Unferth's room before the last search.
        pass_door("unferthOut3", "poshdoor", "poshdooropen", 2922, 3558, 2921, 3558, 2923, 3558, "the street east of Unferth's")

        -- findBobToFinish: worn Locate, one final time.
        locate_bob("findBobToFinish", true)

        -- talkToBobToFinish: ends in a p_telejump to Bob's own home tile.
        talk_to_bob("talkToBobToFinish")
        t.exec("talkToBobToFinish-dialog", t.chat.play, {
            "player:Hi Bob! How's it hang... going?",
            "npc:Wonderful! Neite and I have been all over Gielin",
            "npc:Where do we turn here?",
            "npc:North... I think.",
            "npc:You think?!",
            "npc:Yes: north.",
            "npc:Slow down!",
            "npc:For what?",
            "npc:That camel!",
            "npc:What camel?!",
            "npc:Are you sure this is a good idea?",
            "npc:Yeh, the old King and I go way back.",
            "npc:Hey old King! How's things?",
            "npc:Same as always: adventurers trying to kill me.",
            "npc:There, there, could be worse!",
            "npc:I guess... one moment...",
            "npc:Lolz die noob!",
            "npc:See what I mean?",
            "npc:Mate! You need a new line of work!",
            "npc:I said left at the camel!",
            "npc:What camel?!",
            "npc:You're too close to the pyramid!",
            "npc:Do you want to drive?!",
            "npc:I think we're lost.",
            "npc:No, I know where I'm going.",
            "npc:Are we there yet?",
            "npc:No.",
            "npc:Are we there yet?",
            "npc:No!",
            "npc:I'm the King of Gielinor!",
            "player:Phew!",
        })
        t.expect("quest.stage.bob_home", t.quest.expect_stage("bob_home"))
        local bob3_tile_result, bob3_tile = t.world.tile()
        t.check("bob3.home_teleport", bob3_tile_result == "ok" and bob3_tile
            and bob3_tile.x == 2924 and bob3_tile.z == 3565,
            "landed " .. tostring(bob3_tile and (bob3_tile.x .. "," .. bob3_tile.z) or bob3_tile_result))

        -- talkToUnferthToFinish: 65->70, the present.
        -- From Bob's home (2924,3565, the street) to poshdoor on foot.
        pass_door("unferthIn3", "poshdoor", "poshdooropen", 2922, 3558, 2923, 3558, 2921, 3558, "inside Unferth's room")
        t.exec("talkToUnferthToFinish", t.player.talk_to, "twocats_unferth", 1)
        t.exec("talkToUnferthToFinish-dialog", t.chat.play, {
            "player:Hi Unferth!",
            "npc:Hello! Bob came home today! I'm so happy!",
            "player:That's good news!",
            "npc:Fat lot of use you were! He came home on his own",
            "player:Er...",
            "npc:This guy is unbelievable!",
            "player:You're not wrong.",
            "npc:Of course I'm not wrong.",
            "player:I'll be going now.",
            "npc:Before you go, I found the strangest thing. Ther",
        })
        t.expect("quest.stage.complete", t.var.await_server("varb1028_twocats_quest", 70, 10))
        t.ticks(3) -- completion is asynchronous (section 8): let the queued
        -- [queue,twocats_quest_complete] land before reading the present.

        -- twocats.rs2 has no `~<abbr>_journal` proc at all (grep for
        -- "journal" in it is empty) -- section 8's documented case. Hand-roll
        -- the three rows quest.expect_complete() would have written.
        local varp_complete_result, varp_complete_detail = t.quest.expect_stage("complete")
        t.step("quest.varp_complete", varp_complete_result == "ok" and "PASS" or "FAIL",
            "expect_stage(complete) -> " .. tostring(varp_complete_result)
                .. " " .. tostring(varp_complete_detail))

        local scroll_result, scroll_detail = t.scroll.title()
        local scroll_name = type(scroll_detail) == "table" and scroll_detail.name or nil
        local scroll_pass = scroll_result == "ok" and type(scroll_name) == "string"
            and string.find(scroll_name, "A Tail of Two Cats", 1, true) ~= nil
        local scroll_shot_result, scroll_shot_detail = t.shot("quest.scroll")
        local scroll_shot_note = ""
        if scroll_shot_result == "ok" and type(scroll_shot_detail) == "string"
            and string.find(scroll_shot_detail, "unchanged", 1, true) then
            scroll_shot_note = " [scroll already photographed: " .. scroll_shot_detail .. "]"
        end
        t.step("quest.scroll_title", scroll_pass and "PASS" or "FAIL",
            "expected a title containing A Tail of Two Cats got=" .. tostring(scroll_name)
                .. " (" .. tostring(scroll_result) .. ")" .. scroll_shot_note)
        local scroll_close_result = t.scroll.close()
        t.check("quest.scroll_close", scroll_close_result == "ok",
            "scroll.close() -> " .. tostring(scroll_close_result))

        local qp_after_result, qp_after = t.var.varp("varp101_qp")
        local points_delta = (qp_after_result == "ok" and qp_before_result == "ok")
            and (qp_after - qp_before) or nil
        t.step("quest.points", points_delta == 2 and "PASS" or "FAIL",
            "qp " .. tostring(qp_before) .. " -> " .. tostring(qp_after)
                .. " delta=" .. tostring(points_delta) .. " expected=2")

        -- The present: twocats_present (twocats.rs2:1558-1566) is the real
        -- completion container -- open it and assert the item grants. The
        -- hat is NOT granted again here (parity1o dropped the second,
        -- unsourced hat) -- it was already asserted at reward.nurses_hat_granted.
        t.expect("present.have", t.inv.await("twocats_present", 1, 10))
        local _, lamps_before = t.inv.count("twocats_rewardlamp")
        local _, toy_before = t.inv.count("twocats_mouse_toy")
        t.exec("present.open", t.player.inv_op, "twocats_present", 1)
        t.exec("present.open-dialog", t.chat.play, { "mesbox:You open the package" })
        -- Literal rewards (wiki / Quest Helper: 2 antique lamps and a mouse
        -- toy in the present), exact before-to-after deltas.
        local lamps_r, lamps_d = t.inv.await("twocats_rewardlamp", 2, 5)
        local _, lamps_after = t.inv.count("twocats_rewardlamp")
        t.check("reward.lamps", lamps_r == "ok" and type(lamps_before) == "number" and type(lamps_after) == "number"
                and lamps_after - lamps_before == 2,
            "twocats_rewardlamp " .. tostring(lamps_before) .. " -> " .. tostring(lamps_after)
                .. " (want +2; await " .. tostring(lamps_r) .. " " .. tostring(lamps_d) .. ")")
        local _, toy_after = t.inv.count("twocats_mouse_toy")
        t.check("reward.mousetoy", type(toy_before) == "number" and type(toy_after) == "number"
                and toy_after - toy_before == 1,
            "twocats_mouse_toy " .. tostring(toy_before) .. " -> " .. tostring(toy_after) .. " (want +1)")
        t.expect("present.consumed", t.inv.expect_absent("twocats_present"))

        t.finish(0)
        return
    end,
}
