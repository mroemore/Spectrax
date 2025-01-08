#ifndef NOTES_H
#define NOTES_H

#include <string.h>
#include <stdio.h>


typedef enum {
	OFF = -1,
    C,
    Db,
    D,
    Eb,
    E,
    F,
    Gb,
    G,
    Ab,
    A,
    Bb,
    B,
    NOTE_COUNT
} Note;



extern const char* noteNames[NOTE_COUNT];

#define MAX_OCTAVES 9

// 2D array to store note frequencies
extern const float noteFrequencies[NOTE_COUNT][MAX_OCTAVES];

char* getNoteString(int note, int octave);

#endif