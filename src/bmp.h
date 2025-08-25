#ifndef BMP_H
#define BMP_H

#include <stdint.h>

void bmp_write_file(const char* filename, int* pixels, int width, int height);

#endif