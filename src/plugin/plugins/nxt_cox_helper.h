#ifndef TORIRS_PLUGIN_NXT_COX_HELPER_H
#define TORIRS_PLUGIN_NXT_COX_HELPER_H

#include "plugin/plugins/plugin_draw.h"
#include "plugin/torirs_plugin_api.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The Chambers of Xeric helper's paint, apart from the plugin so a test can
 * draw it from the shipped art and hold it against the C++ client's picture.
 * @see nxt_cox_helper.c for what every number is measured from.
 */

#define NXT_COX_HELPER_MAX_ROOMS 24

struct NxtCoxHelperRoom
{
    char kind[16];
    char name[24];
    char letter;
};

/** Everything one paint draws. Zero it whole before filling: it is the
 *  derived image's key, padding included. */
struct NxtCoxHelperView
{
    struct NxtCoxHelperRoom rooms[NXT_COX_HELPER_MAX_ROOMS];
    int room_count;
    char total[16];
    char yours[16];
    char time[16];
    bool collapsed;
};

struct NxtCoxHelperArt
{
    struct PluginDraw_Atlas const* text;
    uint32_t const* plate_px;
    int plate_w;
    int plate_h;
    /** Optional. */
    uint32_t const* arrow_px;
    int arrow_w;
    int arrow_h;
};

int NxtCoxHelper_Width(void);
int NxtCoxHelper_Height(struct NxtCoxHelperView const* view);
/** The time row's line-box top, box pixels. */
int NxtCoxHelper_TimeTop(struct NxtCoxHelperView const* view);
/** The arrow's box, box pixels. */
void NxtCoxHelper_ArrowRect(struct NxtCoxHelperView const* view, struct ToriRS_Rect* out);

/** A score as raids_overlay_databox_1511 prints one: comma thousands, and
 *  "Lots!" from two billion. `cap` >= 16. */
void NxtCoxHelper_FormatScore(char* out, size_t cap, int score);
/** Raid ticks as the picture prints them: hh:mm:ss. `cap` >= 12. */
void NxtCoxHelper_FormatTime(char* out, size_t cap, int ticks);

/** Paint the whole box into `argb`, which is Width x Height(view). */
void NxtCoxHelper_Paint(
    struct NxtCoxHelperView const* view,
    struct NxtCoxHelperArt const* art,
    uint32_t* argb,
    int w,
    int h);

#endif /* TORIRS_PLUGIN_NXT_COX_HELPER_H */
