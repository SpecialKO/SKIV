# Little CMS 2

Used by SKIV to convert images that are not sRGB (wide gamut photos: Display P3, Adobe RGB, ICC profiles of other spaces) into
the scRGB the viewer draws in (`src/tabs/viewer.cpp`, see `SKIV_ConvertToScRGB`).

- Version: Little CMS 2.20 (released 2026-10-31), `lcms2-2.20.tar.gz` from https://github.com/mm2/Little-CMS/releases
  (SHA-256 `18ff5504fa9cc8a2b1f3591180d90ead16c8a2ea78e530523b46eab70e1ca6b9`)
- Only `src/` (the library) and `include/` are included, without the plugins, utilities and tests. The files are unchanged.
- Compiled as part of `SKIV.vcxproj` (filter "Source Files\lcms2"), with `include/` as an include directory.
- License: MIT (see `LICENSE`).

To update: replace the files with those of a newer release (the same file names; add any new `.c` files in `src/` to
`SKIV.vcxproj` and its filters).
