# Quest Helper port (RuneLite plugin -> Lua plugin)

`QhToLua.java` translates the RuneLite Quest Helper plugin
(`/Users/matthewevers/Documents/git_repos/quest-helper`) into Lua for the
client's plugin host. It is a real compiler front end: it runs javac (with
Lombok applied, exactly as Gradle builds the plugin) to attribute every tree,
then emits Lua from the typed trees. Types decide what an operator means
(`+` concatenation, integer division, char arithmetic), javac decides which
overload a call names, and constant folding decides every gameval id.

    tools/questhelper_port/build.sh            # regenerate script/plugins/quest_helper/gen

What it writes, under `script/plugins/quest_helper/gen/`:

- `fw/<package>.lua` -- one module per framework package, loaded at start.
- `helpers/<category>/<dir>.lua` -- one module per quest / diary / miniquest
  directory. These are **loaded on demand**: nothing reads a quest module
  until the player opens that quest (or a feature that needs its
  requirements runs).
- `index.lua` -- class -> module map, module dependencies, framework order,
  the lazy classes' framework bases and overrides.
- `enums.lua` -- RuneLite API enums the plugin reads (Quest, Skill, Prayer,
  ...), dumped from the runelite-api jar by reflection.
- `inventory.tsv` -- every external (JDK / RuneLite / Guice) member the
  generated code calls, with the Lua name it calls it by. The hand-written
  runtime in `script/plugins/quest_helper/rt/` must provide each one;
  `harness/check_inventory.lua` boots the entry and says which are missing
  (exit 1 when any is).

The plugin config declaration (from `QuestHelperConfig`'s `@ConfigItem`
methods) is not a generated file: `build.sh` runs `gen_config_decl.lua` and
splices its items into the entry (below).

Hand-written, not generated: `rt/` (the Java runtime, the RuneLite API over
the client's plugin API) and `ui/` (the side panel, which in RuneLite is
Swing). `com.questhelper.panel.*` is therefore declared but not emitted.

## Lua conventions the generated code relies on

- Classes live in the global registry `C`, keyed by binary name
  (`C["com.questhelper.steps.NpcStep"]`); a class is realised lazily on first
  access, superclass first, then its static initialiser runs.
- Methods are called with `:`; an overloaded name carries its erased
  signature (`addStep__Requirement_QuestStep`). The rule is in
  `Names.method()` and is the same for library types, so the runtime names
  its functions by the inventory.
- Constructors: `Cls.__new__sig(...)` allocates and runs `__init__sig`.
- Arrays are `{n = length, [1..n]}`; `null` is `nil`.
- Lambdas are objects with their single abstract method (`J.fn`).

## Integration (the plugin entry and its checks)

- `script/plugins/quest_helper.lua` -- the plugin entry (boot, module loading,
  event fan-out); `docs/QUEST_HELPER_PORT.md` "The plugin entry".
- `splice_config.lua` -- `build.sh` runs `gen_config_decl.lua` and splices its
  items into the entry's `config = {...}` between the GENERATED CONFIG markers.
- `harness/test_entry.lua` -- the entry end to end against `harness/mock_api.lua`
  under the client's sandbox globals and instruction count: boot, panel,
  X Marks the Spot loaded on demand, its varbit through its states, the
  overlay passes, the spade requirement, a 32k-loc scene.
- `harness/test_residency.lua` -- the residency pass and the frame-spread
  quest list: every helper harvested and its module unloaded (a mocked
  `api.sandbox.module_unload`), the skill filter / "Show meets reqs" / quest
  lists / Check-items totals over all 272 equal to its own `--reference` run
  (every module loaded), unload/reload identity and eviction, the largest
  callback and the peak heap; `--no-unload` is the sandbox without the verb.
- `harness/check_inventory.lua` -- every external row of `gen/inventory.tsv`
  looked up on the booted runtime; `QH_INV_VERBOSE=1` also lists the rows
  that only implementors, lambdas or constructors provide.
- `harness/run_all_real.lua` -- every helper set up on the REAL runtime
  (the entry booted, no stubs), each loaded through `QH.load_quest`;
  `QH_BUDGET=1` reports the largest callback's instruction count.

