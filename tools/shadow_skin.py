"""Build an MPC plugin skin from a Force Shadow style layout (.conf).

The layout uses the shadow_page.conf widget syntax (see force-shadow's
docs/adding-a-page.md), so a page designed for Force Shadow becomes a native
MPC skin with the same look: shadow_art (force-shadow's own renderer) draws
backgrounds, knob filmstrips and button states; MPC draws live values.

Layout file:
    [tab NAME]
    frame   x= y= w= h= title="..."
    knob    cx= cy= r= label="..." key=<param>
    toggle  cx= cy= label="..." key=<param>
    button  cx= cy= label="..." key=<param>          (trigger)
    enum_h  cx= cy= label="..." key=<param> options="A,B,.." [sw=<px>] [rows=<n>]
    enum_v  cx= cy= label="..." key=<param> options="A,B,.."
    qlinks  "PAGE NAME" = key,key,...                  (optional, repeatable)

Coordinates are Force Shadow landscape pixels (1280x800); the plugin area is
1280x628, taken from y=Y_OFF. Each `qlinks` line makes one MPC sub-page of
that tab (same design, its own Q-Link set, max 16: 1-8 bank 1, 9-16 bank 2).
Without one, a tab's first 16 controls in file order get the Q-Links.
Option counts must match the parameter's own (module.json) options.
"""
import os
import re
import shlex
import subprocess

W, H, Y_OFF = 1280, 628, 86
PLATE, INK, INK_DIM, ACCENT, ACCENT_HI = "131211", "efe9d8", "8f8878", "c1552f", "e2793f"
SEG_ON, SEG_OFF, SEG_ON_TX = "f2f1ee", "050403", "1c1a17"
FRAMES = 128               # filmstrip frames (stock strips: 128, numFrames 127)
KNOB_QLINKS = [13, 9, 5, 1, 14, 10, 6, 2]
CONTROL_KINDS = ("knob", "toggle", "button", "enum_h", "enum_v")


def text_width(s, scale=1.5):          # render_conf_preview.c text_width()
    return int(len(s) * 10 * scale - scale)


def parse_layout(path):
    tabs = []
    for raw in open(path):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        m = re.match(r"\[tab (.+)\]$", line)
        if m:
            tabs.append({"name": m.group(1).strip(), "widgets": [], "qlinks": []})
            continue
        if line.startswith("qlinks"):
            m = re.match(r'qlinks\s+"([^"]+)"\s*=\s*(.+)$', line)
            tabs[-1]["qlinks"].append((m.group(1), [k.strip() for k in m.group(2).split(",") if k.strip()]))
            continue
        toks = shlex.split(line)
        w = {"kind": toks[0]}
        for t in toks[1:]:
            k, _, v = t.partition("=")
            w[k] = v
        for k in ("x", "y", "w", "h", "cx", "cy", "r", "sw", "rows"):
            if k in w:
                w[k] = int(w[k])
        if "options" in w:
            w["options"] = w["options"].split(",")
        tabs[-1]["widgets"].append(w)
    return tabs


def qlink_for_slot(slot):
    return KNOB_QLINKS[slot % 8] + 2 * (slot // 8)


# ---- geometry of each widget (shadow coords), mirroring render_conf_preview.c ----

def seg_rects(w):
    n = len(w["options"])
    if w["kind"] == "enum_v":
        sw, sh, gap = 135, 30, 2
        y0 = w["cy"] - (n * (sh + gap)) // 2
        return [(w["cx"] - sw // 2, y0 + i * (sh + gap), sw, sh) for i in range(n)]
    sw, sh, gap = w.get("sw") or 117, 33, 2
    rows = w.get("rows", 1)
    per = -(-n // rows)
    out = []
    for i in range(n):
        r, c = divmod(i, per)
        cnt = min(per, n - r * per)
        x0 = w["cx"] - (cnt * sw + (cnt - 1) * gap) // 2
        out.append((x0 + c * (sw + gap), w["cy"] - sh // 2 + r * (sh + gap), sw, sh))
    return out


def label_cmds(w):
    """Static text baked into the page background."""
    k, lab = w["kind"], w.get("label", "")
    if k == "knob":
        return ["text|%d|%d|1.5|%s|%s" % (w["cx"], w["cy"] + w["r"] + 12, INK, lab)]
    if k == "toggle":
        return ["text|%d|%d|1.5|%s|%s" % (w["cx"], w["cy"] + 13 + 10, INK, lab)]
    if k == "enum_h":
        return ["text|%d|%d|1.5|%s|%s" % (w["cx"], w["cy"] - 33 // 2 - 22, INK, lab)]
    if k == "enum_v":
        n = len(w["options"])
        return ["text|%d|%d|1.5|%s|%s" % (w["cx"], w["cy"] - (n * 32) // 2 - 24, ACCENT_HI, lab)]
    return []


def button_rect(w):
    bw, bh = text_width(w["label"]) + 36, 39
    return (w["cx"] - bw // 2, w["cy"] - bh // 2, bw, bh)


# ---- TUI.json helpers ----

def _bounds(x, y, w, h, focus="No", show="Show", visible="Always"):
    return {"version": 2, "acceptsHWFocus": focus, "showWhenDataModelInvalid": show, "whenVisible": visible,
            "boundsType": "Absolute", "bounds": "%d %d %d %d" % (x, y, w, h), "additionalInvalidatingHandles": []}


def _sub(ctype, data, bnd, name=""):
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": ctype, "data": data},
            "handle remapping": {"version": 1, "map": []}, "bounds": bnd}


def _action(on, handler, extra=""):
    return {"version": 2, "onAction": on, "handler": handler, "handleName": "" if handler == "Show Overlay" else "Data",
            "additionalData": extra, "handle remapping": {"version": 1, "map": []}}


def _local(key, actions, children):
    clear = {"version": 1, "colour": "0", "image": ""}
    return {"key": key, "value": {"version": 4, "actions": actions,
                                  "backgroundData": {"version": 1, "focussed": clear, "unfocussed": clear},
                                  "ignoreMousePresses": False, "disableCoarseDataWheel": False, "repeats": 1,
                                  "hideQLinkBounds": False, "componentsData": children}}


def _focus(w, h):
    return _sub("Focus", {"version": 1, "backgroundColour": "14ffffff", "outlineColour": "ff" + ACCENT_HI,
                          "backgroundInset": 2.0, "outlineThickness": 2.0},
                _bounds(0, 0, w, h, visible="WhenFocussed"), "Focus")


def _button(on_img, off_img, bid, n, w, h, x=0, y=0):
    return _sub("Button", {"version": 2, "onImage": on_img, "offImage": off_img, "buttonId": bid,
                           "numButtonsInGroup": n, "handleName": "Data", "gestureBehaviour": "Instant"},
                _bounds(x, y, w, h), "Button")


def _placed(ctype, name, index, x, y, w, h, focus="Yes"):
    return {"version": 2,
            "componentData": {"version": 1, "name": name, "type": ctype, "data": {"version": 1, "handleName": "Data"}},
            "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter %d" % index}]},
            "bounds": _bounds(x, y - Y_OFF, w, h, focus=focus, show="Hide")}


def build(layout_path, params, skin_dir, art_bin, png_from_ppm):
    """Returns (localComponentDefinitions, tabs, qlink map entries); writes PNGs into skin_dir."""
    index = {p["key"]: i for i, p in enumerate(params)}
    tabs_in = parse_layout(layout_path)
    work = os.path.join(skin_dir, ".art")
    os.makedirs(work, exist_ok=True)
    script, defs, pages, qmap, ppms = [], {}, [], [], []

    def art(name):
        ppm = os.path.join(work, name + ".ppm")
        ppms.append((ppm, os.path.join(skin_dir, name + ".png")))
        return ppm

    radii = set()
    for t, tab in enumerate(tabs_in):
        kids, controls = [], []
        for w in tab["widgets"]:
            if w["kind"] not in CONTROL_KINDS:
                continue
            k = w["key"]
            if k not in index:
                raise SystemExit("layout: key %r is not a plugin parameter" % k)
            p = params[index[k]]
            if w["kind"].startswith("enum") and len(w["options"]) != len(p.get("options") or []):
                raise SystemExit("layout: %s has %d options, parameter has %d" % (k, len(w["options"]), len(p.get("options") or [])))
            controls.append(k)

        # background: frames + static labels, cropped to the plugin area
        script.append("clear|" + PLATE)
        for w in tab["widgets"]:
            if w["kind"] == "frame":
                script.append("frame|%d|%d|%d|%d|%s" % (w["x"], w["y"], w["w"], w["h"], w.get("title", "")))
            script += label_cmds(w)
        bg = "sh_bg_%d" % t
        script.append("crop|%s|0|%d|%d|%d" % (art(bg), Y_OFF, W, H))
        kids.append(_sub("Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": bg + ".png"},
                         _bounds(0, 0, W, H), "Background"))

        for w in tab["widgets"]:
            kind = w["kind"]
            if kind not in CONTROL_KINDS:
                continue
            i, name = index[w["key"]], w.get("label", w["key"])
            if kind == "knob":
                r = w["r"]
                s, cw = 2 * r + 10, max(130, 2 * r + 10)   # value label width; LFO knobs sit 138 px apart
                ch = s // 2 + r + 29 + 24
                radii.add(r)
                key = "shKnob%d" % r
                defs.setdefault(key, _local(key, [_action("Mouse Down", "Q-Link"),
                                                  _action("Double Click", "Show Overlay", "knob overlay"),
                                                  _action("Enter Pressed", "Show Overlay", "knob overlay")], [
                    _focus(cw, ch),
                    _sub("Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": "sh_knob_r%d.png" % r,
                                  "numFrames": FRAMES - 1, "invert": False, "dragOrientation": "Vertical",
                                  "handleName": "Data"}, _bounds((cw - s) // 2, 0, s, s), "Knob"),
                    _sub("Label", {"version": 1, "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web",
                                                                                     "style": "SemiBold", "height": 22.0},
                                                               "colour": "ff" + INK_DIM,
                                                               "justification": "horizontallyCentred verticallyCentred",
                                                               "case": "Upper Case"},
                                   "type": "Value", "handleName": "Data"},
                         _bounds(0, s // 2 + r + 27, cw, 26), "Value")]))
                kids.append(_placed(key, name, i, w["cx"] - cw // 2, w["cy"] - s // 2, cw, ch))
            elif kind == "toggle":
                key = "shToggle"
                if key not in defs:
                    for on in (0, 1):
                        script += ["clear|" + PLATE, "pill|100|100|%d" % on,
                                   "crop|%s|74|86|53|29" % art("sh_pill_%s" % ("on" if on else "off"))]
                    defs[key] = _local(key, [_action("Mouse Down", "Q-Link"), _action("Enter Pressed", "Toggle Switch")],
                                       [_focus(120, 56), _button("sh_pill_on.png", "sh_pill_off.png", 1, 1, 53, 29, 33, 4)])
                kids.append(_placed(key, name, i, w["cx"] - 60, w["cy"] - 18, 120, 56))
            elif kind == "button":
                x, y, bw, bh = button_rect(w)
                img = "sh_btn_%s" % w["key"]
                for state, col in (("off", ACCENT), ("on", ACCENT_HI)):
                    script += ["clear|" + PLATE, "button|%d|%d|%s|%s" % (w["cx"], w["cy"], col, w["label"]),
                               "crop|%s|%d|%d|%d|%d" % (art("%s_%s" % (img, state)), x, y, bw, bh)]
                key = "shTrig_" + w["key"]
                defs[key] = _local(key, [_action("Mouse Down", "Q-Link"), _action("Enter Pressed", "Toggle Switch")],
                                   [_focus(bw, bh), _button(img + "_on.png", img + "_off.png", 1, 1, bw, bh)])
                kids.append(_placed(key, name, i, x, y, bw, bh))
            else:  # enum_h / enum_v: radio group, one image button per option
                n = len(w["options"])
                for o, (x, y, sw, sh) in enumerate(seg_rects(w)):
                    img = "sh_seg_%s_%d" % (w["key"], o)
                    lab = w["options"][o]
                    for state, fill, ink in (("on", SEG_ON, SEG_ON_TX), ("off", SEG_OFF, INK_DIM)):
                        script += ["clear|" + PLATE, "seg|%d|%d|%d|%d|%s|%s|%s" % (x, y, sw, sh, fill, ink, lab),
                                   "crop|%s|%d|%d|%d|%d" % (art("%s_%s" % (img, state)), x, y, sw, sh)]
                    key = "shSeg_%s_%d" % (w["key"], o)
                    defs[key] = _local(key, [_action("Mouse Down", "Q-Link")],
                                       [_button(img + "_on.png", img + "_off.png", o, n, sw, sh)])
                    kids.append(_placed(key, "%s %s" % (name, lab), i, x, y, sw, sh, focus="Yes" if o == 0 else "No"))

        sets = tab["qlinks"] or [(tab["name"], controls[:16])]
        for sp, (title, keys) in enumerate(sets):
            if len(keys) > 16:
                raise SystemExit("layout: qlinks %r has %d keys (max 16)" % (title, len(keys)))
            ql = {"Q-Link %d" % (q + 1): -1 for q in range(16)}
            for s, k in enumerate(keys):
                if k not in index:
                    raise SystemExit("layout: qlinks key %r is not a parameter" % k)
                ql["Q-Link %d" % qlink_for_slot(s)] = index[k]
            comp = "%s|%s" % (tab["name"], title)
            pages.append({"version": 3, "tabName": title, "fnKeyIndex": t, "fnKeySubIndex": sp,
                          "qlinkBoundsData": [qlink_bounds(tab, keys)], "componentName": comp,
                          "initialSize": "0 0 %d %d" % (W, H), "scale": 1.0})
            qmap.append({"Tab": t + 1, "SubTab": sp + 1, "Bank Direction": "Column", "Q-Links": ql})
            defs[comp] = {"key": comp, "value": {
                "version": 4, "actions": [],
                "backgroundData": {"version": 1, "focussed": {"version": 1, "colour": "ff" + PLATE, "image": ""},
                                   "unfocussed": {"version": 1, "colour": "ff" + PLATE, "image": ""}},
                "ignoreMousePresses": False, "disableCoarseDataWheel": False, "repeats": 1,
                "hideQLinkBounds": False, "componentsData": kids}}

    for r in sorted(radii):
        script.append("strip|%s|%d|%d|%s" % (art("sh_knob_r%d" % r), r, FRAMES, PLATE))
    subprocess.run([art_bin], input="\n".join(script) + "\n", text=True, check=True)
    for ppm, png in ppms:
        png_from_ppm(ppm, png)
    for f in os.listdir(work):
        os.remove(os.path.join(work, f))
    os.rmdir(work)
    return list(defs.values()), pages, qmap


def qlink_bounds(tab, keys):
    """Rectangle around the controls a page's Q-Links drive (plugin coords)."""
    xs, ys = [], []
    for w in tab["widgets"]:
        if w.get("key") not in keys:
            continue
        if w["kind"] == "knob":
            r = w["r"]
            xs += [w["cx"] - 65, w["cx"] + 65]
            ys += [w["cy"] - r - 8, w["cy"] + r + 56]
        elif w["kind"] == "button":
            continue   # a shared trigger (e.g. GENERATE) would stretch the box across frames
        elif w["kind"] == "toggle":
            xs += [w["cx"] - 60, w["cx"] + 60]
            ys += [w["cy"] - 18, w["cy"] + 38]
        else:
            for x, y, sw, sh in seg_rects(w):
                xs += [x, x + sw]
                ys += [y - 40, y + sh]
    if not xs:
        return "0 0 %d %d" % (W, H)
    x0, y0 = max(0, min(xs) - 6), max(0, min(ys) - Y_OFF - 6)
    return "%d %d %d %d" % (x0, y0, min(W, max(xs) + 6) - x0, min(H, max(ys) - Y_OFF + 6) - y0)
