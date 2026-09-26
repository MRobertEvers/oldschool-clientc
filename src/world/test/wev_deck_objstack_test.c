/*
 * seam18 C: ground items on a vessel DECK.
 *
 * A deck world (its actors are borrowed from the carrier -- the foreign-actor
 * hook is what marks it) has no minusedlevel of its own: world_local_level
 * answers 0 there, while a rider's drop, and a deck npc's loot, land on the
 * deck's config plane (1 on the Pandemonium). The obj stack loop in
 * World_CycleRegisterPainterDynamics filtered deck stacks against that 0, so
 * a drop on deck was never drawn and so never picked: no Take row, ever
 * (build/quest_gate/parity1p_pryingtimes_deckdrop). A deck's ground items
 * register at their own plane, like its locs and its borrowed actors; the
 * root keeps the reference rule (objStacks[minusedlevel] only).
 */
#include "world/world.h"
#include "painters/painters.h"
#include "toridraw_types.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static void
borrowed(void* user, struct World* w)
{
    (void)user;
    (void)w;
}

static int
painted(struct PaintersBuffer* b, int id)
{
    for( int i = 0; i < b->command_count; ++i )
        if( b->commands[i]._bf_kind == PNTR_CMD_ELEMENT &&
            painter_command_element_id(&b->commands[i]) == id )
            return 1;
    return 0;
}

int
main(void)
{
    char no_ops[5][32] = { { 0 } };
    struct PaintersBuffer b = { .commands = calloc(256, sizeof(*b.commands)),
                                .command_capacity = 256 };
    struct World* deck;
    struct World* root;
    int failures = 0;

    ToriDraw_InitSinTable();
    ToriDraw_InitCosTable();
    assert(b.commands);

    deck = World_New();
    World_ResetScene(deck, 0, 0, 8);
    World_SetLoadComplete(deck, true);
    World_SetForeignActorRegisterFn(deck, borrowed, NULL);
    World_ObjStackAdd(deck, 910, 4, 3, 1, 1917, 1, "Beer", (const char(*)[32])no_ops);
    painter_set_draw_distance(deck->painter, 8);
    painter_set_level_mask(deck->painter, 0xF);
    World_CycleRegisterDynamics(deck);
    painter_paint_bucket(deck->painter, &b, 0, 0, 0);
    if( !painted(&b, 910) )
    {
        fprintf(stderr, "FAIL: a deck world's plane-1 ground item was not painted\n");
        failures++;
    }

    root = World_New();
    World_ResetScene(root, 50, 50, 16);
    World_SetLoadComplete(root, true);
    World_ObjStackAdd(root, 911, 4, 3, 1, 1917, 1, "Beer", (const char(*)[32])no_ops);
    World_ObjStackAdd(root, 912, 5, 3, 0, 1917, 1, "Beer", (const char(*)[32])no_ops);
    painter_set_draw_distance(root->painter, 16);
    painter_set_level_mask(root->painter, 0xF);
    World_CycleRegisterDynamics(root);
    b.command_count = 0;
    painter_paint_bucket(root->painter, &b, 0, 0, 0);
    if( painted(&b, 911) )
    {
        fprintf(stderr, "FAIL: the root painted a ground item off the local plane\n");
        failures++;
    }
    if( !painted(&b, 912) )
    {
        fprintf(stderr, "FAIL: the root did not paint its local-plane ground item\n");
        failures++;
    }

    free(b.commands);
    World_Free(root);
    World_Free(deck);
    if( failures )
        return 1;
    puts("deck ground items: a deck world paints its plane-1 drop; the root keeps the local-plane rule PASS");
    return 0;
}
