#!/usr/bin/env python3
"""Control one warm sailing client. See docs/sailing_harness.md for the protocol."""

from __future__ import annotations

import argparse
import contextlib
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SESSION = Path("/tmp") / f"3draster-sailing-{os.getuid()}"
POLL_SECONDS = 0.002


class HarnessError(RuntimeError):
    pass


def atomic_write(path: Path, value: str) -> None:
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(value, encoding="utf-8")
    temporary.replace(path)


class Session:
    def __init__(self, directory: Path, timeout: float = 30):
        self.directory = directory.expanduser().resolve()
        self.timeout = timeout
        self.directory.mkdir(parents=True, exist_ok=True)

    @contextlib.contextmanager
    def locked(self):
        with (self.directory / "controller.lock").open("a") as lock:
            deadline = time.monotonic() + self.timeout
            while True:
                try:
                    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    break
                except BlockingIOError:
                    if time.monotonic() >= deadline:
                        raise HarnessError("Another controller still owns this session")
                    time.sleep(POLL_SECONDS)
            try:
                yield
            finally:
                fcntl.flock(lock, fcntl.LOCK_UN)

    def metadata(self) -> dict:
        path = self.directory / "session.json"
        return json.loads(path.read_text()) if path.exists() else {}

    def alive(self) -> bool:
        metadata = self.metadata()
        pid = metadata.get("pid")
        if not pid:
            return False
        # A stale PID file must never authorize stopping an unrelated process.
        probe = subprocess.run(
            ["ps", "-p", str(pid), "-o", "lstart=", "-o", "command="],
            capture_output=True, text=True, check=False,
        )
        identity = probe.stdout.strip()
        return bool(identity and identity == metadata.get("process_identity"))

    def log_tail(self) -> str:
        path = self.directory / "client.log"
        if not path.exists():
            return ""
        with path.open("rb") as log:
            log.seek(0, os.SEEK_END)
            log.seek(max(0, log.tell() - 4000))
            return log.read().decode("utf-8", errors="replace")

    def _receive(self, timeout: float) -> dict:
        response = self.directory / "response"
        deadline = time.monotonic() + timeout
        next_process_check = time.monotonic() + 1.0
        while not response.exists():
            now = time.monotonic()
            if now >= deadline:
                raise HarnessError(
                    "Mailbox timed out; the outstanding command remains pending. "
                    "Use 'recover' to collect its late response before another command."
                )
            if now >= next_process_check:
                if not self.alive():
                    raise HarnessError("Client exited while handling request:\n" + self.log_tail())
                next_process_check = now + 1.0
            time.sleep(POLL_SECONDS)
        result = json.loads(response.read_text())
        pending_path = self.directory / "pending.json"
        pending = json.loads(pending_path.read_text()) if pending_path.exists() else {}
        if result.get("ok") and pending.get("reload_metadata") is not None:
            atomic_write(self.directory / "session.json", json.dumps(pending["reload_metadata"], indent=2))
        response.unlink()
        pending_path.unlink(missing_ok=True)
        return result

    def request(self, command: str, timeout: float | None = None) -> dict:
        if not command or "\n" in command or "\r" in command or len(command.encode()) >= 2048:
            raise HarnessError("Mailbox command must be one nonempty line shorter than 2048 bytes")
        if (self.directory / "pending.json").exists():
            raise HarnessError("A previous command is pending; use 'recover' first")
        if (self.directory / "request").exists() or (self.directory / "response").exists():
            raise HarnessError("Mailbox has uncollected data; use 'recover' first")
        reload_metadata = None
        if command == "reload" or command.startswith("reload "):
            reload_metadata = self.metadata()
            pack = (Path(command[7:]) / "script.dat" if command.startswith("reload ")
                    else Path(reload_metadata.get("launch_script_pack", reload_metadata["script_pack"])))
            reload_metadata["script_pack"] = str(pack.resolve())
            reload_metadata["script_sha256"] = hashlib.sha256(pack.read_bytes()).hexdigest()
            if not reload_metadata.get("allow_stale_scripts"):
                fresh = subprocess.run(
                    [sys.executable, str(ROOT / "tools/server_scripts_stale.py"), "--out", str(pack.parent)],
                    capture_output=True, text=True, check=False,
                )
                if fresh.returncode != 1:
                    raise HarnessError("Reload refused before changing the live session; rebuild the script pack. "
                                       + (fresh.stdout or fresh.stderr).strip())
        started = time.perf_counter()
        atomic_write(self.directory / "pending.json", json.dumps({"command": command, "reload_metadata": reload_metadata}))
        atomic_write(self.directory / "request", command + "\n")
        result = self._receive(self.timeout if timeout is None else timeout)
        result["elapsed_ms"] = round((time.perf_counter() - started) * 1000, 3)
        return result

    def checked(self, command: str, timeout: float | None = None) -> dict:
        result = self.request(command, timeout)
        if not result.get("ok"):
            raise HarnessError(f"{command}: {result.get('error', result)}")
        return result

    def capture(self, output: Path) -> dict:
        output = output.expanduser().resolve()
        if output.suffix.lower() != ".png":
            raise HarnessError("Capture output must use .png (the renderer readback format)")
        output.parent.mkdir(parents=True, exist_ok=True)
        previous_mtime = output.stat().st_mtime_ns if output.exists() else None
        result = self.checked("shot " + str(output))
        if not output.is_file() or output.stat().st_size == 0:
            raise HarnessError(f"Capture response had no image: {output}")
        if previous_mtime == output.stat().st_mtime_ns:
            raise HarnessError(f"Capture did not update the existing image: {output}")
        with output.open("rb") as image:
            if image.read(8) != b"\x89PNG\r\n\x1a\n":
                raise HarnessError(f"Capture is not a PNG from renderer readback: {output}")
        result["path"] = str(output)
        result["bytes"] = output.stat().st_size
        result["world_order_mode"] = result.get("renderer")
        result["renderer"] = self.metadata().get("renderer", "unknown")
        return result

    def start(self, args) -> dict:
        resume_save = getattr(args, "resume_save", False)
        scripts = getattr(args, "scripts", None)
        if self.alive():
            metadata = self.metadata()
            if metadata.get("renderer") != args.renderer:
                raise HarnessError("Stop the existing session before changing its renderer")
            if metadata.get("manifest") != str(args.manifest.expanduser().resolve()):
                raise HarnessError("Stop the existing session before changing its manifest")
            if metadata.get("boat", "skiff") != args.boat:
                raise HarnessError("Stop the existing session before changing its boat fixture")
            if metadata.get("fixture_tile", [3072, 3160]) != list(args.fixture_tile):
                raise HarnessError("Stop the existing session before changing its fixture tile")
            if metadata.get("headless") != args.headless:
                raise HarnessError("Stop the existing session before changing window visibility")
            if metadata.get("user") != args.user:
                raise HarnessError("Stop the existing session before changing its test account")
            if scripts and metadata.get("script_pack") != str(scripts.expanduser().resolve() / "script.dat"):
                raise HarnessError("Stop the existing session or use reload before changing its script pack")
            result = self.checked("state")
            return {**result, "reused": True, "session": str(self.directory)}
        binary = args.binary.expanduser().resolve()
        manifest = args.manifest.expanduser().resolve()
        if not binary.is_file() or not os.access(binary, os.X_OK):
            raise HarnessError(f"Build the embedded client first: {binary}")
        if not manifest.is_file():
            raise HarnessError(f"Missing manifest: {manifest}")
        for name in ("request", "response", "pending.json", "request.tmp", "response.tmp"):
            (self.directory / name).unlink(missing_ok=True)
        saves = self.directory / "saves"
        saves.mkdir(exist_ok=True)
        name = "".join(c.lower() if c.isascii() and c.isalnum() else "_" if c in "_- " else ""
                       for c in args.user)
        save_path = saves / f"{name}.ini"
        if resume_save:
            if not save_path.is_file():
                raise HarnessError("No saved character to resume; use logout before stopping the session")
        boats = {"raft": "1 3 1 3840 6456", "skiff": "2 5 2 3840 6448",
                 "sloop": "3 10 3 3840 6432"}
        # The fixture tile is where the hull is spawned and boarded. Its
        # default is the surveyed ocean; a dock berth is what the social
        # proofs need, because Board-friend only exists at a mooring.
        fixture_x, fixture_z = args.fixture_tile
        env = os.environ.copy()
        for name in ("TORIRS_MAX_FRAMES", "TORIRS_EXIT_BMP", "TORIRS_NET_CHEAT", "TORIRS_SCREENSHOT"):
            env.pop(name, None)
        env.update({
            "TORIRS_CONTENT_TEST": str(self.directory),
            "TORIRS_CONTENT_TEST_CHECKPOINTS": "1",
            "TORIRS_PREFS": str(self.directory / "client-prefs.ini"),
            "TORIRS_PLUGIN_PREFS": str(self.directory / "plugin-prefs.ini"),
            "TORIRS_TRANSPORT": "embed",
            "TORIRSSERVER_SAVES": str(saves),
            "TORIRSSERVER_REV": "osrs239",
            "TORIRSSERVER_CONTENT": str(ROOT / "OSRS-Content/osrs239-content"),
            "TORIRSSERVER_SCRIPTS": str(ROOT / "OSRS-Content/osrs239-content/server/scripts/build"),
            "TORIRS_STDERR_UNBUFFERED": "1",
            "TORIRS_NET_CHEAT": (
                "setting 18314 50;setlevel sailing 99;setlevel construction 99;"
                f"vesselgoto {fixture_x} {fixture_z} 0;"
                f"vesselspawnat {fixture_x} {fixture_z} {boats[args.boat]};vesselboard;helm"),
        })
        if args.headless:
            if args.renderer != "soft3d":
                raise HarnessError("--headless uses SDL's dummy driver and requires --renderer soft3d")
            env.update({"SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy"})
        # BootManifest resolves its cache against the manifest's directory;
        # the embedded server needs that identical path, without configparser's
        # duplicate-key restrictions on this project's repeated sections.
        section = ""
        for raw in manifest.read_text().splitlines():
            line = raw.strip()
            if line.startswith("["):
                section = line
            if section == "[cache:boot]" and line.startswith("dir="):
                env["TORIRSSERVER_CACHE"] = str((manifest.parent / line[4:].strip()).resolve())
            if section == "[net:boot]" and line.startswith("scripts="):
                env["TORIRSSERVER_SCRIPTS"] = str((manifest.parent / line[8:].strip()).resolve())
        if scripts:
            env["TORIRSSERVER_SCRIPTS"] = str(scripts.expanduser().resolve())
        if resume_save:
            # Empty env values fall back to the manifest's cheat=. A nonempty
            # list containing zero commands suppresses both bootstrap sources.
            env["TORIRS_NET_CHEAT"] = ";"
        renderer = {"soft3d": "--soft3d", "gl3": "--opengl3", "gl3-zbuffer": "--opengl3-zbuffer"}
        script_pack = Path(env["TORIRSSERVER_SCRIPTS"]) / "script.dat"
        if env.get("TORIRSSERVER_ALLOW_STALE_SCRIPTS") != "1":
            fresh = subprocess.run(
                [sys.executable, str(ROOT / "tools/server_scripts_stale.py"), "--out", str(script_pack.parent)],
                capture_output=True, text=True, check=False,
            )
            if fresh.returncode != 1:
                raise HarnessError("Start refused before launching the client; rebuild the script pack. "
                                   + (fresh.stdout or fresh.stderr).strip())
        provenance = {
            "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
            "script_pack": str(script_pack),
            "launch_script_pack": str(script_pack),
            "script_sha256": hashlib.sha256(script_pack.read_bytes()).hexdigest(),
            "allow_stale_scripts": env.get("TORIRSSERVER_ALLOW_STALE_SCRIPTS") == "1",
        }
        argv = [str(binary), "--manifest", str(manifest), "--user", args.user,
                "--pass", "test", renderer[args.renderer], *args.client_arg]
        archived_save = None
        if not resume_save and save_path.is_file():
            # Fixture mode must start with exactly the requested hull. Loading
            # an old aboard save first would leave its reconstructed vessel
            # alive beside the newly spawned fixture. Preserve the save for
            # review instead of silently overwriting it with test setup.
            history = saves / "history"
            history.mkdir(exist_ok=True)
            archived_save = history / f"{name}-{time.time_ns()}.ini"
            save_path.replace(archived_save)
        started = time.perf_counter()
        with (self.directory / "client.log").open("wb") as log:
            process = subprocess.Popen(argv, cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
                                       stdout=log, stderr=log, start_new_session=True)
        identity = subprocess.run(
            ["ps", "-p", str(process.pid), "-o", "lstart=", "-o", "command="],
            capture_output=True, text=True, check=False,
        ).stdout.strip()
        atomic_write(self.directory / "session.json", json.dumps({
            "pid": process.pid, "process_identity": identity, "binary": str(binary),
            "manifest": str(manifest), "renderer": args.renderer, "user": args.user,
            "headless": args.headless,
            "boat": args.boat,
            "fixture_tile": [fixture_x, fixture_z],
            "resume_save": resume_save,
            "archived_save": str(archived_save) if archived_save else None,
            **provenance,
        }, indent=2))
        deadline = time.monotonic() + args.startup_timeout
        while time.monotonic() < deadline:
            result = self.checked("state", max(1, deadline - time.monotonic()))
            sailing = result.get("sailing", {})
            vessel = sailing.get("vessel", {})
            # Login publishes a root world before its bootstrap cheats and the
            # first WORLDENTITY_INFO/PLAYER_INFO pair put the client aboard.
            # A loaded ocean alone is not a ready sailing fixture.
            aboard_ready = (result.get("aboard_view", 0) > 0 and sailing.get("aboard")
                            and result["aboard_view"] == vessel.get("view"))
            location_ready = (aboard_ready if sailing.get("aboard") else result.get("aboard_view", 0) == 0)
            fixture_ready = aboard_ready and sailing.get("player", {}).get("navigating") == vessel.get("id")
            if (result.get("online") and result.get("world_ready")
                    and (location_ready if resume_save else fixture_ready)):
                if not resume_save:
                    self.checked("cheat sailpanel")
                    self.checked("camera 256 256 1000")
                result = self.checked("state")
                return {**result, "reused": False, "pid": process.pid,
                        "session": str(self.directory),
                        "startup_ms": round((time.perf_counter() - started) * 1000, 3)}
            self.checked("step 30", max(1, deadline - time.monotonic()))
        raise HarnessError("Client did not become ready before startup deadline:\n" + self.log_tail())

    def stop(self) -> dict:
        metadata = self.metadata()
        if not self.alive():
            return {"ok": True, "stopped": False}
        pid = metadata["pid"]
        os.kill(pid, signal.SIGTERM)
        deadline = time.monotonic() + 5
        while self.alive() and time.monotonic() < deadline:
            time.sleep(0.01)
        if self.alive():
            raise HarnessError(f"Client {pid} did not stop after SIGTERM")
        return {"ok": True, "stopped": True, "pid": pid}


def benchmark(session: Session, iterations: int, include_step: bool) -> dict:
    def summary(samples):
        ordered = sorted(samples)
        return {"count": len(samples), "median_ms": round(statistics.median(samples), 3),
                "p95_ms": round(ordered[max(0, math.ceil(len(samples) * .95) - 1)], 3),
                "max_ms": round(max(samples), 3)}

    session.checked("pause")
    operations = {"state": lambda: session.checked("state"),
                  "capture": lambda: session.capture(session.directory / "captures/bench.png")}
    if include_step:
        session.checked("save harness_bench")
        operations["step_30"] = lambda: session.checked("step 30")
    results = {}
    try:
        for name, operation in operations.items():
            operation()  # Warm asset/capture allocations separately from samples.
            samples = [operation()["elapsed_ms"] for _ in range(iterations)]
            results[name] = summary(samples)
    finally:
        if include_step:
            session.checked("restore harness_bench")
    return {"ok": True, "target_ms": 100, "operations": results,
            "query_capture_target_met": all(results[n]["p95_ms"] < 100 for n in ("state", "capture"))}


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--session", type=Path, default=DEFAULT_SESSION)
    p.add_argument("--timeout", type=float, default=30, help="Warm command timeout in seconds")
    sub = p.add_subparsers(dest="action", required=True)
    start = sub.add_parser("start", help="Start or reuse a warm client; never builds")
    start.add_argument("--binary", type=Path, default=ROOT / "src/torirs")
    start.add_argument("--manifest", type=Path, default=ROOT / "manifests/manifest_osrs239_sailing.ini")
    start.add_argument("--scripts", type=Path, help="Use an isolated compiled script-pack directory")
    start.add_argument("--resume-save", action="store_true", help="Load the saved character without spawning or teleporting the fixture")
    start.add_argument("--user", default="sailingtest")
    start.add_argument("--boat", choices=("raft", "skiff", "sloop"), default="skiff")
    start.add_argument("--fixture-tile", type=int, nargs=2, default=(3072, 3160),
                       metavar=("X", "Z"),
                       help="Absolute tile the fixture hull spawns on; default is the surveyed ocean")
    start.add_argument("--renderer", choices=("soft3d", "gl3", "gl3-zbuffer"), default="soft3d")
    start.add_argument("--startup-timeout", type=float, default=120)
    start.add_argument("--headless", action="store_true", help="Render software frames without a visible window")
    start.add_argument("--client-arg", action="append", default=[], help="Additional flag; use =--flag")
    for name in ("status", "pause", "resume", "stop", "recover", "reload", "close"):
        sub.add_parser(name)
    step = sub.add_parser("step")
    step.add_argument("frames", type=int, help="Client cycles, 20 ms each; 30 = one game tick")
    capture = sub.add_parser("capture")
    capture.add_argument("output", nargs="?", type=Path)
    for name in ("save", "restore", "varbit"):
        sub.add_parser(name).add_argument("name")
    for name in ("cheat", "raw"):
        sub.add_parser(name).add_argument("text", nargs="+")
    proc = sub.add_parser("proc", help="Run a named content proc in the PRIMARY player's context")
    proc.add_argument("name")
    proc.add_argument("args", nargs="*", type=int, help="Up to four integer proc arguments")
    peer = sub.add_parser("peer", help="Second real server player; no words reports its state")
    peer.add_argument("words", nargs="*",
                      help="create|place|board|walk|varbit|private|cheat|button|proc|namedialog|oploc|remove")
    click = sub.add_parser("click")
    click.add_argument("x", type=int)
    click.add_argument("y", type=int)
    click.add_argument("button", nargs="?", default="left", choices=("left", "right", "middle"))
    hover = sub.add_parser("hover")
    hover.add_argument("x", type=int)
    hover.add_argument("y", type=int)
    camera = sub.add_parser("camera")
    camera.add_argument("yaw", type=int)
    camera.add_argument("pitch", type=int)
    camera.add_argument("zoom", type=int, nargs="?", default=100)
    button = sub.add_parser("button")
    button.add_argument("name")
    button.add_argument("sub", type=int, nargs="?", default=-1)
    button.add_argument("op", type=int, nargs="?", default=1)
    widget = sub.add_parser("widget")
    widget.add_argument("name")
    widget.add_argument("sub", type=int, nargs="?", default=-1)
    bench = sub.add_parser("bench")
    bench.add_argument("--iterations", type=int, default=20)
    bench.add_argument("--include-step", action="store_true")
    return p


def main() -> int:
    args = parser().parse_args()
    if args.timeout <= 0:
        raise HarnessError("--timeout must be positive")
    session = Session(args.session, args.timeout)
    with session.locked():
        action = args.action
        if action == "start":
            result = session.start(args)
        elif action == "stop":
            result = session.stop()
        elif action == "recover":
            result = session._receive(args.timeout)
        elif action == "status" and not session.alive():
            result = {"ok": False, "running": False, "session": str(session.directory)}
        elif not session.alive():
            raise HarnessError("No running session; use 'start' first")
        elif action == "capture":
            output = args.output or session.directory / "captures" / f"frame-{time.time_ns()}.png"
            result = session.capture(output)
        elif action == "bench":
            if not 1 <= args.iterations <= 10000:
                raise HarnessError("--iterations must be between 1 and 10000")
            result = benchmark(session, args.iterations, args.include_step)
        else:
            if action == "step":
                if not 0 <= args.frames <= 30000:
                    raise HarnessError("step must be between 0 and 30000 cycles")
                command = f"step {args.frames}"
            elif action in ("save", "restore", "varbit"):
                if not args.name or any(c.isspace() for c in args.name):
                    raise HarnessError("Names must be a single word")
                command = f"{action} {args.name}"
            elif action in ("cheat", "raw"):
                command = ("cheat " if action == "cheat" else "") + " ".join(args.text)
            elif action == "proc":
                if not args.name or any(c.isspace() for c in args.name):
                    raise HarnessError("A proc name must be a single word")
                if len(args.args) > 4:
                    raise HarnessError("A content proc takes at most four integer arguments")
                command = " ".join(["proc", args.name, *(str(v) for v in args.args)])
            elif action == "peer":
                # Words are rejoined with single spaces, so a quoted argument
                # ("peer cheat 'setlevel sailing 99'") reaches the mailbox as
                # the one line the peer command parser expects.
                command = " ".join(["peer", *args.words]).strip()
            elif action == "click":
                command = f"click {args.x} {args.y} {args.button}"
            elif action == "hover":
                command = f"hover {args.x} {args.y}"
            elif action == "camera":
                command = f"camera {args.yaw} {args.pitch} {args.zoom}"
            elif action in ("button", "widget"):
                command = f"{action} {args.name} {args.sub}"
                if action == "button":
                    command += f" {args.op}"
            else:
                command = "state" if action == "status" else action
            result = session.request(command)
        print(json.dumps(result, sort_keys=True))
        return 0 if result.get("ok") else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (HarnessError, OSError, ValueError) as exc:
        print(json.dumps({"ok": False, "error": str(exc)}))
        sys.exit(1)
