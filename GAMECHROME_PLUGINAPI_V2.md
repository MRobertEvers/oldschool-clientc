# Game chrome and plugin API v2

Status: implemented

This document replaces the ownership proposal in the pasted “Publish, Don't
Claim” note. It keeps the useful parts of the current implementation—retained
layout declarations, per-revision role bindings, automatic resource cleanup,
and the three correct drawing layers—but gives each decision one clear owner
and gives plugin authors a much smaller API to learn.

## Implementation status

| Phase | Status | What that means now |
| --- | --- | --- |
| 0. Behavior tests | Complete | Frame geometry, lifecycle, retained execution, and safe-placement behavior have focused regressions. |
| 1. V2 shell | Complete | The typed V2 contract, host registration, per-instance state, callback dispatch, and namespaced modules are the sole bundled plugin API. |
| 2. Frame catalogue | Complete | Preferences, stable offers, host selection, last-valid retention, atomic candidate validation, Client Settings, and native V2 frame providers are live. |
| 3. Named UI | Complete | Interned names, facets, deterministic conflicts, retained changes, frame nodes, action routing, and RevConfig bindings are live. |
| 4. Placement | Complete | Exact rectangle sets, named reservations, stable ordering, and resolved revisions replace public pseudo-slots. |
| 5. Retained web execution | Complete | Setters record an O(1)-coalesced mutation journal; idle drain is O(1). `web` and `browser` are the only external executors; BUFFER is internal recovery/fallback presentation. |
| 6. API migration/removal | Complete | Bundled C and Lua plugins use native V2. The V1 flat API, subscription surface, adapter, aliases, and pseudo-slots are removed. |

The phase sections below are a historical implementation record. They explain
why the system was built in that order; they are not outstanding work.

## The short version

The system obeys six rules:

1. A frame plugin **offers** one or more frames. It never claims the screen.
2. The host is the only code that chooses the active frame. `Auto` means “use
   the frame the lane/server already selected.”
3. Every plugin-addressable piece of game chrome has one semantic name in one
   UI tree. The name survives a cache revision, a frame switch, and a CS2
   rebuild.
4. Plugins ask the placement service where something fits. They do not combine
   viewport, frame, lane, keyboard, and notch rectangles themselves.
5. Plugin callbacks restate desired state. The host validates and atomically
   commits it; partially built frames and order-dependent claims never become
   visible.
6. Application-chrome panels use retained semantic controls. Canvas drawing is
   for content over the game, not for recreating panel text, lists, buttons, or
   settings.

The most important user-visible change is that “Gameframe” becomes one client
setting. Enabling two frame-provider plugins cannot produce a race because
frame providers do not have independent enable switches.

## Why v2 was needed

The pre-v2 public surface had good machinery but exposed too much of that
machinery as the programming model. The items below describe the removed
legacy starting point, not APIs that remain available.

- `ToriRS_PluginApi` is a flat table of roughly one hundred operations in a
  header over four thousand lines long. Related operations are far apart and C
  and Lua present different shapes.
- Legacy `layout_claim()` was an anonymous global lock. The first caller won. Both
  `gameframe.c` and `mobile_gameframe.c` can be enabled together, and registry
  order then becomes policy.
- Legacy `layout_claim()` had three unrelated jobs: acquire the frame, change its
  canvas policy, and request another layout pass. Code re-claims after a drawer
  click simply to invalidate geometry.
- A frame choice existed partly in a plugin enable flag, partly in a
  plugin enum, partly in the server-opened `frame_root`, and partly in the
  host's `layout_owner`. Those values could disagree.
- The same UI item can be addressed as a layout slot, a numeric member, a raw
  component id, a RevConfig role, or a chrome-part string. A plugin author has
  to know which identity is valid for which operation.
- `SAFE_GAMECHROME` and `SAFE_LANECHROME` are derived areas placed in the same
  enum as movable live surfaces. `safe_os()` is separate, so the common caller
  still has to combine all three correctly.
- `plugin_rect_subtract()` deliberately reduces a non-rectangular result to
  the largest rectangle. That is a useful fallback, but it is not an exact
  description of usable screen space and should not be presented as one.

This is not a request to replace `UITree`. It is a request to put a coherent
public model in front of the behavior already implemented by `UITree` and the
plugin host.

### Corrections to the pasted proposal

The useful idea in that proposal is “publish choices; let the host select.” V2
changes several details:

- The legacy frame plugins did check a refused `layout_claim()` and log it.
  That did not fix the underlying first-runner-wins policy; the catalogue now
  replaces it.
- Offers are static plugin descriptors, not `frame_publish()` calls made during
  `EV_START`. Static descriptors make the entire catalogue available before
  lifecycle callbacks and eliminate another ordering edge.
- A minimum size constrains a selected frame's logical canvas. It does not make
  the resolver change frames when the window crosses that size.
- `Auto` resolves to the native frame itself. A plugin does not inspect
  `frame_root` to choose a look-alike.
- The common safe answer is `OVERLAY_SAFE` in a placement module, not another
  member of the layout-slot enum.
- Safe space is retained as a region set internally. Intersecting three
  already-lossy “largest rectangle” answers would still discard valid space.
- Named UI facet conflicts are resolved from the full declaration set. They do
  not preserve the legacy first-claim rule under a different name.

## What stays

The following design choices are already sound and should remain:

- The cache or RevConfig owns the real live surfaces: viewport, minimap, chat,
  sidebar, modal content, and cache-provided controls.
- A frame layout is retained. The host clears a candidate builder, calls one
  provider, and treats omitted surfaces as hidden.
- Frame art draws above the scene and below mounted interfaces. Canvas overlays
  draw above interfaces. World overlays remain clipped to the viewport.
- UI names resolve live rather than exposing long-lived component-array
  indices.
- Resources, reservations, UI contributions, and scene instances are owned by
  a plugin and are removed automatically when it stops.
- RevConfig role matchers remain the per-lane translation layer.
- Missing content is normally an answer, while misuse of the C contract is an
  assertion. Lua converts the same misuse into an actionable script error.

## A human mental model

There are four layers, applied in this order:

```text
lane tree
  The cache/RevConfig supplies live surfaces and native controls.
      |
      v
selected frame
  Exactly one provider arranges those surfaces and supplies base furniture.
      |
      v
named UI contributions
  Plugins may replace one facet of a named node or provide a missing node.
      |
      v
overlays, panels, and scene additions
  Ordinary plugin output is placed against the resolved frame.
```

The frame catalogue is not a fifth visual layer. It is only a list of choices
from which the host selects the second layer.

### Terms used by the public API

| Term | Meaning |
| --- | --- |
| lane | The booted cache, RevConfig profile, and client behavior belonging to one revision/lineage. |
| frame offer | One selectable frame supplied by core or a provider plugin. |
| active frame | The single offer the host has resolved and committed. |
| surface | Live client content that a frame places but does not implement, such as the viewport or chat. |
| UI node | A semantic, named piece of UI with bounds, appearance, actions, and an optional parent. |
| contribution | A retained request to provide or replace facets of a UI node. |
| placement area | A possibly fragmented set of rectangles in which a class of content may be placed. |
| reservation | A named, retained request for a dock or overlay to consume space. |

The public API does not use *arranger*, *dresser*, *part*, and generic
*object* as overlapping terms. In this repository “obj” already means a ground
item and `object_*` currently means a plugin-owned 3D model. V2 uses **UI node**
for named interface elements and **scene instance** for plugin-owned 3D models.

## One authority per decision

| Question | Sole authority |
| --- | --- |
| Which frame offers exist? | Core plus static frame-offer descriptors registered by provider plugins. |
| Which frame did the user ask for? | One device-local `preferred_frame` string in `preferences.ini`. |
| Which frame is active now? | The host's frame resolver. This value is derived, never independently persisted. |
| Which native toplevel did the server open? | The app/engine. It is an input to `core/native`, not a plugin selection. |
| What does a semantic UI name mean on this lane? | RevConfig plus the active frame's retained declaration. |
| Where is that UI node now? | The resolved `UITree`/frame declaration. |
| Where may an overlay be placed? | The placement service, derived from current occluders and reservations. |

RevConfig therefore does **not** select a plugin frame. It maps the lane's
native nodes into the common vocabulary and marks lane-owned occluders. Plugin
source owns plugin frame offers. Preferences own the user's choice.

## Frame catalogue and selection

### Stable identifiers

Every offer has a stable machine id and a separate label. The host creates the
machine id from the provider plugin id and the offer's local id:

```text
core/native
gameframe-layout/classic-fixed
gameframe-layout/modern-fixed
gameframe-layout/modern-resizable
mobile-gameframe/stone-drawer
```

Only labels are shown in the normal UI. The stable id is saved. Reordering or
renaming a label therefore cannot change a saved choice.

`auto` is a preference value, not a frame offer. In v2 it resolves to
`core/native`, whose implementation follows the toplevel selected by the
server. There should not be separate “Auto” implementations in frame plugins.

Plugin ids are already unique. Local offer ids must be unique within their
provider. Duplicate ids are rejected when the plugin is registered, before any
frame can be selected. There is no last-writer-wins behavior.

### Frame offers are descriptors, not startup calls

A C provider declares offers in its plugin definition. A Lua plugin returns
the same information in its definition table. Publication is therefore
complete before plugins start and cannot depend on event order.

Illustrative C shape:

```c
static struct ToriRS_FrameOffer const FRAME_OFFERS[] = {
    {
        .struct_size = sizeof(struct ToriRS_FrameOffer),
        .id = "classic-fixed",
        .title = "Classic Fixed",
        .canvas = TORIRS_FRAME_CANVAS_FIXED,
        .width = 765,
        .height = 503,
        .build = frame_build_classic,
        .draw = frame_draw_classic,
    },
    {
        .struct_size = sizeof(struct ToriRS_FrameOffer),
        .id = "modern-resizable",
        .title = "Modern Resizable",
        .canvas = TORIRS_FRAME_CANVAS_WINDOW,
        .min_width = 765,
        .min_height = 503,
        .build = frame_build_modern_resizable,
        .draw = frame_draw_modern,
    },
    { .id = NULL }
};

struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_GAMEFRAME = {
    .struct_size = sizeof(struct ToriRS_PluginDefV2),
    .id = "gameframe-layout",
    .title = "Gameframe Layouts",
    .version = "2.0.0",
    .state_size = sizeof(struct FrameState),
    .frames = FRAME_OFFERS,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
    },
};
```

The mobile descriptor must use `MOBILE_MIN_W` and `MOBILE_MIN_H` from
`mobile_gameframe.c`. The current width is 640; the pasted design's 320 by 480
example does not describe the implementation and must not become a second
source of truth.

### Provider lifetime

A frame provider is not an independently toggleable visual feature. Its offers
appear under Client Settings > Gameframe, and choosing an offer activates the
provider. This removes the contradictory states “plugin enabled but not
selected” and “selected offer's plugin disabled.”

Provider-specific settings, such as Stone Drawer's art family and map housing,
remain available as advanced settings for that offer. They do not control
whether the offer owns the screen.

The host may start providers lazily. While a selected provider is loading
assets, the last valid frame—or `core/native` if there is none—stays active.
The provider returns one of the statuses below. A non-ready result also carries
a short reason string for the settings page and log:

- `READY`: the candidate is complete and may be committed.
- `PENDING`: required asynchronous assets are still loading; retry when an
  asset or relevant input changes.
- `UNSUPPORTED`: this offer cannot run on the current lane/platform, with a
  human-readable reason.
- `ERROR`: the build failed; keep a safe frame and report the error once.

Stopping or faulting the active provider immediately restores `core/native`.
The saved preference is not overwritten; switching lanes or fixing the
provider may make it available again.

### Resolver algorithm

The resolver runs when the preference, lane, game screen, catalogue, provider
state, or native toplevel changes.

1. Outside the game screen, activate no gameframe override and leave the title
   tree alone.
2. If the preference is `auto`, target `core/native`.
3. Otherwise find the exact saved offer id.
4. If it is missing or unsupported, keep the last valid committed frame (or
   `core/native` when none has committed), preserve the requested id, and
   expose one fallback reason to settings and diagnostics.
5. If it is pending, keep the last valid active frame while it loads.
6. Build into a scratch declaration, validate it, and atomically commit it.
7. Dispatch layout and frame drawing only to the committed provider.

Canvas minimums are constraints, not resolver predicates. A resizable frame
must not switch back to native merely because a user made the window smaller.
For a fixed offer the engine uses the stated logical size and letterboxes or
scales it. For a window-following offer it clamps the logical canvas to the
offer's minimum using the existing plugin-layout minimum-size path.

### One live state record

The host keeps one record similar to:

```c
struct PluginFrameSelection {
    char requested_id[TORIRS_FRAME_ID_MAX]; /* "auto" or a canonical id */
    int active_provider;
    int active_offer;
    enum ToriRS_FrameStatus status;
    char reason[TORIRS_FRAME_REASON_MAX];
    uint32_t revision;
};
```

`active_provider` and `active_offer` are the only answer to “who draws the
frame?” Engine suppression, canvas policy, callbacks, settings status, and
debug output derive from this record. A transient target/candidate staging
record is allowed, but it is never a second active frame. There is no public
layout owner, per-plugin claimed flag, or second copied active name.

The server's live root id remains an engine fact used by `core/native` and the
RevConfig binder. It is not another active-frame id.

### Migrating existing preferences

Existing users must not lose their current choice. On the first v2 load, when
`preferred_frame` is absent:

1. If `gameframe-layout` is enabled, map its saved `layout` label/index to the
   corresponding `gameframe-layout/*` offer. Its current `Auto` value maps to
   the new `auto`, whose clearer meaning is “leave the server/native choice in
   charge.”
2. Otherwise, if `mobile-gameframe` is enabled, select
   `mobile-gameframe/stone-drawer`.
3. Otherwise store `auto`.
4. If both old plugins are enabled, prefer `gameframe-layout`, matching their
   current registry/start order, and log the one-time migration decision.

Stone Drawer's `art` and `housing` settings remain in its provider settings.
Record a migration version so stale legacy `enabled=` and `layout=` lines do
not re-run this conversion. Do not delete unknown keys while decoding an old
preferences file.

### Explicit invalidation

The selected provider gets one operation:

```c
api->frame.invalidate(api);
```

It means “my retained frame declaration is stale.” Drawer changes, chat
open/close, provider configuration, and newly loaded masks use it. It does not
select, acquire, release, or alter canvas policy. Calls by a non-active provider
are harmless and do not affect selection.

## Atomic retained frame building

The host passes a `ToriRS_FrameBuilder` only to the active offer's build
callback. Ordinary plugins never receive it.

The builder has task-oriented operations:

```c
builder->surface(builder, TORIRS_SURFACE_VIEWPORT, rect);
builder->surface(builder, TORIRS_SURFACE_COMPASS, compass_rect);
builder->surface(builder, TORIRS_SURFACE_ORBS, orb_pack_rect);
builder->surface(builder, TORIRS_SURFACE_CHAT, rect);
builder->surface_member(builder, TORIRS_SURFACE_CHAT_BUTTONS, 3, rect);
builder->skin(builder, TORIRS_SURFACE_MINIMAP, skin);
builder->surface_overlay(builder, TORIRS_SURFACE_COMPASS, &overlay);
builder->ui_node(builder, "frame.minimap.housing", &node);
builder->scrollbar(builder, &skin);
builder->reason(builder, "Waiting for the minimap housing image");
```

The build callback also receives a context containing:

- the logical canvas;
- the already-composed frame-build placement area;
- the lane identity/capabilities needed by a genuinely cross-lane provider;
- the local offer id being built.

The host starts with an empty scratch builder. Before commit it verifies:

- required surfaces exist and all rectangles are valid;
- surface/member numbers are in range;
- UI names are valid and unique within the declaration;
- parent/anchor relationships contain no cycle;
- image handles belong to the provider; an invalid/foreign handle rejects the
  candidate, while a required image that is still loading makes it pending;
- only supported surfaces receive skins or overlays;
- no provider tries to declare a derived placement area as a surface.

If validation fails, no part of the candidate is applied. This is the key
difference from making fallback depend on whether a plugin “faulted last
pass”: the previous valid tree remains intact and there is never a half-frame.
`reason()` is ignored for `READY` and supplies the explanation for `PENDING`,
`UNSUPPORTED`, or `ERROR`.

After a successful commit, named UI modifiers are resolved, placement areas
are recomputed, and one change notification is emitted. Notifications are
coalesced; a reservation changed from inside a notification schedules another
transaction rather than recursively dispatching.

Named nodes authored by `FrameBuilder.ui_node` are presented on the frame
surface: above the world and below native interface widgets. Their action
regions therefore capture clicks on custom redstones and switches without
blocking inventory/panel controls drawn over them. They do not depend on the
lane's corresponding native chrome node remaining visible.

## Named UI tree

### One semantic name

Public names use a dotted hierarchy:

```text
frame.viewport
frame.minimap
frame.minimap.housing
frame.compass
frame.chat
frame.chat.button.public
frame.chat.button.private
frame.chat.button.trade
frame.chat.button.report
frame.sidebar
frame.sidebar.tab.0
frame.modal
frame.orb.hitpoints
frame.orb.prayer
frame.orb.run
frame.orb.special
```

These are semantic identities, not paths into the cache. A RevConfig profile
may bind `frame.chat.button.report` to a dat1 slot member on one lane and an IF3
component on another. A selected plugin frame may provide it directly. The
consumer uses the same name in all three cases.

Names under `frame.*` are a core-owned vocabulary. New well-known names are
added deliberately and documented. A plugin-created private node is
automatically namespaced as `plugin.<plugin-id>.<local-name>`, so two plugins
cannot collide accidentally.

During migration, old names such as `report_button`, `minimap_edge`,
`orb_run`, and `xp_drops` are aliases to canonical names. New code uses only
canonical names; aliases are removed after all bundled profiles and plugins
have migrated.

### References are stable; resolutions are snapshots

A plugin may cache a `ToriRS_UiNodeRef`, which represents the semantic name.
It may not cache a component id or `UITree` node index.

```c
struct ToriRS_UiNodeRef report = api->ui.ref(api, "frame.chat.button.report");
struct ToriRS_UiNodeInfo info;

if( api->ui.info(api, report, &info) && info.visible ) {
    /* info.bounds is a snapshot for this transaction/frame. */
}
```

When a CS2 subtree rebuilds or the frame changes, the reference stays valid and
resolves to the new provider/node. An absent node is an ordinary false result.

Ordinary actions are semantic too:

```c
api->ui.invoke(api, report, "activate");
```

RevConfig maps `activate` to the correct lane-native operation internally. Raw
component ids and numeric ops remain available only under an explicitly
lane-specific `api->cache` escape hatch.

A plugin that replaces an action facet can delegate to the lane control below
it without recursively calling its own callback:

```c
if (api->ui.base_action_available(api, run, "enable"))
    api->ui.invoke_base(api, run, "enable");
```

`invoke_base` resolves the live RevConfig action role at call time, so a cache
subtree rebuild, revision change, or custom gameframe does not leave a stored
component id behind. Two-state controls use `enable` and `disable`; ordinary
buttons use `activate`. Plugin frame/replacement hiding is transparent to this
delegation, while cache/CS2 hiding is authoritative: for example a special-
attack button with no compatible weapon makes `base_action_available` false.
This pair was added in API 2.3.

### Facets replace chrome scopes

A UI node has three independently replaceable facets:

| Facet | Contains |
| --- | --- |
| bounds | Rectangle and parent/anchor placement. |
| appearance | Art for each state, label placement, and visible state. |
| actions | Hit region and named actions. |

These correspond to the useful part of the current POSITION, APPEARANCE, and
HITBOX scopes, but are expressed as retained contributions rather than claims.

A plugin declares contributions in its definition or startup state. The host
then calls it only for facets that resolved to it. There is no grant mask that
every caller must remember to check and no `owner()` query used for control
flow.

The contribution modes are:

- `MODIFY`: replace selected facets of an existing node; inactive while the
  node is absent.
- `PROVIDE_IF_MISSING`: supply a node only when the active frame/lane has none.
- `REPLACE_OR_PROVIDE`: replace it when present and provide it when absent;
  this is the migration path for minimap orbs and XP drops.

Independent facets compose. If two enabled plugins request the same exclusive
facet, the base facet remains active and both contributions receive a conflict
status naming the other plugin. This is deliberately fail-safe: it produces
neither duplicate pixels nor two answers to one click, and it does not invent
a winner from registration order. The plugin settings page surfaces the
conflict so a person can resolve it. If there is no base facet, that facet stays
absent until the conflict is resolved.

Provider diagnostics are observational; normal plugin behavior never depends
on which implementation currently supplies a facet.

Dynamic retained state is a restatement, not an imperative draw side channel:

```c
api->ui.update(api, node, TORIRS_UI_FACET_APPEARANCE, &appearance);
```

The call may change only facets declared by that plugin's static contribution.
The host validates and copies the complete replacement first, rejects foreign
or unready resource handles, and journals only a real change. Identical
restatements are silent. Resource references are zero-invalid and 1-based, so
a zero-initialized descriptor never aliases legacy image slot zero.

The presenter rebuilds its compact winning-provider list only when the named
UI registry revision changes. Ordinary frames walk only that active list. An
appearance winner gets its static state art/label and optional scoped
`on_ui_node_draw`; an actions winner installs the resolved, clipped hit region
and routes its named operation through `ui.invoke`/`on_ui_node_action`.
Conflicted facets create no presentation entry, so neither contender paints or
acts and the base remains authoritative.

### Tree relationships replace manual anchoring

A contributed node states its parent, placement relative to that parent, clip
behavior, and whether it paints before or after the parent's own appearance.
The host retains that relationship and reapplies it after rebuilds.

This replaces pairs such as `chrome_add(..., "minimap", AFTER, ...)` plus a
separate `role_anchor()` inside a draw event. The plate, changing fill, label,
and hit region all follow the same node and therefore cannot drift to opposite
sides of a housing.

The selected frame is the base provider for `frame.*` names while it is active.
It does not need to claim `minimap_edge` from the lane; its build declaration
states the active `frame.minimap.housing` node and its render position directly.

## Safe areas and placement

### Safe areas are not surfaces

V2 exposes placeable live surfaces only through `ToriRS_Surface`. Canvas is
build context, and safe results belong to the placement module. The former
`CANVAS`, `SAFE_GAMECHROME`, and `SAFE_LANECHROME` pseudo-slots are not public
API.

V2 defines these read-only area kinds:

| Area | Exact meaning | Main caller |
| --- | --- | --- |
| `PLATFORM_SAFE` | Canvas minus OS exclusions such as keyboard, notch, or system bars. | Full-screen UI and diagnostics. |
| `FRAME_BUILD` | `PLATFORM_SAFE` minus visible lane-owned UI the selected frame may not replace. | The frame provider; passed into its build callback. |
| `OVERLAY_SAFE` | Viewport (or canvas if no viewport) intersected with `PLATFORM_SAFE`, minus all visible resolved UI occluders and plugin reservations. | HUD text, counters, and floating overlays. |
| `RAW_VIEWPORT` | The resolved viewport rectangle with no safety subtraction. | World-aligned drawing that intentionally sits beneath chrome. |

`OVERLAY_SAFE` is the common composed answer the original note was looking
for. It includes platform, lane, and active-frame exclusions. A plugin should
not normally query and intersect those sources itself.

### Preserve fragmented geometry internally

Subtracting a centered or corner rectangle from another rectangle usually
produces more than one rectangle. The placement service therefore keeps a
small region set internally rather than immediately throwing every fragment
away except the largest.

The ordinary API is purpose-oriented:

```c
struct ToriRS_Rect label;

if( api->placement.place(
        api,
        TORIRS_AREA_OVERLAY_SAFE,
        TORIRS_ANCHOR_TOP_RIGHT,
        120,
        24,
        8,
        &label) ) {
    draw->text(draw, label.x, label.y, "Example", 0xffffff);
}
```

The host chooses a fragment that fits the requested size, anchor, and margin.
Advanced callers can iterate the area's rectangles or ask whether a rectangle
is contained. No caller has to reproduce the subtraction algorithm.

If no fragment fits, `place()` returns false. It never silently returns canvas
space known to be covered.

### Occluders are metadata on named nodes

Visible named UI nodes may carry either or both flags:

- `blocks_frame`: a selected frame must arrange around it;
- `blocks_overlay`: ordinary overlay placement must avoid it.

RevConfig sets these flags on lane-owned nodes such as the OldSchool popout
rail. Frame builders set `blocks_overlay` on housings, chat, sidebar furniture,
and other controls they place over the viewport. Hidden nodes do not occlude.

This replaces the numbered `lane_chrome_0` scan and the hard-coded
`SAFE_GAMECHROME` occluder array. Adding a dock then changes the derived result
by declaring what the dock is, not by editing a second list elsewhere.

### Reservations are named and deterministic

A persistent dock can reserve an edge of an area:

```c
api->placement.reserve(
    api, "tracker-panel", TORIRS_AREA_OVERLAY_SAFE,
    TORIRS_EDGE_RIGHT, panel_width);
```

The name is local to the plugin. Restating the same reservation updates it in
place; zero releases it; plugin teardown releases it automatically. The host
orders simultaneous reservations by a stable key `(edge, plugin id,
reservation name)`, not by the order callbacks happened to run.

`api->placement.reservation_rect()` returns the assigned box for a dock that
needs to draw into the space it consumed. Changes are published in one
coalesced `on_placement_changed` callback with a revision number.

### Source inputs remain inspectable

Specialized code may query `PLATFORM_SAFE`, `FRAME_BUILD`, or the resolved UI
nodes directly. Platform adapters feed OS exclusions into `PLATFORM_SAFE`
internally; plugins do not call a separate platform-safe function.

## A smaller, consistent plugin API

### Definition and instance state

V2 plugin definitions contain callback tables rather than manual subscription.
The host allocates zeroed per-instance state using `state_size` and passes it
to every callback. Bundled plugins store neither `g_api` nor mutable plugin
state in file globals.

Illustrative shape:

```c
struct ToriRS_PluginDefV2 {
    uint32_t struct_size;
    char const* id;
    char const* title;
    char const* version;
    size_t state_size;
    struct ToriRS_ConfigSchema const* config;
    struct ToriRS_FrameOffer const* frames;
    struct ToriRS_UiContribution const* ui_contributions;
    uint32_t flags;
    int event_priority;
    int draw_order;
    struct ToriRS_PluginCallbacks callbacks; /* always last */
};
```

Plugins use named callbacks such as `on_start`, `on_server_tick`,
`on_draw_world`, `on_draw_canvas`, `on_ui_build`, and
`on_placement_changed`. Draw/frame builders are callback parameters, so a
plugin cannot retain or use them on the wrong drawing surface.

### Namespaced modules

`ToriRS_ApiV2` contains the opaque plugin context and small, embedded module
tables, giving C calls the same hierarchy Lua uses:

| Module | Responsibilities |
| --- | --- |
| `core` | Log, player notification, screen state, clocks, capability checks. |
| `config` | Typed get/set of the plugin's declared settings. |
| `world` | Player/NPC/item/scenery snapshots and iteration. |
| `input` | Keys, pointer, hovered tile/entity, text input. |
| `ui` | Semantic UI references, snapshots, actions, and contribution status. |
| `placement` | Safe areas, anchored placement, containment, and reservations. |
| `frame` | Read-only catalogue/selection status and invalidation for the active provider. |
| `draw` | World/canvas drawing through callback-scoped builders. |
| `assets` | Bytes, images, models, screenshots, and async state. |
| `scene` | Meshes and plugin-owned 3D scene instances. |
| `panel` | The plugin's application-chrome page and controls. |
| `cache` | Explicitly lane-specific ids, raw components, varps, and native component operations. |

C and Lua are first-class native V2 surfaces with the same modules, callbacks,
builders, value semantics, and ownership rules. For example,
`api->placement.place(...)` in C is `api.placement.place(...)` in Lua. Lua is
not a compatibility wrapper around the removed flat API.

`core.capability` is host-backed, not a plugin-side platform guess. The stable
names are `touch` (current touch UI/input policy), `web` (Emscripten lane), and
`browser` (embedded BROWSER transport supported by this build); unknown names
return false.

Asset requests report the host's retained state. `PENDING` is the only state
that asks a plugin to wait for `on_asset`; `READY` is usable now, while
`MISSING`, `INVALID`, `BUDGET`, and `ERROR` are distinct terminal answers.
Image/model references are zero-invalid and are returned only for `PENDING` or
`READY`, so zero-initialized plugin state never aliases resource slot zero.

Every top-level and module struct carries `struct_size`; the API has a major
and minor version. Because modules are embedded, every module has a fixed
reserved function-slot tail and the total `ToriRS_ApiV2` size is frozen for
major version 2. A minor release consumes a reserved slot in place; it never
grows a module and shifts the modules after it. The independently sized
callback table is the final definition field, so extending it shifts no other
definition field. Strided descriptor arrays (`FrameOffer`, `UiContribution`,
`UiNode`, and `SelectOption`) likewise have frozen sizes with reserved tails.
An incompatible semantic or exhausted-layout change increments the major
version.

### Return and error rules

- A missing runtime thing returns false/absent: no NPC, no node on this frame,
  an asset still pending, or no safe fragment large enough.
- A malformed C call asserts according to repository convention: null required
  pointer, invalid builder lifetime, or an impossible enum supplied by bundled
  code.
- Data from config, Lua, cache, or a server is validated and reported rather
  than asserted.
- Budget failures return a typed status and the host logs the full reason once.
- APIs with more than two meaningful failure states use an enum result, not
  overloaded values such as `-1`, `0`, and a bit mask.

### Structured UI options

The panel API needs options with separate values and labels:

```c
struct ToriRS_SelectOption {
    uint32_t struct_size;
    char const* value;   /* stable value saved or returned */
    char const* label;   /* human text */
    bool enabled;
    char const* detail;  /* optional unavailable/fallback reason */
};
```

This is required for the dynamic frame catalogue and also fixes the current
pattern where enum labels are saved and later parsed back as values. The
Gameframe dropdown contains `auto` plus the non-core catalogue offers;
`core/native` is the internal resolution of `auto`, not a duplicate user
choice. Unavailable saved choices remain visible with their reason.

### Plugin panels are native retained chrome

`on_ui_build` declares application-chrome content as semantic panel nodes. Use
the ordinary builders (`heading`, `paragraph`, `label`, `key_value`, `toggle`,
`select`, `button`, and `action_row`) or `panel->node` for a less common
semantic kind. The web and embedded-browser executors then create real retained
controls and own their typography, layout, scaling, clipping, focus, and hit
regions.

The semantic kind survives that entire path. A heading becomes a native `h2`,
a paragraph a `p`, a key/value row a `dl`/`dt`/`dd`, progress a native
`progress` control (with an accessible legacy fallback), errors an alert, and
actions real `button` elements. The executor does not flatten all readouts to
one styled span and it does not ask plugins to recreate OSRS controls.

Navigation has the same single meaning everywhere. The top wrench/Manage page
owns plugin configuration; choosing a plugin there opens its generated settings
face. A plugin's own rail icon always opens `TORIRS_PANEL_VIEW_PAGE`, never its
settings. While a settings face is visible the shell's selected rail destination
is Manage, even though the host still knows which plugin is being configured.

This panel model is deliberately not the game's `UITree`. `UITree` owns the
cache gameframe, mounted interfaces, minimap, and in-game hit testing. A plugin
panel has a smaller semantic tree whose only external presentations are web
executors:

```text
plugin on_ui_build
  -> retained semantic panel model
  -> exact add/remove/property mutation journal
  -> web/browser command queue
  -> HTML/JS executor
  -> native DOM elements

native DOM event
  -> typed intent queue
  -> generation + widget-identity validation
  -> plugin on_ui_action
```

There is no steady-state tree diff. The setter that changes a property records
that exact mutation, repeated writes coalesce on the retained widget, and the
executor updates only that DOM node. A complete panel snapshot is reserved for
initial binding, recovery, or a real structural page replacement.

Lists of changing stats or drill-down destinations use
`TORIRS_PANEL_ACTION_ROW`. It is a full-width native navigation row with a
stable `id`, primary `label`, and optional concise `text` summary. It has no
checkbox; activating any part of it emits `TORIRS_PANEL_ACTION_ACTIVATE`.
`panel.set_text(id, text)` patches the summary in place without rebuilding the
page or replacing the DOM node. C exposes
`panel->action_row(panel, id, label, summary)` and Lua exposes the equivalent
`panel.action_row(id, label, summary)` builder.

Panel text, status blocks, tracker rows, tables, lists, and settings must not be
rendered into a `CUSTOM` bitmap. Doing so substitutes image pixels for native
content, bypassing the executor's font and interaction semantics and making
device-pixel-ratio and interface scaling part of plugin code.
`on_draw_world` and `on_draw_canvas` remain the correct callbacks for overlays
that belong over the game. The in-canvas panel presenter is a host-owned
fallback for the same semantic model, not a second panel-authoring API.

## Public frame API example

The following is intentionally shorter than either current frame plugin. It
shows the expected shape, not final geometry:

```c
static enum ToriRS_FrameBuildResult
stone_drawer_build(
    struct ToriRS_ApiV2* api,
    void* plugin_state,
    struct ToriRS_FrameBuilder* frame,
    struct ToriRS_FrameBuildContext const* in)
{
    struct StoneDrawer* state = plugin_state;
    struct ToriRS_Rect usable;

    if( !stone_drawer_art_ready(api, state) )
    {
        frame->reason(frame, "Waiting for Stone Drawer artwork");
        return TORIRS_FRAME_PENDING;
    }
    if( !api->placement.primary(api, in->available, &usable) )
    {
        frame->reason(frame, "No usable screen area for the Stone Drawer");
        return TORIRS_FRAME_UNSUPPORTED;
    }

    frame->surface(frame, TORIRS_SURFACE_VIEWPORT, usable);
    frame->surface(frame, TORIRS_SURFACE_MINIMAP, map_rect(usable));
    if( state->drawer_open )
        frame->surface(frame, TORIRS_SURFACE_SIDEBAR, drawer_rect(usable));

    frame->ui_node(frame, "frame.minimap.housing", &state->map_housing);
    frame->ui_node(frame, "frame.sidebar.rail", &state->rail);
    return TORIRS_FRAME_READY;
}

static void
stone_drawer_toggle(struct ToriRS_ApiV2* api, void* plugin_state)
{
    struct StoneDrawer* state = plugin_state;
    state->drawer_open = !state->drawer_open;
    api->frame.invalidate(api);
}
```

There is no claim, owner check, screen gate, manual release, re-claim, or stale
global rectangle. The host invokes `build` for the requested candidate at a
safe layout fence, keeps the last committed frame visible until validation
succeeds, and owns cleanup/fallback.

### Lua plugins use the same contract

Lua plugins remain supported. Each script returns one V2 definition table;
its `id` must match the manifest id. Lua uses the same static frame offers,
named UI contributions, module names, callback lifetimes, and typed outcomes
as C. For example, a Lua frame provider can be this small:

```lua
return {
    id = "simple-frame",
    title = "Simple Frame",
    version = "1.0.0",
    frames = {
        {
            id = "window",
            title = "Simple Window",
            canvas = "window",
            min_width = 640,
            min_height = 480,
            build = function(api, frame, context)
                local usable = api.placement.primary(context.available)
                if not usable then
                    frame.reason("No usable frame area")
                    return "unsupported"
                end
                frame.surface("viewport", usable)
                return "ready"
            end,
        },
    },
}
```

The Lua runtime translates that table directly into a native
`ToriRS_PluginDefV2`; it does not reconstruct the removed flat API. Builders
are valid only during their callback, and invalid Lua values produce a script
error instead of reaching native assertions. The complete LuaLS surface is in
[`script/plugins/plugin_api.meta.lua`](script/plugins/plugin_api.meta.lua).

## Retained chrome execution

The retained model must not discover changes by walking every panel, widget,
and property after one value changes. A global `dirty` flag followed by a
smaller or faster full scan is still the wrong cost model.

There is no tree-diff step in steady state. The code that performs a mutation
already knows the exact object and property it touched, so that setter records
the corresponding event immediately. The only comparison is the setter's
constant-time old-value/new-value check; it is not an executor traversal.

Every model mutation goes through a compare-then-set operation. When the value
really changes, that operation records the affected handle and property bit in
a bounded mutation journal. Repeated writes to the same property before the
executor runs coalesce to one entry; the executor receives the final retained
value, not every intermediate edit.

Coalescing is itself constant-time. Each live panel and widget stores the
index of its pending journal entry (fenced by the widget incarnation), so a
setter never searches the existing queue. This matters during a page rebuild:
touching 1,000 controls and then refining them must remain O(changes), not turn
into an O(changes squared) backward scan of the journal.

```text
model setter
  compare equal -> return, record nothing
  changed       -> store value, enqueue (node, property)
                                  |
                                  v
web executor tick          drain queued changes only
  no queued changes -> O(1), no transaction and no model scan
  queued changes    -> BEGIN, commands for those changes, END
```

That rule applies across the whole path, not just the C serializer. The plugin
panel adapter consumes exact changed-row records instead of restating every
row, and the browser runtime coalesces dirty widget handles while applying a
transaction. A one-widget delta therefore visits one DOM widget. Updating a
dropdown selection changes its selected index; it does not rebuild the option
elements.

Structural mutations record add/remove/reorder commands in the same journal.
Removal is ordered before reuse of the same handle. A page replacement records
one boundary followed by the new page's initial commands.

A complete snapshot is generated only when:

- a web executor binds for the first time;
- it reconnects/rebinds after losing its retained copy;
- the mutation journal overflows or the executor explicitly reports lost
  state.

Overflow never drops an arbitrary suffix of changes. It sets a `needs_snapshot`
flag, clears the unusable incremental journal, and sends one complete snapshot
on the next drain.

The only external plugin-chrome executors are:

- `web`: the Emscripten/DOM executor;
- `browser`: the embedded web-engine executor used by native shells.

The in-canvas buffer path remains an internal presenter/fallback, not a
selectable external executor. SDL, GDI, Android-native, and generic `platform`
executor choices are removed from parsing, factories, normal source lists,
profiles, and manifests. Keeping dormant selectable names would make the
supported set ambiguous and allow an untested path to return.

Executor tests must prove:

- 100 idle ticks inspect zero panels/widgets and emit no BEGIN/END pair;
- compare-equal setters enqueue nothing;
- several writes to one property emit one command with the final value;
- reused option/title buffers whose text changed still enqueue the relevant
  list property, even when their pointer and item count are unchanged;
- changing one widget does not inspect unrelated widgets;
- one browser delta visits only its changed DOM widgets, and a selection-only
  delta preserves existing option elements;
- add/remove/reuse ordering is deterministic;
- bind/rebind/overflow emits exactly one complete snapshot;
- normal builds contain no SDL/GDI/Android executor symbol or availability
  define.

## Completed implementation record

The project used the phases below to keep the client usable throughout the
change. Every phase and exit condition listed here is complete. Imperative
wording records what was done; references to V1 describe temporary scaffolding
that has since been deleted.

### Phase 0 — Pin the current behavior (complete)

Goal: make the migration distinguish intended visual behavior from accidental
ownership behavior.

Work:

1. Record the legacy first-caller behavior, then replace that fixture with the
   resolver's permanent registration-order-independence assertion.
2. Add golden expectations for Classic Fixed, Modern Fixed, Modern Resizable,
   and Stone Drawer geometry before changing their callbacks.
3. Add tests for title-to-game-to-title transitions, root rebuilds, drawer/chat
   invalidation, and asset-pending startup.
4. Add safe-area fixtures covering a fixed frame, floating resizable chrome,
   the OldSchool popout rail, keyboard bottom inset, and a corner notch.

Files:

- `src/plugin/test/torirs_plugin_host_test.c`
- `src/plugin/test/gameframe_test.c`
- `src/plugin/test/mobile_gameframe_test.c`
- `src/plugin/test/xp_orbs_test.c`
- `src/ui/test/uitree_test_frame.c`

Exit condition: intended current geometry and lifecycle behavior are covered,
and the ownership race is isolated in a test that phase 2 can replace with the
new invariant.

### Phase 1 — Introduce the V2 shell (complete)

Goal: make the public contract understandable without changing frame behavior.

Work:

1. Add `src/plugin/torirs_plugin_v2.h` with `ToriRS_ApiV2`, module structs,
   callback tables, typed rectangles/references/results, `struct_size`, and
   major/minor versioning.
2. Add per-plugin zeroed state allocation to the host.
3. Add callback-table dispatch beside the v1 subscription dispatch.
4. Implement v2 modules initially as thin adapters over existing host
   functions. Keep the adapter private; do not duplicate engine state.
5. Migrate one small bundled plugin first to prove lifecycle, state, C naming,
   and cleanup.
6. Add a compile-only example plugin and API documentation test so examples in
   this document cannot silently stop compiling.

Files:

- new `src/plugin/torirs_plugin_v2.h`
- `src/plugin/torirs_plugin_host.[ch]`
- `src/plugin/torirs_plugin_registry.c`
- one small plugin, such as `src/plugin/plugins/item_stats.c`
- `src/makefile`

Intermediate exit condition at the time: V1 and V2 plugins could run together,
the first migrated plugin had no global `g_api`, and behavior stayed covered.
That coexistence scaffold was removed in Phase 6.

### Phase 2 — Build the frame catalogue and resolver (complete)

Goal: remove first-caller ownership and provide one Gameframe setting.

Work:

1. Add `src/plugin/torirs_plugin_frame.[ch]` containing the offer catalogue,
   selection record, resolver, candidate builder, validation, and diagnostics.
2. Register `core/native` unconditionally. Its activation releases effective
   plugin geometry and follows the existing lane/server toplevel.
3. Extend `RS_Prefs` with `preferred_frame`, defaulting to `auto`; decode and
   encode it as a stable string in `[preferences]`.
4. Implement the one-time legacy preference migration described above.
5. Add a dynamic Gameframe selector to `client_settings.c` using structured
   option values. Show requested, active, loading, and fallback states.
6. Convert the three offers in `gameframe.c` and the Stone Drawer offer in
   `mobile_gameframe.c` to descriptors and builder callbacks.
7. Replace every re-claim used as invalidation with `frame.invalidate()`.
8. Make provider definitions non-toggleable in the ordinary plugin roster.
9. Route canvas policy, frame drawing, and lane-chrome suppression from the
   selection record.
10. Kept `layout_claim`, `layout_release`, and `PluginHost_LayoutOwner` out of
    the public path, then deleted the internal `layout_owned` adapter storage
    after both bundled providers used direct V2 builders.

Files:

- new `src/plugin/torirs_plugin_frame.[ch]`
- `src/plugin/torirs_plugin_v2.h`
- `src/plugin/torirs_plugin_host.[ch]`
- `src/plugin/torirs_plugin_bridge.u.c`
- `src/app.[ch]`
- `src/game/rs_prefs.[ch]`
- `src/plugin/plugins/client_settings.c`
- `src/plugin/plugins/gameframe.c`
- `src/plugin/plugins/mobile_gameframe.c`
- `src/plugin/torirs_plugin_registry.c`

Exit condition: both providers can be installed and catalogued simultaneously;
changing registration/start order has no effect; only the selected callback
runs; invalid or pending builds never blank the frame; `auto` follows native.

### Phase 3 — Introduce the named UI node registry (complete)

Goal: make one name work for lookup, replacement, anchoring, drawing, and
input.

Work:

1. Add `src/plugin/torirs_plugin_ui.[ch]` with name interning, `UiNodeRef`, live
   resolution, node metadata, contribution declarations, facet resolution, and
   conflict status.
2. Seed the `frame.*` vocabulary in one table and reject malformed or duplicate
   declarations at registration/build time.
3. Feed the presenter from the registry's exact changed-node journal. Keep a
   direct `UiNodeRef` index; rebuild hierarchy/order only for structural
   changes, and make an unchanged frame an O(1) return except for explicitly
   unresolved native-role probes.
4. Extend RevConfig role entries with canonical names, named actions, and
   occlusion flags. Retain current match expressions as the binding mechanism.
5. Make frame builders publish canonical UI nodes as part of their atomic
   declaration.
6. Add temporary aliases from current role/chrome names.
7. Migrate `gameframe.c` and `mobile_gameframe.c` housing/buttons first, then
   `minimap_orbs.c` and `xp_orbs.c` to retained contributions.
8. Move raw component ids and numeric operations under the v2 `cache` module.
9. Add diagnostics that show a node's active provider for each facet and any
   unresolved conflict.
10. Delete migrated `chrome_*`, `role_replace`, and `role_anchor` call paths only
   after all bundled users have moved.

Files:

- new `src/plugin/torirs_plugin_ui.[ch]`
- `src/plugin/torirs_plugin_v2.h`
- `src/plugin/torirs_plugin_host.[ch]`
- `src/plugin/torirs_plugin_bridge.u.c`
- `src/ui/uitree_role.[ch]`
- `src/ui/uitree_frame.c`
- affected `revconfig/*/*_ui.ini` and `*_cache.ini`
- `src/plugin/plugins/{gameframe,mobile_gameframe,minimap_orbs,xp_orbs}.c`

Exit condition: a cached `UiNodeRef` follows a frame switch and CS2 rebuild;
the orb plate/dynamic drawing/input share one node relationship; two conflicting
facet contributions show a stable conflict and never double-draw or double-act.

### Phase 4 — Replace pseudo-slots with placement areas (complete)

Goal: give frame builders and overlays one correct answer for usable space.

Work:

1. Add `src/plugin/torirs_plugin_placement.[ch]` with region-set subtraction,
   intersection, containment, anchored placement, and named reservations.
2. Feed it platform exclusions from the engine seam. Adapt today's rectangular
   `safe_os` result first, then permit more than one exclusion.
3. Feed it lane/frame occluders from resolved named-node metadata.
4. Pass `FRAME_BUILD` directly in `FrameBuildContext`.
5. Migrate overlay callers to `placement.place(OVERLAY_SAFE, ...)` or explicit
   area iteration.
6. Make reservation ordering stable and expose assigned reservation rectangles.
7. Coalesce changes and increment placement revision only when the resolved
   area set actually changes.
8. Remove `SAFE_GAMECHROME` and `SAFE_LANECHROME` from the public slot enum;
   remove the numbered `lane_chrome_<n>` convention after profiles migrate.

Files:

- new `src/plugin/torirs_plugin_placement.[ch]`
- `src/plugin/torirs_plugin_v2.h`
- `src/plugin/torirs_plugin_host.[ch]`
- `src/plugin/torirs_plugin_bridge.u.c`
- platform/app safe-inset producers
- RevConfig profiles with lane chrome
- plugins using `slot_rect`, `safe_os`, or `layout_reserve`

Exit condition: the combined keyboard + lane rail + floating chrome case is
correct without plugin-side intersection; a fragmented area can place at more
than one anchor; hidden occluders consume no space; reservation order does not
depend on callback order.

### Phase 5 — Make retained execution event-driven and web-only (complete)

Goal: make executor work proportional to actual mutations and remove dormant
non-web external executor paths.

Work:

1. Put a bounded, deduplicated `(handle, property)` journal on the retained
   chrome model, with per-handle pending-entry indices for O(1) coalescing.
2. Have every compare-then-set mutator append/coalesce its own change; record
   structural add/remove/reorder operations at the mutation site.
3. Give the plugin-panel adapter its own exact changed-row journal, then change
   `ToriRSChromeSync_Run` to drain exact records without walking panel or
   widget capacity. Preserve transaction ordering and copied string payloads.
4. Keep the executor's delivered-value shadow only for net-zero coalescing,
   recycled-identity fences, acknowledgement, and recovery; it never discovers
   changes by scanning the model.
5. On bind, page replacement, queue overflow, or reported state loss, schedule
   one full retained snapshot and then return to incremental drain.
6. Keep only `web` and `browser` as external kinds. Remove SDL/GDI/Android and
   generic `platform` selection from parsers, factories, builds, profiles, and
   manifests. Keep the in-canvas buffer as an internal fallback.
7. In the browser reducer, coalesce the widget handles named by one transaction
   and render those widgets only. Keep option replacement separate from
   selection so a selected-index update never rebuilds the list.
8. Add instrumentation/tests proving idle O(1), one-widget mutation isolation,
   coalescing, removal-before-handle-reuse, and one-shot recovery snapshots.

Files:

- `src/ui/uitree_debug_overlay.[ch]`
- `src/ui/torirs_chrome_exec.[ch]`
- `src/ui/torirs_chrome_exec_kind.[ch]`
- `src/ui/torirs_chrome_exec_{web,winbrowser}.c`
- `src/ui/test/uitree_test_chrome_exec.c`
- `src/plugin/torirs_plugin_panel.u.c`
- `src/plugin_chrome/runtime-source.js` and its generated runtimes
- `src/web/torirs_chrome.js`
- `src/platform/platform*.mk`, `src/makefile`, manifests, and profiles

Exit condition: an idle executor tick does no model scan or transaction; work
after a mutation is bounded by queued changes; clean builds expose only the two
web executors and the internal canvas fallback.

### Phase 6 — Finish the API migration (complete)

Goal achieved: one public API and one vocabulary remain.

Work:

1. Migrated every bundled C plugin to namespaced V2 modules and callbacks.
2. Moved plugin-owned 3D objects to `scene.instance_*`; ground items remain
   world snapshots.
3. Made Lua expose the same native V2 modules, callbacks, builders, and values
   as C.
4. Kept `script/plugins/plugin_api.meta.lua` aligned with the runtime API
   inventory.
5. Migrated bundled Lua scripts and removed compatibility aliases.
6. Reduced `PLUGIN_CHROME.md` to the current operational model.
7. Deleted the V1 declarations, flat API, subscription adapter, pseudo-slots,
   dead host fields, and old public terminology.
8. Kept the versioned, append-only V2 ABI rules described above.

Files:

- bundled `src/plugin/plugins/*.c`
- `src/plugin/torirs_plugin_lua.c`
- `script/plugins/*.lua`
- `script/plugins/plugin_api.meta.lua`
- the removed V1 header surface and temporary adapter
- `PLUGIN_CHROME.md`

Exit condition met: C and Lua expose the same module tree; bundled plugins do
not call `layout_claim`, `chrome_claim`, `role_anchor`, `safe_os`, or a derived
safe slot, and bundled profiles/plugins use canonical names.

## Test and acceptance checklist

The implementation is verified by the existing focused suites:

```sh
make -C src test-plugin-host
make -C src test-gameframe
make -C src test-mobile-gameframe
make -C src test-xp-orbs
make -C src test-nxt-plugins
make -C src test-feature-flags
make -C src test-uitree
```

Add focused targets for the new modules rather than expanding the already large
host test indefinitely:

```text
test-plugin-frame
test-plugin-ui
test-plugin-placement
test-plugin-api-v2
```

The completed implementation maintains these invariants:

- Registering any number of frame providers cannot change the active frame;
  only the saved Gameframe preference can.
- `Auto` visibly follows server/native fixed, resizable, and mobile toplevel
  changes without asking a plugin to infer `frame_root`.
- A missing, loading, unsupported, or faulted requested offer always leaves a
  usable frame and reports one understandable reason.
- Resizing never changes the selected offer solely because its minimum was
  crossed.
- A frame declaration is committed whole or not at all.
- A semantic UI reference survives a frame switch and tree rebuild.
- Two plugins cannot silently win the same UI facet by running first.
- One hit region has at most one action provider.
- Ordinary overlays use `OVERLAY_SAFE` without hand-written intersections.
- Keyboard, notch, lane rail, frame chrome, and reservations all compose in
  the placement tests.
- Plugin stop/reload releases contributions, reservations, images, scene
  instances, and provider state automatically.
- The Client Settings page stores stable option ids, never display labels or
  dropdown indices.
- Debug output can state: requested frame, active frame, fallback reason, a
  named node's provider per facet, and the occluders that produced a placement
  result.

## Removed V1 to V2 reference

| Current API/concept | V2 replacement |
| --- | --- |
| Legacy/internal `layout_claim/release/owned` | Static `FrameOffer` descriptors plus the host resolver. |
| Legacy re-claim to force layout | `frame.invalidate()`. |
| `EV_LAYOUT` | Selected offer's `build` callback with a scratch `FrameBuilder`. |
| `EV_DRAW_FRAME` | Selected offer's callback-scoped frame draw builder. |
| Public `frame_root` inference | `auto -> core/native`; root id remains internal to the native binder. |
| `layout_slot*` | `FrameBuilder.surface*`, `skin`, and `ui_node`. |
| `slot_rect` / `slot_member_rect` | `ui.ref` plus `ui.info`; surfaces use canonical `frame.*` names. |
| `role_rect/visible/click` | `ui.info` and `ui.invoke`. |
| `role_id` / raw `if_click` | Advanced, explicitly lane-specific `cache` module only. |
| `chrome_claim/add/paint/ops/state` | Retained `UiContribution` and resolved UI-node facets. |
| `chrome_owner/claimed` | Host conflict status; optional diagnostics only. |
| `role_replace/role_anchor` | UI-node provider and parent/paint relationship. |
| `SAFE_GAMECHROME`, `SAFE_LANECHROME`, `safe_os` | `placement` areas, normally `OVERLAY_SAFE` or callback-provided `FRAME_BUILD`. |
| `layout_reserve` | Named `placement.reserve` plus `reservation_rect`. |
| Flat `object_*` world model calls | `scene.instance_*`. |

## Historical first vertical slice (complete)

The migration deliberately began with this small vertical slice rather than a
bulk rename:

1. Add `preferred_frame` and the catalogue/resolver.
2. Register `core/native` and one `gameframe-layout/classic-fixed` offer.
3. Drive the existing layout implementation through a scratch builder adapter.
4. Put `Auto` and `Classic Fixed` in Client Settings using stable option values.
5. Prove selection is unchanged when Mobile Gameframe is registered before or
   after Gameframe Layout.
6. Convert one drawer/chat state change from re-claiming to
   `frame.invalidate()`.

That slice removed the dangerous ownership race and validated the saved-choice
model before the broader named-node and placement migrations were completed.
