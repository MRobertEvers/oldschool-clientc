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
-- BLOCKED at the Gertrude step: see the comment above that t.blocked() call.
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
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "twocats_quest",
            constants = {
                not_started = 0,
                accepted = 5,
                hild_done = 10,
                bob_found = 20,
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
        t.exec("goto-gertrude", t.player.goto_tile, 3151, 3410, 0)
        t.exec("talkToGertrude", t.player.talk_to, "gertrude", 1)
        -- Confirmed live (run 1): t.chat.text() reads the currently-open
        -- page, which is the PLAYER's own first line of gertrude.rs2's
        -- fluffs_not_started branch (line 12) -- "Hello, are you okay?",
        -- unique to that branch and absent from gertrude_route_topics'.
        local gertrude_text_result, gertrude_text = t.chat.text()
        t.check(
            "gertrude.wrong_branch",
            gertrude_text_result == "ok" and gertrude_text ~= nil
                and gertrude_text:find("Hello, are you okay?", 1, true) ~= nil,
            string.format(
                "talk_to gertrude read=%s text=%q -- gertrude.rs2:12, the " ..
                "Fluffs (Gertrude's Cat) quest's OWN fluffs_not_started " ..
                "intro, not the Bob's-parents topic chooser",
                tostring(gertrude_text_result), tostring(gertrude_text)
            )
        )

        -- CONTENT BUG, not a driver seam: areas/varrock/scripts/gertrude.rs2
        -- [opnpc1,gertrude] (lines 11-63) branches on %fluffs FIRST, in a
        -- chain that names every one of Fluffs' own states (not_started=0,
        -- started=1, paid_boy, gave_milk, gave_sardine, rescued=5) before
        -- its trailing `else { ~gertrude_route_topics; }` (line 62-63) --
        -- the only path that reaches this quest's own
        -- [proc,gertrude_route_topics] (gertrude.rs2:85-114), which is what
        -- gates the "Ask about Bob's parents" choice on 20<=%twocats_quest
        -- <=28 (gertrude.rs2:93-95) and calls
        -- ~twocats_gertrude_after_bob (twocats.rs2:118-120).
        -- A fresh character's %fluffs (osrs239 varp 180, quest_fluffs.varp,
        -- scope=perm, no [varps] row in fresh_lumbridge.ini) defaults to 0 =
        -- ^fluffs_not_started (quest_fluffs.constant:5), which matches
        -- gertrude.rs2's FIRST branch every time -- the else, and this
        -- quest's own Gertrude step, can only be reached once the wholly
        -- unrelated Fluffs/Gertrude's Cat quest is fully COMPLETE
        -- (%fluffs=^fluffs_complete=6, quest_fluffs.constant:11). Real OSRS's
        -- A Tail of Two Cats has no such prerequisite, and twocats.rs2's own
        -- comments (twocats.rs2:51-54, written about death_woman_indoors1 but
        -- naming gertrude_post/reldo_normal as the "same delegation idiom")
        -- assume %twocats_quest alone gates the handoff -- the author did not
        -- see the outer %fluffs branch in gertrude.rs2 that swallows it.
        -- Corroborated by the npc def itself: all.npc:91772 [gertrude] has
        -- `multivarp=fluffs`, multinpc1-6=gertrude_quest (fluffs 0-5),
        -- multinpc7=gertrude_post (fluffs=6 only) -- the "gertrude_post" name
        -- Quest Helper and twocats.rs2's own (dead) second trigger both use
        -- for this NPC literally never appears until Fluffs is done.
        t.blocked("areas/varrock/scripts/gertrude.rs2:11-63 [opnpc1,gertrude]: " ..
            "the Bob's-parents topic (gertrude_route_topics' twocats branch, " ..
            "gated on 20<=%twocats_quest<=28) is reachable only through this " ..
            "dispatch's trailing else, which itself requires %fluffs=" ..
            "fluffs_complete(6) -- the unrelated Fluffs/Gertrude's Cat quest " ..
            "finished. A fresh character's %fluffs defaults to 0 " ..
            "(fluffs_not_started) and always hits the first branch instead, " ..
            "so quest_atailoftwocats/scripts/twocats.rs2's own Gertrude step " ..
            "(twocats.rs2:108-120) can never be reached from a fresh run.")
        return
    end,
}
