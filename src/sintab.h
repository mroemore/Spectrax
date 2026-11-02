#ifndef SPECTRAX_SINTAB_H
#define SPECTRAX_SINTAB_H

/* Shared sine lookup table.
 *
 * Rationale (measured on the RG35XX SP's Cortex-A53): sinf() costs ~44.6 ns
 * per call there against ~3.5 ns on a modern x86 core, and sine_op() calls it
 * once per FM operator per sample -- 37% of total FM voice cost. A table with
 * linear interpolation measures ~21.3 ns on the same core (2.1x faster) and
 * its accuracy is set by the table size, not by the interpolation:
 *
 *   1024 entries + lerp : -106 dBFS peak error  (transparent)
 *    256 entries + lerp :  -83 dBFS             (below 16-bit audio noise)
 *
 * Table size was measured to make no speed difference from 256 to 16384
 * entries, because the cost is the phase scale, int convert and interpolate,
 * not the memory access. 1024 entries (4 KB) is therefore used: it stays in
 * L1, it is transparent, and it keeps room for a larger table later if a
 * harsher antialiasing requirement appears.
 *
 * Interpolation is not optional at this size: the raw lookup's error is
 * periodic in the table index, so 1024 raw entries give -44 dBFS -- audible.
 */

#define SIN_TAB_BITS 10
#define SIN_TAB_SIZE (1 << SIN_TAB_BITS)
#define SIN_TAB_MASK (SIN_TAB_SIZE - 1)

/* Initialise the table. Safe to call more than once; the first call wins so
 * that a test harness and the app can both ask for it. */
void sinTabInit(void);

/* sin(2*pi*phase) for any phase. The argument is reduced with floorf-like
 * wrap rather than fmodf: fmodf is a library call costing ~10 ns on the A53,
 * comparable to a whole table lookup, and the wrap is exact because the phase
 * is always finite and small here. */
float sinTab(float phase);

#endif
