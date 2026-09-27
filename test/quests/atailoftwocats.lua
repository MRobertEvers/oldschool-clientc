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
-- the dial is pointing"), INLINED as repeated code at each of the three call
-- sites -- this file may not declare a local function, per section 1's own
-- shape rule and the header note the rejected file already carried.
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
-- `%twocats_locator_direction`, travels that bearing (a plain `goto_tile`/
-- `walk_to`, section 2's travel rule -- never a click on anything the guide
-- gates), and repeats until Bob is actually on screen, then talks to him for
-- real. `talkToBob`/`talkToBobAgain`/`talkToBobToFinish` retry the click a
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
--   * Hild (twocats.rs2:178-246): first visit (stage 0/5) always ends
--     "I don't have the death runes... Come back to me when you have them."
--     and writes stage 10 UNCONDITIONALLY -- the runes gate only the SECOND
--     visit's atomic exchange (worn-or-carried amulet + 5 death runes + a
--     free slot -> the enchanted `twocats_amuletofcatspeak`, unworn, in the
--     backpack), which writes stage 15. Runes are staged mid-run, after the
--     first visit has already run the runes-not-held branch for real (a
--     brought-along Quest Helper item given before that first visit would
--     desync `chat.play`'s script: twocats.rs2:207-212 checks the same
--     `inv_total(inv,deathrune)<5` unconditionally on THIS visit too).
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
    setup = {
        "::clearinv", -- fourteen tutorial slots would otherwise sit in the way
        "::give ics_little_amulet_of_catspeak 1", -- Icthlarin's Little
        -- Helper's own reward item, worn to accept (twocats.rs2:62's
        -- ~twocats_catspeak reads WORN) and handed to Hild worn-or-carried.
        "::give growncatobject 1", -- Quest Helper's `cat` FollowerItemRequirement:
        -- "You must have a cat or kitten with you" (~twocats_has_cat reads
        -- ~ratcatch_has_cat, which accepts any grown-cat colour carried).
        -- Death runes are NOT staged here (run 1's bug): twocats.rs2:198-202
        -- checks `inv_total(inv,deathrune) < 5` unconditionally on the FIRST
        -- Hild visit too, before it sets stage 10 -- runes already in the
        -- backpack skip straight into the same-page enchant branch, which
        -- desyncs chat.play's first-visit script entirely (measured run 1:
        -- talkToHild-dialog mismatched at "I don't have the death runes.").
        -- Given via t.cheat AFTER the first Hild visit instead, matching
        -- twocats.rs2's own two-visit shape (a brought-along item, staged
        -- late on purpose).
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
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "twocats_quest", -- a VARBIT (id 1028) packed into the
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
        local qp_before_result, qp_before = t.var.varp("qp")

        t.exec("amulet.equip", t.player.equip, "ics_little_amulet_of_catspeak")

        -- talkToUnferth: NE Burthorpe (2918,3558). Verbatim accept scene,
        -- ending in the real Yes/No confirm.
        t.exec("goto-unferth", t.player.goto_tile, 2918, 3558, 0)
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

        -- talkToHild (first visit, 5->10): death_woman_indoors1, 2930,3566.
        -- Always ends "I don't have the death runes" and writes 10, whether
        -- or not the runes are already carried (twocats.rs2:198-202).
        t.exec("goto-hild", t.player.goto_tile, 2930, 3566, 0)
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
            "player:I don't have the death runes.",
            "npc:Come back to me when you have them.",
        })
        t.expect("quest.stage.hild_asked", t.quest.expect_stage("hild_asked"))

        -- The death runes -- a brought-along Quest Helper item (deathRune5)
        -- -- staged NOW, after the first visit has already run the
        -- runes-not-held branch for real (see the setup-list note above).
        local deathrune_result = t.cheat("::give deathrune 5")
        t.check("deathrune.given", deathrune_result == "ok", "::give deathrune 5 -> " .. tostring(deathrune_result))
        t.ticks(3)

        -- talkToHild (second visit, 10->15): the atomic exchange. This is
        -- the SAME guide step (steps.put(5,talkToHild); steps.put(10,
        -- talkToHild)), driven a second time to reach the real enchant
        -- branch.
        t.exec("talkToHild-enchant", t.player.talk_to, "death_woman_indoors1", 1)
        t.exec("talkToHild-enchant-dialog", t.chat.play, {
            "npc:Greetings Adventurer, do you have the death rune",
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

        -- findBob: the catspeak amulet (e)'s Open op (unworn -- still in the
        -- backpack, [opheld3,twocats_amuletofcatspeak]) turned FOR REAL: press
        -- the right whisker until %twocats_locator_found reads 1 -- checked
        -- BEFORE any extra turn (the rejected file's bug) -- read
        -- %twocats_locator_direction, close the panel, and travel that
        -- bearing (Bob wanders the whole world now,
        -- [ai_timer,death_growncat_black]) until he is actually on screen.
        -- Method proved end to end by
        -- build/parity_state/parity1p/scripts/twocats_locate.lua (157/157) --
        -- never ::twocats_gotobob, which the b25 sampler rejected as handing
        -- the guide's locate leg to a debugproc.
        local findBob_in_view = false
        for hop = 1, 40 do
            t.exec("findBob.open." .. hop, t.player.inv_op, "twocats_amuletofcatspeak", 3)
            local findBob_mounted = t.ui.await_open("bob_locator_amulet", 10)
            t.check("findBob.mounted." .. hop, findBob_mounted == "ok",
                "await_open(bob_locator_amulet) -> " .. tostring(findBob_mounted))
            local _, findBob_r_widget = t.ui.widget("bob_locator_amulet:bob_locator_r_whisker")
            local findBob_found, findBob_turns = 0, 0
            for i = 1, 17 do
                t.ticks(2)
                local fr, fv = t.var.server("twocats_locator_found")
                if fr == "ok" and fv == 1 then
                    findBob_found = 1
                    break
                end
                if findBob_r_widget then
                    t.ui.invoke(findBob_r_widget, 1)
                end
                findBob_turns = findBob_turns + 1
            end
            local _, findBob_dir = t.var.server("twocats_locator_direction")
            local _, findBob_here = t.world.tile()
            t.check("findBob.dial_lit." .. hop, findBob_found == 1,
                "turned the right whisker " .. findBob_turns .. " time(s); found="
                    .. findBob_found .. " direction=" .. tostring(findBob_dir) .. " from "
                    .. tostring(findBob_here and findBob_here.x) .. ","
                    .. tostring(findBob_here and findBob_here.z))
            local findBob_near = false
            if findBob_found == 1 and findBob_r_widget then
                t.ui.invoke(findBob_r_widget, 1)
                t.ticks(2)
                local nr, nv = t.var.server("twocats_locator_found")
                findBob_near = (nr == "ok" and nv == 1)
            end
            t.key("escape")
            local findBob_closed = t.ui.await_close("bob_locator_amulet", 10)
            t.check("findBob.closed." .. hop, findBob_closed == "ok",
                "await_close(bob_locator_amulet) -> " .. tostring(findBob_closed))
            if findBob_found ~= 1 or findBob_dir == nil or findBob_dir < 1 or findBob_dir > 8 then
                break
            end
            -- Stop the search only once he is genuinely adjacent (radius 3,
            -- not 8) -- talk_to's own click has to route there and land
            -- inside a handful of ticks, and Bob keeps moving the whole
            -- time; the content's own all-direction "near" reading is the
            -- same close-range signal, so either one ends the search.
            if findBob_near then
                t.note("findBob: Bob in view after " .. (hop - 1) .. " hop(s), all-direction near from "
                    .. tostring(findBob_here.x) .. "," .. tostring(findBob_here.z))
                findBob_in_view = true
                break
            end
            local findBob_vr, findBob_vrow = t.npc.nearest("death_growncat_black", 3)
            if findBob_vr == "ok" then
                t.note("findBob: Bob in view after " .. (hop - 1) .. " hop(s) at "
                    .. tostring(findBob_vrow.x) .. "," .. tostring(findBob_vrow.z)
                    .. ", dial direction " .. tostring(findBob_dir))
                findBob_in_view = true
                break
            end
            local findBob_dx, findBob_dz = 0, 1
            if findBob_dir == 2 then findBob_dx, findBob_dz = 1, 1
            elseif findBob_dir == 3 then findBob_dx, findBob_dz = 1, 0
            elseif findBob_dir == 4 then findBob_dx, findBob_dz = 1, -1
            elseif findBob_dir == 5 then findBob_dx, findBob_dz = 0, -1
            elseif findBob_dir == 6 then findBob_dx, findBob_dz = -1, -1
            elseif findBob_dir == 7 then findBob_dx, findBob_dz = -1, 0
            elseif findBob_dir == 8 then findBob_dx, findBob_dz = -1, 1
            end
            local findBob_sr = t.npc.nearest("death_growncat_black", 40)
            if findBob_sr == "ok" then
                local findBob_wr = t.player.walk_to(findBob_here.x + findBob_dx * 6, findBob_here.z + findBob_dz * 6)
                t.check("findBob.walk." .. hop, findBob_wr == "ok" or findBob_wr == "timeout",
                    "walk_to -> " .. tostring(findBob_wr))
                if findBob_wr ~= "ok" then
                    t.exec("findBob.hop." .. hop, t.player.goto_tile,
                        findBob_here.x + findBob_dx * 5, findBob_here.z + findBob_dz * 5, 0)
                end
            else
                t.exec("findBob.travel." .. hop, t.player.goto_tile,
                    findBob_here.x + findBob_dx * 40, findBob_here.z + findBob_dz * 40, 0)
            end
        end
        t.check("findBob", findBob_in_view, "Bob in view: " .. tostring(findBob_in_view))

        -- Wear the enchanted amulet: every talk from here needs it WORN
        -- (~twocats_catspeak), and worn also switches the locator's own verb
        -- from inv_op (Open) to the worn-tab "Locate" used by the later legs.
        t.exec("amulet_e.equip", t.player.equip, "twocats_amuletofcatspeak")

        -- talkToBob: talk_to's own click already routes to wherever Bob
        -- currently is, so try it directly first -- a large pre-walk to a
        -- coordinate read a moment earlier just gives him longer to wander
        -- off it before the click lands (measured: worse, not better). Bob
        -- wanders, so a press landing on a tile he has just left
        -- ("map_flag: no dialogue") is retried after a SMALL step toward
        -- wherever the pool now says he is, a fresh read every attempt; a
        -- miss on any one read never aborts the retry (he is still nearby,
        -- just momentarily outside this call's own radius).
        local talkToBob_result, talkToBob_detail
        for attempt = 1, 12 do
            talkToBob_result, talkToBob_detail = t.player.talk_to("death_growncat_black", 1)
            if talkToBob_result == "ok" and not tostring(talkToBob_detail):find("no dialogue") then
                break
            end
            local tb_vr, tb_vrow = t.npc.nearest("death_growncat_black", 40)
            if tb_vr == "ok" then
                t.player.walk_to(tb_vrow.x + 1, tb_vrow.z, 6)
            end
        end
        t.check("talkToBob", talkToBob_result == "ok"
            and not tostring(talkToBob_detail):find("no dialogue"),
            tostring(talkToBob_result) .. " " .. tostring(talkToBob_detail))
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

        -- talkToGertrude: west of Varrock, BASE symbol "gertrude" (trap 19).
        t.exec("goto-gertrude", t.player.goto_tile, 3151, 3410, 0)
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

        -- talkToReldo: Varrock Castle library (3209,3495). A deliberate
        -- negative check first -- unworn amulet -- content's own "meow"
        -- refusal and the stage staying 25 (twocats.rs2:775-778) -- then
        -- re-equip and drive the real exchange (writes 30).
        t.exec("goto-reldo", t.player.goto_tile, 3209, 3495, 0)
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
        t.expect("reldo.unworn_stays_gertrude_done", t.var.await_server("twocats_quest", 25, 3))
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
        t.expect("reldo.withbook", t.var.await_server("twocats_reldo", 1, 3))

        -- findBobAgain: the WORN "Locate" verb (equipment tab -> wornitems
        -- slot2 op 2, [inv_button2,wornitems:slot2]), the amulet already worn
        -- from the Reldo re-equip above. Same real turn-and-travel loop as
        -- findBob, worn this time.
        t.exec("goto-findBobAgain", t.player.goto_tile, 3209, 3495, 0)
        local findBobAgain_in_view = false
        for hop = 1, 40 do
            local findBobAgain_tab_result = t.ui.tab("equipment")
            t.check("findBobAgain.tab." .. hop, findBobAgain_tab_result == "ok",
                "ui.tab(equipment) -> " .. tostring(findBobAgain_tab_result))
            t.ticks(2)
            local _, findBobAgain_worn_slot = t.ui.widget("wornitems:slot2")
            t.check("findBobAgain.worn_slot." .. hop, findBobAgain_worn_slot ~= nil,
                "wornitems:slot2 component=" .. tostring(findBobAgain_worn_slot))
            if findBobAgain_worn_slot then
                t.ui.invoke(findBobAgain_worn_slot, 2)
            end
            local findBobAgain_mounted = t.ui.await_open("bob_locator_amulet", 10)
            t.check("findBobAgain.mounted." .. hop, findBobAgain_mounted == "ok",
                "await_open(bob_locator_amulet) -> " .. tostring(findBobAgain_mounted))
            local _, findBobAgain_r_widget = t.ui.widget("bob_locator_amulet:bob_locator_r_whisker")
            local findBobAgain_found, findBobAgain_turns = 0, 0
            for i = 1, 17 do
                t.ticks(2)
                local fr, fv = t.var.server("twocats_locator_found")
                if fr == "ok" and fv == 1 then
                    findBobAgain_found = 1
                    break
                end
                if findBobAgain_r_widget then
                    t.ui.invoke(findBobAgain_r_widget, 1)
                end
                findBobAgain_turns = findBobAgain_turns + 1
            end
            local _, findBobAgain_dir = t.var.server("twocats_locator_direction")
            local _, findBobAgain_here = t.world.tile()
            t.check("findBobAgain.dial_lit." .. hop, findBobAgain_found == 1,
                "turned the right whisker " .. findBobAgain_turns .. " time(s); found="
                    .. findBobAgain_found .. " direction=" .. tostring(findBobAgain_dir) .. " from "
                    .. tostring(findBobAgain_here and findBobAgain_here.x) .. ","
                    .. tostring(findBobAgain_here and findBobAgain_here.z))
            local findBobAgain_near = false
            if findBobAgain_found == 1 and findBobAgain_r_widget then
                t.ui.invoke(findBobAgain_r_widget, 1)
                t.ticks(2)
                local nr, nv = t.var.server("twocats_locator_found")
                findBobAgain_near = (nr == "ok" and nv == 1)
            end
            t.key("escape")
            local findBobAgain_closed = t.ui.await_close("bob_locator_amulet", 10)
            t.check("findBobAgain.closed." .. hop, findBobAgain_closed == "ok",
                "await_close(bob_locator_amulet) -> " .. tostring(findBobAgain_closed))
            if findBobAgain_found ~= 1 or findBobAgain_dir == nil or findBobAgain_dir < 1 or findBobAgain_dir > 8 then
                break
            end
            -- Stop only once genuinely adjacent (radius 3, not 8) or the
            -- content's own all-direction "near" reading fires -- see
            -- findBob's note above.
            if findBobAgain_near then
                t.note("findBobAgain: Bob in view after " .. (hop - 1) .. " hop(s), all-direction near from "
                    .. tostring(findBobAgain_here.x) .. "," .. tostring(findBobAgain_here.z))
                findBobAgain_in_view = true
                break
            end
            local findBobAgain_vr, findBobAgain_vrow = t.npc.nearest("death_growncat_black", 3)
            if findBobAgain_vr == "ok" then
                t.note("findBobAgain: Bob in view after " .. (hop - 1) .. " hop(s) at "
                    .. tostring(findBobAgain_vrow.x) .. "," .. tostring(findBobAgain_vrow.z)
                    .. ", dial direction " .. tostring(findBobAgain_dir))
                findBobAgain_in_view = true
                break
            end
            local findBobAgain_dx, findBobAgain_dz = 0, 1
            if findBobAgain_dir == 2 then findBobAgain_dx, findBobAgain_dz = 1, 1
            elseif findBobAgain_dir == 3 then findBobAgain_dx, findBobAgain_dz = 1, 0
            elseif findBobAgain_dir == 4 then findBobAgain_dx, findBobAgain_dz = 1, -1
            elseif findBobAgain_dir == 5 then findBobAgain_dx, findBobAgain_dz = 0, -1
            elseif findBobAgain_dir == 6 then findBobAgain_dx, findBobAgain_dz = -1, -1
            elseif findBobAgain_dir == 7 then findBobAgain_dx, findBobAgain_dz = -1, 0
            elseif findBobAgain_dir == 8 then findBobAgain_dx, findBobAgain_dz = -1, 1
            end
            local findBobAgain_sr = t.npc.nearest("death_growncat_black", 40)
            if findBobAgain_sr == "ok" then
                local findBobAgain_wr = t.player.walk_to(findBobAgain_here.x + findBobAgain_dx * 6, findBobAgain_here.z + findBobAgain_dz * 6)
                t.check("findBobAgain.walk." .. hop, findBobAgain_wr == "ok" or findBobAgain_wr == "timeout",
                    "walk_to -> " .. tostring(findBobAgain_wr))
                if findBobAgain_wr ~= "ok" then
                    t.exec("findBobAgain.hop." .. hop, t.player.goto_tile,
                        findBobAgain_here.x + findBobAgain_dx * 5, findBobAgain_here.z + findBobAgain_dz * 5, 0)
                end
            else
                t.exec("findBobAgain.travel." .. hop, t.player.goto_tile,
                    findBobAgain_here.x + findBobAgain_dx * 40, findBobAgain_here.z + findBobAgain_dz * 40, 0)
            end
        end
        t.check("findBobAgain", findBobAgain_in_view, "Bob in view: " .. tostring(findBobAgain_in_view))

        -- talkToBobAgain: same shape as talkToBob -- talk_to directly first
        -- (its own click routes to his current tile), a SMALL corrective
        -- step on a miss, re-reading his position every attempt.
        local talkToBobAgain_result, talkToBobAgain_detail
        for attempt = 1, 12 do
            talkToBobAgain_result, talkToBobAgain_detail = t.player.talk_to("death_growncat_black", 1)
            if talkToBobAgain_result == "ok" and not tostring(talkToBobAgain_detail):find("no dialogue") then
                break
            end
            local tba_vr, tba_vrow = t.npc.nearest("death_growncat_black", 40)
            if tba_vr == "ok" then
                t.player.walk_to(tba_vrow.x + 1, tba_vrow.z, 6)
            end
        end
        t.check("talkToBobAgain", talkToBobAgain_result == "ok"
            and not tostring(talkToBobAgain_detail):find("no dialogue"),
            tostring(talkToBobAgain_result) .. " " .. tostring(talkToBobAgain_detail))
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
        t.exec("goto-sphinx", t.player.goto_tile, 3300, 2784, 0)
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

        -- The findBobAgain leg left the sidebar on the equipment tab (trap
        -- 294: a use_on issued from another tab refuses on the ARM, printing
        -- "armed by this call" even though the press never landed --
        -- measured run 1, useRake). Every chore below is a use_on; paint the
        -- inventory tab back before the first one.
        local chores_tab_result = t.ui.tab("inventory")
        t.check("chores.tab_inventory", chores_tab_result == "ok",
            "ui.tab(inventory) -> " .. tostring(chores_tab_result))
        t.ticks(2)

        -- useRake / plantSeeds: Unferth's patch, north of the house through
        -- its own north door (poordoor).
        t.exec("goto-door", t.player.goto_tile, 2920, 3560, 0)
        t.exec("door.open", t.player.click_loc, "poordoor", 1)
        local patch = t.player.by_symbol("loc", "twocats_patch")
        t.check("patch.found", patch ~= nil, "twocats_patch resolved: " .. tostring(patch and patch.id))
        local useRake_result, useRake_detail = t.player.use_on("rake", patch)
        t.expect("useRake", t.var.await_server("twocats_chores_tidygarden", 3, 120))
        t.check("useRake.press", useRake_result ~= nil,
            "use_on -> " .. tostring(useRake_result) .. " " .. tostring(useRake_detail)
                .. "; tidygarden read back above")
        t.expect("chore.weeds_collected", t.inv.await("weeds", 3, 10))
        t.exec("plantSeeds", t.player.use_on, "potato_seed", patch)
        t.expect("quest.stage.tidygarden_planted", t.var.await_server("twocats_chores_tidygarden", 4, 10))
        t.expect("chore.seeds_used", t.inv.expect_absent("potato_seed"))

        -- makeBed
        t.exec("goto-house", t.player.goto_tile, 2918, 3558, 0)
        t.exec("makeBed", t.player.click_loc, "twocats_bed", 1)
        t.expect("quest.stage.bed_made", t.var.await_server("twocats_chores_tidyhouse", 1, 10))

        -- useLogsOnFireplace / lightLogs (the lighting itself is a bare
        -- anim + p_delay, no chat line -- use_on's own settle times out on
        -- success, so it is graded on the varbit read back, not the click).
        local fireplace = t.player.by_symbol("loc", "twocats_fireplace")
        t.check("fireplace.found", fireplace ~= nil, "twocats_fireplace resolved: " .. tostring(fireplace and fireplace.id))
        t.exec("useLogsOnFireplace", t.player.use_on, "logs", fireplace)
        t.expect("quest.stage.logs_placed", t.var.await_server("twocats_chores_warmhuman", 1, 10))
        local lightLogs_result, lightLogs_detail = t.player.use_on("tinderbox", fireplace)
        t.expect("lightLogs", t.var.await_server("twocats_chores_warmhuman", 2, 10))
        t.check("lightLogs.press", lightLogs_result ~= nil,
            "use_on -> " .. tostring(lightLogs_result) .. " " .. tostring(lightLogs_detail)
                .. "; warmhuman read back above (silent trigger, twocats.rs2:1161-1174)")

        -- useChocolateCakeOnTable / useMilkOnTable
        local table_loc = t.player.by_symbol("loc", "twocats_table")
        t.check("table.found", table_loc ~= nil, "twocats_table resolved: " .. tostring(table_loc and table_loc.id))
        t.exec("useChocolateCakeOnTable", t.player.use_on, "chocolate_cake", table_loc)
        t.expect("quest.stage.cake_placed", t.var.await_server("twocats_chores_feedhuman", 3, 10))
        t.exec("useMilkOnTable", t.player.use_on, "bucket_milk", table_loc)
        t.expect("quest.stage.milk_placed", t.var.await_server("twocats_chores_feedhuman", 4, 10))
        t.expect("chore.bucket_returned_empty", t.inv.expect_has("bucket_empty", 1))

        -- useShearsOnUnferth (same silent-trigger shape as lightLogs)
        local unferth = t.player.by_symbol("npc", "twocats_unferth")
        t.check("unferth.found", unferth ~= nil, "twocats_unferth resolved: " .. tostring(unferth and unferth.id))
        local useShears_result, useShears_detail = t.player.use_on("shears", unferth)
        t.expect("useShearsOnUnferth", t.var.await_server("twocats_chores_tidyhuman", 8, 250))
        t.check("useShearsOnUnferth.press", useShears_result ~= nil,
            "use_on -> " .. tostring(useShears_result) .. " " .. tostring(useShears_detail)
                .. "; tidyhuman read back above")

        -- The potatoes grow: sanctioned GRIND fast-forward
        -- (docs/QUEST_SERVER_CHEATS.md:120), read back below by the varbit
        -- reaching 8 and then by the cat's own 40->45 announcement.
        local grow_result = t.cheat("::twocats_growpotatoes")
        t.check("garden.grow_cheat", grow_result == "ok", "::twocats_growpotatoes -> " .. tostring(grow_result))
        t.expect("garden.grown", t.var.await_server("twocats_chores_tidygarden", 8, 10))
        t.exec("chores.finished-dialog", t.chat.play, {
            "npc:Well done, that's all the chores finished!",
            "npc:Let's talk to Unferth to see if there's anything",
        })
        t.expect("quest.stage.chores_done", t.quest.expect_stage("chores_done"))

        -- reportToUnferth
        t.exec("goto-unferth-report", t.player.goto_tile, 2918, 3558, 0)
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

        -- talkToApoth: SW Varrock (3195,3405). A real three-way menu, then
        -- the Doctor's/Nurse hat choice -- "Nurse hat." here.
        t.exec("goto-apothecary", t.player.goto_tile, 3195, 3405, 0)
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
        t.expect("reward.nurses_hat_granted", t.inv.expect_has("twocats_nurses_hat", 1))

        -- talkToUnferthAsDoctor: first, deliberately refused holding a
        -- weapon (~twocats_disguised's own "no weapon/shield" clause), then
        -- the real cure (twocats.rs2:1459-1491, 30 pages).
        t.exec("cure.equip_hat", t.player.equip, "twocats_nurses_hat")
        t.exec("cure.equip_shirt", t.player.equip, "desert_shirt")
        t.exec("cure.equip_robe", t.player.equip, "desert_robe")
        t.exec("cure.equip_sword", t.player.equip, "bronze_sword")
        t.exec("goto-unferth-cure", t.player.goto_tile, 2918, 3558, 0)
        t.exec("cure.refused", t.player.talk_to, "twocats_unferth", 1)
        t.exec("cure.refused-dialog", t.chat.play, {
            "player:Good day Unferth, I am Doctor ",
            "npc:No you're not! A Doctor wouldn't be holding that",
        })
        t.expect("cure.refused_stays_hat_given", t.var.await_server("twocats_quest", 55, 2))
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

        -- findBobToFinish: worn Locate, one final time -- the same real
        -- turn-and-travel loop.
        local findBobToFinish_in_view = false
        for hop = 1, 40 do
            local findBobToFinish_tab_result = t.ui.tab("equipment")
            t.check("findBobToFinish.tab." .. hop, findBobToFinish_tab_result == "ok",
                "ui.tab(equipment) -> " .. tostring(findBobToFinish_tab_result))
            t.ticks(2)
            local _, findBobToFinish_worn_slot = t.ui.widget("wornitems:slot2")
            t.check("findBobToFinish.worn_slot." .. hop, findBobToFinish_worn_slot ~= nil,
                "wornitems:slot2 component=" .. tostring(findBobToFinish_worn_slot))
            if findBobToFinish_worn_slot then
                t.ui.invoke(findBobToFinish_worn_slot, 2)
            end
            local findBobToFinish_mounted = t.ui.await_open("bob_locator_amulet", 10)
            t.check("findBobToFinish.mounted." .. hop, findBobToFinish_mounted == "ok",
                "await_open(bob_locator_amulet) -> " .. tostring(findBobToFinish_mounted))
            local _, findBobToFinish_r_widget = t.ui.widget("bob_locator_amulet:bob_locator_r_whisker")
            local findBobToFinish_found, findBobToFinish_turns = 0, 0
            for i = 1, 17 do
                t.ticks(2)
                local fr, fv = t.var.server("twocats_locator_found")
                if fr == "ok" and fv == 1 then
                    findBobToFinish_found = 1
                    break
                end
                if findBobToFinish_r_widget then
                    t.ui.invoke(findBobToFinish_r_widget, 1)
                end
                findBobToFinish_turns = findBobToFinish_turns + 1
            end
            local _, findBobToFinish_dir = t.var.server("twocats_locator_direction")
            local _, findBobToFinish_here = t.world.tile()
            t.check("findBobToFinish.dial_lit." .. hop, findBobToFinish_found == 1,
                "turned the right whisker " .. findBobToFinish_turns .. " time(s); found="
                    .. findBobToFinish_found .. " direction=" .. tostring(findBobToFinish_dir) .. " from "
                    .. tostring(findBobToFinish_here and findBobToFinish_here.x) .. ","
                    .. tostring(findBobToFinish_here and findBobToFinish_here.z))
            local findBobToFinish_near = false
            if findBobToFinish_found == 1 and findBobToFinish_r_widget then
                t.ui.invoke(findBobToFinish_r_widget, 1)
                t.ticks(2)
                local nr, nv = t.var.server("twocats_locator_found")
                findBobToFinish_near = (nr == "ok" and nv == 1)
            end
            t.key("escape")
            local findBobToFinish_closed = t.ui.await_close("bob_locator_amulet", 10)
            t.check("findBobToFinish.closed." .. hop, findBobToFinish_closed == "ok",
                "await_close(bob_locator_amulet) -> " .. tostring(findBobToFinish_closed))
            if findBobToFinish_found ~= 1 or findBobToFinish_dir == nil or findBobToFinish_dir < 1 or findBobToFinish_dir > 8 then
                break
            end
            -- Stop only once genuinely adjacent (radius 3, not 8) or the
            -- content's own all-direction "near" reading fires -- see
            -- findBob's note above.
            if findBobToFinish_near then
                t.note("findBobToFinish: Bob in view after " .. (hop - 1) .. " hop(s), all-direction near from "
                    .. tostring(findBobToFinish_here.x) .. "," .. tostring(findBobToFinish_here.z))
                findBobToFinish_in_view = true
                break
            end
            local findBobToFinish_vr, findBobToFinish_vrow = t.npc.nearest("death_growncat_black", 3)
            if findBobToFinish_vr == "ok" then
                t.note("findBobToFinish: Bob in view after " .. (hop - 1) .. " hop(s) at "
                    .. tostring(findBobToFinish_vrow.x) .. "," .. tostring(findBobToFinish_vrow.z)
                    .. ", dial direction " .. tostring(findBobToFinish_dir))
                findBobToFinish_in_view = true
                break
            end
            local findBobToFinish_dx, findBobToFinish_dz = 0, 1
            if findBobToFinish_dir == 2 then findBobToFinish_dx, findBobToFinish_dz = 1, 1
            elseif findBobToFinish_dir == 3 then findBobToFinish_dx, findBobToFinish_dz = 1, 0
            elseif findBobToFinish_dir == 4 then findBobToFinish_dx, findBobToFinish_dz = 1, -1
            elseif findBobToFinish_dir == 5 then findBobToFinish_dx, findBobToFinish_dz = 0, -1
            elseif findBobToFinish_dir == 6 then findBobToFinish_dx, findBobToFinish_dz = -1, -1
            elseif findBobToFinish_dir == 7 then findBobToFinish_dx, findBobToFinish_dz = -1, 0
            elseif findBobToFinish_dir == 8 then findBobToFinish_dx, findBobToFinish_dz = -1, 1
            end
            local findBobToFinish_sr = t.npc.nearest("death_growncat_black", 40)
            if findBobToFinish_sr == "ok" then
                local findBobToFinish_wr = t.player.walk_to(findBobToFinish_here.x + findBobToFinish_dx * 6, findBobToFinish_here.z + findBobToFinish_dz * 6)
                t.check("findBobToFinish.walk." .. hop, findBobToFinish_wr == "ok" or findBobToFinish_wr == "timeout",
                    "walk_to -> " .. tostring(findBobToFinish_wr))
                if findBobToFinish_wr ~= "ok" then
                    t.exec("findBobToFinish.hop." .. hop, t.player.goto_tile,
                        findBobToFinish_here.x + findBobToFinish_dx * 5, findBobToFinish_here.z + findBobToFinish_dz * 5, 0)
                end
            else
                t.exec("findBobToFinish.travel." .. hop, t.player.goto_tile,
                    findBobToFinish_here.x + findBobToFinish_dx * 40, findBobToFinish_here.z + findBobToFinish_dz * 40, 0)
            end
        end
        t.check("findBobToFinish", findBobToFinish_in_view, "Bob in view: " .. tostring(findBobToFinish_in_view))

        -- talkToBobToFinish: same shape as talkToBob/talkToBobAgain -- talk
        -- directly first, a small corrective step on a miss. Ends in a
        -- p_telejump back to Bob's own home tile.
        local talkToBobToFinish_result, talkToBobToFinish_detail
        for attempt = 1, 12 do
            talkToBobToFinish_result, talkToBobToFinish_detail = t.player.talk_to("death_growncat_black", 1)
            if talkToBobToFinish_result == "ok" and not tostring(talkToBobToFinish_detail):find("no dialogue") then
                break
            end
            local tbf_vr, tbf_vrow = t.npc.nearest("death_growncat_black", 40)
            if tbf_vr == "ok" then
                t.player.walk_to(tbf_vrow.x + 1, tbf_vrow.z, 6)
            end
        end
        t.check("talkToBobToFinish", talkToBobToFinish_result == "ok"
            and not tostring(talkToBobToFinish_detail):find("no dialogue"),
            tostring(talkToBobToFinish_result) .. " " .. tostring(talkToBobToFinish_detail))
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
        t.exec("goto-unferth-finish", t.player.goto_tile, 2918, 3558, 0)
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
        t.expect("quest.stage.complete", t.var.await_server("twocats_quest", 70, 10))
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

        local qp_after_result, qp_after = t.var.varp("qp")
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
        t.exec("present.open", t.player.inv_op, "twocats_present", 1)
        t.exec("present.open-dialog", t.chat.play, { "mesbox:You open the package" })
        t.expect("reward.lamps", t.inv.expect_has("twocats_rewardlamp", 2))
        t.expect("reward.mousetoy", t.inv.expect_has("twocats_mouse_toy", 1))

        t.finish(0)
        return
    end,
}
