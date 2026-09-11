# ToriRSChrome browser fonts

These are real outline webfonts derived from the exact cache-font masks already
baked into `src/engine/torirs_debug_font_baked.c`. They keep browser text
selectable, searchable, editable, and available to accessibility APIs; they are
not sprite or canvas text.

| CSS face | Source | Authored size | Baseline / line box |
| --- | --- | ---: | ---: |
| `ToriRS Chrome` 400 | Body / cache archive 495 (`p12`) | 12px | 12px / 16px |
| `ToriRS Chrome` 700 | Menu / cache archive 496 (`b12`) | 12px | 12px / 16px |
| `ToriRS Chrome Small` 400 | Small / cache archive 494 | 10px | 10px / 12px |

Every nonzero baked mask pixel is represented by an integer-aligned TrueType
outline cell. One source pixel is 64 font units, so it is one CSS pixel when a
face is used at its authored size. Advances, offsets, baseline, and descender
space come directly from the baked C source and its generated metrics header.
The printable ASCII range and U+00A3 POUND SIGN are mapped. The source bake has
no drawn backtick or vertical-bar bitmap, so U+0060 and U+007C intentionally use
the source font's advance-only width, just like space.

The `.woff` files are WOFF 1 for old Android WebView as well as modern browser
engines. The `.ttf` files are the uncompressed fallback. The `.eot` files are
uncompressed EOT Classic 0x00020002 wrappers for Internet Explorer 6-8 / the
Windows XP MSHTML host. Their required root string is the stable `file:///`
prefix because that host stages the compatibility page at an absolute local
file URL whose directory can vary. `manifest.json` records the source and
output hashes.

Recommended shared CSS (paths are relative to the plugin-chrome page after the
host copies this directory beside it):

```css
@font-face {
  font-family: "ToriRS Chrome";
  src: url("font/ToriRSBody.eot");
  src: url("font/ToriRSBody.eot?#iefix") format("embedded-opentype"),
       url("font/ToriRSBody.woff") format("woff"),
       url("font/ToriRSBody.ttf") format("truetype");
  font-style: normal;
  font-weight: 400;
}

@font-face {
  font-family: "ToriRS Chrome";
  src: url("font/ToriRSMenu.eot");
  src: url("font/ToriRSMenu.eot?#iefix") format("embedded-opentype"),
       url("font/ToriRSMenu.woff") format("woff"),
       url("font/ToriRSMenu.ttf") format("truetype");
  font-style: normal;
  font-weight: 700;
}

@font-face {
  font-family: "ToriRS Chrome Small";
  src: url("font/ToriRSSmall.eot");
  src: url("font/ToriRSSmall.eot?#iefix") format("embedded-opentype"),
       url("font/ToriRSSmall.woff") format("woff"),
       url("font/ToriRSSmall.ttf") format("truetype");
  font-style: normal;
  font-weight: 400;
}
```

The dedicated XP stylesheet can be simpler and should avoid the `?#iefix`
query on a local `file:///` URL. IE 6-8 only needs the EOT source, and the CSS
family should match the embedded EOT family:

```css
@font-face {
  font-family: "ToriRS Chrome";
  src: url("font/ToriRSBody.eot");
  font-style: normal;
  font-weight: 400;
}

@font-face {
  font-family: "ToriRS Chrome";
  src: url("font/ToriRSMenu.eot");
  font-style: normal;
  font-weight: 700;
}
```

Use Body and Menu at `font-size: 12px; line-height: 16px`, and Small at
`font-size: 10px; line-height: 12px`. Avoid fractional transforms, zoom, and
letter spacing on text containers if exact pixel-grid rendering matters. The
browser/OS rasterizer can still apply platform antialiasing, especially at a
fractional device scale; the outlines and layout metrics themselves remain
exact.

Regenerate from the repository root:

```sh
python3 -m venv /tmp/torirs-chrome-fonts
/tmp/torirs-chrome-fonts/bin/pip install -r tools/torirs_chrome_fonts.requirements.txt
/tmp/torirs-chrome-fonts/bin/python tools/torirs_chrome_fonts.py
```
