# The editor's dock layout persisted as a device-local preference
# (editor_layout.ini, TORIRS_EDITOR_LAYOUT): the pure codec and the two IO
# queue tasks. Linked into every client build like the panels
# (tools-panels.mk); tested by test-editor-panels.
#
# Included BEFORE OBJ_DIR and CFLAGS exist (see tools-panels.mk): only
# variable appends here.
MAPED_CLIENT_SRCS += editor/editor_layout.c editor/task_editor_layout_io.c
