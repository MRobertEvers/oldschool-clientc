#!/usr/bin/env python3
"""Tests for tools/ss_allocate.py on an editor lane: a var declared with no id.

    python3 tools/test_ss_allocate_lane.py

A lane's content tree (build/lanes/<name>/content) shows its base through
links and has no `tools/` beside it. The allocator used to look for
`<tree>/../tools/var_prefix_names.py`, so a `[my_quest]` varp block written
without an id failed every RUN on a lane. These hold what replaced that: the
tool is found in the repository, its rules are applied to the lane's OWN files
only, and nothing is written through a link into the base.

Everything is built under a temporary directory; OSRS-Content is only read
(for `tools/var_prefix_names.py`). Without that file the tests are skipped.
"""
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import ss_allocate  # noqa: E402

TOOL = os.path.join(REPO, 'OSRS-Content', 'tools', 'var_prefix_names.py')

VARP = (
    '// The quest\'s progress.\n'
    '[thistle_quest]\n'
    'scope=perm\n'
    'clientcode=default\n'
    'transmit=no\n'
    'protect=no\n'
    'wholewrite=deny\n'
    'wholeread=deny\n'
)
SCRIPT = (
    '[debugproc,thistle]\r\n'
    '%thistle_quest = ^thistle_not_started;\r\n'
    'mes("Thistle: reset.");\r\n'
)
BASE_SCRIPT = '[debugproc,other]\n%thistle_quest = 0;\n'


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as handle:
        handle.write(text.encode('utf-8'))


def read(path):
    with open(path, 'rb') as handle:
        return handle.read().decode('utf-8')


@unittest.skipUnless(os.path.isfile(TOOL), 'OSRS-Content/tools/var_prefix_names.py is not checked out')
class LaneRespell(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        root = self.scratch.name
        # The base tree, and a lane beside nothing that has `tools/`.
        self.base = os.path.join(root, 'base', 'osrs-content')
        self.lane = os.path.join(root, 'lanes', 'q', 'content')
        self.base_script = os.path.join(self.base, 'server', 'scripts', 'areas', 'other.rs2')
        write(self.base_script, BASE_SCRIPT)
        os.makedirs(os.path.join(self.lane, 'ported'))
        os.makedirs(os.path.join(self.lane, 'configs'))
        # The ledger as the allocator has just written it.
        write(os.path.join(self.lane, 'pack', 'varp.alloc'), '9517=varp9517_thistle_quest\n')
        quest = os.path.join(self.lane, 'server', 'scripts', 'quests', 'quest_thistle')
        self.varp = os.path.join(quest, 'configs', 'quest_thistle.varp')
        self.script = os.path.join(quest, 'scripts', 'quest_thistle.rs2')
        write(self.varp, VARP)
        write(self.script, SCRIPT)
        # A file of the lane that is still the base's: a second hard link.
        self.linked = os.path.join(self.lane, 'server', 'scripts', 'areas', 'other.rs2')
        os.makedirs(os.path.dirname(self.linked))
        os.link(self.base_script, self.linked)

    def tearDown(self):
        self.scratch.cleanup()

    def test_tool_is_found_in_the_repository(self):
        tool, beside = ss_allocate.find_var_prefix_tool(self.lane)
        self.assertFalse(beside, 'a lane has no tools/ beside its content')
        self.assertTrue(os.path.samefile(tool, TOOL))
        tool, beside = ss_allocate.find_var_prefix_tool(os.path.join(REPO, 'OSRS-Content', 'osrs-content'))
        self.assertTrue(beside, 'the main tree runs the tool beside it, as before')

    def test_own_files_leave_links_out(self):
        own = ss_allocate.own_text_files(self.lane)
        self.assertIn('server/scripts/quests/quest_thistle/configs/quest_thistle.varp', own)
        self.assertIn('server/scripts/quests/quest_thistle/scripts/quest_thistle.rs2', own)
        self.assertIn('pack/varp.alloc', own)
        self.assertNotIn('server/scripts/areas/other.rs2', own, 'a hard link to the base is not the lane\'s file')

    def test_respell_writes_the_lane_and_not_the_base(self):
        ss_allocate.respell_vars(self.lane)
        self.assertIn('\n[varp9517_thistle_quest]\n', read(self.varp))
        script = read(self.script)
        self.assertIn('%varp9517_thistle_quest = ^thistle_not_started;\r\n', script, 'the script reads the new name')
        self.assertEqual(script.count('\r\n'), 3, 'the file keeps its own line endings')
        self.assertEqual(read(self.base_script), BASE_SCRIPT, 'the base tree is not written')
        self.assertEqual(read(self.linked), BASE_SCRIPT, 'nor is it written through the lane\'s link')
        self.assertEqual(os.stat(self.linked).st_nlink, 2, 'the link is still a link')
        leftovers = [name for _, _, names in os.walk(self.lane) for name in names if name.endswith('.tmp')]
        self.assertEqual(leftovers, [], 'no temporary is left behind')
        # Idempotent: the names carry their prefix now.
        before = (read(self.varp), read(self.script))
        ss_allocate.respell_vars(self.lane)
        self.assertEqual(before, (read(self.varp), read(self.script)))

    def test_main_tree_is_untouched(self):
        status = ['git', '-C', os.path.join(REPO, 'OSRS-Content'), 'status', '--short']
        before = subprocess.run(status, capture_output=True, text=True).stdout
        ss_allocate.respell_vars(self.lane)
        after = subprocess.run(status, capture_output=True, text=True).stdout
        self.assertEqual(before, after, 'respelling a lane changes nothing in OSRS-Content')


if __name__ == '__main__':
    unittest.main()
