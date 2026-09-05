# Plugin contract, major 3

This is the normative contract implemented by M2–M5. The declarations and
shared policy code are in `src/plugin/torirs_plugin_contract.h` and `.c`.
The current V2 host has not yet been replaced. No compatibility execution
path is part of the intended result.

## Identity, authority and capability

1. A semantic selector names a query. An element reference names one exact
   incarnation in one tree or plugin instance. Reparenting/reordering preserve
   that incarnation; deletion, replacement, remount replacement and reuse do
   not. A saved reference never silently resolves to another incarnation.
2. Native IDs, parent links, child indices and script/server address spaces
   remain native. Presentation placement is not native reparenting. Added UI
   parts have their own identity; an extension is never fabricated as a native
   control or used as evidence that a native capability exists.
3. Every plugin declares required and optional semantic capabilities. Required
   absence rejects activation before publication. Optional absence has an
   explicit unavailable result and feature status. Native state remains
   authoritative within each capability, including server hiding on IF1.
4. A native hide blocks its native scope in paint, attached presentation and
   actions. Native selected-panel, mount, mask, screen and controller limits
   retain their own scope. Mount bookkeeping and item updates cannot clear an
   independent native hide. Showing native content requires the corresponding
   native show operation; releasing a plugin is not one.

## Property ownership and reads

| Family | Native state | Presentation may claim | Release / mutation outcome |
|---|---|---|---|
| Identity/topology | IDs, incarnation, hierarchy, mounts, sibling order | Relative attachment order; identity of owned parts | Native mutations remain live; stale references fail |
| Geometry | Requested coordinates, sizing modes, aspect and native layout | Individually named X, Y, width and height constraints | Native requests keep updating; release resolves the latest request |
| Scrolling/clipping | Content extent, canonical offsets, native masks and restrictions | An explicit permitted wrapper/popup clip policy | Moving a surface retains scroll and native restrictions; valid offsets survive, invalid offsets clamp natively |
| Text/items/state | Native text, quantities, counters, CS1 active values, selection, parameters | Live bindings for extensions; state-to-style mappings | No startup copies replace native content; bindings revalidate identity |
| Appearance | Current image, colours, font, opacity and state variants | Separate image mapping, fill mapping, default-text palette, font and opacity multiplier | Unmapped native variants fall through; native transitions remain observable |
| Animation/model | Native sequence, frame/cycle, pose and animation updates | A separate presentation pose offset or mapped art | Native animation continues; a sprite claim does not claim animation or opacity |
| Operations/hooks | Current operations, masks, labels, listeners and hotkeys | Declared added actions and explicit native action invocation | Native operations are never copied into stale startup menus |

Native opacity applies to self where the revision's native renderer applies
it to self. It does not become subtree opacity because a plugin owns an image.
Opacity modulation multiplies current native opacity. Native drag translation
and ghosting are separate native interaction effects with their native subtree
scope. Native visibility is always evaluated independently of opacity.

Computed geometry getters observe current effective constraints synchronously,
including native set/get sequences inside one script. Resolved zero is zero.
Requested values remain available to the engine and inspector for release.
Script relative coordinates remain native-parent-local and unscrolled. Canvas
bounds include presentation placement and scrolling; window/drawable conversion
is explicit. These coordinate labels cannot be mixed implicitly.

Unclaimed descendants run native layout inside effective allocation. A provider
that cannot support requested reflow rejects it with `UNSUPPORTED_LAYOUT` and
a reason before publication. It does not give scripts one computed box and
paint/hit testing another allocation. Geometry claims do not claim content,
visibility, masks, operations, animation or unrelated axes.

Text palette mappings operate on native semantic/default style. Inline semantic
colours and native state changes survive. In particular, changing native chat's
default white foreground for parchment does not rewrite chat strings, discard
message colours or freeze text updates.

## Transactions, conflicts and scheduling

Declarations form an atomic presentation bundle. Invalid references, invalid
properties, unsupported constraints or invalid anchor graphs reject the entire
candidate and preserve a still-valid previous bundle. Existing native changes
are not rolled back when a plugin declaration fails.

An exclusive property conflict suspends every presentation contribution in the
conflicting bundles and exposes current native presentation. No contender wins
by registration order. Unrelated bundles remain eligible. Releasing one
contender resolves the others against current native state. The host reports
the property, target and participants. A user-selected frame is a single
activated provider; unselected frame offers publish no competing claims.

Additive attachments are ordered next to their anchor using BEFORE, AFTER or
REPLACE_SELF, with stable owner/part-name ordering for equal relations. Anchor
cycles are invalid. REPLACE_SELF replaces decoration only; native operational
descendants remain intact. A replacement is eligible only while its source and
target are eligible. This rule is transitive and is shared by paint, input and
retained actions.

Native mutations apply first. Bindings and allocation constraints then resolve;
native layout and required resize callbacks settle before one presentation
revision is published for paint and interaction. Reentrant/async work enters
at a documented safe point. No mutation can change half a published revision.
Repeated conflicting layout changes terminate with responsible participants and
properties reported; they do not oscillate indefinitely or silently defer a
script-visible computed read to the next frame.

| Callback phase | Read | Declare presentation | Invoke native action | Release |
|---|---|---|---|---|
| Describe | yes | no | no | no |
| Activate | yes | yes | no | yes |
| Native event | yes | yes | no | yes |
| Layout | yes | yes | no | yes |
| Action | yes | yes | yes | yes |
| Paint | yes | no | no | no |
| Stop | yes | no | no | yes |

An invocation returns dispatch status, not a fabricated server acknowledgement.
The host revalidates action identity, current native operation/mask and
availability immediately before dispatch. A decoration declaration grants no
native action authority.

## Retained interaction and lifecycle

Menus, pressed controls, drag sources/targets, focus, tooltips and queued
callbacks retain checked identities and owner epochs. A recycled numeric ID
does not inherit them. Changed operations/masks or presentation trigger
revalidation; an unavailable target cannot receive a retained action. Focus
transfer to a new incarnation requires an explicit valid focus operation.

Disable, failure, reload, frame selection and resource replacement publish
transactionally. They revoke affected actions/callbacks before freeing state.
Releasing presentation exposes current native state, never saved obsolete
values. Callback APIs and borrowed snapshot pointers are scoped to the callback;
only copied values and checked references may be retained. Data migration
preserves useful configuration/tracker records without executing old code.

Both C and Lua use the same result vocabulary, capabilities, phases, references,
ordering, conflict rules and lifetime validation. No public raw-tree mutation,
revision-specific component lookup, obsolete builder or compatibility loader
is retained at the end of M5.

## Executable conformance

`make -C src test-plugin-contract` checks phase authority, incarnation equality,
invalid candidate atomicity and order-independent bundle conflicts.
`make -C src test-plugin-contract-native` selects M1's production-tree cases in
the existing UITree harness. The original implementation is observed failing
native-hide/content independence, zero-width computed readback, and focus on
recycled IDs. M2 must make these pass and expand structural/retained cases;
the actual revision harness remains mandatory in M3/M6.
