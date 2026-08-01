---
recommended_plan_path: .liteagent/plans/2026-05-28-c-to-zig-daw-blog.md
title: "From C to Zig: A DAW Developer's Guide — Blog & Hosting"
created: 2026-05-28
status: ready
revision: 3
---

# Plan: C-to-Zig Blog Post for Spectrax Developer

## Overview
Produce a blog-quality web page explaining 15–20 key differences between C and Zig, using actual code from the Spectrax DAW as "before" examples and idiomatic Zig 0.16 as "after." Host accessibly over the Tailscale network.

## Scope
- In: Auditing exactly 3 Spectrax source modules (dstruct.{c,h}, io.{c,h}, voice.{c,h}), writing 20 C→Zig translation pairs with explanatory prose, building a standalone index.html, a serve.sh script, Tailscale instructions (HOSTING.md), and a final verification pass.
- Out: Public deployment, domains, TLS, modifying Spectrax itself, any build pipeline.
- Constraints: No Node/npm. Python 3 available. Tailscale already running.
- Hard limit: If additional Spectrax source files are needed beyond the 3 named modules, that requires a scope change.

## Known Defects in Source Examples
| File | Lines | Bug | Classification |
|------|-------|-----|----------------|
| dstruct.c | 174–175 | n->data = malloc(dataSize) immediately overwritten by n->data = data | teaching-moment |
| dstruct.c | 179–181 | buildGraph() incomplete stub | exclude |
| io.c | 167 | length = header.subchunk2Size / (header.bitsPerSample / 4) — integer division wrong for non-8-bit | teaching-moment |
| io.c | 191, 214 | // free(data); commented out | teaching-moment |
| io.c | 32–114 | #ifdef _WIN32 / #else platform block for directory traversal | not a bug, translation target |

## The 20 Differences
1. Explicit allocators — dstruct.c:3–29 (createList) — std.mem.Allocator, .alloc()
2. Error unions + try — io.c:128–222 (load_wav_sample) — !T, error sets, try
3. defer / errdefer — io.c:129,143,173–176 (fopen/fclose pairs) — defer, errdefer
4. comptime replaces preprocessor — oscillator.h:8–12 (#define SAMPLE_RATE 44100) — comptime constants
5. Tagged union — exhaustive dispatch — voice.c:50–75 (freeVoice switch) — switch (v) { .fm => ... }
6. Tagged union — type-safe field access — voice.h:189–215 (struct Voice + union vd) — union(enum)
7. Tagged union — payload init by type — voice.c:255–294 (initialize_voice switch) — comptime type switching
8. Optional types (?T) — dstruct.c:4–8 (malloc+NULL guard) — ?*T, orelse, if (ptr) |p|
9. No header files — Every .h forward declaration — @import("module.zig")
10. No hidden control flow — voice.c:259 (voice->generate = generateBlep) — explicit function calls
11. Slices vs pointer+length — sample.h:37–42 (Sample.data + Sample.length) — []f32 with .len
12. Built-in test blocks — tests/testVoice.c (Unity) — test "..." { ... }
13. Safety-checked UB — voice.c:161–163 (manual bounds check) — automatic in Debug/ReleaseSafe
14. @import vs #include — voice.h chain — const mod = @import("modsystem.zig")
15. Switch as exhaustive expression — filters.c:13–26 (switch assigning processSample) — switch expression
16. Struct methods — oscillator.c:65–77 (createOperator) — fn Operator.init(allocator, ...)
17. for over slices with index — voice.c:155–158 — for (data, 0..) |sample, i|
18. Platform conditionals via build system — io.c:32–114 (#ifdef _WIN32) — builtin.os.tag
19. @Vector SIMD built-in — filters.c:44–59 (scalar biquad) — @Vector(4, f32)
20. Build system (build.zig) — Makefile — declarative build.zig

## Subtask DAG
1. codebase-audit (no deps) → audit-notes.md
2. setup-hosting (no deps) → serve.sh, HOSTING.md
3. diffs-list-finalize (depends: codebase-audit) → diffs-map.md
4. write-zig-translations (depends: diffs-list-finalize) → translations.md
5. build-webpage (depends: write-zig-translations) → index.html
6. verify-output (depends: build-webpage, setup-hosting) → verify-report.md

## DAG Waves
Wave 1 (parallel): codebase-audit, setup-hosting
Wave 2: diffs-list-finalize
Wave 3: write-zig-translations
Wave 4: build-webpage
Wave 5: verify-output

## Risk Register
R1: Zig 0.16 breaking changes — LOW — pin to exact release
R2: <15 clean examples after defect filtering — MEDIUM — supplement with well-known C audio idioms
R3: highlight.js Zig support inadequate — LOW — test early, fallback to manual CSS
R4: voice.c excerpts can't be self-contained — MEDIUM — reference types by name, add prose note
R5: C patterns with no Zig equivalent — LOW — audit flags, skip or show closest approach
R6: Tailscale IP changes — LOW — tailscale ip -4 in serve.sh, recommend MagicDNS
R7: python3 http.server missing — LOW — fallback to busybox httpd or ncat -l
