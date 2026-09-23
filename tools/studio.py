#!/usr/bin/env python3
"""Skin studio: first-pass layouts, an Inkscape round trip, and previews for MPC plugin skins.

    studio.py auto     PARAMS -o layout.conf [--title NAME]    parameters -> first-pass layout
    studio.py to-svg   layout.conf -o layout.svg               layout -> editable SVG (Inkscape/Penpot)
    studio.py from-svg layout.svg -o layout.conf               edited SVG -> layout
    studio.py preview  SKIN_DIR -o out_%d.png                  built skin -> one PNG per page

PARAMS is a Schwung module.json (chain_params, plus ui_hierarchy sections when present) or a
port's params.json ({"params": [...]}). The layout is the shadow_page.conf-style file that
shadow_skin.py builds skins from (see its docstring), so every route ends in the same pipeline:

    auto ──► layout.conf ──► to-svg ──► (edit in Inkscape) ──► from-svg ──► layout.conf ──► skin
             (or hand-edit layout.conf directly; any step is optional)

SVG conventions (what to-svg writes and from-svg reads), all in plugin pixels (1280x628):
  - one Inkscape layer per tab, labelled `tab <NAME>`; only the first is visible by default
  - the layer's <desc> holds `qlinks "PAGE" = key,...` lines (one per nested page; optional)
  - each control is an element (usually a group) whose Inkscape label is its layout line without
    coordinates, e.g. `knob key=cutoff label="CUTOFF"`, `frame title="FILTER"`,
    `list key=result cols=2 rows=4 gap=4`; the geometry comes from its first circle/rect
  - knobs: a circle (centre + radius); everything else: a rect (centre and/or size, by kind)
  - anything without such a label (drawings, text, images) is ignored for now; later it becomes
    background artwork
Moving, resizing and duplicating elements in Inkscape is all you need; transforms are handled.
"""
import argparse
import json
import math
import os
import re
import shlex
import sys
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shadow_skin  # noqa: E402

W, H, Y_OFF = shadow_skin.W, shadow_skin.H, shadow_skin.Y_OFF
SVG_NS = "http://www.w3.org/2000/svg"
INK_NS = "http://www.inkscape.org/namespaces/inkscape"
SODI_NS = "http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd"
ET.register_namespace("", SVG_NS)
ET.register_namespace("inkscape", INK_NS)
ET.register_namespace("sodipodi", SODI_NS)
LABEL = "{%s}label" % INK_NS
GROUPMODE = "{%s}groupmode" % INK_NS
GEOM_KEYS = ("x", "y", "w", "h", "cx", "cy", "r", "sw")


# ---------------------------------------------------------------- parameters

def load_params(path):
    """-> (params list, sections [(label, [keys])])"""
    d = json.load(open(path))
    if "params" in d:
        ps = d["params"]
        return ps, [(d.get("name", "Main"), [p["key"] for p in ps])]
    caps = d.get("capabilities", d)
    ps = caps.get("chain_params") or d.get("chain_params") or []
    keys = {p["key"] for p in ps}
    sections, seen = [], set()
    levels = (caps.get("ui_hierarchy") or {}).get("levels") or {}
    order = ["root"] + [k for k in levels if k != "root"]
    for lv in order:
        if lv not in levels:
            continue
        ks = [k for k in levels[lv].get("params", []) if isinstance(k, str) and k in keys and k not in seen]
        seen.update(ks)
        if ks:
            sections.append((levels[lv].get("label", lv), ks))
    rest = [p["key"] for p in ps if p["key"] not in seen]
    if not sections:   # no ui_hierarchy: group by key prefix
        groups = {}
        for k in rest:
            groups.setdefault(k.split("_")[0], []).append(k)
        sections = [(g.upper(), ks) for g, ks in groups.items()]
    elif rest:
        sections.append(("More", rest))
    return ps, sections


def kind_for(p):
    t = p.get("type", "float")
    if t in ("readout", "stepper"):
        return t
    if t == "trigger" or p.get("access") == "write":
        return "button"
    if t == "slot":
        return "slot"
    opts = [str(o).lower() for o in p.get("options") or []]
    if opts:
        if len(opts) == 2 and opts[0] in ("off", "free", "no"):
            return "toggle"
        return "enum_v" if len(opts) <= 6 else "enum_h"   # 7+ won't fit one column in a row
    return "knob"


# ---------------------------------------------------------------- auto layout

SLOT_W, SLOT_X0 = 158, 8
ROW_Y = (Y_OFF + 6, Y_OFF + 6 + 312)     # frame tops (shadow coords) of the two rows
ROW_H = 304


def auto_layout(params, sections, title=None):
    """Sections -> titled frames; rows of 8 slots (= a Q-Link bank); 2 rows per tab."""
    byk = {p["key"]: p for p in params}
    items = []   # (section label, key, slots): steppers/readouts take 2 slots
    for label, ks in sections:
        for k in ks:
            kd = kind_for(byk[k])
            is_arrow = (k.endswith("_prev") or k.endswith("_next")) and k[:-5] in byk
            if kd == "slot" or is_arrow:   # result rows and stepper arrows belong to their widget
                continue
            items.append((label, k, 2 if kd in ("readout", "stepper") else 1))
    # Pack into rows of 8 slots. A section that won't fit in what's left of a row starts a new row;
    # one bigger than a row (e.g. an LFO with 14 controls) starts a new tab and fills its rows.
    tabs, cur, row, used = [], [], [], 0

    def close_row():
        nonlocal row, used, cur
        if row:
            cur.append(row)
        row, used = [], 0
        if len(cur) == 2:
            tabs.append(cur)
            cur = []

    for i, it in enumerate(items):
        if not row or it[0] != row[-1][0]:
            need = sum(x[2] for x in items[i:] if x[0] == it[0])
            if need > 8:
                close_row()
                if cur:
                    tabs.append(cur)
                    cur = []
            elif used + need > 8:
                close_row()
        if used + it[2] > 8:
            close_row()
        row.append(it)
        used += it[2]
    close_row()
    if cur:
        tabs.append(cur)

    out = ["# first-pass layout from studio.py auto; edit freely (or round-trip through to-svg)"]
    for t, trows in enumerate(tabs):
        names = []
        for r in trows:
            for lab in dict.fromkeys(x[0] for x in r):
                if lab not in names:
                    names.append(lab)
        tab_name = " / ".join(names).upper()
        if len(tab_name) > 28:
            tab_name = names[0].upper() + " +%d" % (len(names) - 1)
        out += ["", "[tab %s]" % tab_name]
        qkeys = []
        for r_i, r in enumerate(trows):
            y0 = ROW_Y[r_i]
            slot = 0
            start = 0
            while start < len(r):   # one frame per run of the same section
                end = start
                while end < len(r) and r[end][0] == r[start][0]:
                    end += 1
                span = sum(x[2] for x in r[start:end])
                fx = SLOT_X0 + slot * SLOT_W
                out.append('frame x=%d y=%d w=%d h=%d title="%s"' % (fx + 2, y0, span * SLOT_W - 6, ROW_H, r[start][0].upper()))
                for label, k, width in r[start:end]:
                    cx = SLOT_X0 + slot * SLOT_W + width * SLOT_W // 2
                    out.append(widget_line(byk[k], kind_for(byk[k]), cx, y0, width))
                    qkeys.append(k)
                    slot += width
                start = end
        out.append('qlinks "%s" = %s' % (tab_name, ",".join(qkeys[:16])))
    return "\n".join(out) + "\n"


def short_label(name, n):
    """Fit a label to n chars of the shadow font: drop an 'LFO1 >' style prefix (the frame
    title already says it), keep only glyphs the font has, then truncate."""
    t = name.split(">")[-1].strip().upper()
    t = "".join(c if c.isalnum() or c in " .-/%+:" else " " for c in t)
    return " ".join(t.split())[:n]


def widget_line(p, kind, cx, y0, width):
    lab = short_label(p.get("name") or p["key"], 10 * width)
    cy = y0 + 150
    if kind == "knob":
        return 'knob cx=%d cy=%d r=36 label="%s" key=%s' % (cx, cy - 20, lab, p["key"])
    if kind == "toggle":
        return 'toggle cx=%d cy=%d label="%s" key=%s' % (cx, cy - 20, lab, p["key"])
    if kind == "button":
        return 'button cx=%d cy=%d label="%s" key=%s' % (cx, cy - 20, lab[:7], p["key"])
    if kind == "enum_v":
        opts = ",".join(short_label(str(o), 8) or "-" for o in p["options"])
        top = y0 + 76   # below the frame title and the selector's own label
        return 'enum_v cx=%d cy=%d label="%s" key=%s options="%s"' % (cx, top + len(p["options"]) * 16, lab, p["key"], opts)
    if kind == "enum_h":   # two columns of 66 px segments, option names squeezed to 4 chars
        opts = ",".join(short_label(str(o), 12).replace(" ", "")[:4] or "-" for o in p["options"])
        rows = (len(p["options"]) + 1) // 2
        return 'enum_h cx=%d cy=%d label="%s" key=%s options="%s" sw=66 rows=%d' % (cx, y0 + 92, lab, p["key"], opts, rows)
    w = width * SLOT_W - 24
    return '%s cx=%d cy=%d w=%d h=48 label="%s" key=%s' % (kind, cx, cy, w, lab, p["key"])


# ---------------------------------------------------------------- conf <-> svg

def conf_line(w):
    """Widget dict -> layout line (inverse of shadow_skin.parse_layout)."""
    parts = [w["kind"]]
    for k in GEOM_KEYS + ("rows", "cols", "th", "gap"):
        if k in w:
            parts.append("%s=%d" % (k, w[k]))
    for k, v in w.items():
        if k in GEOM_KEYS + ("kind", "rows", "cols", "th", "gap"):
            continue
        if k == "options":
            v = ",".join(v)
        parts.append('%s="%s"' % (k, v) if (" " in str(v) or k in ("label", "title", "options")) else "%s=%s" % (k, v))
    return " ".join(parts)


def shape_for(w):
    """Widget (shadow coords) -> (svg tag, attrs) in plugin coords."""
    k = w["kind"]
    if k == "frame":
        return "rect", dict(x=w["x"], y=w["y"] - Y_OFF, width=w["w"], height=w["h"])
    if k == "knob":
        return "circle", dict(cx=w["cx"], cy=w["cy"] - Y_OFF, r=w["r"])
    if k == "list":
        return "rect", dict(x=w["x"], y=w["y"] - Y_OFF, width=w["w"], height=w["h"])
    if k in ("readout", "stepper"):
        return "rect", dict(x=w["cx"] - w["w"] / 2, y=w["cy"] - w["h"] / 2 - Y_OFF, width=w["w"], height=w["h"])
    if k == "toggle":
        return "rect", dict(x=w["cx"] - 25.5, y=w["cy"] - 13.5 - Y_OFF, width=51, height=27)
    if k == "button":
        x, y, bw, bh = shadow_skin.button_rect(w)
        return "rect", dict(x=x, y=y - Y_OFF, width=bw, height=bh)
    if k in ("enum_h", "enum_v"):
        rs = shadow_skin.seg_rects(w)
        x0, y0 = min(r[0] for r in rs), min(r[1] for r in rs)
        x1, y1 = max(r[0] + r[2] for r in rs), max(r[1] + r[3] for r in rs)
        return "rect", dict(x=x0, y=y0 - Y_OFF, width=x1 - x0, height=y1 - y0)
    raise ValueError(k)


STYLE = {"frame": "fill:none;stroke:#8f8a78;stroke-width:2",
         "knob": "fill:#e9e4d3;stroke:#3a352c;stroke-width:3",
         "default": "fill:#c7c2b0;fill-opacity:0.6;stroke:#2f4a6b;stroke-width:2"}


def to_svg(conf_path, params_path=None):
    tabs, top = shadow_skin.parse_layout(conf_path)
    opts = {}
    if params_path:
        for p in load_params(params_path)[0]:
            if p.get("options"):
                opts[p["key"]] = [str(o).upper() for o in p["options"]]
    shadow_skin.apply_theme(top)
    root = ET.Element("{%s}svg" % SVG_NS, {"width": str(W), "height": str(H), "viewBox": "0 0 %d %d" % (W, H)})
    ET.SubElement(root, "{%s}desc" % SVG_NS).text = "\n".join(top)   # style/theme lines ride along
    ET.SubElement(root, "{%s}rect" % SVG_NS, {"x": "0", "y": "0", "width": str(W), "height": str(H),
                                              "style": "fill:#%s" % shadow_skin.PLATE, LABEL: "page background"})
    for t, tab in enumerate(tabs):
        layer = ET.SubElement(root, "{%s}g" % SVG_NS, {GROUPMODE: "layer", LABEL: "tab " + tab["name"], "id": "tab%d" % t})
        if t:
            layer.set("style", "display:none")
        ET.SubElement(layer, "{%s}desc" % SVG_NS).text = "\n".join(
            'qlinks "%s" = %s' % (n, ",".join(ks)) for n, ks in tab["qlinks"])
        for i, w in enumerate(tab["widgets"]):
            if w["kind"].startswith("enum") and not w.get("options") and w.get("key") in opts:
                w["options"] = opts[w["key"]]
            tag, attrs = shape_for(w)
            label = strip_geom(conf_line(w))
            if w["kind"] == "enum_h" and not w.get("options"):   # no option count to recompute sw from
                label += " sw=%d" % (w.get("sw") or 117)
            g = ET.SubElement(layer, "{%s}g" % SVG_NS, {LABEL: label, "id": "t%dw%d" % (t, i)})
            a = {k: str(v) for k, v in attrs.items()}
            a["style"] = STYLE.get(w["kind"], STYLE["default"])
            ET.SubElement(g, "{%s}%s" % (SVG_NS, tag), a)
            txt = w.get("title") or w.get("label") or w.get("key", "")
            tx = attrs.get("cx", attrs.get("x", 0) + attrs.get("width", 0) / 2)
            ty = (attrs.get("cy", 0) + attrs.get("r", 0) + 16) if tag == "circle" else attrs["y"] - 4
            if w["kind"] == "frame":
                tx, ty = attrs["x"] + 12, attrs["y"] + 20
            e = ET.SubElement(g, "{%s}text" % SVG_NS, {"x": str(tx), "y": str(ty),
                                                        "style": "font:bold 13px sans-serif;fill:#%s;text-anchor:%s" % (
                                                            shadow_skin.INK, "start" if w["kind"] == "frame" else "middle")})
            e.text = txt
    return ET.tostring(root, encoding="unicode")


def strip_geom(line):
    return " ".join(t for t in shlex.split(line, posix=False) if t.split("=")[0] not in GEOM_KEYS + ("th",))


# -- svg -> conf

def parse_transform(s):
    m = [1, 0, 0, 1, 0, 0]
    for name, args in re.findall(r"(\w+)\s*\(([^)]*)\)", s or ""):
        v = [float(x) for x in re.split(r"[\s,]+", args.strip()) if x]
        if name == "translate":
            t = [1, 0, 0, 1, v[0], v[1] if len(v) > 1 else 0]
        elif name == "scale":
            t = [v[0], 0, 0, v[1] if len(v) > 1 else v[0], 0, 0]
        elif name == "matrix":
            t = v
        elif name == "rotate" and len(v) == 1:
            c, sn = math.cos(math.radians(v[0])), math.sin(math.radians(v[0]))
            t = [c, sn, -sn, c, 0, 0]
        else:
            continue
        m = mul(m, t)
    return m


def mul(a, b):
    return [a[0] * b[0] + a[2] * b[1], a[1] * b[0] + a[3] * b[1], a[0] * b[2] + a[2] * b[3],
            a[1] * b[2] + a[3] * b[3], a[0] * b[4] + a[2] * b[5] + a[4], a[1] * b[4] + a[3] * b[5] + a[5]]


def apply(m, x, y):
    return m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]


def geometry(el, m):
    """First circle/ellipse/rect under el -> ('circle', cx, cy, r) or ('rect', x, y, w, h), plugin coords."""
    for node in el.iter():
        tag = node.tag.split("}")[-1]
        nm = mul(m, parse_transform(node.get("transform"))) if node is not el else m
        if tag in ("circle", "ellipse"):
            cx, cy = apply(nm, float(node.get("cx", 0)), float(node.get("cy", 0)))
            r = float(node.get("r") or node.get("rx") or 0) * math.sqrt(abs(nm[0] * nm[3] - nm[1] * nm[2]))
            return ("circle", cx, cy, r)
        if tag == "rect":
            x, y = float(node.get("x", 0)), float(node.get("y", 0))
            w, h = float(node.get("width", 0)), float(node.get("height", 0))
            pts = [apply(nm, px, py) for px, py in ((x, y), (x + w, y), (x, y + h), (x + w, y + h))]
            xs, ys = [p[0] for p in pts], [p[1] for p in pts]
            return ("rect", min(xs), min(ys), max(xs) - min(xs), max(ys) - min(ys))
    return None


def element_to_line(label, geo):
    toks = shlex.split(label)
    kind, attrs = toks[0], dict(t.split("=", 1) for t in toks[1:] if "=" in t)
    w = {"kind": kind, **attrs}
    if geo[0] == "circle":
        _, cx, cy, r = geo
        x, y, gw, gh = cx - r, cy - r, 2 * r, 2 * r
    else:
        _, x, y, gw, gh = geo
        cx, cy = x + gw / 2, y + gh / 2
    Y = Y_OFF
    if kind == "frame" or kind == "list":
        w.update(x=round(x), y=round(y + Y), w=round(gw), h=round(gh))
        if kind == "list":
            rows, gap = int(w.get("rows", 4)), int(w.get("gap", 4))
            w["th"] = round((gh - (rows - 1) * gap) / rows)
            for k in ("rows", "cols", "gap"):
                w[k] = int(w.get(k, {"rows": 4, "cols": 1, "gap": 4}[k]))
    elif kind == "knob":
        w.update(cx=round(cx), cy=round(cy + Y), r=max(12, round(gw / 2)))
    elif kind in ("readout", "stepper"):
        w.update(cx=round(cx), cy=round(cy + Y), w=round(gw), h=round(gh))
    elif kind == "enum_h":
        w.update(cx=round(cx), cy=round(cy + Y))
        if "options" in w or "n" in w:
            n = len(w["options"].split(",")) if "options" in w else int(w.pop("n"))
            rows = int(w.get("rows", 1))
            per = -(-n // rows)
            w["sw"] = max(20, round((gw - 2 * (per - 1)) / per))
    else:
        w.update(cx=round(cx), cy=round(cy + Y))
    for k in ("sw", "rows"):
        if k in w and not isinstance(w[k], int):
            w[k] = int(w[k])
    if "options" in w:
        w["options"] = w["options"].split(",")
    if "cx" in w and kind != "knob" and (kind != "enum_h" or w.get("options") or "sw" in w):
        # the anchor isn't always the shape's centre (odd sizes, multi-row selectors): place the
        # shape this anchor would draw, and shift the anchor by the difference
        _, a = shape_for(w)
        w["cx"] += round(cx - (a["x"] + a["width"] / 2))
        w["cy"] += round(cy - (a["y"] + a["height"] / 2))
    return conf_line(w)


def from_svg(svg_path):
    root = ET.parse(svg_path).getroot()
    out = ["# from %s via studio.py from-svg" % os.path.basename(svg_path)]
    desc = root.find("{%s}desc" % SVG_NS)
    if desc is not None and desc.text:
        out += [l.strip() for l in desc.text.splitlines() if l.strip()]
    base = parse_transform(root.get("transform"))
    for layer in root.findall("{%s}g" % SVG_NS):
        lab = layer.get(LABEL, "")
        if not lab.startswith("tab "):
            continue
        out += ["", "[%s]" % lab]
        lm = mul(base, parse_transform(layer.get("transform")))

        def walk(el, m):
            for ch in el:
                cm = mul(m, parse_transform(ch.get("transform")))
                l = ch.get(LABEL, "")
                if l.split(" ")[0] in shadow_skin.CONTROL_KINDS + ("frame",):
                    geo = geometry(ch, cm)
                    if geo:
                        out.append(element_to_line(l, geo))
                elif ch.tag.endswith("}g"):
                    walk(ch, cm)
        walk(layer, lm)
        d = layer.find("{%s}desc" % SVG_NS)
        if d is not None and d.text:
            out += [l.strip() for l in d.text.splitlines() if l.strip().startswith("qlinks")]
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------- preview

def preview(skin_dir, out_pattern, frame=40):
    """Composite a built skin into PNGs (what MPC should draw), one per page. Needs Pillow."""
    from PIL import Image, ImageDraw
    t = json.load(open(os.path.join(skin_dir, "TUI.json")))["pageData"]
    defs = {d["key"]: d["value"] for d in t["componentDefinitions"]["localComponentDefinitions"]}
    xywh = lambda b: [int(float(v)) for v in b["bounds"].split()]
    outs = []
    for n, tab in enumerate(t["tabs"]):
        im = Image.new("RGB", (W, H), (0, 0, 0))
        dr = ImageDraw.Draw(im)
        for c in defs[tab["componentName"]]["componentsData"]:
            cd = c["componentData"]
            x, y, w, h = xywh(c["bounds"])
            if cd["type"] == "Image":
                im.paste(Image.open(os.path.join(skin_dir, cd["data"]["image"])).convert("RGB"), (x, y))
                continue
            for s in defs[cd["type"]]["componentsData"]:
                sd = s["componentData"]
                sx, sy, sw, sh = xywh(s["bounds"])
                if sd["type"] == "Knob":
                    st = Image.open(os.path.join(skin_dir, sd["data"]["filmStrip"])).convert("RGB")
                    fw = st.size[0]
                    im.paste(st.crop((0, frame * fw, fw, (frame + 1) * fw)), (x + sx, y + sy))
                elif sd["type"] == "Button":
                    img = sd["data"]["offImage"]
                    if img:
                        im.paste(Image.open(os.path.join(skin_dir, img)).convert("RGB"), (x + sx, y + sy))
                elif sd["type"] == "Label":
                    dr.rectangle([x + sx, y + sy, x + sx + sw - 1, y + sy + sh - 1], outline=(70, 110, 160))
        qx, qy, qw, qh = [int(v) for v in tab["qlinkBoundsData"][0].split()]
        dr.rectangle([qx, qy, qx + qw, qy + qh], outline=(80, 200, 120))
        out = out_pattern % n
        im.save(out)
        outs.append((out, tab["tabName"]))
    return outs


# ---------------------------------------------------------------- cli

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("auto"); a.add_argument("params"); a.add_argument("-o", required=True)
    s = sub.add_parser("to-svg"); s.add_argument("conf"); s.add_argument("-o", required=True)
    s.add_argument("--params", help="module.json/params.json, to fill in omitted option lists")
    f = sub.add_parser("from-svg"); f.add_argument("svg"); f.add_argument("-o", required=True)
    p = sub.add_parser("preview"); p.add_argument("skin"); p.add_argument("-o", required=True)
    args = ap.parse_args()
    if args.cmd == "auto":
        params, sections = load_params(args.params)
        open(args.o, "w").write(auto_layout(params, sections))
    elif args.cmd == "to-svg":
        open(args.o, "w").write(to_svg(args.conf, args.params))
    elif args.cmd == "from-svg":
        open(args.o, "w").write(from_svg(args.svg))
    else:
        for out, name in preview(args.skin, args.o):
            print(out, name)
    print("wrote", args.o)


if __name__ == "__main__":
    main()
