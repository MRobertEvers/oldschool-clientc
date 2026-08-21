"""Profiles and the manifests they name.

A PROFILE is a named launch configuration: which world (manifest), which
frontend, which build flavor, plus the session choices that used to be either a
whole-file manifest copy or a line of shell history.

A MANIFEST stays what it always was — the world. The launcher reads a handful
of its fields to work out what a run needs; it never reinterprets them.
"""

import os

from .iniparse import Ini

# The six keys bootmanifest.c resolves through bm_join_path(), i.e. relative to
# the MANIFEST'S OWN directory rather than the process cwd. run-live.sh's
# manifest_path() and run-live.ps1 implement the same rule. Getting this list
# wrong is how a manifest move silently points a run at a cache that isn't
# there, so it is stated once, here.
MANIFEST_RELATIVE_KEYS = (
    "dir",
    "scripts",
    "content_dir",
    "repo_root",
    "revconfig_ui",
    "revconfig_cache",
)

CLIENT_KINDS = ("native", "web", "web-idb", "runelite", "headless")


class LaunchError(Exception):
    """A configuration or environment fault the user has to fix.

    Raised instead of exiting inline so the CLI owns how faults are printed.
    """


class Manifest:
    """A boot manifest, with the typed reads the launcher needs."""

    def __init__(self, path, ini):
        self.path = path
        self.ini = ini
        self.dir = os.path.dirname(os.path.abspath(path))

    @classmethod
    def load(cls, path):
        if not os.path.isfile(path):
            raise LaunchError("no manifest at '%s'" % path)
        return cls(path, Ini.load(path))

    def resolve_path(self, value):
        """Join a manifest-relative value onto the manifest's own directory."""
        if not value:
            return value
        if os.path.isabs(value):
            return value
        return os.path.normpath(os.path.join(self.dir, value))

    # -- world identity -------------------------------------------------
    @property
    def cache_dir(self):
        raw = self.ini.get("cache:boot", "dir")
        return self.resolve_path(raw) if raw else None

    @property
    def revision(self):
        return self.ini.get("cache:boot", "revision")

    # -- networking -----------------------------------------------------
    @property
    def transport(self):
        return self.ini.get("net:boot", "transport")

    @property
    def net_host(self):
        return self.ini.get("net:boot", "host") or "127.0.0.1"

    @property
    def net_port(self):
        return self.ini.get("net:boot", "port")

    @property
    def rev(self):
        return self.ini.get("net:boot", "rev")

    @property
    def ws_port(self):
        return self.ini.get("net:boot", "ws_port")

    @property
    def user(self):
        return self.ini.get("net:boot", "user")

    @property
    def password(self):
        return self.ini.get("net:boot", "pass")

    @property
    def server_scripts(self):
        raw = self.ini.get("net:boot", "scripts")
        return self.resolve_path(raw) if raw else None

    # -- content --------------------------------------------------------
    @property
    def lanes(self):
        return self.ini.get_all("content:lanes", "lane")

    # -- editor ---------------------------------------------------------
    @property
    def is_editor(self):
        return bool(self.ini.get("editor:boot", "content_dir"))

    @property
    def editor_server(self):
        return self.ini.get("editor:boot", "server") or "embed"

    @property
    def editor_port(self):
        return self.ini.get("editor:boot", "port") or "43610"

    @property
    def editor_content_dir(self):
        raw = self.ini.get("editor:boot", "content_dir")
        return self.resolve_path(raw) if raw else None

    @property
    def editor_repo_root(self):
        raw = self.ini.get("editor:boot", "repo_root")
        return self.resolve_path(raw) if raw else None

    @property
    def interface_id(self):
        return self.ini.get("ui:boot", "interface_id")

    # -- derived artifacts (staleness) ----------------------------------
    def derived(self):
        """The `[derived:*]` blocks, as (name, {key: value}) in file order.

        Each block declares one artifact this world is built from: where it
        lands, the make target that rebuilds it, and the checker that decides
        whether it is stale. The launcher stays generic over them — see
        staleness.py for why the policy lives in the checker and not here.
        """
        blocks = []
        for section in self.ini.sections_with_prefix("derived:"):
            fields = dict(self.ini.items(section))
            blocks.append((section.split(":", 1)[1], fields))
        return blocks


class Profile:
    """A named launch configuration."""

    def __init__(self, name, path, ini, repo_root):
        self.name = name
        self.path = path
        self.ini = ini
        self.repo_root = repo_root

    @property
    def description(self):
        return self.ini.get("profile", "description") or ""

    @property
    def world_path(self):
        world = self.ini.get("profile", "world")
        if not world:
            raise LaunchError(
                "profile '%s' names no world= manifest" % self.name)
        if os.path.isabs(world):
            return world
        return os.path.join(self.repo_root, world)

    @property
    def client(self):
        kind = self.ini.get("profile", "client") or "native"
        if kind not in CLIENT_KINDS:
            raise LaunchError(
                "profile '%s': unknown client '%s' (want one of %s)"
                % (self.name, kind, ", ".join(CLIENT_KINDS)))
        return kind

    @property
    def flavor(self):
        raw = self.ini.get("profile", "flavor") or ""
        return [part.strip() for part in raw.split(",") if part.strip()]

    @property
    def session(self):
        return dict(self.ini.items("session"))

    @property
    def env(self):
        return dict(self.ini.items("env"))

    @property
    def client_args(self):
        return self.ini.get_all("args", "arg")

    @property
    def services(self):
        """The processes this profile starts, in start order.

        Declared, not inferred: a dependency is listed before whatever needs
        it, and reading this line tells you the whole process set.
        """
        raw = self.ini.get("profile", "services") or ""
        return [part.strip() for part in raw.split(",") if part.strip()]

    def service_config(self, name):
        """The `[service:<name>]` block — ports and paths for one service."""
        return dict(self.ini.items("service:%s" % name))

    def overrides(self):
        """`[override:<section>]` blocks as (section, [(key, value)…])."""
        blocks = []
        for section in self.ini.sections_with_prefix("override:"):
            blocks.append((section.split(":", 1)[1], self.ini.items(section)))
        return blocks

    def manifest(self):
        return Manifest.load(self.world_path)


def profiles_dir(repo_root):
    return os.path.join(repo_root, "profiles")


def list_profiles(repo_root):
    """Every profile in the registry, name-sorted."""
    directory = profiles_dir(repo_root)
    if not os.path.isdir(directory):
        return []
    found = []
    for entry in sorted(os.listdir(directory)):
        if not entry.endswith(".ini"):
            continue
        name = entry[:-4]
        found.append(load_profile(repo_root, name))
    return found


def load_profile(repo_root, name):
    """Load one profile by name (with or without the .ini suffix)."""
    if name.endswith(".ini"):
        name = name[:-4]
    path = os.path.join(profiles_dir(repo_root), name + ".ini")
    if not os.path.isfile(path):
        available = ", ".join(profile.name for profile in list_profiles(repo_root))
        raise LaunchError(
            "no profile '%s' in %s\navailable: %s"
            % (name, profiles_dir(repo_root), available or "(none)"))
    return Profile(name, path, Ini.load(path), repo_root)


def generate_resolved_manifest(profile, out_dir):
    """Write the base manifest with `[override:*]` applied, return its path.

    Returns the BASE manifest path unchanged when a profile overrides nothing,
    so the common case keeps booting the file you can read in manifests/ and
    there is no generated copy to wonder about.

    The rewrite is line-level so every comment survives — these manifests carry
    more explanation than settings, and a merge that dropped it would make the
    generated file useless to read. Two things are done to the copy:

      * an overridden key is replaced in place, inside its own section;
      * the six manifest-relative keys are re-expressed relative to the copy,
        which lives at a different depth than the original — see reframe().
    """
    override_blocks = profile.overrides()
    base_path = profile.world_path
    if not override_blocks:
        return base_path

    manifest = Manifest.load(base_path)

    def reframe(value):
        """Rewrite a manifest-relative path for the copy's new location.

        The copy lives in build/manifests/, one level deeper than the original,
        so `../cache.osrs239` would resolve somewhere else entirely if carried
        over untouched. It is re-expressed RELATIVE TO THE COPY rather than
        made absolute, because an absolute path breaks the web lane: the page
        sends its cache directory to io_server, which refuses absolute paths
        outright as untrusted input. A relative one survives both — the native
        client resolves it against the copy's directory, and io_server
        normalises the `..` segments away.
        """
        if not value or os.path.isabs(value):
            return value
        return os.path.relpath(manifest.resolve_path(value), out_dir)

    pending = {}
    for section, items in override_blocks:
        for key, value in items:
            pending[(section, key)] = value

    with open(base_path, "r", encoding="utf-8", errors="replace") as handle:
        lines = handle.read().splitlines()

    out_lines = []
    section = ""
    applied = set()
    for raw in lines:
        stripped = raw.strip()
        if stripped.startswith("["):
            end = stripped.find("]")
            if end > 0:
                section = stripped[1:end].strip()
            out_lines.append(raw)
            continue
        if stripped and stripped[0] not in ";#" and "=" in stripped:
            key = stripped.split("=", 1)[0].strip()
            if (section, key) in pending:
                value = pending[(section, key)]
                # An OVERRIDDEN path key needs the same absolute rewrite as an
                # inherited one. A profile author writes `dir=../cache.x`
                # against the manifest they are overriding, not against
                # build/manifests/ where the copy lands — resolving it here is
                # what keeps those two frames from disagreeing.
                if key in MANIFEST_RELATIVE_KEYS:
                    value = reframe(value)
                out_lines.append("%s=%s" % (key, value))
                applied.add((section, key))
                continue
            if key in MANIFEST_RELATIVE_KEYS:
                value = stripped.split("=", 1)[1].strip()
                out_lines.append("%s=%s" % (key, reframe(value)))
                continue
        out_lines.append(raw)

    # An override naming a key the base never states is an ADDITION, not a
    # typo to swallow: append it under its section (creating the section when
    # the base has none) so `[override:js5:boot] enabled=1` can turn something
    # on that the base manifest simply does not mention.
    missing = [item for item in pending if item not in applied]
    if missing:
        by_section = {}
        for section_name, key in missing:
            by_section.setdefault(section_name, []).append(key)
        for section_name, keys in by_section.items():
            out_lines.append("")
            out_lines.append("[%s]" % section_name)
            for key in keys:
                value = pending[(section_name, key)]
                if key in MANIFEST_RELATIVE_KEYS:
                    value = reframe(value)
                out_lines.append("%s=%s" % (key, value))

    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "%s.ini" % profile.name)
    header = [
        "; GENERATED by ./launch from:",
        ";   profile  %s" % os.path.relpath(profile.path, profile.repo_root),
        ";   world    %s" % os.path.relpath(base_path, profile.repo_root),
        "; Edits here are overwritten on the next run — change the profile or",
        "; the manifest instead. Manifest-relative paths were made absolute",
        "; because this copy does not sit beside the original.",
        "",
    ]
    with open(out_path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(header + out_lines) + "\n")
    return out_path
