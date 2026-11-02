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


class CompletionScriptsTest(unittest.TestCase):
    """One glue file per shell, all driven by the same `launch complete`."""

    def test_every_shell_has_a_script_that_asks_launch_complete(self):
        from . import completion
        for shell in completion.SHELLS:
            body = completion.script(shell)
            self.assertIsNotNone(body, shell)
            self.assertIn("complete --", body, shell)

    def test_shell_spellings_resolve(self):
        from . import completion
        self.assertEqual("powershell", completion.shell_name("pwsh"))
        self.assertEqual("powershell", completion.shell_name("PowerShell"))
        self.assertEqual("bash", completion.shell_name("sh"))
        self.assertEqual("fish", completion.shell_name("fish"))
        self.assertIsNone(completion.shell_name("cmd"))
        self.assertIsNone(completion.shell_name(""))

    def test_every_shell_has_an_install_line(self):
        from . import completion
        self.assertEqual(set(completion.SHELLS), set(completion.SHELL_INSTALL))

    def test_the_shell_slot_completes_to_every_shell(self):
        import io
        from . import completion
        out = io.StringIO()
        completion.emit(cli.REPO_ROOT, ["completion", ""], out)
        offered = {line.split("\t")[0] for line in out.getvalue().splitlines()}
        self.assertEqual(set(completion.SHELLS), offered)

    def test_completion_command_prints_the_named_shells_script(self):
        import io
        from contextlib import redirect_stdout
        from . import completion
        for typed, shell in (("fish", "fish"), ("pwsh", "powershell")):
            out = io.StringIO()
            with redirect_stdout(out):
                code = cli.main(["completion", typed])
            self.assertEqual(0, code, typed)
            self.assertEqual(completion.script(shell), out.getvalue(), typed)

    def test_an_unknown_shell_is_refused_by_name(self):
        import io
        from contextlib import redirect_stderr, redirect_stdout
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = cli.main(["completion", "cmd"])
        self.assertEqual(1, code)
        self.assertEqual("", out.getvalue())
        self.assertIn("no completion script for 'cmd'", err.getvalue())
