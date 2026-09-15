/**
 * RuneConfig — the content tree's declaration files.
 *
 * One grammar covers the whole family, because they are one format wearing
 * different extensions:
 *
 *   record files   .npc .obj .loc .inv .enum .struct .param .seq .spotanim
 *                  .varp .dbtable .dbrow .if .mesanim .idk .flo .flu .frm
 *                  `[name]` opens a record, `key=value` fills a field.
 *   allocations    .alloc `id=name`, the server's own id ledger
 *   name indexes   .pack .compack .memberpack .filepack `id=name`, the cache's
 *   constants      .constant `^name = value`
 *   spawns         .spawn `==== NPC ====` sections of whitespace columns
 *
 * The value side is deliberately shallow. A value is a comma-separated list of
 * items, and an item is either a name-shaped token or free text — `name=Tool
 * Leprechaun` and `data=levelfailure,You need a Smithing level of 13 …` are
 * both ordinary. Splitting further would mean deciding per key what the field
 * means, which is the LSP's job (it has the workspace index); the grammar's
 * job is to say where the tokens are.
 */

const NAME_CORE = '(?:[A-Za-z_][A-Za-z0-9_]*|[0-9][0-9_]*[A-Za-z][A-Za-z0-9_]*)';
const NAME = new RegExp(
  '\\.?' + NAME_CORE + '(?:[+-]+[A-Za-z0-9_]+)*[+-]*(?::[A-Za-z0-9_]+)?',
);

module.exports = grammar({
  name: 'runeconfig',

  // Newlines end a line, so they are not trivia.
  extras: (_) => [/[ \t\r﻿]/],

  externals: (_) => [],

  rules: {
    source_file: ($) => repeat($._line),

    _line: ($) =>
      choice(
        $.comment,
        $.record,
        $.constant_definition,
        $.id_binding,
        $.property,
        $.section_marker,
        $.spawn_row,
        $._newline,
      ),

    _newline: (_) => token(/\r?\n/),

    comment: (_) => token(seq('//', /[^\r\n]*/)),

    // ----------------------------------------------------------------
    // Records — `[block_name]`, then the fields that fill it
    // ----------------------------------------------------------------

    record: ($) => seq('[', field('name', $.record_name), ']'),

    record_name: ($) => choice($.name, $.number, $.coord),

    // `key=value`, `key=` (an explicitly empty field, which .if writes), and
    // the repeated forms (`data=`, `val=`, `param=`, `frame=`) that make a
    // list out of several lines.
    //
    // The key is lexed as an ordinary name so that a spawn row's leading name
    // is the same token: which of the two a line is only becomes clear at the
    // '=', and giving the key its own token would make the lexer decide one
    // token too early.
    property: ($) =>
      seq(field('key', $.name), '=', optional(field('value', $.value)), $._newline),

    // Items may be empty, and a trailing comma is ordinary — `valstr=0,`,
    // `param=param_1111,str,`, `condop=3,65535,20251,0,63,`.
    value: ($) =>
      choice(
        seq($.value_item, repeat(seq(',', optional($.value_item)))),
        repeat1(seq(',', optional($.value_item))),
      ),

    // A value item is a name, a number, a coord, or free prose. The three
    // typed shapes are what go-to-definition can follow; the prose shape is
    // everything a message field holds.
    value_item: ($) => choice($.coord, $.number, $.name, $.text),

    // ----------------------------------------------------------------
    // Allocations and name indexes — `id=name`
    // ----------------------------------------------------------------

    // pack/varp.alloc: `5727=bankpin_code`. The id leads, which is what tells
    // this apart from a property (whose key always carries a letter).
    //
    // The bound name is not always name-shaped: an overlay tree's pack lines
    // carry a path (`26000=ported/rs558_ancient_curses/curses_animset_2998`)
    // and a component index can bind a numeric name (`4=01`).
    id_binding: ($) =>
      seq(field('id', $.number), '=', optional(field('name', $.value_item)), $._newline),

    // ----------------------------------------------------------------
    // Constants — `^name = value`
    // ----------------------------------------------------------------

    // `^dm_default = 5`. The '=' is required: every constant in the corpus
    // writes one, and making it optional would let the value's free-text
    // token swallow the '=' itself (it is the longer match).
    constant_definition: ($) =>
      seq(
        field('name', $.constant),
        '=',
        optional(field('value', $.value)),
        $._newline,
      ),

    // ----------------------------------------------------------------
    // Spawn files
    // ----------------------------------------------------------------

    // `==== NPC ====` / `==== LOC ====` / `==== OBJ ====`
    //
    // Matched to the end of the line rather than to the closing run of '=':
    // a bounded pattern accepts at the OPENING run already ("==" + "" + "=="
    // is a whole match of "===="), which strands the rest of the marker as a
    // property.
    section_marker: (_) => token(prec(2, /={2,}[^\r\n]*/)),

    // `snakeboss_fisherman   2189  3069 0` — a name and its columns.
    spawn_row: ($) =>
      seq(field('name', $.name), repeat1($.spawn_column), $._newline),

    spawn_column: ($) => choice($.coord, $.number, $.name),

    // ----------------------------------------------------------------
    // Tokens
    // ----------------------------------------------------------------

    coord: (_) => token(/[0-9]+(_[0-9]+)+/),

    number: (_) => token(/-?[0-9]+/),

    name: (_) => token(NAME),

    constant: (_) => token(new RegExp('\\^' + NAME.source)),

    // Anything else that can sit in a value: prose, punctuation, a path, a
    // sentence. It stops at a comma or a newline, which are the only two
    // characters the value syntax itself uses.
    //
    // No token precedence here on purpose: tree-sitter weighs explicit
    // precedence ahead of match length, so a negative one would make `name`
    // win the first word of `name=Tool Leprechaun` and strand the rest. With
    // both at zero the longest match wins, which is the rule that wants
    // applying — and a value that is exactly name-shaped ties on length and
    // falls to `name`, declared first.
    text: (_) => token(/[^,\r\n]+/),
  },
});
