#!/usr/bin/env python3
"""Drive a torirs client or its map editor through the agent control port.

The client listens when it is started with `--agent-port <N>` (0 picks a free
port; `--agent-port-file <path>` writes the port there). This file is the
other end: a command line for a shell, and `class Client` for a script.
docs/AGENT_API.md has every verb, its arguments and its reply.

    python tools/agent/torirs_agent.py [--port N | --port-file F] <verb> [key=value ...]

prints the reply as JSON and exits 0, or 1 when the reply is ok:false (2 for
a usage or connection error). With neither --port nor --port-file the port is
read from TORIRS_AGENT_PORT, else from the file `start` writes
(TORIRS_AGENT_PORT_FILE, else torirs_agent.port in the temp directory).

A value is JSON when it parses as JSON (`x=120`, `visible_only=true`,
`from=[10,20]`), a list of whole numbers when it is `10,20`, and a string
otherwise (`label=Catalog`). `mods=shift,ctrl` is a list of names.

Conveniences:

    start <manifest> [--window WxH] [--port-file F] [--exe PATH] [--log FILE]
          [--wait SECONDS] [-- extra client args]
        Start the client (src/torirs_win64.exe on Windows, src/torirs
        elsewhere) from the repository root with `--agent-port 0
        --agent-port-file F`, wait until it listens and has booted, and print
        its hello. Its stdout and stderr go to the log file (F + ".log").
    click <label> [panel=...]      ui_click by caption
    set <label> <text> [panel=...] ui_set by caption
    shot <path> [rect=x,y,w,h]     the presented frame to a .png or .bmp
    key <chord>                    key chord=<chord>
    type <text>                    type text=<text>
    action <id>                    action action=<id>  (a hotkey action's id)
    select <dropdown> <option>     open a dropdown and pick an option by label

As a module:

    from torirs_agent import Client
    with Client(port_file="agent.port") as agent:
        agent.wait(until="ready")
        agent.ui_set(label="Find", panel="Catalog", text="crate")
        rows = agent.ui(panel="Catalog", text="crate")
        agent.action("file.run")
        agent.wait(until="idle", timeout_ms=120000)
        print(agent.log()["lines"])

Standard library only.
"""

import json
import os
import re
import socket
import subprocess
import sys
import tempfile
import time

DEFAULT_PORT_FILE = os.path.join(tempfile.gettempdir(), "torirs_agent.port")
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


class AgentError(Exception):
    """A reply with ok:false. `reply` is the whole reply; str() is its error."""

    def __init__(self, reply):
        Exception.__init__(self, reply.get("error", "the agent server refused the request"))
        self.reply = reply


def default_port_file():
    return os.environ.get("TORIRS_AGENT_PORT_FILE") or DEFAULT_PORT_FILE


def read_port_file(path):
    """The port a client wrote to `path`. Raises OSError / ValueError."""
    with open(path) as handle:
        return int(handle.read().strip())


class Client:
    """One connection to a client's agent control server.

    Every verb is a method that returns the reply as a dict and raises
    AgentError when the reply is ok:false. `call` is the same without the
    raise, for a caller that wants to read the refusal itself.
    """

    def __init__(self, port=None, port_file=None, host="127.0.0.1", timeout=60.0):
        if port is None:
            if port_file is None and os.environ.get("TORIRS_AGENT_PORT"):
                port = int(os.environ["TORIRS_AGENT_PORT"])
            else:
                port = read_port_file(port_file or default_port_file())
        self.port = int(port)
        self.timeout = timeout
        self._next_id = 0
        self._pending = b""
        self._socket = socket.create_connection((host, self.port), timeout=timeout)

    def close(self):
        if self._socket is not None:
            self._socket.close()
            self._socket = None

    def __enter__(self):
        return self

    def __exit__(self, kind, value, trace):
        self.close()
        return False

    # ---- the protocol: one JSON object per line each way -----------------

    def _read_line(self, timeout):
        self._socket.settimeout(timeout)
        while b"\n" not in self._pending:
            chunk = self._socket.recv(65536)
            if not chunk:
                raise ConnectionError("the client closed the agent connection")
            self._pending += chunk
        line, _, self._pending = self._pending.partition(b"\n")
        return line.decode("utf-8")

    def call(self, verb, timeout=None, **args):
        """Send one request and return its reply, ok or not."""
        self._next_id += 1
        request = {"id": self._next_id, "verb": verb}
        for key, value in args.items():
            if value is not None:
                request[key] = value
        if request["id"] != self._next_id or request["verb"] != verb:
            raise ValueError("\"id\" and \"verb\" are the protocol's own names, not arguments")
        self._socket.sendall(json.dumps(request).encode("utf-8") + b"\n")
        reply = json.loads(self._read_line(self.timeout if timeout is None else timeout))
        # Replies come one per request, in order. One for a line the server
        # could not read carries id null.
        if reply.get("id") not in (self._next_id, None):
            raise ConnectionError("the reply is to request %r, not %r" % (reply.get("id"), self._next_id))
        return reply

    def request(self, verb, timeout=None, **args):
        reply = self.call(verb, timeout=timeout, **args)
        if not reply.get("ok"):
            raise AgentError(reply)
        return reply

    # ---- core ------------------------------------------------------------

    def hello(self):
        return self.request("hello")

    def help(self):
        return self.request("help")

    def wait(self, frames=None, until=None, timeout_ms=None):
        """`frames` frames, or until the client is "idle" or "ready"."""
        patience = self.timeout
        if until is not None:
            patience = (10000 if timeout_ms is None else timeout_ms) / 1000.0 + 30.0
        elif frames is not None:
            patience = max(self.timeout, frames / 10.0 + 30.0)
        return self.request("wait", timeout=patience, frames=frames, until=until, timeout_ms=timeout_ms)

    def mouse(self, x, y, action="click", button=None, mods=None):
        return self.request("mouse", x=x, y=y, action=action, button=button, mods=mods)

    def drag(self, start, end, steps=None, button=None, mods=None):
        """Press at `start` (x, y), move to `end` over `steps` frames, release."""
        patience = max(self.timeout, (steps or 12) / 10.0 + 30.0)
        return self.request(
            "drag", timeout=patience, **{"from": list(start), "to": list(end)}, steps=steps, button=button, mods=mods
        )

    def wheel(self, x, y, notches, mods=None):
        return self.request("wheel", x=x, y=y, notches=notches, mods=mods)

    def key(self, chord, action=None, hold=None):
        return self.request("key", chord=chord, action=action, hold=hold)

    def type(self, text):
        patience = max(self.timeout, len(text) / 10.0 + 30.0)
        return self.request("type", timeout=patience, text=text)

    def shot(self, path, rect=None):
        return self.request("shot", path=os.path.abspath(path), rect=rect)

    def ui(self, panel=None, visible_only=None, text=None):
        return self.request("ui", panel=panel, visible_only=visible_only, text=text)

    def ui_click(self, widget=None, label=None, panel=None, tab=None, row=None, option=None, menu=None,
                 button=None, mods=None, double=None):
        return self.request(
            "ui_click", widget=widget, label=label, panel=panel, tab=tab, row=row, option=option, menu=menu,
            button=button, mods=mods, double=double,
        )

    def ui_set(self, text, widget=None, label=None, panel=None, enter=None):
        return self.request("ui_set", widget=widget, label=label, panel=panel, text=text, enter=enter)

    def log(self, since=None, limit=None, source=None):
        return self.request("log", since=since, limit=limit, source=source)

    def quit(self, discard=None):
        return self.request("quit", discard=discard)

    # ---- editor ----------------------------------------------------------

    def editor(self):
        return self.request("editor")

    def actions(self, text=None):
        return self.request("actions", text=text)

    def action(self, action):
        """Run the hotkey action whose id is `action` ("file.save_all")."""
        return self.request("action", action=action)

    def tile_to_screen(self, x, z, level=None):
        return self.request("tile_to_screen", x=x, z=z, level=level)

    def screen_to_tile(self, x, y):
        return self.request("screen_to_tile", x=x, y=y)

    def world_click(self, x, z, level=None, button=None, mods=None, force=None):
        return self.request("world_click", x=x, z=z, level=level, button=button, mods=mods, force=force)

    def camera(self, x=None, z=None, level=None, yaw=None, pitch=None, zoom=None):
        return self.request("camera", x=x, z=z, level=level, yaw=yaw, pitch=pitch, zoom=zoom)

    # ---- game ------------------------------------------------------------

    def game(self, chat_lines=None):
        return self.request("game", chat_lines=chat_lines)

    def npcs(self, name=None, npc_id=None, limit=None):
        return self.request("npcs", name=name, npc_id=npc_id, limit=limit)

    def npc_click(self, name=None, npc_id=None, slot=None, button=None, mods=None, tries=5):
        """Click an npc where it stands now.

        An npc that walks between the pointer's arrival and the press is not
        clicked (the reply says so, and nothing was pressed): this asks again,
        up to `tries` times, as a person follows it with the pointer.
        """
        reply = {}
        for _ in range(max(1, tries)):
            reply = self.call("npc_click", name=name, npc_id=npc_id, slot=slot, button=button, mods=mods)
            if reply.get("ok") or "ask again" not in reply.get("error", ""):
                break
        if not reply.get("ok"):
            raise AgentError(reply)
        return reply

    # ---- conveniences made of the verbs above ----------------------------

    def click(self, label, panel=None, **more):
        """Click what is captioned `label` (ui_click by label)."""
        return self.ui_click(label=label, panel=panel, **more)

    def wait_idle(self, timeout_ms=10000):
        return self.wait(until="idle", timeout_ms=timeout_ms)

    def widgets(self, text=None, panel=None):
        """Every on-screen widget (holding `text`), flat, each with its panel's title."""
        found = []
        for entry in self.ui(panel=panel, text=text, visible_only=True)["panels"]:
            for widget in entry.get("widgets", ()):
                widget = dict(widget)
                widget["panel"] = entry.get("title", "")
                found.append(widget)
        return found

    def select(self, option, widget=None, label=None, panel=None):
        """Open the dropdown named by `widget` or `label` and pick `option`.

        `option` is an option's label (whole, case aside) or its index. The
        open list shows ten rows; when the option is outside them the wheel is
        turned over the list until it is in, as a person would.
        """
        opened = self.ui_click(widget=widget, label=label, panel=panel)
        polarity = 1
        stuck = False
        for _ in range(600):
            state = self.ui(visible_only=True)
            dropdown = state.get("dropdown")
            if not dropdown:
                raise AgentError({"ok": False, "error": "clicking widget %s opened no list" % opened.get("widget")})
            index = option
            if not isinstance(option, int):
                names = [w for p in state["panels"] for w in p.get("widgets", ()) if w["id"] == dropdown["widget"]]
                options = names[0].get("options", []) if names else []
                matches = [i for i, name in enumerate(options) if name.lower() == str(option).lower()]
                if not matches:
                    self.key("escape")
                    raise AgentError({"ok": False, "error": "the list has no option \"%s\": it has %s" % (
                        option, ", ".join(options) or "none this client can list")})
                index = matches[0]
            rows = dropdown["rows"]
            shown = [row["option"] for row in rows]
            if index in shown:
                return self.ui_click(option=index)
            boxes = [row["rect"] for row in rows if "rect" in row]
            if not boxes or not shown:
                break
            x = boxes[0][0] + boxes[0][2] // 2
            y = boxes[0][1] + boxes[0][3] // 2
            before = dropdown["first"]
            # The list's first row has to go down (-1) or up (+1) to reach it.
            want = -1 if index < shown[0] else 1
            # A positive notch scrolls up, toward the first option; if this
            # list reads the wheel the other way, the first notches say so:
            # the list moves the wrong way, or (already at that end) not at
            # all.
            self.wheel(x, y, -want * polarity)
            after = (self.ui(visible_only=True).get("dropdown") or {}).get("first", before)
            if after == before:
                if stuck:
                    break
                stuck = True
                polarity = -polarity
            else:
                stuck = False
                if (after - before) * want < 0:
                    polarity = -polarity
        self.key("escape")
        raise AgentError({"ok": False, "error": "option %r could not be scrolled into the open list" % (option,)})


# ---- start a client ----------------------------------------------------------


def client_executable():
    return os.path.join(REPO_ROOT, "src", "torirs_win64.exe" if os.name == "nt" else "torirs")


def start(manifest, window=None, port_file=None, exe=None, log=None, wait=180.0, extra=()):
    """Start a client that listens, wait for it to boot, return (Client, info).

    The client runs from the repository root and outlives this process; its
    stdout and stderr are in `info["log"]`. End it with Client.quit().
    """
    # Absolute, in the platform's own spelling: Windows does not find
    # "src/torirs_win64.exe" (a relative path with forward slashes) even from
    # the right directory -- CreateProcess answers "file not found".
    exe = os.path.abspath(exe or client_executable())
    if not os.path.exists(exe):
        raise FileNotFoundError("%s is not built: build the client first" % exe)
    manifest = os.path.abspath(manifest)
    if not os.path.exists(manifest):
        raise FileNotFoundError("no manifest at %s" % manifest)
    port_file = os.path.abspath(port_file or default_port_file())
    log = os.path.abspath(log or port_file + ".log")
    if os.path.exists(port_file):
        os.remove(port_file)

    command = [exe, "--manifest", manifest, "--agent-port", "0", "--agent-port-file", port_file]
    if window:
        command += ["--window", window]
    command += list(extra)
    options = {}
    if os.name == "nt":
        # Its own process group and no console of ours: closing the shell
        # that started it does not close it.
        options["creationflags"] = 0x00000008 | 0x00000200
    else:
        options["start_new_session"] = True
    with open(log, "wb") as sink:
        process = subprocess.Popen(
            command, cwd=REPO_ROOT, stdin=subprocess.DEVNULL, stdout=sink, stderr=subprocess.STDOUT, **options
        )

    deadline = time.time() + wait
    port = None
    while port is None:
        if process.poll() is not None:
            raise RuntimeError("the client exited with code %s before it listened; its output is in %s"
                               % (process.returncode, log))
        if time.time() > deadline:
            raise TimeoutError("the client did not listen within %.0f s; its output is in %s" % (wait, log))
        try:
            port = read_port_file(port_file)
        except (OSError, ValueError):
            time.sleep(0.1)

    agent = Client(port=port)
    info = {"port": port, "pid": process.pid, "port_file": port_file, "log": log}
    remaining_ms = int(max(1.0, deadline - time.time()) * 1000)
    ready = agent.call("wait", timeout=remaining_ms / 1000.0 + 30.0, until="ready", timeout_ms=remaining_ms)
    info["hello"] = agent.hello()
    if not ready.get("ok"):
        info["error"] = "the client listens but had not booted after %.0f s: %s" % (wait, ready.get("error", ""))
    return agent, info


# ---- the command line --------------------------------------------------------

# A verb's bare first arguments, in order: `click Catalog` is `ui_click label=Catalog`.
POSITIONAL = {
    "click": ("ui_click", ("label",)),
    "set": ("ui_set", ("label", "text")),
    "shot": ("shot", ("path",)),
    "key": ("key", ("chord",)),
    "type": ("type", ("text",)),
    "action": ("action", ("action",)),
    "npc_click": ("npc_click", ("name",)),
}

INT_LIST = re.compile(r"^-?\d+(,-?\d+)+$")
# Arguments that are text whatever they look like: a label "12" is not twelve.
TEXT_ARGS = {"label", "text", "panel", "path", "chord", "action", "button", "until", "source", "name"}
# Every argument a verb takes. `name=value` is an argument when the name is
# one of these; anything else before the bare arguments are filled is one of
# them (a label may hold an '=').
KNOWN_ARGS = TEXT_ARGS | {
    "x", "y", "z", "frames", "timeout_ms", "mods", "from", "to", "steps", "notches", "hold", "rect", "visible_only",
    "widget", "tab", "row", "option", "menu", "double", "enter", "since", "limit", "discard", "level", "force", "yaw",
    "pitch", "zoom", "chat_lines", "npc_id", "slot",
}


def parse_value(key, text):
    if key == "mods":
        return [name for name in text.split(",") if name]
    if key in TEXT_ARGS:
        return text
    if INT_LIST.match(text):
        return [int(part) for part in text.split(",")]
    try:
        return json.loads(text)
    except ValueError:
        return text


def parse_arguments(verb, words):
    """(verb, args) for the words after the verb: `name=value`, and bare ones by position."""
    names = ()
    if verb in POSITIONAL:
        verb, names = POSITIONAL[verb]
    args = {}
    for word in words:
        key, equals, value = word.partition("=")
        free = [name for name in names if name not in args]
        if equals and (key in KNOWN_ARGS or (not free and re.match(r"^[a-z_]+$", key))):
            args[key] = parse_value(key, value)
        elif free:
            args[free[0]] = word
        else:
            raise SystemExit("torirs_agent: '%s' is not name=value" % word)
    missing = [name for name in names if name not in args]
    if missing:
        raise SystemExit("torirs_agent: this needs <%s>" % "> <".join(missing))
    if verb == "shot" and "path" in args:
        args["path"] = os.path.abspath(args["path"])
    return verb, args


def usage(code):
    sys.stderr.write(__doc__)
    raise SystemExit(code)


def main(argv):
    port = None
    port_file = None
    host = "127.0.0.1"
    timeout = 60.0
    words = list(argv)

    while words and words[0].startswith("--"):
        flag = words.pop(0)
        if flag in ("--help", "-h"):
            usage(0)
        if not words:
            usage(2)
        value = words.pop(0)
        if flag == "--port":
            port = int(value)
        elif flag == "--port-file":
            port_file = value
        elif flag == "--host":
            host = value
        elif flag == "--timeout":
            timeout = float(value)
        else:
            sys.stderr.write("torirs_agent: unknown option %s\n" % flag)
            raise SystemExit(2)
    if not words:
        usage(2)
    verb = words.pop(0)

    if verb == "start":
        options = {"port_file": port_file}
        extra = []
        if "--" in words:
            split = words.index("--")
            extra = words[split + 1:]
            words = words[:split]
        manifest = None
        while words:
            word = words.pop(0)
            if word in ("--window", "--port-file", "--exe", "--log", "--wait"):
                if not words:
                    usage(2)
                value = words.pop(0)
                options[word[2:].replace("-", "_")] = float(value) if word == "--wait" else value
            elif manifest is None:
                manifest = word
            else:
                usage(2)
        if manifest is None:
            usage(2)
        try:
            agent, info = start(manifest, extra=extra, **options)
        except (OSError, RuntimeError, TimeoutError, ConnectionError) as problem:
            sys.stderr.write("torirs_agent: %s\n" % problem)
            raise SystemExit(2)
        agent.close()
        print(json.dumps(info, indent=2))
        raise SystemExit(1 if "error" in info else 0)

    try:
        agent = Client(port=port, port_file=port_file, host=host, timeout=timeout)
    except (OSError, ValueError) as problem:
        sys.stderr.write(
            "torirs_agent: no client to talk to (%s). Start one with --agent-port, or `start <manifest>`.\n" % problem
        )
        raise SystemExit(2)
    with agent:
        try:
            if verb == "select":
                if len(words) < 2:
                    raise SystemExit("torirs_agent: select <dropdown label> <option label> [panel=...]")
                _, more = parse_arguments("ui_click", words[2:])
                reply = agent.select(words[1], label=words[0], panel=more.get("panel"))
                reply.setdefault("ok", True)
            else:
                verb, args = parse_arguments(verb, words)
                # A verb that waits answers late by design: give it the time
                # it was told to take, and a margin.
                patience = max(timeout, 30.0 + sum(
                    float(amount) / scale
                    for amount, scale in (
                        (len(str(args.get("text", ""))), 10.0),
                        (args.get("steps", 0), 10.0),
                        (args.get("frames", 0), 10.0),
                        (args.get("timeout_ms", 10000 if verb == "wait" else 0), 1000.0),
                    )
                    if isinstance(amount, (int, float))
                ))
                reply = agent.call(verb, timeout=patience, **args)
        except AgentError as refusal:
            reply = refusal.reply
        except (OSError, ConnectionError) as problem:
            sys.stderr.write("torirs_agent: %s\n" % problem)
            raise SystemExit(2)
    print(json.dumps(reply, indent=2))
    raise SystemExit(0 if reply.get("ok") else 1)


if __name__ == "__main__":
    main(sys.argv[1:])
