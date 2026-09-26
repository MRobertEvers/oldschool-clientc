-- Shield of Arrav -- driven through the BLACK ARM GANG side as far as a
-- single client can actually go.
--
-- HOW THIS FILE DIFFERS FROM THE SCAFFOLD. `new_quest.py`'s output merged
-- BOTH of Quest Helper's alternate routes (Phoenix Gang AND Black Arm Gang)
-- into one linear script, with several CHECK-guessed dialogues resolved to
-- the wrong branch entirely (`talkToRoald`'s text was a different quest's
-- werewolf/Morytania line -- king_roald.rs2 has no such branch anywhere
-- near `%phoenixgang`/`%blackarmgang`). This file was rebuilt from scratch
-- by reading quest_blackarmgang.rs2 and every NPC script it calls into
-- (reldo.rs2, tramp.rs2, katrine.rs2, weaponsmaster.rs2). Reldo and the
-- shared "Shield of Arrav" book ARE driven for real below (RE-AUTHOR after
-- parity1n/1m, 2026-09-25/26: Reldo is not a gate on Charlie -- tramp.rs2
-- starts `%blackarmgang` unconditionally, no claimed gate -- but Quest
-- Helper's `startQuest`/`searchBookcase`/`talkToReldoAgain` steps are real
-- content this route can reach without ever joining the Phoenix Gang, since
-- `%phoenixgang` only reaches `spoken_reldo`, nowhere near
-- `phoenixgang_joined`). What this file still never touches is the actual
-- Phoenix Gang RECRUITMENT (Baraek/Straven/Jonny the Beard) -- katrine.rs2's
-- `[opnpc1,katrine]` refuses outright once `%phoenixgang >= ^phoenixgang_joined`
-- and the reverse is equally true on straven.rs2's `[opnpc1,straven]`
-- (`%blackarmgang >= ^blackarmgang_joined` -> `@straven_blackarmdog`) --
-- the two gangs are mutually exclusive for a single character, so there is
-- no route that visits both, and no route to the OTHER certificate half
-- `curator_take_blackarm_half` would eventually need anyway (see below).
--
-- RETRY after 68c5e8d9d. `p_opnpc` used to read the npc's menu verb through
-- an accessor gated on the record having a NAME, and every multinpc shell in
-- this cache is nameless (2,458 of them) -- so `[label,player_melee_attack]`'s
-- own `p_opnpc(2)` re-arm was silently dropped after the first swing and the
-- Weaponsmaster could never be finished (measured twice: hitsplat 2, 5, 10,
-- then every further re-engagement timed out for 150 ticks). `npc_menu_verb`
-- now resolves that verb child-then-base through the ungated row, so the
-- fight finishes for real. This file resumes from there: kill him, take both
-- crossbows through `[opobj3,phoenix_crossbow]`, hand them to Katrine
-- (`%blackarmgang` -> `joined`), open the Black Arm cupboard for the shield
-- half, and take it to the curator for two half-certificates.
--
-- HOW THE TWO-PLAYER FINISH IS DRIVEN, past the door/cupboard/curator. Shield
-- of Arrav's last mechanic is a TRADE, not a click: curator.rs2's
-- `curator_take_blackarm_half` hands a Black Arm player two
-- `arravcertificate_rht` -- both the SAME half -- and
-- quest_blackarmgang.rs2's `[opheldu,arravcertificate_lft]` /
-- `[opheldu,arravcertificate_rht]` each fire only when `last_useitem` is the
-- OPPOSITE half, so one character can never hold both. The only source of an
-- `arravcertificate_lft` is a Phoenix Gang partner's own
-- `curator_take_phoenix_half` hand-in, and the weapon-store door
-- (`phoenixdoor2`, `[oploc1,phoenixdoor2]`) likewise only opens for a Phoenix
-- Gang member or someone holding `phoenixkey2`, the key Straven hands out on
-- that same route -- and the two gangs are mutually exclusive on one
-- character (this file's header, above).
--
-- `[debugproc,blackarmgang_partner]` (quest_blackarmgang.rs2, content parity
-- pass 3 -- docs/quests/shield_of_arrav.md, docs/QUEST_SERVER_CHEATS.md's
-- two-player partner affordances table) performs exactly the two hand-offs a
-- real Phoenix Gang partner would make -- `inv_add(phoenixkey2, 1)` and
-- `inv_add(arravcertificate_lft, 1)`, each a no-op once already held, nothing
-- else -- standing in for the trade window a second client would open.
-- EVERYTHING after that debugproc is a real click: the actual
-- `[oplocu,phoenixdoor2]` key-unlock (`use_on(phoenixkey2, phoenixdoor2)`,
-- never a `::goto` past the lock), the crossbow theft, Katrine, the
-- cupboard, the curator hand-in, and the actual
-- `[opheldu,arravcertificate_lft]` combine. The leftover
-- `arravcertificate_rht` this file ends holding is the spare the partner
-- would have been given -- the real quest leaves it in your backpack too.
--
-- MEASURED (build/quest_gate/probe_arrav_3, 25 rows PASS): `~mesbox`
-- SUSPENDS, and `[label,arrav_combine_certificate]`'s two `inv_del`s and its
-- `inv_add(arravcertificate)` are the lines BELOW its `~mesbox` -- so the
-- backpack is untouched until the box is dismissed. The combine row's own
-- detail reads "backpack unchanged" and an `inv.await` placed before the
-- dismissal times out. Dismiss, THEN read.
--
-- RETRY after parity1b (docs/quests/shield_of_arrav.md, 2026-09-23). The
-- queue's last_failure named two raw-ladder stand-ins this file used to
-- carry: a `::give arravcertificate_lft 1` before the curator visit, and a
-- `goto_tile` straight into the weapon store that teleported past the
-- locked `phoenixdoor2` instead of unlocking it. Both are replaced below by
-- the real `::blackarmgang_partner` debugproc (grants both items the trade
-- would, before the door is ever approached) and a genuine
-- `use_on(phoenixkey2, phoenixdoor2)` press on the real loc.

return {
    id = "blackarmgang",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so items fit
        -- The Weaponsmaster (weaponsmaster.rs2) is wired to attack any
        -- non-Phoenix player who so much as reaches for a crossbow
        -- (`npc_setmode(opplayer2)`, hitpoints=20 attack/str/def=21/21/21
        -- per quest_blackarmgang.rs2's own selftest harness) -- AND
        -- `[opobj3,phoenix_crossbow]`'s `@weaponsmaster_stop;` is a JUMP,
        -- not a call: reaching it never falls back through to `@pickup_obj`
        -- (measured: the row text still reads "Take Phoenix crossbow" and
        -- `click_minimenu` still answers `ok`, but the backpack count never
        -- moves while he is alive and within 10 tiles). He has to be dead
        -- before the theft can land at all, so setup arms the character to
        -- kill him quickly -- a prerequisite fight (trap 16 exempts gearing
        -- up), not the quest's own deliverable: the crossbows are still
        -- taken through two real clicks.
        "::setlevel hitpoints 99",
        "::setlevel defence 99",
        "::setlevel attack 99",
        "::setlevel strength 99",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "blackarmgang",
            constants = {
                not_started = 0,
                started = 1,
                spoken_katrine = 2,
                joined = 3,
                complete = 4,
            },
            row = "quest_shieldofarrav",
            display = "Shield of Arrav",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)
        t.expect("quest.stage.not_started_reset", t.quest.expect_stage("not_started"))

        -- ------------------------------------------------------- Reldo x2
        -- Quest Helper's stage-0 steps (startQuest/searchBookcase/
        -- talkToReldoAgain) are the guide's SHARED start for both routes --
        -- Reldo never writes %blackarmgang (tramp.rs2 starts that route
        -- unconditionally, on its own, below), so he is not a gate on
        -- Charlie, but he IS real content this route passes through on a
        -- fresh character, and RE-AUTHOR after parity1n/1m says drive him
        -- for real rather than declare the shared start a gap.
        --
        -- `[opnpc1,reldo]` (areas/varrock/scripts/reldo.rs2:21) is keyed on
        -- the SPAWNED symbol -- m50_54.spawn:13 places the BASE "reldo",
        -- never the multinpc child `reldo_normal` Quest Helper's own
        -- NpcStep targets (trap 19) -- and `same_thing`'s multinpc-family
        -- walk credits either name against the guide step, so "reldo" is
        -- both the real click target and the one that satisfies coverage.
        -- Fresh character (%phoenixgang/%blackarmgang/%squire all
        -- not_started, no lost_tribe_brooch): the p_choice3 branch offers
        -- "I'm in search of a quest." as option 1 -> @reldo_phoenixstart.
        t.exec("goto-startQuest", t.player.goto_tile, 3209, 3495, 0)
        t.exec("startQuest", t.player.talk_to, "reldo", 1)
        t.exec("startQuest-dialog", t.chat.play, {
            "npc:Hello stranger.",
            "choose:I'm in search of a quest.",
            "player:I'm in search of a quest.",
            "npc:Hmmm. I don't",
            "npc:Let me think actually",
            "npc:Ah, yes. I think I have something",
            -- OSRS low-combat advisory (parity1n, 2026-09-25) is a plain
            -- ~mesbox ahead of the p_choice2_header confirm, gated on
            -- `~player_combat_level < 10` (reldo.rs2's own idiom, matching
            -- kaqemeex.rs2's, driven the same way in druid.lua) -- but THIS
            -- file's `setup` raises attack/strength/defence/hitpoints to 99
            -- before `run()` ever starts (needed to one-side the
            -- Weaponsmaster fight below), so by the time Reldo is talked to
            -- the character's combat level is already far past 10 and the
            -- advisory never fires. Measured run 1: entry here expecting
            -- `mesbox` read `options rows=Yes.|No.` instead -- the box was
            -- never shown, straight to the confirm.
            "choose:Yes.",
            "player:Of course.",
            "npc:Ah yes. I know.",
        })
        local phoenix_started_result, phoenix_started_value = t.var.server("phoenixgang")
        t.check("reldo.phoenixgangStarted",
            phoenix_started_result == "ok" and phoenix_started_value == 1,
            string.format("t.var.server(phoenixgang) -> %s %s, expected 1 (phoenixgang_started) after reldo_phoenixstart's Yes.",
                tostring(phoenix_started_result), tostring(phoenix_started_value)))

        -- `[oploc1,questbookcase]` (quest_blackarmgang.rs2:6) grants
        -- the_shield_of_arrav only while %phoenixgang = phoenixgang_started
        -- exactly, which the Reldo conversation above just set. Map decode
        -- (trap 29): m50_54.jl2 "0 12 37: 2402 10 3" -> 50*64+12,54*64+37 =
        -- 3212,3493,0, a few tiles from Reldo's own spawn.
        t.exec("goto-searchBookcase", t.player.goto_tile, 3212, 3493, 0)
        t.exec("searchBookcase", t.player.click_loc, "questbookcase", 1)
        t.exec("searchBookcase-dialog", t.chat.play, {
            "player:Aha!",
            "mesbox:You take the book from the bookcase.",
        })
        t.expect("shieldOfArravBook.taken", t.inv.await("the_shield_of_arrav", 1, 10))

        -- arrav_book.rs2's `[opheld1,the_shield_of_arrav]`: reading it while
        -- %phoenixgang = started advances it to read_book and plays three
        -- lore mesboxes. inv_op names its own press/detail (not the nil-
        -- detail hollow shape) but never which branch ran (section 8) --
        -- t.exec through the click, grade the STATE on the var read below,
        -- same idiom betweenarock.lua's/atailoftwocats.lua's own book/item
        -- opheld rows use.
        t.exec("readShieldOfArravBook", t.player.inv_op, "the_shield_of_arrav", 1)
        t.exec("readShieldOfArravBook-dialog", t.chat.play, {
            "mesbox:The Shield of Arrav by A. R. Wright.",
            "mesbox:In the year 143 of the fifth age",
            "mesbox:The thieves became Varrock's most powerful crime gang.",
        })
        local phoenix_read_result, phoenix_read_value = t.var.server("phoenixgang")
        t.check("reldo.phoenixgangReadBook",
            phoenix_read_result == "ok" and phoenix_read_value == 2,
            string.format("t.var.server(phoenixgang) -> %s %s, expected 2 (phoenixgang_read_book) after reading the book",
                tostring(phoenix_read_result), tostring(phoenix_read_value)))

        -- Back to Reldo: the top-of-script guard
        -- `%phoenixgang = ^phoenixgang_read_book -> @reldo_read_book` fires
        -- before the ordinary menu this time, naming Baraek (Phoenix Gang)
        -- and Charlie the Tramp (Black Arm Gang) -- no "Hello stranger."
        -- opener, the guard short-circuits ahead of it.
        t.exec("goto-talkToReldoAgain", t.player.goto_tile, 3209, 3495, 0)
        t.exec("talkToReldoAgain", t.player.talk_to, "reldo", 1)
        t.exec("talkToReldoAgain-dialog", t.chat.play, {
            "npc:Then perhaps you now have your quest?",
            "player:I think I do.",
            "npc:No, I don't. However, I hear Baraek",
            "player:And the Black Arm Gang?",
            "npc:There are rumours that they are based in the south west corner",
            "player:Thanks! I'll get to it!",
            "npc:Good luck.",
        })
        local phoenix_spoken_result, phoenix_spoken_value = t.var.server("phoenixgang")
        t.check("reldo.phoenixgangSpokenReldo",
            phoenix_spoken_result == "ok" and phoenix_spoken_value == 3,
            string.format("t.var.server(phoenixgang) -> %s %s, expected 3 (phoenixgang_spoken_reldo) -- Reldo named both Baraek and Charlie the Tramp",
                tostring(phoenix_spoken_result), tostring(phoenix_spoken_value)))

        -- ------------------------------------------------ Charlie the tramp
        -- tramp.rs2's `[opnpc1,tramppg]` (south Varrock, by the alleyway):
        -- spawn row m50_52.spawn "tramppg 3208 3391 0".
        t.exec("goto-talkToCharlie", t.player.goto_tile, 3208, 3391, 0)
        t.exec("talkToCharlie", t.player.talk_to, "tramppg", 1)
        t.exec("talkToCharlie-dialog", t.chat.play, {
            "npc:Spare some change guv",
            "choose:Is there anything down this alleyway?",
            "player:Is there anything down this alleyway",
            "npc:Yes, there is actually",
            "choose:Do you think they would let me join?",
            "player:Do you think they would let me join",
            "npc:You never know",
            "npc:But don't upset her",
        })
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- ------------------------------------------------------- Katrine
        -- katrine.rs2's `[opnpc1,katrine]`, `katrine_standard` -> the
        -- "I've heard you're the Black Arm Gang." arm (only offered once
        -- %blackarmgang = started, which the tramp step above just set) ->
        -- `katrine_gangmember` -> "Ok, no problem." sets
        -- %blackarmgang = spoken_katrine. Spawn row m49_52.spawn
        -- "katrine 3186 3385 0".
        t.exec("goto-talkToKatrine", t.player.goto_tile, 3186, 3385, 0)
        t.exec("talkToKatrine", t.player.talk_to, "katrine", 1)
        t.exec("talkToKatrine-dialog", t.chat.play, {
            "player:What is this place?",
            "npc:It's a private business",
            "choose:I've heard you're the Black Arm Gang.",
            "player:I've heard you're the Black Arm Gang",
            "npc:Who told you that?",
            "choose:It was the tramp outside.",
            "player:It was the tramp outside",
            "npc:Is that guy still out there?",
            "npc:So now you've found us",
            "choose:I want to become a member of your gang.",
            "player:I want to become a member of your gang",
            "npc:How unusual.",
            "npc:Normally we recruit",
            "npc:How can I be sure",
            "choose:Well, you can give me a try can't you?",
            "player:Well, you can give me a try",
            "npc:I'm not so sure.",
            "npc:Thinking about it",
            "npc:Our rival gang",
            "npc:We're fresh out of crossbows",
            "npc:Then I'll be happy",
            "choose:Ok, no problem.",
            "player:Ok, no problem",
            "npc:Great! You'll find",
        })
        t.expect("quest.stage.spoken_katrine", t.quest.expect_stage("spoken_katrine"))

        -- ------------------------------------------- the partner's hand-off
        -- `::blackarmgang_partner` (docs/QUEST_SERVER_CHEATS.md's two-player
        -- partner affordances table; quest_blackarmgang.rs2) performs
        -- exactly the two hand-offs a real Phoenix Gang partner would make
        -- -- `phoenixkey2` (the weapon-store key) and `arravcertificate_lft`
        -- (the Phoenix half) -- nothing else, each a no-op if already held.
        -- Called here, BEFORE the weapon store, because the key is needed to
        -- unlock `phoenixdoor2` below; the certificate half rides along
        -- unused until the combine near the curator. `getWeaponStoreKey` is
        -- a genuinely two-player guide step ("Get the weapon storeroom key
        -- from another player"): `phoenixkey2` is granted only on the
        -- Phoenix route (areas/varrock/scripts/straven.rs2:88), no
        -- single-client path reaches it, and `::blackarmgang_partner`
        -- performs exactly that hand-off (docs/QUEST_SERVER_CHEATS.md's
        -- two-player partner affordances table) -- same idiom as
        -- tradeCertificateHalf below.
        -- PARTNER: getWeaponStoreKey ::blackarmgang_partner a two-player step (get the key from another player); phoenixkey2 is granted only on the Phoenix route (areas/varrock/scripts/straven.rs2:88) with no single-client path -- ::blackarmgang_partner performs exactly that hand-off
        local partner_result, partner_detail = t.cheat("::blackarmgang_partner")
        local partner_key_await = t.inv.await("phoenixkey2", 1, 10)
        local partner_key_result, partner_key_count = t.inv.count("phoenixkey2")
        local partner_cert_await = t.inv.await("arravcertificate_lft", 1, 10)
        local partner_cert_result, partner_cert_count = t.inv.count("arravcertificate_lft")
        t.check("stage.partnerHandoff",
            partner_result == "ok"
                and partner_key_await == "ok" and partner_key_result == "ok" and (partner_key_count or 0) >= 1
                and partner_cert_await == "ok" and partner_cert_result == "ok" and (partner_cert_count or 0) >= 1,
            string.format("::blackarmgang_partner -> %s (%s); phoenixkey2 await=%s count=%s(%s); "
                .. "arravcertificate_lft await=%s count=%s(%s)",
                tostring(partner_result), tostring(partner_detail),
                tostring(partner_key_await), tostring(partner_key_count), tostring(partner_key_result),
                tostring(partner_cert_await), tostring(partner_cert_count), tostring(partner_cert_result)))

        -- ------------------------------------------------------ phoenixdoor2
        -- `[oploc1,phoenixdoor2]` refuses "The door is securely locked."
        -- with no key held; `[oplocu,phoenixdoor2]` unlocks it for real when
        -- `last_useitem = phoenixkey2` (m50_52.jl2:570, "0 51 58: 2398 0 3"
        -- decodes to 3251,3386,0 -- trap 29's map-text decode, since a `.loc`
        -- config carries no coordinate). Stand on the street outside it and
        -- press the real trigger -- no `::goto` past the lock.
        t.exec("goto-phoenixdoor2", t.player.goto_tile, 3251, 3389, 0)
        local phoenixdoor2_target = t.player.by_symbol("loc", "phoenixdoor2")
        t.exec("unlockPhoenixDoor2", t.player.use_on, "phoenixkey2", phoenixdoor2_target)
        -- The door's own confirmation is a bare `mes()` line (`[proc,
        -- open_hideout_door]`'s `$message = "You unlock the door."`), a
        -- chat-LOG line, never a page -- read it back with `msg.expect`
        -- rather than trusting the settle's `map_flag` arm alone.
        t.expect("unlockPhoenixDoor2.message", t.msg.expect("You unlock the door."))

        -- ------------------------------------------------ steal 2 crossbows
        -- quest_blackarmgang.rs2's `[opobj3,phoenix_crossbow]`:
        --   if (npc_find(coord, weaponsmaster, 10, 0) = true) { @weaponsmaster_stop; }
        -- `@label;` in this dialect is a JUMP, not a call -- reaching
        -- weaponsmaster_stop never falls back through to the pickup below it
        -- (measured: click_minimenu presses the real "Take Phoenix
        -- crossbow" row and answers ok, but the backpack count never moves
        -- while he is alive and within 10 tiles -- the room is small enough
        -- that "within 10 tiles" is the whole room). He has to be dead
        -- first; setup's attack/strength/hitpoints/defence boost is what
        -- makes that a short, one-sided fight (a prerequisite, trap 16),
        -- not a substitute for the real clicks that steal the crossbows.
        --
        -- Ordinary floor travel from the unlocked door's entry room, not a
        -- second gate: `fai_varrock_ladder` (m50_52.jl2:3196, "0 52 56:
        -- 11794 10" -> 3252,3384,0) climbs to the Weaponsmaster's own floor,
        -- and `goto_tile`'s destination-level-climbs-stairs-for-you rule
        -- (QUEST_AUTHORING.md section 2) is exactly this case -- the lock
        -- above was the real gate and it is already open.
        t.exec("goto-weaponStore", t.player.goto_tile, 3252, 3384, 1)

        t.exec("attackWeaponsmaster", t.player.attack, "weaponsmaster")
        -- RETRY after 68c5e8d9d: npc_menu_verb now resolves the Weaponsmaster's
        -- (a nameless multinpc shell) menu verb child-then-base through the
        -- ungated row, so `[label,player_melee_attack]`'s own `p_opnpc(2)`
        -- re-arm reaches him and the fight finishes for real -- no more
        -- stall at hitsplat 10.
        local wm_dead_result, wm_dead_detail = t.npc.await_dead("weaponsmaster", 150, 10, 20)
        t.expect("weaponsmasterDead", wm_dead_result, wm_dead_detail)

        -- quest_blackarmgang.rs2's `[opobj3,phoenix_crossbow]` reads
        -- `npc_find(coord, weaponsmaster, 10, 0)` -- the NPC POOL, not the
        -- health bar -- so confirm he has actually left it (not just hit 0)
        -- before pressing the crate. Hollow: `ok`/`timeout`, nil detail
        -- (QUEST_AUTHORING.md trap 12/section 8) -- t.expect, not t.exec.
        local wm_gone_result = t.npc.await_gone("weaponsmaster", 10, 10)
        t.expect("weaponsmaster.gone", wm_gone_result)

        -- ------------------------------------------------ take 2 crossbows
        -- Two separate ground stacks, one crossbow each (m50_52.spawn:
        -- "phoenix_crossbow 3243 3383 1" and "phoenix_crossbow 3245 3385 1").
        -- click_obj answers `ok` with a NIL detail (section 8's fourth
        -- hollow verb, alongside trap 12's three) -- call it directly and
        -- write the counts read back ourselves, same as every other quest
        -- file's click_obj row (betweenarock.lua, cog.lua, haunted.lua, ...).
        local cb0_result, cb0_count = t.inv.count("phoenix_crossbow")
        local take1_result = t.player.click_obj("phoenix_crossbow", 3)
        local cb1_result, cb1_count = t.inv.count("phoenix_crossbow")
        t.check("takeCrossbow1",
            take1_result == "ok" and cb1_result == "ok" and (cb1_count or 0) > (cb0_count or 0),
            string.format("click_obj(phoenix_crossbow,3) -> %s; phoenix_crossbow %s(%s) -> %s(%s)",
                tostring(take1_result), tostring(cb0_count), tostring(cb0_result),
                tostring(cb1_count), tostring(cb1_result)))

        local take2_result = t.player.click_obj("phoenix_crossbow", 3)
        local cb2_result, cb2_count = t.inv.count("phoenix_crossbow")
        t.check("takeCrossbow2",
            take2_result == "ok" and cb2_result == "ok" and (cb2_count or 0) > (cb1_count or 0),
            string.format("click_obj(phoenix_crossbow,3) -> %s; phoenix_crossbow %s(%s) -> %s(%s)",
                tostring(take2_result), tostring(cb1_count), tostring(cb1_result),
                tostring(cb2_count), tostring(cb2_result)))

        t.expect("crossbows.two", t.inv.expect_has("phoenix_crossbow", 2))

        -- ----------------------------------------------------- join Katrine
        -- katrine.rs2's `[label,katrine_got_yet]`: >=2 phoenix_crossbow ->
        -- gives them to Katrine, %blackarmgang = ^blackarmgang_joined.
        t.exec("goto-handInKatrine", t.player.goto_tile, 3186, 3385, 0)
        t.exec("handInKatrine", t.player.talk_to, "katrine", 1)
        t.exec("handInKatrine-dialog", t.chat.play, {
            "npc:Have you got those crossbows",
            "player:Yes, I have.",
            "mesbox:You give the crossbows to Katrine.",
            "npc:You can join our gang now",
        })
        t.expect("quest.stage.joined", t.quest.expect_stage("joined"))

        -- -------------------------------------------------------- cupboard
        -- `[oploc1,blackarmcupboardshut]` (WorldPoint 3189,3386,1, Quest
        -- Helper's `getShieldFromCupboard`): one click does the open AND the
        -- search -- unlike Jerico's two-stage cupboard, there is only ever
        -- one trigger here -- and grants `arravshield2` behind its own
        -- `~mesbox`. goto_tile climbs the base's stairs directly (section 2).
        t.exec("goto-cupboard", t.player.goto_tile, 3189, 3386, 1)
        t.exec("cupboard.search", t.player.click_loc, "blackarmcupboardshut", 1)
        t.exec("cupboard.dismiss", t.chat.play, {
            "mesbox:You find half a shield, which you take.",
        })
        -- The grant lands inside the mesbox's own script, transmitted only
        -- at the tick boundary chat.play already waits out (trap 25) --
        -- poll anyway, then read the count back (trap 12's habit: inv.await
        -- answers ok with a nil detail too).
        local shield2_await_result = t.inv.await("arravshield2", 1, 10)
        local shield2_count_result, shield2_count = t.inv.count("arravshield2")
        t.step("shield2.taken",
            (shield2_await_result == "ok" and shield2_count_result == "ok" and (shield2_count or 0) >= 1) and "PASS" or "FAIL",
            string.format("inv.await(arravshield2,1,10) -> %s; inv.count(arravshield2) -> %s (%s)",
                tostring(shield2_await_result), tostring(shield2_count_result), tostring(shield2_count)))

        -- ---------------------------------------------------------- curator
        -- curator.rs2's `[opnpc1,curator]`: %blackarmgang>=joined &
        -- <complete & arravshield2>0 jumps straight to
        -- `@curator_take_blackarm_half` -- no menu, one direct branch.
        -- Spawn row m50_53.spawn "curator 3257 3447 0".
        t.exec("goto-talkToHaig", t.player.goto_tile, 3257, 3447, 0)
        t.exec("talkToHaig", t.player.talk_to, "curator", 1)
        t.exec("talkToHaig-dialog", t.chat.play, {
            -- [opnpc1,curator]'s own opening line runs unconditionally
            -- BEFORE the digplainletter/certificate/shield-half guard chain
            -- that jumps to @curator_take_blackarm_half -- it is a real
            -- first page, not decoration (trap 18 is about which SIDE opens
            -- a branch, not about a page ahead of the branch).
            "npc:Welcome to the museum of Varrock.",
            "player:Hello there. I'm here about the Shield of Arrav.",
            "npc:The Museum has been searching for that",
            "player:Well, I'm here to claim it.",
            "npc:You've found the shield? Let's have a look!",
            "mesbox:You show the shield half to the curator.",
            "npc:This is incredible! But where's the other half?",
            "player:I obtained this half from the Black Arm Gang.",
            "npc:That does sound plausible.",
            "player:So will I be rewarded for recovering half of the shield?",
            "npc:I'm afraid the reward is for the recovery of the full shield.",
            "npc:The two of you can then swap one of the half-certificates",
            "mesbox:The curator gives you two half-certificates.",
        })
        local shield2_gone_await = t.inv.await("arravcertificate_rht", 2, 10)
        local cert_result, cert_count = t.inv.count("arravcertificate_rht")
        local shield2_after_result, shield2_after_count = t.inv.count("arravshield2")
        t.step("curator.certificates",
            (shield2_gone_await == "ok" and cert_result == "ok" and (cert_count or 0) >= 2
                and shield2_after_result == "ok" and (shield2_after_count or 0) == 0) and "PASS" or "FAIL",
            string.format("inv.await(arravcertificate_rht,2,10) -> %s; arravcertificate_rht=%s(%s); arravshield2=%s(%s) (curator_take_blackarm_half: del shield2, add 2x cert_rht)",
                tostring(shield2_gone_await), tostring(cert_count), tostring(cert_result),
                tostring(shield2_after_count), tostring(shield2_after_result)))

        -- ------------------------------------------- the partner's half, held
        -- Already handed over by `::blackarmgang_partner` before the weapon
        -- store (this file's header) -- confirm it is still carried, rather
        -- than cheating it in a second time here.
        -- PARTNER: tradeCertificateHalf ::blackarmgang_partner a two-player step (trade one certificate half for the other with another player); arravcertificate_lft is granted only on the Phoenix route by curator_take_phoenix_half (areas/varrock/scripts/curator.rs2:183) with no single-client path -- ::blackarmgang_partner performs exactly that hand-off, driven above before the weapon store
        local partner_count_result, partner_count = t.inv.count("arravcertificate_lft")
        t.check("stage.partnerHalfHeld",
            partner_count_result == "ok" and (partner_count or 0) >= 1,
            string.format("inv.count(arravcertificate_lft) -> %s (%s) -- granted earlier by "
                .. "::blackarmgang_partner, the Phoenix partner's spare half traded in the real "
                .. "two-player quest",
                tostring(partner_count), tostring(partner_count_result)))

        -- ------------------------------------------------- combine the two
        -- The real `[opheldu,arravcertificate_lft]` dispatch: one half used
        -- on the other, `last_useitem` being the opposite half, so
        -- `@arrav_combine_certificate` is taken rather than
        -- `~displaymessage(^dm_default)`.
        t.exec("combineCertificates", t.player.use_item_on_item,
            "arravcertificate_lft", "arravcertificate_rht")
        -- The mesbox is the SUSPENSION the inv lines sit under (header).
        t.exec("combineCertificates-dismiss", t.chat.play, {
            "mesbox:You combine the two halves to make a complete certificate.",
        })
        local full_await_result = t.inv.await("arravcertificate", 1, 10)
        local full_result, full_count = t.inv.count("arravcertificate")
        local lft_left_result, lft_left = t.inv.count("arravcertificate_lft")
        local rht_left_result, rht_left = t.inv.count("arravcertificate_rht")
        t.step("combine.certificate",
            (full_await_result == "ok" and full_result == "ok" and (full_count or 0) == 1
                and lft_left_result == "ok" and (lft_left or 0) == 0
                and rht_left_result == "ok" and (rht_left or 0) == 1) and "PASS" or "FAIL",
            string.format("inv.await(arravcertificate,1,10) -> %s; arravcertificate=%s(%s); "
                .. "arravcertificate_lft=%s(%s) (consumed); arravcertificate_rht=%s(%s) "
                .. "(the spare half the partner would have been given) -- "
                .. "arrav_combine_certificate: del lft, del rht, add arravcertificate",
                tostring(full_await_result), tostring(full_count), tostring(full_result),
                tostring(lft_left), tostring(lft_left_result),
                tostring(rht_left), tostring(rht_left_result)))

        -- ------------------------------------------------------ King Roald
        -- king_roald.rs2's `[opnpc1,king_roald]` falls through its additive
        -- quest branches to `@roald_arrav_dialogue`, which, with an
        -- `arravcertificate` in the backpack and `%blackarmgang` at joined,
        -- plays the claim and `queue(blackarmgang_quest_complete, 0, 0)`.
        -- Spawn row m50_54.spawn "king_roald 3222 3472 0".
        local coins_before_result, coins_before = t.inv.count("coins")

        t.exec("goto-talkToRoald", t.player.goto_tile, 3222, 3472, 0)
        t.exec("talkToRoald", t.player.talk_to, "king_roald", 1)
        t.exec("talkToRoald-dialog", t.chat.play, {
            "player:Greetings, your majesty.",
            "player:I have come to claim the reward",
            "mesbox:You show the certificate to the king.",
            "npc:My goodness! This claim is for the reward",
            "npc:I never thought I'd see anyone claim this reward!",
            "npc:I see you are claiming half the reward",
            "mesbox:You hand over a certificate.",
        })

        -- `[queue,blackarmgang_quest_complete]` runs on a LATER tick than the
        -- click that queued it (section 8's completion-is-asynchronous rule):
        -- it writes %blackarmgang, deletes the certificate, adds the coins and
        -- paints the scroll.
        t.ticks(3)
        t.expect("quest.stage.complete", t.quest.expect_stage("complete"))

        -- The reward this pack actually grants, beside the scroll's own text:
        -- `inv_del(inv, arravcertificate, 1); inv_add(inv, coins, 600)`.
        local coins_after_result, coins_after = t.inv.count("coins")
        local cert_spent_result, cert_spent = t.inv.count("arravcertificate")
        t.check("reward.coins",
            coins_before_result == "ok" and coins_after_result == "ok"
                and type(coins_before) == "number" and type(coins_after) == "number"
                and coins_after == coins_before + 600
                and cert_spent_result == "ok" and (cert_spent or 0) == 0,
            string.format("coins %s -> %s (delta %s, expected 600 from "
                .. "[queue,blackarmgang_quest_complete]'s inv_add(inv, coins, 600)); "
                .. "arravcertificate after = %s (%s), expected 0 -- the same queue's "
                .. "inv_del hands it over",
                tostring(coins_before), tostring(coins_after),
                tostring((type(coins_after) == "number" and type(coins_before) == "number")
                    and (coins_after - coins_before) or "n/a"),
                tostring(cert_spent), tostring(cert_spent_result)))

        t.quest.expect_complete()
        t.finish(0)
    end,
}
