# libwebp (decoder and demux only)

Used by SKIV to decode WebP images, still and animated (`src/tabs/viewer.cpp`, through `src/utility/animated_images.inl`).

- Version: libwebp v1.6.0 (commit 4fa2191, 2025-07-07), from https://github.com/webmproject/libwebp
- Only the sources of `libwebpdecoder` and `libwebpdemux` are included (as listed in libwebp's `Makefile.vc`), without the
  MIPS / MSA / NEON variants, which SKIV's Win32 / x64 builds do not use. The files are unchanged.
- Compiled as part of `SKIV.vcxproj` (filter "Source Files\libwebp"), with this folder as an include directory, as the
  sources include each other as `src/...`.
- License: BSD 3-Clause (see `COPYING` and `PATENTS`).

To update: replace the files with those of a newer release (the same file names; add any new ones `Makefile.vc` lists for
`DEC_OBJS`, `DEMUX_OBJS`, `DSP_DEC_OBJS` and `UTILS_DEC_OBJS` to `SKIV.vcxproj` and its filters).
