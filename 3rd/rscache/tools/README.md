# rscache tools

Offline utilities that sit on top of `3rd/rscache` for discovering and porting
assets between cache revisions.

## Build

```sh
make -C 3rd/rscache tools
# binaries land in 3rd/rscache/tools/find_anims/find_anims
#                 and 3rd/rscache/tools/port_npc/port_npc
```

Or from this directory:

```sh
make
```

## `find_anims`

Identify every animation that might apply to an NPC's models by walking from the
NPC's base animation set (opcodes 13/14 and, on RS2, BasType via opcode 127) to
the framemap shared by those sequences, then collecting every other sequence
that references the same framemap.

```sh
# dat2
./find_anims/find_anims --rev osrs230 ../../cache.osrs230 --npc 1
./find_anims/find_anims --rev rs643 ../../cache.rs643 --npc 7343 --strict --json

# dat1
./find_anims/find_anims --rev lc254 ../../cache254 --npc 1
```

Flags: `--npc ID`, `--model ID`, `--seq ID` (exactly one), `--strict` (drop
candidates whose framemap does not cover the model's bone labels), `--json`.

## `port_npc`

Port an NPC and its asset closure (models, sequences, frames, framemaps, and
optionally related animations / BasType) between cache revisions. Dry-run by
default; pass `--apply` to write.

```sh
./port_npc/port_npc \
  --from-rev osrs230 ../../cache.osrs230 \
  --to-rev rs643 ../../cache.rs643 \
  --npc 1 --out /tmp/cache.out --apply
```

Ids are kept when free in the destination and remapped on collision. See the
tool's `--help` for `--include-related-anims`, `--emit-bas`, `--strict-models`,
and `--texture-map`.

Revision 239 NPC records are refused: the decoder does not yet consume them
exactly (see `3rd/rscache/README.md` known decode gaps).
