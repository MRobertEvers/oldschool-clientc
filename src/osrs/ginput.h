#ifndef GINPUT_H
#define GINPUT_H

#include <stdbool.h>
#include <stdint.h>

struct GInput
{
    int quit;

    int mouse_clicked;
    int mouse_clicked_x;
    int mouse_clicked_y;
    int mouse_button_down; /* 1 while left button is held, 0 on release */
    int mouse_x;
    int mouse_y;

    int w_pressed;
    int a_pressed;
    int s_pressed;
    int d_pressed;

    int up_pressed;
    int down_pressed;
    int left_pressed;
    int right_pressed;
    int space_pressed;

    int f_pressed;
    int r_pressed;

    int m_pressed;
    int n_pressed;
    int i_pressed;
    int k_pressed;
    int l_pressed;
    int j_pressed;
    int q_pressed;
    int e_pressed;

    int comma_pressed;
    int period_pressed;

    /* Chat input: when chat_interface_id==-1, these capture typing. Reset each frame. */
    int chat_key_char;     /* 0=none, else ASCII e.g. 'a' */
    int chat_key_return;   /* 1 if Enter/Return pressed */
    int chat_key_backspace; /* 1 if Backspace pressed */

    double time_delta_accumulator_seconds;
};

#endif