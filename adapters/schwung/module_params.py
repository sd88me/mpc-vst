"""Schwung module.json -> the generic parameter list (docs/PORTING.md "Parameters").

chain_params are already close to the generic shape; `access: "write"` becomes `momentary`, and
ui_hierarchy levels become the skin studio's sections."""
import json


def is_module(d):
    return "chain_params" in d or "chain_params" in d.get("capabilities", {})


def load(path):
    """-> (params, sections [(label, [keys])] or None when the module has no ui_hierarchy)"""
    d = json.load(open(path))
    caps = d.get("capabilities", d)
    raw = caps.get("chain_params") or d.get("chain_params")
    if not raw:
        raise SystemExit("%s: no chain_params" % path)
    params = []
    for p in raw:
        q = {k: v for k, v in p.items() if k != "access"}
        if p.get("access") == "write":
            q["momentary"] = True
        params.append(q)
    keys = {p["key"] for p in params}
    levels = (caps.get("ui_hierarchy") or {}).get("levels") or {}
    sections, seen = [], set()
    for lv in ["root"] + [k for k in levels if k != "root"]:
        if lv not in levels:
            continue
        ks = [k for k in levels[lv].get("params", []) if isinstance(k, str) and k in keys and k not in seen]
        seen.update(ks)
        if ks:
            sections.append((levels[lv].get("label", lv), ks))
    return params, sections or None
