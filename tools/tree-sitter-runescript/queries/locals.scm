; Local scoping for RuneScript.
;
; A script is the whole scope: RuneScript has no block scoping, and neither
; does the compiler — `def_int` inside an if-block declares a local the rest of
; the script can read (ssc_compile.c's declare_local appends to one flat list
; per script, which is why `def_int` is proc-scoped and not block-scoped).

(script) @local.scope

(parameter name: (local) @local.definition.parameter)
(return_value name: (local) @local.definition.parameter)
(declaration name: (local) @local.definition.var)

(local) @local.reference
