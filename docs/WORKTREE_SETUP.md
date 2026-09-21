# Worktree setup (lane-quest-driver)

- OSRS-Content: real submodule checkout at the recorded commit (cloned with
  --reference to the main checkout's objects). The compiled script pack lives at
  OSRS-Content/osrs239-content/server/scripts/build/script.dat; rebuild with
  `make -C src torirsserver-scripts`.
- cache.osrs239: a full COPY of the pristine rev-239 cache (218 MB), not a link,
  so nothing here can touch the main tree's copy.
- build/manifests/osrs239.ini: the launcher-generated embed manifest
  (dir=../../cache.osrs239, transport=embed). Use it for headless runs:
  `./src/<binary> --manifest build/manifests/osrs239.ini --user <name> --pass test`.
  manifests/manifest_osrs239.ini points at a sparse JS5 cache that does not
  exist here; the content test rewrites it (tools/content_selftest.py:62-70),
  the runner must do the same or use the build/manifests one.
- Never point TORIRSSERVER_SAVES, TORIRS_PREFS or TORIRS_PLUGIN_PREFS at the
  repo's own files; use a scratch dir per run.
- Server pack band: `make -C src torirsserver-servpack` writes 10,428 records into
  OSRS-Content/osrs239-content/server/pack and then fails its membership gate
  ("6 record(s) state a server field pack/<ns>.server does not claim"). Same
  submodule commit as the main tree, so this is pre-existing content state, not
  this worktree; the band files it needs exist. Not required by the quest driver.
- A fresh account boots into the Character Creator (a modal nothing headless
  can click). A quest fixture save must set, under [varps] (scope=perm only):
  the `tutorial` varp (id 281) to its finished value (1000, the value the
  tutorial selftest leaves behind) and `newplayer_design_done`
  (server/scripts/tutorial/configs/tutorial.varp) to 1. Resolve both ids
  through the content symbol table when writing the fixture generator; never
  hand-type them into a test.
- Verified boot 2026-09-19: ./src/torirs_qd --manifest build/manifests/osrs239.ini
  --user qdcheck --pass test --soft3d with SDL_VIDEODRIVER=dummy,
  TORIRS_EMBED_CLOCK_MS=20, TORIRS_MAX_FRAMES=400, private saves/prefs:
  embedded server loads the copied cache, login ok, Lumbridge renders.
