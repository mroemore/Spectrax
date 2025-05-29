#ifndef SEQUENCERIO_H
#define SEQUENCERIO_H

#include "../io.h"
#include "../sequencer.h"

SequencerFileResult saveSequencerState(const char *filename, Arranger *arranger, PatternList *patterns);
SequencerFileResult loadSequencerState(const char *filename, Arranger *arranger, PatternList *patterns);

#endif
