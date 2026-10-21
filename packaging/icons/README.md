# Application icons

`tpc2-next.svg` is the editable source artwork. The PNG, ICO and macOS
iconset files are generated from the same design with:

```sh
python -m pip install pillow
python scripts/generate-icons.py
```

The generated files are committed so a normal build does not need Pillow:

- `tpc2-next-*.png` — Linux desktop icons and README artwork.
- `tpc2-next.ico` — Windows executable resource.
- `tpc2-next.iconset/` — input for macOS `iconutil`, which creates the
  `.icns` during the build.
