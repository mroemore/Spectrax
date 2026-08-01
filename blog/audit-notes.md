# C-to-Zig Audit Notes — Spectrax DAW (Wave 1)

**Scope:** `src/dstruct.{c,h}`, `src/io.{c,h}`, `src/voice.{c,h}`
**Date:** 2026-05-28
**Purpose:** Map 20 planned Zig contrast points to actual C code, classify defects, note interdependencies.

---

## A. MAPPING TABLE

| # | C File + Line Range | Function / Construct | 1-Sentence Summary of C Pattern |
|---|---------------------|---------------------|--------------------------------|
| 1 | `dstruct.c:3-29` | `createList` | Allocates a `List` struct and its element pool via two separate `malloc` calls, with manual NULL checks and cleanup of the first allocation if the second fails. |
| 2 | `io.c:128-222` | `load_wav_sample` | Opens a WAV file, reads a header struct, validates magic numbers, then branches on `bitsPerSample` (8 vs 16) to convert PCM to float; every error path manually calls `fclose` (and sometimes `free`) before returning. |
| 3 | `io.c:129, 143, 173-176` | (within `load_wav_sample`) | Cleanup is duplicated across error paths: `fclose(file)` appears at lines 138, 144, 150, 175, 198, 217; `free(pcm_data)` and `free(data)` appear in the 8-bit and 16-bit branches — the classic C "cleanup cascade" that Zig's `errdefer` collapses. |
| 4 | `oscillator.h:8-12` (OUT OF SCOPE) | (header-level) | **Out of hard scope.** The planned contrast point targets `comptime` usage in oscillator definitions. Best in-scope substitute: `dstruct.h:8` (`#define MAX_NODE_CHILDREN 32`) and `voice.h:14-18` (compile-time constants via `#define`) — these are preprocessor constants that Zig would express as `comptime` `const` values with type safety. |
| 5 | `voice.c:50-75` | `freeVoice` | Uses a `switch(v->type)` on a `VoiceType` enum to dispatch type-specific cleanup (FM operators, sample, or nop); the C union `v->vd` has no runtime tag — the programmer must keep `v->type` and the active union member in sync manually. |
| 6 | `voice.h:189-215` | `struct Voice` | Defines a `Voice` struct containing a `VoiceType type` field alongside an anonymous union `vd` with five variant structs (`fm`, `blep`, `spectral`, `sampler`, `granular`) — the classic C "manual tagged union" pattern where the tag and payload are separate fields. |
| 7 | `voice.c:255-294` | `initialize_voice` (switch block) | A `switch(voice->type)` initializes the correct union member (`vd.fm`, `vd.sampler`, etc.) and assigns a function pointer (`voice->generate`); the `VOICE_TYPE_SPECTRAL` case at line 291 falls through to `default` because it lacks a `break` — a latent bug. |
| 8 | `dstruct.c:4-8` | `createList` (NULL check) | After `malloc`, the code checks `if(!l)` and returns `NULL` — the C idiom for "optional" that Zig replaces with `?T` and the `orelse` operator; note that every caller must also check for NULL (e.g., `appendToList` at line 61). |
| 9 | Every `.h` file (dstruct.h, io.h, voice.h) | Forward declarations + include guards | All three headers use `#ifndef`/`#define` include guards, `typedef struct Foo Foo;` forward declarations, and expose function prototypes — Zig eliminates header files entirely by using `@import` and putting declarations alongside definitions. |
| 10 | `voice.c:259` | `voice->generate = generateBlep;` | A function pointer is assigned directly inside a switch case; there is no hidden control flow — what you see is what executes. In Zig this maps to a tagged-union method dispatch where the compiler guarantees exhaustiveness. |
| 11 | `sample.h:37-42` (OUT OF SCOPE) | (header-level) | **Out of hard scope.** The planned contrast point targets Zig slices (`[]T`) vs C pointer+length pairs. Best in-scope substitute: `dstruct.c:72` (`memcpy(element->data, data, dataSize)`) — the C pattern of passing a `void*` pointer alongside a `size_t dataSize` is exactly what Zig slices unify into a single `[]const u8` value. |
| 12 | `tests/testVoice.c` (OUT OF SCOPE) | (test file) | **Out of hard scope.** The planned contrast point targets Zig's built-in `test` blocks vs C test frameworks. No in-scope substitute exists; the blog should reference this file briefly as an example of how C requires an external test harness (e.g., Unity, CUnit) while Zig has `std.testing` built in. |
| 13 | `voice.c:161-163` | `initVoicePool` (bounds check) | A bounds check `if(channelIndex >= MAX_SEQUENCER_CHANNELS || channelIndex < 0)` prints an error message but does **not** return or abort — execution falls through to line 166 (`vm->voiceCount[channelIndex] = 0`), which performs an out-of-bounds array write. This is a real defect: the "safety check" is cosmetic. In Zig, `vm.voiceCount[channelIndex]` would trigger a panic in safe modes. |
| 14 | `voice.h:4-12` | `#include` chain | `voice.h` includes 9 headers: `kiss_fft.h`, `settings.h`, `oscillator.h`, `sample.h`, `notes.h`, `modsystem.h`, `blit_synth.h`, `filters.h`, `fft.h` — a deep include chain that creates compilation coupling. In Zig, each `@import("file.zig")` is explicit and the compiler resolves dependencies without textual inclusion. |
| 15 | `filters.c:13-26` (OUT OF SCOPE) | (function-level) | **Out of hard scope.** The planned contrast point targets Zig's `switch` as an expression (returns a value) vs C's `switch` as a statement. Best in-scope substitute: `voice.c:97-112` (`generateBlep`'s switch on `shape`) — the C switch assigns to `out.L` in each case, which in Zig could be written as `out.L = switch (shape) { .Ramp => ..., .Square => ..., .Sine => ... };`. |
| 16 | `oscillator.c:65-77` (OUT OF SCOPE) | (function-level) | **Out of hard scope.** The planned contrast point targets Zig struct methods (`fn method(self: *Self)`) vs C's free functions taking a struct pointer. Best in-scope substitute: `voice.c:50-75` (`freeVoice(Voice *v)`) — a free function that operates on a struct pointer, which in Zig would be `fn free(self: *Voice) void` attached to the `Voice` struct. |
| 17 | `voice.c:155-158` | `generateVoice` (end) | **Line reference discrepancy.** The upstream plan cites "for over slices" at lines 155-158, but those lines are `out.R *= pan; out.L *= 1.0 - pan; return out;` — no loop exists here. The nearest relevant pattern is `voice.c:168-177` (`for(int i = 0; i < voiceCount; i++)` in `initVoicePool`) iterating over a voice pool array, or `voice.c:556-580` (`granular_process`'s `for(int i = 0; i < GRAIN_COUNT; i++)`) iterating over grains. In Zig, both would be `for (pool, 0..) |voice, i|` or `for (0..GRAIN_COUNT) |i|`. |
| 18 | `io.c:32-114` | `populateDirectoryList` | A `#ifdef _WIN32` / `#else` block implements directory traversal using Win32 API (`FindFirstFile`/`FindNextFile`) on Windows and POSIX (`opendir`/`readdir`) on Unix — Zig replaces this with `std.fs` cross-platform abstractions that compile to the correct syscalls per target OS. |
| 19 | `filters.c:44-59` (OUT OF SCOPE) | (function-level) | **Out of hard scope.** The planned contrast point targets Zig's `@Vector` SIMD types vs C's platform-specific SIMD intrinsics. Best in-scope substitute: `voice.c:178-184` (the 8-bit PCM-to-float conversion loop) — a loop that processes interleaved audio samples element-by-element, which in Zig could be vectorized with `@Vector` types for batch processing. |
| 20 | `Makefile` (OUT OF SCOPE) | (build file) | **Out of hard scope.** The planned contrast point targets Zig's built-in `build.zig` vs C's Makefile/CMake. No in-scope substitute exists; the blog should reference the Makefile briefly to show how Zig consolidates compilation, linking, and test execution into a single build system. |

---

## B. DEFECT INVENTORY

Five pre-identified items classified as "teaching-moment" (worth showing in the blog as a C pitfall that Zig avoids) or "exclude" (not suitable for the blog's audience or scope).

### Item 1: `dstruct.c:174-175` — Double-assign `n->data`
```c
n->data = malloc(dataSize);   // line 174: allocates memory
n->data = data;               // line 175: immediately overwrites the pointer
```
**Classification: teaching-moment**
The `malloc`'d buffer is leaked — its pointer is overwritten by the caller-supplied `data` pointer without freeing the allocation. In Zig, the `defer`/`errdefer` model makes it harder to accidentally leak a freshly allocated buffer, and the compiler would flag the unused result of `malloc` if assigned to a variable that is immediately reassigned. This is a clean example of C's "manual memory discipline" failing silently.

### Item 2: `dstruct.c:179-181` — `buildGraph()` incomplete stub
```c
Node* buildGraph(List* l){
    Node* current;
    // (empty body, no return)
}
```
**Classification: teaching-moment**
The function declares an uninitialized local pointer and returns nothing (implicit UB — the return value is indeterminate). In Zig, this would be a compile error: the function's return type `*Node` requires a value on every code path, and `var current: ?*Node = undefined;` would require explicit initialization. This demonstrates Zig's guarantee that all code paths return a value.

### Item 3: `io.c:167` — Integer division for sample length
```c
int length = header.subchunk2Size / (header.bitsPerSample / 4);
```
**Classification: teaching-moment**
For `bitsPerSample == 16`, the inner expression `16 / 4 = 4` (correct for 16-bit: 4 bytes per stereo sample). For `bitsPerSample == 8`, `8 / 4 = 2` (correct for 8-bit: 2 bytes per stereo sample). However, this only works because both 8 and 16 are divisible by 4. For any other bit depth (e.g., 24), `24 / 4 = 6` but the correct bytes-per-sample would be `24 / 8 * numChannels`. The formula is coincidentally correct for the two supported depths but semantically wrong. In Zig, using typed constants and explicit byte calculations (`bitsPerSample / 8 * numChannels`) would make the intent clear, and the compiler would catch type mismatches.

### Item 4: `io.c:191, 214` — Commented-out `free(data)`
```c
// free(data);   // line 191 (8-bit branch)
// free(data);   // line 214 (16-bit branch)
```
**Classification: teaching-moment**
The `data` buffer (allocated at line 168) is passed to `loadSample()` at lines 189 and 212 but never freed afterward — the `free(data)` calls are commented out. Whether this is intentional (ownership transfer to `loadSample`) or a leak depends on `loadSample`'s implementation (out of scope). Either way, this is a textbook example of C's ambiguous ownership semantics. In Zig, the type system would make ownership explicit: `loadSample` would either take ownership (`[]f32` by value) or borrow (`[]const f32`), and the caller would know whether to free.

### Item 5: `io.c:32-114` — `#ifdef _WIN32` platform block
```c
#ifdef _WIN32
    // Win32 API directory traversal (~40 lines)
#else
    // POSIX directory traversal (~40 lines)
#endif
```
**Classification: exclude**
This is not a defect — it's a legitimate platform conditional. The blog should reference it in the context of Zig's `std.fs` abstraction (difference #18), but it should not be classified as a "bug" or "teaching moment" about C's flaws. The C code is doing the right thing for a cross-platform codebase; the contrast is that Zig provides this abstraction in the standard library rather than requiring the developer to write both branches.

---

## C. INTERDEPENDENCY NOTES

### voice.c external dependencies

`voice.c` includes the following headers beyond the three scoped modules:

| Header | Used for | Self-contained excerpt feasible? |
|--------|----------|----------------------------------|
| `fft.h` | `Fft` struct, `initFFT`, `pushFrameToFFT`, `processFFTData` (used in spectral voice init, lines 441-452) | No — the spectral initialization code references `Fft`, `kiss_fftr_cfg`, and FFT processing functions. A self-contained excerpt would need to stub or summarize these. |
| `kiss_fft.h` / `kiss_fftr.h` | FFT configuration types and functions (lines 455-458) | No — third-party FFT library. Can be summarized as "external FFT library calls." |
| `notes.h` | `OFF` constant (line 228), `NOTE_INFO_SIZE` (line 213) | Partially — `OFF` and `NOTE_INFO_SIZE` are likely simple constants that can be inlined in an excerpt. |
| `blit_synth.h` | `blep_saw`, `blep_square`, `noblep_saw`, `noblep_sine` (lines 99-108), `BLEP_RAMP`, `BLEP_SQUARE`, `BLEP_SINE` enums | Partially — the function signatures can be stubbed; the BLIT synthesis algorithm is not essential to the tagged-union contrast. |
| `modsystem.h` | `ParamList`, `ModList`, `Parameter`, `Envelope`, `LFO`, `Operator`, `createParamList`, `addModulation`, `freeModList`, `freeParamList`, `freeEnvelope`, `freeLFO`, `freeOperator`, `createParameter`, `createParameterEx`, `createParameterPro`, `createAD`, `createParamPointerAD`, `createParamPointerOperator`, `ParameterValue`, `MO_MUL`, `MO_ADD`, `MT_ENV`, `MT_LFO`, `MT_RND` | **Heavy dependency.** Nearly every function in `voice.c` interacts with the modulation/parameter system. For the tagged-union contrast (differences #5, #6, #7), the modsystem calls are noise — excerpts should focus on the `switch(v->type)` and `vd.*` union access, treating modsystem calls as opaque. |
| `sample.h` | `Sample`, `SamplePool`, `loadSample`, `freeSample`, `getSampleValueFwd`, `getSampleValueRev`, `SamplePlaybackType`, `SPT_FORWARD`, etc. | **Heavy dependency.** The sampler voice type (`VOICE_TYPE_SAMPLE`) is the most used variant in the codebase. Excerpts about tagged unions will need at least a forward declaration of `Sample` and `SamplePool`. |

### io.c external dependencies

| Header | Used for | Self-contained excerpt feasible? |
|--------|----------|----------------------------------|
| `gui.h` | `ColourScheme`, `Color` types (in function signatures in io.h) | No — but these are only in function declarations, not in `load_wav_sample`. The error-union contrast (difference #2) focuses on `load_wav_sample` which does not use gui types. |
| `settings.h` | `Settings` struct (used by `createVoiceManager` in voice.c, not io.c) | Not directly relevant to io.c's scoped content. |
| `sequencer.h` | `Arranger`, `PatternList` (in function signatures in io.h) | Not directly relevant to io.c's scoped content. |
| `sample.h` | `SamplePool`, `loadSample` (used in `load_wav_sample` at line 189/212, and `loadSamplesfromDirectory` at line 122) | **Moderate dependency.** The `loadSample` call is the success path of `load_wav_sample`. For the error-union contrast, the excerpt can stop at the `loadSample` call and treat it as an opaque success sink. |

### dstruct.c external dependencies

`dstruct.c` and `dstruct.h` are **self-contained** — they only include `<stdio.h>`, `<stdlib.h>`, and `<string.h>`. The `List` and `Node` types, along with all functions, are fully defined within these two files. This makes dstruct the cleanest source for allocator and optional-type contrasts (differences #1, #8).

### Feasibility summary for self-contained excerpts

| Contrast | Feasibility | Notes |
|----------|-------------|-------|
| #1 Allocators (`dstruct.c:3-29`) | **High** — fully self-contained |
| #2 Error unions (`io.c:128-222`) | **Medium** — needs `SamplePool` forward-decl, but `loadSample` call is opaque |
| #3 Defer/errdefer (`io.c` lines) | **Medium** — same as #2 |
| #8 Optional types (`dstruct.c:4-8`) | **High** — fully self-contained |
| #5 Tagged union dispatch (`voice.c:50-75`) | **Medium** — needs modsystem/sample stubs but they are noise to the contrast |
| #6 Tagged union fields (`voice.h:189-215`) | **Medium** — needs `ParamList`, `ModList`, `Sample`, `Filter`, `Instrument` forward-decls |
| #7 Tagged union init (`voice.c:255-294`) | **Low-Medium** — heavy modsystem dependency; excerpt should focus on switch + union member assignment |
| #10 No hidden control flow (`voice.c:259`) | **High** — single line, self-explanatory |
| #13 Safety-checked UB (`voice.c:161-163`) | **High** — self-contained bounds check bug |
| #17 For over slices (`voice.c` — corrected range) | **Medium** — `granular_process` is self-contained except for `Sample` dependency |
| #18 Platform conditionals (`io.c:32-114`) | **High** — fully self-contained |

---

## D. TRANSLATION GOTCHAS

C constructs in the planned 20 differences that lack clean one-to-one Zig equivalents, or that reference files outside the hard scope.

### Out-of-scope file references

Six of the 20 planned differences reference files outside the hard scope (`dstruct.{c,h}`, `io.{c,h}`, `voice.{c,h}`):

| # | Referenced file | Status | Recommendation for blog |
|---|-----------------|--------|------------------------|
| 4 | `oscillator.h:8-12` | Out of scope | Reference briefly as a `comptime` example; use `voice.h:14-18` (`#define` constants) as the in-scope parallel showing C's preprocessor vs Zig's typed compile-time constants. |
| 11 | `sample.h:37-42` | Out of scope | Reference briefly for slices; use `dstruct.c:72` (`memcpy` with `void*` + `size_t`) as the in-scope parallel showing C's pointer+length pair vs Zig's `[]const u8` slice. |
| 12 | `tests/testVoice.c` | Out of scope | Reference briefly to contrast C's external test harness with Zig's built-in `test` blocks. No in-scope substitute. |
| 15 | `filters.c:13-26` | Out of scope | Reference briefly for switch expressions; use `voice.c:97-112` (`generateBlep`) as the in-scope parallel showing C's statement-switch vs Zig's expression-switch. |
| 16 | `oscillator.c:65-77` | Out of scope | Reference briefly for struct methods; use `voice.c:50-75` (`freeVoice`) as the in-scope parallel showing C's free-function-with-pointer vs Zig's attached methods. |
| 19 | `filters.c:44-59` | Out of scope | Reference briefly for SIMD; use `voice.c:178-184` (PCM conversion loop) as the in-scope parallel showing element-by-element processing vs Zig's `@Vector` batch operations. |
| 20 | `Makefile` | Out of scope | Reference briefly for build systems. No in-scope substitute. |

### C constructs lacking clean Zig equivalents

#### 1. VLAs (Variable-Length Arrays)
No VLAs appear in the three scoped files, but `io.c:10` uses `char header[4]` (fixed-size) and `io.c:35` uses `char searchPath[MAX_PATH]` (macro-sized). If VLAs existed in the broader codebase, Zig would require `std.heap` allocation or comptime-known sizes. **Not a blocker for scoped excerpts.**

#### 2. Variadic functions
None of the three scoped files define variadic functions. `printf` and `fprintf` are variadic (from libc), but these are called, not defined. Zig's `std.fmt` replaces variadic formatting with compile-time checked format strings. **Not a blocker for scoped excerpts.**

#### 3. Designated initializer arrays
`voice.c:299-303` uses a designated initializer for a struct:
```c
Preset p1 = (Preset){
    .voiceType = VOICE_TYPE_FM,
    .pd.fm.selectedAlgorithm = 0,
    .modSettingsCount = 4
};
```
This is actually close to Zig's struct initialization syntax (`.field = value`), so it maps cleanly. However, C allows partial initialization (unspecified fields are zeroed) while Zig requires all fields unless `.{}` default initialization is used. **Minor gotcha: partial vs full initialization semantics differ.**

#### 4. `void*` generic pointers
`dstruct.h:23` (`void *data`), `dstruct.h:25` (`void *(*getValue)(ListElement *element)`), `dstruct.c:59` (`const void* data`) — the `List` uses `void*` for type-erased data storage. Zig has no direct `void*` equivalent for generic data; the idiomatic approach is `anytype` generics or `*anyopaque` with explicit casting. This is a significant translation gotcha: the entire `List`/`ListElement` design would need rethinking in Zig, likely using `std.ArrayList(T)` or a custom generic container.

#### 5. Function pointer as "method"
`voice.h:206` (`GenerateSample generate`) stores a function pointer inside the struct to simulate method dispatch. Zig would use a tagged union with inline `switch`-based dispatch or actual struct methods. The C pattern works but loses compile-time exhaustiveness checking — adding a new `VoiceType` variant requires remembering to update every `switch` and every function-pointer assignment.

#### 6. Implicit fall-through in switch
`voice.c:291-293`: the `VOICE_TYPE_SPECTRAL` case lacks a `break`, falling through to `default`:
```c
case VOICE_TYPE_SPECTRAL:
    voice->vd.spectral.sample = inst->id.sampler.sample;
    voice->vd.spectral.samplePosition = 0.0f;
    addModulation(voice->paramList, &voice->envelope[0]->base, voice->volume, 1.0f, MO_MUL);
    voice->generate = generateSpectral;
    // no break — falls through to default
default:
    break;
```
In this specific case, `default` is empty so the fall-through is harmless. But it's a maintenance hazard. Zig requires explicit `else` in switches and does not allow fall-through without `fallthrough` keyword, eliminating this class of bug.

#### 7. Macro-based constants vs typed compile-time values
Throughout the scoped files, constants are defined with `#define` (e.g., `voice.h:14-18`: `MAX_LFOS`, `MAX_ENVELOPES`, `MAX_FM_OPERATORS`). These have no type and are subject to text substitution. Zig's `comptime` values are typed and participate in the type system. The translation is straightforward but requires choosing appropriate types (`usize` for counts, `u32` for bit depths, etc.).
