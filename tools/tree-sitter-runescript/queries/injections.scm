; A string interpolation carries RuneScript of its own — <tostring($count)>,
; <text_gender("man", "woman")> — so it parses as this same grammar.
;
; Markup interpolations (<br>, <p,happy>, <col=ff0000>) parse as an
; `identifier` or fail; that is why the injection is `combined` and its errors
; are tolerated rather than surfaced. The LSP does its own sub-tokenisation of
; interpolation bodies and does not rely on this.

((interpolation_body) @injection.content
  (#match? @injection.content "[($]")
  (#set! injection.language "runescript")
  (#set! injection.include-children))
