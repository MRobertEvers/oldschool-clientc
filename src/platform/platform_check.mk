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
#   make -C src lane-check PLATFORM=win64
#   make -C src lane-check-artifact PLATFORM=win32   # after linking
#   make -C src lane-check-artifact PLATFORM=win64   # after linking

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
# D3D_DISABLE_9EX removes the Vista-only interfaces from MinGW's d3d9.h at
# compile time. The post-link probe below also proves the executable imports
# classic Direct3DCreate9 and no Ex/D3DX/shader-compiler entry point.
LANE_REQUIRE_win32 := _WIN32_WINNT=0x0501 WINVER=0x0501 -march=i686 \
                      -mfpmath=387 console:5.01 -static -static-libgcc win32_compat.h \
                      TORIRS_HAVE_D3D9=1 D3D_DISABLE_9EX=1 -ld3d9
LANE_FORBID_win32  := -dead_strip -sUSE_SDL=2 webgl1 TORIRS_HAVE_GL3 \
                      -ld3dx -ld3dcompiler -ldxcompiler

# --- Modern Windows: explicit Windows 10+, x86_64, standalone D3D9 ----------
LANE_REQUIRE_win64 := _WIN32_WINNT=0x0A00 WINVER=0x0A00 -march=x86-64 \
                      console:6.0 -static -static-libgcc win32_compat.h \
                      TORIRS_HAVE_D3D9=1 D3D_DISABLE_9EX=1 -ld3d9
LANE_FORBID_win64  := -march=i686 -mfpmath=387 console:5.01 -dead_strip \
                      -sUSE_SDL=2 webgl1 TORIRS_HAVE_GL3 \
                      -ld3dx -ld3dcompiler -ldxcompiler

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
LANE_EXTRA_CHECKS_win32 := lane-check-toolchain-win32 lane-check-fixed-function-win32
LANE_EXTRA_CHECKS_win64 := lane-check-toolchain-win64 lane-check-fixed-function-win32

.PHONY: lane-check lane-check-flags lane-check-all lane-check-artifact \
        lane-check-toolchain-win32 lane-check-toolchain-win64 \
        lane-check-fixed-function-win32

lane-check: lane-check-flags $(LANE_EXTRA_CHECKS_$(PLATFORM))

lane-check-flags:
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

# Every lane's declarative flag contract, from one command. Toolchain-triple
# checks remain on `lane-check` because one ambient gcc cannot be both i686 and
# x86_64; each Windows wrapper runs the full check with its own compiler.
lane-check-all:
	@for p in $(PLATFORM_LIST); do $(MAKE) --no-print-directory PLATFORM=$$p lane-check-flags || exit 1; done

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

lane-check-toolchain-win64:
	@triple=`$(PLATFORM_CC) -dumpmachine 2>/dev/null`; \
	case "$$triple" in \
		x86_64-*mingw*) ;; \
		'') echo "lane-check: '$(PLATFORM_CC)' not found on PATH" >&2; exit 1;; \
		*) echo "lane-check: win64 needs an x86_64 MinGW toolchain, but '$(PLATFORM_CC) -dumpmachine' says '$$triple'." >&2; \
		   echo "  Use build_windows.ps1 or put the repository mingw64/bin first on PATH." >&2; \
		   exit 1;; \
	esac

# COM shader methods are vtable calls and therefore are not named PE imports.
# Enforce the fixed-function/no-9Ex contract at the source boundary too.
lane-check-fixed-function-win32:
	@if grep -En 'Direct3DCreate9Ex[[:space:]]*\(|IDirect3D(9Ex|Device9Ex)|IDirect3DDevice9_Create(Vertex|Pixel)Shader[[:space:]]*\(|D3DX[A-Za-z0-9_]*[[:space:]]*\(|D3D(Compile|Disassemble)[A-Za-z0-9_]*[[:space:]]*\(' \
		platform/platform_win32_renderer_d3d9*.c; then \
		echo "lane-check: Windows D3D9 renderer uses a forbidden 9Ex, shader, or D3DX API" >&2; exit 1; fi

# Post-link probes read the ABI and imports back off each Windows artifact.
# Other platforms have no artifact-only contract yet and pass trivially.
lane-check-artifact:
	@if [ ! -e '$(TARGET)' ]; then echo "lane-check-artifact: no $(TARGET) — build first" >&2; exit 1; fi
	@case '$(PLATFORM)' in \
		win32) expected_fmt=pei-i386; expected_subsystem=5.1; artifact_name='XP i686' ;; \
		win64) expected_fmt=pei-x86-64; expected_subsystem=6.0; artifact_name='modern x86_64' ;; \
		*) echo "lane-check-artifact: PLATFORM=$(PLATFORM) ok (nothing to probe)"; exit 0 ;; \
	esac; \
	fmt=`objdump -f '$(TARGET)' | sed -n 's/.*file format //p'`; \
	if [ "$$fmt" != "$$expected_fmt" ]; then \
		echo "lane-check-artifact: $(TARGET) is '$$fmt', expected $$expected_fmt ($$artifact_name PE)" >&2; exit 1; fi; \
	major=`objdump -p '$(TARGET)' | sed -n 's/^MajorSubsystemVersion[[:space:]]*//p' | sed -n '1p'`; \
	minor=`objdump -p '$(TARGET)' | sed -n 's/^MinorSubsystemVersion[[:space:]]*//p' | sed -n '1p'`; \
	if [ "$$major.$$minor" != "$$expected_subsystem" ]; then \
		echo "lane-check-artifact: $(TARGET) subsystem=$$major.$$minor, expected $$expected_subsystem for $$artifact_name" >&2; exit 1; fi; \
	imports=`objdump -p '$(TARGET)' | sed -n '/The Import Tables/,/PE File Base Relocations/p'`; \
	imports_lc=`printf '%s\n' "$$imports" | tr '[:upper:]' '[:lower:]'`; \
	if ! printf '%s\n' "$$imports_lc" | grep -Eq 'dll name:[[:space:]]*d3d9\.dll'; then \
		echo "lane-check-artifact: $(TARGET) does not import d3d9.dll" >&2; exit 1; fi; \
	if ! printf '%s\n' "$$imports_lc" | grep -Eq '(^|[[:space:]])direct3dcreate9$$'; then \
		echo "lane-check-artifact: $(TARGET) does not import classic Direct3DCreate9" >&2; exit 1; fi; \
	if printf '%s\n' "$$imports_lc" | grep -Eq 'direct3dcreate9ex|idirect3d[^[:space:]]*9ex|dll name:[[:space:]]*d3dx|dll name:[[:space:]]*(d3dcompiler|dxcompiler)|(^|[[:space:]])(d3dcompile[^[:space:]]*|d3ddisassemble|dxccreateinstance|d3dx[^[:space:]]*shader[^[:space:]]*)$$'; then \
		echo "lane-check-artifact: $(TARGET) imports D3D9Ex, D3DX, or a shader compiler" >&2; exit 1; fi; \
	if printf '%s\n' "$$imports_lc" | grep -Eq 'dll name:[[:space:]]*(opengl32|libgles[^.]*)\.dll|dll name:[[:space:]]*(dxgi|d2d1|dwrite)\.dll'; then \
		echo "lane-check-artifact: $(TARGET) imports GL/GLES, DXGI, Direct2D, or DirectWrite" >&2; exit 1; fi; \
	if printf '%s\n' "$$imports_lc" | grep -Eq 'dll name:[[:space:]]*sdl|(^|[[:space:]])_*sdl_[^[:space:]]*$$'; then \
		echo "lane-check-artifact: $(TARGET) imports SDL — Windows lanes are raw Win32" >&2; exit 1; fi; \
	if printf '%s\n' "$$imports_lc" | grep -Eq 'dll name:[[:space:]]*(libgcc_s_[^.]*|libwinpthread-1|libstdc\+\+-6)\.dll'; then \
		echo "lane-check-artifact: $(TARGET) imports a MinGW runtime DLL instead of being standalone" >&2; exit 1; fi; \
	for timing_api in queryperformancecounter queryperformancefrequency timebeginperiod timeendperiod; do \
		if ! printf '%s\n' "$$imports_lc" | grep -Eq "(^|[[:space:]])$$timing_api$$"; then \
			echo "lane-check-artifact: $(TARGET) is missing XP-safe pacing import $$timing_api" >&2; exit 1; fi; \
	done; \
	if printf '%s\n' "$$imports_lc" | grep -Eq '(^|[[:space:]])(gettickcount64|createwaitabletimerex[aw]?|setwaitabletimerex)$$'; then \
		echo "lane-check-artifact: $(TARGET) imports a post-XP timing API" >&2; exit 1; fi; \
	if [ '$(PLATFORM)' = 'win32' ] && printf '%s\n' "$$imports_lc" | grep -Eq '(^|[[:space:]])_?putenv_s$$'; then \
		echo "lane-check-artifact: $(TARGET) imports _putenv_s, which XP msvcrt.dll does not export" >&2; exit 1; fi; \
	echo "lane-check-artifact: $(TARGET) ok ($$expected_fmt, subsystem $$expected_subsystem, fixed-function d3d9, QPC pacing, standalone)"
