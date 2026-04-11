# Fonts

`system_ui.rtf` is the repo-shipped raster fallback that GDI now tries after the staged `tahoma-*.rtf` assets.

`system_ui.font` remains in the tree as the descriptor fallback behind `system_ui.rtf` and ahead of the built-in mini-font metrics.

`tahoma-12.rtf`, `tahoma-14.rtf`, `tahoma-16.rtf`, and `tahoma-18.rtf` are prerasterized Tahoma raster-font binaries that GDI now prefers for default UI text.

Each `.rtf` file contains a fixed header plus a glyph lookup table. Every lookup row stores the glyph coverage offset and byte size, so the renderer can jump straight to the prerasterized alpha bitmap without reparsing text descriptors.

`tahoma.ttf` stays in this directory as the local source asset for the rasterizer. The FAT staging step copies `system_ui.rtf`, `system_ui.font`, and the generated `tahoma-*.rtf` files into `C:\fonts` inside `fat32.img`.

Regenerate the staged raster assets with:

```bash
python tools/fntmaker.py --input applications/assets/fonts/tahoma.ttf --output-dir applications/assets/fonts --prefix tahoma --family Tahoma --sizes 12 14 16 18
```

Regenerate the fixed-size System UI raster fallback with:

```bash
python tools/fntmaker.py --input applications/assets/fonts/tahoma.ttf --output-dir applications/assets/fonts --prefix system_ui --family "System UI" --sizes 12
```
