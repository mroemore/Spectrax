CC = gcc

CFLAGS = -Iinclude -lportaudio -lraylib -lm
MINGW_FLAGS =  -Llib/win -lgdi32 -lwinmm
LINUX_FLAGS =  -Llib/linux -lGL -lrt -ldl -lX11
ARM_FLAGS = -Iinclude/arm  -lportaudio -l:libraylib.a -g -O1 -lm -lpthread -ldl
ARM_LD_FLAGS = -Llib/arm -L/muos-sdk/aarch64-buildroot-linux-gnu/sysroot/usr/lib -I/muos-sdk/aarch64-buildroot-linux-gnu/sysroot/usr/lib/gl4es/ -lSDL2 -lasound

DEBUG_FLAGS = -g
RELEASE_FLAGS = -O2
ASAN_FLAGS = -fsanitize=address -fno-omit-frame-poiner
VALGRIND_FLAGS = -O0
SRC_DIR = src
OUT_DIR = bin

TARGET = spectrax

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
		$(SRC_DIR)/gui.c \
		$(SRC_DIR)/notes.c \
		$(SRC_DIR)/io.c \
		$(SRC_DIR)/settings.c \
		$(SRC_DIR)/appstate.c \
		$(SRC_DIR)/oscillator.c \
		$(SRC_DIR)/sample.c \
		$(SRC_DIR)/wavetable.c \
		$(SRC_DIR)/filters.c \
		$(SRC_DIR)/sequencer.c 

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
