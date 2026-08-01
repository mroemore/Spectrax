# C→Zig Differences Map — Spectrax DAW

20 ordered contrast points for a C-programming DAW developer learning Zig.
Each entry maps to actual C source within `src/dstruct.{c,h}`, `src/io.{c,h}`, `src/voice.{c,h}`.
Out-of-scope file references from the original plan are handled via the audit's recommended substitutes.

---

## 1. Two-stage allocation with manual cleanup cascade
- **DAW relevance:** DAW plugins frequently allocate a primary struct plus a secondary buffer (e.g., a voice pool and its element arena) in two steps. Managing failure of the second allocation without leaking the first is a recurring pattern in real-time audio code.
- **C source:** `dstruct.c:3-29` (`createList`)
- **Classification:** clean-excerpt
- **Zig translation highlights:** Zig's allocator interface (`std.mem.Allocator`) bundles allocation and deallocation into a single typed abstraction, eliminating the two-malloc cascade and its manual rollback logic.
- **Zig idioms to notice:** `defer` for guaranteed cleanup on any exit path, plus `try`/`catch` with error unions replacing manual NULL propagation.

## 2. Error propagation with repeated fclose/free
- **DAW relevance:** Loading sample libraries involves opening files, reading headers, validating magic numbers, and converting PCM data — every failure path currently repeats `fclose` and sometimes `free`, which is error-prone when adding new validation checks.
- **C source:** `io.c:128-222` (`load_wav_sample`)
- **Classification:** clean-excerpt
- **Zig translation highlights:** Zig's error unions (`!T`) propagate failures up the call stack without requiring each intermediate function to perform resource cleanup, collapsing the six `fclose` call sites into one.
- **Zig idioms to notice:** `errdefer` runs only on error paths, paired with `defer` for unconditional cleanup — together they replace the manual cleanup cascade entirely.

## 3. Cleanup duplication across error paths
- **DAW relevance:** The `load_wav_sample` function has `fclose(file)` scattered across lines 138, 144, 150, 175, 198, and 217, plus `free(pcm_data)` and `free(data)` duplicated inside the 8-bit and 16-bit branches — any new validation step risks missing a cleanup path.
- **C source:** `io.c:129, 143, 173-176` (within `load_wav_sample`)
- **Classification:** clean-excerpt
- **Zig translation highlights:** Zig's `errdefer` guarantees that every resource acquired before an error is released exactly once, regardless of how many early-return branches exist.
- **Zig idioms to notice:** The pairing of `defer` (always runs) with `errdefer` (runs only on error) creates a structured cleanup model that the compiler enforces — no cleanup path can be accidentally skipped.

## 4. Preprocessor constants vs typed compile-time values
- **DAW relevance:** DAW codebases use `#define` extensively for buffer sizes, voice counts, and DSP parameters (e.g., `MAX_LFOS`, `MAX_ENVELOPES`, `MAX_FM_OPERATORS`), but these have no type and participate in no type checking.
- **C source:** `dstruct.h:8` (`MAX_NODE_CHILDREN 32`) and `voice.h:14-18` (`MAX_LFOS`, `MAX_ENVELOPES`, `MAX_FM_OPERATORS`, `MAX_DETUNE`, `MAX_PATCHES`) — substitute for the original plan's `oscillator.h:8-12` reference.
- **Classification:** clean-excerpt
- **Zig translation highlights:** Zig replaces text-substitution macros with typed `comptime` constants that the compiler checks for overflow, type mismatches, and unused values.
- **Zig idioms to notice:** `const MAX_LFOS: usize = 8;` is a compile-time value with a concrete type, visible to the type system and usable in array sizes without preprocessor tricks.

## 5. Tagged union dispatch — exhaustive freeVoice
- **DAW relevance:** Voice cleanup must free type-specific resources (FM operators, sample data, granular processors) based on the active voice variant — missing a case in the switch silently leaks memory for that voice type.
- **C source:** `voice.c:50-75` (`freeVoice`)
- **Classification:** clean-excerpt
- **Zig translation highlights:** Zig's tagged union (`union(enum)`) bundles the enum tag with the payload, so `switch` on the union is guaranteed by the compiler to cover every variant — adding a new voice type becomes a compile error until handled.
- **Zig idioms to notice:** `switch (v.type) { .fm => ..., .sampler => ..., .blep => ..., .grain => ..., .spectral => ... }` with no `else` branch; the compiler rejects the code if any variant is missing.

## 6. Tagged union fields — type-safe union access in struct Voice
- **DAW relevance:** The `Voice` struct carries a `VoiceType type` field alongside an anonymous union `vd` with five variant structs — the C compiler cannot prevent reading `vd.fm` when `type == VOICE_TYPE_SAMPLE`, leading to undefined behavior.
- **C source:** `voice.h:189-215` (`struct Voice`)
- **Classification:** clean-excerpt
- **Zig translation highlights:** Zig's `union(enum)` makes the tag and payload a single value, so accessing a variant field is only legal inside the corresponding `switch` arm — the compiler prevents cross-variant field access at compile time.
- **Zig idioms to notice:** `const Voice = struct { type: VoiceType, payload: union(enum) { fm: FmVoiceData, sampler: SamplerVoiceData, ... } };` — the tag and payload are inseparable, eliminating the manual sync burden.

## 7. Tagged union initialization — payload init in initialize_voice
- **DAW relevance:** Initializing a voice requires setting the correct union member (`vd.fm`, `vd.sampler`, etc.) and assigning the matching function pointer — the C code's `VOICE_TYPE_SPECTRAL` case falls through to `default` due to a missing `break`, a latent bug that could corrupt union state.
- **C source:** `voice.c:255-294` (`initialize_voice` switch block)
- **Classification:** teaching-moment (bug demo)
- **Zig translation highlights:** Zig requires every union variant to be initialized with the correct tagged constructor, and `switch` does not allow implicit fall-through, so the missing-break bug cannot exist.
- **Zig idioms to notice:** `voice.payload = .{ .sampler = .{ .sample = inst.sample, .samplePosition = 0.0, .samplePool = inst.sp } };` — the compiler verifies that the assigned variant matches the declared tag.

## 8. NULL checks after malloc → optional types
- **DAW relevance:** Every allocation in a DAW plugin can fail under memory pressure, and the C idiom of `if (!ptr) return NULL;` forces every caller to also check — the pattern propagates through the entire call chain.
- **C source:** `dstruct.c:4-8` (`createList` NULL check after first malloc)
- **Classification:** clean-excerpt
- **Zig translation highlights:** Zig's optional type (`?T`) and `orelse` operator make failure handling explicit at the type level, so callers cannot accidentally dereference a null pointer without a compile error.
- **Zig idioms to notice:** `const l = allocator.create(List) catch return null;` — the `catch` clause handles the error inline, and the return type `?List` documents that the function can fail.

## 9. Header files and include guards → @import
- **DAW relevance:** DAW projects accumulate deep include chains (`voice.h` pulls in 9 headers), creating compilation coupling and slow rebuilds — every change to a transitive dependency triggers a full recompile.
- **C source:** `dstruct.h:1-80`, `io.h:1-129`, `voice.h:1-256` (all three headers with `#ifndef` guards and `typedef struct` forward declarations)
- **Classification:** clean-excerpt
- **Zig translation highlights:** Zig eliminates header files entirely — declarations live alongside definitions in `.zig` files, and `@import("file.zig")` resolves dependencies at compile time without textual inclusion.
- **Zig idioms to notice:** `const dstruct = @import("dstruct.zig");` is a namespaced import; there is no preprocessor, no include guards, and no separate declaration/definition split.

## 10. Function pointer assignment — no hidden control flow
- **DAW relevance:** Assigning `voice->generate = generateBlep` inside a switch case is the C way to simulate polymorphic dispatch — what you see is what executes, but the compiler cannot verify that the assigned function matches the voice type.
- **C source:** `voice.c:259` (`voice->generate = generateBlep;`)
- **Classification:** clean-excerpt
- **Zig translation highlights:** In Zig, dispatch happens through a tagged-union `switch` where the compiler guarantees that the correct branch executes for each variant, eliminating the need for function pointer fields entirely.
- **Zig idioms to notice:** `switch (voice.type) { .blep => generateBlep(voice), .fm => generateFM(voice), ... }` — no function pointer storage, no possibility of a mismatched assignment.

## 11. Pointer+length pairs → slices
- **DAW relevance:** Audio buffers in C are passed as `void*` data pointers alongside `size_t dataSize` (e.g., `memcpy(element->data, data, dataSize)`), requiring every function to carry both values and trust that they are consistent.
- **C source:** `dstruct.c:72` (`memcpy(element->data, data, dataSize)`) — substitute for the original plan's `sample.h:37-42` reference.
- **Classification:** clean-excerpt
- **Zig translation highlights:** Zig slices (`[]const u8`) bundle the pointer and length into a single value, so the compiler enforces that the length always matches the actual buffer size.
- **Zig idioms to notice:** `fn append(list: *List, data: []const u8) void` — the slice carries its length, eliminating the separate `dataSize` parameter and the risk of mismatched pointer/length pairs.

## 12. External test harness → built-in test blocks
- **DAW relevance:** C projects rely on external test frameworks (Unity, CUnit) and separate test binaries (e.g., `tests/testVoice.c`), adding build complexity and making it easy to skip tests during development.
- **C source:** `tests/testVoice.c` — referenced briefly; no in-scope substitute exists.
- **Classification:** clean-excerpt
- **Zig translation highlights:** Zig's built-in `test` blocks live alongside the code they test and run via `zig test`, requiring no external framework or separate build target.
- **Zig idioms to notice:** `test "freeVoice releases all resources" { ... }` inside the same file as `freeVoice` — tests are first-class language constructs, not an afterthought.

## 13. Cosmetic bounds check → safety-checked UB
- **DAW relevance:** The bounds check at `voice.c:161-163` prints an error message but does not return or abort, so execution falls through to an out-of-bounds array write — a real defect where the "safety check" is purely cosmetic.
- **C source:** `voice.c:161-163` (`initVoicePool` bounds check)
- **Classification:** teaching-moment (bug demo)
- **Zig translation highlights:** In Zig, `vm.voiceCount[channelIndex]` triggers a panic in safe build modes when the index is out of bounds, turning a silent memory corruption into a hard failure that catches the bug immediately.
- **Zig idioms to notice:** Zig's safety-checked array indexing panics on out-of-bounds access in Debug and ReleaseSafe modes, making it impossible to silently corrupt adjacent memory.

## 14. Deep include chain → explicit @import
- **DAW relevance:** `voice.h` includes 9 headers (`kiss_fft.h`, `settings.h`, `oscillator.h`, `sample.h`, `notes.h`, `modsystem.h`, `blit_synth.h`, `filters.h`, `fft.h`), creating a compilation dependency graph where changing any one header triggers a full rebuild.
- **C source:** `voice.h:4-12` (`#include` chain)
- **Classification:** clean-excerpt
- **Zig translation highlights:** Each `@import("file.zig")` in Zig is explicit and resolved by the compiler without textual inclusion, so only the files that actually use a dependency need to import it.
- **Zig idioms to notice:** `const kiss_fft = @import("kiss_fft.zig");` is a scoped import visible only within the current file — there is no transitive include pollution.

## 15. Switch as statement → switch as expression
- **DAW relevance:** The `generateBlep` function assigns to `out.L` in each `case` of a switch, repeating the assignment pattern — in Zig, the switch itself produces the value, reducing boilerplate and making the intent clearer.
- **C source:** `voice.c:97-112` (`generateBlep`'s switch on `shape`) — substitute for the original plan's `filters.c:13-26` reference.
- **Classification:** clean-excerpt
- **Zig translation highlights:** Zig's `switch` is an expression that returns a value, so `out.L = switch (shape) { .Ramp => ..., .Square => ..., .Sine => ... };` replaces the repeated assignment pattern.
- **Zig idioms to notice:** `out.L = switch (shape) { .Ramp => blep_saw(...), .Square => blep_square(...), .Sine => noblep_sine(...) };` — the switch body is the value, not a side effect.

## 16. Free function with struct pointer → attached method
- **DAW relevance:** `freeVoice(Voice *v)` is a free function that operates on a `Voice` pointer — in Zig, this becomes `fn free(self: *Voice) void` attached to the `Voice` struct, making the relationship explicit.
- **C source:** `voice.c:50-75` (`freeVoice`) — substitute for the original plan's `oscillator.c:65-77` reference.
- **Classification:** clean-excerpt
- **Zig translation highlights:** Zig attaches methods to structs via `fn free(self: *Voice) void`, so the function is called as `voice.free()` and the compiler enforces that `self` is the correct type.
- **Zig idioms to notice:** `pub fn free(self: *Voice) void { ... }` inside the `Voice` struct definition — methods are just functions with a `self` parameter, called with dot syntax.

## 17. For loop over array → for with index and slice iteration
- **DAW relevance:** Iterating over a voice pool array (`for(int i = 0; i < voiceCount; i++)`) or over grains (`for(int i = 0; i < GRAIN_COUNT; i++)`) is ubiquitous in DSP code — Zig's `for` loops over slices eliminate manual index management and off-by-one errors.
- **C source:** `voice.c:556-580` (`granular_process`'s `for(int i = 0; i < GRAIN_COUNT; i++)`) — corrected from the original plan's `voice.c:155-158` reference.
- **Classification:** clean-excerpt
- **Zig translation highlights:** Zig's `for (pool, 0..) |voice, i|` or `for (0..GRAIN_COUNT) |i|` iterates with bounds-checked indexing and optional element access, eliminating manual counter management.
- **Zig idioms to notice:** `for (0..GRAIN_COUNT) |i| { ... }` is a range-based loop with compile-time bounds; `for (items, 0..) |item, i|` gives both element and index safely.

## 18. Platform-specific directory traversal → std.fs cross-platform abstraction
- **DAW relevance:** The `populateDirectoryList` function uses `#ifdef _WIN32` to select between Win32 API (`FindFirstFile`/`FindNextFile`) and POSIX (`opendir`/`readdir`) — this Linux/POSIX path excerpt (lines 75-114) is the code that runs on non-Windows DAW hosts.
- **C source:** `io.c:32-114` (`populateDirectoryList` — the `#ifdef _WIN32` / `#else` block, with the POSIX/Linux path at lines 75-114)
- **Classification:** clean-excerpt
- **Zig translation highlights:** Zig's `std.fs` provides cross-platform directory iteration (`std.fs.openDir`, `dir.iterate()`) that compiles to the correct syscalls per target OS, eliminating the need for manual `#ifdef` branches.
- **Zig idioms to notice:** `var iter = dir.iterate(); while (iter.next() catch break) |entry| { ... }` — a single code path works on all platforms, with the standard library handling OS differences.

## 19. Element-by-element audio processing → @Vector SIMD batch operations
- **DAW relevance:** The 8-bit PCM-to-float conversion loop at `voice.c:178-184` processes interleaved audio samples one at a time — in a DAW, this inner loop runs millions of times per second and is a prime candidate for SIMD vectorization.
- **C source:** `voice.c:178-184` (the 8-bit PCM-to-float conversion loop within `load_wav_sample`) — substitute for the original plan's `filters.c:44-59` reference.
- **Classification:** clean-excerpt
- **Zig translation highlights:** Zig's `@Vector(N, T)` type enables batch processing of audio samples with SIMD instructions, replacing the element-by-element loop with a single vectorized operation.
- **Zig idioms to notice:** `const v: @Vector(4, f32) = @as(@Vector(4, f32), @bitCast(pcm_chunk));` — the compiler auto-vectorizes where possible and exposes explicit SIMD types for manual optimization.

## 20. Makefile build system → build.zig
- **DAW relevance:** C DAW projects use Makefiles or CMake to manage compilation, linking against audio libraries (e.g., `-lportaudio`, `-lsndfile`), and test execution — build configuration is separate from the language and often fragile.
- **C source:** `Makefile` — referenced briefly; no in-scope substitute exists.
- **Classification:** clean-excerpt
- **Zig translation highlights:** Zig's `build.zig` consolidates compilation, linking, and test execution into a single build system written in Zig itself, with first-class support for cross-compilation and dependency management.
- **Zig idioms to notice:** `exe.linkSystemLibrary("portaudio");` and `exe.addIncludePath(.{ .path = "src" });` in `build.zig` — the build script is Zig code, with full access to the standard library and compile-time evaluation.
