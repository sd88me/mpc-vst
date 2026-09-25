#!/usr/bin/env python3
"""Skin studio in the browser: a local editor for layout.conf (`studio.py serve`).

    studio.py serve layout.conf [--params params.json] [--port 8765] [--host 127.0.0.1]

Serves tools/studio_web/ and a small JSON API. The page edits the layout; this side reads and writes it and
draws every widget with the browser renderer's own SVG (html_art.Art, styled by tools/html_art/default.css and
the layout's art_css=), so the canvas shows what `"art": "html"` bakes. The shadow_art renderer (the default)
draws the same geometry with its bitmap font; `studio.py preview` on a built skin shows that exactly.

Only the layout's own folder is served (/files/...) and written (save, CSS, uploads); writes need an X-Studio
header, which a page from another site can't send without a CORS preflight this server never answers.
Standard library only.

API (JSON):
  GET  /api/doc                      the layout as a document (below), the parameters, renderer defaults
  POST /api/render {head, widgets}   -> {vars, td3, css, items: [{svg, live, box, open}]} per widget
  POST /api/parse  {line}            -> {w} (a widget line typed by hand)
  POST /api/save   {head, tabs}      writes the layout (x.new, then renamed over it; the first save keeps x.bak)
  POST /api/file   {name, text}      writes a text file next to the layout (the art_css stylesheet)
  POST /api/upload?name=F            raw body -> a file next to the layout (fonts, SVG artwork)

Document: {head: [raw lines before the first tab], tabs: [{name, lines: [item]}]}, where an item is
{t: "w", w: {...}, raw} (a widget), {t: "q", title, keys, raw} (a qlinks line) or {t: "x", raw} (comments,
blank lines). An item whose fields still match its raw line is written back as that line, so loading and
saving without edits reproduces the file exactly.
"""
import copy
import json
import os
import re
import sys
from html import escape
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, unquote, urlparse

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import html_art  # noqa: E402
import shadow_skin  # noqa: E402
import studio  # noqa: E402

WEB = os.path.join(HERE, "studio_web")
Y_OFF, W, H = shadow_skin.Y_OFF, shadow_skin.W, shadow_skin.H
THEME_GLOBALS = ("TD3", "FONT_LABEL_PATH") + tuple(shadow_skin.THEME_KEYS.values())
DEFAULTS = {k: getattr(shadow_skin, k) for k in THEME_GLOBALS}
TYPES = {".html": "text/html", ".js": "text/javascript", ".css": "text/css", ".svg": "image/svg+xml",
         ".ttf": "font/ttf", ".otf": "font/otf", ".woff": "font/woff", ".woff2": "font/woff2", ".png": "image/png",
         ".json": "application/json", ".conf": "text/plain", ".txt": "text/plain"}


# ---------------------------------------------------------------- the layout as a document

def load_doc(path):
    head, tabs = [], []
    for raw in open(path, encoding="utf-8").read().split("\n"):
        line = raw.strip()
        m = re.match(r"\[tab (.+)\]$", line)
        if m:
            tabs.append({"name": m.group(1).strip(), "raw": raw, "lines": []})
        elif not tabs:
            head.append(raw)
        elif not line or line.startswith("#") or line.startswith("qlinks_track"):
            tabs[-1]["lines"].append({"t": "x", "raw": raw})
        elif line.startswith("qlinks"):
            m = re.match(r'qlinks\s+"([^"]+)"\s*=\s*(.+)$', line)
            tabs[-1]["lines"].append({"t": "q", "title": m.group(1), "raw": raw,
                                      "keys": [k.strip() for k in m.group(2).split(",") if k.strip()]})
        else:
            tabs[-1]["lines"].append({"t": "w", "w": shadow_skin.parse_widget(line), "raw": raw})
    if tabs and tabs[-1]["lines"] and tabs[-1]["lines"][-1] == {"t": "x", "raw": ""}:
        tabs[-1]["lines"].pop()   # the file's final newline
    elif not tabs and head and head[-1] == "":
        head.pop()
    return {"head": head, "tabs": tabs}


def item_line(it):
    if it["t"] == "x":
        return it["raw"]
    if it["t"] == "q":
        line = 'qlinks "%s" = %s' % (it["title"], ",".join(it["keys"]))
        old = re.match(r'\s*qlinks\s+"([^"]+)"\s*=\s*(.+)$', it.get("raw") or "")
        if old and old.group(1) == it["title"] and [k.strip() for k in old.group(2).split(",") if k.strip()] == it["keys"]:
            return it["raw"]
        return line
    w = it["w"]
    if it.get("raw"):
        try:
            if shadow_skin.parse_widget(it["raw"].strip()) == w:
                return it["raw"]
        except ValueError:
            pass
    return studio.conf_line(w)


def dump_doc(doc):
    out = list(doc["head"])
    for tab in doc["tabs"]:
        m = re.match(r"\s*\[tab (.+)\]\s*$", tab.get("raw") or "")
        out.append(tab["raw"] if m and m.group(1).strip() == tab["name"] else "[tab %s]" % tab["name"])
        out += [item_line(it) for it in tab["lines"]]
    return "\n".join(out) + "\n"


def write_file(path, data):
    """x.new, then renamed over x; the first write ever keeps the original as x.bak."""
    mode = "wb" if isinstance(data, bytes) else "w"
    if os.path.exists(path) and not os.path.exists(path + ".bak"):
        with open(path, "rb") as src, open(path + ".bak", "wb") as bak:
            bak.write(src.read())
    with open(path + ".new", mode, **({} if mode == "wb" else {"encoding": "utf-8"})) as f:
        f.write(data)
    os.replace(path + ".new", path)


# ---------------------------------------------------------------- drawing widgets

def set_theme(head):
    """shadow_skin's palette for these top-level lines (from its defaults, so removed lines take effect)."""
    for k, v in DEFAULTS.items():
        setattr(shadow_skin, k, v)
    lines = [l.strip() for l in head if l.strip() and not l.strip().startswith("#") and "=" in l]
    art = html_art.Art()
    art.theme_lines([l for l in lines if not l.startswith("font_label=")])
    return art, lines


def live_text(x, y, w, h, text, size, colour, anchor="middle"):
    """MPC's own text (Titillium Web, drawn on the device over the skin): shown in the editor only."""
    tx = {"start": x, "middle": x + w / 2, "end": x + w}[anchor]
    return ('<text class="live" x="%g" y="%g" text-anchor="%s" dominant-baseline="central" '
            'style="font-size:%gpx;fill:#%s">%s</text>') % (tx, y + h / 2, anchor, size, colour, escape(text))


def resolve(w, params):
    """A copy of w with the options the builder would use (the parameter's, when the line gives none)."""
    w = copy.deepcopy(w)
    p = params.get(w.get("key"), {})
    if w["kind"] in ("enum_h", "enum_v", "popup") and not w.get("options"):
        opts = [str(o) for o in p.get("options") or []] or ["ONE", "TWO", "THREE"]
        w["options"] = [o.upper() for o in opts] if w["kind"] != "popup" else opts
    return w


def box_of(w):
    k = w["kind"]
    if k == "art":
        return [0, Y_OFF, W, H]
    if k in ("frame", "list"):
        return [w["x"], w["y"], w["w"], w["h"]]
    _, a = studio.shape_for(w)
    return [a.get("x", a.get("cx", 0) - a.get("r", 0)), a.get("y", a.get("cy", 0) - a.get("r", 0)) + Y_OFF,
            a.get("width", 2 * a.get("r", 0)), a.get("height", 2 * a.get("r", 0))]


def widget_svg(w, art, params, base_dir):
    """-> (baked SVG, live-text SVG, selection box, open-popup SVG) for one widget, in shadow coords."""
    ss = shadow_skin
    w = resolve(w, params)
    k, p = w["kind"], params.get(w.get("key"), {})
    art.ops = []
    live, opened = [], ""
    name = w.get("label") or p.get("name") or w.get("key", "")
    first = (w.get("options") or [""])[0]
    if k == "art":
        path = os.path.join(base_dir, w.get("file", ""))
        if os.path.isfile(path):
            art.svg_file(path, 0, Y_OFF, W, H)
    elif k in ("frame", "readout", "stepper", "menu", "popup", "list"):
        for c in ss.baked_cmds(w, None, base_dir):
            art.run(c)
    if k == "knob":
        r = w["r"]
        s, cw = 2 * r + 10, max(130, 2 * r + 10)
        x0, y0 = w["cx"] - cw // 2, w["cy"] - s // 2
        art.ops.append(art.knob_svg(w["cx"], w["cy"], r, 40))
        live += [live_text(x0, y0 + s // 2 + r + 2, cw, 20, name, 17, ss.INK),
                 live_text(x0, y0 + s // 2 + r + 24, cw, 26, "40", 22, ss.INK_DIM)]
    elif k in ("slider_v", "slider_h"):
        sw_, sh_ = w["w"], w["h"]
        sq, cw = max(sw_, sh_), max(130, max(sw_, sh_))
        x0, y0 = w["cx"] - cw // 2, w["cy"] - sq // 2
        art.ops.append(art.slider_svg(w["cx"] - sw_ // 2, w["cy"] - sh_ // 2, sw_, sh_, k == "slider_v", 0.4))
        name_y = y0 + (sq - sh_) // 2 + sh_ + 2
        live += [live_text(x0, name_y, cw, 20, name, 17, ss.INK), live_text(x0, name_y + 22, cw, 26, "40", 22, ss.INK_DIM)]
    elif k == "toggle":
        art.run("pill|%d|%d|0" % (w["cx"], w["cy"]))
        live.append(live_text(w["cx"] - 60, w["cy"] - 18 + 34, 120, 20, name, 15, ss.INK))
    elif k == "button":
        art.run("button|%d|%d|%s|%s" % (w["cx"], w["cy"], w.get("color") or ss.BTN_BG or ss.ACCENT, w.get("label", "")))
    elif k in ("enum_h", "enum_v"):
        for c in ss.label_cmds(w):
            art.run(c)
        for o, (x, y, sw, sh) in enumerate(ss.seg_rects(w)):
            on = o == 0
            art.run("seg|%d|%d|%d|%d|%s|%s|%s" % (x, y, sw, sh, ss.SEG_ON if on else ss.SEG_OFF,
                                                  ss.SEG_ON_TX if on else ss.INK_DIM, w["options"][o]))
    elif k in ("readout", "menu", "popup"):
        x, y, rw, rh = w["cx"] - w["w"] // 2, w["cy"] - w["h"] // 2, w["w"], w["h"]
        dot = k == "readout" and w.get("style") == "dotmatrix"
        live.append(live_text(x + 8, y, rw - (44 if k == "popup" else 16), rh, first if k != "readout" else name,
                              26, ss.DISPLAY_INK if dot else ss.ACCENT))
        if k == "popup":
            cx, cy = w["cx"] + w["w"] // 2 - 22, w["cy"]
            art.ops.append('<path d="M%d %d L%d %d L%d %d Z" style="fill:#%s"/>' % (
                cx - 8, cy - 4, cx + 8, cy - 4, cx, cy + 5, ss.ACCENT))
            (px, py, pw, ph), orects = ss.popup_panel(w)
            ops, art.ops = art.ops, []
            art.run("tile|%d|%d|%d|%d|%s|%s|2" % (px, py, pw, ph, ss.LCD, ss.ACCENT))
            for o, (ox, oy, ow, oh) in enumerate(orects):
                on = o == 0
                art.run("seg|%d|%d|%d|%d|%s|%s|%s" % (ox, oy, ow, oh, ss.SEG_ON if on else ss.LCD,
                                                      ss.SEG_ON_TX if on else ss.INK, w["options"][o]))
            opened, art.ops = "".join(art.ops), ops
    elif k == "stepper":
        h = w["h"]
        x0, y0 = w["cx"] - w["w"] // 2, w["cy"] - h // 2
        dot = w.get("style") == "dotmatrix"
        live.append(live_text(x0 + h + 11, y0, w["w"] - 2 * h - 22, h, name, 26, ss.DISPLAY_INK if dot else ss.ACCENT))
    elif k == "list":
        for i, (x, y, tw, th) in enumerate(ss.list_tiles(w)):
            live.append(live_text(x + 12, y, tw - 24, th, "%s %d" % (w.get("key", ""), i + 1), 24, ss.ACCENT, "start"))
    try:
        box = box_of(w)
    except (KeyError, ValueError):
        box = None
    return "".join(art.ops), "".join(live), box, opened


def render(head, widgets, params, base_dir):
    art, lines = set_theme(head)
    items = []
    for w in widgets:
        try:
            svg, live, box, opened = widget_svg(w, art, params, base_dir)
            items.append({"svg": svg, "live": live, "box": box, "open": opened})
        except Exception as e:   # a half-typed line: show the error on that widget, keep the rest
            msg = "needs %s=" % e.args[0] if isinstance(e, KeyError) else "%s: %s" % (type(e).__name__, e)
            items.append({"svg": "", "live": "", "box": None, "open": "", "error": msg})
    vars_ = ";".join("--%s:%s" % (k.replace("_", "-"), html_art.hexc(v)) for k, v in art.theme.items())
    css = [l.partition("=")[2].strip() for l in lines if l.startswith("art_css=")]
    return {"vars": vars_, "td3": art.td3, "css": css, "items": items}


# ---------------------------------------------------------------- server

class Studio:
    def __init__(self, layout, params_path=None):
        self.layout = os.path.abspath(layout)
        self.dir = os.path.dirname(self.layout)
        self.params, self.sections = [], []
        if params_path:
            ps, secs = studio.load_params(params_path)
            self.params = [{"key": p["key"], "name": p.get("name", p["key"]), "options": [str(o) for o in p.get("options") or []],
                            "hint": studio.kind_for(p)} for p in ps]
            self.sections = secs
        self.by_key = {p["key"]: p for p in self.params}

    def local(self, name):
        """A path inside the layout's folder, or None."""
        path = os.path.realpath(os.path.join(self.dir, unquote(name)))
        return path if path == self.dir or path.startswith(self.dir + os.sep) else None

    def files(self, exts):
        """Files with these extensions under the layout's folder (for the page's pickers)."""
        out = []
        for root, dirs, names in os.walk(self.dir):
            dirs[:] = [d for d in dirs if not d.startswith(".") and d not in ("build", "node_modules")]
            out += [os.path.join(root, n) for n in sorted(names) if os.path.splitext(n)[1].lower() in exts]
            if len(out) > 200:
                break
        return out

    def doc(self):
        if not os.path.exists(self.layout):
            d = {"head": [], "tabs": [{"name": "PAGE 1", "raw": "", "lines": []}]}
        else:
            d = load_doc(self.layout)
        d.update(path=self.layout, name=os.path.basename(self.layout), params=self.params, sections=self.sections,
                 theme=html_art.THEME, defs=html_art.DEFS, css=[os.path.relpath(p, self.dir) for p in self.files((".css",))],
                 art=[os.path.relpath(p, self.dir) for p in self.files((".svg",))],
                 fonts=[os.path.relpath(p, self.dir) for p in self.files((".ttf", ".otf", ".woff", ".woff2"))],
                 plugin={"w": W, "h": H, "y": Y_OFF})
        return d


def handler(st):
    class H(BaseHTTPRequestHandler):
        def log_message(self, fmt, *a):
            if "/api/" in (a[0] if a else ""):
                return
            sys.stderr.write("studio: " + fmt % a + "\n")

        def send(self, code, body, ctype="application/json"):
            if not isinstance(body, bytes):
                body = (json.dumps(body) if ctype == "application/json" else body).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", ctype + ("; charset=utf-8" if ctype.startswith("text") or "json" in ctype else ""))
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def static(self, root, rel):
            path = os.path.realpath(os.path.join(root, unquote(rel)))
            if not (path.startswith(os.path.realpath(root) + os.sep) and os.path.isfile(path)):
                return self.send(404, {"error": "not found"})
            with open(path, "rb") as f:
                self.send(200, f.read(), TYPES.get(os.path.splitext(path)[1].lower(), "application/octet-stream"))

        def do_GET(self):
            u = urlparse(self.path)
            if u.path == "/api/doc":
                return self.send(200, st.doc())
            if u.path == "/":
                return self.static(WEB, "index.html")
            for prefix, root in (("/web/", WEB), ("/html_art/", os.path.join(HERE, "html_art")), ("/files/", st.dir)):
                if u.path.startswith(prefix):
                    return self.static(root, u.path[len(prefix):])
            self.send(404, {"error": "not found"})

        def do_POST(self):
            u = urlparse(self.path)
            if self.headers.get("X-Studio") != "1":   # a custom header: another site's page can't send it here
                return self.send(403, {"error": "missing X-Studio header"})
            body = self.rfile.read(int(self.headers.get("Content-Length") or 0))
            try:
                if u.path == "/api/upload":
                    name = os.path.basename(parse_qs(u.query).get("name", [""])[0])
                    path = st.local(name) if name and not name.startswith(".") else None
                    if not path or os.path.splitext(name)[1].lower() not in (".svg", ".ttf", ".otf", ".woff", ".woff2", ".css"):
                        return self.send(400, {"error": "fonts (.ttf .otf .woff .woff2), .svg and .css only"})
                    write_file(path, body)
                    return self.send(200, {"name": name})
                req = json.loads(body or b"{}")
                if u.path == "/api/render":
                    return self.send(200, render(req.get("head", []), req.get("widgets", []), st.by_key, st.dir))
                if u.path == "/api/parse":
                    return self.send(200, {"w": shadow_skin.parse_widget(req["line"].strip())})
                if u.path == "/api/save":
                    text = dump_doc(req)
                    write_file(st.layout, text)
                    return self.send(200, {"saved": st.layout, "doc": st.doc()})
                if u.path == "/api/file":
                    path = st.local(req.get("name", ""))
                    if not path or os.path.splitext(path)[1].lower() not in (".css", ".svg"):
                        return self.send(400, {"error": "only .css and .svg files next to the layout"})
                    write_file(path, req.get("text", ""))
                    return self.send(200, {"name": req["name"]})
            except (ValueError, KeyError, IndexError) as e:
                return self.send(400, {"error": "%s: %s" % (type(e).__name__, e)})
            self.send(404, {"error": "not found"})
    return H


def serve(layout, params_path=None, host="127.0.0.1", port=8765):
    st = Studio(layout, params_path)
    srv = ThreadingHTTPServer((host, port), handler(st))
    print("skin studio: http://%s:%d/  (editing %s; Ctrl+C to stop)" % (host, srv.server_address[1], st.layout))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    srv.server_close()


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("layout")
    ap.add_argument("--params")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8765)
    a = ap.parse_args()
    serve(a.layout, a.params, a.host, a.port)
