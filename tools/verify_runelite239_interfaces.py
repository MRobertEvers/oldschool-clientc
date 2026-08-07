#!/usr/bin/env python3
"""Real RuneLite/deob interface contract test, driven through JCTL EVENTS.

Start this before a fresh ``run-osrs239.sh`` client launch.  It waits for JCTL,
subscribes to EVENTS before autologin, and then asserts the authoritative Java
client's state, menu/input path, mount table, packet traffic, RuneLite log, and
complete-frame PNG artifacts.
"""

from __future__ import annotations

import hashlib
import os
import pathlib
import queue
import re
import socket
import struct
import threading
import time


HOST = "127.0.0.1"
ROOT = pathlib.Path(__file__).resolve().parents[1]
PORT = int(os.environ.get("JCTL_PORT", "43601"))
RUN_DIR = pathlib.Path(os.environ.get(
    "RUNELITE239_RUN_DIR", str(ROOT / "build" / "run239"),
)).expanduser().resolve()
PROOF = RUN_DIR / "proof"
REPORT = PROOF / "interface_verify.log"
CLIENT_LOG = RUN_DIR / "client.log"
SERVER_LOG = RUN_DIR / "server.log"
CONNECT_TIMEOUT = 75.0
EXPECTED_INJECTED = os.environ.get(
    "EXPECTED_INJECTED_CLIENT",
    "/Users/matthewevers/Documents/git_repos/Deobfuscator/instr/build/out/"
    "injected-client-1.12.33-instr.jar",
)
EXPECTED_LOGIN = os.environ.get("EXPECTED_RUNELITE_LOGIN", "asdf")

# These are scale-512 pixels produced by the golden rev-239 item-model path after
# clearing its sprite cache. A scale-0 image contains only quantity text and was
# the exact kind of false-positive this gate is meant to prevent. These hashes
# catch the class144 regression which dropped one face per depth bucket.
ITEM_SPRITES = {
    (562, 2, "chaos"): "43870ec38dfc7af0c429c84bf0b6a25cb874e77563167422b7c253e9b2ddb155",
    (555, 94, "water"): "1ee70022d641599b8b67491705f18f1d1b9c476bae5e417308b956039429647a",
    (565, 98, "blood"): "49272087dbafa1128fd33a48aa7ed5987a437e078fedb3909d0271e9c42cecaa",
    (560, 96, "death"): "2893c77cdf408b9fb4b980de503a5b8fc4def2411736735ce67587eb61c0842e",
    (314, 5, "feather"): "645b93c76cd1d0c87ad21dc5829f04e3875c3261d1acb428528e5eb29b11613b",
}


# RuneLite writes a compiler-control NPE before the client is initialized on
# this local launcher.  It is startup noise, not a game integration failure.
# The health gate therefore begins at the *last* current-run initialization
# marker instead of rejecting generic words such as "Exception" or
# "NullPointerException" anywhere in the file.  These are the precise runtime
# failure messages emitted by EventBus, the overlay renderer and the deob's
# otherwise-silent packet decoder.
CLIENT_RUNTIME_FAILURES = (
    ("EventBus subscriber exception", re.compile(
        r"Uncaught exception in event subscriber", re.IGNORECASE)),
    ("overlay rendering exception", re.compile(
        r"Error during overlay rendering", re.IGNORECASE)),
    ("scheduled runnable exception", re.compile(
        r"Uncaught exception in runnable", re.IGNORECASE)),
    ("scheduled callable exception", re.compile(
        r"Uncaught exception in callable", re.IGNORECASE)),
    ("uncaught client exception", re.compile(
        r"\bUncaught exception:\s*", re.IGNORECASE)),
    ("packet decode error", re.compile(
        r"^pktlog:\s*!!\s*decode\b", re.IGNORECASE | re.MULTILINE)),
    ("missing local player", re.compile(
        r"(?:getLocalPlayer\(\).{0,160}\bis null\b|"
        r"\blocal player\b.{0,80}\b(?:null|missing|not (?:available|materialized))\b)",
        re.IGNORECASE)),
)

# A named interface packet must never fall back to another revision's payload
# or disappear because revision 239 has no mapping.  Other known protocol gaps
# (for example UPDATE_PID while that packet is under investigation) are not an
# interface-contract failure and are intentionally outside this expression.
INTERFACE_WIRE_GAP = re.compile(
    r"^mock230:\s+osrs239\s+(?:payload for (IF_[A-Z0-9_]+) is not transcribed|"
    r"has no (IF_[A-Z0-9_]+) -- dropping it)",
    re.MULTILINE,
)

INTERFACE_HOST_GAP = re.compile(
    r"^\s+(IF_[A-Z0-9_]+)\s+first wanted by ",
    re.MULTILINE,
)


class ContractError(RuntimeError):
    pass


def line_reader(sock: socket.socket, out: queue.Queue[str]) -> None:
    try:
        with sock, sock.makefile("r", encoding="utf-8", newline="\n") as stream:
            for line in stream:
                out.put(line.rstrip("\r\n"))
    finally:
        out.put("__EVENTS_EOF__")


def connect_when_ready() -> socket.socket:
    """Wait for the newly launched client's loopback control socket."""
    deadline = time.monotonic() + CONNECT_TIMEOUT
    last_error: OSError | None = None
    while time.monotonic() < deadline:
        try:
            sock = socket.create_connection((HOST, PORT), timeout=1)
            # The timeout is only for each connect attempt. EVENTS is a
            # deliberately blocking stream and can be quiet during startup.
            sock.settimeout(None)
            return sock
        except OSError as exc:
            last_error = exc
            time.sleep(0.05)
    raise ContractError(f"JCTL did not listen on {HOST}:{PORT}: {last_error}")


def command(stream, text: str) -> str:
    stream.write((text + "\n").encode())
    line = stream.readline().decode().rstrip("\r\n")
    if not line.startswith("ok "):
        raise ContractError(f"{text!r}: {line or 'connection closed'}")
    return line


def require(condition: bool, detail: str) -> None:
    if not condition:
        raise ContractError(detail)


def png_evidence(path: pathlib.Path) -> str:
    data = path.read_bytes()
    require(data.startswith(b"\x89PNG\r\n\x1a\n"), f"not a PNG: {path}")
    width, height = struct.unpack(">II", data[16:24])
    require((width, height) == (765, 503), f"wrong PNG dimensions: {width}x{height}")
    require(len(data) > 20_000, f"suspiciously small screenshot: {len(data)} bytes")
    return f"{path.name} sha256={hashlib.sha256(data).hexdigest()} bytes={len(data)}"


def sprite_evidence(path: pathlib.Path, expected_sha256: str) -> str:
    data = path.read_bytes()
    require(data.startswith(b"\x89PNG\r\n\x1a\n"), f"not a PNG: {path}")
    width, height = struct.unpack(">II", data[16:24])
    require((width, height) == (36, 32),
            f"wrong item-sprite dimensions: {path.name} {width}x{height}")
    digest = hashlib.sha256(data).hexdigest()
    require(digest == expected_sha256,
            f"item-sprite pixel contract changed: {path.name} {digest} != {expected_sha256}")
    return f"{path.name} sha256={digest} bytes={len(data)}"


def audit_evidence(path: pathlib.Path, minimum_groups: int) -> str:
    text = path.read_text(encoding="utf-8", errors="replace")
    match = re.search(
        r"^SUMMARY groups=(\d+) widgets=(\d+) actions=(\d+) dead=(\d+)$",
        text,
        re.MULTILINE,
    )
    require(match is not None, f"interface audit summary missing: {path}")
    groups, widgets, actions, dead = map(int, match.groups())
    require(groups >= minimum_groups,
            f"interface audit loaded only {groups} groups, expected >= {minimum_groups}")
    require(widgets >= 6000, f"interface audit saw only {widgets} widgets")
    require(actions >= 4700, f"interface audit saw only {actions} actions")
    require(dead == 0 and not re.search(r"(?m)^DEAD ", text),
            f"interface audit found {dead} dead actions: {path}")
    return (f"{path.name} groups={groups} widgets={widgets} "
            f"actions={actions} dead={dead}")


def current_client_runtime(client_log: str) -> str:
    """Return this launch after RuneLite initialization, excluding startup noise."""
    starts = list(re.finditer(
        r"(?m)^.*RuneLite\s+1\.12\.33 .* starting up, args:.*$", client_log))
    require(starts, "RuneLite 1.12.33 startup marker is missing from client.log")
    current_run = client_log[starts[-1].start():]
    initialized = re.search(
        r"(?m)^.*Client initialization took \d+ms\..*$", current_run)
    require(initialized is not None,
            "RuneLite current run never reached the client initialization marker")
    return current_run[initialized.start():]


def one_line_context(text: str, match: re.Match[str]) -> str:
    """A stable, bounded diagnostic suitable for the one-line verifier failure."""
    start = text.rfind("\n", 0, match.start()) + 1
    end = text.find("\n", match.end())
    if end < 0:
        end = len(text)
    return text[start:end].strip()[:240]


def log_health_evidence(client_log: str, server_log: str) -> list[str]:
    """Fail on current-run client faults and revision-239 interface wire gaps."""
    runtime = current_client_runtime(client_log)
    failures: list[str] = []

    # Several local renderer experiments intentionally produce drop-in hybrid
    # jars. They expose JCTL too, so a visual test can pass against the wrong
    # client unless the launcher records and the verifier pins the classpath.
    require(f"run_java_client: injected={EXPECTED_INJECTED}" in client_log,
            "authoritative instrumented jar was not the launched client: "
            f"expected {EXPECTED_INJECTED}")
    require(f"CS2AutoLogin: logging in as {EXPECTED_LOGIN}" in runtime,
            f"current RuneLite run did not log in as {EXPECTED_LOGIN}")
    require(f"mock230: login user='{EXPECTED_LOGIN}' session=ok" in server_log,
            f"mock230 did not accept the controlled account {EXPECTED_LOGIN}")
    for label, pattern in CLIENT_RUNTIME_FAILURES:
        match = pattern.search(runtime)
        if match is not None:
            failures.append(f"{label}: {one_line_context(runtime, match)}")

    gaps: list[str] = []
    for match in INTERFACE_WIRE_GAP.finditer(server_log):
        name = match.group(1) or match.group(2) or "IF_?"
        if name not in gaps:
            gaps.append(name)
    if gaps:
        failures.append("untranscribed/missing osrs239 interface packet(s): "
                        + ", ".join(gaps))

    host_gaps = sorted(set(INTERFACE_HOST_GAP.findall(server_log)))
    if host_gaps:
        failures.append("content-reachable interface host opcode(s) missing: "
                        + ", ".join(host_gaps))

    require(not failures, "log health gate failed: " + "; ".join(failures))
    return [
        "client.log current-run runtime failures=0 "
        "(compiler-control startup NPE excluded by initialization boundary)",
        f"client.log authoritative injected jar={EXPECTED_INJECTED}",
        f"client.log controlled account={EXPECTED_LOGIN}",
        "server.log named osrs239 interface wire gaps=0",
        "server.log content-reachable interface host gaps=0",
    ]


def find_event(lines: list[str], marker: str, after: int = -1,
               extra: str | None = None) -> int:
    for index in range(after + 1, len(lines)):
        line = lines[index]
        if marker in line and (extra is None or extra in line):
            return index
    suffix = f" containing {extra!r}" if extra is not None else ""
    raise ContractError(f"EVENTS missing {marker!r}{suffix} after event index {after}")


def assert_login_event_order(lines: list[str]) -> str:
    """Prove EVENTS attached pre-login and GPI materialized before LOGGED_IN."""
    starting = find_event(lines, "gamestate STARTING")
    # Autologin can submit credentials directly from STARTING; in that path the
    # golden client never publishes an intermediate LOGIN_SCREEN transition.
    # Requiring one would reject the exact controllable launch used by this
    # contract, while STARTING still proves EVENTS won the connection race.
    logging_in = find_event(lines, "gamestate LOGGING_IN", starting)
    gpi_empty = find_event(lines, "gpi init ", logging_in, "entity=false")
    loading = find_event(lines, "gamestate LOADING", gpi_empty)
    map_complete = find_event(lines, "packet_out op=24 len=0", loading)
    # The golden client publishes its LOGGED_IN game state after accepting the
    # login response and before consuming the first queued PLAYER_INFO. What
    # protects RuneLite subscribers is the server ordering within that queue:
    # PLAYER_INFO materializes the actor before IF_OPENTOP and UPDATE_STAT.
    logged_in = find_event(lines, "gamestate LOGGED_IN", map_complete)
    gpi_ready = find_event(lines, "gpi update ", logged_in, "entity=true")
    opentop = find_event(lines, "packet_in op=96 len=2", gpi_ready)
    stat = find_event(lines, "packet_in op=46 len=7", opentop)
    find_event(lines, "actor world=", gpi_ready)
    return ("EVENTS order STARTING<LOGGING_IN<GPI(entity=false)<"
            "LOADING<MAP_BUILD_COMPLETE<LOGGED_IN<GPI(entity=true)<"
            "IF_OPENTOP<UPDATE_STAT; actor materialized before UI/stat queue")


def assert_event_sequence(lines: list[str]) -> str:
    """Reject a subscriber queue/dropout that an eventual-state probe would hide."""
    sequence = [int(match.group(1)) for line in lines
                if (match := re.match(r"^event (\d+) ", line)) is not None]
    require(sequence, "EVENTS transcript contains no numbered events")
    for previous, current in zip(sequence, sequence[1:]):
        require(current == previous + 1,
                f"EVENTS sequence gap/replay disorder: {previous} -> {current}")
    return f"EVENTS sequence contiguous {sequence[0]}..{sequence[-1]} ({len(sequence)} events)"


def main() -> int:
    PROOF.mkdir(parents=True, exist_ok=True)
    events: queue.Queue[str] = queue.Queue()
    startup_events: list[str] = []
    event_sock = connect_when_ready()
    event_sock.sendall(b"EVENTS\n")
    threading.Thread(target=line_reader, args=(event_sock, events), daemon=True).start()

    control = connect_when_ready()
    control.settimeout(8)
    stream = control.makefile("rwb", buffering=0)
    transcript: list[str] = []

    def run(text: str) -> str:
        answer = command(stream, text)
        transcript.append(f"> {text}\n< {answer}")
        return answer

    try:
        # When this verifier is started before RuneLite, EVENTS wins the race
        # with autologin and observes the complete login instead of sampling a
        # client which merely happens to look healthy after the fact.
        first = events.get(timeout=5)
        require(first == "ok EVENTS", f"EVENTS handshake: {first}")
        login_deadline = time.monotonic() + CONNECT_TIMEOUT
        while time.monotonic() < login_deadline:
            try:
                state = events.get(timeout=0.5)
            except queue.Empty:
                continue
            require(state != "__EVENTS_EOF__",
                    "EVENTS connection closed during login")
            startup_events.append(state)
            if "gamestate LOGGED_IN" in state:
                break
        else:
            raise ContractError("EVENTS never reported gamestate LOGGED_IN")

        # Allow the login script's bounded relocation/rebuild to finish before
        # driving widgets. Completion is observed through EVENTS and direct
        # client state, never inferred from an idle timeout or a server write.
        run("wait 1500")
        require("entity=true" in run("player"), "local player actor is not materialized")
        require("state=2" in run("social"), "friend store did not finish loading")

        # Empty Friends -> empty Ignore: packet, replacement mount, and visuals.
        run("clickwidget 164 40")
        run("wait 500")
        require("164:82->429(type=1)" in run("mounts"), "Friends is not mounted")
        run(f"shot {PROOF / 'final_friends.png'}")
        run("clickwidget 429 1")
        run("wait 500")
        require("164:82->432(type=1)" in run("mounts"), "Ignore did not replace Friends")
        run(f"shot {PROOF / 'final_ignore.png'}")

        # A stat op opens a modal, runs CS2 layout, and the mount-clipped X closes it.
        run("clickwidget 164 53")
        run("wait 300")
        run("clickwidget 320 5")
        run("wait 700")
        require("164:16->860(type=0)" in run("mounts"), "skill guide modal not mounted")
        run(f"shot {PROOF / 'final_skill_open.png'}")
        close = run("clickwidget 860 4")
        require("visible=518,10,13x23" in close, "mount clipping was not applied to close X")
        run("wait 400")
        require("164:16->860" not in run("mounts"), "skill guide modal did not unmount")
        run(f"shot {PROOF / 'final_skill_closed.png'}")

        # Real terrain input must produce the actor's run pose, not just a
        # two-tile server delta. Polling the actor is bounded state inspection.
        run("click 300 300 1")
        running = ""
        for _ in range(35):
            run("wait 80")
            running = run("actor")
            if "pose=824" in running:
                break
        require("pose=824" in running,
                f"server RUN never animated as client run pose 824: {running}")
        run(f"shot {PROOF / 'final_run_pose824.png'}")

        # Inventory identity, source-target mask/menu semantics, and pixels are
        # independent gates: correct ids cannot excuse corrupt raster output.
        run("clickwidget 164 55")
        run("wait 250")
        inventory = run("itemcontainer 93")
        for expected in (
            "slot=0 id=562 qty=2 name=Chaos rune",
            "slot=2 id=555 qty=94 name=Water rune",
            "slot=3 id=565 qty=98 name=Blood rune",
            "slot=4 id=560 qty=96 name=Death rune",
            "slot=5 id=314 qty=5 name=Feather",
        ):
            require(expected in inventory, f"inventory mismatch: missing {expected}")
        run("clickwidgetsub 149 0 0 3")
        inv_menu = run("menu")
        require("[Use|<col=ff9040>Chaos rune</col> type=WIDGET_TARGET" in inv_menu,
                f"inventory source target is missing: {inv_menu}")
        run(f"shot {PROOF / 'final_inventory_menu_icons.png'}")
        sprite_paths: list[tuple[pathlib.Path, str]] = []
        for (item_id, quantity, label), expected_hash in ITEM_SPRITES.items():
            path = PROOF / f"final_item_{item_id}_{label}.png"
            result = run(f"itemsprite {item_id} {quantity} {path}")
            require(" 36x32" in result, f"item sprite generation failed: {result}")
            sprite_paths.append((path, expected_hash))

        # World map: zoom is local onOp CS2 (no server packet); close is a
        # RESUME_PAUSEBUTTON round trip that must remove the overlay mount.
        run("clickwidget 160 55")
        run("wait 600")
        require("164:18->595(type=1)" in run("mounts"), "world-map overlay not mounted")
        initial_map = run("worldmap")
        zoom_match = re.search(r"zoom=([0-9.]+)", initial_map)
        require(zoom_match is not None, f"world-map zoom telemetry missing: {initial_map}")
        zoom = float(zoom_match.group(1))
        for _ in range(8):
            if zoom == 4.0:
                break
            run("clickwidget 595 28" if zoom < 4.0 else "clickwidget 595 27")
            run("wait 250")
            normalized_map = run("worldmap")
            normalized_match = re.search(r"zoom=([0-9.]+)", normalized_map)
            require(normalized_match is not None,
                    f"world-map zoom telemetry missing: {normalized_map}")
            zoom = float(normalized_match.group(1))
        require(zoom == 4.0, f"world-map could not normalize to zoom 4: {initial_map}")
        run(f"shot {PROOF / 'final_worldmap_zoom4.png'}")
        run("clickwidget 595 27")
        run("wait 250")
        require("zoom=3" in run("worldmap"), "world-map zoom-out onOp did not reach zoom 3")
        run(f"shot {PROOF / 'final_worldmap_zoom3.png'}")
        run("clickwidget 595 28")
        run("wait 250")
        require("zoom=4" in run("worldmap"), "world-map zoom-in onOp did not return to zoom 4")
        run("clickwidget 595 38")
        run("wait 400")
        require("164:18->595" not in run("mounts"), "world-map close did not unmount overlay")
        run(f"shot {PROOF / 'final_worldmap_closed.png'}")

        # Dynamic music rows are DB-backed CS2 children. Normalize the scroll
        # to top, then move its thumb to the known unlocked Crest row.
        run("clickwidget 164 43")
        run("wait 400")
        run("drag 741 276 741 252")
        run("wait 500")
        top_rows = run("visiblesubs 239 11")
        require("sub=3" in top_rows and "text=Adventure" in top_rows,
                f"music top rows were not constructed: {top_rows}")
        run("drag 741 276 741 288")
        run("wait 500")
        crest_rows = run("visiblesubs 239 11")
        require("sub=115" in crest_rows and "text=Crest of a Wave" in crest_rows,
                f"music Crest row was not constructed: {crest_rows}")
        run("clickwidgetsub 239 11 115 3")
        crest_menu = run("menu")
        for option in ("Play", "Unlock hint", "playlist 1", "playlist 2", "playlist 3"):
            require(option in crest_menu, f"music row missing {option}: {crest_menu}")
        run("clickmenuop Play")
        run("wait 300")
        require("text=Crest of a Wave" in run("widget 239 4"),
                "music Play did not update the now-playing widget")
        require("Now playing: Crest of a Wave" in run("chat 30"),
                "music Play result did not reach client chat")
        run(f"shot {PROOF / 'final_music_play.png'}")

        # Prove both playlist transitions independent of persisted initial state.
        run("clickwidgetsub 239 11 115 3")
        playlist_before = run("menu")
        before_remove = "Remove from playlist 1" in playlist_before
        first_option = "Remove from playlist 1" if before_remove else "Add to playlist 1"
        second_option = "Add to playlist 1" if before_remove else "Remove from playlist 1"
        require(first_option in playlist_before, f"playlist op missing: {playlist_before}")
        run(f"clickmenuop {first_option}")
        run("wait 300")
        run("clickwidgetsub 239 11 115 3")
        playlist_toggled = run("menu")
        require(second_option in playlist_toggled and first_option not in playlist_toggled,
                f"playlist carrier varp did not change menu state: {playlist_toggled}")
        run(f"shot {PROOF / 'final_music_playlist_toggled.png'}")
        run(f"clickmenuop {second_option}")
        run("wait 300")
        run("clickwidgetsub 239 11 115 3")
        playlist_restored = run("menu")
        require(first_option in playlist_restored and second_option not in playlist_restored,
                f"playlist carrier varp did not restore menu state: {playlist_restored}")
        run(f"shot {PROOF / 'final_music_playlist_restored.png'}")
        run("click 540 300 1")  # close menu on the scene edge

        # Locked row unlock-hint must be visible in the decoded chat store, not
        # merely acknowledged by an outbound IF_BUTTON packet.
        run("drag 741 288 741 252")
        run("drag 741 276 741 252")
        run("wait 500")
        locked_rows = run("visiblesubs 239 11")
        require("sub=3" in locked_rows and "text=Adventure" in locked_rows,
                f"could not return music list to Adventure: {locked_rows}")
        run("clickwidgetsub 239 11 3 3")
        locked_menu = run("menu")
        require("Unlock hint" in locked_menu, f"locked row has no Unlock hint: {locked_menu}")
        run("clickmenuop Unlock hint")
        run("wait 250")
        require("This track unlocks in Varrock." in run("chat 30"),
                "locked music hint did not reach client chat")
        run("click 92 485 1")
        run("wait 150")
        run(f"shot {PROOF / 'final_music_unlock_hint_chat.png'}")

        # Emote dynamic sub-ids expose two independently server-armed ops.
        run("clickwidget 164 42")
        run("wait 300")
        require("sub=0" in run("visiblesubs 216 2"), "emote dynamic grid is missing")
        run("clickwidgetsub 216 2 0 3")
        yes_menu = run("menu")
        require("Loop" in yes_menu and "Perform" in yes_menu, f"emote ops missing: {yes_menu}")
        run("clickmenuop Loop")
        loop_state = ""
        for _ in range(20):
            run("wait 60")
            loop_state = run("actor")
            if "anim=189" in loop_state:
                break
        require("anim=189" in loop_state, f"Loop Yes animation did not decode: {loop_state}")
        run(f"shot {PROOF / 'final_emote_loop_yes.png'}")
        run("clickwidgetsub 216 2 12 3")
        dance_menu = run("menu")
        require("Perform" in dance_menu and "Dance" in dance_menu,
                f"Dance Perform op missing: {dance_menu}")
        run("clickmenuop Perform")
        perform_state = ""
        for _ in range(20):
            run("wait 60")
            perform_state = run("actor")
            if "anim=866" in perform_state:
                break
        require("anim=866" in perform_state,
                f"Perform Dance animation did not decode: {perform_state}")
        run(f"shot {PROOF / 'final_emote_perform_dance.png'}")

        # Account Benefits is a server-opened modal whose close is local CS2.
        run("clickwidget 164 39")
        run("wait 300")
        require("actions=[View Benefits]" in run("widget 109 41"),
                "Account Benefits action is absent")
        run("clickwidget 109 41")
        run("wait 400")
        require("164:16->275(type=0)" in run("mounts"),
                "Membership Benefits modal did not mount")
        run(f"shot {PROOF / 'final_account_benefits_open.png'}")
        benefits_audit = PROOF / "final_ifaceaudit_benefits.txt"
        run(f"ifaceaudit {benefits_audit}")
        run("clickwidgetsub 275 2 13")
        run("wait 300")
        require("164:16->275" not in run("mounts"),
                "Membership Benefits close did not unmount modal")
        run(f"shot {PROOF / 'final_account_benefits_closed.png'}")
        main_audit = PROOF / "final_ifaceaudit_main.txt"
        run(f"ifaceaudit {main_audit}")

        # Drain the asynchronous proof without polling screenshots.
        deadline = time.monotonic() + 2
        seen: list[str] = []
        while time.monotonic() < deadline:
            try:
                seen.append(events.get(timeout=0.2))
            except queue.Empty:
                pass
        joined = "\n".join(startup_events + seen)
        require("__EVENTS_EOF__" not in startup_events + seen,
                "EVENTS connection closed before the contract completed")
        require("packet_out op=47 len=9" in joined, "no client interface packet in EVENTS")
        require("packet_in op=7 len=7" in joined, "no IF_OPENSUB in EVENTS")
        require("packet_in op=23 len=4" in joined, "no IF_CLOSESUB in EVENTS")
        require("packet_out op=115 len=6" in joined,
                "world-map RESUME_PAUSEBUTTON is absent from EVENTS")
        require("packet_out op=114" in joined, "terrain movement packet is absent from EVENTS")
        require("packet_in op=35 len=10" in joined, "MIDI_SONG V2 packet is absent from EVENTS")
        require("pose=824" in joined, "run pose 824 is absent from EVENTS")
        require("anim=189" in joined and "anim=866" in joined,
                "emote animations are absent from EVENTS")
        require(joined.count("screenshot ") >= 17, "screenshot completion events missing")
        require("decode_error" not in joined, "packet decode_error reported through EVENTS")
        event_evidence = assert_login_event_order(startup_events + seen)
        sequence_evidence = assert_event_sequence(startup_events + seen)

        evidence = [png_evidence(PROOF / name) for name in (
            "final_friends.png", "final_ignore.png", "final_skill_open.png",
            "final_skill_closed.png", "final_run_pose824.png",
            "final_inventory_menu_icons.png", "final_worldmap_zoom4.png",
            "final_worldmap_zoom3.png", "final_worldmap_closed.png",
            "final_music_play.png", "final_music_playlist_toggled.png",
            "final_music_playlist_restored.png", "final_music_unlock_hint_chat.png",
            "final_emote_loop_yes.png", "final_emote_perform_dance.png",
            "final_account_benefits_open.png", "final_account_benefits_closed.png",
        )]
        evidence.extend(sprite_evidence(path, digest) for path, digest in sprite_paths)
        evidence.append(audit_evidence(benefits_audit, 26))
        evidence.append(audit_evidence(main_audit, 25))

        require(CLIENT_LOG.is_file(), f"RuneLite log is missing: {CLIENT_LOG}")
        require(SERVER_LOG.is_file(), f"mock230 log is missing: {SERVER_LOG}")
        client_log = CLIENT_LOG.read_text(encoding="utf-8", errors="replace")
        server_log = SERVER_LOG.read_text(encoding="utf-8", errors="replace")
        health_evidence = log_health_evidence(client_log, server_log)
        for expected in (
            "mock230: <- IF_BUTTON1 239:11 sub=115",
            "mock230: <- IF_BUTTON2 239:11 sub=3",
            "mock230: <- IF_BUTTON2 216:2 sub=0",
            "mock230: <- IF_BUTTON1 216:2 sub=12",
            "mock230: <- IF_BUTTON1 109:41",
        ):
            require(expected in server_log, f"server trigger evidence missing: {expected}")
        require(server_log.count("mock230: <- IF_BUTTON3 239:11 sub=115") >= 2,
                "music playlist add/remove did not reach IF_BUTTON3 twice")

        REPORT.write_text(
            "RuneLite 239 full interface parity contract: PASS\n\n"
            + "\n\n".join(transcript) + "\n\nEVENTS\n" + joined + "\n\n"
            + "\n".join([event_evidence, sequence_evidence, *health_evidence, *evidence]) + "\n",
            encoding="utf-8",
        )
        print(f"PASS: {REPORT}")
        for item in evidence:
            print(item)
        return 0
    except Exception as exc:
        queued: list[str] = []
        while True:
            try:
                queued.append(events.get_nowait())
            except queue.Empty:
                break
        REPORT.write_text(
            "RuneLite 239 full interface parity contract: FAIL\n"
            + str(exc) + "\n\n" + "\n\n".join(transcript)
            + ("\n\nEVENTS\n" + "\n".join(startup_events + queued)
               if startup_events or queued else "")
            + "\n",
            encoding="utf-8",
        )
        print(f"FAIL: {exc}\nreport: {REPORT}")
        return 1
    finally:
        stream.close()
        control.close()
        event_sock.close()


if __name__ == "__main__":
    raise SystemExit(main())
