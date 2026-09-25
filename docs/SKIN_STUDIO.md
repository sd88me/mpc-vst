# Skin studio

Tools for designing MPC plugin skins without hand-placing pixels. Every route ends in the same
`layout.conf` (Force Shadow `shadow_page.conf` widget syntax), which `tools/shadow_skin.py` turns into the
skin (`TUI.json`, Q-Links, artwork). Use any step on its own or chain them:

```
params ──auto──► layout.conf ──to-svg──► layout.svg ──(Inkscape)──► from-svg ──► layout.conf ──► skin ──preview──► PNGs
                     ▲ │
                     └─┴── serve: edit it in the browser
```

| Step | Command | Use it when |
|---|---|---|
| Auto-layout | `tools/studio.py auto params.json -o layout.conf` | You want a working first page in seconds |
| Browser editor | `tools/studio.py serve layout.conf --params params.json` | You want to lay out, restyle and check it by hand |
| To SVG | `tools/studio.py to-svg layout.conf -o layout.svg --params params.json` | You want to rearrange it visually |
| From SVG | `tools/studio.py from-svg layout.svg -o layout.conf` | You've edited the SVG |
| Build skin | the port's gen script (`shadow_skin.write_skin(...)`) | Always last |
| Preview | `tools/studio.py preview "<skin>/Plugin Skins" -o page_%d.png` | Before deploying anything (a page with popups also gets `page_N_open.png`, and one with mode panels a `page_N_mode<p>-<i>.png` per other option) |

The tools need Python 3; the skin build and preview also need Pillow (the ports run them in a
`python:3.11-slim` container). The browser renderer (below) runs in its own image, `mpc-vst-html-art`.

## Auto-layout
- Input: the port's parameter file (`tools/params.py`; its `sections` become frames), or an adapter's
  source (see `adapters/`). Without sections, parameters are grouped by key prefix.
- Output: pages of 2 rows × 8 slots. Row 1 is Q-Link bank 1 (Q-Links 1–8) and row 2 is bank 2 (9–16).
  Each section becomes a titled frame, and a section with more than 8 controls gets its own page.
- Control types: numbers → knob, off/on → toggle, `momentary` / trigger → button,
  up to 6 options → vertical selector, 7+ → `popup` (a field that opens the full list), readout/stepper →
  2-slot text box.
- A `popup` list opens under the field (else above, adding columns until it fits) and closes on a pick; a
  Q-Link turn steps the value and leaves it open. Swap any `enum_h`/`enum_v` line for
  `popup cx= cy= w= h= key=<param>` by hand when an option list takes too much room.
- Labels are shortened to fit (an `LFO1 > ` prefix is dropped, since the frame title already says it).

## Browser editor
`tools/studio.py serve layout.conf --params params.json` and open http://127.0.0.1:8765/. The editor needs Python 3 only
(no Docker, Pillow or Chromium); any browser will do. A layout that doesn't exist yet starts empty. To try it, copy
`tools/skin_template.conf` somewhere and open it with `--params tools/skin_template.params.json`.

- **The canvas is the skin.** Each widget is drawn with the browser renderer's own SVG and stylesheets
  (`tools/html_art.py`, `default.css`, the layout's `art_css=`), so it looks like a `"art": "html"` build. MPC's own text
  (names, values, list rows) is drawn on top in Titillium Web where the device puts it (**Live text**). A skin built with
  the default renderer (shadow_art) has the same geometry but its bitmap font; `preview` shows that exactly.
- **Layout:** drag to move (Shift locks an axis), drag the corner handle to resize (knob radius, box size, segment
  width), arrows nudge by 1 (Shift: 10), drag across empty canvas to select several, then align or spread them.
  Toggles, buttons and vertical selectors have renderer-fixed sizes, so they only move. **Slots** shows the
  auto-layout's 8 × 2 grid.
- **Inspector:** every field of the selected line, a parameter picker, `when=` with the parameter's options, and the
  whole layout line to edit by hand (anything the fields don't cover).
- **Tabs and layers:** add, rename (double-click), reorder, duplicate or delete tabs; reorder lines, duplicate
  (Ctrl+D), delete. Frames and art are drawn behind the controls, as in the built skin.
- **Modes:** a tab with `when=` lines gets a mode bar; pick an option to see that mode (**Other modes** dims the rest).
- **Q-Links:** one card per `qlinks` set (sub-page), 16 slots in the two banks. Click a slot, then a control on the
  canvas. The canvas numbers the selected set's controls. `qlinks_track` is below.
- **Theme:** `style=` and a colour picker per `theme_*` colour, shown live.
- **Style:** creates or edits the `art_css=` stylesheet with a live preview: sliders for the renderer's text sizes,
  corner radius and sheen, font upload (`@font-face` added for you) and a font picker for labels and frame titles.
  Fonts only change baked text; MPC draws names and values in its own fonts (docs/NOTES.md).
- **Import SVG art:** uploads a drawing and adds an `art file=` line (drawn by the browser renderer).
- **Checks:** unknown parameters, option counts that differ from the parameter, bad `when=`, controls overlapping or
  past the plugin area's edge, broken lines, Q-Link sets over 16. Click one to select the widget.
- **Saving** (Ctrl+S) writes the layout as `layout.conf.new`, then renames it over the old one; the first save keeps the
  original as `layout.conf.bak`. Lines you didn't change are written back exactly as they were, comments included;
  a changed line is rewritten in `from-svg`'s format. Undo/redo cover every edit.

The server listens on 127.0.0.1 only (`--host` to change it) and reads and writes files only in the layout's folder.

## Inkscape (or Penpot) round trip
- Open `layout.svg` in Inkscape. Each tab is a **layer** (`tab <NAME>`); only the first is visible,
  so toggle layers in the Layers panel.
- Each control is a group whose **label** (Object Properties or the Layers and Objects panel) is its
  layout line without coordinates, e.g. `knob key=cutoff label="CUTOFF"`. Move, resize, duplicate or delete
  it; the converter reads the position and size from the circle or rect inside.
- To add a control, copy one from `tools/skin_template.svg` (one of every kind) and change its label.
- Knob size = circle radius. Slider/readout/stepper/popup/list size = rect size (`slider_v` / `slider_h`). Horizontal-segment width = rect
  width ÷ options per row. Toggles, buttons and vertical selectors use only the centre (their size
  is fixed by the renderer).
- Q-Link sets live in the layer's **description** (`qlinks "PAGE" = key,...`, one line per nested page).
- Anything without a control label (your own drawings, gradients, text, logos) is **background artwork**:
  `from-svg` writes each tab's to `<layout>.<tab>.art.svg` next to the layout and adds an `art file=...` line,
  and `to-svg` puts it back as editable shapes. Put a drawing in a group labelled `art when=<param>:<option>`
  to show it only in that mode. Art needs the browser renderer.
- `to-svg` → `from-svg` without edits reproduces the layout exactly (verified on Maze Voice: identical
  `TUI.json`, Q-Links and every image).

## Artwork renderers
The layout says where everything goes; a renderer draws it. Two do, from the same layout:

- **shadow_art** (default): force-shadow's own renderer (vendored), so a skin matches the Force Shadow page it
  was ported from pixel for pixel. Bitmap font; controls are opaque squares on the plate colour.
- **Browser** (`"art": "html"` in vst.json): `tools/html_art.py` draws each piece as SVG in headless Chromium
  (`tools/html_art/`, built into the `mpc-vst-html-art` Docker image by `tools/build_port.sh`). Text is real
  Titillium Web by default (MPC's own live-text font, so baked and live text match), knobs have a value arc,
  controls have transparent edges so they sit on artwork, and `art file=` drawings go into the background.
  Restyle it with a stylesheet: a top-level `art_css=skin.css` line in the layout, loaded after
  `tools/html_art/default.css` (its header lists the classes and variables). Any font (`@font-face` with a
  file next to the CSS), knob look, gradient or shadow; `theme_*` lines still set the colours. Only colours,
  shapes and effects change: sizes and positions stay the layout's, because MPC puts its live controls there.

A port with its own build script runs `gen_vst.py` inside `mpc-vst-html-art` instead of `python:3.11-slim`
(see `tools/build_port.sh`).

## Mode panels
End any layout line (a frame too) with `when=<param>:<option>` to show it only while that option parameter is at
that option (option name, any case, or its index). Stack alternatives in the same place, one line per mode:
```
knob  cx=221 cy=410 r=30 label="RATE" key=lfo1_rate when=lfo1_sync:free
popup cx=221 cy=404 w=200 h=44 label="SYNC DIV" key=lfo1_div when=lfo1_sync:sync
```
MPC switches them itself as the parameter changes (IndexedEnabling, docs/NOTES.md). A hidden control keeps its
Q-Link, so either leave it in the page's `qlinks` set or map only controls every mode shows. The preview draws the
page with every option parameter at its first option, plus one `page_N_mode<p>-<i>.png` per other option (p = the
parameter's index). One condition per line: for a control shown in two modes, repeat the line.

## Q-Links
MPC reads two maps from the skin's `Q-Links.json`:
- **Screen mode** (Q-Links follow the page on screen): one map per page. In the layout, each `qlinks "PAGE" =
  key,...` line in a tab gives that page's set, in order: keys 1–8 on knob bank 1, 9–16 on bank 2 (converted to
  MPC's bottom-up grid numbering for you). Without a `qlinks` line, a tab uses its first 16 controls in file order.
- **Program/track mode** (Q-Links fixed to the track or program, whatever page is showing): one map. Set it with a
  top-level `qlinks_track = key,...` line (same ordering); without it, page 1's set is used.

## Coming next
Tracked in [ROADMAP.md](ROADMAP.md) ("Skin controls" and "Porting and tooling"), including a build-and-preview
button in the browser editor.
