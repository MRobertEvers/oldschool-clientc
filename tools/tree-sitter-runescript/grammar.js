/**
 * RuneScript — the language of `.rs2` (ServerScript) and `.cs2` (ClientScript).
 *
 * Both extensions are the same grammar; only the command vocabulary differs
 * (server opcodes vs client opcodes), which is a name-resolution question, not
 * a syntactic one. The LSP tells them apart by file extension.
 *
 * The awkward parts of this language are all lexical, and this grammar follows
 * `src/serverscript/ssc_lex.c` rather than inventing its own rules:
 *
 *   - five sigils bind to the following name and change what it means
 *     ($local, %var, ^constant, ~proc, @label);
 *   - names may contain '+' and '-' ("premade_cheese+tom_batta",
 *     "antidote++"), the same characters calc() uses as operators — they are
 *     told apart by spacing, which no calc() expression in the corpus violates;
 *   - names may carry one ':' joining two halves ("multi2:com_1");
 *   - names may begin with digits ("3dose1strength") or be all-underscore-and-
 *     digits ("_222", a decompiled client opcode with no known name);
 *   - coord literals ("0_49_50_3_11") open exactly like an integer, and
 *     "0_41_53_compofishspot" opens exactly like a coord;
 *   - string literals carry <interpolation> and <markup>, where the angle
 *     brackets are content rather than syntax, and an interpolation may hold a
 *     quoted string of its own.
 *
 * Binary arithmetic exists only inside calc(); that is a real property of the
 * language (ssc_compile.c's parse_calc_term is reachable from nowhere else),
 * and modelling it keeps '&' and '|' unambiguous between arithmetic and the
 * condition combinators.
 */

// A name, matching read_ident()/looks_like_identifier() in ssc_lex.c.
//
//   letter_led      if_close, _222, bow_string
//   digit_led       3dose1strength, 0_41_53_compofishspot (needs a letter to
//                   be a name at all — without one it is a coord or an int)
//   sign runs       premade_cheese+tom_batta, godsword_blade1+2, antidote++
//   qualified       multi2:com_1, interface_774:48, interface_4:6 (the half
//                   after the colon is a component, named or numbered)
//   leading dot     .chatnpc (47 scripts really are named that), .npc_say
const NAME_CORE = '(?:[A-Za-z_][A-Za-z0-9_]*|[0-9][0-9_]*[A-Za-z][A-Za-z0-9_]*)';
const NAME_SIGNS = '(?:[+-]+[A-Za-z0-9_]+)*[+-]*';
const NAME_QUALIFIER = '(?::[A-Za-z0-9_]+)?';
const NAME = new RegExp('\\.?' + NAME_CORE + NAME_SIGNS + NAME_QUALIFIER);

/** `sigil` glued to a name with no space between, as one token. */
const sigiled = (sigil) => new RegExp('\\' + sigil + NAME.source);

const commaSep1 = (rule) => seq(rule, repeat(seq(',', rule)));
const commaSep = (rule) => optional(commaSep1(rule));

module.exports = grammar({
  name: 'runescript',

  extras: ($) => [/[\s﻿]/, $.comment],

  word: ($) => $.identifier,

  supertypes: ($) => [$._statement, $._expression],

  rules: {
    source_file: ($) => repeat($.script),

    // ----------------------------------------------------------------
    // Scripts
    // ----------------------------------------------------------------

    // `[trigger,subject]`, then the two optional signature lists. A return
    // list never appears without an argument list, so nesting them this way
    // is what makes the empty `()` case unambiguous.
    script: ($) =>
      seq(
        field('header', $.script_header),
        optional(
          seq(
            field('parameters', $.parameter_list),
            optional(field('returns', $.return_list)),
          ),
        ),
        repeat($._statement),
      ),

    script_header: ($) =>
      seq(
        '[',
        field('trigger', $.trigger),
        ',',
        field('subject', $.subject),
        // `[command,queue*]` declares the vararg form; the star is part of
        // the declaration's name.
        optional('*'),
        ']',
      ),

    trigger: ($) => $.identifier,

    // `_` for a global trigger, a name, or a coord — `[mapzoneexit,0_49_46]`
    // is addressed by a coord that lexes as neither a name nor an int.
    subject: ($) => choice($.identifier, $.coord, $.number),

    parameter_list: ($) => seq('(', commaSep($.parameter), ')'),

    parameter: ($) => seq(field('type', $.type), field('name', $.local)),

    // Return values may be named — `[proc,x]()(int $found, int $type)` — even
    // though the format stores no return arity and the compiler discards the
    // list. The names are load-bearing documentation, so they are parsed.
    return_list: ($) => seq('(', commaSep($.return_value), ')'),

    return_value: ($) =>
      seq(field('type', $.type), optional(field('name', $.local))),

    type: ($) => $.identifier,

    // ----------------------------------------------------------------
    // Statements
    // ----------------------------------------------------------------

    _statement: ($) =>
      choice(
        $.if_statement,
        $.while_statement,
        $.switch_statement,
        $.return_statement,
        $.declaration,
        $.assignment,
        $.expression_statement,
        $.empty_statement,
        // A script body is sometimes wrapped in braces of its own, and a
        // bare block is a statement wherever one is legal.
        $.block,
      ),

    block: ($) => seq('{', repeat($._statement), '}'),

    /** Braces are optional around a single statement — `if (x = 1) return;`
     *  is idiomatic and common, and ssc_compile.c's parse_block allows it.
     *  A braced body is just the block statement. */
    _body: ($) => $._statement,

    // prec.right settles the dangling else: an `else` binds to the nearest
    // unclosed `if`, which is what the compiler's recursive parse_if does.
    if_statement: ($) =>
      prec.right(seq(
        'if',
        '(',
        field('condition', $._condition),
        ')',
        field('consequence', $._body),
        optional(seq('else', field('alternative', $._body))),
      )),

    while_statement: ($) =>
      seq(
        'while',
        '(',
        field('condition', $._condition),
        ')',
        field('body', $._body),
      ),

    // `switch_int`, `switch_obj`, `switch_npc`, ... — the suffix is the type
    // of the subject, which is how the compiler recognises the statement.
    switch_statement: ($) =>
      seq(
        field('keyword', $.switch_keyword),
        '(',
        field('value', $._calc_expression),
        ')',
        '{',
        repeat($.switch_case),
        '}',
      ),

    switch_case: ($) =>
      seq(
        'case',
        choice(field('default', 'default'), field('value', commaSep1($._case_value))),
        ':',
        repeat($._statement),
      ),

    _case_value: ($) =>
      choice($.number, $.coord, $.constant, $.identifier, $.string, $.boolean),

    // The trailing ';' is optional but always binds to the statement rather
    // than opening an empty one — prec.right is what says so.
    return_statement: ($) =>
      prec.right(seq(
        'return',
        optional(seq('(', commaSep($._calc_expression), ')')),
        optional(';'),
      )),

    // `def_int $x = 5;`, `def_string $s;`, and the array form
    // `def_int $arr($size);` — the parenthesised value is the length.
    declaration: ($) =>
      prec.right(seq(
        field('keyword', $.def_keyword),
        field('name', $.local),
        optional(seq('(', field('length', $._calc_expression), ')')),
        optional(seq('=', field('value', $._calc_expression))),
        optional(';'),
      )),

    // `$x = 1;`, `$x, $z = ~door_coords();`, `$arr($i) = 1;`, `%varp = 1;`,
    // `.%secondary = 1;`
    //
    // Both sides are lists, and they need not be the same length: one call
    // can fill several targets (`$x, $z = ~door_coords()`), and several
    // values can fill several targets (`$s1, $s2 = "", ""`).
    assignment: ($) =>
      prec.right(seq(
        field('left', commaSep1($._assign_target)),
        '=',
        field('right', commaSep1($._calc_expression)),
        optional(';'),
      )),

    _assign_target: ($) => choice($.array_ref, $.local, $.variable_ref),

    // A command with no arguments is written bare — `if_close;`, `_222;`.
    // Nothing syntactic separates that from a name, so it is only a command
    // call in statement position; in expression position a bare name is an
    // `identifier` and the LSP decides whether it names a command or a cache
    // symbol, exactly as ssc_compile.c's parse_command does.
    expression_statement: ($) =>
      prec.right(
        seq(
          choice($._call, $.vararg_command_call, $.bare_command_call),
          optional(';'),
        ),
      ),

    bare_command_call: ($) => field('name', $.identifier),

    empty_statement: ($) => ';',

    // ----------------------------------------------------------------
    // Conditions
    // ----------------------------------------------------------------

    _condition: ($) =>
      choice($.comparison, $.condition_and, $.condition_or, $.condition_group),

    condition_group: ($) => seq('(', $._condition, ')'),

    comparison: ($) =>
      seq(
        field('left', $._expression),
        field('operator', choice('=', '!', '!=', '<', '>', '<=', '>=')),
        field('right', $._expression),
      ),

    condition_and: ($) => prec.left(2, seq($._condition, '&', $._condition)),

    condition_or: ($) => prec.left(1, seq($._condition, '|', $._condition)),

    // ----------------------------------------------------------------
    // Expressions
    // ----------------------------------------------------------------

    _expression: ($) =>
      choice(
        $.number,
        $.coord,
        $.string,
        $.boolean,
        $.null,
        $.array_ref,
        $.local,
        $.variable_ref,
        $.constant,
        $.calc,
        $._call,
        $.identifier,
        $.parenthesized_expression,
      ),

    // A paren group is the one place a binary operator can appear outside
    // calc()'s own argument, because calc((1 + 2) * 3) nests one.
    parenthesized_expression: ($) => seq('(', $._calc_expression, ')'),

    _call: ($) => choice($.command_call, $.proc_call, $.label_call),

    command_call: ($) =>
      seq(field('name', $.identifier), field('arguments', $.argument_list)),

    // `queue*(script, delay)(arg, arg)` — a different opcode from `queue`,
    // not a modifier on it. Only ever a statement, which is what keeps the
    // star out of calc()'s multiplication.
    vararg_command_call: ($) =>
      seq(
        field('name', $.identifier),
        '*',
        field('arguments', $.argument_list),
        optional(field('vararg_arguments', $.argument_list)),
      ),

    proc_call: ($) =>
      seq(field('name', $.proc), optional(field('arguments', $.argument_list))),

    label_call: ($) =>
      seq(field('name', $.label), optional(field('arguments', $.argument_list))),

    argument_list: ($) => seq('(', commaSep($._calc_expression), ')'),

    array_ref: ($) =>
      seq(field('name', $.local), '(', field('index', $._calc_expression), ')'),

    // `.%var` reads the variable off the secondary pointer.
    variable_ref: ($) => seq(optional('.'), $.variable),

    // ----------------------------------------------------------------
    // calc() — the only place binary arithmetic exists
    // ----------------------------------------------------------------

    calc: ($) => seq('calc', '(', $._calc_expression, ')'),

    // Every value position accepts arithmetic; only a comparison's two
    // operands do not, which is what keeps '&' and '|' unambiguous between
    // arithmetic and the condition combinators. ServerScript is stricter than
    // this — ssc_compile.c reaches parse_calc_term only from calc() — but the
    // decompiled client corpus writes `calc($a - foo($b - $c))`, and an editor
    // grammar that rejects real files is worth less than a permissive one.
    _calc_expression: ($) => choice($._expression, $.binary_expression),

    binary_expression: ($) =>
      choice(
        prec.left(
          3,
          seq(
            field('left', $._calc_expression),
            field('operator', choice('*', '/', '%')),
            field('right', $._calc_expression),
          ),
        ),
        prec.left(
          2,
          seq(
            field('left', $._calc_expression),
            field('operator', choice('+', '-')),
            field('right', $._calc_expression),
          ),
        ),
        prec.left(
          1,
          seq(
            field('left', $._calc_expression),
            field('operator', choice('&', '|')),
            field('right', $._calc_expression),
          ),
        ),
      ),

    // ----------------------------------------------------------------
    // Literals and names
    // ----------------------------------------------------------------

    // Angle brackets inside a string are content: <$name> and <tostring($n)>
    // are interpolations the compiler expands, <br> and <p,happy> are markup
    // the client renders. An interpolation may carry a quoted string of its
    // own — <text_gender("man", "woman")> — so the closing quote only counts
    // at bracket depth zero.
    string: ($) =>
      seq('"', repeat(choice($.string_text, $.string_interpolation)), '"'),

    string_text: (_) => token.immediate(prec(1, /[^"<]+/)),

    string_interpolation: ($) =>
      seq(
        token.immediate('<'),
        repeat(choice($.interpolation_body, $.string_interpolation)),
        token.immediate('>'),
      ),

    interpolation_body: (_) => token.immediate(/[^<>]+/),

    // `0_49_50_3_11` is level_mx_mz_lx_lz, packed into one int by the
    // compiler. It starts like an integer and only reveals itself at the
    // first underscore, so the longest match is what tells the two apart.
    //
    // Fewer than five parts is a real shape too: `[mapzone,0_38_53]` names a
    // map square, and the compiler's read_number keeps only the first part's
    // value for those — they are addressed by name, not by key.
    coord: (_) => token(/[0-9]+(_[0-9]+)+/),

    number: (_) => token(choice(/0[xX][0-9a-fA-F]+/, /-?[0-9]+/)),

    boolean: (_) => choice('true', 'false'),

    null: (_) => 'null',

    def_keyword: (_) => token(prec(1, /def_[a-z_0-9]+/)),

    switch_keyword: (_) => token(prec(1, /switch_[a-z_0-9]+/)),

    identifier: (_) => token(NAME),

    local: (_) => token(sigiled('$')),

    variable: (_) => token(sigiled('%')),

    constant: (_) => token(sigiled('^')),

    proc: (_) => token(sigiled('~')),

    label: (_) => token(sigiled('@')),

    comment: (_) =>
      token(choice(seq('//', /[^\r\n]*/), seq('/*', /[^*]*\*+([^/*][^*]*\*+)*/, '/'))),
  },
});
