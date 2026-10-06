# prayer plan fix: legends + mm

## 2026-10-04 step 0
- branches ok (both matthew-mbp-m4-b56). parent HEAD 8ed828053, content a0ffd99cb2. waves 457041eec / content c93c574f20.
- read RESULTS.md + fail.txt of v3merge evidence.

## step 1 analysis (2026-10-04)
- legends: only protected stretch = leg 10 final fight (San/Irvig/Ranalph/Nezikchened). today's v3: prayer 19 at leg.10.pack (regen from 2), fights 121/113/147/269 ticks, demon OUT OF shark lowest 37.
  leg 10 inventory full: ::give shark 12 lands 8, ::give 4doseprayerrestore 2 lands NOTHING (count 2 = leg 4's). Fix: drop spent lockpicks/swamprocks/pickaxe before the pack.
  nezikchened.rs2:189 final spawn does stat_sub(prayer,0,75) -> drink AFTER the demon is present.
  prayer 60 (QH req 42), potion = 7+60/4 = 22/dose. plan: drink to >=50 before totem, top-up >=45 per hero, >=55 after demon present; protect off after.
- mm: missiles on at row 73 never turned off -> 52 pts gone by ~cum 660. ravine = ~42 ticks, then jail (no archers). Plan: missiles off after each ravine knockout; drink before temple/ninja/gorilla/zombie/demon with asserting rows; melee off at the child and after the zombie.
  mm demon today: OUT OF lobster, lowest 39 -> margin row 'food left' may fail. watch.
- tool: build/orchestrator/fix_b56/prayer_ledger.py LEDGER regex lo hi width (cum ticks)

## step 2 edits written (uncommitted, test/quests/{legends,mm}.lua), lint clean, luac ok
- legends: leg 10 drops junk before the pack, leg.10.pack-prayer (doses>=10), useTotemOnTotem-drink (>=50), hero() per fighter: -drink(want) -protect -prayer(need) -margin; defeatDemon-protectOff; margin rows on leg 4/8 Nezikchened.
- mm: enterValley-prayer/-protectOff, enterValleyForAmuletMake-prayer/-protectOff, enterTemple-drink, talkToMonkeyChild-protectOff, killNinja-restore(drink_to 45)/-prayer/-margin, killGorilla-drink/-prayer/-margin, killZombie-drink/-prayer/-margin/-protectOff, prepareForBattle-drink, killDemon-prayer/-margin.
- next: run mm under today's rules (pr_mm.run1.log)

## mm run1 (today rules): 351/352, FAIL enterValleyForAmuletMake-protect (no tick after tab; branch never ran before). fixed with t.ticks(1); same tick added in legends useTotemOnTotem block. demon margin 39/3 lobsters left, prayer after 9.

## proof tree (git only so far): build/orchestrator/worktrees/prayer-proof at 457041eec + merge origin/matthew-mbp-m4-b56 -> local df5a06f01 (conflict _conformance.lua took waves side, conformance not used by quest runs). content worktree (git -C OSRS-Content worktree add) at c93c574f20 + merge b56 -> local 5dc6b7db7b (varp.alloc union, no number collisions). nothing pushed.

## mm run2 (today rules): PASS 352/352, published to OSRS-Content quest_mm/play; gate green; lint clean; coverage FULL 78 (ALT 1, DRIVEN 77).
- next: build proof tree (private objdir build_prayerproof)

## proof mm run1 (waves rules, df5a06f01/5dc6b7db7b, src/torirs_prayerproof): 351/352, FAIL killDemon-margin: 316 ticks, 17 lobsters eaten -> 0, lowest 33, prayer ran dry ~260t. gorilla 43->7.
- fix: mm setup wears rune helm/platebody/platelegs/kiteshield (QH combatGear MonkeyMadnessI.java:327,:887); gorilla drink want 50.

## proof mm run2: setup FAIL rune_platebody needs Dragon Slayer -> rune_chainbody. run3: armour worked but 18 lobsters barely eaten -> backpack full: bananas 4/5, zombie bones not taken, 83 FAILs. fix: setup lobster 18 -> 10.

## proof mm run4 (waves rules): PASS 352/352. ravine 52->42, 42->32; temple drink 32->52; ninja 52->43; gorilla drink 42->51, 48->12 (176t); zombie drink 7->45, 45->33; demon 51->9 (204t), lobsters 18->13, lowest 33. next: mm run3 in main (today rules) with final file.

## mm COMMITTED: content 5bc8ef66ca, parent 5e6acb314, both pushed. today run3: PASS 352/352 gate green lint clean cov FULL 78.
- next: legends on proof tree

## proof legends run1 (waves rules): PASS 560/560. leg10: pack sharks 15, doses 16, prayer 0; drink 0->60 (3); San 58->35 (98t); Irvig drink 35->56, ->36 (111t); Ranalph 35->56 ->29 (133t); demon arrival -> 0, drink 0->59 (3), 59->12 (232t); 0 sharks eaten in leg 10, lowest 53-57; off at 10, doses left 8. leg4 margin 46/5, leg8 37/3.
- next: legends main (today rules)

## legends COMMITTED: content 72cf9fc0dd, parent 73659f408, pushed. today: PASS 560/560 gate green lint clean cov FULL 99.
## proof tree removed (symlink unlinked, content worktree then parent removed, pruned). evidence: build/orchestrator/fix_b56/proof_evidence/. DONE.
