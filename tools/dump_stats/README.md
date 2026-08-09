# dump_stats — every npc and obj record in a dat2 cache, as CSV

```sh
make -C tools/dump_stats

tools/dump_stats/dump_stats --rev rs727 cache.rs727_preeoc \
    --npc-csv out/rs727/npc_stats.csv \
    --obj-csv out/rs727/obj_stats.csv
```

Works for any dat2 cache `3rd/rscache` has a profile for (`--rev osrs239`,
`--rev 643`, …), or a stated identity (`--game rs2 --epoch dat2 --revision 727`).
`--npc-only` / `--obj-only` skip a family; `--raw-dir DIR` also writes the
undecoded record bytes as `DIR/{npc,obj}.bin` plus an `(id offset size)` index,
which is what the opcode work below needed.

Records are walked from each type's own reference table rather than a `0..max`
sweep: config groups are sparse once a revision has removed content, and a sweep
decodes a lot of absent records and reports their misses as failures.

## Reading the output

Three columns per row say how far the decode got:

| Column | Meaning |
|---|---|
| `record_bytes` | the record's size in the cache |
| `decoded_bytes` | how much the decoder consumed |
| `stop_opcode` | `-1` when the record ended cleanly on opcode 0; otherwise the opcode that stopped it |

A row with `stop_opcode` other than `-1` is truthful up to `decoded_bytes` and
empty after it. The decoders stop at the first opcode they do not know rather
than guessing a payload length, because an unknown length makes every later read
land inside that payload and fill real fields with garbage — so a short row is a
row to filter out, not a row to distrust field by field.

`params` holds opcode 249's key/value map as `key=value;key=value`. On a pre-EoC
cache that is where anything the struct has no field for lives, **including
equipment bonuses** — objs carry them there, npcs do not carry combat stats in
the cache at all (only `combat_level` and `size`). The `stat_74`…`stat_79`
columns are the OldSchool-only npc opcodes 74-79 and stay at their default of 1
on an RS2 cache.

Strings are transcoded from the cache's windows-1252 to UTF-8; written straight
out they make a file no CSV reader will open, since real names carry bytes like
0x92.

## check_727_opcodes.py

```sh
tools/dump_stats/dump_stats --rev rs727 cache.rs727_preeoc --raw-dir /tmp/raw727
python3 tools/dump_stats/check_727_opcodes.py /tmp/raw727 npc
python3 tools/dump_stats/check_727_opcodes.py /tmp/raw727 obj
```

The independent check on the rev-727 opcode table that
`3rd/rscache/src/revisions/rev_dat2_rs727.c` pins. It holds the same table in
Python and reports what fraction of records consume exactly — 100% of objs,
99.81% of npcs. Keep it in step with the C decoder; it is the cheap way to test a
change to that codec without rebuilding, and the only thing that would catch the
two drifting apart.
