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
    enum_h  cx= cy= label="..." key=<param> [options="A,B,.."] [sw=<px>] [rows=<n>]
    enum_v  cx= cy= label="..." key=<param> [options="A,B,.."]   (options default to the param's)
    slider_v cx= cy= w= h= label="..." key=<param>     (vertical slider; value text below)
    slider_h cx= cy= w= h= label="..." key=<param>     (horizontal slider; value text below)
    readout cx= cy= w= h= label="..." key=<param>      (live value text)
    menu    cx= cy= w= h= label="..." key=<param>      (value text; tap opens MPC's native picker)
    stepper cx= cy= w= h= label="..." key=<param>      (live text; arrows = <param>_prev / <param>_next)
    list    x= y= w= h= cols= rows= th= gap= key=<p>   (rows = params <p>_1..<p>_N: text + tap)
    qlinks  "PAGE NAME" = key,key,...                  (optional, repeatable)
Top level: `qlinks_track = key,...` sets the Q-Links used outside page-follow mode (default: page 1's).
Top-level `style=` / `theme_<name>=RRGGBB` lines are the shadow_page.conf ones; `color=` on a
button overrides its fill.

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
LCD, LINE, BTN_BG, BTN_TEXT, BOX = "1a120d", "2a2823", "", "fdf3ea", "1f1f1f"
TD3 = False   # style=td3: frames are filled boxes, so widget crops sit on BOX, not the page bg
FRAMES = 128               # filmstrip frames (stock strips: 128, numFrames 127)
KNOB_QLINKS = [13, 9, 5, 1, 14, 10, 6, 2]
CONTROL_KINDS = ("knob", "slider_v", "slider_h", "toggle", "button", "enum_h", "enum_v", "readout", "stepper", "list", "menu")
THEME_KEYS = {"bg": "PLATE", "ink": "INK", "ink_dim": "INK_DIM", "accent": "ACCENT", "accent_hi": "ACCENT_HI",
              "seg_active": "SEG_ON", "seg_inactive": "SEG_OFF", "seg_active_tx": "SEG_ON_TX",
              "lcd": "LCD", "line": "LINE", "btn_bg": "BTN_BG", "btn_text": "BTN_TEXT", "box": "BOX"}


def text_width(s, scale=1.5):          # render_conf_preview.c text_width()
    return int(len(s) * 10 * scale - scale)


def parse_layout(path):
    """Returns (tabs, top-level style/theme lines)."""
    tabs, top = [], []
    for raw in open(path):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if not tabs and "=" in line and not line.startswith("[") and not line.startswith("qlinks_track"):
            top.append(line)
            continue
        m = re.match(r"\[tab (.+)\]$", line)
        if m:
            tabs.append({"name": m.group(1).strip(), "widgets": [], "qlinks": []})
            continue
        if line.startswith("qlinks_track"):
            top.append(line)
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
        for k in ("x", "y", "w", "h", "cx", "cy", "r", "sw", "rows", "cols", "th", "gap"):
            if k in w:
                w[k] = int(w[k])
        if "options" in w:
            w["options"] = w["options"].split(",")
        tabs[-1]["widgets"].append(w)
    return tabs, top


def apply_theme(top):
    """theme_* lines override the palette used for text/segments/tiles drawn from Python."""
    g = globals()
    for line in top:
        if line.strip() == "style=td3":
            g["TD3"] = True
        k, _, v = line.partition("=")
        if k.startswith("theme_") and k[6:] in THEME_KEYS:
            g[THEME_KEYS[k[6:]]] = v.strip()


def under():
    """Colour behind widgets: td3 frames are filled boxes."""
    return BOX if TD3 else PLATE


def slug(t):
    return "".join(c if c.isalnum() else "_" for c in t).strip("_") or "x"


def shade(hexcol, f):
    r, gr, b = (int(hexcol[i:i + 2], 16) for i in (0, 2, 4))
    return "%02x%02x%02x" % tuple(max(0, min(255, int(c * f))) for c in (r, gr, b))


def list_keys(w):
    return ["%s_%d" % (w["key"], i + 1) for i in range(w["cols"] * w["rows"])]


def list_tiles(w):
    tw = (w["w"] - (w["cols"] - 1) * w["gap"]) // w["cols"]
    return [(w["x"] + (i % w["cols"]) * (tw + w["gap"]), w["y"] + (i // w["cols"]) * (w["th"] + w["gap"]), tw, w["th"])
            for i in range(w["cols"] * w["rows"])]


def stepper_arrows(w):
    x0, y0, h = w["cx"] - w["w"] // 2, w["cy"] - w["h"] // 2, w["h"]
    return (x0, y0, h, h), (x0 + w["w"] - h, y0, h, h)


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
    if k in ("slider_v", "slider_h"):
        return ["text|%d|%d|1.5|%s|%s" % (w["cx"], w["cy"] + w["h"] // 2 + 10, INK, lab)]
    if k == "enum_h":
        return ["text|%d|%d|1.5|%s|%s" % (w["cx"], w["cy"] - 33 // 2 - 22, INK, lab)]
    if k == "enum_v":
        n = len(w["options"])
        return ["text|%d|%d|1.5|%s|%s" % (w["cx"], w["cy"] - (n * 32) // 2 - 24, ACCENT_HI, lab)]
    return []


def button_rect(w):
    bw, bh = text_width(w["label"]) + 36, 39
    if TD3:   # widget_button(): +24 wide, 48 tall, plus a 2 px outline ring
        bw, bh = bw + 24 + 4, 48 + 4
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


def _value_label(x, y, w, h, size, colour, just="horizontallyCentred verticallyCentred"):
    return _sub("Label", {"version": 1, "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web",
                                                                         "style": "SemiBold", "height": size},
                                                   "colour": "ff" + colour, "justification": just, "case": "Original"},
                          "type": "Value", "handleName": "Data"}, _bounds(x, y, w, h), "Value")


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
    tabs_in, top = parse_layout(layout_path)
    apply_theme(top)
    work = os.path.join(skin_dir, ".art")
    os.makedirs(work, exist_ok=True)
    script, defs, pages, qmap, ppms = [], {}, [], [], []
    theme_conf = os.path.join(work, "theme.conf")
    open(theme_conf, "w").write("\n".join(l for l in top if not l.startswith("qlinks_track")) + "\n")
    script.append("theme|" + theme_conf)

    def art(name):
        ppm = os.path.join(work, name + ".ppm")
        ppms.append((ppm, os.path.join(skin_dir, name + ".png")))
        return ppm

    radii, sliders = set(), set()
    for t, tab in enumerate(tabs_in):
        kids, controls = [], []
        for w in tab["widgets"]:
            if w["kind"] not in CONTROL_KINDS:
                continue
            k = w["key"]
            need = list_keys(w) if w["kind"] == "list" else [k]
            if w["kind"] == "stepper":
                need += [k + "_prev", k + "_next"]
            for nk in need:
                if nk not in index:
                    raise SystemExit("layout: key %r is not a plugin parameter" % nk)
            if w["kind"] == "list":
                controls += list_keys(w)
                continue
            p = params[index[k]]
            if w["kind"].startswith("enum") and not w.get("options"):
                w["options"] = [str(o).upper() for o in p.get("options") or []]   # default: the parameter's own
            if w["kind"].startswith("enum") and len(w["options"]) != len(p.get("options") or []):
                raise SystemExit("layout: %s has %d options, parameter has %d" % (k, len(w["options"]), len(p.get("options") or [])))
            controls.append(k)

        # background: frames + static labels, cropped to the plugin area
        script.append("clear|" + PLATE)
        for w in tab["widgets"]:
            if w["kind"] == "frame":
                script.append("frame|%d|%d|%d|%d|%s" % (w["x"], w["y"], w["w"], w["h"], w.get("title", "")))
            elif w["kind"] in ("readout", "stepper", "menu"):
                script.append("%s|%d|%d|%d|%d|%s" % ("readout" if w["kind"] == "menu" else w["kind"],
                                                     w["cx"], w["cy"], w["w"], w["h"], w.get("label") or "-"))
            elif w["kind"] == "list":
                for (x, y, tw, th) in list_tiles(w):
                    script.append("tile|%d|%d|%d|%d|%s|%s|0" % (x, y, tw, th, LCD, LINE))
            script += label_cmds(w)
        bg = "sh_bg_%d" % t
        script.append("crop|%s|0|%d|%d|%d" % (art(bg), Y_OFF, W, H))
        kids.append(_sub("Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": bg + ".png"},
                         _bounds(0, 0, W, H), "Background"))

        for w in tab["widgets"]:
            kind = w["kind"]
            if kind not in CONTROL_KINDS:
                continue
            i, name = index.get(w["key"], -1), w.get("label", w["key"])
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
                        script += ["clear|" + under(), "pill|100|100|%d" % on,
                                   "crop|%s|74|86|53|29" % art("sh_pill_%s" % ("on" if on else "off"))]
                    defs[key] = _local(key, [_action("Mouse Down", "Q-Link"), _action("Enter Pressed", "Toggle Switch")],
                                       [_focus(120, 56), _button("sh_pill_on.png", "sh_pill_off.png", 1, 1, 53, 29, 33, 4)])
                kids.append(_placed(key, name, i, w["cx"] - 60, w["cy"] - 18, 120, 56))
            elif kind == "button":
                x, y, bw, bh = button_rect(w)
                img = "sh_btn_%s_%s" % (w["key"], slug(w["label"]))
                base = w.get("color") or BTN_BG or ACCENT
                for state, col in (("off", base), ("on", shade(base, 1.35))):
                    script += ["clear|" + under(), "button|%d|%d|%s|%s" % (w["cx"], w["cy"], col, w["label"]),
                               "crop|%s|%d|%d|%d|%d" % (art("%s_%s" % (img, state)), x, y, bw, bh)]
                key = "shTrig_%s_%s" % (w["key"], slug(w["label"]))
                defs[key] = _local(key, [_action("Mouse Down", "Q-Link"), _action("Enter Pressed", "Toggle Switch")],
                                   [_focus(bw, bh), _button(img + "_on.png", img + "_off.png", 1, 1, bw, bh)])
                kids.append(_placed(key, name, i, x, y, bw, bh))
            elif kind in ("slider_v", "slider_h"):
                sw_, sh_ = w["w"], w["h"]
                vert = kind == "slider_v"
                img = "sh_%s_%dx%d" % (kind, sw_, sh_)
                sliders.add((img, sw_, sh_, vert))
                sq = max(sw_, sh_)   # filmstrip frames are square (as stock); padding is transparent
                cw = max(130, sq)
                ch = (sq - sh_) // 2 + sh_ + 27 + 26
                key = "shSlider_%s_%dx%d" % ("v" if vert else "h", sw_, sh_)
                defs.setdefault(key, _local(key, [_action("Mouse Down", "Q-Link"),
                                                  _action("Double Click", "Show Overlay", "knob overlay"),
                                                  _action("Enter Pressed", "Show Overlay", "knob overlay")], [
                    _focus(cw, ch),
                    _sub("Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": img + ".png",
                                  "numFrames": FRAMES - 1, "invert": False,
                                  "dragOrientation": "Vertical" if vert else "Horizontal",
                                  "handleName": "Data"}, _bounds((cw - sq) // 2, 0, sq, sq), "Slider"),
                    _value_label(0, (sq - sh_) // 2 + sh_ + 27, cw, 26, 22.0, INK_DIM)]))
                kids.append(_placed(key, name, i, w["cx"] - cw // 2, w["cy"] - sq // 2, cw, ch))
            elif kind == "menu":
                x, y, rw, rh = w["cx"] - w["w"] // 2, w["cy"] - w["h"] // 2, w["w"], w["h"]
                key = "shMenu_%dx%d" % (rw, rh)
                overlay = [_action("Mouse Down", "Show Overlay", "menu overlay"),
                           _action("Double Click", "Show Overlay", "menu overlay"),
                           _action("Enter Pressed", "Show Overlay", "menu overlay")]
                defs.setdefault(key, _local(key, overlay, [_focus(rw, rh), _value_label(8, 0, rw - 16, rh, 26.0, ACCENT)]))
                kids.append(_placed(key, name, i, x, y, rw, rh))
            elif kind == "readout":
                x, y, rw, rh = w["cx"] - w["w"] // 2, w["cy"] - w["h"] // 2, w["w"], w["h"]
                key = "shReadout_%dx%d" % (rw, rh)
                defs.setdefault(key, _local(key, [], [_value_label(8, 0, rw - 16, rh, 26.0, ACCENT)]))
                kids.append(_placed(key, name, i, x, y, rw, rh, focus="No"))
            elif kind == "stepper":
                x0, y0 = w["cx"] - w["w"] // 2, w["cy"] - w["h"] // 2
                h = w["h"]
                key = "shStepText_%dx%d" % (w["w"] - 2 * h - 6, h)
                defs.setdefault(key, _local(key, [_action("Mouse Down", "Q-Link"),
                                                  _action("Double Click", "Show Overlay", "knob overlay")],
                                            [_focus(w["w"] - 2 * h - 6, h),
                                             _value_label(8, 0, w["w"] - 2 * h - 22, h, 26.0, ACCENT)]))
                kids.append(_placed(key, name, i, x0 + h + 3, y0, w["w"] - 2 * h - 6, h))
                for side, (ax, ay, aw, ah) in zip(("prev", "next"), stepper_arrows(w)):
                    akey = "shTap_%dx%d" % (aw, ah)
                    defs.setdefault(akey, _local(akey, [_action("Enter Pressed", "Toggle Switch")],
                                                 [_button("", "", 1, 1, aw, ah)]))
                    kids.append(_placed(akey, "%s %s" % (name, side), index[w["key"] + "_" + side], ax, ay, aw, ah, focus="No"))
            elif kind == "list":
                for slot, ((x, y, tw, th), sk) in enumerate(zip(list_tiles(w), list_keys(w))):
                    img = "sh_tile_%dx%d" % (tw, th)
                    for state, border in (("on", 3), ("off", 0)):
                        script += ["clear|" + under(), "tile|%d|%d|%d|%d|%s|%s|%d" % (x, y, tw, th, LCD, SEG_ON if border else LINE, border),
                                   "crop|%s|%d|%d|%d|%d" % (art("%s_%s" % (img, state)), x, y, tw, th)]
                    key = "shRow_%dx%d" % (tw, th)
                    # the Value label lies over the button and takes the touch, so the row itself toggles on touch
                    defs.setdefault(key, _local(key, [_action("Mouse Down", "Toggle Switch"), _action("Enter Pressed", "Toggle Switch")],
                                                [_focus(tw, th), _button(img + "_on.png", img + "_off.png", 1, 1, tw, th),
                                                 _value_label(12, 0, tw - 24, th, 24.0, ACCENT, "left verticallyCentred")]))
                    kids.append(_placed(key, "%s %d" % (name, slot + 1), index[sk], x, y, tw, th, focus="Yes" if slot == 0 else "No"))
            else:  # enum_h / enum_v: radio group, one image button per option
                n = len(w["options"])
                for o, (x, y, sw, sh) in enumerate(seg_rects(w)):
                    img = "sh_seg_%s_%d" % (w["key"], o)
                    lab = w["options"][o]
                    for state, fill, ink in (("on", SEG_ON, SEG_ON_TX), ("off", SEG_OFF, INK_DIM)):
                        script += ["clear|" + under(), "seg|%d|%d|%d|%d|%s|%s|%s" % (x, y, sw, sh, fill, ink, lab),
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

    for img, sw_, sh_, vert in sorted(sliders):
        script.append("sstrip|%s|%d|%d|%d|%d|%s" % (art(img), sw_, sh_, FRAMES, 1 if vert else 0, under()))
    for r in sorted(radii):
        script.append("strip|%s|%d|%d|%s" % (art("sh_knob_r%d" % r), r, FRAMES, under()))
    subprocess.run([art_bin], input="\n".join(script) + "\n", text=True, check=True)
    for ppm, png in ppms:
        png_from_ppm(ppm, png)
    for img, sw_, sh_, vert in sliders:
        square_strip(os.path.join(skin_dir, img + ".png"), sw_, sh_)
    for f in os.listdir(work):
        os.remove(os.path.join(work, f))
    os.rmdir(work)
    return list(defs.values()), pages, qmap


def square_strip(path, w, h):
    """w x h frames -> square max(w,h) frames with the slider centred and transparent padding."""
    from PIL import Image
    src = Image.open(path).convert("RGBA")
    n, sq = src.size[1] // h, max(w, h)
    out = Image.new("RGBA", (sq, sq * n), (0, 0, 0, 0))
    for k in range(n):
        out.paste(src.crop((0, k * h, w, (k + 1) * h)), ((sq - w) // 2, k * sq + (sq - h) // 2))
    out.save(path)


def qlink_bounds(tab, keys):
    """Rectangle around the controls a page's Q-Links drive (plugin coords)."""
    xs, ys = [], []
    for w in tab["widgets"]:
        if w["kind"] == "list":
            for (x, y, tw, th), k in zip(list_tiles(w), list_keys(w)):
                if k in keys:
                    xs += [x, x + tw]
                    ys += [y, y + th]
            continue
        if w.get("key") not in keys:
            continue
        if w["kind"] in ("slider_v", "slider_h"):
            xs += [w["cx"] - max(65, w["w"] // 2), w["cx"] + max(65, w["w"] // 2)]
            ys += [w["cy"] - w["h"] // 2, w["cy"] + w["h"] // 2 + 56]
            continue
        if w["kind"] in ("readout", "stepper", "menu"):
            xs += [w["cx"] - w["w"] // 2, w["cx"] + w["w"] // 2]
            ys += [w["cy"] - w["h"] // 2 - 26, w["cy"] + w["h"] // 2]
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


AKAI = "/usr/share/Akai/Content/Synths/"


def program_qlinks(layout_path, params, qmap):
    """Q-Links outside page-follow (screen) mode: `qlinks_track = key,...` at the top of the layout
    (up to 16, same bank order as pages), else the first page's set."""
    index = {p["key"]: i for i, p in enumerate(params)}
    for line in parse_layout(layout_path)[1]:
        if line.startswith("qlinks_track"):
            keys = [k.strip() for k in line.split("=", 1)[1].split(",") if k.strip()]
            if len(keys) > 16:
                raise SystemExit("layout: qlinks_track has %d keys (max 16)" % len(keys))
            ql = {"Q-Link %d" % (q + 1): -1 for q in range(16)}
            for s_, k in enumerate(keys):
                if k not in index:
                    raise SystemExit("layout: qlinks_track key %r is not a parameter" % k)
                ql["Q-Link %d" % qlink_for_slot(s_)] = index[k]
            return ql
    return dict(qmap[0]["Q-Links"])


def write_skin(outdir, vendor, name, layout_path, params, art_bin):
    """Build the whole skin folder <outdir>/<vendor> - VST - <name>/ from a layout. Needs Pillow."""
    import json
    from PIL import Image
    d = os.path.join(outdir, "%s - VST - %s" % (vendor, name))
    skin = os.path.join(d, "Plugin Skins")
    os.makedirs(skin, exist_ok=True)
    comps, tabs, qmap = build(layout_path, params, skin, art_bin, lambda a, b: Image.open(a).save(b))
    tui = {"pageData": {
        "version": 1,
        "componentDefinitions": {"version": 2, "importFiles": [AKAI + "Generic/Generic Knob Overlay.json",
                                                              AKAI + "Generic/Generic Menu Overlay.json"],
                                 "localComponentDefinitions": comps},
        "info": {"version": 1, "type": "CompleteDescription"},
        "tabs": tabs}}
    qlinks = {"version": 4, "info": {"version": 1, "type": "CompleteDescription"},
              "Screen Mode Q-Links": {"version": 4, "map": qmap},
              "Program Mode Q-Links": program_qlinks(layout_path, params, qmap)}
    open(os.path.join(d, "version.xml"), "w").write(
        "<?xml version='1.0' encoding='utf-8'?>\n<plugincontent version=\"1.0\">\n"
        "\t<identifier>%s.vst.%s</identifier>\n\t<version>1.0.0.0</version>\n</plugincontent>\n"
        % (vendor, name.lower().replace(" ", "")))
    for f, obj in (("TUI.json", tui), ("Q-Links.json", qlinks), ("Q-Links - 8by1.json", qlinks)):
        json.dump(obj, open(os.path.join(skin, f), "w"), indent=4)
    return d
