# Skin studio

Tools for designing MPC plugin skins without hand-placing pixels. Every route ends in the same
`layout.conf` (Force Shadow `shadow_page.conf` widget syntax), which `tools/shadow_skin.py` turns into the
skin (`TUI.json`, Q-Links, artwork). Use any step on its own or chain them:

```
params ──auto──► layout.conf ──to-svg──► layout.svg ──(Inkscape)──► from-svg ──► layout.conf ──► skin ──preview──► PNGs
```

| Step | Command | Use it when |
|---|---|---|
| Auto-layout | `tools/studio.py auto module.json -o layout.conf` | You want a working first page in seconds |
| To SVG | `tools/studio.py to-svg layout.conf -o layout.svg --params module.json` | You want to rearrange it visually |
| From SVG | `tools/studio.py from-svg layout.svg -o layout.conf` | You've edited the SVG |
| Build skin | the port's gen script (`shadow_skin.write_skin(...)`) | Always last |
| Preview | `tools/studio.py preview "<skin>/Plugin Skins" -o page_%d.png` | Before deploying anything |

The tools need Python 3; the skin build and preview also need Pillow (the ports run them in a
`python:3.11-slim` container).

## Auto-layout
- Input: a Schwung `module.json` (its `ui_hierarchy` levels become sections) or a port's `params.json`.
  Without `ui_hierarchy`, parameters are grouped by key prefix.
- Output: pages of 2 rows × 8 slots. Row 1 is Q-Link bank 1 (Q-Links 1–8) and row 2 is bank 2 (9–16).
  Each section becomes a titled frame, and a section with more than 8 controls gets its own page.
- Control types: numbers → knob, off/on → toggle, `access:"write"` / trigger → button,
  up to 6 options → vertical selector, 7+ → two-column segments, readout/stepper → 2-slot text box.
- Labels are shortened to fit (an `LFO1 > ` prefix is dropped, since the frame title already says it).

## Inkscape (or Penpot) round trip
- Open `layout.svg` in Inkscape. Each tab is a **layer** (`tab <NAME>`); only the first is visible,
  so toggle layers in the Layers panel.
- Each control is a group whose **label** (Object Properties or the Layers and Objects panel) is its
  layout line without coordinates, e.g. `knob key=cutoff label="CUTOFF"`. Move, resize, duplicate or delete
  it; the converter reads the position and size from the circle or rect inside.
- To add a control, copy one from `tools/skin_template.svg` (one of every kind) and change its label.
- Knob size = circle radius. Readout/stepper/list size = rect size. Horizontal-segment width = rect
  width ÷ options per row. Toggles, buttons and vertical selectors use only the centre (their size
  is fixed by the renderer).
- Q-Link sets live in the layer's **description** (`qlinks "PAGE" = key,...`, one line per nested page).
- Anything without a control label (your own drawings, text, logos) is ignored for now. Using it as
  background artwork is the next step (browser-rendered art, see below).
- `to-svg` → `from-svg` without edits reproduces the layout exactly (verified on Maze Voice: identical
  `TUI.json`, Q-Links and every image).

## Coming next
- Background artwork from the SVG: anything you draw in Inkscape becomes the page's background image.
- Browser-rendered widgets (HTML/CSS/SVG in headless Chromium) instead of the Force Shadow renderer: any
  font, knob style, gradient or shadow, with the same layout files.
