#!/usr/bin/env python3
"""Drive runescript-lsp over its own protocol and check the answers.

    make -C tools/runescript-lsp test
    python3 tools/runescript-lsp/test/protocol_test.py [path-to-binary]

The fixture beside this file is a miniature content tree: a varp allocation and
the record it allocates, an obj file, a constant file, and two scripts — one
declaring a proc, one calling it and getting three things wrong on purpose. The
wrong ones are the point: a test that only opens correct input cannot tell a
working diagnostic from an absent one.
"""

import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURE = os.path.join(HERE, "fixture")
DEFAULT_BINARY = os.path.join(os.path.dirname(HERE), "runescript-lsp")

FAILURES = []
CHECKS = 0


def check(condition, description, detail=""):
    global CHECKS
    CHECKS += 1
    if condition:
        print(f"  ok   {description}")
    else:
        print(f"  FAIL {description}")
        if detail:
            print(f"       {detail}")
        FAILURES.append(description)


class Server:
    def __init__(self, binary):
        self.process = subprocess.Popen(
            [binary],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.next_id = 1
        self.notifications = []

    def send(self, message):
        body = json.dumps(message).encode("utf-8")
        self.process.stdin.write(b"Content-Length: %d\r\n\r\n" % len(body))
        self.process.stdin.write(body)
        self.process.stdin.flush()

    def request(self, method, params):
        request_id = self.next_id
        self.next_id += 1
        self.send({"jsonrpc": "2.0", "id": request_id, "method": method, "params": params})
        while True:
            message = self.read()
            if message is None:
                raise RuntimeError(f"server closed while answering {method}")
            if message.get("id") == request_id:
                return message.get("result")
            self.notifications.append(message)

    def notify(self, method, params):
        self.send({"jsonrpc": "2.0", "method": method, "params": params})

    def read(self):
        length = 0
        while True:
            line = self.process.stdout.readline()
            if not line:
                return None
            if line in (b"\r\n", b"\n"):
                break
            if line.lower().startswith(b"content-length:"):
                length = int(line.split(b":")[1].strip())
        if not length:
            return None
        body = b""
        while len(body) < length:
            chunk = self.process.stdout.read(length - len(body))
            if not chunk:
                break
            body += chunk
        return json.loads(body.decode("utf-8"))

    def drain(self, seconds=0.4):
        """Collect notifications the server pushed without being asked."""
        deadline = time.time() + seconds
        self.process.stdout.flush()
        while time.time() < deadline:
            # A ping with a known answer is the portable way to find out
            # whether anything is queued ahead of it.
            result = self.request("textDocument/documentSymbol", {
                "textDocument": {"uri": "file:///nonexistent"}
            })
            del result
            return

    def close(self):
        try:
            self.request("shutdown", {})
            self.notify("exit", {})
            self.process.wait(timeout=5)
        except Exception:
            self.process.kill()


def uri_of(*parts):
    return "file://" + os.path.join(FIXTURE, *parts)


def read_fixture(*parts):
    with open(os.path.join(FIXTURE, *parts), encoding="utf-8") as handle:
        return handle.read()


def diagnostics_for(server, uri):
    return [
        note["params"]["diagnostics"]
        for note in server.notifications
        if note.get("method") == "textDocument/publishDiagnostics"
        and note["params"]["uri"] == uri
    ]


def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_BINARY
    if not os.path.exists(binary):
        print(f"no binary at {binary} — run `make -C tools/runescript-lsp` first")
        return 1

    server = Server(binary)

    print("initialize")
    result = server.request("initialize", {
        "processId": os.getpid(),
        "rootUri": "file://" + FIXTURE,
        "capabilities": {},
    })
    capabilities = result.get("capabilities", {})
    check(capabilities.get("hoverProvider") is True, "advertises hover")
    check(capabilities.get("definitionProvider") is True, "advertises definition")
    check("semanticTokensProvider" in capabilities, "advertises semantic tokens")
    legend = capabilities.get("semanticTokensProvider", {}).get("legend", {})
    token_types = legend.get("tokenTypes", [])
    check("function" in token_types and "keyword" in token_types,
          "semantic token legend is populated", str(token_types[:5]))
    server.notify("initialized", {})

    main_uri = uri_of("scripts", "main.rs2")
    main_text = read_fixture("scripts", "main.rs2")
    server.notify("textDocument/didOpen", {
        "textDocument": {"uri": main_uri, "languageId": "runescript",
                         "version": 1, "text": main_text},
    })

    lines = main_text.split("\n")

    def position_of(needle, occurrence=0):
        seen = 0
        for row, line in enumerate(lines):
            start = 0
            while True:
                column = line.find(needle, start)
                if column < 0:
                    break
                if seen == occurrence:
                    return {"line": row, "character": column + 1}
                seen += 1
                start = column + 1
        raise AssertionError(f"{needle!r} is not in the fixture")

    print("\nhover")
    hover = server.request("textDocument/hover", {
        "textDocument": {"uri": main_uri},
        "position": position_of("bronze_bar"),
    })
    text = json.dumps(hover)
    check("obj" in text, "hover on a bare name says which namespace it is", text[:200])
    check("Bronze bar" in text, "hover quotes the record's own name= field", text[:200])

    hover = server.request("textDocument/hover", {
        "textDocument": {"uri": main_uri},
        "position": position_of("~string_bow"),
    })
    text = json.dumps(hover)
    check("proc" in text, "hover on a ~proc says proc", text[:200])
    check("boolean" in text, "hover on a ~proc shows its signature", text[:300])
    check("unstrung bow" in text, "hover on a ~proc shows its doc comment", text[:400])

    hover = server.request("textDocument/hover", {
        "textDocument": {"uri": main_uri},
        "position": position_of("%bankpin_code"),
    })
    text = json.dumps(hover)
    check("varp" in text, "hover on a %var says varp", text[:200])
    check("5727" in text, "hover on a %var reports the allocated id", text[:300])

    hover = server.request("textDocument/hover", {
        "textDocument": {"uri": main_uri},
        "position": position_of("$count"),
    })
    text = json.dumps(hover)
    check("int" in text and "local" in text, "hover on a $local says its type", text[:200])

    print("\ndefinition")
    definition = server.request("textDocument/definition", {
        "textDocument": {"uri": main_uri},
        "position": position_of("~string_bow"),
    })
    check(any(entry["uri"].endswith("lib.rs2") for entry in definition),
          "a ~proc resolves to the file that declares it", json.dumps(definition))

    definition = server.request("textDocument/definition", {
        "textDocument": {"uri": main_uri},
        "position": position_of("%bankpin_code"),
    })
    targets = sorted(os.path.basename(entry["uri"]) for entry in definition)
    check("varp.alloc" in targets, "a %var resolves to its allocation", str(targets))
    check("test.varp" in targets, "a %var resolves to its record too", str(targets))

    definition = server.request("textDocument/definition", {
        "textDocument": {"uri": main_uri},
        "position": position_of("^dm_default"),
    })
    check(any(entry["uri"].endswith("test.constant") for entry in definition),
          "a ^constant resolves to the .constant that defines it", json.dumps(definition))

    definition = server.request("textDocument/definition", {
        "textDocument": {"uri": main_uri},
        "position": position_of("$count", 1),
    })
    check(definition and definition[0]["range"]["start"]["line"] == 1,
          "a $local resolves to its def_ line", json.dumps(definition))

    print("\nreferences")
    references = server.request("textDocument/references", {
        "textDocument": {"uri": main_uri},
        "position": position_of("bow_string", 0),
        "context": {"includeDeclaration": True},
    })
    files = {os.path.basename(entry["uri"]) for entry in references}
    check("lib.rs2" in files and "test.obj" in files,
          "references reach across files", str(sorted(files)))

    print("\nsemantic tokens")
    tokens = server.request("textDocument/semanticTokens/full", {
        "textDocument": {"uri": main_uri},
    })
    data = tokens.get("data", [])
    check(len(data) % 5 == 0 and len(data) > 40,
          "semantic tokens come back as whole 5-tuples", f"{len(data)} integers")
    # Every delta-line is non-negative and every token names a legend entry.
    ok = all(data[i] >= 0 and data[i + 3] < len(token_types) for i in range(0, len(data), 5))
    check(ok, "every semantic token is in range")

    print("\ndiagnostics")
    server.notify("textDocument/didChange", {
        "textDocument": {"uri": main_uri, "version": 2},
        "contentChanges": [{"text": main_text}],
    })
    # The change republishes; read until the diagnostics for this uri arrive.
    published = None
    for _ in range(10):
        message = server.read()
        if message is None:
            break
        server.notifications.append(message)
        if (message.get("method") == "textDocument/publishDiagnostics"
                and message["params"]["uri"] == main_uri):
            published = message["params"]["diagnostics"]
            break
    messages = [d["message"] for d in (published or [])]
    check(any("missing_proc" in m for m in messages),
          "an unknown ~proc is reported", str(messages))
    check(any("no_such_constant" in m for m in messages),
          "an unknown ^constant is reported", str(messages))
    check(any("undeclared" in m for m in messages),
          "an undeclared $local is reported", str(messages))
    check(not any("string_bow" in m or "bankpin_code" in m or "bronze_bar" in m
                  for m in messages),
          "the names that DO resolve are not reported", str(messages))

    print("\ncompletion")
    completion = server.request("textDocument/completion", {
        "textDocument": {"uri": main_uri},
        "position": position_of("~string_bow"),
    })
    labels = [item["label"] for item in completion.get("items", [])]
    check(any(label.startswith("~string_bow") for label in labels),
          "~ completes procs", str(labels[:8]))

    print("\ndocument symbols")
    symbols = server.request("textDocument/documentSymbol", {
        "textDocument": {"uri": main_uri},
    })
    check(any("[opheldu,bow_string]" == entry["name"] for entry in symbols),
          "the script header is a document symbol", json.dumps(symbols)[:200])

    print("\nthe clientscript dialect")
    # A decompiled `.cs2` addresses locals, variables and scripts by id, and
    # its constants come from the decompiler's vocabulary rather than this
    # tree. None of that is a missing declaration — but a proc that really is
    # missing still has to be reported, or the whole class has gone quiet.
    client_uri = uri_of("scripts", "client.cs2")
    client_text = read_fixture("scripts", "client.cs2")
    server.notify("textDocument/didOpen", {
        "textDocument": {"uri": client_uri, "languageId": "runescript",
                         "version": 1, "text": client_text},
    })
    published = None
    for _ in range(10):
        message = server.read()
        if message is None:
            break
        server.notifications.append(message)
        if (message.get("method") == "textDocument/publishDiagnostics"
                and message["params"]["uri"] == client_uri):
            published = message["params"]["diagnostics"]
            break
    messages = [d["message"] for d in (published or [])]
    check(not any("varbit542" in m or "var1356" in m or "varcint70" in m for m in messages),
          "an id-addressed %variable is not reported", str(messages))
    check(not any("int0" in m or "intarray0" in m for m in messages),
          "a self-declaring $local is not reported", str(messages))
    check(not any("script222" in m for m in messages),
          "an id-addressed ~script is not reported", str(messages))
    check(not any("clientscript" in m for m in messages),
          "a client trigger word is not reported", str(messages))
    check(any("genuinely_missing_proc" in m for m in messages),
          "a ~proc that really is missing IS reported", str(messages))

    print("\nconfig files")
    varp_uri = uri_of("configs", "test.varp")
    varp_text = read_fixture("configs", "test.varp")
    server.notify("textDocument/didOpen", {
        "textDocument": {"uri": varp_uri, "languageId": "runeconfig",
                         "version": 1, "text": varp_text},
    })
    symbols = server.request("textDocument/documentSymbol", {
        "textDocument": {"uri": varp_uri},
    })
    names = [entry["name"] for entry in symbols]
    check("bankpin_code" in names, "a config record is a document symbol", str(names))

    tokens = server.request("textDocument/semanticTokens/full", {
        "textDocument": {"uri": varp_uri},
    })
    check(len(tokens.get("data", [])) > 20, "config files get semantic tokens too",
          str(len(tokens.get("data", []))))

    server.close()

    print()
    if FAILURES:
        print(f"{len(FAILURES)} of {CHECKS} checks failed:")
        for failure in FAILURES:
            print(f"  - {failure}")
        return 1
    print(f"all {CHECKS} checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
