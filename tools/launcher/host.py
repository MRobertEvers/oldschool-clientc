"""Host-specific command lines used by the otherwise portable launcher."""

import os
import sys


IS_WINDOWS = os.name == "nt"
PLATFORM_NAMES = ("windows", "macos", "linux")


def platform_name():
    """Stable profile-overlay name for the current host OS."""
    if IS_WINDOWS:
        return "windows"
    if sys.platform == "darwin":
        return "macos"
    return "linux"


def make_command(repo_root, target, make_args=(), parallel=False):
    """Return (argv, display) for one src/makefile target on this host."""
    make_args = list(make_args)
    if parallel and not any(arg == "-j" or arg.startswith("-j")
                            for arg in make_args):
        make_args.insert(0, "-j")
    if IS_WINDOWS:
        script = os.path.join(repo_root, "make.ps1")
        argv = [
            "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
            "-File", script,
        ] + make_args + [target]
        display = ".\\make.ps1 %s" % " ".join(make_args + [target])
        return argv, display

    argv = ["make", "-C", "src"] + make_args + [target]
    return argv, " ".join(argv)


def python_command(argv):
    """Run repository helpers with the interpreter already running launch."""
    return [sys.executable] + list(argv)
