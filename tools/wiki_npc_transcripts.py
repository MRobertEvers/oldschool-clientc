#!/usr/bin/env python3
"""
wiki_npc_transcripts — pull the OSRS Wiki's `Transcript:` page for spawned npcs.

    tools/wiki_npc_transcripts.py --fetch <gameval> ...     # resolve + cache
    tools/wiki_npc_transcripts.py --fetch --from-file f     # one gameval per line
    tools/wiki_npc_transcripts.py --draft <gameval>         # print a RuneScript draft

A npc's dialogue that no RSProx recording covers comes from its wiki
transcript (memory: recent-recordings-means-uncovered-quests). This tool only
caches those pages and drafts from them; the draft is reviewed and edited by
hand before it reaches a `.rs2`, because a transcript's `{{tcond|...}}`
branches key on state (quests, items, diaries) the draft cannot resolve.

## id -> page

`Special:Lookup?type=npc&id=<id>` answers with a redirect to the page whose
infobox states that id. The id comes from `configs/all.npc.compack`, so the
join is by cache id, never by display name ("Guard" names dozens of records).
One request per lookup; cached in `wiki/npc_pages.tsv` and never repeated.

## What gets written

    OSRS-Content/osrs-content/wiki/npc_pages.tsv               gameval, id, page
    OSRS-Content/osrs-content/wiki/transcripts/<page>.wikitext  raw wikitext
    OSRS-Content/osrs-content/wiki/transcripts_manifest.tsv     page, revid, fetch_date

Etiquette as wiki_fetch.py: one request per second, a contact address in the
User-Agent, no parallelism; titles are fetched 50 to a request.
"""

from __future__ import annotations

import argparse
import csv
import datetime
import glob
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(REPO, "OSRS-Content", "osrs-content")
WIKI = os.path.join(CONTENT, "wiki")
PAGES_TSV = os.path.join(WIKI, "npc_pages.tsv")
OUT_DIR = os.path.join(WIKI, "transcripts")
MANIFEST = os.path.join(WIKI, "transcripts_manifest.tsv")
COMPACK = os.path.join(CONTENT, "configs", "all.npc.compack")

SITE = "https://oldschool.runescape.wiki"
API = SITE + "/api.php"
UA = "3draster-npc-dialogue/1.0 (mrobertevers@gmail.com; tools/wiki_npc_transcripts.py)"


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def safe_filename(title: str) -> str:
    return re.sub(r"[^A-Za-z0-9._()' -]", "_", title).replace(" ", "_")


def load_ids() -> dict[str, int]:
    out = {}
    with open(COMPACK, encoding="latin-1") as f:
        for line in f:
            line = line.rstrip("\n")
            if "=" in line and not line.startswith("//"):
                ident, name = line.split("=", 1)
                out[name] = int(ident)
    return out


def load_pages() -> dict[str, dict]:
    if not os.path.exists(PAGES_TSV):
        return {}
    with open(PAGES_TSV, newline="", encoding="utf-8") as f:
        return {r["gameval"]: r for r in csv.DictReader(f, delimiter="\t")}


def write_pages(rows: dict[str, dict]) -> None:
    with open(PAGES_TSV, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=["gameval", "id", "page"], delimiter="\t")
        w.writeheader()
        for k in sorted(rows):
            w.writerow(rows[k])


def load_manifest() -> dict[str, dict]:
    if not os.path.exists(MANIFEST):
        return {}
    with open(MANIFEST, newline="", encoding="utf-8") as f:
        return {r["page"]: r for r in csv.DictReader(f, delimiter="\t")}


def write_manifest(rows: dict[str, dict]) -> None:
    with open(MANIFEST, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=["page", "revid", "fetch_date"], delimiter="\t")
        w.writeheader()
        for k in sorted(rows):
            w.writerow(rows[k])


def lookup_page(npc_id: int) -> str:
    """The page Special:Lookup redirects to, without its #version fragment,
    or "" when the wiki has no page stating this id."""
    url = f"{SITE}/w/Special:Lookup?type=npc&id={npc_id}"
    opener = urllib.request.build_opener(NoRedirect)
    try:
        opener.open(urllib.request.Request(url, headers={"User-Agent": UA}), timeout=30)
    except urllib.error.HTTPError as e:
        if e.code in (301, 302, 303, 307, 308):
            loc = e.headers.get("Location", "")
            path = urllib.parse.urlparse(loc).path
            if "/w/" not in path:
                return ""
            title = urllib.parse.unquote(path.split("/w/", 1)[1]).replace("_", " ")
            if title.startswith("Special:"):
                return ""
            return title
        raise
    return ""


def api(params: dict) -> dict:
    params = dict(params, format="json", formatversion="2")
    url = API + "?" + urllib.parse.urlencode(params)
    with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": UA}), timeout=60) as r:
        return json.load(r)


def fetch(gamevals: list[str]) -> None:
    ids = load_ids()
    pages = load_pages()
    for g in gamevals:
        if g in pages:
            continue
        assert g in ids, f"{g}: not in {COMPACK}"
        page = lookup_page(ids[g])
        pages[g] = {"gameval": g, "id": str(ids[g]), "page": page}
        print(f"{g}\t{ids[g]}\t{page or '-'}", file=sys.stderr)
        write_pages(pages)
        time.sleep(1)

    manifest = load_manifest()
    wanted = sorted({pages[g]["page"] for g in gamevals if pages[g]["page"]} - set(manifest))
    os.makedirs(OUT_DIR, exist_ok=True)
    today = datetime.date.today().isoformat()
    for i in range(0, len(wanted), 50):
        batch = wanted[i : i + 50]
        titles = {"Transcript:" + t: t for t in batch}
        data = api({
            "action": "query", "prop": "revisions", "rvprop": "content|ids",
            "rvslots": "main", "redirects": "1", "titles": "|".join(titles),
        })
        q = data["query"]
        back = {r["to"]: r["from"] for r in q.get("redirects", [])}
        norm = {n["to"]: n["from"] for n in q.get("normalized", [])}
        for p in q["pages"]:
            src = back.get(p["title"], p["title"])
            src = norm.get(src, src)
            page = titles.get(src, src[len("Transcript:"):])
            if p.get("missing"):
                manifest[page] = {"page": page, "revid": "missing", "fetch_date": today}
                continue
            rev = p["revisions"][0]
            text = rev["slots"]["main"]["content"]
            with open(os.path.join(OUT_DIR, safe_filename(page) + ".wikitext"), "w", encoding="utf-8") as f:
                f.write(text)
            manifest[page] = {"page": page, "revid": str(rev["revid"]), "fetch_date": today}
        write_manifest(manifest)
        time.sleep(1)
    have = sum(1 for g in gamevals if pages[g]["page"] and manifest.get(pages[g]["page"], {}).get("revid") not in (None, "missing"))
    print(f"{len(gamevals)} gamevals, {have} with a transcript", file=sys.stderr)


# ------------------------------------------------------------------ draft
#
# A draft is RuneScript for the page's first conversational section, with
# every state the transcript branches on left for the reviewer:
#
#   {{topt|...}} groups      -> ~p_choiceN and an if-chain, exact
#   {{trandom}} + options    -> switch_int(random(n)), exact
#   {{tcond|...}} groups     -> the FIRST branch inline, the others commented
#                               out under a `// TCOND` marker, because the
#                               condition (a quest stage, an item held) has no
#                               mechanical translation; the reviewer writes it
#   {{tact|opens=<shop>}}    -> @<shop_key>_open when that label is live
#
# Sections that are not the npc's ordinary Talk-to (overheads, clue steps,
# historical, game modes) are named in a comment and not drafted.

TEMPLATE_RE = re.compile(r"\{\{(\w+)\|?((?:[^{}]|\{\{[^{}]*\}\})*)\}\}")
SKIP_SECTION_RE = re.compile(
    r"overhead|historical|treasure trail|^(beginner|easy|medium|hard|elite|master)$|crack the clue|"
    r"deadman|leagues|pre-release|varlamore|astral contact|every \d+ ticks|unknown condition",
    re.IGNORECASE,
)
BOLD = "'" * 3


def clean(text: str) -> str:
    text = re.sub(r"\{\{overhead\|([^{}]*)\}\}", r"\1", text)
    text = re.sub(r"\[\[(?:[^\]|]*\|)?([^\]]*)\]\]", r"\1", text)
    text = re.sub(r"'{2,3}", "", text)
    text = re.sub(r"\{\{(?:sic|\*)\}\}", "", text)
    text = re.sub(r"\[[Pp]layer(?:'s)? name\]|\[[Pp]layer\]", "<displayname>", text)
    text = text.replace('"', "\\\"")
    text = re.sub(r"<br\s*/?>", " ", text)
    return re.sub(r"\s+", " ", text).strip()


def template_args(body: str) -> tuple[str, list[str]]:
    m = TEMPLATE_RE.match(body)
    if not m:
        return "", []
    return m.group(1), m.group(2).split("|") if m.group(2) else []


def parse_items(lines: list[str]) -> list[tuple[int, str]]:
    out = []
    for line in lines:
        m = re.match(r"^(\*+)\s*(.*)$", line)
        if m:
            out.append((len(m.group(1)), m.group(2)))
    return out


def take_group(items, i, depth, kind):
    """Consecutive `kind` items at `depth`, each with its children."""
    group = []
    while i < len(items) and items[i][0] >= depth:
        level, body = items[i]
        if level == depth:
            if not body.startswith("{{" + kind):
                break
            group.append((body, []))
        elif group:
            group[-1][1].append(items[i])
        i += 1
    return group, i


def emit(items, depth, indent, out, ctx):
    i = 0
    while i < len(items):
        level, body = items[i]
        if level < depth:
            return
        if level > depth:
            i += 1
            continue
        if body.startswith("{{trandom"):
            group, i = take_group(items, i + 1, depth, "topt")
            out.append(f"{indent}switch_int(random({len(group)})) {{")
            for k, (_, sub) in enumerate(group):
                out.append(f"{indent}    case {k} :")
                emit(sub, depth + 1, indent + "        ", out, ctx)
            out.append(f"{indent}}}")
            continue
        if body.startswith("{{topt"):
            group, i = take_group(items, i, depth, "topt")
            texts, conds = [], []
            for b, _ in group:
                args = template_args(b)[1]
                plain = [a for a in args if not a.startswith("cond=")]
                texts.append(clean(plain[0]) if plain else "?")
                conds += [f"option {len(texts)} only {clean(a[5:])}" for a in args if a.startswith("cond=")]
            for c in conds:
                out.append(f"{indent}// TOPT {c}")
            var = f"$choice{depth}"
            decl = "" if var in ctx["declared"] else "def_int "
            ctx["declared"].add(var)
            args = ", ".join(f'"{t}", {k + 1}' for k, t in enumerate(texts))
            out.append(f"{indent}{decl}{var} = ~p_choice{len(group)}({args});")
            for k, (_, sub) in enumerate(group):
                out.append(f"{indent}{'if' if k == 0 else '} else if'} ({var} = {k + 1}) {{")
                emit(sub, depth + 1, indent + "    ", out, ctx)
            out.append(f"{indent}}}")
            continue
        if body.startswith("{{tcond"):
            group, i = take_group(items, i, depth, "tcond")
            for k, (b, sub) in enumerate(group):
                cond = clean(template_args(b)[1][0]) if template_args(b)[1] else "?"
                out.append(f"{indent}// TCOND {'>>>' if k == 0 else '---'} {cond}")
                if k == 0:
                    emit(sub, depth + 1, indent, out, ctx)
                else:
                    tmp: list[str] = []
                    emit(sub, depth + 1, indent, tmp, ctx)
                    out.extend(f"{indent}// {ln[len(indent):]}" for ln in tmp)
            out.append(f"{indent}// TCOND <<<")
            continue
        m = re.match(r"^" + BOLD + r"(.+?):" + BOLD + r"\s*(.*)$", body)
        if m:
            who, text = m.group(1), clean(m.group(2))
            if who.lower() == "player":
                out.append(f'{indent}~chatplayer("{text}");')
            else:
                out.append(f'{indent}~chatnpc("{text}");')
        elif body.startswith("{{tact"):
            _, args = template_args(body)
            kv = dict(a.split("=", 1) for a in args if "=" in a)
            if "opens" in kv and clean(kv["opens"]) in ctx["shops"]:
                out.append(f"{indent}@{ctx['shops'][clean(kv['opens'])]};")
            elif args and args[0] != "end":
                out.append(f"{indent}// TACT {clean('|'.join(args))}")
        elif body.startswith("{{tbox"):
            _, args = template_args(body)
            txt = [a for a in args if "=" not in a]
            out.append(f'{indent}~mesbox("{clean(txt[-1] if txt else body)}");')
        elif body.startswith("{{mes|"):
            out.append(f'{indent}mes("{clean(template_args(body)[1][0])}");')
        elif body.startswith("{{qact") or body.startswith("{{tselect") or body.startswith("{{tmissing"):
            if body.startswith("{{qact"):
                out.append(f"{indent}// TACT {clean('|'.join(template_args(body)[1]))}")
        elif body and not body.startswith("{{"):
            out.append(f'{indent}~mesbox("{clean(body)}");')
        else:
            out.append(f"{indent}// ? {clean(body)}")
        i += 1


def live_shop_labels() -> dict[str, str]:
    """Storefront title (as the wiki writes it in `opens=`) -> open label."""
    out = {}
    for path in glob.glob(os.path.join(CONTENT, "server", "scripts", "**", "*.rs2"), recursive=True):
        text = open(path, encoding="utf-8", errors="replace").read()
        for m in re.finditer(r"^\[label,(\w+_open)\]\s*\n~openshop\([^\n]*\"([^\"]+)\"\);", text, re.M):
            out[m.group(2)] = m.group(1)
            out[m.group(2).rstrip(".")] = m.group(1)
    return out


def page_label(page: str) -> str:
    return re.sub(r"_+", "_", re.sub(r"[^a-z0-9]+", "_", page.lower())).strip("_") + "_talk"


def draft_page(page: str, gamevals: list[str], ctx: dict) -> str:
    path = os.path.join(OUT_DIR, safe_filename(page) + ".wikitext")
    text = open(path, encoding="utf-8").read()
    rev = load_manifest().get(page, {}).get("revid", "?")
    sections: list[tuple[str, list[str]]] = [("", [])]
    for line in text.split("\n"):
        m = re.match(r"^(=+)\s*(.*?)\s*=+\s*$", line)
        if m:
            sections.append((m.group(2), []))
        else:
            sections[-1][1].append(line)
    sections = [(n, l) for n, l in sections if parse_items(l)]
    chosen = next((s for s in sections if not SKIP_SECTION_RE.search(s[0])), None)
    out = [
        f"// {page} -- https://oldschool.runescape.wiki/w/Transcript:{page.replace(' ', '_')}"
        f" (revision {rev}).",
    ]
    others = [n for n, _ in sections if chosen is None or n != chosen[0]]
    if others:
        out.append(f"// Not drafted: {'; '.join(clean(n) for n in others)}.")
    out += [f"[opnpc1,{g}] @{page_label(page)};" for g in gamevals]
    out += ["", f"[label,{page_label(page)}]"]
    if chosen:
        ctx["declared"] = set()
        emit(parse_items(chosen[1]), 1, "", out, ctx)
    return "\n".join(out) + "\n"


def draft(gamevals: list[str]) -> None:
    pages = load_pages()
    by_page: dict[str, list[str]] = {}
    for g in gamevals:
        if g not in pages:
            sys.exit(f"{g}: not resolved; run --fetch first")
        page = pages[g]["page"]
        if not page or not os.path.exists(os.path.join(OUT_DIR, safe_filename(page) + ".wikitext")):
            print(f"// ######## {g}: no transcript ({page or 'no wiki page states this id'})\n")
            continue
        by_page.setdefault(page, []).append(g)
    ctx = {"shops": live_shop_labels()}
    for page, gs in by_page.items():
        print(f"// ######## {' '.join(gs)}")
        print(draft_page(page, gs, ctx))


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fetch", action="store_true")
    ap.add_argument("--draft", action="store_true")
    ap.add_argument("--from-file")
    ap.add_argument("gamevals", nargs="*")
    args = ap.parse_args()
    gamevals = list(args.gamevals)
    if args.from_file:
        gamevals += [ln.split()[0] for ln in open(args.from_file) if ln.strip()]
    if args.fetch:
        fetch(gamevals)
    if args.draft:
        draft(gamevals)


if __name__ == "__main__":
    main()
