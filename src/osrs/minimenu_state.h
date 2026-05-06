#ifndef MINIMENU_STATE_H
#define MINIMENU_STATE_H

/* 64 world options + Walk here + Cancel + 2 spare */
#define MINIMENU_MAX_OPTIONS 68
#define MINIMENU_OPTION_LEN  80

/** Right-click context menu state (canvas coords for x/y/width/height). */
struct MinimenuState
{
    int  visible; /* 1 = show context menu */
    /** Client.ts menuArea: 0 = main viewport, 1 = sidebar, 2 = chatbox (openMenu hit-test offsets). */
    int  menu_area;
    /** Canvas origin for menu-local ↔ canvas transforms (drawMinimenu / mouseLoop). */
    int  origin_x;
    int  origin_y;
    int  x;       /* top-left on canvas */
    int  y;
    int  width;
    int  height;
    int  option_count;
    char options[MINIMENU_MAX_OPTIONS][MINIMENU_OPTION_LEN];
    int  option_action[MINIMENU_MAX_OPTIONS]; /* MinimenuAction value */
    int  option_param_a[MINIMENU_MAX_OPTIONS];
    int  option_param_b[MINIMENU_MAX_OPTIONS];
    int  option_param_c[MINIMENU_MAX_OPTIONS];
};

#endif
