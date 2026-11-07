#ifndef SRC_UI_TORIRS_CHROME_SCALE_Q_H
#define SRC_UI_TORIRS_CHROME_SCALE_Q_H

/**
 * Which chrome scale to ask for: the pure arithmetic behind the client's
 * chrome scale ladder (main.c main_dynamic_chrome_scale), the manifest's and
 * the environment's pins (`[ui:boot] chrome_scale=`, TORIRS_CHROME_SCALE) and
 * the editor's View > Chrome scale (editor_ui.h). Workbench step 6.0b, design
 * section 2 "R1 in full".
 *
 * The scale is in QUARTERS of a device pixel per chrome pixel: 4 is 1x, 5 is
 * 1.25x, 6 is 1.5x, 7 is 1.75x, 8 is 2x, 10 is 2.5x, 12 is 3x -- the device
 * pixel ratios displays report. The chrome model takes q exactly
 * (ToriRSChrome_SetScaleQ, through the editor's shim Editor_ChromeSetScaleQ,
 * which forwards it unchanged); the baked tier it reads bitmaps from is
 * ToriRSChrome_ScaleTier(q), the tier at or above it.
 *
 * The NATIVE draw path takes q exactly too: main.c hands it to
 * App_SetChromeScaleQ, whose scene bridge registers the bake's faces and
 * skin at q (ToriRSChromeBake_Face / _Skin), and the DOM presentation zooms
 * by CSS to the same q.
 *
 * Header-only and dependency-free, so the client, the boot manifest's parser
 * and the tests share one table.
 */

#include <assert.h>
#include <stddef.h>

/** 1x, and the bounds of every scale asked for: 1x to 3x. */
#define TORIRS_CHROME_SCALEQ_ONE 4
#define TORIRS_CHROME_SCALEQ_MIN 4
#define TORIRS_CHROME_SCALEQ_MAX 12
/** A touch device's floor: 2x (design: "a touch device never goes under 2x"). */
#define TORIRS_CHROME_SCALEQ_TOUCH_MIN 8

/** The design's steps, the rows of View > Chrome scale after Auto. */
#define TORIRS_CHROME_SCALEQ_STEP_COUNT 7

static inline int
ToriRSChromeScaleQ_Step(int index)
{
    static const int k_steps[TORIRS_CHROME_SCALEQ_STEP_COUNT] = { 4, 5, 6, 7, 8, 10, 12 };
    if( index < 0 )
        index = 0;
    if( index >= TORIRS_CHROME_SCALEQ_STEP_COUNT )
        index = TORIRS_CHROME_SCALEQ_STEP_COUNT - 1;
    return k_steps[index];
}

/** `q` in percent (6 -> 150), what the View rows and the console say. */
static inline int
ToriRSChromeScaleQ_Percent(int q)
{
    return q * 100 / TORIRS_CHROME_SCALEQ_ONE;
}

/** The step a quarter value is, or -1 when it is none of them. */
static inline int
ToriRSChromeScaleQ_StepIndex(int q)
{
    for( int i = 0; i < TORIRS_CHROME_SCALEQ_STEP_COUNT; i++ )
        if( ToriRSChromeScaleQ_Step(i) == q )
            return i;
    return -1;
}

/**
 * A display's ratio, in percent (drawable pixels per window point x 100,
 * PlatformWindow_DisplayDensityPercent), snapped to the nearest step: clamped
 * to 100..300 first, a tie going to the larger step (a 2.25x display gets
 * 2.5x, never chrome smaller than the display asks for).
 */
static inline int
ToriRSChromeScaleQ_Snap(int percent)
{
    int best = ToriRSChromeScaleQ_Step(0);
    int best_gap = -1;

    if( percent < 100 )
        percent = 100;
    if( percent > 300 )
        percent = 300;
    for( int i = 0; i < TORIRS_CHROME_SCALEQ_STEP_COUNT; i++ )
    {
        int const step = ToriRSChromeScaleQ_Step(i);
        int gap = ToriRSChromeScaleQ_Percent(step) - percent;
        if( gap < 0 )
            gap = -gap;
        /* Steps ascend: `<=` lets the larger of two equidistant ones win. */
        if( best_gap < 0 || gap <= best_gap )
        {
            best = step;
            best_gap = gap;
        }
    }
    return best;
}

/** Auto: the display's ratio snapped, and never under 2x on a touch device. */
static inline int
ToriRSChromeScaleQ_FromDisplay(
    int percent,
    int touch)
{
    int q = ToriRSChromeScaleQ_Snap(percent);
    if( touch && q < TORIRS_CHROME_SCALEQ_TOUCH_MIN )
        q = TORIRS_CHROME_SCALEQ_TOUCH_MIN;
    return q;
}

/**
 * A pin's text -- `[ui:boot] chrome_scale=`, TORIRS_CHROME_SCALE -- as a
 * quarter value: an integer ("2") or a decimal ("1.5", "1.25") device pixels
 * per chrome pixel, rounded to the nearest quarter and clamped to 1x..3x (a
 * "4" from an older manifest is 3x, as the integer chrome clamped it). Not a
 * step table: a pin says exactly what it wants. 0 for anything else (empty,
 * negative, zero, trailing junk): the caller says so and ignores it. `text`
 * is never NULL: "no pin" is the caller's test, made before it asks.
 */
static inline int
ToriRSChromeScaleQ_Parse(const char* text)
{
    long whole = 0;
    long frac = 0;
    long frac_scale = 1;
    int digits = 0;
    long q;
    const char* p = text;

    assert(text);
    while( *p >= '0' && *p <= '9' )
    {
        if( whole < 100 )
            whole = whole * 10 + (*p - '0');
        digits++;
        p++;
    }
    if( *p == '.' )
    {
        p++;
        while( *p >= '0' && *p <= '9' )
        {
            if( frac_scale < 10000 )
            {
                frac = frac * 10 + (*p - '0');
                frac_scale *= 10;
            }
            digits++;
            p++;
        }
    }
    if( digits == 0 || *p != '\0' )
        return 0;
    /* Nearest quarter, halves up: (whole + frac/scale) * 4 + 1/2. */
    q = whole * TORIRS_CHROME_SCALEQ_ONE + (frac * TORIRS_CHROME_SCALEQ_ONE * 2 + frac_scale) / (frac_scale * 2);
    if( q <= 0 )
        return 0;
    if( q < TORIRS_CHROME_SCALEQ_MIN )
        q = TORIRS_CHROME_SCALEQ_MIN;
    if( q > TORIRS_CHROME_SCALEQ_MAX )
        q = TORIRS_CHROME_SCALEQ_MAX;
    return (int)q;
}

/**
 * The nearest whole scale to `q`, in quarters, for a consumer that can only
 * draw an integer tier: q rounded to a multiple of 4, halves up -- 5 -> 4,
 * 6 and 7 -> 8, 10 -> 12; 4, 8 and 12 unchanged. Halves up is
 * PlatformWindow_PixelDensity's own rounding of a display's ratio. The
 * native chrome no longer goes through this (it draws q itself since the
 * scene bridge registers the bake at q); nothing in the client calls it
 * today, and the editor's workbench test pins it.
 */
static inline int
ToriRSChromeScaleQ_Whole(int q)
{
    assert(q >= TORIRS_CHROME_SCALEQ_MIN);
    assert(q <= TORIRS_CHROME_SCALEQ_MAX);
    return (q + TORIRS_CHROME_SCALEQ_ONE / 2) / TORIRS_CHROME_SCALEQ_ONE * TORIRS_CHROME_SCALEQ_ONE;
}

#endif
