#!/usr/bin/env python3
"""Drive RuneLite (Jagex's own client, our deob gamepack) against a private
server -- the oracle for "does the real 242 client do what Jagex's does".

Everything goes through JCtl, the gamepack's control channel
(Deobfuscator instr/revisions/<rev>/instrumentation/JCtl.java; RUNNING.md §6).
Act on things by NAME, never by pixel: an NPC wanders between a screenshot and
a click, and a hull under the side panel cannot be clicked (2026-11-17, ten
minutes chasing Captain Barnaby by coordinates).

    rlctl.py up [--account wire59] [--at x,z,level] [--coins 1000]
                                     private server (recorded) + RuneLite, logged in
    rlctl.py down                    stop both
    rlctl.py jctl '<command>'        one raw JCtl line (`npcs 20`, `menu`, `gamestate`...)
    rlctl.py cheat 'tele 0_41_51_54_11'
                                     a `::` command (the account is admin)
    rlctl.py npc 'Captain Barnaby' Brimhaven
                                     right-click the nearest such NPC, pick the row
    rlctl.py walkto <x> <z>          walk until there (or beside it, where the route ends)
    rlctl.py jctl 'clickloc Gangplank Cross'   ...and every JDrive command: where, locs,
                                     clickloc, objs, clickobj, inv, clickitem, useon,
                                     walk, dialog, continue, choose, waitfor
                                     (Deobfuscator instr/revisions/242/instrumentation/JDrive.java)
    rlctl.py shot out.png            the client's framebuffer
    rlctl.py burst <dir> [n] [ms]    n screenshots ms apart, tiled into <dir>/burst.png
    rlctl.py quest <test> [--keep] [--trace]
                                     run a quest test (test/quests/<test>.lua) through
                                     the gamepack: the C client's own quest driver,
                                     hosted by rldrive, primitives answered by JQuest.java
    rlctl.py wire [<recording.bin>] [name]
                                     transcribe the newest (or given) recording of
                                     our server with EVERY RSProx filter on
                                     (home-wire), to diff against a Jagex session

State lives in the scratch directory named by $RLCTL_DIR (default
/tmp/rlctl-$USER): saves/, the run dir, the server script. Ports are private
(game 45123, jav_config 9081, JCtl 43651) so the owner's own RuneLite (43601)
and server (43594) are never touched.
"""
import argparse
import os
import shutil
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DIR = os.environ.get("RLCTL_DIR", "/tmp/rlctl-%s" % os.environ.get("USER", "me"))
GAME_PORT, JAV_PORT, JCTL_PORT = 45123, 9081, 43651
MANIFEST = os.path.join(ROOT, "build", "manifests", "osrs242-runelite.ini")
SERVER_BIN = os.environ.get("RLCTL_SERVER", os.path.join(ROOT, "src", "build_opt", "torirsserver"))
JAVA = "/Library/Java/JavaVirtualMachines/temurin-21.jdk/Contents/Home/bin/java"
WIRE_HOME = os.path.expanduser("~/.rsprox-index-cache/home-wire")
PASSWORD = "rlctl-test-1"


def jctl(line, timeout=60.0):
    """One JCtl command; its one reply line."""
    with socket.create_connection(("127.0.0.1", JCTL_PORT), timeout=timeout) as s:
        s.sendall((line + "\n").encode())
        buf = b""
        while not buf.endswith(b"\n"):
            chunk = s.recv(65536)
            if not chunk:
                break
            buf += chunk
    return buf.decode(errors="replace").strip()


def jctl_up():
    try:
        return jctl("stat", timeout=2).startswith("ok")
    except OSError:
        return False


def wait_for(pred, seconds, what):
    end = time.time() + seconds
    while time.time() < end:
        if pred():
            return
        time.sleep(1)
    sys.exit("rlctl: timed out waiting for " + what)


def listening(port):
    try:
        socket.create_connection(("127.0.0.1", port), timeout=1).close()
        return True
    except OSError:
        return False


def server_env():
    content = os.path.join(ROOT, "OSRS-Content", "osrs-content")
    cache = os.path.join(ROOT, "cache.osrs242")
    env = dict(os.environ)
    env.update(TORIRSSERVER_CONTENT_DIR=content, TORIRSSERVER_CONTENT=content,
               TORIRSSERVER_SAVES=os.path.join(DIR, "saves"), TORIRSSERVER_CACHE=cache,
               TORIRSSERVER_JS5_CACHE=cache, TORIRSSERVER_REV="osrs242", TORIRSSERVER_VERBOSE="1",
               TORIRSSERVER_DEV="1", TORIRSSERVER_JS5_REV="242")
    return env


def make_account(name, at, fixture_name="fresh_lumbridge.ini"):
    saves = os.path.join(DIR, "saves")
    os.makedirs(saves, exist_ok=True)
    env = server_env()
    if not os.path.exists(os.path.join(saves, "accounts", name + ".ini")):
        subprocess.run([SERVER_BIN, "--account", "create", name, "admin"], input=PASSWORD + "\n",
                       text=True, env=env, check=True, capture_output=True)
    # Recorded: every session is an RSProx .bin under saves/recordings/<name>/.
    subprocess.run([SERVER_BIN, "--account", "record", name, "on"], env=env, check=True,
                   capture_output=True)
    fixture = os.path.join(ROOT, "test", "quests", "fixtures", fixture_name)
    out = []
    for line in open(fixture):
        key = line.split("=", 1)[0].strip()
        if key == "name":
            line = "name = %s\n" % name
        elif at and key in ("x", "z", "level"):
            x, z, level = at
            line = "%s = %d\n" % (key, {"x": x, "z": z, "level": level}[key])
        out.append(line)
    with open(os.path.join(saves, name + ".ini"), "w") as handle:
        handle.writelines(out)


def cmd_up(args):
    os.makedirs(DIR, exist_ok=True)
    assert os.path.exists(SERVER_BIN), "no server binary at %s (RLCTL_SERVER=...)" % SERVER_BIN
    if args.at is not None:
        at = tuple(int(v) for v in args.at.split(","))
        make_account(args.account, at, getattr(args, "fixture", None) or "fresh_lumbridge.ini")
    if jctl_up():
        print("rlctl: RuneLite already up on JCtl %d -- `rlctl.py down` first to restart" % JCTL_PORT)
        return
    home = os.path.join(DIR, "home")
    os.makedirs(home, exist_ok=True)
    # The game cache run-runelite.sh seeds is the owner's ~/jagexcache; only
    # RuneLite's settings, profiles and plugins are this client's own.
    jagexcache = os.path.join(home, "jagexcache")
    if not os.path.lexists(jagexcache):
        os.symlink(os.path.expanduser("~/jagexcache"), jagexcache)
    env = server_env()
    env.update(GAME_PORT=str(GAME_PORT), JAV_PORT=str(JAV_PORT), JCTL_PORT=str(JCTL_PORT),
               RUNDIR=os.path.join(DIR, "run"), TORIRS_MOCK_BIN=SERVER_BIN, TORIRS_NO_PREPARE="1",
               # The gamepack logs itself in (instrumentation CS2AutoLogin.java).
               # Its own RuneLite home: RuneLite keeps everything under
               # user.home/.runelite, and sharing the owner's meant his synced
               # profile and external plugins in every test client, and a
               # profiles.json he was writing read half-done -- a launch fell
               # back to the default profile, custom chrome off, and came up
               # with the canvas under the traffic lights (2026-11-18).
               JAVA_EXTRA_OPTS="-Dcs2.autologin=%s:%s -Duser.home=%s" % (args.account, PASSWORD, home))
    log = open(os.path.join(DIR, "launch.out"), "w")
    launcher = subprocess.Popen(["bash", os.path.join(ROOT, "run-runelite.sh"), MANIFEST], cwd=ROOT, env=env,
                                stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    with open(os.path.join(DIR, "launcher.pid"), "w") as handle:
        handle.write(str(launcher.pid))
    wait_for(jctl_up, 900, "JCtl (the gamepack may be rebuilding; see %s/launch.out)" % DIR)
    wait_for(lambda: jctl("gamestate").endswith("LOGGED_IN"), 120, "the auto-login")
    time.sleep(2)
    # The welcome screen's "Click here to play" (the lobby), when it is up.
    jctl("click 382 335")
    time.sleep(2)
    if args.coins:
        cmd_cheat(argparse.Namespace(text="give coins %d" % args.coins))
    print("rlctl: up -- %s logged in at %s; server log %s/run/server.log" % (args.account, args.at, DIR))


RLDRIVE_SRC = os.path.join(ROOT, "tools", "torirs_runelite", "rldrive.c")
RLDRIVE_BIN = os.path.join(ROOT, "build", "rldrive", "rldrive")


def build_rldrive():
    """The host that runs the quest driver against the gamepack (rldrive.c)."""
    if os.path.exists(RLDRIVE_BIN) and os.path.getmtime(RLDRIVE_BIN) >= os.path.getmtime(RLDRIVE_SRC):
        return
    os.makedirs(os.path.dirname(RLDRIVE_BIN), exist_ok=True)
    lua = os.path.join(ROOT, "3rd", "lua")
    sources = [os.path.join(lua, f) for f in sorted(os.listdir(lua))
               if f.endswith(".c") and f not in ("lua.c", "luac.c")]
    subprocess.run(["cc", "-std=gnu11", "-O1", "-g", "-I" + lua, RLDRIVE_SRC] + sources +
                   ["-lm", "-o", RLDRIVE_BIN], check=True)


def cmd_quest(args):
    """A quest test (test/quests/<id>.lua), driven through the gamepack: the
    same Lua the C client runs, its primitives answered by JQuest.java."""
    sys.path.insert(0, os.path.join(ROOT, "tools", "quest_gate"))
    import run as quest_run  # write_wrapper_script: the same wrapper run.py makes
    test = args.test if args.test.endswith(".lua") else os.path.join(ROOT, "test", "quests", args.test + ".lua")
    test_id = os.path.splitext(os.path.basename(test))[0]
    import re
    m = re.search(r'fixture\s*=\s*"([^"]+)"', open(test).read())
    fixture = m.group(1) if m else "fresh_lumbridge.ini"
    if jctl_up() and not args.keep:
        cmd_down(args)
    if not jctl_up():
        os.makedirs(DIR, exist_ok=True)
        make_account(args.account, None, fixture)
        cmd_up(argparse.Namespace(account=args.account, at=None, coins=0, fixture=fixture))
    build_rldrive()
    session = args.session or os.path.join(DIR, "quest", test_id)
    if os.path.isdir(session):
        shutil.rmtree(session)
    os.makedirs(os.path.join(session, "script"))
    script = os.path.join(session, "script", test_id + ".lua")
    quest_run.write_wrapper_script(test, script)
    cmd = [RLDRIVE_BIN, "--port", str(JCTL_PORT), "--script", script, "--session", session, "--root", ROOT]
    if args.trace:
        cmd.append("--trace")
    print("rlctl: driving %s (fixture %s); ledger %s/ledger.tsv" % (test_id, fixture, session))
    sys.exit(subprocess.run(cmd).returncode)


def cmd_down(args):
    if jctl_up():
        try:
            jctl("quit", timeout=3)
        except OSError:
            pass
    for name in ("server", "javconfig"):
        pid_file = os.path.join(DIR, "run", name + ".pid")
        if os.path.exists(pid_file):
            try:
                os.kill(int(open(pid_file).read().strip()), 15)
            except (OSError, ValueError):
                pass
    # The launcher stops the server and jav_config by their pid files when its
    # client exits. A new `up` before that cleanup ran had its fresh server
    # killed by the old launcher (2026-11-17), so wait for the launcher itself.
    pid_file = os.path.join(DIR, "launcher.pid")
    if os.path.exists(pid_file):
        try:
            pid = int(open(pid_file).read().strip())
            end = time.time() + 60
            while time.time() < end:
                os.kill(pid, 0)
                time.sleep(0.5)
        except (OSError, ValueError):
            pass
        os.remove(pid_file)
    print("rlctl: down")


def cmd_jctl(args):
    print(jctl(args.line))


def cmd_cheat(args):
    # The chat box takes typing directly; Enter sends it as CLIENT_CHEAT.
    for line in ("type ::" + args.text, "key 10"):
        reply = jctl(line)
        assert reply.startswith("ok"), reply
    time.sleep(1.2)
    print("rlctl: ::" + args.text)


def cmd_npc(args):
    reply = jctl("clicknpcname %s %s" % (args.name.replace(" ", "_"), args.option))
    print(reply)
    if not reply.startswith("ok"):
        sys.exit(1)


def where():
    """{x, z, plane, idle, dialog} from JCtl `where`."""
    reply = jctl("where")
    out = {}
    for part in reply.split()[2:]:
        if "=" in part:
            key, value = part.split("=", 1)
            out[key] = value
    return out


def cmd_walkto(args):
    """Walk until on (x, z), or as close as the route allows and stopped there.
    The server routes to the nearest reachable tile, as Jagex's does, so a
    blocked target ends beside it; the minimap's precision is a tile or so,
    so a far walk is finished by an on-screen click once within range."""
    end = time.time() + args.timeout
    while time.time() < end:
        here = where()
        if int(here["x"]) == args.x and int(here["z"]) == args.z:
            print("rlctl: at %d,%d" % (args.x, args.z))
            return
        print(jctl("walk %d %d" % (args.x, args.z)))
        still = 0
        last = None
        while time.time() < end and still < 3:
            time.sleep(0.6)
            here = where()
            pos = (here["x"], here["z"])
            still = still + 1 if pos == last else 0
            last = pos
            if int(here["x"]) == args.x and int(here["z"]) == args.z:
                print("rlctl: at %d,%d" % (args.x, args.z))
                return
        if abs(int(here["x"]) - args.x) <= 1 and abs(int(here["z"]) - args.z) <= 1:
            # Beside it and stopped: the tile itself is not reachable.
            print("rlctl: beside %d,%d at %s,%s (the route ends there)" % (args.x, args.z, here["x"], here["z"]))
            return
    sys.exit("rlctl: walkto %d,%d timed out at %s" % (args.x, args.z, where()))


def cmd_shot(args):
    print(jctl("shot " + os.path.abspath(args.path)))


def cmd_burst(args):
    os.makedirs(args.dir, exist_ok=True)
    paths = []
    for i in range(args.n):
        path = os.path.abspath(os.path.join(args.dir, "f%03d.png" % i))
        jctl("shot " + path)
        paths.append(path)
        time.sleep(args.ms / 1000.0)
    try:
        from PIL import Image
    except ImportError:
        print("rlctl: %d frames in %s (no PIL to tile them)" % (len(paths), args.dir))
        return
    cols = 6
    thumbs = [Image.open(p).resize((255, 168)) for p in paths]
    sheet = Image.new("RGB", (255 * cols, 168 * ((len(thumbs) + cols - 1) // cols)))
    for i, im in enumerate(thumbs):
        sheet.paste(im, ((i % cols) * 255, (i // cols) * 168))
    sheet.save(os.path.join(args.dir, "burst.png"))
    print("rlctl: %d frames, sheet %s" % (len(paths), os.path.join(args.dir, "burst.png")))


def cmd_wire(args):
    rec = args.recording
    if not rec:
        rec_dir = os.path.join(DIR, "saves", "recordings")
        bins = [os.path.join(dp, f) for dp, _, fs in os.walk(rec_dir) for f in fs if f.endswith(".bin")]
        assert bins, "no recordings under " + rec_dir
        rec = max(bins, key=os.path.getmtime)
    archive = os.path.join(WIRE_HOME, ".rsprox", "binary", "archive")
    assert os.path.isdir(archive), "no %s (see memory wire-diff-ours-vs-jagex)" % archive
    shutil.copy(rec, os.path.join(archive, args.name + ".bin"))
    txt = os.path.join(archive, args.name + ".txt")
    if os.path.exists(txt):
        os.remove(txt)
    classpath = open(os.path.expanduser("~/rsprox-index-data/rsprox-c.classpath")).read().strip()
    with open(os.path.join(archive, args.name + ".log"), "w") as log:
        subprocess.run([JAVA, "-Duser.home=" + WIRE_HOME, "-cp", classpath,
                        "net.rsprox.proxy.cli.TranscribeCommandKt", "-name", "archive/" + args.name],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    print(txt)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("up")
    p.add_argument("--account", default="wire59")
    p.add_argument("--at", default="3222,3218,0", help="x,z,level the save starts at")
    p.add_argument("--coins", type=int, default=0)
    p.set_defaults(fn=cmd_up)
    sub.add_parser("down").set_defaults(fn=cmd_down)
    p = sub.add_parser("jctl")
    p.add_argument("line")
    p.set_defaults(fn=cmd_jctl)
    p = sub.add_parser("cheat")
    p.add_argument("text")
    p.set_defaults(fn=cmd_cheat)
    p = sub.add_parser("npc")
    p.add_argument("name")
    p.add_argument("option", nargs="?", default="Talk-to")
    p.set_defaults(fn=cmd_npc)
    p = sub.add_parser("walkto")
    p.add_argument("x", type=int)
    p.add_argument("z", type=int)
    p.add_argument("--timeout", type=float, default=60.0)
    p.set_defaults(fn=cmd_walkto)
    p = sub.add_parser("shot")
    p.add_argument("path")
    p.set_defaults(fn=cmd_shot)
    p = sub.add_parser("burst")
    p.add_argument("dir")
    p.add_argument("n", nargs="?", type=int, default=24)
    p.add_argument("ms", nargs="?", type=int, default=300)
    p.set_defaults(fn=cmd_burst)
    p = sub.add_parser("quest")
    p.add_argument("test", help="test id (test/quests/<id>.lua) or a path")
    p.add_argument("--account", default="wire59")
    p.add_argument("--session", help="where the ledger and shots land")
    p.add_argument("--keep", action="store_true", help="drive the client already up (no fresh login)")
    p.add_argument("--trace", action="store_true", help="print every JCtl exchange")
    p.set_defaults(fn=cmd_quest)
    p = sub.add_parser("wire")
    p.add_argument("recording", nargs="?")
    p.add_argument("name", nargs="?", default="ours")
    p.set_defaults(fn=cmd_wire)
    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
