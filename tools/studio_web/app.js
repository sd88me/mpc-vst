// Skin Studio editor (tools/studio_web.py serves it). Edits the layout document; the server draws each widget with
// the browser renderer's own SVG, so the canvas is the baked skin. Coordinates are the layout's (Force Shadow
// 1280x800, plugin area from y=86), which is the canvas viewBox, so no conversion anywhere.
"use strict";

const $ = (s, el = document) => el.querySelector(s);
const $$ = (s, el = document) => [...el.querySelectorAll(s)];
const SVGNS = "http://www.w3.org/2000/svg";
const PX = { x: 0, y: 86, w: 1280, h: 628 };   // the plugin area, in layout coordinates

// ---------------------------------------------------------------- widget kinds

const CONTROLS = ["knob", "slider_v", "slider_h", "toggle", "button", "enum_h", "enum_v", "readout", "stepper", "list", "menu", "popup"];
const PALETTE = [
  ["knob", "Knob"], ["slider_v", "V slider"], ["slider_h", "H slider"], ["toggle", "Toggle"], ["button", "Button"],
  ["enum_v", "Selector ↕"], ["enum_h", "Segments ↔"], ["popup", "Popup"], ["readout", "Readout"], ["stepper", "Stepper"],
  ["list", "List"], ["menu", "Menu"], ["frame", "Frame"],
];
const TEMPLATE = {   // new widgets, centred on the plugin area (the fields of tools/skin_template.conf)
  knob: { r: 36 }, slider_v: { w: 40, h: 170 }, slider_h: { w: 200, h: 36 }, toggle: {}, button: {},
  enum_v: {}, enum_h: {}, readout: { w: 300, h: 40 }, popup: { w: 220, h: 40 }, stepper: { w: 300, h: 52 },
  menu: { w: 220, h: 40 }, list: { w: 600, h: 188, cols: 2, rows: 4, th: 44, gap: 4 }, frame: { w: 400, h: 300 },
};
// inspector fields per kind: [field, label, type]; types: n (int), on (optional int), s (text), key, opts, when, sel:<a,b>, color, file
const GEOM = {
  frame: [["x", "x", "n"], ["y", "y", "n"], ["w", "width", "n"], ["h", "height", "n"]],
  list: [["x", "x", "n"], ["y", "y", "n"], ["w", "width", "n"], ["h", "height", "n"], ["cols", "columns", "n"], ["rows", "rows", "n"], ["th", "row height", "n"], ["gap", "gap", "n"]],
  knob: [["cx", "centre x", "n"], ["cy", "centre y", "n"], ["r", "radius", "n"]],
  toggle: [["cx", "centre x", "n"], ["cy", "centre y", "n"]],
  button: [["cx", "centre x", "n"], ["cy", "centre y", "n"]],
  enum_v: [["cx", "centre x", "n"], ["cy", "centre y", "n"]],
  enum_h: [["cx", "centre x", "n"], ["cy", "centre y", "n"], ["sw", "segment width", "on"], ["rows", "rows", "on"]],
  popup: [["cx", "centre x", "n"], ["cy", "centre y", "n"], ["w", "width", "n"], ["h", "height", "n"], ["cols", "list columns", "on"]],
  art: [],
};
for (const k of ["slider_v", "slider_h", "readout", "stepper", "menu"]) GEOM[k] = [["cx", "centre x", "n"], ["cy", "centre y", "n"], ["w", "width", "n"], ["h", "height", "n"]];
const TEXT = {
  frame: [["title", "title", "s"]],
  art: [["file", "SVG file", "file"]],
  button: [["label", "label", "s"], ["key", "parameter", "key"], ["color", "colour", "color"]],
  enum_h: [["label", "group label", "s"], ["key", "parameter", "key"], ["options", "options (comma separated; empty = the parameter's)", "opts"]],
  enum_v: [["label", "group label", "s"], ["key", "parameter", "key"], ["options", "options (comma separated; empty = the parameter's)", "opts"]],
  popup: [["label", "label", "s"], ["key", "parameter", "key"], ["options", "options (empty = the parameter's)", "opts"]],
  readout: [["label", "label", "s"], ["key", "parameter", "key"], ["style", "style", "sel:,dotmatrix"]],
  stepper: [["label", "label", "s"], ["key", "parameter", "key"], ["style", "style", "sel:,dotmatrix"],
            ["prev", "prev parameter (default <key>_prev)", "key"], ["next", "next parameter (default <key>_next)", "key"],
            ["get", "text from parameter (optional)", "key"]],
  list: [["key", "row parameters (<key>_1 … <key>_N)", "s"]],
};
for (const k of ["knob", "slider_v", "slider_h", "toggle", "menu"]) TEXT[k] = [["label", "label", "s"], ["key", "parameter", "key"]];

// ---------------------------------------------------------------- state

const S = {
  doc: null, tab: 0, sel: [], undo: [], redo: [], dirty: false, items: [], themeKey: "",
  cache: new Map(), mode: {}, qset: 0, pick: -1, panel: "inspect",
  view: { live: true, bounds: false, qlinks: true, slots: false, modes: false },
  css: { name: "", text: null, saved: null }, renderSeq: 0,
};

const tabObj = () => S.doc.tabs[S.tab];
const lines = () => tabObj().lines;
const W = i => lines()[i].w;
const widgetIdx = () => lines().map((it, i) => (it.t === "w" ? i : -1)).filter(i => i >= 0);
const params = () => S.doc.params || [];
const param = k => params().find(p => p.key === k);
const clone = o => JSON.parse(JSON.stringify(o));
const snapshot = () => JSON.stringify({ head: S.doc.head, tabs: S.doc.tabs, tab: S.tab });

function toast(msg, err) {
  const t = $("#toast");
  t.textContent = msg;
  t.className = "ui-toast" + (err ? " ui-error" : "");
  t.hidden = false;
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => (t.hidden = true), err ? 6000 : 2200);
}

async function api(path, body, raw) {
  const r = await fetch(path, body === undefined ? {} : { method: "POST", headers: { "X-Studio": "1" }, body: raw ? body : JSON.stringify(body) });
  const d = await r.json().catch(() => ({ error: r.statusText }));
  if (!r.ok) throw new Error(d.error || r.statusText);
  return d;
}

// ---------------------------------------------------------------- editing (every change goes through edit())

function edit(fn, opts = {}) {
  if (!opts.noUndo) {
    S.undo.push(opts.snap || snapshot());
    if (S.undo.length > 200) S.undo.shift();
    S.redo = [];
  }
  fn();
  setDirty(true);
  refresh(opts);
}

function restore(snap) {
  const d = JSON.parse(snap);
  S.doc.head = d.head;
  S.doc.tabs = d.tabs;
  S.tab = Math.min(d.tab, S.doc.tabs.length - 1);
  S.sel = S.sel.filter(i => lines()[i] && lines()[i].t === "w");
  setDirty(true);
  refresh();
}

function undo() { if (S.undo.length) { S.redo.push(snapshot()); restore(S.undo.pop()); } }
function redo() { if (S.redo.length) { S.undo.push(snapshot()); restore(S.redo.pop()); } }

function setDirty(d) {
  S.dirty = d;
  $("#dirty").hidden = !d;
  $("#undo").disabled = !S.undo.length;
  $("#redo").disabled = !S.redo.length;
}

// top-level lines (theme_*, style=, art_css=, qlinks_track)
function headGet(k) {
  const l = S.doc.head.find(l => l.trim().startsWith(k + "="));
  return l === undefined ? undefined : l.trim().slice(k.length + 1).trim();
}
function headSet(k, v) {
  const i = S.doc.head.findIndex(l => l.trim().startsWith(k + "="));
  if (v === undefined || v === "") { if (i >= 0) S.doc.head.splice(i, 1); }
  else if (i >= 0) S.doc.head[i] = k + "=" + v;
  else S.doc.head.push(k + "=" + v);
}
// qlinks_track's own spelling is "qlinks_track = a,b" (spaces around =)
function trackGet() {
  const l = S.doc.head.find(l => /^\s*qlinks_track\s*=/.test(l));
  return l === undefined ? undefined : l.split("=").slice(1).join("=").trim();
}
function trackSet(v) {
  const i = S.doc.head.findIndex(l => /^\s*qlinks_track\s*=/.test(l));
  if (!v) { if (i >= 0) S.doc.head.splice(i, 1); } else if (i >= 0) S.doc.head[i] = "qlinks_track = " + v; else S.doc.head.push("qlinks_track = " + v);
}

// ---------------------------------------------------------------- modes (when=<param>:<option>)

function modeParams() {
  const m = new Map();
  for (const i of widgetIdx()) {
    const wh = W(i).when;
    if (!wh) continue;
    const [k, o] = wh.split(":");
    if (!m.has(k)) m.set(k, new Set());
    m.get(k).add(o);
  }
  const out = [];
  for (const [k, seen] of m) {
    const p = param(k);
    out.push({ key: k, options: p && p.options.length ? p.options : [...seen] });
  }
  return out;
}

function shownInMode(w) {
  if (!w.when) return true;
  const [k, o] = w.when.split(":");
  const mp = modeParams().find(m => m.key === k);
  const opts = mp ? mp.options.map(x => String(x).toLowerCase()) : [];
  const cur = S.mode[k] || 0;
  const want = opts.indexOf(String(o).toLowerCase());
  return (want >= 0 ? want : /^\d+$/.test(o) ? +o : -1) === cur;
}

// ---------------------------------------------------------------- rendering

async function refresh(opts = {}) {
  renderTabs();
  renderModes();
  renderLayers();
  await renderCanvas();
  if (!opts.keepPanel || S.panel === "checks") renderPanel();   // after the canvas: it shows the render's errors
  renderChecksBadge();
}

async function renderCanvas() {
  const seq = ++S.renderSeq;
  const idx = widgetIdx();
  const themeKey = JSON.stringify(S.doc.head);
  const need = [];
  for (const i of idx) {
    const key = themeKey + JSON.stringify(W(i));
    if (!S.cache.has(key)) need.push([key, W(i)]);
  }
  if (need.length) {
    try {
      const r = await api("/api/render", { head: S.doc.head, widgets: need.map(n => n[1]) });
      need.forEach(([key], j) => S.cache.set(key, r.items[j]));
      applyTheme(r);
    } catch (e) { toast("Render: " + e.message, true); return; }
    if (S.cache.size > 4000) S.cache.clear();
  } else if (themeKey !== S.themeKey) {
    try { applyTheme(await api("/api/render", { head: S.doc.head, widgets: [] })); } catch (e) { toast(e.message, true); }
  }
  S.themeKey = themeKey;
  if (seq !== S.renderSeq) return;
  S.items = [];
  const gw = $("#l-widgets"), gl = $("#l-live");
  let wh = "", lh = "";
  // frames, art and text boxes are baked into the page background and MPC places the controls over it: draw
  // the non-controls first, each group in file order (so a frame's click area never covers a control)
  const order = [...idx.filter(i => !CONTROLS.includes(W(i).kind)), ...idx.filter(i => CONTROLS.includes(W(i).kind))];
  for (const i of order) {
    const it = S.cache.get(themeKey + JSON.stringify(W(i)));
    S.items[i] = it;
    const w = W(i);
    const vis = shownInMode(w) ? "" : S.view.modes ? " ui-dim" : " ui-hide";
    const art = w.kind === "art" ? " ui-art" : "";
    const b = it.box;
    const hit = b && w.kind !== "art" ? `<rect class="ui-hit" data-i="${i}" x="${b[0]}" y="${b[1]}" width="${b[2]}" height="${b[3]}"/>` : "";
    wh += `<g class="ui-w${vis}${art}" data-i="${i}">${it.svg}${hit}</g>`;
    lh += `<g class="ui-w${vis}" data-i="${i}">${it.live}</g>`;
  }
  gw.innerHTML = wh;
  gl.innerHTML = lh;
  gl.style.display = S.view.live ? "" : "none";
  renderOverlay();
}

function applyTheme(r) {
  const c = $("#canvas");
  c.setAttribute("style", r.vars);
  c.classList.toggle("td3", !!r.td3);
  // the layout's stylesheet(s), after default.css; while the Style panel edits one, its text replaces the file
  const want = r.css.map(n => (S.css.text !== null && n === S.css.name ? "live:" + n : n));
  const have = $$("[data-artcss]").map(el => el.dataset.artcss);
  if (want.join("|") !== have.join("|")) {
    $$("[data-artcss]").forEach(el => el.remove());
    for (const n of want) {
      let el;
      if (n.startsWith("live:")) { el = document.createElement("style"); el.textContent = rebase(S.css.text, n.slice(5)); }
      else { el = document.createElement("link"); el.rel = "stylesheet"; el.href = "/files/" + n + "?t=" + Date.now(); }
      el.dataset.artcss = n;
      document.head.appendChild(el);
    }
  }
}

// a stylesheet shown inline: its url(...)s are relative to its file
function rebase(css, name) {
  const dir = "/files/" + name.split("/").slice(0, -1).map(s => s + "/").join("");
  return css.replace(/url\(\s*(['"]?)(?!data:|https?:|\/)([^'")]+)\1\s*\)/g, (m, q, u) => `url("${dir}${u}")`);
}

function liveCss() {
  const el = $$("style[data-artcss]").find(e => e.dataset.artcss === "live:" + S.css.name);
  if (el) el.textContent = rebase(S.css.text, S.css.name);
}

function renderOverlay() {
  const o = $("#l-over");
  let h = "";
  if (S.view.slots) {   // studio.py auto's grid: 8 slots of 158 px from x=8, rows at y=92 and y=404 (304 tall)
    for (let r = 0; r < 2; r++) for (let s = 0; s < 8; s++) {
      const x = 8 + s * 158, y = 92 + r * 312;
      h += `<rect class="ui-slot" x="${x}" y="${y}" width="158" height="304"/><text class="ui-slot-n" x="${x + 6}" y="${y + 16}">Q${r * 8 + s + 1}</text>`;
    }
  }
  const bad = new Set(checks().filter(c => c.level === "error" && c.i !== undefined && c.tab === S.tab).map(c => c.i));
  for (const i of widgetIdx()) {
    const it = S.items[i], w = W(i);
    if (!it || !it.box || w.kind === "art" || !shownInMode(w)) continue;
    const b = it.box;
    if (S.view.bounds) h += `<rect class="ui-bound" x="${b[0]}" y="${b[1]}" width="${b[2]}" height="${b[3]}"/>`;
    if (bad.has(i)) h += `<rect class="ui-bad" x="${b[0] - 3}" y="${b[1] - 3}" width="${b[2] + 6}" height="${b[3] + 6}"/>`;
  }
  if (S.view.qlinks) {
    const set = qlinkSets()[S.qset];
    if (set) set.keys.forEach((k, s) => {
      const i = widgetIdx().find(i => W(i).key === k && shownInMode(W(i)));
      if (i === undefined || !S.items[i] || !S.items[i].box) return;
      const b = S.items[i].box;
      h += `<g class="ui-q"><circle cx="${b[0] - 2}" cy="${b[1] - 2}" r="10"/><text x="${b[0] - 2}" y="${b[1] + 2}" text-anchor="middle">${s + 1}</text></g>`;
    });
  }
  for (const i of S.sel) {
    const it = S.items[i];
    if (!it) continue;
    if (W(i).kind === "popup" && S.sel.length === 1 && it.open) h += `<g>${it.open}</g>`;
    if (!it.box) continue;
    const b = it.box;
    h += `<rect class="ui-selbox" x="${b[0] - 3}" y="${b[1] - 3}" width="${b[2] + 6}" height="${b[3] + 6}"/>`;
    if (S.sel.length === 1 && resizable(W(i))) h += `<rect class="ui-handle" data-handle="${i}" x="${b[0] + b[2] - 3}" y="${b[1] + b[3] - 3}" width="10" height="10"/>`;
  }
  o.innerHTML = h;
  const s = S.sel.length === 1 && S.items[S.sel[0]] && S.items[S.sel[0]].box;
  $("#status").textContent = S.sel.length > 1 ? `${S.sel.length} selected` :
    s ? `${W(S.sel[0]).kind}  x ${s[0]}  y ${s[1]}  (plugin y ${s[1] - PX.y})  ${s[2]} × ${s[3]}` +
        (S.items[S.sel[0]].error ? "  —  " + S.items[S.sel[0]].error : "") :
    `${lines().filter(l => l.t === "w").length} widgets on this tab · drag to move, corner handle to resize, arrows nudge (Shift ×10)`;
}

const resizable = w => !["toggle", "button", "enum_v", "art"].includes(w.kind) && !(w.kind === "enum_h" && !w.sw && !w.options && !param(w.key));

// ---------------------------------------------------------------- tabs, modes, layers

function renderTabs() {
  const t = $("#tabs");
  t.innerHTML = "";
  S.doc.tabs.forEach((tab, i) => {
    const b = document.createElement("button");
    b.textContent = tab.name;
    b.className = i === S.tab ? "ui-on" : "";
    b.title = "Double-click to rename";
    b.onclick = () => { if (S.tab !== i) { S.tab = i; S.sel = []; S.qset = 0; S.pick = -1; refresh(); } };
    b.ondblclick = () => renameTab(i);
    t.appendChild(b);
  });
  const add = document.createElement("button");
  add.textContent = "+";
  add.title = "Add a tab";
  add.onclick = () => {
    const name = prompt("New tab name", "PAGE " + (S.doc.tabs.length + 1));
    if (name) edit(() => { S.doc.tabs.push({ name: name.trim(), raw: "", lines: [] }); S.tab = S.doc.tabs.length - 1; S.sel = []; });
  };
  t.appendChild(add);
}

function renameTab(i) {
  const name = prompt("Tab name", S.doc.tabs[i].name);
  if (name && name.trim() !== S.doc.tabs[i].name) edit(() => (S.doc.tabs[i].name = name.trim()));
}

function renderModes() {
  const el = $("#modes");
  const mps = modeParams();
  el.hidden = !mps.length;
  if (!mps.length) return;
  el.innerHTML = "<span>Mode:</span>";
  for (const mp of mps) {
    const span = document.createElement("span");
    span.textContent = mp.key;
    const seg = document.createElement("span");
    seg.className = "ui-seg";
    mp.options.forEach((o, oi) => {
      const b = document.createElement("button");
      b.textContent = o;
      b.className = (S.mode[mp.key] || 0) === oi ? "ui-on" : "";
      b.onclick = () => { S.mode[mp.key] = oi; refresh({ keepPanel: true }); };
      seg.appendChild(b);
    });
    span.appendChild(seg);
    el.appendChild(span);
  }
}

function describe(w) {
  return w.kind === "frame" ? w.title || "frame" : w.kind === "art" ? w.file : w.label || w.key || "";
}

function renderLayers() {
  const ol = $("#layers");
  ol.innerHTML = "";
  for (const i of widgetIdx()) {
    const w = W(i);
    const li = document.createElement("li");
    li.className = (S.sel.includes(i) ? "ui-on" : "") + (shownInMode(w) ? "" : " ui-off");
    li.innerHTML = `<span class="ui-kind">${w.kind}</span><span></span>${w.when ? `<span class="ui-when">${esc(w.when)}</span>` : ""}`;
    li.children[1].textContent = describe(w);
    li.onclick = e => select(i, e.shiftKey || e.metaKey || e.ctrlKey);
    ol.appendChild(li);
  }
}

const esc = s => String(s).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);

function select(i, add) {
  if (S.pick >= 0 && i !== null) return assignPick(i);
  if (i === null) S.sel = [];
  else if (add) S.sel = S.sel.includes(i) ? S.sel.filter(x => x !== i) : [...S.sel, i];
  else S.sel = [i];
  renderLayers();
  renderOverlay();
  if (S.panel === "inspect") renderPanel();
}

// ---------------------------------------------------------------- canvas interaction

function svgPoint(e) {
  const c = $("#canvas");
  const p = c.createSVGPoint();
  p.x = e.clientX; p.y = e.clientY;
  return p.matrixTransform(c.getScreenCTM().inverse());
}
const snapV = v => { const s = +$("#snap").value; return Math.round(v / s) * s; };

function moveWidget(w, dx, dy) {
  if ("cx" in w) { w.cx += dx; w.cy += dy; } else if ("x" in w) { w.x += dx; w.y += dy; }
}

function setupCanvas() {
  const c = $("#canvas");
  let drag = null;
  c.addEventListener("pointerdown", e => {
    if (e.button !== 0) return;
    const p = svgPoint(e);
    const h = e.target.dataset && e.target.dataset.handle;
    if (h !== undefined) {
      const i = +h;
      drag = { type: "resize", i, p0: p, w0: clone(W(i)), snap: snapshot(), last: null };
    } else if (e.target.classList.contains("ui-hit")) {
      const i = +e.target.dataset.i;
      if (S.pick >= 0) { assignPick(i); return; }
      if (e.shiftKey) { select(i, true); return; }
      if (!S.sel.includes(i)) select(i, false);
      drag = { type: "move", p0: p, snap: snapshot(), dx: 0, dy: 0 };
    } else {
      if (!e.shiftKey) select(null);
      drag = { type: "marquee", p0: p, add: e.shiftKey };
    }
    c.setPointerCapture(e.pointerId);
  });
  c.addEventListener("pointermove", e => {
    if (!drag) return;
    const p = svgPoint(e);
    let dx = p.x - drag.p0.x, dy = p.y - drag.p0.y;
    if (drag.type === "move") {
      if (e.shiftKey) { if (Math.abs(dx) > Math.abs(dy)) dy = 0; else dx = 0; }   // Shift: one axis
      drag.dx = snapV(dx); drag.dy = snapV(dy);
      for (const g of $$(".ui-w", c)) if (S.sel.includes(+g.dataset.i)) g.setAttribute("transform", `translate(${drag.dx} ${drag.dy})`);
      $("#l-over").setAttribute("transform", `translate(${drag.dx} ${drag.dy})`);
      const b = S.items[S.sel[0]] && S.items[S.sel[0]].box;
      if (b) $("#status").textContent = `x ${b[0] + drag.dx}  y ${b[1] + drag.dy}  (Δ ${drag.dx}, ${drag.dy})`;
    } else if (drag.type === "resize") {
      const w = resized(drag.w0, snapV(dx), snapV(dy));
      if (JSON.stringify(w) !== drag.last) {
        drag.last = JSON.stringify(w);
        lines()[drag.i].w = w;
        if (!drag.busy) { drag.busy = true; renderCanvas().then(() => (drag && (drag.busy = false))); }
      }
    } else {
      const x = Math.min(p.x, drag.p0.x), y = Math.min(p.y, drag.p0.y);
      drag.rect = [x, y, Math.abs(dx), Math.abs(dy)];
      let m = $(".ui-marquee", c);
      if (!m) { m = document.createElementNS(SVGNS, "rect"); m.setAttribute("class", "ui-marquee"); $("#l-over").appendChild(m); }
      m.setAttribute("x", x); m.setAttribute("y", y); m.setAttribute("width", Math.abs(dx)); m.setAttribute("height", Math.abs(dy));
    }
  });
  const end = () => {
    if (!drag) return;
    const d = drag;
    drag = null;
    $("#l-over").removeAttribute("transform");
    $$(".ui-w", c).forEach(g => g.removeAttribute("transform"));
    if (d.type === "move" && (d.dx || d.dy)) {
      edit(() => S.sel.forEach(i => moveWidget(W(i), d.dx, d.dy)), { snap: d.snap });
    } else if (d.type === "resize" && d.last) {
      edit(() => {}, { snap: d.snap });
    } else if (d.type === "marquee" && d.rect && d.rect[2] > 3) {
      const [x, y, w, h] = d.rect;
      const inside = i => {
        const b = S.items[i] && S.items[i].box;
        return b && W(i).kind !== "art" && shownInMode(W(i)) && b[0] >= x && b[1] >= y && b[0] + b[2] <= x + w && b[1] + b[3] <= y + h;
      };
      S.sel = [...new Set([...(d.add ? S.sel : []), ...widgetIdx().filter(inside)])];
      renderLayers(); renderOverlay();
      if (S.panel === "inspect") renderPanel();
    } else renderOverlay();
  };
  c.addEventListener("pointerup", end);
  c.addEventListener("pointercancel", end);
}

// a resize by (dx, dy) of the bottom-right corner; the top-left stays put
function resized(w0, dx, dy) {
  const w = clone(w0);
  const k = w.kind;
  if (k === "knob") w.r = Math.max(12, w0.r + Math.round((dx + dy) / 2));
  else if (k === "frame" || k === "list") {
    w.w = Math.max(20, w0.w + dx);
    w.h = Math.max(20, w0.h + dy);
    if (k === "list") w.th = Math.max(10, Math.round((w.h - (w.rows - 1) * w.gap) / w.rows));
  } else if (k === "enum_h") {
    const n = (w.options || (param(w.key) || {}).options || [1, 2, 3]).length, per = Math.ceil(n / (w.rows || 1));
    w.sw = Math.max(20, (w0.sw || 117) + Math.round(dx / per));
  } else if ("w" in w) {
    w.w = Math.max(16, w0.w + dx);
    w.h = Math.max(16, w0.h + dy);
    w.cx = w0.cx + Math.round((w.w - w0.w) / 2);
    w.cy = w0.cy + Math.round((w.h - w0.h) / 2);
  }
  return w;
}

function setupKeys() {
  document.addEventListener("keydown", e => {
    const typing = /INPUT|TEXTAREA|SELECT/.test(document.activeElement.tagName);
    const mod = e.ctrlKey || e.metaKey;
    if (mod && e.key.toLowerCase() === "s") { e.preventDefault(); save(); return; }
    if (typing) return;
    if (mod && e.key.toLowerCase() === "z") { e.preventDefault(); e.shiftKey ? redo() : undo(); }
    else if (mod && e.key.toLowerCase() === "y") { e.preventDefault(); redo(); }
    else if (mod && e.key.toLowerCase() === "d") { e.preventDefault(); duplicate(); }
    else if (mod && e.key.toLowerCase() === "a") { e.preventDefault(); S.sel = widgetIdx().filter(i => W(i).kind !== "art" && shownInMode(W(i))); renderLayers(); renderOverlay(); renderPanel(); }
    else if (e.key === "Delete" || e.key === "Backspace") { e.preventDefault(); remove(); }
    else if (e.key === "Escape") { S.pick = -1; select(null); if (S.panel === "qlinks") renderPanel(); }
    else if (e.key.startsWith("Arrow") && S.sel.length) {
      e.preventDefault();
      const n = e.shiftKey ? 10 : 1;
      const [dx, dy] = { ArrowLeft: [-n, 0], ArrowRight: [n, 0], ArrowUp: [0, -n], ArrowDown: [0, n] }[e.key];
      // consecutive nudges are one undo step
      const now = Date.now(), coalesce = now - (setupKeys.last || 0) < 800;
      setupKeys.last = now;
      edit(() => S.sel.forEach(i => moveWidget(W(i), dx, dy)), { noUndo: coalesce, keepPanel: false });
    }
  });
}

// ---------------------------------------------------------------- add / duplicate / delete / order

function shortLabel(name, n) {
  let t = String(name).split(">").pop().trim().toUpperCase().replace(/[^A-Z0-9 .\-/%+:#]/g, " ");
  return t.split(/\s+/).join(" ").slice(0, n);
}

function usedKeys() {
  const s = new Set();
  for (const tab of S.doc.tabs) for (const it of tab.lines) if (it.t === "w" && it.w.key) s.add(it.w.key);
  return s;
}

function addWidget(kind) {
  const t = TEMPLATE[kind];
  const w = { kind };
  const cx = 640, cy = 400;
  if (kind === "frame" || kind === "list") Object.assign(w, { x: cx - t.w / 2, y: cy - t.h / 2 }, t);
  else Object.assign(w, { cx, cy }, t);
  if (kind === "frame") w.title = "FRAME";
  else {
    const used = usedKeys();
    const free = params().filter(p => !used.has(p.key) && !p.key.endsWith("__open"));
    const p = free.find(p => p.hint === kind) || free.find(p => kind.startsWith("enum") ? p.options.length : !p.options.length) || free[0];
    if (kind !== "list") w.label = p ? shortLabel(p.name, kind === "button" ? 7 : 12) : kind.toUpperCase().replace("_", " ");
    w.key = p ? p.key : "param_" + kind;
  }
  // the builder's field order: geometry first, then label, then key (conf_line writes it that way too)
  let at = lines().length;
  const lastW = widgetIdx().pop();
  if (lastW !== undefined) at = lastW + 1;
  edit(() => { lines().splice(at, 0, { t: "w", w, raw: "" }); S.sel = [at]; });
  if (S.panel !== "inspect") showPanel("inspect");
}

function duplicate() {
  if (!S.sel.length) return;
  edit(() => {
    const copies = S.sel.slice().sort((a, b) => a - b).map(i => { const w = clone(W(i)); moveWidget(w, 16, 16); return { t: "w", w, raw: "" }; });
    const at = Math.max(...S.sel) + 1;
    lines().splice(at, 0, ...copies);
    S.sel = copies.map((_, j) => at + j);
  });
}

function remove() {
  if (!S.sel.length) return;
  edit(() => {
    for (const i of S.sel.slice().sort((a, b) => b - a)) lines().splice(i, 1);
    S.sel = [];
  });
}

function reorder(dir) {
  if (S.sel.length !== 1) return;
  const idx = widgetIdx(), pos = idx.indexOf(S.sel[0]), to = idx[pos + dir];
  if (to === undefined) return;
  edit(() => {
    const L = lines(), a = S.sel[0];
    [L[a], L[to]] = [L[to], L[a]];
    S.sel = [to];
  });
}

// ---------------------------------------------------------------- right panels

function showPanel(p) {
  S.panel = p;
  $$("#paneltabs button").forEach(b => b.classList.toggle("ui-on", b.dataset.p === p));
  $$(".ui-panel").forEach(el => (el.hidden = el.id !== "p-" + p));
  if (p !== "qlinks") S.pick = -1;
  renderPanel();
}

function renderPanel() {
  ({ inspect: renderInspect, qlinks: renderQlinks, theme: renderTheme, css: renderCss, checks: renderChecks })[S.panel]();
}

function field(label, input, cls = "") {
  const f = document.createElement("label");
  f.className = "ui-field " + cls;
  const s = document.createElement("span");
  s.textContent = label;
  f.append(s, input);
  return f;
}

function el(tag, props = {}, ...kids) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(props)) {
    if (k === "style") e.setAttribute("style", v);
    else if (k.startsWith("on")) e[k] = v;
    else if (k in e) e[k] = v;
    else e.setAttribute(k, v);
  }
  e.append(...kids.filter(k => k !== null && k !== undefined));
  return e;
}

// one inspector edit = one undo step, however many keystrokes it takes
function bindEdit(input, apply, evt = "input") {
  let snap = null;
  input.addEventListener("focus", () => (snap = snapshot()));
  input.addEventListener(evt, () => {
    const s = snap;
    snap = null;
    edit(() => apply(input), { snap: s || undefined, noUndo: s === null && evt === "input", keepPanel: true });
    syncLine();
  });
  input.addEventListener("blur", () => (snap = null));
}

// the inspector's layout-line box follows edits made in the other fields
function syncLine() {
  const ta = $("#p-inspect textarea");
  if (ta && document.activeElement !== ta && S.sel.length === 1) ta.value = shownLine(lines()[S.sel[0]]);
}

function keyList(id) {
  if (!$("#" + id)) {
    const dl = el("datalist", { id });
    document.body.appendChild(dl);
  }
  const dl = $("#" + id);
  dl.innerHTML = params().map(p => `<option value="${esc(p.key)}">${esc(p.name)}${p.options.length ? " (" + p.options.length + " options)" : ""}</option>`).join("");
  return id;
}

function whenList() {
  const opts = [];
  for (const p of params()) if (p.options.length >= 2 && !p.key.endsWith("__open")) for (const o of p.options) opts.push(`${p.key}:${String(o).toLowerCase()}`);
  if (!$("#dl-when")) document.body.appendChild(el("datalist", { id: "dl-when" }));
  $("#dl-when").innerHTML = opts.map(o => `<option value="${esc(o)}">`).join("");
  return "dl-when";
}

function renderInspect() {
  const p = $("#p-inspect");
  p.innerHTML = "";
  if (!S.sel.length) return renderTabSettings(p);
  if (S.sel.length > 1) return renderMulti(p);
  const i = S.sel[0], w = W(i), it = S.items[i];
  p.append(el("div", { className: "ui-kindtag" }, w.kind));
  if (it && it.error) p.append(el("p", { className: "ui-note ui-err" }, it.error));
  const geo = GEOM[w.kind] || [];
  if (geo.length) {
    const g = el("div", { className: "ui-grid" });
    for (const [k, label, type] of geo) {
      const inp = el("input", { type: "number", value: w[k] ?? "", placeholder: type === "on" ? "auto" : "" });
      bindEdit(inp, x => {
        const v = x.value === "" ? undefined : Math.round(+x.value);
        const ww = W(S.sel[0]);
        if (v === undefined && type === "on") delete ww[k]; else if (v !== undefined && !isNaN(v)) ww[k] = v;
      });
      g.append(field(label, inp));
    }
    p.append(g);
  }
  for (const [k, label, type] of TEXT[w.kind] || []) {
    let inp;
    if (type.startsWith("sel:")) {
      inp = el("select");
      for (const o of type.slice(4).split(",")) inp.append(el("option", { value: o, textContent: o || "default", selected: (w[k] || "") === o }));
    } else if (type === "color") {
      const wrap = el("div", { className: "ui-row", style: "margin:0" });
      const c = el("input", { type: "color", value: "#" + (w.color || "e8341c") });
      const clear = el("button", { textContent: w.color ? "Theme colour" : "(theme colour)", disabled: !w.color, onclick: () => edit(() => delete W(S.sel[0]).color) });
      bindEdit(c, x => (W(S.sel[0]).color = x.value.slice(1)));
      wrap.append(c, clear);
      p.append(field(label, wrap));
      continue;
    } else if (type === "file") {
      inp = el("select");
      for (const f of S.doc.art) inp.append(el("option", { value: f, textContent: f, selected: w.file === f }));
      if (!S.doc.art.includes(w.file)) inp.append(el("option", { value: w.file, textContent: w.file + " (missing)", selected: true }));
    } else {
      inp = el("input", { type: "text", value: type === "opts" ? (w.options || []).join(",") : w[k] ?? "" });
      if (type === "key") inp.setAttribute("list", keyList("dl-keys"));
    }
    bindEdit(inp, x => {
      const ww = W(S.sel[0]), v = x.value.trim();
      if (type === "opts") { if (v) ww.options = v.split(",").map(s => s.trim()); else delete ww.options; }
      else if (v === "" && k !== "label" && k !== "title") delete ww[k];
      else ww[k] = x.value;
    }, inp.tagName === "SELECT" ? "change" : "input");
    p.append(field(label, inp));
    if (k === "key" && params().length && w.key && !param(w.key) && w.kind !== "list") p.append(el("p", { className: "ui-note ui-err" }, `“${w.key}” is not a parameter of this plugin.`));
  }
  const whenIn = el("input", { type: "text", value: w.when || "", placeholder: "always shown" });
  whenIn.setAttribute("list", whenList());
  bindEdit(whenIn, x => { const v = x.value.trim(); if (v) W(S.sel[0]).when = v; else delete W(S.sel[0]).when; });
  p.append(field("show only in mode (when=<param>:<option>)", whenIn));
  // the whole line, for anything the fields don't cover
  const raw = el("textarea", { rows: 3, value: shownLine(lines()[i]), spellcheck: false });
  raw.addEventListener("change", async () => {
    try {
      const r = await api("/api/parse", { line: raw.value });
      edit(() => (lines()[S.sel[0]].w = r.w));
    } catch (e) { toast("That line doesn't parse: " + e.message, true); }
  });
  p.append(field("layout line (edit and leave the box to apply)", raw));
  p.append(el("div", { className: "ui-row" },
    el("button", { textContent: "Duplicate", onclick: duplicate }), el("button", { textContent: "Delete", onclick: remove })));
  if (w.kind === "popup") p.append(el("p", { className: "ui-note" }, "Selected, a popup shows its open option list. gen_vst.py adds the hidden ", el("code", {}, w.key + "__open"), " parameter it needs."));
  if (["toggle", "button", "enum_v"].includes(w.kind)) p.append(el("p", { className: "ui-note" }, "The renderer fixes this control's size; only its position is yours."));
}

// the file's own line while the widget is as loaded, else the line the server will write
function shownLine(it) { return it.raw && it.w0 === JSON.stringify(it.w) ? it.raw.trim() : lineOf(it.w); }
function markLoaded() { for (const t of S.doc.tabs) for (const it of t.lines) if (it.t === "w") it.w0 = JSON.stringify(it.w); }

function lineOf(w) {   // mirrors studio.conf_line
  const G = ["x", "y", "w", "h", "cx", "cy", "r", "sw"], N = ["rows", "cols", "th", "gap"];
  const parts = [w.kind];
  for (const k of [...G, ...N]) if (k in w) parts.push(`${k}=${w[k]}`);
  for (const [k, v0] of Object.entries(w)) {
    if (k === "kind" || G.includes(k) || N.includes(k)) continue;
    const v = k === "options" ? v0.join(",") : String(v0);
    parts.push(v.includes(" ") || ["label", "title", "options"].includes(k) ? `${k}="${v}"` : `${k}=${v}`);
  }
  return parts.join(" ");
}

function renderMulti(p) {
  p.append(el("div", { className: "ui-kindtag" }, `${S.sel.length} widgets`));
  const boxes = () => S.sel.map(i => [i, S.items[i] && S.items[i].box]).filter(x => x[1]);
  const align = fn => edit(() => {
    const bs = boxes();
    const all = { x0: Math.min(...bs.map(b => b[1][0])), y0: Math.min(...bs.map(b => b[1][1])),
                  x1: Math.max(...bs.map(b => b[1][0] + b[1][2])), y1: Math.max(...bs.map(b => b[1][1] + b[1][3])) };
    for (const [i, b] of bs) { const [dx, dy] = fn(b, all); moveWidget(W(i), Math.round(dx), Math.round(dy)); }
  });
  const distribute = axis => edit(() => {
    const bs = boxes().sort((a, b) => (a[1][axis] + a[1][axis + 2] / 2) - (b[1][axis] + b[1][axis + 2] / 2));
    if (bs.length < 3) return;
    const c0 = bs[0][1][axis] + bs[0][1][axis + 2] / 2, c1 = bs.at(-1)[1][axis] + bs.at(-1)[1][axis + 2] / 2;
    bs.forEach(([i, b], j) => {
      const d = Math.round(c0 + (c1 - c0) * j / (bs.length - 1) - (b[axis] + b[axis + 2] / 2));
      moveWidget(W(i), axis ? 0 : d, axis ? d : 0);
    });
  });
  const g = el("div", { className: "ui-align" },
    el("button", { textContent: "⇤ Left", onclick: () => align((b, a) => [a.x0 - b[0], 0]) }),
    el("button", { textContent: "↔ Centre", onclick: () => align((b, a) => [(a.x0 + a.x1) / 2 - (b[0] + b[2] / 2), 0]) }),
    el("button", { textContent: "Right ⇥", onclick: () => align((b, a) => [a.x1 - b[0] - b[2], 0]) }),
    el("button", { textContent: "Spread ↔", onclick: () => distribute(0) }),
    el("button", { textContent: "⤒ Top", onclick: () => align((b, a) => [0, a.y0 - b[1]]) }),
    el("button", { textContent: "↕ Middle", onclick: () => align((b, a) => [0, (a.y0 + a.y1) / 2 - (b[1] + b[3] / 2)]) }),
    el("button", { textContent: "Bottom ⤓", onclick: () => align((b, a) => [0, a.y1 - b[1] - b[3]]) }),
    el("button", { textContent: "Spread ↕", onclick: () => distribute(1) }));
  p.append(el("h3", {}, "Align"), g);
  // shared fields: knob radius, when=
  const knobs = S.sel.filter(i => W(i).kind === "knob");
  if (knobs.length > 1) {
    const r = el("input", { type: "number", value: W(knobs[0]).r });
    bindEdit(r, x => knobs.forEach(i => (W(i).r = Math.max(12, Math.round(+x.value)))));
    p.append(field("knob radius (all selected knobs)", r));
  }
  const wh = el("input", { type: "text", value: S.sel.every(i => W(i).when === W(S.sel[0]).when) ? W(S.sel[0]).when || "" : "", placeholder: "always shown" });
  wh.setAttribute("list", whenList());
  bindEdit(wh, x => S.sel.forEach(i => { if (x.value.trim()) W(i).when = x.value.trim(); else delete W(i).when; }));
  p.append(field("show only in mode (all selected)", wh));
  p.append(el("div", { className: "ui-row" }, el("button", { textContent: "Duplicate", onclick: duplicate }), el("button", { textContent: "Delete", onclick: remove })));
}

function renderTabSettings(p) {
  const t = tabObj();
  p.append(el("div", { className: "ui-kindtag" }, "tab"));
  const name = el("input", { type: "text", value: t.name });
  name.addEventListener("change", () => name.value.trim() && edit(() => (tabObj().name = name.value.trim())));
  p.append(field("tab name (MPC shows it on the tab's function key)", name));
  const move = d => edit(() => { const T = S.doc.tabs, j = S.tab + d; [T[S.tab], T[j]] = [T[j], T[S.tab]]; S.tab = j; });
  p.append(el("div", { className: "ui-row" },
    el("button", { textContent: "◀ Move", disabled: S.tab === 0, onclick: () => move(-1) }),
    el("button", { textContent: "Move ▶", disabled: S.tab === S.doc.tabs.length - 1, onclick: () => move(1) }),
    el("button", { textContent: "Duplicate tab", onclick: () => edit(() => { const c = clone(t); c.name += " COPY"; c.raw = ""; S.doc.tabs.splice(S.tab + 1, 0, c); S.tab++; }) }),
    el("button", { textContent: "Delete tab", disabled: S.doc.tabs.length < 2, onclick: () => confirm(`Delete tab “${t.name}” and its ${t.lines.filter(l => l.t === "w").length} widgets?`) && edit(() => { S.doc.tabs.splice(S.tab, 1); S.tab = Math.max(0, S.tab - 1); S.sel = []; }) })));
  p.append(el("p", { className: "ui-note" }, "Click a widget to edit it, drag an empty area to select several. The canvas is the plugin area (1280 × 628); coordinates are the layout's, where the plugin area starts at y = 86."));
  const n = t.lines.filter(l => l.t === "x" && l.raw.trim().startsWith("#")).length;
  if (n) p.append(el("p", { className: "ui-note" }, `${n} comment line${n > 1 ? "s" : ""} in this tab are kept as they are.`));
}

// ---------------------------------------------------------------- Q-Links

function qlinkSets() { return lines().filter(l => l.t === "q"); }

function tabControls() {
  const keys = [];
  for (const i of widgetIdx()) {
    const w = W(i);
    if (!CONTROLS.includes(w.kind)) continue;
    if (w.kind === "list") { for (let n = 1; n <= w.cols * w.rows; n++) keys.push(`${w.key}_${n}`); continue; }
    keys.push(w.key);
  }
  return keys;
}

function renderQlinks() {
  const p = $("#p-qlinks");
  p.innerHTML = "";
  const sets = qlinkSets();
  if (S.qset >= sets.length) S.qset = 0;
  p.append(el("p", { className: "ui-note" }, "Each set is one MPC sub-page of this tab: the same screen with its own Q-Links. 1–8 are knob bank 1, 9–16 bank 2."));
  if (!sets.length) {
    const auto = tabControls().slice(0, 16);
    p.append(el("p", { className: "ui-note" }, `No qlinks line: the tab's first 16 controls in file order get the Q-Links (${auto.length} now).`),
      el("button", { className: "ui-wide", textContent: "Edit this tab's Q-Links", onclick: () => edit(() => lines().push({ t: "q", title: tabObj().name, keys: auto, raw: "" })) }));
  }
  sets.forEach((q, si) => {
    const box = el("div", { className: "ui-qset" + (si === S.qset ? " ui-on" : ""), onclick: () => { if (S.qset !== si) { S.qset = si; renderOverlay(); renderQlinks(); } } });
    const title = el("input", { type: "text", value: q.title });
    title.addEventListener("change", () => title.value.trim() && edit(() => (qlinkSets()[si].title = title.value.trim()), { keepPanel: true }));
    box.append(el("div", { className: "ui-qhead" }, title,
      el("button", { textContent: "Fill", title: "Fill with this tab's controls in layout order", onclick: () => edit(() => (qlinkSets()[si].keys = tabControls().slice(0, 16))) }),
      el("button", { textContent: "×", title: "Delete this set", onclick: () => edit(() => lines().splice(lines().indexOf(qlinkSets()[si]), 1)) })));
    for (const bank of [0, 1]) {
      box.append(el("div", { className: "ui-bank" }, `Bank ${bank + 1}: Q-Links ${bank * 8 + 1}–${bank * 8 + 8}`));
      const g = el("div", { className: "ui-slots" });
      for (let s = bank * 8; s < bank * 8 + 8; s++) {
        const k = q.keys[s];
        const pk = k && param(k);
        const slot = el("div", { className: "ui-qslot" + (k ? "" : " ui-empty") + (S.pick === s && si === S.qset ? " ui-pick" : ""),
          title: k ? `${k}${pk ? " — " + pk.name : ""}\nClick, then click a widget to reassign` : "Click, then click a widget on the canvas" });
        slot.append(el("b", {}, "Q" + (s + 1)), k || (s <= q.keys.length ? "click to assign" : "—"));
        if (k) slot.append(el("span", { className: "ui-x", textContent: "×", onclick: e => { e.stopPropagation(); edit(() => qlinkSets()[si].keys.splice(s, 1)); } }));
        slot.onclick = e => {
          e.stopPropagation();
          if (s > q.keys.length) return toast("Fill the Q-Links in order: a set has no gaps.");
          S.qset = si;
          S.pick = S.pick === s ? -1 : s;
          renderQlinks(); renderOverlay();
          if (S.pick >= 0) $("#status").textContent = `Click a widget to put it on Q-Link ${s + 1} (Esc cancels)`;
        };
        g.append(slot);
      }
      box.append(g);
    }
    p.append(box);
  });
  if (sets.length) p.append(el("button", { textContent: "+ Add a Q-Link set (sub-page)", onclick: () => edit(() => lines().push({ t: "q", title: tabObj().name + " " + (sets.length + 1), keys: [], raw: "" })) }));
  // track mode
  p.append(el("h3", {}, "Program / track mode"));
  const tr = el("input", { type: "text", value: trackGet() || "", placeholder: "page 1's set" });
  tr.addEventListener("change", () => edit(() => trackSet(tr.value.split(",").map(s => s.trim()).filter(Boolean).join(",")), { keepPanel: true }));
  p.append(field("qlinks_track: keys, in order, when Q-Links follow the track rather than the page", tr));
}

function assignPick(i) {
  const w = W(i);
  if (!CONTROLS.includes(w.kind)) return toast("Only controls go on Q-Links.");
  const key = w.kind === "list" ? w.key + "_1" : w.key;
  const s = S.pick, si = S.qset;
  S.pick = -1;
  edit(() => {
    const q = qlinkSets()[si];
    const had = q.keys.indexOf(key);
    if (had >= 0 && had !== s) q.keys.splice(had, 1);   // a key sits on one Q-Link of a set
    q.keys[Math.min(s, q.keys.length)] = key;
  }, { keepPanel: false });
}

// ---------------------------------------------------------------- theme

const THEME_HELP = {
  bg: "page background", panel: "panels", line: "frame lines", ink: "text", ink_dim: "dim text, values", ink_faint: "faint text",
  accent: "accent (values, buttons)", accent_hi: "titles, highlights", knob_face: "knob face", knob_ring: "knob track", knob_dot: "knob arc, pointer",
  lcd: "text boxes", seg_active: "selected option", seg_inactive: "other options", seg_active_tx: "selected option text", box: "td3 boxes",
  btn_bg: "button (td3)", btn_text: "button text (td3)", btn_text_plain: "button text", display_bg: "dot display", display_cell: "dot cells",
  display_ink: "dot display text", display_off: "unlit dots", display_bezel: "dot display bezel",
};

function renderTheme() {
  const p = $("#p-theme");
  p.innerHTML = "";
  const style = el("select", {}, el("option", { value: "", textContent: "Force Shadow (outlined frames)" }), el("option", { value: "td3", textContent: "td3 (filled boxes)" }));
  style.value = headGet("style") || "";
  style.onchange = () => edit(() => headSet("style", style.value), { keepPanel: true });
  p.append(field("frame style (style=)", style));
  p.append(el("h3", {}, "Colours"), el("p", { className: "ui-note" }, "theme_<name>= lines: the renderer's CSS variables, and the colours MPC's live text uses. Unset ones use the default shown."));
  for (const [k, def] of Object.entries(S.doc.theme)) {
    const v = headGet("theme_" + k);
    const c = el("input", { type: "color", value: "#" + (v || def) });
    c.addEventListener("focus", () => (c._snap = snapshot()));
    c.addEventListener("input", () => { const s = c._snap; c._snap = null; edit(() => headSet("theme_" + k, c.value.slice(1)), { snap: s || undefined, noUndo: !s, keepPanel: true }); });
    c.addEventListener("change", () => renderTheme());
    const lab = el("div", {}, el("code", { className: v ? "ui-set" : "ui-unset" }, k), el("div", { className: "ui-note", style: "margin:0" }, THEME_HELP[k] || ""));
    const reset = el("button", { textContent: "reset", disabled: !v, onclick: () => edit(() => headSet("theme_" + k, undefined)) });
    p.append(el("div", { className: "ui-color" }, c, lab, reset));
  }
  const other = S.doc.head.filter(l => l.trim() && !/^\s*(theme_|style=|art_css=|qlinks_track)/.test(l));
  if (other.length) p.append(el("h3", {}, "Other top-level lines"), el("p", { className: "ui-note" }, "Kept as they are: ", ...other.map(l => el("code", {}, l.trim())), ""));
}

// ---------------------------------------------------------------- style (art_css)

const LOOK = [   // default.css variables: [name, label, min, max, step, unit]
  ["--title-size", "Frame titles", 10, 40, 1, "px"], ["--label-size", "Labels", 8, 30, 1, "px"], ["--seg-size", "Options", 8, 30, 1, "px"],
  ["--button-size", "Buttons", 8, 30, 1, "px"], ["--radius", "Corner radius", 0, 20, 1, "px"], ["--sheen", "Sheen", 0, 0.5, 0.01, ""],
];
const LOOK_START = "/* skin studio: look */", LOOK_END = "/* end skin studio */";

function lookVars(css) {
  const m = css.indexOf(LOOK_START), n = css.indexOf(LOOK_END);
  const vars = {};
  if (m >= 0 && n > m) for (const [, k, v] of css.slice(m, n).matchAll(/(--[\w-]+)\s*:\s*([^;]+);/g)) vars[k] = v.trim();
  return vars;
}

function setLookVars(css, vars) {
  const body = Object.entries(vars).map(([k, v]) => `  ${k}: ${v};`).join("\n");
  const block = `${LOOK_START}\n:root {\n${body}\n}\n${LOOK_END}\n`;
  const m = css.indexOf(LOOK_START), n = css.indexOf(LOOK_END);
  return m >= 0 && n > m ? css.slice(0, m) + block + css.slice(n + LOOK_END.length).replace(/^\n/, "") : block + css;
}

async function loadCss(name) {
  S.css.name = name;
  try {
    const r = await fetch("/files/" + name, { cache: "no-store" });
    S.css.text = S.css.saved = r.ok ? await r.text() : "";
  } catch { S.css.text = S.css.saved = ""; }
  S.themeKey = "";   // re-apply: the page swaps the file's <link> for the live text
  $$("[data-artcss]").forEach(e => e.remove());
  await renderCanvas();
}

function renderCss() {
  const p = $("#p-css");
  p.innerHTML = "";
  const name = headGet("art_css");
  p.append(el("p", { className: "ui-note" }, "The layout's own stylesheet (art_css=), loaded after the renderer's default.css: fonts, knob looks, gradients, shadows. Sizes and positions stay the layout's; MPC places its live controls there."));
  if (!name) {
    p.append(el("button", { className: "ui-wide ui-primary", textContent: "Create skin.css", onclick: async () => {
      const n = "skin.css";   // an existing one is used as it is
      if (!S.doc.css.includes(n)) {
        try { await api("/api/file", { name: n, text: CSS_START }); S.doc.css.push(n); } catch (e) { return toast(e.message, true); }
      }
      edit(() => headSet("art_css", n));
    } }));
    if (S.doc.css.length) {
      const pick = el("select", {}, el("option", { value: "", textContent: "or use an existing stylesheet…" }), ...S.doc.css.map(c => el("option", { value: c, textContent: c })));
      pick.onchange = () => pick.value && edit(() => headSet("art_css", pick.value));
      p.append(pick);
    }
    return;
  }
  if (S.css.name !== name || S.css.text === null) { loadCss(name).then(() => S.panel === "css" && renderCss()); p.append(el("p", { className: "ui-note" }, "Loading " + name + "…")); return; }
  const dirty = S.css.text !== S.css.saved;
  p.append(el("div", { className: "ui-qhead" }, el("code", {}, name), el("button", { className: dirty ? "ui-primary" : "", textContent: dirty ? "Save CSS" : "Saved", disabled: !dirty, onclick: saveCss })));
  // look sliders: a managed :root block at the top of the file
  p.append(el("h3", {}, "Look"));
  const vars = lookVars(S.css.text);
  for (const [k, label, min, max, step, unit] of LOOK) {
    const cur = vars[k] !== undefined ? parseFloat(vars[k]) : undefined;
    const out = el("span", { className: "ui-note", style: "margin:0" }, cur !== undefined ? String(cur) : "default");
    const r = el("input", { type: "range", min, max, step, value: cur ?? (k === "--sheen" ? 0.1 : k === "--radius" ? 6 : k === "--title-size" ? 20 : 15) });
    r.oninput = () => { const v = lookVars(S.css.text); v[k] = r.value + unit; setCssText(setLookVars(S.css.text, v)); out.textContent = r.value; };
    p.append(el("div", { className: "ui-slider" }, el("span", {}, label), r, out));
  }
  // fonts
  p.append(el("h3", {}, "Fonts"));
  const fams = ["Titillium Web", ...[...S.css.text.matchAll(/font-family\s*:\s*["']([^"']+)["']\s*;[^}]*src/g)].map(m => m[1])];
  const uniq = [...new Set(fams)];
  for (const [k, label] of [["--font", "Labels and options"], ["--title-font", "Frame titles"]]) {
    const s = el("select", {}, el("option", { value: "", textContent: "default" }), ...uniq.map(f => el("option", { value: `"${f}"`, textContent: f })));
    s.value = vars[k] ? vars[k].split(",")[0].trim() : "";
    s.onchange = () => { const v = lookVars(S.css.text); if (s.value) v[k] = `${s.value}, "Titillium Web", sans-serif`; else delete v[k]; setCssText(setLookVars(S.css.text, v)); };
    p.append(field(label, s));
  }
  const up = el("input", { type: "file", accept: ".ttf,.otf,.woff,.woff2", hidden: true });
  up.onchange = async () => {
    const f = up.files[0];
    if (!f || !replaceOk(f.name, S.doc.fonts)) return;
    try {
      await api("/api/upload?name=" + encodeURIComponent(f.name), await f.arrayBuffer(), true);
      const fam = f.name.replace(/\.[^.]+$/, "").replace(/[-_](Regular|Bold|SemiBold|Medium|Light|Italic)$/i, "").replace(/[-_]/g, " ");
      const up_ = "../".repeat(name.split("/").length - 1);
      setCssText(S.css.text + `\n@font-face { font-family: "${fam}"; src: url("${up_}${f.name}"); }\n`);
      if (!S.doc.fonts.includes(f.name)) S.doc.fonts.push(f.name);
      toast(`Added font “${fam}”`);
      renderCss();
    } catch (e) { toast(e.message, true); }
  };
  p.append(el("button", { textContent: "Upload a font…", onclick: () => up.click() }), up);
  p.append(el("p", { className: "ui-note" }, "Baked text only (frame titles, group labels, options, buttons). MPC draws names and values itself, in Titillium Web or Roboto whatever the skin says."));
  // the file
  p.append(el("h3", {}, "Stylesheet"));
  const ta = el("textarea", { rows: 18, value: S.css.text, spellcheck: false });
  ta.oninput = () => { S.css.text = ta.value; liveCss(); markCss(); };
  p.append(ta, el("p", { className: "ui-note" }, "Classes and variables: tools/html_art/default.css (its header lists them). Changes show on the canvas as you type."));
}

const CSS_START = `/* Skin stylesheet (the layout's art_css=), loaded after tools/html_art/default.css. Override its variables or
 * classes: .knob-face, .knob-arc, .frame-border, .frame-title, .seg-bg, .button-bg, .box, .slider-thumb, ...
 * Colours come from the layout's theme_* lines as var(--accent), var(--ink), ... */
`;

function setCssText(t) {
  S.css.text = t;
  liveCss();
  const ta = $("#p-css textarea");
  if (ta && ta.value !== t) ta.value = t;
  markCss();
}

function markCss() {
  const b = $("#p-css .ui-qhead button");
  if (!b) return;
  const d = S.css.text !== S.css.saved;
  b.disabled = !d; b.textContent = d ? "Save CSS" : "Saved"; b.className = d ? "ui-primary" : "";
}

async function saveCss() {
  try {
    await api("/api/file", { name: S.css.name, text: S.css.text });
    S.css.saved = S.css.text;
    markCss();
    toast("Saved " + S.css.name);
  } catch (e) { toast(e.message, true); }
}

// ---------------------------------------------------------------- checks

function checks() {
  const out = [], ps = params(), has = ps.length > 0, keys = new Set(ps.map(p => p.key));
  const names = new Set();
  S.doc.tabs.forEach((tab, t) => {
    if (names.has(tab.name)) out.push({ level: "error", tab: t, msg: `Two tabs are called “${tab.name}”.` });
    names.add(tab.name);
    const ws = tab.lines.map((it, i) => [i, it]).filter(([, it]) => it.t === "w");
    for (const [i, it] of ws) {
      const w = it.w, where = `${w.kind} ${describe(w)}`;
      if (w.kind === "art") continue;
      const need = [];
      if (CONTROLS.includes(w.kind)) {
        if (!w.key) { out.push({ level: "error", tab: t, i, msg: `${where}: no parameter (key=).` }); continue; }
        if (w.kind === "list") for (let n = 1; n <= (w.cols || 1) * (w.rows || 1); n++) need.push(`${w.key}_${n}`);
        else need.push(w.key);
        if (w.kind === "stepper") need.push(w.prev || w.key + "_prev", w.next || w.key + "_next", ...(w.get ? [w.get] : []));
      }
      if (has) for (const k of need) if (!keys.has(k)) out.push({ level: "error", tab: t, i, msg: `${where}: “${k}” is not a parameter.` });
      const p = has && param(w.key);
      if (p && ["enum_h", "enum_v", "popup"].includes(w.kind)) {
        if (w.options && w.options.length !== p.options.length) out.push({ level: "error", tab: t, i, msg: `${where}: ${w.options.length} options, the parameter has ${p.options.length}.` });
        if (!p.options.length) out.push({ level: "error", tab: t, i, msg: `${where}: “${w.key}” has no options.` });
      }
      if (!has && ["enum_h", "enum_v"].includes(w.kind) && !w.options) out.push({ level: "info", tab: t, i, msg: `${where}: options come from the parameter (open the studio with --params to see them).` });
      if (w.when) {
        const [k, o] = w.when.split(":");
        const wp = has && param(k);
        if (has && (!wp || wp.options.length < 2)) out.push({ level: "error", tab: t, i, msg: `${where}: when=${w.when}: “${k}” is not an option parameter.` });
        else if (wp && !wp.options.map(x => String(x).toLowerCase()).includes(String(o).toLowerCase()) && !(/^\d+$/.test(o) && +o < wp.options.length))
          out.push({ level: "error", tab: t, i, msg: `${where}: when=${w.when}: no option “${o}”.` });
      }
      if (t !== S.tab) continue;   // geometry checks need the rendered boxes: this tab only
      const b = S.items[i] && S.items[i].box;
      if (S.items[i] && S.items[i].error) out.push({ level: "error", tab: t, i, msg: `${where}: ${S.items[i].error}` });
      if (b && (b[0] < 0 || b[1] < PX.y || b[0] + b[2] > PX.w || b[1] + b[3] > PX.y + PX.h)) out.push({ level: "warn", tab: t, i, msg: `${where} goes past the plugin area's edge.` });
    }
    if (t === S.tab) {   // overlapping controls shown at the same time
      const cs = ws.filter(([i, it]) => CONTROLS.includes(it.w.kind) && S.items[i] && S.items[i].box);
      for (let a = 0; a < cs.length; a++) for (let c = a + 1; c < cs.length; c++) {
        const [i, A] = cs[a], [j, B] = cs[c];
        if (A.w.when && B.w.when && A.w.when !== B.w.when) continue;
        const p = S.items[i].box, q = S.items[j].box;
        if (p[0] < q[0] + q[2] - 1 && q[0] < p[0] + p[2] - 1 && p[1] < q[1] + q[3] - 1 && q[1] < p[1] + p[3] - 1)
          out.push({ level: "warn", tab: t, i: j, msg: `${B.w.kind} ${describe(B.w)} overlaps ${A.w.kind} ${describe(A.w)}.` });
      }
    }
    for (const it of tab.lines.filter(l => l.t === "q")) {
      if (it.keys.length > 16) out.push({ level: "error", tab: t, msg: `Q-Links “${it.title}”: ${it.keys.length} keys (16 at most).` });
      if (has) for (const k of it.keys) if (!keys.has(k)) out.push({ level: "error", tab: t, msg: `Q-Links “${it.title}”: “${k}” is not a parameter.` });
    }
  });
  const tr = trackGet();
  if (has && tr) for (const k of tr.split(",").map(s => s.trim()).filter(Boolean)) if (!keys.has(k)) out.push({ level: "error", msg: `qlinks_track: “${k}” is not a parameter.` });
  if (!has) out.push({ level: "info", msg: "No parameter file: keys aren't checked. Start the studio with --params <params.json>." });
  return out;
}

function renderChecksBadge() {
  const n = checks().filter(c => c.level !== "info").length;
  const b = $("#check-count");
  b.hidden = !n;
  b.textContent = n;
}

function renderChecks() {
  const p = $("#p-checks");
  p.innerHTML = "";
  const cs = checks();
  if (!cs.length) p.append(el("p", { className: "ui-note" }, "Nothing to report. The skin build runs the same checks on keys and options, and more."));
  for (const c of cs) {
    const tabName = c.tab !== undefined ? S.doc.tabs[c.tab].name : "layout";
    const d = el("div", { className: "ui-check-item" + (c.level === "error" ? " ui-error" : c.level === "info" ? " ui-info" : "") }, c.msg, el("small", {}, tabName));
    d.onclick = () => { if (c.tab !== undefined && c.tab !== S.tab) { S.tab = c.tab; S.sel = []; } if (c.i !== undefined) S.sel = [c.i]; refresh({ keepPanel: true }); };
    p.append(d);
  }
}

// ---------------------------------------------------------------- load / save

async function save() {
  try {
    const r = await api("/api/save", { head: S.doc.head, tabs: S.doc.tabs });
    const tab = S.tab;
    Object.assign(S.doc, r.doc);
    markLoaded();
    S.tab = Math.min(tab, S.doc.tabs.length - 1);
    setDirty(false);
    if (S.css.text !== null && S.css.text !== S.css.saved) await saveCss();
    toast("Saved " + S.doc.name);
    refresh({ keepPanel: true });
  } catch (e) { toast("Save failed: " + e.message, true); }
}

// an upload with the name of a file already next to the layout replaces it (the first time, it's kept as .bak)
const replaceOk = (name, have) => !have.includes(name) || confirm(`Replace ${name} next to the layout?`);

async function importArt() {
  const f = $("#art-file").files[0];
  if (!f) return;
  if (!replaceOk(f.name, S.doc.art)) return ($("#art-file").value = "");
  try {
    await api("/api/upload?name=" + encodeURIComponent(f.name), await f.arrayBuffer(), true);
    if (!S.doc.art.includes(f.name)) S.doc.art.push(f.name);
    S.cache.clear();
    // art goes first: drawn behind the frames and controls
    edit(() => { lines().splice(0, 0, { t: "w", w: { kind: "art", file: f.name }, raw: "" }); S.sel = [0]; });
    toast("Added " + f.name + " (drawn by the browser renderer: vst.json \"art\": \"html\")");
  } catch (e) { toast(e.message, true); }
  $("#art-file").value = "";
}

function fit() {
  const z = $("#zoom").value, c = $("#canvas"), st = $("#stage");
  const w = z === "fit" ? Math.max(320, Math.min(st.clientWidth - 32, (st.clientHeight - 32) * 1280 / 628)) : 1280 * +z;
  c.setAttribute("width", Math.round(w));
  c.setAttribute("height", Math.round(w * 628 / 1280));
}

async function main() {
  try { S.doc = await api("/api/doc"); } catch (e) { toast("Can't load the layout: " + e.message, true); return; }
  markLoaded();
  $("#file").textContent = S.doc.name;
  document.title = "Skin Studio — " + S.doc.name;
  $("#defs").innerHTML = S.doc.defs;
  const pal = $("#palette");
  for (const [k, label] of PALETTE) pal.append(el("button", { textContent: label, title: "Add a " + k, onclick: () => addWidget(k) }));
  $("#import-art").onclick = () => $("#art-file").click();
  $("#art-file").onchange = importArt;
  $("#undo").onclick = undo;
  $("#redo").onclick = redo;
  $("#save").onclick = save;
  $("#dup").onclick = duplicate;
  $("#del").onclick = remove;
  $("#layer-up").onclick = () => reorder(-1);
  $("#layer-down").onclick = () => reorder(1);
  for (const k of Object.keys(S.view)) $("#v-" + k).onchange = e => { S.view[k] = e.target.checked; refresh({ keepPanel: true }); };
  $("#zoom").onchange = fit;
  $$("#paneltabs button").forEach(b => (b.onclick = () => showPanel(b.dataset.p)));
  window.addEventListener("resize", fit);
  window.addEventListener("beforeunload", e => { if (S.dirty || (S.css.text !== null && S.css.text !== S.css.saved)) { e.preventDefault(); e.returnValue = ""; } });
  setupCanvas();
  setupKeys();
  fit();
  setDirty(false);
  await refresh();
}

main();
