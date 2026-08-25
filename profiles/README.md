# Profiles

A **profile** is a named launch configuration: which world, which frontend,
which build flavor, which processes, and the session choices that used to live
in shell history or in a copied manifest.

```
./launch list                     every profile, with a one-line description
./launch show osrs239-web         the resolved plan — processes, ports, argv
./launch run  osrs239             prepare, build, start services, launch
./launch bench osrs239-bench      time the world's [bench:*] scenes
./launch status                   what is up right now, across all runs
./launch stop  osrs239-web        stop the services this run started
./launch logs  osrs239-web -f     follow its service logs
./launch doctor                   preflight this checkout
```

## Tab completion

```
eval "$(./launch completion zsh)"      # or bash — add it to your rc file
```

zsh users who prefer a real completion file:

```
./launch completion zsh > ~/.zfunc/_launch     # with ~/.zfunc in $fpath
```

Completing a profile name lists the registry with each description; completing
`stop`, `status`, or `logs` lists the runs that have state on disk and marks the
ones still running; `logs <run> <TAB>` lists the log files that run actually
has. The shell asks `launch complete` for the candidates, so a new subcommand,
flag, or profile is completable without touching the shell scripts — see
[tools/launcher/completion.py](../tools/launcher/completion.py).


## Benchmark profiles

`./launch bench <profile>` times the `[bench:<name>]` scenes its world
declares — one fixed camera over a fixed set of map squares, one offline
client process each, per-window perf samples. `osrs239-bench` is the worked
example, and `docs/PERF_HARNESS.md` covers the scene keys and how to read the
table.

Two things make a bench profile different from a run profile, and both are the
world's doing rather than the profile's:

  * **No server, structurally.** The world states no `transport=` and no
    `host=`, so the client never constructs a net subsystem. `--offline` in
    `[args]` is the belt to that braces.
  * **`flavor=opt`, always.** A benchmark against a `-O0` build measures the
    register allocator, and the ranking it produces between two kernels does
    not survive being compiled properly.

`./launch run osrs239-bench` still works and boots one client at the manifest
spawn — which is how you go and look at a scene by hand after a number moves.

## Naming

The same scheme the worlds and revconfigs use: `<epoch><revision>`, where the
epoch is `osrs` (OldSchool) or `rs` (everything before it), with the server
family glued on where a revision has more than one — `rs289lc` and `rs254lc` are
Lost City builds, `osrs233xrsps` is an xrsps one.

A `-suffix` after that is a *way of running* the same world, not a different
world: `osrs239-web`, `osrs239-asan`, `osrs239-net`. Content lanes read the same
way, because a lane is a world variant reached through one profile —
`osrs239-summoning`, `osrs239-curses`, `osrs239-rs2012`.

The profile's name is its filename, so this is the whole registry; nothing
inside a profile restates it.

## The split: worlds vs profiles

A **manifest** (`manifests/*.ini`) is a WORLD — a cache lineage, a revision,
content lanes, UI chrome, the era of client behaviour. A **profile** is a way
of running one.

The rule that keeps this from growing back into fourteen near-identical
manifests: **a new manifest is warranted only by a new world.** Everything else
— a different transport, a different port, a sanitizer build, a browser instead
of a window — is a profile. `osrs239-net.ini` is the worked example: it is the
`osrs239` world with three lines overridden, and it replaces what used to be a
whole-file copy of the manifest.

## Schema

```ini
[profile]
description=one line, shown by `./launch list`
world=manifests/manifest_osrs239.ini
client=native | web | web-idb | runelite | headless
flavor=opt                       ; comma list: opt,debug,asan,memtrace,nosimd,tdo
services=torirsserver, io_server ; explicit, in START order; empty = none

[service:torirsserver]           ; per-service config, one section each
port=43594

[service:io_server]
port=8088
root=build-web

[override:net:boot]              ; applied onto the world manifest
transport=tcp
port=43596

[session]
user=testc
pass=test

[env]                            ; passed to the client and its services
TORIRSSERVER_VERBOSE=1

[args]                           ; extra client arguments, one per line
arg=--soft3d
```

## Services are declared, not inferred

`services=` lists exactly what the launcher starts, in the order it starts
them. Nothing appears that you did not name — reading the profile tells you the
whole process set.

| service | what it is | default port |
|---|---|---|
| `torirsserver` | game world + JS5 on one socket (first byte picks) | 43594 |
| `io_server` | serves the page, `POST /io` cache reads, `GET /boot/` | 8088 |
| `js5_server` | cache over raw TCP or WebSocket (byte-sniffed) | 43595 |
| `javconfig` | RuneLite reads `codebase` from it; `file://` is refused | 8080 |
| `torirsmaped` | map-editor daemon owning the content tree | 43610 |

An empty `services=` is a real answer, and the common one: a manifest with
`transport=embed` links the server into the client, so there is no separate
process to start.

Two things stop a declaration from being a footgun:

* **A structural omission is refused.** `client=web` with no `io_server` cannot
  work — nothing else serves the page — so the launcher names the missing
  service and prints the line to add, before starting anything.
* **A likely omission is a note.** A manifest with `transport=tcp` and no
  `torirsserver` gets a one-line note, and only when nothing is already
  listening on the port. Dialling a server somebody else runs is an ordinary
  setup here (LostCity for `rs289lc` and `rs254lc`, an external checkout for
  `osrs233xrsps`), so this never blocks.

## Process ownership

A run's state is `build/run/<profile>/`: one pidfile and one log per service,
plus the resolved plan. That is what makes `status`, `stop` and `logs` work,
and it is why the launcher only ever signals pids it started — never a `pkill`
pattern, which in this tree would take sibling servers with it.

Readiness is per-service and is not always a port probe. `torirsserver` binds
early to fail fast on a port conflict, then spends ~10s loading scripts and
content, and may still refuse to run at the end of it (a stale script pack is
the usual reason). Connecting to that socket proves nothing, so the launcher
waits for the line the server prints when it is genuinely serving. A service
that dies during startup is reported with the tail of its own log, because
these servers explain themselves clearly and that explanation is the answer.

## Preparation

Before launching, the composed cache and the compiled script pack are checked
and rebuilt if stale. Two routes:

* **`[derived:*]` blocks in the manifest** (see
  `manifests/manifest_osrs239_summoning.ini`) — the world declares what it is
  built from, and the launcher stays generic over it.
* **otherwise `run-live.sh`**, through its existing `TORIRS_PREPARE_ONLY=1`
  seam — the same door `run-runelite.sh` already uses to borrow that bake
  policy rather than copy it.

`--skip-checks` runs the cache and script pack exactly as they stand. It is the
fast client-code iteration path; do not use it after changing content unless
running stale artifacts is the point.
