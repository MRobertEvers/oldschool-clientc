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
npm install                       # TypeScript
make -C ../../3rd/rscache/tools cs2 cachepack
make -C ../../src web             # the client, for the browser
make dev                          # dev server + browser, watching example/ui/*.tsx
```

`make dev` opens a page with three panes: the client, the state to drive it
with, and the `.if` / `.compack` / `.cs2` / JavaScript the interface compiles to.
Save a record and the preview follows.

The preview **is the game client**. `build-web/torirs.wasm` is the same C client
built from the same sources by `make -C src web`; it renders with the real
toridraw, runs the real CS2 VM against the real UITree, and hit-tests with its
own code. The dev page hosts it in an iframe and drives it over the command bus
(`src/cmd/cmdbus.h`), the same serializable frame ring the native client's
keyboard, mouse and network input already travel on.

That is a deliberate reversal. cs2dom used to carry its own port of the client's
interface runtime — tree, layout, emit walk, hit tests and a canvas painter —
checked against the C client command for command. It reached 881 of 881
interfaces matching, and still drew the wrong picture, because an emit list
cannot see a painter. `CS2_DOM_CLIENT_PLAN.md` has the full argument. The short
version: there is already a renderer that is right by construction.

## The edit loop

An edit is to a text tree and the client reads a packed cache, so something has
to pack — and it is `cachepack`, the same tool the real bake uses. A preview
showing bytes no bake would produce is the failure this whole tool was rebuilt to
stop repeating.

```
edit a .if or .cs2  ->  cachepack --asset-only  ->  preview cache  ->  reboot
```

The preview cache is a **copy** of the real one, under `build/preview-cache`,
with this content tree's interfaces and scripts written over it. Copied because
nothing here should be able to reach the cache the rest of the repo builds
against; copied once because it is 218MB.

Three things have to happen together or the edit silently does not appear, and
all three are in `verify-edit-loop`: io_server is restarted (it holds the cache
open, and its own records behind that), the client is rebooted with
`cache_reset=1` (it keeps every archive it has read in IndexedDB), and a failed
bake does **not** reboot — the cache still holds the last good bytes, so the
client would come back showing the previous interface.

```sh
node bin/cs2dom.js dev --project example \
  --cache ../../cache.osrs239 --rev osrs239
```

`--rev` is the cachepack profile name; cache formats are revision-sensitive, so
it is stated rather than guessed.

Then, when it looks right:

```sh
npm run build        # write the content tree and allocate ids
npm run bake         # make -C src torirsserver-cache
```

`npm run ship` does both.

## What CS2 becomes

Scripts are translated, not interpreted. `cs2 decompile --emit ast-json` hands
the C decompiler's own structured tree to `src/cs2_js_emit.js`, which lowers each
script to one JavaScript generator function — parking on an asset load becomes a
`yield`, and a `gosub` becomes a `yield*`, so a park propagates through call
frames the way the client's yield planner has it. Arithmetic goes through
`src/cs2_intrinsics.js`, whose signed-32 behaviour is pinned to the C VM's
handlers rather than to JavaScript's operators.

All 9,724 decompilable scripts in the corpus lower, parse and declare every
local. `make corpus-aot` is that gate.

The reverse direction, `src/js_to_cs2.js`, deliberately accepts a narrower
subset and **refuses by name** rather than approximating: a construct it cannot
represent is an error naming the construct, never quietly different CS2.

## Interfaces round-trip

`src/tsx_import.js` turns a cache interface into editable TSX and back. Fields
the element vocabulary does not model ride a `raw` prop; hook bindings stay
binding records so sentinels survive. `src/if_record.js` reads and writes `.if`
byte-identically.

All 968 interfaces and their 968 `.compack` files survive a parse-and-write
unchanged — 1,936 identical, 0 differing. `make roundtrip-if` is that gate, and
it is what makes a diff after an edit show the edit and nothing else.

## Why it is shaped this way

**CS2 output is source, not bytecode.** `RSCache_CS2_Compile` already exists and
cachepack already calls it, so a generated script is a file you can read in a
diff, step through in the decompiler's dialect, and patch by hand. Every script
this tool generates is handed to the real compiler (`3rd/rscache/tools/cs2/cs2`)
*before* anything is written, so a tree never holds source that cannot bake.

**React is a build-time language, not a runtime.** There is no React reconciler
in the game client and none in the preview. The compiler renders the authored
component once, splits every prop into two piles, and the split is the shipping
reactivity model:

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
Each slice declares the range its ids live in, so `useStat(40)` is a build error
naming the range (`0..22`) rather than a component that silently never updates.
Ranges come from the content tree where the tree states them, and from the client
where they are fixed.

The dev page's state controls write into the client through the command bus:
varps, varbits, a script to run, or a `::` command. What answers the read is the
client's own host, so there is no second implementation of it to keep honest.

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
tree, because writing generated source back into a binary cache is a separate
bake operation.

Source CS2 with symbolic RuneStar names also needs the RuneStar name-table
directory. Set `CACHEPACK_CS2_NAMES`, or add a project-relative `"cs2Names"`
path in `cs2dom.json`; the usual sibling `cs2` checkout is discovered
automatically.

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
- Cache-authored `name=` and target hooks are dropped on the way to the client's
  runtime, so those props are written correctly but do not arrive.
- No common-subexpression elimination — an expression used by two props of one
  component is emitted twice.

## Layout of the source

| file | what it holds |
|---|---|
| `src/cs2_js_emit.js` | CS2 syntax trees → generator-function JavaScript |
| `src/cs2_intrinsics.js` | signed-32 arithmetic, pinned to the C VM's handlers |
| `src/js_to_cs2.js` | JavaScript → CS2 source, a stated subset that refuses by name |
| `src/verify.js` | handing generated CS2 to the real compiler before it is written |
| `src/generated/cs2_host_surface.js` | the host method signatures, from the decompiler's command table |
| `src/generated/cs2_host_park.js` | which opcodes can park, from the client's own yield planner |
| `src/tsx_import.js` | cache interface ⇄ editable TSX |
| `src/if_record.js` | byte-identical `.if` read and write |
| `src/expr.js` | symbolic expressions; the operator helpers |
| `src/transform.js` | TypeScript → executable module, with operators rewritten |
| `src/loader.js` | runs a component in a `vm` context to find out what it draws |
| `src/runtime.js` | what a `.tsx` imports: elements, hooks, actions |
| `src/components.js` | every element and prop, and the field/command each maps to |
| `src/ops.js` | the operation vocabulary and argument orders |
| `src/ir.js` | lowering: the static/dynamic split, hooks, scripts |
| `src/emit_if.js` | `.if` and `.compack` |
| `src/emit_cs2.js` | CS2 source |
| `src/host.js` | host state slices and the id ranges a build checks against |
| `src/cmd_frames.js` | host commands as cmdbus bytes, for driving the preview |
| `src/dev_client.js` | the dev server: the client, an io_server, and the bake |
| `src/dev_page_client.js` | the dev page: the client in an iframe, state, records |
| `src/dev_records.js` | an interface's script closure, lowered for the records pane |
| `src/ledger.js` | id allocation through the pack files |
| `src/export.js` | edits → content tree → cachepack |

## Gates

| gate | command | number |
|---|---|---|
| the translation suites and the `.if` round trip | `make test` | 1,936 identical, 0 differing |
| every decompilable script lowers | `make corpus-aot` | 9,724 / 9,724 |
| the generated tables match their sources | `make generated-check` | — |
| the whole chain, edit to packed bytes | `make roundtrip-chain` | — |

Three more need a built client, so they are not part of `make test`:

| gate | command | asks |
|---|---|---|
| the wire | `make verify-cmd-wire` | does C read the frames this tool writes? |
| the preview | `make smoke-client` | does the client boot, draw, and take a command? |
| the loop | `make verify-edit-loop` | does an edit reach the screen? |

The last one is the only check that can catch a preview reading a cache nobody
is writing to — every other gate stays green through it.

## Plans

`CS2_DOM_CLIENT_PLAN.md` is the current one. `CS2_DOM_REDESIGN_PLAN.md` and
`CS2_DOM_ARCHITECTURE_PLAN.md` are superseded, and are kept for their
measurements and for their record of what a from-scratch interface renderer has
to get right.
