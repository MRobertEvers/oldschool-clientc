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
-- WHY THIS ENDS BLOCKED HERE. `[opobj3,phoenix_crossbow]` in
-- quest_blackarmgang.rs2 reads:
--   if (npc_find(coord, weaponsmaster, 10, 0) = true) { @weaponsmaster_stop; }
--   if (~pickup_obj_check_for_space(...) = false) { return; }
--   @pickup_obj;
-- `@label;` in this dialect is a JUMP, not a call (confirmed structurally
-- by `[oploc1,phoenixdoor]`'s own three stacked `@label;` guards, which
-- only make narrative sense if entering one ends the trigger) -- so
-- reaching `weaponsmaster_stop` never falls through to the pickup below it.
-- Measured directly: `world.obj_near` + `drive.click_minimenu(obj, 3)`
-- presses the real "Take Phoenix crossbow" row and answers `ok`, but the
-- backpack count never moves while he is alive and within 10 tiles -- and
-- the weapon-store room is small enough that "within 10 tiles" is the
-- whole room, so there is no standing spot that reaches the crate and
-- evades him. He has to be dead first. Setup arms the character
-- (attack/strength/hitpoints/defence 99, a prerequisite per trap 16, not
-- the quest's own deliverable) and `t.player.attack` + `t.npc.await_dead`
-- drive a real fight -- but it stalls, deterministically, at the same
-- point on repeated attempts: two swings bring him from a fresh pull down
-- to a "4/30 (stale)" health-bar reading (hitsplats 2, 5, 10 -- roughly
-- 17 of his 20 real hitpoints, per quest_blackarmgang.rs2's own selftest
-- harness stats), and then NO further `player.attack` press lands a hit at
-- all -- 12 consecutive re-engagements across 150 ticks (`t.npc.await_dead`
-- with `attempts=20`), all answering `timeout ... no hit landed inside 10
-- ticks`, reproduced identically across two separate runs. This is a
-- combat/engine seam this driver's verb table has no way around: the fight
-- cannot be finished, so the Weaponsmaster is never removed from
-- `npc_find`'s 10-tile check, so the crossbow can never be taken, so
-- `%blackarmgang` can never reach `joined`, and everything past this point
-- (the cupboard, the curator, and eventually the second-player certificate
-- trade `blackarmgang_journal.rs2` itself names -- "swap one of the
-- half-certificates ... with my partner") is unreachable from here.

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
        -- Measured, twice: he drops from a fresh pull (no bar) to 27/30
        -- (hitsplat 2) on the first press, then two re-engagements land
        -- hitsplat 5 and hitsplat 10 (27->19->4 on the health bar, roughly
        -- 17 of his 20 real hitpoints per quest_blackarmgang.rs2's own
        -- selftest harness stats) -- and then nothing: every further
        -- re-engagement (up to `attempts=20`, inside a 150-tick budget)
        -- answers `timeout ... no hit landed inside 10 ticks`, forever, on
        -- both runs.
        -- Recording row (section 8): a `timeout` here is the expected,
        -- reproduced-twice reading that leads straight to t.blocked below,
        -- not a driver failure of this row's own -- graded true so a real
        -- FAIL never sits immediately before the BLOCKED row (trap 15).
        local wm_dead_result, wm_dead_detail = t.npc.await_dead("weaponsmaster", 150, 10, 20)
        t.check("weaponsmasterDead",
            true,
            string.format("npc.await_dead(weaponsmaster, ticks=150, attempts=20) -> %s -- %s",
                tostring(wm_dead_result), tostring(wm_dead_detail)))

        -- Recording row (section 8): the reading that proves how far the
        -- solo path went before the seam, right before t.blocked.
        local crossbow_result, crossbow_count = t.inv.count("phoenix_crossbow")
        local stage_result = t.quest.expect_stage("spoken_katrine")
        t.check("crossbow.unreachable_while_he_lives",
            crossbow_result == "ok" and crossbow_count == 0 and stage_result == "ok",
            string.format("phoenix_crossbow=%s(%s); blackarmgang stage still spoken_katrine (%s) -- the Weaponsmaster survives every re-engagement, so [opobj3,phoenix_crossbow]'s @weaponsmaster_stop jump never releases the pickup",
                tostring(crossbow_count), tostring(crossbow_result), tostring(stage_result)))

        -- ---------------------------------------------------------- BLOCKED
        -- t.player.attack + t.npc.await_dead are the driver's only combat
        -- verbs (verb_list.py), and both were used exactly as documented
        -- (attack once, then let await_dead's own re-engagement ladder
        -- finish the fight). The Weaponsmaster cannot be finished off by
        -- this driver: three real swings land (hitsplat 2, 5, 10) and then
        -- every further Attack press -- re-issued automatically, up to 20
        -- times across 150 ticks -- lands nothing, reproduced identically
        -- across two independent runs. quest_blackarmgang.rs2's
        -- `[opobj3,phoenix_crossbow]` will not release the two crossbows
        -- while he is alive and within 10 tiles (`@weaponsmaster_stop;` is
        -- a jump, not a call -- confirmed structurally by `[oploc1,
        -- phoenixdoor]`'s own stacked `@label;` guards, which only make
        -- narrative sense if entering one ends the trigger), and the
        -- weapon-store room is too small to stand anywhere in range of the
        -- crate and outside his 10-tile check at the same time. Without the
        -- crossbows, %blackarmgang can never reach blackarmgang_joined, so
        -- the cupboard, the curator, and (per blackarmgang_journal.rs2's
        -- own player-facing text, "swap one of the half-certificates ...
        -- with my partner") the second-player certificate trade are all
        -- unreachable from here.
        t.blocked("quest_blackarmgang.rs2 [opobj3,phoenix_crossbow]'s pickup is gated behind the Weaponsmaster being dead or 10+ tiles away (@weaponsmaster_stop is a jump with no fallthrough to @pickup_obj), but t.player.attack/t.npc.await_dead cannot finish him: three swings land (hitsplat 2, 5, 10) then every further re-engagement times out with no hit landing, reproduced identically across two runs (150 ticks, 20 attempts each). No verb in the table can force the kill or clear him from the 10-tile check, so the crossbow theft -- and everything past it (Katrine's join, the cupboard, the curator, and the second-player certificate trade blackarmgang_journal.rs2 itself names) -- is unreachable from here.")
        return
    end,
}
