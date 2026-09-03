#!/usr/bin/env python3
"""Keep the runtime Lua tables and LuaLS declarations exactly in step."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
C_SOURCE = ROOT / "src/plugin/torirs_plugin_lua.c"
META_SOURCE = ROOT / "script/plugins/plugin_api.meta.lua"


def c_arrays(source: str) -> dict[str, list[list[str]]]:
    arrays: dict[str, list[list[str]]] = {}
    pattern = re.compile(
        r"\}\s+([A-Z][A-Z0-9_]*)\[\]\s*=\s*\{(.*?)\n\s*\};",
        re.S,
    )
    for name, body in pattern.findall(source):
        entries = re.findall(r'\{\s*"([^"]+)"\s*,', body)
        arrays.setdefault(name, []).append(entries)
    return arrays


def meta_classes(source: str) -> tuple[dict[str, dict[str, str]], set[str]]:
    classes: dict[str, dict[str, str]] = {}
    callable_aliases: set[str] = set()
    current: str | None = None
    for line in source.splitlines():
        alias = re.match(r"---@alias\s+(torirs\.[A-Za-z0-9_]+)\s+(.+)", line)
        if alias and re.search(r"\bfun\s*\(", alias.group(2)):
            callable_aliases.add(alias.group(1))
        declared = re.match(r"---@class\s+(torirs\.[A-Za-z0-9_]+)", line)
        if declared:
            current = declared.group(1)
            classes.setdefault(current, {})
            continue
        field = re.match(r"---@field\s+([A-Za-z0-9_]+)\??\s+([^\s].*)", line)
        if field and current:
            classes[current][field.group(1)] = field.group(2)
    return classes, callable_aliases


def callable_fields(
    classes: dict[str, dict[str, str]], callable_aliases: set[str], name: str
) -> set[str]:
    fields = classes.get(name, {})
    return {
        field
        for field, annotation in fields.items()
        if re.search(r"\bfun\s*\(", annotation)
        or annotation.split()[0] in callable_aliases
    }


def fail_differences(label: str, runtime: set[str], documented: set[str]) -> list[str]:
    errors: list[str] = []
    missing = sorted(runtime - documented)
    phantom = sorted(documented - runtime)
    if missing:
        errors.append(f"{label}: runtime callable(s) missing from meta: {', '.join(missing)}")
    if phantom:
        errors.append(f"{label}: meta callable(s) missing from runtime: {', '.join(phantom)}")
    return errors


def main() -> int:
    c_source = C_SOURCE.read_text(encoding="utf-8")
    meta_source = META_SOURCE.read_text(encoding="utf-8")
    arrays = c_arrays(c_source)
    classes, callable_aliases = meta_classes(meta_source)
    errors: list[str] = []

    expected_arrays = {
        "FNS": "torirs.Api",
        "CHROME": "torirs.ChromeApi",
        "ENTITY": "torirs.EntityApi",
        "PLACEMENT": "torirs.Placement",
        "WIN": "torirs.WindowApi",
        "PANEL": "torirs.PanelApi",
        "DRAW": "torirs.Draw",
        "TOP": "torirs.LayoutTopLevel",
    }
    for array, meta_class in expected_arrays.items():
        rows = arrays.get(array, [])
        if len(rows) != 1:
            errors.append(f"C inventory: expected one {array} registration table, found {len(rows)}")
            continue
        errors.extend(
            fail_differences(
                meta_class,
                set(rows[0]),
                callable_fields(classes, callable_aliases, meta_class),
            )
        )

    verbs = arrays.get("VERBS", [])
    if len(verbs) != 2:
        errors.append(f"C inventory: expected Role and LayoutRegion VERBS tables, found {len(verbs)}")
    else:
        errors.extend(
            fail_differences(
                "torirs.Role",
                set(verbs[0]),
                callable_fields(classes, callable_aliases, "torirs.Role"),
            )
        )
        errors.extend(
            fail_differences(
                "torirs.LayoutRegion",
                set(verbs[1]),
                callable_fields(classes, callable_aliases, "torirs.LayoutRegion"),
            )
        )

    # These callables are installed explicitly because they need unusual
    # upvalues rather than the ordinary registration-table loop.
    explicit = {
        "torirs.Api": {"role"},
        "torirs.Layout": {"revision"},
        "torirs.EvMenuBuild": {"add"},
    }
    for meta_class, runtime in explicit.items():
        documented = callable_fields(classes, callable_aliases, meta_class)
        if meta_class == "torirs.Api":
            documented -= set(arrays.get("FNS", [[]])[0])
        errors.extend(fail_differences(meta_class + " explicit", runtime, documented))

    regions = arrays.get("REGIONS", [])
    runtime_layout_tables = set(regions[0]) | {"top_level"} if len(regions) == 1 else set()
    documented_layout = set(classes.get("torirs.Layout", {})) - callable_fields(
        classes, callable_aliases, "torirs.Layout"
    )
    errors.extend(
        fail_differences("torirs.Layout tables", runtime_layout_tables, documented_layout)
    )

    runtime_api_tables = {"chrome", "entity", "layout", "placement", "window", "panel", "config"}
    documented_api_tables = set(classes.get("torirs.Api", {})) - callable_fields(
        classes, callable_aliases, "torirs.Api"
    )
    errors.extend(fail_differences("torirs.Api tables", runtime_api_tables, documented_api_tables))

    # Prove that the source still mounts each table the inventory inferred.
    for table in sorted(runtime_api_tables):
        if not re.search(rf'lua_setfield\(L,\s*-2,\s*"{re.escape(table)}"\);', c_source):
            errors.append(f"C inventory: api.{table} is inventoried but never mounted")

    if errors:
        for error in errors:
            print(f"lua api inventory: {error}", file=sys.stderr)
        return 1
    callable_count = sum(len(rows[0]) for name, rows in arrays.items() if name in expected_arrays)
    callable_count += sum(len(v) for v in verbs) + sum(len(v) for v in explicit.values())
    print(
        f"lua api inventory: {callable_count} callable registrations and "
        f"{len(runtime_api_tables) + len(runtime_layout_tables)} tables match meta"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
