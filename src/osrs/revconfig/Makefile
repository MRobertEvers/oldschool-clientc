CC = gcc
OBJ_DIR = build
TARGET = a.out

# 1. Find the root directory
PROJECT_ROOT_FOLDER := $(shell while [ "$$PWD" != "/" ]; do \
                    if [ -d "$$PWD/.git" ]; then echo $$PWD; break; fi; \
                    cd ..; \
                done)
PROJECT_ROOT = $(abspath $(PROJECT_ROOT_FOLDER))/src

# 2. Setup Flags
CFLAGS = -Wall -Wextra -g -I$(PROJECT_ROOT) -I.

# 3. List Sources with absolute/relative paths
SRCS = static_ui_load_test.c \
       static_ui_load.c \
       uiscene.c \
	   $(PROJECT_ROOT)/bmp.c \
	   $(PROJECT_ROOT)/graphics/dash.c \
       $(PROJECT_ROOT)/graphics/dashmap.c \
       $(PROJECT_ROOT)/graphics/lighting.c \
       $(PROJECT_ROOT)/graphics/shared_tables.c \
       $(PROJECT_ROOT)/3rd/ini/ini.c \
       $(PROJECT_ROOT)/3rd/bzip/bzip.c \
       $(PROJECT_ROOT)/3rd/miniz/miniz.c \
       $(PROJECT_ROOT)/3rd/tommath/mpi.c \
       $(PROJECT_ROOT)/osrs/buildcachedat.c \
       $(PROJECT_ROOT)/osrs/palette.c \
       $(PROJECT_ROOT)/osrs/dash_utils.c \
       $(PROJECT_ROOT)/osrs/rscache/disk.c \
       $(PROJECT_ROOT)/osrs/rscache/cache.c \
       $(PROJECT_ROOT)/osrs/rscache/archive.c \
       $(PROJECT_ROOT)/osrs/rscache/archive_decompress.c \
       $(PROJECT_ROOT)/osrs/rscache/compression.c \
       $(PROJECT_ROOT)/osrs/rscache/xtea.c \
       $(PROJECT_ROOT)/osrs/rscache/xtea_config.c \
       $(PROJECT_ROOT)/osrs/rscache/reference_table.c \
       $(PROJECT_ROOT)/osrs/rscache/rsbuf.c \
       $(PROJECT_ROOT)/osrs/rscache/filelist.c \
       $(PROJECT_ROOT)/osrs/rscache/cache_dat.c \
	   $(PROJECT_ROOT)/osrs/rscache/tables/model.c \
	   $(PROJECT_ROOT)/osrs/rscache/tables/string_utils.c \
       $(PROJECT_ROOT)/osrs/rscache/tables_dat/pix8.c \
       $(PROJECT_ROOT)/osrs/rscache/tables_dat/pix32.c \
	   $(PROJECT_ROOT)/osrs/rscache/tables_dat/config_versionlist_mapsquare.c \
       revconfig.c \
       revconfig_load.c

# 4. Create the Object list (Flat in build/)
OBJS = $(addprefix $(OBJ_DIR)/, $(notdir $(SRCS:.c=.o)))

all: $(OBJ_DIR) $(TARGET)
all: $(OBJ_DIR) $(TARGETS)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# 5. Link step
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)
# 5. Link steps
COMMON_OBJS = $(OBJ_DIR)/ini.o $(OBJ_DIR)/revconfig.o $(OBJ_DIR)/revconfig_load.o

a.out: $(OBJ_DIR)/static_ui_load_test.o $(OBJ_DIR)/static_ui_load.o $(OBJ_DIR)/uiscene.o $(OBJ_DIR)/cache_dat.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# 6. THE FIX: Explicitly define how to build each object
# This loop creates a specific rule for every file in SRCS
define make-goal
$(OBJ_DIR)/$(notdir $(1:.c=.o)): $(1)
	$(CC) $(CFLAGS) -c $(1) -o $$@
endef

$(foreach src,$(SRCS),$(eval $(call make-goal,$(src))))

clean:
	rm -rf $(OBJ_DIR) $(TARGET)
	rm -rf $(OBJ_DIR) $(TARGETS)

.PHONY: all clean