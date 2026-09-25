# Vendored: force-shadow's skin renderer

`tools/shadow_art.c` draws skin artwork with force-shadow's own offline renderer, so a plugin skin
matches the Force shadow page it came from. The two files it needs are copied here unchanged, in
force-shadow's layout (`render_conf_preview.c` includes `../src/font8x8.h`), so no force-shadow
checkout is needed to build a skin:

- `tools/render_conf_preview.c`
- `src/font8x8.h`
- `tools/stb_truetype.h` (force-shadow's own vendored copy of stb_truetype v1.26, public domain / MIT, by
  Sean Barrett; used for the optional `font_title=`/`font_label=` TrueType text)

From [sd88me/force-shadow](https://github.com/sd88me/force-shadow) at `55f4e653f6190b67318f6c8440c5cceb3e1bd34d`, MIT licensed (`LICENSE`).

They don't follow force-shadow's changes on their own, so skins stay the same until this copy is
updated on purpose. To update it, from this repo's root with a force-shadow checkout at `../force-shadow`:
```
cp ../force-shadow/tools/render_conf_preview.c tools/vendor/force-shadow/tools/
cp ../force-shadow/src/font8x8.h tools/vendor/force-shadow/src/
cp ../force-shadow/tools/stb_truetype.h tools/vendor/force-shadow/tools/
```
then update the commit above, rebuild a port and check its skin previews before committing.
