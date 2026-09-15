#!/usr/bin/env python3
"""Drive runescript-lsp against the real content tree.

    python3 tools/runescript-lsp/test/content_tree_test.py [content-root]

The fixture test in protocol_test.py proves the wiring; this one proves the
answers are right about content nobody wrote for the test — a real `.rs2` in
OSRS-Content, resolved against the real allocation ledgers and cache name
indexes. It is separate because it needs that tree to exist and takes a few
seconds to index it.

SKIPs rather than fails when the tree is not there, so it can run anywhere.
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from protocol_test import Server, check, FAILURES, DEFAULT_BINARY  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
DEFAULT_CONTENT = os.path.join(REPO_ROOT, "OSRS-Content", "osrs239-content")

# A real script, and the things in it whose resolution is worth asserting.
SAMPLE = os.path.join("server", "scripts", "skill_fletching", "scripts", "bows.rs2")


def main():
    content_root = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_CONTENT
    binary = DEFAULT_BINARY

    if not os.path.isdir(content_root):
        print(f"SKIP: no content tree at {content_root}")
        return 0
    if not os.path.exists(binary):
        print(f"SKIP: no binary at {binary}")
        return 0

    sample = os.path.join(content_root, SAMPLE)
    if not os.path.exists(sample):
        print(f"SKIP: no {SAMPLE} under {content_root}")
        return 0

    server = Server(binary)
    print(f"indexing {content_root} ...")
    result = server.request("initialize", {
        "processId": os.getpid(),
        "rootUri": "file://" + content_root,
        "capabilities": {},
    })
    check(bool(result.get("capabilities")), "initialize answered")
    server.notify("initialized", {})

    with open(sample, encoding="utf-8") as handle:
        text = handle.read()
    uri = "file://" + sample
    server.notify("textDocument/didOpen", {
        "textDocument": {"uri": uri, "languageId": "runescript", "version": 1, "text": text},
    })

    lines = text.split("\n")

    def position_of(needle):
        for row, line in enumerate(lines):
            column = line.find(needle)
            if column >= 0:
                return {"line": row, "character": column + 1}
        raise AssertionError(f"{needle!r} is not in {SAMPLE}")

    print("\nreal names resolve")
    hover = server.request("textDocument/hover", {
        "textDocument": {"uri": uri}, "position": position_of("bow_string"),
    })
    text_out = json.dumps(hover)
    check("obj" in text_out, "bow_string is an obj", text_out[:240])
    check("id " in text_out, "an obj carries the id its name index gives it", text_out[:240])

    hover = server.request("textDocument/hover", {
        "textDocument": {"uri": uri}, "position": position_of("unstrung_shortbow"),
    })
    check("obj" in json.dumps(hover), "unstrung_shortbow is an obj", json.dumps(hover)[:200])

    definition = server.request("textDocument/definition", {
        "textDocument": {"uri": uri}, "position": position_of("~string_bow"),
    })
    check(bool(definition), "~string_bow has a definition", json.dumps(definition)[:200])
    check(any(entry["uri"].endswith(".rs2") for entry in definition),
          "and it is in a script file", json.dumps(definition)[:200])

    definition = server.request("textDocument/definition", {
        "textDocument": {"uri": uri}, "position": position_of("^dm_default"),
    })
    check(any(entry["uri"].endswith(".constant") for entry in definition),
          "^dm_default resolves into a .constant", json.dumps(definition)[:200])

    print("\nthe file is clean")
    tokens = server.request("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}})
    data = tokens.get("data", [])
    check(len(data) > 500, "a real script produces a full token stream", f"{len(data)} integers")

    # This is a file the compiler accepts, so the only diagnostics it should
    # attract are none.
    server.notify("textDocument/didChange", {
        "textDocument": {"uri": uri, "version": 2},
        "contentChanges": [{"text": text}],
    })
    published = None
    for _ in range(20):
        message = server.read()
        if message is None:
            break
        if (message.get("method") == "textDocument/publishDiagnostics"
                and message["params"]["uri"] == uri):
            published = message["params"]["diagnostics"]
            break
    messages = [d["message"] for d in (published or [])]
    check(published is not None, "diagnostics were published for the file")
    check(not messages, "a script the compiler accepts has no diagnostics", str(messages[:6]))

    server.close()

    print()
    if FAILURES:
        print(f"{len(FAILURES)} checks failed")
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
