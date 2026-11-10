# The OS text clipboard (platform/platform_clipboard.h): paste into the
# editor's fields, and copy names, paths and script snippets out of it.
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS and the content paths are defined: rule lines here name no
# such variable (it would expand empty), recipes use them freely.
MAPED_CLIENT_SRCS += platform/platform_clipboard.c
