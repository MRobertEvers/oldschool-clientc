# The editor's vendored UI faces (docs/CHROME_SCALE.md "The UI faces"): Inter,
# JetBrains Mono and the Lucide icon subset under engine/fonts/vendor/, baked
# into the client as TrueType bytes and drawn by ui/torirs_chrome_bake.c in
# the chrome's UI font slots (TORIRS_CHROME_FONT_UI and its siblings), which
# the editor's theme (torirs_chrome_theme_editor) names.
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS and the content paths are defined: rule lines here name no
# such variable (it would expand empty), recipes use them freely.

# The TrueType bytes. ui/torirs_chrome_bake.c reads them through
# ToriRSChromeUiFont_File, so every binary that links the bake links this.
MAPED_CLIENT_SRCS += engine/torirs_chrome_ui_fonts_baked.c

# ---- the baked files are reproducible, not a comment --------------------------
#
# tools/chrome_ui_fonts.py writes, from the vendored TTFs and nothing else:
#   engine/torirs_chrome_ui_fonts_baked.{c,h}   each face's bytes
#   ui/uitree_ui_font_metrics.h                 each face's units per em,
#                                               ascent, descent and the advance
#                                               of bytes 32..255, in font units
# No cache, no compiler. Needs Python 3 with fontTools 4.59.2 (importable, or
# FONTTOOLS_DIR naming the directory that holds it).
#
# chrome-ui-fonts        rewrite them. Run after changing a vendored face
#                        (tools/chrome_ui_fonts_vendor.py, chrome_ui_icons_vendor.py)
#                        and commit what it writes.
# chrome-ui-fonts-check  write nothing; FAIL when a committed file differs from
#                        a rerun. A clean tree passes.
#
# The pixel size each slot draws at is not baked: it is k_dbg_ui_faces in
# ui/uitree_debug_overlay.c. To look at a change of size, face or theme
# without building the client:
#   python3 $(REPO_ROOT)/tools/chrome_ui_theme_preview.py --out <png> [--q 6]
.PHONY: chrome-ui-fonts chrome-ui-fonts-check
chrome-ui-fonts:
	python3 $(REPO_ROOT)/tools/chrome_ui_fonts.py

chrome-ui-fonts-check:
	python3 $(REPO_ROOT)/tools/chrome_ui_fonts.py --check
