#!/usr/bin/env python3
import json
from pathlib import Path
import tempfile
import unittest

from cs2_proc_headers import audit, repair_references


class ProcHeadersTest(unittest.TestCase):
    def test_reference_renames_require_actual_calls_and_decoded_hooks(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            root = base / "content"
            (root / "scripts").mkdir(parents=True)
            (root / "pack").mkdir()
            (base / "docs").mkdir()
            (root / "pack/12_clientscripts.pack").write_text("1=caller\n2=current_helper\n3=wrapper\n4=current_body\n5=current_hook\n")
            for name, header, body in (
                ("caller", "[clientscript,caller]", '~old_helper;\ncc_input_setonfocuschanged("old_hook");\nmes("old_hook");\n'),
                ("current_helper", "[proc,current_helper]", "return;\n"),
                ("wrapper", "[clientscript,wrapper]", "~wrapper;\n"),
                ("current_body", "[proc,current_body]", "return;\n"),
                ("current_hook", "[clientscript,current_hook]", "return;\n"),
            ):
                (root / f"scripts/{name}.cs2").write_text(header+"\n"+body)
            (base / "docs/CS2_SCRIPT_NAME_AUDIT.tsv").write_text("id\told_name\n2\told_helper\n4\twrapper\n5\told_hook\n")
            graph = base / "graph.tsv"
            graph.write_text("call\t1\t0\t2\ncall\t3\t0\t4\nconstant\t1\t1\t5\n")
            # A coincidental integer equal to the hook ID is insufficient.
            result = repair_references(root, graph)
            self.assertTrue(any(r["name"] == "old_hook" for r in result["unresolved"]))
            asts = base / "asts"
            asts.mkdir()
            (asts / "1.json").write_text(json.dumps({"id":1,"body":{"kind":"clientscript","scriptId":5}}))
            result = repair_references(root, graph, write=True, hook_asts=asts)
            self.assertEqual(result["unresolved"], [])
            self.assertEqual({r["target"] for r in result["repaired_references"]}, {2,4,5})
            source = (root / "scripts/caller.cs2").read_text()
            self.assertIn("~current_helper;", source)
            self.assertIn('cc_input_setonfocuschanged("current_hook")', source)
            self.assertIn('mes("old_hook")', source)
            self.assertIn("~current_body;", (root / "scripts/wrapper.cs2").read_text())

    def test_native_edges_distinguish_helpers_recursion_and_wrappers(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "scripts").mkdir()
            (root / "pack").mkdir()
            entries = {
                1: ("caller", "~helper;\n~absent_edge;\n"),
                2: ("helper", "return;\n"),
                3: ("wrapper", "~wrapper;\n"),
                5: ("recursive", "~recursive;\n"),
                6: ("absent_edge", "return;\n"),
            }
            (root / "pack/12_clientscripts.pack").write_text("".join(
                f'{ident}={name} hashname("[clientscript,{name}]")\n' for ident,(name,_) in entries.items()))
            for ident,(name,body) in entries.items():
                (root / f"scripts/{name}.cs2").write_text(f"// {ident}\n[clientscript,{name}]\n{body}")
            graph = root / "graph.tsv"
            graph.write_text("script\t1\t0\t0\t0\nscript\t2\t0\t0\t0\nscript\t3\t0\t0\t0\nscript\t5\t0\t0\t0\nscript\t6\t0\t0\t0\ncall\t1\t0\t2\ncall\t3\t0\t4\ncall\t5\t0\t5\n")
            result = audit(root, graph, write=True)
            self.assertEqual({r["id"] for r in result["verified_headers"]}, {2,5})
            self.assertIn("[proc,helper]", (root / "scripts/helper.cs2").read_text())
            self.assertIn("[proc,recursive]", (root / "scripts/recursive.cs2").read_text())
            self.assertIn("[clientscript,wrapper]", (root / "scripts/wrapper.cs2").read_text())
            self.assertIn("[clientscript,absent_edge]", (root / "scripts/absent_edge.cs2").read_text())
            self.assertEqual(audit(root, graph)["verified_headers"], [])


if __name__ == "__main__":
    unittest.main()
