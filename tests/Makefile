# Compiler
CC = gcc

# Compiler flags
CFLAGS = -I../include -lportaudio -lraylib -lm -DLINUX -L../lib/linux -lGL -lrt -ldl -lX11 -Wall -g

# Source and object files
SRC = testVoice.c ../src/voice.c ../src/filters.c ../src/oscillator.c ../src/io.c ../src/wavetable.c ../src/notes.c ../src/sample.c ../src/modsystem.c ../src/blit_synth.c ../src/oscillator.c ../src/settings.c
OBJS = $(SRC:.c=.o)

# Target executable
TARGET = test_voice

# Default target
all: $(TARGET)

dbg: all
dbg: 
	(gdb -ex "run" $(TARGET))

# Rule to link the final executable
$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS)

# Rule to compile .c files into .o files
%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

# Clean up build artifacts
clean:
	rm -f $(OBJS) $(TARGET)

# Phony targets
.PHONY: all clean