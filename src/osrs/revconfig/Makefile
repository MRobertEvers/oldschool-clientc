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
SRCS = revconfig_test.c \
       $(PROJECT_ROOT)/3rd/ini/ini.c \
       revconfig.c \
       revconfig_load.c

# 4. Create the Object list (Flat in build/)
OBJS = $(addprefix $(OBJ_DIR)/, $(notdir $(SRCS:.c=.o)))

all: $(OBJ_DIR) $(TARGET)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# 5. Link step
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

# 6. THE FIX: Explicitly define how to build each object
# This loop creates a specific rule for every file in SRCS
define make-goal
$(OBJ_DIR)/$(notdir $(1:.c=.o)): $(1)
	$(CC) $(CFLAGS) -c $(1) -o $$@
endef

$(foreach src,$(SRCS),$(eval $(call make-goal,$(src))))

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: all clean