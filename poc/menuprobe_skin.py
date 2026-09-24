#!/usr/bin/env python3
"""Skin for poc/menuprobe.so: native-picker rows + font-name rows.
   python3 poc/menuprobe_skin.py OUTDIR  ->  OUTDIR/sd88me - VST - MPC Menu Probe/
Top row: menu controls for params 0..3 (Props, VSTXML, Both, States); tap opens MPC's menu overlay.
Font rows, top to bottom, all showing param 4's fixed text:
  Titillium Web, Roboto, Liberation Mono, Liberation Serif, NoSuchFont (fallback check).
Right side: "IndexedEnabling/<i>/4/Parameter <p>" visibility test for params 1 (VSTXML, 4 states) and
0 (Props, no VSTXML states). Four stacked Value labels per param, one per index, in the option's colour;
only the one matching the current value should show.
Tab 2 "PICKER": self-drawn pop-up picker. The field shows param 2 (Both); tapping it toggles param 0
(Props) as the "open" flag; a panel of 4 option buttons (radio group on param 2) is visible only while
param 0 is on (IndexedEnabling/1/2/Parameter 0). A param-1 menu sits under the panel to check whether
touches pass through a covering component. Needs Pillow (option text is baked into the button PNGs)."""
import json, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import shadow_skin as S

FONTS = [("Titillium Web", "Regular"), ("Roboto", "Regular"), ("Liberation Mono", "Regular"),
         ("Liberation Serif", "Regular"), ("NoSuchFont", "Regular")]


def label(kind, x, y, w, h, font, style, size, colour, just="left verticallyCentred", handle="Data"):
    return S._sub("Label", {"version": 1, "textStyle": {"version": 1, "font": {"version": 1, "name": font,
                                                                            "style": style, "height": size},
                                                      "colour": "ff" + colour, "justification": just, "case": "Original"},
                            "type": kind, "handleName": handle}, S._bounds(x, y, w, h), kind)


def placed(ctype, name, index, x, y, w, h, focus="Yes"):
    return S._placed(ctype, name, index, x, y + S.Y_OFF, w, h, focus)


OPTS = ["RED", "GREEN", "BLUE", "YELLOW"]
PX, PY, PW, RH = 420, 150, 360, 80   # panel x/y/width, option row height


def picker_art(skin_dir):
    from PIL import Image, ImageDraw, ImageFont
    font = ImageFont.load_default(size=36)
    for i, o in enumerate(OPTS):
        for state, bg, fg in (("on", (59, 240, 255), (20, 20, 20)), ("off", (46, 45, 46), (230, 230, 230))):
            im = Image.new("RGB", (PW - 20, RH - 10), bg)
            d = ImageDraw.Draw(im)
            d.text((24, (RH - 10) // 2), o, font=font, fill=fg, anchor="lm")
            im.save(os.path.join(skin_dir, "pick_%d_%s.png" % (i, state)))
    im = Image.new("RGB", (PW, len(OPTS) * RH + 10), (20, 20, 20))
    ImageDraw.Draw(im).rectangle([0, 0, PW - 1, im.size[1] - 1], outline=(59, 240, 255), width=2)
    im.save(os.path.join(skin_dir, "pick_panel.png"))


def picker_defs(skin_dir):
    picker_art(skin_dir)
    fw, fh = 360, 80
    field = S._local("pickField", [S._action("Mouse Down", "Toggle Switch"), S._action("Enter Pressed", "Toggle Switch")],
                     [S._focus(fw, fh), label("Value", 16, 0, fw - 32, fh, "Roboto", "Regular", 40.0, "3bf0ff", handle="Text")])
    panel = S._local("pickPanel", [], [S._sub("Image", {"version": 2, "imageType": "Regular", "colour": "0",
                                                        "image": "pick_panel.png"}, S._bounds(0, 0, PW, len(OPTS) * RH + 10), "Image")])
    rows = []
    for i in range(len(OPTS)):
        key = "pickOpt%d" % i
        rows.append(S._local(key, [S._action("Mouse Down", "Q-Link")],
                             [S._button("pick_%d_on.png" % i, "pick_%d_off.png" % i, i, len(OPTS), PW - 20, RH - 10)]))
    under = S._local("pickUnder", [S._action("Mouse Down", "Show Overlay", "knob overlay")],
                     [S._focus(300, 80), label("Value", 16, 0, 268, 80, "Titillium Web", "SemiBold", 34.0, "ffe040")])
    kids = [placed("pickField", "Pick field", 0, 40, 40, fw, fh),
            placed("pickUnder", "Under panel", 1, PX + 30, PY + 120, 300, 80)]
    for k in [placed("pickPanel", "Panel", 2, PX, PY, PW, len(OPTS) * RH + 10, focus="No")] + \
             [placed("pickOpt%d" % i, "Opt %d" % i, 2, PX + 10, PY + 10 + i * RH, PW - 20, RH - 10, focus="No")
              for i in range(len(OPTS))]:
        k["bounds"]["showWhenDataModelInvalid"] = "Show"
        k["bounds"]["additionalInvalidatingHandles"] = ["IndexedEnabling/1/2/Parameter 0"]
        kids.append(k)
    kids[0]["handle remapping"]["map"].append({"key": "Text", "value": "Parameter 2"})
    page = S._local("pickerPage", [], kids)
    page["value"]["backgroundData"]["focussed"]["colour"] = "ff1e1d1e"
    page["value"]["backgroundData"]["unfocussed"]["colour"] = "ff1e1d1e"
    return [field, panel, under, page] + rows


def main(out):
    mw, mh = 280, 70
    menu = S._local("probeMenu", [S._action("Mouse Down", "Show Overlay", "menu overlay"),
                                  S._action("Double Click", "Show Overlay", "menu overlay"),
                                  S._action("Enter Pressed", "Show Overlay", "menu overlay")],
                    [S._focus(mw, mh + 34),
                     label("Name", 8, 0, mw - 16, 30, "Titillium Web", "SemiBold", 24.0, "a0a0a0"),
                     label("Value", 8, 34, mw - 16, mh, "Titillium Web", "SemiBold", 34.0, "3bf0ff")])
    kids = [placed("probeMenu", "Menu %d" % i, i, 30 + i * 310, 20, mw, mh + 34) for i in range(4)]
    defs = [menu]
    for n, (f, st) in enumerate(FONTS):
        key = "probeFont%d" % n
        defs.append(S._local(key, [], [label("Value", 0, 0, 600, 64, f, st, 44.0, "ffffff")]))
        kids.append(placed(key, "Font %d" % n, 4, 40, 150 + n * 90, 600, 64, focus="No"))
    colours = ["ff4040", "40ff60", "4080ff", "ffe040"]
    defs.append(S._local("probeCaption", [], [label("Name", 0, 0, 500, 40, "Titillium Web", "SemiBold", 26.0, "a0a0a0")]))
    for g, (param, y) in enumerate(((1, 150), (0, 330))):
        kids.append(placed("probeCaption", "Caption %d" % g, param, 700, y, 500, 40, focus="No"))
        for i, c in enumerate(colours):
            key = "probeEnable%d" % i
            if g == 0:
                defs.append(S._local(key, [], [label("Value", 0, 0, 500, 90, "Roboto", "Regular", 64.0, c)]))
            k = placed(key, "Enable %d/%d" % (param, i), param, 700, y + 45, 500, 90, focus="No")
            k["bounds"]["showWhenDataModelInvalid"] = "Show"
            k["bounds"]["additionalInvalidatingHandles"] = ["IndexedEnabling/%d/4/Parameter %d" % (i, param)]
            kids.append(k)
    page = S._local("probePage", [], kids)
    page["value"]["backgroundData"]["focussed"]["colour"] = "ff1e1d1e"
    page["value"]["backgroundData"]["unfocussed"]["colour"] = "ff1e1d1e"
    defs.append(page)
    skin_dir = os.path.join(out, "sd88me - VST - MPC Menu Probe", "Plugin Skins")
    os.makedirs(skin_dir, exist_ok=True)
    defs += picker_defs(skin_dir)
    tabs = [{"version": 3, "tabName": name, "fnKeyIndex": t, "fnKeySubIndex": 0,
             "qlinkBoundsData": ["0 0 1280 130"], "componentName": comp,
             "initialSize": "0 0 %d %d" % (S.W, S.H), "scale": 1.0}
            for t, (name, comp) in enumerate((("PROBE", "probePage"), ("PICKER", "pickerPage")))]
    ql = {"Q-Link %d" % (q + 1): -1 for q in range(16)}
    for slot, q in enumerate(S.KNOB_QLINKS[:4]):
        ql["Q-Link %d" % q] = slot
    tui = {"pageData": {"version": 1,
                        "componentDefinitions": {"version": 2, "importFiles": [S.AKAI + "Generic/Generic Knob Overlay.json",
                                                                              S.AKAI + "Generic/Generic Menu Overlay.json"],
                                                 "localComponentDefinitions": defs},
                        "info": {"version": 1, "type": "CompleteDescription"}, "tabs": tabs}}
    qlinks = {"version": 4, "info": {"version": 1, "type": "CompleteDescription"},
              "Screen Mode Q-Links": {"version": 4, "map": [{"Tab": t, "SubTab": 1, "Bank Direction": "Column", "Q-Links": ql}
                                                   for t in (1, 2)]},
              "Program Mode Q-Links": ql}
    d = os.path.join(out, "sd88me - VST - MPC Menu Probe")
    skin = os.path.join(d, "Plugin Skins")
    os.makedirs(skin, exist_ok=True)
    open(os.path.join(d, "version.xml"), "w").write(
        "<?xml version='1.0' encoding='utf-8'?>\n<plugincontent version=\"1.0\">\n"
        "\t<identifier>sd88me.vst.mpcmenuprobe</identifier>\n\t<version>1.0.0.0</version>\n</plugincontent>\n")
    for f, obj in (("TUI.json", tui), ("Q-Links.json", qlinks), ("Q-Links - 8by1.json", qlinks)):
        json.dump(obj, open(os.path.join(skin, f), "w"), indent=4)
    print(d)


if __name__ == "__main__":
    main(sys.argv[1])
