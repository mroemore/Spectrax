# Spectrax manual

Documentation meant to ship with a release — user- and author-facing guides.
Internal notes, plans, specs, research and generated API docs stay in `docs/`
(which is gitignored).

Add a file here when it answers "how do I use/configure/extend this?" rather
than "how did we decide this?".

## Contents

- [UI styling](ui-styling.md) — the JSON styling and layout system: every
  style type and field, the class/`extends` resolution chain, `clr.json`
  colours, the layout solver, and recipes for adding styles, classes,
  layouts, fields, types and colours.
- [Tutorials](tutorials/building-a-component.md) — worked examples that
  build something with the styling system. Start with
  [Building a new component](tutorials/building-a-component.md): a new
  layout and a new widget, from the first class to the tests.

## Adding a guide

1. Write it in this folder (Markdown, `# Title` first line).
2. Link it from the list above.
3. Keep examples runnable on Void Linux *and* other distros; call out
   platform differences explicitly.
