#include "render/world_views.h"

#include <assert.h>
#include <stddef.h>

void
WorldViews_Init(
    struct WorldViews* wv,
    struct UIMinimenu* runner_menu)
{
    assert(wv);
    assert(runner_menu);
    wv->split.view_count = 1;
    wv->frame = &wv->views[0];
    wv->frame->minimenu = runner_menu;
}

struct App_WorldView*
WorldViews_Runner(struct WorldViews* wv)
{
    assert(wv);
    return &wv->views[APP_VIEW_RUNNER];
}

struct App_WorldView*
WorldViews_Presented(struct WorldViews* wv)
{
    assert(wv);
    return wv->split.attached ? &wv->views[APP_VIEW_PLAYER_CLIENT] : &wv->views[APP_VIEW_RUNNER];
}
