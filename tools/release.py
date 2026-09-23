#!/usr/bin/env python3
"""Package a built plugin as one shareable zip with an installer and generated install instructions.

  tools/release.py --so build/x.so --skin "build/skin/<vendor> - VST - <name>" --entry build/pluginlist-entry.xml \
      --version 1.0.0 [--extra DIR:vst/sub] [--bench bench.json] [--about "one line"] [-o dist]

The zip unpacks to <Name>-<version>/ with:
  install.sh / uninstall.sh   run on the device as root (MPC is stopped and restarted, MPC.settings is backed up)
  INSTALL.md                  generated instructions (scripted and manual), requirements, bench results
  plugin.xml                  the pluginList-arm <PLUGIN> entry
  payload/vst/...             the .so (+ --extra payload), copied to the directory in the entry's file="..."
  payload/Synths/<skin>/      the skin, copied to /sdcard/Synths
  SHA256SUMS
Standard library only. See docs/RELEASING.md.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import stat
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))

ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument("--so", required=True)
ap.add_argument("--skin", required=True, help="the skin folder '<vendor> - VST - <name>'")
ap.add_argument("--entry", required=True, help="pluginlist-entry.xml from the port's build")
ap.add_argument("--version", required=True)
ap.add_argument("--extra", action="append", default=[], help="SRC:DEST extra payload, DEST under vst/ (e.g. bin:vst/x)")
ap.add_argument("--bench", help="JSON line from `tools/bench.sh ... -j` on a Gen1 device")
ap.add_argument("--about", default="", help="one-line description for INSTALL.md")
ap.add_argument("-o", "--out", default="dist")
a = ap.parse_args()

entry = open(a.entry).read().strip()
attr = dict(re.findall(r'(\w+)="([^"]*)"', entry))
name, so_path = attr["name"], attr["file"]
so_dir, so_name = os.path.dirname(so_path), os.path.basename(so_path)
if os.path.basename(a.so) != so_name:
    raise SystemExit("entry file= is %s but --so is %s" % (so_name, os.path.basename(a.so)))
skin_name = os.path.basename(os.path.normpath(a.skin))
if skin_name != "%s - VST - %s" % (attr["manufacturer"], name):
    raise SystemExit("skin folder must be named '%s - VST - %s'" % (attr["manufacturer"], name))
for need in ("version.xml", "Plugin Skins/TUI.json"):
    if not os.path.exists(os.path.join(a.skin, need)):
        raise SystemExit("skin is missing " + need)
bench = json.loads(open(a.bench).read().strip().splitlines()[-1]) if a.bench else None

slug = re.sub(r"[^A-Za-z0-9]+", "-", name).strip("-")
top = "%s-%s" % (slug, a.version)
stage = tempfile.mkdtemp()
root = os.path.join(stage, top)
os.makedirs(os.path.join(root, "payload", "vst"))
shutil.copy2(a.so, os.path.join(root, "payload", "vst", so_name))
shutil.copytree(a.skin, os.path.join(root, "payload", "Synths", skin_name))
extras = []
for spec in a.extra:
    src, dest = spec.split(":", 1)
    if not dest.startswith("vst/"):
        raise SystemExit("--extra DEST must be under vst/")
    d = os.path.join(root, "payload", dest)
    if os.path.isdir(src):
        shutil.copytree(src, d, symlinks=True)
    else:
        shutil.copy2(src, d)
    extras.append(dest[4:])
open(os.path.join(root, "plugin.xml"), "w").write(entry + "\n")
shutil.copy2(os.path.join(HERE, "release", "plugin_list.awk"), root)

sub = {"@NAME@": name, "@SO_DIR@": so_dir, "@SO_NAME@": so_name, "@SKIN@": skin_name,
       "@EXTRAS@": " ".join("'%s'" % e for e in extras), "@VERSION@": a.version}
for script in ("install.sh", "uninstall.sh"):
    text = open(os.path.join(HERE, "release", script)).read()
    for k, v in sub.items():
        text = text.replace(k, v)
    p = os.path.join(root, script)
    open(p, "w", newline="\n").write(text)
    os.chmod(p, 0o755)

# INSTALL.md
kind = "instrument" if attr.get("isInstrument") == "1" else "effect"
b = ""
if bench:
    b = ("\n## CPU\n\nMeasured on a Gen1 device (MPC Live/One/X/Force class, Cortex-A17) with `tools/bench.sh` from "
         "[mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins): worst p99 **%.1f%%** of one audio block, worst "
         "block %.1f%%, verdict **%s**. As a rule of thumb, several instances run comfortably when p99 is under 15%%.\n"
         % (bench["p99_pct"], bench["max_pct"], bench["verdict"]))
extra_md = "".join("- `payload/vst/%s` → `%s/%s`\n" % (e, so_dir, e) for e in extras)
install_md = """# {name} {ver}

{about}A native MPC OS plugin ({kind}) with its own MPC screen skin, loaded by MPC's built-in plugin host.

## Requirements

- A first-generation MPC OS standalone device (32-bit ARM, like the Force, MPC Live / Live II, One, X and Key 61).
  The installer refuses anything else. Newer models are untested.
- **Root shell access** (SSH) to the device. Stock MPC OS doesn't offer this; you need a modded unit.
- Tested on MPC OS with a Force. Installing plugins this way is unofficial: back up first, use at your own risk.

## Install (scripted)

1. Unzip, then copy the whole folder to the device, e.g. `scp -r {top} root@<device-ip>:/tmp/`
2. Run it: `ssh root@<device-ip> sh /tmp/{top}/install.sh`

The installer checks the device, copies the files, then **stops MPC** (save your project first), backs up
`MPC.settings`, adds the plugin to MPC's plugin list and starts MPC again. Running it again upgrades in place.
Add `-y` to skip the confirmation prompt.

Then add **{name}** to a track from the plugin browser ({where}). Its screen appears in the plugin view,
and the Q-Links follow the page.

## Uninstall

`ssh root@<device-ip> sh /tmp/{top}/uninstall.sh` removes the files and the plugin-list entry (it also stops and
restarts MPC). Projects that use the plugin will load without it.

## Install by hand

1. Copy the files:
   - `payload/vst/{so}` → `{so_path}`
{extra_md}   - `payload/Synths/{skin}/` → `/sdcard/Synths/{skin}/`
2. Stop MPC: `systemctl stop acvs`
3. Back up the settings file, `MPC.settings` (on a Force: `/media/az01-internal/Settings/MPC/MPC.settings`).
4. In `MPC.settings`, inside `<VALUE name="pluginList-arm"><KNOWNPLUGINS>`, add the line from `plugin.xml`.
   If there is no `pluginList-arm` value yet, add one just before `</PROPERTIES>`:
   ```xml
   <VALUE name="pluginList-arm">
     <KNOWNPLUGINS>
       (the line from plugin.xml)
     </KNOWNPLUGINS>
   </VALUE>
   ```
5. Start MPC: `systemctl start acvs`. If MPC shows default settings, restore your backup (the XML was malformed).
{bench}
## Files

See `SHA256SUMS`. Made with [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins).
""".format(name=name, ver=a.version, about=(a.about + "\n\n") if a.about else "", kind=kind, top=top, so=so_name,
           so_path=so_path, skin=skin_name, extra_md=extra_md, bench=b,
           where="Instrument plugins" if kind == "instrument" else "Insert effects")
open(os.path.join(root, "INSTALL.md"), "w").write(install_md)

def walk(top):
    """Every file and symlink under top (symlinks, including ones to directories, are not followed)."""
    for d, dirs, files in os.walk(top):
        dirs.sort()
        for f in sorted(files + [x for x in dirs if os.path.islink(os.path.join(d, x))]):
            yield os.path.join(d, f)


sums = []
for p in walk(root):
    if not os.path.islink(p):
        sums.append("%s  %s" % (hashlib.sha256(open(p, "rb").read()).hexdigest(), os.path.relpath(p, root)))
open(os.path.join(root, "SHA256SUMS"), "w").write("\n".join(sums) + "\n")

os.makedirs(a.out, exist_ok=True)
zpath = os.path.join(a.out, top + "-mpc-armv7.zip")
with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
    for p in walk(root):
        info = zipfile.ZipInfo(os.path.relpath(p, stage), date_time=(2026, 1, 1, 0, 0, 0))
        info.create_system = 3   # unix, so modes and symlinks survive
        if os.path.islink(p):
            info.external_attr = (stat.S_IFLNK | 0o777) << 16
            z.writestr(info, os.readlink(p))
        else:
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (stat.S_IFREG | (0o755 if os.stat(p).st_mode & stat.S_IXUSR else 0o644)) << 16
            with open(p, "rb") as f:
                z.writestr(info, f.read())
shutil.rmtree(stage)
print("%s (%d files, %.1f MB)" % (zpath, len(sums) + 1, os.path.getsize(zpath) / 1e6))
