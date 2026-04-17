---
name: UI Dirty Flag System
overview: Per-uitree-node dirty flags with command-cache replay to skip re-running UI step handlers; world/minimap/compass always dirty; hover edge-triggered for hoverable nodes; global invalidation on iface/viewport/tree changes.
isProject: false
---

## Correction: renderer framebuffer behavior (updated)

The original analysis claimed that **every** renderer backend clears the full framebuffer at the start of each frame, and therefore skipping all UI draw commands would produce a black frame unless commands were replayed from cache.

That description matches **some** legacy paths (e.g. full `glClear` / `ClearRenderTargetView` each frame in certain GL/D3D11 entry points).

**Newer renderers do not clear each frame.** They typically retain the previous frame’s color buffer and rely on explicit clears (e.g. `TORIRS_GFX_CLEAR_RECT`), full-scene passes, or compositing rules to update regions.

### Implications for dirty UI work

1. **Command-cache replay** (replaying last frame’s `ToriRSRenderCommand` slice without re-running step handlers) remains the right optimization for **CPU cost** (projection, tree walks, animation hooks). It is **not** forced by “must repaint or go black.”

2. **Skipping emission** when an element is “clean” is a **different** contract: if a node emits no commands and no clear runs for its region, **stale pixels may remain** on backends that preserve the buffer. That can be correct (nothing changed) or wrong (missed invalidation). Dirty rules must match the **active** renderer’s persistence and compositing model.

3. **Pairing clears and draws** (`CLEAR_RECT` + draws in one cached slice) remains important wherever the UI expects a defined background for a widget rect—regardless of whether the GPU clears the whole screen each frame.

4. When auditing behavior, classify render paths as **clear-each-frame** vs **persistent framebuffer** and validate UI dirty/clear policy against the path in use.

---

## Implementation summary (reference)

- Runtime: `UINodeRuntime` parallel to `UITree` nodes; `uiscene_cached_commands` stores appended slices; `LibToriRS_FrameNextCommand` replays or captures per node.
- Pre-pass: `ui_dirty_pre_pass` — global invalidation (iface ids, tab, viewport size, `UITree.generation`), always-dirty types (world, minimap, compass), hover edge for hoverable nodes, content signatures for RS components / builtins.
- Input: redstone tab click handling runs in DFS pre-pass so it runs even when draw is cache-replayed.
- `UITree.generation` increments on each `uitree_push_*`; RS dynamic state structs carry `version` fields for future mutation bumps.

---

## Out of scope

- Overlap propagation between stacked widgets (v2).
- Monotonic cache compaction policy details (thresholds may be tuned per platform memory).
