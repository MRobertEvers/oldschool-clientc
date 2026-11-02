# Phase 4 integration (docs/MAPED_IMPLEMENTATION.md section 1, integrate-4).
#
# Included from src/makefile right after platform_check.mk: recipes may use
# OBJ_DIR and friends, rule lines may not.

# ---------------------------------------------------------------------------
# The launched live lane, end to end (src/maped/test/live_e2e/run.sh): the
# editor server on a writable MIRROR of the content tree, LIVE_ATTACH launch of
# a lane whose game server is a scriptrun bot (control link on the game port)
# beside a real js5_server, then (a) a dialogue line edited through the
# authority + RUN, read by the still-logged-in bot on its next Talk-to, and
# (b) a tile height edited + RUN, one REBUILD to the covering bot. Writes only
# under $(OBJ_DIR)/tmp/live (about 1 GB: the mirror's copied packs and cache);
# OSRS-Content is read. Minutes, not seconds (the first RUN compiles both
# script packs of the mirror in full), so it is NOT on test-editor.
.PHONY: maped-live-e2e
maped-live-e2e: torirsmaped torirsmapedctl torirsserver js5-server sscompile
	LIVE_E2E_OBJ=$(OBJ_DIR) sh maped/test/live_e2e/run.sh
