# Mod Routing UX Round 2 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a real attenuator modulation node (lazy per-connection), fix scroll-to-visible, add scrollbar + coupling, deliver the picker-layer visual language, and the clear-all-routes confirm.

**Architecture:** MT_ATTEN inserted by addModulation (connection.source → attenuator, attenuator.input → real source); processModulations apply pass unchanged (attenuator shapes upstream). Scroll container reflows after offset changes; picker repins dest rects each frame; picker states keyed on a function-held flag.

**Tech Stack:** C99, raylib 5.6-dev, meson/ninja. Spec: `docs/superpowers/specs/2026-09-05-mod-routing-ux-2-design.md`.

## Global Constraints

- Branch `mod-sources-ux`, HEAD 835d531. Do NOT push (#1078).
- Test code in the main binary must be CLI-flag gated (#1136).
- After EVERY xvfb-run sweep orphans: `pkill -9 -x spectrax; pkill -9 -x instrument_harness; pkill -9 -x Xvfb; pkill -9 -f "[X]vfb :"` (#1137).
- No long timeout runs — fixtures self-exit; run them directly; long/uncertain runs go ASYNC (#1147).
- List-walk: children are `*(GuiNode **)e->data` over `e = node->items->head`, `node->itemCount` entries.
- Any new GuiNode field zeroed in initGuiNode (#1132/#1133).
- `meson install -C build` VERIFIED (visible output / bin mtimes).
- Fixtures run against the COMMITTED bin/s1.sng (stash boot-touched s1.sng first).
- Tabs, always braces, file style. Exact signatures below; verify against the file before coding.

---

### Task 1: MT_ATTEN node core

**Files:** `src/modsystem.h`, `src/modsystem.c`, `src/gui_inst_mod.c`
**Produces:** `MT_ATTEN` in ModType; Mod gains `Mod *input; Parameter *attenAmount; Parameter *attenPolarity; Parameter *attenCurve;`.

- [ ] Step 1: enum — add `MT_ATTEN` after `MT_OFS` in ModType (src/modsystem.h). Struct: add the 4 fields to `struct Mod` with a comment (amount shared with connection amount; polarity 0=bi 1=uni; curve 0=linear 1=curved).
- [ ] Step 2: `updateMod` (src/modsystem.c) switch — add `case MT_ATTEN: break;` (mirror MT_OFS, the stateless case). Add generate:
```c
static void modGenerateAtten(Mod *self) {
	if(!self->input || !self->input->output) {
		setParameterValue(self->output, 0.0f);
		return;
	}
	float v = getParameterValue(self->input->output);
	float amt = self->attenAmount ? getParameterValue(self->attenAmount) : 1.0f;
	v *= amt;
	if(self->attenPolarity && getParameterValueAsInt(self->attenPolarity) == 1) {
		v = fmaxf(v, 0.0f);
	}
	if(self->attenCurve && getParameterValueAsInt(self->attenCurve) == 1) {
		v = copysignf(powf(fabsf(v), 0.5f), v);
	}
	setParameterValue(self->output, v);
}
```
Wire MT_ATTEN → modGenerateAtten in the generate dispatch (find where MT_* map to generate functions).
- [ ] Step 3: `removeMod` — when `mod->type == MT_ATTEN`, free attenAmount/attenPolarity/attenCurve (removeFromParamList + freeParameter each, if non-NULL) before the output param.
- [ ] Step 4: `changeModType` (src/modsystem.c:437) rejects `newType == MT_ATTEN` (return false). `modTypeTag` (src/gui_inst_mod.c:906) add an MT_ATTEN case (unreachable — return "ATTN").
- [ ] Step 5: build 0 errors; `meson test -C build` 15 Ok / 0 Fail (unused yet). Commit: `feat(mod): MT_ATTEN attenuator node core (generate, update, remove)`.

### Task 2: Lazy insertion + ownership-aware cleanup

**Files:** `src/modsystem.c`, `src/modsystem.h`, + all removeModulation/removeModulationsForSource callers.
**Consumes:** Task 1. **Produces:** `Mod *createAttenuatorMod(ParamList*, ModList*, Mod*, const char*);`; `removeModulation(ParamList*, ModList*, Parameter*, Mod*)`; `removeModulationsForSource(ParamList*, ModList*, Mod*)` (signatures EXTENDED with ModList*).

- [ ] Step 1: `createAttenuatorMod` — create MT_ATTEN mod (use the real createMod/createLFO-style factory from the file; check its actual signature), set input, create attenAmount (1.0, 0..2), attenPolarity (0, 0..1), attenCurve (0, 0..1) params (name them `<source>_amt/_pol/_crv` — snprintf), set generate = modGenerateAtten, addToModList, return.
- [ ] Step 2: `addModulation` — after createConnection, if `source->type != MT_ATTEN`: create the attenuator; drop the connection's fresh amount param (`removeFromParamList(paramList, conn->amount)` + `freeParameter`) and set `conn->amount = atten->attenAmount; conn->source = atten;`. If source is already MT_ATTEN, reuse it (keep conn->source). Verify createConnection's actual field setup first.
- [ ] Step 3: `removeModulation` — extend signature with ModList*. Do NOT free conn->amount when `conn->source->type == MT_ATTEN` (atten owns it). Still free conn->type. After unlink+free, if the source was MT_ATTEN and `!attenStillInUse(list, atten)` → `removeMod(modList, list, atten)`. Add `static bool attenStillInUse(ParamList*, Mod*)` scanning all params' modulator chains.
- [ ] Step 4: `removeModulationsForSource` — same signature extension + same ownership rules (skip conn->amount free for MT_ATTEN sources; after the loop free the attenuator if unused).
- [ ] Step 5: update ALL callers (grep `removeModulation(` / `removeModulationsForSource(` in src/ + tests/) to the new signatures. Build 0 errors.
- [ ] Step 6: commit `feat(mod): lazy per-connection attenuator insertion + ownership-aware cleanup`. Do NOT re-pin test expectations yet (Task 3).

### Task 3: Re-pin feedback tests + new attenuator tests

**Files:** `tests/dsp/test_modsystem.c`
**Consumes:** Tasks 1-2.

- [ ] Step 1: The attenuator decouples the within-apply cascade. Key mechanics: the constGen* functions OVERWRITE their output every generate call, so the attenuators always read the const gen values; but a destination that previously read a param UPDATED EARLIER IN THE SAME APPLY pass now reads the attenuator's input (the source's pre-apply value). Expect: two_cycle A/B converge to a DIFFERENT fixed point (the original was (0.5, 0.5); the decoupled version reads B.out and A.out directly, likely (0.5, 0.25)). Derive + OBSERVE: add a temporary print of a/b/c currentValue for ~8 iterations, run, then pin the OBSERVED steady values deterministically (do NOT guess — read the numbers).
- [ ] Step 2: `test_two_cycle_feedback` — replace the 0.5/0.5 convergence asserts with the OBSERVED fixed point, keep finiteness + [0,1] clamps, update the top comment explaining the decoupling (attens read the source's pre-apply output).
- [ ] Step 3: `test_three_cycle_feedback` — same procedure (modList [A,B,C,C',B',A']); pin the OBSERVED steady values (may or may not equal the old 0.2/0.3/0.2 — derive from the apply order: a reads B', b reads C', c reads A' where A' reads A.output which the a-apply may have just written), keep clamps, update the comment.
- [ ] Step 4: `test_self_modulation` — verify still passes (self-ADD through own atten is identity); if not, re-pin with comment.
- [ ] Step 5: add six new tests (register in main()): `test_atten_identity` (default atten passes 0.25 through to a dest), `test_atten_amount` (amount 0.5 → 0.125; assert c->source->type == MT_ATTEN + setParameterBaseValue(c->amount, 0.5f)), `test_atten_polarity_uni` (bi lets a negative through; uni clamps to 0 — build a negative source by direct generate assignment), `test_atten_curve` (0.25 → 0.5 via sqrt), `test_atten_lazy_insertion` (c->source->type == MT_ATTEN && c->source->input == real source), `test_atten_cleanup` (two dests, remove one → atten survives; remove last → ml->count back to 1).
- [ ] Step 6: `meson test -C build` all green (15 + 6 = 21). Verify install. Commit: `test(mod): re-pin feedback cascade with attenuators + attenuator behavior suite`.

### Task 4: Keep the rest coherent — line matching, UI hiding, modStripColor

**Files:** `src/gui_inst_mod.c`, `src/vizfx.c`
**Consumes:** Task 2 (connections' source may be MT_ATTEN).

- [ ] Step 1: add `static bool connFromSource(ModConnection *c, Mod *src)` = `c->source == src || (c->source && c->source->type == MT_ATTEN && c->source->input == src)`. Use it in `drawRouteLinesNode`'s connection match, `cbRouteToDest`'s erase-mode check, and `syncRouteLinesOverlay` ctx population (verify each).
- [ ] Step 2: `appendModSourceEntry` skips MT_ATTEN mods (the source container iterates modList — attenuators are connection-internal; skip them so they never appear as source rows). `cbCycleSourceType`'s next-type computation skips MT_ATTEN.
- [ ] Step 3: `modStripColor` (vizfx.c) — add `case MT_ATTEN: return getColourScheme()->modStripDefault;`.
- [ ] Step 4: build + meson test all green (21). Fixture sanity: add_route_delete + mod_sources PASS (they exercise routes; the attenuator is transparent at defaults). Commit: `fix(mod): route matching follows attenuators; hide atten nodes from source UI`.

### Task 5: Scroll bug — reflow after offset change

**Files:** `src/graph_gui.c` (`scrollToVisible`)
**Consumes:** nothing.

- [ ] Step 1: at the end of `scrollToVisible`, when `target != sc->scrollOffset` BEFORE assignment, call `reflowCoordinates(&sc->base)` after storing the new offset (re-positions children with the new offset; draw-time scissor clips). Guard: skip the reflow if unchanged.
- [ ] Step 2: build; verify by unit: add `tests/dsp/test_graph_nav.c` a test creating a ScrollContainer with 8 rows of rowH 40 in a viewport h=120, append 8 children, call scrollToVisible on row 7's node, assert row 7's y is within [container.y, container.y+120] after reflow. Run green. Commit: `fix(graph): scrollToVisible reflows children so scroll actually moves`.

### Task 6: Scrollbar

**Files:** `src/graph_gui.c` (drawNode's scrollable branch or the container's draw), theme colors if needed
**Consumes:** Task 5.

- [ ] Step 1: in drawNode's ScrollContainer path, after drawing children, if `sc->contentH > (int)n->h`: draw a right-edge track + thumb: track = dim rectangle at x=n->x+n->w-6, y=n->y, w=4, h=n->h (colour = cs.defaultCell or a fixed grey); thumb h = max(8, n->h*n->h/sc->contentH); thumb y = n->y + (int)((n->h - thumbH) * sc->scrollOffset / (sc->contentH - n->h)). Colour = cs.dial. Inside the scissor. Non-interactive.
- [ ] Step 2: build + boot (fixture sanity: any instrument fixture PASS). Commit: `feat(graph): scrollbar on the mod container when content overflows`.

### Task 7: Picker/base scroll coupling

**Files:** `src/gui_inst_mod.c`, `src/gui_instrument.c`
**Consumes:** Task 5 (base scroll works).

- [ ] Step 1: add `static void syncPickerDestRects(void)` in gui_inst_mod.c: while the ROUTE picker layer is up (topmost non-passive layer named "ROUTE"), for each g_destCtx[i] (i < picker count) re-find the base dial rect via `findDialRectForParam(getSelectedInstGraph()->root, g_destCtx[i].dest, &r)` and re-pin the corresponding dest button (x/y/w/h) — call it from `gui_instrument_draw` BEFORE drawing the base graph (so both base + picker use scrolled positions that frame).
- [ ] Step 2: picker nav scroll: in the picker input path (gui_inst_mod.c input handling, where nav happens), after a nav move, if the newly selected dest's pinned rect is outside the base mod container viewport (find the container by name "mod_wrap" via findScrollContainerByName), adjust the base container's scrollOffset (clamp) via `scrollToVisible((ScrollContainer*)modwrap, syntheticNode)` where syntheticNode is a temp GuiNode whose x/y/w/h = the dest's base rect (set y = destRect.y + a fixed row height). Reflow the container. (syncPickerDestRects then repins next frame.)
- [ ] Step 3: build + boot + a scripted picker-nav check if feasible; else verify the mod_sources fixture still PASSes. Commit: `feat(gui): picker + base mod container scroll coupling`.

### Task 8: Ghost (preview) lines

**Files:** `src/gui_inst_mod.c` (the picker's draw-only overlay node — extend the existing label-overlay node)
**Consumes:** picker rects (Task 7's syncPickerDestRects).

- [ ] Step 1: extend the picker's full-screen draw node: for EVERY dest (g_destCtx[i], i < picker count), draw a desaturated faint line from the source ROUTE button anchor to the dest rect centre: colour = lerp(cs.routeAdd, grey(128,128,128), 0.5f) with alpha ~60; 2px. Draw UNDER the real route lines (the real lines draw in drawRouteLinesNode which runs above via the ROUTELINES layer — verify draw order: ghost in the picker layer, real lines in the ROUTELINES layer above).
- [ ] Step 2: build + probe warmpx >= 100. Commit: `feat(gui): desaturated ghost lines to every potential route destination`.

### Task 9: Selection border oscillation + function-held states

**Files:** `src/gui_inst_mod.c`
**Consumes:** Task 8.

- [ ] Step 1: add file-scope `static bool g_pickerFunctionHeld;` + `void guiSetPickerFunctionHeld(bool)` (decl in gui_inst_internal.h). Set from `gui_instrument.c` handlePresetUiInput before dispatching picker input: `guiSetPickerFunctionHeld(isKeyHeld(is, KM_FUNCTION))`; reset when no picker is up.
- [ ] Step 2: `drawRouteDestNode`: when selected:
  - default: outline oscillates green↔brighter green (lerp factor `0.5 + 0.5*sinf(GetTime()*4.0f)` between cs.labelSelected and a brighter mix).
  - if `g_pickerFunctionHeld`: routed (a connection from the picker's source to this dest exists — check via connFromSource on g_destCtx[i].dest->modulators with the picker source): outline oscillates bright↔dark RED (lerp cs.routeAdd toward (180,0,0)); draw text `tap <<km_edit>> to clear modulations for <<src_name_dest_name>>` inside, small 7px font, clipped by the scissor. Unrouted: outline static dim green + text `no active modulations.` in dim green.
  - routed-ness helper: `static bool destRoutedBy(Parameter *dest, Mod *src)` using connFromSource.
- [ ] Step 3: build + probe warmpx >= 100 + add_route_delete/mod_sources fixtures PASS. Commit: `feat(gui): picker selection green oscillation + KM_FUNCTION-held routed/unrouted states`.

### Task 10: KM_EDIT-held amount dial

**Files:** `src/gui_inst_mod.c`
**Consumes:** Task 9 (g_pickerFunctionHeld pattern — add `static bool g_pickerEditHeld;` same plumbing).

- [ ] Step 1: add `g_pickerEditHeld` set from handlePresetUiInput (`guiSetPickerEditHeld(isKeyHeld(is, KM_EDIT))`).
- [ ] Step 2: in the picker's draw node, when the selected dest is routed AND g_pickerEditHeld (and NOT g_pickerFunctionHeld): draw a semi-transparent dial centered on the dest showing the connection amount (getParameterValue(conn->amount), 0..2 range): reuse drawDiscreteDialGuiNode-style arc or drawDialGuiNode with a temp parameter context — simplest: draw a filled arc (DrawRing/DrawCircleSector) + value text, alpha ~150.
- [ ] Step 3: build + probe warmpx >= 100. Commit: `feat(gui): EDIT-held shows the routed destination's amount dial`.

### Task 11: Double-tap EDIT → atten editor panel

**Files:** `src/gui_inst_mod.c`, `src/gui_instrument.c` (input plumbing)
**Consumes:** Task 10.

- [ ] Step 1: double-tap detection: track `static int g_pickerEditTapFrame;` — on KM_EDIT just-pressed in the picker with the selected dest routed: if `currentFrameIndex() - g_pickerEditTapFrame < 20` (two frames apart) → open the editor; else record the frame. (currentFrameIndex exists in gui_instrument.c — expose via gui_inst_internal.h or use GetFrameTime-based tracking.)
- [ ] Step 2: editor = mini state machine in the picker input path while `g_attenEditorOpen`: 4 controls navigable (amount dial 0..2, curve LINEAR/CURVED, polarity BI/UNI, RESET). Arrows move focus; KM_EDIT fires the focused control (amount dial adjusts via arrows when focused; curve/polarity toggle; RESET sets amount=1, curve=0, polarity=0). KM_FUNCTION exits the editor back to the routing layer (g_attenEditorOpen=false). Controls drawn near the selected dest (small panel: 4 rows of ~20px) with the existing dial/button draw helpers.
- [ ] Step 3: build + probe warmpx >= 100 + fixtures PASS. Commit: `feat(gui): double-tap EDIT opens the per-connection attenuation editor`.

### Task 12: Clear-all-routes confirm (KM_FUNCTION+EDIT on base ROUTE)

**Files:** `src/gui_inst_mod.c`, `src/gui_instrument.c`
**Consumes:** Task 4 (connFromSource), the overwrite-modal pattern.

- [ ] Step 1: in the base-graph input path (gui_instrument.c handlePresetUiInput, KM_EDIT action branch): when the selected node's actionCb == cbOpenRouteLayer AND `isKeyHeld(is, KM_FUNCTION)` (and not the picker-up case) — instead of cbOpenRouteLayer, push a confirm layer: text `clear all modulations for <src_name>?` (src = modList->mods[sc->idx]->name), YES/NO buttons (NO selected by default). KM_EDIT fires selected; KM_SELECT == NO. YES → `removeModulationsForSource(paramList, modList, src)` (under g_audioLock + rebuilding flag) + rebuildInstrumentGraph + pop layer.
- [ ] Step 2: implement the confirm as a small dedicated layer (copy the overwrite-confirm layer structure from gui_instrument.c — Layer + YES/NO action buttons; NO default = first button selected).
- [ ] Step 3: scripted fixture `clear_routes.txt`: navigate to a source ROUTE button, KM_FUNCTION+EDIT → assert the confirm layer's top + NO selected; EDIT on YES → assert all the source's routes removed (modulators assert verb or preset count style). Add to the fixture gate. Commit: `feat(gui): clear-all-routes confirm (NO default) on KM_FUNCTION+EDIT`.

### Task 13: ADD button to the left of its row

**Files:** `src/gui_instrument.c` (createInstGraph mod header row)
- [ ] Step 1: swap the modHdr children so ADD is first (left), MODS label second. Adjust weights if needed. Build + boot.
- [ ] Step 2: commit: `style(gui): ADD button to the left of the mod header row`.

### Task 14: Full integration gate

**Files:** none (verification).
- [ ] Step 1: fresh `ninja -C build` 0 errors; `meson install -C build` VERIFIED; `meson test -C build` all green (15 + 6).
- [ ] Step 2: fixtures (committed s1.sng; stash bin/s1.sng + fm1.ipb drift first): chip_meta, preset_save_load, add_route_delete, task4_size_verify, mod_sources, save_overwrite, clear_routes → PASS. rm Xm1.ipb between fixtures.
- [ ] Step 3: `--probe-route` warmpx >= 100 (and zero PROBE without the flag). Boot clean (short, async).
- [ ] Step 4: orphan sweep. Commit any fixture changes.
- [ ] Step 5: whole-branch review dispatch (reviewer agent), then present merge options.
