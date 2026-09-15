# revconfig

Boot data, alongside [`manifests/`](../manifests) and [`profiles/`](../profiles):
the INI pairs a manifest names in `[ui:boot]`.

```ini
[ui:boot]
chrome=revconfig
revconfig_ui=../revconfig/rs245_2lc/rs245_2lc_dat1_ui.ini
revconfig_cache=../revconfig/rs289lc/rs289lc_dat1_cache.ini
```

## Two halves, and why they are separate files

A revconfig is read as a pair, and the split is not arbitrary — it is the line
between what a world **looks like** and what its **cache holds**.

* **`*_ui.ini` — the layout.** Panels, components, fonts *by reference*, where
  everything sits. It names things by section id (`font=b12`), never by cache
  address.
* **`*_cache.ini` — the bindings.** What each of those section ids actually is
  in this cache: which jagfile archive a sprite comes out of, which file stem a
  font is, which fixed scene slot it pins to.

That is what lets one layout serve several caches. `rs289lc` ships a cache file
and no UI file, because 289 did not move the gameframe — it renamed the four
fonts underneath it (`p11` → `p11_full`, a different glyph layout as well as a
different name; see the file's own header). The manifest takes its UI half from
`rs245_2lc` and its cache half from `rs289lc`, and neither file has to know.

A cache file also answers for the things the client draws on its OWN behalf and
which therefore have no interface record to carry an id: the graphic-defaults
sprites (compass, map dots, click cross), the fonts it sets hitsplats and the
minimenu in, and the `[script:]` / `[iface:]` / `[varbit:]` / `[seq:]` /
`[setting:]` ids it drives by number. Undeclared means absent — the slot stays
unbound and the feature switches off — because the alternative is a built-in
default, and a built-in default is a wrong answer on every cache but one.

## Naming

`<epoch><revision>`, where the epoch is `osrs` (OldSchool) or `rs` (everything
before it), and an `lc` suffix marks a Lost City build. The directory and the
files inside it carry the same name, so a path names its own profile.

## What is here

| directory | |
|---|---|
| `rs245_2lc/` | The Lost City 2004 gameframe (dat1). The UI half of every dat1 world in this tree. |
| `rs289lc/` | Cache bindings for Lost City's January-2005 build. Cache half only. |
| `osrs217/` | OldSchool 217-era (dat2). |
| `osrs239/` | OldSchool 239 (dat2). Cache half only — the gameframe is the cache's own IF3 tree, so there is no layout to state. |
| `osrs_static/` | The generic OldSchool static gameframe, for the `--dat2` client-built path rather than a cache interface. |
| `osrs_kronos/` | As above, for a Kronos server's screen 165 layout. |
| `cullmaps/` | Baked painter cull maps (`.bin`, gitignored — regenerate with `tools/deprecated/gen_painters_cullmap/`). |

## Why it is not under `v0/`

It used to be, and before that `src/osrs/revconfig/configs/`. `v0/` is the
retired first client; this data is read by the *current* one — `src/main.c`'s
defaults, every dat1 manifest, `test-ui-slots`. Boot data that the live client
cannot start without does not belong inside a tree kept only for reference,
where the next person to delete `v0/` takes the client with it.

The revconfig **code** (`revconfig.c`, `uitree_load.c`) stayed in `v0/`, because
that really is retired: `src/revconfig/` and `src/engine/uitree_builder/` are
what read these files now.
