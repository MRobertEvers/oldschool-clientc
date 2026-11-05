/*
 * The STAMPS codec on its own (editor/editor_stamps.h), content-free:
 *
 *   ./editor_stamps_test
 *
 * Parse -> emit is byte-identical whatever the file holds (comments, blank
 * lines, CRLF, no final newline); a malformed line is reported with its
 * number; every piece kind formats to a line that parses back to it; a new
 * stamp's text is the comment block, its header and its pieces; the quarter
 * turn moves every piece as one unit about the origin (a 2x1 loc stays on the
 * tiles it covered, turned; rotations and overlay rotations step; four turns
 * are none); the splice plan from one text to another keeps the common head
 * and tail, fits every command, rebuilds the new text applied forwards and
 * the old one applied backwards; the index gains a stamp's line once.
 */

#include "editor/editor_stamps.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failed;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            g_failed++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while( 0 )

static const char k_smithy[] = "# the forge\n"
                               "stamp smithy | note the forge's walls and anvil\n"
                               "\n"
                               "loc 0 0 0 1902 0 1\n"
                               "loc 1 0 0 1902 0 1\n"
                               "  loc 2 1 0 2097 10 0 2x1\n"
                               "// a comment the editor never wrote\n"
                               "tile 0 0 0 h12 u3\n"
                               "tile 1 0 0 o7/2/3 f1\n"
                               "tile 0 1 0\n"
                               "spawn npc smith 1 1 0\n"
                               "spawn obj bronze_bar 2 0 0 5\n"
                               "loc 0 0 1 1903 22 0\n";

static char*
emit(const struct Editor_StampsFile* file)
{
    size_t const n = Editor_StampsEmit(file, NULL, 0);
    char* text = malloc(n + 1);
    assert(text);
    Editor_StampsEmit(file, text, n + 1);
    return text;
}

static void
test_round_trip(void)
{
    static const char* const k_texts[] = {
        k_smithy,
        "stamp a\r\nloc 0 0 0 1 10 0\r\n",
        "stamp b\nloc 0 0 0 1 10 0",
        "",
        "\n\n# only comments\n",
    };
    struct Editor_StampsFile file;

    Editor_StampsInit(&file);
    for( size_t i = 0; i < sizeof(k_texts) / sizeof(k_texts[0]); i++ )
    {
        struct Editor_StampsParseResult const r = Editor_StampsParse(&file, k_texts[i], strlen(k_texts[i]));
        char* text;
        CHECK(r.ok, "text %d parses (line %d)", (int)i, r.line);
        text = emit(&file);
        CHECK(strcmp(text, k_texts[i]) == 0, "text %d emits byte for byte", (int)i);
        free(text);
    }
    Editor_StampsParse(&file, k_smithy, strlen(k_smithy));
    CHECK(Editor_StampsHeaderCount(&file) == 1 && strcmp(Editor_StampsHeaderName(&file, 0), "smithy") == 0,
        "the header names the stamp");
    CHECK(Editor_StampsFindHeader(&file, "smithy") == 1, "on line index 1");
    CHECK(strcmp(file.lines[1].note, "the forge's walls and anvil") == 0, "with its note");
    CHECK(file.lines[5].kind == EDITOR_STAMPS_LINE_PIECE && file.lines[5].piece.size_x == 2
              && file.lines[5].piece.size_z == 1 && file.lines[5].piece.loc_id == 2097,
        "an indented loc with its 2x1 footprint");
    CHECK(file.lines[8].piece.tile.has_overlay && file.lines[8].piece.tile.overlay_id == 7
              && file.lines[8].piece.tile.shape == 2 && file.lines[8].piece.tile.rotation == 3
              && file.lines[8].piece.tile.settings == 1,
        "a tile's overlay and flags");
    CHECK(file.lines[11].piece.is_obj && file.lines[11].piece.count == 5, "an obj spawn's count");
    Editor_StampsFree(&file);
}

static void
test_refusals(void)
{
    static const struct
    {
        const char* text;
        int line;
    } k_bad[] = {
        { "loc 0 0 0 1 10 0\nstamp a\n", 1 },              /* a piece before the header */
        { "stamp a\nstamp a\n", 2 },                       /* the name twice */
        { "stamp a b\n", 1 },                              /* a name with a space */
        { "stamp a\nloc 0 0 0 1 23 0\n", 2 },              /* shape 23 */
        { "stamp a\nloc 0 0 0 1 10 4\n", 2 },              /* rotation 4 */
        { "stamp a\nloc 0 0 0 1 10 0 2y1\n", 2 },          /* a footprint without its x */
        { "stamp a\nloc 0 0 9 1 10 0\n", 2 },              /* a level offset past 3 */
        { "stamp a\nloc 999 0 0 1 10 0\n", 2 },            /* past the reach */
        { "stamp a\ntile 0 0 0 h0\n", 2 },                 /* h0 is not spellable */
        { "stamp a\ntile 0 0 0 h3 h4\n", 2 },              /* a token twice */
        { "stamp a\ntile 0 0 0 q1\n", 2 },                 /* no such token */
        { "stamp a\nspawn pet cat 0 0 0\n", 2 },           /* neither npc nor obj */
        { "stamp a | colour red\n", 1 },                   /* a segment the grammar lacks */
        { "stamp a\nhouse 0 0 0\n", 2 },                   /* no such line */
    };
    struct Editor_StampsFile file;

    Editor_StampsInit(&file);
    for( size_t i = 0; i < sizeof(k_bad) / sizeof(k_bad[0]); i++ )
    {
        struct Editor_StampsParseResult const r = Editor_StampsParse(&file, k_bad[i].text, strlen(k_bad[i].text));
        char* text = emit(&file);
        CHECK(!r.ok && r.line == k_bad[i].line, "bad text %d refused at line %d (ok %d line %d)", (int)i, k_bad[i].line,
            r.ok, r.line);
        CHECK(strcmp(text, k_bad[i].text) == 0, "and still carried byte for byte");
        free(text);
    }
    Editor_StampsFree(&file);
    CHECK(Editor_StampNameValid("house_2-b"), "a name of letters, digits, _ and -");
    CHECK(!Editor_StampNameValid("") && !Editor_StampNameValid("a.b") && !Editor_StampNameValid("a/b")
              && !Editor_StampNameValid("012345678901234567890123456789012345678901234567"),
        "empty, a dot, a slash, 48 bytes are not names");
}

static void
test_format_parses_back(void)
{
    struct Editor_StampPiece pieces[4];
    char line[EDITOR_STAMP_LINE_MAX + 1];
    struct Editor_StampsLine parsed;

    memset(pieces, 0, sizeof(pieces));
    pieces[0].kind = EDITOR_STAMP_LOC;
    pieces[0].dx = -3;
    pieces[0].dz = 7;
    pieces[0].dlevel = 1;
    pieces[0].loc_id = 40001;
    pieces[0].shape = 9;
    pieces[0].rotation = 3;
    pieces[0].size_x = 3;
    pieces[0].size_z = 2;
    pieces[1].kind = EDITOR_STAMP_TILE;
    pieces[1].dx = 1;
    pieces[1].tile.has_height = 1;
    pieces[1].tile.height = 1;
    pieces[1].tile.underlay_id = 200;
    pieces[2].kind = EDITOR_STAMP_TILE;
    pieces[2].dz = -1;
    pieces[3].kind = EDITOR_STAMP_SPAWN;
    pieces[3].dx = 2;
    pieces[3].dlevel = -1;
    pieces[3].count = 1;
    snprintf(pieces[3].name, sizeof(pieces[3].name), "%s", "guard_2");
    for( int i = 0; i < 4; i++ )
    {
        int const n = Editor_StampFormatPiece(&pieces[i], line, sizeof(line));
        CHECK(n > 0 && (size_t)n < sizeof(line), "piece %d formats", i);
        CHECK(Editor_StampsParseLine(line, &parsed) == EDITOR_STAMPS_LINE_PIECE, "piece %d's line parses: %s", i, line);
        CHECK(memcmp(&parsed.piece, &pieces[i], sizeof(pieces[i])) == 0, "piece %d comes back as it was: %s", i, line);
    }
}

static void
test_new_stamp_text(void)
{
    struct Editor_Stamp stamp;
    struct Editor_StampsFile file;
    struct Editor_Stamp back;
    struct Editor_StampPiece piece;
    char* text;
    size_t n;

    Editor_StampInit(&stamp);
    Editor_StampInit(&back);
    snprintf(stamp.name, sizeof(stamp.name), "%s", "porch");
    memset(&piece, 0, sizeof(piece));
    piece.kind = EDITOR_STAMP_LOC;
    piece.loc_id = 1278;
    piece.shape = 10;
    piece.size_x = 1;
    piece.size_z = 1;
    CHECK(Editor_StampAdd(&stamp, &piece), "a loc goes in");
    piece.dx = 1;
    piece.rotation = 2;
    CHECK(Editor_StampAdd(&stamp, &piece), "and another");
    piece.dx = EDITOR_STAMP_REACH + 1;
    CHECK(!Editor_StampAdd(&stamp, &piece), "but not one past the reach");
    n = Editor_StampEmitNew(&stamp, NULL, 0);
    text = malloc(n + 1);
    assert(text);
    CHECK(Editor_StampEmitNew(&stamp, text, n + 1) == n, "the size is the length");
    CHECK(strstr(text, "\nstamp porch\nloc 0 0 0 1278 10 0\nloc 1 0 0 1278 10 2\n") != NULL, "the header and its pieces (%s)",
        text);
    CHECK(text[0] == '#', "after the comment block");
    Editor_StampsInit(&file);
    CHECK(Editor_StampsParse(&file, text, n).ok, "the new text parses");
    CHECK(Editor_StampFromFile(&file, &back) && back.count == 2 && strcmp(back.name, "porch") == 0,
        "and reads back as the stamp");
    free(text);
    Editor_StampsFree(&file);
    Editor_StampFree(&stamp);
    Editor_StampFree(&back);
}

/** Whether a stamp holds a loc piece standing exactly so. */
static int
has_loc(
    const struct Editor_Stamp* stamp,
    int dx,
    int dz,
    int loc_id,
    int rotation,
    int sx,
    int sz)
{
    for( int i = 0; i < stamp->count; i++ )
    {
        const struct Editor_StampPiece* p = &stamp->pieces[i];
        if( p->kind == EDITOR_STAMP_LOC && p->dx == dx && p->dz == dz && p->loc_id == loc_id && p->rotation == rotation
            && p->size_x == sx && p->size_z == sz )
            return 1;
    }
    return 0;
}

static void
test_turn_as_a_unit(void)
{
    struct Editor_StampsFile file;
    struct Editor_Stamp stamp;
    struct Editor_Stamp original;
    char* before;
    char* after;
    size_t n;

    Editor_StampsInit(&file);
    Editor_StampInit(&stamp);
    Editor_StampInit(&original);
    Editor_StampsParse(&file, k_smithy, strlen(k_smithy));
    Editor_StampFromFile(&file, &stamp);
    Editor_StampCopy(&original, &stamp);

    Editor_StampRotate(&stamp, 1);
    /* (x, z) -> (z, -x), clockwise from above with z north: east goes
     * south. */
    CHECK(has_loc(&stamp, 0, 0, 1902, 2, 1, 1), "the origin wall stays on the origin, turned 1 -> 2");
    CHECK(has_loc(&stamp, 0, -1, 1902, 2, 1, 1), "the wall east of it is south of it now");
    /* The 2x1 table at (2, 1) covered x 2..3, z 1: turned it covers x 1,
     * z -3..-2 -- SW corner (1, -3), 1x2. */
    CHECK(has_loc(&stamp, 1, -3, 2097, 1, 1, 2), "the 2x1 table keeps its tiles, turned: SW (1,-3), 1x2");
    CHECK(stamp.pieces[4].kind == EDITOR_STAMP_TILE && stamp.pieces[4].dx == 0 && stamp.pieces[4].dz == -1
              && stamp.pieces[4].tile.rotation == 0,
        "the overlay east of the origin turns 3 -> 0 and goes south");
    CHECK(stamp.pieces[6].kind == EDITOR_STAMP_SPAWN && stamp.pieces[6].dx == 1 && stamp.pieces[6].dz == -1,
        "the smith goes with the house");
    CHECK(stamp.pieces[8].dlevel == 1 && stamp.pieces[8].dx == 0 && stamp.pieces[8].dz == 0,
        "the upper floor's piece keeps its level");
    CHECK(stamp.turned == 1, "the stamp says it is turned once");

    Editor_StampRotate(&stamp, 3);
    CHECK(stamp.turned == 0, "four quarters are a whole turn");
    CHECK(stamp.count == original.count
              && memcmp(stamp.pieces, original.pieces, sizeof(stamp.pieces[0]) * (size_t)stamp.count) == 0,
        "and every piece is back exactly");
    Editor_StampRotate(&stamp, -1);
    Editor_StampRotate(&original, 3);
    CHECK(memcmp(stamp.pieces, original.pieces, sizeof(stamp.pieces[0]) * (size_t)stamp.count) == 0,
        "a quarter back is three on");

    /* A turned stamp's text round-trips. */
    n = Editor_StampEmitNew(&stamp, NULL, 0);
    before = malloc(n + 1);
    assert(before);
    Editor_StampEmitNew(&stamp, before, n + 1);
    CHECK(Editor_StampsParse(&file, before, n).ok, "the turned stamp's text parses");
    Editor_StampFromFile(&file, &original);
    after = malloc(n + 1);
    assert(after);
    Editor_StampEmitNew(&original, after, n + 1);
    CHECK(strcmp(before, after) == 0, "and emits the same text again");
    free(before);
    free(after);
    Editor_StampsFree(&file);
    Editor_StampFree(&stamp);
    Editor_StampFree(&original);
}

/** Apply TEXT_SPLICE commands to `text` forwards (or backwards, last first). */
static void
apply_splices(
    char* text,
    size_t* length,
    const struct Editor_Cmd* commands,
    int count,
    int inverse)
{
    for( int k = 0; k < count; k++ )
    {
        const struct Editor_Cmd* c = &commands[inverse ? count - 1 - k : k];
        size_t const at = c->u.text_splice.offset;
        size_t const remove = inverse ? c->u.text_splice.after_len : c->u.text_splice.before_len;
        size_t const insert = inverse ? c->u.text_splice.before_len : c->u.text_splice.after_len;
        const char* bytes = c->u.text_splice.text + (inverse ? 0 : c->u.text_splice.before_len);
        assert(at + remove <= *length);
        assert(memcmp(text + at, c->u.text_splice.text + (inverse ? c->u.text_splice.before_len : 0), remove) == 0);
        memmove(text + at + insert, text + at + remove, *length - at - remove);
        memcpy(text + at, bytes, insert);
        *length = *length - remove + insert;
        text[*length] = '\0';
    }
}

static void
test_splices(void)
{
    static struct Editor_Cmd commands[16];
    static char work[8192];
    static char big_old[4000];
    static char big_new[5000];
    size_t length;
    int count;
    int fits = 1;

    count = Editor_StampSplices("stamp:a", "abcXYZdef", 9, "abcQdef", 7, commands, 16);
    CHECK(count == 2, "a changed middle: one remove, one insert (%d)", count);
    CHECK(commands[0].u.text_splice.offset == 3 && commands[0].u.text_splice.before_len == 3
              && commands[1].u.text_splice.after_len == 1,
        "the head and tail are kept");
    CHECK(commands[0].doc_kind == EDITOR_DOC_TEXT && strcmp(commands[0].doc_key, "stamp:a") == 0
              && commands[0].kind == EDITOR_CMD_TEXT_SPLICE,
        "addressed to the stamp's TEXT document");
    CHECK(Editor_StampSplices("stamp:a", "same", 4, "same", 4, commands, 16) == 0, "nothing to do is no command");

    for( size_t i = 0; i < sizeof(big_old) - 1; i++ )
        big_old[i] = (char)('a' + i % 23);
    for( size_t i = 0; i < sizeof(big_new) - 1; i++ )
        big_new[i] = (char)('A' + i % 19);
    big_new[0] = big_old[0];
    count = Editor_StampSplices("stamp:b", big_old, sizeof(big_old) - 1, big_new, sizeof(big_new) - 1, commands, 16);
    CHECK(count > 2, "a big rewrite takes several commands (%d)", count);
    for( int k = 0; k < count; k++ )
        if( commands[k].u.text_splice.before_len + commands[k].u.text_splice.after_len > EDITOR_CMD_SPLICE_MAX )
            fits = 0;
    CHECK(fits, "each fits one command");
    memcpy(work, big_old, sizeof(big_old));
    length = sizeof(big_old) - 1;
    apply_splices(work, &length, commands, count, 0);
    CHECK(length == sizeof(big_new) - 1 && memcmp(work, big_new, length) == 0, "applied in order: the new text");
    apply_splices(work, &length, commands, count, 1);
    CHECK(length == sizeof(big_old) - 1 && memcmp(work, big_old, length) == 0, "undone in reverse: the old text");
    CHECK(Editor_StampSplices("stamp:b", big_old, sizeof(big_old) - 1, big_new, sizeof(big_new) - 1, commands, 2) == -1,
        "too few commands is -1, not half a plan");
}

static void
test_index(void)
{
    char out[1024];
    size_t n;
    struct Editor_StampsFile file;

    n = Editor_StampsIndexWith("", 0, "house", out, sizeof(out));
    CHECK(n == strlen(out) && out[0] == '#' && strstr(out, "\nstamp house\n") != NULL, "an empty index gains its header and the line");
    {
        char copy[1024];
        snprintf(copy, sizeof(copy), "%s", out);
        n = Editor_StampsIndexWith(copy, strlen(copy), "house", out, sizeof(out));
        CHECK(strcmp(out, copy) == 0, "a listed stamp is not listed twice");
    }
    n = Editor_StampsIndexWith("stamp a", 7, "b", out, sizeof(out));
    CHECK(strcmp(out, "stamp a\nstamp b\n") == 0, "a missing final newline is supplied first (%s)", out);
    Editor_StampsInit(&file);
    CHECK(Editor_StampsParse(&file, out, n).ok && Editor_StampsHeaderCount(&file) == 2, "the index parses: two stamps");
    Editor_StampsFree(&file);
}

int
main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    ToriMem_Init();
    test_round_trip();
    test_refusals();
    test_format_parses_back();
    test_new_stamp_text();
    test_turn_as_a_unit();
    test_splices();
    test_index();
    printf("editor_stamps_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
