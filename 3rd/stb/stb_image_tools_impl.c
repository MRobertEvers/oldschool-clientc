/*
 * stb_image for TESTS AND TOOLS: PNG and JPEG, with stdio, in a translation
 * unit of its own.
 *
 * The client links stb_image_impl.c instead, JPEG-only and without stdio,
 * because its other formats already have readers (engine/png_decode.c, the
 * rscache sprites). A test is different: it holds a picture on disk -- a
 * reference capture, a shipped plugin asset -- and wants it as pixels with no
 * engine around it. This is that, vendored once so no test has to compile
 * stb's implementation into itself.
 *
 * Never link this beside stb_image_impl.o: both define the stbi_* symbols.
 * The makefile compiles it with -w, for the reason stb_image_impl.c gives.
 *
 *   #include "stb_image.h"   then link $(OBJ_DIR)/stb_image_tools_impl.o
 */

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STB_IMAGE_IMPLEMENTATION

#include "stb_image.h"
