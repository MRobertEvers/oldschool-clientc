#!/usr/bin/env python3
"""Conformance for the pinned native-script transformation, not a capture harness."""
import hashlib
import json
from pathlib import Path
import unittest

import plugin_engine_script_hooks as hooks

ROOT=Path(__file__).resolve().parent
FIXTURE=ROOT/'testdata/plugin-engine'


class NativeHookTests(unittest.TestCase):
    def setUp(self):
        self.spec=json.loads((FIXTURE/'script-hooks.json').read_text())
        self.raw=(FIXTURE/'ground_item_row.cs2b').read_bytes()

    def test_pinned_native_script_and_targets(self):
        output,fingerprint=hooks.patch(self.raw,self.spec)
        before,after=hooks.decode(self.raw),hooks.decode(output)
        self.assertEqual(hooks.encode(before),self.raw)
        self.assertEqual(hooks.encode(after),output)
        delta=len(after['ops'])-len(before['ops'])
        self.assertEqual(delta,45)
        at=self.spec['before_pc']
        for pc,(opcode,operand) in enumerate(before['ops']):
            new_pc=pc+(delta if pc>=at else 0)
            self.assertEqual(after['ops'][new_pc][0],opcode)
            if opcode in hooks.BRANCHES:
                target=pc+operand+1
                expected=target+(delta if target>at else 0)
                self.assertEqual(new_pc+after['ops'][new_pc][1]+1,expected)
            else:
                self.assertEqual(after['ops'][new_pc][1],operand)
        self.assertEqual(sum(op==6599 for op,arg in after['ops']),1)
        header=(ROOT.parent/'src/plugin/native_script_hooks.gen.h').read_text()
        self.assertIn(f'0x{fingerprint:016x}',header)

    def test_exact_legacy_overlay_targets_normalize_to_native(self):
        legacy=hooks.decode(self.raw)
        for pc in self.spec['legacy_overlay_dot_pcs']:
            self.assertEqual(legacy['ops'][pc],(103,1))
            legacy['ops'][pc]=(103,0)
        legacy_raw=hooks.encode(legacy)
        self.assertEqual(hashlib.sha256(legacy_raw).hexdigest(),self.spec['legacy_source_sha256'])
        self.assertEqual(hooks.patch(legacy_raw,self.spec),hooks.patch(self.raw,self.spec))

    def test_empty_caption_returns_before_auxiliary_row_creation(self):
        output,_=hooks.patch(self.raw,self.spec)
        ops=hooks.decode(output)['ops']
        guard=[(35,self.spec['string_local']),(4117,0),(0,0),(7,2),(33,3),(21,0)]
        locations=[i for i in range(len(ops)-len(guard)+1) if ops[i:i+len(guard)]==guard]
        self.assertEqual(len(locations),1)
        start=locations[0]
        # The comparison's nonempty edge skips the return and retains native
        # formatting; the empty edge returns the current next-child index.
        self.assertEqual(start+3+1+ops[start+3][1],start+len(guard))
        self.assertLess(start,next(i for i in range(start,len(ops)) if ops[i][0]==103))

    def test_unknown_script_is_rejected_before_mutation(self):
        wrong=bytearray(self.raw);wrong[4]^=1
        with self.assertRaisesRegex(ValueError,'fingerprint mismatch'):
            hooks.patch(bytes(wrong),self.spec)

    def test_native_argument_contract_is_required(self):
        self.spec['trailer_fields'][0]+=1
        with self.assertRaisesRegex(ValueError,'argument/local contract'):
            hooks.patch(self.raw,self.spec)

    def test_native_helper_contract(self):
        helper=(FIXTURE/'loot_aux_has.cs2b').read_bytes()
        spec=self.spec['highlight_helper']
        self.assertEqual(hashlib.sha256(helper).hexdigest(),spec['source_sha256'])
        self.assertEqual(hooks.decode(helper)['fields'],spec['trailer_fields'])


if __name__=='__main__': unittest.main()
