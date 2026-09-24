# Skin studio

Tools for designing MPC plugin skins without hand-placing pixels. Every route ends in the same
`layout.conf` (Force Shadow `shadow_page.conf` widget syntax), which `tools/shadow_skin.py` turns into the
skin (`TUI.json`, Q-Links, artwork). Use any step on its own or chain them:

```
params ──auto──► layout.conf ──to-svg──► layout.svg ──(Inkscape)──► from-svg ──► layout.conf ──► skin ──preview──► PNGs
```

| Step | Command | Use it when |
|---|---|---|
| Auto-layout | `tools/studio.py auto params.json -o layout.conf` | You want a working first page in seconds |
| To SVG | `tools/studio.py to-svg layout.conf -o layout.svg --params params.json` | You want to rearrange it visually |
| From SVG | `tools/studio.py from-svg layout.svg -o layout.conf` | You've edited the SVG |
| Build skin | the port's gen script (`shadow_skin.write_skin(...)`) | Always last |
| Preview | `tools/studio.py preview "<skin>/Plugin Skins" -o page_%d.png` | Before deploying anything (a page with popups also gets `page_N_open.png`) |

The tools need Python 3; the skin build and preview also need Pillow (the ports run them in a
`python:3.11-slim` container).

## Auto-layout
- Input: the port's parameter file (`tools/params.py`; its `sections` become frames), or an adapter's
  source (see `adapters/`). Without sections, parameters are grouped by key prefix.
- Output: pages of 2 rows × 8 slots. Row 1 is Q-Link bank 1 (Q-Links 1–8) and row 2 is bank 2 (9–16).
  Each section becomes a titled frame, and a section with more than 8 controls gets its own page.
- Control types: numbers → knob, off/on → toggle, `momentary` / trigger → button,
  up to 6 options → vertical selector, 7+ → two-column segments, readout/stepper → 2-slot text box.
- Auto-layout never picks `popup`; swap an `enum_h`/`enum_v` line for
  `popup cx= cy= w= h= key=<param>` by hand when a long option list takes too much room. The list opens
  under the field (else above, adding columns until it fits) and closes on a pick.
- Labels are shortened to fit (an `LFO1 > ` prefix is dropped, since the frame title already says it).

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
- Anything without a control label (your own drawings, text, logos) is ignored for now. Using it as
  background artwork is the next step (browser-rendered art, see below).
- `to-svg` → `from-svg` without edits reproduces the layout exactly (verified on Maze Voice: identical
  `TUI.json`, Q-Links and every image).

## Q-Links
MPC reads two maps from the skin's `Q-Links.json`:
- **Screen mode** (Q-Links follow the page on screen): one map per page. In the layout, each `qlinks "PAGE" =
  key,...` line in a tab gives that page's set, in order: keys 1–8 on knob bank 1, 9–16 on bank 2 (converted to
  MPC's bottom-up grid numbering for you). Without a `qlinks` line, a tab uses its first 16 controls in file order.
- **Program/track mode** (Q-Links fixed to the track or program, whatever page is showing): one map. Set it with a
  top-level `qlinks_track = key,...` line (same ordering); without it, page 1's set is used.

## Coming next
- Background artwork from the SVG: anything you draw in Inkscape becomes the page's background image.
- Browser-rendered widgets (HTML/CSS/SVG in headless Chromium) instead of the Force Shadow renderer: any
  font, knob style, gradient or shadow, with the same layout files.
