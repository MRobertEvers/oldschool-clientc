# Lane invariant checks -- `make -C src lane-check [PLATFORM=...]`.
#
# platform.mk declares what a lane *is*; this file declares what must remain
# true about it, so a flag that silently goes missing (or silently appears)
# fails here instead of on a user's machine. It is deliberately a separate file
# from platform.mk, which stays a pure set of declarations.
#
# The checks are data, not code: each lane lists substrings that must appear in
# its effective compile+link flags and substrings that must not. Matching is
# done with make's $(findstring), never by interpolating the flag string into a
# shell command -- the web lane's -sEXPORTED_FUNCTIONS value contains both
# quote characters and would not survive the trip.
#
#   make -C src lane-check                    # this host's native lane
#   make -C src lane-check PLATFORM=web
#   make -C src lane-check PLATFORM=win32
#   make -C src lane-check-artifact PLATFORM=win32   # after linking

# --- macOS: desktop GL, and the one linker that has -dead_strip -------------
LANE_REQUIRE_macos := OpenGL -dead_strip
LANE_FORBID_macos  := --gc-sections

# --- Linux: desktop GL, and specifically NOT the Apple-only strip flag -------
LANE_REQUIRE_linux := -lGL
LANE_FORBID_linux  := -dead_strip

# --- Windows XP: the ABI contract that makes the binary loadable on XP SP3 ---
# console:5.01 is the PE subsystem *version*. Without it binutils stamps 6.00
# and XP declines to load the image at all, which is the failure this check
# exists to prevent -- it has no symptom you can debug from the XP side.
# Forbidding SDL/GL/WebGL here is what keeps the lane Soft3D-only: the old
# "native or else" object test had already leaked the WebGL object in.
LANE_REQUIRE_win32 := _WIN32_WINNT=0x0501 WINVER=0x0501 -march=pentium4 -msse2 \
                      -mfpmath=sse console:5.01 -static-libgcc win32_compat.h
LANE_FORBID_win32  := -dead_strip -sUSE_SDL=2 webgl1 TORIRS_HAVE_GL3

# --- Web: WebGL1 pinned, and no ASYNCIFY ------------------------------------
# The IO path yields to the main loop and lets torirs_host.js pump responses
# back in (docs/web_build.md). ASYNCIFY would rewrite the entire module to buy
# the same behaviour at a large size and speed cost, so its absence is a
# property of the design rather than an accident worth re-deciding.
LANE_REQUIRE_web := -sMIN_WEBGL_VERSION=1 -sMAX_WEBGL_VERSION=1 \
                    GL_SUPPORT_AUTOMATIC_ENABLE_EXTENSIONS=0
LANE_FORBID_web  := ASYNCIFY -dead_strip

# Everything a lane actually compiles and links with. GPU object names are in
# here so "no WebGL in the win32 link" is checkable as a flag would be.
LANE_EFFECTIVE = $(CFLAGS) $(LDFLAGS) $(PLATFORM_GPU_OBJ_NAMES)

LANE_MISSING = $(strip $(foreach f,$(LANE_REQUIRE_$(PLATFORM)), \
                 $(if $(findstring $(f),$(LANE_EFFECTIVE)),,$(f))))
LANE_FORBIDDEN_PRESENT = $(strip $(foreach f,$(LANE_FORBID_$(PLATFORM)), \
                           $(if $(findstring $(f),$(LANE_EFFECTIVE)),$(f),)))

# Lanes needing more than a flag comparison name an extra target here.
LANE_EXTRA_CHECKS_win32 := lane-check-toolchain-win32

.PHONY: lane-check lane-check-all lane-check-artifact lane-check-toolchain-win32

lane-check: $(LANE_EXTRA_CHECKS_$(PLATFORM))
	@if [ -z '$(LANE_REQUIRE_$(PLATFORM))$(LANE_FORBID_$(PLATFORM))' ]; then \
		echo "lane-check: no invariants declared for PLATFORM=$(PLATFORM)" >&2; exit 1; fi
	@if [ -n '$(LANE_MISSING)' ]; then \
		echo "lane-check: PLATFORM=$(PLATFORM) is MISSING required flags:" >&2; \
		for f in $(LANE_MISSING); do echo "    $$f" >&2; done; \
		echo "  (declared in src/platform/platform_check.mk, set in platform.mk)" >&2; \
		exit 1; fi
	@if [ -n '$(LANE_FORBIDDEN_PRESENT)' ]; then \
		echo "lane-check: PLATFORM=$(PLATFORM) has FORBIDDEN flags:" >&2; \
		for f in $(LANE_FORBIDDEN_PRESENT); do echo "    $$f" >&2; done; \
		exit 1; fi
	@echo "lane-check: PLATFORM=$(PLATFORM) ok"

# Every lane, from one command. Runs on any host: nothing here compiles.
lane-check-all:
	@for p in $(PLATFORM_LIST); do $(MAKE) --no-print-directory PLATFORM=$$p lane-check || exit 1; done

# 32-bitness is not forced with -m32 (on an x86_64 MinGW without multilib that
# fails deep in the assembler with nothing pointing at the cause). The triple is
# checked instead, so choosing the wrong toolchain says so in one line.
lane-check-toolchain-win32:
	@triple=`$(PLATFORM_CC) -dumpmachine 2>/dev/null`; \
	case "$$triple" in \
		i686-*mingw*) ;; \
		'') echo "lane-check: '$(PLATFORM_CC)' not found on PATH" >&2; exit 1;; \
		*) echo "lane-check: win32 needs an i686 MinGW toolchain, but '$(PLATFORM_CC) -dumpmachine' says '$$triple'." >&2; \
		   echo "  Put an i686 mingw32/bin ahead of PATH (build_winxp.ps1 -Toolchain does this)." >&2; \
		   exit 1;; \
	esac

# Post-link probes: what the flags were supposed to produce, read back off the
# artifact. Only the win32 lane has anything a compiler flag cannot already
# guarantee, so the other lanes pass trivially.
lane-check-artifact:
	@if [ ! -e '$(TARGET)' ]; then echo "lane-check-artifact: no $(TARGET) — build first" >&2; exit 1; fi
	@if [ '$(PLATFORM)' != 'win32' ]; then echo "lane-check-artifact: PLATFORM=$(PLATFORM) ok (nothing to probe)"; exit 0; fi; \
	fmt=`objdump -f '$(TARGET)' | sed -n 's/.*file format //p'`; \
	if [ "$$fmt" != "pei-i386" ]; then \
		echo "lane-check-artifact: $(TARGET) is '$$fmt', expected pei-i386 (32-bit PE)" >&2; exit 1; fi; \
	subv=`objdump -p '$(TARGET)' | sed -n 's/^MajorSubsystemVersion[[:space:]]*//p'`; \
	if [ "$$subv" != "5" ]; then \
		echo "lane-check-artifact: $(TARGET) MajorSubsystemVersion=$$subv, expected 5 — XP will refuse to load it" >&2; exit 1; fi; \
	if nm '$(TARGET)' 2>/dev/null | grep -q ' T SDL_'; then \
		echo "lane-check-artifact: $(TARGET) links SDL — the XP lane is GDI-only" >&2; exit 1; fi; \
	echo "lane-check-artifact: $(TARGET) ok (pei-i386, subsystem 5.01, no SDL)"
