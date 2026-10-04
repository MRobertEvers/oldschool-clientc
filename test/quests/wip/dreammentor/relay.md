# dreammentor relay -- seam2 bank proof (matthew-mbp-m4-b56-seam2, 2026-10-03)

The committed test (`test/quests/dreammentor.lua`, ecd69a678) stops at `t.blocked` because the dream
fights need more food than the 28-slot backpack holds beside the twenty pieces fed to Cyrisus and
the tools, and the driver had no bank verb. Seam `bank_withdraw_and_deposit_verbs` added
`t.bank.open/withdraw/deposit/count/close` (script/plugins/quest_driver/ui.lua) and the SETUP-only
cheat `::bankgive <obj> <n>` (OSRS-Content general/scripts/misc/cheat_bank.rs2; the driver refuses it
after `t.quest.bind`).

`seam2_bank_proven.lua` is the committed file with three changes:

1. setup `::bankgive shark 24` (the guide's fight food waits in the bank: Quest Helper
   DreamMentor.java's BankSlotIcons food requirements);
2. leg 3, after the potion is made and before `lightBrazier` ("Equip your combat equipment, food,
   and light the Brazier"): walk out of the sink house, along the street and in through the Lunar
   Isle bank's south doorway (x 2099, no door loc), `t.bank.open("lunar_moonclan_bankbooth", 2,
   { at = { 2099, 3920 } })`, deposit the spent hammer and pestle and mortar, withdraw 22 sharks,
   close -- 24 sharks carried into the dream;
3. the blocked tail is now the real Inadequacy kill, then a `t.blocked` naming the NEXT stop.

Proved: `run.py --script test/quests/wip/dreammentor/seam2_bank_proven.lua --name s2dm_bank3
--no-build --no-publish` -> SUMMARY pass=230 fail=0 blocked=1 (build/quest_gate/s2dm_bank3):
row 213 `bank.withdraw: shark backpack 2 -> 24 (+22), bank 24 -> 2 (-22)`, row 220 `sharks carried
into the dream: 24`, row 229 `killInadaquacy-dead PASS ... dead after 976 tick(s)`.

## Where it stops next (not a bank seam)

- The Inadequacy took 976 ticks, 141 re-engagements and 22 of 24 sharks with the twisted bow the
  ecd69a678 author chose; a 600-tick wait (e13d962a9's) times out at 49/80 (s2dm_bank1 row 229).
  e13d962a9 killed all four with a magic shortbow + rune arrows and 24 of 25 sharks; try that gear
  (and its rune_chainbody) before anything else -- the twisted bow may be the slow part.
- After the kill `npc.await_present dream_everlasting` (40 tiles, 40 ticks) found nothing while the
  player's hitpoints fell 80 -> 31 (s2dm_bank2 rows 231-242): read dreammentor_dream.rs2's boss
  sequence (ai_queue1 at :460) for how the next form appears before writing the next row.
- The bank holds 2 sharks, the hammer and the pestle afterwards; the next author can withdraw more
  only by freeing slots (seal of passage and tinderbox are still carried).
