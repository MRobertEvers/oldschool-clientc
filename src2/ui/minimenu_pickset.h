#ifndef MINIMENU_PICKSET_H
#define MINIMENU_PICKSET_H

#include <stdbool.h>
#include <stdint.h>

struct GameRunescape;

enum MinimenuPickKind
{
    MINIMENU_PICK_NONE = 0,
    MINIMENU_PICK_NPC,
    MINIMENU_PICK_SCENERY,
    MINIMENU_PICK_TERRAIN,
    MINIMENU_PICK_INV_SLOT,
    MINIMENU_PICK_UI,
};

struct MinimenuPick
{
    enum MinimenuPickKind kind;
    int id;
    int secondary_id;
    int tertiary_id;
    int quaternary_id;
};

#define MINIMENU_PICKSET_MAX 32

struct MinimenuPickSet
{
    struct MinimenuPick items[MINIMENU_PICKSET_MAX];
    int count;
};

void
minimenu_pickset_reset(struct MinimenuPickSet* set);

bool
minimenu_pickset_add(
    struct MinimenuPickSet* set,
    enum MinimenuPickKind kind,
    int id,
    int secondary_id,
    int tertiary_id,
    int quaternary_id);

void
world_pickset_to_minimenu_pickset(
    struct GameRunescape* game,
    struct MinimenuPickSet* out);

void
inv_slot_to_minimenu_pickset(
    int inv_index,
    int slot,
    int obj_id,
    int32_t component_index,
    struct MinimenuPickSet* out);

void
ui_component_to_minimenu_pickset(
    int32_t component_index,
    struct MinimenuPickSet* out);

#endif
