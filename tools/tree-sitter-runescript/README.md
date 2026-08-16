# `tree-sitter-runescript`

A tree-sitter grammar for RuneScript — the language of `.rs2` (ServerScript)
and `.cs2` (ClientScript). Both extensions are the same grammar; only the
command vocabulary differs, which is a name-resolution question rather than a
syntactic one, and the LSP answers it from the file extension.

## Building

```bash
make -C tools/runescript-lsp grammars    # regenerate both parsers
npx tree-sitter-cli@0.26.12 test         # the corpus in test/
```

`src/parser.c` is checked in, so building the server needs no Node.

## What it is checked against

The repo's own corpus, which is the only test that matters for a grammar meant
to read it:

```bash
npx tree-sitter-cli@0.26.12 parse --quiet --stat \
    '../../OSRS-Content/osrs239-content/server/scripts/**/*.rs2'
npx tree-sitter-cli@0.26.12 parse --quiet --stat \
    '../../OSRS-Content/osrs239-content/scripts/*.cs2'
```

| corpus | files | parsed |
| --- | --- | --- |
| `.rs2` ServerScript | 2,400 | 100% |
| `.cs2` ClientScript | 9,368 | 100% |
| ported overlays | 20 | 100% |

## The parts that took work

The grammar follows `src/serverscript/ssc_lex.c` rather than inventing its own
rules, because the awkward parts of this language are all lexical.

**Names are not identifiers.** They may contain `+` and `-`
(`premade_cheese+tom_batta`, `godwars_godsword_blade1+2`, `antidote++`) — the
same characters `calc()` uses as operators, told apart by spacing. They may
carry one `:` joining two halves (`multi2:com_1`, `interface_774:48`, where the
second half is sometimes a number). They may begin with digits
(`3dose1strength`) or be nothing but underscores and digits (`_222`, a
decompiled client opcode with no known name).

**A coord opens like an integer.** `0_49_50_3_11` is level_mx_mz_lx_lz packed
into one int; `0_38_53` is a three-part map square; `0_41_53_compofishspot` is a
*name* that opens exactly like a coord. Longest-match is what tells the three
apart.

**Strings carry content in angle brackets.** `<$name>` and `<tostring($n)>` are
interpolations the compiler expands, `<br>` and `<p,happy>` are markup the
client renders, and they nest: `"::pray <0-<calc(^prayer_count - 1)>>"`. An
interpolation may hold a quoted string of its own —
`<text_gender("man", "woman")>` — so a bare `"` only ends the literal at
bracket depth zero.

**Braces are optional around a single statement.** `if (random(4) ! 0) return;`
is idiomatic and common.

**Binary arithmetic is a value-position thing, not a general one.** Modelling it
that way is what keeps `&` and `|` unambiguous between arithmetic and the
condition combinators — a comparison's two operands are the one place they
cannot appear. ServerScript is stricter still (`ssc_compile.c` reaches
`parse_calc_term` only from `calc()`), but the decompiled client corpus writes
`calc($a - foo($b - $c))`, and an editor grammar that rejects real files is
worth less than a permissive one.

## Queries

`queries/highlights.scm`, `locals.scm` and `injections.scm` are for editors
that read tree-sitter queries directly (Neovim, Helix, Zed). VS Code does not:
it takes semantic tokens from the LSP, which resolves names against the
workspace index and can therefore tell an obj from an npc from a command —
something no purely syntactic query can do.
