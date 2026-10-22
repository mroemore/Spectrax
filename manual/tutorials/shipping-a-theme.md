# Shipping a theme

[Building a new component](building-a-component.md) built a widget out of
the styling system: classes, layouts, and the tests that keep them
honest. This tutorial is the other half — the **files**. Where the
config actually lives, which copy the app reads, what `meson install`
does to it, how a theme is authored and shipped, and what happens when
you get one of those wrong.

The reference for the styling *language* is the
[UI styling guide](../ui-styling.md). This is the reference for the
*plumbing*. Read the guide's §2, §3, §10 and §11 first if you have not;
this tutorial assumes you know that a style is a lookup, that colours
are tokens rather than hex values, and that nothing hot-reloads.

Everything below is reproducible on Void Linux with the repo checked out.
The commands are plain `sh`, `meson` and `gcc`, so they work on any
distro with meson, ninja and a C toolchain. Two things are worth
calling out:

- The config and data dirs follow the XDG base-directory spec
  (`~/.config/spectrax`, `~/.local/share/spectrax`), which is the same
  on every distro. A distro that does not set `XDG_CONFIG_HOME` falls
  back to `$HOME/.config`, which is exactly what the ladder in §2
  handles.
- The windowed checks need an X server. On a headless box (CI, a
  container, a bare SSH session) `run_scripted.sh` runs the harness
  under `xvfb-run` for you.

```
manual/tutorials/examples/
├── clr-ember.json    a variant theme in the canonical grouped form
├── clr-legacy.json   the same colours in the legacy flat form
└── cfg.json          a cfg.json that points at the ember theme
```

and one companion tool:

```
src/tools/theme_tool/theme_tool.c   resolve the config dirs, audit a
                                    theme file, show what the app
                                    writes back, report which classes
                                    the config binds
```

Build it:

```sh
meson compile -C build
./build/src/tools/theme_tool/theme_tool --help
```

---

## Contents

1. [The three places the config lives](#1-the-three-places-the-config-lives)
2. [How the app picks one](#2-how-the-app-picks-one)
3. [What `meson install` does, and does not do](#3-what-meson-install-does-and-does-not-do)
4. [The stale-config trap](#4-the-stale-config-trap)
5. [Authoring a theme](#5-authoring-a-theme)
6. [Which keys actually take effect](#6-which-keys-actually-take-effect)
7. [Colour-token discipline](#7-colour-token-discipline)
8. [Validation as a workflow](#8-validation-as-a-workflow)
9. [Hot reload: the honest version](#9-hot-reload-the-honest-version)
10. [Assets, fonts and the curve-icon atlas](#10-assets-fonts-and-the-curve-icon-atlas)
11. [The `text` style type](#11-the-text-style-type)
12. [Before you commit a config change](#12-before-you-commit-a-config-change)
13. [Gotcha table](#13-gotcha-table)
14. [What is still not covered](#14-what-is-still-not-covered)
15. [Where the pieces live](#15-where-the-pieces-live)

---

## 1. The three places the config lives

The guide's §1 says what the three files are for. This is where they
physically are in a checkout, and which one wins.

| Location | Holds | Read by |
|---|---|---|
| `src/layout.json` | the shipped **styles + layouts** | the shipped-config test (`test_layout`) |
| `bin/clr.json` | the shipped **theme** | the shipped-theme test (`test_theme`); the installer seeds the XDG dir from it |
| `bin/cfg.json` | the shipped **settings** | the installer seeds the XDG dir from it |
| `bin/layout.json` | a **copy** of the shipped styles | the instrument harness |
| `~/.config/spectrax/` | your **live** config | a default `./bin/spectrax` launch |
| `.` (the launch cwd) | whatever is there | only when no XDG config dir exists |

Two of those files are where the shipped defaults are *authored*; the
rest are copies. The rule that catches everybody out:

> **The repo is not a runtime config location.** `src/layout.json` and
> `bin/clr.json` are where the shipped defaults are *authored*. The app
> reads the XDG config dir. Editing the repo files changes nothing on
> screen until they are copied.

`bin/clr.json` is the odd one out: it is both the shipped theme *and*
the seed the installer copies into `~/.config/spectrax/`. There is no
`src/clr.json`.

Four legacy theme files are still in the tree and are read by nothing:
`bin/CLR.dat`, `bin/data/CLR_DEFAULT.dat` (a binary `CSC1` blob) and
`bin/data/colourscheme.txt`, `bin/data/colourscheme2.txt` (a CSV of
RGB triples). They are the pre-JSON scheme format. Delete them if you
are pruning; nothing will miss them.

## 2. How the app picks one

`src/paths.c` resolves two directories at startup, before any file is
read. `resolveConfigDir()` walks this ladder and stops at the first hit:

1. `--config-dir DIR`
2. `$XDG_CONFIG_HOME/spectrax` — **only if it contains `cfg.json`**
3. `$HOME/.config/spectrax` — **only if it contains `cfg.json`**
4. `.` (the current working directory)

`resolveDataDir()` walks the same shape, with two differences: the flag
is `--data-dir`, and the gate is **the directory existing**, not a
`cfg.json` inside it.

Ask the tool, which calls the app's own functions rather than
re-implementing them:

```sh
$ ./build/src/tools/theme_tool/theme_tool where
config dir candidates (first hit wins, src/paths.c):
  1. --config-dir                    (not given)
  2. $XDG_CONFIG_HOME/spectrax       (unset)
  3. $HOME/.config/spectrax          /home/krang/.config/spectrax                 cfg.json: yes   <- wins
  4. . (cwd)                         /home/krang/proj/Spectrax                    cfg.json: no
resolved config dir: /home/krang/.config/spectrax

data dir candidates (first hit wins, src/paths.c):
  1. --data-dir                      (not given)
  2. $XDG_DATA_HOME/spectrax         (unset)
  3. $HOME/.local/share/spectrax     /home/krang/.local/share/spectrax            dir: yes     <- wins
  4. . (cwd)                         /home/krang/proj/Spectrax                    dir: yes
resolved data dir: /home/krang/.local/share/spectrax

files the app reads from the config dir:
  /home/krang/.config/spectrax/cfg.json        present  (settings; names the theme file)
  /home/krang/.config/spectrax/clr.json        present  (theme: cfg.json names "clr.json")
  /home/krang/.config/spectrax/layout.json     present  (styles + layouts)
```

The two directories are used for different things and are resolved
independently:

- **The config dir** holds `cfg.json`, the theme file and
  `layout.json`. It is where your settings and colours live.
- **The data dir** holds samples, songs, presets and — through a
  symlink — `resources/`. The app `chdir()`s into it, so every
  *relative* asset path in a style config (`resources/images/dial2.png`)
  and every relative path in the theme (`resources/fonts/console.ttf`)
  resolves against it.

### Pointing the app at a config dir

`--config-dir` and `--data-dir` override the ladder outright. This is
the tool for trying a config without touching your live one:

```sh
./bin/spectrax --config-dir ~/trial-cfg --data-dir ~/proj/Spectrax/bin
```

**Use an absolute path.** The app reads the config dir relative to the
directory you launched from, but it `chdir()`s into the data dir
*before* it saves — and the save path is rebuilt from the same string
(`src/main.c`, the block after the main loop):

```c
snprintf(cfgPath, sizeof(cfgPath), "%s/cfg.json", data.configDir);
snprintf(clrPath, sizeof(clrPath), "%s/%s", data.configDir, data.settings->themeFile);
saveSettingsJson(cfgPath, data.settings, data.settings->themeFile);
saveThemeJson(clrPath, getColourScheme(), getFontConfig());
```

With `--config-dir bin --data-dir bin` (the dev command in the guide's
§2) the app ends up looking for `bin/cfg.json` *inside* `bin/`, finds
nothing, and silently saves nothing at all. The same happens with a
relative `--config-dir` and an absolute `--data-dir`: the settings land
in the data dir. A relative config dir is only safe when the data dir
is `.` too.

## 3. What `meson install` does, and does not do

`meson/install.sh` runs as meson's install script. It has three jobs,
and every one of them is careful not to overwrite anything.

**1. Copy the binaries into `bin/`.** An explicit allow-list — the app,
the two harnesses, the sample analyser and the visualiser. It is not a
wildcard, so a new tool is not installed unless you add it.

**2. Seed the XDG config dir.** Only files that are *missing* are
copied, and they are copied from `bin/`, not from `src/`:

```sh
for name in cfg.json clr.json layout.json; do
    if [ ! -e "$CONFIG_DIR/$name" ] && [ -f "$BIN/$name" ]; then
        cp "$BIN/$name" "$CONFIG_DIR/$name"
    fi
done
```

**3. Link the data dir.** `data`, `resources` and `s1.sng` become
symlinks into `bin/`, so the checkout stays the single source of truth.

Run it against a scratch `$HOME` and watch (the paths are elided):

```sh
$ HOME="$PWD/.tmp_files/fakehome" MESON_SOURCE_ROOT="$PWD" \
  MESON_BUILD_ROOT="$PWD/build" sh meson/install.sh
installed spectrax -> bin/
installed nav_harness -> bin/
installed instrument_harness -> bin/
installed inspectro_wavget -> bin/
installed vizulobe -> bin/
xdg: config cfg.json -> .../.config/spectrax/cfg.json
xdg: config clr.json -> .../.config/spectrax/clr.json
xdg: config layout.json -> .../.config/spectrax/layout.json
xdg: data data -> .../.local/share/spectrax/data
xdg: data resources -> .../.local/share/spectrax/resources
xdg: data s1.sng -> .../.local/share/spectrax/s1.sng
```

Run it a second time and there are no `xdg:` lines at all — the files
are already there. Edit the seeded `cfg.json` by hand and run it again:
your edit survives. **The installer never clobbers.**

The same is true of `bin/layout.json`: it is copied from
`src/layout.json` only when it does not exist yet.

```sh
if [ -f "$SOURCE_ROOT/src/layout.json" ] && [ ! -e "$BIN/layout.json" ]; then
    cp "$SOURCE_ROOT/src/layout.json" "$BIN/layout.json"
    echo "installed layout.json -> bin/"
fi
```

So the consequence is the one that costs people an afternoon:

> **After the first install, `src/layout.json` is frozen.** Edit it,
> and neither `bin/layout.json` nor `~/.config/spectrax/layout.json`
> moves. The shipped-config test still passes — it compiles
> `src/layout.json` directly — so the suite stays green while the app
> runs the old file.

## 4. The stale-config trap

Two copies of the same idea are in play, and they drift apart:

- the **shipped** config, which the tests validate, and
- the **live** config, which the app reads.

On this machine they have already drifted. The shipped theme covers all
47 canonical colour paths; the live one does not:

```sh
$ ./build/src/tools/theme_tool/theme_tool check bin/clr.json
theme: bin/clr.json
  schema: declared
  font: resources/fonts/console.ttf size=9 spacing=1
  colours: 47 canonical keys, 47 provided, 0 missing, 0 unknown, 0 legacy alias
  OK: the theme covers every canonical colour path

$ ./build/src/tools/theme_tool/theme_tool check ~/.config/spectrax/clr.json
theme: /home/krang/.config/spectrax/clr.json
  schema: declared
  font: resources/fonts/console.ttf size=9 spacing=1
  colours: 47 canonical keys, 46 provided, 1 missing, 0 unknown, 0 legacy alias
    missing:  text.disabled
  FAIL
```

`text.disabled` was added to the palette after that live theme was
written. The app still works — `initDefaultColourScheme()` seeds every
field before the theme is loaded, so the missing key keeps the built-in
default — but the file on disk no longer describes what the app is
using, and the next time the app quits it will add the key.

The same trap applies to `layout.json`, and it is worse there, because
a missing class is a **silent fallback** at runtime: the widget drops to
the class named after its type, or to the baked default, and nothing
complains.

Three ways out, in order of preference:

1. **Run against the file you edited.** `--config-dir` with an absolute
   path. Nothing else on the machine is touched.
2. **Copy it into the live config dir.** `cp src/layout.json
   ~/.config/spectrax/layout.json`. Remember the app will rewrite
   `clr.json` on exit, but it never rewrites `layout.json`.
3. **Delete the live copy and re-install.** The installer re-seeds the
   missing file from `bin/`. This is how you get back to the shipped
   look.

## 5. Authoring a theme

A theme is one JSON file. It has two parts:

```jsonc
{
  "schema": 2,
  "font": { "path": "resources/fonts/console.ttf", "size": 9, "spacing": 1 },
  "colors": {
    "surface": { "window": "#101820FF", ... },
    "text":    { "primary": "#E8F1F8FF", ... },
    "chip":    { "palette": [ "#2E6C82FF", ... ] }
  }
}
```

### 5.1 The canonical shape

`schema: 2` means the **grouped** form: the colours are nested by what
they are for — `surface`, `text`, `line`, `cell`, `control`, `waveform`,
`sample`, `arranger`, `mod`, `chip`. The nesting is sugar. The key is
the dotted path, `surface.panel`, whether you write it nested or flat.

The file name is **not** fixed. `cfg.json` names it:

```jsonc
"theme": "clr-ember.json"
```

and the app resolves that against the config dir, so
`"theme": "themes/ember.json"` works too, as long as the directory
exists. The default is `clr.json` when the key is absent.

### 5.2 `chip.palette`

The one array in the theme. It is the eight chip identities, in order:

```jsonc
"chip": {
    "palette": [
        "#2E6C82FF", "#3F8C6CFF", "#8C6C2EFF", "#6C3F8CFF",
        "#2E8C8CFF", "#8C3F5AFF", "#5A8C3FFF", "#8C8C3FFF"
    ]
}
```

Do not confuse it with the *style* side of the same name.
`layout.json` has:

```jsonc
"chip": {
    "palette": [ "chip.palette0", "chip.palette1", ..., "chip.palette7" ]
}
```

**One is hex, one is token names.** The theme holds the colours; the
style says which colour each chip slot uses. The style's array is
indexed by the channel's palette index, modulo eight.

### 5.3 Legacy flat keys

Any theme written by an earlier build used the historical field names
as flat keys: `background`, `panel`, `valueDisplayBg`, `layerDim`,
`font`, `secondaryFont`, `label`, `labelSelected`, `valueText`,
`arrangerCellText`, `panelBorder`, `wrapperBorder`, `outline`, `vline`,
`stepBorder`, `defaultCell`, `blankCell`, `highlightedCell`,
`selectedCell`, `dial`, `reddish`, `waveformBg`, `waveform`,
`waveformAlt`, `poly`, `spectrogramPlayhead`, `sampleBg`, `sampleAltBg`,
`sampleBorder`, `stepClosed`, `arrangerPlayhead`, `modStripLfo`,
`modStripEnv`, `modStripRnd`, `modStripOfs`, `modStripDefault`,
`routeAdd`, `routeMul`, `chipPalette0` … `chipPalette7`.

They all still load, through the alias table in `src/io/config_io.c`.
`manual/tutorials/examples/clr-legacy.json` is a full theme written
that way; the tool tells you what it resolves to:

```sh
$ ./build/src/tools/theme_tool/theme_tool check manual/tutorials/examples/clr-legacy.json
theme: manual/tutorials/examples/clr-legacy.json
  schema: declared
  font: resources/fonts/console.ttf size=9 spacing=1
  colours: 47 canonical keys, 46 provided, 1 missing, 0 unknown, 46 legacy alias
    missing:  text.disabled
    legacy:   background -> surface.window
    legacy:   panel -> surface.panel
    ...
    legacy:   chipPalette7 -> chip.palette7
  FAIL
```

Two things to notice:

- **The flat form is lossy.** `text.disabled` has no legacy alias —
  it was added after the rename — so a flat theme simply cannot express
  it. The app fills it in with the built-in default and writes it back
  in the grouped form on the next quit. The two example themes differ
  in exactly that one key:

  ```sh
  $ ./build/src/tools/theme_tool/theme_tool canon manual/tutorials/examples/clr-legacy.json -o .tmp_files/a.json
  $ ./build/src/tools/theme_tool/theme_tool canon manual/tutorials/examples/clr-ember.json  -o .tmp_files/b.json
  $ diff .tmp_files/a.json .tmp_files/b.json
  22c22
  < 			"disabled":	"#6E6060FF"
  ---
  > 			"disabled":	"#46555FFF"
  ```

- **`colors.font` is not the font block.** The top-level `"font"` object
  is the font configuration. The *legacy colour* named `font` lives
  inside `"colors"` and means `text.primary`. They are different keys in
  different objects, and the flat form is the only place the collision
  is visible.

**Write new themes in the grouped form.** The flat form only exists so
that old files keep loading.

### 5.4 What the app writes back on exit

The app saves the theme when it quits:

```c
saveThemeJson(clrPath, getColourScheme(), getFontConfig());
```

`saveThemeJson()` walks the same canonical-path table the loader uses,
so it cannot drift from it. The consequences:

- The file is rewritten in the **grouped `schema: 2` form**, tab
  indented, with every colour as `#RRGGBBAA`.
- It is rewritten **whole**. The in-memory scheme is the seeded
  defaults with your file overlaid, so a key you omitted comes back as
  the built-in default, and a key the app does not know about is
  **dropped**.
- Your comments and key order are gone. It is a data file, not a
  document.

`theme_tool canon` calls the very same writer, so you can see the
result without starting the app:

```sh
$ ./build/src/tools/theme_tool/theme_tool canon bin/clr.json | head -12
{
	"schema":	2,
	"font":	{
		"path":	"resources/fonts/console.ttf",
		"size":	9,
		"spacing":	1
	},
	"colors":	{
		"surface":	{
			"window":	"#CF6E3AFF",
```

**Edit the theme while the app is closed.** If the app is running when
you edit, your edit is overwritten by the in-memory scheme when it
quits.

The rewrite is not new behaviour — it is how the flat form got into
the tree in the first place. `bin/clr.json` was flat until commit
`060aa20` migrated it by hand; before that commit the writer emitted
that flat shape on every quit.

### 5.5 Shipping it

To ship the ember theme:

1. Copy it next to your other config files:
   `cp manual/tutorials/examples/clr-ember.json ~/.config/spectrax/`
2. Point `cfg.json` at it: `"theme": "clr-ember.json"`.
3. Restart the app.

To ship it to a *machine*, `meson install` will not do it — the
installer only seeds the three default names. Copy the theme file and
edit `cfg.json` on the target, or bake the new default into `bin/`
before the install runs.

## 6. Which keys actually take effect

Three files, and not every key in them is wired to anything. This
section is the map, and it is the part that decides whether an edit you
just made shows up on screen.

### The theme: every key counts

`clr.json` is read key by key. Every canonical colour path in it is
applied to the scheme, and the scheme is what every style resolves
against. **A theme key always takes effect** — the only ways a theme
key can fail are the silent ones in §7 (unknown key, transparent
value, malformed value).

### The style config: only the classes that are bound

`layout.json` is different. A class is a **lookup**: a widget asks for
it by name. A class that no widget asks for is never consulted — the
widget falls back to the type's *baked* default.

Ask which classes the config binds:

```sh
$ ./build/src/tools/theme_tool/theme_tool bound src/layout.json
layout: src/layout.json
  layout env-stage                binds: stage-rate, stage-curve-icon
  11 classes defined
  bound by a layout's childClasses: stage-rate stage-curve-icon
  used only as an extends ancestor: dial, stage-curve
  not bound by anything: dial-discrete, btn, type-label, step-cell, route-dest, chip, text
  note: a class can also be bound in C with guiNodeSetClass();
        the config cannot see that.
```

In the shipped app nothing binds a class in C either. `guiNodeSetClass()`
is called from exactly two places:

```sh
$ grep -rn "guiNodeSetClass" src/*.c
src/graph_gui.c:199:void guiNodeSetClass(GuiNode *gn, const char *className) {
src/gui_core.c:442: * guiNodeSetClass (which handles NULL). Returns NULL if createGuiNode
src/gui_core.c:452:		guiNodeSetClass(gn, className);
src/gui_style.c:51:			guiNodeSetClass(child, ld->childClasses[i]);
```

One of them is `applyLayout()` applying a layout's `childClasses` — the
config-driven path. The other is `createTextGuiNode()`, and no
`src/*.c` file calls that factory.

So the classes in force are `stage-rate` and `stage-curve-icon`, plus
their `extends` ancestors `stage-curve` and `dial`. Every other widget
in the app — the buttons, the chips, the step cells, the type labels,
the route slots, the discrete dials — has no class of its own.

**What has changed since this section was written.** The resolver now
falls back to the class *named after the type* before reaching for the
baked default:

```c
const DialStyle *resolveDialStyle(const GuiNode *gn) {
	int idx = -1;
	if(gn && gn->className) {
		idx = findClass(gn->className, STYLE_DIAL);
	}
	if(idx < 0) {
		idx = findTypeClass(STYLE_DIAL);   /* the class named "dial" */
	}
	if(idx >= 0) return &g_dialClasses[g_classMap[idx].index];
	return &g_defaultDial;                 /* the BAKED default */
}
```

`createDialGuiNode()` still never sets a class name, but the dial now
resolves to the JSON entry named `dial` rather than to `g_defaultDial`.
That is what makes the per-type entries live — and it is why the layer
of indirection worth understanding is the *class name*, not whether one
was set.

You can watch it happen. From the repo root, make the shipped `dial`
class bigger and resolve a dial with and without the class:

```sh
$ sed 's/"size": 20,/"size": 30,/' src/layout.json > probe.json
$ cat > probe.c <<'EOF'
#include <stdio.h>
#include <string.h>
#include "gui_style.h"
#include "graph_gui.h"
#include "theme.h"

int main(void) {
	ColourScheme cs;
	memset(&cs, 0, sizeof(cs));
	if(!compileLayoutConfig("probe.json", &cs)) { return 1; }
	GuiNode *n = createBlankGuiNode();
	const DialStyle *a = resolveDialStyle(n);
	printf("classless: knob.size %d  asset '%s'\n", a->knob.size, a->knob.assetPath);
	guiNodeSetClass(n, "dial");
	const DialStyle *b = resolveDialStyle(n);
	printf("class dial: knob.size %d  asset '%s'\n", b->knob.size, b->knob.assetPath);
	return 0;
}
EOF
$ gcc -I include -I src -I third_party/cjson probe.c \
    build/src/libspectrax_core.a build/libspectrax_cjson.a \
    -L lib/linux -lraylib -lm -o probe
$ ./probe
classless: knob.size 30  asset 'resources/images/dial2.png'
class dial: knob.size 30  asset 'resources/images/dial2.png'
```

Since the type-key fallback landed, the classless dial resolves to the
same entry as the explicitly-classed one. (Before it, the classless
dial stayed at 20 px and the `dial` entry never reached it.) A
`"stage-rate"` dial, which *does* inherit from `dial`, still uses its
own size — an explicit class always wins over the type-name fallback.

### What to edit, then

- **To change the whole app's look, change the theme** for anything the
  config does not name, or **edit the per-type class** (`"dial"`,
  `"btn"`, `"chip"`, …) for that style type. Both reach every widget of
  that type.
- **To change one widget, bind a class to it** — through a layout's
  `childClasses`, or in C with `guiNodeSetClass()`. An explicit class
  wins over both the type-named entry and the theme.
- Remember the ordering: **explicit class → class named after the type
  → baked default.**

Those entries are not junk. `test_layout` requires one per style type,
they document the schema, and the shipped ones mirror the baked
defaults so the app looks identical either way. They are what you edit
to restyle a whole widget type without touching C.

## 7. Colour-token discipline

A style never carries a hex value. It carries the **name** of a theme
token:

```jsonc
"macro-hint": { "extends": "text", "color": "text.disabled" }
```

and the theme carries the hex. That split is what lets you re-skin the
whole app by swapping one file, and it is why the token table is
fixed: `themeFieldByName()` in `src/io/config_io.c` is the only
mapping from a name to a colour, and it is consulted at **compile
time**, when `compileLayoutConfig()` resolves every style's colour
against the theme that was loaded at startup.

The failure modes are all silent, and they are the reason the validators
exist:

| You write | What happens |
|---|---|
| a hex value in a style (`"#FF00FF"`) | ignored; the inherited colour survives |
| a token that does not exist (`"text.prmary"`) | ignored; the inherited colour survives |
| a token that exists but is misspelled in the **theme** | the theme key is ignored; the built-in default survives |
| a theme colour with alpha `00` | treated as **unset**; the default survives |
| a theme value that is not `#RRGGBB[AA]` | ignored; the default survives |
| a theme key the app does not know | ignored on load, **dropped** on the next save |

None of these print anything. The theme-side ones are what
`theme_tool check` is for:

```sh
$ ./build/src/tools/theme_tool/theme_tool check broken.json
theme: broken.json
  schema: declared
  font: resources/fonts/console.ttf size=9 spacing=1
  colours: 47 canonical keys, 3 provided, 44 missing, 1 unknown, 0 legacy alias
    missing:  surface.inset
    missing:  surface.dim
    missing:  text.secondary
    ...
    missing:  chip.palette7
    unknown:  text.nope
  FAIL
```

The style-side ones are what `test_layout` is for — it checks every
colour value in the shipped `src/layout.json` against the token table.

## 8. Validation as a workflow

Two validators ship with the project, and they are the pre-commit gate.

```sh
meson compile -C build
meson test -C build test_theme   # the shipped theme
meson test -C build test_layout  # the shipped style config
meson test -C build               # the whole suite
```

`test_theme` compiles the **shipped** `bin/clr.json` into the path
table: every canonical path must resolve, the file must supply every
one of them, the writer must round-trip, the legacy flat keys must
still load, and unknown or zero-alpha keys must write nothing.

`test_layout` compiles the **shipped** `src/layout.json` and fails on
anything the parser would otherwise ignore: a class that does not
classify, an `extends` target missing from the same file, a colour that
is not a theme token, a `childClasses` entry naming an unknown class, a
`weights`/`childClasses` length mismatch, or a missing per-type default
class.

Both are *shipped-config* guards. They say nothing about **your**
theme file, which is what `theme_tool check` is for:

```sh
./build/src/tools/theme_tool/theme_tool check ~/.config/spectrax/clr.json
```

It exits non-zero when a canonical key is missing, a key is unknown, a
value is transparent, or a value is not a hex colour. That makes it
usable as a gate in a script or a pre-commit hook.

The tool also has its own self-test, wired into the suite:

```sh
$ meson test -C build
 7/19 spectrax:test_theme                OK              0.02s
 ...
14/19 spectrax:theme_tool_selftest       OK              0.02s
```

### Windowed check

The instrument harness boots the **shipped app** against
`bin/layout.json` and drives its own graph, so it verifies the shipped
config end to end — classes, layouts, geometry and all:

```sh
$ src/tools/instrument_harness/run_scripted.sh fixtures/env_stage.txt
...
PASS
```

`run_scripted.sh` runs the harness under Xvfb with its working
directory set to `bin/`, which is why it finds `bin/layout.json` and
`bin/resources/`. The harness only *reads* `bin/s1.sng`; it is the app
that rewrites that file when it quits. The harness does write
instrument presets, and `run_scripted.sh` removes the one the preset
fixture creates both before and after the run.

Tutorial 1 could not use the harness for its demo, because the harness
drives the app's own graph and the demo's widget is not in it. For the
**shipped** config the harness is the right tool, and this is the piece
Tutorial 1's coverage table lists as "not covered".

## 9. Hot reload: the honest version

**Nothing re-reads the JSON.** There is no file watcher, no SIGHUP
handler, no re-read on focus. The config is read once, at startup, and
the styles it produces are constants for the life of the process:

```
cfg.json                     settings + theme filename
  ↓
initDefaultColourScheme()    seed every colour key
loadThemeJson(clr.json)      overlay the theme
  ↓
compileLayoutConfig(layout.json, cs)
  ↓  colours are resolved to RGBA *now* — they are baked into the style structs
InitGUI()                    builds the node tree; draw fns resolve styles
  ↓
finalizeStyles()             loads knob textures named by `knob.asset`
  ↓
draw loop                    styles are constants; nothing re-reads JSON
```

So the answer to "how do I see my change?" is **restart the app**. For
the harness, "restart" means re-run the fixture — it boots a fresh
process each time, so it always picks the current `bin/layout.json`.

Two consequences of "the styles are constants" that are worth knowing
before you write a tool that tries to be clever:

**Colours are baked at compile time.** A style's `color` field holds an
RGBA value, resolved when `compileLayoutConfig()` ran. Changing
`clr.json` cannot change a colour in a running process, even if you
could get the file re-read.

**A second compile replaces everything.** `compileLayoutConfig()`
resets the class registry, every per-type entry count and the layout
table on every call. A second call does not merge with the first — it
*replaces* it. From Tutorial 1's self-test:

```c
CHECK(compileLayoutConfig(path, &cs), "compile the hex fixture");
CHECK(styleClassType("macro-amount") < 0, "the second compile wiped the class registry");
CHECK(layoutByName("macro-bank") == NULL, "the second compile wiped the layout table");
```

Anything holding a `const DialStyle *` or a `const LayoutDef *` across
a recompile is holding a pointer into a registry that no longer
exists. Re-resolve it. The app and the harness compile exactly once,
so this only matters if you add a reload path — and if you do, this is
the contract you have to honour.

## 10. Assets, fonts and the curve-icon atlas

### Knob textures

`knob.asset` is a path to a PNG, resolved against the **process working
directory** at the moment the texture is loaded — which, for the app,
is the data dir, because the app `chdir()`s there before `InitGUI()`:

```jsonc
"knob": {
    "size": 20,
    "radius": 10,
    "asset": "resources/images/dial2.png"
}
```

So the path is `resources/images/...` relative to
`~/.local/share/spectrax`, which the installer symlinks to
`bin/resources`. The texture loads once, in `finalizeStyles()`, after
the window exists:

```c
for(int i = 0; i < g_dialClassCount; i++) {
    if(g_dialClasses[i].knob.assetPath[0] && g_dialClasses[i].knob.assetTex.id == 0) {
        g_dialClasses[i].knob.assetTex = LoadTexture(g_dialClasses[i].knob.assetPath);
    }
}
```

A failed load is silent: the texture id stays 0 and the draw function
falls back to the procedural disc and arc. A typo in the path is
therefore indistinguishable from a missing file — both give you the
fallback knob.

### The curve-icon atlas

There is no file for it. `buildCurveIconTexture()` draws the 32 curve
glyphs into a texture at startup:

```c
#define CURVE_ICON_COUNT 32
#define CURVE_ICON_SIZE 8
```

32 frames of 8×8 pixels, generated from the curve shapes themselves.
The style opts in with `curveIcon.frameCount`, and the frame is picked
from the parameter's normalised value:

```jsonc
"stage-curve-icon": {
    "extends": "stage-curve",
    "curveIcon": { "frameCount": 32, "size": 12, "color": "text.value" }
}
```

There is no per-frame JSON and no way to substitute your own atlas.
`frameCount` below 32 collapses the top of the value range onto the
last frame, so use 32 for a 1:1 mapping.

### Fonts

Three font slots exist, and **only one of them is themeable**:

| Slot | Loaded from | Themeable |
|---|---|---|
| pixel | `font.path` / `font.size` in the theme | yes |
| text | `resources/fonts/setback.png` (hard-coded) | no |
| symbol | `resources/fonts/iconzfin.png` (hard-coded) | no |

A style picks a slot by name:

```jsonc
"note": { "font": "text", "fontSize": 0, "spacing": 4, ... }
```

`"text"` and `"symbol"` select the hard-coded fonts; anything else —
including `"pixel"` and a typo — falls through to the pixel font. The
font file path is resolved against the data dir like any other asset.

`font.spacing` is parsed and then ignored: each label style carries its
own `spacing`.

## 11. The `text` style type

`text` is the type for static captions. Tutorial 1 §14 covers the
alignment; the rest of the type is here.

```jsonc
"text": {
    "font": "pixel",
    "fontSize": 10,
    "spacing": 1,
    "offsetX": 0,
    "offsetY": 0,
    "hAlign": "left",
    "vAlign": "top",
    "color": "text.primary",
    "colorSelected": "text.primary"
}
```

- `hAlign` is `left`, `center` or `right`; `vAlign` is `top`, `middle`
  or `bottom`. The parser maps `left|top → 0`, `center|middle → 1`,
  `right|bottom → 2`, so the two scales share a number — `"center"` is
  a valid `vAlign` and means "middle".
- Alignment is computed against the node's own `w`/`h`, and the offset
  is added *after*.
- **Text nodes are non-selectable.** `createTextGuiNode()` builds the
  node with `selectable = 0`, so navigation skips it and it never takes
  the cursor. `colorSelected` is unreachable for them, and the shipped
  config sets it to the same value as `color` to say so.
- There is no fill and no border. A text node draws text and nothing
  else; if you want a box, use a `btn` or a `step-cell`.

## 12. Before you commit a config change

1. **Validate the theme.**
   `./build/src/tools/theme_tool/theme_tool check <file>` — exits
   non-zero on a missing key, an unknown key, a transparent value or a
   malformed value.
2. **Validate the styles.**
   `meson compile -C build && meson test -C build test_layout` — this
   compiles the shipped `src/layout.json` and fails on anything the
   parser would silently ignore.
3. **Validate the theme as shipped.**
   `meson test -C build test_theme` — the shipped `bin/clr.json` must
   cover every canonical path.
4. **Run the whole suite.** `meson test -C build` — 19 tests, about
   three seconds.
5. **Look at it.** `src/tools/instrument_harness/run_scripted.sh
   fixtures/env_stage.txt` for the shipped config.
6. **Restore what the app rewrote.** The app rewrites `bin/s1.sng` and
   your theme file when it quits. If a run touched the song:
   `git show HEAD:bin/s1.sng > bin/s1.sng`.
7. **Remember the copies.** A change to `src/layout.json` does not
   reach the app until you copy it into the config dir, or run with
   `--config-dir`.

## 13. Gotcha table

| Symptom | Cause |
|---|---|
| A style change does nothing | the app is reading `~/.config/spectrax/layout.json`, not your edit (§4) |
| `meson install` did not update `bin/layout.json` | it only copies when the file is missing (§3) |
| `meson install` did not update your config | it only seeds missing files, and never clobbers (§3) |
| The app started with the wrong colours | a stale theme file in the live config dir (§4) |
| A new colour token is not applied | the theme file lacks the key; the default wins (§7) |
| A token name in a style is ignored | typo, or the token does not exist — silent no-op (§7) |
| A hex value in a style is ignored | colour fields take token names only (§7) |
| A theme colour is transparent | alpha `00` is "unset"; the default wins (§7) |
| Your theme edits vanished | the app rewrites the theme file on exit (§5.4) |
| The theme file is now one flat block | the writer emits the grouped form, and drops unknown keys (§5.4) |
| A legacy flat theme lost `text.disabled` | there is no alias for it; the default is used (§5.3) |
| `chip.palette` is not showing | the *style* takes token names, the *theme* takes hex (§5.2) |
| You edited the `btn` class and nothing happened | no widget binds that class — it is inert (§6) |
| A style change reaches one widget but not the others | the other widgets have their own class; the change only hit one entry (see §6 for the resolution chain) |
| The app does not see the change | nothing hot-reloads; restart (§9) |
| Your classes vanished mid-session | a second `compileLayoutConfig()` replaces the registry (§9) |
| The knob texture did not load | the path is relative to the data dir; a failed load is silent (§10) |
| `font.spacing` does nothing | it is parsed and ignored (§10) |
| The `text` font did not change | only the pixel font is themeable (§10) |
| The curve icons look wrong at the top of the range | `curveIcon.frameCount` below 32 (§10) |
| Your settings were not saved | a relative `--config-dir` with a different data dir (§2) |

## 14. What is still not covered

Tutorial 1's coverage table listed three gaps. Here is where each one
stands after this tutorial:

- **Hot reload.** Covered, honestly: nothing reloads, the app restarts,
  and a second compile replaces the registry (§9).
- **An instrument-harness fixture.** Covered for the **shipped** config
  (§8). It is still impossible for a component that does not exist in
  the app's graph — that is what Tutorial 1's self-contained demo is
  for.
- **A new style type or a new style field.** Still open. Both need
  edits in `src/gui_style.h` and `src/gui_style.c`, so they belong to
  the app, not to a config file. The guide's
  [Recipe D](../ui-styling.md#recipe-d--add-a-new-style-field-end-to-end)
  and
  [Recipe E](../ui-styling.md#recipe-e--add-a-new-style-type) are the
  procedures.

Two smaller things this tutorial deliberately does not do:

- **No file watcher.** A reload path would have to re-read the theme,
  re-seed the scheme, re-compile the layout and re-resolve every
  cached style pointer — and it would have to deal with the fact that
  the theme file is rewritten on exit, so a watcher would fight the
  writer. Restarting is the design, not an oversight.
- **No live editor.** `theme_tool` checks and canonicalises; it does
  not write a theme for you. The colours are yours to choose.

### The one thing a config author still cannot see

§6 turned up a gap in the shipped config that no validator catches:
seven of the eleven classes in `src/layout.json` are bound to no
widget, so editing them was a silent no-op. The shipped-config test
validates that each class *compiles*, not that it is *used*, so nothing
in the suite failed.

**Resolved:** the resolver now falls back to the type-named class
(`resolveXStyle` → `findTypeClass`) before the baked default, so an
unclassed widget picks up its type's entry. The resolution chain is
**explicit class → type-named class → baked default** — see the guide's
§4.3. The shipped config renders pixel-identical because those entries
were written to mirror the baked defaults, but editing them now works:
a `"dial"` or `"chip"` change reaches every widget of that type.

`theme_tool bound` still prints the map, and is now the way to check
*which* class a widget ends up on rather than whether your edit is
futile.

### Is the styling system fully documented?

With the guide, Tutorial 1 and this tutorial: **the configuration
language is.** Every style type and field, the class/`extends`
resolution chain, the colour tokens, the layout solver, the file
locations, the deploy rules, the authoring workflow, the validators and
the reload semantics are all written down and, where they can be,
executable.

What is **not** documented is the app's own graph: which node is
created where, in which order, and what each screen's layout looks
like. That is C code, not configuration — `src/gui_*.c` — and the
guide's §5 says which draw function consumes each style type, which is
the boundary a config author actually works at.

Two things would close the remaining gap:

1. A screen-by-screen map of the app's node tree, the way §6 maps the
   class bindings.
2. A decision on whether the per-type entries should be bound (see
   above), and a validator that fails when a shipped class is
   referenced by nothing.

## 15. Where the pieces live

| Path | What it is |
|---|---|
| `src/paths.c` | the config-dir and data-dir ladders, `--config-dir` / `--data-dir` |
| `src/main.c` | the boot sequence and the save-on-exit block |
| `src/io/config_io.c` | the canonical path table, the alias table, the theme loader and writer |
| `src/gui_style.c` | `compileLayoutConfig()`, the class registry, `finalizeStyles()` |
| `src/gui_core.c` | `initDefaultColourScheme()`, the font slots, `styleFont()` |
| `src/curve_icons.c` | the runtime-generated curve-icon atlas |
| `meson/install.sh` | the binary allow-list, the XDG seeding, the data-dir symlinks |
| `tests/dsp/test_theme.c` | the shipped-theme validator |
| `tests/dsp/test_layout.c` | the shipped-style-config validator |
| `src/tools/theme_tool/` | this tutorial's companion tool |
| `src/tools/styling_tutorial/` | Tutorial 1's self-contained demo |
| `bin/clr.json` | the shipped theme (canonical `schema: 2`) |
| `src/layout.json` | the shipped styles and layouts |
| `manual/ui-styling.md` | the styling reference guide |

### The tool's commands

| Command | What it answers |
|---|---|
| `theme_tool where` | which config dir and data dir does the app resolve, and what is in them? |
| `theme_tool check <theme.json>` | is this theme complete, canonical and free of typos? |
| `theme_tool canon <theme.json>` | what will the app write to this theme file when it exits? |
| `theme_tool bound <layout.json>` | which classes does the style config actually bind? |
| `theme_tool tokens` | what are the canonical colour paths? |
| `theme_tool --selftest` | the tool's own headless self-check |

`theme_tool` is a development tool: nothing in it is linked into
`spectrax`, and `meson/install.sh` does not copy it into `bin/`.
