#!/usr/bin/env python3
"""Turn a quest run's artefacts into a verdict.

Red on, and only on, things that mean the test did not actually happen:

  * any row in ledger.tsv whose verdict is FAIL (a BLOCKED row is its own
    bucket -- see below, not a silent pass and not folded into FAIL),
  * a missing ledger, or one whose summary row disagrees with its rows,
  * a listed quest with no ledger at all -- run.py always creates
    build/quest_gate/<quest>/ before it launches a client, so this also
    catches a process that never got that far,
  * a step that claims a shot (its `shots` column) that is not on disk,
  * a shot that is on disk but undersized (a capture that raced the
    renderer -- less than 1000 bytes),
  * two shots with the same MD5, anywhere in one quest's shots/ directory --
    the failure that looks most like a pass: a screenshot per interaction,
    all of them identical, is a driver that never drove anything (the driver
    no longer WRITES a second copy of a frame it has already photographed --
    see UNCHANGED FRAMES below -- so this rule can no longer fire on a quest
    whose verbs simply did not move the screen, and what is left of it is the
    case it was written for),
  * a shot whose own top-left 64x64 corner matches the Character-Creator or
    pre-login fingerprint (tools/quest_gate/fingerprints/) -- a run that
    never actually got past boot/login can still write a ledger full of
    PASS rows if every verb it calls answers `refused`/`timeout` in a way
    the quest's own asserts do not catch; this catches it at the pixel
    level instead of trusting the ledger to have noticed,
  * (once a quest's source uses the scaffold that makes these meaningful --
    see MINIMUM SHAPE below) too few step rows or PNGs for how the run ended
    (the rule table below), no quest.* row when the source calls
    quest.bind, no quest.varp_complete row and no trailing BLOCKED row, or a
    non-setup row with no shot at all.

BLOCKED is counted separately from PASS/FAIL (phase 1: torirs_plugin_drive.c
writes `SUMMARY ... pass=N fail=M blocked=K`, the `blocked=K` token present
only when K>0). A quest whose ledger has fail==0 and at least one BLOCKED
row is reported "blocked", not "green" and not "RED": it is a known,
declared stub (`t.blocked(...)`, or `quest.expect_complete`'s own
`unsupported` row for a verb that has not landed yet), not a regression.
`gate.py` still exits non-zero for it BY DEFAULT -- a stub is not nothing,
CI should see it -- unless `--allow-blocked` is given, in which case a
blocked-but-not-failing quest exits 0.

UNCHANGED FRAMES. A screenshot request whose frame is byte-identical to the
last shot the run actually wrote is answered, not written: the file is
deleted again, the row's `shots` column stays EMPTY and its detail gains
`[frame unchanged]` (core.lua / torirs_plugin_drive_ui.c, 2026-09-19). That
is why the "every auto-shooting row carries a shot" rule below exempts a row
carrying that marker -- `t.exec` shoots after every verb, including the many
(`npc.await_present` on an npc already there, `inv.count`, a walk to the tile
the player is on) that change nothing on screen, and Sheep Herder was
rejected for four byte-identical consecutive PNGs its author could do
nothing about. The exemption is narrow on purpose: the marker is written by
the driver, from the bytes of a picture it really did take and really did
compare, so a row that carries it is a row whose capture SUCCEEDED -- the
rule's own target, a capture that silently failed, still has an empty shots
column and no marker, and still fails. A `<name>-FAIL` capture is never
suppressed, so a FAIL row always keeps its picture.

MINIMUM SHAPE. The rules about row/PNG counts, `quest.*` rows,
`quest.varp_complete`/BLOCKED, and "every non-setup row has a shot" describe
the NEW scaffold (`t.quest.bind{...}`, `t.exec(...)`) from
docs/QUEST_SUITE_KIT.md phase 2, which auto-shoots every row it writes
(core.lua's own `record_with_shot`) -- by construction, a well-formed
t.exec/t.check row can never trip the shot rule, so a shotless non-setup row
is a genuine capture failure, not a false positive waiting to happen. A
quest file that does not yet call `t.quest.bind`/`t.exec` (the two real
quests, cooks_assistant/hans, as of this pass -- hand-written against the
OLDER t.step/t.expect idiom, which never auto-shoots) has nothing these
rules describe, and enforcing them against that older idiom would flag
manually-written rows that were never wrong -- so each rule is gated on the
quest's OWN source file already using the verb it is checking, read once
per quest from test/quests/<quest>.lua (not from the ledger, which carries
no marker for which verb wrote a row).

The row/PNG minimum is smaller for a run that ends BLOCKED than for one
that ends green, because an honest `t.blocked()` reached three or four
steps into a quest cannot manufacture a fifth without padding (trap 15),
and the ledger's own trailing verdict already says the run stopped on
purpose rather than ran out of rows. Rule table (also in
docs/QUEST_AUTHORING.md section 7 -- re-read that file if this drifts):

    ledger's last row      | min step rows | min PNGs
    ----------------------- | -------------: | -------:
    BLOCKED                 |             4 |        2
    anything else (green)   |             8 |        4

  and, independent of which row of the table applies (unconditional on how
  the run ended, conditional only on the source calling `quest.bind` at
  all): at least one `quest.*` row.

Reads build/quest_gate/<quest>/ for every quest `run.py` would run (see
tools/quest_gate/quest_list.py) unless told to check only specific ones.

Usage:
  tools/quest_gate/gate.py [--all] [--allow-blocked]
  tools/quest_gate/gate.py <quest> [<quest> ...] [--allow-blocked]
"""

import argparse
import hashlib
import json
import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import ledger  # noqa: E402
import quest_list  # noqa: E402

MIN_SHOT_BYTES = 1000
FINGERPRINTS_DIR = os.path.join(HERE, "fingerprints")
FINGERPRINT_NAMES = ("character_creator", "pre_login")
# Measured separation (fingerprints/README.md): an ordinary in-world corner
# is the engine's flat clear colour, tens of levels away from either
# reference in every channel. A software-rendered headless frame does not
# introduce that much run-to-run noise, so this threshold has real headroom
# on both sides rather than being tuned to just barely pass today's shots.
FINGERPRINT_MATCH_THRESHOLD = 12.0
# See the rule table in the MINIMUM SHAPE banner section above: a run whose
# ledger ends on a BLOCKED row is held to the smaller pair, everything else
# (a green run) to the larger pair.
MIN_ROWS_GREEN = 8
MIN_ROWS_BLOCKED = 4
MIN_SHOTS_GREEN = 4
MIN_SHOTS_BLOCKED = 2
# core.lua's flush() folds this into the detail of a row whose capture was
# suppressed as an unchanged frame (see UNCHANGED FRAMES in the banner).
UNCHANGED_MARKER = "[frame unchanged]"


def artefact_dir(name):
    assert name
    return os.path.join(REPO_ROOT, "build", "quest_gate", name)


def parse_summary_counts(summary):
    """("pass=<n> fail=<m>[ blocked=<k>]" -> (n, m, k_or_None), or
    (None, None, None) if it cannot be read -- treated as its own
    disagreement, never as a pass by default. `blocked` is only present in
    the tuple when the SUMMARY row itself carried a `blocked=` token
    (torirs_plugin_drive.c only writes one when K>0)."""
    if summary is None or len(summary) < 6:
        return None, None, None
    text = summary[5]
    counts = {}
    for token in text.split():
        key, _, value = token.partition("=")
        if key in ("pass", "fail", "blocked"):
            try:
                counts[key] = int(value)
            except ValueError:
                return None, None, None
    return counts.get("pass"), counts.get("fail"), counts.get("blocked")


def md5_of(path):
    digest = hashlib.md5()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


# --------------------------------------------------------------- PNG corner
#
# Enough of the PNG format to read the top-left NxN pixels of the RGB,
# 8-bit, non-interlaced files this project's own screenshot writer produces
# (app_plugin_assets.c: tdefl_write_image_to_png_file_in_memory_ex(...,
# num_channels=3, ...)) -- stdlib + zlib only, no Pillow, matching
# fingerprints/capture_fingerprints.py's OWN copy of this reader (kept in
# sync by hand; verified byte-identical against an independent decode --
# sips -- of the same capture, 2026-09-19).

def _png_chunks(data):
    i = 8
    while i < len(data):
        length = struct.unpack(">I", data[i:i + 4])[0]
        ctype = data[i + 4:i + 8]
        cdata = data[i + 8:i + 8 + length]
        i += 12 + length
        yield ctype, cdata
        if ctype == b"IEND":
            break


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def _unfilter_row(ftype, raw, prev, bpp):
    n = len(raw)
    recon = bytearray(n)
    for i in range(n):
        x = raw[i]
        a = recon[i - bpp] if i >= bpp else 0
        b = prev[i] if prev is not None else 0
        c = prev[i - bpp] if (prev is not None and i >= bpp) else 0
        if ftype == 0:
            v = x
        elif ftype == 1:
            v = x + a
        elif ftype == 2:
            v = x + b
        elif ftype == 3:
            v = x + (a + b) // 2
        elif ftype == 4:
            v = x + _paeth(a, b, c)
        else:
            raise ValueError("unknown PNG filter type %d" % ftype)
        recon[i] = v & 0xFF
    return bytes(recon)


def read_png_corner(path, size=64):
    """{"width","height","channels","block_w","block_h","rows"} for the
    top-left size x size pixels of `path` -- None on anything this reader
    does not understand (an unsupported colour type/bit depth/interlace),
    which check_quest treats as "cannot compare", not as a match or a
    finding on its own; the ordinary shot-integrity checks already cover a
    file that is not a real PNG at all."""
    assert path
    try:
        with open(path, "rb") as handle:
            data = handle.read()
    except OSError:
        return None
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    width = height = bitdepth = colortype = None
    idat = bytearray()
    for ctype, cdata in _png_chunks(data):
        if ctype == b"IHDR":
            if len(cdata) < 13:
                return None
            width, height, bitdepth, colortype, compression, filt, interlace = \
                struct.unpack(">IIBBBBB", cdata[:13])
            if compression != 0 or filt != 0 or interlace != 0:
                return None
        elif ctype == b"IDAT":
            idat += cdata
    if not width or not height:
        return None
    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(colortype)
    if not channels or bitdepth != 8:
        return None
    try:
        raw = zlib.decompress(bytes(idat))
    except zlib.error:
        return None
    stride = 1 + width * channels
    rows_needed = min(size, height)
    needed_bytes = min(size, width) * channels
    if len(raw) < rows_needed * stride:
        return None
    prev = None
    rows = []
    for r in range(rows_needed):
        start = r * stride
        row = _unfilter_row(raw[start], raw[start + 1:start + 1 + needed_bytes], prev, channels)
        rows.append(row)
        prev = row
    return {"width": width, "height": height, "channels": channels,
            "block_w": min(size, width), "block_h": rows_needed, "rows": rows}


_FINGERPRINT_CACHE = {}


def load_fingerprint(name):
    """The stored reference block, decoded from its rows_hex once and
    cached -- gate.py may check dozens of shots across a suite run."""
    if name in _FINGERPRINT_CACHE:
        return _FINGERPRINT_CACHE[name]
    path = os.path.join(FINGERPRINTS_DIR, "%s.json" % name)
    with open(path, "r", encoding="utf-8") as handle:
        payload = json.load(handle)
    payload["rows"] = [bytes.fromhex(row) for row in payload["rows_hex"]]
    _FINGERPRINT_CACHE[name] = payload
    return payload


def fingerprint_distance(corner, fingerprint):
    """Mean absolute byte difference over the region both blocks actually
    have in common -- a window/shot size that differs slightly from the
    fingerprint's own capture still compares meaningfully."""
    rows_n = min(corner["block_h"], fingerprint["block_h"])
    if rows_n == 0:
        return None
    total = 0
    count = 0
    for r in range(rows_n):
        a = corner["rows"][r]
        b = fingerprint["rows"][r]
        n = min(len(a), len(b))
        if n == 0:
            continue
        total += sum(abs(a[i] - b[i]) for i in range(n))
        count += n
    if count == 0:
        return None
    return total / count


def matches_boot_fingerprint(shot_path):
    """The name of the fingerprint `shot_path` matches within
    FINGERPRINT_MATCH_THRESHOLD, or None. Silently None (not a finding on
    its own) when the shot cannot be decoded at all -- the shot-integrity
    checks in check_quest already flag a missing/undersized/corrupt file;
    this function's job is only the pixel comparison."""
    corner = read_png_corner(shot_path)
    if corner is None:
        return None
    for name in FINGERPRINT_NAMES:
        fingerprint = load_fingerprint(name)
        distance = fingerprint_distance(corner, fingerprint)
        if distance is not None and distance <= FINGERPRINT_MATCH_THRESHOLD:
            return name
    return None


# ---------------------------------------------------------- minimum shape
#
# Whether a quest's OWN SOURCE FILE uses the verb each rule is about -- see
# the module banner ("MINIMUM SHAPE") for why this is read from the source
# and not inferred from the ledger.

def _quest_source_text(name):
    path = quest_list.quest_path(REPO_ROOT, name)
    if not os.path.isfile(path):
        return ""
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        return handle.read()


def _long_bracket_level(text, index):
    """The level of the Lua long bracket opening at `text[index]` (`[[` is 0,
    `[==[` is 2), or None when nothing opens there."""
    if index >= len(text) or text[index] != "[":
        return None
    cursor = index + 1
    level = 0
    while cursor < len(text) and text[cursor] == "=":
        level += 1
        cursor += 1
    if cursor < len(text) and text[cursor] == "[":
        return level
    return None


def _long_bracket_end(text, index, level):
    """The index just past the matching `]]`/`]==]`, or len(text) when the
    bracket is never closed (a truncated file, not this function's problem)."""
    closer = "]" + "=" * level + "]"
    found = text.find(closer, index + 2 + level)
    if found == -1:
        return len(text)
    return found + len(closer)


def strip_lua_comments(source_text):
    """`source_text` with every Lua comment replaced by a space, and every
    string literal left alone.

    Why this exists: the two rules below switch on whether a quest file USES
    `quest.bind`/`t.exec`, and a raw substring search answers yes to a file
    that only NAMES the verb in a comment. That is not a hypothetical --
    test/quests/cooks_assistant.lua's own banner, explaining which idiom it
    was written in, flipped both rules on and drove the file off the idiom to
    get out from under them (2026-09-19, phase 3 review). A comment is prose;
    only code counts."""
    out = []
    index = 0
    length = len(source_text)
    while index < length:
        char = source_text[index]
        if char in ("'", '"'):
            quote = char
            out.append(char)
            index += 1
            while index < length:
                current = source_text[index]
                out.append(current)
                index += 1
                if current == "\\" and index < length:
                    out.append(source_text[index])
                    index += 1
                elif current == quote:
                    break
            continue
        if char == "[":
            level = _long_bracket_level(source_text, index)
            if level is not None:
                # A long STRING -- kept verbatim, comments only start at `--`.
                end = _long_bracket_end(source_text, index, level)
                out.append(source_text[index:end])
                index = end
                continue
        if char == "-" and source_text.startswith("--", index):
            level = _long_bracket_level(source_text, index + 2)
            if level is not None:
                index = _long_bracket_end(source_text, index + 2, level)
            else:
                while index < length and source_text[index] != "\n":
                    index += 1
            out.append(" ")
            continue
        out.append(char)
        index += 1
    return "".join(out)


def _uses_quest_bind(source_text):
    return "quest.bind" in strip_lua_comments(source_text)


# The two verbs that shoot a row by themselves (core.lua's record_with_shot):
# `t.exec(name, verb, ...)` and `t.check(name, ...)`. Both are TOKEN matches:
# the root table is `t` and the verb is an ordinary Lua Name, so the pattern
# bounds BOTH ends -- `(?<![\w.])t` refuses `quest_t.exec` and a preceding
# `foo.t`, and `exec(`/`check(` must be the whole name before the call's own
# paren. The text they are matched against has already had its comments
# stripped (strip_lua_comments, above), so naming the verb in prose switches
# nothing on. Only a row NAME that is a plain string literal is collected: a
# name built by concatenation is not one this gate can attribute to a row, and
# a rule that cannot name its row does not get to fail one.
_DO_CALL_RE = re.compile(r"""(?<![\w.])t\s*\.\s*exec\s*\(\s*(["'])([^"']*)\1""")
_CHECK_CALL_RE = re.compile(r"""(?<![\w.])t\s*\.\s*check\s*\(\s*(["'])([^"']*)\1""")
# core.lua appends `-2`, `-3`, ... to a row name repeated within one run.
_REPEAT_SUFFIX_RE = re.compile(r"-\d+$")


def shooting_row_names(source_text):
    """Every ledger row name this source writes through an auto-shooting verb.

    PER ROW, not per file: the old rule was "this file mentions t.exec
    somewhere, so EVERY non-setup/non-quest row must carry a shot", which
    fails rows written with `t.expect` -- a verb that has never shot
    anything and was never supposed to. A row only owes a shot when the
    source actually wrote it with a verb that takes one."""
    code = strip_lua_comments(source_text)
    names = set()
    for match in _DO_CALL_RE.finditer(code):
        names.add(match.group(2))
    for match in _CHECK_CALL_RE.finditer(code):
        names.add(match.group(2))
    return names


def minimum_shape_findings(name, rows, shots_dir):
    findings = []
    source_text = _quest_source_text(name)
    checks_stage = _uses_quest_bind(source_text)
    shooting_rows = shooting_row_names(source_text)

    # The rule table (module banner, MINIMUM SHAPE): a run whose ledger ends
    # on a BLOCKED row is held to the smaller pair -- an honest t.blocked()
    # three or four steps in cannot reach the green minimum without padding.
    trailing_blocked = bool(rows) and rows[-1]["verdict"] == "BLOCKED"
    min_rows = MIN_ROWS_BLOCKED if trailing_blocked else MIN_ROWS_GREEN
    min_shots = MIN_SHOTS_BLOCKED if trailing_blocked else MIN_SHOTS_GREEN

    if len(rows) < min_rows:
        findings.append("only %d step row(s), need >= %d (minimum shape, %s run)" % (
            len(rows), min_rows, "BLOCKED" if trailing_blocked else "green"))

    if checks_stage:
        if not any(row["step"].startswith("quest.") for row in rows):
            findings.append("uses quest.bind but has no quest.* row (e.g. "
                             "quest.expect_stage/quest.varp_complete) -- minimum shape "
                             "requires one")
        has_varp_complete = any(row["step"] == "quest.varp_complete" and row["verdict"] == "PASS"
                                 for row in rows)
        if not (has_varp_complete or trailing_blocked):
            findings.append("uses quest.bind but has no passing quest.varp_complete row, "
                             "and the last row is not BLOCKED (a tier-4 stub must end on "
                             "a BLOCKED row)")

    png_count = 0
    if os.path.isdir(shots_dir):
        png_count = sum(1 for entry in os.listdir(shots_dir) if entry.endswith(".png"))
    if png_count < min_shots:
        findings.append("only %d PNG(s) under shots/, need >= %d (%s run)" % (
            png_count, min_shots, "BLOCKED" if trailing_blocked else "green"))

    for row in rows:
        step = row["step"]
        if step not in shooting_rows and _REPEAT_SUFFIX_RE.sub("", step) not in shooting_rows:
            continue
        if row["shots"]:
            continue
        # The capture happened and was identical to the previous one, so the
        # driver kept the picture it already had (UNCHANGED FRAMES, above).
        # That is an empty shots column with a reason attached, and the reason
        # is written by the driver rather than by the quest file.
        if UNCHANGED_MARKER in row["detail"]:
            continue
        findings.append("step %r is written with t.exec/t.check, which always "
                         "shoots, but has no shot recorded and no %s in its "
                         "detail -- an empty shots column there means the "
                         "capture itself failed" % (step, UNCHANGED_MARKER))

    return findings


def check_quest(name, allow_blocked):
    """(findings, blocked) for one quest -- findings is every RED-level
    problem (as plain strings); blocked is every BLOCKED row's own
    (step, detail) description, kept separate so a declared stub is never
    silently graded the same as a real defect (see the module banner)."""
    findings = []
    blocked = []
    directory = artefact_dir(name)
    ledger_path = os.path.join(directory, "ledger.tsv")
    shots_dir = os.path.join(directory, "shots")
    rows, summary = ledger.read(ledger_path)

    if rows is None:
        findings.append("no ledger.tsv at %s" % ledger_path)
        return findings, blocked

    if not rows:
        findings.append("ledger.tsv has no step rows")

    for row in rows:
        if row["verdict"] == "BLOCKED":
            blocked.append("%s: %s" % (row["step"], row["detail"]))
        elif row["verdict"] != "PASS":
            findings.append("step %r: verdict %s -- %s" % (
                row["step"], row["verdict"], row["detail"]))

    if summary is None:
        findings.append("ledger.tsv has no trailing SUMMARY row")
    else:
        counted_pass = sum(1 for row in rows if row["verdict"] == "PASS")
        counted_fail = sum(1 for row in rows if row["verdict"] == "FAIL")
        counted_blocked = sum(1 for row in rows if row["verdict"] == "BLOCKED")
        summary_pass, summary_fail, summary_blocked = parse_summary_counts(summary)
        if summary_pass is None or summary_fail is None:
            findings.append("SUMMARY row's pass/fail counts could not be read: %s"
                             % "\t".join(summary))
        else:
            if (summary_pass, summary_fail) != (counted_pass, counted_fail):
                findings.append(
                    "SUMMARY row disagrees with its own rows: SUMMARY says "
                    "pass=%d fail=%d, the rows say pass=%d fail=%d"
                    % (summary_pass, summary_fail, counted_pass, counted_fail))
            # `blocked=K` is only ever written when K>0 (phase 1) -- so a
            # missing token is only a disagreement if the rows actually
            # counted a BLOCKED one; a present token must match exactly.
            effective_summary_blocked = summary_blocked if summary_blocked is not None else 0
            if effective_summary_blocked != counted_blocked:
                findings.append(
                    "SUMMARY row disagrees on blocked count: SUMMARY says blocked=%s, "
                    "the rows say blocked=%d" % (summary_blocked, counted_blocked))
            elif summary_blocked == 0:
                findings.append("SUMMARY row carries \"blocked=0\" -- only write the "
                                 "token when K>0")
        summary_verdict = summary[2] if len(summary) > 2 else None
        expected_verdict = "PASS" if counted_fail == 0 else "FAIL"
        if summary_verdict != expected_verdict:
            findings.append(
                "SUMMARY row's own verdict is %s but its rows say %s"
                % (summary_verdict, expected_verdict))

    for row in rows:
        for shot_name in ledger.shot_names(row):
            shot_path = os.path.join(shots_dir, "%s.png" % shot_name)
            if not os.path.isfile(shot_path):
                findings.append("step %r claims shot %r, which is not on disk (%s)"
                                 % (row["step"], shot_name, shot_path))
                continue
            size = os.path.getsize(shot_path)
            if size < MIN_SHOT_BYTES:
                findings.append("step %r's shot %r is only %d byte(s) (< %d)"
                                 % (row["step"], shot_name, size, MIN_SHOT_BYTES))

    if os.path.isdir(shots_dir):
        by_digest = {}
        for entry in sorted(os.listdir(shots_dir)):
            if not entry.endswith(".png"):
                continue
            path = os.path.join(shots_dir, entry)
            by_digest.setdefault(md5_of(path), []).append(entry)
            matched = matches_boot_fingerprint(path)
            if matched:
                findings.append("shot %r matches the %s fingerprint -- this run never "
                                 "actually got past boot/login" % (entry, matched))
        for digest, names in sorted(by_digest.items()):
            if len(names) > 1:
                findings.append("%d shots share one MD5 (%s), a screenshot that never "
                                 "changed: %s" % (len(names), digest, ", ".join(names)))

    findings.extend(minimum_shape_findings(name, rows, shots_dir))

    return findings, blocked


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("quests", nargs="*", help="check only these quests (default: --all)")
    parser.add_argument("--all", action="store_true", help="check every discovered quest")
    parser.add_argument("--allow-blocked", action="store_true",
                        help="accept a quest whose ledger has BLOCKED row(s) and no FAIL "
                             "row (fail==0) -- without this flag such a quest still exits "
                             "non-zero, reported \"blocked\" rather than \"RED\"")
    arguments = parser.parse_args()

    if arguments.quests:
        names = arguments.quests
    else:
        names = quest_list.discover(REPO_ROOT)
        if not names:
            # An empty suite is a discovery FAILURE, not an empty pass: it is
            # indistinguishable, from here, from test/quests/ having been
            # wiped or misconfigured, and CI must not read that as green.
            print("gate: no quest files under test/quests/ -- nothing to check", file=sys.stderr)
            return 1

    total_findings = 0
    any_unaccepted_blocked = False
    any_accepted_blocked = False
    for name in names:
        findings, blocked = check_quest(name, arguments.allow_blocked)
        if findings:
            status = "RED"
        elif blocked:
            status = "blocked" if arguments.allow_blocked else "RED"
        else:
            status = "green"
        print("%-24s %s" % (name, status))
        for finding in findings:
            print("    - %s" % finding)
        for row in blocked:
            print("    ~ blocked: %s" % row)
        total_findings += len(findings)
        if blocked and not findings:
            if arguments.allow_blocked:
                any_accepted_blocked = True
            else:
                any_unaccepted_blocked = True

    print("")
    if total_findings or any_unaccepted_blocked:
        if total_findings:
            print("gate: %d finding(s) across %d quest(s)" % (total_findings, len(names)),
                  file=sys.stderr)
        if any_unaccepted_blocked:
            print("gate: blocked row(s) present without --allow-blocked", file=sys.stderr)
        return 1
    if any_accepted_blocked:
        print("gate: %d quest(s) accepted (--allow-blocked covers a BLOCKED row in at "
              "least one)" % len(names))
    else:
        print("gate: %d quest(s) green" % len(names))
    return 0


if __name__ == "__main__":
    sys.exit(main())
