"""Which long-lived processes a run starts, and how to start each one.

A profile DECLARES its services by name, in start order:

    [profile]
    services=torirsserver, io_server, js5_server

    [service:io_server]
    port=8088
    root=build-web

Declared rather than inferred, so that reading the profile tells you the whole
process set — no rule elsewhere to know, and no surprise process appearing
because a manifest field changed somewhere else.

What the launcher will NOT do is let an omission become a mystery at runtime. A
`client=web` run with no io_server declared cannot work, and the failure would
otherwise arrive as a browser tab that never loads. So the declaration is
checked against what the frontend actually needs, and a missing one is named
before anything starts — see required_services().
"""

import os

# Ports that are conventions rather than settings. Each is overridable in the
# service's own section; these are the defaults the rest of the tree assumes.
DEFAULT_GAME_PORT = "43594"
DEFAULT_JS5_PORT = "43595"
DEFAULT_WEB_PORT = "8088"
DEFAULT_JAV_PORT = "8080"
DEFAULT_MAPED_PORT = "43610"

# The server-side binaries are built at OPT=1 into src/build_opt (the path
# run-live.sh defaults TORIRS_MOCK_BIN to). io_server is the exception: the
# makefile pins every flavor variable off for it, so it is always src/build.
SERVER_OBJDIR = "build_opt"
IO_SERVER_OBJDIR = "build"

SERVICE_NAMES = (
    "torirsserver", "io_server", "js5_server", "javconfig", "torirsmaped")


class Service:
    """One supervised process.

    `ready` is a (kind, target) spec rather than a callable so a plan can be
    written to disk and still describe how to re-check the service later:
      ("tcp", port)     the port accepts a connection
      ("http", url)     the URL answers
      (None, None)      nothing to wait for
    """

    def __init__(self, name, argv, build_target=None, binary_candidates=(),
                 port=None, ready=(None, None), description=""):
        self.name = name
        self.argv = argv
        self.build_target = build_target
        self.binary_candidates = binary_candidates
        self.port = port
        self.ready = ready
        self.description = description

    def to_json(self):
        return {
            "name": self.name,
            "argv": self.argv,
            "port": self.port,
            "ready_kind": self.ready[0],
            "ready_target": self.ready[1],
            "description": self.description,
        }


def _first_existing(repo_root, candidates):
    for candidate in candidates:
        path = os.path.join(repo_root, candidate)
        if os.path.isfile(path) and os.access(path, os.X_OK):
            return candidate
    return candidates[0] if candidates else None


# ------------------------------------------------------------- builders
def _build_torirsserver(config, context):
    candidates = (
        "src/%s/torirsserver" % SERVER_OBJDIR,
        "src/build/torirsserver",
    )
    binary = _first_existing(context["repo_root"], candidates)
    port = config.get("port") or _default_game_port(context)
    argv = ["./" + binary, str(port)]
    rev = config.get("rev") or context["manifest"].rev
    if rev:
        argv += ["--rev", rev]
    return Service(
        name="torirsserver",
        argv=argv,
        build_target="torirsserver",
        binary_candidates=candidates,
        port=str(port),
        # NOT a port probe. This server binds early to fail fast on a port
        # conflict, then spends ~10s loading scripts, db tables and content —
        # and can still refuse to run at the end of it (a stale script pack is
        # the common one). A connect during that window succeeds against a
        # server that is not serving and may never serve, which is exactly the
        # false "up" this replaces. It prints this line when it is genuinely
        # ready, so that is what we wait for.
        ready=("log", "listening on"),
        description="game world + JS5 on one socket (first byte picks)",
    )


def _default_game_port(context):
    """The port a game server should bind when the profile names none.

    An EMBED manifest's `[net:boot] port=` is a placeholder for a transport
    that never binds, so it must not be taken literally once a frontend that
    cannot embed has forced a real server to exist.
    """
    manifest = context["manifest"]
    transport = (manifest.transport or "").strip()
    if transport in ("tcp", "ws") and manifest.net_port:
        return manifest.net_port
    return DEFAULT_GAME_PORT


def _build_io_server(config, context):
    candidates = ("src/%s/io_server" % IO_SERVER_OBJDIR,)
    binary = _first_existing(context["repo_root"], candidates)
    port = config.get("port") or DEFAULT_WEB_PORT
    root = config.get("root") or "build-web"
    argv = [
        "./" + binary,
        "--manifest", context["manifest_path"],
        "--root", root,
        "--port", str(port),
    ]
    if config.get("boot_root"):
        argv += ["--boot-root", config["boot_root"]]
    return Service(
        name="io_server",
        argv=argv,
        build_target="io-server",
        binary_candidates=candidates,
        port=str(port),
        ready=("http", "http://127.0.0.1:%s/" % port),
        description="serves %s, POST /io cache reads, GET /boot/" % root,
    )


def _build_js5_server(config, context):
    candidates = (
        "src/%s/js5_server" % SERVER_OBJDIR,
        "src/build/js5_server",
    )
    binary = _first_existing(context["repo_root"], candidates)
    port = config.get("port") or DEFAULT_JS5_PORT
    cache = config.get("cache") or context["manifest"].cache_dir
    revision = config.get("revision") or context["manifest"].revision
    argv = ["./" + binary, "--cache", cache, "--port", str(port)]
    if revision:
        argv += ["--revision", str(revision)]
    return Service(
        name="js5_server",
        argv=argv,
        build_target="js5-server",
        binary_candidates=candidates,
        port=str(port),
        ready=("tcp", str(port)),
        description="cache over raw TCP or WebSocket (byte-sniffed)",
    )


def _build_javconfig(config, context):
    manifest = context["manifest"]
    port = config.get("port") or DEFAULT_JAV_PORT
    cachedir = config.get("cachedir") or os.path.expanduser(
        "~/jagexcache/torirs-%s" % (manifest.rev or "osrs239"))
    argv = [
        "python3", "tools/torirs_javconfig.py",
        "--host", config.get("host") or "127.0.0.1",
        "--port", str(port),
        "--revision", str(config.get("revision")
                         or manifest.ini.get("net:boot", "client_version")
                         or "239"),
        "--world-id", str(config.get("world_id") or "1"),
        "--environment", str(config.get("environment") or "0"),
        "--cachedir", cachedir,
    ]
    return Service(
        name="javconfig",
        argv=argv,
        port=str(port),
        ready=("http", "http://127.0.0.1:%s/jav_config.ws" % port),
        description="RuneLite reads codebase from here; file:// is refused",
    )


def _build_torirsmaped(config, context):
    manifest = context["manifest"]
    candidates = (
        "src/%s/torirsmaped" % SERVER_OBJDIR,
        "src/build/torirsmaped",
    )
    binary = _first_existing(context["repo_root"], candidates)
    port = config.get("port") or manifest.editor_port or DEFAULT_MAPED_PORT
    content = (config.get("content_dir") or manifest.editor_content_dir
               or "OSRS-Content/osrs239-content")
    repo_dir = config.get("repo_root") or manifest.editor_repo_root or "."
    argv = [
        "./" + binary, str(port),
        "--content-dir", content,
        "--repo-root", repo_dir,
    ]
    return Service(
        name="torirsmaped",
        argv=argv,
        build_target="torirsmaped",
        binary_candidates=candidates,
        port=str(port),
        ready=("tcp", str(port)),
        description="map-editor daemon owning the content tree",
    )


BUILDERS = {
    "torirsserver": _build_torirsserver,
    "io_server": _build_io_server,
    "js5_server": _build_js5_server,
    "javconfig": _build_javconfig,
    "torirsmaped": _build_torirsmaped,
}


# ------------------------------------------------------------ validation
def required_services(profile, manifest):
    """Services without which this frontend structurally cannot run.

    Deliberately a SHORT list. A declared service is the only thing that gets
    started; this exists so a true impossibility is reported as a sentence
    before anything starts, rather than as a browser tab that never loads.

    A game server is NOT on this list even when the manifest states
    transport=tcp, because dialling a server somebody else runs is an ordinary
    setup here — LostCity for rs254lc, an external checkout for osrs233xrsps. Requiring
    one would refuse to launch a configuration that works. That case is an
    advisory instead; see advisory_services().
    """
    client = profile.client
    required = {}
    if client in ("web", "web-idb"):
        required["io_server"] = (
            "client=%s has nothing else to serve the page from" % client)
    return required


def advisory_services(profile, manifest, declared):
    """Services this run probably wants but has not declared, as {name: why}.

    Reported as a note, and only when nothing is already listening on the port
    — so a server you started yourself, or one an external project runs, stays
    silent rather than being nagged about.
    """
    client = profile.client
    transport = (manifest.transport or "").strip()
    advisory = {}

    if "torirsserver" not in declared:
        if client in ("web", "web-idb"):
            advisory["torirsserver"] = (
                "a browser cannot host the in-process server, so one has to "
                "run beside the page")
        elif transport in ("tcp", "ws"):
            advisory["torirsserver"] = (
                "the manifest dials %s:%s over %s"
                % (manifest.net_host, manifest.net_port or "?", transport))
    if client == "web-idb" and "js5_server" not in declared:
        advisory["js5_server"] = (
            "the IndexedDB lane fills the browser's cache over JS5")
    if (manifest.is_editor and manifest.editor_server == "tcp"
            and "torirsmaped" not in declared):
        advisory["torirsmaped"] = (
            "[editor:boot] server=tcp attaches to a daemon")
    return advisory


def build_services(profile, manifest, manifest_path, repo_root):
    """The declared services, in the order the profile lists them."""
    from .profiles import LaunchError

    context = {
        "manifest": manifest,
        "manifest_path": manifest_path,
        "repo_root": repo_root,
        "profile": profile,
    }
    built = []
    for name in profile.services:
        if name not in BUILDERS:
            raise LaunchError(
                "profile '%s': unknown service '%s' (known: %s)"
                % (profile.name, name, ", ".join(SERVICE_NAMES)))
        built.append(BUILDERS[name](profile.service_config(name), context))
    return built
