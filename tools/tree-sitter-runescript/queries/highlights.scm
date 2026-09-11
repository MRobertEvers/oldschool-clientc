; RuneScript highlighting.
;
; These are the tree-sitter capture names (nvim-treesitter / helix / zed read
; this file directly). The VS Code extension does not: it takes semantic tokens
; from the LSP, which classifies names against the workspace index and can
; therefore tell an obj from an npc from a command — something no purely
; syntactic query can do. Keep the two in agreement where they overlap.

; ------------------------------------------------------------------
; Script headers
; ------------------------------------------------------------------

(script_header
  "[" @punctuation.bracket
  trigger: (trigger) @keyword.function
  ","  @punctuation.delimiter
  subject: (subject) @function
  "]" @punctuation.bracket)

(parameter type: (type) @type)
(return_value type: (type) @type)

; ------------------------------------------------------------------
; Keywords and control flow
; ------------------------------------------------------------------

[
  "if"
  "else"
  "while"
  "case"
  "default"
  "return"
] @keyword

(def_keyword) @keyword
(switch_keyword) @keyword.conditional
"calc" @function.builtin

; ------------------------------------------------------------------
; Names, by sigil
; ------------------------------------------------------------------

(local) @variable
(variable) @variable.builtin       ; %varp / %varbit / %varn / %vars
(constant) @constant
(proc) @function
(label) @label

(command_call name: (identifier) @function.builtin)
(bare_command_call name: (identifier) @function.builtin)
(vararg_command_call name: (identifier) @function.builtin)
(proc_call name: (proc) @function)
(label_call name: (label) @label)

; A bare name in an expression is a cache symbol (an obj, an npc, a loc, an
; interface, a category, ...). Which one is a workspace question, so the query
; can only say "not a local".
(identifier) @variable.parameter

; ------------------------------------------------------------------
; Literals
; ------------------------------------------------------------------

(number) @number
(coord) @number
(boolean) @boolean
(null) @constant.builtin

(string) @string
(string_text) @string
(string_interpolation) @string.special
(interpolation_body) @embedded

(comment) @comment @spell

; ------------------------------------------------------------------
; Operators and punctuation
; ------------------------------------------------------------------

(comparison operator: _ @operator)
(binary_expression operator: _ @operator)

[
  "&"
  "|"
] @operator

[
  "("
  ")"
  "{"
  "}"
  "["
  "]"
] @punctuation.bracket

[
  ","
  ";"
  ":"
  "="
] @punctuation.delimiter

(ERROR) @error
