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

        -- Step 25: Reldo, Varrock castle library (spawned 3209,3495,0,
        -- areas/world/configs/m50_54.spawn:13, base symbol "reldo").
        -- twocats.rs2:129 wires this quest's Reldo step to
        -- [opnpc1,reldo_normal], but reldo_normal (npc.compack "4243=
        -- reldo_normal", a multinpc1 child of [reldo]'s multivarbit=
        -- twocats_reldo -- all.npc:170573-170585) has ZERO *.spawn rows
        -- anywhere in this pack (confirmed: grep -rln reldo_normal turns up
        -- only .rs2/.constant sources, never a *.spawn file) -- exactly
        -- section 8's "a symbol that resolves in the compack but has zero
        -- live placements anywhere in the loaded world is content_bug".
        -- The npc that is actually placed and clickable is the BASE symbol
        -- "reldo", whose own trigger (areas/varrock/scripts/reldo.rs2:8-96)
        -- has no %twocats_quest branch at all -- unlike gertrude.rs2, which
        -- was given an explicit else to stand in for its own dead
        -- gertrude_post trigger (that file's own comment, lines 20-23),
        -- nothing in reldo.rs2 stands in for [opnpc1,reldo_normal] (docs/
        -- QUEST_AUTHORING.md trap 19: "the npc side has no child-then-base
        -- fallback yet"). Driven anyway to record the live evidence.
        t.exec("goto-reldo", t.player.goto_tile, 3209, 3495, 0)
        t.exec("talkToReldo", t.player.talk_to, "reldo", 1)
        local reldo_kind = t.chat.kind()
        local reldo_text_result, reldo_text = t.chat.text()
        t.check(
            "reldo.dead_trigger",
            true,
            string.format(
                "talk_to reldo opened kind=%s text=%q (reldo.rs2:24's " ..
                "generic \"Hello stranger.\" / trade-and-library small " ..
                "talk) -- wanted twocats.rs2:141's player line \"I have a " ..
                "cat related question.\"; reldo.rs2:8-96 never reads " ..
                "%%twocats_quest, so [opnpc1,reldo_normal] (twocats.rs2:" ..
                "129-138) is unreachable through the npc that actually " ..
                "spawns",
                tostring(reldo_kind), tostring(reldo_text_result == "ok" and reldo_text or reldo_text_result)
            )
        )
        t.expect("quest.stage.reldo_unmoved", t.quest.expect_stage("gertrude_done"))

        t.blocked("CONTENT BUG: OSRS-Content/osrs239-content/server/scripts/" ..
            "areas/varrock/scripts/reldo.rs2:8-96 [opnpc1,reldo] has no " ..
            "%twocats_quest branch, and quest_atailoftwocats/scripts/" ..
            "twocats.rs2:129-138's own [opnpc1,reldo_normal] trigger is " ..
            "registered against a child npc symbol (reldo_normal, a " ..
            "multinpc1 child of [reldo]'s multivarbit=twocats_reldo, all." ..
            "npc:170573-170585) that has zero *.spawn placements anywhere " ..
            "in this pack -- only the base symbol \"reldo\" is ever spawned " ..
            "(areas/world/configs/m50_54.spawn:13), and the npc op lookup " ..
            "has no child-then-base fallback (docs/QUEST_AUTHORING.md trap " ..
            "19), so this quest's Reldo step can never be reached by a real " ..
            "click. %twocats_quest is confirmed unmoved at gertrude_done " ..
            "(25) by the row above.")
        return
    end,
}
