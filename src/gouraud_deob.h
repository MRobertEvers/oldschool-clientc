#ifndef GOURAUD_DEOB_H
#define GOURAUD_DEOB_H

#include <stdint.h>

void drawGouraudTriangle(
    int* pixels, int y1, int y2, int y3, int x1, int x2, int x3, int hsl1, int hsl2, int hsl3);

void drawGouraudScanline(
    int* var0, int var1, int var2, int var3, int var4, int var5, int var6, int var7);

#endif