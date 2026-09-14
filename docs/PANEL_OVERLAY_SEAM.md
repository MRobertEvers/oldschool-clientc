# The panel overlay seam

The subsystem plan that took `struct App` from 443 fields to 332 stopped at one
family and said why:

> The two GAME lists have twenty-eight reaches, all inside the translation unit,
> and moved with their routing rule. The bridge's forty-nine reaches are into
> the PANEL arrays, which is what the seam note is for — and those stay until it
> is written.

This is that note. It answers the three questions the plan asked — which of the
stage's primitives the panel needs, in which surface, and whether the panel
staging window is the stage's or the panel's — so that the cut can be made by
whoever owns that surface without re-deriving any of it.

It describes the code as it stands at `da11f4474`. It proposes no code.

## The family

Eighteen fields, twenty-eight declaration lines, `src/app.h` — `panel_overlays`,
`panel_overlay_owner`, `panel_overlay_row`, `panel_overlay_count`,
`panel_overlay_stage`, `panel_overlay_stage_count`, `panel_overlay_stage_active`,
`panel_overlay_stage_overflow`, `panel_overlay_origin_x`, `panel_overlay_origin_y`,
`panel_overlay_scale`, `panel_overlay_clip`, `panel_overlay_generation`,
`panel_overlay_revision`, `panel_custom_last_draw_cycle`,
`panel_custom_has_draw_cycle`, `panel_custom_pixels`,
`panel_custom_pixel_capacity`.

**89 reaches, 11 functions, 4 files.**

| File | Reaches | Functions | What it is |
|---|---:|---:|---|
| `src/plugin/torirs_plugin_panel.u.c` | 71 | 7 | the owner |
| `src/app/app_overlay.c` | 12 | 2 | the router's panel branch |
| `src/app/app_chrome.c` | 3 | 1 | the buffer presenter's fallback |
| `src/app.c` | 3 | 1 | `App_Shutdown` frees the raster scratch |

Two corrections to the sentence quoted above, both in the direction of more
work rather than less. The reaches are not **the bridge's**:
`torirs_plugin_bridge.u.c` has none, and never had any — it sets
`plugin_draw_canvas` and nothing else in this family. They are the panel unity
file's. And **forty-nine** does not reproduce under any counting; the figure
below counts every mention of a family field inside a function body, per file.
Neither correction changes the plan's conclusion, which was that this family is
not app.c's to cut.

The owner's seven: `_overlay_commit` (20), `_draw_custom` (16),
`_raster_custom` (15), `_overlay_reset` (9), `_overlay_visible` (5),
`_overlay_bump` (3), `_overlay_move` (3). Six are `static`. **One is exported**
— `app_plugin_panel_overlay_visible` — and `app_chrome.c` is its only caller.

## Q1 — which primitives

Four of the seven `UITREE_ENTITY_OVERLAY_*` kinds: **RECT, SPRITE, TEXT, LINE**.

The polygon run — `POLY_BEGIN` / `POLY_POINT` / `POLY_END` — **cannot arrive**.
The only verbs that produce one are `draw_tile` and `draw_hull`, and
`plugin_draw_require_world` asserts the WORLD surface for both
(`torirs_plugin_host.c`): a tile and a hull are named in scene terms, and a
panel well has no scene behind it, so it is a contract violation rather than a
runtime state.

`ToriRSChromePanelDraw_ToChromePrim` therefore `return 0`s on the polygon kinds
as a default, not as a live divergence. Do not read that `default:` as a
capability gap to close — closing it would mean deciding what a scene-space
hull means in panel coordinates, which is the question the assert already
answered.

## Q2 — which surface

`OVERLAY_SURFACE_PANEL` exists in `render/overlay_stage.h` **so the stage can
refuse it**. `OverlayStage_Push` never holds one; `OverlayStage_Items` returns
NULL for it. The routing happens one level above, in `app_overlay_push`, before
the stage is reached.

Past that point there are two consumers, and their reach differs:

- **BUFFER** (in-canvas developer/plugin chrome) — one retained item at a time
  through `app_plugin_panel_overlay_visible` → `ToriRSChromePanelDraw_ToChromePrim`
  → `ToriRSChromePrim`, merged by `app_chrome_merged_prims`. The four kinds
  above.
- **WEB/BROWSER** — the whole of one owner's run, re-based to region-local
  coordinates and rendered by `ToriRS_Soft3D_RenderFrame` through a
  `UITREE_EMIT_ENTITY_OVERLAY` desc into `panel_custom_pixels`, handed to
  `custom_present` as a `ToriRSChromeCustomFrame`. The full emit path.

Both read the same retained array. Neither reads the stage array for its own
sake — see the first hazard below.

## Q3 — whose window

**The panel's.** Five properties separate it from the stage's, and none is a
matter of taste:

| | `OverlayStage` | the panel's staging |
|---|---|---|
| lifetime | one frame (`_Reset` each frame) | one selection generation, across frames |
| identity | by surface | by widget serial, plus a row index for O(1) clip lookup |
| on push | keep or drop, verbatim | transform by origin/scale/clip first, then keep |
| space | screen | panel-local, scaled |
| commit | none | atomic replace of one owner's run, declining on empty |
| re-place | never | `_overlay_move` re-bases a run whose origin moved and size did not |

A retained, generation-keyed, serial-owned, scroll-re-placeable store with
transactional commit is not the frame-scoped list the stage holds. It shares
only the element type.

The panel branch sits inside `app_overlay_push` today for one reason: that
function is the single point every draw verb lands on. The **routing** decision
belongs there and should stay — "is a panel window open?" is exactly what the
router is for. The **staging** behind it is the panel's and can move behind one
call the panel owns, leaving the router with the question and none of the state.

That is the shape of the cut: `app_overlay_push`'s ten reaches and
`app_overlay_count`'s two become one call each. The chrome side needs nothing —
it is already one function wide.

## Three hazards, in the order they will bite

**1. `panel_overlay_stage` has two jobs.** It is the transient list a plugin's
`on_ui_draw` fills through the router, and it is also the scratch
local-coordinate buffer `_raster_custom` borrows to re-base a retained run
before rendering. The second use is safe only because no plugin callback is open
at that moment — the code says so in a comment and nothing enforces it. A module
boundary must either keep the two apart or carry that argument across with them.

**2. The address that looks shared is not.** `_draw_custom` passes
`&app->panel_overlay_stage` to `PluginHost_PanelDraw` as `void* surface`. The
host **never dereferences it**: `plugin_draw_allow` does
`assert(surface == ctx->host->draw_surface); (void)surface;`. It is a nonce, and
the drawing comes back through the engine's draw verbs and the bridge, not
through that pointer. **Moving this array out of `struct App` does not touch the
plugin host ABI.** Anyone sizing this cut will assume otherwise from the
signature; they would be wrong, and it is most of the perceived cost.

**3. `panel_custom_pixels` is the family's only heap**, grown by `realloc` in
`_raster_custom` and freed in `App_Shutdown`. It is also the only member of the
family the composition root still knows about. Whatever takes the family takes
that lifetime, and `App_Shutdown` should lose all three of its reaches in the
same commit — otherwise the composition root keeps a pointer into a subsystem
that has left, which is exactly the shape the reach-in gate exists to catch.

## Not scheduled

This note does not schedule the cut. `PluginPanelHost` — around fifty fields,
the largest single block left in `struct App` — belongs to the plugin-engine
owner and the porcelain-layer plan that is rewriting the surface, and a struct
rename underneath an in-flight rewrite is a merge conflict with nothing to show
for it. The plan said so, the overlay commit said so, and this note does not
change it. What it changes is that the cut is now costed.
