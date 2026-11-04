#ifndef SRC_UI_TEST_CHROME_WIDGET_SYNC_H
#define SRC_UI_TEST_CHROME_WIDGET_SYNC_H

/*
 * The map editor's chrome widgets (docs/MAPED_DESIGN.html #chrome), one sync
 * test per kind: the add call, its setters, what Build draws, the intent a
 * presentation reports, and the command an executor is sent -- the same five
 * things every older kind has. Each file drives the real model and the real
 * executor seam (ToriRSChromeSync over a recorder), so a kind that renders in
 * the canvas but sends nothing to the web and browser executors fails here.
 *
 * make -C src test-chrome-widgets
 */

#include "ui/torirs_chrome_exec.h"

#include <stdio.h>

extern int g_cw_checks;
extern int g_cw_failed;

#define CW_CHECK(cond, what)                                                                   \
    do                                                                                         \
    {                                                                                          \
        g_cw_checks++;                                                                         \
        if( !(cond) )                                                                          \
        {                                                                                      \
            g_cw_failed++;                                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, what);                     \
        }                                                                                      \
    } while( 0 )

/* One model, one sync, one recorder: static, the model is a few MB. */
extern struct ToriRSChrome g_cw_ui;
extern struct ToriRSChromeSync g_cw_sync;
extern struct ToriRSChromeRecorder g_cw_rec;

/** Fresh model on an 800x600 surface with every skin slot available, bound to
 *  a recording executor. */
void cw_reset(void);
/** Build, then Sync, recording from an empty log. @return commands recorded. */
int cw_run(void);
/** First recorded command of `kind` for `widget` (-1 any), or NULL. */
struct ToriRSChromeCmd const* cw_find(int kind, int widget);
int cw_count(int kind);
/** A full primary click at (x, y), and the same with a Build first so the
 *  hit boxes are current. */
void cw_click(int x, int y);
/** Prims in the display list that draw skin slot `slot`. */
int cw_prims_with_slot(int slot);
/** Apply one intent as a presentation would report it. @return its result. */
int cw_intent(int kind, int panel, int widget, int value, int x, int y, char const* text);

void cw_test_tree(void);
void cw_test_vlist(void);
void cw_test_iconbutton(void);
void cw_test_slider(void);
void cw_test_radio(void);
void cw_test_modal(void);
void cw_test_tooltip(void);
void cw_test_code(void);
void cw_test_graph(void);
void cw_test_splitter(void);
void cw_test_skin(void);
void cw_test_capacity(void);
void cw_test_button(void);
void cw_test_truncation(void);

#endif
