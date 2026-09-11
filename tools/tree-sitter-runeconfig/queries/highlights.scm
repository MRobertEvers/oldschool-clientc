; RuneConfig highlighting.
;
; As with the script grammar, the VS Code extension takes semantic tokens from
; the LSP instead — it can tell `bronze_bar` in `data=bar,bronze_bar` from the
; prose in `data=processmessage,You smelt the …`, which needs the workspace
; index and the key's meaning, not the syntax.

(record
  "[" @punctuation.bracket
  name: (record_name) @type.definition
  "]" @punctuation.bracket)

(property key: (name) @property)
(constant_definition name: (constant) @constant.macro)
(id_binding id: (number) @number)
(id_binding name: (value_item) @type.definition)

(spawn_row name: (name) @function)
(section_marker) @keyword

(value_item (name) @variable)
(value_item (number) @number)
(value_item (coord) @number)
(value_item (text) @string)

(comment) @comment @spell

"=" @operator
"," @punctuation.delimiter

(ERROR) @error
