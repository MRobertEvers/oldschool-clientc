import os
import unittest
from unittest import mock

from . import cli
from .iniparse import Ini
from .profiles import Profile


class BuildPlanPathTest(unittest.TestCase):
    def test_client_manifest_argument_uses_portable_separators(self):
        profile = Profile(
            "test",
            os.path.join(cli.REPO_ROOT, "profiles", "test.ini"),
            Ini.loads(
                "[profile]\n"
                "world=manifests/manifest_osrs239.ini\n"
                "client=native\n",
                "test.ini"),
            cli.REPO_ROOT,
            platform="windows")

        with mock.patch.object(
                cli, "generate_resolved_manifest",
                return_value=profile.world_path):
            plan = cli.build_plan(profile)

        manifest_index = plan.client_argv.index("--manifest") + 1
        self.assertEqual(
            "manifests/manifest_osrs239.ini",
            plan.client_argv[manifest_index])
        self.assertNotIn("\\", plan.client_argv[manifest_index])


if __name__ == "__main__":
    unittest.main()
