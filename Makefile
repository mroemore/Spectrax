CC = gcc

CFLAGS = -Iinclude -lportaudio -lraylib -lm
MINGW_FLAGS =  -Llib/win -lgdi32 -lwinmm
LINUX_FLAGS =  -Llib/linux -lGL -lrt -ldl -lX11 -lkissfft-float
ARM_FLAGS = -Iinclude/arm  -lportaudio -l:libraylib.a -g -O0 -lm -lpthread -ldl
ARM_LD_FLAGS = -Llib/arm -L/muos-sdk/aarch64-buildroot-linux-gnu/sysroot/usr/lib -I/muos-sdk/aarch64-buildroot-linux-gnu/sysroot/usr/lib/gl4es/ -lSDL2 -lasound

DEBUG_FLAGS = -g -O0
RELEASE_FLAGS = -O2
ASAN_FLAGS = -fsanitize=address -fno-omit-frame-poiner
VALGRIND_FLAGS = -O0
SRC_DIR = src
OUT_DIR = bin

TARGET = spectrax

# --- fil-c build (DSP-only, no GUI/audio backends) ---
FILC_CC      = /usr/local/bin/clang
FILC_CFLAGS  = -O2 -g -Iinclude -Isrc -Ithird_party/kissfft
FILC_DSP_DIR = dsp
FILC_KISSFFT_DIR = third_party/kissfft
FILC_TARGET  = spectrax_filc
FILC_TESTS   = test_wav_writer test_notes test_fft test_oscillator test_wavetable test_blit_synth test_distortion test_filters test_modsystem test_voice

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S), Linux)
    CFLAGS += -DLINUX $(LINUX_FLAGS)
else ifeq ($(UNAME_S), Darwin)
    CFLAGS += -DMACOS
else ifneq ($(findstring MINGW,$(UNAME_S)),)
    CFLAGS += -DWINDOWS $(MINGW_FLAGS)
else
    $(error Unsupported platform: $(UNAME_S))
endif

# Prepend src/ to source files
SRCS = 	$(SRC_DIR)/main.c \
		$(SRC_DIR)/voice.c \
		$(SRC_DIR)/blit_synth.c \
		$(SRC_DIR)/distortion.c \
		$(SRC_DIR)/modsystem.c \
		$(SRC_DIR)/input.c \
		$(SRC_DIR)/graph_gui.c \
		$(SRC_DIR)/dstruct.c \
		$(SRC_DIR)/gui.c \
		$(SRC_DIR)/notes.c \
		$(SRC_DIR)/io.c \
		$(SRC_DIR)/settings.c \
		$(SRC_DIR)/appstate.c \
		$(SRC_DIR)/oscillator.c \
		$(SRC_DIR)/sample.c \
		$(SRC_DIR)/fft.c \
		$(SRC_DIR)/dataviz.c \
		$(SRC_DIR)/wavetable.c \
		$(SRC_DIR)/filters.c \
		$(SRC_DIR)/sequencer.c \
		$(SRC_DIR)/io/gui_io.c \
		$(SRC_DIR)/io/preset_io.c \
		$(SRC_DIR)/io/sequencer_io.c \
		$(SRC_DIR)/io/settings_io.c \

# Generate object files in the src directory
OBJS = $(SRCS:.c=.o)

all: CFLAGS += $(DEBUG_FLAGS)
all: $(OUT_DIR)/$(TARGET)

release: CFLAGS += $(RELEASE_FLAGS)
release: $(OUT_DIR)/$(TARGET)

arm: TARGET = spectrax_arm
arm: CC = aarch64-buildroot-linux-gnu-gcc
arm: CFLAGS = $(RELEASE_FLAGS)
arm: CFLAGS += $(ARM_FLAGS)
arm: CFLAGS += $(ARM_LD_FLAGS)
arm: $(OUT_DIR)/$(TARGET)

debug: all
debug:
	(cd bin/ && chmod +x $(TARGET) && gdb -ex "run" $(TARGET))

valc: CFLAGS += $(VALGRIND_FLAGS)
valc: all
valc:
	(cd bin && valgrind --suppressions=../valgrind.supp --gen-suppressions=all --leak-check=full ./$(TARGET))

valg: CFLAGS += $(VALGRIND_FLAGS)
valg: all
valg:
	(cd bin && valgrind ./$(TARGET))

asan: CFLAGS += $(ASAN_FLAGS) + $(DEBUG_FLAGS)
asan:
	(cd bin && ./$(TARGET))

$(OUT_DIR)/$(TARGET): $(OBJS) | $(OUT_DIR)
	$(CC) -o $@ $^ $(CFLAGS)

# Rule to compile .c files into .o files in the src directory
%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

# Ensure the output directory exists
$(OUT_DIR):
	mkdir -p $(OUT_DIR)

# Clean up object files in the src directory and the target binary
clean:
	rm -f $(OBJS) $(OUT_DIR)/$(TARGET)

# --- fil-c targets ---
.PHONY: filc filc-test filc-clean

filc: CFLAGS = $(FILC_CFLAGS)
filc: $(OUT_DIR)/$(FILC_TARGET)

$(OUT_DIR)/$(FILC_TARGET): $(FILC_DSP_DIR)/dsp_main.o $(FILC_DSP_DIR)/wav_writer.o | $(OUT_DIR)
	$(FILC_CC) -o $@ $^ $(FILC_CFLAGS)

$(FILC_DSP_DIR)/%.o: $(FILC_DSP_DIR)/%.c
	$(FILC_CC) -c $< -o $@ $(FILC_CFLAGS)

# kissfft objects — built once, linked into the fft test binary
$(FILC_KISSFFT_DIR)/%.o: $(FILC_KISSFFT_DIR)/%.c
	$(FILC_CC) -c $< -o $@ $(FILC_CFLAGS)

# src/*.o for fil-c — separate dir to avoid clobbering gcc builds
$(FILC_DSP_DIR)/src_%.o: src/%.c
	$(FILC_CC) -c $< -o $@ $(FILC_CFLAGS)

FILC_SRC_OBJS = $(FILC_DSP_DIR)/src_notes.o $(FILC_DSP_DIR)/src_fft.o
FILC_KISSFFT_OBJS = $(FILC_KISSFFT_DIR)/kiss_fft.o $(FILC_KISSFFT_DIR)/kiss_fftr.o

filc-test: $(addprefix $(OUT_DIR)/,$(FILC_TESTS))

$(OUT_DIR)/test_wav_writer: $(FILC_DSP_DIR)/test_wav_writer.o $(FILC_DSP_DIR)/wav_writer.o | $(OUT_DIR)
	$(FILC_CC) -o $@ $^ $(FILC_CFLAGS)

$(OUT_DIR)/test_notes: tests/dsp/test_notes.o $(FILC_DSP_DIR)/src_notes.o | $(OUT_DIR)
	$(FILC_CC) -o $@ $^ $(FILC_CFLAGS)

$(OUT_DIR)/test_fft: tests/dsp/test_fft.o $(FILC_DSP_DIR)/src_fft.o $(FILC_KISSFFT_OBJS) | $(OUT_DIR)
	$(FILC_CC) -o $@ $^ $(FILC_CFLAGS)

# Section 3 tests — DSP primitives. Each test links its own src/<name>.o
# plus any src/ dependencies the header chain pulls in.

$(OUT_DIR)/test_oscillator: tests/dsp/test_oscillator.o $(FILC_DSP_DIR)/src_oscillator.o $(FILC_DSP_DIR)/src_modsystem.o $(FILC_DSP_DIR)/src_wavetable.o $(FILC_DSP_DIR)/src_dstruct.o | $(OUT_DIR)
	$(FILC_CC) -o $@ $^ $(FILC_CFLAGS)

$(OUT_DIR)/test_wavetable: tests/dsp/test_wavetable.o $(FILC_DSP_DIR)/src_wavetable.o | $(OUT_DIR)
	$(FILC_CC) -o $@ $^ $(FILC_CFLAGS)

$(OUT_DIR)/test_blit_synth: tests/dsp/test_blit_synth.o $(FILC_DSP_DIR)/src_blit_synth.o | $(OUT_DIR)
	$(FILC_CC) -o $@ $^ $(FILC_CFLAGS)

$(OUT_DIR)/test_distortion: tests/dsp/test_distortion.o $(FILC_DSP_DIR)/src_distortion.o | $(OUT_DIR)
	$(FILC_CC) -o $@ $^ $(FILC_CFLAGS)

$(OUT_DIR)/test_filters: tests/dsp/test_filters.o $(FILC_DSP_DIR)/src_filters.o | $(OUT_DIR)
	$(FILC_CC) -o $@ $^ $(FILC_CFLAGS)

# Section 4 tests — voice + modulation. Heavy dependency chains.

$(OUT_DIR)/test_modsystem: tests/dsp/test_modsystem.o $(FILC_DSP_DIR)/src_modsystem.o $(FILC_DSP_DIR)/src_wavetable.o $(FILC_DSP_DIR)/src_dstruct.o | $(OUT_DIR)
	$(FILC_CC) -o $@ $^ $(FILC_CFLAGS)

# voice pulls in nearly everything via voice.h
$(OUT_DIR)/test_voice: tests/dsp/test_voice.o \
	$(FILC_DSP_DIR)/src_voice.o \
	$(FILC_DSP_DIR)/src_modsystem.o \
	$(FILC_DSP_DIR)/src_oscillator.o \
	$(FILC_DSP_DIR)/src_wavetable.o \
	$(FILC_DSP_DIR)/src_dstruct.o \
	$(FILC_DSP_DIR)/src_blit_synth.o \
	$(FILC_DSP_DIR)/src_filters.o \
	$(FILC_DSP_DIR)/src_fft.o \
	$(FILC_DSP_DIR)/src_notes.o \
	$(FILC_DSP_DIR)/src_sample.o \
	$(FILC_KISSFFT_OBJS) | $(OUT_DIR)
	$(FILC_CC) -o $@ $^ $(FILC_CFLAGS)

tests/dsp/%.o: tests/dsp/%.c
	$(FILC_CC) -c $< -o $@ $(FILC_CFLAGS)

filc-clean:
	rm -f $(FILC_DSP_DIR)/*.o $(FILC_KISSFFT_DIR)/*.o tests/dsp/*.o $(OUT_DIR)/$(FILC_TARGET) $(addprefix $(OUT_DIR)/,$(FILC_TESTS))
