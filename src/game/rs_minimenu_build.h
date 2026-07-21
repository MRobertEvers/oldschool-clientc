#ifndef SRC_RS_MINIMENU_BUILD_H
#define SRC_RS_MINIMENU_BUILD_H

#include "engine/cache_provider.h"
#include "inv/inv_manager.h"
#include "revconfig/revconfig.h"
#include "task_runner.h"
#include "ui/uitree.h"
#include "ui/uitree_host.h"
#include "ui/uitree_minimenu.h"

#include <stdbool.h>

struct World;
struct World_PickSet;

/*
 * Minimenu population (reference buildMinimenu, v1 ui_click builders): walks
 * the UI hit stack under the click and turns component ops / inventory-slot
 * obj configs / social client codes into menu rows. Lives in game/ because it
 * reads the cache provider (objtype names + inv_actions) and inventories.
 */

/** Chat-line seam: when non-NULL and the hit stack contains a chat node with
 * a UITreeChatMinimenuConfig, resolves the sender under (x, y). Returns 1 and
 * fills out_sender/out_chat_type on a hit. NULL until chat rendering exists. */
struct RS_MinimenuChatSource
{
    int (*line_at)(void* user, int x, int y, char* out_sender, int sender_cap, int* out_chat_type);
    void* user;
};

struct RS_MinimenuBuildCtx
{
    struct UITree* tree;
    struct UITreeHost const* ui_host;
    struct CacheProvider* provider;
    struct TaskRunner* runner;
    struct InvManager* invs;
    struct RS_MinimenuChatSource const* chat; /* NULL = no chat lines yet */

    /* World hittest results for this click (NULL/false = no world rows). The
     * pickset must have been refreshed at the click point by the caller. */
    struct World* world;
    struct World_PickSet const* world_pickset;
    bool click_in_world;
};

/** Build the full menu for a right click at (click_x, click_y): Cancel row,
 * per-hit-node rows (top-most component first), priority-sorted. */
void
RS_Minimenu_Build(
    struct RS_MinimenuBuildCtx const* ctx,
    int click_x,
    int click_y,
    struct UIMinimenu* out);

/** Default (left-click) entry after sorting: the top-most normal-priority row
 * (excluding Walk here / Examine / Cancel), else the Walk row, else -1
 * (caller falls back to the legacy click-hook path). Reference
 * chooseDefaultMenuEntry. */
int
RS_Minimenu_DefaultOptionIndex(struct UIMinimenu const* menu);

#endif
