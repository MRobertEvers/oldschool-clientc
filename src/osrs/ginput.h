#ifndef GINPUT_H
#define GINPUT_H

#include <stdbool.h>
#include <stdint.h>

struct GInput
{
    int quit;

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
};

#endif