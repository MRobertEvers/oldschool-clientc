# revconfig

Boot data, alongside [`manifests/`](../manifests) and [`profiles/`](../profiles):
the INI pairs a manifest names in `[ui:boot]`.

```ini
[ui:boot]
chrome=revconfig
revconfig_ui=../revconfig/rev_245_2/rev_245_2_dat1_ui.ini
revconfig_cache=../revconfig/rev_289/rev_289_dat1_cache.ini
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

That is what lets one layout serve several caches. `rev_289` ships a cache file
and no UI file, because 289 did not move the gameframe — it renamed the four
fonts underneath it (`p11` → `p11_full`, a different glyph layout as well as a
different name; see the file's own header). The manifest takes its UI half from
`rev_245_2` and its cache half from `rev_289`, and neither file has to know.

## What is here

| directory | |
|---|---|
| `rev_245_2/` | The LostCity 2004 gameframe (dat1). The UI half of every dat1 world in this tree. |
| `rev_289/` | Cache bindings for LostCity's January-2005 build. Cache half only. |
| `rev_os217/` | OldSchool 217-era (dat2). |
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
