# Old CoX test scripts -- REFERENCE ONLY, BROKEN

Owner, 2026-10-13: these do not work. They were moved out of `test/raids/` so
the Scripts tab (`./launch run osrs239-scripts`), the raid gate and every other
runner stop offering them. Nothing runs them; nothing should.

Kept for what they record -- each room's setup cheats, kit and the checks an
earlier pass wanted -- not for what they do. A working CoX test lives in
`test/raids/` and passes there:

| working | what it proves |
|---|---|
| `cox_olm_solo` | the Great Olm solo, v2 solver, to the looted chest |
| `cox_olm_chest` | the reward chest and its interface |
| `cox_olm_mechanics` | every Olm attack, photographed |
| `cox_olm_douse` / `cox_olm_douse_op` | a fire-wall flame doused by spell / by the Douse op |
| `cox_helper_layout` | the native CoX helper's room list against the server's layout |

The ones here: the per-room tests (`cox_crabs` ... `cox_vespula`), the old
4:1 (`cox_olm_solo_4t41`) and the chamber probes (`cox_olm_chamber_shot`,
`cox_olm_map_compare`, `cox_olm_rise_compare`, `cox_olm_visual`). To revive
one, rewrite it against the current content and move it back into
`test/raids/` only when it passes.
