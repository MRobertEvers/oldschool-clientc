/*
 * The CLIENTOP_* registry and its context getters.
 *
 * Every registration below is a real one, copied out of a decompiled
 * clientscript with its arguments intact -- `_6708(1, "Mark tile", 4762)` is
 * what clientscript 6681 actually installs. The two things most likely to be
 * wrong here are which KIND an opcode belongs to (ten opcodes, five kinds,
 * alternating) and whether a context getter answers when it should not, and
 * neither shows up as anything but "the row does nothing".
 *
 * To re-derive:
 *   3rd/rscache/tools/cs2/cs2 decompile --cache cache.osrs239 --rev osrs239 \
 *       --out /tmp/cs2 6681 4762 7580 6688
 */

#include "game/rs_clientop.h"

#include "cs2vm2/cs2_opcode.h"

#include <stdio.h>
#include <string.h>

static int g_failures;
static int g_checks;

#define CHECK(cond, msg)                                                                      \
    do                                                                                        \
    {                                                                                         \
        g_checks++;                                                                           \
        if( !(cond) )                                                                         \
        {                                                                                     \
            g_failures++;                                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg));                   \
        }                                                                                     \
    } while( 0 )

/** Read one context getter as an int. -2 for "not a context getter", -3 for a
 *  getter that answered on the string stack instead. */
static int
ctx_int(struct RS_ClientOpState const* st, int opcode, int running)
{
    int value = 0;
    char const* text = NULL;
    if( !RS_ClientOpContextRead(st, opcode, running, &value, &text) )
        return -2;
    if( text )
        return -3;
    return value;
}

static char const*
ctx_str(struct RS_ClientOpState const* st, int opcode, int running)
{
    int value = 0;
    char const* text = NULL;
    if( !RS_ClientOpContextRead(st, opcode, running, &value, &text) )
        return NULL;
    return text;
}

int
main(void)
{
    struct RS_ClientOpState st;

    RS_ClientOpReset(&st);

    /* ---- nothing installed, nothing running ----------------------------- */
    {
        CHECK(RS_ClientOpGet(&st, RS_CLIENTOP_TILE, 1) == NULL, "no op starts installed");
        CHECK(
            ctx_int(&st, CS2_OP__6950, 4762) == -1,
            "and a context getter with no dispatch answers -1, not tile 0");
    }

    /* ---- "Mark tile": _6708(1, "Mark tile", 4762), clientscript 6681 ----- */
    {
        struct RS_ClientOpSlot const* op;

        CHECK(
            RS_ClientOpApply(&st, CS2_OP_CLIENTOP_TILE_SET, true, 1, "Mark tile", 4762),
            "6708 is in the family");
        op = RS_ClientOpGet(&st, RS_CLIENTOP_TILE, 1);
        CHECK(op != NULL, "the row is installed");
        CHECK(op && strcmp(op->label, "Mark tile") == 0, "with its label");
        CHECK(op && op->script_id == 4762, "and the script that runs it");
        /* 6708 is the TILE pair. Off by one kind and it would be the player's,
         * which is the failure this whole table exists to prevent. */
        CHECK(
            RS_ClientOpGet(&st, RS_CLIENTOP_PLAYER, 1) == NULL,
            "6708 is the tile pair, not the player's");

        /* The label is COPIED. It arrives borrowed from the VM's string pool,
         * which is freed when the frame unwinds; a row holding the pointer
         * would draw whatever landed there next. */
        {
            char borrowed[] = "Mark tile";
            RS_ClientOpApply(&st, CS2_OP_CLIENTOP_TILE_SET, true, 2, borrowed, 4762);
            memset(borrowed, 'x', sizeof(borrowed) - 1);
            op = RS_ClientOpGet(&st, RS_CLIENTOP_TILE, 2);
            CHECK(op && strcmp(op->label, "Mark tile") == 0, "the label is copied, not held");
            RS_ClientOpDel(&st, RS_CLIENTOP_TILE, 2);
        }
    }

    /* ---- the dispatch context, as clientscript 4762 reads it -------------
     *
     *   if (_7038(_6950, 6, 1) = true) { highlight_tile_off(_6950, 6, 1); }
     *
     * so `_6950` has to be the tile that was clicked, and only while 4762 is
     * the script running.
     */
    {
        struct RS_ClientOpContext ctx;
        int const coord = RS_CLIENTOP_COORD(0, 3238, 3216);

        memset(&ctx, 0, sizeof(ctx));
        ctx.kind = RS_CLIENTOP_TILE;
        ctx.script_id = 4762;
        ctx.uid = -1;
        ctx.type = -1;
        ctx.coord = coord;
        RS_ClientOpContextBegin(&st, &ctx);

        CHECK(ctx_int(&st, CS2_OP__6950, 4762) == coord, "4762 reads the clicked tile");
        /*
         * The identity gate. The script does not run where it is dispatched --
         * RS_CS2_RunScript queues a task -- so the context cannot be scoped by
         * a bracket around the call and is scoped by WHICH SCRIPT is asking
         * instead. Without this, any later script reading `_6950` would get
         * the tile of a click the user made minutes ago and mark it.
         */
        CHECK(
            ctx_int(&st, CS2_OP__6950, 9999) == -1,
            "and no other script can read it");
        /* Kind matters too: a loc getter during a TILE dispatch is asking
         * about a loc that is not there. */
        CHECK(
            ctx_int(&st, CS2_OP__6802, 4762) == -1,
            "a loc getter answers nothing during a tile dispatch");
    }

    /* ---- the obj block, including the COUNT (`_6853`) --------------------
     *
     * A ground stack is identified by BOTH its id and its count -- the
     * reference's own FINDOBJ matches a menu row to an obj with
     * `obj->id == entry->id && obj->count == entry->count`, which is what
     * tells two stacks of the same item on one tile apart. `_6852` is the id
     * and `_6853` is the count.
     */
    {
        struct RS_ClientOpContext ctx;
        int const coord = RS_CLIENTOP_COORD(0, 3222, 3218);

        memset(&ctx, 0, sizeof(ctx));
        ctx.kind = RS_CLIENTOP_OBJ;
        ctx.script_id = 4646;
        ctx.uid = -1;
        ctx.type = 995;
        ctx.count = 250;
        ctx.coord = coord;
        snprintf(ctx.name, sizeof(ctx.name), "Coins");
        RS_ClientOpContextBegin(&st, &ctx);

        CHECK(ctx_int(&st, CS2_OP__6852, 4646) == 995, "_6852 is the obj id");
        CHECK(ctx_int(&st, CS2_OP__6853, 4646) == 250, "_6853 is the stack count");
        CHECK(ctx_int(&st, CS2_OP__6851, 4646) == coord, "_6851 is the coord");
        RS_ClientOpContextEnd(&st);
        CHECK(
            ctx_int(&st, CS2_OP__6853, 4646) == -1,
            "and outside the dispatch there is no count to report");
    }

    /* ---- "Tag": _6700(1, "Tag", 6688), clientscript 7580 -----------------
     *
     *   ~script6688: highlight_npc_on(_6751, _6752, 6); ~script6695(_6751,
     *                _6752, _6750, _6753)
     *
     * so all four npc getters have to answer, on the right stacks.
     */
    {
        struct RS_ClientOpContext ctx;
        int const coord = RS_CLIENTOP_COORD(0, 3200, 3200);

        RS_ClientOpApply(&st, CS2_OP_CLIENTOP_NPC_SET, true, 1, "Tag", 6688);
        RS_ClientOpApply(&st, CS2_OP_CLIENTOP_NPC_SET, true, 2, "Tag-All", 6689);
        CHECK(RS_ClientOpGet(&st, RS_CLIENTOP_NPC, 1) != NULL, "Tag is installed");
        CHECK(RS_ClientOpGet(&st, RS_CLIENTOP_NPC, 2) != NULL, "so is Tag-All");

        memset(&ctx, 0, sizeof(ctx));
        ctx.kind = RS_CLIENTOP_NPC;
        ctx.script_id = 6688;
        ctx.uid = 11;
        ctx.type = 3029;
        ctx.coord = coord;
        snprintf(ctx.name, sizeof(ctx.name), "Goblin");
        RS_ClientOpContextBegin(&st, &ctx);

        CHECK(ctx_int(&st, CS2_OP__6751, 6688) == 11, "_6751 is the uid");
        CHECK(ctx_int(&st, CS2_OP__6752, 6688) == coord, "_6752 is the coord");
        CHECK(ctx_int(&st, CS2_OP__6753, 6688) == 3029, "_6753 is the type");
        CHECK(
            ctx_str(&st, CS2_OP__6750, 6688) &&
                strcmp(ctx_str(&st, CS2_OP__6750, 6688), "Goblin") == 0,
            "_6750 is the name");
        /*
         * The name getters answer on the STRING stack and everything else on
         * the int stack, and the reader is what says which. Pushing an int
         * where the script's frame was sized for a string desynchronises the
         * string stack for the rest of the script -- a failure that surfaces
         * several opcodes later, in whatever ran next.
         */
        CHECK(ctx_int(&st, CS2_OP__6750, 6688) == -3, "_6750 is string-valued");
        CHECK(ctx_str(&st, CS2_OP__6751, 6688) == NULL, "_6751 is not");

        /* Absent is "" and never NULL: a script doing string_length on it must
         * get 0 rather than crash the VM. */
        CHECK(
            ctx_str(&st, CS2_OP__6750, 1) && ctx_str(&st, CS2_OP__6750, 1)[0] == '\0',
            "an out-of-dispatch name is empty, not NULL");
    }

    /* ---- DEL, as the settings toggle uses it ----------------------------- */
    {
        /* clientscript 7580's else branch: _6701(1); _6701(2). */
        CHECK(
            RS_ClientOpApply(&st, CS2_OP_CLIENTOP_NPC_DEL, false, 1, NULL, 0),
            "6701 is in the family");
        RS_ClientOpApply(&st, CS2_OP_CLIENTOP_NPC_DEL, false, 2, NULL, 0);
        CHECK(RS_ClientOpGet(&st, RS_CLIENTOP_NPC, 1) == NULL, "Tag is gone");
        CHECK(RS_ClientOpGet(&st, RS_CLIENTOP_NPC, 2) == NULL, "and Tag-All");
        CHECK(
            RS_ClientOpGet(&st, RS_CLIENTOP_TILE, 1) != NULL,
            "and the tile row, a different kind, is untouched");
    }

    /* ---- what this does not own ----------------------------------------- */
    {
        CHECK(
            !RS_ClientOpApply(&st, 7035, true, 1, "x", 1),
            "an opcode outside 6700..6709 is refused");
        CHECK(ctx_int(&st, 6951, 4762) == -2, "_6951 is not a context getter");
        /* A slot past the table is refused rather than asserted: the number
         * comes from a cache script. */
        RS_ClientOpApply(
            &st, CS2_OP_CLIENTOP_TILE_SET, true, RS_CLIENTOP_SLOT_MAX + 3, "x", 1);
        CHECK(g_checks > 0, "an out-of-range slot does not crash the client");
    }

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
