import unittest

from .iniparse import Ini
from .profiles import LaunchError, Profile


PROFILE_TEXT = """
[profile]
description=base
world=manifests/base.ini
client=native
flavor=opt
services=game

[profile@windows]
description=windows
flavor=debug

[profile@macos]
description=macos

[env]
COMMON=1
SHARED=base

[env@windows]
SHARED=windows
WINDOWS_ONLY=1

[args]
arg=--base-one
arg=--base-two

[args@windows]
arg=--windows-one
arg=--windows-two

[service:game]
port=1000
root=base

[service:game@windows]
port=2000

[override:net:boot]
transport=tcp
port=1000

[override:net:boot@windows]
port=2000
"""


class ProfilePlatformOverlayTest(unittest.TestCase):
    def profile(self, platform):
        ini = Ini.loads(PROFILE_TEXT, "demo.ini")
        return Profile("demo", "demo.ini", ini, "repo", platform=platform)

    def test_windows_overlays_each_profile_section_shape(self):
        profile = self.profile("windows")
        self.assertEqual("windows", profile.description)
        self.assertEqual(["debug"], profile.flavor)
        self.assertEqual(
            {"COMMON": "1", "SHARED": "windows", "WINDOWS_ONLY": "1"},
            profile.env)
        self.assertEqual(
            ["--windows-one", "--windows-two"], profile.client_args)
        self.assertEqual(
            {"root": "base", "port": "2000"},
            profile.service_config("game"))
        self.assertEqual(
            [("net:boot", [("transport", "tcp"), ("port", "2000")])],
            profile.overrides())

    def test_other_platform_inherits_unmentioned_keys(self):
        profile = self.profile("macos")
        self.assertEqual("macos", profile.description)
        self.assertEqual(["opt"], profile.flavor)
        self.assertEqual({"COMMON": "1", "SHARED": "base"}, profile.env)
        self.assertEqual(["--base-one", "--base-two"], profile.client_args)
        self.assertEqual("1000", profile.service_config("game")["port"])

    def test_unknown_platform_suffix_is_rejected(self):
        ini = Ini.loads("[profile@windwos]\nclient=native\n", "bad.ini")
        with self.assertRaisesRegex(LaunchError, "unknown platform 'windwos'"):
            Profile("bad", "bad.ini", ini, "repo", platform="windows")


if __name__ == "__main__":
    unittest.main()
