# Polyend Community Edition Synth Plugin SDK (.psyn) Makefile
# Builds standalone relocatable SD-card synth plugins (/Synths/*.psyn)

SDK_DIR       := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT     := $(abspath $(SDK_DIR)/..)

ifneq ($(wildcard $(REPO_ROOT)/agt/10.3/bin/arm-none-eabi-g++),)
  TOOLCHAIN   ?= $(REPO_ROOT)/agt/10.3/bin/arm-none-eabi-
else ifneq ($(wildcard $(REPO_ROOT)/../tracker-ce/agt/10.3/bin/arm-none-eabi-g++),)
  TOOLCHAIN   ?= $(REPO_ROOT)/../tracker-ce/agt/10.3/bin/arm-none-eabi-
else
  TOOLCHAIN   ?= arm-none-eabi-
endif

CXX           := $(TOOLCHAIN)g++
PYTHON        ?= python3
BUILD_DIR     ?= $(SDK_DIR)/build

CXXFLAGS      := -std=c++20 -includecstdint -felide-constructors -Wno-psabi \
                 -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-threadsafe-statics \
                 -fcheck-new -Wall -Wextra -fno-builtin -fno-common \
                 -ffunction-sections -fdata-sections -ffreestanding \
                 -fsingle-precision-constant -ffast-math -O3 -DNDEBUG \
                 -mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16 \
                 -mfp16-format=ieee -mword-relocations \
                 -DPRODUCT=WAVE -DPRODUCT_WAVE \
                 -I$(SDK_DIR)/include \
                 -I$(REPO_ROOT)/synth/include \
                 -I$(REPO_ROOT)/synth/config \
                 -I$(REPO_ROOT)/util/include \
                 -I$(REPO_ROOT)/util/3rdparty \
                 -I$(REPO_ROOT)/thirdparty/boost/include \
                 -I$(REPO_ROOT)/thirdparty/enum_util/include

LDFLAGS       := -nostartfiles --specs=nano.specs \
                 -T$(SDK_DIR)/tools/psyn.ld \
                 -Wl,--gc-sections -Wl,--emit-relocs \
                 -Wl,--wrap=malloc -Wl,--wrap=free -Wl,--wrap=calloc \
                 -Wl,--wrap=_malloc_r -Wl,--wrap=_free_r -Wl,--wrap=_calloc_r

all: $(BUILD_DIR)/CHIP.psyn $(BUILD_DIR)/MRPH.psyn

$(BUILD_DIR)/CHIP.elf: $(SDK_DIR)/engines/chip.cpp $(SDK_DIR)/include/psyn/psyn_plugin.h $(SDK_DIR)/include/psyn/psyn_cpp_bridge.h $(SDK_DIR)/tools/psyn.ld
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< $(LDFLAGS) -o $@

HAS_SAMPLE_SLOTS ?= 0
SUPPORTS_IDLE_SLEEP ?= 1
HAS_VOICE_TICK_IDLE ?= 1

$(BUILD_DIR)/CHIP.psyn: $(BUILD_DIR)/CHIP.elf $(SDK_DIR)/tools/psyn_builder.py
	$(PYTHON) $(SDK_DIR)/tools/psyn_builder.py \
		--elf $< \
		--out $@ \
		--code-name "CHIP" \
		--display-name "Chiptune" \
		--patch-folder "CHIP" \
		--type-id-wave 12 \
		--type-id-play-tracker 8 \
		--default-voices 4 \
		--max-voices 8 \
		--output-channels 1 \
		--renders-aux 0 \
		--has-sample-slots $(HAS_SAMPLE_SLOTS) \
		--supports-idle-sleep $(SUPPORTS_IDLE_SLEEP) \
		--has-voice-tick-idle $(HAS_VOICE_TICK_IDLE)

# ---- MORPHANGUTAN (GibGob OS) ----
# the xPack toolchain needs the exception index symbols defined
PSYN_EXTRA_LDFLAGS ?= -Wl,--defsym=__exidx_start=0 -Wl,--defsym=__exidx_end=0

$(BUILD_DIR)/MRPH.elf: $(SDK_DIR)/engines/morphangutan.cpp $(SDK_DIR)/engines/morphangutan/morph_dsp.h $(SDK_DIR)/include/psyn/psyn_plugin.h $(SDK_DIR)/include/psyn/psyn_cpp_bridge.h $(SDK_DIR)/tools/psyn.ld
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(SDK_DIR)/engines $< $(LDFLAGS) $(PSYN_EXTRA_LDFLAGS) -o $@

$(BUILD_DIR)/MRPH.psyn: $(BUILD_DIR)/MRPH.elf $(SDK_DIR)/tools/psyn_builder.py
	$(PYTHON) $(SDK_DIR)/tools/psyn_builder.py \
		--elf $< \
		--out $@ \
		--code-name "MRPH" \
		--display-name "Morphangutan" \
		--patch-folder "MORPHANGUTAN" \
		--type-id-wave 60 \
		--type-id-play-tracker 60 \
		--default-voices 6 \
		--max-voices 8 \
		--output-channels 2 \
		--renders-aux 0 \
		--has-sample-slots 0 \
		--supports-idle-sleep 1 \
		--has-voice-tick-idle 1

# host tests + WAV previews (runs on the computer, not the Tracker)
HOSTCXX ?= g++
morph-test:
	$(HOSTCXX) -O2 -std=c++20 -Wall $(SDK_DIR)/engines/morphangutan/dsp_test.cpp -o $(BUILD_DIR)/morph_dsp_test
	$(BUILD_DIR)/morph_dsp_test

morph-previews:
	@mkdir -p $(BUILD_DIR)/morph_previews
	$(HOSTCXX) -O2 -std=c++20 -Wall $(SDK_DIR)/engines/morphangutan/preview.cpp -o $(BUILD_DIR)/morph_preview
	$(BUILD_DIR)/morph_preview $(BUILD_DIR)/morph_previews

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean morph-test morph-previews
