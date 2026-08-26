#!/usr/bin/env python3
"""Contract tests for the shared C/TypeScript opcode-semantics foundation."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))

import gen_opcodes as generator  # noqa: E402
from opcode_semantics import (  # noqa: E402
    FOUNDATION_OPCODE_NAMES,
    OPCODE_SEMANTICS,
    OperandKind,
    StackEffectKind,
    validate_opcode_semantics,
)


class OpcodeSemanticsContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.entries = generator.merge_local(generator.parse_opcodes(generator.VENDOR))

    def test_schema_matches_opcode_table_and_production_dispatch(self) -> None:
        validate_opcode_semantics(
            self.entries,
            generator.DISPATCH_SOURCE,
            generator.operand_kind,
            generator.handler_kind,
        )

    def test_foundation_coverage_is_exact_and_sorted(self) -> None:
        self.assertEqual(
            {semantic.name for semantic in OPCODE_SEMANTICS},
            set(FOUNDATION_OPCODE_NAMES),
        )
        self.assertEqual(
            [semantic.opcode for semantic in OPCODE_SEMANTICS],
            sorted(semantic.opcode for semantic in OPCODE_SEMANTICS),
        )

    def test_control_flow_contract_matches_current_c_vm(self) -> None:
        by_name = {semantic.name: semantic for semantic in OPCODE_SEMANTICS}
        # Opcode 7 is the traditional integer-not-equal branch.  A stale prose
        # comment once described it as unary false; the executable schema must
        # preserve the C/reference client's two-pop behavior.
        self.assertEqual(by_name["BRANCH_NOT"].int_pops, ("lhs", "rhs"))
        self.assertEqual(by_name["BRANCH_NOT"].intrinsic, "branchIntNotEquals")
        self.assertEqual(by_name["RETURN"].int_pops, ())
        self.assertEqual(by_name["RETURN"].string_pops, ())

    def test_operand_and_signed_arithmetic_contracts_are_explicit(self) -> None:
        by_name = {semantic.name: semantic for semantic in OPCODE_SEMANTICS}
        self.assertEqual(by_name["PUSH_CONSTANT_INT"].operand, OperandKind.INT32)
        self.assertEqual(by_name["PUSH_CONSTANT_STRING"].operand, OperandKind.STRING)
        for name in ("ADD", "SUB", "MULTIPLY", "DIV", "MOD"):
            with self.subTest(name=name):
                semantic = by_name[name]
                self.assertEqual(semantic.operand, OperandKind.INT8)
                self.assertEqual(semantic.int_pops, ("lhs", "rhs"))
                self.assertFalse(semantic.may_yield)

    def test_pure_corpus_rows_are_explicit_without_promoting_null_stub(self) -> None:
        by_name = {semantic.name: semantic for semantic in OPCODE_SEMANTICS}
        promoted = {
            "MOVECOORD",
            "POW",
            "APPEND",
            "LOWERCASE",
            "TOSTRING",
            "COMPARE",
            "ESCAPE",
            "STRING_LENGTH",
            "SUBSTRING",
            "REMOVETAGS",
            "STRING_INDEXOF_STRING",
            "ON_MOBILE",
            "CLIENTTYPE",
            "ARRAY_LENGTH",
        }
        self.assertTrue(promoted.issubset(by_name))
        self.assertNotIn("PUSH_CONSTANT_NULL", by_name)
        for name in promoted:
            with self.subTest(name=name):
                semantic = by_name[name]
                self.assertEqual(semantic.operand, OperandKind.INT8)
                self.assertFalse(semantic.may_yield)

    def test_wire_operand_catalog_matches_native_decode_order(self) -> None:
        # clientscript.c handles these cases before consulting either metadata
        # table.  In particular, historical INT32 table overrides for command
        # opcodes are unreachable and must not leak into the TypeScript reader.
        self.assertEqual(generator.wire_operand_kind(3), "string")
        self.assertEqual(generator.wire_operand_kind(0), "int32")
        self.assertEqual(generator.wire_operand_kind(21), "int8")
        self.assertEqual(generator.wire_operand_kind(61), "int64")
        self.assertEqual(generator.wire_operand_kind(62), "int8")
        self.assertEqual(generator.wire_operand_kind(63), "int8")
        for opcode in (100, 3170, 3171, 3172, 3173, 4122, 7463):
            with self.subTest(opcode=opcode):
                self.assertEqual(generator.wire_operand_kind(opcode), "int8")

        wire = generator.emit_wire_opcodes_ts(self.entries)
        self.assertIn(
            '{ opcode: 3170, name: "LOCAL_NOTIFICATION", operand: "int8" }', wire
        )
        self.assertIn(
            '{ opcode: 61, name: "PUSH_CONSTANT_LONG", operand: "int64" }', wire
        )
        self.assertIn(
            '{ opcode: 6758, name: "_6758", operand: "int8" }', wire
        )
        self.assertIn(
            '{ opcode: 6764, name: "_6764", operand: "int8" }', wire
        )
        self.assertIn("51: 60", wire)
        self.assertIn("4500: 6516", wire)

    def test_dynamic_stack_effects_are_not_misreported_as_fixed_counts(self) -> None:
        by_name = {semantic.name: semantic for semantic in OPCODE_SEMANTICS}
        self.assertEqual(
            by_name["JOIN_STRING"].stack_effect,
            StackEffectKind.JOIN_STRING_COUNT,
        )
        self.assertEqual(
            by_name["GOSUB_WITH_PARAMS"].stack_effect,
            StackEffectKind.CALLEE_SIGNATURE,
        )
        for name in ("PUSH_ARRAY_INT", "POP_ARRAY_INT"):
            with self.subTest(name=name):
                self.assertEqual(
                    by_name[name].stack_effect,
                    StackEffectKind.ARRAY_ELEMENT_TYPE,
                )

    def test_validation_rejects_c_dispatch_drift(self) -> None:
        source = generator.DISPATCH_SOURCE.read_text(encoding="utf-8")
        changed = source.replace(
            "return CS2VM2_Op_Add(vm, frame, operand);",
            "return CS2VM2_Op_Sub(vm, frame, operand);",
            1,
        )
        self.assertNotEqual(source, changed)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "cs2vm2.c"
            path.write_text(changed, encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "ADD: schema intrinsic maps"):
                validate_opcode_semantics(
                    self.entries,
                    path,
                    generator.operand_kind,
                    generator.handler_kind,
                )

    def test_checked_in_generated_artifacts_are_current(self) -> None:
        expected = {
            generator.OUT_DIR / "cs2_opcode_semantics.gen.h": generator.emit_semantics_h(),
            generator.OUT_DIR / "cs2_opcode_semantics.gen.c": generator.emit_semantics_c(),
            generator.OUT_DIR
            / "cs2vm2_core_dispatch.gen.inc": generator.emit_core_dispatch_inc(),
            generator.CS2DOM_GENERATED_OUT_DIR
            / "cs2_opcode_semantics.ts": generator.emit_semantics_ts(),
            generator.CS2DOM_GENERATED_OUT_DIR
            / "cs2_wire_opcodes.ts": generator.emit_wire_opcodes_ts(self.entries),
        }
        for path, content in expected.items():
            with self.subTest(path=path):
                self.assertEqual(path.read_text(encoding="utf-8"), content)

    def test_generated_c_metadata_and_dispatch_declarations_compile(self) -> None:
        compiler = shutil.which(os.environ.get("CC", "cc"))
        if compiler is None:
            self.skipTest("no C compiler available")
        source = r'''#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2_opcode_semantics.gen.h"

struct DispatchDeclaration
{
    int opcode;
    enum CS2_SemanticsIntrinsic intrinsic;
    char const* handler;
};

#define STRINGIFY_INNER(value) #value
#define STRINGIFY(value) STRINGIFY_INNER(value)
#define CS2_VM_CORE_DISPATCH_ROW(opcode, intrinsic, handler) \
    { opcode, intrinsic, STRINGIFY(handler) },
struct DispatchDeclaration const dispatch[] = {
#include "cs2vm2/cs2vm2_core_dispatch.gen.inc"
};

int main(void)
{
    int const promoted[] = {
        CS2_OP_MOVECOORD,
        CS2_OP_POW,
        CS2_OP_APPEND,
        CS2_OP_LOWERCASE,
        CS2_OP_TOSTRING,
        CS2_OP_COMPARE,
        CS2_OP_ESCAPE,
        CS2_OP_STRING_LENGTH,
        CS2_OP_SUBSTRING,
        CS2_OP_REMOVETAGS,
        CS2_OP_STRING_INDEXOF_STRING,
        CS2_OP_ON_MOBILE,
        CS2_OP_CLIENTTYPE,
        CS2_OP_ARRAY_LENGTH,
    };
    for( unsigned i = 0; i < sizeof(promoted) / sizeof(promoted[0]); i++ )
    {
        struct CS2_OpcodeSemantics const* row =
            CS2_OpcodeSemanticsLookup(promoted[i]);
        if( !row || row->operand != CS2_SEM_OPERAND_INT8 )
            return 5;
    }
    if( CS2_OpcodeSemanticsLookup(CS2_OP_PUSH_CONSTANT_NULL) != 0 )
        return 6;
    struct CS2_OpcodeSemantics const* branch = CS2_OpcodeSemanticsLookup(CS2_OP_BRANCH_NOT);
    if( !branch || branch->int_pop_count != 2 )
        return 1;
    struct CS2_OpcodeSemantics const* join = CS2_OpcodeSemanticsLookup(CS2_OP_JOIN_STRING);
    if( !join || join->stack_effect != CS2_SEM_STACK_EFFECT_JOIN_STRING_COUNT )
        return 4;
    if( CS2_OpcodeSemanticsLookup(999999) != 0 )
        return 2;
    if( (int)(sizeof(dispatch) / sizeof(dispatch[0])) != cs2_opcode_semantics_count )
        return 3;
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            test_c = Path(directory) / "semantics_test.c"
            binary = Path(directory) / "semantics_test"
            test_c.write_text(source, encoding="utf-8")
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{REPO / 'src'}",
                    str(test_c),
                    str(REPO / "src/cs2vm2/cs2_opcode_semantics.gen.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
                cwd=REPO,
            )
            subprocess.run([str(binary)], check=True, cwd=REPO)

    def test_generated_typescript_declarations_typecheck(self) -> None:
        compiler = REPO / "tools/cs2dom/node_modules/.bin/tsc"
        if not compiler.is_file():
            self.skipTest("tools/cs2dom dependencies are not installed")
        subprocess.run(
            [
                str(compiler),
                "--noEmit",
                "--strict",
                "--target",
                "ES2020",
                "--module",
                "ESNext",
                str(generator.CS2DOM_GENERATED_OUT_DIR / "cs2_opcode_semantics.ts"),
            ],
            check=True,
            cwd=REPO,
        )


if __name__ == "__main__":
    unittest.main()
