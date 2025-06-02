CC = gcc
PROJECT_ROOT_RELATIVE = ../../..
SRC_DIR = ../..
CFLAGS = -I$(PROJECT_ROOT_RELATIVE)/include -I../.. -lportaudio -lraylib -lm -L$(PROJECT_ROOT_RELATIVE)/lib/linux -lGL -lrt -ldl -lX11 -lkissfft-float


DEBUG_FLAGS = -g -O0
RELEASE_FLAGS = -O2
OUT_DIR = .

TARGET = inspectro_wavget

SRCS = 	main.c \
		sa_gui.c \
		sa_audio.c \
		$(SRC_DIR)/input.c \
		$(SRC_DIR)/io.c \
		$(SRC_DIR)/sample.c \
		$(SRC_DIR)/fft.c \
		$(SRC_DIR)/dataviz.c

OBJS = $(SRCS:.c=.o)

all: CFLAGS += $(DEBUG_FLAGS)
all: $(OUT_DIR)/$(TARGET)


dbg: clean
dbg: all
dbg:
	(chmod +x $(TARGET) && gdb -ex "run" $(TARGET))

$(OUT_DIR)/$(TARGET): $(OBJS) | $(OUT_DIR)
	$(CC) -o $@ $^ $(CFLAGS)

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

clean:
	rm -f $(OBJS) $(OUT_DIR)/$(TARGET)
