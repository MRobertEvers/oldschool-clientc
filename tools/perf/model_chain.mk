# make -f tools/perf/model_chain.mk [MODEL_CHAIN_TARGET=host]
# No APK, GL context, cache decode or server is linked into this benchmark.
MC_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
MODEL_CHAIN_TARGET ?= android
MC_OUT ?= $(MC_ROOT)/build/model-chain/$(MODEL_CHAIN_TARGET)
MC_NDK ?= $(HOME)/Library/Android/sdk/ndk/27.0.12077973
ifeq ($(MODEL_CHAIN_TARGET),android)
MC_HOST := $(if $(filter Darwin,$(shell uname -s)),darwin-x86_64,linux-x86_64)
MC_CC := $(MC_NDK)/toolchains/llvm/prebuilt/$(MC_HOST)/bin/armv7a-linux-androideabi21-clang
MC_ARCH := -march=armv7-a -mfpu=neon -mfloat-abi=softfp
else ifeq ($(MODEL_CHAIN_TARGET),host)
MC_CC := cc
MC_ARCH :=
else
$(error MODEL_CHAIN_TARGET must be android or host)
endif
MC_CFLAGS := -std=c11 -O3 -DNDEBUG -D_GNU_SOURCE -gline-tables-only -fomit-frame-pointer \
    -flto $(MC_ARCH) -MMD -MP -I$(MC_ROOT) -I$(MC_ROOT)/src -I$(MC_ROOT)/3rd/toridraw
MC_LDFLAGS := -flto -lm
MC_MODEL_CPU_FLAGS ?=
MC_OBJS := $(MC_OUT)/replay.o $(MC_OUT)/stage.o $(MC_OUT)/toridraw.o
.PHONY: model-chain all FORCE
all model-chain: $(MC_OUT)/model_chain_replay
$(MC_OUT)/model_chain_replay: $(MC_OBJS)
	$(MC_CC) $(MC_OBJS) $(MC_LDFLAGS) -o $@
$(MC_OUT)/config: FORCE
	@mkdir -p $(@D)
	@printf '%s\n' '$(MC_CC) $(MC_CFLAGS) $(MC_LDFLAGS) $(MC_MODEL_CPU_FLAGS) $(MC_LIBRARY_CPU_FLAGS)' > $@.new
	@cmp -s $@ $@.new && rm $@.new || mv $@.new $@
$(MC_OUT)/replay.o: $(MC_ROOT)/tools/perf/model_chain_replay.c $(MC_OUT)/config
	$(MC_CC) $(MC_CFLAGS) -c $< -o $@
$(MC_OUT)/stage.o: $(MC_ROOT)/src/platform/platform_renderer_gles2_dualcore_stage.c $(MC_OUT)/config
	$(MC_CC) $(MC_CFLAGS) -c $< -o $@
$(MC_OUT)/toridraw.o: $(MC_ROOT)/3rd/toridraw/toridraw_unity.c $(MC_OUT)/config
	$(MC_CC) $(MC_CFLAGS) $(MC_MODEL_CPU_FLAGS) -c $< -o $@
.PHONY: model-chain-tests
model-chain-tests: $(MC_OUT)/indices_test $(MC_OUT)/face_sort_test
$(MC_OUT)/indices_test: $(MC_ROOT)/tools/perf/model_chain_indices_test.c \
    $(MC_ROOT)/src/platform/platform_renderer_gles2_indices.h $(MC_OUT)/config
	$(MC_CC) $(MC_CFLAGS) $< $(MC_LDFLAGS) -o $@
$(MC_OUT)/face_sort_test: $(MC_ROOT)/3rd/toridraw/toridraw_face_sort_bitonic_radix_test.c $(MC_OUT)/toridraw.o
	$(MC_CC) $(MC_CFLAGS) $^ $(MC_LDFLAGS) -o $@
-include $(MC_OBJS:.o=.d)

# Stage ownership, publication, exhaustion and concurrent-prefix correctness.
model-chain-tests: $(MC_OUT)/stage_test
$(MC_OUT)/stage_test: $(MC_ROOT)/src/platform/test/gles2_dualcore_stage_test.c $(MC_OUT)/stage.o $(MC_OUT)/toridraw.o
	$(MC_CC) $(filter-out -DNDEBUG,$(MC_CFLAGS)) $^ $(MC_LDFLAGS) -pthread -o $@

# Compare whole compiled pipelines in one PMU process. Each .so owns its state.
MC_LIBRARY_CPU_FLAGS ?=
.PHONY: model-chain-library model-chain-compare
model-chain-library: $(MC_OUT)/libmodel_chain.so
MC_LIB_OBJS := $(MC_OUT)/library/replay.o $(MC_OUT)/library/stage.o $(MC_OUT)/library/toridraw.o
$(MC_OUT)/libmodel_chain.so: $(MC_LIB_OBJS)
	$(MC_CC) $^ -shared -Wl,-Bsymbolic -Wl,--no-undefined $(MC_LDFLAGS) -o $@
$(MC_OUT)/library/replay.o: $(MC_ROOT)/tools/perf/model_chain_replay.c $(MC_OUT)/config
	@mkdir -p $(@D)
	$(MC_CC) $(MC_CFLAGS) $(MC_LIBRARY_CPU_FLAGS) -fPIC -DMODEL_CHAIN_LIBRARY=1 -c $< -o $@
$(MC_OUT)/library/stage.o: $(MC_ROOT)/src/platform/platform_renderer_gles2_dualcore_stage.c $(MC_OUT)/config
	@mkdir -p $(@D)
	$(MC_CC) $(MC_CFLAGS) $(MC_LIBRARY_CPU_FLAGS) -fPIC -c $< -o $@
$(MC_OUT)/library/toridraw.o: $(MC_ROOT)/3rd/toridraw/toridraw_unity.c $(MC_OUT)/config
	@mkdir -p $(@D)
	$(MC_CC) $(MC_CFLAGS) $(MC_LIBRARY_CPU_FLAGS) -fPIC -c $< -o $@
-include $(MC_LIB_OBJS:.o=.d)
model-chain-compare: $(MC_OUT)/model_chain_compare
$(MC_OUT)/model_chain_compare: $(MC_ROOT)/tools/perf/model_chain_compare.c $(MC_OUT)/config
	$(MC_CC) $(MC_CFLAGS) $< $(MC_LDFLAGS) -ldl -o $@

.PHONY: placement-chain
placement-chain: $(MC_OUT)/placement_chain_replay
$(MC_OUT)/placement_chain_replay: $(MC_ROOT)/tools/perf/placement_chain_replay.c $(MC_OUT)/toridraw.o $(MC_OUT)/config
	$(MC_CC) $(MC_CFLAGS) -I$(MC_ROOT)/3rd/trspk $< $(MC_OUT)/toridraw.o $(MC_LDFLAGS) -o $@
.PHONY: placement-tests
placement-tests: $(MC_OUT)/placement_chain_test
$(MC_OUT)/placement_chain_test: $(MC_ROOT)/tools/perf/placement_chain_test.c $(MC_OUT)/toridraw.o $(MC_OUT)/config
	$(MC_CC) $(MC_CFLAGS) -I$(MC_ROOT)/3rd/trspk $< $(MC_OUT)/toridraw.o $(MC_LDFLAGS) -o $@
-include $(MC_OUT)/placement_chain_replay.d $(MC_OUT)/placement_chain_test.d

.PHONY: animation-chain
animation-chain: $(MC_OUT)/anim_chain_replay
$(MC_OUT)/anim_chain_replay: $(MC_ROOT)/tools/perf/anim_chain_replay.c $(MC_OUT)/toridraw.o $(MC_OUT)/config
	$(MC_CC) $(MC_CFLAGS) $< $(MC_OUT)/toridraw.o $(MC_LDFLAGS) -o $@
-include $(MC_OUT)/anim_chain_replay.d
.PHONY: animation-tests
animation-tests: $(MC_OUT)/anim_chain_test
$(MC_OUT)/anim_chain_test: $(MC_ROOT)/tools/perf/anim_chain_test.c $(MC_OUT)/toridraw.o $(MC_OUT)/config
	$(MC_CC) $(MC_CFLAGS) $< $(MC_OUT)/toridraw.o $(MC_LDFLAGS) -o $@
-include $(MC_OUT)/anim_chain_test.d

.PHONY: bake-chain
bake-chain: $(MC_OUT)/bake_chain_replay
$(MC_OUT)/bake_chain_replay: $(MC_ROOT)/tools/perf/bake_chain_replay.c $(MC_ROOT)/src/render/trspk_toridraw.c $(MC_OUT)/toridraw.o $(MC_OUT)/config
	$(MC_CC) $(MC_CFLAGS) -I$(MC_ROOT)/3rd/trspk $(MC_ROOT)/tools/perf/bake_chain_replay.c $(MC_ROOT)/src/render/trspk_toridraw.c $(MC_OUT)/toridraw.o $(MC_LDFLAGS) -o $@
.PHONY: bake-tests
bake-tests: $(MC_OUT)/bake_chain_test
$(MC_OUT)/bake_chain_test: $(MC_ROOT)/tools/perf/bake_chain_test.c $(MC_ROOT)/src/render/trspk_toridraw.c $(MC_OUT)/toridraw.o $(MC_OUT)/config
	$(MC_CC) $(MC_CFLAGS) -I$(MC_ROOT)/3rd/trspk $(MC_ROOT)/tools/perf/bake_chain_test.c $(MC_ROOT)/src/render/trspk_toridraw.c $(MC_OUT)/toridraw.o $(MC_LDFLAGS) -o $@
