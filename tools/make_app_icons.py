#!/usr/bin/env python3
"""
Generate every platform's launcher icon from one source image.

ONE source of truth. Each platform gets the container format its loader
actually requires -- Android wants PNG mipmaps at five densities plus an
adaptive pair, the web wants PNG plus a real multi-size .ico, macOS wants
.icns, Windows wants .ico, and SDL wants raw RGBA it can hand
SDL_CreateRGBSurfaceFrom. A .jpg renamed to .png is not any of those.

Requires: pillow, numpy, scipy. On macOS `iconutil` (Xcode command line
tools) is also used, for the .icns; without it the .iconset is left in place
for a macOS host to finish.

Usage:
    python3 tools/make_app_icons.py res/torirs_icon.jpg
    python3 tools/make_app_icons.py res/torirs_icon.jpg --out-root /tmp/dryrun
"""

import argparse
import os
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

# --- Android -------------------------------------------------------------
# Legacy square mipmaps: the density buckets every API level understands.
ANDROID_MIPMAP_DENSITIES = {
    "mdpi": 48,
    "hdpi": 72,
    "xhdpi": 96,
    "xxhdpi": 144,
    "xxxhdpi": 192,
}

# Adaptive icons (API 26+) are drawn on a 108x108dp canvas of which only the
# central 72x72dp is guaranteed visible -- the launcher masks the rest to
# whatever shape the device uses. Authoring at 432px (=108dp @ xxxhdpi) and
# insetting the art to the safe zone is what stops a circular mask cropping
# the logo's edges.
ADAPTIVE_CANVAS = 432
ADAPTIVE_SAFE_FRACTION = 72.0 / 108.0

# Google Play listing art.
PLAY_STORE_SIZE = 512

# --- Web -----------------------------------------------------------------
FAVICON_ICO_SIZES = [16, 32, 48]
# Only what index.html and site.webmanifest actually reference. 48 already
# lives inside favicon.ico, and 180 is what apple-touch-icon.png is; a
# second copy of either under a name nothing links is just a stray file.
WEB_PNG_SIZES = [16, 32, 192, 512]
APPLE_TOUCH_SIZE = 180

# --- Windows -------------------------------------------------------------
# 256 is the largest an .ico may hold, and Explorer picks per view mode.
WINDOWS_ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]

# --- macOS ---------------------------------------------------------------
# iconutil requires exactly these names; a missing pair is a silent omission
# from the resulting .icns, so the whole ladder is spelled out.
ICNS_VARIANTS = [
    ("icon_16x16.png", 16),
    ("icon_16x16@2x.png", 32),
    ("icon_32x32.png", 32),
    ("icon_32x32@2x.png", 64),
    ("icon_128x128.png", 128),
    ("icon_128x128@2x.png", 256),
    ("icon_256x256.png", 256),
    ("icon_256x256@2x.png", 512),
    ("icon_512x512.png", 512),
    ("icon_512x512@2x.png", 1024),
]

# --- SDL -----------------------------------------------------------------
# Embedded so the desktop window has an icon with no file to find at runtime.
SDL_ICON_SIZE = 64


# The source is a badge saved on a flat white field. Anything at or above
# this on every channel is a candidate for that field; the artwork's own
# lightest tones (the cream lettering, the steel blades) sit well below it.
BACKDROP_THRESHOLD = 240

# A pixel whose every channel falls below this is outline, not body.
OUTLINE_MAX_CHANNEL = 40


def knock_out_backdrop(image):
    """Make the flat field the artwork was saved on transparent.

    Grown from the border INWARD through near-white pixels only, and kept to
    the one connected region that actually touches the border. A global
    "every white pixel becomes transparent" rule would also punch holes in
    the sword blades, the lettering and the eyes -- interior whites that are
    part of the art. Reaching those from outside means crossing the badge's
    black outline, so a connectivity test keeps them where a colour test
    alone would not.

    Returns the image unchanged if the border is not a flat light field;
    a source that was authored with real transparency has nothing to strip.
    """
    rgba = np.array(image)
    near_white = np.all(rgba[:, :, :3] >= BACKDROP_THRESHOLD, axis=2)

    # 4-connectivity: a diagonal touch is not a way out of the badge.
    labels, count = ndimage.label(near_white)
    if count == 0:
        return image

    border_labels = set(labels[0, :]) | set(labels[-1, :])
    border_labels |= set(labels[:, 0]) | set(labels[:, -1])
    border_labels.discard(0)
    if not border_labels:
        return image

    backdrop = np.isin(labels, list(border_labels))

    # Zero the RGB under the cleared pixels too. Leaving white there would
    # let it bleed back in through any filter that ignores alpha.
    rgba[backdrop] = (0, 0, 0, 0)
    return Image.fromarray(rgba, "RGBA")


def load_source(path):
    """Open the source and normalize it to a square, backdrop-free RGBA master."""
    image = Image.open(path).convert("RGBA")

    # Only strip a backdrop the source actually has. An image that already
    # carries alpha has been authored deliberately; re-cutting it would be
    # second-guessing the artist.
    if np.asarray(image)[:, :, 3].min() == 255:
        image = knock_out_backdrop(image)

    # Crop the cleared margin away entirely, so the badge fills the icon
    # instead of sitting in a transparent border that shrinks it at every
    # size. Measured on alpha alone -- getbbox() on RGBA would call a
    # transparent-but-white pixel non-empty and return the whole frame.
    box = image.getchannel("A").getbbox()
    assert box is not None, "source is fully transparent"
    image = image.crop(box)

    # Square it by padding rather than cropping: a crop silently eats the
    # edges of a logo that was not authored square, and that damage is
    # invisible until it ships.
    width, height = image.size
    if width != height:
        side = max(width, height)
        square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
        square.paste(image, ((side - width) // 2, (side - height) // 2))
        image = square

    return image


def resized(master, size):
    """Resize in premultiplied alpha.

    PIL filters each channel independently, so a fully transparent pixel's
    colour still contributes to its neighbours. That is how a knocked-out
    white field comes back as a pale fringe at 48x48 -- the box removed
    above, returning at exactly the sizes where it is most visible.
    Premultiplying makes a transparent pixel contribute nothing.
    """
    source = np.asarray(master).astype(np.float64)
    alpha = source[:, :, 3:4] / 255.0
    premultiplied = np.concatenate([source[:, :, :3] * alpha, source[:, :, 3:4]], axis=2)

    small = Image.fromarray(
        premultiplied.round().clip(0, 255).astype(np.uint8), "RGBA"
    ).resize((size, size), Image.LANCZOS)

    out = np.asarray(small).astype(np.float64)
    out_alpha = out[:, :, 3:4] / 255.0
    straight = np.divide(
        out[:, :, :3],
        out_alpha,
        out=np.zeros_like(out[:, :, :3]),
        where=out_alpha > 0,
    )
    recombined = np.concatenate([straight, out[:, :, 3:4]], axis=2)
    return Image.fromarray(recombined.round().clip(0, 255).astype(np.uint8), "RGBA")


def circular(image):
    """Mask an image to a circle, for Android's ic_launcher_round."""
    size = image.size[0]
    # Supersample the mask so the circle's edge is antialiased rather than
    # a stair-step.
    scale = 4
    mask = Image.new("L", (size * scale, size * scale), 0)
    from PIL import ImageDraw

    ImageDraw.Draw(mask).ellipse((0, 0, size * scale - 1, size * scale - 1), fill=255)
    mask = mask.resize((size, size), Image.LANCZOS)

    out = image.copy()
    out.putalpha(mask)
    return out


def dominant_background(master):
    """Pick the plate colour that shows behind the art, from the art itself.

    Used for the Android adaptive background and the maskable web icon, both
    of which show wherever the launcher's mask is wider than the artwork. It
    is sampled from OPAQUE pixels only: the corners are transparent now, and
    a plate averaged from them would paint the white box straight back on.
    """
    rgba = np.asarray(master)
    opaque = rgba[rgba[:, :, 3] > 128][:, :3]
    assert opaque.size > 0

    # Drop the near-black outline before voting. It is the single most
    # common colour in a pixel-art badge (a thick border around every
    # element), but it is a BORDER, not the body -- a plate painted to match
    # it reads as a black box with the icon floating on it, where a plate
    # matching the body reads as the badge continuing past the mask.
    body = opaque[opaque.max(axis=1) >= OUTLINE_MAX_CHANNEL]
    if body.size > 0:
        opaque = body

    # Modal colour, quantized to 5 bits per channel so that JPEG's near
    # duplicates of one shade count as that shade rather than splitting the
    # vote between a few hundred neighbours.
    quantized = (opaque // 8).astype(np.uint32)
    keys = quantized[:, 0] * 1024 + quantized[:, 1] * 32 + quantized[:, 2]
    top = int(np.argmax(np.bincount(keys)))
    return "#%02X%02X%02X" % ((top // 1024) * 8, ((top // 32) % 32) * 8, (top % 32) * 8)


def write_png(image, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    image.save(path, "PNG", optimize=True)
    print("  %s (%dx%d PNG)" % (path, image.size[0], image.size[1]))


def emit_android(master, out_root):
    res = os.path.join(out_root, "android", "src", "main", "res")

    for density, size in ANDROID_MIPMAP_DENSITIES.items():
        icon = resized(master, size)
        write_png(icon, os.path.join(res, "mipmap-" + density, "ic_launcher.png"))
        write_png(
            circular(icon), os.path.join(res, "mipmap-" + density, "ic_launcher_round.png")
        )

    # Adaptive foreground: art inset into the safe zone of a transparent
    # 432x432 canvas.
    safe = int(ADAPTIVE_CANVAS * ADAPTIVE_SAFE_FRACTION)
    foreground = Image.new("RGBA", (ADAPTIVE_CANVAS, ADAPTIVE_CANVAS), (0, 0, 0, 0))
    art = resized(master, safe)
    offset = (ADAPTIVE_CANVAS - safe) // 2
    foreground.paste(art, (offset, offset), art)
    write_png(
        foreground, os.path.join(res, "drawable-nodpi", "ic_launcher_foreground.png")
    )

    background = dominant_background(master)

    adaptive_xml = (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        "<!--\n"
        "  Adaptive launcher icon (API 26+). Older devices ignore this file\n"
        "  entirely and use the square mipmap-<density>/ic_launcher.png beside\n"
        "  it, which is why both still ship: minSdk here is 21.\n"
        "-->\n"
        '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
        '    <background android:drawable="@color/ic_launcher_background" />\n'
        '    <foreground android:drawable="@drawable/ic_launcher_foreground" />\n'
        "</adaptive-icon>\n"
    )
    for name in ("ic_launcher.xml", "ic_launcher_round.xml"):
        path = os.path.join(res, "mipmap-anydpi-v26", name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as handle:
            handle.write(adaptive_xml)
        print("  %s" % path)

    colors_path = os.path.join(res, "values", "ic_launcher_background.xml")
    os.makedirs(os.path.dirname(colors_path), exist_ok=True)
    with open(colors_path, "w") as handle:
        handle.write(
            '<?xml version="1.0" encoding="utf-8"?>\n'
            "<resources>\n"
            '    <color name="ic_launcher_background">%s</color>\n'
            "</resources>\n" % background
        )
    print("  %s (background %s)" % (colors_path, background))

    write_png(
        resized(master, PLAY_STORE_SIZE),
        os.path.join(out_root, "android", "play_store_icon.png"),
    )


def emit_web(master, out_root):
    web = os.path.join(out_root, "src", "web")

    for size in WEB_PNG_SIZES:
        write_png(resized(master, size), os.path.join(web, "icon-%d.png" % size))

    write_png(
        resized(master, APPLE_TOUCH_SIZE), os.path.join(web, "apple-touch-icon.png")
    )

    # A maskable icon must keep its content inside the central 80% -- Android
    # Chrome crops the rest to the platform's mask shape.
    maskable = Image.new("RGBA", (512, 512), dominant_background(master))
    inset = int(512 * 0.8)
    art = resized(master, inset)
    maskable.paste(art, ((512 - inset) // 2, (512 - inset) // 2), art)
    write_png(maskable, os.path.join(web, "icon-maskable-512.png"))

    # A real multi-size .ico, not a PNG with the extension changed.
    ico_path = os.path.join(web, "favicon.ico")
    os.makedirs(os.path.dirname(ico_path), exist_ok=True)
    resized(master, max(FAVICON_ICO_SIZES)).save(
        ico_path, "ICO", sizes=[(s, s) for s in FAVICON_ICO_SIZES]
    )
    print("  %s (ICO %s)" % (ico_path, "/".join(str(s) for s in FAVICON_ICO_SIZES)))

    manifest_path = os.path.join(web, "site.webmanifest")
    with open(manifest_path, "w") as handle:
        handle.write(
            "{\n"
            '  "name": "ToriRS",\n'
            '  "short_name": "ToriRS",\n'
            '  "icons": [\n'
            '    { "src": "icon-192.png", "sizes": "192x192", "type": "image/png" },\n'
            '    { "src": "icon-512.png", "sizes": "512x512", "type": "image/png" },\n'
            '    { "src": "icon-maskable-512.png", "sizes": "512x512",'
            ' "type": "image/png", "purpose": "maskable" }\n'
            "  ],\n"
            '  "background_color": "#14100c",\n'
            '  "theme_color": "#14100c",\n'
            '  "display": "standalone"\n'
            "}\n"
        )
    print("  %s" % manifest_path)


def emit_desktop(master, out_root):
    icons = os.path.join(out_root, "res", "icons")
    os.makedirs(icons, exist_ok=True)

    # Windows .ico for the executable and the window class.
    ico_path = os.path.join(icons, "torirs.ico")
    resized(master, max(WINDOWS_ICO_SIZES)).save(
        ico_path, "ICO", sizes=[(s, s) for s in WINDOWS_ICO_SIZES]
    )
    print("  %s (ICO %s)" % (ico_path, "/".join(str(s) for s in WINDOWS_ICO_SIZES)))

    # macOS .icns, built the canonical way: an .iconset directory handed to
    # iconutil. PIL can write .icns but produces a narrower ladder.
    iconset = os.path.join(icons, "torirs.iconset")
    if os.path.isdir(iconset):
        shutil.rmtree(iconset)
    os.makedirs(iconset)
    for name, size in ICNS_VARIANTS:
        resized(master, size).save(os.path.join(iconset, name), "PNG")

    icns_path = os.path.join(icons, "torirs.icns")
    if shutil.which("iconutil"):
        subprocess.run(
            ["iconutil", "-c", "icns", iconset, "-o", icns_path], check=True
        )
        print("  %s (ICNS, 10 variants)" % icns_path)
        shutil.rmtree(iconset)
    else:
        print("  iconutil unavailable; left %s for a macOS host" % iconset)

    # A plain PNG for Linux .desktop entries / hicolor themes.
    write_png(resized(master, 256), os.path.join(icons, "torirs-256.png"))


def emit_sdl_embedded(master, out_root):
    """Emit the window icon as C, so the desktop window needs no icon file."""
    icon = resized(master, SDL_ICON_SIZE)
    pixels = icon.tobytes()  # RGBA8888, row-major, top-down.

    path = os.path.join(out_root, "src", "platform", "platform_app_icon.c")
    os.makedirs(os.path.dirname(path), exist_ok=True)

    lines = []
    lines.append("/*")
    lines.append(" * GENERATED FILE -- do not edit.")
    lines.append(" *")
    lines.append(" * Produced by tools/make_app_icons.py from the one source icon. The")
    lines.append(" * window icon is embedded rather than loaded so that a desktop run")
    lines.append(" * from any working directory still has one; there is no path to get")
    lines.append(" * wrong and no file to ship alongside the binary.")
    lines.append(" */")
    lines.append('#include "platform/platform_app_icon.h"')
    lines.append("")
    lines.append("const int platform_app_icon_width = %d;" % SDL_ICON_SIZE)
    lines.append("const int platform_app_icon_height = %d;" % SDL_ICON_SIZE)
    lines.append("")
    lines.append("/* RGBA8888, row-major, top-down. */")
    lines.append(
        "const unsigned char platform_app_icon_rgba[%d] = {" % len(pixels)
    )
    for start in range(0, len(pixels), 16):
        chunk = pixels[start : start + 16]
        lines.append("    " + " ".join("0x%02X," % b for b in chunk))
    lines.append("};")
    lines.append("")

    with open(path, "w") as handle:
        handle.write("\n".join(lines))
    print("  %s (%d bytes RGBA)" % (path, len(pixels)))

    header_path = os.path.join(out_root, "src", "platform", "platform_app_icon.h")
    with open(header_path, "w") as handle:
        handle.write(
            "/*\n"
            " * The application icon, embedded as RGBA8888 for SDL_SetWindowIcon\n"
            " * and the Win32 window class. GENERATED -- see\n"
            " * tools/make_app_icons.py.\n"
            " */\n"
            "#ifndef PLATFORM_APP_ICON_H\n"
            "#define PLATFORM_APP_ICON_H\n"
            "\n"
            "extern const int platform_app_icon_width;\n"
            "extern const int platform_app_icon_height;\n"
            "extern const unsigned char platform_app_icon_rgba[];\n"
            "\n"
            "#endif /* PLATFORM_APP_ICON_H */\n"
        )
    print("  %s" % header_path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", help="the one source image (jpg/png/...)")
    parser.add_argument(
        "--out-root",
        default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
        help="repo root to write into (default: this checkout)",
    )
    args = parser.parse_args()

    if not os.path.isfile(args.source):
        sys.exit("source image not found: %s" % args.source)

    master = load_source(args.source)
    if master.size[0] < 512:
        print(
            "WARNING: source is %dx%d; 1024x1024 or larger is wanted for the "
            "macOS 512@2x variant." % master.size,
            file=sys.stderr,
        )

    print("source: %s -> %dx%d square RGBA master" % (args.source, *master.size))
    print("android:")
    emit_android(master, args.out_root)
    print("web:")
    emit_web(master, args.out_root)
    print("desktop:")
    emit_desktop(master, args.out_root)
    print("sdl/win32 embedded:")
    emit_sdl_embedded(master, args.out_root)
    print("done.")


if __name__ == "__main__":
    main()
