"""
Build res/hitsplats_osrs239.png — every hitsplat sprite cache.osrs239 ships, named.

Three things go on the sheet and they have three different authorities:

  * the sprite name and id      the cache's own gameval table (pack/8_sprites.pack)
  * the record and its role     derived from configs/all.hitsplat: which record draws
                                the sprite, and whether a selector wraps it as the
                                "me" leaf, the tinted "other" leaf or the max-hit leaf
  * the identity               RuneLite's HitsplatID for this id space, corroborated
                                against the glyph (a shield is a shield, an up arrow
                                is CYAN_UP, two brains are the sanity pair)

Anything with no source for the third column is labelled for what it draws and
said to be unnamed, rather than given a colour word and left to look identified.
"""
import io, os, struct, sys
from PIL import Image, ImageDraw, ImageFont

# ---- reading what the tree already holds ---------------------------------
SPRITES = 'OSRS-Content/osrs239-content/sprites'
CONFIG  = 'OSRS-Content/osrs239-content/configs/all.hitsplat'
COMPACK = 'OSRS-Content/osrs239-content/configs/all.hitsplat.compack'
SPRPACK = 'OSRS-Content/osrs239-content/pack/8_sprites.pack'

def read_bmp32(path):
    """The 32-bit BGRA bottom-up bmp `bmp_write_file` writes. Returns (w, h, [RGBA...])."""
    d = open(path, 'rb').read()
    assert d[:2] == b'BM', path
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    bpp = struct.unpack_from('<H', d, 28)[0]
    assert bpp == 32, (path, bpp)
    px = []
    for y in range(h - 1, -1, -1):
        row = off + y * w * 4
        for x in range(w):
            b, g, r, a = d[row + x*4: row + x*4 + 4]
            px.append((r, g, b, a))
    return w, h, px

def load_meta(d):
    meta = {}
    for line in io.open(os.path.join(d, 'pack.meta'), encoding='utf-8'):
        line = line.strip()
        if line.startswith('//') or '=' not in line: continue
        k, v = line.split('=', 1)
        meta[k] = v
    return meta

def sprite_ids():
    ids = {}
    for line in io.open(SPRPACK, encoding='utf-8'):
        line = line.strip()
        if '=' in line and not line.startswith('//'):
            i, n = line.split('=', 1)
            ids[n] = int(i)
    return ids

def records():
    recs, order, cur = {}, [], None
    for line in io.open(CONFIG, encoding='utf-8'):
        line = line.strip()
        if line.startswith('['):
            cur = line[1:-1]; recs[cur] = {}; order.append(cur)
        elif '=' in line and cur:
            k, v = line.split('=', 1); recs[cur][k] = v
    return recs, order

def record_ids():
    ids = {}
    for line in io.open(COMPACK, encoding='utf-8'):
        line = line.strip()
        if '=' in line and not line.startswith('//'):
            i, n = line.split('=', 1)
            ids[n] = int(i)
    return ids

# ---- the sheet -----------------------------------------------------------

OUT = sys.argv[1] if len(sys.argv) > 1 else 'res/hitsplats_osrs239.png'

BG      = (26, 27, 31)
PANEL   = (36, 38, 44)
PANEL2  = (32, 34, 39)
INK     = (238, 238, 242)
DIM     = (150, 152, 162)
ACCENT  = (240, 186, 92)
GREEN   = (126, 196, 130)
RED     = (226, 116, 110)
GOLD    = (214, 176, 112)

F   = '/System/Library/Fonts/Supplemental/Arial.ttf'
FB  = '/System/Library/Fonts/Supplemental/Arial Bold.ttf'
f10 = ImageFont.truetype(F, 11)
f11 = ImageFont.truetype(F, 12)
f12 = ImageFont.truetype(F, 13)
b12 = ImageFont.truetype(FB, 13)
b15 = ImageFont.truetype(FB, 16)
b22 = ImageFont.truetype(FB, 24)

SCALE = 3

def sprite_image(name):
    d = os.path.join(SPRITES, name)
    meta = load_meta(d)
    full_w, full_h, cw, ch, ox, oy = [int(v) for v in meta['sprite0'].split(',')]
    im = Image.new('RGBA', (full_w, full_h), (0, 0, 0, 0))
    bmp = os.path.join(d, '0.bmp')
    if os.path.exists(bmp):
        w, h, px = read_bmp32(bmp)
        crop = Image.new('RGBA', (w, h))
        crop.putdata(px)
        im.paste(crop, (ox, oy))
    return im

def checker(size, a=(62,63,70), b=(52,53,60), step=6):
    im = Image.new('RGB', size, a)
    d = ImageDraw.Draw(im)
    for y in range(0, size[1], step):
        for x in range(0, size[0], step):
            if (x // step + y // step) % 2:
                d.rectangle([x, y, x+step-1, y+step-1], fill=b)
    return im

SID = sprite_ids()
RID = record_ids()
RECS, ORDER = records()
SPRITE_OF = {n: r['sprite'] for n, r in RECS.items() if 'sprite' in r}
BY_ID = {v: k for k, v in RID.items()}

def sprite_for_record(rec_id):
    return SPRITE_OF[BY_ID[rec_id]]

# --- the identifications ---------------------------------------------------
#
# A family is one appearance in three skins: the leaf a selector shows you for
# your own hit, the darker leaf it shows for someone else's when hitsplat tinting
# is on, and (where the family has one) the max-hit leaf. The wrapper ids are the
# ones a server sends; the leaves are what gets drawn.
#
# (title, runelite, wrapper note, [(role, leaf record id), ...])
FAMILIES = [
    ("Block — a miss, the blue 0", "BLOCK_ME 12 / BLOCK_OTHER 13", "12 / 13",
     [("me", 26), ("other (tinted)", 27)]),
    ("Damage — the ordinary hit", "DAMAGE_ME 16 / OTHER 17 / MAX 43", "16 / 17 / 43",
     [("me", 28), ("other (tinted)", 29), ("max hit", 48)]),
    ("Cyan damage — shield", "DAMAGE_*_CYAN 18 / 19 / 44", "18 / 19 / 44",
     [("me", 34), ("other (tinted)", 35), ("max hit", 51)]),
    ("Orange damage — armour", "DAMAGE_*_ORANGE 20 / 21 / 45", "20 / 21 / 45",
     [("me", 36), ("other (tinted)", 37), ("max hit", 52)]),
    ("Yellow damage — up arrow", "DAMAGE_*_YELLOW 22 / 23 / 46", "22 / 23 / 46",
     [("me", 30), ("other (tinted)", 31), ("max hit", 49)]),
    ("White damage — down arrow", "DAMAGE_*_WHITE 24 / 25 / 47", "24 / 25 / 47",
     [("me", 32), ("other (tinted)", 33), ("max hit", 50)]),
    ("Poise damage", "DAMAGE_*_POISE 53 / 54 / 55", "53 / 54 / 55",
     [("me", 56), ("other (tinted)", 57), ("max hit", 58)]),
    ("Poison", "POISON 65", "65 / 66 / 2",
     [("me", 68), ("other (tinted)", 69), ("max hit", 70)]),
    ("Prayer drain — down arrow", "PRAYER_DRAIN 60", "59 / 60 / 61",
     [("me", 62), ("other (tinted)", 63), ("max hit", 64)]),
    ("Cyan up", "CYAN_UP 11", "10 / 11",
     [("me", 39), ("other (tinted)", 40)]),
    ("Cyan down", "CYAN_DOWN 15", "14 / 15",
     [("me", 41), ("other (tinted)", 42)]),
    ("Green up/down blob", "no external name", "8 / 9  (the poise glyph, in green)",
     [("me", 7), ("other (tinted)", 38)]),
]

# (record id, identity, note)
STANDALONE = [
    (0,  "Corruption",       "RuneLite CORRUPTION 0"),
    (1,  "Blocked / immune", "icon only — no number drawn"),
    (3,  "Disease blocked",  "RuneLite DISEASE_BLOCKED 3"),
    (4,  "Disease",          "RuneLite DISEASE 4"),
    (5,  "Venom",            "RuneLite VENOM 5"),
    (6,  "Heal",             "RuneLite HEAL 6"),
    (67, "Bleed",            "RuneLite BLEED 67"),
    (71, "Sanity drain",     "RuneLite SANITY_DRAIN 71"),
    (72, "Sanity restore",   "RuneLite SANITY_RESTORE 72"),
    (73, "Doom",             "RuneLite DOOM 73"),
    (74, "Burn",             "RuneLite BURN 74"),
    (75, "Frost — bright",   "unnamed — pairs with 76"),
    (76, "Frost — dark",     "unnamed — the tinted half"),
    (78, "Armour burst",     "unnamed — orange glyph, flared"),
    (79, "Cog — bright",     "unnamed — trio 79/80/81"),
    (80, "Cog — dark",       "unnamed — reads as tinted"),
    (81, "Cog — mid",        "unnamed — reads as max hit"),
]

EMPTY = ["hitmark_48", "hitmark_49", "hitmark_50", "hitmark_51", "hitmark_52", "hitmark_53"]

# --- layout ----------------------------------------------------------------
W = 1480
CELL = 108           # sprite tile, sprite drawn at SCALE
FAM_W, FAM_H = 700, 150
sheet = Image.new('RGB', (W, 2000), BG)
d = ImageDraw.Draw(sheet)

def tile(x, y, sprite_name, caption_lines, cap_colours=None):
    """One sprite on a checkerboard with its caption lines under it."""
    im = sprite_image(sprite_name)
    im = im.resize((im.width*SCALE, im.height*SCALE), Image.NEAREST)
    bg = checker((im.width, im.height))
    bg.paste(im, (0, 0), im)
    sheet.paste(bg, (x + (CELL - im.width)//2, y))
    d.rectangle([x + (CELL-im.width)//2 - 1, y - 1,
                 x + (CELL-im.width)//2 + im.width, y + im.height],
                outline=(70, 72, 80))
    ty = y + im.height + 5
    for i, line in enumerate(caption_lines):
        col = (cap_colours or {}).get(i, DIM if i else INK)
        d.text((x + CELL//2, ty), line, font=f10 if i else f11, fill=col, anchor='mt')
        ty += 13

y = 24
d.text((28, y), "OSRS-239 hitsplat sprites", font=b22, fill=INK); y += 34
d.text((28, y), "every sprite config group 32 draws in cache.osrs239 — 49 in use, 6 empty packs. "
                "Sprite names and ids are the cache's own (pack/8_sprites.pack); records and roles "
                "are read out of configs/all.hitsplat;", font=f12, fill=DIM); y += 17
d.text((28, y), "identities are RuneLite's HitsplatID for this id space, each one checked against "
                "the glyph. What no source names is labelled for what it draws and marked unnamed.",
       font=f12, fill=DIM); y += 30

d.text((28, y), "WRAPPED FAMILIES", font=b15, fill=ACCENT); y += 20
d.text((28, y), "One appearance in three skins. The ids on the left are the SELECTOR records a server sends — they ask the player's "
                "“Hitsplat tinting” / “Max hit hitsplats” setting and", font=f11, fill=DIM); y += 15
d.text((28, y), "resolve to one of the leaves below. Sending a leaf id instead switches both settings off for that hit.",
       font=f11, fill=DIM); y += 22

fam_top = y
for i, (title, rl, wrappers, leaves) in enumerate(FAMILIES):
    col, row = i % 2, i // 2
    x = 28 + col * (FAM_W + 24)
    fy = fam_top + row * FAM_H
    d.rectangle([x, fy, x + FAM_W - 10, fy + FAM_H - 14], fill=PANEL)
    d.text((x + 14, fy + 10), title, font=b12, fill=INK)
    d.text((x + 14, fy + 27), rl, font=f10, fill=GREEN if 'no external name' not in rl else GOLD)
    d.text((x + 14, fy + 42), "send: " + wrappers, font=f10, fill=DIM)
    tx = x + 216
    for role, rec in leaves:
        sp = sprite_for_record(rec)
        tile(tx, fy + 12, sp, [role, f"{sp}  ({SID[sp]})", f"record {rec}"])
        tx += CELL + 40
y = fam_top + ((len(FAMILIES) + 1) // 2) * FAM_H + 8

d.text((28, y), "STANDALONE LEAVES", font=b15, fill=ACCENT); y += 20
d.text((28, y), "No selector wraps these: the id a server sends is the appearance it gets. A gold caption means no source names it — "
                "the label is what it draws.", font=f11, fill=DIM); y += 22
PER = 6
SCELL_W, SCELL_H = (W - 56) // PER, 158
for i, (rec, identity, note) in enumerate(STANDALONE):
    cx = 28 + (i % PER) * SCELL_W
    cy = y + (i // PER) * SCELL_H
    d.rectangle([cx, cy, cx + SCELL_W - 12, cy + SCELL_H - 12], fill=PANEL2)
    sp = sprite_for_record(rec)
    tile(cx + (SCELL_W - 12 - CELL)//2, cy + 10, sp,
         [identity, f"{sp}  ({SID[sp]})", f"record {rec}", note],
         {0: INK if 'unnamed' not in note else GOLD})
y += ((len(STANDALONE) + PER - 1) // PER) * SCELL_H + 8

d.text((28, y), "EMPTY PACKS", font=b15, fill=ACCENT); y += 20
d.text((28, y), "The cache ships six more hitmark packs (6832-6837). Each is a 25×25 sprite with a one-colour palette and no pixels, "
                "and no hitsplat record points at one.", font=f11, fill=DIM); y += 22
for i, n in enumerate(EMPTY):
    cx = 28 + i * (CELL + 40)
    tile(cx, y, n, [n, f"({SID[n]})", "empty"])
y += CELL + 40

d.text((28, y), "Sources: sprite names — the cache's gameval table. Roles — the opcode 17/18 selectors in configs/all.hitsplat, "
                "keyed on varbit 10236 (hitsplat tinting) and 14196 (max hit).", font=f10, fill=DIM); y += 14
d.text((28, y), "Identities — net.runelite.api.HitsplatID, agreeing with Near-Reality/Zenyte's HitType where the two overlap "
                "(SHIELD 18/19, ARMOUR 20/21, CHARGE 22/23, DISCHARGE 24/25).", font=f10, fill=DIM); y += 14
d.text((28, y), "Note: RuneLite puts POISON at 65, i.e. the green splash family. The green up/down-blob family (records 7/8/9/38) that "
                "all.hitsplat.compack calls “poison” is a different glyph and has no external name.", font=f10, fill=RED); y += 22

sheet = sheet.crop((0, 0, W, y))
sheet.save(OUT)
print(OUT, sheet.size)
