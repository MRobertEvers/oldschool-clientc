# Plugin contract, major 3: live widgets and native events

This replaces the unshipped property-claim proposal following the user's explicit
choice of the RuneLite model. There are no public claims, bundles, declaration
transactions or conflict-suspension rules. The existing production host now exposes the major-3 aggregate in
`src/plugin/torirs_plugin_api.h`. Its live-widget geometry/read/reset methods
are wired through `torirs_plugin_contract.h` for C and Lua. Owned text, presentation hiding, semantic binding subscriptions and tree-publication subscriptions are also live. Interactive controls,
native style/content setters, other widget events and actions remain to implement; the
former frame/UI builders remain only for the product ports still in progress.

## Execution and lifecycle

Plugins start, subscribe to events, operate on live widgets, and shut down.
All native widget/script operations execute on the client thread. The host
validates the plugin owner and execution context at API entry. An ordinary
event may mutate widgets and invoke native scripts when the VM is idle. A
callback inside script execution may read/write supported widget state and
schedule later work, but may not recursively enter the VM. Paint may read and
draw or schedule later work; it may not mutate widgets or execute scripts.
Shutdown may reset widgets and release owned state, but cannot enqueue new work
or invoke scripts. Invalid contexts return `WRONG_CONTEXT` without side effects.
The host additionally checks actual VM occupancy, not just the callback label.

`revalidate` settles the addressed native widget's geometry; `revalidateScroll`
also settles its native scroll/layout scope. These operations must give native
computed getters coherent read-after-write behavior, including within one CS2
script. Native layout inputs remain distinct from computed and forced geometry.
Reading resolved zero returns zero. Paint and input share the settled result.

The host owns subscriptions, added widgets, resources and pending work. It
revokes callbacks/actions before freeing owner state. New registrations wait
until the next dispatch; removed/disabled participants are skipped even if they
were in its original snapshot. Deferred work carries its owner epoch and checked
widget references. Disable/reload cannot run old code against new plugin state.
Native scripts remain non-reentrant; later and end-of-tick queues have explicit
safe points and bounded processing. C plugins remain trusted in-process code.

## Widgets, native state and reset

A widget reference identifies one incarnation in one tree/instance. Reparenting
and movement preserve identity; deletion, replacement and slot reuse do not.
References never silently rebind. A fresh query or explicitly following helper
can discover the current widget. C plugins receive checked references, not raw
UITree pointers. Lua uses the same validation.

Shared role/frame helpers support portable plugins. Native numeric widget and
script lookup is available to declared revision-specific features. CS2 APIs
return `UNAVAILABLE` on a revision without that capability; the host does not
pretend that CS1 is CS2. Required unavailable features reject activation before
publishing UI; optional features have explicit reduced behavior.

Native identity, addressing and topology remain unchanged by presentation edits.
A plugin can add owned children and listeners, but cannot remove another owner's
or native children through the owned-widget remove API. Removing an owned parent
revokes its descendants and their retained input/listeners before freeing them.

On native widgets, setters affect the documented presentation/content field and
retain only the ownership/reset metadata necessary to reveal current native
state later. Native writers continue to update their own state. Widget reset and
plugin cleanup remove that owner's edits, exposing remaining active edits or
current native values. They never restore a startup snapshot. Setters do not
implicitly acquire unrelated fields, masks, operations, opacity or animation.
Owned plugin widgets use ordinary setters with the same geometry/input rules.

There is no conflict negotiation. Event handlers run by descending explicit
priority, then ascending stable plugin ID, then registration sequence. Direct
writes become visible in that order; the last applicable setter wins. Equal
priorities do not make plugin load order significant. The inspector reports the
last writer. Teardown removes only that owner's edits and registrations.
User-selected frames activate one frame provider; overlays remain independent.

Native hide, including RS2 server hiding, selected-panel state, mounts and native
restrictions always bound paint and input. Plugin hide adds a restriction;
clearing it releases only that restriction. Moving/skinning a widget cannot
reveal hidden content or grant a denied operation. Native hide scope remains
self/subtree/controller as defined by that revision.

Native scrolling, clipping, content extent, opacity and animation keep their
own scope. Forced geometry does not remove native masks or scroll restrictions.
Native self-opacity does not become subtree opacity. Re-skin helpers use current
native state/semantic colors and preserve live text, quantities and state
variants; a skin does not substitute an obsolete content snapshot.


The initial owned-control path provides `create_text`, `set_text`,
`set_text_color` and `remove` in C and Lua. Creation keys are scoped to owner and
parent; repeated creation returns the same live child. Owned text uses ordinary
typed geometry setters. Content/color setters currently accept only that owner's
text widgets; native styling remains a separate unfinished port.

Owned children have no native component ID or dynamic child index. Native child
lookup/iteration excludes them. Clearing a native slot preserves attached owned
siblings, like profile-owned controls; deleting the native parent invalidates the
whole owned subtree. Native hiding and clipping still apply. Native or foreign
widgets cannot be moved into an owned subtree, and another owner cannot create or
write inside it. `remove` is ownership-checked; owner teardown removes its added
widgets as well as its geometry edits. Creation during shutdown is rejected.

### Owned-control operations (implemented slice)

C `widgets.set_on_op(context, widget, label, listener, user)` and Lua
`widget:set_on_op(label, callback)` arm exactly one operation on an owned
widget. The label is shorter than `TORIRS_WIDGET_OP_LABEL_MAX` (64 bytes) and is
stored in the same native menu option storage native components use, so the
control becomes input-eligible through the ordinary hit test: it hovers, shows
the label as the left-click default and in the right-click menu, and is
dismissed by the same retained-menu checks. Native widgets never take a plugin
operation (`NATIVE_BLOCKED`); another owner's control is refused the same way.

Each arming issues a registration serial that is part of the node's native
action signature. Replacing the listener or removing it (NULL / nil) retires
every retained row and press built for the previous registration; a row whose
node was freed, reused, hidden natively, or natively unavailable never reaches
the listener. The listener receives `TORIRS_WIDGET_OPERATION` (`operation` 1,
`native_revision` = registration) in a mutating event context: it may edit
widgets, invoke checked native actions, re-arm or remove its own operation, and
disable or reload its plugin. Paint and shutdown cannot arm; shutdown removal is
an accepted no-op because teardown releases the registrations and the owned
widgets. Registrations are bounded at 128 armed controls per owner in C and
Lua alike; a full table reclaims only a registration whose widget no longer
exists and never evicts a live one. Lua releases the callback closure on
removal, on reclamation and on stop.

### Owned image controls (implemented slice)

C `widgets.create_image(context, parent, key, out)` and Lua
`widget:create_image(key)` create this owner's keyed graphic child, idempotent
like `create_text`. `set_image(widget, image, width, height)` installs one of
this plugin's live image tokens (validated in the plugin's own runtime, so the
adapter only ever sees a slot it published) together with the control's size;
a token the plugin never received, or has released, is `INVALID_ARGUMENT`.
Releasing an image later blanks every owned control still showing it, so a
recycled slot is never drawn. `set_opacity(widget, 255..0)` applies to owned
widgets only. Native graphics never take a plugin image (`NATIVE_BLOCKED`).
Combined with `set_on_op`, an image control is a complete button: the
Screenshot plugin's camera is an owned image control in the live viewport, or
over the report button's slot while that native button's presentation is hidden.

### Widget anchors (implemented slice)

C `widgets.set_anchor(context, widget, target, relation)` and Lua
`widget:set_anchor(target, "over"|"behind"|"replace"|"native")` retain a depth
relation from one widget to another: its paint and input are ordered directly
OVER the target, directly BEHIND it, or in its place. This is the element-
anchored depth the frame declarations already had between slots, now stated
between nodes and available to every plugin. The anchor is an ordinary retained
edit on the anchored widget (any widget this plugin may edit, owned or native):
the latest writer wins, `reset` drops it, and the owner's teardown releases it.
`"native"` clears this owner's relation and takes no target.

Ordering is one common pass shared by paint, hit testing, hover and retained
menu liveness (`UITree_FrameReorder`). A target's subtree is written where its
earliest record stood: BEHIND children, then the target itself or a presented
REPLACE child, then OVER children, each recursively. REPLACE inherits the
target's native veto both ways: a natively hidden target takes its replacement
down, a hidden replacement reveals the target, and a replaced target is not
presented (no retained menu rows, no contribution). A target that emits nothing
leaves its OVER/BEHIND children at their native positions; there is nothing to
be over or behind. Rejected at set time: self, an ancestor or descendant of the
widget, a cycle through the effective anchors (`INVALID_ARGUMENT`), a dead
target (`STALE_REFERENCE`), another owner's control (`NATIVE_BLOCKED`).

### Native re-skin (implemented slice)

`set_image(widget, image, 0, 0)` on a native sprite, graphic or compass retains
this plugin's image as that widget's art; `set_mask(widget, image)` on a native
minimap, compass or sprite retains the clip, where an empty image reference is
the explicit "no mask". Both are ordinary retained edits: latest writer wins,
`reset` drops this owner's, releasing the image drops every skin naming it. The
emit walk applies them last, after the lane's own state and any frame
declaration, and only while the native widget shows a graphic of its own, so a
skin never substitutes for a picture a script or the server took away. The
widget keeps its own geometry (a non-zero size is `INVALID_ARGUMENT`; use
`set_size`). Plugin masks always cut where they are transparent, whichever
polarity the era's cache masks use. Owned image controls keep taking their
picture through `set_image` with a size, and take no mask (`NATIVE_BLOCKED`).

### Provided gameframes (implemented slice)

A frame offer (`ToriRS_PluginDef.frames`) no longer needs a builder. An offer
without `build` is served by `on_gameframe(api, state, event)`: when the offer
is the selected frame the host raises the event with `active` true and the
logical canvas (the pinned size for a FIXED offer, the window for a WINDOW
offer), and the provider lays the frame out with the widget API -- `set_position`
and `set_size` on the role widgets, `set_hidden` for surfaces the frame does
not show, `set_image`/`set_mask` for re-skins, owned images and controls with
`set_anchor` for decoration behind, over or in place of the live surfaces. The
event runs in the layout context, so every widget setter is permitted. The
provider returns `READY`, `PENDING` (native stays up, status LOADING) or
`UNSUPPORTED` (fallback with the provider's `reason`); a non-READY answer from
an already provided frame releases it. The event is raised again on every
canvas change while the offer stands, and once more with `active` false when
the offer is released, before the provider's teardown.

The engine's half of a provided frame is `UITree_FrameProvide`: it collects and
suppresses the lane's own chrome by root group and binds the roles, but places
and hides no surface -- unlike a declaration, whose unplaced surfaces are
hidden -- and it leaves the surfaces' geometry unowned, so the provider's
retained edits are the layout. Every container above a native widget a plugin
moved or resized stops clipping, so the new box is seen wherever it was put;
that release reads the merged geometry override, so it follows ANY owner's
move under a provided frame, not only the provider's (narrowing it to the
provider is open). A provider that is released while it still runs -- the host
took the frame back, or the provider declined the root it now finds itself
over -- hears `active` false and must take its own furniture off the tree
(owned children removed, role edits `reset`), because nothing else does until
its teardown. Canvas policy (a FIXED offer pins the window,
a WINDOW offer states a minimum) is unchanged. Lua: `on_gameframe(api, ev)`
returns nothing or `"ready"`, or `"pending"`/`"unsupported"` with a reason.

The event also carries `safe` (`x, y, width, height`, canvas pixels): the canvas
less what the platform is covering -- the soft keyboard band on a phone -- and
the whole canvas when no band is up. It is the engine's `platform_safe_rect`,
which reads the same `UITree_LayoutSafeBottomEdge` a profile's
`safe_area=os:bottom` row reads, so a provider's bottom strip and the login box
cannot disagree about where the keyboard starts. The host polls that rect at the
frame boundary while a frame is provided and, when it moves, re-asks the
provider once through the same one-shot layout request a selection transition
uses (the app's keyboard-inset path marks the layout dirty as well; both
collapse into one `on_gameframe`). The lane's own popout strip
(`lane_chrome_0`) is deliberately NOT subtracted: it is a widget, and the frame
plugins find and subtract it themselves. Lua receives it as `ev.safe`, a
`torirs.Rect`.

## Events, listeners and actions

`widgets.watch(role, listener, user)` follows the current semantic binding at the
native pre-input/paint publication fence. It sends BOUND for a newly available
incarnation and UNBOUND for a binding that disappeared or changed, with old
UNBOUND preceding new BOUND. Native hiding alone is not an unbind. Callbacks are
snapshotted by subscription serial; replacements/new subscriptions wait for the
next dispatch, and removals/disabled owners are rechecked before each callback.
Nested enable/disable preserves the calling callback's execution context.
Lua closures use the same host subscriptions, scoped API access and budget.

This helper does not represent individual cache-widget load completion or a
synchronous CS2 decision hook. Those separate events remain to implement. On
OSRS, the `sidebar` widget helper resolves the validated common parent of the
numbered native tab mounts, not an arbitrary tab or the side-modal sibling.
Incompatible member topology returns unavailable. The older adapter retains its
revconfig region. Call `reset` on UNBOUND when the plugin no longer wants its
edits on an old-but-still-cached widget; plugin shutdown resets all owner edits.


The initial widget events are load, close, native state change, before/after
layout and user operation. CS2 additionally has script pre/post and named script
callbacks. The older adapter emits real native widget/layout/state events from
revconfig, mounted interfaces, completed CS1 evaluation, client-code updates and
packets. It does not fabricate script execution events for CS1.

Each event documents its exact native boundary, payload lifetime and whether it
can be consumed or return a result. No generic mutable stack or event-reducer
framework is needed. Callback arguments are borrowed for that call; saved widget
references remain subject to normal incarnation checks. A synchronous script
callback cannot yield. Its result is consumed before the native operation
continues; absence/failure uses the declared native default. Incompatible script
patches/profiles are rejected using pinned identity/hash and argument contracts.

Native action references include widget identity, operation and action revision.
Dispatch rechecks current native availability, masks, labels/listeners and item
identity, including retained menus and added controls. Calling an action returns
dispatch status, not a fabricated server acknowledgement. Native hooks remain
native; plugin listeners are separately owned and revoked on cleanup.

## Verification boundary

`make -C src test-plugin-contract` currently checks opaque widget identity and
execution-context policy. The old claim resolver/tests have been removed.
Existing UITree and native CS1/CS2 tests continue to validate their native fixes.
Host routing and actual C/Lua geometry probes now pass on rs289lc and OSRS239.
They do not prove the complete widget API or event lifecycle.
The initial context tests pass; `runelite-context-negative` in the local evidence
directory observes failures when script reentry is allowed or widget incarnation
comparison is removed. These focused controls do not validate host dispatch.
The revised implementation plan requires actual C/Lua plugin examples on both
real revisions before broad migration, then complete retained-interaction,
owner-cleanup, ordering, native-state/reset and frontend conformance.

## Implemented tree publications and presentation hiding

`widgets.find_all(role)` returns checked references to all current matches, in the role's own numbering. A frame role spread over members -- the sidebar's tabs, the chat filters, the orb block's children -- answers one slot per member: slot `m` is member `m`, a member the current frame does not have is an invalid reference (`ToriRS_WidgetRefValid` false; `false` in the Lua table) left in its slot, and `count` is one past the highest member present. The array is never compacted, so a caller seating tab 9 reads `refs[9]` whether or not tab 8 exists; a caller iterating skips the invalid slots. A role with no numbering produces its one bound widget. With `TORIRS_TRACE_PLUGIN_WORLD` set the engine reports each answer that changed as `PLUGIN_FIND_ALL owner=<plugin> role=<role> count=<n> missing=<comma list of members whose slot is invalid>`, and `tools/gameframe_pixels.py --find-all-holes ROLE:COUNT:MISSING` (harness knob `GF_MATRIX_FIND_ALL_HOLES`) pins that numbering in a capture. `find(role)` is unchanged and answers any one member. The `ground_item_labels` adapter returns the caption text widgets in OSRS239 coordinate-overlay slot 0; native buttons and absolute-width timer text remain separate. This mapping was checked against all six prepared `overlay_coord_create` producers: ground labels use slot 0, coordinate timers use 1, loc timers use 2–5, cannon HUD uses 6, clues use 7. The caption constructors in `torirs_gi_row` and the overflow row use width 108 with parent-relative sizing. That is an explicit adapter assumption for the pinned content, not a guarantee about arbitrary future caches. The rs289lc adapter reports this native-only role unavailable.

`widgets.watch_tree(callback)` publishes an initial notification and later structural tree changes at the existing pre-input/paint fence. It shares subscription ownership, priority/plugin-ID/registration ordering and dispatch snapshots with binding watches. New or restarted subscriptions wait for the next dispatch; unchanged subscriptions are not replayed merely because another subscriber was added. Lua receives `(nil, event)` with `event.kind == 'tree_changed'`; query current widgets inside the callback. Pure geometry and presentation-hide changes do not generate structural notifications. Passing nil unregisters. Semantic role names beginning with `@` are reserved internally.

`widget:set_hidden(boolean)` changes presentation and input eligibility without writing native `behavior.hide`, native server hiding, mounting or projection state. The last setter wins among plugin presentation edits; reset/disable exposes the remaining writer or current native state. A plugin show cannot reveal a natively hidden widget. Another plugin cannot hide an owned widget. Hiding invalidates paint/reachability readers, including mounted-ancestor queries, hit/hover, scrolling, drop targets and native input availability. Native copying does not copy these presentation edits.

Ground Items uses these methods for caption suppression when the approved native callback is unavailable. When the callback runs, it clears its suppression and edits the native caption synchronously, before native control layout. UI remount schedules ground-item overlay rebuilding in the existing native refresh driver. Native captions return after disable, including intervening native settings changes.

## Synchronous native script callbacks (implemented slice)

Opcode 6599 follows RuneLite's `runelite_callback`: it pops the callback name and completes synchronously before the next native instruction. A yielding handler is rejected. C and Lua receive `on_script_callback` with a checked `ScriptRef`; `scripts.counts/get_int/get_string/set_int/set_string` expose a declared window indexed from its top. References expire at dispatch end and cannot be reused in another callback. Native arguments are read-only unless their producer explicitly marks a result slot writable. No VM pointer, raw stack array or resizable stack is public. Native UI invocation is refused during this callback; `scripts.invalidate(name)` coalesces a native rebuild at its ordinary safe point and is permitted for shutdown cleanup.

The initial adapter contract is `groundItemCaption`, script 7232. It exposes eleven integer slots and one caption string. From the top: ignore (0), highlight (1), row offset (2), row count (3), row index (4), edit-mode (5), color (6), native value (7), quantity (8), object ID (9), coordinate (10). Only row offset (2), color (6) and caption (string index 0) are writable. The event also supplies the checked current caption widget. The patch applies the row offset before subsequent native auxiliary-control layout. The native producer validates the complete normalized script fingerprint, ID and stack shape. The preparation tool additionally verifies the exact source bytes and helper 7225's source/argument contract. Unknown or mismatched native integrations are refused; rs289lc reports script callbacks unavailable.

`tools/plugin_engine_script_hooks.py` inserts the callback before native text measurement, writes its outputs back to the native locals, and relocates branches/switches. Native CS2 then performs its own caption, control and timer layout. The cachepack overlay preserves all base data and unrelated index entries. `plugin-engine-cache-hooks` builds this derived cache; the checker's exit convention is 1=fresh, 0=stale, 2=error, matching the launcher. Source/specification mismatches are errors, not freshness exemptions.

Static event dispatch now snapshots participant lifetimes, so enabling/restarting a plugin inside a callback cannot join the in-progress dispatch. Native aux-list revisions invalidate ground-item views after Ignore/Highlight changes without depending on a one-tick timer window.

This is not completion of all script/layout APIs. Ground Items' native callback path still needs full multi-row filtering/order and native timer/control combination acceptance. Native script scheduling/pre/post events and the rest of the full plan remain open.

## Native caption layout settings

`widget:parent()` returns the current native physical parent as a checked reference (nil when absent); presentation positioning never reparents it. `set_projection_height(height)` accepts 0–32767 world-height units only on native anchored overlay root layers. It adds lift to the existing top/mid/foot projections, so ordinary camera updates retain the edit. Unsupported widgets return an explicit unsupported-layout result. Forced overlay size is read before native anchor placement.

`set_text_outline(boolean)` adds four black glyph-neighbor passes while retaining foreground markup colors. False resumes native shadow behavior. Last setter wins per property; owner reset restores the remaining owner or current native/default presentation. Foreign owned widgets reject edits. Outline-only changes invalidate paint, not reachability. Geometry and height still use explicit native revalidation; no plugin paint repair loop is required. Software, GL3, GLES2 and D3D9 text paths share outline offsets; native software pixels and a GLES2 UI compile have been verified, while Windows rendering remains an open frontend gate.

## Checked native widget actions

C `widgets.actions(context, widget, out, capacity, count)` and Lua `widget:actions()` return current native component actions with copied labels and checked action references. Lua invokes `api.widgets.invoke(action.ref)`; C invokes `widgets.invoke(context, action.ref)`. A zero-capacity C query reports the required count. Labels may contain native color markup and differ in spelling/case across adapters. These are native component operations, not invented generic “click” packets. Inventory grids and chat-line selections still require their own selection APIs and are not represented by a widget-level action query.

The native menu builder supplies IF1 button types, IF3 numbered operations, native filter choices and current event-mask/hook gating. References carry the checked widget plus row ordinal and a signature of native operation state, label, pick targets and effective server mask. Invoke regenerates the current rows and validates the reference before using the ordinary native dispatcher. Tree replacement, changed action state or changed server masks cannot retarget a retained reference. Native/mount/presentation hiding and host availability remain authoritative. `OK` means dispatch was accepted; it does not promise a server acknowledgement or a completed asynchronous script.

Invocation is allowed from startup and ordinary event callbacks, and refused during painting, shutdown or synchronous native script callbacks. Lua action references are userdata; a table cannot forge one, and full-width identity does not pass through a Lua number. `widget:visible()` reads current logical native/presentation visibility; it is not a pixel-occlusion test.

`public_chat_button` is a shared profile role: IF1 uses `slot(chat_buttons, public)`, OSRS239 uses the operational layer at `iface(chat, 11)`. The public probe source is unchanged between them. `report_button` is also shared, but the current rs289lc builtin provides no report operation; an empty action list is accurate. OSRS239 exposes its native Report operations. The Report probe's dispatch returned OK without a report dialog in this fixture, so end-to-end Report behavior remains open.

Frame tree rebinding now preserves current player chat modes while replacing tree-owned slot identities. Later native/server mode changes still win. This repairs a native reset observed during the C action/remount probe; plugins do not repair it by periodically reapplying preferences.

## Plugin settings pages (panel model, verified slice)

A plugin's own page is the host's panel model: `panel.request` registers it, `on_ui_build` describes rows through the panel builder, `on_ui_action` receives one semantic intent per control, and `panel.set_options` / `panel.set_text` retain later changes to two existing rows without rebuilding the page. This is the one panel/action model every chrome executor consumes; the in-canvas `buffer` executor is what a headless run lands on when no web presenter starts, and it paints the same rows into the game canvas. A pick from any executor ends in `app_plugin_panel_dispatch_row` with the row's identity fences, so the plugin's callback, its native write and the republish are the same regardless of presenter.

Verified on both revisions with the two essential plugins: Client Settings shows the gameframe choice, its detail line, interface scaling and the scaling filter; picking the filter writes device option 15 in the CS2 host's option table and the page re-reads it. Feature Flags shows the engine's twenty published flags under their headings; picking "Off" for arrow-key orbit writes the camera control bit and reads back 0 while the revision default stays 1. A native gameframe that is the saved choice now reads "Active", not "Switching". Not verified: the executor's own dropdown hit-test under a real click, scrolling of long pages, and the web presenters.

