# Tombs of Amascut

Research and implementation plan for the raid, in the shape the Theatre of Blood
work in this tree already proved out.

| File | What it is |
|---|---|
| [`TOMBS_OF_AMASCUT_PLAN.md`](TOMBS_OF_AMASCUT_PLAN.md) | **the plan** — eight steps, what gates what, and the sixteen open questions collected in one table |
| [`ENCOUNTERS.md`](ENCOUNTERS.md) | **the research log** — every room, npc, mechanic, sound family, music track, invocation, point rule and loot row, each tagged with where it came from |
| [`ASSET_INDEX.md`](ASSET_INDEX.md) | **the id catalogue** — the twelve map squares, 183 npcs, 1,357 locs, 86 items, 244 sequences, 946 sounds, 94 varbits, ten interfaces |
| [`SOURCES.md`](SOURCES.md) | where all of it came from, with pinned wiki revisions and the two commands that regenerate everything |
| [`sources/`](sources/) | the evidence itself — 36 wiki pages as raw wikitext, plus the cache extracts |

**Start with the plan.** It cites the other two rather than repeating them.

The one-line summary of the research: **the rev239 cache already contains the
entire raid** — maps, npcs, locs, items, animations, sounds, music, interfaces,
and even the 46-row invocation table as `struct` params. Nothing needs to be
authored. The whole job is server content.

Regenerate the evidence:

```
python3 tools/toa_cache_dump.py OSRS-Content/osrs239-content \
    docs/minigames/tombs_of_amascut/sources
python3 tools/toa_fetch_wiki.py docs/minigames/tombs_of_amascut/sources \
    --combat-achievements
```

Check the docs against it — **39 checks**, covering the twelve map squares, the
invocation table's 600 ceiling and its prerequisites, every npc / loc / obj / seq
id these documents quote, the twelve music archives, and the Combat Achievement
id range:

```
python3 tools/toa_verify_docs.py
```

It is proven able to fail: changing one quoted npc id to a nonexistent one is
caught as `npc ids quoted in the docs but absent from the cache dump`, and
lowering `Insanity` from +50 to +45 in the invocation dump is caught as
`raid-level ceiling is 595, wiki says 600`.
