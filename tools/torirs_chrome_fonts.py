#!/usr/bin/env python3
"""Build browser fonts from ToriRSChrome's baked cache-font masks.

The source fonts are deliberately not re-read from a cache here.  The C bake is
the byte-for-byte font source used by the retained native chrome, and the
metrics header is its layout contract.  Converting those checked-in artifacts
keeps browser and native chrome on the same glyphs, advances and baseline.

Each opaque source pixel becomes an axis-aligned TrueType outline cell.  At the
authored CSS size (10px for Small and 12px for Body/Menu), one source pixel is
exactly one CSS pixel because every source pixel is 64 font units.  Adjacent
equal row-runs are coalesced to keep the outline fonts small.

Requires fontTools 4.59.2.  It is a generation-only dependency; no browser or
application runtime depends on Python or fontTools.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence

EXPECTED_FONTTOOLS_VERSION = "4.59.2"
FONT_UNITS_PER_PIXEL = 64
MAC_EPOCH_AT_UNIX_EPOCH = 2_082_844_800

# ToriDraw's 94 cache glyph slots.  The bake currently carries an empty final
# slot for vertical bar; ordinary space and all unmapped bytes use the separate
# advance-only metric.  Backtick is not a cache glyph, so the browser cmap maps
# it to an empty glyph with that same advance.  This still gives the browser a
# complete printable-ASCII cmap without inventing pixels the game font lacks.
SOURCE_CODEPOINTS = tuple(
    map(
        ord,
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789"
        "!\"\u00a3$%^&*()-_=+[{]};:'@#~,<.>/?\\|",
    )
)
if len(SOURCE_CODEPOINTS) != 94 or len(set(SOURCE_CODEPOINTS)) != 94:
    raise AssertionError("the baked ToriDraw charset must contain 94 unique slots")


@dataclass(frozen=True)
class SourceFont:
    symbol: str
    archive_id: int
    family: str
    style: str
    weight: int
    css_size: int
    line_height: int
    line_box: int
    glyph_bits: bytes
    widths: tuple[int, ...]
    heights: tuple[int, ...]
    offset_x: tuple[int, ...]
    offset_y: tuple[int, ...]
    advances: tuple[int, ...]


FONT_SPECS = {
    "Small": {
        "archive_id": 494,
        "family": "ToriRS Chrome Small",
        "style": "Regular",
        "weight": 400,
        "css_size": 10,
        "stem": "ToriRSSmall",
    },
    "Body": {
        "archive_id": 495,
        "family": "ToriRS Chrome",
        "style": "Regular",
        "weight": 400,
        "css_size": 12,
        "stem": "ToriRSBody",
    },
    "Menu": {
        "archive_id": 496,
        "family": "ToriRS Chrome",
        "style": "Bold",
        "weight": 700,
        "css_size": 12,
        "stem": "ToriRSMenu",
    },
}


def _numbers(initializer: str) -> tuple[int, ...]:
    return tuple(int(value) for value in re.findall(r"-?\d+", initializer))


def _field(struct_body: str, name: str) -> tuple[int, ...]:
    match = re.search(rf"\.{re.escape(name)}\s*=\s*\{{(.*?)\}}", struct_body, re.S)
    if not match:
        raise ValueError(f"missing .{name} initializer")
    return _numbers(match.group(1))


def _define(metrics_text: str, symbol: str, suffix: str) -> int:
    match = re.search(
        rf"^#define\s+ToriRSChromeFont_{re.escape(symbol)}_{suffix}\s+(\d+)\s*$",
        metrics_text,
        re.M,
    )
    if not match:
        raise ValueError(f"missing {symbol}_{suffix} in metrics header")
    return int(match.group(1))


def parse_source_font(c_text: str, metrics_text: str, symbol: str) -> SourceFont:
    spec = FONT_SPECS[symbol]
    bits_match = re.search(
        rf"static const unsigned char {re.escape(symbol)}_glyph_bits\[(\d+)\]\s*=\s*"
        r"\{(.*?)\n\};",
        c_text,
        re.S,
    )
    if not bits_match:
        raise ValueError(f"missing {symbol}_glyph_bits")
    declared_bit_count = int(bits_match.group(1))
    bit_values = _numbers(bits_match.group(2))
    if len(bit_values) != declared_bit_count or any(value not in (0, 255) for value in bit_values):
        raise ValueError(f"invalid {symbol} glyph mask ({len(bit_values)}/{declared_bit_count})")

    struct_match = re.search(
        rf"static struct ToriDraw_Font {re.escape(symbol)}_font\s*=\s*\{{(.*?)\n\}};",
        c_text,
        re.S,
    )
    if not struct_match:
        raise ValueError(f"missing {symbol}_font")
    body = struct_match.group(1)
    widths = _field(body, "glyph_width")
    heights = _field(body, "glyph_height")
    offset_x = _field(body, "offset_x")
    offset_y = _field(body, "offset_y")
    advances = _field(body, "advance")
    line_height_match = re.search(r"\.line_height\s*=\s*(\d+)", body)
    if not line_height_match:
        raise ValueError(f"missing {symbol} line height")
    line_height = int(line_height_match.group(1))

    for field_name, values in (
        ("glyph_width", widths),
        ("glyph_height", heights),
        ("offset_x", offset_x),
        ("offset_y", offset_y),
    ):
        if len(values) != len(SOURCE_CODEPOINTS):
            raise ValueError(f"{symbol} {field_name} has {len(values)} values, expected 94")
    if len(advances) != len(SOURCE_CODEPOINTS) + 1:
        raise ValueError(f"{symbol} advance has {len(advances)} values, expected 95")
    if sum(width * height for width, height in zip(widths, heights)) != len(bit_values):
        raise ValueError(f"{symbol} glyph dimensions do not consume the mask blob")

    # Check the emitted pointers too.  This catches a reordered or sparse bake
    # that could otherwise be silently interpreted as a concatenated blob.
    alpha_match = re.search(r"\.glyph_alpha\s*=\s*\{(.*?)\}", body, re.S)
    if not alpha_match:
        raise ValueError(f"missing {symbol} glyph_alpha")
    pointer_tokens = re.findall(
        rf"{re.escape(symbol)}_glyph_bits\s*\+\s*(\d+)|\b(NULL)\b",
        alpha_match.group(1),
    )
    pointers: list[int | None] = [int(offset) if offset else None for offset, null in pointer_tokens]
    if len(pointers) != len(SOURCE_CODEPOINTS):
        raise ValueError(f"{symbol} glyph_alpha has {len(pointers)} values, expected 94")
    cursor = 0
    for index, (pointer, width, height) in enumerate(zip(pointers, widths, heights)):
        expected = cursor if width > 0 and height > 0 else None
        if pointer != expected:
            raise ValueError(
                f"{symbol} glyph {index} mask offset is {pointer}, expected {expected}"
            )
        cursor += width * height

    metric_line_height = _define(metrics_text, symbol, "LINE_HEIGHT")
    line_box = _define(metrics_text, symbol, "LINE_BOX")
    derived_line_box = max(y + height for y, height in zip(offset_y, heights))
    if line_height != metric_line_height:
        raise ValueError(f"{symbol} C/header line-height mismatch")
    if line_box != derived_line_box:
        raise ValueError(f"{symbol} line-box mismatch ({line_box}/{derived_line_box})")
    if line_box < line_height:
        raise ValueError(f"{symbol} line box is above its baseline")

    return SourceFont(
        symbol=symbol,
        archive_id=int(spec["archive_id"]),
        family=str(spec["family"]),
        style=str(spec["style"]),
        weight=int(spec["weight"]),
        css_size=int(spec["css_size"]),
        line_height=line_height,
        line_box=line_box,
        glyph_bits=bytes(bit_values),
        widths=widths,
        heights=heights,
        offset_x=offset_x,
        offset_y=offset_y,
        advances=advances,
    )


def _row_runs(row: bytes) -> tuple[tuple[int, int], ...]:
    runs: list[tuple[int, int]] = []
    start: int | None = None
    for x, alpha in enumerate(row):
        if alpha and start is None:
            start = x
        elif not alpha and start is not None:
            runs.append((start, x))
            start = None
    if start is not None:
        runs.append((start, len(row)))
    return tuple(runs)


def _mask_rectangles(mask: bytes, width: int, height: int) -> tuple[tuple[int, int, int, int], ...]:
    """Return non-overlapping x0,y0,x1,y1 rectangles in mask coordinates."""
    active: dict[tuple[int, int], int] = {}
    rectangles: list[tuple[int, int, int, int]] = []
    for y in range(height + 1):
        runs = set(_row_runs(mask[y * width : (y + 1) * width])) if y < height else set()
        for run, start_y in tuple(active.items()):
            if run not in runs:
                rectangles.append((run[0], start_y, run[1], y))
                del active[run]
        for run in runs:
            if run not in active:
                active[run] = y
    return tuple(sorted(rectangles, key=lambda item: (item[1], item[0], item[3], item[2])))


def _source_masks(font: SourceFont) -> tuple[bytes, ...]:
    masks: list[bytes] = []
    cursor = 0
    for width, height in zip(font.widths, font.heights):
        size = width * height
        masks.append(font.glyph_bits[cursor : cursor + size])
        cursor += size
    if cursor != len(font.glyph_bits):
        raise AssertionError("unconsumed glyph data")
    return tuple(masks)


def _postscript_name(family: str, style: str) -> str:
    compact_family = re.sub(r"[^A-Za-z0-9]", "", family)
    compact_style = re.sub(r"[^A-Za-z0-9]", "", style)
    return f"{compact_family}-{compact_style}"


def _fonttools_modules():
    try:
        import fontTools
        from fontTools.fontBuilder import FontBuilder
        from fontTools.pens.ttGlyphPen import TTGlyphPen
        from fontTools.ttLib import TTFont
        from fontTools.ttLib.tables.O_S_2f_2 import Panose
    except ImportError as error:
        raise SystemExit(
            "fontTools is required to generate browser fonts. Install the pinned generator "
            f"dependency with: python3 -m pip install fonttools=={EXPECTED_FONTTOOLS_VERSION}"
        ) from error
    return fontTools, FontBuilder, TTGlyphPen, TTFont, Panose


def build_ttf(font: SourceFont, destination: Path, source_digest: str, allow_version: bool) -> None:
    font_tools, FontBuilder, TTGlyphPen, _TTFont, Panose = _fonttools_modules()
    if font_tools.__version__ != EXPECTED_FONTTOOLS_VERSION and not allow_version:
        raise SystemExit(
            f"fontTools {font_tools.__version__} is installed; reproducible output requires "
            f"{EXPECTED_FONTTOOLS_VERSION} (or pass --allow-fonttools-version)"
        )

    units_per_em = font.css_size * FONT_UNITS_PER_PIXEL
    ascent = font.line_height * FONT_UNITS_PER_PIXEL
    descent = -(font.line_box - font.line_height) * FONT_UNITS_PER_PIXEL
    masks = _source_masks(font)
    source_index = {codepoint: index for index, codepoint in enumerate(SOURCE_CODEPOINTS)}
    codepoints = tuple(range(0x20, 0x7F)) + (0x00A3,)

    glyph_order = [".notdef"]
    glyph_name_by_codepoint: dict[int, str] = {}
    glyphs = {}
    horizontal_metrics: dict[str, tuple[int, int]] = {}

    notdef_pen = TTGlyphPen(None)
    glyphs[".notdef"] = notdef_pen.glyph()
    horizontal_metrics[".notdef"] = (font.advances[-1] * FONT_UNITS_PER_PIXEL, 0)

    for codepoint in codepoints:
        if codepoint == 0x20:
            glyph_name = "space"
        elif codepoint == 0x60:
            glyph_name = "grave"
        elif codepoint == 0x7C:
            glyph_name = "bar"
        elif codepoint == 0x00A3:
            glyph_name = "sterling"
        elif 0x21 <= codepoint <= 0x7E:
            glyph_name = f"uni{codepoint:04X}"
        else:
            glyph_name = f"uni{codepoint:04X}"
        glyph_name_by_codepoint[codepoint] = glyph_name
        glyph_order.append(glyph_name)

        pen = TTGlyphPen(None)
        source_glyph = source_index.get(codepoint)
        rectangles: tuple[tuple[int, int, int, int], ...] = ()
        if source_glyph is not None and font.widths[source_glyph] and font.heights[source_glyph]:
            rectangles = _mask_rectangles(
                masks[source_glyph], font.widths[source_glyph], font.heights[source_glyph]
            )
            if sum((x1 - x0) * (y1 - y0) for x0, y0, x1, y1 in rectangles) != sum(
                bool(alpha) for alpha in masks[source_glyph]
            ):
                raise AssertionError(f"{font.symbol} U+{codepoint:04X} outline lost mask pixels")
            for x0, y0, x1, y1 in rectangles:
                left = (font.offset_x[source_glyph] + x0) * FONT_UNITS_PER_PIXEL
                right = (font.offset_x[source_glyph] + x1) * FONT_UNITS_PER_PIXEL
                top = (font.line_height - font.offset_y[source_glyph] - y0) * FONT_UNITS_PER_PIXEL
                bottom = (font.line_height - font.offset_y[source_glyph] - y1) * FONT_UNITS_PER_PIXEL
                # Clockwise contours, as recommended for filled TrueType outlines.
                pen.moveTo((left, bottom))
                pen.lineTo((left, top))
                pen.lineTo((right, top))
                pen.lineTo((right, bottom))
                pen.closePath()
        glyphs[glyph_name] = pen.glyph()

        if source_glyph is None:
            advance_px = font.advances[-1]
            lsb = 0
        else:
            advance_px = font.advances[source_glyph]
            opaque_columns = [rect[0] for rect in rectangles]
            lsb = (
                (font.offset_x[source_glyph] + min(opaque_columns)) * FONT_UNITS_PER_PIXEL
                if opaque_columns
                else 0
            )
        horizontal_metrics[glyph_name] = (advance_px * FONT_UNITS_PER_PIXEL, lsb)

    builder = FontBuilder(units_per_em, isTTF=True)
    builder.setupGlyphOrder(glyph_order)
    builder.setupCharacterMap(glyph_name_by_codepoint)
    builder.setupGlyf(glyphs)
    builder.setupHorizontalMetrics(horizontal_metrics)
    builder.setupHorizontalHeader(ascent=ascent, descent=descent, lineGap=0)

    ps_name = _postscript_name(font.family, font.style)
    full_name = font.family if font.style == "Regular" else f"{font.family} {font.style}"
    version = "Version 1.000"
    unique = f"ToriRS:{ps_name}:1.000:{source_digest[:16]}"
    builder.setupNameTable(
        {
            "familyName": font.family,
            "styleName": font.style,
            "uniqueFontIdentifier": unique,
            "fullName": full_name,
            "psName": ps_name,
            "version": version,
            "manufacturer": "ToriRS",
            "designer": "Jagex cache font; browser conversion by ToriRS",
            "description": (
                f"ToriRSChrome {font.symbol} pixel-outline font from OSRS cache archive "
                f"{font.archive_id}."
            ),
        }
    )

    panose = Panose()
    panose.bFamilyType = 2
    panose.bSerifStyle = 11
    panose.bWeight = 8 if font.weight >= 700 else 5
    panose.bProportion = 3
    panose.bContrast = 9
    panose.bStrokeVariation = 2
    panose.bArmStyle = 2
    panose.bLetterForm = 2
    panose.bMidline = 2
    panose.bXHeight = 3
    builder.setupOS2(
        version=3,
        usWeightClass=font.weight,
        usWidthClass=5,
        fsType=0,
        ySubscriptXSize=units_per_em * 2 // 3,
        ySubscriptYSize=units_per_em * 2 // 3,
        ySubscriptXOffset=0,
        ySubscriptYOffset=units_per_em // 7,
        ySuperscriptXSize=units_per_em * 2 // 3,
        ySuperscriptYSize=units_per_em * 2 // 3,
        ySuperscriptXOffset=0,
        ySuperscriptYOffset=units_per_em * 2 // 5,
        yStrikeoutSize=FONT_UNITS_PER_PIXEL,
        yStrikeoutPosition=ascent * 7 // 10,
        sFamilyClass=0,
        panose=panose,
        ulCodePageRange1=1,
        ulCodePageRange2=0,
        achVendID="TRRS",
        fsSelection=0x20 if font.weight >= 700 else 0x40,
        usFirstCharIndex=0x20,
        usLastCharIndex=0x00A3,
        sTypoAscender=ascent,
        sTypoDescender=descent,
        sTypoLineGap=0,
        usWinAscent=ascent,
        usWinDescent=-descent,
        sxHeight=(font.line_height - 6) * FONT_UNITS_PER_PIXEL,
        sCapHeight=(font.line_height - 2) * FONT_UNITS_PER_PIXEL,
        usDefaultChar=0,
        usBreakChar=0x20,
        usMaxContext=1,
    )
    builder.setupPost(
        italicAngle=0,
        underlinePosition=-FONT_UNITS_PER_PIXEL,
        underlineThickness=FONT_UNITS_PER_PIXEL,
        isFixedPitch=0,
        keepGlyphNames=True,
    )
    builder.setupMaxp()
    builder.setupHead(
        unitsPerEm=units_per_em,
        created=MAC_EPOCH_AT_UNIX_EPOCH,
        modified=MAC_EPOCH_AT_UNIX_EPOCH,
        flags=0x000B,  # baseline, lsb=xMin, and integer PPEM scaler math
        macStyle=1 if font.weight >= 700 else 0,
        lowestRecPPEM=font.css_size,
        fontDirectionHint=2,
        indexToLocFormat=1,
        glyphDataFormat=0,
    )
    # Ask old rasterizers not to grayscale the authored-size pixel outlines.
    gasp = builder.font["gasp"] = builder.font.get("gasp") or __import__(
        "fontTools.ttLib", fromlist=["newTable"]
    ).newTable("gasp")
    gasp.gaspRange = {0xFFFF: 0x0001}

    builder.font.recalcTimestamp = False
    builder.font.recalcBBoxes = True
    destination.parent.mkdir(parents=True, exist_ok=True)
    builder.save(destination)


def write_woff(ttf_path: Path, destination: Path, allow_version: bool) -> None:
    font_tools, _FontBuilder, _TTGlyphPen, TTFont, _Panose = _fonttools_modules()
    if font_tools.__version__ != EXPECTED_FONTTOOLS_VERSION and not allow_version:
        raise SystemExit(f"fontTools {EXPECTED_FONTTOOLS_VERSION} is required")
    font = TTFont(ttf_path, recalcTimestamp=False)
    font.flavor = "woff"
    font.recalcTimestamp = False
    font.save(destination, reorderTables=True)


def _utf16le(value: str) -> bytes:
    return value.encode("utf-16le")


def write_eot(ttf_path: Path, destination: Path) -> None:
    """Wrap a TTF in uncompressed EOT Classic 0x00020002 for IE 6-8.

    IE8 treats root strings as URL prefixes and rejects an empty root list.  The
    XP host stages the compatibility page under a varying absolute file URL, so
    ``file:///`` is the stable least-broad prefix that covers every staging
    directory without coupling the font bake to a machine-specific path.
    """
    _font_tools, _FontBuilder, _TTGlyphPen, TTFont, _Panose = _fonttools_modules()
    font = TTFont(ttf_path, recalcTimestamp=False)
    ttf = ttf_path.read_bytes()
    os2 = font["OS/2"]
    head = font["head"]
    names = font["name"]

    def english_name(name_id: int, fallback: str) -> str:
        # Prefer Windows English, then any Unicode record, then the supplied
        # deterministic fallback.  The generator's own names always take the
        # first branch; the fallbacks make corruption errors intelligible.
        record = names.getName(name_id, 3, 1, 0x0409)
        if record is None:
            record = names.getName(name_id, 3, 10, 0x0409)
        if record is None:
            candidates = names.names
            record = next((item for item in candidates if item.nameID == name_id), None)
        return record.toUnicode() if record is not None else fallback

    family = _utf16le(english_name(1, "ToriRS Chrome"))
    style = _utf16le(english_name(2, "Regular"))
    version_name = _utf16le(english_name(5, "Version 1.000"))
    full_name = _utf16le(english_name(4, "ToriRS Chrome"))
    panose = bytes(
        getattr(os2.panose, field)
        for field in (
            "bFamilyType",
            "bSerifStyle",
            "bWeight",
            "bProportion",
            "bContrast",
            "bStrokeVariation",
            "bArmStyle",
            "bLetterForm",
            "bMidline",
            "bXHeight",
        )
    )

    # EOT fields are little-endian; the embedded sfnt remains big-endian.
    output = bytearray()
    output.extend(struct.pack("<IIII", 0, len(ttf), 0x00020002, 0))
    output.extend(panose)
    output.extend(struct.pack("<BBIHH", 0, 0, os2.usWeightClass, os2.fsType, 0x504C))
    output.extend(
        struct.pack(
            "<IIIIII",
            os2.ulUnicodeRange1,
            os2.ulUnicodeRange2,
            os2.ulUnicodeRange3,
            os2.ulUnicodeRange4,
            os2.ulCodePageRange1,
            os2.ulCodePageRange2,
        )
    )
    output.extend(struct.pack("<I", head.checkSumAdjustment))
    output.extend(struct.pack("<IIIIH", 0, 0, 0, 0, 0))
    for value in (family, style, version_name, full_name):
        output.extend(struct.pack("<H", len(value)))
        output.extend(value)
        output.extend(struct.pack("<H", 0))
    # IE8 validates this NUL-terminated UTF-16LE prefix against the page URL.
    root_string = _utf16le("file:///" + "\0")
    output.extend(struct.pack("<H", len(root_string)))
    output.extend(root_string)
    output.extend(struct.pack("<I", (sum(root_string) & 0xFFFFFFFF) ^ 0x50475342))
    output.extend(struct.pack("<IHHII", 0, 0, 0, 0, 0))
    output.extend(ttf)
    struct.pack_into("<I", output, 0, len(output))
    destination.write_bytes(output)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_manifest(
    destination: Path,
    source_path: Path,
    metrics_path: Path,
    fonts: Sequence[SourceFont],
    outputs: dict[str, dict[str, Path]],
) -> None:
    data = {
        "schema": 1,
        "generator": "tools/torirs_chrome_fonts.py",
        "fonttools": EXPECTED_FONTTOOLS_VERSION,
        "source": {
            "glyphs": "src/engine/torirs_debug_font_baked.c",
            "glyphs_sha256": sha256(source_path),
            "metrics": "src/ui/uitree_debug_font_metrics.h",
            "metrics_sha256": sha256(metrics_path),
            "cache_revision": "osrs239",
        },
        "conversion": {
            "units_per_source_pixel": FONT_UNITS_PER_PIXEL,
            "outline": "coalesced rectangles over nonzero baked mask pixels",
            "cmap": "U+0020-U+007E and U+00A3",
            "advance_only": ["U+0020 SPACE", "U+0060 GRAVE ACCENT", "U+007C VERTICAL LINE"],
            "eot_root_string": "file:///",
        },
        "fonts": [],
    }
    for font in fonts:
        stem = str(FONT_SPECS[font.symbol]["stem"])
        data["fonts"].append(
            {
                "symbol": font.symbol,
                "cache_archive": font.archive_id,
                "family": font.family,
                "style": font.style,
                "weight": font.weight,
                "authored_css_px": font.css_size,
                "baseline_px": font.line_height,
                "line_box_px": font.line_box,
                "files": {
                    extension: {
                        "path": path.name,
                        "bytes": path.stat().st_size,
                        "sha256": sha256(path),
                    }
                    for extension, path in sorted(outputs[stem].items())
                },
            }
        )
    destination.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def generate(
    source_path: Path,
    metrics_path: Path,
    output_dir: Path,
    allow_fonttools_version: bool,
) -> None:
    c_text = source_path.read_text(encoding="utf-8")
    metrics_text = metrics_path.read_text(encoding="utf-8")
    fonts = tuple(parse_source_font(c_text, metrics_text, symbol) for symbol in FONT_SPECS)
    source_digest = hashlib.sha256(source_path.read_bytes()).hexdigest()
    output_dir.mkdir(parents=True, exist_ok=True)
    outputs: dict[str, dict[str, Path]] = {}

    for font in fonts:
        stem = str(FONT_SPECS[font.symbol]["stem"])
        ttf_path = output_dir / f"{stem}.ttf"
        woff_path = output_dir / f"{stem}.woff"
        eot_path = output_dir / f"{stem}.eot"
        build_ttf(font, ttf_path, source_digest, allow_fonttools_version)
        write_woff(ttf_path, woff_path, allow_fonttools_version)
        write_eot(ttf_path, eot_path)
        outputs[stem] = {"ttf": ttf_path, "woff": woff_path, "eot": eot_path}

    write_manifest(output_dir / "manifest.json", source_path, metrics_path, fonts, outputs)


def main(argv: Sequence[str] | None = None) -> int:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source",
        type=Path,
        default=repo / "src/engine/torirs_debug_font_baked.c",
        help="baked ToriDraw font C source",
    )
    parser.add_argument(
        "--metrics",
        type=Path,
        default=repo / "src/ui/uitree_debug_font_metrics.h",
        help="baked chrome metric header",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=repo / "res/plugin_chrome/font",
        help="font output directory",
    )
    parser.add_argument(
        "--allow-fonttools-version",
        action="store_true",
        help="allow non-pinned fontTools output (not byte-reproducible)",
    )
    args = parser.parse_args(argv)
    generate(args.source.resolve(), args.metrics.resolve(), args.out.resolve(), args.allow_fonttools_version)
    return 0


if __name__ == "__main__":
    sys.exit(main())
