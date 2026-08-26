#!/usr/bin/env python3
"""Focused generator tests for the typed CS2 Host catalog."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from gen_host_schema import (
    build_semantics,
    emit_ts,
    parse,
    parse_command_signatures,
    parse_decode_operands,
    parse_executable_reviews,
    parse_native_wire_rule,
    ts_field_name,
    ts_field_type,
    wire_operand,
)


CS2DOM = HERE.parent
REPO = HERE.parents[2]
REQUESTS = REPO / "src" / "cs2vm2" / "cs2vm2_host_request_kinds.def"
COMMANDS = CS2DOM / "src" / "cs2_commands.js"
OPERANDS = REPO / "src" / "osrs" / "rscache" / "dat2a" / "dat2a_cs2_opcode_decode.c"
NATIVE_DECODER = REPO / "3rd" / "rscache" / "src" / "datatypes" / "clientscript.c"
EXECUTABLE_REVIEWS = HERE / "cs2_host_executable_semantics.json"


class HostSchemaGeneratorTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.requests = parse(REQUESTS)
        cls.commands = parse_command_signatures(COMMANDS)
        cls.operands = parse_decode_operands(OPERANDS)
        cls.wire_rule = parse_native_wire_rule(NATIVE_DECODER)
        cls.executable_reviews = parse_executable_reviews(EXECUTABLE_REVIEWS)
        cls.semantics = build_semantics(
            cls.requests, cls.commands, cls.operands, cls.wire_rule,
            cls.executable_reviews,
        )
        cls.by_name = {item.request.name: item for item in cls.semantics}

    def test_manifest_is_complete_unique_and_classified(self) -> None:
        self.assertEqual(633, len(self.requests))
        self.assertEqual(633, len({request.name for request in self.requests}))
        self.assertEqual(633, len({request.opcode for request in self.requests}))
        self.assertEqual(
            {"none": 221, "read": 216, "topology": 36, "geometry": 29,
             "external": 131},
            {
                barrier: sum(item.barrier == barrier for item in self.semantics)
                for barrier in ("none", "read", "topology", "geometry", "external")
            },
        )
        reviewed = {item.request.opcode for item in self.semantics
                    if item.executable_reviewed}
        self.assertEqual(set(self.executable_reviews), reviewed)
        self.assertEqual(57, len(reviewed))
        self.assertTrue(all(item.request.name == self.executable_reviews[item.request.opcode].name
                            for item in self.semantics if item.executable_reviewed))

    def test_actual_native_wire_operand_precedes_generated_table(self) -> None:
        self.assertEqual("int32", self.by_name["PUSH_VAR"].operand)
        self.assertEqual("int8", self.by_name["CC_CREATE"].operand)
        # The generated decode table says INT32 here, but native reads every
        # opcode >= 100 through its signed-int8 fast path first.
        self.assertEqual("int32", self.operands[3170])
        self.assertEqual("int8", self.by_name["LOCAL_NOTIFICATION"].operand)
        self.assertEqual(100, self.wire_rule.int8_from)
        self.assertEqual(frozenset({21, 38, 39, 62, 63}), self.wire_rule.int8_opcodes)
        self.assertEqual("int8", wire_operand(4122, "int32", self.wire_rule))

    def test_result_provenance_is_not_executable_review(self) -> None:
        push_var = self.by_name["PUSH_VAR"]
        self.assertEqual(("int", "number", "i", "wasm-adapter-override"), (
            push_var.result_kind, push_var.result_type, push_var.stack_output,
            push_var.result_source,
        ))
        create = self.by_name["CC_CREATE"]
        self.assertEqual("component", create.result_kind)
        self.assertEqual("CS2HostComponentRef | null", create.result_type)
        self.assertEqual("wasm-adapter-special-case", create.result_source)
        db_field = self.by_name["DB_GETFIELD"]
        self.assertEqual("db-field", db_field.result_kind)
        self.assertEqual("?", db_field.stack_output)
        self.assertFalse(db_field.executable_reviewed)

    def test_observer_barriers_are_explicit(self) -> None:
        self.assertEqual("none", self.by_name["CC_SETPOSITION"].barrier)
        self.assertEqual("geometry", self.by_name["CC_GETWIDTH"].barrier)
        self.assertEqual("topology", self.by_name["CC_FIND"].barrier)
        self.assertEqual("read", self.by_name["PUSH_VAR"].barrier)
        self.assertEqual("external", self.by_name["SOUND_SYNTH"].barrier)

    def test_c_field_shapes_become_logical_types(self) -> None:
        set_on = next(
            item.request for item in self.semantics if item.request.name == "CC_SETONCLICK"
        )
        fields = {field.name: field for field in set_on.fields}
        self.assertEqual("readonly number[]", ts_field_type(fields["trigger_ids"]))
        self.assertEqual(
            "readonly [low: number, high: number]", ts_field_type(fields["str_arg_mask"])
        )
        self.assertEqual("readonly string[]", ts_field_type(fields["str_args"]))
        component_param = next(
            item.request for item in self.semantics
            if item.request.name == "CC_GETCOMPONENTPARAM"
        )
        kind_field = next(field for field in component_param.fields if field.name == "kind")
        self.assertEqual("value_kind", ts_field_name(kind_field))

    def test_generation_is_deterministic_and_carries_the_review_gate(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            first = Path(directory) / "first.ts"
            second = Path(directory) / "second.ts"
            for output in (first, second):
                emit_ts(
                    self.semantics, output, REQUESTS, COMMANDS, OPERANDS, NATIVE_DECODER,
                    EXECUTABLE_REVIEWS,
                )
            self.assertEqual(first.read_bytes(), second.read_bytes())
            generated = first.read_text(encoding="utf-8")
            self.assertIn("executableReviewed: true", generated)
            self.assertIn("executableReviewed: false", generated)
            self.assertIn("Executable Host reviews: cs2_host_executable_semantics.json", generated)
            self.assertIn("cs2HostOpcodeHasReviewedExecutableSemantics", generated)
            self.assertIn("LOCAL_NOTIFICATION: 3170", generated)

    def test_missing_join_inputs_fail_closed(self) -> None:
        with self.assertRaisesRegex(ValueError, "no command signature"):
            build_semantics(self.requests, {}, self.operands, self.wire_rule)
        with self.assertRaisesRegex(ValueError, "no Dat2 operand form"):
            build_semantics(self.requests, self.commands, {}, self.wire_rule)

    def test_review_manifest_identity_is_fail_closed(self) -> None:
        reviews = dict(self.executable_reviews)
        original = reviews[100]
        reviews[100] = type(original)(
            original.opcode, "CC_DELETE", original.adapter, original.native_reference,
        )
        with self.assertRaisesRegex(ValueError, "expected CC_CREATE"):
            build_semantics(
                self.requests, self.commands, self.operands, self.wire_rule, reviews,
            )


if __name__ == "__main__":
    unittest.main()
