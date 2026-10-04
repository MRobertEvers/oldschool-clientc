# ghostsahoy: lobster food + graded hp row (seam matthew-mbp-m4-b56-seam1)

The giant lobster swings now (OSRS-Content 83c8fa8b73). The committed test still passes
(closer run: 400/0, hp 23 -> 14 across the fight), but from ~17 hp without food a probe
ended at 3 hp. Apply this to test/quests/ghostsahoy.lua (proved by s1gh_after2 263/0, gate green):

```diff
--- test/quests/ghostsahoy.lua	2026-10-03 15:21:33
+++ test/quests/ghostsahoy.lua	2026-10-03 16:09:36
@@ -44,6 +44,7 @@
         "::give logs 1",
         "::give tinderbox 1", -- lights the fire the nettle-water is boiled over
         "::give rune_scimitar 1", -- worn for the giant lobster
+        "::give lobster 4", -- food for the giant lobster: it swings back now (max hit 4), and the rock falls bring hp to ~17 first
         "::setlevel agility 25",
         "::setlevel cooking 20",
         "::setlevel attack 40",
@@ -519,13 +520,19 @@
         t.exec("killLobster.attack", t.player.attack, "giant_lobster", 2, 20)
         -- 30 hp at attack/strength 40 with a rune scimitar: ~75 ticks
         -- (build/quest_gate/s26gh_lob2: dead after 76). 60 was too short.
-        t.exec("killLobster.dead", t.npc.await_dead_engaged, 150, 6)
+        t.exec("killLobster.dead", t.npc.await_dead_engaged, 150, 6, { eat = { item = "lobster", below = 12 } })
         local lob_hp1 = player_hp()
-        -- Recorded, not graded: the b56 sampler saw hp flat through the whole fight, and a
-        -- probe (build/quest_gate/fixb56_ghost_lobster2, Defence 1, no Attack click) took one
-        -- 3-hp hit before auto-retaliate engaged and none in the ~25 ticks after -- a content
-        -- seam in the lobster's swing, reported, not fixed here.
-        t.note("player hp across the lobster fight " .. tostring(lob_hp0) .. " -> " .. tostring(lob_hp1) .. " /40")
+        -- Graded (seam ghostsahoy_giant_lobster_never_swings): the lobster is an aggressive
+        -- melee monster that keeps swinging until it dies (wiki Giant_lobster_(Ghosts_Ahoy)
+        -- oldid 15272821: Stab, speed 4, max hit 4). Before the fix it hit once and stopped
+        -- (b56 sampler, hp 23/40 flat). Margin: at least 2 hp lost across the fight, every
+        -- single hit at most 4 is the probe's to show (build/quest_gate/s1lob_after).
+        local _, food_left = t.inv.count("lobster")
+        local eaten = 4 - (tonumber(food_left) or 4)
+        t.expect("killLobster.player_hp",
+            (lob_hp0 and lob_hp1 and (lob_hp0 - lob_hp1 >= 2 or (eaten >= 1 and lob_hp0 >= 12))) and "ok" or "refused",
+            "player hp across the lobster fight " .. tostring(lob_hp0) .. " -> " .. tostring(lob_hp1)
+                .. " /40, lobsters eaten " .. eaten .. " (eaten only below 12) -- want a drop of >= 2 or a meal: the lobster keeps swinging")
 
         t.exec("searchChestAfterLobster", t.player.click_loc, "ahoy_chest_open", 1)
         t.exec("inv.scrap3", t.inv.await, "ahoy_map_scrap_3", 1, 5)
```
