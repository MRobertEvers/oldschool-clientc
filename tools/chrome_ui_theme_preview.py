#!/usr/bin/env python3
"""Draw the map editor's main frame in the editor theme, without building.

    python3 tools/chrome_ui_theme_preview.py [--out PNG] [--q 4..12]

A picture of what src/ui/uitree_debug_overlay.c lays out under
torirs_chrome_theme_editor: the menu bar, the tool rail, a dock group with
tabs, a form (label, field, select, ticked and empty checkbox, a button with a
keycap, a slider), two lists with a selected and a hovered row, an open menu,
a tooltip and the status bar. For looking at the theme and the faces when the
client cannot be built; it is not a test.

Nothing here is retyped from the C. It READS, at run time:

    src/ui/uitree_debug_overlay.c     the theme's initialiser (one `.key =
                                      value,` a line) and the UI faces' table
                                      (k_dbg_ui_faces: em, ascent, line box)
    src/ui/uitree_ui_font_metrics.h   each face's units per em and advances
    src/ui/torirs_chrome_metrics.h    the TORIRS_CHROME_M_* geometry
    src/ui/torirs_chrome_icons.h      the icon codes
    src/engine/fonts/vendor/*.ttf     the faces themselves

and places every glyph with the chrome's own arithmetic: the pen steps by
round(units * em * q / (upem * 4)) per byte (ToriRSChrome_GlyphAdvanceQ), the
baseline is the row's centred line box plus the slot's ascent
(dbg_row_text_baseline). What it does NOT share with the client is the
rasteriser: glyphs are drawn by FreeType (through Pillow), unhinted, where the
client uses stb_truetype. Both fill by exact area coverage, so weight and
edges should agree to within a rounding; the layout code below is a
transcription of the C's and is only as right as its reading of it.

It also prints the contrast of each text / ground pair the theme uses, and
exits 1 when one is under the ratio it is held to (body text 7:1, dim text
4.5:1, marks 3:1) or a key of the theme is pure yellow.

Needs Python 3 and Pillow.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

REPO = Path(__file__).resolve().parents[1]
OVERLAY_C = REPO / "src/ui/uitree_debug_overlay.c"
UI_METRICS_H = REPO / "src/ui/uitree_ui_font_metrics.h"
CHROME_METRICS_H = REPO / "src/ui/torirs_chrome_metrics.h"
ICONS_H = REPO / "src/ui/torirs_chrome_icons.h"
VENDOR = REPO / "src/engine/fonts/vendor"

FACE_FILE = {
    "InterRegular": "Inter-Regular.ttf",
    "InterSemiBold": "Inter-SemiBold.ttf",
    "JetBrainsMono": "JetBrainsMono-Regular.ttf",
    "Icons": "ToriRSIcons.ttf",
}

# ---------------------------------------------------------------- parsing


def parse_theme(text: str, name: str) -> dict[str, int]:
    m = re.search(r"struct ToriRSChromeTheme const %s = \{(.*?)\n\};" % re.escape(name), text, re.S)
    if not m:
        sys.exit(f"{OVERLAY_C}: no initialiser for {name}")
    slots = dict(re.findall(r"(TORIRS_CHROME_FONT_\w+) = (\d+)", (REPO / "src/ui/uitree_debug_overlay.h").read_text(encoding="utf-8")))
    theme = {}
    for key, value in re.findall(r"^\s*\.(\w+) = ([^,]+),", m.group(1), re.M):
        value = value.strip()
        if value in slots:
            theme[key] = int(slots[value])
        else:
            theme[key] = int(value, 0)
    return theme


def parse_faces(text: str) -> dict[str, dict]:
    faces = {}
    for slot, face, em, ascent, box in re.findall(
        r"DBG_UI_FACE\((TORIRS_CHROME_FONT_\w+), (\w+), (\d+), (\d+), (\d+)\)", text
    ):
        faces[slot] = {"face": face, "em": int(em), "ascent": int(ascent), "box": int(box)}
    if len(faces) != 5:
        sys.exit(f"{OVERLAY_C}: expected the five UI faces in k_dbg_ui_faces, found {sorted(faces)}")
    return faces


def parse_ui_metrics(text: str) -> dict[str, dict]:
    out = {}
    for face in FACE_FILE:
        upem = re.search(r"ToriRSChromeUiFont_%s_UPEM (\d+)" % face, text)
        table = re.search(r"ToriRSChromeUiFont_%s_advance_units\[(\d+)\] = \{(.*?)\};" % face, text, re.S)
        if not upem or not table:
            sys.exit(f"{UI_METRICS_H}: no metrics for {face}")
        units = [int(v) for v in re.findall(r"\d+", table.group(2))]
        assert len(units) == int(table.group(1)), face
        out[face] = {"upem": int(upem.group(1)), "units": units}
    return out


def parse_chrome_metrics(text: str) -> dict[str, int]:
    text = text.replace("\\\n", " ")
    raw = dict(re.findall(r"^#define (TORIRS_CHROME_M_\w+)\s+(.+?)\s*$", text, re.M))
    done: dict[str, int] = {}

    def value(name: str) -> int:
        if name not in done:
            expr = re.sub(r"TORIRS_CHROME_M_\w+", lambda m: str(value(m.group(0))), raw[name])
            if not re.fullmatch(r"[\d\s()+\-*/]+", expr):
                sys.exit(f"{CHROME_METRICS_H}: cannot read {name} = {raw[name]}")
            done[name] = int(eval(expr))  # digits and arithmetic only, checked above
        return done[name]

    return {name[len("TORIRS_CHROME_M_") :]: value(name) for name in raw if re.search(r"\d", raw[name])}


def parse_icons(text: str) -> dict[str, str]:
    return dict(re.findall(r'#define TORIRS_CHROME_ICON_(\w+)\s+"(.)"', text))


# ---------------------------------------------------------------- the chrome's arithmetic


class Chrome:
    def __init__(self, q: int):
        overlay = OVERLAY_C.read_text(encoding="utf-8")
        self.q = q
        self.th = parse_theme(overlay, "torirs_chrome_theme_editor")
        self.faces = parse_faces(overlay)
        self.ui_metrics = parse_ui_metrics(UI_METRICS_H.read_text(encoding="utf-8"))
        self.m = parse_chrome_metrics(CHROME_METRICS_H.read_text(encoding="utf-8"))
        self.icons = parse_icons(ICONS_H.read_text(encoding="utf-8"))
        slots = dict(
            re.findall(r"(TORIRS_CHROME_FONT_\w+) = (\d+)", (REPO / "src/ui/uitree_debug_overlay.h").read_text(encoding="utf-8"))
        )
        self.slot_name = {int(v): k for k, v in slots.items() if k in self.faces}
        self.fonts = {}
        for slot, f in self.faces.items():
            # The em at this scale; FreeType takes it as a real, like stb.
            self.fonts[slot] = ImageFont.truetype(str(VENDOR / FACE_FILE[f["face"]]), f["em"] * q / 4)
        self.font_row = self.slot_name[self.th["font_row"]]
        self.font_title = self.slot_name[self.th["font_title"]]
        self.font_menu = self.slot_name[self.th["font_menu"]]
        self.font_small = self.slot_name[self.th["font_small"]]
        self.font_code = self.slot_name[self.th["font_code"]]
        self.font_icon = "TORIRS_CHROME_FONT_ICON"
        assert self.th["font_icon"] == int(slots[self.font_icon]), "the editor theme has an icon face"
        self.img = None
        self.draw = None

    # ToriRSChrome_ScalePx
    def px(self, v: int) -> int:
        if v < 0:
            return -((-v * self.q + 2) // 4)
        return (v * self.q + 2) // 4

    # ToriRSChrome_FontDrawsByte / _FontAdvanceUnits / dbg_ui_advance_px
    def advance(self, slot: str, ch: str) -> int:
        f = self.faces[slot]
        met = self.ui_metrics[f["face"]]
        c = ord(ch)
        if not (0x21 <= c <= 0x7E):
            c = 0x20
        units = met["units"][c - 32]
        den = met["upem"] * 4
        return (2 * units * f["em"] * self.q + den) // (2 * den)

    def measure(self, slot: str, text: str) -> int:
        return sum(self.advance(slot, ch) for ch in text)

    def ascent(self, slot: str) -> int:
        return self.px(self.faces[slot]["ascent"])

    def box(self, slot: str) -> int:
        return self.px(self.faces[slot]["box"])

    def row_h(self) -> int:
        return self.px(self.th["row_h"] or self.th["row_h_base"] or self.m["ROW_H"])

    # dbg_row_text_baseline, for any face
    def baseline(self, slot: str, y: int, h: int) -> int:
        return y + (h - self.box(slot)) // 2 + self.ascent(slot)

    # dbg_elide_text
    def elide(self, slot: str, text: str, width: int) -> str:
        if width <= 0 or self.measure(slot, text) <= width:
            return text
        budget = width - self.measure(slot, "...")
        kept, used = 0, 0
        while kept < len(text) and used + self.advance(slot, text[kept]) <= budget:
            used += self.advance(slot, text[kept])
            kept += 1
        return text[:kept] + "..."

    # ---- prims

    def begin(self, w: int, h: int, ground: int):
        self.img = Image.new("RGB", (w, h), rgb(ground))
        self.draw = ImageDraw.Draw(self.img)

    def rect(self, x, y, w, h, color, filled=True, trans=0, clip=None):
        if w <= 0 or h <= 0 or trans >= 255:
            return
        x0, y0, x1, y1 = x, y, x + w, y + h
        if clip:
            x0, y0 = max(x0, clip[0]), max(y0, clip[1])
            x1, y1 = min(x1, clip[0] + clip[2]), min(y1, clip[1] + clip[3])
            if x1 <= x0 or y1 <= y0:
                return
        if not filled:
            # A one-pixel outline of the box (clipped as four rects).
            for r in ((x, y, w, 1), (x, y + h - 1, w, 1), (x, y, 1, h), (x + w - 1, y, 1, h)):
                self.rect(*r, color, True, trans, clip)
            return
        if trans:
            layer = Image.new("RGB", (x1 - x0, y1 - y0), rgb(color))
            under = self.img.crop((x0, y0, x1, y1))
            self.img.paste(Image.blend(layer, under, trans / 255.0), (x0, y0))
        else:
            self.draw.rectangle((x0, y0, x1 - 1, y1 - 1), fill=rgb(color))

    def text(self, x, baseline, text, color, slot, clip=None):
        """Byte by byte from a whole-pixel pen, as the atlas draws it."""
        font = self.fonts[slot]
        target, off = self.img, (0, 0)
        if clip:
            cx0, cy0 = max(clip[0], 0), max(clip[1], 0)
            cx1, cy1 = min(clip[0] + clip[2], self.img.width), min(clip[1] + clip[3], self.img.height)
            if cx1 <= cx0 or cy1 <= cy0:
                return
            target = self.img.crop((cx0, cy0, cx1, cy1))
            off = (cx0, cy0)
        d = ImageDraw.Draw(target)
        pen = x
        for ch in text:
            if 0x21 <= ord(ch) <= 0x7E:
                d.text((pen - off[0], baseline - off[1]), ch, font=font, fill=rgb(color), anchor="ls")
            pen += self.advance(slot, ch)
        if clip:
            self.img.paste(target, off)
            self.draw = ImageDraw.Draw(self.img)

    # dbg_push_icon_glyph
    def icon(self, box, name, color, clip=None):
        slot = self.font_icon
        ch = self.icons[name]
        x, y, w, h = box
        self.text(x + (w - self.measure(slot, ch)) // 2, y + (h - self.box(slot)) // 2 + self.ascent(slot), ch, color, slot, clip or box)


def rgb(v: int):
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255)


def luminance(v: int) -> float:
    def lin(c):
        c /= 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

    r, g, b = rgb(v)
    return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b)


def contrast(a: int, b: int) -> float:
    la, lb = luminance(a), luminance(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


# ---------------------------------------------------------------- widgets (transcribed from the C)


class Window:
    """dbg_build_window under a flat theme: the box, the title bar and its
    furniture, the pinned tab strip, and a column to stack rows in."""

    def __init__(self, c: Chrome, x, y, w, h, title="", tabs=None, active=0, hover_tab=-1, furniture=("POP_OUT", "CLOSE"),
                 joined_above=False, joined_below=False, hollow=False, grip=False):
        th, m = c.th, c.m
        self.c = c
        self.x, self.y, self.w, self.h = x, y, w, h
        rule = c.px(1)
        edge = rule
        box = c.box(c.font_title)
        separator_y = box + c.px(2)
        head_h = (edge - rule) + separator_y + rule if title else edge
        clip = (x, y, w, h)
        if not hollow:
            c.rect(x, y, w, h, th["panel_body"])
        else:
            c.rect(x, y, w, head_h, th["panel_body"])
        if title:
            c.rect(x + edge, y + rule, w - 2 * edge, separator_y - rule, th["panel_title_bg"])
            c.text(x + edge + c.px(m["PAD"]), y + c.px(1) + c.ascent(c.font_title), title, th["panel_title_text"], c.font_title, clip)
            c.rect(x + edge, y + separator_y, w - 2 * edge, rule, th["panel_border"])
            # dbg_panel_close_box, then each button left of the last.
            side = box - 2 * rule
            bx = x + w - edge - c.px(m["CLOSE_PAD"]) - side
            for name in reversed(furniture):
                c.icon((bx, y + edge + (box - side) // 2, side, side), name, th["panel_title_text"])
                bx -= side + c.px(m["CLOSE_PAD"])
        c.rect(x, y, rule, h, th["panel_border"])
        c.rect(x + w - rule, y, rule, h, th["panel_border"])
        if not joined_above:
            c.rect(x, y, w, rule, th["panel_border"])
        if not joined_below:
            c.rect(x, y + h - rule, w, rule, th["panel_border"])
        self.col_x = x + edge + c.px(m["PAD"])
        self.col_w = w - 2 * edge - 2 * c.px(m["PAD"])
        self.row_y = y + head_h + c.px(m["PAD"])
        self.clip = (self.col_x, y + head_h, self.col_w, h - head_h - edge)
        if tabs:
            self.strip(tabs, active, hover_tab)
        if grip:
            dot, pitch = c.px(2), c.px(3)
            gx = x + w - rule - 2 * rule
            gy = y + h - rule - 2 * rule
            for j in range(3):
                for i in range(3 - j):
                    c.rect(gx - dot + rule - i * pitch, gy - dot + rule - j * pitch, dot, dot, th["grip"])

    def strip(self, tabs, active, hover_tab):
        """dbg_push_tabstrip, flat."""
        c, th, m = self.c, self.c.th, self.c.m
        x, y, w, h = self.col_x, self.row_y, self.col_w, c.px(m["TAB_H"])
        c.rect(x, y + h - c.px(1), w, c.px(1), th["panel_border"])
        pen = x
        for i, cap in enumerate(tabs):
            tw = c.measure(c.font_row, cap) + 2 * c.px(2 * m["TAB_PAD_X"])
            if i == hover_tab and i != active:
                c.rect(pen, y, tw, h - c.px(1), th["row_hover"], trans=th["row_hover_trans"])
            if i == active:
                c.rect(pen, y + h - c.px(2), tw, c.px(2), th["tab_marker"])
            ink = th["text"] if i == active else th["text_hover"] if i == hover_tab else th["text_dim"]
            c.text(pen + (tw - c.measure(c.font_row, cap)) // 2, c.baseline(c.font_row, y, h), cap, ink, c.font_row, (pen, y, tw, h))
            pen += tw
        self.row_y += h + c.px(m["ROW_GAP"])

    def next(self, h):
        y = self.row_y
        self.row_y += h + self.c.px(self.c.m["ROW_GAP"])
        return y

    # dbg_row_label_w: a third of the content column, never under the authored one.
    def label_w(self):
        return max(self.col_w // 3, self.c.px(self.c.m["LABEL_W"]))

    def label(self, text, dim=False, heading=False):
        c, th = self.c, self.c.th
        y = self.next(c.row_h())
        x = self.col_x + (c.px(c.m["FIELD_PAD_X"]) if heading else 0)
        c.text(x, c.baseline(c.font_row, y, c.row_h()), text, th["text_dim"] if (dim or heading) else th["text"], c.font_row, self.clip)
        if heading:
            c.rect(self.col_x, y + c.row_h() - c.px(1), self.col_w, c.px(1), th["separator"])

    def field_chrome(self, box, focused=False):
        """dbg_push_field_chrome, and the focus ring a field draws over it."""
        c, th = self.c, self.c.th
        x, y, w, h = box
        c.rect(x, y, w, h, th["input_bg"])
        c.rect(x, y, w, h, th["dropdown_border"], filled=False)
        c.rect(x + c.px(1), y + c.px(1), w - 2 * c.px(1), h - 2 * c.px(1),
               th["input_border_focus"] if focused else th["dropdown_border_inner"], filled=False)

    def text_input(self, label, value, focused=False, selected=False):
        c, th, m = self.c, self.c.th, self.c.m
        h = c.row_h()
        y = self.next(h)
        c.text(self.col_x, c.baseline(c.font_row, y, h), label, th["text_dim"], c.font_row, self.clip)
        bx = self.col_x + self.label_w()
        bw = self.col_x + self.col_w - bx
        self.field_chrome((bx, y, bw, h), focused)
        tx = bx + c.px(m["FIELD_PAD_X"])
        inner = (tx, y + c.px(1), bw - 2 * c.px(m["FIELD_PAD_X"]), h - 2 * c.px(1))
        if selected:
            c.rect(tx, y + c.px(m["FIELD_INSET"]), max(c.measure(c.font_row, value), c.px(3)), h - 2 * c.px(m["FIELD_INSET"]),
                   th["text_selection"], trans=th["text_selection_trans"], clip=inner)
        c.text(tx, c.baseline(c.font_row, y, h), value, th["input_text"], c.font_row, inner)
        if focused:
            c.rect(tx + c.measure(c.font_row, value), y + c.px(m["FIELD_INSET"]), c.px(1), h - 2 * c.px(m["FIELD_INSET"]), th["input_text"])

    def dropdown(self, label, value):
        c, th, m = self.c, self.c.th, self.c.m
        h = c.row_h()
        y = self.next(h)
        c.text(self.col_x, c.baseline(c.font_row, y, h), label, th["text_dim"], c.font_row, self.clip)
        bx = self.col_x + self.label_w()
        bw = self.col_x + self.col_w - bx
        self.field_chrome((bx, y, bw, h))
        arrow = c.px(m["DROP_ARROW"])
        ax = bx + bw - c.px(m["FIELD_INSET"]) - arrow
        c.icon((ax, y + (h - arrow) // 2, arrow, arrow), "CHEVRON_DOWN", th["text_dim"], (bx, y, bw, h))
        tx = bx + c.px(m["FIELD_PAD_X"])
        value = c.elide(c.font_row, value, ax - tx)
        c.text(tx, y + (h - c.box(c.font_row)) // 2 + c.ascent(c.font_row), value, th["dropdown_text"], c.font_row, (bx, y, ax - bx, h))
        return (bx, y, bw, h)

    def checkbox(self, label, checked, hovered=False):
        c, th, m = self.c, self.c.th, self.c.m
        h = c.row_h()
        y = self.next(h)
        size = c.px(m["BOX"])
        by = y + (h - size) // 2
        c.rect(self.col_x, by, size, size, th["accent"] if checked else th["input_bg"])
        c.rect(self.col_x, by, size, size,
               th["outline_hover"] if hovered else th["accent"] if checked else th["check_box"], filled=False)
        if checked:
            c.icon((self.col_x, by, size, size), "CHECK", th["check_mark"])
        c.text(self.col_x + size + c.px(m["CHECK_GAP"]), c.baseline(c.font_row, y, h), label,
               th["text_hover"] if hovered else th["text"], c.font_row, self.clip)

    def keycap(self, cap, chord, ink, clip):
        """dbg_push_keycap."""
        c, th = self.c, self.c.th
        x, y, w, h = cap
        c.rect(x, y, w, h, th["keycap_bg"], clip=clip)
        c.rect(x, y, w, h, th["keycap_border"], filled=False, clip=clip)
        c.text(x + (w - c.measure(c.font_small, chord)) // 2, y + (h - c.box(c.font_small)) // 2 + c.ascent(c.font_small),
               chord, ink, c.font_small, clip)

    def button(self, caption, chord=None, hovered=False, pressed=False, disabled=False, fill=False, same_row_x=None):
        c, th, m = self.c, self.c.th, self.c.m
        h = c.row_h()
        y = self.next(h)
        bw = self.col_w if fill else min(c.px(m["LABEL_W"]), self.col_w)
        x = self.col_x
        ground = th["button_bg"] if disabled else th["button_bg_checked"] if pressed else th["button_bg_hover"] if hovered else th["button_bg"]
        c.rect(x, y, bw, h, ground)
        c.rect(x, y, bw, h, th["button_border"], filled=False)
        inset = c.px(m["KEYCAP_INSET"])
        track_x, track_w = x, bw
        if chord:
            cap_w = min(c.measure(c.font_small, chord) + 2 * c.px(m["KEYCAP_PAD_X"]), bw - 2 * inset)
            cap_x = x + bw - inset - cap_w
            track_x = x + c.px(m["FIELD_PAD_X"])
            track_w = cap_x - c.px(m["KEYCAP_GAP"]) - track_x
            self.keycap((cap_x, y + inset, cap_w, h - 2 * inset), chord, th["text_dim"] if disabled else th["keycap_text"], (x, y, bw, h))
            caption = c.elide(c.font_row, caption, track_w)
        ink = th["text_dim"] if disabled else th["text_hover"] if hovered or pressed else th["text"]
        c.text(track_x + (track_w - c.measure(c.font_row, caption)) // 2, c.baseline(c.font_row, y, h), caption, ink, c.font_row, (x, y, bw, h))

    def scrollbar(self, bar, content, view, offset):
        """dbg_push_scrollbar with no skin: track, grip, a chevron at each end."""
        c, th, m = self.c, self.c.th, self.c.m
        x, y, w, h = bar
        arrow = c.px(m["SCROLL_W"])
        track_y, track_h = y + arrow, h - 2 * arrow
        if track_h <= 0 or content <= view:
            return
        grip_h = min(max(track_h * view // content, c.px(m["SCROLL_GRIP_MIN"])), track_h)
        grip_y = track_y + (track_h - grip_h) * offset // (content - view)
        c.rect(x, track_y, w, track_h, th["scroll_track"])
        c.rect(x, grip_y, w, grip_h, th["scroll_grip"])
        c.icon((x, y, arrow, arrow), "CHEVRON_UP", th["text_dim"])
        c.icon((x, y + h - arrow, arrow, arrow), "CHEVRON_DOWN", th["text_dim"])

    def vlist(self, caption, rows, selected=-1, hovered=-1, total=None, first=0, cells=False):
        """dbg_draw_list: a plain list, or (cells) rows of a picture well, a
        name, a dim part, keycaps and a right field (dbg_draw_cells)."""
        c, th, m = self.c, self.c.th, self.c.m
        line_h = c.box(c.font_row) + c.px(2)
        cap_h = c.row_h() if caption else 0
        total = total if total is not None else len(rows)
        h = cap_h + len(rows) * line_h + 2 * c.px(1)
        y = self.next(h)
        bar = c.px(m["SCROLL_W"]) if total > len(rows) else 0
        box = (self.col_x, y + cap_h, self.col_w, len(rows) * line_h + 2 * c.px(1))
        inner = (box[0] + c.px(1), box[1] + c.px(1), box[2] - 2 * c.px(1) - bar, box[3] - 2 * c.px(1))
        if caption and cells:
            self.cells(caption, (inner[0], y, inner[2], c.row_h()), header=True)
        elif caption:
            c.text(self.col_x, c.baseline(c.font_row, y, c.row_h()), caption, th["text_dim"], c.font_row, self.clip)
        c.rect(*box, th["textarea_bg"])
        c.rect(*box, th["dropdown_border"], filled=False)
        c.rect(box[0] + c.px(1), box[1] + c.px(1), box[2] - 2 * c.px(1), box[3] - 2 * c.px(1), th["dropdown_border_inner"], filled=False)
        if bar:
            self.scrollbar((inner[0] + inner[2], inner[1], bar, inner[3]), total * line_h, len(rows) * line_h, first * line_h)
        for i, row in enumerate(rows):
            ly = inner[1] + i * line_h
            if i == selected:
                c.rect(inner[0], ly, inner[2], line_h, th["selection_bg"], trans=th["selection_trans"], clip=inner)
            elif i == hovered:
                c.rect(inner[0], ly, inner[2], line_h, th["row_hover"], trans=th["row_hover_trans"], clip=inner)
            if cells:
                self.cells(row, (inner[0], ly, inner[2], line_h), selected=(i == selected), clip=inner)
            else:
                c.text(inner[0] + c.px(m["FIELD_PAD_X"]), ly + (line_h - c.box(c.font_row)) // 2 + c.ascent(c.font_row), row,
                       th["selection_text"] if i == selected else th["text"], c.font_row, inner)

    def cells(self, row, line, header=False, selected=False, clip=None):
        c, th, m = self.c, self.c.th, self.c.m
        swatch, name, dim, keys, right = row
        x, y, w, h = line
        font = c.font_row
        pad, gap, thumb = c.px(m["FIELD_PAD_X"]), c.px(3), c.px(16)
        right_w = c.measure(font, "000000")
        keys_w = min(w // 3, c.px(96))
        right_x = x + w - pad - right_w
        keys_x = right_x - gap - keys_w
        name_x = x + pad + thumb + gap
        name_w = keys_x - gap - name_x
        base = y + (h - c.box(font)) // 2 + c.ascent(font)
        name_ink = th["text_dim"] if header else th["selection_text"] if selected else th["text"]
        dim_ink = th["text"] if selected else th["text_dim"]
        if not header:
            ty = y + (h - thumb) // 2
            c.rect(x + pad, ty, thumb, thumb, th["input_bg"], clip=clip)
            if swatch is not None:
                c.rect(x + pad + c.px(2), ty + c.px(2), thumb - c.px(4), thumb - c.px(4), swatch, clip=clip)
        shown = c.elide(font, name, name_w)
        c.text(name_x, base, shown, name_ink, font, clip)
        used = c.measure(font, shown)
        space = c.measure(font, " ")
        if dim and name_w - used - space >= c.measure(font, "a..."):
            c.text(name_x + used + space, base, c.elide(font, dim, name_w - used - space), dim_ink, font, clip)
        if header:
            c.text(keys_x, base, keys, th["text_dim"], font, clip)
        else:
            cap_h = min(c.box(c.font_small) + c.px(2), h)
            kx = keys_x
            for key in keys.split():
                kw = c.measure(c.font_small, key) + 2 * c.px(m["KEYCAP_PAD_X"])
                self.keycap((kx, y + (h - cap_h) // 2, kw, cap_h), key, th["keycap_text"], clip)
                kx += kw + gap
        c.text(right_x + right_w - c.measure(font, right), base, right, th["text"] if selected else th["text_dim"], font, clip)

    def slider(self, label, value, lo, hi, hovered=False):
        c, th, m = self.c, self.c.th, self.c.m
        h = c.row_h()
        y = self.next(h)
        knob = c.px(16)
        label_w = min(c.measure(c.font_row, label) + c.px(6), self.col_w * 30 // 100)
        tx = self.col_x + label_w
        tw = self.col_x + self.col_w - c.px(36) - tx
        mid = y + h // 2
        knob_x = tx + (value - lo) * (tw - knob) // (hi - lo)
        c.text(self.col_x, c.baseline(c.font_row, y, h), c.elide(c.font_row, label, label_w - c.px(6)), th["text_dim"], c.font_row, self.clip)
        rail = c.px(4)
        c.rect(tx, mid - rail // 2, tw, rail, th["input_border"])
        c.rect(tx, mid - rail // 2, knob_x + knob // 2 - tx, rail, th["accent"])
        hw, hh = c.px(6), c.px(14)
        c.rect(knob_x + (knob - hw) // 2, mid - hh // 2, hw, hh, th["slider_knob_hover"] if hovered else th["slider_knob"])
        text = str(value)
        c.text(self.col_x + self.col_w - c.measure(c.font_row, text), c.baseline(c.font_row, y, h), text, th["text"], c.font_row, self.clip)

    def tree(self, label, depth, expander, selected=False, hovered=False):
        c, th, m = self.c, self.c.th, self.c.m
        h = c.row_h()
        y = self.next(h)
        x = self.col_x + depth * c.px(12)
        ink = th["selection_text"] if selected else th["text_hover"] if hovered else th["text"]
        if selected:
            c.rect(self.col_x, y, self.col_w, h, th["selection_bg"], trans=th["selection_trans"])
        elif hovered:
            c.rect(self.col_x, y, self.col_w, h, th["row_hover"], trans=th["row_hover_trans"])
        if expander:
            c.icon((x, y, c.px(16), h), "CHEVRON_DOWN" if expander == "open" else "CHEVRON_RIGHT", ink if selected else th["text_dim"])
        c.text(x + c.px(16) + c.px(3), c.baseline(c.font_row, y, h), label, ink, c.font_row, self.clip)

    def code(self, lines, caret_line=0):
        """A CODE view: the mono face, a gutter of line numbers."""
        c, th, m = self.c, self.c.th, self.c.m
        font = c.font_code
        line_h = c.box(font) + c.px(1)
        h = len(lines) * line_h + 2 * c.px(1)
        y = self.next(h)
        box = (self.col_x, y, self.col_w, h)
        c.rect(*box, th["textarea_bg"])
        c.rect(*box, th["dropdown_border"], filled=False)
        c.rect(box[0] + c.px(1), box[1] + c.px(1), box[2] - 2 * c.px(1), box[3] - 2 * c.px(1), th["dropdown_border_inner"], filled=False)
        inner = (box[0] + c.px(1), box[1] + c.px(1), box[2] - 2 * c.px(1), box[3] - 2 * c.px(1))
        gutter = c.measure(font, "99") + 2 * c.px(4)
        if th["input_bg"] != th["textarea_bg"]:
            c.rect(inner[0], inner[1], gutter, inner[3], th["input_bg"])
        for i, (line, spans) in enumerate(lines):
            ly = inner[1] + i * line_h
            base = ly + (line_h - c.box(font)) // 2 + c.ascent(font)
            num = str(i + 1)
            c.text(inner[0] + gutter - c.px(4) - c.measure(font, num), base, num, th["text_dim"], font, inner)
            tx = inner[0] + gutter + c.px(m["FIELD_PAD_X"])
            c.text(tx, base, line, th["input_text"], font, inner)
            for start, length, color in spans:
                c.text(tx + c.measure(font, line[:start]), base, line[start : start + length], color, font, inner)


def menubar(c: Chrome, x, y, w, items):
    """dbg_build_menubar under a flat theme. `items` are (kind, text, state):
    kind menu | item | caption | label; state on | hover | off | dim | ink."""
    th, m = c.th, c.m
    h = c.box(c.font_menu) + c.px(4)
    pad = c.px(m["PAD"])
    c.rect(x, y, w, h, th["menu_chrome"])
    pen = x + pad
    first = True
    boxes = {}
    for kind, text, state in items:
        font = c.font_small if kind == "caption" else c.font_menu
        tw = c.measure(font, text)
        box = (pen - pad // 2, y, tw + pad, h)
        boxes[text.strip()] = box
        if kind == "caption":
            lead = len(text) - len(text.lstrip(" "))
            lead_w = lead * c.advance(font, " ")
            if not first:
                c.rect(pen - pad + lead_w // 2, y + c.px(5), c.px(1), h - 2 * c.px(5), th["text_dim"], trans=150)
            c.text(pen + lead_w, y + (h - c.box(font)) // 2 + c.ascent(font), text[lead:], th["text_dim"], font)
        else:
            item = kind in ("menu", "item")
            if state == "on":
                c.rect(box[0], y + c.px(3), box[2], h - 2 * c.px(3), th["button_bg_checked"])
            elif item and state == "hover":
                c.rect(box[0], y + c.px(3), box[2], h - 2 * c.px(3), th["button_bg_hover"])
            ink = (th["text_dim"] if state == "off" or state == "dim" else th["selection_text"] if state == "on"
                   else th["text_hover"] if item and state == "hover" else th["panel_title_text"] if item else th["text"])
            c.text(pen, y + c.px(2) + c.ascent(font), text, ink, font)
        first = False
        pen += tw + 2 * pad
    return h, boxes


def menu_list(c: Chrome, x, y, rows, hovered=-1):
    """An open menu-mode dropdown list (dbg_build_dropdown_list, flat)."""
    th, m = c.th, c.m
    font = c.font_row
    row_h = c.box(font) + c.px(4)
    pad = c.px(m["DROP_LIST_PAD"])
    tab = c.px(16)
    widest = max(c.measure(font, left) + (tab + c.measure(font, right) if right else 0) for left, right in rows)
    w = widest + 2 * c.px(m["FIELD_PAD_X"]) + c.px(6)
    h = len(rows) * row_h + 2 * pad
    c.rect(x, y, w, h, th["menu_body"])
    c.rect(x, y, w, h, th["menu_chrome"], filled=False)
    row_x, row_w = x + pad, w - 2 * pad
    for i, (left, right) in enumerate(rows):
        ry = y + pad + i * row_h
        base = ry + (row_h - c.box(font)) // 2 + c.ascent(font)
        if i == hovered:
            c.rect(row_x, ry, row_w, row_h, th["selection_bg"], trans=th["selection_trans"])
        ink = th["menu_hover_text"] if i == hovered else th["menu_text"]
        c.text(row_x + c.px(m["FIELD_PAD_X"]), base, left, ink, font)
        if right:
            c.text(row_x + row_w - c.px(m["FIELD_PAD_X"]) - c.measure(font, right), base, right,
                   ink if i == hovered else th["text_dim"], font)


def toolbar(c: Chrome, x, y, w, buttons, max_bottom):
    """dbg_build_toolbar, one column: `buttons` are (icon, chord, state)."""
    th, m = c.th, c.m
    box, gap = c.px(32), c.px(2)
    pad = gap + c.px(1)
    pitch = box + gap
    fit = min(len(buttons), max(0, (max_bottom - y - 2 * pad + gap) // pitch))
    h = 2 * pad + fit * pitch - gap
    c.rect(x, y, w, h, th["panel_body"])
    c.rect(x, y, w, h, th["panel_border"], filled=False)
    boxes = []
    for i, (icon, chord, state) in enumerate(buttons[:fit]):
        bx, by = x + pad, y + pad + i * pitch
        boxes.append((bx, by, box, box))
        if state == "on":
            c.rect(bx, by, box, box, th["selection_bg"], trans=th["selection_trans"])
        elif state == "hover":
            c.rect(bx, by, box, box, th["row_hover"], trans=th["row_hover_trans"])
        ink = th["text_dim"] if state == "off" else th["accent"] if state == "on" else th["text_hover"] if state == "hover" else th["text"]
        c.icon((bx, by, box, box), icon, ink)
        if chord:
            c.text(bx + box - c.px(1) - c.measure(c.font_small, chord), by + box - c.px(2), chord,
                   th["selection_text"] if state == "on" else th["text_dim"], c.font_small, (bx, by, box, box))
    return boxes


def tooltip(c: Chrome, x, y, text, chord=None):
    """dbg_build_tooltip."""
    th, m = c.th, c.m
    font = c.font_small
    pad = c.px(5)
    tw = c.measure(font, text)
    cap_w = c.measure(font, chord) + 2 * c.px(m["KEYCAP_PAD_X"]) if chord else 0
    w = tw + (c.px(m["KEYCAP_GAP"]) + cap_w if chord else 0) + 2 * pad
    h = c.box(font) + 2 * pad
    c.rect(x, y, w, h, th["tooltip_bg"])
    c.rect(x, y, w, h, th["tooltip_border"], filled=False)
    c.text(x + pad, y + pad + c.ascent(font), text, th["tooltip_text"], font)
    if chord:
        cx = x + pad + tw + c.px(m["KEYCAP_GAP"])
        c.rect(cx, y + pad, cap_w, c.box(font), th["keycap_bg"])
        c.rect(cx, y + pad, cap_w, c.box(font), th["keycap_border"], filled=False)
        c.text(cx + (cap_w - c.measure(font, chord)) // 2, y + pad + c.ascent(font), chord, th["keycap_text"], font)


# ---------------------------------------------------------------- the picture


def world_view(c: Chrome, box):
    """What a hollow World panel frames: stands in for the 3D view."""
    x, y, w, h = box
    for j in range(h):
        t = j / max(h - 1, 1)
        c.draw.line((x, y + j, x + w - 1, y + j), fill=(int(74 - 26 * t), int(96 - 30 * t), int(52 - 14 * t)))
    step = c.px(48)
    for gx in range(x + step, x + w, step):
        c.draw.line((gx, y, gx, y + h - 1), fill=(66, 88, 48))
    for gy in range(y + step, y + h, step):
        c.draw.line((x, gy, x + w - 1, gy), fill=(66, 88, 48))


def picture(c: Chrome, out: Path):
    th, m = c.th, c.m
    W, H = c.px(1280), c.px(720)
    c.begin(W, H, 0x000000)

    bar_h = c.box(c.font_menu) + c.px(4)
    status_y = H - bar_h
    top = c.px(28)
    rail_x, col_x = c.px(4), c.px(48)
    dock_w = c.px(348)
    dock_x = W - dock_w - c.px(4)
    world_w = dock_x - col_x - c.px(4)
    console_h = c.px(214)
    world_h = status_y - top - console_h - c.px(8)

    # The World panel: a hollow frame round the view.
    world = Window(c, col_x, top, world_w, world_h, "World - level 0", hollow=True)
    head = c.box(c.font_title) + c.px(3)
    world_view(c, (col_x + c.px(1), top + head, world_w - 2 * c.px(1), world_h - head - c.px(1)))

    # The console group: a frame (title, strip) with its member under it.
    cy = top + world_h + c.px(4)
    frame_h = c.box(c.font_title) + c.px(3) + c.px(m["PAD"]) + c.px(m["TAB_H"]) + c.px(m["ROW_GAP"]) + c.px(m["PAD"]) + c.px(1)
    Window(c, col_x, cy, world_w, frame_h, "Console / History / Problems", tabs=["Output (F9)", "History (F10)", "Problems"], active=0,
           hover_tab=1, joined_below=True)
    con = Window(c, col_x, cy + frame_h, world_w, console_h - frame_h, joined_above=True, grip=True)
    con.vlist("", ["editor: path width 1.5 tiles", "editor: brush radius 1", "editor: Save all (Ctrl+S)",
                   "editor: saved 4 document(s) as text"], total=40, first=36)
    con.button("Clear")

    # The dock column: one group, its Catalog member under it.
    Window(c, dock_x, top, dock_w, frame_h, "Catalog / Locs / Spawns / Inspector",
           tabs=["Catalog (F8)", "Locs (F5)", "Inspector (F7)"], active=0, joined_below=True)
    dock = Window(c, dock_x, top + frame_h, dock_w, status_y - top - frame_h - c.px(4), joined_above=True, grip=True)
    dock.text_input("Find", "crate", focused=True)
    select_box = dock.dropdown("Kind", "loc (locations)")
    dock.label("1 to 13 of 13 loc", dim=True)
    crate = (0x8A6A3A, 0x7A5C30, 0x9B7A46)
    dock.vlist(
        (None, "name", "", "shapes", "id"),
        [
            (crate[0], "Crate", "crate_1", "10 11", "366"),
            (crate[1], "Crates", "crates_stack", "10", "355"),
            (crate[2], "Crate of bananas", "karamja_crate", "10 22", "2072"),
            (None, "Crate (open)", "crate_open", "10", "3398"),
            (crate[0], "Wooden crate", "fenwick_crate", "10", "70761"),
        ],
        selected=0, hovered=2, total=13, cells=True,
    )
    dock.label("Placement", heading=True)
    dock.checkbox("Follow the selection", True)
    dock.checkbox("Snap to the tile grid", False, hovered=True)
    dock.slider("Rotation", 2, 0, 3)
    dock.button("Rotate", chord="Q", fill=True)
    dock.button("Duplicate", chord="Ctrl+D", hovered=True, fill=True)
    dock.button("Delete", chord="Del", disabled=True, fill=True)
    dock.button("Find same")
    dock.label("Record", heading=True)
    dock.tree("fenwick_crate", 0, "open")
    dock.tree("ops", 1, "closed", selected=True)
    dock.tree("models", 1, "closed", hovered=True)
    blue, string, number = 0x569CD6, 0xCE9178, 0xB5CEA8
    dock.code([
        ("[oploc1,fenwick_crate]", [(0, 22, 0xDCDCAA)]),
        ("if (%fenwick_lantern = 0) {", [(0, 2, blue), (25, 1, number)]),
        ('    ~chatnpc("Evening.");', [(13, 10, string)]),
        ("}", []),
    ])

    # The tool rail.
    tools = ["TOOL_SELECT", "TOOL_RECT", "TOOL_HEIGHT", "TOOL_FLATTEN", "TOOL_SMOOTH", "TOOL_PAINT_UNDERLAY",
             "TOOL_PAINT_OVERLAY", "TOOL_FLAGS", "TOOL_PLACE", "TOOL_MOVE", "TOOL_ROTATE", "TOOL_DELETE",
             "TOOL_PLACE_PLAYER", "TOOL_PAINT_PATH", "TOOL_ROUGHEN", "TOOL_SCATTER", "TOOL_PLATEAU", "TOOL_REGION",
             "TOOL_LASSO", "TOOL_TIDY", "SAVE", "RUN", "PLUS", "MAP"]
    chords = list("1235678904MTXPHUBVOJY") + ["", "", "", ""]
    rail = [(icon, chords[i] if i < 20 else "", "on" if i == 8 else "hover" if i == 2 else "off" if icon == "RUN" else "") for i, icon in enumerate(tools)]
    boxes = toolbar(c, rail_x, top, c.px(38), rail, status_y - c.px(4))

    # The bars, over everything (the top layer).
    _, items = menubar(c, 0, 0, W, [
        ("menu", "File", ""), ("menu", "Edit", ""), ("menu", "View", "hover"),
        ("caption", "   Level", ""), ("item", "Auto", ""), ("item", "0", "on"), ("item", "1", ""), ("item", "2", ""),
        ("item", "3", ""), ("item", "Above", "off"),
        ("caption", "   Workspace", ""), ("item", "Build", "on"), ("item", "Terrain", ""), ("item", "Script", ""),
        ("item", "Playtest", ""),
    ])
    c.rect(0, status_y, W, bar_h, th["menu_chrome"])
    pen = c.px(m["PAD"])
    for text, ink in [("[drag] paint", "text_dim"), ("[ [ ] ] radius", "text_dim"), ("[Shift+[ ]] strength", "text_dim"),
                      ("4551,3897 L0 | m71_60", "text"), ("sel: 1 loc | Paint underlay r1 | level 0", "text"),
                      ("live attached", "text"), ("working: 3 changed", "text")]:
        c.text(pen, status_y + c.px(2) + c.ascent(c.font_menu), text, th[ink], c.font_menu)
        pen += c.measure(c.font_menu, text) + 2 * c.px(m["PAD"])

    # What floats: the View menu, open under its title; a tool's tooltip.
    view_box = items["View"]
    menu_list(c, view_box[0], view_box[1] + view_box[3], [
        ("Catalog", "F8"), ("Locs", "F5"), ("Inspector", "F7"), ("Palette", "hidden"), ("Console", "F9"),
        ("Chrome scale Auto", "on"), ("Chrome scale 150 %", ""), ("Text size Normal", "on"),
        ("Reset layout", ""), ("Snap to grid", "on"),
    ], hovered=3)
    tip = boxes[12]
    tooltip(c, tip[0] + tip[2] + c.px(8), tip[1] + c.px(18), "Place the play account on the tile", "X")

    out.parent.mkdir(parents=True, exist_ok=True)
    c.img.save(out)
    print(f"wrote {out} ({W}x{H}, q = {c.q})")


def report(c: Chrome) -> int:
    th = c.th
    pairs = [
        ("body text on the panel body", "text", "panel_body", 7.0),
        ("body text in a well", "input_text", "input_bg", 7.0),
        ("dim text on the panel body", "text_dim", "panel_body", 4.5),
        ("dim text in a well", "text_dim", "textarea_bg", 4.5),
        ("title on the title bar", "panel_title_text", "panel_title_bg", 7.0),
        ("menu-bar text on the bar", "panel_title_text", "menu_chrome", 4.5),
        ("dim text on the bar", "text_dim", "menu_chrome", 3.0),
        ("selected row text on the selection", "selection_text", "selection_bg", 7.0),
        ("plain text on the selection", "text", "selection_bg", 4.5),
        ("button text on the button", "text", "button_bg", 4.5),
        ("button text on the hovered button", "text_hover", "button_bg_hover", 4.5),
        ("keycap text on the keycap", "keycap_text", "keycap_bg", 4.5),
        ("tooltip text on the tooltip", "tooltip_text", "tooltip_bg", 7.0),
        ("menu text on the menu", "menu_text", "menu_body", 7.0),
        ("accent on the panel body", "accent", "panel_body", 3.0),
        ("accent on the selection (a pressed tool)", "accent", "selection_bg", 3.0),
        ("tick on the accent", "check_mark", "accent", 3.0),
        ("empty checkbox edge on the body", "check_box", "panel_body", 3.0),
        ("error text on the well", "error", "textarea_bg", 4.5),
    ]
    bad = 0
    print("contrast (WCAG relative luminance):")
    for what, fg, bg, need in pairs:
        ratio = contrast(th[fg], th[bg])
        flag = "" if ratio >= need else "   <-- under %.1f" % need
        bad += ratio < need
        print("  %5.2f:1  %-42s #%06X on #%06X%s" % (ratio, what, th[fg], th[bg], flag))
    yellow = [k for k, v in th.items() if isinstance(v, int) and v == 0xFFFF00]
    if yellow:
        print("pure yellow in the theme:", yellow)
        bad += 1
    print("faces at 1x:")
    for slot, f in c.faces.items():
        print("  %-28s %-14s em %2d px, ascent %2d, line box %2d" % (slot, f["face"], f["em"], f["ascent"], f["box"]))
    print("row height %d px at 1x" % (c.th["row_h"] or c.th["row_h_base"] or c.m["ROW_H"]))
    return 1 if bad else 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, default=REPO / "src/build_opt/tmp/chrome_ui_theme/preview.png")
    ap.add_argument("--q", type=int, default=4, help="the chrome scale in quarters (4 = 100 %%, 6 = 150 %%, 8 = 200 %%)")
    args = ap.parse_args(argv)
    if not 4 <= args.q <= 12:
        ap.error("--q is 4..12")
    c = Chrome(args.q)
    picture(c, args.out)
    return report(c)


if __name__ == "__main__":
    sys.exit(main())
