#ifndef MINIMENU_STATE_H
#define MINIMENU_STATE_H

/* 64 world options + Walk here + Cancel + 2 spare */
#define MINIMENU_MAX_OPTIONS 68
#define MINIMENU_OPTION_LEN  80

/** Right-click context menu state (iface-viewport coordinates for x/y/width/height). */
struct MinimenuState
{
    int  visible; /* 1 = show context menu */
    int  x;       /* top-left in iface-viewport coords */
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
