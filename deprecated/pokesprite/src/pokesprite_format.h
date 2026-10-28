#ifndef POKESPRITE_FORMAT_H
#define POKESPRITE_FORMAT_H

/*
 * PokeSprite sheets: the asset the 2D client draws instead of a model.
 *
 * One sheet is one subject playing one sequence, in every facing it has:
 *
 *   npc/<npc type>/<seq>             a whole npc, RGBA
 *   layer/<m|f>/kit/<idk id>/<seq>   one identity kit of a player, LAYER data
 *   layer/<m|f>/obj/<obj id>/<seq>   one worn obj of a player, LAYER data
 *
 * <seq> is -1 for the bind pose. The key is the whole identity of a sheet: it
 * is what the client asks io_server for (GET /pokesprite/<key>) and what the
 * store's index is keyed on, so there is one spelling of "which sheet".
 *
 * ## The file (.pss), little-endian
 *
 *   char[4]  magic "PSS1"
 *   u8       kind          POKESPRITE_KIND_*
 *   u8       facings       4 (down, left, right, up), or 1
 *   u16      frames        frames per facing
 *   u16      cell_w, cell_h
 *   i16      origin_x, origin_y   the model origin's pixel in a cell
 *   i16      ground_x, ground_y   where the subject stands, for its shadow
 *   u16      duration_ms[frames]
 *   u8       material_count       LAYER only, else 0
 *   per material:
 *     u8     base_rgb[3]
 *     u8     steps               ramp length, 1..3
 *     u8     flags               POKESPRITE_MAT_*
 *     u8     hsl16_count
 *     u16    hsl16[hsl16_count]  the model colours it came from: a design
 *                                recolour replaces one of these
 *   u32      png_size
 *   u8       png[png_size]       8-bit RGBA, the cells in the grid below
 *
 * The grid: each facing is a band of `rows` rows of `per_row` cells, frames
 * left to right, wrapping -- facing f, frame i is the cell at column
 * i % per_row of row f * rows + i / per_row. per_row is every frame
 * (rows = 1) unless that row would pass POKESPRITE_MAX_ROW_PX, the widest
 * image the tree's PNG reader takes; then as many cells as fit
 * (PokeSprite_FramesPerRow). Both are derived from the header, not stored.
 *
 * An RGBA sheet's pixels are colours. A LAYER sheet's pixels are data:
 *
 *   R  material index into the table above
 *   G  shade band, 0 darkest
 *   B  depth: 128 + (camera z - zoom) / 2, nearer is smaller; 255 = none
 *   A  255 covered, 0 not
 *
 * and the client composites a player's layers by that depth, colouring each
 * pixel from its material's ramp (see tools/pokesprite/layers.py, which is the
 * reference compositor).
 */

#include <stdbool.h>
#include <stdint.h>

#define POKESPRITE_MAGIC "PSS1"

/*
 * The client type the PokeSprite client logs in with (the login header's
 * clientType, which the server keeps on the player). Not one of the
 * reference's values (1 desktop ... 10 this client's desktop, 2/3/7/8
 * mobile): the stock interface scripts branch only on the mobile ones, so
 * this gets the desktop layout, and the server can tell a 2D client from a
 * 3D one -- ToriRSServer_PlayerIsPokesprite.
 */
#define POKESPRITE_CLIENT_TYPE 31

enum PokeSpriteKind
{
    POKESPRITE_KIND_RGBA = 1,
    POKESPRITE_KIND_LAYER = 2,
};

enum
{
    POKESPRITE_MAT_HEAD = 1u << 0, /* drawn by a head kit: eyes may go here */
    POKESPRITE_MAT_SKIN = 1u << 1, /* the skin identity colour: the face */
};

#define POKESPRITE_MAX_FRAMES 512
#define POKESPRITE_MAX_MATERIALS 64
#define POKESPRITE_MAX_MATERIAL_HSL 32
#define POKESPRITE_DEPTH_NONE 255
#define POKESPRITE_MAX_ROW_PX 4096

struct PokeSpriteMaterial
{
    uint8_t base_rgb[3];
    uint8_t steps;
    uint8_t flags;
    uint8_t hsl16_count;
    uint16_t hsl16[POKESPRITE_MAX_MATERIAL_HSL];
};

struct PokeSpriteSheet
{
    enum PokeSpriteKind kind;
    int facings;
    int frames;
    int cell_w;
    int cell_h;
    int origin_x;
    int origin_y;
    int ground_x;
    int ground_y;
    uint16_t duration_ms[POKESPRITE_MAX_FRAMES];
    int material_count;
    struct PokeSpriteMaterial materials[POKESPRITE_MAX_MATERIALS];
    /* Borrowed from the parsed buffer: valid while it is. */
    const uint8_t* png;
    int png_size;
};

/**
 * Parse a .pss. Returns false when the bytes are not a well-formed sheet --
 * which is data from the network or the disk, so a legitimate answer, not a
 * contract violation. `out->png` points into `data`.
 */
bool
PokeSprite_ParseSheet(
    const uint8_t* data,
    int size,
    struct PokeSpriteSheet* out);

/**
 * Cells per sheet row for `frames` frames `cell_w` wide (see "The grid").
 * A facing then takes (frames + per_row - 1) / per_row rows.
 */
int
PokeSprite_FramesPerRow(
    int cell_w,
    int frames);

/**
 * Whether `key` is a well-formed sheet key: [a-z0-9_/-], no "..", no leading
 * or doubled slash, at most 127 characters. io_server turns it into a path, so
 * this is the gate in front of the filesystem.
 */
bool
PokeSprite_KeyValid(const char* key);

#endif
