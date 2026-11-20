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

all: $(BUILD_DIR)/CHIP.psyn $(BUILD_DIR)/MRPH.psyn $(BUILD_DIR)/LILI.psyn $(BUILD_DIR)/GORILLA.psyn

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

# ---- LILITRON (GibGob OS): the voice is /SynthSamples/LILI/LILITRON.wav ----
$(BUILD_DIR)/LILI.elf: $(SDK_DIR)/engines/lilitron.cpp $(SDK_DIR)/engines/lilitron/lilitron_table.h $(SDK_DIR)/include/psyn/psyn_plugin.h $(SDK_DIR)/include/psyn/psyn_cpp_bridge.h $(SDK_DIR)/tools/psyn.ld
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(SDK_DIR)/engines $< $(LDFLAGS) $(PSYN_EXTRA_LDFLAGS) -o $@

$(BUILD_DIR)/LILI.psyn: $(BUILD_DIR)/LILI.elf $(SDK_DIR)/tools/psyn_builder.py
	$(PYTHON) $(SDK_DIR)/tools/psyn_builder.py \
		--elf $< \
		--out $@ \
		--code-name "LILI" \
		--display-name "Lilitron" \
		--patch-folder "LILITRON" \
		--type-id-wave 61 \
		--type-id-play-tracker 61 \
		--default-voices 4 \
		--max-voices 4 \
		--output-channels 1 \
		--renders-aux 0 \
		--has-sample-slots 1 \
		--supports-idle-sleep 1 \
		--has-voice-tick-idle 0

# ---- GORILLAGRAIN (GibGob OS): granular; samples in /SynthSamples/GORILLA/ ----
$(BUILD_DIR)/GORILLA.elf: $(SDK_DIR)/engines/gorillagrain.cpp $(SDK_DIR)/engines/gorillagrain/grain_dsp.h $(SDK_DIR)/engines/morphangutan/morph_dsp.h $(SDK_DIR)/include/psyn/psyn_plugin.h $(SDK_DIR)/include/psyn/psyn_cpp_bridge.h $(SDK_DIR)/tools/psyn.ld
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(SDK_DIR)/engines $< $(LDFLAGS) $(PSYN_EXTRA_LDFLAGS) -o $@

$(BUILD_DIR)/GORILLA.psyn: $(BUILD_DIR)/GORILLA.elf $(SDK_DIR)/tools/psyn_builder.py
	$(PYTHON) $(SDK_DIR)/tools/psyn_builder.py \
		--elf $< \
		--out $@ \
		--code-name "GORILLA" \
		--display-name "GORILLAGRAIN" \
		--patch-folder "GORILLAGRAIN" \
		--type-id-wave 62 \
		--type-id-play-tracker 62 \
		--default-voices 4 \
		--max-voices 4 \
		--output-channels 2 \
		--renders-aux 0 \
		--has-sample-slots 1 \
		--supports-idle-sleep 1 \
		--has-voice-tick-idle 1

# a diagnosis build (beeps where the sound stops): build/diag/GORILLA.psyn
$(BUILD_DIR)/diag/GORILLA.elf: $(SDK_DIR)/engines/gorillagrain.cpp $(SDK_DIR)/engines/gorillagrain/grain_dsp.h
	@mkdir -p $(BUILD_DIR)/diag
	$(CXX) $(CXXFLAGS) -DGORILLA_DIAG -I$(SDK_DIR)/engines $< $(LDFLAGS) $(PSYN_EXTRA_LDFLAGS) -o $@

$(BUILD_DIR)/diag/GORILLA.psyn: $(BUILD_DIR)/diag/GORILLA.elf $(SDK_DIR)/tools/psyn_builder.py
	$(PYTHON) $(SDK_DIR)/tools/psyn_builder.py --elf $< --out $@ \
		--code-name "GORILLA" --display-name "GORILLAGRAIN" --patch-folder "GORILLAGRAIN" \
		--type-id-wave 62 --type-id-play-tracker 62 --default-voices 4 --max-voices 4 \
		--output-channels 2 --renders-aux 0 --has-sample-slots 1 --supports-idle-sleep 1 --has-voice-tick-idle 1

# the whole plugin (glue + DSP) through its C API, as the Tracker's loader drives it
GRAIN_HOST_INC := -I$(SDK_DIR)/include -I$(REPO_ROOT)/synth/include -I$(REPO_ROOT)/synth/config \
                  -I$(REPO_ROOT)/util/include -I$(REPO_ROOT)/util/3rdparty \
                  -I$(REPO_ROOT)/thirdparty/boost/include -I$(REPO_ROOT)/thirdparty/enum_util/include -I$(SDK_DIR)/engines
grain-plugin-test:
	$(HOSTCXX) -std=c++20 -includecstdint -O3 -ffast-math -fno-exceptions -DPRODUCT=WAVE -DPRODUCT_WAVE \
		-D__dso_handle=psyn_test_dso_handle -D__cxa_atexit=psyn_test_cxa_atexit $(GRAIN_HOST_INC) \
		$(SDK_DIR)/engines/gorillagrain/plugin_harness.cpp -o $(BUILD_DIR)/grain_plugin_harness
	GARBAGE=1 $(BUILD_DIR)/grain_plugin_harness $(SDK_DIR)/engines/gorillagrain/GORILLA.wav

grain-test:
	$(HOSTCXX) -O2 -std=c++20 -Wall -I$(SDK_DIR)/engines $(SDK_DIR)/engines/gorillagrain/grain_test.cpp -o $(BUILD_DIR)/grain_test
	$(BUILD_DIR)/grain_test

# ---- hardware test builds of MORPHANGUTAN (to find what the Tracker dislikes) ----
# TA: mono, no FX   TB: stereo, no FX   TC: sine test tone, mono, no FX   TD: full, automatic ID
MORPH_TEST_FLAGS_TA := -DMORPH_MONO -DMORPH_NO_FX
MORPH_TEST_FLAGS_TB := -DMORPH_NO_FX
MORPH_TEST_FLAGS_TC := -DMORPH_MONO -DMORPH_NO_FX -DMORPH_TEST_SINE
MORPH_TEST_FLAGS_TD :=
# TE: the real engine, stereo, no FX, beeps a code when it stays silent (see MORPH_DIAG)
MORPH_TEST_FLAGS_TE := -DMORPH_NO_FX -DMORPH_DIAG
MORPH_TEST_CH_TA := 1
MORPH_TEST_CH_TB := 2
MORPH_TEST_CH_TC := 1
MORPH_TEST_CH_TD := 2
MORPH_TEST_CH_TE := 2

$(BUILD_DIR)/MR%.elf: $(SDK_DIR)/engines/morphangutan.cpp $(SDK_DIR)/engines/morphangutan/morph_dsp.h
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(MORPH_TEST_FLAGS_$*) -I$(SDK_DIR)/engines $< $(LDFLAGS) $(PSYN_EXTRA_LDFLAGS) -o $@

$(BUILD_DIR)/MR%.psyn: $(BUILD_DIR)/MR%.elf $(SDK_DIR)/tools/psyn_builder.py
	$(PYTHON) $(SDK_DIR)/tools/psyn_builder.py --elf $< --out $@ \
		--code-name "MR$*" --display-name "Morph test $*" --patch-folder "MR$*" \
		--type-id-wave 0 --type-id-play-tracker 0 --default-voices 4 --max-voices 8 \
		--output-channels $(MORPH_TEST_CH_$*) --renders-aux 0 --has-sample-slots 0 \
		--supports-idle-sleep 1 --has-voice-tick-idle 1

morph-hwtest: $(BUILD_DIR)/MRTA.psyn $(BUILD_DIR)/MRTB.psyn $(BUILD_DIR)/MRTC.psyn $(BUILD_DIR)/MRTD.psyn $(BUILD_DIR)/MRTE.psyn

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

.PHONY: all clean morph-test morph-previews morph-hwtest grain-test grain-plugin-test
