# Plugin port and screenshot register

This is an implementation checklist, not a completion claim. Every row still has the full plan’s lifecycle, interaction, composition and release gates. Changing the ABI or a graphics type alone does not close a product row. Screenshots live under `/private/tmp/plugin-engine-evidence`; fixture receipts, logs, saved state and source copies accompany captures.

| Consumer | Current state | Captures |
|---|---|---|
| `script/plugins/_beamprobe.lua` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `script/plugins/_drawprobe.lua` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `script/plugins/_gicount.lua` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `script/plugins/_giprobe.lua` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `script/plugins/_hoverprobe.lua` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `script/plugins/_hullprobe.lua` | graphics probe; OSRS two-shape capture inspected; both-revision two-shape captures inspected | `hullprobe-port-osrs/m01`, `hullprobe-port-lc/r01` |
| `script/plugins/_paneldemo.lua` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `script/plugins/_probe.lua` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `script/plugins/_roleprobe.lua` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `script/plugins/_windemo.lua` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `script/plugins/entity_highlighter.lua` | menu port fixes retained species/action; OSRS highlight checked; LC spatial/occlusion and native menu acceptance pending | `entity-visible-osrs/m01`, `entity-visible-lc/r01` |
| `script/plugins/ground_items.lua` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `script/plugins/loot_beam.lua` | pending product acceptance; major-3 registration alone is insufficient | Pending |
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
