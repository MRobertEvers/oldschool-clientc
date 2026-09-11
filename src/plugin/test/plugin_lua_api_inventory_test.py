#!/usr/bin/env python3
"""Prove the Lua runtime, LuaLS declaration, and bundled scripts are peers of the current API."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
C_SOURCE = ROOT / "src/plugin/torirs_plugin_lua.c"
API_SOURCE = ROOT / "src/plugin/torirs_plugin_api.h"
META_SOURCE = ROOT / "script/plugins/plugin_api.meta.lua"
SCRIPT_DIR = ROOT / "script/plugins"


def lua_fn_arrays(source: str) -> dict[str, set[str]]:
    arrays: dict[str, set[str]] = {}
    for name, body in re.findall(
        r"static\s+struct\s+LuaFn\s+const\s+(LUA_[A-Z_]+_FNS)\[\]\s*=\s*\{(.*?)\n\};",
        source,
        re.S,
    ):
        arrays[name] = set(re.findall(r'\{\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*,', body))
    return arrays


def runtime_modules(source: str) -> dict[str, str]:
    match = re.search(
        r"static\s+struct\s+LuaModuleRegistration\s+const\s+LUA_API_MODULES\[\]\s*=\s*\{(.*?)\n\};",
        source,
        re.S,
    )
    if not match:
        return {}
    return dict(re.findall(r'\{\s*"([a-z_]+)"\s*,\s*(LUA_[A-Z_]+_FNS)\s*\}', match.group(1)))


def meta_classes(source: str) -> dict[str, dict[str, str]]:
    classes: dict[str, dict[str, str]] = {}
    current: str | None = None
    for line in source.splitlines():
        declared = re.match(r"---@class\s+(torirs\.[A-Za-z0-9_]+)", line)
        if declared:
            current = declared.group(1)
            classes.setdefault(current, {})
            continue
        field = re.match(r"---@field\s+([A-Za-z_][A-Za-z0-9_]*)\??\s+(.+)", line)
        if field and current:
            classes[current][field.group(1)] = field.group(2)
    return classes


def struct_callables(source: str, struct_name: str) -> set[str]:
    match = re.search(
        rf"struct\s+{re.escape(struct_name)}\s*\{{(.*?)\n\}};",
        source,
        re.S,
    )
    if not match:
        return set()
    return set(re.findall(r"\(\*([a-z][a-z0-9_]*)\)\s*\(", match.group(1)))


def callable_fields(fields: dict[str, str]) -> set[str]:
    return {name for name, annotation in fields.items() if re.search(r"\bfun\s*\(", annotation)}


def difference(label: str, runtime: set[str], meta: set[str]) -> list[str]:
    errors: list[str] = []
    if runtime - meta:
        errors.append(f"{label}: runtime-only: {', '.join(sorted(runtime - meta))}")
    if meta - runtime:
        errors.append(f"{label}: meta-only: {', '.join(sorted(meta - runtime))}")
    return errors


def strip_line_comments(source: str) -> str:
    return "\n".join(line.split("--", 1)[0] for line in source.splitlines())


def main() -> int:
    c_source = C_SOURCE.read_text(encoding="utf-8")
    api_source = API_SOURCE.read_text(encoding="utf-8") + (ROOT / "src/plugin/torirs_plugin_contract.h").read_text(encoding="utf-8")
    meta_source = META_SOURCE.read_text(encoding="utf-8")
    arrays = lua_fn_arrays(c_source)
    modules = runtime_modules(c_source)
    classes = meta_classes(meta_source)
    errors: list[str] = []

    expected_array_names = set(modules.values()) | {
        "LUA_WIDGET_METHOD_FNS",
        "LUA_GRAPHICS_FNS",
        "LUA_PANEL_BUILDER_FNS",
    }
    errors += difference("registration arrays", set(arrays), expected_array_names)

    module_structs = {
        "widgets": "ToriRS_WidgetApi", "scripts": "ToriRS_ScriptApi",
        "core": "ToriRS_CoreApi", "config": "ToriRS_ConfigApi",
        "world": "ToriRS_WorldApi", "input": "ToriRS_InputApi",
        "menu": "ToriRS_MenuApi",
        "frame": "ToriRS_FrameApi", "draw": "ToriRS_DrawApi",
        "assets": "ToriRS_AssetsApi", "scene": "ToriRS_SceneApi",
        "panel": "ToriRS_PanelApi", "cache": "ToriRS_CacheApi",
        "client": "ToriRS_ClientApi", "game": "ToriRS_GameApi",
    }
    errors += difference("canonical module set", set(modules), set(module_structs))
    for module, struct_name in module_structs.items():
        if module == "widgets":
            widget_calls = arrays.get("LUA_WIDGET_FNS", set()) | arrays.get("LUA_WIDGET_METHOD_FNS", set())
            native_calls = struct_callables(api_source, struct_name)
            native_calls = { {"get_widget":"get", "get_text":"text"}.get(name,name) for name in native_calls }
            errors += difference("live widgets versus C API", widget_calls, native_calls)
            errors += difference("widget methods versus metadata", arrays.get("LUA_WIDGET_METHOD_FNS",set()), callable_fields(classes.get("torirs.Widget",{})))
            continue
        errors += difference(
            f"api.{module} versus {struct_name}",
            arrays.get(modules.get(module, ""), set()),
            struct_callables(api_source, struct_name),
        )

    api_fields = classes.get("torirs.Api", {})
    errors += difference("api modules", set(modules), set(api_fields))
    for module, array in sorted(modules.items()):
        annotation = api_fields.get(module, "")
        class_match = re.match(r"(torirs\.[A-Za-z0-9_]+)", annotation)
        if not class_match:
            errors.append(f"api.{module}: missing module class in meta")
            continue
        errors += difference(
            f"api.{module}", arrays.get(array, set()),
            callable_fields(classes.get(class_match.group(1), {})),
        )

    for array, class_name in (
        ("LUA_GRAPHICS_FNS", "torirs.Graphics"),
        ("LUA_PANEL_BUILDER_FNS", "torirs.PanelBuilder"),
    ):
        errors += difference(
            class_name,
            arrays.get(array, set()),
            callable_fields(classes.get(class_name, {})),
        )
    errors += difference(
        "draw builder versus API header",
        arrays.get("LUA_GRAPHICS_FNS", set()),
        struct_callables(api_source, "ToriRS_Graphics"),
    )
    errors += difference(
        "panel builder versus API header",
        arrays.get("LUA_PANEL_BUILDER_FNS", set()),
        struct_callables(api_source, "ToriRS_PanelBuilder"),
    )

    handler_match = re.search(
        r"LUA_HANDLER_NAMES\[LUA_HANDLER_COUNT\]\s*=\s*\{(.*?)\n\};",
        c_source,
        re.S,
    )
    handlers = set(re.findall(r'"(on_[a-z_]+)"', handler_match.group(1))) if handler_match else set()
    errors += difference(
        "callbacks versus API header",
        handlers,
        struct_callables(api_source, "ToriRS_PluginCallbacks"),
    )
    errors += difference(
        "plugin callbacks", handlers,
        {name for name in classes.get("torirs.Plugin", {}) if name.startswith("on_")},
    )

    forbidden_c = {
        "ToriRS_PluginApi": "legacy API type",
        "ToriRS_PluginDefV2 const": "previous plugin definition",
        "PluginHost_RegisterV2(host": "previous registration",
    }
    for token, label in forbidden_c.items():
        if token in c_source:
            errors.append(f"Lua runtime still contains {label}: {token}")

    remove_match = re.search(
        r"static\s+char\s+const\*\s+const\s+REMOVE\[\]\s*=\s*\{(.*?)\n\s*\};",
        c_source,
        re.S,
    )
    removed_globals = (
        set(re.findall(r'"([a-z_]+)"', remove_match.group(1)))
        if remove_match else set()
    )
    documented_nil = set(
        re.findall(r"---@type\s+nil\s*\n([a-z_]+)\s*=\s*nil", meta_source)
    )
    errors += difference("removed sandbox globals", removed_globals, documented_nil)

    canonical_modules = set(modules)
    legacy_callbacks = {
        "on_frame", "on_obj_spawn", "on_obj_count", "on_obj_despawn",
        "on_layout_changed", "on_screen_change", "on_panel_build",
        "on_panel_action", "on_panel_layout", "on_panel_draw",
        "on_canvas_click", "on_ui",
    }
    for path in sorted(SCRIPT_DIR.glob("*.lua")):
        if path == META_SOURCE:
            continue
        source = strip_line_comments(path.read_text(encoding="utf-8"))
        for top in re.findall(r"\bapi\.([A-Za-z_][A-Za-z0-9_]*)", source):
            if top not in canonical_modules:
                errors.append(f"{path.name}: noncanonical api.{top}")
        for callback in re.findall(r"\bfunction\s+[A-Za-z_][A-Za-z0-9_]*\.(on_[a-z_]+)", source):
            if callback not in handlers:
                errors.append(f"{path.name}: unsupported callback {callback}")
            if callback in legacy_callbacks:
                errors.append(f"{path.name}: legacy callback {callback}")

    screenshot = (SCRIPT_DIR / "screenshot.lua").read_text(encoding="utf-8")
    for required in (
        'api.widgets.watch("report_button"',
        'api.widgets.watch("viewport"',
        ":create_image(",
        ":set_image(",
        ":set_on_op(",
        "report:set_hidden(",
        '"report-button"',
        'off|top-left|top-right|bottom-left|bottom-right|report-button',
        "api.assets.screenshot(",
    ):
        if required not in screenshot:
            errors.append(f"screenshot.lua: missing retained camera behavior: {required}")
    for forbidden in ("ui_contributions", "on_ui_node_draw", "on_ui_node_action", "on_canvas_action", "api.ui.", "api.placement."):
        if forbidden in screenshot:
            errors.append(f"screenshot.lua: superseded execution API still used: {forbidden}")

    if errors:
        for error in errors:
            print(f"lua API inventory: {error}", file=sys.stderr)
        return 1
    callable_count = sum(len(arrays[name]) for name in expected_array_names)
    print(
        f"lua API inventory: {len(modules)} modules, {callable_count} callables, "
        f"{len(handlers)} callbacks, and bundled scripts match"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
