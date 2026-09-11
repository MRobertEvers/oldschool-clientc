/*
 * stb_image, compiled JPEG-only, in a translation unit of its own.
 *
 * Its own TU (rather than an implementation define inside the wrapper) for the
 * same reason rscache and lua have unity objects here: vendored code is not
 * ours to make -Wall -Wextra clean, and the makefile compiles this one file
 * with -w so that engine/jpeg_decode.c keeps every warning the build asks for.
 *
 * Only the JPEG decoder is built. The cache's other image formats already have
 * readers -- PNG in engine/png_decode.c, the RS pix8/pix32 sprites in rscache
 * -- so stb's PNG/BMP/GIF/PSD/HDR paths would be a second answer to questions
 * this tree has already answered.
 *
 * No stdio: every caller already holds the bytes, because they came out of a
 * jagfile member or a dat2 group, never off disk on their own.
 */

#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STB_IMAGE_IMPLEMENTATION

#include "stb_image.h"
