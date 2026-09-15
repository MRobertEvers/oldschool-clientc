#!/usr/bin/env python3
"""Check mutation discovery against compiler-resolved lvalues, including macros."""
import unittest

from plugin_engine_inventory import clang_api, field_access_kind


class FieldAccessTest(unittest.TestCase):
    def test_index_reads_are_not_writes_and_macro_writes_are_not_lost(self):
        cx = clang_api()
        source = """struct Node { int index; int values[4]; int *data; struct { int x; } pos; };
void mutate(int *);
void observe(const int *);
#define SET_INDEX(n, value) ((n)->index = (value))
void f(struct Node *node, int *output) {
 node->index = 1;
 output[node->index] = 3;
 node->values[node->index] += 4;
 SET_INDEX(node, 7);
 node->pos.x++;
 node->data[1] = 2;
 mutate(&node->index);
 mutate(node->values);
 observe(node->values);
}
"""
        index = cx.Index.create()
        unit = index.parse("field_access.c", args=["-std=c11"], unsaved_files=[("field_access.c",source)])
        self.assertFalse([d for d in unit.diagnostics if d.severity >= cx.Diagnostic.Error])
        found = {}
        def visit(node, ancestors=()):
            if node.kind == cx.CursorKind.MEMBER_REF_EXPR:
                found[(node.location.line, node.spelling)] = field_access_kind(node, ancestors, cx)
            for child in node.get_children():
                visit(child, (*ancestors,node))
        visit(unit.cursor)
        self.assertEqual(found[(6,"index")], "write")
        self.assertEqual(found[(7,"index")], "read")
        self.assertEqual(found[(8,"values")], "write")
        self.assertEqual(found[(8,"index")], "read")
        self.assertEqual(found[(9,"index")], "write")
        self.assertEqual(found[(10,"x")], "write")
        self.assertEqual(found[(10,"pos")], "read")
        self.assertEqual(found[(11,"data")], "pointee_write")
        self.assertEqual(found[(12,"index")], "address_escape")
        self.assertEqual(found[(13,"values")], "address_escape")
        self.assertEqual(found[(14,"values")], "read")


if __name__ == "__main__":
    unittest.main()
