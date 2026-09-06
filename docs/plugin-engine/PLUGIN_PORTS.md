# Plugin port and screenshot register

This is an implementation checklist, not a completion claim. Every row still has the full plan’s lifecycle, interaction, composition and release gates. Changing the ABI or a graphics type alone does not close a product row. Screenshots live under `/private/tmp/plugin-engine-evidence`; fixture receipts, logs, saved state and source copies accompany captures.

| Consumer | Current state | Captures |
|---|---|---|
| `script/plugins/_beamprobe.lua` | common logic tick and live settings port; both-revision column captures inspected; full release gate pending | `beamprobe-port-osrs/m01`, `beamprobe-port-lc/r01` |
| `script/plugins/_drawprobe.lua` | live-world coordinates and rectangle opacity fixed; both-revision captures inspected | `drawprobe-alpha-osrs/m01`, `drawprobe-alpha-lc/r01` |
| `script/plugins/_gicount.lua` | live scene origin; labels no longer restricted to log frames; OSRS capture produced, LC pending | `gicount-port-osrs/m01` |
| `script/plugins/_giprobe.lua` | live scene origin; both revisions survive disable/re-enable with two labels and one tile marker | `giprobe-origin-osrs/m01`, `giprobe-text-lc/r01` |
| `script/plugins/_hoverprobe.lua` | major-3 graphics; OSRS hover marker inspected; LC pending | `hoverprobe-port-osrs/m01` |
| `script/plugins/_hullprobe.lua` | graphics probe; both-revision two-shape captures inspected; full release gate pending | `hullprobe-port-osrs/m01`, `hullprobe-port-lc/r01` |
| `script/plugins/_paneldemo.lua` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `script/plugins/_probe.lua` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `script/plugins/_roleprobe.lua` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `script/plugins/_windemo.lua` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `script/plugins/entity_highlighter.lua` | menu port fixes retained species/action; OSRS highlight checked; LC spatial/occlusion and native menu acceptance pending | `entity-visible-osrs/m01`, `entity-visible-lc/r01` |
| `script/plugins/ground_items.lua` | live caption hiding and native remount recovery implemented; native edit-mode button overlaps long Lua text (layout acceptance open) | `ground-text-osrs/m01`, `ground-partyhat-lc/r01`, `ground-caption-controls/m01`, `ground-caption-live-native/m01`, `ground-caption-remount/m01`, `ground-hide-rs289-stats/r01` |
| `script/plugins/loot_beam.lua` | major-3 scene API; real dropped-item beams inspected on both revisions; full lifecycle/composition acceptance pending | `lootbeam-low-osrs/m01`, `lootbeam-drop-lc/r01` |
| `script/plugins/performance_display.lua` | owned-widget port; settings, remount and disable/re-enable checked; full release gate pending | `performance-port-osrs/m01`, `performance-port-lc/r01`, `performance-settings-osrs/m01`, `performance-disabled-lc/r01`, `performance-reenabled-remount-osrs/m01` |
| `script/plugins/screenshot.lua` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `script/plugins/tile_indicator.lua` | graphics port; stationary marker checked; native walking/overlap/lifecycle acceptance pending | `tile-port-lua-osrs/m01`, `tile-port-lua-lc/r01` |
| `src/plugin/plugins/client_settings.c` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `src/plugin/plugins/feature_flags.c` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `src/plugin/plugins/gameframe.c` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `src/plugin/plugins/item_stats.c` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `src/plugin/plugins/loot_tracker.c` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `src/plugin/plugins/minimap_orbs.c` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `src/plugin/plugins/mobile_gameframe.c` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `src/plugin/plugins/nxt_bird_nest.c` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `src/plugin/plugins/nxt_cannon_ammo.c` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `src/plugin/plugins/nxt_highlight.c` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `src/plugin/plugins/tileind.c` | graphics port; stationary marker checked; native walking/overlap/lifecycle acceptance pending | `tile-port-c-osrs/m01`, `tile-port-c-lc/r01` |
| `src/plugin/plugins/xp_orbs.c` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `src/plugin/plugins/xp_tracker.c` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `src/plugin/torirs_plugin_lua.c` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `script/plugins/_widgetprobe.lua` | owned text and geometry probe; native actions/style extensions remain pending | `owned-widget-lua-osrs/m01`, `owned-widget-lua-lc/r01` |
| `src/plugin/plugins/widget_demo.c` | owned text and geometry probe; native actions/style extensions remain pending | `owned-widget-c-osrs/m01`, `owned-widget-c-lc/r01` |

## Inspected product evidence

- Performance Display: four white metric lines on both revisions; OSRS settings capture has exactly two magenta lines at viewport-local (160,103) and (160,118). Disabled rs289lc has zero owned metric nodes and zero visible labels. OSRS root 164 after remount and re-enable has four labels. Native chat controls: eight OSRS, four rs289lc. Fixed native sidebars: fourteen OSRS buttons, thirteen rs289lc buttons. Root 164 has thirteen sidebar buttons plus top logout X; its small-canvas native orb overlap is still an open native baseline limitation.
- Tile Indicator: one cyan true-tile marker in each of the four C/Lua × revision captures. Enlarged world crops match across languages. Full C screenshots inspected at 2× retain eight OSRS/four rs289lc chat controls and fourteen/thirteen sidebar buttons. Lua hover ordering now matches C. Destination/walking and overlap need native interaction captures.
- Entity Highlighter: OSRS capture shows one magenta hull around Romeo. Initial captures selected absent NPC species and had no highlights; `pixels-product.txt` now rejects both initial captures. The rs289lc broad-tag capture shows four hulls through palace walls, The central figure is the local player, confirmed by the separate hull probe; it correctly remains unmarked. Native menu interaction and target visibility cases still need acceptance.
- Hull probe: Both revisions show one cyan bounds hull and one tighter magenta mesh hull around the player. OSRS retains eight chat controls/fourteen sidebar buttons; rs289lc retains four/thirteen.

## Executed regressions

`make -C src test-plugin-lua test-plugin-host test-plugin-api test-tileind-v2` passes. The Lua suite executes the actual shipped product sources with deterministic time, draw counts, settings, remounts and retained NPC actions. `make -C src OPT=0 ENABLE_ASAN=1 test-plugin-lua test-uitree test-plugin-host` passed. The final menu binding test also passes with ASan, verifying menu.add forwards its dispatch/action and rejects use after dispatch.

`tools/plugin_engine_negative_controls.py --suite lua NEW_DIRECTORY` runs the existing Lua test binary against private copied script fixtures. Six observed failures in `product-lua-negative`: rendered frame counter, work averaging window, metric visibility, hover order, recycled NPC slot and retained Tag intent. The earlier retained-menu source reproduced its named failing assertion in `/private/tmp/plugin-engine-entity-retained-menu-red.log`. Compiler/syntax failures are not counted.

The runtime graphics context is now `ToriRS_Graphics` / `torirs.Graphics`. Menu additions moved from `api.ui.menu_add` to `api.menu.add`; no compatibility alias remains. This does not complete removal of the old frame/panel/UI contribution builders. Existing user configuration keys and tracker data have not been renamed or swept.

## Live world and ground-item checkpoint

`api.world.scene_origin()` (C and Lua) reads the current native world's absolute southwest tile. Ground Items and the two ground-item probes no longer infer it from a stationary player or depend on having received an earlier world-loaded event. The old Ground Items source fails mid-session startup while moving; breaking the production getter fails the host assertion. The gi-probe captures retain two orange labels and one tile marker after disable/re-enable on both real revisions.

Ground Items preserves the requested Highlight/Hide action and item definition through a retained menu, including despawn. Exact-name exception lists permit removing one item from a wildcard rule without deleting other saved preferences. Existing keys/data remain; `highlight_exceptions` and `hide_exceptions` default empty. These are ordinary configuration data, not execution ABI compatibility.

**Open native acceptance failure:** OSRS239's existing CS2 overlay also labels the dropped rune platebody, producing overlapping native and Lua text (`ground-text-osrs/m01`). The native driver is `app_ground_items_tick`, with per-coordinate overlays built by `torirs_ground_items_overlay` using `overlay_coord_create(coord,0,...)`; projection visibility is applied by the existing overlay positioning pass. Resolve this through the live widget/native event integration. Do not globally switch off saved native settings, delete operational children, or declare the product complete from its one-label trace assertion. rs289lc's debug-created Silver bar has one full Lua label (an unrelated arrow caption is partly clipped at the viewport edge).

Native fixture inputs read from the real handlers: OSRS inventory rune platebody right-click `(618,235)`, then Drop `(610,280)`. Both Ground Items and Loot Beams receive the resulting real item update. On the pinned Lost City server, debugprocs require `~`; `500,~addobj2 1` creates one random item at the player's tile. Earlier plain `addobj`/`varrock` command requests are not applied-state evidence. Each connection reserved a new account. Public drops can be seen by other simultaneous fixture accounts, so do not treat unrelated nearby stacks as deterministic counts.

Loot Beams' OSRS rune platebody is below its default high threshold: `tier=low` shows one blue beam. The rs289lc capture uses `tier=low,low_value=0` and a real debug-created stack, also with one blue beam. The beam probe separately shows its deliberately wide translucent column on both revisions. It now uses the client logic tick; height changes rebuild its geometry, color updates recolor it, and stop frees the probe's mesh/instance.

The draw probe now places its cyan filled tile and yellow outlined tile in the actual loaded world. Its rectangle's requested alpha 128 was lost in `ToriRS_FrameNextCommand`; forwarding the transparency restores blending on both revisions. The renderer test fails without that field, and the pixel rule rejects the earlier fully opaque capture. The native frame sprite test needed its unused model-draw link boundary closed after the previous animation changes; that stub aborts if unexpectedly invoked. General graphics line-alpha behavior and the zero-alpha rectangle/outline convention remain to reconcile during complete execution-API cutover.

Observed negative controls: `ground-product-negative` covers origin, retained action intent and wildcard exceptions; `overlay-probe-negative` covers logic-tick creation and uninterrupted probe labels. Logs `/private/tmp/plugin-engine-scene-origin-host-red.log` and `/private/tmp/plugin-engine-overlay-opacity-red.log` contain the intended host/renderer failures. Relevant host, Lua and frame translation tests pass; ASan host/Lua run is `/private/tmp/plugin-engine-ground-asan.log`.

The existing gameframe harness accepts `GF_MATRIX_OVERLAY_TEXT` (pipe-separated expected captions). A capture-fence text fingerprint and matching foreground pixels are both required; this verifies that the specified overlay caption rendered, not that no native caption overlaps it. Captures and enlarged inspections retain 8 chat/14 sidebar controls on OSRS fixed and 4/13 on rs289lc.

## Native caption integration checkpoint

The original default-mode duplicate label is resolved by `find_all("ground_item_labels")`, `watch_tree` and `set_hidden`. Caption-only suppression preserves native controls/timer widgets and leaves native script state live. `ground-caption-live-native` changes native varbit 14885 while the caption is suppressed, disables the plugin, and shows the current native caption without its former price suffix. `ground-caption-remount` rebuilds into root 164 and retains one Lua caption; native caption nodes remain present and suppressed.

The first remount run failed because the native UI rebuild cleared overlay records and only re-fired scenery/NPC producers. `app_client_triggers_refire` now schedules the existing ground-item refresh too. The pre-fix binary/source and failing capture are retained in `ground-remount-native-negative`; this is a native fix, not a plugin repair loop.

**Remaining visual failure:** `ground-caption-controls` enables native edit controls and timers (varbits 14871/14873). One native button per current overlay is retained and input-eligible in this fixture; the longer Lua text overlaps the visible button. The native script source describes additional auxiliary widgets, so actual visible control counts must not be inferred from its source. Resolve the layout and verify native button actions before closing Ground Items. Earlier whole-overlay hiding captures are superseded as final behavior: they also suppressed native controls.

The same Ground Items source passes the real rs289lc stats/CS1 scenario: 19 skill cells, Strength 20/20, four chat controls, thirteen sidebar controls. The native-overlay role is unavailable there and does not suppress the older native UI.

Observed negative controls: `widget-visibility-negative` breaks effective hiding; `tree-watch-negative/tree_watch_epoch.log` breaks callback identity; `tree-watch-publication-negative` suppresses tree notifications. The first attempted publication mutant dispatched the wrong event kind and was rejected by the negative runner; only the corrected named assertion is counted. `ground-caption-negative` runs a copied Lua source with the hide setter removed and fails the native caption assertion. Native snapshots now distinguish retained overlay roots, caption hiding/emission and live native operation widgets.

`test_live_widget_visibility` covers input/paint, both IF1/IF3 native-hide authority, latest-writer reset, native content updates, foreign ownership, copying and stale trees. The host subscription test covers initial delivery, stable epochs, nested restart, callback context and disable. Normal and ASan UI/host/Lua suites pass; full combined interaction and release gates remain open.
