# The PokeSprite client: its own build, apart from src/makefile.
#
#   make -C pokeclient            # build/pokeclient
#   make -C pokeclient test       # compositor parity with tools/pokesprite
#
# The game's shared code (app, net, world, UI, embedded server: 552 objects) is
# compiled HERE, into build/shared/, by commands gen_shared_mk.py reads out of
# src/makefile with a dry run. src/makefile is only read -- never run for real,
# never edited -- and nothing in it knows this client exists. SHARED_ARGS picks
# the flavour of the shared code (EMBED_SERVER=1: the server runs in-process).

ROOT    := $(abspath ..)
SRC     := $(ROOT)/src
THIRD   := $(ROOT)/3rd
BUILD   ?= build
SHARED_ARGS ?= EMBED_SERVER=1

.PHONY: all test clean
all: $(BUILD)/pokeclient

$(BUILD):
	mkdir -p $(BUILD)

# ---- the shared code -------------------------------------------------------
$(BUILD)/shared.mk: $(SRC)/makefile $(SRC)/platform/platform.mk gen_shared_mk.py | $(BUILD)
	python3 gen_shared_mk.py $@ $(BUILD)/shared $(SHARED_ARGS)

$(BUILD)/shared:
	mkdir -p $@

-include $(BUILD)/shared.mk
# The shared objects' dependency files name sources and headers relative to
# src/ (their commands run there); this lets make find them from here, so a
# shared header edit rebuilds what includes it instead of linking a stale object.
VPATH := $(SRC)

# ---- this client -----------------------------------------------------------
# Compiled with the SHARED code's flags: the same defines give `struct App`
# and every other shared struct the same layout on both sides of each call.
CLIENT_SRCS := pokeclient_main.c ps_render.c ps_world.c ps_fetch.c ps_sheet.c ps_compose.c \
	ps_color.c
CLIENT_OBJS := $(patsubst %.c,$(BUILD)/client/%.o,$(CLIENT_SRCS)) \
	$(BUILD)/client/pokesprite_format.o

$(BUILD)/client:
	mkdir -p $@

$(BUILD)/client/%.o: %.c $(BUILD)/shared.mk | $(BUILD)/client
	$(CC) $(SHARED_CFLAGS) -I. -c $< -o $@

$(BUILD)/client/pokesprite_format.o: $(SRC)/pokesprite/pokesprite_format.c $(BUILD)/shared.mk \
		| $(BUILD)/client
	$(CC) $(SHARED_CFLAGS) -I. -c $< -o $@

$(BUILD)/pokeclient: $(SHARED_OBJS) $(CLIENT_OBJS)
	$(CC) $(SHARED_CFLAGS) -o $@ $(CLIENT_OBJS) $(SHARED_OBJS) $(SHARED_LDFLAGS)
	@echo "built $@"

# ---- compositor parity (no shared code needed) -----------------------------
TEST_CFLAGS := -std=c11 -g -O2 -Wall -Wextra -Wno-unused-parameter -I. -I$(SRC) \
	-I$(THIRD)/miniz -MMD -MP
TEST_SRCS := ps_color.c ps_sheet.c ps_compose.c test/compose_test.c \
	$(SRC)/pokesprite/pokesprite_format.c $(SRC)/engine/png_decode.c $(THIRD)/miniz/miniz.c
TEST_OBJS := $(patsubst %.c,$(BUILD)/test/%.o,$(notdir $(TEST_SRCS)))
vpath %.c . test $(SRC)/pokesprite $(SRC)/engine $(THIRD)/miniz

$(BUILD)/test:
	mkdir -p $@

$(BUILD)/test/%.o: %.c | $(BUILD)/test
	$(CC) $(TEST_CFLAGS) -c $< -o $@

$(BUILD)/compose_test: $(TEST_OBJS)
	$(CC) $(TEST_CFLAGS) -o $@ $^ -lm

STORE ?= $(ROOT)/build/poke/store_test
test: $(BUILD)/compose_test
	python3 test/compose_parity.py $(BUILD)/compose_test $(STORE)

clean:
	rm -rf $(BUILD)

-include $(wildcard $(BUILD)/client/*.d $(BUILD)/test/*.d $(BUILD)/shared/*.d)
