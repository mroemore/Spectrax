# The GUI phase: the sampler's surface, and the modulation screen

Date: 2026-11-04
Status: design, approved by the human partner in a mockup-driven brainstorm
Branch: `pattern-mod` (the consolidated branch — `main` + the ARM work + the sampler character engine's DSP)
Related documents: `docs/superpowers/plans/2026-11-02-adaptive-arm-handheld.md` (the canonical-core plan), `docs/superpowers/plans/2026-10-09-sampler-emulation-engine.md` (the character-engine plan), `docs/superpowers/plans/2026-10-09-sampler-emulation-engine-progress.md` (its ledger), `docs/superpowers/specs/2026-11-03-canonical-synthesis-spec.md` (the synthesis contract)

---

## Intent

The sampler's DSP landed: the bake, the machine profiles, the quantiser laws, the read path with the intended tuning, the root note, the two interpolators, the velocity's level. **None of it is reachable.** The instrument screen shows five dials — `SAMPLE`, `LOOP`, `START`, `END`, `PLAYBACK` — of which only `PLAYBACK` does nothing, and the ten controls that define the sampler's *character* do not exist as controls at all. The filter dials the character engine's parameters would bind are inert because the OTA cascade was deferred to this phase.

The modulation system has the opposite problem: it has grown a list on the instrument screen that takes a fifth of the screen and shows every source whether it is connected or not. On a 640×480 screen that list and the sampler's controls cannot both fit.

The phase does two things. It gives the sampler the surface its DSP deserves, and it moves the modulation system's full interface to a screen of its own so the instrument screen can show the instrument.

The principle the human partner set, and the one every decision below answers to: **immediacy decides placement.** A control you reach for while the instrument is playing belongs on the screen you are playing from; a control you set once can live behind a step. Where a choice had to be made between showing a control and showing something else, the control won, and the other thing shrank.

---

## What was decided, and why

Each decision below is the human partner's, from the mockup sessions of 2026-11-03 and 2026-11-04. The reasoning is recorded because the decisions are not obvious from the result.

| # | Decision | Reasoning as given |
|---|---|---|
| 1 | The modulation area on the instrument screen becomes **one line** that cycles through the connected modulators. | "immediacy of use should influence placement on the screen"; the alternative considered was a compact row per connection. |
| 2 | The line **cycles** — it is one slot, and the whole list is walked through it. | "ideally we could cycle through the whole list from that one mod slot". |
| 3 | A **dedicated modulation screen** carries the full interface. | "ideally we would have a dedicated modulation screen which shows a full featured mod interface". |
| 4 | The modulation screen is **two panes**: the source list on the left, the selected source's detail on the right. | "I think A is the right call" (of three drawn layouts). |
| 5 | The pattern track's **step editing stays on the pattern screen**; the modulation screen routes it. | "agreed". |
| 6 | The modulation screen shows a **preview of the recorded pattern** for reference. | "it would be nice to have a representation of the recorded pattern on the mod screen for reference". |
| 7 | The sampler's controls stay on the **instrument screen**, in two new rows. | "i would actually prefer to shrink the modulation than to forego controls on screen". |
| 8 | The loop points are set with a **cursor on the waveform**, not with dials. | "A but name the buttons 'start' and 'end'" (of two drawn interactions). |
| 9 | `FUNCTION`+`START`/`END` **selects** the point, or adds one if it does not exist. | as given. |
| 10 | `FUNCTION` then `EDIT` **deletes** the selected point. | "if we have either the button selected or the cursor selected, pressing the combo FUNCTION then EDIT deletes the cursor". |
| 11 | `SELECT`+`EDIT` opens a **context menu** on the selected point. | "it would be nice if we could access some additional options when a cursor is selected by pressing SELECT and EDIT". |
| 12 | The context menu holds exactly **two** items: *nearest zero crossing* and *crossfade length*. | "let's only add crossfade length from your list" — the zero-crossing action was the partner's own; of six proposed, only the crossfade was added. |
| 13 | The crossfade is **visualised on the waveform**. | "it should be visualised on the waveform too thouh". |
| 14 | The sequencer's velocity is edited in a **velocity lane** — a second grid under the note grid. | "velocity lane". |
| 15 | The instrument screen's bottom band **stays as it is**. | "1". |
| 16 | The crossfade is a **saved parameter**, so the preset format bumps to **V5**. | "thats fine", after being told a new instrument parameter means a V5 magic and a V4→V5 migration. |
| 17 | The modulation screen's destination picker is a **plain list of names**. | "plain list", against the alternative of pushing the instrument screen back up behind the picker. |
| 18 | The route lines are **redrawn from the modulation line** to the destination dials. | "3:a". |

---

## The screens

### 1. The instrument screen (the SAMPLE instrument)

**Measured facts about the screen as it is** (from a live capture at 640×480, and from the layout code):

- The screen is 640×480. The instrument screen's content occupies the top 396px; the band below (396–480) is the visualiser.
- The instrument screen's vertical layout, inside the 396px: the meta row (32px), the preset row (33px), the voice-type control panel, the `MODS`/`ADD` header (8px), the modulation list (195px).
- The meta row is `TPREV | TYPE | TNEXT | VOICES | pad`. `TPREV`/`TNEXT` cycle the **voice type**; `VOICES` is the per-channel voice count (1..8).
- The modulation list shows every source: the four core envelopes, the runtime sources, and one grouped `PTN` row with four `ROUTE` buttons.

**After this phase**, the SAMPLE instrument's control panel has four dial rows and the waveform, and the modulation list is gone from this screen:

| Row | Controls |
|---|---|
| `R_1` | `SAMPLE` (dial) · `LOOP` (dial) · `START` (button + value) · `END` (button + value) · `PLAYBACK` (dial) |
| `R_2` | `PAN` (dial) · `GAIN` (dial) · `VIEW` (button) · two blanks |
| `R_3` | `PROFILE` · `LAW` · `INTERP` · `BITS` · `RATE` — all discrete dials except `BITS`/`RATE` |
| `R_4` | `FILTER` · `CUTOFF` · `RES` · `TUNE` · `ROOT` — `FILTER` discrete, the rest continuous |
| `R_5` | the waveform, with the loop region, the crossfade and the points drawn on it |
| — | the modulation line, one row high |

`PROFILE`, `LAW`, `INTERP` and `FILTER` are discrete dials (`drawDiscreteDialGuiNode`); `BITS`, `RATE`, `CUTOFF`, `RES` and `TUNE` are continuous. `PROFILE`, `LAW`, `INTERP`, `BITS` and `RATE` bind the character engine's existing parameters (`profile`, `law`, `interp`, `bitDepth`, `sampleRate`); `FILTER`, `CUTOFF`, `RES`, `TUNE` bind the chain's `filterOn`, `cutoff`, `res`, `tune`; `ROOT` binds the `root` parameter added for the intended tuning.

`START` and `END` stop being dials. They become **buttons that carry the point's value** (`START 1.250`), because the point is what `FUNCTION` acts on and the point is moved on the waveform, not by turning a dial. The loop parameters remain the instrument's `loopStartIndex`/`loopEndIndex` — they are saved in the preset and can be modulated. Only their *presentation* changes.

**The modulation line.** One row, one slot. It is a **dial-shaped node**: on an ordinary node `LEFT`/`RIGHT` moves the cursor, on a dial it changes the value, and this node's value is the index into the list of connected modulators. It reads:

```
MODS   ENV1 → CUTOFF  0.75   OPEN →
```

- `LEFT`/`RIGHT` cycle through the connected modulators.
- The amount is **shown, not edited** here; the modulation screen edits it.
- `EDIT` opens the modulation screen, focused on the modulator the line is showing.
- The line is empty (`MODS   —`) when nothing is connected.
- The **route lines** are drawn from the modulation line to each destination dial that the *currently shown* modulator modulates. The lines were anchored to the modulation list, which is leaving this screen; anchoring them to the line instead keeps the same immediacy — the line shows you what the modulator you are looking at is doing, to the dials you can see.

### 2. The modulation screen

A **full-graph layer over the instrument screen**, not a scene in the global `SELECT`+`LEFT`/`RIGHT` cycle — it is part of the instrument, not a peer of it. The app's layer stack already routes input to the top layer first and the route picker and the clear-all confirmation already live there.

Two panes, the list about 40% wide and the detail 60%:

```
┌──────────────────────────────────────────────────────────────┐
│ ← BACK   MODULATION — FM1                          CH 1      │
├───────────────┬──────────────────────────────────────────────┤
│ SOURCES  +ADD │ ENV 1                          (the selected)│
│ ▸ ENV 1       │ ┌──────────┬──────────┐                     │
│   ENV 2       │ │ ATK 0.50 │ DEC 0.80 │                     │
│   LFO 1       │ │ CRV  ●   │ CRV  ●   │                     │
│   LFO 2       │ └──────────┴──────────┘                     │
│   RND 1       │                                              │
│   PTN 1…4     │ ROUTES                                       │
│               │ PITCH     0.50   ✕                           │
│               │ CUTOFF    0.75   ✕                           │
│               │ + ROUTE                                     │
└───────────────┴──────────────────────────────────────────────┘
```

- **Left pane**: every source — the core envelopes, the runtime sources, the pattern track — with `+ ADD` in the header. The selected source is highlighted. `DEL` acts on the selected source.
- **Right pane, upper**: the selected source's own parameters, exactly as the instrument screen's rows showed them (an envelope's `ATK`/`DEC`/`CRV` stages, an LFO's `RATE`/`SHAPE`, a random source's `RATE`/`SHAPE`).
- **Right pane, lower**: the source's routes, each with the destination, the amount, and a remove. `+ ROUTE` opens the destination picker.
- The **route picker** becomes a **plain list of destination names** in a small layer over this screen. (It was a ghost overlay with a button pinned over each routable dial's rectangle, so it lined up with the dials behind it; the modulation screen covers those dials, so there is nothing to point at.)
- The **clear-all confirmation** moves here too, triggered from the route list.
- The **pattern track** appears in the list. Its step editing stays on the pattern screen; the modulation screen shows a **preview of the recorded pattern** — the shape of the pattern's steps — in the detail pane, so you can see what you are routing.

The screen edits the **selected channel's** instrument, the same one the instrument screen shows.

### 3. The sampler's waveform: points

The waveform becomes focusable and holds **points**. A point is a position on the waveform that can be selected, moved, re-placed by an action, or deleted.

Three kinds: the **cursor**, the **loop start**, and the **loop end**. The loop start and end behave exactly like the cursor.

| Verb | Effect |
|---|---|
| `LEFT`/`RIGHT` | move the cursor one column of the current view |
| `SHIFT`+`LEFT`/`RIGHT` | move the cursor one frame |
| `FUNCTION`+`START` | select the loop start, or add one if it does not exist |
| `FUNCTION`+`END` | select the loop end, or add one if it does not exist |
| `FUNCTION` then `EDIT` | delete the selected point |
| `SELECT`+`EDIT` | open the context menu for the selected point |

- A **selected point follows the cursor**: `FUNCTION`+`START` both selects the start and jumps the cursor to it; from then on, moving the cursor moves the point. This is what makes it direct manipulation rather than a form.
- The cursor moves one *column* of the current view per press, so the existing `ZOOM` view mode becomes meaningful — zoom in and the step gets finer. `SHIFT` moves by a single frame.
- The **cursor is UI state**: it is not a parameter and is not saved. The **loop start and end are the instrument's parameters**, saved in the preset and modulatable. The green markers on the waveform are those parameters' indicators.
- `VIEW` still cycles `FIT` → `LOOP` → `ZOOM`.

**The context menu** holds exactly two things:

1. **`NEAREST ZERO CROSSING`** — put the point where the waveform crosses zero, so a loop does not click.
2. **`CROSSFADE`** — a **dial row**, not a plain item, because the app's instrument for a number is a dial. It sets the crossfade length in milliseconds.

### 4. The crossfade

The crossfade is a property of the **loop**, not of a point, so it reads the same whichever point the menu was opened on.

- **Unit**: milliseconds. **Range**: 0 to the loop's length. **Default**: 0.
- **Law**: equal-gain for correlated material, equal-power for uncorrelated drum hits and noise — the corpus's rule. Whether the loop's head and tail are correlated is **measured once**, when the loop points or the crossfade change, off the audio path.
- **Visualisation**: the loop region is shaded; the head and tail bands are shaded harder, with the fade curve drawn over the waveform across each; the value is written on the waveform (`XF 12ms`) as well as in the menu.

The corpus's law, verbatim: *"The crossfade must be at least one loop period. At the seam the two crossfading copies must be the same waveform or they cancel: a phase error of 0.5 cycles gives complete cancellation. A crossfade shorter than one loop period therefore imposes a fixed phase error and produces a periodic amplitude dip, audible as a thumping loop. Equal-gain linear fades for correlated material, equal-power sine/cosine fades for uncorrelated drum hits and noise. Below about 10 ms you want a zero-crossing-displaced or pitch-synchronous loop instead."*

### 5. The pattern screen's velocity lane

A **second grid under the note grid**, the same shape — one row per channel, one cell per step. Each cell draws a **bar whose height is that step's velocity**. The cursor moves into the lane to edit it.

The velocity already exists in the sequencer (`velocity[i]`, 1..127, zero meaning *unset, so full*); nothing edits it, so the MPC60's velocity-to-start-address behaviour cannot be heard at all. The lane is chosen over a dial on the selected step because a lane shows the velocity's *shape* across the pattern, and over editing the cell directly because the cell is already dense.

---

## The DSP that comes with it

The phase is not only a GUI change. Three of the sampler's pieces are inert or absent and their controls are part of this surface:

1. **The OTA cascade** (`filter`, `cutoff`, `res` are bound but inert). The corpus's topology: four one-poles in a global feedback loop, no `tanh`, no oversampling. Its coefficients come from the canonical math module (`canonicalExp`), never `expf` — the no-libm invariant.
2. **The crossfade** in the read path, with the equal-gain/equal-power law and the correlation measured per loop.
3. **The playback modes** — `SPT_FORWARD`/`SPT_REVERSE`/`SPT_PINGPONG` exist as an enum and are ignored today. `getSampleValueRev` is unreachable.
4. **The waveform preview must show the bake, not the source.** It draws the *source* sample's peak envelope today, and never uses an advance at all. What the voice plays is the **bake** — resampled to the profile's rate, truncated to the profile's bit depth, band-limited — so on a character profile the drawn signal and the heard signal are visibly different. The preview draws the baked material, so the display matches what is heard.

The corpus's scripted verification belongs with the OTA filter: the sweep at 4–8× the working rate with aliases appearing as reflected downward streaks, the THD+N by notch method, the impulse test counting four distinct poles in the decay, and the white-noise-through-envelope cutoff trajectory.

---

## Invariants

- **Criterion C is unchanged.** No synthesis operation may depend on the device, the architecture, the voice count, the channel count or the load. The crossfade's correlation measurement is a *load-time* property of the loop, not a per-sample decision, and the pattern preview and the waveform drawing are display.
- **The no-libm invariant.** The OTA cascade's coefficients come from the canonical math module.
- **The canonical engine rate.** Nothing here reads a runtime rate.
- **The app has no mouse.** Every interaction in this phase is reachable with the app's verbs: `LEFT`, `RIGHT`, `UP`, `DOWN`, `SELECT`, `START`, `EDIT`, `FUNCTION`, `NAV_LEFT`, `NAV_RIGHT`, `MOD_EXTRA`, `ADD`, `REMOVE`, `CTRL`, `SHIFT`, `EQUAL`, `MINUS`.
- **The app's idioms.** Dials turn with `LEFT`/`RIGHT`; action buttons fire on `EDIT`; the cursor moves between nodes with the arrows; `FUNCTION`+arrows is page navigation; `SELECT`+`UP`/`DOWN` cycles the instrument screen's channel.
- **Parameter discipline.** A parameter cleared by a preset or an instrument rebuild must be recreated (memory #1135). Every new parameter is created with its range, step and default, and is present in the preset format.
- **The preset format bumps to V5** for the crossfade, with a V4→V5 migration, because the crossfade is a saved instrument parameter. The format is a raw struct dump with a hand-migrated version chain, so the bump is a known shape of work and the tests must cover it.

---

## My calls

These are the builder's to revisit. Each states the cost of being wrong.

1. **The modulation screen is a layer, not a scene.** The global scene cycle (`SELECT`+`LEFT`/`RIGHT`) is arranger → pattern → instrument; a sub-screen of the instrument does not belong there. The layer stack already carries full-graph overlays with their own navigation. — *Cost if wrong:* a scene would need its own input branch in `main.c` and its own graph rebuild.
2. **The modulation line is dial-shaped and shows the amount without editing it.** The amount is edited on the modulation screen. The alternative — a two-axis widget — has no precedent in the app. — *Cost if wrong:* the instrument screen cannot tweak a route amount without a layer.
3. **The crossfade length is set with a dial row inside the context menu**, not with a dial on the instrument screen. — *Cost if wrong:* one more control on a screen that is already twenty dials deep.
4. **"One loop period" in the corpus's law is read as one period of the *material*, not of the loop.** The alternative reading makes the crossfade as long as the whole loop, leaving nothing to play. — *Cost if wrong:* the parameter's range and its default are wrong.
5. **The pattern's preview is drawn in the modulation screen's detail pane**, not in the narrow source row. — *Cost if wrong:* the preview is less visible.
6. **The voice types `GRAIN` and `SPECTRAL` are greyed out, not removed.** Their code stays; `GRAIN`'s granular buffer is already only allocated for a `GRAIN` voice (`voice.c:583`), so there is no memory work to do. — *Cost if wrong:* the code is harder to remove later.
7. **The budget shows in the meta row's `VOICES` dial**: its maximum becomes the device budget's per-channel voice count, the governor's narrowing shows as a reduced ceiling, and the type selector skips the two voice types that cannot make a sound. — *Cost if wrong:* the governor still drops voices with nothing on screen saying so.
8. **The instrument screen's bottom band stays at 83px.** The human partner's decision; the band's argument is that it is now the only modulation feedback left on that screen.
9. **The one-frame step uses `NAV_LEFT`/`NAV_RIGHT`, not `SHIFT`.** On the keyboard `SHIFT` *is* `SELECT` — both are `KEY_LEFT_SHIFT` (`input.h:45,55`) — and `SELECT`+`LEFT`/`RIGHT` is the global scene switch, evaluated before the scene's own input, so `SHIFT`+arrows would switch scenes instead of moving the cursor a frame. `NAV_LEFT`/`NAV_RIGHT` are bound on both devices (L1/R1 on a gamepad, Q/W on the keyboard) and used nowhere in the app. — *Cost if wrong:* the fine step is on the shoulder buttons rather than shift.
10. **The velocity lane mirrors the note grid** — one cell under each note cell, four steps per row, for the pattern being edited. The data forces it: the velocity is per *pattern* per step (`sequencer.h:24`), not per channel, and the note grid is four steps per row (`gui_pattern.c:41`); eight channels can share one pattern, so a per-channel lane would have nothing to draw. — *Cost if wrong:* the lane cannot show two channels' velocities at once.
11. **The crossfade is stored in milliseconds** and converted to frames at read time, so its audible length survives a change of the profile's bake rate. The loop points are stored in source frames; the crossfade is their sibling on the screen but not in the file. — *Cost if wrong:* the parameter, the preset and the DSP all change.
12. **`FUNCTION`+`START`/`END` is a chord** — hold `FUNCTION`, press the `START`/`END` *button* in row 1 — while `FUNCTION` then `EDIT` is a sequence. Buttons here fire on `EDIT` and `FUNCTION`+arrows is page navigation, so the chord is the reading that fits the app. — *Cost if wrong:* the two gestures have to be re-taught.
13. **The clear-all is triggered from the route list** — a `CLEAR ALL` at the end of the list — and the source list's row carries `DEL`. The old trigger was `FUNCTION`+`EDIT` on a source's `ROUTE` button, and the new source list has no such button. — *Cost if wrong:* the trigger moves again.

---

## Non-goals

- **No mouse or touch support.** The app has none, and this phase does not add any.
- **No new scene in the global cycle.**
- **No change to the song format.** The pattern's velocity lives in its own optional trailing chunk. The **preset** format does change, to V5, for the crossfade.
- **No new instrument-level modulation routing model.** The attenuation primitive, the source/destination model and the parameter machinery are as they are.
- **The ladder filter** (the SP-1200's transistor ladder) is not in this phase: it needs 2× oversampling and half-delay compensation, and the corpus is explicit that it is a different topology from the OTA cascade.
- **The loop micro-variation** (the corpus's ±0.2 dB per-pass drift) is not in this phase.

---

## What is left to the builder

- The order of the work, and the checkpoints. The dependency is fixed: the modulation list must leave the instrument screen before the sampler's panel can grow into its space. The safe sequence is to **build the modulation screen first, with the instrument screen's list still in place**, and only then switch the instrument screen over and remove the list — so that every step is shippable and the risky subtraction happens last, on a known-good screen.
- The exact node names, the layout weights, and the graph plumbing for each screen.
- The correlation measurement for the crossfade's fade law, and where it is cached.
- The exact form of the pattern preview.
- The V4→V5 preset migration, and the coverage for it.
- Whether the crossfade's law choice is exposed as a parameter or derived. (Derived is the plan's assumption; an override costs one more parameter.)
- The harness fixtures for each new interaction, and the coverage floors for the new code.
