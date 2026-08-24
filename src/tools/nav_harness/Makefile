# nav_harness — visual testing harness for the graph navigation code.
# Usage: make && xvfb-run -a -s "-screen 0 1280x800x24" ./nav_harness --out out

CC = gcc
SRC = ../../..
CFLAGS = -g -O0 -I$(SRC)/src -I$(SRC)/include -DDEBUG_GRAPH_DRAW -Wall
LDFLAGS = -L$(SRC)/lib/linux -lGL -lrt -ldl -lX11 -lraylib -lm

OBJS = nav_harness.o $(SRC)/src/graph_gui.o $(SRC)/src/dstruct.o

all: nav_harness

nav_harness: $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

clean:
	rm -f nav_harness nav_harness.o $(SRC)/src/graph_gui.o $(SRC)/src/dstruct.o

.PHONY: all clean
