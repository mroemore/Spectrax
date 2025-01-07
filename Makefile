CC = gcc

CFLAGS = -Iinclude -lportaudio -lraylib -lm
MINGW_FLAGS =  -Llib/win -lgdi32 -lwinmm
LINUX_FLAGS =  -Llib/linux -lGL -lrt -ldl -lX11

DEBUG_FLAGS = -g
RELEASE_FLAGS = -O2

OUT_DIR = bin

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

SRCS = 	src/main.c \
		src/voice.c \
		src/blit_synth.c \
		src/distortion.c \
		src/modsystem.c \
		src/input.c \
		src/gui.c \
		src/notes.c \
		src/io.c \
		src/settings.c \
		src/appstate.c \
		src/oscillator.c \
		src/sample.c \
		src/sequencer.c 

OBJS = $(SRCS:.c=.o)

TARGET = spectrax

all: CFLAGS += $(DEBUG_FLAGS)
all: $(OUT_DIR)/$(TARGET)

release: CFLAGS += $(RELEASE_FLAGS)
release: $(OUT_DIR)/$(TARGET)

$(OUT_DIR)/$(TARGET): $(OBJS) | $(OUT_DIR)
	$(CC) -o $@ $^ $(CFLAGS)

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

clean:
	rm -f $(OBJS) $(OUT_DIR)/$(TARGET)