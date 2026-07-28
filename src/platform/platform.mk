# Platform target selection.
#
# One variable picks the whole host: compiler, windowing/audio/IO backends,
# object directory and link output.
#
#   make -C src                        native, debug     -> src/torirs
#   make -C src release                native, optimized -> src/torirs
#   make -C src PLATFORM=web           web, debug        -> build-web/torirs.js
#   make -C src web                    web, optimized    -> build-web/torirs.js
#
# Objects never mix: each (PLATFORM, OPT) pair owns its own OBJ_DIR, so
# switching targets never links a wasm object into a native binary or reads a
# struct field at an offset another flavor's headers produced.
#
# Adding a platform means adding one block here plus its platform/*.c backends;
# nothing above this file needs to know the list.

PLATFORM ?= native

ifeq ($(PLATFORM),native)
  PLATFORM_CC       := $(if $(filter default,$(origin CC)),cc,$(CC))
  PLATFORM_OBJ_BASE := build
  PLATFORM_TARGET   := torirs
  # Windowing/audio/IO backends for this host. The lists are subtracted from
  # and added to the shared SRCS in the makefile, so a backend swap is local.
  PLATFORM_SRCS     := platform/platform_x_io.c \
                       platform/platform_audio_sdl2.c \
                       platform/platform_sdl2_renderer_gl3.c

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
  PLATFORM_LDFLAGS := -lm -Wl,-dead_strip $(SDL_LIBS)

  UNAME_S := $(shell uname -s)
  ifeq ($(UNAME_S),Darwin)
    PLATFORM_LDFLAGS += -framework OpenGL
  else
    PLATFORM_LDFLAGS += -lGL
  endif

else ifeq ($(PLATFORM),web)
  PLATFORM_CC       := emcc
  PLATFORM_OBJ_BASE := build_web
  PLATFORM_TARGET   := $(REPO_ROOT)/build-web/torirs.js
  # The GPU renderer builds here too, against WebGL1 rather than desktop GL —
  # same file, see TORIRS_GL_ES2 below. IO is asynchronous, audio is WebAudio.
  PLATFORM_SRCS     := platform/platform_x_io_web.c \
                       platform/io_wire.c \
                       platform/platform_audio_wasm.c \
                       platform/platform_sdl2_renderer_gl3.c

  # TORIRS_PLATFORM_WEB is the source-level switch (there is no local disk, so
  # App_Init must not open one). -sUSE_SDL=2 must be a *compile* flag too: it
  # is what puts the SDL2 port's headers on the include path.
  #
  # _GNU_SOURCE: the tree uses strdup/strtok_r/strcasecmp, which are POSIX, not
  # ISO C. Apple libc declares them under -std=c11 anyway; emscripten's musl
  # headers correctly do not, and an undeclared strdup compiles to a call
  # returning int — a truncated pointer, and a crash far from the call.
  PLATFORM_BASE_CFLAGS := -DTORIRS_PLATFORM_WEB=1 -D_GNU_SOURCE
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
                      -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPU8","HEAP32","callMain","FS","FS_readFile"]' \
                      -sEXPORTED_FUNCTIONS='["_main","_malloc","_free","_torirs_io_request_len","_torirs_io_request_ptr","_torirs_io_request_taken","_torirs_io_response_alloc","_torirs_io_response_submit","_torirs_io_fail_pending","_torirs_io_stats"]'

  ifeq ($(OPT),1)
    # -g0 discards the DWARF the shared CFLAGS' -g put in the objects. Without
    # it emcc keeps the debug info and, to keep it valid, runs only a subset of
    # the binaryen optimizations — a release build that is both larger and
    # slower than asked for.
    PLATFORM_LDFLAGS += -O3 -g0
  else
    PLATFORM_LDFLAGS += -sASSERTIONS=1
  endif

else
  $(error unknown PLATFORM '$(PLATFORM)' — expected one of: native web)
endif
