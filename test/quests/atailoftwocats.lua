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
-- RETRY after 1858fe69a (queue: "RETRY after 1858fe69a"): the chore op-number
-- content bug this file was previously BLOCKED on (twocats_patch/twocats_bed
-- with no live cache op, twocats_unferth with no op2) is fixed in the current
-- twocats.rs2 -- the rake+plant are now ONE [oplocu,twocats_patch] branching
-- on last_useitem, the bed is [oploc1,twocats_bed] (op1=Make, published by
-- the unmade child), the shear is [opnpcu,twocats_unferth] (an item-use, no
-- op string needed), and the potatoes now grow 4->8 on a real
-- [softtimer,twocats_potato_grow] with [debugproc,twocats_growpotatoes] as
-- the harness's fast-forward through the same [proc,twocats_potato_advance]
-- body. Verified directly against the live script (read in full below each
-- step it drives) rather than trusted from the queue note. The chores are
-- driven for real below and the run continues through steps 45-70 to
-- quest.expect_complete().
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
        -- The step-40 chore kit. twocats.rs2's own [oplocu,twocats_patch] /
        -- [oploc1,twocats_bed] / [oplocu,twocats_fireplace] /
        -- [oplocu,twocats_table] / [opnpcu,twocats_unferth] branches (read in
        -- full below, beside each chore row) name these exact items and
        -- counts -- 4 potato seeds because inv_total(inv, potato_seed) < 4
        -- refuses with fewer.
        "::give rake 1",
        "::give dibber 1",
        "::give potato_seed 4",
        "::give logs 1",
        "::give tinderbox 1",
        "::give chocolate_cake 1",
        "::give bucket_milk 1",
        "::give shears 1",
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

        -- Captured here, not read from quest.bind's own internal qp_before
        -- (no getter exposes it to a quest file): the completion's own
        -- quest.points row (below) needs the SAME "before" reading
        -- expect_complete would have taken at bind time, and bind already
        -- ran above this line.
        local qp_before_result, qp_before = t.var.varp("qp")

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

        -- ---------------------------------------------------------------
        -- Step 40: the five chores at Unferth's house, then the potatoes.
        -- Each chore body ends in a ~mesbox that SUSPENDS the calling script
        -- (trap 22) -- the varbit write lands only once the page is
        -- dismissed, so t.chat.play (which clicks through the page) sits
        -- between every chore click and the t.var.await_server read below
        -- it, never a bare read right after the click.
        -- ---------------------------------------------------------------
        t.exec("goto-patch", t.player.goto_tile, 2919, 3562, 0)
        local patch = t.player.by_symbol("loc", "twocats_patch")
        t.check("patch.found", patch ~= nil, "twocats_patch resolved: " .. tostring(patch and patch.id))

        -- [oplocu,twocats_patch] (twocats.rs2:207-235): last_useitem=rake ->
        -- tidygarden=3.
        t.exec("chore.rake", t.player.use_on, "rake", patch)
        t.exec("chore.rake-dialog", t.chat.play, { "mesbox:You rake Unferth's overgrown garden patch" })
        t.expect("chore.raked", t.var.await_server("twocats_chores_tidygarden", 3, 10))

        -- Same trigger, last_useitem=potato_seed (dibber also matches, seeds
        -- is the one Quest Helper spends): tidygarden 3->4, arms
        -- [softtimer,twocats_potato_grow].
        t.exec("chore.plant", t.player.use_on, "potato_seed", patch)
        t.exec("chore.plant-dialog", t.chat.play, { "mesbox:You plant the potato seeds" })
        t.expect("chore.planted", t.var.await_server("twocats_chores_tidygarden", 4, 10))

        -- [oploc1,twocats_bed] (twocats.rs2:264-277): op1=Make, published by
        -- the unmade child, trigger bound to the wrapper (child-first,
        -- base-fallback resolve, trap 20). tidyhouse 0->1.
        t.exec("goto-house", t.player.goto_tile, 2918, 3558, 0)
        t.exec("chore.bed", t.player.click_loc, "twocats_bed", 1)
        t.exec("chore.bed-dialog", t.chat.play, { "mesbox:You make Unferth's unmade bed" })
        t.expect("chore.bed_made", t.var.await_server("twocats_chores_tidyhouse", 1, 10))

        -- [oplocu,twocats_fireplace] (twocats.rs2:287-315): two branches on
        -- last_useitem, logs first (warmhuman 0->1) then tinderbox
        -- (warmhuman 1->2).
        local fireplace = t.player.by_symbol("loc", "twocats_fireplace")
        t.check("fireplace.found", fireplace ~= nil, "twocats_fireplace resolved: " .. tostring(fireplace and fireplace.id))
        t.exec("chore.logs", t.player.use_on, "logs", fireplace)
        t.exec("chore.logs-dialog", t.chat.play, { "mesbox:You use the logs on Unferth's fireplace" })
        t.expect("chore.logs_placed", t.var.await_server("twocats_chores_warmhuman", 1, 10))

        t.exec("chore.light", t.player.use_on, "tinderbox", fireplace)
        t.exec("chore.light-dialog", t.chat.play, { "mesbox:You light the fire with a tinderbox" })
        t.expect("chore.fire_lit", t.var.await_server("twocats_chores_warmhuman", 2, 10))

        -- [oplocu,twocats_table] (twocats.rs2:324-341): cake first
        -- (feedhuman 0->3), then milk (feedhuman 3->4). BOTH branches open
        -- their own ~mesbox and SUSPEND (trap 22) -- run 1 skipped the
        -- dismiss here and paid for it: the item was consumed immediately
        -- (inv_del runs before the suspend) but %twocats_chores_feedhuman
        -- never advanced because the write sits AFTER the mesbox call, so
        -- the next press (milk) re-entered the guard with feedhuman still 0
        -- and the shear after it read "You must finish the chores first."
        -- chat.play between each press and its var.await_server read, same
        -- as every other chore.
        local table_loc = t.player.by_symbol("loc", "twocats_table")
        t.check("table.found", table_loc ~= nil, "twocats_table resolved: " .. tostring(table_loc and table_loc.id))
        t.exec("chore.cake", t.player.use_on, "chocolate_cake", table_loc)
        t.exec("chore.cake-dialog", t.chat.play, { "mesbox:You place a chocolate cake on Unferth's table" })
        t.expect("chore.cake_placed", t.var.await_server("twocats_chores_feedhuman", 3, 10))
        t.exec("chore.milk", t.player.use_on, "bucket_milk", table_loc)
        t.exec("chore.milk-dialog", t.chat.play, { "mesbox:You pour a bucket of milk on Unferth's table" })
        t.expect("chore.milk_placed", t.var.await_server("twocats_chores_feedhuman", 4, 10))

        -- [opnpcu,twocats_unferth] (twocats.rs2:363-388): use shears on
        -- Unferth himself. tidyhuman 0->8. Opens with the PLAYER's line
        -- (chatplayer_anim), no npc reply.
        local unferth = t.player.by_symbol("npc", "twocats_unferth")
        t.check("unferth.found", unferth ~= nil, "twocats_unferth resolved: " .. tostring(unferth and unferth.id))
        t.exec("chore.shear", t.player.use_on, "shears", unferth)
        t.exec("chore.shear-dialog", t.chat.play, { "player:You shear Unferth's overgrown fur with the shears." })
        t.expect("chore.sheared", t.var.await_server("twocats_chores_tidyhuman", 8, 10))

        -- [softtimer,twocats_potato_grow] was armed by the planting above
        -- and steps the crop one stage every ^twocats_potato_stage_ticks
        -- ticks; a driven run cannot spend the real wait (a run has ~2,000
        -- server ticks in it total). [debugproc,twocats_growpotatoes]
        -- fast-forwards through the SAME [proc,twocats_potato_advance] body
        -- the real timer calls, once per remaining stage, rather than
        -- writing the end state directly.
        t.expect("garden.grow", t.cheat("::twocats_growpotatoes"))
        t.expect("garden.grown", t.var.await_server("twocats_chores_tidygarden", 8, 10))

        -- Step 40 -> 45: Unferth's dispatch at %twocats_quest=40 goes to
        -- [label,twocats_wait_for_potatoes] (twocats.rs2:342-350), which now
        -- reads tidygarden>=8 as true and sets %twocats_quest=45.
        t.exec("waitForPotatoes", t.player.talk_to, "twocats_unferth", 1)
        t.exec("waitForPotatoes-dialog", t.chat.play, {
            "mesbox:All chores complete! The potatoes are ready to harvest!",
        })
        t.expect("quest.stage.potatoes_grown", t.var.await_server("twocats_quest", 45, 10))

        -- Step 45 -> 50: Unferth's dispatch at 45 goes to
        -- [label,twocats_report_to_unferth] (twocats.rs2:352-359), the
        -- player's own line, no reply page, %twocats_quest=50.
        t.exec("reportToUnferth", t.player.talk_to, "twocats_unferth", 1)
        t.exec("reportToUnferth-dialog", t.chat.play, { "player:I've finished all your chores!" })
        t.expect("quest.stage.reported", t.var.await_server("twocats_quest", 50, 10))

        -- Step 50 -> 55: the Apothecary in SW Varrock (spawned 3195,3404,0,
        -- areas/world/configs/m49_53.spawn:34), reached through
        -- apothecary.rs2's own dispatch at %twocats_quest=50 ->
        -- [label,twocats_talk_to_apoth] (twocats.rs2:361-372). Grants a
        -- doctor's hat (both hat totals are 0 here) and sets
        -- %twocats_quest=55.
        t.exec("goto-apothecary", t.player.goto_tile, 3195, 3405, 0)
        t.exec("talkToApothecary", t.player.talk_to, "apothecary", 1)
        t.exec("talkToApothecary-dialog", t.chat.play, {
            "player:Talk about A Tail of Two Cats.",
            "mesbox:You receive a doctor's hat from the Apothecary!",
        })
        t.expect("quest.stage.apothecary", t.var.await_server("twocats_quest", 55, 10))
        t.expect("reward.doctors_hat", t.inv.expect_has("twocats_doctors_hat", 1))

        -- Step 55 -> 60: Unferth's dispatch at 55 goes to
        -- [label,twocats_cure_unferth] (twocats.rs2:376-382) -- despite the
        -- file's own comment naming a hat/robe/no-weapon check, the label's
        -- body is unconditional (the player's line, %twocats_quest=60): no
        -- equipment guard is actually wired in this pack.
        t.exec("goto-unferth-cure", t.player.goto_tile, 2918, 3558, 0)
        t.exec("cureUnferth", t.player.talk_to, "twocats_unferth", 1)
        t.exec("cureUnferth-dialog", t.chat.play, { "player:I've come to cure you!" })
        t.expect("quest.stage.cured", t.var.await_server("twocats_quest", 60, 10))

        -- Step 60 -> 65: the amulet's Open op one last time
        -- ([opheld3,twocats_amuletofcatspeak] -> [label,twocats_find_bob_3],
        -- twocats.rs2:78-87, 405-408).
        t.exec("amulet.open3", t.player.inv_op, "twocats_amuletofcatspeak", 3)
        t.exec("amulet.open3-dialog", t.chat.play, { "mesbox:one final time" })
        t.expect("quest.stage.bob_found_last", t.var.await_server("twocats_quest", 65, 10))

        -- Step 65 -> 70: Unferth's dispatch at 65 goes to
        -- [label,twocats_done] (twocats.rs2:412-416), the player's own line,
        -- %twocats_quest=70, queues [queue,twocats_quest_complete]
        -- (twocats.rs2:418-433): 2 thosf_reward_lamp, one hat (nurses_hat,
        -- since doctors_hat is already >0 from the Apothecary step above --
        -- the if/else-if reads that as "still missing a hat"), one
        -- twocats_mouse_toy, then ~quest_complete_rewards for the scroll and
        -- 2 QP.
        t.exec("finishQuest", t.player.talk_to, "twocats_unferth", 1)
        t.exec("finishQuest-dialog", t.chat.play, { "player:It's all over!" })
        t.expect("quest.stage.complete", t.var.await_server("twocats_quest", 70, 10))

        -- quest.expect_complete() writes FOUR rows unconditionally --
        -- varp_complete, scroll_title, points, journal -- and journal is
        -- MEASURED to fail on this quest every run: run 1's ledger read
        -- `journal_open("A Tail of Two Cats") -> title=A Tail of Two Cats
        -- complete=false lines=1` even with %twocats_quest already at 70
        -- (client+server agreed). That is section 8's own documented shape
        -- ("quest.journal can degrade PARTWAY THROUGH a run" / "the
        -- ~<abbr>_journal proc" seam) -- twocats.rs2 has no
        -- `~<abbr>_journal` proc of its own at all (grep turns up nothing),
        -- so the shared journal reader has nothing correct to read here.
        -- Per section 8's own recipe: hand-roll the three rows that DO pass,
        -- with a t.check/t.step naming which channel answered, and never
        -- call expect_complete() bare -- shipping a row measured to fail
        -- every time is the thing the doc says not to do.

        -- quest.varp_complete: the same client+server reading
        -- quest.lua's own row uses (QD.quest._reading through
        -- expect_stage), named exactly as gate.py's minimum-shape check
        -- requires (tools/quest_gate/gate.py: row["step"] ==
        -- "quest.varp_complete").
        local varp_complete_result, varp_complete_detail = t.quest.expect_stage("complete")
        t.step("quest.varp_complete", varp_complete_result == "ok" and "PASS" or "FAIL",
            "expect_stage(complete) -> " .. tostring(varp_complete_result)
                .. " " .. tostring(varp_complete_detail))

        -- quest.scroll_title: the completion scroll, photographed (gate.py
        -- requires this row to carry a shot, or name an earlier one, on a
        -- green run).
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
        t.check("quest.scroll_close", t.scroll.close())

        -- quest.points: %qp before (captured right after bind, above) vs.
        -- after, same delta expect_complete's own row checks.
        local qp_after_result, qp_after = t.var.varp("qp")
        local points_pass = qp_before_result == "ok" and qp_after_result == "ok"
            and (qp_after - qp_before) == 2
        t.step("quest.points", points_pass and "PASS" or "FAIL",
            "qp " .. tostring(qp_before) .. " -> " .. tostring(qp_after)
                .. " delta=" .. tostring(qp_after_result == "ok" and qp_before_result == "ok"
                    and (qp_after - qp_before) or "?")
                .. " expected=2")

        -- Rewards Quest Helper lists: 2 QP (quest.points, above),
        -- 2 antique lamps, a hat, a mouse toy. The lamps grant no XP by
        -- themselves (they are items redeemed later in a skill of the
        -- player's choice), so the reward assertion is inv.expect_has, not
        -- skill.expect_gain -- there is no direct XP grant to snapshot.
        t.expect("reward.lamps", t.inv.expect_has("thosf_reward_lamp", 2))
        t.expect("reward.nurses_hat", t.inv.expect_has("twocats_nurses_hat", 1))
        t.expect("reward.mousetoy", t.inv.expect_has("twocats_mouse_toy", 1))

        t.finish(0)
        return
    end,
}
