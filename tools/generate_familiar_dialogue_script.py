#!/usr/bin/env python3
"""Generate summoning_dialogue.rs2 from the checked-in familiar dialogue corpus.

The corpus (`docs/summoning_port/familiar_dialogue.json`, built once by
tools/build_familiar_dialogue_corpus.py) is the source of truth. This turns it
into ServerScript and nothing else — no text is authored here.

Two adaptations happen at this step, both forced by the rev-230 chat interface:

  * **Pagination.** The wiki records a familiar's whole speech as one bullet;
    this era's dialogue body is a single 479x67 component that wraps and then
    CLIPS at about four lines, and `mock230_send_if_settext` builds its packet
    in a 512-byte buffer. Pack yak's longest line is 675 characters. Long lines
    are therefore split at sentence boundaries into successive dialogue pages,
    which is what the player clicks through in the real game anyway.
  * **Character folding.** Curly quotes and ellipses are folded to ASCII, and
    angle brackets to parentheses, because `<` opens a colour tag in this
    client's text renderer.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
CORPUS = REPO / "docs/summoning_port/familiar_dialogue.json"
OUT = (
    REPO
    / "OSRS-Content/osrs239-content/server/scripts/ported_scape2009_summoning"
    / "scripts/summoning_dialogue.rs2"
)

# One dialogue page. The body is 479px wide, lineheight 16, 67px tall -> four
# lines; at this era's proportional font that is comfortably over 200 characters,
# so 200 leaves room for wide glyphs without ever reaching the clip.
PAGE_CHARS = 200

FOLD = {
    "‘": "'", "’": "'", "“": "'", "”": "'",
    "…": "...", "–": "-", "—": "-", " ": " ",
    "é": "e", "′": "'", "`": "'", "«": "'", "»": "'",
}


def fold(text: str) -> str:
    for bad, good in FOLD.items():
        text = text.replace(bad, good)
    text = text.replace("<", "(").replace(">", ")").replace('"', "'")
    return re.sub(r"\s+", " ", text).strip()


def paginate(text: str, budget: int = PAGE_CHARS) -> list[str]:
    """Split one spoken line into dialogue pages at sentence boundaries."""
    if len(text) <= budget:
        return [text]
    # Sentence ends, keeping the punctuation with the sentence it closes.
    pieces = re.findall(r".+?(?:[.!?]+[\"')\]]*\s+|$)", text, re.S)
    pages: list[str] = []
    current = ""
    for piece in pieces:
        piece = piece.strip()
        if not piece:
            continue
        if len(piece) > budget:
            # A single sentence longer than a page: fall back to words.
            if current:
                pages.append(current)
                current = ""
            words: list[str] = []
            length = 0
            for word in piece.split(" "):
                if length + len(word) + 1 > budget and words:
                    pages.append(" ".join(words))
                    words, length = [], 0
                words.append(word)
                length += len(word) + 1
            if words:
                current = " ".join(words)
            continue
        if current and len(current) + 1 + len(piece) > budget:
            pages.append(current)
            current = piece
        else:
            current = f"{current} {piece}".strip()
    if current:
        pages.append(current)
    return pages


def below_gate_line(familiar: dict) -> str:
    """What the familiar says before the player can understand it.

    Transcript:Spirit wolf is the only page that records the split, and what it
    shows is the same utterance with the translation withheld: 'Whurf?' below
    level 11, 'Whurf? (What are you doing?)' at 11+. Every other familiar gets
    that rule applied to its own first line rather than an invented line.
    """
    if familiar.get("below_gate"):
        return fold(familiar["below_gate"])
    for conversation in familiar["conversations"]:
        for line in conversation["lines"]:
            if line["who"] == "npc":
                return fold(re.sub(r"\s*\([^()]*\)\s*$", "", line["text"]).strip())
    return ""


def emit_lines(conversation: dict, indent: str) -> list[str]:
    out = []
    for line in conversation["lines"]:
        for page in paginate(fold(line["text"])):
            page = page.replace('"', "'")
            if line["who"] == "npc":
                out.append(
                    f'{indent}if (~summoning_familiar_say($familiar, "{page}") = false) '
                    f"return(true);"
                )
            else:
                out.append(f'{indent}~chatplayer("{page}");')
    return out


def guard_expression(guard: dict) -> str:
    if guard["kind"] == "bob_items":
        op = "<=" if guard["op"] == "<=" else "="
        return f"~summoning_familiar_bob_items {op} {guard['count']}"
    raise SystemExit(f"unknown guard kind {guard['kind']!r}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if the file is stale")
    args = parser.parse_args()

    corpus = json.loads(CORPUS.read_text(encoding="utf-8"))
    familiars = corpus["familiars"]
    meta = corpus["_meta"]

    body: list[str] = []
    body.append("// GENERATED FILE — do not edit by hand.")
    body.append("//")
    body.append("// Written by tools/generate_familiar_dialogue_script.py from")
    body.append("// docs/summoning_port/familiar_dialogue.json. Edit the corpus, or the")
    body.append("// generator, and re-run `make -C src summoning-dialogue`.")
    body.append("//")
    body.append("// Every line below is a RuneScape Wiki transcript of the real familiar")
    body.append(f"// dialogue ({meta['source']}, {meta['licence']}).")
    body.append("// The port needs them because 2009scape authors a conversation for spirit")
    body.append("// graahk alone and marks the line the other 77 fall back on as probably")
    body.append("// inauthentic — see docs/summoning_port/FAMILIAR_INTERACT.md.")
    body.append("//")
    body.append("// Each `~summoning_familiar_chat_<type>` returns false when it has nothing")
    body.append("// to say, which is how a familiar whose only recorded conversations are")
    body.append("// conditional falls back to the refusal line.")
    body.append("")

    # Dispatcher.
    body.append("[proc,summoning_familiar_chat](npc_uid $familiar, int $type)(boolean)")
    for type_id in sorted(familiars, key=int):
        entry = familiars[type_id]
        body.append(
            f"if ($type = {type_id}) return(~summoning_familiar_chat_{type_id}($familiar));"
            f"  // {entry['name']}"
        )
    body.append("return(false);")
    body.append("")

    # Below-gate utterance: the sound with the translation withheld.
    #
    # Boolean rather than string-returning so the one familiar with no audible
    # sound of its own falls back without the caller comparing strings.
    # Karamthulhu overlord is telepathic — every one of its lines is a
    # parenthetical thought, so stripping the translation leaves nothing, and
    # that is a real property of the familiar rather than a parse failure.
    body.append("// Below the gate the familiar is audible but not understood.")
    body.append("[proc,summoning_familiar_chat_untranslated](npc_uid $familiar, int $type)(boolean)")
    untranslated = 0
    for type_id in sorted(familiars, key=int):
        line = below_gate_line(familiars[type_id])
        if not line:
            body.append(
                f"// {familiars[type_id]['name']} has no untranslated sound: every line it"
            )
            body.append("// speaks is a parenthetical thought.")
            continue
        untranslated += 1
        page = paginate(line)[0]
        body.append(
            f'if ($type = {type_id}) return(~summoning_familiar_say($familiar, "{page}"));'
        )
    body.append("return(false);")
    body.append("")

    total_conversations = 0
    total_pages = 0
    for type_id in sorted(familiars, key=int):
        entry = familiars[type_id]
        conversations = entry["conversations"]
        total_conversations += len(conversations)
        guarded = [c for c in conversations if "guard" in c]
        pooled = [c for c in conversations if "guard" not in c]

        body.append(f"// {entry['name']} — {entry['url']}")
        if entry["conditional"]:
            body.append(
                f"// {len(entry['conditional'])} further conversation(s) on this page need "
                "world state"
            )
            body.append("// this lane does not model, and are held out of the pool:")
            for held in entry["conditional"]:
                body.append(f"//   - {held['condition']}")
        body.append(f"[proc,summoning_familiar_chat_{type_id}](npc_uid $familiar)(boolean)")

        for conversation in guarded:
            body.append(f"// {conversation['condition']}")
            body.append(f"if ({guard_expression(conversation['guard'])}) {{")
            emitted = emit_lines(conversation, "    ")
            total_pages += len(emitted)
            body.extend(emitted)
            body.append("    return(true);")
            body.append("}")

        if pooled:
            if len(pooled) == 1:
                emitted = emit_lines(pooled[0], "")
                total_pages += len(emitted)
                body.extend(emitted)
                body.append("return(true);")
            else:
                body.append(f"def_int $pick = random({len(pooled)});")
                for index, conversation in enumerate(pooled):
                    if conversation.get("ordering"):
                        body.append(
                            f"// source unlocks this one {conversation['ordering']}; "
                            "flattened into the pool"
                        )
                    body.append(f"if ($pick = {index}) {{")
                    emitted = emit_lines(conversation, "    ")
                    total_pages += len(emitted)
                    body.extend(emitted)
                    body.append("    return(true);")
                    body.append("}")
                body.append("return(true);")
        else:
            body.append("return(false);")
        body.append("")

    text = "\n".join(body).rstrip() + "\n"

    if args.check:
        if not OUT.exists() or OUT.read_text(encoding="utf-8") != text:
            print(
                "generate_familiar_dialogue_script: summoning_dialogue.rs2 is stale — "
                "run `make -C src summoning-dialogue`",
                file=sys.stderr,
            )
            return 1
        print("generate_familiar_dialogue_script: up to date")
        return 0

    OUT.write_text(text, encoding="utf-8")
    print(
        f"generate_familiar_dialogue_script: {len(familiars)} familiars, "
        f"{total_conversations} conversations, {total_pages} dialogue pages -> {OUT}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
