#!/usr/bin/env python3
"""html_art: skin artwork drawn by a browser (headless Chromium), as an alternative to shadow_art.c.

Reads the same commands as shadow_art.c on stdin (see its header; shadow_skin.py writes them) and writes the
same PPM files, so the layout, the skin builder and the skin format don't change: only how things look.
Each drawing becomes SVG styled by CSS (tools/html_art/default.css, then the layout's own `art_css=` file),
so a port can use any font, knob style, gradient or shadow. Geometry comes from the commands as before.

One extra command, which only this renderer has:
  svg|file.svg|x|y|w|h     draw an SVG drawing into that box (a layout's `art file=...` line)

Controls (knob and slider strips, toggles, buttons, option segments, tiles: a canvas holding just one of them)
come out with a transparent background, so they sit on any artwork; they are written as RGBA PNG data under the
.ppm name the command gave (Pillow, which converts them, reads either). Backgrounds stay opaque.

Runs where Playwright's Chromium is (tools/html_art/Dockerfile). Select it with vst.json "art": "html"
(tools/build_port.sh), or SHADOW_ART=tools/html_art.py for gen_vst.py.
"""
import io
import math
import os
import re
import sys
from html import escape

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import shadow_skin  # noqa: E402  (button sizes must match the skin builder's own)

W, H = 1280, 800
# theme_<key> -> CSS variable --<key with dashes>; defaults are render_conf_preview.c's
THEME = {"bg": "131211", "panel": "1c1a17", "line": "2a2823", "ink": "efe9d8", "ink_dim": "8f8878",
         "ink_faint": "5c584c", "accent": "c1552f", "accent_hi": "e2793f", "knob_face": "efe9d8",
         "knob_ring": "2a2823", "knob_dot": "c1552f", "lcd": "1a120d", "seg_active": "f2f1ee",
         "seg_inactive": "050403", "seg_active_tx": "1c1a17", "box": "1f1f1f", "btn_bg": "e8341c",
         "btn_text": "050403", "btn_text_plain": "fdf3ea", "display_bg": "1c2612", "display_cell": "24301a",
         "display_ink": "cdeb63", "display_off": "2c3a1d", "display_bezel": "0d1108"}
DEFS = """<defs>
<linearGradient id="sheen-linear" x1="0" y1="0" x2="0" y2="1">
 <stop offset="0" stop-color="#fff" style="stop-opacity:var(--sheen)"/><stop offset="0.55" stop-color="#fff" stop-opacity="0"/>
</linearGradient>
<radialGradient id="sheen-radial" cx="0.35" cy="0.3" r="0.8">
 <stop offset="0" stop-color="#fff" style="stop-opacity:calc(var(--sheen) * 2.5)"/><stop offset="1" stop-color="#fff" stop-opacity="0"/>
</radialGradient>
</defs>"""


def hexc(s):
    return "#" + s.strip().lstrip("#")


class Art:
    def __init__(self):
        self.theme = dict(THEME)
        self.td3 = False
        self.css = []            # the port's own stylesheet(s)
        self.ops = []            # SVG for the current canvas
        self.kinds = []          # the command behind each op, to tell a lone control from a background
        self.jobs = []           # ("crop", svg, path, x, y, w, h) | ("strip", ...) | ("sstrip", ...)

    # ---- commands ----
    def load_theme(self, path):
        self.theme_lines([l.strip() for l in open(path) if l.strip() and not l.strip().startswith("#")])

    def theme_lines(self, lines):
        shadow_skin.apply_theme(lines)   # font_label= sizes buttons, exactly as the skin builder does
        for l in lines:
            k, _, v = l.partition("=")
            if k == "style":
                self.td3 = v.strip() == "td3"
            elif k.startswith("theme_") and k[6:] in self.theme:
                self.theme[k[6:]] = v.strip()
            elif k == "art_css":
                self.css.append(v.strip())

    def text(self, x, y, s, cls, anchor="middle", extra=""):
        return '<text class="%s" x="%g" y="%g" text-anchor="%s" dominant-baseline="central"%s>%s</text>' % (
            cls, x, y, anchor, extra, escape(s))

    def frame(self, x, y, w, h, title):
        if self.td3:
            o = '<rect class="frame-border" x="%d" y="%d" width="%d" height="%d" rx="10"/>' % (x + 1, y + 1, w - 2, h - 2)
            o += '<line class="frame-rule" x1="%d" y1="%d" x2="%d" y2="%d"/>' % (x + 18, y + 38.5, x + w - 18, y + 38.5)
        else:
            o = '<rect class="frame-border" x="%g" y="%g" width="%d" height="%d"/>' % (x + 0.5, y + 0.5, w - 1, h - 1)
            o += '<line class="frame-rule" x1="%d" y1="%g" x2="%d" y2="%g"/>' % (x + 18, y + 36.5, x + w - 18, y + 36.5)
        if title:
            o += self.text(x + 18, y + 20, title, "frame-title", "start")
        self.ops.append(o)

    def knob_svg(self, cx, cy, r, pct):
        R = r + 2.5
        a0, a1 = -135.0, -135.0 + 270.0 * pct / 100.0
        pt = lambda a, rad: (cx + rad * math.sin(math.radians(a)), cy - rad * math.cos(math.radians(a)))
        sx, sy = pt(a0, R)
        ex, ey = pt(135.0, R)
        o = '<path class="knob-track" style="stroke-width:%g" d="M%.2f %.2f A%g %g 0 1 1 %.2f %.2f"/>' % (
            max(2, r / 10), sx, sy, R, R, ex, ey)
        if pct > 0:
            vx, vy = pt(a1, R)
            o += '<path class="knob-arc" style="stroke-width:%g" d="M%.2f %.2f A%g %g 0 %d 1 %.2f %.2f"/>' % (
                max(2, r / 10), sx, sy, R, R, 1 if a1 - a0 > 180 else 0, vx, vy)
        o += '<circle class="knob-face" cx="%g" cy="%g" r="%g"/>' % (cx, cy, r - 2)
        o += '<circle class="knob-sheen" cx="%g" cy="%g" r="%g"/>' % (cx, cy, r - 2)
        px0, py0 = pt(a1, r * 0.3)
        px1, py1 = pt(a1, r * 0.75)
        o += '<line class="knob-pointer" style="stroke-width:%g" x1="%.2f" y1="%.2f" x2="%.2f" y2="%.2f"/>' % (
            max(2, r / 9), px0, py0, px1, py1)
        return '<g class="knob">%s</g>' % o

    def slider_svg(self, x, y, w, h, vert, t):
        o = '<rect class="slider-well" x="%d" y="%d" width="%d" height="%d" rx="%g"/>' % (x, y, w, h, (w if vert else h) / 2)
        pad = 4
        th = (w if vert else h) - 2 * pad
        if vert:
            ty = y + pad + round((1 - t) * (h - 2 * pad - th))
            o += '<rect class="slider-fill" x="%g" y="%d" width="6" height="%d" rx="3"/>' % (x + w / 2 - 3, ty + th // 2, max(0, y + h - pad - (ty + th // 2)))
            o += '<circle class="slider-thumb" cx="%g" cy="%g" r="%g"/>' % (x + w / 2, ty + th / 2, th / 2)
        else:
            tx = x + pad + round(t * (w - 2 * pad - th))
            o += '<rect class="slider-fill" x="%d" y="%g" width="%d" height="6" rx="3"/>' % (x + pad, y + h / 2 - 3, max(0, tx + th // 2 - x - pad))
            o += '<circle class="slider-thumb" cx="%g" cy="%g" r="%g"/>' % (tx + th / 2, y + h / 2, th / 2)
        return o

    def box_label(self, x0, y0, label):
        return self.text(x0, y0 - 15, label, "box-label", "start") if label else ""

    def arrow(self, cx, cy, size, d, cls):
        b = cx - d * size
        return '<path class="%s" d="M%d %d L%d %d L%d %d Z"/>' % (cls, b, cy - size, b, cy + size, cx, cy)

    def dot_cell(self, x, y, w, h):
        p = 4
        gcols, grows = (w - 8) // p, (h - 6) // p
        gx, gy = x + (w - gcols * p) // 2, y + (h - grows * p) // 2 + 1
        pid = "dots%d_%d" % (len(self.ops), len(self.jobs))
        return ('<rect class="dot-cell" x="%d" y="%d" width="%d" height="%d" rx="5"/>'
                '<pattern id="%s" x="%d" y="%d" width="%d" height="%d" patternUnits="userSpaceOnUse">'
                '<rect width="%d" height="%d" style="fill:var(--display-off)"/></pattern>'
                '<rect x="%d" y="%d" width="%d" height="%d" fill="url(#%s)"/>') % (
            x, y, w, h, pid, gx, gy, p, p, p - 2, p - 2, gx, gy, gcols * p, grows * p, pid)

    def svg_file(self, path, x, y, w, h):
        src = open(path, encoding="utf-8").read()
        m = re.search(r"<svg\b[^>]*>", src)
        end = src.rfind("</svg>")
        if not m or end < 0:
            raise SystemExit("html_art: %s is not an SVG file" % path)
        vb = re.search(r'viewBox="([^"]+)"', m.group(0))
        vb = vb.group(1) if vb else "0 0 %d %d" % (w, h)
        self.ops.append('<svg x="%d" y="%d" width="%d" height="%d" viewBox="%s" overflow="hidden">%s</svg>' % (
            x, y, w, h, vb, src[m.end():end]))

    def run(self, line):
        a = line.split("|")
        op, n = a[0], len(a)
        if op not in ("theme", "crop", "strip", "sstrip"):
            self.kinds.append(op)
        I = lambda k: int(a[k])
        if op == "clear" and n == 2:
            self.ops = ['<rect x="0" y="0" width="%d" height="%d" fill="%s"/>' % (W, H, hexc(a[1]))]
            self.kinds = ["clear"]
        elif op == "theme" and n == 2:
            self.load_theme(a[1])
        elif op == "frame" and n == 6:
            self.frame(I(1), I(2), I(3), I(4), "" if a[5] == "-" else a[5])
        elif op == "frameblank" and n == 5:
            self.frame(I(1), I(2), I(3), I(4), "")
        elif op == "text" and n == 6:
            sc = float(a[3])
            self.ops.append(self.text(I(1), I(2) + 4.5 * sc, a[5], "text", extra=' style="fill:%s"' % hexc(a[4])))
        elif op == "knob" and n == 5:
            self.ops.append(self.knob_svg(I(1), I(2), I(3), I(4)))
        elif op == "pill" and n == 4:
            cx, cy, on = I(1), I(2), I(3)
            lx = cx + 51 // 2 - 27 // 2 if on else cx - 51 // 2 + 27 // 2
            self.ops.append('<g class="pill%s"><rect class="pill-track" x="%g" y="%g" width="50" height="26" rx="13"/>'
                            '<circle class="pill-thumb" cx="%d" cy="%d" r="9.5"/></g>' % (
                                " on" if on else "", cx - 25, cy - 13, lx, cy))
        elif op == "button" and n == 5:
            cx, cy, col, lab = I(1), I(2), hexc(a[3]), a[4]
            bw, bh = shadow_skin.text_width(lab) + 36, 39
            if self.td3:
                bw, bh = bw + 28, 52
            x, y = cx - bw // 2, cy - bh // 2
            self.ops.append('<g class="button%s" style="--fill:%s">'
                            '<rect class="button-bg" x="%d" y="%d" width="%d" height="%d"/>'
                            '<rect class="button-sheen" x="%d" y="%d" width="%d" height="%d"/>%s</g>' % (
                                " td3" if self.td3 else "", col, x + 2, y + 2, bw - 4, bh - 4, x + 2, y + 2, bw - 4, bh - 4,
                                self.text(cx, cy, lab, "button-tx")))
        elif op == "seg" and n == 8:
            x, y, w, h = I(1), I(2), I(3), I(4)
            self.ops.append('<g class="seg" style="--fill:%s;--ink:%s"><rect class="seg-bg" x="%d" y="%d" width="%d" height="%d"/>'
                            '<rect class="seg-sheen" x="%d" y="%d" width="%d" height="%d"/>%s</g>' % (
                                hexc(a[5]), hexc(a[6]), x, y, w, h, x, y, w, h, self.text(x + w / 2, y + h / 2, a[7], "seg-tx")))
        elif op in ("readout", "stepper", "dotreadout", "dotstepper") and n == 6:
            cx, cy, w, h = I(1), I(2), I(3), I(4)
            x0, y0 = cx - w // 2, cy - h // 2
            o = self.box_label(x0, y0, "" if a[5] == "-" else a[5])
            if op == "readout":
                o += '<rect class="box" x="%g" y="%g" width="%d" height="%d"/>' % (x0 + 0.5, y0 + 0.5, w - 1, h - 1)
            elif op == "stepper":
                bx, bw = x0 + h + 3, w - 2 * h - 6
                o += '<rect class="arrow-bg" x="%d" y="%d" width="%d" height="%d" rx="5"/>' % (x0, y0, h, h)
                o += '<rect class="arrow-bg" x="%d" y="%d" width="%d" height="%d" rx="5"/>' % (x0 + w - h, y0, h, h)
                o += self.arrow(x0 + h // 2, cy, h // 4, -1, "arrow") + self.arrow(x0 + w - h // 2, cy, h // 4, 1, "arrow")
                o += '<rect class="box" x="%g" y="%g" width="%d" height="%d"/>' % (bx + 0.5, y0 + 0.5, bw - 1, h - 1)
            else:
                o += '<rect class="dot-bezel" x="%d" y="%d" width="%d" height="%d" rx="8"/>' % (x0 - 4, y0 - 4, w + 8, h + 8)
                if op == "dotstepper":
                    o += '<rect class="dot-bezel" x="%d" y="%d" width="%d" height="%d" rx="5"/>' % (x0, y0, h, h)
                    o += '<rect class="dot-bezel" x="%d" y="%d" width="%d" height="%d" rx="5"/>' % (x0 + w - h, y0, h, h)
                    o += self.arrow(x0 + h // 2, cy, h // 4, -1, "dot-arrow") + self.arrow(x0 + w - h // 2, cy, h // 4, 1, "dot-arrow")
                    o += self.dot_cell(x0 + h + 3, y0, w - 2 * h - 6, h)
                else:
                    o += self.dot_cell(x0, y0, w, h)
            self.ops.append(o)
        elif op == "tile" and n == 8:
            x, y, w, h, bw = I(1), I(2), I(3), I(4), I(7)
            o = '<g style="--fill:%s;--border:%s"><rect class="tile" x="%d" y="%d" width="%d" height="%d"/>' % (
                hexc(a[5]), hexc(a[6]), x, y, w, h)
            if bw > 0:
                o += '<rect class="tile-border" style="stroke-width:%d" x="%g" y="%g" width="%g" height="%g"/>' % (
                    bw, x + bw / 2, y + bw / 2, w - bw, h - bw)
            else:
                o += '<rect class="tile-rule" x="%d" y="%d" width="%d" height="1"/><rect class="tile-rule" x="%d" y="%d" width="%d" height="1"/>' % (
                    x, y, w, x, y + h - 1, w)
            self.ops.append(o + "</g>")
        elif op == "svg" and n == 6:
            self.svg_file(a[1], I(2), I(3), I(4), I(5))
        elif op == "crop" and n == 6:
            lone = self.kinds[:1] == ["clear"] and len(self.kinds) == 2 and self.kinds[1] in ("pill", "button", "seg", "tile", "knob")
            svg = "".join(self.ops[1:] if lone else self.ops)
            self.jobs.append(("crop", svg, a[1], I(2), I(3), I(4), I(5), lone))
        elif op == "strip" and n == 5:
            r, frames = I(2), I(3)
            s = 2 * r + 10
            self.jobs.append(("strip", a[1], s, s, frames,
                              [self.knob_svg(s / 2, s / 2, r, round(100.0 * k / (frames - 1))) for k in range(frames)]))
        elif op == "sstrip" and n == 7:
            w, h, frames, vert = I(2), I(3), I(4), I(5)
            self.jobs.append(("strip", a[1], w, h, frames,
                              [self.slider_svg(0, 0, w, h, vert, k / (frames - 1)) for k in range(frames)]))
        else:
            raise SystemExit("html_art: bad command: %s (%d fields)" % (op, n))

    # ---- rendering ----
    def page(self):
        css = ['<link rel="stylesheet" href="file://%s">' % os.path.join(HERE, "html_art", "default.css")]
        css += ['<link rel="stylesheet" href="file://%s">' % os.path.abspath(c) for c in self.css]
        vars_ = ";".join("--%s:%s" % (k.replace("_", "-"), hexc(v)) for k, v in self.theme.items())
        return ('<!doctype html><html><head><meta charset="utf-8"><style>:root{%s}html,body{margin:0;background:transparent}'
                'svg{display:block}</style>%s</head><body><svg id="c" xmlns="http://www.w3.org/2000/svg" class="%s" '
                'width="%d" height="%d">%s<g id="g"></g></svg></body></html>') % (
            vars_, "".join(css), "td3" if self.td3 else "", W, H, DEFS)

    def render(self):
        from PIL import Image
        from playwright.sync_api import sync_playwright
        tmp = os.path.join(os.path.dirname(os.path.abspath(self.jobs[0][2] if self.jobs[0][0] == "crop" else self.jobs[0][1])),
                           "html_art_page.html")
        open(tmp, "w", encoding="utf-8").write(self.page())
        with sync_playwright() as pw:
            browser = pw.chromium.launch()
            pg = browser.new_page(viewport={"width": W, "height": H})
            pg.goto("file://" + tmp)
            pg.evaluate("document.fonts.ready")
            last = None

            def show(svg, w, h):
                pg.set_viewport_size({"width": w, "height": h})
                pg.evaluate("([s, w, h]) => { const c = document.getElementById('c'); c.setAttribute('width', w);"
                            " c.setAttribute('height', h); document.getElementById('g').innerHTML = s; }", [svg, w, h])
                pg.evaluate("document.fonts.ready")

            for job in self.jobs:
                if job[0] == "crop":
                    _, svg, path, x, y, w, h, lone = job
                    if svg != last:
                        show(svg, W, H)
                        last = svg
                    out = Image.new("RGBA" if lone else "RGB", (w, h), (0, 0, 0, 0) if lone else (0, 0, 0))
                    cx0, cy0, cx1, cy1 = max(0, x), max(0, y), min(W, x + w), min(H, y + h)
                    if cx1 > cx0 and cy1 > cy0:
                        shot = pg.screenshot(clip={"x": cx0, "y": cy0, "width": cx1 - cx0, "height": cy1 - cy0},
                                             omit_background=lone)
                        out.paste(Image.open(io.BytesIO(shot)).convert(out.mode), (cx0 - x, cy0 - y))
                    out.save(path, "PNG" if lone else "PPM")
                else:
                    _, path, fw, fh, frames, parts = job
                    out = Image.new("RGBA", (fw, fh * frames), (0, 0, 0, 0))
                    per = max(1, 8000 // fh)
                    for k0 in range(0, frames, per):
                        chunk = parts[k0:k0 + per]
                        svg = "".join('<g transform="translate(0 %d)">%s</g>' % (i * fh, p) for i, p in enumerate(chunk))
                        show(svg, fw, fh * len(chunk))
                        last = None
                        shot = pg.screenshot(clip={"x": 0, "y": 0, "width": fw, "height": fh * len(chunk)}, omit_background=True)
                        out.paste(Image.open(io.BytesIO(shot)).convert("RGBA"), (0, k0 * fh))
                    out.save(path, "PNG")
            browser.close()
        os.remove(tmp)


def main():
    art = Art()
    for raw in sys.stdin:
        line = raw.rstrip("\r\n")
        if line:
            art.run(line)
    if art.jobs:
        art.render()


if __name__ == "__main__":
    main()
