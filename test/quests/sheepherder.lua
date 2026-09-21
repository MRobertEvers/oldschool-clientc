-- Sheep Herder quest test.
-- Scaffolded from Quest Helper's helpers/quests/sheepherder/, rewritten
-- against the real content scripts under
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_sheepherder/.
-- Tier (quest_inventory.tsv): 1.
--
-- Rewards (quest_sheepherder.rs2 [queue,sheepherder_complete]): 3100 coins,
-- 4 quest points, no item, no xp. No CHECK markers remain.
--
-- Fixture start: fresh_lumbridge.ini stands the player at 3206,3233,0
-- (Lumbridge, beside Hans). 100 coins is a genuine prerequisite -- Doctor
-- Orbon charges it for the plague suit and nothing in this quest's own
-- scripts lets the player earn coins, so it is given in setup, never
-- during run() (rule: setup STAGES, run() drives).
--
-- BLOCKED at the herd: [label,prod_sheep] (diseased_sheep.rs2) is meant to
-- nudge a wild plaguesheep_N one tile per click until it reaches the pen
-- gate zone. Three independently-tuned drives of that click (see the herd
-- section below) all leave %sheepherder_sheep_a at bit=0 -- see that
-- section's banner and the t.blocked() reason for the full evidence.
return {
    id = "sheepherder",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::sheepherder",
        "::give coins 100", -- Doctor Orbon's plague-suit price; not obtainable in-quest
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "sheepherderquest",
            constants = {
                complete = 3,
                not_started = 0,
                questpoints = 4,
                tasked_with_disposing_of_sheep = 2,
                tasked_with_talking_to_dr_orbon = 1,
            },
            row = "quest_sheepherder",
            display = "Sheep Herder",
            points = 4,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- setup cheats' effects are not client-side yet

        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Councillor Halgrive, outside the East Ardougne church.
        t.exec("goto-talkToHalgrive", t.player.goto_tile, 2615, 3298, 0)
        t.exec("talkToHalgrive", t.player.talk_to, "councillor_halgrive")
        t.exec("talkToHalgrive-dialog", t.chat.play, {
            "player:Hello.",
            "npc:I've been better.",
            "choose:What's wrong?",
            "player:What's wrong?",
            "npc:You may or may not be aware,",
            "npc:However, four sheep recently e",
            "npc:They believe that the sheep ha",
            "npc:As the councillor responsible ",
            "npc:Unfortunately nobody wants to ",
            "choose:I can do that for you.",
            "player:I can do that for you.",
            "npc:Y-you will??? That is excellen",
            "npc:Before you will be allowed to ",
            "player:Where can I find some protecti",
            "npc:Doctor Orbon wears it when con",
            "npc:Please also take this poisoned",
            "mesbox:The councillor gives you some ",
            "player:How will I know which sheep ar",
            "npc:The poor creatures have develo",
        })
        t.expect("quest.stage.tasked_with_talking_to_dr_orbon", t.quest.expect_stage("tasked_with_talking_to_dr_orbon"))
        t.check("halgrive.feed_granted", select(2, t.inv.count("poisoned_feed")) == 1, string.format("poisoned_feed count=%s after Halgrive's accept", tostring(select(2, t.inv.count("poisoned_feed")))))

        -- Doctor Orbon, in the chapel just north of the church. 100 coins
        -- (setup) buys the plague jacket and trousers outright, so the
        -- branch below is the successful-purchase page order, not the
        -- "not enough money" one.
        t.exec("goto-talkToOrbon", t.player.goto_tile, 2614, 3306, 0)
        t.exec("talkToOrbon", t.player.talk_to, "doctor_orbon")
        t.exec("talkToOrbon-dialog", t.chat.play, {
            "player:Hi Doctor.",
            "player:I need to acquire some protect",
            "npc:Protective clothing? I'm afrai",
            "npc:I suppose I could sell you thi",
            "choose:Ok, I'll take it.",
            "player:Ok, I'll take it.",
            "mesbox:You give Doctor Orbon 100 coin",
            "npc:These should protect you from",
        })
        t.expect("quest.stage.tasked_with_disposing_of_sheep", t.quest.expect_stage("tasked_with_disposing_of_sheep"))
        t.ticks(1) -- the purchase's inv_del/inv_add lands a tick after the mesbox page (measured run 1: read here was stale)
        t.check("orbon.gear_bought", select(2, t.inv.count("plague_jacket")) == 1 and select(2, t.inv.count("plague_trousers")) == 1, string.format("plague_jacket=%s plague_trousers=%s coins=%s after purchase", tostring(select(2, t.inv.count("plague_jacket"))), tostring(select(2, t.inv.count("plague_trousers"))), tostring(select(2, t.inv.count("coins")))))

        -- Wear the suit -- sheepherder_gate.rs2 and diseased_sheep.rs2 both
        -- check `worn`, not the backpack.
        t.exec("equip-jacket", t.player.equip, "plague_jacket")
        t.exec("equip-trousers", t.player.equip, "plague_trousers")

        -- The cattleprod is a ground item in Farmer Brumty's barn
        -- (m40_52.spawn), not handed over in any dialogue. click_obj answers
        -- `ok` with a nil detail on this pick-up (measured run 1), the
        -- hollow shape trap 12 names -- called directly, not through
        -- t.exec, with the before/after backpack count as the real detail.
        t.exec("goto-barn", t.player.goto_tile, 2604, 3357, 0)
        local cattleprod_before_result, cattleprod_before = t.inv.count("cattleprod")
        local pickup_result, pickup_detail = t.player.click_obj("cattleprod")
        local cattleprod_after_result, cattleprod_after = t.inv.count("cattleprod")
        t.step("pickupCattleprod", (pickup_result == "ok" and cattleprod_after_result == "ok" and cattleprod_after > (cattleprod_before or 0)) and "PASS" or "FAIL",
            string.format("click_obj -> %s (%s); cattleprod %s -> %s", tostring(pickup_result), tostring(pickup_detail), tostring(cattleprod_before), tostring(cattleprod_after)))
        t.exec("equip-cattleprod", t.player.equip, "cattleprod")

        -- Herd the first wild sheep into the enclosure. [label,prod_sheep]
        -- (diseased_sheep.rs2) is meant to nudge plaguesheep_1 one tile away
        -- from the player per click until its coordinate falls inside
        -- sheepherder_pen_gate, at which point it jumps the gate on its own
        -- -- read back as %sheepherder_sheep_a going from 0 to a nonzero bit
        -- lane. Three drives of that click, each a real change from the
        -- last, all left it at 0:
        --
        --   1. bare talk_to, 1-tick wait between clicks: settles
        --      "map_flag" every time -- the click only issues a walk, and
        --      "torirsserver: dropping [label,prod_sheep], which suspended
        --      while [label,prod_sheep] waits" appears once per click,
        --      i.e. the NEXT click is cancelling the previous invocation
        --      before p_arrivedelay (torirs_server_scripts.c SS_OP_P_ARRIVEDELAY)
        --      -- a one-tick suspend when the player moved that tick -- can
        --      clear.
        --   2. walk_near before every single click, 5-tick wait: every
        --      attempt times out outright, with no drop message at all --
        --      the repeated pre-walk leaves the player mid-step when the
        --      click fires.
        --   3. ONE walk_near to arrive (confirmed "already within 1"),
        --      THEN repeated plain talk_to from that standing tile with a
        --      full 8-tick settle window per attempt, 15 attempts: still
        --      every attempt times out. The FAIL screenshot
        --      (shots/43-herd.sheep1_in_pen-FAIL.png) shows the sheep has
        --      moved well off to the screen edge by the last attempt, so
        --      the clicks are doing SOMETHING to it, but never the one
        --      thing %sheepherder_sheep_a is gated on.
        --
        -- The same click_minimenu machinery talks to councillor_halgrive
        -- and doctor_orbon without incident from equal or greater range, so
        -- this is not a general approach/click problem in the driver --
        -- and the identical bit=0/timeout result was independently
        -- confirmed for plaguesheep_2, plaguesheep_3 and plaguesheep_4 in
        -- the same run (build/quest_gate/sheepherder/ledger.tsv, run 3:
        -- herd.sheep2_in_pen, herd.sheep3_in_pen, herd.sheep4_in_pen all
        -- FAIL the same way), so this is not sheep1-specific either.
        t.exec("goto-prodRedSheep", t.player.goto_tile, 2609, 3344, 0)
        local herd_target = t.player.by_symbol("npc", "plaguesheep_1")
        if herd_target then t.exec("walkNearRedSheep", t.player.walk_near, herd_target, 15) end
        local herd_result, herd_detail = "not_started", ""
        local herd_attempts = 0
        local herd_bit_result, herd_bit_value = t.var.varbit("sheepherder_sheep_a")
        while herd_bit_result == "ok" and herd_bit_value == 0 and herd_attempts < 15 do
            herd_result, herd_detail = t.player.talk_to("plaguesheep_1")
            t.ticks(8) -- a full settle window for p_arrivedelay + prod_sheep
            herd_attempts = herd_attempts + 1
            herd_bit_result, herd_bit_value = t.var.varbit("sheepherder_sheep_a")
        end

        if herd_bit_result == "ok" and herd_bit_value ~= 0 then
            t.check("herd.sheep1_in_pen", true,
                string.format("sheepherder_sheep_a bit=%s after %d prod click(s), last talk_to -> %s (%s)",
                    tostring(herd_bit_value), herd_attempts, tostring(herd_result), tostring(herd_detail)))
            t.finish(0)
            return
        end

        t.blocked(string.format(
            "diseased_sheep.rs2 [opnpc1,plaguesheep_1]/[label,prod_sheep]: talk_to('plaguesheep_1') answers ok every click (the player already standing adjacent, walk_near -> 'already within 1') but _settle_after_click times out and %%sheepherder_sheep_a stays bit=0 after %d attempts x 8 ticks each -- no BAAAAA/chat line, no bit write; the same result (timeout, bit stuck at 0) was independently confirmed for plaguesheep_2/3/4 in this file's own prior full run (ledger rows herd.sheep2_in_pen/herd.sheep3_in_pen/herd.sheep4_in_pen), while talk_to to councillor_halgrive and doctor_orbon PASS from equal or greater range with the same verb -- the seam is this wandering multinpc npc's prod interaction specifically, not the driver's click/approach machinery",
            herd_attempts))
        return
    end,
}
