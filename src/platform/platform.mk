# Platform target selection.
# Human-readable platform contracts and known defects live in
# docs/platform_quirks.md; update that registry with every platform exception.
#
# One variable picks the whole host: compiler, windowing/audio/IO backends,
# object directory and link output.
#
#   make -C src all                    host native, debug -> src/torirs
#   make -C src release                host native, opt   -> src/torirs
#   make -C src winxp                  Windows XP, opt    -> src/torirs.exe
#   make -C src win64                  Windows 10+ x64    -> src/torirs_win64.exe
#   make -C src web                    web, optimized     -> build-web/torirs.js
#
# Objects never mix: each (PLATFORM, OPT) pair owns its own OBJ_DIR, so
# switching targets never links a wasm object into a native binary or reads a
# struct field at an offset another flavor's headers produced.
#
# This file is the only place that knows a platform exists. The makefile reads
# the PLATFORM_* variables below and never tests PLATFORM itself, so adding a
# platform means adding one block here plus its platform/*.c backends; nothing
# above this file needs to know the list. Every block must set all of:
#
#   PLATFORM_CC              compiler driver
#   PLATFORM_OBJ_BASE        objdir stem (flavor suffixes are appended)
#   PLATFORM_TARGET          link output
#   PLATFORM_SRCS            windowing/audio/IO backends to add to SRCS
#   PLATFORM_GPU_OBJ_NAMES   GPU-binding object *names* (no path -- OBJ_DIR is
#                            computed after this file is included)
#   PLATFORM_BASE_CFLAGS     flags every TU needs, vendored units included
#   PLATFORM_CFLAGS          base + this platform's client-only flags
#   PLATFORM_LDFLAGS         link flags
#   PLATFORM_STRIP_LDFLAGS   dead-code stripping, if this linker has it
#   PLATFORM_MEMTRACE_WRAP_LDFLAGS  how MEMTRACE=1 reaches the allocator here
#   PLATFORM_TARGET_MEMTRACE_SUFFIX suffix MEMTRACE=1 adds to PLATFORM_TARGET
#   PLATFORM_EXE_SUFFIX      what the linker appends to an executable here
#
# and may override PLATFORM_WINDOW_SRC (defaulted at the bottom).

PLATFORM_LIST := macos linux win32 win64 web android

PLATFORM ?= native

# `native` is not a platform, it is "whatever this host is". Resolving it here
# keeps every block below a declaration rather than a probe -- the old single
# `native` block branched on uname *inside* itself, which is how an Apple-only
# linker flag ended up on the Linux link line. $(OS) is set by Windows itself,
# so a native Windows build selects the modern x64 lane without needing uname.
# The XP wrapper always requests the explicit win32 lane.
#
# `override` is load-bearing: `native` also arrives from the command line (the
# io-server target re-invokes make with PLATFORM=native), and a command-line
# variable beats a plain assignment here -- without it PLATFORM would stay
# `native`, match no block, and hit the unknown-PLATFORM error below.
ifeq ($(PLATFORM),native)
  ifeq ($(OS),Windows_NT)
    override PLATFORM := win64
  else ifeq ($(shell uname -s),Darwin)
    override PLATFORM := macos
  else
    override PLATFORM := linux
  endif
endif

ifneq ($(filter $(PLATFORM),macos linux),)
  # --- Desktop SDL2 hosts -----------------------------------------------------
  # macOS and Linux differ only in how GL is linked and whether the linker has a
  # dead-strip flag, so they share this block and split in the tail below. They
  # are still separate PLATFORM values: the difference has to be declarable, not
  # discovered mid-recipe.
  PLATFORM_CC       := $(if $(filter default,$(origin CC)),cc,$(CC))
  PLATFORM_OBJ_BASE := build
  PLATFORM_TARGET   := torirs
  # Windowing/audio/IO backends for this host. The lists are subtracted from
  # and added to the shared SRCS in the makefile, so a backend swap is local.
  PLATFORM_SRCS     := platform/platform_x_io.c \
                       platform/platform_x_io_js5_cache.c \
                       platform/platform_x_io_ondemand.c \
                       platform/platform_x_http.c \
                       platform/platform_audio_sdl2.c \
                       platform/platform_gl_context_sdl.c \
                       platform/platform_sdl2_renderer_gl3.c \
                       platform/platform_sdl2_renderer_gl3zb.c
  # The desktop-GL binding. The web lane builds the same renderer against
  # WebGL1 and needs its index-splitting object instead; the Windows lanes own
  # D3D9 in their regular platform source and need no binding object here.
  PLATFORM_GPU_OBJ_NAMES := opengl3_sdlgl.o
  PLATFORM_EXE_SUFFIX :=
  # The JS5 cache producer is executor-side and currently native-only. Tests
  # live under js5/test, so this non-recursive wildcard picks production units
  # without accidentally linking test mains into the client.
  JS5_SRCS          := $(wildcard js5/*.c)

  SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
  SDL_LIBS   := $(shell pkg-config --libs   sdl2 2>/dev/null)
  ifeq ($(strip $(SDL_CFLAGS)$(SDL_LIBS)),)
    SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
    SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null)
  endif
  ifeq ($(strip $(SDL_CFLAGS)$(SDL_LIBS)),)
    SDL_CFLAGS := -I/opt/homebrew/include/SDL2 -I/usr/local/include/SDL2
    SDL_LIBS   := -L/opt/homebrew/lib -L/usr/local/lib -lSDL2
  endif

  # Flags every translation unit needs, third-party units included.
  PLATFORM_BASE_CFLAGS :=
  # The desktop-GL frame renderer exists only here; main.c compiles its GL
  # paths out rather than stubbing them, so --opengl3 is refused instead of
  # silently ignored on a host that cannot do it.
  PLATFORM_CFLAGS_EXTRA := -DTORIRS_HAVE_GL3=1
  PLATFORM_CFLAGS  := $(PLATFORM_BASE_CFLAGS) $(PLATFORM_CFLAGS_EXTRA) $(SDL_CFLAGS)
  PLATFORM_LDFLAGS := -lm $(SDL_LIBS)

  # A traced binary must not share ./torirs with an untraced one: MEMTRACE is a
  # long-running capture and `make -C src all` in another terminal would
  # silently replace it mid-session. Only the lanes whose output name is not
  # referenced from outside the build can afford the rename.
  PLATFORM_TARGET_MEMTRACE_SUFFIX := _mt

  ifeq ($(PLATFORM),macos)
    # A dynamically linked SDL may allocate before ASan's interceptors finish
    # initialising on macOS 26. The static archive avoids that startup edge;
    # src/makefile supplies the matching dyld interpose shim.
    ifeq ($(ENABLE_ASAN),1)
      SDL_LIBS := $(shell sdl2-config --static-libs 2>/dev/null)
      ifeq ($(strip $(SDL_LIBS)),)
        SDL_LIBS := $(shell pkg-config --libs --static sdl2 2>/dev/null)
      endif
      ifeq ($(strip $(SDL_LIBS)),)
        SDL_LIBS := -L/opt/homebrew/lib -Wl,-force_load,/opt/homebrew/lib/libSDL2.a \
                    -framework CoreAudio -framework AudioToolbox -framework CoreVideo \
                    -framework Cocoa -framework Carbon -framework IOKit \
                    -framework QuartzCore -framework Metal -lobjc
      endif
      PLATFORM_LDFLAGS := -lm $(SDL_LIBS)
    endif
    PLATFORM_LDFLAGS += -framework OpenGL
    PLATFORM_STRIP_LDFLAGS := -Wl,-dead_strip
    # ld64 has no --wrap. The tracer instead defines malloc/free as strong
    # symbols and reaches the real ones through dlsym(RTLD_NEXT), so there is
    # nothing to add at link time.
    PLATFORM_MEMTRACE_WRAP_LDFLAGS :=
  else
    PLATFORM_LDFLAGS += -lGL
    PLATFORM_MEMTRACE_WRAP_LDFLAGS := \
        -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
        -Wl,--wrap=reallocf -Wl,--wrap=posix_memalign -Wl,--wrap=strdup
    # Deliberately empty. -dead_strip is ld64-only and GNU ld rejects it, which
    # is why it cannot just be shared with macOS. The GNU equivalent,
    # --gc-sections, only does real work when the objects were compiled with
    # -ffunction-sections -fdata-sections, and this tree does not use those --
    # so setting it here would be a no-op that looked like parity.
    PLATFORM_STRIP_LDFLAGS :=
  endif

else ifeq ($(PLATFORM),win32)
  # Explicit Windows XP host: a raw Win32 window with fixed-function D3D9 by
  # default and the GDI Soft3D presenter behind explicit --soft3d. There is no
  # SDL, GL, D3D9Ex, D3DX or shader compiler dependency. The windowing source is
  # swapped in the Makefile via PLATFORM_WINDOW_SRC below.
  PLATFORM_CC       := $(if $(filter default,$(origin CC)),gcc,$(CC))
  PLATFORM_OBJ_BASE := build_win32
  PLATFORM_TARGET   := torirs.exe
  # IO is the portable stdio backend; audio is the null backend (no sound on XP
  # for now). No GL renderer, no SDL audio.
  PLATFORM_SRCS     := platform/platform_x_io.c \
                       platform/platform_x_io_js5_cache.c \
                       platform/platform_x_io_ondemand.c \
                       platform/platform_x_http.c \
                       platform/platform_audio_null.c \
                       platform/platform_win32_timing.c \
                       platform/platform_win32_renderer_d3d9_core.c \
                       platform/platform_win32_renderer_d3d9_painter.c \
                       platform/platform_win32_renderer_d3d9_zbuffer.c
  # Keep the executor-side JS5 producer native-only. The wildcard is
  # deliberately non-recursive so server and test mains cannot enter the client.
  JS5_SRCS          := $(wildcard js5/*.c)
  # TRSPK's CPU retained-mode core is already linked through trspk_unity.o.
  # D3D9 calls live in the regular platform source above, so there is no extra
  # out-of-tree binding object (and in particular no WebGL object) here.
  PLATFORM_GPU_OBJ_NAMES :=
  # The linker appends .exe here. Host tools (sscompile, cachepack) are invoked
  # by path, and a tracked macOS build of cachepack sits next to the Windows one
  # under the extensionless name -- so the suffix has to be spelled, not left to
  # the shell's .exe fallback, or the Mach-O file wins and sh reports only
  # "Exec format error".
  PLATFORM_EXE_SUFFIX := .exe
  # platform_win32gdi.c replaces platform_sdl2.c (see PLATFORM_WINDOW_SRC).
  PLATFORM_WINDOW_SRC := platform/platform_win32gdi.c
  #
  # The XP ABI contract. Every flag below is load-bearing for "runs on XP SP3",
  # and none of it can be left to the ambient toolchain:
  #
  #   _WIN32_WINNT/WINVER=0x0501  cap the Windows API surface at XP, so a
  #                               post-XP import fails at compile time here
  #                               rather than as "procedure entry point not
  #                               found" on the target.
  #   -march=pentium4             SSE2 is assumed present on every XP target.
  #     -mfpmath=sse              Pentium 4 is the canonical 32-bit SSE2
  #                               baseline; -mfpmath=sse then keeps float work
  #                               out of x87 entirely.
  #
  #                               This is load-bearing for speed, not just
  #                               codegen taste. The textured span in
  #                               3rd/toridraw/.../span/tex.span.u.c selects
  #                               NEON/AVX2/SSE4.1/SSE2/scalar with #if at
  #                               compile time, and -march=i686 meant this lane
  #                               took the scalar fallback -- on a lane where
  #                               the textured span is 79% of raster cycles and
  #                               ~50% of the frame. See the R1 target in
  #                               docs/CS2_OPTIMIZATION_TARGETS.md.
  #
  #                               -mfpmath=sse also drops x87's 80-bit
  #                               intermediates, so float results can differ in
  #                               the last bit from the old lane. That is a
  #                               deliberate accepted change, not a regression.
  #   -include win32_compat.h     setenv/unsetenv, which MinGW does not ship
  #                               and the embedded rev-230 server calls. It is
  #                               in BASE_CFLAGS, not CFLAGS, so it reaches the
  #                               vendored units too -- same reasoning as
  #                               _GNU_SOURCE on the web lane below.
  #
  # 32-bitness is deliberately *not* forced with -m32: on an x86_64 MinGW
  # without multilib that fails deep in the assembler. `make lane-check`
  # asserts the toolchain triple instead, which fails legibly.
  PLATFORM_BASE_CFLAGS := -DTORIRS_HAVE_D3D9=1 -DD3D_DISABLE_9EX=1 \
                          -DTORIRS_NO_D3D8=1 -DTORIRS_NO_D3D11=1 \
                          -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 \
                          -march=pentium4 -mtune=generic -mfpmath=sse \
                          -include $(SRC_DIR)/platform/win32_compat.h
  # No TORIRS_HAVE_GL3: this lane selects the fixed-function D3D9 path instead.
  PLATFORM_CFLAGS  := $(PLATFORM_BASE_CFLAGS)
  #
  # --subsystem,console:5.01 does two things and replaces a bare -mconsole. It
  # keeps stdout/stderr (the perf eff_fps line) attached, and it stamps the PE
  # subsystem *version* at 5.01. Modern binutils default that field to 6.00,
  # and XP refuses to load an image that claims to need Vista.
  #
  # -static-libgcc so the target needs no libgcc DLL. There is no
  # -static-libstdc++ counterpart because this lane is pure C11 -- that flag
  # belongs to the retired CMake recipe, which compiled C++ platform sources.
  # -static, not just -static-libgcc. The toolchain is built `threads=posix`, so
  # its runtime pulls in libwinpthread-1.dll even for a program that starts no
  # threads — and the binary then fails to start with STATUS_DLL_NOT_FOUND
  # (0xC0000135) anywhere that DLL is not beside it. It resolved here only
  # because the toolchain's own bin/ happened to be on PATH, which is exactly
  # the accident that does not survive the copy to an XP box: build_winxp.ps1
  # stages the .exe and nothing else. Linking static makes the one file the
  # whole deliverable, which is what this lane is for.
  PLATFORM_LDFLAGS := -lm -static -static-libgcc -Wl,--subsystem,console:5.01 \
                      -ld3d9 -lgdi32 -luser32 -lws2_32 -lwinmm -lpsapi -lkernel32
  # See the linux note above: --gc-sections would be a no-op here too.
  PLATFORM_STRIP_LDFLAGS :=
  PLATFORM_MEMTRACE_WRAP_LDFLAGS := \
      -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
      -Wl,--wrap=reallocf -Wl,--wrap=posix_memalign -Wl,--wrap=strdup
  # torirs.exe_mt would not be a runnable name; the suffix is for the extension-
  # less desktop output only.
  PLATFORM_TARGET_MEMTRACE_SUFFIX :=

else ifeq ($(PLATFORM),win64)
  # Modern Windows 10/11, x86_64. This deliberately shares the proven raw
  # Win32 + fixed-function D3D9/GDI backends with XP; the platform difference
  # is the ABI/toolchain contract, not a second window or renderer stack.
  PLATFORM_CC       := $(if $(filter default,$(origin CC)),gcc,$(CC))
  PLATFORM_OBJ_BASE := build_win64
  # Keep the source artifact distinct from XP's tracked src/torirs.exe. The
  # wrapper stages this as dist/win64/torirs.exe.
  PLATFORM_TARGET   := torirs_win64.exe
  PLATFORM_SRCS     := platform/platform_x_io.c \
                       platform/platform_x_io_js5_cache.c \
                       platform/platform_x_io_ondemand.c \
                       platform/platform_x_http.c \
                       platform/platform_audio_null.c \
                       platform/platform_win32_timing.c \
                       platform/platform_win32_renderer_d3d9_core.c \
                       platform/platform_win32_renderer_d3d9_painter.c \
                       platform/platform_win32_renderer_d3d9_zbuffer.c
  # Keep the executor-side JS5 producer native-only. The wildcard is
  # deliberately non-recursive so server and test mains cannot enter the client.
  JS5_SRCS          := $(wildcard js5/*.c)
  PLATFORM_GPU_OBJ_NAMES :=
  PLATFORM_EXE_SUFFIX := .exe
  PLATFORM_WINDOW_SRC := platform/platform_win32gdi.c

  # Explicit modern ABI floor: x86_64 Windows 10/11. The source remains pure C
  # and fixed-function D3D9; D3D9Ex/D3DX/shader compilers are not introduced
  # just because the OS floor is newer. win32_compat.h supplies the embedded
  # server's POSIX setenv/unsetenv names on MinGW.
  PLATFORM_BASE_CFLAGS := -DTORIRS_HAVE_D3D9=1 -DD3D_DISABLE_9EX=1 \
                          -DTORIRS_NO_D3D8=1 -DTORIRS_NO_D3D11=1 \
                          -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 \
                          -march=x86-64 -mtune=generic \
                          -include $(SRC_DIR)/platform/win32_compat.h
  PLATFORM_CFLAGS := $(PLATFORM_BASE_CFLAGS)

  # One-file delivery, just like XP. This Winlibs compiler uses POSIX threads;
  # without -static it can pull in libwinpthread-1.dll even though the client
  # does not create a pthread.
  # PE subsystem versions are not Windows marketing/API versions. MSVC uses
  # 6.0 for modern x64 console programs; stamping 10.0 makes the Windows 11
  # loader reject the image with STATUS_INVALID_IMAGE_FORMAT before main.
  # _WIN32_WINNT/WINVER above remain the actual Windows 10 API floor.
  PLATFORM_LDFLAGS := -lm -static -static-libgcc -Wl,--subsystem,console:6.0 \
                       -ld3d9 -lgdi32 -luser32 -lws2_32 -lwinmm -lpsapi -lkernel32
  PLATFORM_STRIP_LDFLAGS :=
  PLATFORM_MEMTRACE_WRAP_LDFLAGS := \
      -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
      -Wl,--wrap=reallocf -Wl,--wrap=posix_memalign -Wl,--wrap=strdup
  PLATFORM_TARGET_MEMTRACE_SUFFIX :=

else ifeq ($(PLATFORM),web)
  PLATFORM_CC       := emcc
  PLATFORM_TARGET   := $(REPO_ROOT)/build-web/torirs.js
  # WebGL1 cannot index past 16 bits, so this lane needs the index-splitting
  # object the desktop GL binding does not (see webgl1_index16.h).
  PLATFORM_GPU_OBJ_NAMES := webgl1_index16.o
  # emcc names its own outputs (PLATFORM_TARGET is explicit); host tools built
  # alongside a web build are native and take the host's suffix, which on the
  # machines this lane runs on is none.
  PLATFORM_EXE_SUFFIX :=

  # --- How a browser answers the IO queue ----------------------------------
  #
  # ONE web lane, and its platform IO executor is JavaScript.
  #
  # The architecture is [game -> IO Queue] :> [platform IO executor]. The game
  # queues items; the platform executes them. On the desktop that executor is
  # platform_x_io.c, which has a filesystem and sockets. A browser has neither
  # and cannot read anything synchronously without freezing the tab, so its
  # executor is platform/platform_web_io.js, linked with --js-library so that
  # its functions ARE the definitions of the PlatformX_IO_* symbols the client
  # calls. No C shim stands in between: one would be a second executor inside
  # the seam, reading the queue twice, in two languages, with two chances to
  # disagree about what an item says.
  #
  # WHERE EACH KIND'S BYTES COME FROM:
  #
  #   CACHE / REFERENCE_TABLE   the record database, which the cache PRODUCERS
  #                             fill -- JS5 for dat2, the 2004 on-demand
  #                             protocol for dat1. Those already have servers of
  #                             their own and do their own CRC and version
  #                             checks, so the executor reads what they wrote
  #                             and never fetches an archive itself.
  #
  #   CONFIG_FILE / SCRIPT      the database, then io_server's /boot/ route.
  #                             These are the ones that had no route at all
  #                             before, which is why a plugin's assets were
  #                             unreachable in a browser however healthy the
  #                             server was.
  #
  #   FILE_READ / FILE_WRITE    the database and nothing else. They are the
  #                             player's own, and io_server refuses them by kind
  #                             for the same reason.
  #
  # WHAT IS STILL C, and why that is not a compromise:
  #
  #   asyncio_abi.c        the queue reporting its own layout, so the executor
  #                        reads offsets it was given rather than offsets it
  #                        assumed. A wrong offset does not fail -- it decodes
  #                        `kind` out of the middle of a path.
  #
  #   platform_web_api.c   the cache format. Container framing, bzip2, gzip,
  #                        XTEA and the reference table have exactly one
  #                        implementation in this tree (3rd/rscache), and a
  #                        second one in JavaScript would be wrong in ways
  #                        nothing downstream could catch: a bad decode does not
  #                        throw, it yields a plausible archive. DECODE IS NOT
  #                        IO, so it was never the executor's job.
  #
  # There is deliberately no second lane. A "wire" lane once existed in which
  # the browser had no cache at all and every item crossed a socket to
  # io_server; it needed its own C backend (platform_x_io_web.c), its own wire
  # codec, and a duplicate of every decision this one makes. One executor that
  # reads a local store and falls back to the server covers both deployments:
  # with no server the database answers, with an empty database the server does.
  PLATFORM_OBJ_BASE := build_web
  PLATFORM_SRCS     := platform/platform_web_api.c \
                       platform/platform_audio_wasm.c \
                       platform/platform_gl_context_sdl.c \
                       platform/platform_sdl2_renderer_webgl1.c \
                       platform/platform_sdl2_renderer_webgl1zb.c
  # The queue's ABI reporter. Not in PLATFORM_SRCS because it belongs to the
  # QUEUE, not to any platform: a second non-C executor would need the same
  # numbers, and putting it beside the platform that reads it today would make
  # the next one copy it.
  PLATFORM_EXTRA_SRCS := asyncio_abi.c
  # No JS5 here: the producer is JavaScript (src/web/torirs_js5.js), and so
  # is the dat1 one. Nothing in this module speaks either protocol.
  JS5_SRCS          :=
  WEB_CACHE_CFLAGS  :=
  # The executor is a link input: a change to it must relink the module, because
  # it is as much a part of the program as any object file.
  PLATFORM_JS_LIBRARY := $(SRC_DIR)/platform/platform_web_io.js
  # Everything JavaScript calls into. Without these on the list the runtime may
  # drop them, and the failure is a page that loads and then throws on the first
  # read rather than a link error naming the symbol.
  WEB_CACHE_EXPORTS := ,"_ToriRS_IO_DescribeAbi","_ToriRS_IO_DescribeAbiCount","_ToriRS_WebApi_ArchiveDecode","_ToriRS_WebApi_ArchiveApplyMetadata","_ToriRS_WebApi_ReferenceTableFromContainer","_ToriRS_WebApi_Dat1ArchiveDecode","_ToriRS_WebApi_Dat1ArchiveStructSize","_ToriRS_WebApi_ArchiveStructSize","_ToriRS_WebApi_ReferenceTableStructSize","_ToriRS_WebApi_ArchiveFree","_ToriRS_WebApi_Dat2TableDiskId"
  # UTF8ArrayToString/UTF8ToString read paths out of the queue; setValue is used
  # by the store's EM_JS bodies and the ABI read.
  WEB_CACHE_RUNTIME := ,"setValue","UTF8ToString","UTF8ArrayToString"

  # TORIRS_PLATFORM_WEB is the source-level switch (there is no local disk, so
  # App_Init must not open one). -sUSE_SDL=2 must be a *compile* flag too: it
  # is what puts the SDL2 port's headers on the include path.
  #
  # _GNU_SOURCE: the tree uses strdup/strtok_r/strcasecmp, which are POSIX, not
  # ISO C. Apple libc declares them under -std=c11 anyway; emscripten's musl
  # headers correctly do not, and an undeclared strdup compiles to a call
  # returning int — a truncated pointer, and a crash far from the call.
  #
  # WEB_CACHE_CFLAGS is in BASE, not CFLAGS: TORIRS_WEB_CACHE_IDB changes what
  # RSCache_Dat2Disk means to app.c AND how the vendored rscache unit is built,
  # so a definition only the client saw would give the two halves different
  # ideas of the same struct.
  PLATFORM_BASE_CFLAGS := -DTORIRS_PLATFORM_WEB=1 -D_GNU_SOURCE $(WEB_CACHE_CFLAGS)
  # TORIRS_GL_ES2 builds the GPU renderer against WebGL1 (GLES2, no
  # extensions); TORIRS_HAVE_GL3 says a GPU renderer exists at all. It is
  # opt-in, like the desktop one: pass --webgl1 (in the page's query string).
  PLATFORM_CFLAGS  := $(PLATFORM_BASE_CFLAGS) -sUSE_SDL=2 \
                      -DTORIRS_GL_ES2=1 -DTORIRS_HAVE_GL3=1 \
                      -Wno-unknown-warning-option

  # Nothing is baked into the module. The files the client opens by name — the
  # boot manifest, the RevConfig INIs it points at — are named on the command
  # line, and the command line here is the page's query string, so they cannot
  # be decided at link time. The harness fetches them from the IO server's
  # /boot/ route into the virtual filesystem before main() runs, which is what
  # lets one build open any manifest. FORCE_FILESYSTEM below is for that.

  # Memory. The client allocates and frees multi-megabyte archives for the whole
  # boot (a dat2 config group is a couple of MB) interleaved with small
  # long-lived ones. A wasm heap never shrinks, so that pattern is exactly what
  # fragments dlmalloc — the same osrs230 boot that peaks at 250MB natively ran
  # the default allocator past the 2GB ceiling. mimalloc handles the mixed-size
  # churn, and the 4GB maximum is the wasm32 limit, i.e. headroom rather than a
  # reservation (growth is on demand).
  # This is what makes "WebGL1, no extensions" a guarantee rather than an
  # intention. MIN/MAX_WEBGL_VERSION=1 stops the runtime handing the client a
  # WebGL2 context, so a GLES3-only call fails here instead of in someone
  # else's browser; GL_SUPPORT_AUTOMATIC_ENABLE_EXTENSIONS=0 stops emscripten
  # quietly enabling every extension the browser offers, so a renderer that
  # reached for one would fail here too.
  #
  # There is deliberately no -sASYNCIFY. The IO path yields to
  # emscripten_set_main_loop and lets torirs_host.js pump responses back in
  # (see docs/web_build.md); ASYNCIFY would rewrite the whole module to get the
  # same effect at a large size and speed cost. `make lane-check` asserts it
  # stays absent.
  PLATFORM_LDFLAGS := -lm -sUSE_SDL=2 \
                      -sMIN_WEBGL_VERSION=1 \
                      -sMAX_WEBGL_VERSION=1 \
                      -sGL_SUPPORT_AUTOMATIC_ENABLE_EXTENSIONS=0 \
                      -sMALLOC=mimalloc \
                      -sALLOW_MEMORY_GROWTH=1 \
                      -sINITIAL_MEMORY=268435456 \
                      -sMAXIMUM_MEMORY=4294967296 \
                      -sSTACK_SIZE=8388608 \
                      -sFORCE_FILESYSTEM=1 \
                      -sEXIT_RUNTIME=0 \
                      -sENVIRONMENT=web \
                      -sMODULARIZE=0 \
                      -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPU8","HEAP32","callMain","FS","FS_readFile"$(WEB_CACHE_RUNTIME)]' \
                      -sEXPORTED_FUNCTIONS='["_main","_malloc","_free","_torirs_cmdbus_push_bytes"$(WEB_CACHE_EXPORTS)]' \
                      --js-library $(PLATFORM_JS_LIBRARY)
  # emcc strips at the wasm level; there is no ld flag to add here.
  PLATFORM_STRIP_LDFLAGS :=
  # emscripten's musl only routes these four through --wrap; reallocf and
  # posix_memalign are not separate symbols there, and strdup is inlined to a
  # malloc it already sees.
  PLATFORM_MEMTRACE_WRAP_LDFLAGS := \
      -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free
  # The web target keeps its name, which the page's <script src> is written
  # against.
  PLATFORM_TARGET_MEMTRACE_SUFFIX :=

  # OPT=0 on the web lane is -Og, not -O0. Every other lane can afford a
  # literal -O0 debug build; wasm cannot. Clang emits one wasm local per
  # source temporary and one load/store per access at -O0, so the module comes
  # out several times larger, the browser spends that much longer compiling it,
  # and the client runs too slowly to reproduce anything worth debugging --
  # a frame budget missed by 10x changes what the bug looks like. -Og keeps
  # every local variable inspectable and does not reorder the code, so a
  # DWARF-stepping session still works; it just stops emitting the naive form
  # of it. PLATFORM_DEBUG_O_LEVEL is what the makefile puts in $(O_LEVEL) for
  # OPT=0 (see the default at the bottom of this file).
  PLATFORM_DEBUG_O_LEVEL := -Og

  ifeq ($(OPT),1)
    # -g0 discards the DWARF the shared CFLAGS' -g put in the objects. Without
    # it emcc keeps the debug info and, to keep it valid, runs only a subset of
    # the binaryen optimizations — a release build that is both larger and
    # slower than asked for.
    PLATFORM_LDFLAGS += -O3 -g0
  else
    # -Og at link too: emcc decides the binaryen pass set from the *link*
    # -O level, and defaults to -O0 there no matter what the objects were
    # compiled with -- so leaving it off gives back the unoptimized wasm the
    # compile-side -Og was chosen to avoid. ASSERTIONS stays on; that is what
    # the debug lane is for.
    PLATFORM_LDFLAGS += -Og -sASSERTIONS=1
  endif

else ifeq ($(PLATFORM),android)
  # --- Android, NDK, no SDL ---------------------------------------------------
  #
  # The same shape as the win32 lane: a raw platform window backend implementing
  # platform_sdl2.h, a CPU-presented software frame, and no SDL, GL, or audio
  # dependency at all. What differs is that the link output is a SHARED LIBRARY
  # rather than an executable -- an Android app's process is started by the
  # runtime, and this lane's `main` is called on a thread the Java side spawns
  # (platform/platform_android_jni.c), not by the kernel.
  #
  # Two backends, both new, and the split is the same one every lane makes:
  #
  #   platform_android.c       the PlatformSDL2 interface over ANativeWindow.
  #                            Owns the ARGB canvas, the letterbox, and the
  #                            blit. Knows nothing about Java.
  #   platform_android_jni.c   the JNI surface. Owns the render thread, the
  #                            Surface handoff, and the input queue. Knows
  #                            nothing about how a frame is drawn.
  #
  # Nothing above platform/ changes: main.c's frame loop, the software renderer
  # and the touch gesture policy (input/torirs_touch.c) are the desktop ones.
  #
  # ABI/API. ANDROID_ABI picks the architecture and ANDROID_API the platform
  # floor; both are variables because the device decides them, not the tree.
  # The default pair is the OLDEST thing this lane is expected to run on -- a
  # 32-bit armv7 phone at API 21 -- because that is the target that fails, and a
  # default that silently only works on a modern device is a default that hides
  # the lane's actual constraint until someone plugs in the old one.
  ANDROID_ABI ?= armeabi-v7a
  ANDROID_API ?= 21

  # Where the NDK is. ANDROID_NDK_HOME wins; otherwise the newest one under the
  # SDK that ANDROID_HOME/ANDROID_SDK_ROOT names, and failing that the
  # per-platform default install location. Probed rather than declared because
  # this is the one fact about the lane that lives on the machine and not in the
  # tree -- everything else in this block is a declaration.
  ANDROID_SDK_HOME ?= $(if $(ANDROID_HOME),$(ANDROID_HOME),$(if $(ANDROID_SDK_ROOT),$(ANDROID_SDK_ROOT),$(HOME)/Library/Android/sdk))
  #
  # The NDK version is PINNED, not "whatever is newest".
  #
  # A toolchain change moves the codegen, the libc stubs and the default linker
  # behaviour all at once, and on a lane whose target device is a 2013 phone
  # that is not something to inherit from whatever happened to be installed
  # last. This is the same version android/build.gradle names, so the .so and
  # the APK are built by one toolchain. ANDROID_NDK_VERSION overrides it; if the
  # pinned one is absent the newest is used with a warning, rather than the
  # compiler driver failing with a path nobody recognises.
  ANDROID_NDK_VERSION ?= 27.0.12077973
  ifeq ($(strip $(ANDROID_NDK_HOME)),)
    ifneq ($(wildcard $(ANDROID_SDK_HOME)/ndk/$(ANDROID_NDK_VERSION)),)
      ANDROID_NDK_HOME := $(ANDROID_SDK_HOME)/ndk/$(ANDROID_NDK_VERSION)
    else
      ANDROID_NDK_HOME := $(lastword $(sort $(wildcard $(ANDROID_SDK_HOME)/ndk/*)))
      $(warning NDK $(ANDROID_NDK_VERSION) is not installed - falling back to $(notdir $(ANDROID_NDK_HOME)); sdkmanager "ndk;$(ANDROID_NDK_VERSION)" installs the pinned one)
    endif
  endif

  # The prebuilt toolchain's host tag. The NDK ships one per build host, and on
  # Apple Silicon the tag is still darwin-x86_64 (it runs under Rosetta) -- so
  # this is keyed off the OS, never off the machine's architecture.
  ifeq ($(shell uname -s),Darwin)
    ANDROID_HOST_TAG := darwin-x86_64
  else
    ANDROID_HOST_TAG := linux-x86_64
  endif
  ANDROID_TOOLCHAIN := $(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/$(ANDROID_HOST_TAG)

  # The clang driver name encodes the triple AND the API level, which is how the
  # NDK selects the right libc stubs. armeabi-v7a's triple carries the `eabi`
  # suffix and its own -mfpu; every other ABI is plain.
  ifeq ($(ANDROID_ABI),armeabi-v7a)
    ANDROID_TRIPLE     := armv7a-linux-androideabi
    # NEON is not optional here. The toridraw span/projection/facesort kernels
    # dispatch on __ARM_NEON at compile time, and without -mfpu=neon every one
    # of them silently takes the scalar fallback -- on the exact device class
    # that can least afford it. Every armv7 Android device since 2012 has NEON.
    ANDROID_ARCH_CFLAGS := -march=armv7-a -mfpu=neon -mfloat-abi=softfp
  else ifeq ($(ANDROID_ABI),arm64-v8a)
    ANDROID_TRIPLE     := aarch64-linux-android
    ANDROID_ARCH_CFLAGS := -march=armv8-a
  else ifeq ($(ANDROID_ABI),x86_64)
    ANDROID_TRIPLE     := x86_64-linux-android
    ANDROID_ARCH_CFLAGS := -msse4.1
  else
    $(error unsupported ANDROID_ABI '$(ANDROID_ABI)' — expected armeabi-v7a, arm64-v8a or x86_64)
  endif

  PLATFORM_CC       := $(ANDROID_TOOLCHAIN)/bin/$(ANDROID_TRIPLE)$(ANDROID_API)-clang
  # Per-ABI objects. Two ABIs are two different compilers and two different
  # word sizes; sharing an OBJ_DIR between them is the mixed-flavor failure the
  # top of this file exists to prevent.
  PLATFORM_OBJ_BASE := build_android_$(ANDROID_ABI)
  # Straight into the APK's jniLibs source set, so a native build is immediately
  # what the next `gradle assemble` packages. The Gradle project does not build
  # C -- it consumes what this makefile produced (android/README.md).
  PLATFORM_TARGET   := $(REPO_ROOT)/android/src/main/jniLibs/$(ANDROID_ABI)/libtorirs.so

  # Portable stdio IO and the null audio backend. Android has a filesystem and
  # ordinary BSD sockets, so platform_x_io.c and the JS5/on-demand cache
  # producers work here unchanged; audio is silent for now, exactly as the two
  # Windows lanes are.
  PLATFORM_SRCS     := platform/platform_x_io.c \
                       platform/platform_x_io_js5_cache.c \
                       platform/platform_x_io_ondemand.c \
                       platform/platform_x_http.c \
                       platform/platform_audio_null.c \
                       platform/platform_android_jni.c \
                       platform/platform_android_gl.c \
                       platform/platform_sdl2_renderer_webgl1.c \
                       platform/platform_sdl2_renderer_webgl1zb.c
  PLATFORM_WINDOW_SRC := platform/platform_android.c
  JS5_SRCS          := $(wildcard js5/*.c)

  # --- NO EMBEDDED SERVER on this lane ---------------------------------------
  #
  # EMBED_SERVER stays 0 here. Android is a CLIENT: it dials a real server over
  # TCP or WebSocket, exactly as the desktop client does when it is not hosting
  # its own world. Linking ToriRSServer into the APK would put a second world
  # simulation on a phone, needing the compiled script pack and the server's own
  # copy of the cache on the device, to serve exactly one player who is already
  # in the process.
  #
  # This is worth STATING rather than leaving as a default, because the failure
  # mode is silent. net_transport_embed.c compiles to a stub without
  # -DTORIRS_EMBED_SERVER=1, so a manifest carrying `[net:boot] transport=embed`
  # does not fail to load here -- it comes up and connects to nothing. The boot
  # menu is what catches that before it happens: BootProfile refuses an `embed`
  # manifest by name and says why, the same way it refuses one whose cache was
  # never pushed.
  #
  # A lane that genuinely wanted the in-process world could still ask for it
  # (`make -C src PLATFORM=android EMBED_SERVER=1 ...`) and would get its own
  # OBJ_DIR, as on every other host. Nothing here forbids it; it is simply not
  # what this lane is for.

  # --- The GPU variant is GLES2, and it is the one this tree already has ------
  #
  # Android's GPU renderer is the WEB lane's renderer, unchanged. That is not a
  # shortcut -- WebGL1 *is* GLES2, and the file was written to that ceiling: no
  # uniform blocks, no 32-bit element indices (hence webgl1_index16.o, which
  # splits an index range into 16-bit windows), no GLES3/desktop pixel-store
  # parameters. Every constraint it already respects is a constraint a 2013
  # armv7 phone's driver actually has, so pointing this lane at the GL3
  # renderer instead would mean relaxing a ceiling the device still enforces.
  #
  # trspk_webgl1.h includes <GLES2/gl2.h> on any non-emscripten host already,
  # and the NDK sysroot ships it -- so the renderer needs nothing added for the
  # GL calls themselves. What it needed was a CONTEXT, and that was the only
  # thing it ever used SDL for. That seam is now platform/platform_gl_context.h,
  # which BOTH GL renderers program to and which each lane implements once:
  # platform_gl_context_sdl.c on the SDL hosts, platform_android_gl.c over EGL
  # here. There is no SDL on this lane -- no header, no library, no SDL symbol
  # in any object it links -- and the 11k-line GLES2 renderer is still the same
  # file the browser builds.
  PLATFORM_GPU_OBJ_NAMES := webgl1_index16.o
  PLATFORM_EXE_SUFFIX :=

  # -fPIC because the output is a shared library and every object lands in it.
  # _GNU_SOURCE for the same reason the web lane needs it: bionic hides strdup/
  # strtok_r/strcasecmp behind feature macros under -std=c11, and an undeclared
  # strdup compiles to a call returning int -- a truncated pointer on a 64-bit
  # ABI, and a crash nowhere near the call.
  PLATFORM_BASE_CFLAGS := -DTORIRS_PLATFORM_ANDROID=1 -D_GNU_SOURCE -fPIC \
                          $(ANDROID_ARCH_CFLAGS)
  # TORIRS_HAVE_GL3 says a GPU renderer exists at all; TORIRS_GL_ES2 says it is
  # built against the GLES2 ceiling. Both are the web lane's spelling, because
  # it is the web lane's renderer. Like every other host with a GPU path, it is
  # OPT-IN at run time (`--opengl3`); the software rasterizer stays the default,
  # so a device whose driver refuses the context still boots.
  #
  PLATFORM_CFLAGS  := $(PLATFORM_BASE_CFLAGS) \
                      -DTORIRS_HAVE_GL3=1 -DTORIRS_GL_ES2=1
  # -llog is __android_log_print (this lane's stderr -- see platform_android.c),
  # -landroid is ANativeWindow. -shared, and -Wl,--no-undefined so a symbol this
  # library forgot to define fails at link here rather than as an
  # UnsatisfiedLinkError on the device.
  PLATFORM_LDFLAGS := -shared -Wl,--no-undefined -lm -llog -landroid -lGLESv2 -lEGL
  # --gc-sections does real work only with -ffunction-sections/-fdata-sections,
  # which this tree does not compile with. Same reasoning as the linux lane.
  PLATFORM_STRIP_LDFLAGS :=
  PLATFORM_MEMTRACE_WRAP_LDFLAGS := \
      -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
      -Wl,--wrap=reallocf -Wl,--wrap=posix_memalign -Wl,--wrap=strdup
  # The .so name is what System.loadLibrary("torirs") resolves; a suffix would
  # make a traced build unloadable rather than distinguishable.
  PLATFORM_TARGET_MEMTRACE_SUFFIX :=

else
  $(error unknown PLATFORM '$(PLATFORM)' — expected one of: native $(PLATFORM_LIST))
endif

# The windowing implementation of the PlatformSDL2 interface. SDL platforms use
# platform_sdl2.c; both Windows blocks override this with platform_win32gdi.c.
PLATFORM_WINDOW_SRC ?= platform/platform_sdl2.c

# The chrome executor that comes with this window backend, and the define that
# tells the chooser it is here. Empty is a valid answer: a lane with no native
# executor falls back to the in-canvas buffer one, which every build has.
#
# Keyed off the WINDOW backend rather than the platform name because that is
# what actually decides it -- the aux window is SDL_CreateWindow, so every lane
# using platform_sdl2.c gets it and the two Win32 lanes (which will get a GDI
# tool window instead) do not.
ifeq ($(PLATFORM),web)
# The web lane builds on SDL but presents through the DOM: a second SDL window
# in a browser tab is not a thing, and the page can host real form controls,
# which is strictly better than a canvas pretending to be one.
PLATFORM_CHROME_EXEC_SRC ?= ui/torirs_chrome_exec_web.c
PLATFORM_CHROME_EXEC_CFLAGS ?= -DTORIRS_CHROME_EXEC_WEB_AVAILABLE=1
else ifeq ($(PLATFORM_WINDOW_SRC),platform/platform_sdl2.c)
PLATFORM_CHROME_EXEC_SRC ?= ui/torirs_chrome_exec_sdl.c
PLATFORM_CHROME_EXEC_CFLAGS ?= -DTORIRS_CHROME_EXEC_SDL_AVAILABLE=1
else ifeq ($(PLATFORM_WINDOW_SRC),platform/platform_win32gdi.c)
# USER32 only -- see the no-comctl32 note in the executor. Nothing is added to
# PLATFORM_LDFLAGS because the Windows lanes already link user32 and gdi32.
PLATFORM_CHROME_EXEC_SRC ?= ui/torirs_chrome_exec_gdi.c
PLATFORM_CHROME_EXEC_CFLAGS ?= -DTORIRS_CHROME_EXEC_GDI_AVAILABLE=1
else
PLATFORM_CHROME_EXEC_SRC ?=
PLATFORM_CHROME_EXEC_CFLAGS ?=
endif

# What OPT=0 means for this lane's -O level. -O0 everywhere except the web
# lane, which cannot afford it: see the web block above.
PLATFORM_DEBUG_O_LEVEL ?= -O0
