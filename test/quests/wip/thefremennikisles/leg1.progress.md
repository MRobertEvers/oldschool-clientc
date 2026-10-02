# leg 1 notebook (thefremennikisles) -- runner 1, 0 runs used
Stopped before writing any Lua: the ladder's leg 1 cannot be driven first.
- ladder order is the guide's steps.put order, not route order. Leg 1 = stages 90..290
  (talkToMawnis .. makeShield). Stage 90 (^fris_help_mawnis) is written only by the jester
  act, fris_jester.rs2:294, after spy 1 (stage 55/60, fris_jester.rs2:625), i.e. after ladder
  leg 2 (talkToMord .. talkToSlug, s0-50) and leg 3's first rows (goSpyOnMawnis, tellSlugReport1).
- getYakArmour (s280) and makeShield (s290) come after the decree (270/275, ladder legs 5-6),
  after the window/beard tax rounds and spy 2 (legs 3-5). Not reachable from stage 90..150.
- Only start-staging debugproc exists (::fremennikisles -> stage 0) plus ::fremennikisleskingdoor
  (stage 310). ::setvar of the quest var is forbidden, so no honest way to enter at 90 or 280.
- No test/quests/thefremennikisles.lua written (nothing honest to write).
Fix for the orchestrator: re-cut legs in ROUTE order (stage 0 -> 60 -> 90..150 -> 160..275 -> 280/290 -> 300+),
or let one author drive 0..150 as leg 1; rows 8-9 (s280/290) belong after the decree rows.

- runner 2 (fresh after give-up): re-checked ladder --leg 1 (still s90..s290, steps 1-9) and queue row; unchanged, no re-cut landed. 0 runs used. Gave up again for the same reason.
