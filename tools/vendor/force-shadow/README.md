# Vendored: force-shadow's skin renderer

`tools/shadow_art.c` draws skin artwork with force-shadow's own offline renderer, so a plugin skin
matches the Force shadow page it came from. The two files it needs are copied here unchanged, in
force-shadow's layout (`render_conf_preview.c` includes `../src/font8x8.h`), so no force-shadow
checkout is needed to build a skin:

- `tools/render_conf_preview.c`
- `src/font8x8.h`

From [sd88me/force-shadow](https://github.com/sd88me/force-shadow) at `ad94add18bc4433b8a704ff9f890e3eed2324d2e`, MIT licensed (`LICENSE`).

They don't follow force-shadow's changes on their own, so skins stay the same until this copy is
updated on purpose. To update it, from this repo's root with a force-shadow checkout at `../force-shadow`:
```
cp ../force-shadow/tools/render_conf_preview.c tools/vendor/force-shadow/tools/
cp ../force-shadow/src/font8x8.h tools/vendor/force-shadow/src/
```
then update the commit above, rebuild a port and check its skin previews before committing.
