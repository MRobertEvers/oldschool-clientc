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
-- (tramp.rs2, katrine.rs2, weaponsmaster.rs2), and drives ONLY the Black
-- Arm Gang half -- the tramp/Katrine/weapon-stash chain -- the Phoenix Gang
-- start (Reldo/book/Baraek/Straven) is never touched, because katrine.rs2's
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
-- WHY THIS STILL ENDS BLOCKED, past the cupboard/curator. The Shield of
-- Arrav's own finishing mechanic needs a SECOND player: the curator hands a
-- Black Arm player two `arravcertificate_rht` (this file's own outcome), but
-- `[opheldu,arravcertificate_lft]`/`[opheldu,arravcertificate_rht]` only fire
-- on the OTHER half (`last_useitem = arravcertificate_rht` / `_lft`
-- respectively) -- using one `arravcertificate_rht` on another is not a
-- wired trigger at all, it falls to `~displaymessage(^dm_default)`. A Phoenix
-- Gang partner is the only source of an `arravcertificate_lft`
-- (`curator_take_phoenix_half`), and the two gangs are mutually exclusive on
-- one character (this file's header already established that). This driver
-- has one client, so the combine, the queued completion, and
-- `quest.expect_complete()` are unreachable -- not a driver seam, the quest's
-- own two-player design, exactly as `blackarmgang_journal.rs2` and
-- `curator.rs2`'s own comment ("The two of you can then swap one of the
-- half-certificates to make a full one each") say.

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

        -- ---------------------------------------------------------- BLOCKED
        -- Past this point Shield of Arrav's OWN finishing mechanic needs a
        -- second player, not a driver workaround. curator.rs2's
        -- `curator_take_blackarm_half` grants two `arravcertificate_rht` --
        -- both the SAME half. quest_blackarmgang.rs2's
        -- `[opheldu,arravcertificate_lft]`/`[opheldu,arravcertificate_rht]`
        -- only fire when `last_useitem` is the OPPOSITE half (checked
        -- structurally: `last_useitem = arravcertificate_rht` inside the
        -- `_lft` trigger and vice versa) -- using one `arravcertificate_rht`
        -- on another matches neither `case`, so it falls to
        -- `~displaymessage(^dm_default)`; there is no `[opheldu,
        -- arravcertificate_rht]` handler for `last_useitem = arravcertificate_rht`
        -- at all. The only source of an `arravcertificate_lft` is a Phoenix
        -- Gang partner's OWN curator hand-in (`curator_take_phoenix_half`),
        -- and this file's header already established the two gangs are
        -- mutually exclusive on one character -- so no second client this
        -- driver could spin up would even be ABLE to hold the missing half.
        -- `[queue,blackarmgang_quest_complete]` is reached only from
        -- `arrav_combine_certificate`'s `inv_del`s, so `%blackarmgang` can
        -- never reach `complete` from here, and `quest.expect_complete()`
        -- would just be a longer way of proving the same seam. This is the
        -- quest's own two-player design (curator.rs2's own comment: "The two
        -- of you can then swap one of the half-certificates to make a full
        -- one each"), not something a different click sequence gets around.
        t.blocked("Shield of Arrav's completion is a two-player trade: curator_take_blackarm_half grants two arravcertificate_rht (both the SAME half), and [opheldu,arravcertificate_lft]/[opheldu,arravcertificate_rht] only fire on last_useitem being the OPPOSITE half -- using one arravcertificate_rht on another matches no case and falls to ^dm_default. The only source of an arravcertificate_lft is a Phoenix Gang partner's own curator_take_phoenix_half hand-in, and the two gangs are mutually exclusive on one character, so this single-client driver can never hold both halves to combine. %blackarmgang can never reach blackarmgang_complete from here, so quest.expect_complete() is unreachable -- the quest's own two-player design, not a driver seam.")
        return
    end,
}
