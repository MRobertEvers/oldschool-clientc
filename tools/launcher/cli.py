"""`./launch` — one entry point for every way this tree can be run.

The launcher RESOLVES and DISPATCHES. It deliberately does not re-implement
what already works: content preparation stays in run-live.sh (reached through
its TORIRS_PREPARE_ONLY seam, or replaced per-world by `[derived:*]` blocks),
jar patching stays in run-runelite.sh, and the flavor objdir scheme stays in
src/makefile. What is new here is a NAME for a runnable configuration, and
ownership of the processes that name implies.
"""

import argparse
import contextlib
import json
import os
import signal
import subprocess
import sys
import time
import urllib.parse

from . import bench, host, services as services_mod
from . import staleness, supervisor
from .profiles import (LaunchError, generate_resolved_manifest, list_profiles,
                       load_profile)

REPO_ROOT = os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def say(message):
    print("launch: %s" % message, file=sys.stderr)


# ----------------------------------------------------------------- flavor
def flavor_make_args(flavors, embed):
    """Flavor names -> make variables + the binary the link lands in.

    Each flavor already owns an object directory in src/makefile; nothing here
    invents a naming scheme, it only states which variables select which one.
    """
    make_args = []
    binary = "src/torirs"
    target = "torirs"

    if "debug" in flavors:
        make_args.append("OPT=0")
    if "nosimd" in flavors and "asan" not in flavors:
        make_args.append("TORIDRAW_NO_SIMD=1")
    if "tdo" in flavors:
        make_args.append("TORIDRAW_OPT=1")
    if "memtrace" in flavors:
        make_args.append("MEMTRACE=1")
        binary = "src/torirs_mt"
        target = "torirs_mt"
    if "asan" in flavors:
        # ASan on this tree is a package deal, and the parts are not optional:
        # UBSan rides along, NO_SIMD goes with it because UBSan cannot see
        # inside the vector kernels, and the whole thing gets its own objdir so
        # an unsanitized src/torirs is never clobbered.
        make_args += [
            "ENABLE_ASAN=1", "ENABLE_UBSAN=1", "TORIDRAW_NO_SIMD=1",
            "PLATFORM_OBJ_BASE=build_asan", "PLATFORM_TARGET=torirs_asan",
        ]
        binary = "src/torirs_asan"
        target = "torirs_asan"
    if embed:
        make_args.append("EMBED_SERVER=1")
    return make_args, binary, target


def _no_asan_on_windows(profile, flavors):
    """Why the asan flavor cannot run here, and the two ways to say so."""
    keep = [name for name in flavors if name != "asan"] or ["opt"]
    return "\n".join([
        "flavor 'asan' cannot be built on Windows: the lane is MinGW, and GCC",
        "ships no sanitizer runtime for that target (the link fails on a",
        "missing libsanitizer.spec). ASan and UBSan are macOS/Linux flavors.",
        "",
        "  run this world uninstrumented, once:",
        "    ./launch run %s --flavor %s" % (profile.name, ",".join(keep)),
        "",
        "  or make that the standing Windows answer, in %s:"
        % os.path.relpath(profile.path, REPO_ROOT),
        "    [profile@windows]",
        "    flavor=%s" % ",".join(keep),
    ])


def asan_env():
    return {
        "ASAN_OPTIONS": os.environ.get(
            "ASAN_OPTIONS",
            "detect_leaks=0:halt_on_error=0:abort_on_error=0"
            ":print_stacktrace=1:log_path=stderr"),
        "UBSAN_OPTIONS": os.environ.get(
            "UBSAN_OPTIONS", "print_stacktrace=1"),
    }


def _expected_port(service_name, profile, manifest):
    """Where an undeclared service would be expected to answer, if anywhere."""
    config = profile.service_config(service_name)
    if config.get("port"):
        return config["port"]
    if service_name == "torirsserver":
        transport = (manifest.transport or "").strip()
        if transport in ("tcp", "ws") and manifest.net_port:
            return manifest.net_port
        return services_mod.DEFAULT_GAME_PORT
    if service_name == "js5_server":
        return services_mod.DEFAULT_JS5_PORT
    if service_name == "torirsmaped":
        return manifest.editor_port
    return None


def _check_declared_services(profile, manifest, service_list, client):
    """Refuse a profile whose declared services cannot run its frontend.

    Services are declared, never inferred — but an omission must not be left to
    surface as a tab that never loads or a client stuck on "Connecting to
    server...". The check names the missing service, says why the run needs it,
    and prints the line to add.
    """
    declared = {service.name for service in service_list}

    # Advisory: probably wanted, but a server somebody else runs is a normal
    # setup here, so this only speaks up when the port is genuinely silent.
    for name, why in services_mod.advisory_services(
            profile, manifest, declared, client).items():
        expected = _expected_port(name, profile, manifest)
        if expected and supervisor.port_listening(expected):
            continue
        say("note: no %s declared and nothing is listening%s — %s"
            % (name, " on %s" % expected if expected else "", why))

    missing = [
        (name, why)
        for name, why in services_mod.required_services(
            profile, manifest, client).items()
        if name not in declared]
    if not missing:
        return
    # The EFFECTIVE client in the headline, not profile.client: the whole
    # reason this fires may be a --client override, and naming the declared
    # frontend there reads as a contradiction of the line under it.
    lines = [
        "profile '%s' declares services=%s, which cannot run client=%s%s:"
        % (profile.name,
           ", ".join(profile.services) or "(none)",
           client,
           " (--client override; the profile declares %s)" % profile.client
           if client != profile.client else "")]
    for name, why in missing:
        lines.append("    missing %-14s %s" % (name, why))
    complete = list(profile.services) + [name for name, _ in missing]
    lines.append("")
    if client != profile.client:
        # Editing the native profile to carry a browser's services would be
        # the wrong fix: the frontend is not the only thing a web run needs
        # differently. Point at the pattern this tree already uses instead.
        lines.append("  --client is a frontend override, and a browser run "
                     "needs more than a frontend")
        lines.append("  (transport=ws, a ws_port a browser can reach, "
                     "chrome=web). Run the profile that")
        lines.append("  states all of it, or add one beside this profile:")
        lines.append("    ./launch list        # look for a -web sibling")
    else:
        lines.append("  add to [profile] in %s:"
                     % os.path.relpath(profile.path, REPO_ROOT))
        lines.append("    services=%s" % ", ".join(complete))
    raise LaunchError("\n".join(lines))


# ------------------------------------------------------------------- plan
class Plan:
    """Everything one `run` needs, resolved before anything is started."""

    def __init__(self, profile, manifest, manifest_path, client, flavors,
                 service_list, client_argv, env, binary, make_args, target,
                 url=None, delegate=None, base_manifest_path=None):
        self.profile = profile
        self.manifest = manifest
        self.manifest_path = manifest_path
        # The world as authored, kept separate from the file actually booted:
        # once overrides generate a copy, `manifest` describes the copy, and
        # reporting that as the world would hide which manifest to go edit.
        self.base_manifest_path = base_manifest_path or manifest_path
        self.client = client
        self.flavors = flavors
        self.services = service_list
        self.client_argv = client_argv
        self.env = env
        self.binary = binary
        self.make_args = make_args
        self.target = target
        self.url = url
        self.delegate = delegate

    def to_json(self):
        return {
            "profile": self.profile.name,
            "platform": self.profile.platform,
            "description": self.profile.description,
            "world": os.path.relpath(self.base_manifest_path, REPO_ROOT),
            "manifest_used": os.path.relpath(self.manifest_path, REPO_ROOT),
            "client": self.client,
            "flavors": self.flavors,
            "binary": self.binary,
            "make_args": self.make_args,
            "client_argv": self.client_argv,
            "env": self.env,
            "url": self.url,
            "delegate": self.delegate,
            "services": [service.to_json() for service in self.services],
            "started": time.strftime("%Y-%m-%d %H:%M:%S"),
        }


def build_plan(profile, client_override=None, flavor_override=None,
               extra_args=()):
    manifest = profile.manifest()
    manifest_path = generate_resolved_manifest(
        profile, os.path.join(REPO_ROOT, "build", "manifests"))
    # Re-read through the generated file when one was written, so every later
    # question (transport, cache dir, ports) is answered by the file the run
    # actually boots rather than the base it came from.
    if manifest_path != profile.world_path:
        from .profiles import Manifest
        manifest = Manifest.load(manifest_path)

    # bootmanifest.c deliberately uses '/' as its portable manifest path
    # separator.  os.path.relpath emits '\\' on Windows; passing that spelling
    # makes the C loader see a bare filename, so manifest-relative values such
    # as ../cache.osrs239 are left relative to the process working directory.
    # Forward slashes are accepted by Windows file APIs and preserve the
    # manifest's own directory as the path-resolution base.
    manifest_rel = os.path.relpath(manifest_path, REPO_ROOT).replace("\\", "/")

    client = client_override or profile.client
    flavors = (
        [part.strip() for part in flavor_override.split(",") if part.strip()]
        if flavor_override else profile.flavor)

    transport = (manifest.transport or "").strip()
    embed = transport == "embed" and client in ("native", "headless")
    make_args, binary, target = flavor_make_args(flavors, embed)

    # Native Windows uses the repository's explicit modern-Windows lane and
    # produces a PE executable with a different name from the Unix SDL build.
    if host.IS_WINDOWS and client in ("native", "headless"):
        # The Windows lanes are MinGW, and GCC has no sanitizer runtime for
        # that target at all -- not in the repository's toolchain, not in any
        # other. The flags compile and then the link dies on a missing
        # libsanitizer.spec. Refuse the flavor by name here rather than quietly
        # dropping it: a run that says "asan" and hands back an uninstrumented
        # binary reports "no ASan errors" for the wrong reason.
        if "asan" in flavors:
            raise LaunchError(_no_asan_on_windows(profile, flavors))
        debug = "debug" in flavors
        if profile.windows_lane == "win32":
            # The XP lane. Different target, and a DIFFERENT BINARY NAME -- the
            # win32 makefile writes src/torirs.exe, which is also what
            # build_winxp.ps1 produces and what gets pushed to the box, so a
            # profile and a hand build cannot disagree about which file is
            # under test.
            binary = "src/torirs.exe"
            target = "winxp-debug" if debug else "winxp"
        else:
            binary = "src/torirs_win64.exe"
            target = "win64-debug" if debug else "win64"
        make_args = [arg for arg in make_args if arg != "OPT=0"]

    service_list = []
    delegate = None
    if client == "runelite":
        # run-runelite.sh owns the jar surgery AND its own service lifecycle,
        # with pidfiles of its own. Deriving javconfig/torirsserver here too
        # would mean two supervisors racing for the same two ports, so this
        # frontend delegates wholesale rather than half-managing it.
        delegate = "run-runelite.sh"
    else:
        # Services run with cwd=repo root, so they get the same repo-relative
        # manifest path the client is given. Keeping the two identical is what
        # makes io_server's GET /boot/<path> serve the page the very file the
        # native client would have opened.
        service_list = services_mod.build_services(
            profile, manifest, manifest_rel, REPO_ROOT)
        _check_declared_services(profile, manifest, service_list, client)

    session = profile.session
    user = session.get("user") or manifest.user or "asdf"
    password = session.get("pass") or manifest.password or "a"

    client_argv = []
    url = None
    env = dict(profile.env)

    if client in ("native", "headless"):
        client_argv = [
            "./" + binary,
            "--manifest", manifest_rel,
            "--user", user,
            "--pass", password,
        ] + list(profile.client_args) + list(extra_args)
        if embed:
            env.setdefault("TORIRS_TRANSPORT", "embed")
        if client == "headless":
            # A headless run must terminate on its own, or it is a hung job
            # rather than a capture. Give it a frame cap unless one is stated.
            env.setdefault("SDL_VIDEODRIVER", "dummy")
            env.setdefault("TORIRS_MAX_FRAMES", "60")
    elif client in ("web", "web-idb"):
        args = ["--manifest", manifest_rel, "--user", user, "--pass", password]
        args += list(profile.client_args) + list(extra_args)
        # The page is served by the io_server this profile declared, so the URL
        # takes that service's port rather than a second copy of the number.
        web_port = next(
            (service.port for service in service_list
             if service.name == "io_server"),
            services_mod.DEFAULT_WEB_PORT)
        query = {"args": ",".join(args)}
        # Point the page at the js5_server this profile declared. Without this
        # the host page falls back to its own default of 43594 — which is the
        # GAME port, because ToriRSServer serves game and JS5 on one socket —
        # and the IndexedDB lane tries to prime its cache from a server that
        # answers by closing the connection. Starting the right service is only
        # half of owning it; the client has to be told where it is.
        js5 = next(
            (service for service in service_list
             if service.name == "js5_server"), None)
        if js5:
            query["js5_host"] = "localhost"
            query["js5_port"] = js5.port
        url = "http://localhost:%s/?%s" % (
            web_port, urllib.parse.urlencode(query))
        target = "web-idb" if client == "web-idb" else "web"
        make_args = []

    if "asan" in flavors:
        env.update(asan_env())

    return Plan(profile, manifest, manifest_path, client, flavors,
                service_list, client_argv, env, binary, make_args, target,
                url=url, delegate=delegate,
                base_manifest_path=profile.world_path)


# --------------------------------------------------------------- commands
def cmd_list(args):
    profiles = list_profiles(REPO_ROOT)
    if not profiles:
        say("no profiles in %s" % os.path.join(REPO_ROOT, "profiles"))
        return 1
    width = max(len(profile.name) for profile in profiles)
    for profile in profiles:
        try:
            client = profile.client
        except LaunchError:
            client = "?"
        print("  %-*s  %-9s  %s"
              % (width, profile.name, client, profile.description))
    return 0


def cmd_show(args):
    profile = load_profile(REPO_ROOT, args.profile)
    plan = build_plan(profile, args.client, args.flavor, args.args)
    print("profile     %s" % profile.name)
    print("platform    %s" % profile.platform)
    print("description %s" % profile.description)
    print("world       %s" % os.path.relpath(plan.base_manifest_path, REPO_ROOT))
    if plan.manifest_path != profile.world_path:
        print("resolved    %s  (overrides applied)"
              % os.path.relpath(plan.manifest_path, REPO_ROOT))
    print("client      %s" % plan.client)
    print("flavor      %s" % (",".join(plan.flavors) or "(default)"))
    print("transport   %s" % (plan.manifest.transport or "(none)"))
    print("chrome      %s" % (plan.manifest.chrome_executor or "(default)"))
    print("cache       %s" % (plan.manifest.cache_dir or "(none)"))
    lanes = plan.manifest.lanes
    print("lanes       %s" % (", ".join(lanes) if lanes else "(tree defaults)"))

    derived = plan.manifest.derived()
    print("derived     %s"
          % (", ".join(name for name, _ in derived) if derived
             else "(none declared — prepared via run-live.sh)"))

    if plan.delegate:
        print("delegate    %s (owns its own services)" % plan.delegate)
    print("services    %s"
          % ("none" if not plan.services else ""))
    for service in plan.services:
        print("    %-14s port %-6s %s"
              % (service.name, service.port or "-", service.description))
        print("        %s" % " ".join(service.argv))
    if plan.make_args or plan.target:
        _, display = host.make_command(
            REPO_ROOT, plan.target, plan.make_args, parallel=True)
        print("build       %s" % display)
    if plan.client_argv:
        print("client      %s" % " ".join(plan.client_argv))
    if plan.url:
        print("url         %s" % plan.url)
    if plan.env:
        print("env         %s"
              % " ".join("%s=%s" % item for item in sorted(plan.env.items())))
    return 0


def _run_make(target, make_args, extra_env=None):
    argv, display = host.make_command(
        REPO_ROOT, target, make_args, parallel=True)
    say(display)
    env = dict(os.environ)
    if extra_env:
        env.update({str(k): str(v) for k, v in extra_env.items()})
    return subprocess.run(argv, cwd=REPO_ROOT, env=env).returncode


def _prepare(plan, skip_checks, force):
    if skip_checks:
        say("--skip-checks: running the cache and script pack as they stand")
        return True
    if plan.manifest.derived():
        for name, where in staleness.coverage_gaps(plan.manifest):
            say("warning: %s declares [derived:*] blocks but none for '%s'"
                % (os.path.relpath(plan.base_manifest_path, REPO_ROOT), name))
            say("  %s is named but will NOT be checked for staleness by anything"
                % where)
        results, ok = staleness.prepare_derived(
            plan.manifest, REPO_ROOT, force=force)
        for result in results:
            say("%s: %s%s"
                % (result.name, result.action,
                   " — %s" % result.detail if result.detail else ""))
        return ok
    ok, detail = staleness.prepare_via_run_live(
        os.path.relpath(plan.manifest_path, REPO_ROOT), REPO_ROOT, plan.env)
    say(detail)
    return ok


def _ensure_services_built(plan):
    for service in plan.services:
        if not service.build_target:
            continue
        binary = service.binary_candidates[0] if service.binary_candidates else None
        # services_mod.built_binary, not os.path.isfile: the candidate is a
        # makefile target path and the linker adds the platform's suffix.
        if binary and services_mod.built_binary(REPO_ROOT, binary):
            continue
        say("building %s (%s)" % (service.name, service.build_target))
        if _run_make(service.build_target, []) != 0:
            say("failed to build %s" % service.name)
            return False
    return True


def _start_services(plan):
    started = []
    for service in plan.services:
        say("starting %s%s"
            % (service.name,
               " on port %s" % service.port if service.port else ""))
        pid, reason = supervisor.start_service(
            REPO_ROOT, plan.profile.name, service, env=plan.env)
        if pid is None:
            say("%s did not come up: %s" % (service.name, reason))
            tail = supervisor.log_tail(REPO_ROOT, plan.profile.name, service.name)
            if tail:
                # Print the reason here rather than pointing at a file. These
                # servers explain themselves clearly when they refuse to start,
                # and that explanation is the whole answer.
                say("  last lines of its log:")
                for line in tail:
                    print("      %s" % line, file=sys.stderr)
            say("  full log: %s"
                % os.path.relpath(
                    supervisor.logfile_path(
                        REPO_ROOT, plan.profile.name, service.name), REPO_ROOT))
            for done in reversed(started):
                supervisor.stop_pid(done)
            return False
        started.append(pid)
        say("  %s up (pid %d)" % (service.name, pid))
    return True


class _Signalled(KeyboardInterrupt):
    """SIGTERM or SIGHUP, raised where a Ctrl-C would be.

    So that one path — the `finally` in cmd_run — takes the run down whether
    the user pressed Ctrl-C, closed the terminal, or `kill`ed the launcher.
    Without this, the default disposition kills the launcher outright and its
    services are left holding their ports with nothing watching them.
    """


@contextlib.contextmanager
def _dying_is_an_exception():
    """Turn SIGTERM/SIGHUP into _Signalled for the duration of a block."""

    def handler(signum, frame):
        raise _Signalled("signal %d" % signum)

    previous = {}
    for name in ("SIGTERM", "SIGHUP"):
        signum = getattr(signal, name, None)
        if signum is None:
            continue
        try:
            previous[signum] = signal.signal(signum, handler)
        except (OSError, ValueError):
            pass
    try:
        yield
    finally:
        for signum, old_handler in previous.items():
            try:
                signal.signal(signum, old_handler)
            except (OSError, ValueError):
                pass


def _resolve_running_services(profile, live, args):
    """This profile's services are already up. Restart them, or stop here.

    Re-running a profile that is already running is the ordinary way to pick up
    a change, so the answer is nearly always "restart" -- which is why this
    asks instead of refusing. It refuses only where it cannot ask: a
    non-interactive run (CI, a script, a pipe) must not block on a prompt, and
    must not quietly kill a process the caller did not mention either, so there
    it keeps the old behaviour and names the flag that says yes in advance.

    Returns True when the caller may proceed.
    """
    listing = ", ".join("%s(pid %s)" % (row["name"], row["pid"]) for row in live)
    say("this profile already has services running: %s" % listing)

    if args.restart:
        answer = "y"
    elif not _stdin_is_interactive():
        say("stop them first:  ./launch stop %s" % profile.name)
        say("or pass --restart to stop and start them in one step")
        return False
    else:
        try:
            answer = input("launch: stop them and start again? [Y/n] ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            # A Ctrl-C or a closed stdin at the prompt is "no", and the run has
            # started nothing yet, so there is nothing to unwind.
            print("")
            return False

    if answer not in ("", "y", "yes"):
        say("left running:  ./launch stop %s" % profile.name)
        return False

    for service_name, outcome in supervisor.stop_run_now(REPO_ROOT, profile.name):
        say("  %-14s %s" % (service_name, outcome))

    still = [row for row in supervisor.run_status(REPO_ROOT, profile.name)["services"]
             if row["state"] == "running"]
    if still:
        # Reported rather than pressed on with: starting a second copy over a
        # service that would not die is how a port conflict gets blamed on the
        # wrong process.
        say("could not stop: %s"
            % ", ".join("%s(pid %s)" % (row["name"], row["pid"]) for row in still))
        return False
    return True


def _stdin_is_interactive():
    try:
        return sys.stdin is not None and sys.stdin.isatty()
    except (AttributeError, ValueError):
        return False


def cmd_run(args):
    profile = load_profile(REPO_ROOT, args.profile)
    plan = build_plan(profile, args.client, args.flavor, args.args)

    existing = supervisor.run_status(REPO_ROOT, profile.name)
    live = [row for row in existing["services"] if row["state"] == "running"]
    if live and not _resolve_running_services(profile, live, args):
        return 1

    # From here down this process OWNS whatever it starts, and every exit path
    # runs the same shutdown: the client exiting, Ctrl-C at any point —
    # including one landing in the middle of the shutdown itself — a SIGTERM,
    # or an exception. A service that outlives the launcher goes on holding
    # its port, which presents on the next run as "port already in use" with
    # nothing visible to blame. The one deliberate exception is a run that
    # asked for the services to outlive it (--detach, --no-client) AND got far
    # enough to hand them over; an interrupted startup is not that.
    leave_up = False
    with _dying_is_an_exception():
        try:
            code, leave_up = _run_plan(plan, args)
            return code
        finally:
            if not leave_up:
                _stop_quietly(profile.name)


def _run_plan(plan, args):
    """Do the run. Returns (exit code, whether the services should stay up)."""
    profile = plan.profile
    if not _prepare(plan, args.skip_checks, args.force_bake):
        return 1, False

    if plan.delegate:
        say("delegating to %s (it owns jar patching and its own services)"
            % plan.delegate)
        argv = ["./" + plan.delegate,
                os.path.relpath(plan.manifest_path, REPO_ROOT)]
        env = dict(os.environ)
        env["TORIRS_NO_PREPARE"] = "1"
        env.update({str(k): str(v) for k, v in plan.env.items()})
        # The delegate started nothing of ours and stops its own services, so
        # there is nothing here to take down.
        return subprocess.run(argv, cwd=REPO_ROOT, env=env).returncode, True

    if plan.target and not args.no_build:
        if _run_make(plan.target, plan.make_args, plan.env) != 0:
            say("client build failed")
            return 1, False

    if not _ensure_services_built(plan):
        return 1, False

    supervisor.write_plan(REPO_ROOT, profile.name, plan.to_json())

    if not _start_services(plan):
        return 1, False

    if args.no_client:
        say("services up; --no-client, so nothing else is started")
        say("status:  ./launch status %s" % profile.name)
        say("stop:    ./launch stop %s" % profile.name)
        return 0, True

    if plan.url:
        say(plan.url)
        if not args.no_open:
            _open_browser(plan.url)
        if args.detach:
            say("services up; --detach, so they keep running without me")
            say("stop:    ./launch stop %s" % profile.name)
            return 0, True
        say("serving — Ctrl-C to stop (services stop with it)")
        return _supervise_until_interrupt(plan), False

    if args.detach:
        say("--detach with a native client would leave nothing to watch; "
            "starting the client in the foreground")
    env = dict(os.environ)
    env.update({str(k): str(v) for k, v in plan.env.items()})
    say(" ".join(plan.client_argv))
    code = subprocess.run(plan.client_argv, cwd=REPO_ROOT, env=env).returncode
    return code, args.detach


def _open_browser(url):
    for opener in ("open", "xdg-open"):
        try:
            subprocess.run([opener, url], check=False,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            return
        except FileNotFoundError:
            continue


def _supervise_until_interrupt(plan):
    """Hold a web run open, and take the whole run down if a service dies.

    A page server still answering while the game server behind it is gone is
    worse than a stopped run: the tab loads, the world never arrives, and
    nothing says why.
    """
    try:
        while True:
            time.sleep(1.0)
            status = supervisor.run_status(REPO_ROOT, plan.profile.name)
            for row in status["services"]:
                if row["state"] in ("dead", "orphan"):
                    say("%s stopped unexpectedly — stopping the run"
                        % row["name"])
                    say("  log: %s"
                        % os.path.relpath(
                            supervisor.logfile_path(
                                REPO_ROOT, plan.profile.name, row["name"]),
                            REPO_ROOT))
                    return 1
    except KeyboardInterrupt:
        say("interrupted")
        return 130


def _stop_quietly(profile_name):
    for name, outcome in supervisor.stop_run_now(REPO_ROOT, profile_name):
        if outcome.startswith("stopped"):
            say("stopped %s" % name)


# ------------------------------------------------------------------- bench
def _bench_selection(args, suite):
    """The scenes, renderers and repeat count this invocation asks for."""
    scenes = suite.select(
        [name.strip() for name in (args.scene or "").split(",") if name.strip()])
    renderers = [name.strip()
                 for name in (args.renderer or "").split(",") if name.strip()]
    if renderers:
        unknown = [name for name in renderers if name not in bench.RENDERER_FLAGS]
        if unknown:
            raise LaunchError(
                "--renderer names %s; this client has %s"
                % (", ".join(unknown), ", ".join(sorted(bench.RENDERER_FLAGS))))
    else:
        renderers = suite.renderers
    return scenes, renderers, args.repeat or suite.repeat


def _bench_run_one(plan, run, shots, timeout):
    """One client process. Returns its exit code, or None if it was killed."""
    env = dict(os.environ)
    env.update({str(key): str(value) for key, value in plan.env.items()})
    env.update(run.env(shots=shots))
    if shots:
        # The client's bmp writer does not create its directory; it
        # reports the failed open and carries on rendering, so a missing
        # directory costs a shot rather than a run.
        os.makedirs(run.shot_dir, exist_ok=True)
    argv = list(plan.client_argv) + run.args()
    # stdout and stderr both into the run's log. The perf report the client
    # prints at exit is on stderr, and it is what you go and read when a
    # number in the table looks wrong.
    with open(run.log_path, "w", encoding="utf-8", errors="replace") as log:
        log.write("$ %s\n\n" % " ".join(argv))
        log.flush()
        try:
            return subprocess.run(
                argv, cwd=REPO_ROOT, env=env, stdout=log,
                stderr=subprocess.STDOUT, timeout=timeout).returncode
        except subprocess.TimeoutExpired:
            log.write("\n\nlaunch: killed after %ds\n" % timeout)
            return None


def cmd_bench(args):
    profile = load_profile(REPO_ROOT, args.profile)
    plan = build_plan(profile, args.client, args.flavor, args.args)
    if plan.client not in ("native", "headless"):
        raise LaunchError(
            "bench needs a native client to time; profile '%s' is client=%s"
            % (profile.name, plan.client))

    world = os.path.relpath(plan.base_manifest_path, REPO_ROOT)
    suite = bench.load_suite(plan.manifest)
    if not suite.scenes:
        raise LaunchError(
            "%s declares no [bench:<scene>] blocks, so there is nothing to "
            "measure" % world)
    scenes, renderers, repeat = _bench_selection(args, suite)

    if args.list:
        say("%s - cache %s" % (world, os.path.relpath(suite.cache_dir, REPO_ROOT)))
        say("renderers: %s" % ", ".join(renderers))
        for scene in scenes:
            print("%-24s %s" % (scene.name, scene.summary_line()))
            if scene.description:
                print("%-24s %s" % ("", scene.description))
        return 0

    stamp = time.strftime("%Y%m%d-%H%M%S")
    out_dir = os.path.join(REPO_ROOT, "build", "bench", profile.name, stamp)
    os.makedirs(out_dir, exist_ok=True)

    if not _prepare(plan, args.skip_checks, False):
        return 1
    if plan.target and not args.no_build:
        if _run_make(plan.target, plan.make_args, plan.env) != 0:
            say("client build failed")
            return 1

    runs = bench.plan_runs(suite, scenes, renderers, repeat, out_dir)
    say("%d run%s into %s"
        % (len(runs), "" if len(runs) == 1 else "s",
           os.path.relpath(out_dir, REPO_ROOT)))

    rows = []
    failed = []
    for index, run in enumerate(runs, 1):
        say("[%d/%d] %s - %d frames"
            % (index, len(runs), run.stem, run.scene.total_frames))
        code = _bench_run_one(plan, run, args.shots, args.timeout)
        samples = bench.read_windows(run.windows_csv_path)
        row = bench.summarise(run, samples)
        row["exit_code"] = code
        rows.append(row)
        if code is None:
            failed.append("%s: killed after %ds" % (run.stem, args.timeout))
        elif code != 0:
            failed.append("%s: exit %d" % (run.stem, code))
        elif row["samples_kept"] == 0:
            failed.append(
                "%s: no samples - see %s"
                % (run.stem, os.path.relpath(run.log_path, REPO_ROOT)))

    print(bench.format_table(rows))
    print("")
    say("milliseconds. p50/p95 are medians across the kept samples of a run, "
        "'worst p95' the largest single sample")

    summary = {
        "profile": profile.name,
        "world": world,
        "manifest_used": os.path.relpath(plan.manifest_path, REPO_ROOT),
        "cache": os.path.relpath(suite.cache_dir, REPO_ROOT),
        "revision": plan.manifest.revision,
        "flavors": plan.flavors,
        "binary": plan.binary,
        "renderers": renderers,
        "repeat": repeat,
        "stamp": stamp,
        "runs": rows,
    }
    summary_path = os.path.join(out_dir, "summary.json")
    with open(summary_path, "w", encoding="utf-8") as handle:
        json.dump(summary, handle, indent=2)
    say("summary: %s" % os.path.relpath(summary_path, REPO_ROOT))

    if args.baseline:
        baseline_path = args.baseline
        if os.path.isdir(baseline_path):
            baseline_path = os.path.join(baseline_path, "summary.json")
        if not os.path.isfile(baseline_path):
            raise LaunchError("no bench summary at '%s'" % baseline_path)
        with open(baseline_path, encoding="utf-8") as handle:
            baseline = json.load(handle)
        if baseline.get("cache") != summary["cache"]:
            # Not fatal, but it has to be said: a different cache is
            # different geometry, and a percentage measured against
            # different geometry is not a renderer measurement.
            say("warning: baseline was measured against cache %s, this run "
                "against %s" % (baseline.get("cache"), summary["cache"]))
        print("")
        print(bench.format_deltas(rows, baseline.get("runs", [])))
        print("")
        say("vs %s - positive is slower now"
            % os.path.relpath(baseline_path, REPO_ROOT))

    for problem in failed:
        say(problem)

    if suite.budget_ms is not None and not args.no_gate:
        breached = bench.over_budget(rows, suite.budget_ms)
        for row, value in breached:
            say("over budget: %s/%s frame p95 %.2f ms > %.2f ms"
                % (row["scene"], row["renderer"], value, suite.budget_ms))
        if breached:
            return 1
    return 1 if failed else 0


def cmd_status(args):
    names = [args.profile] if args.profile else supervisor.known_runs(REPO_ROOT)
    if not names:
        print("no runs on record (build/run/ is empty)")
        return 0
    any_live = False
    for name in names:
        status = supervisor.run_status(REPO_ROOT, name)
        rows = status["services"]
        plan = status["plan"] or {}
        header = "%s  [%s]" % (name, plan.get("client", "?"))
        print(header)
        if plan.get("url"):
            print("    url  %s" % plan["url"])
        if not rows:
            print("    (no services — client-only run)")
        for row in rows:
            mark = {"running": "up", "dead": "DEAD", "orphan": "ORPHAN",
                    "stopped": "--"}.get(row["state"], row["state"])
            if row["state"] == "running":
                any_live = True
            print("    %-14s %-7s pid %-8s port %-6s %s"
                  % (row["name"], mark, row["pid"] or "-", row["port"] or "-",
                     row["description"]))
        print()
    return 0 if any_live or args.profile else 0


def cmd_stop(args):
    if args.all:
        names = supervisor.known_runs(REPO_ROOT)
    elif args.profile:
        names = [args.profile]
    else:
        say("name a profile, or pass --all")
        return 1
    if not names:
        print("nothing to stop")
        return 0
    for name in names:
        results = supervisor.stop_run_now(REPO_ROOT, name)
        if not results:
            continue
        print("%s:" % name)
        for service_name, outcome in results:
            print("    %-14s %s" % (service_name, outcome))
    return 0


def cmd_logs(args):
    directory = supervisor.run_dir(REPO_ROOT, args.profile)
    if not os.path.isdir(directory):
        say("no run state for '%s'" % args.profile)
        return 1
    logs = sorted(name for name in os.listdir(directory)
                  if name.endswith(".log"))
    if not logs:
        say("no logs for '%s'" % args.profile)
        return 1
    if args.service:
        wanted = args.service + ".log"
        if wanted not in logs:
            say("no log '%s' (have: %s)"
                % (args.service, ", ".join(name[:-4] for name in logs)))
            return 1
        logs = [wanted]
    paths = [os.path.join(directory, name) for name in logs]
    if args.follow:
        return subprocess.run(["tail", "-f"] + paths).returncode
    return subprocess.run(["tail", "-n", str(args.lines)] + paths).returncode


def cmd_completion(args):
    """Print the shell glue, so installing it is one line of copy-paste."""
    from . import completion

    shell = args.shell
    if not shell:
        shell = os.path.basename(os.environ.get("SHELL", "")) or "bash"
    if shell not in completion.SHELLS:
        raise LaunchError("no completion script for '%s' (have: %s)"
                          % (shell, ", ".join(sorted(completion.SHELLS))))
    body = completion.script(shell)
    if body is None:
        raise LaunchError("completion script for '%s' is missing from %s"
                          % (shell, os.path.join("tools", "launcher",
                                                 "completions")))
    sys.stdout.write(body)
    if sys.stdout.isatty():
        # Only when a human is looking: piping this into a file or eval must
        # get the script and nothing else.
        say("install with:  eval \"$(./launch completion %s)\"" % shell)
    return 0


def cmd_doctor(args):
    problems = []
    checks = []
    notes = []

    submodule = os.path.join(REPO_ROOT, "OSRS-Content", "osrs239-content")
    if os.path.isdir(submodule):
        checks.append(("content submodule", "present"))
    else:
        problems.append("OSRS-Content is not checked out — "
                        "git submodule update --init")

    if os.path.isdir(os.path.join(REPO_ROOT, "profiles")):
        checks.append(("profile registry",
                       "%d profiles" % len(list_profiles(REPO_ROOT))))
    else:
        problems.append("no profiles/ directory")

    profiles = [load_profile(REPO_ROOT, args.profile)] if args.profile \
        else list_profiles(REPO_ROOT)

    # Ports first, across every profile at once: two profiles that both want
    # 43594 cannot run together, and finding that out as "port already in use"
    # halfway through a start is a worse way to learn it.
    claims = {}
    for profile in profiles:
        for name in profile.services:
            port = profile.service_config(name).get("port")
            if port:
                claims.setdefault(port, []).append("%s/%s" % (profile.name, name))
    for port, owners in sorted(claims.items()):
        if len(owners) > 1:
            notes.append("port %s is claimed by %s — those profiles cannot run "
                         "at the same time" % (port, ", ".join(owners)))
        if supervisor.port_listening(port):
            notes.append("port %s is in use right now (wanted by %s)"
                         % (port, ", ".join(owners)))

    needs_web = any(profile.client in ("web", "web-idb") for profile in profiles)
    if needs_web:
        module = os.path.join(REPO_ROOT, "build-web", "torirs.js")
        if os.path.isfile(module):
            checks.append(("web module", "build-web/torirs.js present"))
        else:
            problems.append(
                "a web profile exists but build-web/torirs.js is missing — "
                "make -C src web")

    if any(profile.client == "runelite" for profile in profiles):
        toolchain = os.path.join(REPO_ROOT, "toolchains", "unpacked")
        vendored = os.path.join(
            REPO_ROOT, "toolchains", "java-toolchain-osrs239.zip")
        if os.path.isdir(toolchain) or os.path.isfile(vendored):
            checks.append(("java toolchain", "available for the runelite lane"))
        else:
            problems.append(
                "a runelite profile exists but neither toolchains/unpacked nor "
                "toolchains/java-toolchain-osrs239.zip is present")

    for profile in profiles:
        try:
            manifest = profile.manifest()
        except LaunchError as error:
            problems.append("%s: %s" % (profile.name, error))
            continue
        cache = manifest.cache_dir
        if cache and not os.path.isdir(cache):
            problems.append(
                "%s: cache '%s' does not exist"
                % (profile.name, os.path.relpath(cache, REPO_ROOT)))
        scripts = manifest.server_scripts
        if scripts and not os.path.isdir(scripts):
            problems.append(
                "%s: script pack '%s' not built"
                % (profile.name, os.path.relpath(scripts, REPO_ROOT)))
        for gap, where in staleness.coverage_gaps(manifest):
            problems.append(
                "%s: declares [derived:*] but nothing for '%s' — %s is named "
                "and will not be staleness-checked at all"
                % (profile.name, gap, where))

    for name, detail in checks:
        print("  ok    %-22s %s" % (name, detail))
    for note in notes:
        print("  note  %s" % note)
    for problem in problems:
        print("  FAULT %s" % problem)
    if not problems:
        print("\nno problems found%s"
              % (" (%d note%s above)" % (len(notes), "" if len(notes) == 1 else "s")
                 if notes else ""))
    return 1 if problems else 0


# ------------------------------------------------------------------ entry
def main(argv=None):
    if argv is None:
        argv = sys.argv[1:]
    if argv and argv[0] == "complete":
        # The shell's own question, answered before argparse can object to a
        # half-typed word. Not a user-facing subcommand, so it is not declared
        # below; see tools/launcher/completions/ for the callers.
        from . import completion

        words = argv[1:]
        if words and words[0] == "--":
            words = words[1:]
        try:
            completion.emit(REPO_ROOT, words, sys.stdout)
        except Exception:
            # A completer that prints a traceback into the command line is
            # worse than one that offers nothing.
            return 1
        return 0

    parser = argparse.ArgumentParser(
        prog="launch",
        description="Run a named configuration of this tree.")
    sub = parser.add_subparsers(dest="command")

    sub.add_parser("list", help="every profile in the registry")

    show = sub.add_parser("show", help="the resolved plan, without running it")
    show.add_argument("profile")
    show.add_argument("--client")
    show.add_argument("--flavor")
    show.add_argument("args", nargs="*")

    run = sub.add_parser("run", help="prepare, build, start services, launch")
    run.add_argument("profile")
    run.add_argument("--client", help="override the profile's frontend")
    run.add_argument("--flavor", help="override the profile's build flavor")
    run.add_argument("--skip-checks", action="store_true",
                     help="run the cache and script pack exactly as they are")
    run.add_argument("--force-bake", action="store_true",
                     help="rebuild every [derived:*] artifact without asking")
    run.add_argument("--no-build", action="store_true",
                     help="skip the client build")
    run.add_argument("--no-client", action="store_true",
                     help="start the services and stop there")
    run.add_argument("--detach", action="store_true",
                     help="leave services running after this command exits")
    run.add_argument("--restart", action="store_true",
                     help="if this profile's services are already running, "
                          "stop them and start again without asking")
    run.add_argument("--no-open", action="store_true",
                     help="print the URL instead of opening a browser")
    run.add_argument("args", nargs="*", help="extra client arguments")

    bench_cmd = sub.add_parser(
        "bench", help="time the world's [bench:*] scenes")
    bench_cmd.add_argument("profile")
    bench_cmd.add_argument("--scene",
                           help="comma-separated scene names (default: all)")
    bench_cmd.add_argument("--renderer",
                           help="comma-separated renderers, overriding [bench]")
    bench_cmd.add_argument("--repeat", type=int,
                           help="whole client processes per scene and renderer")
    bench_cmd.add_argument("--baseline",
                           help="an earlier run directory or summary.json to "
                                "report deltas against")
    bench_cmd.add_argument("--shots", action="store_true",
                           help="dump one BMP per run at the sample camera")
    bench_cmd.add_argument("--list", action="store_true",
                           help="print the suite and stop")
    bench_cmd.add_argument("--client", help="override the profile's frontend")
    bench_cmd.add_argument("--flavor",
                           help="override the profile's build flavor")
    bench_cmd.add_argument("--no-build", action="store_true",
                           help="skip the client build")
    bench_cmd.add_argument("--skip-checks", action="store_true",
                           help="run the cache exactly as it is")
    bench_cmd.add_argument("--no-gate", action="store_true",
                           help="report a budget_ms breach without failing")
    bench_cmd.add_argument("--timeout", type=int, default=900,
                           help="seconds before one run is killed")
    bench_cmd.add_argument("args", nargs="*", help="extra client arguments")

    status = sub.add_parser("status", help="what is running")
    status.add_argument("profile", nargs="?")

    stop = sub.add_parser("stop", help="stop a run's services")
    stop.add_argument("profile", nargs="?")
    stop.add_argument("--all", action="store_true")

    logs = sub.add_parser("logs", help="a run's service logs")
    logs.add_argument("profile")
    logs.add_argument("service", nargs="?")
    logs.add_argument("-f", "--follow", action="store_true")
    logs.add_argument("-n", "--lines", type=int, default=40)

    doctor = sub.add_parser("doctor", help="preflight this checkout")
    doctor.add_argument("profile", nargs="?")

    shell = sub.add_parser("completion",
                           help="print the shell tab-completion script")
    shell.add_argument("shell", nargs="?", help="bash or zsh ($SHELL by default)")

    args = parser.parse_args(argv)
    if not args.command:
        parser.print_help()
        return 1

    handlers = {
        "list": cmd_list, "show": cmd_show, "run": cmd_run,
        "bench": cmd_bench,
        "status": cmd_status, "stop": cmd_stop, "logs": cmd_logs,
        "doctor": cmd_doctor, "completion": cmd_completion,
    }
    try:
        return handlers[args.command](args)
    except LaunchError as error:
        say(str(error))
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
