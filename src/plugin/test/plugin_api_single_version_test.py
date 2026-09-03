#!/usr/bin/env python3
"""Keep the removed flat plugin API from growing back beside V2."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SELF = Path(__file__).resolve()
EXCLUDED = {
    SELF,
    (ROOT / "src/plugin/test/plugin_lua_api_inventory_test.py").resolve(),
}

FORBIDDEN_FILES = (
    "src/plugin/torirs_plugin.h",
    "src/plugin/torirs_plugin_v2_adapter.c",
    "src/plugin/torirs_plugin_v2_adapter.h",
    "src/plugin/test/torirs_plugin_v2_adapter_test.c",
)

FORBIDDEN = (
    ("flat API type", r"\bToriRS_PluginApi\b"),
    ("flat API context type", r"\bToriRS_PluginCtx\b"),
    ("flat plugin definition", r"\bToriRS_PluginDef\b"),
    ("flat registration", r"\bPluginHost_Register\s*\("),
    ("flat API accessor", r"\bPluginHost_Api\s*\("),
    ("manual event id", r"\bTORIRS_PLUGIN_EV_[A-Z0-9_]+\b"),
    ("flat event spelling", r"\bEV_[A-Z0-9_]+\b"),
    ("manual event type", r"\bToriRS_Plugin(?:Event|Verdict)\b"),
    ("manual event verdict", r"\bTORIRS_PLUGIN_(?:PASS|CONSUME)\b"),
    ("manual subscription", r"->\s*subscribe\s*\("),
    ("old numeric ABI", r"\bTORIRS_PLUGIN_ABI\b"),
    ("V1 adapter", r"torirs_plugin_v2_adapter"),
    ("legacy public value", r"\bToriRS_Legacy[A-Za-z0-9_]*\b"),
    (
        "old public value type",
        r"\bToriRS_Plugin(?:PlayerSnap|NpcSnap|ObjSnap|LocSnap|ObjInfo|Feature|Lane|"
        r"HoverEntity|LootSource|LootRow|ConfigItem|PanelDesc)\b",
    ),
    ("old surface slot", r"\bTORIRS_PLUGIN_SLOT_[A-Z0-9_]+\b"),
    ("old placement area", r"\bTORIRS_PLUGIN_AREA_[A-Z0-9_]+\b"),
    ("old placement edge", r"\bTORIRS_PLUGIN_PLACEMENT_EDGE_[A-Z0-9_]+\b"),
    ("old frame-canvas value", r"\bTORIRS_PLUGIN_CANVAS_[A-Z0-9_]+\b"),
    ("old engine-frame value", r"\bToriRS_EngineFrame[A-Za-z0-9_]*\b"),
    ("old plugin window API", r"\bPluginHost_Win[A-Za-z0-9_]*\b"),
    ("old chrome pass", r"\bPluginHost_ChromeTick\b"),
    (
        "old ownership verb",
        r"\b(?:layout_claim|layout_release|layout_owned|layout_set|chrome_claim|chrome_add|"
        r"chrome_part|chrome_paint|safe_os|role_replace|role_anchor)\b|plugin_layout_owned",
    ),
    ("ambiguous compatibility size", r"\bTORIRS_[A-Z0-9_]+_LEGACY_SIZE\b"),
    ("duplicated V2 drawing helper", r"\b(?:PluginDraw_[A-Za-z0-9_]+V2|PluginDraw_AtlasV2)\b"),
)


def source_files() -> list[Path]:
    files: list[Path] = []
    for suffix in ("*.c", "*.h", "*.inc"):
        files.extend((ROOT / "src").rglob(suffix))
    files.extend((ROOT / "script/plugins").glob("*.lua"))
    return sorted(path for path in files if path.resolve() not in EXCLUDED)


def main() -> int:
    errors: list[str] = []
    for relative in FORBIDDEN_FILES:
        if (ROOT / relative).exists():
            errors.append(f"{relative}: removed V1 file exists")

    compiled = [(label, re.compile(pattern)) for label, pattern in FORBIDDEN]
    for path in source_files():
        text = path.read_text(encoding="utf-8", errors="replace")
        relative = path.relative_to(ROOT)
        for line_number, line in enumerate(text.splitlines(), 1):
            for label, pattern in compiled:
                if pattern.search(line):
                    errors.append(f"{relative}:{line_number}: {label}: {line.strip()}")

    if errors:
        print("single-version plugin API check failed:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        return 1
    print("single-version plugin API: V2 only")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
