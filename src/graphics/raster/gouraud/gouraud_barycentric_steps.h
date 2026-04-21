#ifndef GOURAUD_BARYCENTRIC_STEPS_H
#define GOURAUD_BARYCENTRIC_STEPS_H

static inline int
gouraud_barycentric_hsl_step_ish8(
    int numerator,
    int sarea)
{
    return (numerator << 8) / sarea;
}

#endif
