# The ROM

Not included, not bundled, not committed anywhere: the Microwave II/XT operating system is Waldorf's
copyrighted firmware. You need your own copy, in any of the forms gearmulator's Xenia accepts:

| Form | Size | Notes |
|---|---|---|
| Full ROM dump (`.bin`) | 256 KB | a dump of the flash |
| Two half ROM dumps (`.bin`) | 128 KB each | the two chips' dumps; the loader pairs them by content |
| OS update (`.mid`) | ~166-171 KB | a Microwave II/XT OS update MIDI file |

The loader identifies files by size and content, not by name, and picks the newest OS version if there are
several. It is gearmulator's own `xt::RomLoader`.

## Where

The plugin searches `MODULE_DIR` (vst.json `defines.MODULE_DIR`, `/sdcard/vst/xenia`), not its subfolders,
and also the folder the `.so` itself is in (`/sdcard/vst`, gearmulator's default):

```
/sdcard/vst/xenia/<anything>.mid      (or .bin)
```

Copy it there yourself (scp, USB, or MPC's file browser). The plugin's Status readout shows `No ROM` when
it finds nothing, `Booting` while the firmware starts (several seconds with the interpreter), then the
emulation load and the count of dropped blocks.

To use another folder, change `defines.MODULE_DIR` in `vst.json` and rebuild.
