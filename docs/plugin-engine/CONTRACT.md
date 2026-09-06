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

`widgets.find_all(role)` returns checked references to all current matches. Ordinary frame roles currently produce their one bound widget. The `ground_item_labels` adapter returns the caption text widgets in OSRS239 coordinate-overlay slot 0; native buttons and absolute-width timer text remain separate. This mapping was checked against all six prepared `overlay_coord_create` producers: ground labels use slot 0, coordinate timers use 1, loc timers use 2–5, cannon HUD uses 6, clues use 7. The caption constructors in `torirs_gi_row` and the overflow row use width 108 with parent-relative sizing. That is an explicit adapter assumption for the pinned content, not a guarantee about arbitrary future caches. The rs289lc adapter reports this native-only role unavailable.

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
