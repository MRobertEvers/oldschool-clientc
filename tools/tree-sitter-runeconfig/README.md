# `tree-sitter-runeconfig`

A tree-sitter grammar for the content tree's declaration files. One grammar
covers the whole family, because they are one format wearing different
extensions:

| shape | files | example |
| --- | --- | --- |
| records | `.npc` `.obj` `.loc` `.inv` `.enum` `.struct` `.param` `.seq` `.spotanim` `.varp` `.varc` `.varbit` `.dbtable` `.dbrow` `.dbi` `.if` `.idk` `.mesanim` `.flo` `.flu` `.hitsplat` `.healthbar` `.mapelement` `.overlay` `.underlay` | `[molanisk]` then `name=Molanisk` |
| allocations | `pack/<ns>.alloc` | `5727=bankpin_code` |
| name indexes | `.pack` `.compack` | `0=npc/royal_dwarf_citizen1_head` |
| membership | `pack/<ns>.client` `.server` | a bare name per line |
| constants | `.constant` | `^dm_default = 5` |
| spawns | `.spawn` | `==== NPC ====` then whitespace columns |

## Building

```bash
make -C tools/runescript-lsp grammars
npx tree-sitter-cli@0.26.12 test
```

## What it is checked against

Every declaration file in the repo — 4,899 of them across nineteen extensions,
all parsed:

```bash
find ../../OSRS-Content -name '*.npc' > /tmp/files.txt
npx tree-sitter-cli@0.26.12 parse --stat --paths /tmp/files.txt | tail -1
```

| extension | files | extension | files |
| --- | --- | --- | --- |
| `.compack` | 1,151 | `.varp` | 299 |
| `.if` | 977 | `.inv` | 275 |
| `.spawn` | 974 | `.dbi` | 147 |
| `.constant` | 375 | `.npc` | 127 |
| …and eleven more | | | **all 100%** |

## The parts that took work

**The value side is deliberately shallow.** A value is a comma-separated list
of items, and an item is either a name-shaped token or free text —
`name=Tool Leprechaun` and
`data=levelfailure,You need a Smithing level of 13 to smelt a blurite bar.` are
both ordinary. Splitting further would mean deciding per key what the field
means, which is the LSP's job (it has the index); the grammar's job is to say
where the tokens are.

**A key is lexed as an ordinary name.** A property line and a spawn row both
open with one, and which a line is only becomes clear at the `=`. Giving the
key its own token made the lexer decide one token too early, and every `.spawn`
file failed.

**`text` carries no token precedence, on purpose.** tree-sitter weighs explicit
precedence ahead of match length, so a negative one made `name` win the first
word of `name=Tool Leprechaun` and stranded the rest. With both at zero the
longest match wins, and a value that *is* name-shaped ties on length and falls
to `name`, which is declared first.

**A section marker matches to end of line.** `={2,}[^\r\n=]*={2,}` accepts at
the opening run already — `"=="` + `""` + `"=="` is a whole match of `"===="` —
which stranded the rest of `==== NPC ====` as a property.

**Empty items and trailing commas are ordinary.** `valstr=0,`,
`param=param_1111,str,`, `condop=3,65535,20251,0,63,`.
