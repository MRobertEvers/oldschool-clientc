#!/usr/bin/env python3
"""Generate the M0 discovery register; unresolved entries deliberately block closure.

Clang supplies the complete tree schema (including anonymous union arms and
owned records). Repository discovery includes disabled scripts and archived
executables. Candidate accesses are lexical, NOT proof of type or ownership:
their disposition must be reviewed before M0 can be declared closed.
"""
import argparse
import collections
import json
from pathlib import Path
import re
import subprocess
import shlex
import sys
import ctypes

ROOT = Path(__file__).resolve().parents[1]


def clang_api():
    from clang import cindex as cx
    resource = Path(subprocess.check_output(["clang", "-print-resource-dir"], text=True).strip())
    # Use the same compiler's library and builtin headers. Mixing pip's
    # libclang with Apple's NEON headers left three actual frontend TUs out.
    library = resource.parents[1] / "libclang.dylib"
    if sys.platform == "darwin" and library.exists() and not cx.Config.loaded:
        cx.Config.set_library_file(str(library))
        cx.Config.set_compatibility_check(False)
    for kind in ("Binary", "Unary"):
        getter = getattr(cx.conf.lib, f"clang_getCursor{kind}OperatorKind")
        getter.argtypes = [cx.Cursor]
        getter.restype = ctypes.c_int
        spelling = getattr(cx.conf.lib, f"clang_get{kind}OperatorKindSpelling")
        spelling.argtypes = [ctypes.c_int]
        spelling.restype = cx._CXString
        spelling.errcheck = cx._CXString.from_result
    return cx


def field_access_kind(node, ancestors, cx):
    """Follow the lvalue, not just source offsets around an assignment.

    In `array[node->index] = value`, index is READ. In `node->values[i] =
    value`, the owned array is written. Operators come from Clang, including
    macro expansions whose token extents need not describe their lvalues.
    """
    current = node
    indirect = False
    transparent = (cx.CursorKind.UNEXPOSED_EXPR, cx.CursorKind.PAREN_EXPR)
    for parent in reversed(ancestors):
        if parent.kind in transparent:
            current = parent
            continue
        children = list(parent.get_children())
        if parent.kind == cx.CursorKind.MEMBER_REF_EXPR:
            return "read"  # another field, not the aggregate containing it, is the lvalue
        if parent.kind == cx.CursorKind.ARRAY_SUBSCRIPT_EXPR:
            if not children or children[0].hash != current.hash:
                return "read"
            indirect |= node.type.kind == cx.TypeKind.POINTER
            current = parent
            continue
        if parent.kind == cx.CursorKind.UNARY_OPERATOR:
            kind = cx.conf.lib.clang_getCursorUnaryOperatorKind(parent)
            op = cx.conf.lib.clang_getUnaryOperatorKindSpelling(kind)
            if op == "&":
                return "address_escape"
            if op in ("++", "--"):
                return "pointee_write" if indirect else "write"
            if op == "*":
                indirect = True
                current = parent
                continue
            return "read"
        if parent.kind in (cx.CursorKind.BINARY_OPERATOR, cx.CursorKind.COMPOUND_ASSIGNMENT_OPERATOR):
            if not children or children[0].hash != current.hash:
                return "read"
            kind = cx.conf.lib.clang_getCursorBinaryOperatorKind(parent)
            op = cx.conf.lib.clang_getBinaryOperatorKindSpelling(kind)
            if op in ("=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="):
                return "pointee_write" if indirect else "write"
            return "read"
        if parent.kind == cx.CursorKind.CALL_EXPR and parent.referenced:
            arguments = list(parent.get_arguments())
            parameters = list(parent.referenced.get_arguments())
            for index, argument in enumerate(arguments):
                if argument.hash != current.hash or index >= len(parameters):
                    continue
                parameter = parameters[index].type
                if parameter.kind == cx.TypeKind.POINTER and not parameter.get_pointee().is_const_qualified():
                    return "address_escape"
        return "read"
    return "read"


def field_storage_root(node, cx):
    """Distinguish local value construction from writes through shared pointers."""
    current = node
    while True:
        if current.kind == cx.CursorKind.DECL_REF_EXPR and current.referenced:
            declaration = current.referenced
            value_type = declaration.type
            pointer = value_type.kind in (cx.TypeKind.POINTER, cx.TypeKind.LVALUEREFERENCE)
            local = declaration.semantic_parent.kind == cx.CursorKind.FUNCTION_DECL
            return {"root": declaration.spelling,
                    "kind": "indirect" if pointer else "local-value" if local else "static-value",
                    "type": value_type.spelling}
        children = list(current.get_children())
        if not children or current.kind == cx.CursorKind.CALL_EXPR:
            return {"root": current.spelling, "kind": "computed", "type": current.type.spelling}
        current = children[0]


def tracked():
    return subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT).decode().split("\0")[:-1]


def schema():
    result = subprocess.run(["clang", "-std=c11", "-Isrc", "-I.", "-I3rd/rscache/include",
        "-I3rd/rscache/src", "-Xclang", "-ast-dump=json", "-fsyntax-only", "-x", "c",
        "src/ui/uitree.h"], cwd=ROOT, capture_output=True, text=True, check=True)
    ast = json.loads(result.stdout)
    records = {}
    def collect(node):
        if node.get("kind") == "RecordDecl" and node.get("completeDefinition"):
            if node.get("name", "").startswith("UITree"):
                records[node["name"]] = node
        for child in node.get("inner", []):
            collect(child)
    collect(ast)
    fields = []
    def flatten(node, prefix, parents=()):
        anonymous = collections.deque()
        for child in node.get("inner", []):
            if child["kind"] == "RecordDecl" and not child.get("name"):
                anonymous.append(child)
            if child["kind"] != "FieldDecl":
                continue
            name = child.get("name", "<anonymous>")
            qualified = prefix + "." + name
            typename = child.get("type", {}).get("qualType", "")
            loc = child.get("loc", {})
            fields.append({"field": qualified, "type": typename, "line": loc.get("line"),
                "disposition": "REVIEW_REQUIRED", "writers": [], "readers": []})
            target = re.search(r"struct (UITree\w+)", typename)
            if target and target[1] in records and target[1] not in parents:
                flatten(records[target[1]], qualified, (*parents, target[1]))
            elif "unnamed" in typename and anonymous:
                flatten(anonymous.popleft(), qualified, parents)
    for name in ("UITreeComponent", "UITree"):
        flatten(records[name], name, (name,))
    return fields


def strip_c(text):
    # Preserve offsets and line numbers. Strings/comments are not code writers.
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
        lambda m: re.sub(r"[^\n]", " ", m[0]), text, flags=re.S)


def discover():
    fields = schema()
    by_leaf = collections.defaultdict(list)
    for field in fields:
        by_leaf[field["field"].rsplit(".", 1)[-1]].append(field)
    plugins, consumers, operations, opcodes, packets, mutations = [], [], [], [], [], []
    access = re.compile(r"(?:->|\.)\s*(\w+)\s*(?:\[[^\]\n]*\]\s*)*(?P<write>\+\+|--|<<=|>>=|[+*/%&|^-]=|=(?!=))?")
    public = re.compile(r"ToriRS_.*(?:Api|Plugin|Builder)|PluginHost_|torirs_plugin_|torirs\.plugin|api_version|\[plugin:")
    for path in tracked():
        file = ROOT / path
        if file.suffix.lower() not in (".c", ".h", ".inc", ".lua", ".ini", ".py", ".js", ".md", ".html", ".sh", ".mk", ".m", ".mm", ".cc", ".cpp", ".hpp", ".def", ".json", ".ts", ".tsx") and file.name.lower() not in ("makefile", "cmakelists.txt"):
            continue
        if not file.is_file():
            continue
        source = file.read_text(errors="replace")
        if public.search(source) or path.startswith(("src/plugin/", "src/plugin_chrome/", "old/plugin_chrome_native/", "script/plugins/")):
            kind = "documentation" if file.suffix == ".md" else "consumer"
            if path.startswith("old/"):
                kind = "archived-execution-surface"
            disposition = ("PRESERVE_USER_DATA" if path.startswith("plugin_prefs") else
                           "RETIRE_EXECUTABLE_ARCHIVE" if path.startswith("old/") else
                           "UPDATE_DOCUMENTATION" if file.suffix == ".md" else
                           "UPDATE_METADATA" if file.name == "plugin_api.meta.lua" else
                           "PORT_TEST" if "/test/" in path or file.name.startswith("test_") else
                           "PORT_MANIFEST" if file.suffix == ".ini" else
                           "PORT_HOST" if path.startswith("src/") and not path.startswith("src/plugin/plugins/") else
                           "PORT_PLUGIN" if path.startswith(("src/plugin/plugins/", "script/plugins/")) else
                           "UPDATE_TOOLING")
            consumers.append({"path": path, "kind": kind, "disposition": disposition,
                "persistence": "preserve" if path.startswith("plugin_prefs") else "review"})
        if file.suffix == ".c":
            for match in re.finditer(r'struct\s+ToriRS_PluginDef\w*\s+const\s+(\w+)\s*=\s*\{(.*?)\n\};', source, re.S):
                ident = re.search(r'\.id\s*=\s*"([^"]+)"', match[2])
                plugins.append({"path": path, "symbol": match[1], "id": ident[1] if ident else None,
                    "kind": "C", "disposition": "PORT", "verified_revisions": []})
        if path.startswith("script/plugins/") and file.suffix == ".lua" and file.name != "plugin_api.meta.lua":
            plugins.append({"path": path, "kind": "Lua", "disposition": "PORT", "verified_revisions": []})
        if not path.startswith("src/") or file.suffix not in (".c", ".h", ".inc"):
            continue
        source_lines = source.splitlines()
        clean = strip_c(source)
        for lineno, line in enumerate(clean.splitlines(), 1):
            for match in access.finditer(line):
                if match[1] not in by_leaf:
                    continue
                write = bool(match["write"]) or bool(re.search(r"(?:\+\+|--)[^;]*$", line[:match.start()]))
                entry = f"{path}:{lineno}"
                for field in by_leaf[match[1]]:
                    field["writers" if write else "readers"].append(entry)
                if write:
                    mutations.append({"path": path, "line": lineno, "field_leaf": match[1],
                        "expression": source_lines[lineno-1].strip(),
                        "disposition": "TYPE_AND_PHASE_REVIEW_REQUIRED"})
            for name in re.findall(r"\b(UITree_(?:Set|Apply|Cc|Clear|Reparent|Push|Mount|Close|Mark|Notify|Input)\w*)\s*\(", line):
                operations.append({"name": name, "path": path, "line": lineno})
        if path == "src/cs2vm2/cs2vm2_host_request_kinds.def":
            pass
        if path == "src/game/rs_gameproto_exec.c":
            packets = sorted(set(re.findall(r"case (PKT_NAME_\w+):", clean)))
    # All widget opcodes, not just SET-prefixed ones: create/copy/delete,
    # getters, listeners, input, find, op and target operations all matter.
    hosted = []
    for line in (ROOT / "src/cs2vm2/cs2vm2_host_request_kinds.def").read_text().splitlines():
        match = re.match(r"CS2VM_HOST_REQUEST_KIND\((\w+),\s*(\d+)", line)
        if match:
            name = match[1]
            ui = name.startswith(("CC_", "IF_", "OVERLAY_")) or name == "SETANTIDRAG"
            row = {"name": name, "opcode": int(match[2]),
                   "disposition": "M2_NATIVE_UI_OPERATION" if ui else "M3_NATIVE_STATE_OR_SERVICE"}
            hosted.append(row)
            if ui:
                opcodes.append(row)
    all_opcodes = [{"name": name, "opcode": int(value), "disposition": "HOSTED" if name in {r['name'] for r in hosted} else "VM_EXECUTION"}
                   for name,value in re.findall(r"^#define CS2_OP_(\w+)[ \t]+(\d+)\b", (ROOT / "src/cs2vm2/cs2_opcode.h").read_text(), re.M)]
    for field in fields:
        for key in ("readers", "writers"):
            field[key] = sorted(set(field[key]))
    return {"schema_version": 1, "closure": "BLOCKED_UNREVIEWED",
        "method": "Clang schema; lexical candidate references include same-name fields of other types. Bulk memory writes and aliasing require mechanism review.",
        "fields": fields, "mutation_candidates": mutations, "operations": operations,
        "widget_opcodes": opcodes, "hosted_opcodes": hosted, "all_cs2_opcodes": all_opcodes,
        "packet_dispatch_cases": packets,
        "plugins": plugins, "consumers": consumers}


def typed_accesses(compile_log):
    """Resolve field identities in every TU from an actual successful build.

    Requires the libclang Python package. Failed parses are blocking entries;
    frontend/compiler branches absent from the build remain explicit gaps.
    """
    cx = clang_api()
    index = cx.Index.create()
    resource_dir = subprocess.check_output(["clang", "-print-resource-dir"], text=True).strip()
    sdk = subprocess.check_output(["xcrun", "--show-sdk-path"], text=True).strip() if __import__("sys").platform == "darwin" else None
    commands = {}
    for line in compile_log.read_text().splitlines():
        if " -c " not in line or not line.startswith(("cc ", "clang ", "gcc ")):
            continue
        args = shlex.split(line)
        source = args[args.index("-c")+1]
        path = (ROOT / "src" / source).resolve()
        if not path.is_relative_to(ROOT / "src") or not path.suffix == ".c":
            continue
        flags = []
        it = iter(args[1:])
        for arg in it:
            if arg in ("-c", "-o"):
                next(it)
            elif arg not in ("-MMD", "-MP"):
                flags.append(arg)
        if sdk:
            flags += ["-isysroot", sdk]
        flags += ["-resource-dir", resource_dir]
        commands[str(path)] = flags
    accesses, errors, source_lines = {}, [], {}
    for path, flags in commands.items():
        print(f"AST {Path(path).relative_to(ROOT)}", flush=True)
        tu = index.parse(path, args=flags)
        bad = [str(d) for d in tu.diagnostics if d.severity >= cx.Diagnostic.Error]
        if bad:
            errors.append({"path": path, "errors": bad})
            continue
        def walk(node, ancestors=(), function=""):
            if node.kind == cx.CursorKind.FUNCTION_DECL:
                function = node.spelling
            if node.kind == cx.CursorKind.MEMBER_REF_EXPR and node.referenced:
                ref = node.referenced
                location = str(ref.location.file or "")
                if "/ui/uitree" in location and ref.kind == cx.CursorKind.FIELD_DECL:
                    source_path = str(node.location.file)
                    if source_path.startswith(str(ROOT)):
                        if source_path not in source_lines:
                            source_lines[source_path] = Path(source_path).read_text().splitlines()
                        usage = field_access_kind(node, ancestors, cx)
                        item = {"path": str(Path(source_path).relative_to(ROOT)), "line": node.location.line,
                            "column": node.location.column, "field": ref.get_usr(), "field_name": ref.spelling,
                            "declaration": f"{Path(location).relative_to(ROOT)}:{ref.location.line}",
                            "record": ref.semantic_parent.spelling,
                            "storage": field_storage_root(node, cx),
                            "function": function, "use": usage,
                            "expression": source_lines[source_path][node.location.line-1].strip(),
                            "disposition": "REVIEW_REQUIRED"}
                        accesses[(source_path,node.location.line,node.location.column,usage)] = item
            for child in node.get_children():
                loc = str(child.location.file or "")
                if loc.startswith(str(ROOT / "src")):
                    walk(child, (*ancestors,node), function)
        walk(tu.cursor)
    return {"translation_units": list(commands), "parse_blockers": errors,
        "coverage_gaps": ["preprocessor branches not selected by supplied build", "indirect bulk writes require review"],
        "accesses": list(accesses.values())}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--compile-log", type=Path, help="resolve typed field accesses using libclang")
    args = parser.parse_args()
    data = discover()
    if args.compile_log:
        data["typed_accesses"] = typed_accesses(args.compile_log)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(data, indent=2) + "\n")
    print(json.dumps({key: len(data[key]) for key in ("fields", "mutation_candidates", "operations", "widget_opcodes", "plugins", "consumers")}))
    print("M0 closure: BLOCKED_UNREVIEWED (discovery is not semantic verification)")


if __name__ == "__main__":
    main()
