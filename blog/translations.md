# From C to Zig: A DAW Developer's Guide

*By the Spectrax team — May 28, 2026*

Andrew Kelley started building Zig while working on the Genesis DAW — a digital audio workstation written in C that kept hitting the same pain points: manual memory management, fragile error handling, and the endless preprocessor gymnastics required to keep a real-time audio codebase sane. Zig grew out of that frustration. If you're a C programmer building audio software — samplers, synthesizers, effects plugins, or full DAWs — this guide shows you what the translation actually looks like, using real code from Spectrax, an open-source DAW. Each section presents a verbatim C excerpt from Spectrax's source, followed by an idiomatic Zig 0.16 translation. No marketing fluff, no hand-waving — just code.

---

## 1. Two-Stage Allocation with Manual Cleanup Cascade

DAW plugins frequently allocate a primary struct plus a secondary buffer in two steps — for example, a voice pool and its element arena. Managing failure of the second allocation without leaking the first is a recurring pattern in real-time audio code. In C, this means checking each `malloc`, and if the second one fails, manually freeing the first before returning. Zig's allocator interface bundles allocation and dealeration into a single typed abstraction, and `defer` guarantees cleanup on any exit path, eliminating the manual rollback logic entirely.

```c
// dstruct.c:3-29
List* createList(int capacity){
    List* l = (List*)malloc(sizeof(List));
    if(!l){
        printf("ERR: createList: malloc fail.\n");
        return NULL;
    }
    l->head = NULL;
    l->tail = NULL;
    l->count = 0;
    l->capacity = capacity;
    l->pool = (ListElement*)malloc(sizeof(ListElement) * l->capacity);
    if(l->pool == NULL){
        free(l);
        printf("ERR: createList: pool malloc fail.\n");
        return NULL;
    }

    l->freeList = l->pool;
    
    for (int i = 0; i < capacity - 1; i++) {
        l->pool[i].next = &l->pool[i + 1];
        l->pool[i + 1].prev = &l->pool[i];
    }

    l->pool[capacity - 1].next = NULL;
    return l;
}
```

```zig
const std = @import("std");

pub const ListElement = struct {
    type: i32,
    prev: ?*ListElement,
    next: ?*ListElement,
    data: ?*anyopaque,
    dataSize: usize,
    getValue: ?*const fn (*ListElement) callconv(.C) ?*anyopaque,
};

pub const List = struct {
    head: ?*ListElement,
    tail: ?*ListElement,
    count: i32,
    capacity: i32,
    pool: []ListElement,
    freeList: ?*ListElement,

    pub fn create(allocator: std.mem.Allocator, capacity: i32) !List {
        var l = try allocator.create(List);
        errdefer allocator.destroy(l);

        l.* = .{
            .head = null,
            .tail = null,
            .count = 0,
            .capacity = capacity,
            .pool = try allocator.alloc(ListElement, @intCast(capacity)),
            .freeList = null,
        };
        errdefer allocator.free(l.pool);

        l.freeList = &l.pool[0];

        var i: usize = 0;
        while (i < @intCast(capacity - 1)) : (i += 1) {
            l.pool[i].next = &l.pool[i + 1];
            l.pool[i + 1].prev = &l.pool[i];
        }

        l.pool[@intCast(capacity - 1)].next = null;
        return l;
    }
};
```

## 2. Error Propagation with Repeated fclose/free

Loading sample libraries involves opening files, reading headers, validating magic numbers, and converting PCM data. Every failure path currently repeats `fclose` and sometimes `free`, which is error-prone when adding new validation checks. Zig's error unions (`!T`) propagate failures up the call stack without requiring each intermediate function to perform resource cleanup. The six `fclose` call sites in the C version collapse into a single `errdefer file.close()`.

```c
// io.c:128-222
void load_wav_sample(const char *filename, SamplePool *sp) {
    FILE *file = fopen(filename, "rb");
    if(!file) {
        printf("Failed to open sample file: %s\n", filename);
        return;
    }

    WAVHeader header;
    if(fread(&header, sizeof(WAVHeader), 1, file) != 1) {
        printf("Failed to read WAV header\n");
        fclose(file);
        return;
    }

    if(strncmp(header.chunkID, "RIFF", 4) != 0 || strncmp(header.format, "WAVE", 4) != 0) {
        printf("Invalid or unsupported WAV file format\n");
        fclose(file);
        return;
    }
    if(strncmp(header.subchunk2ID, "data", 4) != 0) {
        printf("Failed to find data subchunk\n");
        fclose(file);
        return;
    }

    int bit = header.bitsPerSample * header.numChannels;
    int length = header.subchunk2Size / (header.bitsPerSample / 4);
    float *data = (float *)malloc(sizeof(float) * length);
    if(header.bitsPerSample == 8) {
        uint8_t *pcm_data = (uint8_t *)malloc(header.subchunk2Size);
        if(fread(pcm_data, 1, header.subchunk2Size, file) != header.subchunk2Size) {
            printf("Failed to read WAV data\n");
            free(pcm_data);
            free(data);
            fclose(file);
            return;
        }
        // ... 8-bit conversion loop ...
        loadSample(sp, filename, data, header.bitsPerSample * header.numChannels, header.sampleRate, length);
        free(pcm_data);
    } else if(header.bitsPerSample == 16) {
        int16_t *pcm_data = (int16_t *)malloc(header.subchunk2Size);
        if(fread(pcm_data, sizeof(int16_t), header.subchunk2Size / 2, file) != header.subchunk2Size / 2) {
            printf("Failed to read WAV data\n");
            free(pcm_data);
            free(data);
            fclose(file);
            return;
        }
        // ... 16-bit conversion loop ...
        loadSample(sp, filename, data, header.bitsPerSample * header.numChannels, header.sampleRate, length);
        free(pcm_data);
    } else {
        printf("Unsupported bit depth: %d\n", header.bitsPerSample);
        fclose(file);
        return;
    }

    fclose(file);
}
```

```zig
const std = @import("std");

pub const WavLoadError = error{
    OpenFailed,
    ReadFailed,
    InvalidFormat,
    UnsupportedBitDepth,
};

pub fn loadWavSample(
    allocator: std.mem.Allocator,
    sp: *SamplePool,
    filename: []const u8,
) WavLoadError!void {
    var file = std.fs.cwd().openFile(filename, .{}) catch return WavLoadError.OpenFailed;
    defer file.close();

    var header = try file.readStruct(WavHeader);

    if (!std.mem.eql(u8, &header.chunkID, "RIFF") or
        !std.mem.eql(u8, &header.format, "WAVE"))
    {
        return WavLoadError.InvalidFormat;
    }
    if (!std.mem.eql(u8, &header.subchunk2ID, "data")) {
        return WavLoadError.InvalidFormat;
    }

    const length: usize = @divExact(header.subchunk2Size, @as(u32, @intCast(header.bitsPerSample)) / 4);
    var data = try allocator.alloc(f32, length);
    defer allocator.free(data);

    if (header.bitsPerSample == 8) {
        var pcm_data = try allocator.alloc(u8, header.subchunk2Size);
        defer allocator.free(pcm_data);
        _ = try file.readAll(pcm_data);

        var i: usize = 0;
        while (i < length) : (i += 1) {
            var value: f32 = 0.0;
            var ch: usize = 0;
            while (ch < @intCast(header.numChannels)) : (ch += 1) {
                value += (@as(f32, @floatFromInt(pcm_data[i * @as(usize, @intCast(header.numChannels)) + ch])) - 128.0) / 128.0;
            }
            data[i] = value / @as(f32, @floatFromInt(header.numChannels));
        }

        try loadSample(sp, filename, data, @intCast(header.bitsPerSample * header.numChannels), @intCast(header.sampleRate), @intCast(length));
    } else if (header.bitsPerSample == 16) {
        var pcm_data = try allocator.alloc(i16, header.subchunk2Size / 2);
        defer allocator.free(pcm_data);
        _ = try file.readAll(std.mem.sliceAsBytes(pcm_data));

        var i: usize = 0;
        while (i < length) : (i += 1) {
            var value: f32 = 0.0;
            var ch: usize = 0;
            while (ch < @intCast(header.numChannels)) : (ch += 1) {
                value += @as(f32, @floatFromInt(pcm_data[i * @as(usize, @intCast(header.numChannels)) + ch])) / 32768.0;
            }
            data[i] = value / @as(f32, @floatFromInt(header.numChannels));
        }

        try loadSample(sp, filename, data, @intCast(header.bitsPerSample * header.numChannels), @intCast(header.sampleRate), @intCast(length));
    } else {
        return WavLoadError.UnsupportedBitDepth;
    }
}
```

## 3. Cleanup Duplication Across Error Paths

The `load_wav_sample` function has `fclose(file)` scattered across six lines (138, 144, 150, 175, 198, 217), plus `free(pcm_data)` and `free(data)` duplicated inside the 8-bit and 16-bit branches. Any new validation step risks missing a cleanup path. Zig's `errdefer` guarantees that every resource acquired before an error is released exactly once, regardless of how many early-return branches exist. The pairing of `defer` (always runs) with `errdefer` (runs only on error) creates a structured cleanup model that the compiler enforces.

```c
// io.c:129, 143, 173-176 (within load_wav_sample)
    FILE *file = fopen(filename, "rb");
    if(!file) {
        // ... return without fclose — file was never opened
    }
    // ...
    if(fread(&header, sizeof(WAVHeader), 1, file) != 1) {
        fclose(file);  // line 138
        return;
    }
    // ...
    if(strncmp(header.chunkID, "RIFF", 4) != 0 || ...) {
        fclose(file);  // line 144
        return;
    }
    // ...
    if(header.bitsPerSample == 8) {
        // ...
        if(fread(pcm_data, 1, header.subchunk2Size, file) != header.subchunk2Size) {
            free(pcm_data);  // line 173
            free(data);      // line 174
            fclose(file);    // line 175
            return;
        }
    }
```

```zig
// The same function as Section 2 — notice how cleanup is declared
// once, right after each resource is acquired:

    var file = std.fs.cwd().openFile(filename, .{}) catch return error.OpenFailed;
    defer file.close();          // always runs — replaces 6x fclose(file)

    var data = try allocator.alloc(f32, length);
    defer allocator.free(data);  // always runs — replaces free(data) in both branches

    var pcm_data = try allocator.alloc(u8, header.subchunk2Size);
    defer allocator.free(pcm_data);  // always runs — replaces free(pcm_data)
```

## 4. Preprocessor Constants vs Typed Compile-Time Values

DAW codebases use `#define` extensively for buffer sizes, voice counts, and DSP parameters. These have no type and participate in no type checking — `MAX_LFOS` could silently overflow in an expression, or be compared against an incompatible type without warning. Zig replaces text-substitution macros with typed `comptime` constants that the compiler checks for overflow, type mismatches, and unused values.

```c
// dstruct.h:8
#define MAX_NODE_CHILDREN 32

// voice.h:14-18
#define MAX_LFOS 8
#define MAX_ENVELOPES 6
#define MAX_FM_OPERATORS 4
#define MAX_DETUNE 16
#define MAX_PATCHES 255
```

```zig
// dstruct.zig
pub const MAX_NODE_CHILDREN: usize = 32;

// voice.zig
pub const MAX_LFOS: usize = 8;
pub const MAX_ENVELOPES: usize = 6;
pub const MAX_FM_OPERATORS: usize = 4;
pub const MAX_DETUNE: usize = 16;
pub const MAX_PATCHES: usize = 255;
```

## 5. Tagged Union Dispatch — Exhaustive freeVoice

Voice cleanup must free type-specific resources (FM operators, sample data, granular processors) based on the active voice variant. Missing a case in the switch silently leaks memory for that voice type. Zig's tagged union (`union(enum)`) bundles the enum tag with the payload, so `switch` on the union is guaranteed by the compiler to cover every variant — adding a new voice type becomes a compile error until handled.

```c
// voice.c:50-75
void freeVoice(Voice *v) {
    freeModList(v->modList);
    freeParamList(v->paramList);
    freeParameter(v->volume);
    for(int i = 0; i < v->envCount; i++) {
        freeEnvelope(v->envelope[i]);
    }
    for(int i = 0; i < v->lfoCount; i++) {
        freeLFO(v->lfo[i]);
    }
    switch(v->type) {
        case VOICE_TYPE_FM:
            for(int i = 0; i < MAX_FM_OPERATORS; i++) {
                freeOperator(v->vd.fm.operators[i]);
            }
            break;
        case VOICE_TYPE_SAMPLE:
            freeSample(v->vd.sampler.sample);
            break;
        case VOICE_TYPE_BLEP:
        default:
            break;
    }

    free(v);
}
```

```zig
pub const VoiceType = enum {
    sample,
    fm,
    blep,
    grain,
    spectral,
};

pub const Voice = struct {
    type: VoiceType,
    payload: union(VoiceType) {
        fm: FmVoiceData,
        sampler: SamplerVoiceData,
        blep: BlepVoiceData,
        grain: GranularVoiceData,
        spectral: SpectralVoiceData,
    },
    // ... other fields ...

    pub fn free(self: *Voice, allocator: std.mem.Allocator) void {
        // ... free common fields ...

        switch (self.payload) {
            .fm => |*fm| {
                var i: usize = 0;
                while (i < MAX_FM_OPERATORS) : (i += 1) {
                    freeOperator(fm.operators[i], allocator);
                }
            },
            .sampler => |*s| {
                freeSample(s.sample, allocator);
            },
            .blep => {},
            .grain => {},
            .spectral => {},
        }

        allocator.destroy(self);
    }
};
```

## 6. Tagged Union Fields — Type-Safe Union Access in struct Voice

The `Voice` struct carries a `VoiceType type` field alongside an anonymous union `vd` with five variant structs. The C compiler cannot prevent reading `vd.fm` when `type == VOICE_TYPE_SAMPLE`, leading to undefined behavior. Zig's `union(enum)` makes the tag and payload a single value, so accessing a variant field is only legal inside the corresponding `switch` arm — the compiler prevents cross-variant field access at compile time.

```c
// voice.h:189-215
struct Voice {
    VoiceType type;
    float leftPhase;
    float rightPhase;
    float detunePhase[MAX_DETUNE];
    int note[2];
    int samplesElapsed;
    int active;
    ParamList *paramList;
    ModList *modList;
    int envCount;
    int lfoCount;
    Envelope *envelope[4];
    LFO *lfo[2];
    Parameter *frequency;
    Parameter *volume;
    Instrument *instrumentRef;
    GenerateSample generate;
    union {
        FmVoiceData fm;
        BlepVoiceData blep;
        SpectralVoiceData spectral;
        SamplerVoiceData sampler;
        GranularVoiceData granular;
    } vd;
    Filter *filter;
};
```

```zig
pub const Voice = struct {
    type: VoiceType,
    left_phase: f32,
    right_phase: f32,
    detune_phase: [MAX_DETUNE]f32,
    note: [2]i32,
    samples_elapsed: i32,
    active: bool,
    param_list: *ParamList,
    mod_list: *ModList,
    env_count: i32,
    lfo_count: i32,
    envelope: [4]?*Envelope,
    lfo: [2]?*LFO,
    frequency: *Parameter,
    volume: *Parameter,
    instrument_ref: *Instrument,
    generate: *const fn (*Voice, f32, f32) OutVal,
    payload: union(VoiceType) {
        fm: FmVoiceData,
        blep: BlepVoiceData,
        spectral: SpectralVoiceData,
        sampler: SamplerVoiceData,
        granular: GranularVoiceData,
    },
    filter: *Filter,
};

// Access is only legal inside a switch on the tagged union:
fn processVoice(voice: *Voice) void {
    switch (voice.payload) {
        .fm => |*fm| {
            // fm.operators is accessible here
            _ = fm.operators;
        },
        .sampler => |*s| {
            // s.sample is accessible here
            _ = s.sample;
        },
        .blep => {},
        .grain => {},
        .spectral => {},
    }
}
```

## 7. Tagged Union Initialization — Payload Init in initialize_voice

Initializing a voice requires setting the correct union member (`vd.fm`, `vd.sampler`, etc.) and assigning the matching function pointer. The C code's `VOICE_TYPE_SPECTRAL` case falls through to `default` due to a missing `break` — a latent bug that could corrupt union state. Zig requires every union variant to be initialized with the correct tagged constructor, and `switch` does not allow implicit fall-through, so the missing-break bug cannot exist.

```c
// voice.c:255-294
    switch(voice->type) {
        case VOICE_TYPE_BLEP:
            addModulation(voice->paramList, &voice->envelope[0]->base, voice->volume, 1.0f, MO_MUL);
            addModulation(voice->paramList, &voice->envelope[1]->base, voice->frequency, 400.5f, MO_ADD);
            voice->generate = generateBlep;
            break;

        case VOICE_TYPE_SAMPLE:
            voice->vd.sampler.sample = inst->id.sampler.sample;
            voice->vd.sampler.samplePosition = 0.0f;
            voice->vd.sampler.samplePool = inst->id.sampler.sp;
            addModulation(voice->paramList, &voice->envelope[0]->base, voice->volume, 1.0f, MO_MUL);
            voice->generate = generateSample;
            break;

        case VOICE_TYPE_FM:
            voice->vd.fm.operators[0] = createParamPointerOperator(...);
            voice->vd.fm.operators[1] = createParamPointerOperator(...);
            voice->vd.fm.operators[2] = createParamPointerOperator(...);
            voice->vd.fm.operators[3] = createParamPointerOperator(...);
            addModulation(voice->paramList, &voice->envelope[0]->base, voice->vd.fm.operators[0]->outLevel, 1.0f, MO_MUL);
            voice->generate = generateFM;
            break;
        case VOICE_TYPE_GRAIN:
            voice->vd.granular.granularProcessor = createGranularProcessor(inst->id.sampler.sample);
            voice->generate = generateGranular;
            break;
        case VOICE_TYPE_SPECTRAL:
            voice->vd.spectral.sample = inst->id.sampler.sample;
            voice->vd.spectral.samplePosition = 0.0f;
            addModulation(voice->paramList, &voice->envelope[0]->base, voice->volume, 1.0f, MO_MUL);
            voice->generate = generateSpectral;
            // missing break — falls through to default
        default:
            break;
    }
```

```zig
    switch (voice.type) {
        .blep => {
            addModulation(voice.param_list, &voice.envelope[0].base, voice.volume, 1.0, .mul);
            addModulation(voice.param_list, &voice.envelope[1].base, voice.frequency, 400.5, .add);
            voice.generate = generateBlep;
        },
        .sample => {
            voice.payload = .{ .sampler = .{
                .sample = inst.id.sampler.sample,
                .sample_position = 0.0,
                .sample_pool = inst.id.sampler.sp,
            } };
            addModulation(voice.param_list, &voice.envelope[0].base, voice.volume, 1.0, .mul);
            voice.generate = generateSample;
        },
        .fm => {
            voice.payload = .{ .fm = .{
                .operators = .{
                    createParamPointerOperator(voice.param_list, inst.id.fm.ops[0]),
                    createParamPointerOperator(voice.param_list, inst.id.fm.ops[1]),
                    createParamPointerOperator(voice.param_list, inst.id.fm.ops[2]),
                    createParamPointerOperator(voice.param_list, inst.id.fm.ops[3]),
                },
            } };
            addModulation(voice.param_list, &voice.envelope[0].base, voice.payload.fm.operators[0].out_level, 1.0, .mul);
            voice.generate = generateFM;
        },
        .grain => {
            voice.payload = .{ .grain = .{
                .granular_processor = try createGranularProcessor(allocator, inst.id.sampler.sample),
            } };
            voice.generate = generateGranular;
        },
        .spectral => {
            voice.payload = .{ .spectral = .{
                .sample = inst.id.sampler.sample,
                .sample_position = 0.0,
                .spectral_data = undefined,
                .spectral_data_size = 0,
            } };
            addModulation(voice.param_list, &voice.envelope[0].base, voice.volume, 1.0, .mul);
            voice.generate = generateSpectral;
        },
    }
```

## 8. NULL Checks After malloc — Optional Types

Every allocation in a DAW plugin can fail under memory pressure, and the C idiom of `if (!ptr) return NULL;` forces every caller to also check — the pattern propagates through the entire call chain. Zig's optional type (`?T`) and `orelse` operator make failure handling explicit at the type level, so callers cannot accidentally dereference a null pointer without a compile error.

```c
// dstruct.c:4-8
    List* l = (List*)malloc(sizeof(List));
    if(!l){
        printf("ERR: createList: malloc fail.\n");
        return NULL;
    }
```

```zig
    const l = allocator.create(List) catch return null;
    // Return type is ?List — the caller must handle the null case explicitly.
```

## 9. Header Files and Include Guards — @import

DAW projects accumulate deep include chains — `voice.h` pulls in 9 headers — creating compilation coupling and slow rebuilds. Every change to a transitive dependency triggers a full recompile. Zig eliminates header files entirely. Declarations live alongside definitions in `.zig` files, and `@import("file.zig")` resolves dependencies at compile time without textual inclusion. There is no preprocessor, no include guards, and no separate declaration/definition split.

```c
// dstruct.h:1-12
#ifndef DSTRUCT_H
#define DSTRUCT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NODE_CHILDREN 32

typedef struct ListElement ListElement;
typedef struct Node Node;
typedef struct GuiNode GuiNode;
```

```zig
// dstruct.zig
const std = @import("std");

pub const MAX_NODE_CHILDREN: usize = 32;

pub const ListElement = struct {
    // ... fields ...
};

pub const Node = struct {
    // ... fields ...
};

// In another file:
const dstruct = @import("dstruct.zig");
// dstruct.ListElement, dstruct.MAX_NODE_CHILDREN — namespaced, no guards needed.
```

## 10. Function Pointer Assignment — No Hidden Control Flow

Assigning `voice->generate = generateBlep` inside a switch case is the C way to simulate polymorphic dispatch — what you see is what executes, but the compiler cannot verify that the assigned function matches the voice type. In Zig, dispatch happens through a tagged-union `switch` where the compiler guarantees that the correct branch executes for each variant, eliminating the need for function pointer fields entirely.

```c
// voice.c:259
            voice->generate = generateBlep;
```

```zig
// No function pointer stored in the struct. Dispatch is a switch:
fn generateVoice(voice: *Voice, phase_inc: f32, frequency: f32) OutVal {
    return switch (voice.type) {
        .blep => generateBlep(voice, phase_inc, frequency),
        .fm => generateFM(voice, phase_inc, frequency),
        .sample => generateSample(voice, phase_inc, frequency),
        .spectral => generateSpectral(voice, phase_inc, frequency),
        .grain => generateGranular(voice, phase_inc, frequency),
    };
}
```

## 11. Pointer+Length Pairs — Slices

Audio buffers in C are passed as `void*` data pointers alongside `size_t dataSize`, requiring every function to carry both values and trust that they are consistent. Zig slices (`[]const u8`) bundle the pointer and length into a single value, so the compiler enforces that the length always matches the actual buffer size.

```c
// dstruct.c:66-72
    element->data = malloc(dataSize);
    if (element->data == NULL) {
        fprintf(stderr, "Memory allocation for data failed\n");
        freeElement(list, element);
        return NULL;
    }
    memcpy(element->data, data, dataSize);
```

```zig
    element.data = try allocator.alloc(u8, data.len);
    @memcpy(element.data, data);
    // `data` is a []const u8 — the slice carries its length,
    // eliminating the separate dataSize parameter entirely.
```

## 12. External Test Harness — Built-in Test Blocks

C projects rely on external test frameworks (Unity, CUnit) and separate test binaries like `tests/testVoice.c`, adding build complexity and making it easy to skip tests during development. Zig's built-in `test` blocks live alongside the code they test and run via `zig test`, requiring no external framework or separate build target. Tests are first-class language constructs, not an afterthought.

```c
// tests/testVoice.c — referenced briefly
// C requires a separate test file, an external framework (e.g., Unity),
// and a dedicated build target in the Makefile:
//
//   test_voice: tests/testVoice.c src/voice.c
//       $(CC) -o $@ $^ -lunity -lm
//
// Tests are easy to skip during development because they require
// a separate compilation step and binary execution.
```

```zig
// Inside voice.zig — test blocks live alongside the code:
test "freeVoice releases all resources" {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena.deinit();
    const allocator = arena.allocator();

    var voice = try createTestVoice(allocator, .sample);
    voice.free(allocator);
    // If any resource was leaked, the arena's deinit would catch it
    // when running under the GeneralPurposeAllocator in debug mode.
}

test "initialize_voice sets correct payload variant" {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena.deinit();
    const allocator = arena.allocator();

    var inst = try createTestInstrument(allocator, .blep);
    var voice = try Voice.create(allocator, &inst);
    try std.testing.expectEqual(VoiceType.blep, voice.type);
}
```

## 13. Cosmetic Bounds Check — Safety-Checked UB

The bounds check at `voice.c:161-163` prints an error message but does not return or abort, so execution falls through to an out-of-bounds array write — a real defect where the "safety check" is purely cosmetic. In Zig, array indexing triggers a panic in safe build modes when the index is out of bounds, turning silent memory corruption into a hard failure that catches the bug immediately.

```c
// voice.c:161-166
void initVoicePool(VoiceManager *vm, int channelIndex, int voiceCount, Instrument *inst) {
    if(channelIndex >= MAX_SEQUENCER_CHANNELS || channelIndex < 0) {
        printf("out of bounds!\n");
    }
    // BUG: no return — execution falls through to OOB write below
    if(voiceCount >= MAX_VOICES_PER_CHANNEL) voiceCount = MAX_VOICES_PER_CHANNEL;

    vm->voiceCount[channelIndex] = 0;  // out-of-bounds write if channelIndex >= MAX
```

```zig
pub fn initVoicePool(
    vm: *VoiceManager,
    channel_index: usize,
    voice_count: usize,
    inst: *Instrument,
    allocator: std.mem.Allocator,
) !void {
    // In Zig, this indexing panics in Debug/ReleaseSafe mode if out of bounds.
    // No cosmetic check needed — the language enforces it.
    const capped_count = @min(voice_count, MAX_VOICES_PER_CHANNEL);

    vm.voiceCount[channel_index] = 0;

    var i: usize = 0;
    while (i < capped_count) : (i += 1) {
        vm.voicePools[channel_index][i] = try allocator.create(Voice);
        initializeVoice(vm.voicePools[channel_index][i], inst);
        vm.voiceCount[channel_index] += 1;
    }
}
```

## 14. Deep Include Chain — Explicit @import

`voice.h` includes 9 headers (`kiss_fft.h`, `settings.h`, `oscillator.h`, `sample.h`, `notes.h`, `modsystem.h`, `blit_synth.h`, `filters.h`, `fft.h`), creating a compilation dependency graph where changing any one header triggers a full rebuild. Each `@import("file.zig")` in Zig is explicit and resolved by the compiler without textual inclusion, so only the files that actually use a dependency need to import it.

```c
// voice.h:1-12
#ifndef VOICE_H
#define VOICE_H
#include <stdlib.h>
#include "kiss_fft.h"
#include "settings.h"
#include "oscillator.h"
#include "sample.h"
#include "notes.h"
#include "modsystem.h"
#include "blit_synth.h"
#include "filters.h"
#include "fft.h"
```

```zig
// voice.zig — only import what this file actually uses:
const std = @import("std");
const kiss_fft = @import("kiss_fft.zig");
const settings = @import("settings.zig");
const sample = @import("sample.zig");
const notes = @import("notes.zig");
const modsystem = @import("modsystem.zig");
const blit_synth = @import("blit_synth.zig");
const filters = @import("filters.zig");
const fft = @import("fft.zig");
// Each import is scoped to this file — no transitive pollution.
```

## 15. Switch as Statement — Switch as Expression

The `generateBlep` function assigns to `out.L` in each `case` of a switch, repeating the assignment pattern. In Zig, the switch itself produces the value, so `out.L = switch (shape) { ... }` replaces the repeated assignment pattern, reducing boilerplate and making the intent clearer.

```c
// voice.c:94-112
OutVal generateBlep(Voice *currentVoice, float phaseIncrement, float frequency) {
    OutVal out;
    int shape = getParameterValueAsInt(currentVoice->instrumentRef->id.blep.shape);
    switch(shape) {
        case BLEP_RAMP:
            out.L = blep_saw(currentVoice->leftPhase, phaseIncrement);
            out.L *= 0.025;
            break;
        case BLEP_SQUARE:
            out.L = blep_square(currentVoice->leftPhase, phaseIncrement);
            out.L *= 0.025;
            break;
        case BLEP_SINE:
            out.L = noblep_sine(currentVoice->leftPhase);
            out.L *= 0.5;
            break;
    }
    out.R = out.L;
    return out;
}
```

```zig
fn generateBlep(current_voice: *Voice, phase_increment: f32, frequency: f32) OutVal {
    const shape = getParameterValueAsInt(current_voice.instrument_ref.id.blep.shape);
    const l_val = switch (shape) {
        .ramp => blepSaw(current_voice.left_phase, phase_increment) * 0.025,
        .square => blepSquare(current_voice.left_phase, phase_increment) * 0.025,
        .sine => noBlepSine(current_voice.left_phase) * 0.5,
    };
    return .{ .L = l_val, .R = l_val };
}
```

## 16. Free Function with Struct Pointer — Attached Method

`freeVoice(Voice *v)` is a free function that operates on a `Voice` pointer. In Zig, this becomes `fn free(self: *Voice) void` attached to the `Voice` struct, making the relationship explicit. Methods are just functions with a `self` parameter, called with dot syntax.

```c
// voice.c:50-75
void freeVoice(Voice *v) {
    freeModList(v->modList);
    freeParamList(v->paramList);
    freeParameter(v->volume);
    for(int i = 0; i < v->envCount; i++) {
        freeEnvelope(v->envelope[i]);
    }
    for(int i = 0; i < v->lfoCount; i++) {
        freeLFO(v->lfo[i]);
    }
    switch(v->type) {
        case VOICE_TYPE_FM:
            for(int i = 0; i < MAX_FM_OPERATORS; i++) {
                freeOperator(v->vd.fm.operators[i]);
            }
            break;
        case VOICE_TYPE_SAMPLE:
            freeSample(v->vd.sampler.sample);
            break;
        case VOICE_TYPE_BLEP:
        default:
            break;
    }

    free(v);
}
```

```zig
pub const Voice = struct {
    // ... fields ...

    pub fn free(self: *Voice, allocator: std.mem.Allocator) void {
        freeModList(self.mod_list, allocator);
        freeParamList(self.param_list, allocator);
        freeParameter(self.volume, allocator);

        var i: usize = 0;
        while (i < @intCast(self.env_count)) : (i += 1) {
            freeEnvelope(self.envelope[i], allocator);
        }
        i = 0;
        while (i < @intCast(self.lfo_count)) : (i += 1) {
            freeLFO(self.lfo[i], allocator);
        }

        switch (self.payload) {
            .fm => |*fm| {
                i = 0;
                while (i < MAX_FM_OPERATORS) : (i += 1) {
                    freeOperator(fm.operators[i], allocator);
                }
            },
            .sampler => |*s| {
                freeSample(s.sample, allocator);
            },
            .blep => {},
            .grain => {},
            .spectral => {},
        }

        allocator.destroy(self);
    }
};

// Called as: voice.free(allocator);
```

## 17. For Loop Over Array — For with Index and Slice Iteration

Iterating over a voice pool array or over grains is ubiquitous in DSP code. C's `for(int i = 0; i < GRAIN_COUNT; i++)` requires manual index management and is prone to off-by-one errors. Zig's `for` loops over slices and ranges eliminate this, with compile-time bounds checking and optional element access.

```c
// voice.c:556-580
    for(int i = 0; i < GRAIN_COUNT; i++) {
        float adjusted_phaseinc_sample = phaseIncrement * (SAMPLE_RATE / (float)gp->sample->sampleRate);
        gp->grainReadPos[i] += adjusted_phaseinc_sample;
        gp->windowIndex[i] += phaseIncrement;

        if(gp->windowIndex[i] >= GRAIN_WINDOW_SIZE) {
            gp->windowIndex[i] -= GRAIN_WINDOW_SIZE;
        }

        if(gp->grainReadPos[i] >= gp->sample->length) {
            gp->grainReadPos[i] -= gp->sample->length;
        }

        int indexFloor = (int)gp->grainReadPos[i];
        int sIndexCeil = (indexFloor + 1) % gp->sample->length;
        int wIndexFloor = gp->windowIndex[i];
        int wIndexCeil = (wIndexFloor + 1) % GRAIN_WINDOW_SIZE;
        float frac = gp->grainReadPos[i] - indexFloor;

        float windowVal = gp->grainWindow[wIndexFloor] * (1.0f - frac) + gp->grainWindow[wIndexCeil] * frac;
        float value = gp->sample->data[indexFloor] * (1.0f - frac) + gp->sample->data[sIndexCeil] * frac;

        result.L += value * windowVal;
    }

    result.L /= GRAIN_COUNT;
```

```zig
    for (0..GRAIN_COUNT) |i| {
        const adjusted_phaseinc_sample = phase_increment * (@as(f32, @floatFromInt(SAMPLE_RATE)) / @as(f32, @floatFromInt(gp.sample.sampleRate)));
        gp.grain_read_pos[i] += adjusted_phaseinc_sample;
        gp.window_index[i] += phase_increment;

        if (gp.window_index[i] >= GRAIN_WINDOW_SIZE) {
            gp.window_index[i] -= GRAIN_WINDOW_SIZE;
        }

        if (gp.grain_read_pos[i] >= @as(f32, @floatFromInt(gp.sample.length))) {
            gp.grain_read_pos[i] -= @as(f32, @floatFromInt(gp.sample.length));
        }

        const index_floor: usize = @intFromFloat(@floor(gp.grain_read_pos[i]));
        const s_index_ceil = (index_floor + 1) % @as(usize, @intCast(gp.sample.length));
        const w_index_floor: usize = @intFromFloat(@floor(gp.window_index[i]));
        const w_index_ceil = (w_index_floor + 1) % GRAIN_WINDOW_SIZE;
        const frac = gp.grain_read_pos[i] - @as(f32, @floatFromInt(index_floor));

        const window_val = gp.grain_window[w_index_floor] * (1.0 - frac) + gp.grain_window[w_index_ceil] * frac;
        const value = gp.sample.data[index_floor] * (1.0 - frac) + gp.sample.data[s_index_ceil] * frac;

        result.L += value * window_val;
    }

    result.L /= @as(f32, @floatFromInt(GRAIN_COUNT));
```

## 18. Platform-Specific Directory Traversal — std.fs Cross-Platform Abstraction

The `populateDirectoryList` function uses `#ifdef _WIN32` to select between Win32 API (`FindFirstFile`/`FindNextFile`) and POSIX (`opendir`/`readdir`). Zig's `std.fs` provides cross-platform directory iteration that compiles to the correct syscalls per target OS, eliminating the need for manual `#ifdef` branches.

```c
// io.c:32-114
void populateDirectoryList(DirectoryList *list, const char *dirPath) {
#ifdef _WIN32
    WIN32_FIND_DATA findFileData;
    HANDLE hFind;
    char searchPath[MAX_PATH];
    snprintf(searchPath, MAX_PATH, "%s\\*", dirPath);

    hFind = FindFirstFile(searchPath, &findFileData);
    if(hFind == INVALID_HANDLE_VALUE) {
        perror("Failed to open directory");
        return;
    }

    do {
        if(strcmp(findFileData.cFileName, ".") == 0 || strcmp(findFileData.cFileName, "..") == 0) {
            continue;
        }
        char *filepath = (char *)malloc(MAX_PATH);
        // ... construct path, realloc list, add entry ...
    } while(FindNextFile(hFind, &findFileData) != 0);

    FindClose(hFind);
#else
    DIR *dir = opendir(dirPath);
    if(!dir) {
        perror("Failed to open directory");
        return;
    }

    struct dirent *entry;
    while((entry = readdir(dir)) != NULL) {
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char *filepath = (char *)malloc(1024);
        // ... construct path, realloc list, add entry ...
    }

    closedir(dir);
#endif
}
```

```zig
pub fn populateDirectoryList(
    allocator: std.mem.Allocator,
    list: *DirectoryList,
    dir_path: []const u8,
) !void {
    var dir = std.fs.cwd().openDir(dir_path, .{ .iterate = true }) catch return;
    defer dir.close();

    var iter = dir.iterate();
    while (try iter.next()) |entry| {
        if (std.mem.eql(u8, entry.name, ".") or std.mem.eql(u8, entry.name, "..")) {
            continue;
        }

        const filepath = try std.fs.path.join(allocator, &.{ dir_path, entry.name });
        errdefer allocator.free(filepath);

        list.file_paths = try allocator.realloc(list.file_paths, list.count + 1);
        list.file_paths[list.count] = filepath;
        list.count += 1;
    }
}
// Single code path — works on Windows, Linux, macOS. No #ifdef needed.
```

## 19. Element-by-Element Audio Processing — @Vector SIMD Batch Operations

The 8-bit PCM-to-float conversion loop processes interleaved audio samples one at a time. In a DAW, this inner loop runs millions of times per second and is a prime candidate for SIMD vectorization. Zig's `@Vector(N, T)` type enables batch processing of audio samples with SIMD instructions, replacing the element-by-element loop with a single vectorized operation.

```c
// voice.c:178-184 (within load_wav_sample, 8-bit branch)
        for(uint32_t i = 0; i < length; i++) {
            float value = 0.0f;
            for(uint16_t ch = 0; ch < header.numChannels; ch++) {
                value += (pcm_data[i * header.numChannels + ch] - 128) / 128.0f;
            }
            data[i] = value / header.numChannels;
        }
```

```zig
    // Vectorized 8-bit PCM-to-float conversion using @Vector.
    // Processes 4 samples at a time with SIMD on supported targets.
    const vec_len = 4;
    const offset128 = @as(@Vector(vec_len, u8), @splat(128));
    const scale128: @Vector(vec_len, f32) = @splat(1.0 / 128.0);

    var i: usize = 0;
    const simd_limit = (length / vec_len) * vec_len;
    while (i < simd_limit) : (i += vec_len) {
        const raw: @Vector(vec_len, u8) = pcm_data[i..][0..vec_len].*;
        const float_vec: @Vector(vec_len, f32) = @as(@Vector(vec_len, f32),
            @floatFromInt(raw - offset128)) * scale128;
        data[i..][0..vec_len].* = float_vec;
    }
    // Handle remaining samples scalar:
    while (i < length) : (i += 1) {
        data[i] = (@as(f32, @floatFromInt(pcm_data[i])) - 128.0) / 128.0;
    }
```

## 20. Makefile Build System — build.zig

C DAW projects use Makefiles or CMake to manage compilation, linking against audio libraries (e.g., `-lportaudio`, `-lsndfile`), and test execution. Build configuration is separate from the language and often fragile. Zig's `build.zig` consolidates compilation, linking, and test execution into a single build system written in Zig itself, with first-class support for cross-compilation and dependency management.

```makefile
# Makefile — referenced briefly
# CC = gcc
# CFLAGS = -Wall -Wextra -O2 -Isrc
# LDFLAGS = -lportaudio -lsndfile -lm -lpthread
#
# spectrax: src/main.o src/voice.o src/io.o src/dstruct.o
#     $(CC) -o $@ $^ $(LDFLAGS)
#
# test: tests/testVoice.o src/voice.o
#     $(CC) -o $@ $^ -lunity -lm
#
# .PHONY: clean
# clean:
#     rm -f spectrax test *.o tests/*.o
```

```zig
// build.zig — the entire build in one file:
const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "spectrax",
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = optimize,
    });

    exe.linkSystemLibrary("portaudio");
    exe.linkSystemLibrary("sndfile");
    exe.linkLibC();

    b.installArtifact(exe);

    // Tests are a first-class build step:
    const tests = b.addTest(.{
        .root_source_file = b.path("src/voice.zig"),
        .target = target,
        .optimize = optimize,
    });
    const run_tests = b.addRunArtifact(tests);
    const test_step = b.step("test", "Run tests");
    test_step.dependOn(&run_tests.step);
}
```

---

## Where to Go From Here

This guide covered 20 concrete translation points from C to Zig, using real code from the Spectrax DAW project. The patterns here — allocators with `defer`/`errdefer`, tagged unions with exhaustive `switch`, slices replacing pointer+length pairs, and `@Vector` for SIMD — are the ones you'll reach for most often when porting audio code.

For a deeper dive into Zig itself, start with the [official language reference](https://ziglang.org/documentation/master/) and the community-driven [Zig Learn](https://ziglearn.org/). The [Zig website](https://ziglang.org/) has installation instructions and a growing ecosystem of packages.

Spectrax is an open-source DAW project, and its source is available for study. The C-to-Zig translation effort documented here is ongoing — contributions and discussion are welcome. The journey from C to Zig isn't about abandoning what works; it's about keeping the control and performance of C while letting the compiler catch the mistakes that cost hours to debug.
