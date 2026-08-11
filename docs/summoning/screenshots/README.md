# Summoning render verification

These captures were generated from `cache.osrs239.summoning` with the compiled
`build_summoning` server scripts and the software 3D renderer.

- `terrorbird_world_and_familiar_tab.png` shows one Spirit terrorbird rendered
  in the 3D world and a second animated full-body render inside the Equipment
  tab's familiar interface.
- `spirit_wolf_familiar_tab.png` shows a full Spirit wolf in both the world and
  the familiar tab.
- `dreadfowl_world_and_familiar_tab.png` shows the same two rendering paths for
  a Dreadfowl.
- `equipment_tab_restored.png` shows that Back closes the familiar interface,
  restores the normal Equipment contents, and leaves the familiar entry button
  in the top-right corner.

The matching `.log` files contain the runtime evidence. In particular,
`raster_tex_mode` records the preserved complex texture mapping types routed to
the affine face kernel, and `anim_tick` records changing familiar animation
frames. No `cs2 abort` occurs in any capture.
