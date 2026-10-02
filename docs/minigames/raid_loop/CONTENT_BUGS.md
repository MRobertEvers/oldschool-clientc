# Content bugs the raid loop found and has not fixed

Found is not fixed: a defect with no row fails nothing. One line per finding: raid, room,
the constant or script line, what the source says, what we measured, who found it. A row
is removed only by the commit that fixes it, named here in its place.

## From seam1 (2026-10-02)

- ToA, Zebak (and Kephri, Akkha, Ba-Ba, the Wardens): the barrier `~toa_add_barrier`
  spawns for a room whose square has none is placed at entry + 2 x
  `~coord_direction2(entry, fight)`, and that direction can be diagonal (Zebak: NW, gate at
  6452,161 off the entry row); `~toa_pass_barrier` then steps two tiles from the PLAYER in
  the gate-minus-player direction, so the click starts the room (`::toastate` started=1,
  boss uid set) but the player ends on 6452,162 or 6454,161 and `walk_to` 6437,160 stalls
  at the barrier: the arena cannot be reached (runs rre_toacox1, rre_toacox3, rre_toacox4).
  Het's map-placed barrier crosses correctly (rre_het1: 6442,159, started=1). Source to
  settle it: Near Reality's `TOARaidArea.walkBarrier`. Files: `minigame_toa/scripts/toa_barrier.rs2`,
  `~toa_add_barrier`. Found by the raid_room_entry_verbs fixer.
- ToA, Wardens P1: `::toa 10` lands STARTED. The entry tile (31,56) is 10 tiles from the
  fight tile (32,46), inside `^toa_room_arena_radius` 12, so the proximity watchdog
  (`toa_raid.rs2` ~line 1129) starts the room on arrival and the `toa_wardens_barrier`
  click is moot. No source yet places the real entry tile; settle from NR, then move the
  tile or shrink the radius for that room. Found by the raid_room_entry_verbs fixer.
- CoX, every room: `::coxgoto` lands at the room CENTRE (the `::coxgoto_tekton`
  convention), so Tekton wakes on landing (`cox_tekton.rs2` wake-on-range) and hit the
  player 99 -> 65 before any click (rre_toacox3 shot 017). A room test that walks in from
  the door needs a door-tile landing derived from the serpentine path (`cox_layout.rs2`);
  no source places that tile yet. Found by the raid_room_entry_verbs fixer.
- ToB, Hard Mode door: `::tobmode 2` skips the door's `varp6826_tob_completions >= 1`
  requirement (`tob_party.rs2` `~tob_choose_mode`). Deliberate for a room-resume
  affordance; the whole-raid `tob_entry` test must use the real door
  (`tob_surface_raid_entrance` op 1 and the mode menu). Not a bug; a rule for the authors.
- Engine, ground items (client): OBJ_COUNT has no old count on the client path
  (`rs_gameproto_exec.c:538` drops `pkt->old_count`; `App_WorldObjStackSetCount` does not
  take it; the lookup matches id only where Client-TS matches id AND count == old_count),
  so a tile holding two piles of one stackable can retarget the wrong pile. Server side,
  `world_obj_add`'s merge loop merges a public add into ANY active pile of that id on the
  tile, a private one included (it never checks `receiver_pid`). OBJ_DEL carries pos+id
  only (`revpacket.h` PktObjDel, `torirs_server_encode.c:4317`) while rev 239's real
  OBJ_DEL carries the quantity, and `ToriRSServer_WorldGroundFind` takes the lowest slot,
  not the oldest. Invisible for non-stackable twins; wrong for two private piles of one
  stackable with different counts. Found by the client_ground_obj_merge fixer.
- Engine, retaliation: an ordinary npc hit by magic may never retaliate (a Lumbridge man:
  one swing then none, or none at all in the full harness; a cow: none). `DRIVER_NOTES.md`
  "An ordinary npc hit by magic may never retaliate". Found by the npc-state fixer.
- Quest loop impact (for the PR, not a raid row): `ToriRSServer_CombatAddXp` now follows
  LostCity's `Player.addXp` (a drained stat stays drained through xp), as RAID_ORCHESTRATOR.md
  section 4 asks. The quest test `deserttreasure` moved green -> RED because the Ice Path
  chill (`chill.rs2`, ported from LostCity) now really drains: trolls 376 -> 466 ticks and
  magic below Fire Blast's 59 at Kamil. An A/B binary without the hunk ran 249/249. That
  test belongs to the quest loop; it needs a restore or `::setlevel magic` before Kamil
  when this branch reaches v3.
