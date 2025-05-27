CC = gcc
PROJECT_ROOT_RELATIVE = ../../..
SRC_DIR = ../..
CFLAGS = -I$(PROJECT_ROOT_RELATIVE)/include -I../.. -lportaudio -lraylib -lm -L$(PROJECT_ROOT_RELATIVE)/lib/linux -lGL -lrt -ldl -lX11 -lkissfft-float


DEBUG_FLAGS = -g -O0
RELEASE_FLAGS = -O2
OUT_DIR = .

TARGET = inspectro_wavget

# Prepend src/ to source files
SRCS = 	main.c \
		$(SRC_DIR)/input.c \
		$(SRC_DIR)/io.c \
		$(SRC_DIR)/sample.c \
		$(SRC_DIR)/fft.c \
		$(SRC_DIR)/dataviz.c

# Generate object files in the src directory
OBJS = $(SRCS:.c=.o)

all: CFLAGS += $(DEBUG_FLAGS)
all: $(OUT_DIR)/$(TARGET)


dbg: clean
dbg: all
dbg:
	(chmod +x $(TARGET) && gdb -ex "run" $(TARGET))

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
