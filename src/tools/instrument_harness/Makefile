# instrument_harness -- boots straight to the FM instrument screen
# for manual and scripted (Xvfb) UI testing.
#
# This Makefile links the harness against the full app source set
# in ../.. (the src/ tree), EXCEPT main()'s definition in ../../main.c.
# We compile ../../main.c with -DSPECTRAX_HARNESS so the guard around
# its main() (see top of ../../main.c) takes effect, leaving only
# initApplication() and the other helper symbols.
#
# Run from bin/ so that resources/ and data/ (cwd-relative paths
# used by initApplication) resolve correctly. The Makefile sets
# BINDIR for convenience: `make run` does this for you.

CC      = gcc
CFLAGS  = -g -O0 -Wall -Wno-unused-function -DLINUX -DSPECTRAX_HARNESS \
          -I../.. -I../../../include

SRC     = ../..

LDFLAGS = -L../../../lib/linux -lGL -lrt -ldl -lX11 -lkissfft-float \
          -lraylib -lportaudio -lm

APP_OBJS = $(SRC)/voice.o \
           $(SRC)/blit_synth.o \
           $(SRC)/distortion.o \
           $(SRC)/modsystem.o \
           $(SRC)/input.o \
           $(SRC)/graph_gui.o \
           $(SRC)/dstruct.o \
           $(SRC)/gui.o \
           $(SRC)/notes.o \
           $(SRC)/io.o \
           $(SRC)/io/gui_io.o \
           $(SRC)/io/preset_io.o \
           $(SRC)/io/sequencer_io.o \
           $(SRC)/io/settings_io.o \
           $(SRC)/settings.o \
           $(SRC)/appstate.o \
           $(SRC)/oscillator.o \
           $(SRC)/sample.o \
           $(SRC)/fft.o \
           $(SRC)/dataviz.o \
           $(SRC)/wavetable.o \
           $(SRC)/filters.o \
           $(SRC)/sequencer.o \
           $(SRC)/vizfx.o

OBJS    = instrument_harness.o main_harness.o $(APP_OBJS)

BINDIR  = ../../../bin
FIXTURE ?= fixtures/add_route_delete.txt

all: instrument_harness

instrument_harness: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

# main.c compiled with the harness guard so its own main() is
# excluded. Local obj name `main_harness.o` keeps it distinct from
# the app build's src/main.o.
main_harness.o: $(SRC)/main.c $(SRC)/main.h
	$(CC) $(CFLAGS) -c $< -o $@

# Compile app sources from $(SRC) using the same flags the app
# Makefile would use, dropping -DSPECTRAX_HARNESS (only main.c needs
# it; the rest of the app sources are unchanged).
$(SRC)/%.o: $(SRC)/%.c
	$(CC) $(filter-out -DSPECTRAX_HARNESS,$(CFLAGS)) -c $< -o $@

instrument_harness.o: instrument_harness.c $(SRC)/main.h
	$(CC) $(CFLAGS) -c $< -o $@

# Convenience: copy the freshly built binary into bin/ so it can be
# launched with cwd=bin/ and pick up resources/ from the right place.
install: instrument_harness
	cp instrument_harness $(BINDIR)/instrument_harness

# Convenience: launch from bin/ under Xvfb with a short timeout so
# the boot can be smoke-tested without manual interaction.
run: install
	cd $(BINDIR) && timeout 4s xvfb-run -a ./instrument_harness || true

# Scripted (Xvfb) UI run -- drives the harness with the given fixture
# (default: fixtures/add_route_delete.txt) via run_scripted.sh. Exits
# non-zero on any FAIL. The script engine synthesises key events into
# the InputState directly; raylib does not receive keys under Xvfb.
run-scripted: install
	./run_scripted.sh $(FIXTURE)

clean:
	rm -f instrument_harness instrument_harness.o main_harness.o
	@# leave $(SRC)/*.o alone -- they may be needed by the app build

.PHONY: all clean install run run-scripted