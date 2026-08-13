#!/usr/bin/env python3
"""
wiki_infobox — parse a wiki `Infobox Monster` template into per-npc-id stat
blocks. Library, plus a CLI for inspecting one cached page.

    tools/wiki_infobox.py "Goblin"
    tools/wiki_infobox.py "Black Knight" --raw hitpoints

See docs/NPC_WIKI_STATS_PLAN.md §2. A page can hold more than one
`Infobox Monster` template (`{{Multi Infobox}}` wraps two on "Black Knight" —
aggressive and passive forms are different combat records for the same
creature) and each template can hold more than one version
(`|hitpoints1=`, `|hitpoints2=`, ... alongside shared unnumbered keys). What
this module hands back is flat: one dict per **version block**, carrying
every field that applies to it (shared keys merged under the versioned ones)
and the list of cache npc ids (`|id=` or `|id1=`, `|id2=`, ...) that version
covers. That id list is the whole join — see `gen_npc_stats.py`.

Template and wikilink parsing is real (brace/bracket depth counting), not a
regex-over-the-whole-page: field values routinely nest another template
(`{{plink|Coins}}`) or a piped link (`[[Crush]]`), and a regex that assumes
`|` only ever separates infobox fields corrupts on the first `{{...|...}}` it
meets.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MONSTERS_DIR = os.path.join(REPO, "OSRS-Content", "osrs239-content", "wiki", "monsters")

TEMPLATE_OPEN_RE = re.compile(r"\{\{\s*Infobox[ _]Monster\b", re.IGNORECASE)


def _find_matching_close(text: str, open_pos: int) -> int:
    """`open_pos` is the index of the `{{` that opens the template. Returns
    the index just past its matching `}}`, counting nested `{{`/`}}` pairs."""
    depth = 0
    i = open_pos
    n = len(text)
    while i < n - 1:
        two = text[i : i + 2]
        if two == "{{":
            depth += 1
            i += 2
        elif two == "}}":
            depth -= 1
            i += 2
            if depth == 0:
                return i
        else:
            i += 1
    return n


def _split_top_level(body: str, sep: str) -> list[str]:
    """Split on `sep` at brace/bracket depth 0 only."""
    parts = []
    depth = 0
    current = []
    i = 0
    n = len(body)
    while i < n:
        two = body[i : i + 2] if i + 1 < n else ""
        if two in ("{{", "[["):
            depth += 1
            current.append(two)
            i += 2
            continue
        if two in ("}}", "]]"):
            depth -= 1
            current.append(two)
            i += 2
            continue
        ch = body[i]
        if ch == sep and depth == 0:
            parts.append("".join(current))
            current = []
        else:
            current.append(ch)
        i += 1
    parts.append("".join(current))
    return parts


def find_templates(wikitext: str) -> list[str]:
    """Raw parameter body (between the template name and the closing `}}`)
    of every `Infobox Monster` template on the page, in document order."""
    bodies = []
    for m in TEMPLATE_OPEN_RE.finditer(wikitext):
        open_pos = m.start()
        # Skip past ones already consumed inside a prior match's span.
        if bodies and open_pos < bodies[-1][1]:
            continue
        close_pos = _find_matching_close(wikitext, open_pos)
        inner = wikitext[m.end() : close_pos - 2]
        bodies.append((inner, close_pos))
    return [b for b, _ in bodies]


def parse_params(body: str) -> dict[str, str]:
    """`|key = value` pairs at the template's top level, in order, last write
    wins (matches MediaWiki's own rule for a repeated key)."""
    out: dict[str, str] = {}
    for part in _split_top_level(body, "|"):
        if "=" not in part:
            continue
        key, value = part.split("=", 1)
        out[key.strip().lower()] = value.strip()
    return out


NUM_SUFFIX_RE = re.compile(r"^(.*?)(\d+)$")


def split_versions(fields: dict[str, str]) -> list[dict]:
    """`fields` is one template's raw param dict. Returns one entry per
    version: `{"index": N, "fields": {...}}`, `index` 0 for an unversioned
    (single-form) template. Shared (unnumbered) keys are merged into every
    version; a numbered key overrides the shared one of the same base name."""
    numbered: dict[int, dict[str, str]] = {}
    bare: dict[str, str] = {}
    for key, value in fields.items():
        m = NUM_SUFFIX_RE.match(key)
        if m and m.group(1):
            base, num = m.group(1), int(m.group(2))
            numbered.setdefault(num, {})[base] = value
        else:
            bare[key] = value

    if not numbered:
        return [{"index": 0, "fields": bare}]

    versions = []
    for num in sorted(numbered):
        merged = dict(bare)
        merged.update(numbered[num])
        versions.append({"index": num, "fields": merged})
    return versions


def parse_page(wikitext: str) -> list[dict]:
    """Every version block on the page, across every `Infobox Monster`
    template found, each tagged with which template it came from."""
    out = []
    for template_index, body in enumerate(find_templates(wikitext)):
        fields = parse_params(body)
        for version in split_versions(fields):
            out.append(
                {
                    "template_index": template_index,
                    "version_index": version["index"],
                    "ids": get_int_list(version["fields"], "id"),
                    "fields": version["fields"],
                }
            )
    return out


# ---------------------------------------------------------------------------
# Field readers — raw wikitext in, a typed value out. Every field this plan
# maps to server content goes through one of these rather than a bespoke
# regex at the call site, so "how a wiki number gets cleaned" has one answer.
# ---------------------------------------------------------------------------

WIKILINK_RE = re.compile(r"\[\[(?:[^\]|]*\|)?([^\]]*)\]\]")
TEMPLATE_RE = re.compile(r"\{\{[^{}]*\}\}")
REF_RE = re.compile(r"<ref[^>]*>.*?</ref>|<ref[^>]*/>", re.DOTALL | re.IGNORECASE)
COMMENT_RE = re.compile(r"<!--.*?-->", re.DOTALL)
TAG_RE = re.compile(r"<[^>]+>")


def clean_text(raw: str | None) -> str:
    """Strip wiki markup down to display text. Templates are dropped
    wholesale (an infobox field's own template use is decorative — footnote
    icons, not data this plan reads) rather than expanded, since none of the
    fields this plan maps carry information *only* inside a nested template."""
    if raw is None:
        return ""
    s = raw
    s = COMMENT_RE.sub("", s)
    s = REF_RE.sub("", s)
    # Repeat: a template can itself contain a wikilink, so collapse from the
    # inside out until neither pattern matches.
    prev = None
    while prev != s:
        prev = s
        s = WIKILINK_RE.sub(r"\1", s)
        s = TEMPLATE_RE.sub("", s)
    s = TAG_RE.sub("", s)
    s = s.replace("'''", "").replace("''", "")
    return s.strip()


def get_int(fields: dict[str, str], key: str) -> int | None:
    text = clean_text(fields.get(key))
    m = re.search(r"-?\d+", text.replace(",", ""))
    return int(m.group(0)) if m else None


def get_int_list(fields: dict[str, str], key: str) -> list[int]:
    text = clean_text(fields.get(key))
    return [int(n) for n in re.findall(r"\d+", text)]


def get_str(fields: dict[str, str], key: str) -> str:
    return clean_text(fields.get(key))


def get_bool(fields: dict[str, str], key: str, default: bool = False) -> bool:
    text = get_str(fields, key).strip().lower()
    if not text:
        return default
    return text.startswith("yes")


def get_list(fields: dict[str, str], key: str) -> list[str]:
    """A field the wiki writes as a comma/and-joined list, e.g.
    `attack style = [[Crush]], [[Magic]]` or `attributes = Demon and Fire`."""
    text = get_str(fields, key)
    if not text:
        return []
    parts = re.split(r",|\band\b", text)
    return [p.strip() for p in parts if p.strip()]


DISAMBIG_LINK_RE = re.compile(r"\[\[([^\]|#]+)(?:\|[^\]]*)?\]\]")
IGNORED_NAMESPACES = ("File:", "Category:", "Image:", "Template:")


def disambiguation_candidates(wikitext: str) -> list[str]:
    """Every article-namespace wikilink on a page with no `Infobox Monster` —
    the fan-out for `gen_npc_stats.py`'s disambiguation fallback (§ "Fields
    with no destination" in docs/NPC_WIKI_STATS_PLAN.md §2, extended in the
    implementation to resolve names like "Soldier" or "Mummy" that the wiki
    only disambiguates). Order-preserved, deduplicated. Not filtered further
    here — the caller verifies a candidate by checking whether the npc id it
    is looking for actually appears in that candidate's own `id`/`idN` list,
    which is the only check that cannot bind the wrong monster."""
    seen: list[str] = []
    seen_set: set[str] = set()
    for m in DISAMBIG_LINK_RE.finditer(wikitext):
        title = m.group(1).strip()
        if not title or any(title.startswith(ns) for ns in IGNORED_NAMESPACES):
            continue
        if title not in seen_set:
            seen_set.add(title)
            seen.append(title)
    return seen


# ---------------------------------------------------------------------------
# Loading a cached page
# ---------------------------------------------------------------------------


def load_page(title_or_filename: str) -> str:
    path = title_or_filename
    if not os.path.isabs(path):
        candidate = os.path.join(MONSTERS_DIR, title_or_filename)
        if not candidate.endswith(".wikitext"):
            candidate += ".wikitext"
        path = candidate
    with open(path, encoding="utf-8") as f:
        return f.read()


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("title", help="wiki page title (matches a file under wiki/monsters/)")
    ap.add_argument("--raw", metavar="KEY", help="print one field's raw wikitext per version instead of the JSON dump")
    args = ap.parse_args()

    wikitext = load_page(args.title)
    versions = parse_page(wikitext)
    if args.raw:
        for v in versions:
            print(f"template {v['template_index']} version {v['version_index']} ids={v['ids']}: "
                  f"{v['fields'].get(args.raw)!r}")
        return
    print(json.dumps(versions, indent=2))


if __name__ == "__main__":
    main()
