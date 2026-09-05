#!/usr/bin/env python3
"""Acceptance must reject stale, mismatched and bypassed runtime fixtures."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import gameframe_fixture as fixture


class FixtureGateTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "cache").mkdir()
        (self.root / "scripts").mkdir()
        (self.root / "cache/0").write_bytes(b"cache fixture")
        (self.root / "scripts/script.dat").write_bytes(b"data")
        (self.root / "scripts/script.idx").write_bytes(b"index")
        self.binary = self.root / "client"
        self.binary.write_bytes(b"binary")
        self.manifest = self.root / "manifest.ini"
        self.manifest.write_text("""[cache:boot]
revision=239
dir=cache
[net:boot]
rev=osrs239
transport=embed
scripts=scripts
[ui:boot]
logic=cs2
[derived:cache]
out=cache
check=unused
[derived:scripts]
out=scripts
check=unused
""")
        self.addCleanup(patch.stopall)
        patch.object(fixture, "git_state", return_value={"commit": "fixture"}).start()
        patch.dict(fixture.os.environ, {}, clear=True).start()
        self.check = patch.object(fixture, "check_derived", return_value=(False, "fresh")).start()

    def inspect(self):
        return fixture.inspect(self.root, self.binary, self.manifest, "osrs239")

    def test_matched_inputs_are_hashed(self):
        result = self.inspect()
        self.assertTrue(result["accepted"])
        self.assertEqual(len(result["files"]), 3)
        before = result["files"][str(self.root / "scripts/script.dat")]
        (self.root / "scripts/script.dat").write_bytes(b"changed")
        self.assertNotEqual(self.inspect()["files"][str(self.root / "scripts/script.dat")], before)

    def test_stale_or_failed_checker_blocks(self):
        self.check.return_value = True, "checker exited 2: missing source"
        self.assertFalse(self.inspect()["accepted"])

    def test_override_presence_blocks_even_zero(self):
        # The native loader tests getenv, not atoi. '=0' is still a bypass.
        fixture.os.environ["TORIRSSERVER_ALLOW_STALE_SCRIPTS"] = "0"
        self.assertFalse(self.inspect()["accepted"])

    def test_checking_another_pack_cannot_approve_consumed_pack(self):
        self.manifest.write_text(self.manifest.read_text().replace("out=scripts", "out=another-pack"))
        result = self.inspect()
        self.assertFalse(result["accepted"])
        self.assertTrue(any("differs from consumed" in b for b in result["blockers"]))

    def test_revision_mismatch_blocks(self):
        self.manifest.write_text(self.manifest.read_text().replace("logic=cs2", "logic=cs1"))
        self.assertFalse(self.inspect()["accepted"])

    def test_inherited_server_cache_cannot_replace_the_declared_fixture(self):
        fixture.os.environ["TORIRSSERVER_CACHE"] = str(self.root / "another-cache")
        self.assertFalse(self.inspect()["accepted"])

    def test_missing_coverage_blocks(self):
        text = self.manifest.read_text().split("[derived:scripts]")[0]
        self.manifest.write_text(text)
        self.assertFalse(self.inspect()["accepted"])


if __name__ == "__main__":
    unittest.main()
