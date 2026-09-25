"""Tab completion for `./launch`.

The shell asks the launcher what may come next rather than carrying a second,
drifting copy of the command table: profile names come from the registry,
service names for `logs` come from the run state that actually exists on disk,
and a new subcommand or flag is completable the moment it is added below.

Candidates are printed one per line as `word` or `word<TAB>description`; the
shell glue in completions/ decides what to do with the description.
"""

import os

from .profiles import CLIENT_KINDS, profiles_dir
from .services import SERVICE_NAMES

# Every subcommand, with the flags it takes and the shape of its positionals.
#
# `positionals` names one slot per position; the last entry may be "*" to mean
# "and any number more of the same". `value_flags` are the flags that consume
# the word after them, so the completer offers that flag's values instead of a
# new positional.
COMMANDS = {
    "list": {
        "help": "every profile in the registry",
        "positionals": (),
        "flags": {},
    },
    "show": {
        "help": "the resolved plan, without running it",
        "positionals": ("profile", "clientarg*"),
        "flags": {
            "--client": "override the profile's frontend",
            "--flavor": "override the profile's build flavor",
        },
    },
    "run": {
        "help": "prepare, build, start services, launch",
        "positionals": ("profile", "clientarg*"),
        "flags": {
            "--client": "override the profile's frontend",
            "--flavor": "override the profile's build flavor",
            "--skip-checks": "run the cache and script pack exactly as they are",
            "--force-bake": "rebuild every [derived:*] artifact without asking",
            "--no-build": "skip the client build",
            "--no-client": "start the services and stop there",
            "--detach": "leave services running after this command exits",
            "--no-open": "print the URL instead of opening a browser",
        },
    },
    "bench": {
        "help": "time the world's [bench:*] scenes",
        "positionals": ("profile", "clientarg*"),
        "flags": {
            "--scene": "which scenes, comma-separated",
            "--renderer": "which renderers, comma-separated",
            "--repeat": "client processes per scene and renderer",
            "--baseline": "an earlier run to report deltas against",
            "--shots": "dump one BMP per run at the sample camera",
            "--list": "print the suite and stop",
            "--client": "override the profile's frontend",
            "--flavor": "override the profile's build flavor",
            "--no-build": "skip the client build",
            "--skip-checks": "run the cache exactly as it is",
            "--no-gate": "report a budget_ms breach without failing",
            "--timeout": "seconds before one run is killed",
        },
    },
    "status": {
        "help": "what is running",
        "positionals": ("run",),
        "flags": {},
    },
    "stop": {
        "help": "stop a run's services",
        "positionals": ("run",),
        "flags": {"--all": "stop every run on record"},
    },
    "logs": {
        "help": "a run's service logs",
        "positionals": ("run", "service"),
        "flags": {
            "-f": "follow the logs",
            "--follow": "follow the logs",
            "-n": "how many lines to show",
            "--lines": "how many lines to show",
        },
    },
    "doctor": {
        "help": "preflight this checkout",
        "positionals": ("profile",),
        "flags": {},
    },
    "completion": {
        "help": "print the shell tab-completion script",
        "positionals": ("shell",),
        "flags": {},
    },
}

# Flags whose value is the next word, and what that value may be. An empty
# string means the flag still eats the next word — so it is not mistaken for a
# positional — but there is nothing sensible to offer for it.
VALUE_FLAGS = {
    "--client": "client",
    "--flavor": "flavor",
    "--scene": "scene",
    "--renderer": "renderer",
    "--baseline": "",
    "--repeat": "",
    "--timeout": "",
    "-n": "",
    "--lines": "",
}

FLAVORS = {
    "opt": "the default optimized build",
    "debug": "OPT=0",
    "nosimd": "TORIDRAW_NO_SIMD=1",
    "tdo": "TORIDRAW_OPT=1",
    "memtrace": "MEMTRACE=1, links src/torirs_mt",
    "asan": "ASan+UBSan in their own objdir (macOS/Linux only)",
}

SHELLS = {"bash": "bash completion function", "zsh": "zsh compdef function"}


def _profile_names(repo_root):
    directory = profiles_dir(repo_root)
    if not os.path.isdir(directory):
        return []
    return sorted(entry[:-4] for entry in os.listdir(directory)
                  if entry.endswith(".ini"))


def _profiles(repo_root):
    """(name, description) for every profile, without failing on a bad one."""
    from .iniparse import Ini
    out = []
    for name in _profile_names(repo_root):
        description = ""
        try:
            ini = Ini.load(os.path.join(profiles_dir(repo_root), name + ".ini"))
            description = ini.get("profile", "description") or ""
        except Exception:
            pass
        out.append((name, description))
    return out


def _runs(repo_root):
    """Profiles with run state on disk — what `status`/`stop`/`logs` accept."""
    from . import supervisor
    known = supervisor.known_runs(repo_root)
    if not known:
        # No run has been started yet; the profile names are still the only
        # sensible thing to type, and stop/status accept them harmlessly.
        return _profiles(repo_root)
    live = set()
    for name in known:
        try:
            status = supervisor.run_status(repo_root, name)
        except Exception:
            continue
        if any(row["state"] == "running" for row in status["services"]):
            live.add(name)
    # Only the live ones are annotated: a description on every row makes the
    # running run harder to spot, not easier.
    return [(name, "running" if name in live else "") for name in known]


def _services(repo_root, run_name):
    """The log names this run actually has, falling back to the known set."""
    from . import supervisor
    directory = supervisor.run_dir(repo_root, run_name)
    if os.path.isdir(directory):
        found = sorted(entry[:-4] for entry in os.listdir(directory)
                       if entry.endswith(".log"))
        if found:
            return [(name, "") for name in found]
    return [(name, "") for name in SERVICE_NAMES]


def _flavor_candidates(current):
    """--flavor takes a comma list, so complete the segment after the comma."""
    head, _, _ = current.rpartition(",")
    prefix = head + "," if head else ""
    chosen = set(part for part in current.split(",") if part)
    return [(prefix + name, help_text)
            for name, help_text in FLAVORS.items()
            if name not in chosen or (prefix + name) == current]


def candidates(repo_root, words):
    """What may follow, given the words typed so far.

    `words` is everything after the program name up to and including the word
    being completed (which is "" when the cursor sits after a space).
    """
    current = words[-1] if words else ""
    prior = list(words[:-1])

    if not prior:
        return sorted((name, spec["help"]) for name, spec in COMMANDS.items())

    command = prior[0]
    spec = COMMANDS.get(command)
    if spec is None:
        return []

    # Mid-flag: the previous word wants a value, not a new word of our choosing.
    if prior[-1] in VALUE_FLAGS and prior[-1] in spec["flags"]:
        kind = VALUE_FLAGS[prior[-1]]
        if kind == "client":
            return [(name, "") for name in CLIENT_KINDS]
        if kind == "flavor":
            return _flavor_candidates(current)
        if kind == "scene":
            return _scenes(repo_root, _profile_word(spec, prior))
        if kind == "renderer":
            return _renderers()
        return []

    if current.startswith("-"):
        return sorted(spec["flags"].items())

    # Which positional slot is this? Skip the flags and the values they ate.
    filled = []
    skip_next = False
    for word in prior[1:]:
        if skip_next:
            skip_next = False
            continue
        if word.startswith("-"):
            skip_next = word in VALUE_FLAGS
            continue
        filled.append(word)

    slots = spec["positionals"]
    index = len(filled)
    if index >= len(slots):
        slot = slots[-1] if slots and slots[-1].endswith("*") else None
    else:
        slot = slots[index]
    if not slot:
        return []
    slot = slot.rstrip("*")

    if slot == "profile":
        return _profiles(repo_root)
    if slot == "run":
        return _runs(repo_root)
    if slot == "service":
        return _services(repo_root, filled[0]) if filled else []
    if slot == "shell":
        return sorted(SHELLS.items())
    # clientarg: free-form arguments the client parses, nothing to offer.
    return []



def _profile_word(spec, prior):
    """The profile name already typed on this line, if there is one.

    A value flag is completed before the positionals are counted, so the
    profile has to be recovered from the words themselves rather than from
    a slot index.
    """
    if not spec["positionals"] or spec["positionals"][0] != "profile":
        return None
    skip_next = False
    for word in prior[1:]:
        if skip_next:
            skip_next = False
            continue
        if word.startswith("-"):
            skip_next = word in VALUE_FLAGS
            continue
        return word
    return None


def _scenes(repo_root, profile_name):
    """The scene names the named profile's world declares.

    Offered only once a profile is on the line: scenes belong to a world,
    and guessing a registry-wide list would offer names that the profile
    being completed cannot run.
    """
    if not profile_name:
        return []
    try:
        from .bench import load_suite
        from .profiles import load_profile

        suite = load_suite(load_profile(repo_root, profile_name).manifest())
    except Exception:
        # A half-typed profile, a world with no bench block, a manifest
        # mid-edit: none of those are worth an error message inside a
        # completion.
        return []
    return [(scene.name, scene.description) for scene in suite.scenes]


def _renderers():
    from .bench import RENDERER_FLAGS

    return sorted((name, flag) for name, flag in RENDERER_FLAGS.items())


def _is_subsequence(typed, word):
    """Are `typed`'s characters all in `word`, in order? (`osrs-` -> osrs239-web)"""
    position = 0
    for character in typed:
        position = word.find(character, position) + 1
        if position == 0:
            return False
    return True


def match(current, items):
    """The candidates worth showing for a half-typed word, best tier only.

    The shells are told not to do their own filtering, so this is the single
    place that decides what `osrs-<TAB>` means. Tiers are tried in order and
    the first one that finds anything wins, so an exact prefix is never
    diluted by the looser matches that would also have qualified.
    """
    if not current:
        return list(items)
    lowered = current.lower()
    tiers = (
        lambda word: word.startswith(current),
        lambda word: word.lower().startswith(lowered),
        lambda word: lowered in word.lower(),
        lambda word: _is_subsequence(lowered, word.lower()),
    )
    for accepts in tiers:
        hits = [item for item in items if accepts(item[0])]
        if hits:
            return hits
    return []


def emit(repo_root, words, stream):
    current = words[-1] if words else ""
    for word, description in match(current, candidates(repo_root, words)):
        if description:
            stream.write("%s\t%s\n" % (word, description))
        else:
            stream.write("%s\n" % word)


def script(shell):
    """The shell glue, read from completions/ so there is one copy of it."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "completions", "launch.%s" % shell)
    if not os.path.isfile(path):
        return None
    with open(path) as handle:
        return handle.read()
