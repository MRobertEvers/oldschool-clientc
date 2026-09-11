#!/usr/bin/env python3
"""Generate the server-command signature/description table for runescript-lsp.

    python3 tools/runescript-lsp/gen_command_docs.py [path-to-LostCity_Server]

Output (checked in; this never runs at build time):

    src/command_docs.gen.h

Sources:

    content/scripts/engine.rs2          inside the reference server
    tools/cs2_gen_opcodes/opcode_docs.py    this repo's own client-op docs

engine.rs2 is the authoritative signature file — every command the language can
call is declared there as `[command,name](args)(returns)` — and each
declaration carries a one-line `// info:` description above it. That is where a
hover's text has to come from: the arity table this repo already generates
(src/serverscript/gen_opcode_meta.py, same file, same reason) knows how many
values a command pops, but not what they are called or what the command does,
and "pops 3 int" is not an answer to "what does inv_del take?".

Two shapes need care:

  - The `.`-prefixed declarations (`[command,.huntnext]`) are the
    secondary-pointer forms. The dot lives in the operand rather than the
    opcode, so both spellings mean one command; the dot form is recorded as a
    note on the primary rather than as a second entry.
  - 23 of the 521 commands carry no `// info:` line. They get a NULL
    description, and the LSP falls back to what the arity table knows.

The client half is thinner on purpose. A `.cs2` command's *signature* already
comes from the cs2 library's typed prototype table, which the server links; all
that is missing is a sentence saying what the command does, and this repo
already keeps 276 of those in tools/cs2_gen_opcodes/opcode_docs.py. Commands
outside that set keep their prototype signature and no description, which is
what they had before.

Modelled on src/serverscript/gen_opcode_meta.py, including its convention that
the generated header says at the top what regenerates it.
"""

import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_REF = HERE.parent.parent.parent / "LostCity_Server"

COMMAND_RE = re.compile(r"^\[command,(\.?)([A-Za-z_][A-Za-z0-9_]*)(\*?)\](.*)$")
INFO_RE = re.compile(r"^//\s*info:\s*(.*)$")


class Command:
    def __init__(self, name):
        self.name = name
        self.signature = ""
        self.description = None
        self.has_dot_form = False


def parse_engine(path):
    """Read engine.rs2 into {name: Command}, in declaration order."""
    commands = {}
    order = []
    pending_info = None

    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()

        info = INFO_RE.match(line)
        if info:
            pending_info = info.group(1).strip()
            continue

        match = COMMAND_RE.match(line)
        if not match:
            # Any other line — a section banner, a blank, a commented-out
            # declaration — breaks the association with a pending info line.
            if line and not line.startswith("//"):
                pending_info = None
            continue

        dot, name, star, rest = match.groups()
        if star:
            # `[command,queue*]` declares the vararg form, whose opcode is
            # `<name>vararg`; that is how the compiler resolves it.
            name = name + "vararg"

        # The signature is everything after the header: `(args)(returns)`,
        # minus any trailing comment.
        signature = rest.split("//")[0].strip()

        if name not in commands:
            commands[name] = Command(name)
            order.append(name)
        command = commands[name]

        if dot:
            command.has_dot_form = True
            # A dot declaration is the same command; only take its signature
            # and description when the primary has not supplied one.
            if not command.signature:
                command.signature = signature
            if not command.description and pending_info:
                command.description = pending_info
        else:
            command.signature = signature
            if pending_info:
                command.description = pending_info

        pending_info = None

    return [commands[name] for name in order]


def load_client_docs():
    """(lower-case name, summary) for every documented client opcode.

    The table is keyed by the opcode's upper-case VM name; the source spelling
    the decompiler prints is the same word in lower case, which is how the
    server looks it up.
    """
    sys.path.insert(0, str(HERE.parent / "cs2_gen_opcodes"))
    try:
        import opcode_docs
    except ImportError as error:
        print(f"skipping client descriptions: {error}", file=sys.stderr)
        return []

    entries = []
    for name, doc in sorted(opcode_docs.OPCODE_DOCS.items()):
        summary = (doc.summary or "").strip()
        if not summary:
            continue
        if doc.notes:
            summary = f"{summary}\n\n{doc.notes.strip()}"
        entries.append((name.lower(), summary))
    return entries


def c_string(text):
    """A C string literal.

    Newlines are escaped rather than embedded: an opcode_docs `notes` block runs
    to several lines, and a raw newline ends the literal mid-word.
    """
    if text is None:
        return "NULL"
    escaped = (
        text.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\r", "")
        .replace("\t", "\\t")
    )
    return f'"{escaped}"'


def main():
    reference = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_REF
    engine = reference / "content" / "scripts" / "engine.rs2"

    if not engine.exists():
        print(f"no engine.rs2 at {engine}", file=sys.stderr)
        print("pass the path to LostCity_Server as the first argument", file=sys.stderr)
        return 1

    commands = parse_engine(engine)
    described = sum(1 for command in commands if command.description)

    out = []
    out.append("/*")
    out.append(" * Generated by tools/runescript-lsp/gen_command_docs.py from the LostCity")
    out.append(" * reference server's content/scripts/engine.rs2. Do not edit by hand —")
    out.append(" * re-run the generator.")
    out.append(" *")
    out.append(f" * {len(commands)} commands, {described} of them with a description.")
    out.append(" */")
    out.append("")
    out.append("/* Included by exactly one translation unit: index.c. */")
    out.append("")
    out.append("struct RS_CommandDoc")
    out.append("{")
    out.append("    const char* name;")
    out.append("    /** `(args)(returns)`, exactly as engine.rs2 declares it. */")
    out.append("    const char* signature;")
    out.append("    /** The `// info:` line above the declaration, or NULL. */")
    out.append("    const char* description;")
    out.append("    /** A `.name` secondary-pointer form is declared as well. */")
    out.append("    unsigned char has_dot_form;")
    out.append("};")
    out.append("")
    out.append("static const struct RS_CommandDoc g_rs_command_docs[] = {")
    for command in commands:
        out.append(
            "    { %s, %s, %s, %d },"
            % (
                c_string(command.name),
                c_string(command.signature),
                c_string(command.description),
                1 if command.has_dot_form else 0,
            )
        )
    out.append("};")
    out.append("")
    out.append(
        "static const int g_rs_command_doc_count ="
    )
    out.append(
        "    (int)(sizeof(g_rs_command_docs) / sizeof(g_rs_command_docs[0]));"
    )
    out.append("")

    client = load_client_docs()
    out.append("/* Client opcodes, by the name the decompiler prints. Summaries only:")
    out.append(" * the signature comes from the cs2 library's own prototype table. */")
    out.append("struct RS_ClientCommandDoc")
    out.append("{")
    out.append("    const char* name;")
    out.append("    const char* description;")
    out.append("};")
    out.append("")
    out.append("static const struct RS_ClientCommandDoc g_rs_client_command_docs[] = {")
    for name, summary in client:
        out.append("    { %s, %s }," % (c_string(name), c_string(summary)))
    out.append("};")
    out.append("")
    out.append("static const int g_rs_client_command_doc_count =")
    out.append("    (int)(sizeof(g_rs_client_command_docs) / sizeof(g_rs_client_command_docs[0]));")
    out.append("")

    target = HERE / "src" / "command_docs.gen.h"
    target.write_text("\n".join(out), encoding="utf-8")
    print(f"{target}: {len(commands)} server commands ({described} described), "
          f"{len(client)} client descriptions")
    return 0


if __name__ == "__main__":
    sys.exit(main())
