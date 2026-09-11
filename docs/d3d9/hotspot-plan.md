# Why `UITree_LayoutResolve` runs every frame, and a plan for the next four

Companion to `profiles.md`. The profile named the hotspots; this names their
causes and what to do about them. Everything in §1 is measured **on the
Windows 11 machine** against the same live osrs239 world the XP profile used
(`manifests/manifest_osrs239_localnet.ini` -> `torirsserver` on
`127.0.0.1:43596`), so it is reproducible without the XP box.

---

## 1. `UITree_LayoutResolve`: measured, not guessed

### 1.1 The skip gate is already there, and it is working

`UITree_LayoutResolve` opens with an O(1) early-out: same topology, same root
box, nothing invalidated since the last resolve. Its stated purpose is exactly
this case — *"idle frames run CS2 (the gameframe clock varc ticks every frame)
without touching a single layout input."*

It is not broken. It fires. It just almost never gets the chance.

Two runs, `TORIRS_PERF=1`, in-world at Lumbridge, 1024x768:

| | committed `plugin_prefs.ini` (9 plugins) | defaults (14 plugins) |
|---|---|---|
| frames | 1500 | 900 |
| `uitree_layout_resolve` (calls) | 1153 | 1279 |
| `uitree_layout_skip` (early-outs) | 261 (22.6%) | 408 (31.9%) |
| **walks** | **892 (0.59/frame)** | **871 (0.97/frame)** |
| `uitree_layout_nodes` / walk | **6,918** | **6,860** |
| `uitree_layout_node_skip` | 6,100,098 (**98.85%**) | 5,895,203 (**98.67%**) |
| `uitree_layout_depth_recompute` | 241 | — |
| `uitree_cc_create` | 6.4/frame | 12.2/frame |

The plugin set changes the constants and not the shape. **Roughly one walk per
frame, ~6,900 nodes swept, ~99% of them skipped inside the walk.**

### 1.2 Who invalidates: a blame channel, not a guess

`TORIRS_LAYOUT_BLAME=1` (added in `uitree_layout.c`) records the return address
of the call that raises `layout_stale` **from 0** — the transition that
actually costs the following walk; a second invalidation in the same frame is
free and is deliberately not counted. Built at `OPT=0` so the frame that
returns is the frame you wanted named, resolved against `nm`:

```
900 stale 0->1 transitions over 900 frames        <- exactly 1.00 per frame
   338  uitree_note_mutation+0xeb   (UITREE_IMPACT_LAYOUT_SELF)
   321  UITree_Push+0x37f           (a component was created)
     4  UITree_Reparent+0x158
     2  uitree_note_mutation+0xb9   (UITREE_IMPACT_LAYOUT_TREE)
```

Tagging each `UITree_Push` with its parent's `component_id` names the widget
factories — 14.13 pushes per frame, dominated by:

```
  4.00/frame  iface 896, child 1
  2.91/frame  iface 161, child 37     (the gameframe root)
  2.22/frame  iface 162, child 58     (the chatbox)
  2.02/frame  root (parent_index < 0)
  0.95/frame  iface 239, child 11
  0.72/frame  iface 162, child 35
```

### 1.3 The answer

**It runs every frame because the UI tree really does change every frame.** CS2
scripts driving a live gameframe create ~14 components and perform a handful of
genuine geometry writes per frame; the chatbox and the gameframe rebuild rows,
and interface 896 makes four widgets a frame. These are not spurious writes —
the typed setters already carry no-change guards (`uitree_apply_nochange` fires
28,692 times in 1500 frames, i.e. 19 redundant writes per frame are *already*
being filtered before they can invalidate anything). There is simply no idle
frame in a live world.

**So the invalidation is legitimate, and the cost is not the layout math.** The
frame pays an **O(all components)** sweep to do **O(~80 nodes)** of work. That
is the defect, and it is independent of who invalidates.

### 1.4 What to do about it — ranked

| # | change | expected | risk |
|---|---|---|---|
| L1 | **Dirty worklist instead of a full sweep.** Keep a small set of invalidated node indices; resolve those and their descendants. Fall back to the full sweep when the set overflows or `layout_force_full` is set. | **~2.4-2.7 of 2.7-3.0 ms** | medium — the walk propagates `changed` down; the worklist must seed descendants of every changed node |
| L2 | **Split `generation` into topology-that-matters and topology-that-does-not.** A `cc_create` under one parent invalidates the cached `layout_order`/`layout_depth` for the whole tree (`layout_order_gen != generation`). 241 of 892 walks recomputed depth. Append-only pushes can extend the order instead of rebuilding it. | 0.2-0.4 ms | low |
| L3 | **Make the 19 already-filtered no-change writes never reach the tree.** They are caught in `UITree_Set*At`, one call deep; catching them in the CS2 op layer saves the call and the component lookup, not the invalidation. | <0.15 ms | low |
| L4 | **Ask why iface 896 makes 4 widgets a frame.** If that is a per-frame rebuild of a list that rarely changes, fixing it removes ~1/3 of the pushes *and* their topology bumps. | unknown, possibly 0.3-0.5 | low to investigate |

L1 is the one that matters and it subsumes the others: with a worklist,
"something invalidated this frame" stops costing 6,900 iterations.

---

## 2. Plan for the other four

Costs are the XP `--d3d9-zbuffer` figures from `profiles.md` (21.46 ms/frame
total). Every item below names how to verify it, because several are
hypotheses drawn from reading the code with the profile in hand, not
measurements.

### 2.1 `d3d9_draw_model` — 1.845 ms (8.60%)

**What it is.** The per-model entry point in
`platform_win32_renderer_d3d9_core.c:5036`, called once per `TORIRSRC_DRAW_MODEL`.
`painter_commands` measures **1,621 model commands per frame**, so this is
**1.14 us per model**.

**The finding: picking lives inside it.** Lines 5071-5095 run
`ToriDraw_ProjectedModelContainsAabb` / `ToriDraw_ProjectedTileMouseHitTest` /
`ToriDraw_ProjectedModelMouseHitTest` for every pickable model, gated only on
`renderer->pick_enabled` — and `main.c:487` arms that **every frame the cursor
is anywhere in the viewport**. A stationary cursor over an unchanged scene
re-answers the identical hit test 1,621 times a frame.

| # | change | expected | risk |
|---|---|---|---|
| D1 | **Gate the pick pass on cursor movement.** Arm `SetPick` only when the mouse moved, the camera moved, or the scene changed since the last armed frame; otherwise reuse last frame's `pick_hits`. The arm `hunt-w10` (`docs/xpbench/arms/hunt-w10`) already did a cursor gate on pick-only projection — read it first. | 0.3-0.6 ms | low; stale hits for one frame after a scene change if the invalidation set is wrong |
| D2 | **Hoist the pick test out of the draw loop entirely.** It shares only `RenderModel1Project`'s output with drawing. A separate pass over the same command list runs only on armed frames and lets D1 skip it wholesale. | enables D1 cleanly | medium — restructuring |
| D3 | **Split the zbuffer material classification off the per-model path.** `d3d9_draw_model` grew 0.35 -> 1.85 ms going painter -> zbuffer while the sort fell 2.35 -> 0.18. The classification is per (element, pose track); cache it keyed on the pose so a static loc classifies once, not once per frame. | 0.4-0.8 ms | medium — must invalidate on `d3d9_reclassify_face_range` |
| D4 | **Early-out before `RenderModel1Project` for models whose placement and camera are both unchanged.** 1,621 projections per frame for a still camera is the same cross-frame-coherence argument as D1. | 0.2-0.4 ms | medium — correctness depends on a sound "camera unchanged" test |

**Verify:** D1/D2 by running with the cursor parked outside the viewport
(`pick_enabled` false) and re-taking an EIP profile — the delta *is* the pick
cost. Do that before writing any code; it is one run and it sizes D1 exactly.

### 2.2 Picking — ~1.15 ms (`app_world_pick_finish` 0.809 + `RS_Minimenu_Build` 0.208 + `hit_test_interactive_recursive` 0.122)

This is the *classification* half; §2.1 is the *collection* half. They share a
gate.

| # | change | expected | risk |
|---|---|---|---|
| P1 | **`getenv` on the per-frame path.** `app_world_pick_finish` (`app.c:16393`) calls `getenv("TORIRS_WORLD_PICK_DEBUG")` on **every** call. `getenv` on this platform is a linear scan of the environment block, and the codebase already knows this — `app_plugin_highlight_debug()` caches exactly this pattern in a `static int`. Same fix, same file. There is a second one at `main.c:494` (`TORIRS_FRAME_DEBUG`) on the same path. | 0.05-0.15 ms, ~1 hour | none |
| P2 | **Skip classification when the hit set is byte-identical to last frame's.** With the cursor still, `ToriRS_PickHitsClassify` re-derives the same pickset every frame. Hash the raw hits; equal hash -> keep the previous `world_pickset`. | 0.4-0.6 ms | low |
| P3 | **Rebuild the minimenu only when the pickset or modifier state changes.** `RS_Minimenu_Build` at 0.208 ms is downstream of P2 and mostly falls out of it. | 0.15 ms | low |

P1 is free and should go in regardless of whether the rest is ever done.

### 2.3 `app_plugin_highlights_rebuild_pools` — 0.635 ms (2.96%)

**What it is.** `src/plugin/torirs_plugin_bridge.u.c:477`. Sets
`plugin_highlight_count = 0` and rebuilds the entire highlight list from
scratch every frame, walking each armed pool's entities against the highlight
member list — O(entities x members).

**Measured on this machine:** with the committed `plugin_prefs.ini` (9 of 17
plugins), `TORIRS_HIGHLIGHT_DEBUG=1` shows a **loc** highlight registered, so
`want[APP_PLUGIN_HL_POOL_LOC]` is armed and the loc walk runs every frame. The
pool gate (`app_plugin_highlight_pools_wanted`) is doing its job — the pool
genuinely has a member. Note its own comment: a single **opgroup** highlight
re-arms NPC, LOC *and* OBJ at once, so one such member costs three full walks.

| # | change | expected | risk |
|---|---|---|---|
| H1 | **Epoch gate.** Rebuild only when the highlight state or the entity pools changed. Both sides already carry change signals (`RS_HighlightState` mutations; the world's per-cycle entity registration). A still frame with a static highlight should cost a comparison, not a walk. | 0.5-0.6 ms | low |
| H2 | **Index the member list by id.** The walk is entities x members with a linear member scan. A small hash keyed on uid/type turns it into entities x 1. Worth doing only if H1 cannot fully gate. | 0.2-0.3 ms | low |
| H3 | **Narrow the opgroup re-arm.** Record which pools an opgroup highlight can actually match instead of arming all three. | up to 2/3 of the walk when an opgroup member exists | low |

H1 first; H2 and H3 only if the profile still shows it after.

### 2.4 `ToriRS_FrameNextCommand` — 0.576 ms (2.69%)

**What it is.** `src/render/torirs_frame.c:2239`, the pull-based command
iterator: one call per emitted render command.

Two structural costs, both visible by reading it:

1. **Per-command re-derivation of a per-desc constant.** For every command
   pulled, the loop re-runs a chain of `if( desc->kind == ... )` tests to
   recompute `is_scrollbar` and `sb_steps`. A desc that emits N steps runs that
   chain N times, and it is a pure function of the desc.
2. **`memset(out, 0, sizeof(*out))` per command.** `sizeof(struct
   ToriRS_RenderCommand)` is **128 bytes** (measured; the union is 120 of it,
   sized by `u.model`). There are 12 such memsets in the file. At 1,621 world
   commands per frame that is **~207 KB of zeroing per frame** on top of the
   copy — and it is a strong candidate for part of the 1.765 ms sitting in
   `msvcrt.dll` in the same profile.

| # | change | expected | risk |
|---|---|---|---|
| F1 | **Compute `sb_steps` once per desc**, when `emit_index` advances, and cache it on the iterator alongside `scrollbar_step`. | 0.15-0.25 ms | low — mechanical |
| F2 | **Zero only the tag plus the arm being written**, not the whole 128-byte union. Each emitter fills exactly one arm; the memset exists so unwritten fields read as zero. Narrow it to the arm, or drop it where the emitter assigns every field. | 0.1-0.2 ms here, plus some of `msvcrt` | medium — a missed field becomes a garbage read, so pair it with a debug build that poisons the union instead of zeroing it |
| F3 | **Shrink the command.** `u.model` sizes the union at 120 bytes; if it carries anything the renderer re-derives anyway, moving it behind a pointer shrinks every command in the stream. | reduces F2's ceiling | medium |

F1 is the safe one. F2 wants the poison-in-debug harness in the same commit.

---

## 3. Suggested order

1. **P1** — cached `getenv`, no risk, immediate.
2. **The pick ablation run** (cursor outside the viewport) — one run, sizes D1
   and P2 before any code is written.
3. **H1** — epoch gate on highlights, self-contained, ~0.6 ms.
4. **F1** — per-desc step count, mechanical, ~0.2 ms.
5. **L1** — layout dirty worklist. Biggest single win (~2.5 ms) and the most
   design work; do it once the cheap items are banked.
6. **D1/D2** — pick gate, sized by step 2.
7. **D3** — pose-keyed material classification.

Steps 1-4 are ~1 ms of a 21.5 ms frame for low risk. Step 5 alone is worth more
than all of them together.

**Measure each on the XP box through the harness in `profiles.md`, not
locally.** The Windows 11 numbers in §1 are for *attribution* — counts and call
sites, which are platform-independent. They are not frame times, and this
machine's frame is nothing like the P4's.
