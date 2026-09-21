-- A Tail of Two Cats (2 QP). Content:
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_atailoftwocats/scripts/twocats.rs2
--
-- The state carrier is the VARBIT %twocats_quest (id 1028, 0 -> 70), not the
-- unused `[twocats]` varplayer atailoftwocats.varp declares (nothing in
-- twocats.rs2 ever reads or writes %twocats -- see that file's own header
-- comment on why a second carrier with the same RuneScript name would have
-- refused the whole tree). quest.bind's varp= names twocats_quest directly.
--
-- Quest Helper's `NpcID.TWOCATS_UNFERTH_BALD` / `NpcID.RELDO_NORMAL` /
-- `NpcID.GERTRUDE_POST` never spawn in this pack (grep --include='*.spawn'
-- turns up nothing for any of them); the live npcs are the BASE symbols
-- twocats_unferth (2918,3558,0), reldo (3209,3495,0) and gertrude
-- (3151,3410,0) -- the *_bald/*_normal/*_post names are either multinpc
-- children with no spawn of their own or, for gertrude_post, a dead second
-- [opnpc1,gertrude_post] trigger duplicating gertrude.rs2's own fallback.
--
-- RETRY after the 2026-09-21 seam pass (queue: "RETRY after 4420b02611"):
-- gertrude.rs2 puts its %fluffs ladder back in FRONT of A Tail's own window
-- on purpose (see that file's own header comment, lines 10-28) -- Gertrude's
-- Cat is a real prerequisite in THIS pack (the npc def's multivarp=fluffs
-- names it twice over), not the content bug the previous attempt reported,
-- so setup completes it with the same ::complete idiom already used for
-- Icthlarin's Little Helper. The Gertrude step now reaches
-- ~twocats_gertrude_after_bob for real. BLOCKED moved one step further, at
-- Reldo: see the comment above that t.blocked() call.
return {
    id = "atailoftwocats",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- fourteen tutorial slots would otherwise sit in the way
        "::give twocats_amuletofcatspeak 1", -- the item twocats.rs2 actually checks
        -- (Quest Helper's item requirement resolves to ics_little_amulet_of_catspeak
        -- in this pack's obj.compack, obj id 4677 -- a DIFFERENT item from the one
        -- every twocats.rs2 branch checks, twocats_amuletofcatspeak, obj id 6544.
        -- Giving the wrong amulet would fail every "You need a Catspeak amulet"
        -- inv_total(inv, twocats_amuletofcatspeak) guard in the script.)
        "::give deathrune 5", -- Hild's ghost-ward requirement, twocats.rs2:60-63
        -- Quest Helper's prerequisite quest; twocats.rs2 itself never checks
        -- %ics_little_var (the gate the header comment calls "deferred until
        -- that quest is ported" is not wired into any branch below), so this
        -- is prep, not something the run depends on. Row name from
        -- quest_cheat.rs2:683, not the `quest_icthlarin` the scaffold guessed.
        "::complete quest_icthlarinslittlehelper",
        -- Gertrude's Cat, real prerequisite for the Gertrude step below (see
        -- the file header). Row name quest_cheat.rs2:562, cheat body sets
        -- %fluffs = ^fluffs_complete directly.
        "::complete quest_gertrudescat",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "twocats_quest",
            constants = {
                not_started = 0,
                accepted = 5,
                hild_done = 10,
                bob_found = 20,
                gertrude_done = 25, -- twocats.rs2:120, [proc,twocats_gertrude_after_bob]
                reldo_done = 30, -- twocats.rs2:147, [label,twocats_talk_to_reldo]
                bob_found_again = 35, -- twocats.rs2:155, [label,twocats_find_bob_2]
                chores_ready = 40, -- twocats.rs2:169, [label,twocats_talk_to_sphinx]
                complete = 70, -- twocats.rs2:392, set by [label,twocats_done]
            },
            row = "quest_tailoftwocats", -- all.dbrow.compack:143
            display = "A Tail of Two Cats",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- setup cheats (::give, ::complete) are not client-side yet
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Talk to Unferth (twocats_unferth, NE Burthorpe) to accept the quest.
        -- [opnpc1,twocats_unferth]/[opnpc1,twocats_unferth_bald] share one body
        -- (twocats.rs2:27-29); at %twocats_quest=0 it dispatches to
        -- [label,twocats_start] (twocats.rs2:36-42), which opens with the
        -- PLAYER's line (trap 18) and, since we carry the amulet, sets
        -- %twocats_quest=5 with no further page.
        t.exec("goto-unferth", t.player.goto_tile, 2918, 3558, 0)
        t.exec("talkToUnferth", t.player.talk_to, "twocats_unferth", 1)
        t.exec("talkToUnferth-dialog", t.chat.play, {
            "player:I'll help you.",
        })
        t.expect("quest.stage.accepted", t.quest.expect_stage("accepted"))

        -- Talk to Hild (death_woman_indoors1, spawned 2930,3566,0). At
        -- %twocats_quest=5 [opnpc1,death_woman_indoors1] (twocats.rs2:55-56)
        -- dispatches to [label,twocats_talk_to_hild] (twocats.rs2:58-66):
        -- player's line, then (5 death runes in hand) she is thanked
        -- silently and %twocats_quest becomes 10 -- no npc reply page.
        t.exec("goto-hild", t.player.goto_tile, 2930, 3566, 0)
        t.exec("talkToHild", t.player.talk_to, "death_woman_indoors1", 1)
        t.exec("talkToHild-dialog", t.chat.play, {
            "player:I'm here to help.",
        })
        t.expect("quest.stage.hild_done", t.quest.expect_stage("hild_done"))

        -- Find Bob: at %twocats_quest=5|10, Unferth's OWN dispatch branches
        -- straight to [label,twocats_find_bob_1] (twocats.rs2:29, 89-97) --
        -- no separate Bob-the-cat interaction is wired in this pack at all.
        -- death_growncat_black_vis (Quest Helper's findBob target) has no
        -- spawn row anywhere under server/scripts, and its only
        -- [opnpc1,death_growncat_black_vis] owner is Dragon Slayer II's
        -- dragonslayer2.rs2:678-716, gated on %ds2, never on %twocats_quest --
        -- a click there could never advance this quest even if the npc
        -- existed. Talking to Unferth again is the real trigger.
        t.exec("goto-unferth-2", t.player.goto_tile, 2918, 3558, 0)
        t.exec("talkToUnferthFindBob", t.player.talk_to, "twocats_unferth", 1)
        t.exec("talkToUnferthFindBob-dialog", t.chat.play, {
            "mesbox:activate the Catspeak amulet",
        })
        t.expect("quest.stage.bob_found", t.quest.expect_stage("bob_found"))

        -- Talk to Gertrude (west of Varrock, spawned 3151,3410,0) to ask
        -- about Bob's parents -- twocats.rs2's own step-20 handoff
        -- ([label,twocats_after_bob], twocats.rs2:108-111) only has Unferth
        -- POINT at her; the actual topic advance
        -- ([proc,twocats_gertrude_after_bob], twocats.rs2:118-120) is reached
        -- through areas/varrock/scripts/gertrude.rs2's shared dispatch.
        -- Setup already completed Gertrude's Cat (%fluffs=fluffs_complete),
        -- so [opnpc1,gertrude]'s ladder falls to its trailing else
        -- (gertrude.rs2:84-86) -> [proc,gertrude_route_topics]
        -- (gertrude.rs2:107-137). Ratcatchers is NOT also eligible here --
        -- its own ~ratcatch_meets_prereqs (ratcatchers_shared.rs2:83-89)
        -- additionally requires %giantdwarf_quest>=1, which setup never
        -- starts -- so $rat=0/$tail=1 and the dispatch goes straight to
        -- ~twocats_gertrude_after_bob (twocats.rs2:118-120) with no
        -- ratcatchers/two-cats choice menu: the page that opens is that
        -- proc's own PLAYER line (trap 18).
        t.exec("goto-gertrude", t.player.goto_tile, 3151, 3410, 0)
        t.exec("talkToGertrude", t.player.talk_to, "gertrude", 1)
        t.exec("talkToGertrude-dialog", t.chat.play, {
            "player:I found Bob!",
        })
        t.expect("quest.stage.gertrude_done", t.quest.expect_stage("gertrude_done"))

        -- Step 25->30: Reldo, Varrock castle library (spawned 3209,3495,0,
        -- areas/world/configs/m50_54.spawn:13, base symbol "reldo"). RETRY
        -- after b44ce7a2d: the dead [opnpc1,reldo_normal] trigger is gone --
        -- reldo.rs2's own [opnpc1,reldo] (the symbol m50_54.spawn:13 actually
        -- places) now owns the %twocats_quest 25..30 window and hands off to
        -- [proc,twocats_reldo_talk] -> [label,twocats_talk_to_reldo]
        -- (twocats.rs2:137-147), which opens with the PLAYER's line (trap
        -- 18) and, holding the amulet, sets %twocats_reldo=1 and
        -- %twocats_quest=30 with no further page.
        t.exec("goto-reldo", t.player.goto_tile, 3209, 3495, 0)
        t.exec("talkToReldo", t.player.talk_to, "reldo", 1)
        t.exec("talkToReldo-dialog", t.chat.play, {
            "player:I have a cat related question.",
        })
        t.expect("quest.stage.reldo_done", t.quest.expect_stage("reldo_done"))

        -- Step 30: find Bob again via the Catspeak amulet (e)'s Open op
        -- ([opheld3,twocats_amuletofcatspeak], twocats.rs2:78-87 ->
        -- [label,twocats_find_bob_2], twocats.rs2:153-155). The mesbox it
        -- opens SUSPENDS the calling script (trap 22) -- %twocats_quest=35
        -- is written only once the page is dismissed, so dismiss it before
        -- reading the stage: a bare read right after the click is the exact
        -- seam the earlier probe hit (build/quest_gate/atail_mn_unblocked2
        -- row 33 read back 30, not 35, because nothing there dismissed the
        -- page).
        t.exec("amulet.open2", t.player.inv_op, "twocats_amuletofcatspeak", 3)
        t.exec("amulet.open2-dialog", t.chat.play, {
            "mesbox:You use the Catspeak amulet (e) again",
        })
        t.expect("quest.stage.bob_found_again", t.quest.expect_stage("bob_found_again"))

        -- Step 35: the Sphinx in Sophanem (3302,2784,0,
        -- areas/world/configs/m51_43.spawn:46, symbol ics_little_sphinx).
        -- [opnpc1,ics_little_sphinx]'s owner is quest_dragonslayer2/scripts/
        -- dragonslayer2.rs2:1481-1499 (shared with Icthlarin's Little Helper
        -- and Dragon Slayer II on this same npc, trap 19's delegation
        -- idiom); at %twocats_quest 35..40 it hands off to twocats.rs2's own
        -- [label,twocats_talk_to_sphinx] (twocats.rs2:167-169), a player
        -- line with no npc reply.
        t.exec("goto-sphinx", t.player.goto_tile, 3302, 2784, 0)
        t.exec("talkToSphinx", t.player.talk_to, "ics_little_sphinx", 1)
        t.exec("talkToSphinx-dialog", t.chat.play, {
            "player:Ask the Sphinx for help for Bob.",
        })
        t.expect("quest.stage.chores_ready", t.quest.expect_stage("chores_ready"))

        -- Step 40: chores at Unferth's house. First chore is raking
        -- ObjectID.TWOCATS_PATCH (2919,3562,0, symbol twocats_patch) through
        -- [oploc2,twocats_patch] (twocats.rs2:188-199, op slot 2). configs/
        -- all.loc's own [twocats_patch] block (all.loc:103847-103858) and
        -- every one of its ten multiloc weed/potato children declare no
        -- op1/op2/... string at all -- there is no menu row for a real click
        -- to press, exactly the shape trap 20 names for brokeclockpole_red.
        -- Driven anyway to record the live evidence.
        t.exec("goto-patch", t.player.goto_tile, 2919, 3562, 0)
        local patch = t.player.by_symbol("loc", "twocats_patch")
        local rake_result, rake_detail = t.drive.click_minimenu(patch, 2)
        t.check("chore.rake_no_op", true, string.format(
            "click_minimenu(twocats_patch, 2) -> %s %q -- configs/all.loc's " ..
            "[twocats_patch] block (all.loc:103847-103858) and its ten " ..
            "multiloc weed/potato children declare no op string at all, so " ..
            "[oploc2,twocats_patch] (twocats.rs2:188-199), the quest's own " ..
            "first chore (raking Unferth's garden patch), has no menu row a " ..
            "real click can ever press. The same is true of " ..
            "[oploc2,twocats_bed] (make the bed, twocats.rs2:225-236 -- " ..
            "configs/all.loc's [twocats_bed] block has no op string either) " ..
            "and [opnpc2,twocats_unferth]/[opnpc2,twocats_unferth_longhair] " ..
            "(shear Unferth, twocats.rs2:308-324 -- every twocats_unferth* " ..
            "npc def in configs/all.npc declares only op1=Talk-to, no op2). " ..
            "Three of the chores' five triggered actions have zero live " ..
            "menu option between them.",
            tostring(rake_result), tostring(rake_detail)))

        t.blocked("CONTENT BUG: configs/all.loc's [twocats_patch] block " ..
            "(all.loc:103847-103858), [twocats_bed] block, and every " ..
            "twocats_unferth* def in configs/all.npc declare no numbered " ..
            "op string at all, so three of the five chores twocats.rs2's " ..
            "own [label,twocats_do_chores] window (twocats.rs2:180-324) " ..
            "wires through a numbered op -- [oploc2,twocats_patch] (rake, " ..
            "twocats.rs2:188-199), [oploc2,twocats_bed] (make bed, " ..
            "twocats.rs2:225-236), [opnpc2,twocats_unferth] (shear, " ..
            "twocats.rs2:308-324) -- have no menu row a real click can ever " ..
            "press; confirmed live on the rake row above. Even were all " ..
            "three reachable, nothing in this pack ever advances " ..
            "%twocats_chores_tidygarden past 4 (planted, twocats.rs2:218): " ..
            "twocats_patch is not one of skill_farming's registered " ..
            "patches.dbtable rows and no [queue,...] growth timer anywhere " ..
            "under server/scripts references it, so " ..
            "[label,twocats_wait_for_potatoes]'s own tidygarden<8 guard " ..
            "(twocats.rs2:330-337) can never pass. The quest cannot be " ..
            "completed by any sequence of real clicks in this content pack, " ..
            "from either seam alone, let alone both.")
        return
    end,
}
