#!/usr/bin/env python3
"""Tests for tools/config_text.py: the full-key config text markers.

    python3 tools/test_config_text.py
"""
import os
import re
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import config_text  # noqa: E402

OLD = (
    "// Unpacked by cachepack.\n"
    "\n"
    "[bronze_axe]\n"
    "name=Bronze axe\n"
    "recol1s=12\n"
    "recol1d=55\n"
    "param=levelrequire,1\n"
    "\n"
    "[plain]\n"
    "name=Plain\n"
)

NEW = (
    "// Unpacked by cachepack.\n"
    "\n"
    "[bronze_axe]\n"
    "name=Bronze axe\n"
    "desc=default\n"
    "recol1s=12\n"
    "recol1d=55\n"
    "param=levelrequire,1\n"
    "op=empty\n"
    "\n"
    "[plain]\n"
    "name=Plain\n"
    "desc=default\n"
    "recol=default\n"
    "param=empty\n"
    "op=default\n"
)


class Markers(unittest.TestCase):
    def test_marker_is_raw_text(self):
        self.assertIs(config_text.marker("default"), config_text.DEFAULT)
        self.assertIs(config_text.marker("empty"), config_text.EMPTY)
        self.assertIs(config_text.marker("default\r\n"), config_text.DEFAULT)
        self.assertIsNone(config_text.marker("\\default"))
        self.assertIsNone(config_text.marker("\\empty"))
        self.assertIsNone(config_text.marker("Default"))
        self.assertIsNone(config_text.marker(" default"))
        self.assertIsNone(config_text.marker("default "))
        self.assertIsNone(config_text.marker("foo,default"))
        self.assertIsNone(config_text.marker(""))

    def test_markers_never_equal_strings(self):
        self.assertNotEqual(config_text.DEFAULT, "default")
        self.assertNotEqual(config_text.EMPTY, "empty")
        self.assertIsNot(config_text.DEFAULT, config_text.EMPTY)

    def test_unmark_only_touches_the_marker_escape(self):
        self.assertEqual(config_text.unmark("\\default"), "default")
        self.assertEqual(config_text.unmark("\\empty"), "empty")
        self.assertEqual(config_text.unmark("a\\nb"), "a\\nb")
        self.assertEqual(config_text.unmark("\\\\default"), "\\\\default")
        self.assertEqual(config_text.unmark("Bronze axe"), "Bronze axe")

    def test_unescape_matches_cp_unescape(self):
        self.assertEqual(config_text.unescape("\\default"), "default")
        self.assertEqual(config_text.unescape("a\\nb\\rc"), "a\nb\rc")
        self.assertEqual(config_text.unescape("\\\\default"), "\\default")
        self.assertEqual(config_text.unescape("\\[x]"), "[x]")
        self.assertEqual(config_text.unescape("trailing\\"), "trailing\\")

    def test_marker_is_a_contract_violation_for_string_decoders(self):
        with self.assertRaises(AssertionError):
            config_text.unmark("default")
        with self.assertRaises(AssertionError):
            config_text.unescape("empty")


class Filter(unittest.TestCase):
    def test_new_text_filters_to_old_text(self):
        self.assertEqual(config_text.filter_text(NEW), OLD)

    def test_old_text_is_unchanged(self):
        self.assertEqual(config_text.filter_text(OLD), OLD)

    def test_missing_key_stays_missing(self):
        kept = list(config_text.filter_lines(["[a]\n", "name=A\n"]))
        self.assertEqual(kept, ["[a]\n", "name=A\n"])

    def test_default_line_is_dropped(self):
        self.assertIsNone(config_text.filter_line("name=default\n"))
        self.assertIsNone(config_text.filter_line("category=default"))
        self.assertIsNone(config_text.filter_line("op1=default\r\n"))

    def test_empty_line_is_dropped(self):
        self.assertIsNone(config_text.filter_line("param=empty\n"))
        self.assertIsNone(config_text.filter_line("frame=empty"))

    def test_escaped_marker_becomes_plain_string(self):
        self.assertEqual(config_text.filter_line("name=\\default\n"), "name=default\n")
        self.assertEqual(config_text.filter_line("name=\\empty"), "name=empty")
        self.assertEqual(config_text.filter_line("name=\\default\r\n"), "name=default\r\n")

    def test_other_lines_untouched(self):
        for line in ("// name=default\n", "[default]\n", "\n", "name=Defaults\n",
                     "param=foo,default\n", "name=\\\\default\n", "name=a\\nb\n",
                     "name= default\n", "noequals\n"):
            self.assertEqual(config_text.filter_line(line), line)

    def test_read_text_and_lines(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "all.obj")
            with open(p, "w", encoding="utf-8", newline="") as f:
                f.write(NEW)
            self.assertEqual(config_text.read_text(p), OLD)
            self.assertEqual(config_text.read_lines(p), OLD.splitlines())
            self.assertEqual(config_text.read_lines(p, keepends=True),
                             OLD.splitlines(True))


class Records(unittest.TestCase):
    def test_parse_keeps_markers(self):
        recs = config_text.parse_records(NEW)
        self.assertEqual(list(recs), ["bronze_axe", "plain"])
        plain = recs["plain"]
        self.assertIn(("desc", config_text.DEFAULT), plain)
        self.assertIn(("param", config_text.EMPTY), plain)
        self.assertEqual(config_text.value(plain, "desc"), None)
        self.assertEqual(config_text.values(plain, "param"), [])
        self.assertEqual(config_text.value(plain, "name"), "Plain")

    def test_parse_escaped_marker_is_a_string(self):
        recs = config_text.parse_records("[x]\nname=\\default\nop1=\\empty\n")
        self.assertEqual(recs["x"], [("name", "default"), ("op1", "empty")])

    def test_parse_unescape_strings(self):
        recs = config_text.parse_records("[x]\nname=a\\nb\n", unescape_strings=True)
        self.assertEqual(recs["x"], [("name", "a\nb")])
        recs = config_text.parse_records("[x]\nname=a\\nb\n")
        self.assertEqual(recs["x"], [("name", "a\\nb")])

    def test_key_matches_indexed_stem(self):
        self.assertTrue(config_text.key_matches("recol", "recol"))
        self.assertTrue(config_text.key_matches("recol", "recol1s"))
        self.assertTrue(config_text.key_matches("op", "op5"))
        self.assertFalse(config_text.key_matches("op", "ops"))
        self.assertFalse(config_text.key_matches("op", "ifop1"))

    def test_overlay_missing_key_inherits(self):
        base = config_text.parse_records(NEW)["bronze_axe"]
        merged = config_text.apply_overlay(base, [("name", "Shiny axe")])
        self.assertEqual(config_text.value(merged, "name"), "Shiny axe")
        self.assertEqual(config_text.values(merged, "recol1s"), ["12"])

    def test_overlay_default_clears_rank0_value(self):
        base = config_text.parse_records(NEW)["bronze_axe"]
        overlay = config_text.parse_records("[bronze_axe]\nname=default\nrecol=default\n")
        merged = config_text.apply_overlay(base, overlay["bronze_axe"])
        self.assertIsNone(config_text.value(merged, "name"))
        self.assertEqual(config_text.values(merged, "recol1s"), [])
        self.assertEqual(config_text.values(merged, "recol1d"), [])
        self.assertNotIn("name", [k for k, _ in merged])
        self.assertEqual(config_text.values(merged, "param"), ["levelrequire,1"])

    def test_overlay_empty_replaces_list(self):
        base = config_text.parse_records(NEW)["bronze_axe"]
        merged = config_text.apply_overlay(base, [("param", config_text.EMPTY)])
        self.assertEqual(config_text.values(merged, "param"), [])
        self.assertIn(("param", config_text.EMPTY), merged)

    def test_overlay_escaped_default_is_a_value(self):
        base = config_text.parse_records(NEW)["bronze_axe"]
        overlay = config_text.parse_records("[bronze_axe]\nname=\\default\n")
        merged = config_text.apply_overlay(base, overlay["bronze_axe"])
        self.assertEqual(config_text.value(merged, "name"), "default")



DBTABLE = (
    "// Unpacked by cachepack.\n"
    "\n"
    "[quest]\n"
    "column=id,int\n"
    "column=sortname,string\n"
    "column=version,ABSENT\n"
    "column=startcoord,coord\n"
    "column=startnpc,npc\n"
    "column=members,boolean\n"
    "column=requirement_stats,stat,int\n"
    "column=info,string,string\n"
    "default=startcoord,0_0_0_0_0\n"
    "default=members,false\n"
    "\n"
    "[empty_table]\n"
    "column=default\n"
    "default=default\n"
)

DBROW = (
    "[quest_animalmagnetism]\n"
    "table=quest\n"
    "data=id,123\n"
    "data=sortname,Ascent of Arceuus, The\n"
    "data=startcoord,0_48_52_22_31\n"
    "data=startnpc,anma_assistant_multi\n"
    "data=members,true\n"
    "data=requirement_stats,woodcutting,35\n"
    "data=requirement_stats,ranged,30\n"
    "data=info,a\\, b,c, d\\ \n"
    "\n"
    "[quest_none]\n"
    "table=quest\n"
    "data=default\n"
    "\n"
    "[unknown_table_row]\n"
    "table=nonesuch\n"
    "data=x,1,2,3\n"
    "\n"
    "[no_table]\n"
    "table=default\n"
    "data=empty\n"
)


class Db(unittest.TestCase):
    def setUp(self):
        self.tables = config_text.parse_dbtables(DBTABLE)
        self.rows = config_text.parse_dbrows(DBROW, self.tables)
        self.tmp = tempfile.TemporaryDirectory()
        root = self.tmp.name
        os.makedirs(os.path.join(root, "configs"))
        os.makedirs(os.path.join(root, "pack"))
        with open(os.path.join(root, "configs", "all.npc.compack"), "w") as f:
            f.write("// member index\n4408=anma_assistant_multi\n")
        with open(os.path.join(root, "pack", "stat.pack"), "w") as f:
            f.write("4=ranged\n8=woodcutting\n")
        self.names = config_text.Names(root)

    def tearDown(self):
        self.tmp.cleanup()

    def test_column_id_is_position_and_absent_is_a_hole(self):
        quest = self.tables["quest"]
        self.assertEqual([c.name for c in quest.columns][:4],
                         ["id", "sortname", "version", "startcoord"])
        self.assertTrue(quest.column("version").absent)
        self.assertEqual(quest.column("version").types, ())
        self.assertEqual(quest.column("startcoord").id, 3)
        self.assertNotIn(2, quest.schema())
        self.assertEqual(quest.schema()[6], ("requirement_stats", ("stat", "int")))

    def test_table_markers_state_nothing(self):
        self.assertEqual(self.tables["empty_table"].columns, [])

    def test_defaults_are_tuples_per_line(self):
        quest = self.tables["quest"]
        self.assertEqual(quest.column("startcoord").defaults, [("0_0_0_0_0",)])
        self.assertEqual(quest.column("members").defaults, [("false",)])
        self.assertEqual(quest.column("id").defaults, [])

    def test_row_fields_last_takes_the_rest(self):
        row = self.rows["quest_animalmagnetism"]
        self.assertEqual(row.table, "quest")
        self.assertIs(row.schema, self.tables["quest"])
        self.assertEqual(row.fields("sortname"), ["Ascent of Arceuus, The"])
        self.assertEqual(row.tuples("requirement_stats"),
                         [("woodcutting", "35"), ("ranged", "30")])
        self.assertEqual(row.tuples("info"), [("a, b", "c, d ")])
        self.assertEqual(row.columns()[:3], ["id", "sortname", "startcoord"])

    def test_row_markers_and_unknown_tables(self):
        self.assertEqual(self.rows["quest_none"].data, [])
        self.assertEqual(self.rows["no_table"].table, None)
        self.assertEqual(self.rows["no_table"].data, [])
        self.assertEqual(self.rows["unknown_table_row"].tuples("x"), [("1", "2", "3")])

    def test_typed_values_are_the_cache_numbers(self):
        row = self.rows["quest_animalmagnetism"]
        self.assertEqual(row.typed("startcoord"), [(50695455,)])
        self.assertEqual(row.typed("startnpc", self.names), [(4408,)])
        self.assertEqual(row.typed("members"), [(1,)])
        self.assertEqual(row.typed_fields("requirement_stats", self.names), [8, 35, 4, 30])
        self.assertEqual(row.typed("sortname"), [("Ascent of Arceuus, The",)])

    def test_db_int(self):
        self.assertEqual(config_text.db_int("npc", "null"), -1)
        self.assertEqual(config_text.db_int("int", "null"), -1)
        self.assertEqual(config_text.db_int("obj", "1234"), 1234)  # an unnamed id
        self.assertEqual(config_text.db_int("boolean", "false"), 0)
        self.assertEqual(config_text.db_int("coord", "1_43_53_40_25"),
                         (1 << 28) | ((43 * 64 + 40) << 14) | (53 * 64 + 25))
        with self.assertRaises(KeyError):
            config_text.db_int("npc", "nobody", self.names)
        with self.assertRaises(ValueError):
            config_text.db_int("int", "seven")

    def test_db_split_escapes(self):
        self.assertEqual(config_text.db_split("c,a\\,b,x\\\\y", 2), ("c", ["a,b", "x\\y"]))
        self.assertEqual(config_text.db_split("c,a/\\/b", 1), ("c", ["a//b"]))
        self.assertEqual(config_text.db_split("c,trail\\ ", 1), ("c", ["trail "]))
        self.assertEqual(config_text.db_split("c,v // note", 1), ("c", ["v"]))
        self.assertEqual(config_text.db_head("name,x,y"), "name")

    def test_unique_rejects_a_repeated_header(self):
        with self.assertRaises(ValueError):
            config_text.parse_dbrows("[a]\ntable=quest\n[a]\ntable=quest\n", self.tables,
                                     unique=True)


class Params(unittest.TestCase):
    def test_split_param_value_is_the_rest(self):
        self.assertEqual(config_text.split_param("attackrate,5"), ("attackrate", "5"))
        self.assertEqual(config_text.split_param("param_510,Palm, of Plenty"),
                         ("param_510", "Palm, of Plenty"))
        with self.assertRaises(AssertionError):
            config_text.split_param("default")

    def test_param_values_by_declared_type(self):
        with tempfile.TemporaryDirectory() as root:
            os.makedirs(os.path.join(root, "configs"))
            os.makedirs(os.path.join(root, "pack"))
            with open(os.path.join(root, "configs", "all.param"), "w") as f:
                f.write("[attackrate]\ntype=int\n[title]\ntype=string\n[prereq]\ntype=struct\n"
                        "[untyped]\ntype=default\n[members]\ntype=boolean\n"
                        "[where]\ntype=coord\n[sound]\ntype=synth\n")
            with open(os.path.join(root, "configs", "all.struct.compack"), "w") as f:
                f.write("77=struct_77\n")
            with open(os.path.join(root, "pack", "4_soundeffects.pack"), "w") as f:
                f.write("12=grim_piano_a4\n")
            types = config_text.param_types(os.path.join(root, "configs", "all.param"))
            self.assertEqual(types["untyped"], None)
            self.assertEqual(types["members"], "boolean")
            names = config_text.Names(root)
            got = config_text.param_values(
                ["attackrate,5", "title,A, B", "prereq,struct_77", "untyped,-3", "default",
                 "members,yes", "where,0_50_50_1_2", "sound,grim_piano_a4"],
                types, names)
            self.assertEqual(got, {"attackrate": 5, "title": "A, B", "prereq": 77,
                                   "untyped": -3, "members": 1,
                                   "where": (50 * 64 + 1) << 14 | (50 * 64 + 2),
                                   "sound": 12})
            self.assertEqual(config_text.param_int("struct", "null", names), -1)


class ValueTypes(unittest.TestCase):
    """config_text's VALUE_TYPES is the rscache library's ScriptVarType table
    (src/rscache_valuetype.c), row for row -- the one (id, char, word) table every
    C reader (cachepack, the param decoder, the server) maps through."""

    def test_table_matches_library(self):
        source = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "3rd", "rscache",
                              "src", "rscache_valuetype.c")
        with open(source, encoding="utf-8") as f:
            text = f.read()
        body = text[text.index("k_value_types[] = {"):text.index("};", text.index("k_value_types[] = {"))]
        rows = []
        for m in re.finditer(r"\{ (-?\d+), ('(?:[^'\\]|\\.)'|0x[0-9A-F]+), \"(\w+)\" \}", body):
            ident, ch, word = int(m.group(1)), m.group(2), m.group(3)
            ch = chr(int(ch, 16)) if ch.startswith("0x") else ch[1:-1]
            rows.append((word, None if ident < 0 else ident, ch))
        self.assertEqual(rows, [(w, i, c) for w, i, c, _ in config_text.VALUE_TYPES])

    def test_type_word_from_every_spelling(self):
        self.assertEqual(config_text.type_word("namedobj"), "namedobj")
        self.assertEqual(config_text.type_word("O"), "namedobj")
        self.assertEqual(config_text.type_word("13"), "namedobj")
        self.assertEqual(config_text.type_word("\xd0"), "dbrow")
        self.assertIsNone(config_text.type_word("nonsense"))

    def test_value_int_spellings(self):
        self.assertEqual(config_text.value_int("boolean", "yes"), 1)
        self.assertEqual(config_text.value_int("boolean", "false"), 0)
        self.assertEqual(config_text.value_int("coord", "null"), -1)
        self.assertEqual(config_text.value_int(None, "-7"), -7)
        with self.assertRaises(ValueError):
            config_text.value_int("graphic", "sprite_name")


if __name__ == "__main__":
    unittest.main()
