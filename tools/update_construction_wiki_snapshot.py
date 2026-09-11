#!/usr/bin/env python3
"""Refresh the factual OSRS Wiki snapshot used by the Construction crosswalk.

The live Wiki is intentionally not a build dependency. This opt-in updater
reads the current Constructed items table, follows redirects in batches, and
stores only the factual fields needed to reconcile revision-239 furniture:
level, XP, room/hotspot/material text, cache item IDs, built object IDs, and
page revision provenance.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.parse import quote, urlencode
from urllib.request import Request, urlopen


API = "https://oldschool.runescape.wiki/api.php"
PAGE = "Constructed items"
PAGE_URL = "https://oldschool.runescape.wiki/w/Constructed_items"
USER_AGENT = "3draster-construction-audit/1.0 (cache/Wiki reconciliation)"
ITEM_RE = re.compile(r"\{\{ilinkt\|([^}|]+)(?:\|[^}]*)?\}\}", re.IGNORECASE)
INFOBOX_RE = re.compile(r"\{\{Infobox (Construction|Scenery)\b", re.IGNORECASE)
FIELD_RE = re.compile(r"\|\s*([a-z][a-z0-9_]*)\s*=\s*(.*)", re.IGNORECASE)


class SnapshotError(RuntimeError):
    pass


def api(params: dict[str, str], *, post: bool = False) -> dict[str, object]:
    encoded = urlencode(params).encode("utf-8")
    request = Request(
        API if post else f"{API}?{encoded.decode('utf-8')}",
        data=encoded if post else None,
        headers={"User-Agent": USER_AGENT},
    )
    with urlopen(request, timeout=45) as response:
        payload = json.load(response)
    if "error" in payload:
        raise SnapshotError(str(payload["error"]))
    return payload


def cells(chunk: str) -> list[str]:
    result: list[str] = []
    current: list[str] = []
    for line in chunk.splitlines():
        if line.startswith("|") and not line.startswith("|}"):
            if current:
                result.append("\n".join(current))
            current = [line[1:]]
        elif current:
            current.append(line)
    if current:
        result.append("\n".join(current))
    return result


def first_number(text: str) -> int | float | None:
    match = re.search(r"\d+(?:\.\d+)?", text.replace(",", ""))
    if match is None:
        return None
    value = float(match.group())
    return int(value) if value.is_integer() else value


def table_entries(wikitext: str) -> list[dict[str, object]]:
    result = []
    for table_index, chunk in enumerate(re.split(r"\n\|-\s*\n", wikitext)[1:]):
        row = cells(chunk)
        if len(row) < 7:
            continue
        item = ITEM_RE.search(row[0])
        if item is None:
            continue
        source_title = item.group(1).strip()
        text_override = re.search(r"\|txt=([^}|]+)", row[0], re.IGNORECASE)
        result.append(
            {
                "table_index": table_index,
                "name": (text_override.group(1) if text_override else source_title).strip(),
                "source_title": source_title,
                "level": first_number(row[1]),
                "experience": first_number(row[2]),
                "room_wikitext": row[3].strip(),
                "hotspot_wikitext": row[4].strip(),
                "flatpack_wikitext": row[5].strip(),
                "materials_wikitext": row[6].strip(),
            }
        )
    if len(result) < 400:
        raise SnapshotError(f"expected at least 400 Constructed items rows, found {len(result)}")
    return result


def template_blocks(content: str, template: str) -> list[str]:
    """Return balanced wikitext blocks for a named template."""
    starts = list(re.finditer(r"\{\{" + re.escape(template) + r"\b", content, re.IGNORECASE))
    result = []
    for start in starts:
        depth = 0
        cursor = start.start()
        while cursor < len(content) - 1:
            opening = content.find("{{", cursor)
            closing = content.find("}}", cursor)
            if closing < 0:
                break
            if 0 <= opening < closing:
                depth += 1
                cursor = opening + 2
            else:
                depth -= 1
                cursor = closing + 2
                if depth == 0:
                    result.append(content[start.start() : cursor])
                    break
    return result


def template_fields(block: str) -> dict[str, str]:
    result = {}
    for line in block.splitlines():
        match = FIELD_RE.match(line)
        if match is not None:
            result[match.group(1).lower()] = match.group(2).strip()
    return result


def numbers(value: str) -> list[int]:
    return [int(item) for item in re.findall(r"\b\d+\b", value)]


def infobox_metadata(content: str) -> dict[str, object]:
    start = INFOBOX_RE.search(content)
    if start is None:
        return {
            "infobox_template": None,
            "object_ids": [],
            "item_ids": [],
            "object_variants": [],
        }
    template = f"Infobox {start.group(1)}"
    blocks = template_blocks(content[start.start() :], template)
    block = blocks[0] if blocks else content[start.start() :]
    fields = template_fields(block)
    object_ids: list[int] = []
    item_ids: list[int] = []
    variants = []
    for key, value in fields.items():
        match = re.fullmatch(r"(id|itemid)(\d*)", key)
        if match is None:
            continue
        values = numbers(value)
        (object_ids if match.group(1) == "id" else item_ids).extend(values)
        if match.group(1) == "id":
            suffix = match.group(2)
            variants.append(
                {
                    "version": fields.get(f"version{suffix}") if suffix else fields.get("version"),
                    "object_ids": values,
                }
            )
    return {
        "infobox_template": template,
        "object_ids": sorted(set(object_ids)),
        "item_ids": sorted(set(item_ids)),
        "object_variants": variants,
    }


def construction_recipes(content: str) -> list[dict[str, object]]:
    result = []
    for block in template_blocks(content, "Recipe"):
        fields = template_fields(block)
        construction_index = next(
            (
                match.group(1)
                for key, value in fields.items()
                if (match := re.fullmatch(r"skill(\d+)", key))
                and value.strip().lower() == "construction"
            ),
            None,
        )
        if construction_index is None:
            continue
        materials = []
        for key, value in fields.items():
            match = re.fullmatch(r"mat(\d+)", key)
            if match is None:
                continue
            quantity = first_number(fields.get(f"mat{match.group(1)}quantity", "1"))
            materials.append({"name_wikitext": value, "quantity": quantity})
        result.append(
            {
                "level": first_number(fields.get(f"skill{construction_index}lvl", "")),
                "experience": first_number(fields.get(f"skill{construction_index}exp", "")),
                "materials": materials,
                "output_wikitext": fields.get("output1"),
                "output_quantity": first_number(fields.get("output1quantity", "1")),
            }
        )
    return result


def resolved_title(title: str, redirects: dict[str, str]) -> str:
    seen = set()
    while title in redirects and title not in seen:
        seen.add(title)
        title = redirects[title]
    return title


def refresh(reconciliations_path: Path) -> dict[str, object]:
    reconciliations = json.loads(reconciliations_path.read_text(encoding="utf-8"))
    reconciliation_rows = reconciliations.get("rows")
    if not isinstance(reconciliation_rows, dict):
        raise SnapshotError(f"{reconciliations_path}: missing rows object")
    parsed = api(
        {
            "action": "parse",
            "page": PAGE,
            "prop": "wikitext",
            "format": "json",
        }
    )["parse"]
    entries = table_entries(parsed["wikitext"]["*"])
    by_source: dict[str, list[dict[str, object]]] = defaultdict(list)
    for entry in entries:
        by_source[str(entry["source_title"])].append(entry)
    table_titles = set(by_source)
    supplemental_titles = {
        str(value[key])
        for value in reconciliation_rows.values()
        for key in ("page", "experience_page")
        if key in value
    }
    for title in sorted(supplemental_titles - table_titles):
        entry = {
            "table_index": None,
            "name": title,
            "source_title": title,
            "level": None,
            "experience": None,
            "room_wikitext": None,
            "hotspot_wikitext": None,
            "flatpack_wikitext": None,
            "materials_wikitext": None,
            "supplemental": True,
        }
        entries.append(entry)
        by_source[title].append(entry)
    titles = sorted(by_source)

    for offset in range(0, len(titles), 50):
        batch = titles[offset : offset + 50]
        payload = api(
            {
                "action": "query",
                "prop": "revisions",
                "rvprop": "ids|timestamp|content",
                "rvslots": "main",
                "titles": "|".join(batch),
                "redirects": "1",
                "format": "json",
                "formatversion": "2",
            },
            post=True,
        )["query"]
        redirects = {
            item["from"]: item["to"]
            for key in ("normalized", "redirects")
            for item in payload.get(key, [])
        }
        sources_by_page: dict[str, list[str]] = defaultdict(list)
        for title in batch:
            sources_by_page[resolved_title(title, redirects)].append(title)
        for page in payload["pages"]:
            revisions = page.get("revisions", [])
            if not revisions:
                continue
            revision = revisions[0]
            content = revision["slots"]["main"]["content"]
            metadata = infobox_metadata(content)
            recipes = construction_recipes(content)
            for source_title in sources_by_page.get(page["title"], []):
                for entry in by_source[source_title]:
                    entry.update(
                        {
                            "page": page["title"],
                            "wiki": "https://oldschool.runescape.wiki/w/"
                            + quote(page["title"].replace(" ", "_"), safe="()'_"),
                            "revision_id": revision["revid"],
                            "revision_timestamp": revision["timestamp"],
                            **metadata,
                            "recipes": recipes,
                        }
                    )

    missing = [entry["source_title"] for entry in entries if "revision_id" not in entry]
    if missing:
        raise SnapshotError(f"Wiki pages did not resolve: {missing[:10]}")
    return {
        "source": PAGE_URL,
        "source_page_id": parsed["pageid"],
        "constructed_items_count": len(entries) - len(supplemental_titles - table_titles),
        "reconciliations": str(reconciliations_path),
        "retrieved_at": datetime.now(timezone.utc).replace(microsecond=0).isoformat(),
        "policy": "Factual fields only; cache IDs remain implementation authority.",
        "entries": entries,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("tools/data/construction_wiki_items.json"),
    )
    parser.add_argument(
        "--reconciliations",
        type=Path,
        default=Path("tools/data/construction_wiki_reconciliations.json"),
    )
    args = parser.parse_args()
    try:
        snapshot = refresh(args.reconciliations)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(
            json.dumps(snapshot, indent=2, ensure_ascii=False, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    except (HTTPError, URLError, OSError, SnapshotError, KeyError, ValueError) as exc:
        print(f"construction Wiki snapshot: FAIL: {exc}", file=sys.stderr)
        return 1
    print(
        f"construction Wiki snapshot: OK — {len(snapshot['entries'])} rows -> {args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
