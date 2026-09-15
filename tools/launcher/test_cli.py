import os
import unittest
from unittest import mock

from . import cli
from .iniparse import Ini
from .profiles import LaunchError, Profile


def _windows_profile(body):
    return Profile(
        "test",
        os.path.join(cli.REPO_ROOT, "profiles", "test.ini"),
        Ini.loads(body, "test.ini"),
        cli.REPO_ROOT,
        platform="windows")


class BuildPlanPathTest(unittest.TestCase):
    def test_client_manifest_argument_uses_portable_separators(self):
        profile = _windows_profile(
            "[profile]\n"
            "world=manifests/manifest_osrs239.ini\n"
            "client=native\n")

        with mock.patch.object(
                cli, "generate_resolved_manifest",
                return_value=profile.world_path):
            plan = cli.build_plan(profile)

        manifest_index = plan.client_argv.index("--manifest") + 1
        self.assertEqual(
            "manifests/manifest_osrs239.ini",
            plan.client_argv[manifest_index])
        self.assertNotIn("\\", plan.client_argv[manifest_index])


class WindowsAsanTest(unittest.TestCase):
    """The Windows lane is MinGW, which has no GCC sanitizer runtime at all."""

    def _plan(self, flavor):
        profile = _windows_profile(
            "[profile]\n"
            "world=manifests/manifest_osrs239.ini\n"
            "client=native\n"
            "flavor=%s\n" % flavor)
        with mock.patch.object(cli.host, "IS_WINDOWS", True):
            with mock.patch.object(
                    cli, "generate_resolved_manifest",
                    return_value=profile.world_path):
                return cli.build_plan(profile)

    def test_the_asan_flavor_is_refused_by_name(self):
        # Refused, never dropped: a run that says "asan" and hands back an
        # uninstrumented binary reports "no ASan errors" for the wrong reason.
        with self.assertRaises(LaunchError) as raised:
            self._plan("opt,asan")
        message = str(raised.exception)
        self.assertIn("asan", message)
        self.assertIn("--flavor opt", message)
        self.assertIn("[profile@windows]", message)

    def test_every_other_flavor_still_builds_the_win64_lane(self):
        plan = self._plan("opt")
        self.assertEqual("win64", plan.target)
        self.assertNotIn("ENABLE_ASAN=1", plan.make_args)


if __name__ == "__main__":
    unittest.main()
