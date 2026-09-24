# Schwung adapter

Lets a [Schwung](https://github.com/charlesvestal/schwung) `plugin_api_v2` DSP module build as an MPC plugin
unchanged.

- `schwung_engine.c` provides `mpc_engine()` (`wrapper/engine.h`) on top of the module's
  `move_plugin_init_v2()`. The contracts match (44.1 kHz, int16 stereo, 128-frame blocks), so it only
  renames calls; MIDI goes in as source 2 (external).
- `module_params.py` reads the module's `module.json`: `chain_params` become the parameter list
  (`access: "write"` → `momentary`), `ui_hierarchy` levels become the skin studio's sections.

Use it by naming the module in the port's `vst.json` instead of `"params"`:

```json
{ "name": "Maze Voice", "module": "../module.json", "layout": "layout.conf", "build": { … } }
```

`tools/build_port.sh` then links `schwung_engine.c`, and `gen_vst.py` / `studio.py` read `module.json`.
The module's own sources go in `build.sources` as usual.

Offline host test (`tools/host_test.c`): compile this adapter in alongside `wrapper/vst2_wrap.c` and the
module's sources, e.g. `gcc -fsanitize=address -I<port>/build tools/host_test.c wrapper/vst2_wrap.c
adapters/schwung/schwung_engine.c <module sources> -lm`.
