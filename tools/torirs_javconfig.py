#!/usr/bin/env python3
"""
Serve a jav_config that points an OldSchool client at this machine.

RuneLite takes `--jav_config=<url>` and hands the result to the client as its
applet configuration. The client reads the game server's host out of
`codebase` -- so changing that one line is the whole of "connect somewhere
else". Everything below it is copied from the live config because the client
reads those parameters too, and an absent one is not always a default.

    tools/torirs_javconfig.py --host 127.0.0.1 --port 8080
    tools/runelite_patch.py --print-launch

The `param=25` line is the revision. It is the number that has to match the
server, and it is the first thing to check when a client says it is out of date.

THE PARAMETER LIST IS THE WHOLE LIVE ONE, minus nothing. A first version of this
file kept only the params that looked like they mattered — the revision, some
flags — and dropped the ten that are URLs or tokens for Jagex account services
this server has no equivalent of. The client dereferences several of those
during applet init whether or not it ever uses them, and the result is a
NullPointerException on a static deep inside the client
(`Cannot read field "ap" because "cq.gh" is null`) before it issues a single
JS5 request. Nothing in that failure points at a missing parameter.

So the URLs below still point at Jagex. They are not reached by anything this
server drives; they are here because their ABSENCE is what breaks. Trimming
them is not a cleanup.
"""

import argparse
import http.server
import socketserver

TEMPLATE = """title=ToriRS
adverturl=http://{host}/
codebase=http://{host}/
cachedir={cachedir}
storebase=0
initial_jar=gamepack.jar
initial_class=client.class
termsurl=http://{host}/terms
privacyurl=http://{host}/privacy
viewerversion=124
win_sub_version=1
mac_sub_version=2
other_sub_version=2
window_preferredwidth=800
window_preferredheight=600
advert_height=0
applet_minwidth=765
applet_minheight=503
applet_maxwidth=5760
applet_maxheight=2160
msg=lang0=English
param=25={revision}
param=12={world_id}
param=7=0
param=4=1
param=14=0
param=15={environment}
param=21=0
param=6=0
param=5=1
param=10=5
param=3=true
param=8=true
param=16=false
param=13=.runescape.com
param=18=
param=9=ElZAIrq5NpKN6D3mDdihco3oPeYN2KFy2DCquj7JMmECPmLrDP3Bnw
param=2=https://payments.jagex.com/
param=11=https://auth.jagex.com/
param=17=http://www.runescape.com/g=oldscape/slr.ws?order=LPWM
param=19=196515767263-1oo20deqm6edn7ujlihl6rpadk9drhva.apps.googleusercontent.com
param=20=https://social.auth.jagex.com/
param=22=https://auth.runescape.com/
param=28=https://account.jagex.com/
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1",
                    help="host the CLIENT should connect to (goes into codebase)")
    ap.add_argument("--port", type=int, default=8080, help="port to serve this config on")
    ap.add_argument("--revision", type=int, default=239)
    ap.add_argument("--world-id", type=int, default=471,
                    help="applet world id; with a nonzero environment the "
                         "client connects to 40000 + world id")
    ap.add_argument("--environment", type=int, default=0,
                    help="applet environment; zero keeps the live fixed ports")
    ap.add_argument("--cachedir", default="torirs",
                    help="subdirectory of ~/jagexcache the client stores its cache in. "
                         "NOT 'oldschool': that is the live client's, and a client of one "
                         "revision reading a cache another revision wrote crashes on the "
                         "first archive whose format moved.")
    ap.add_argument("--print", dest="dump", action="store_true",
                    help="print the config and exit")
    args = ap.parse_args()

    body = TEMPLATE.format(host=args.host, revision=args.revision,
                           cachedir=args.cachedir, world_id=args.world_id,
                           environment=args.environment).encode()
    if args.dump:
        print(body.decode(), end="")
        return

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, fmt, *a):
            print(f"javconfig: {self.address_string()} {fmt % a}", flush=True)

    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("127.0.0.1", args.port), Handler) as srv:
        print(
            f"javconfig: http://127.0.0.1:{args.port}/jav_config.ws "
            f"-> codebase http://{args.host}/, revision {args.revision}",
            flush=True,
        )
        srv.serve_forever()


if __name__ == "__main__":
    main()
