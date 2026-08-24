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
npm install          # one dependency: typescript
make -C ../../src torirs
make -C ../../3rd/rscache/tools cachepack
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

For presentation, cs2dom makes a content-addressed copy-on-write Dat2 overlay,
replaces the selected interface and every reachable source CS2 script, and asks
the production C client to boot and render it. That means the preview uses the
same App, UITree, CS2 VM, bitmap fonts, sprites, models, animation, clipping,
dynamic children and Soft3D rasterizer as the client itself. A warm overlay is
reused; editing the `.if` or one of its reachable scripts gives it a new key.

The same browser can open a Dat2 cache without an OSRS-Content checkout:

```sh
node bin/cs2dom.js dev --project example \
  --cache ../../cache.osrs239 --rev osrs239
```

`--rev` is the cachepack profile name; cache formats are revision-sensitive, so it
is stated rather than guessed. The exact frame reads that Dat2 cache directly.
On first open, cs2dom also asks the repository's `cachepack` to decode the
interfaces, clientscripts, sprites and models into a read-only tree under the OS
temporary directory. That derived tree supplies the searchable catalog, records
pane and diagnostic authored-component fallback. It is keyed by every cache
file, the revision and cachepack build, so subsequent opens reuse it and a
changed cache invalidates it automatically. The disposable `cs2dom-dat2`
directory may be removed from the OS temporary directory at any time.

When the project has `content` and a Dat2 cache is supplied as above, the picker
shows both **OSRS-Content** and **Dat2 cache** groups. The same interface may appear
in both; its `.if`, scripts, sprites and models are always read from the group selected,
which makes comparing an edited content record with the binary cache straightforward.

Then, when it looks right:

```sh
npm run build        # write the content tree and allocate ids
npm run bake         # make -C src torirsserver-cache
```

`npm run ship` does both. Other scripts: `test`, `check` (type-check),
`cachegen` (regenerate the cache bindings), `ops` (print the vocabulary),
`build:dry` (render and verify, write nothing), `verify` (build the CS2 tool,
then run the tests).

## Why it is shaped this way

**CS2 output is source, not bytecode.** `RSCache_CS2_Compile` already exists and
cachepack already calls it, so a generated script is a file you can read in a
diff, step through in the decompiler's dialect, and patch by hand. Every script
this tool generates is handed to the real compiler (`3rd/rscache/tools/cs2/cs2`)
*before* anything is written, so a tree never holds source that cannot bake.

**React runs at build time, not in the client.** There is no reconciler in the
game. The compiler renders the component once, splits every prop into two piles,
and the split is the whole reactivity model:

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

A read with no model — `enum`, the `db_*` commands — is **listed in the page as
unmodelled** rather than answered with a zero. A preview that quietly invents
values is worse than one that admits what it is guessing.

## Preview fidelity

An **OSRS-Content** or **Dat2 cache** selection is rendered by the production C
client into one framebuffer. There is no parallel browser implementation in
that path: layout, conditional CS2, component mutation, runtime-created children,
cache fonts, sprites, item and player models, animation, clipping, blending and
rasterization all come from the same code that renders the game. The transparent
HTML boxes on top are inspector hit regions only and never repaint client pixels.

An unsaved **Authored TSX** selection is different: it has no cache record for
the C client to open yet. It therefore keeps the fast diagnostic renderer while
you edit. `src/preview.js` ports the C layout and clipping rules, real cache
sprites are used, and models use toridraw WASM. Build the component to put it
through the authoritative native path.

The native preview is offline, so state that normally comes from a logged-in
server starts at the C client's own boot defaults. The controls seed supported
varp, varbit, varc and stat values before UITree build and `onLoad`; they do not
invent an account, inventory or world scene that was never supplied.

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
- Unsaved authored TSX uses the diagnostic browser renderer until it is built;
  imported OSRS-Content and Dat2 records use the exact native framebuffer.

## Layout of the source

| file | what it holds |
|---|---|
| `src/expr.js` | symbolic expressions; the operator helpers |
| `src/transform.js` | TypeScript → executable module, with operators rewritten |
| `src/loader.js` | runs a component in a `vm` context to find out what it draws |
| `src/runtime.js` | what a `.tsx` imports: elements, hooks, actions |
| `src/components.js` | every element and prop, and the field/command each maps to |
| `src/ops.js` | the operation vocabulary and argument orders |
| `src/ir.js` | lowering: the static/dynamic split, hooks, scripts |
| `src/emit_if.js` | `.if` and `.compack` |
| `src/emit_cs2.js` | CS2 source |
| `src/host.js` | host state slices, ranges, preview controls |
| `src/eval.js` | evaluating the IR against made-up state |
| `src/preview.js` | the client's IF3 layout, ported |
| `src/content.js` | `.if`/`.compack` → preview IR and read-only React-style TSX |
| `src/cache_runtime.js` | bounded source-CS2 runtime for imported hooks and dynamic UI |
| `src/model.js` | cache model records → entity-viewer wire bridge for toridraw/WASM |
| `src/dat2.js` | selective, cached Dat2 → read-only content source |
| `src/native_overlay.js` | selected `.if` + reachable CS2 → keyed COW Dat2 overlay |
| `src/native_preview.js` | bounded production-client framebuffer bridge |
| `src/native_tree.js` | validated live UITree snapshot → inspector metadata |
| `src/dev.js`, `src/dev_page.js` | the dev server and its page |
| `src/ledger.js` | id allocation through the pack files |
| `src/verify.js` | handing generated CS2 to the real compiler |

`node test/run_tests.js` runs everything, including a gate that compiles one
probe per command in the vocabulary — an argument order that drifts fails there
rather than in the client.
