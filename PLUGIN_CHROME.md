# Plugin chrome

Plugin chrome is retained UI around the game: the selected gameframe, live
client surfaces, named controls, plugin panels, and overlays. The public model
is deliberately small: plugins publish what they provide, the host selects and
composes it, and executors receive only recorded mutations.

For the detailed rationale, API shapes, migration notes, and phased
implementation plan, see [GAMECHROME_PLUGINAPI_V2.md](GAMECHROME_PLUGINAPI_V2.md).

## Mental model

There are four visual layers, in this order:

```text
lane tree
  Cache/RevConfig provides native controls and live game surfaces.
      |
      v
selected frame
  One host-selected frame arranges the viewport, minimap, chat, and sidebar.
      |
      v
named UI contributions
  Plugins provide missing nodes or replace specific facets of existing nodes.
      |
      v
overlays, panels, and scene additions
  Plugin output is placed against the fully resolved frame.
```

The important drawing boundaries remain:

- Frame art draws above the scene and below mounted client interfaces.
- Canvas overlays draw above mounted interfaces.
- World overlays stay clipped to the viewport.

## Host-selected gameframes

A frame plugin publishes static **frame offers**. Each offer has a stable
canonical id such as `gameframe-layout/classic-fixed` and a separate
human-readable label. Publication does not select or acquire the frame.

The host owns the frame catalogue and is the only code allowed to select the
active offer. The saved Client Settings > Gameframe value is either `auto` or
one canonical offer id. `auto` resolves to `core/native`, which follows the
native toplevel chosen by the lane/server.

Exactly one offer is active. Registration order cannot decide the winner, and
frame providers do not have an independent enable switch. Selecting an offer
activates its provider. If an offer is missing, unsupported, still loading, or
fails to build, the host keeps a valid frame or falls back to `core/native`
without discarding the user's saved choice.

Frame construction is transactional. The selected provider builds into a
scratch declaration; the host validates it and commits the whole declaration
at once. A partial or invalid frame is never exposed. Provider configuration
changes call frame invalidation; they do not re-claim ownership.

## Canonical named UI

Anything another plugin may inspect, alter, or invoke has one semantic dotted
name. Shared chrome uses the core `frame.*` vocabulary; plugin-private nodes
are automatically namespaced as `plugin.<plugin-id>.*`. RevConfig translates
lane-specific component layouts to those names, and documented legacy aliases
canonicalize to the same interned reference.

A named node is split into independently composable facets:

- **bounds**: geometry and tree relationship;
- **appearance**: imagery, label, visibility, and paint behavior;
- **actions**: hit area, enabled state, and named operations.

The lane and selected frame form the base tree. A plugin may declare a retained
contribution with one of three intentions:

- `MODIFY`: replace requested facets only when a base node exists;
- `PROVIDE_IF_MISSING`: provide a node only when the base has none;
- `REPLACE_OR_PROVIDE`: replace the requested facets or provide the node.

Independent facets compose. Competing declarations for the same exclusive
facet do not use first-writer-wins: the host resolves the complete declaration
set deterministically, keeps the base facet when possible, and reports a
conflict to both plugins. Names and contributions remain stable across cache
rebuilds, frame switches, and plugin callback order. Plugin teardown removes
its contributions automatically.

## Exact placement and safe areas

Safe space is geometry, not a fake UI surface. The placement service maintains
bounded, exact rectangle sets rather than reducing every subtraction to one
largest rectangle.

It exposes four areas:

| Area | Meaning |
| --- | --- |
| `PLATFORM_SAFE` | Canvas excluding OS-controlled regions such as notches, system bars, or an on-screen keyboard. |
| `FRAME_BUILD` | Platform-safe space excluding visible lane UI the selected frame may not replace. |
| `OVERLAY_SAFE` | Viewport/canvas space excluding platform hazards, resolved UI occluders, and plugin reservations. |
| `RAW_VIEWPORT` | The resolved viewport without safety subtraction. |

Most HUD and overlay plugins ask `placement.place(OVERLAY_SAFE, ...)` for a
rectangle of a requested size and anchor. Advanced callers may iterate the
fragments or test containment. If no fragment fits, placement fails instead of
silently returning covered space.

Persistent docks use named reservations. A reservation is local to its plugin,
is updated by restating the same name, and is released automatically at plugin
teardown. Simultaneous reservations are ordered by `(edge, plugin id,
reservation name)`, so callback order cannot change the result. Named UI nodes
declare `blocks_frame` and/or `blocks_overlay`; this metadata drives placement
instead of hard-coded slot lists.

## Retained execution uses a mutation journal

Executors do **not** scan or diff the complete retained tree during normal
ticks. Every model setter first compares the old and new value. A real change
records `(handle, property)` in a bounded journal; equal writes record nothing.
Repeated writes to the same property coalesce, so the executor receives the
final retained value once.

```text
setter changes retained state
            |
            v
record/coalesce mutation command
            |
            v
executor drains only queued changes
```

Structural add, remove, and reorder operations use the same journal, with
ordering rules that prevent a removed handle from being reused too early. An
idle tick is O(1): it scans no panels or widgets and opens no transaction.
Normal update work is proportional to the queued changes, not to total UI
size.

A full snapshot is reserved for recovery: first bind, rebind after the
executor loses retained state, or journal overflow. Overflow marks the retained
copy for one complete resync; it never applies an arbitrary partial suffix.

## Executor boundary

The only external plugin-chrome executors are web executors:

- `web`: Emscripten/DOM;
- `browser`: the embedded web engine used by native shells.

The in-canvas `BUFFER` presenter remains an internal fallback and is not a
selectable external executor. SDL, GDI, Android-native, and generic `platform`
executors are not supported paths and must not appear in parsing, factories,
manifests, profiles, or normal source lists.

## Plugin-facing rules

- Publish frame offers statically; do not claim the screen.
- Address UI by canonical semantic name, never by a transient component-array
  index.
- Declare only the facets a plugin supplies, and handle reported conflicts.
- Ask the placement service for usable space; do not hand-compose viewport,
  frame, platform, and keyboard rectangles.
- Treat frame, UI, panel, scene, and draw builders as callback-scoped objects.
- Restate retained state through setters and invalidate only when an input
  changes; do not redraw or republish unchanged state every tick.
- Let the host tear down contributions, reservations, assets, and instances
  owned by a stopped plugin.

The former `layout_claim` / chrome-part scope system is compatibility machinery,
not the plugin API design. New code should use the V2 catalogue, named UI,
placement, and callback-scoped builder APIs described in
[GAMECHROME_PLUGINAPI_V2.md](GAMECHROME_PLUGINAPI_V2.md).
