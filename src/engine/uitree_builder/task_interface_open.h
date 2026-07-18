#ifndef TASK_INTERFACE_OPEN_H
#define TASK_INTERFACE_OPEN_H

#include "asyncio.h"

struct CacheProvider;
struct UITree;
struct RS_CS2Host;
struct InvManager;

struct InterfaceOpenStats
{
    int interface_id;
    int pack_component_count;
    int onload_count;
    int tree_component_count;
};

/**
 * Open one cache interface pack: load pack → prefetch sprites/fonts/models →
 * bake UITree → layout → onLoad CS2 → inv/var transmit.
 * stats may be NULL; when non-NULL filled after the task completes.
 */
struct ToriRS_Task*
CreateTask_InterfaceOpen(
    struct CacheProvider* provider,
    struct UITree* tree,
    struct RS_CS2Host* host,
    struct InvManager* invs,
    int interface_id,
    struct InterfaceOpenStats* stats);

#endif
