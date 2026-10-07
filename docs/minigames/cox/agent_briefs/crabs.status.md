# Crabs room status

- Branch: `cursor/cox-crabs-room-da39` (parent + OSRS-Content)
- Spec coverage: FULL (stun/regen/splash/aggro/needed)
- Content: beam rim-only map_blocked + npc_tele; crystal adjacent score;
  `^cox_crab_paint_ticks=28` (CCW crystal-2 transit)
- Harness: Smash via click_minimenu; west lure/safe; cast-paint; reseat skip
  when already coloured; t.step seat rows (no shot); paint refresh in wait;
  per-iter yield in seat loop
- Gate run27: RED script-error budget after green seat stage=2 (paint expired)
- Next: prove crystals 0-3 + despawn green + FULL
