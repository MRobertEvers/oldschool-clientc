#!/usr/bin/env python3
"""Fail the web build when a hot wasm function is too big for Safari to optimize.

JavaScriptCore (Safari, and every browser on an iPhone or iPad) compiles a wasm
function with its optimizing tier, OMG, only when the function's body is at most
`maximumOMGCandidateCost` bytes: 100,000 by default, compared with the body's
size in the code section. A bigger function runs in the baseline tier (BBQ)
forever, where every wasm local is a stack slot. The CS2 VM's run loop was
116-121 KB after binaryen -O3 inlined the whole opcode switch into it, and ran
at 2.5-2.7x native in JavaScriptCore for that reason alone
(docs/web_wasm_function_size.md).

This reads the linked module (the one the web lane ships) and the symbol map
its link wrote (emcc --emit-symbol-map: `<function index>:<name>` lines, imports
first), then

  - FAILS when a function named in the hot list is over --limit bytes, or when
    a hot-list name marked `required` is not in the module at all (it was
    inlined into its caller: the out-of-line mechanism stopped working);
  - WARNS, without failing, about every other function over --warn bytes
    (Safari runs those in its baseline tier too; whether that matters depends
    on how often they run).

A module with a name section (PROFILE=1 builds, --profiling-funcs) needs no
symbol map.

    python3 tools/wasm_func_size_check.py --wasm build-web/torirs.wasm \
        --symbols src/build_web_opt/torirs.js.symbols --hot tools/wasm_hot_functions.txt

    make -C src PLATFORM=web OPT=1 check-wasm-func-size     (also run by every web OPT=1 link)
"""
import argparse
import sys

JSC_OMG_LIMIT = 100000


def leb(data, i):
    result = shift = 0
    while True:
        byte = data[i]
        i += 1
        result |= (byte & 0x7F) << shift
        shift += 7
        if byte < 0x80:
            return result, i


def read_module(path):
    """Returns (function import count, [body size per defined function], {function index: name})."""
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:4] != b"\0asm":
        raise SystemExit("check-wasm-func-size: %s is not a wasm module" % path)
    i = 8
    imports = 0
    sizes = []
    names = {}
    while i < len(data):
        section = data[i]
        i += 1
        size, i = leb(data, i)
        end = i + size
        if section == 2:
            count, j = leb(data, i)
            for _ in range(count):
                length, j = leb(data, j)
                j += length
                length, j = leb(data, j)
                j += length
                kind = data[j]
                j += 1
                if kind == 0:  # function
                    _, j = leb(data, j)
                    imports += 1
                elif kind == 1:  # table: reftype, limits
                    j += 1
                    flags, j = leb(data, j)
                    _, j = leb(data, j)
                    if flags & 1:
                        _, j = leb(data, j)
                elif kind == 2:  # memory: limits
                    flags, j = leb(data, j)
                    _, j = leb(data, j)
                    if flags & 1:
                        _, j = leb(data, j)
                elif kind == 3:  # global: valtype, mutability
                    j += 2
                elif kind == 4:  # tag
                    j += 1
                    _, j = leb(data, j)
                else:
                    raise SystemExit("check-wasm-func-size: %s: unknown import kind %d" % (path, kind))
        elif section == 10:
            count, j = leb(data, i)
            for _ in range(count):
                length, j = leb(data, j)
                sizes.append(length)
                j += length
        elif section == 0:
            length, j = leb(data, i)
            title = data[j:j + length]
            j += length
            if title == b"name":
                while j < end:
                    sub = data[j]
                    j += 1
                    sub_size, j = leb(data, j)
                    sub_end = j + sub_size
                    if sub == 1:
                        count, k = leb(data, j)
                        for _ in range(count):
                            index, k = leb(data, k)
                            length, k = leb(data, k)
                            names[index] = data[k:k + length].decode("utf-8", "replace")
                            k += length
                    j = sub_end
        i = end
    return imports, sizes, names


def read_symbols(path):
    names = {}
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            index, sep, name = line.rstrip("\n").partition(":")
            if sep and index.isdigit():
                names[int(index)] = name
    return names


def read_hot_list(path):
    """`name [required]` per line; `#` starts a comment."""
    hot = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            words = line.split("#", 1)[0].split()
            if not words:
                continue
            if len(words) > 2 or (len(words) == 2 and words[1] != "required"):
                raise SystemExit("check-wasm-func-size: %s: bad line %r" % (path, line.rstrip()))
            hot.append((words[0], len(words) == 2))
    return hot


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--wasm", required=True)
    parser.add_argument("--symbols", help="emcc --emit-symbol-map output; not needed with a name section")
    parser.add_argument("--hot", required=True, help="hot function list")
    parser.add_argument("--limit", type=int, default=90000,
                        help="largest body allowed for a hot function (default 90000)")
    parser.add_argument("--warn", type=int, default=JSC_OMG_LIMIT,
                        help="report any other function over this (default %d)" % JSC_OMG_LIMIT)
    args = parser.parse_args()
    if args.limit >= JSC_OMG_LIMIT:
        raise SystemExit("check-wasm-func-size: --limit %d leaves no margin under JavaScriptCore's %d"
                         % (args.limit, JSC_OMG_LIMIT))

    imports, sizes, names = read_module(args.wasm)
    source = "name section"
    if not names:
        if not args.symbols:
            raise SystemExit("check-wasm-func-size: %s has no name section; pass --symbols (link with "
                             "--emit-symbol-map)" % args.wasm)
        names = read_symbols(args.symbols)
        source = args.symbols
        if not names:
            raise SystemExit("check-wasm-func-size: %s names no functions" % args.symbols)
        if max(names) != imports + len(sizes) - 1:
            raise SystemExit("check-wasm-func-size: %s does not belong to %s (%d functions named, module "
                             "has %d): relink" % (args.symbols, args.wasm, max(names) + 1, imports + len(sizes)))

    by_name = {}
    for k, size in enumerate(sizes):
        by_name.setdefault(names.get(imports + k, "func%d" % (imports + k)), []).append(size)

    hot = read_hot_list(args.hot)
    hot_names = {name for name, _ in hot}
    failures = []
    print("check-wasm-func-size: %s, %d functions, names from %s; hot limit %d bytes (JavaScriptCore "
          "optimizes up to %d)" % (args.wasm, len(sizes), source, args.limit, JSC_OMG_LIMIT))
    for name, required in hot:
        found = by_name.get(name)
        if not found:
            if required:
                failures.append("%s is not in the module: it was inlined into its caller, so the function "
                                "that called it now carries its body. Find out what stopped keeping it out "
                                "of line (docs/web_wasm_function_size.md)." % name)
                print("  %-36s MISSING (required)" % name)
            else:
                print("  %-36s inlined" % name)
            continue
        for size in found:
            print("  %-36s %7d bytes  %6d to the limit" % (name, size, args.limit - size))
            if size > args.limit:
                failures.append("%s is %d bytes, over the %d-byte limit: past %d JavaScriptCore never "
                                "optimizes it and Safari runs it at about half speed. Move work out of it "
                                "(for the CS2 VM: widen CS2VM2_RUNOP_OUTLINED; docs/web_wasm_function_size.md)."
                                % (name, size, args.limit, JSC_OMG_LIMIT))

    big = sorted(((size, name) for name, found in by_name.items() if name not in hot_names
                  for size in found if size > args.warn), reverse=True)
    for size, name in big:
        print("check-wasm-func-size: warning: %s is %d bytes: Safari runs it in its baseline tier" % (name, size))

    if failures:
        for failure in failures:
            print("check-wasm-func-size: FAIL: " + failure)
        return 1
    print("check-wasm-func-size: PASS (%d hot functions within %d bytes; %d other functions over %d, "
          "warned above)" % (len(hot), args.limit, len(big), args.warn))
    return 0


if __name__ == "__main__":
    sys.exit(main())
