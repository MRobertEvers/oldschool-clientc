#ifndef TASK_INTERFACE_OPEN_H
#define TASK_INTERFACE_OPEN_H

#include "asyncio.h"

struct CacheProvider;
struct UITree;
struct RS_CS2Host;
struct InvManager;
struct UITreeSceneBridge;

struct InterfaceOpenStats
{
    int interface_id;
    int pack_component_count;
    int onload_count;
    int tree_component_count;
};

/**
 * Open one cache interface pack as a root (TS setRootInterface):
 * load → bake → layout → onLoad → layout → inv/var transmit → layout.
 * Spillover sibling groups that are not InterfaceParent-mounted are hidden.
 */
struct ToriRS_Task*
CreateTask_InterfaceOpen(
    struct CacheProvider* provider,
    struct UITree* tree,
    struct RS_CS2Host* host,
    struct InvManager* invs,
    struct UITreeSceneBridge* bridge,
    int interface_id,
    struct InterfaceOpenStats* stats);

/**
 * Open a pack as a sub-interface mounted under target_uid (TS openSubInterface):
 * load → bake → reparent under target → layout(host) → onLoad → layout →
 * onResize → layout → onSubChange → inv/var transmit → layout.
 * type: 0 modal, 1 overlay, 3 tab/sidemodal.
 */
struct ToriRS_Task*
CreateTask_InterfaceOpenSub(
    struct CacheProvider* provider,
    struct UITree* tree,
    struct RS_CS2Host* host,
    struct InvManager* invs,
    struct UITreeSceneBridge* bridge,
    int target_uid,
    int interface_id,
    int type,
    struct InterfaceOpenStats* stats);

#endif
