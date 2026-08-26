# cs2dom

Write an interface as a React component in TypeScript; get the two things the
cache actually holds — an IF3 component tree and CS2 **source** — in the content
tree, ready to bake.

```tsx
export default function RunOrb() {
    const energy = useVarp(varps.sa_energy);
    const percent = energy / 100;
    const low = percent <= 20;

    return (
        <Layer id="root" width={57} height={35}>
            <Text id="readout" font={fonts.p11_full} halign="right"
                  color={low ? 0xff981f : 0x00ff00}>
                {`${percent}`}
            </Text>
            <Layer id="button" x={27} width={26} height={26} ops={['Toggle Run']}>
                <Graphic id="orb" width={26} height={26}
                         sprite={low ? sprites.orb_icon_3 : sprites.orb_icon_4} />
            </Layer>
        </Layer>
    );
}
```

becomes `interfaces/run_orb.if` + `.compack` and one `scripts/*.cs2` per component
that reads state:

```
[readout]
if3=yes
type=4
layer=0
font=494
halign=2
onload=i:9727
onvarptransmit=i:9727
varptriggers=300
```

```
[clientscript,cs2dom_run_orb_readout]
def_int $int0 = 0;
if (calc(%var300 / 100) <= 20) {
	$int0 = 16750623;
} else {
	$int0 = 65280;
}
if_setcolour($int0, interface_969:1);
if_settext("<tostring(calc(%var300 / 100))>", interface_969:1);
```

Then `make -C src torirsserver-cache` bakes it, and the client boots it.

## Start here

```sh
cd tools/cs2dom
npm install          # TypeScript plus the React preview runtime
make -C ../../src torirs
make -C ../../3rd/rscache/tools cachepack
make wasm           # compile the existing C cs2vm2 for the browser
npm start            # dev server + browser, watching example/ui/*.tsx
```

`npm start` opens a page with three panes: the interface, its runtime tree and
host-state controls, and the `.if` and `.cs2` records. Save a file and the page
redraws — no full cache bake and nothing written to the content tree. "New
component" writes a starter `.tsx` and the watcher picks it up.

The interface picker also contains every readable interface under the configured
content tree. Choosing one under **OSRS-Content** opens its `.if` and `.compack`
on demand, rebuilds the parent/child component tree into cs2dom's React-style IR,
and shows a read-only decompiled `.tsx` view beside the original records. The
records are never copied into `ui/` or modified.

For presentation, cs2dom mounts that IR into a browser-owned HostRuntime. The
preview remains a normal React/DOM-style component tree: cache scripts mutate
the same components synchronously, conditional hooks can create and delete
children, cache bitmap fonts and sprites paint into their boxes, and models use
the toridraw WASM component. The production default compiles the repository's
existing `src/cs2vm2` C VM to WASM. Each C HOST request crosses a synchronous
bridge to the JavaScript HostRuntime, which owns and updates the React-style
tree. A review-gated TypeScript migration backend can run only complete
core-only closures today; any unreviewed Host opcode keeps the whole session on
C/WASM. The production C client remains available as a reference oracle, but
its framebuffer is not the interactive UI.

The same browser can open a Dat2 cache without an OSRS-Content checkout:

```sh
node bin/cs2dom.js dev --project example \
  --cache ../../cache.osrs239 --rev osrs239
```

`--rev` is the cachepack profile name; cache formats are revision-sensitive, so it
is stated rather than guessed. On first open, cs2dom asks the repository's
`cachepack` to decode interfaces, readable clientscripts, sprites, fonts and
models into a read-only tree under the OS temporary directory, and retains the
original clientscript bytes for the C CS2VM compiled to WASM. That derived tree
supplies the searchable catalog, live React tree and records pane. It is keyed
by every cache file, the revision and cachepack build, so subsequent opens reuse
it and a changed cache invalidates it automatically. The disposable
`cs2dom-dat2` directory may be removed from the OS temporary directory at any
time.

When the project has `content` and a Dat2 cache is supplied as above, the picker
shows both **OSRS-Content** and **Dat2 cache** groups. The same interface may appear
in both; its `.if`, scripts, sprites and models are always read from the group selected,
which makes comparing an edited content record with the binary cache straightforward.

Then, when it looks right:

```sh
npm run build        # write the content tree and allocate ids
npm run bake         # make -C src torirsserver-cache
```

`npm run ship` does both. Other scripts: `test`, `test:native` (production-C
pixel/state/tree regression), `check` (type-check),
`cachegen` (regenerate the cache bindings), `ops` (print the vocabulary),
`build:dry` (render and verify, write nothing), `verify` (build the CS2 tool,
then run the tests).

### Committed-tree React renderer

`src/react_tree_renderer.js` is the actual React presentation boundary for the
transactional tree migration. It consumes only a small external-store contract:
`getRoots()`, `getNode(renderKey)`, and `subscribe(listener)`. Root arrays and
per-node snapshots remain immutable and referentially stable between commits,
so `useSyncExternalStore` can ignore a coarse store notification for every node
that did not change. Stores may additionally expose targeted node/order
subscriptions and a separate child-order snapshot, so reordering children does
not invalidate their parent's fields. React keys are stable render keys, never
transient VM ids. Pointer, wheel, and keyboard input is delegated once at the
preview root and emitted as plain actions, avoiding per-widget handler closures
and React synthetic events crossing into the runtime.

The built-in renderers cover layers, rectangles, text, graphics, inventory,
models, and lines. A registry can override them by renderer id, component name,
widget type, or role; bitmap-font and toridraw renderers should use that seam.
`src/react_tree_mount.js` supplies the browser `createRoot` wrapper. It is kept
separate so worker-side and server-render tests do not import browser globals.

`make react-runtime` bundles those normal React imports into the local
`web/react_browser_runtime.js`; `npm start` builds it together with the C/WASM
runtime and serves it at `/react-runtime.js`. The live cache-accurate painter is
mounted inside `RetainedInterfaceStage`, so React owns the preview boundary and
observes only final controller transactions while sprite/font/model DOM work
remains cooperatively sliced. The same controller also implements the finer
`ViewTreeStore` contract for authored per-widget React renderers.

The same browser build compiles the exact Dat2 decoder, generated-semantics VM
slice, and whole-closure router to `web/cs2_bytecode_decoder.js`,
`web/cs2_vm_core.js`, and `web/cs2_engine_router.js`. The production mode is
`wasm`; migration tests may request `auto` or `typescript`. `auto` uses
TypeScript only when every declared entry, static hook, opcode-40 dependency,
runtime-installed hook root, and interface group needed by `CC_CREATE` or
`CC_FIND` is proven covered/preloaded. Until those proofs exist, `auto` selects
C/WASM and explicit `typescript` rejects before Host/tree construction. There
is no stateful mid-script fallback.

To measure that migration boundary against real cache programs, run:

```sh
npm run audit:ts-backend
node scripts/audit_ts_backend.js --filter bankmain,pirate_combilock,ca_tasks
node scripts/audit_ts_backend.js --source content --filter bankmain
node scripts/audit_ts_backend.js --all --compact > coverage.json
npm run test:ca-tasks
```

The default audit is intentionally limited to the three named regression
interfaces; `--all` is required for the 968-interface corpus. Stdout is stable
JSON. It separates decoded registry records, unique entry-reachable scripts,
and the sum of per-entry closures, and lists wire-known but unsupported core
opcodes, schema-only Host opcodes, unresolved dynamic hook installers, and
missing GOSUB targets. A Host row is not reported as executable until its
generated `executableReviewed` gate and a real TypeScript implementation both
pass whole-closure routing. The `ca_tasks` differential command exercises the
exact Dat2 mount, hot redraw, filter-button interaction, and follow-up tick. It
currently proves that auto mode safely uses the C/WASM fallback; TypeScript
timing remains skipped until the timer closure installed as script 5244 is
reviewed and admitted.

## Why it is shaped this way

**CS2 output is source, not bytecode.** `RSCache_CS2_Compile` already exists and
cachepack already calls it, so a generated script is a file you can read in a
diff, step through in the decompiler's dialect, and patch by hand. Every script
this tool generates is handed to the real compiler (`3rd/rscache/tools/cs2/cs2`)
*before* anything is written, so a tree never holds source that cannot bake.

**Authored React still compiles at build time; preview React runs in the tool.**
There is no React reconciler in the game client. The compiler renders the
authored component once, splits every prop into two piles, and the split is the
shipping reactivity model. Separately, the local development page uses React as
a renderer over immutable committed tree snapshots:

- a prop holding a plain value becomes a **field in the `.if`** and costs nothing
  at runtime;
- a prop holding an expression that reads state becomes a **statement in a
  generated script**, and the state it reads becomes the **hook that re-runs it**.

So composition, props, helper functions, loops and conditionals are all free —
they are resolved before the cache sees them — and the only thing that ships as
code is the part that genuinely has to change while the interface is open.

**Real operators.** `energy / 100` and `percent <= 20` are division and
comparison, not method calls: `src/transform.js` rewrites every operator inside a
component into a helper that computes when both sides are plain values and builds
an expression node when either is symbolic. `2 + 2` is 4 at build time and never
reaches the emitter.

**Ids are a ledger.** `pack/3_interfaces.pack` and `pack/12_clientscripts.pack`
are already the content tree's id authority, so they are where ids live.
Allocation only appends past the highest; a name that disappears keeps its line
and its id stays spent, because recycling it would hand one script's id to a
different script.

**Generated files are owned.** Every generated file carries a marker, and a build
that finds a file without one stops rather than overwriting it.

## The two update paths

The cache can only author some transmit hooks, and that decides how state
reaches the screen:

| state | how it updates | why |
|---|---|---|
| varp, varbit, stat, inv | the component carries an update script and a trigger list; the client's transmit pump re-runs it | `.if` has keys for these hooks |
| varc (`useState`) | the handler that writes it also carries the updates of everything that reads it | cachepack's `.if` grammar has no varc-transmit key, so nothing can notice the write — but the writer already knows who cared |

A varbit's trigger is its **varp**, not its own id; `cachegen` reads
`configs/all.varbit` to find it.

## Host state

Everything a script reads that is not a component — `CS2VM_HOST_REQUEST_*` in
`src/cs2vm2/cs2vm2_host.h` — is described in `src/host.js` as a set of slices.
Each slice declares three things:

- **the range its ids live in**, so `useStat(40)` is a build error naming the
  range (`0..22`) rather than a component that silently never updates. Ranges
  come from the content tree where the tree states them and from the client where
  they are fixed;
- **how the preview answers a read**, against state the dev page owns;
- **what control the page offers**, because a slice nobody can move is a slice
  that cannot be tested. Variables and stats get sliders; an inventory gets an
  item/count editor, since `inv_getnum` asks about contents rather than a number.

Cache enums, objects, NPCs, locations, map elements, parameters, structs, DB
tables/rows, world-map definitions and font metrics are loaded once per selected
source for synchronous HOST lookups. Bounded client-owned services mirror the C
client's deterministic state as well: highlights and client-op slots, chat
history and filters, seeded friend/ignore lists, loot records, entity overlays,
active subjects/routes and the world-map session.

Actions which the production client hands to another service are kept honest.
For example, friend-list edits and outgoing chat update the local state where the
C client does, then emit a bounded service intent through HostRuntime's callback;
logout and audio requests are recorded in the same way. They do not pretend that
a server accepted the operation. Live account/network answers (such as real
friend presence or hiscores results) and live-world entity/projection answers
remain unavailable unless the caller supplies seed data or a synchronous scene
provider. Those paths use the desktop client's empty, offline or no-target
result, or remain visibly unsupported, rather than fabricating data.

`GOSUB_WITH_PARAMS` is present in the generated request-name manifest for ABI
bookkeeping, but the C VM resolves it internally against its own script registry.
It is not a JavaScript HOST call and its out-of-scope classification is not a
missing browser service.

## Preview fidelity

**Authored TSX**, **OSRS-Content** and **Dat2 cache** selections all use the same
live browser component runtime. `src/preview.js` ports the C layout and clipping
rules; HostRuntime implements the component and input host API; cache bitmap
fonts and sprites use their original assets; models use toridraw WASM. Imported
hooks run during mount and every mouse, keyboard, transmit and timer event can
mutate the tree before it is repainted.

State that normally comes from a logged-in server starts at explicit preview
defaults. The controls seed supported varp, varbit, varc and stat values before
HostRuntime mounts `onLoad`; they do not invent an account, inventory or world
scene that was never supplied. Editing state is a draft until **Save** is
pressed, and hot reload replaces only the preview/tree/records so that draft and
keyboard focus survive source changes.

## Commands

```
cs2dom dev       [--project DIR] [--cache DIR --rev NAME] [--port N] [--no-open]
cs2dom build     [--project DIR] [--dry-run] [--no-verify]
cs2dom cachegen  [--project DIR] [--out FILE]
cs2dom check     [--project DIR]
cs2dom ops
```

A project is a directory with `cs2dom.json`:

```json
{
  "content": "../../../OSRS-Content/osrs239-content",
  "cache": "../../../cache.osrs239",
  "revision": "osrs239",
  "cs2Names": "../../../../cs2/src/main/resources/org/runestar/cs2",
  "sources": "ui",
  "varcPool": [1400, 1499],
  "cachegen": ["sprites", "fonts", "interfaces", "varps", "varbits", "varcs", "invs", "stats"]
}
```

For a read-only Dat2 project, replace `content` with the cache and its explicit
profile:

```json
{
  "cache": "../../../cache.osrs239",
  "revision": "osrs239",
  "sources": "ui",
  "varcPool": [1400, 1499]
}
```

`dev` supports that form directly. `build` still requires an unpacked `content`
tree because writing generated source back into a binary cache is a separate bake
operation.

Source CS2 with symbolic RuneStar names also needs the RuneStar name-table
directory. Set `CACHEPACK_CS2_NAMES`, or add a project-relative `"cs2Names"`
path in `cs2dom.json`; the usual sibling `cs2` checkout is discovered
automatically. A selected script that cannot compile fails visibly instead of
silently retaining stale bytecode from the base cache.

`varcPool` is the id range `useState` allocates from — client-side scratch
variables the cache does not define. `cachegen` picks which tables land in
`cache.gen.ts`; the default leaves out models and items, which are 100,000 lines
nobody autocompletes through. `["all"]` includes everything.

## Elements and props

`cs2dom ops` prints the table. `Layer`, `Rect`, `Text`, `Graphic`, `Model`,
`Line`, with props named after the component fields they set. A prop with no
runtime command (`clickMask`, `alpha`) can only be given a fixed value — binding
state to one is a compile error, not a value that never moves.

There is no `<Inv>`: an inventory component's contents are not a field in IF3,
they are set with `if_setobject` from a script, so an element with an `inv` prop
would be authoring something the format does not have.

## Known gaps

- **`actions.button()` emits `if_triggerop`, which this client does not implement
  yet** (only `CC_TRIGGEROP` is wired; `IF_TRIGGEROP` is open). The cache record
  is correct; the click will not reach the server until the client catches up.
  The build prints this as a warning rather than letting it look like it works.
- Cache-authored `name=` and target hooks are dropped on the way to this client's
  runtime, so those props are written correctly but do not arrive.
- No common-subexpression elimination — an expression used by two props of one
  component is emitted twice.
- If no original or compiled `.cs2b` is available, the preview says that scripts
  are not running. Original Dat2 clientscript bytes currently remain on the C
  CS2VM/WASM reference path. The generated-semantics TypeScript VM is selected
  only for a completely supported script closure; there is no silent or
  stateful mid-invocation engine switch.

## Layout of the source

| file | what it holds |
|---|---|
| `src/expr.js` | symbolic expressions; the operator helpers |
| `src/transform.js` | TypeScript → executable module, with operators rewritten |
| `src/loader.js` | runs a component in a `vm` context to find out what it draws |
| `src/runtime.js` | what a `.tsx` imports: elements, hooks, actions |
| `src/react_tree_renderer.js`, `src/react_tree_mount.js` | real React external-store preview renderer and browser mount boundary |
| `src/components.js` | every element and prop, and the field/command each maps to |
| `src/ops.js` | the operation vocabulary and argument orders |
| `src/ir.js` | lowering: the static/dynamic split, hooks, scripts |
| `src/emit_if.js` | `.if` and `.compack` |
| `src/emit_cs2.js` | CS2 source |
| `src/host.js` | host state slices, ranges, preview controls |
| `src/eval.js` | evaluating the IR against made-up state |
| `src/preview.js` | the client's IF3 layout, ported |
| `src/content.js` | `.if`/`.compack` → preview IR and read-only React-style TSX |
| `src/bytecode.js` | exact original/compiled `.cs2b` program transport |
| `src/cs2_bytecode_decoder.ts`, `src/cs2_engine_router.ts` | strict Dat2 decoding and fail-closed whole-closure backend selection |
| `src/wasm_runtime.js` | browser adapter for the C VM ABI and synchronous HOST bridge |
| `src/host_runtime.js`, `src/host_data.js` | JavaScript HOST implementation over the live React-style tree |
| `src/host_activity.js`, `src/host_chat_social.js` | bounded highlight/client-op and chat/social state plus service intents |
| `src/host_db.js`, `src/host_worldmap.js` | cache-backed DB iterators and world-map state |
| `src/host_loot.js`, `src/host_overlay.js`, `src/host_subject.js` | loot, dynamic overlay and live-subject state with optional scene adapters |
| `wasm/` | narrow Emscripten ABI around the existing `src/cs2vm2` implementation |
| `src/cache_runtime.js` | bounded source analysis used while importing readable records |
| `src/model.js` | cache model records → entity-viewer wire bridge for toridraw/WASM |
| `src/dat2.js` | selective, cached Dat2 → read-only content source |
| `src/native_overlay.js` | selected `.if` + reachable CS2 → keyed COW Dat2 overlay |
| `src/native_preview.js` | bounded production-client framebuffer bridge |
| `src/native_tree.js` | validated live UITree snapshot → inspector metadata |
| `src/dev.js`, `src/dev_page.js` | the dev server and its page |
| `src/ledger.js` | id allocation through the pack files |
| `src/verify.js` | handing generated CS2 to the real compiler |

`npm test`, `make test` and `node test/run_all_tests.js` run every focused HOST
parity suite followed by the central compiler/runtime suite. The latter remains
available directly as `node test/run_tests.js`; it includes a gate that compiles
one probe per command in the vocabulary, so an argument order that drifts fails
there rather than in the client.
