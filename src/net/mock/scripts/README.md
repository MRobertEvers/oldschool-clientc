# The mock server's content

RuneScript sources for the rev-230 mock. Compiled by `src/serverscript`'s
`sscompile`, loaded by `mock230_scripts.c`, executed by the ServerScript VM.

```
make -C src mock230-scripts     # rebuild build/script.dat and script.idx
```

`build/` is checked in, so building or testing the mock needs no compiler. Only
edit-then-recompile touches it.

## These are rev-230 ids, not LostCity's

`pack/*.pack` maps names to ids from **`cache.osrs230`**. LostCity's own content
is 2004-era: its `npc.pack` says something entirely different for id 3105, and
`inv_add(inv, coins, 1)` compiled against its packs would hand the rev-230
client a random item.

The `.rs2` *language* transfers from LostCity. The ids do not. See
`docs/serverscript.md`.

## Layout

```
hans.rs2      [opnpc1,hans] and [opnpc3,hans] — npc 3105's two menu ops
login.rs2     [login,_], plus [opnpc1,goblin]
pack/         name -> id, one file per namespace
build/        compiled output (checked in)
```

## Adding a symbol

Ids come from the cache. To find one:

```
tools/dump_npc/dump_npc --game oldschool --epoch dat2 --revision 230 \
    cache.osrs230 --id 3105
tools/dump_interface/dump_interface cache.osrs230 --iface 231
```

then add `<id>=<name>` to the matching `pack/*.pack`. An unknown name is a
compile error, not a silent zero.

## What content can do today

Messages (`mes`), npc chat (`npc_say`), inventory (`inv_add`, `inv_del`,
`inv_total`, `inv_freespace`), player variables (`%varp` read and write, which
reach the client as VARP_SMALL), coordinates (`coord`, `coordx`/`coordy`/`coordz`,
`movecoord`, `distance`, `npc_coord`), teleports (`p_teleport`, `p_telejump`),
and obj config reads (`oc_name`, `oc_stackable`).

Anything else compiles and runs, but reaches the VM's loud stub: it pops and
pushes what the signature declares so the script survives, and reports once to
stderr. That is deliberate — a server that silently pretends `inv_total`
returned 0 produces a bug report about content rather than about the engine.

**Scripts cannot suspend yet.** `p_delay`, `p_pausebutton` and the queue family
need the parking machinery, which is the next piece of work. A script that tries
is reported and dropped rather than half-run.

## Triggers wired so far

| trigger | fired by |
|---|---|
| `[login,_]` | phase 7 of the tick, once per session |
| `[opnpc1..5,<npc>]` | an OPNPC packet, resolved by npc type then category then global |

A trigger with no script falls through to the mock's hardcoded C behaviour, so
deleting `build/` leaves a working server. That fallback is load-bearing: it is
what keeps `make -C src test-mock230` green while the content is mid-edit.
