# M2 implementation evidence

M2 is in progress. This checkpoint fixes native identity, geometry readback,
content/hide independence, mount suppression and cycle rejection. It does not
complete the mutation audit or the major-3 production cutover.

Implemented in the shared production tree:

- `UITreeNodeRef` validates tree instance, slot and 64-bit incarnation. Focus
  and queued CS2 drag pickup use it. Existing UI retained-incarnation storage
  is widened consistently; resource tokens have their separate lifetime rules.
- A computed zero width/height remains zero in native getters.
- `ApplyObject` changes the addressed component's content. It neither clears
  native hiding nor redirects through guessed equipment children or changes a
  sibling's visibility. Native scripts retain their explicit decoration writes.
- `mount_hidden` is independent of native/cache hide. Async preparation,
  spillover, subinterface mounting and opentop use `SetMountHiddenAt`. Its
  mutation invalidates layout, paint and reachability. Availability, input,
  scrolling, hover and snapshot readers include that restriction.
- Reparent validates the proposed ancestor chain before touching links. Invalid
  targets and cycles fail atomically; valid moves preserve node identity.

The existing UITree harness runs the contract cases in its ordinary suite.
Cases cover recycled native IDs, matching slots/incarnations in different
trees, crossing the old 32-bit incarnation boundary, stale queued pickup,
explicit focus transfer, zero geometry, interleaved native/mount hiding,
behavior replacement during suppression and cyclic reparent attempts.

Verification uses `/private/tmp/plugin-engine-evidence`:

| Evidence | Result |
|---|---|
| `m2-negative-controls-final` | Temporary mutations are compiled into the existing UITree harness; tree identity, incarnation identity, zero geometry, content hiding, mount hiding and topology must each fail their named assertion. Run `python3 tools/plugin_engine_negative_controls.py NEW_DIRECTORY`. Compilation failures or crashes do not count. |
| `m2-osrs-native` | Four prepared native roots pass: 548, 161, 164, 601. Inspected at 2×: 8/8/8/7 chat controls and six green mode labels each. |
| `m2-lc-native` | Both real packet/CS1 scenarios pass. Two distinct accounts verified. Inspected at 2×: 19 skill cells, strength 20/20, four chat captions and three green modes; guide has Attack, Defence and Close controls. |
| `m2-osrs-equipment` | Actual equipment panel and native equip rejection remain operational: rune platebody is rejected by the Dragon Slayer requirement. This is denial evidence, not a successful equipment mutation. |
| `m2-osrs-equipment-live` | Real `item 1079 1`, `equip 3`, then click the worn legs slot. Server save has old legs 27241 and rune legs 1079 in backpack and worn slot 7 empty. Enlarged client capture shows the empty legs silhouette, nine occupied positions, three silhouettes and four equipment action buttons. |

The optimized embedded client builds. UITree passes; host/frame/mobile suites
pass 277/164/144 checks. The native runs use the prepared fixtures from M0,
readiness-based input timing and ordinary freshness preflights, with no stale
override. Native capture commands and final saves are retained in each run.

Remaining M2 obligations include classified deep copy and subtree reuse,
complete action/hook revisions and queued callback validation, async resource
completion, publication-boundary enforcement, remaining direct runtime writers,
and invalidation coverage for every property family. Focus visibility/lifecycle
and the other retained interactions need the complete presentation authority
from M3/M4. These runs do not establish those unimplemented guarantees.
