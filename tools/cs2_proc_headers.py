#!/usr/bin/env python3
"""Audit proc declarations against GOSUB edges decoded from the native cache.

Feed `cs2 callgraph --cache CACHE --rev osrs239` output. Only a uniquely named
source whose actual ID is a native call target is eligible. Self-named wrappers
are excluded unless the native bytecode really recurses. This never enables
the compiler's removed clientscript-to-proc guessing fallback.
"""
import argparse
import collections
import csv
import hashlib
import json
from pathlib import Path
import re


def audit(tree, graph, write=False):
    edges = set()
    scripts = set()
    for line in graph.read_text().splitlines():
        row = line.split("\t")
        if row[0] == "call":
            edges.add((int(row[1]), int(row[3])))
        elif row[0] == "script":
            scripts.add(int(row[1]))
    names = collections.defaultdict(list)
    uses = collections.defaultdict(set)
    for line in (tree / "pack/12_clientscripts.pack").read_text().splitlines():
        if not re.match(r"^\d+=", line):
            continue
        number, stem = line.split("=", 1)
        number = int(number)
        # lc_pack's trailing hashname/hashcode columns describe archive
        # identity; they are not part of the source filename.
        stem = stem.split()[0]
        path = tree / "scripts" / (stem + ".cs2")
        if not path.is_file():
            continue
        source = path.read_text()
        header = re.search(r"^\[(\w+),([^]]+)\]", source, re.M)
        if not header:
            continue
        banner = re.match(r"//\s*(\d+)\b", source)
        if banner and int(banner[1]) != number:
            raise ValueError(f"{path}: banner and pack ID disagree")
        names[header[2]].append((number, path, header[1], source))
        code = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', "", source, flags=re.S)
        for name in re.findall(r"~([A-Za-z_][A-Za-z_0-9]*)", code):
            uses[name].add(number)
    verified, unresolved = [], []
    for name, declarations in sorted(names.items()):
        if name not in uses or all(d[2] != "clientscript" for d in declarations):
            continue
        if re.fullmatch(r"script\d+", name) or any(d[2] == "proc" for d in declarations):
            continue  # explicit IDs and an existing proc do not use the forbidden fallback
        if len(declarations) != 1:
            unresolved.append({"name": name, "reason": "multiple declarations; retain trigger distinction"})
            continue
        number, path, trigger, source = declarations[0]
        if trigger != "clientscript":
            continue
        evidence = sorted(caller for caller in uses[name] if (caller, number) in edges)
        self_wrapper = number in uses[name] and (number, number) not in edges
        row = {"name": name, "id": number, "path": str(path.relative_to(tree)),
               "native_callers": evidence, "source_sha256": hashlib.sha256(source.encode()).hexdigest()}
        if number not in scripts or not evidence or self_wrapper:
            row["reason"] = "self-named wrapper calls another ID" if self_wrapper else "no native call-edge evidence"
            unresolved.append(row)
            continue
        verified.append(row)
        if write:
            before = f"[clientscript,{name}]"
            after = f"[proc,{name}]"
            if source.count(before) != 1:
                raise ValueError(f"{path}: ambiguous header occurrence")
            path.write_text(source.replace(before, after, 1))
    return {"graph_sha256": hashlib.sha256(graph.read_bytes()).hexdigest(),
            "verified_headers": verified, "unresolved": unresolved, "written": write}


def native_hooks(directory):
    hooks = set()
    if directory:
        for path in directory.glob("*.json"):
            document = json.loads(path.read_text())
            def visit(value):
                if isinstance(value, dict):
                    if value.get("kind") == "clientscript" and isinstance(value.get("scriptId"), int):
                        hooks.add((document["id"], value["scriptId"]))
                    for child in value.values():
                        visit(child)
                elif isinstance(value, list):
                    for child in value:
                        visit(child)
            visit(document)
    return hooks


def repair_references(tree, graph, write=False, hook_asts=None):
    """Finish recorded source renames without changing native target IDs.

    Existing current names win. Historical aliases are used only when the
    original caller has the matching GOSUB or decoded hook in native bytecode.
    The output uses the current header name or the decompiler's script<ID>
    form when the target serves a different trigger.
    """
    aliases = collections.defaultdict(set)
    current = collections.defaultdict(set)
    definitions, sources = {}, {}
    for line in (tree / "pack/12_clientscripts.pack").read_text().splitlines():
        match = re.match(r"(\d+)=(\S+)", line)
        if not match:
            continue
        ident, stem = int(match[1]), match[2]
        aliases[stem].add(ident)
        path = tree / "scripts" / f"{stem}.cs2"
        if not path.is_file():
            continue
        source = path.read_text()
        header = re.search(r"^\[(\w+),([^]]+)\]", source, re.M)
        if header:
            definitions[ident] = header[1], header[2]
            aliases[header[2]].add(ident)
            current[(header[1],header[2])].add(ident)
            sources[ident] = path, source
    history = tree.parent / "docs/CS2_SCRIPT_NAME_AUDIT.tsv"
    if history.exists():
        with history.open() as stream:
            for row in csv.DictReader(stream, delimiter="\t"):
                for key in ("old_name", "historical_name", "header_subject", "new_name"):
                    if row.get(key):
                        aliases[row[key]].add(int(row["id"]))
    recovered = tree.parent / "new_script_names.txt"
    if recovered.exists():
        for line in recovered.read_text().splitlines():
            match = re.match(r"(\d+)\s+\[(\w+),([^]]+)\]", line)
            if match:
                aliases[match[3]].add(int(match[1]))
    evidence = {"proc": set(), "clientscript": native_hooks(hook_asts)}
    for line in graph.read_text().splitlines():
        row = line.split("\t")
        if row[0] == "call":
            evidence["proc"].add((int(row[1]), int(row[3])))
    repaired, unresolved = [], []
    tokens = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|~([A-Za-z_][A-Za-z_0-9]*)', re.S)
    for ident,(path,source) in sources.items():
        replacements = []
        for token in tokens.finditer(source):
            if token[0].startswith("~"):
                kind, name = "proc", token[1]
                begin, end = token.start()+1, token.end()
            elif token[0].startswith('"') and re.search(r'\b(?:if|cc)(?:_input)?_seton\w+\s*\(\s*$', source[max(0,token.start()-200):token.start()]):
                match = re.match(r'"([A-Za-z_][A-Za-z_0-9]*)(?=\(|"|\{)', token[0])
                if not match:
                    continue
                kind, name = "clientscript", match[1]
                begin, end = token.start()+1, token.start()+1+len(name)
            else:
                continue
            if re.fullmatch(r"script\d+", name) or len(current[(kind,name)]) == 1:
                continue
            other = set().union(*(ids for (trigger,key),ids in current.items()
                                   if key == name and (kind != "proc" or trigger != "clientscript")))
            if len(other) == 1:
                continue  # the compiler already has one permitted trigger fallback
            candidates = sorted(target for target in aliases[name] if (ident,target) in evidence[kind])
            row = {"caller": ident, "kind": kind, "name": name,
                   "path": str(path.relative_to(tree)), "line": source.count("\n",0,begin)+1}
            if len(candidates) != 1:
                row["candidates"] = candidates
                unresolved.append(row)
                continue
            target = candidates[0]
            trigger, new_name = definitions.get(target, ("", ""))
            if trigger != kind or current[(kind,new_name)] != {target}:
                new_name = f"script{target}"
            if new_name == name:
                continue
            row.update(target=target, replacement=new_name)
            repaired.append(row)
            replacements.append((begin,end,new_name))
        if write and replacements:
            for begin,end,value in reversed(replacements):
                source = source[:begin]+value+source[end:]
            path.write_text(source)
    return {"graph_sha256": hashlib.sha256(graph.read_bytes()).hexdigest(),
            "repaired_references": repaired, "unresolved": unresolved, "written": write}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, required=True)
    parser.add_argument("--callgraph", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--repair-references", action="store_true")
    parser.add_argument("--hook-asts", type=Path, help="native cs2 decompile --emit ast-json output")
    args = parser.parse_args()
    result = (repair_references(args.tree, args.callgraph, args.write, args.hook_asts)
              if args.repair_references else audit(args.tree, args.callgraph, args.write))
    args.report.write_text(json.dumps(result, indent=2) + "\n")
    count = len(result.get("verified_headers", result.get("repaired_references", [])))
    print(f"CS2 source audit: {count} verified changes, {len(result['unresolved'])} unresolved; write={args.write}")


if __name__ == "__main__":
    main()
