CC = cc
OBJ_DIR = build
TARGET = io_reactor_test

PROJECT_ROOT_FOLDER := $(shell while [ "$$PWD" != "/" ]; do \
	if [ -d "$$PWD/.git" ]; then echo $$PWD; break; fi; \
	cd ..; \
done)
REPO_ROOT := $(abspath $(PROJECT_ROOT_FOLDER))
SRC_DIR   := $(REPO_ROOT)/src
SRC2_DIR  := $(REPO_ROOT)/src2
PLAT_DIR  := $(SRC2_DIR)/platforms

CFLAGS = -Wall -Wextra -g \
	-I$(SRC_DIR) -I$(SRC2_DIR) -I$(REPO_ROOT)

SRCS = main.c \
	$(SRC2_DIR)/ioqueue/libtorirs_ioqueue.c \
	$(PLAT_DIR)/platform_x_io_reactor.c \
	$(PLAT_DIR)/platform_x/cachelib.c \
	$(PLAT_DIR)/platform_x/cachelib_platform.c \
	$(PLAT_DIR)/platform_x/cachelib_serialized.c \
	$(SRC_DIR)/osrs/rscache/rscache_unity.c

THIRD_PARTY_SRCS = \
	$(SRC_DIR)/3rd/bzip/bzip.c \
	$(SRC_DIR)/3rd/miniz/miniz.c

ALL_SRCS = $(SRCS) $(THIRD_PARTY_SRCS)
OBJS = $(addprefix $(OBJ_DIR)/, $(notdir $(ALL_SRCS:.c=.o)))

.PHONY: all clean run

all: $(OBJ_DIR) $(TARGET)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lm

define make-goal
$(OBJ_DIR)/$(notdir $(1:.c=.o)): $(1)
	$(CC) $(CFLAGS) -c $(1) -o $$@
endef

$(foreach src,$(SRCS),$(eval $(call make-goal,$(src))))

THIRD_PARTY_CFLAGS := $(filter-out -Wall -Wextra,$(CFLAGS)) -w

$(OBJ_DIR)/bzip.o: $(SRC_DIR)/3rd/bzip/bzip.c
	$(CC) $(THIRD_PARTY_CFLAGS) -c $< -o $@

$(OBJ_DIR)/miniz.o: $(SRC_DIR)/3rd/miniz/miniz.c
	$(CC) $(THIRD_PARTY_CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(OBJ_DIR) $(TARGET) \
		io_reactor_test_config.txt io_reactor_test_script.lua
