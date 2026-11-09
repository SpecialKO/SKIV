// Tests of the animated image decoders (src/utility/animated_images.inl): APNG, GIF and WebP frames, delays, disposal and
//   blending, IsAnimatedImageFile, still WebPs, malformed files, and color sources (src/utility/color_source.inl: ICC
//   profiles, cICP, gAMA/cHRM and Exif in PNG, JPEG and WebP files made here). The expected frames (expected.txt, frames\) come from
//     reference decoders (ffmpeg, Pillow), see make_expected.py. Built and run by run_tests.ps1; returns 0 if all pass.
//       Ported from SKIF (tools\image_tests), and built against SKIV's own stb_image (packages_misc), as the viewer is.
//
//   image_tests [folder with images\, frames\ and expected.txt]

// As the viewer configures stb_image (src/tabs/viewer.cpp)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#define STBI_ONLY_PSD
#define STBI_ONLY_GIF
#define STBI_ONLY_HDR
#include <stb_image.h>

// SKIV's stb_image reports what it found in a PNG through these (the viewer defines them)
thread_local stbi__context::iccp_s SKIV_STBI_ICCP;
thread_local bool                  SKIV_STBI_srgb;
thread_local stbi__context::cicp_s SKIV_STBI_CICP;
thread_local stbi__context::sbit_s SKIV_STBI_SBIT;
thread_local stbi__result_info     SKIV_STBI_ResultInfo;

#include <libwebp/src/webp/decode.h>
#include <libwebp/src/webp/demux.h>

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../../src/utility/animated_images.inl"
#include "../../src/utility/color_source.inl"

namespace fs = std::filesystem;

static int failures = 0,
           checks   = 0;

static void
Check (bool ok, const std::string& name, const std::string& what)
{
  checks++;

  if (! ok)
  {
    failures++;
    printf ("FAIL  %s: %s\n", name.c_str ( ), what.c_str ( ));
  }
}

// A frame's RGBA, with the color of fully transparent pixels taken as black (as in make_expected.py)
static std::vector <uint8_t>
Normalize (const uint8_t* rgba, size_t pixels)
{
  std::vector <uint8_t> out (rgba, rgba + pixels * 4);

  for (size_t i = 0; i < pixels; i++)
    if (out [i * 4 + 3] == 0)
      memset (&out [i * 4], 0, 3);

  return out;
}

// The largest difference of a channel between two frames. Blending semi-transparent pixels may differ by one between
//   decoders (rounding: ffmpeg truncates), so up to 1 is accepted.
static int
MaxDifference (const std::vector <uint8_t>& a, const std::vector <uint8_t>& b)
{
  if (a.size ( ) != b.size ( ))
    return 256;

  int most = 0;

  for (size_t i = 0; i < a.size ( ); i++)
    most = std::max (most, std::abs (static_cast <int> (a [i]) - static_cast <int> (b [i])));

  return most;
}

static std::vector <uint8_t>
ReadFile (const fs::path& path)
{
  std::ifstream         file (path, std::ios::binary);
  std::vector <uint8_t> data ((std::istreambuf_iterator <char> (file)), std::istreambuf_iterator <char> ( ));
  return data;
}

struct frame_s   { uint32_t delay = 0; std::vector <uint8_t> rgba; };
struct decoded_s { int result = -1; uint32_t width = 0, height = 0; std::vector <frame_s> frames; };

// Decodes the frames of an animated image as the viewer does (_OpenAnimatedImage, then its decoder). stopAfter: onFrame returns
//   false after that many frames.
static decoded_s
Decode (const std::vector <uint8_t>& file, size_t stopAfter = SIZE_MAX)
{
  decoded_s out;
  apng_s    apng;
  uint32_t  count = 0;

  const bool isWebP = _IsWebP (file.data ( ), file.size ( )),
             isGIF  = (file.size ( ) >= 4 && memcmp (file.data ( ), "GIF8", 4) == 0);

  const bool opened =
    (isWebP) ? _ScanWebP  (file.data ( ), file.size ( ), out.width, out.height, count) :
    (isGIF)  ? _ScanGIF   (file.data ( ), file.size ( ), out.width, out.height, count) :
               _ParseAPNG (file.data ( ), file.size ( ), apng);

  if (! opened)
    return out;

  if (! isWebP && ! isGIF)
    out.width = apng.width, out.height = apng.height;

  auto onFrame = [&] (const uint8_t* canvas, uint32_t delay)
  {
    out.frames.push_back ({ delay, Normalize (canvas, static_cast <size_t> (out.width) * out.height) });
    return out.frames.size ( ) < stopAfter;
  };

  out.result = (isWebP) ? _DecodeWebP (file.data ( ), file.size ( ), onFrame) :
               (isGIF)  ? _DecodeGIF  (file.data ( ), file.size ( ), onFrame)
                        : _DecodeAPNG (apng, onFrame);
  return out;
}

struct expected_s { std::string name; uint32_t width = 0, height = 0; std::vector <frame_s> frames; };

static std::vector <expected_s>
ReadExpected (const fs::path& folder)
{
  std::vector <expected_s> out;
  std::ifstream            file (folder / "expected.txt");
  std::string              line;

  while (std::getline (file, line))
  {
    if (! line.empty ( ) && line.back ( ) == '\r')
      line.pop_back ( );

    if (line.empty ( ))
      continue;

    std::istringstream s (line);

    if (line [0] != ' ')
    {
      expected_s e; size_t count = 0;
      s >> e.name >> count >> e.width >> e.height;
      out.push_back (e);
    }

    else if (! out.empty ( ))
    {
      frame_s f;
      s >> f.delay;
      out.back ( ).frames.push_back (f);
    }
  }

  // The frames, from frames\<file>.z
  for (auto& e : out)
  {
    const auto z    = ReadFile (folder / "frames" / (e.name + ".z"));
    int        size = 0;
    char*      rgba = stbi_zlib_decode_malloc (reinterpret_cast <const char*> (z.data ( )), static_cast <int> (z.size ( )), &size);
    const auto each = static_cast <size_t> (e.width) * e.height * 4;

    if (rgba != nullptr && each != 0 && static_cast <size_t> (size) == each * e.frames.size ( ))
      for (size_t i = 0; i < e.frames.size ( ); i++)
        e.frames [i].rgba.assign (rgba + i * each, rgba + (i + 1) * each);

    STBI_FREE (rgba);
  }

  return out;
}

#pragma region Color sources

// A minimal ICC profile: the header (its size, "acsp") and a description tag, as a v2 profile has it ('desc') or a v4
//   one ('mluc', UTF-16BE)
static std::vector <uint8_t>
MakeICC (const std::string& name, bool v4, const std::vector <uint8_t>& trc = { })
{
  std::vector <uint8_t> tag;
  auto put32 = [] (std::vector <uint8_t>& v, uint32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back (static_cast <uint8_t> (x >> s)); };

  if (v4)
  {
    tag = { 'm', 'l', 'u', 'c', 0, 0, 0, 0 };
    put32 (tag, 1); put32 (tag, 12);                      // One record, of 12 bytes
    tag.insert (tag.end ( ), { 'e', 'n', 'U', 'S' });
    put32 (tag, static_cast <uint32_t> (name.size ( ) * 2)); put32 (tag, 28);
    for (char c : name) { tag.push_back (0); tag.push_back (static_cast <uint8_t> (c)); }
  }

  else
  {
    tag = { 'd', 'e', 's', 'c', 0, 0, 0, 0 };
    put32 (tag, static_cast <uint32_t> (name.size ( ) + 1));
    tag.insert (tag.end ( ), name.begin ( ), name.end ( ));
    tag.push_back (0);
    tag.resize (tag.size ( ) + 79, 0);                   // The (empty) Unicode and ScriptCode parts
  }

  // The description, and the red tone curve (rTRC) if given
  const uint32_t tags  = trc.empty ( ) ? 1 : 2,
                 first = 132 + tags * 12;

  std::vector <uint8_t> icc (128, 0);
  memcpy (&icc [36], "acsp", 4);
  put32 (icc, tags);
  icc.insert (icc.end ( ), { 'd', 'e', 's', 'c' });
  put32 (icc, first); put32 (icc, static_cast <uint32_t> (tag.size ( )));

  if (! trc.empty ( ))
  {
    icc.insert (icc.end ( ), { 'r', 'T', 'R', 'C' });
    put32 (icc, first + static_cast <uint32_t> (tag.size ( ))); put32 (icc, static_cast <uint32_t> (trc.size ( )));
  }

  icc.insert (icc.end ( ), tag.begin ( ), tag.end ( ));
  icc.insert (icc.end ( ), trc.begin ( ), trc.end ( ));
  _WriteBE32 (icc.data ( ), static_cast <uint32_t> (icc.size ( )));
  return icc;
}

// A zlib stream of one uncompressed block
static std::vector <uint8_t>
ZlibStored (const std::vector <uint8_t>& data)
{
  std::vector <uint8_t> out = { 0x78, 0x01, 0x01 };
  const uint16_t        len = static_cast <uint16_t> (data.size ( ));
  out.insert (out.end ( ), { static_cast <uint8_t> (len), static_cast <uint8_t> (len >> 8),
                             static_cast <uint8_t> (~len), static_cast <uint8_t> (~len >> 8) });
  out.insert (out.end ( ), data.begin ( ), data.end ( ));

  uint32_t a = 1, b = 0;
  for (uint8_t c : data) { a = (a + c) % 65521; b = (b + a) % 65521; }
  out.resize (out.size ( ) + 4);
  _WriteBE32 (&out [out.size ( ) - 4], (b << 16) | a);
  return out;
}

// An Exif block (TIFF): IFD0 pointing at the Exif IFD (ColorSpace), which points at the interoperability IFD (its index)
static std::vector <uint8_t>
MakeExif (bool little, uint16_t colorSpace, const char* interop)
{
  std::vector <uint8_t> t;
  auto p16 = [&] (uint32_t x) { if (little) { t.push_back (x & 0xFF); t.push_back ((x >> 8) & 0xFF); } else { t.push_back ((x >> 8) & 0xFF); t.push_back (x & 0xFF); } };
  auto p32 = [&] (uint32_t x) { if (little) { p16 (x & 0xFFFF); p16 (x >> 16); } else { p16 (x >> 16); p16 (x & 0xFFFF); } };

  t.insert (t.end ( ), { static_cast <uint8_t> (little ? 'I' : 'M'), static_cast <uint8_t> (little ? 'I' : 'M') });
  p16 (42); p32 (8);
  p16 (1); p16 (0x8769); p16 (4); p32 (1); p32 (26); p32 (0);                // IFD0, at 8
  p16 (2); p16 (0xA001); p16 (3); p32 (1); p16 (colorSpace); p16 (0);        // Exif IFD, at 26
           p16 (0xA005); p16 (4); p32 (1); p32 (56); p32 (0);
  p16 (1); p16 (0x0001); p16 (2); p32 (4);                                   // Interoperability IFD, at 56
  t.insert (t.end ( ), interop, interop + 3); t.push_back (0); p32 (0);
  return t;
}

static void
TestColorSources (const fs::path& images)
{
  const auto iccV2 = MakeICC ("Test Profile", false),
             iccV4 = MakeICC ("Display P3",   true);

  // The names of both kinds of profile
  Check (_ReadICCName (iccV2.data ( ), iccV2.size ( )) == "Test Profile", "ICC v2", "wrong name: " + _ReadICCName (iccV2.data ( ), iccV2.size ( )));
  Check (_ReadICCName (iccV4.data ( ), iccV4.size ( )) == "Display P3",   "ICC v4", "wrong name: " + _ReadICCName (iccV4.data ( ), iccV4.size ( )));

  // PNG: chunks inserted after IHDR (33 bytes in)
  const auto still = ReadFile (images / "still.png");

  auto png = [&] (const std::vector <std::pair <const char*, std::vector <uint8_t>>>& chunks)
  {
    std::vector <uint8_t> out (still.begin ( ), still.begin ( ) + 33), chunk;

    for (const auto& c : chunks)
    {
      chunk.clear ( );
      _AppendPNGChunk (chunk, c.first, c.second.data ( ), c.second.size ( ));
      out.insert (out.end ( ), chunk.begin ( ), chunk.end ( ));
    }

    out.insert (out.end ( ), still.begin ( ) + 33, still.end ( ));
    return out;
  };

  std::vector <uint8_t> iccp = { 'P', '3', 0, 0 };
  const auto            z    = ZlibStored (iccV4);
  iccp.insert (iccp.end ( ), z.begin ( ), z.end ( ));

  std::vector <uint8_t> chrm (32);
  const uint32_t        chrmValues [8] = { 31270, 32900, 64000, 33000, 30000, 60000, 15000, 6000 };
  for (int i = 0; i < 8; i++) _WriteBE32 (&chrm [i * 4], chrmValues [i]);

  {
    color_source_s cs;
    const auto     file = png ({ { "iCCP", iccp }, { "gAMA", { 0, 0, 0xB1, 0x8F } }, { "cHRM", chrm } });
    _ReadColorSourcePNG (file.data ( ), file.size ( ), cs);

    Check (cs.icc == iccV4 && cs.icc_name == "Display P3", "PNG iCCP", "profile not read");
    Check (cs.gamma == 45455 && cs.has_chrm && cs.chrm [2] == 0.64f, "PNG gAMA, cHRM", "not read");
    Check (_DescribeColorSource (cs) == "ICC: Display P3", "PNG iCCP", "described as " + _DescribeColorSource (cs));

    for (size_t size = 0; size < file.size ( ); size++)
    {
      color_source_s cut;
      _ReadColorSourcePNG (file.data ( ), size, cut);
    }
  }

  {
    color_source_s cs;
    const auto     file = png ({ { "cICP", { 9, 16, 0, 1 } }, { "iCCP", iccp } });
    _ReadColorSourcePNG (file.data ( ), file.size ( ), cs);

    Check (cs.has_cicp && cs.primaries == 9 && cs.transfer == 16 && cs.full_range == 1, "PNG cICP", "not read");
    Check (_DescribeColorSource (cs) == "cICP: Rec. 2020, PQ", "PNG cICP", "does not override iCCP: " + _DescribeColorSource (cs));
  }

  {
    color_source_s cs, none;
    const auto     file = png ({ { "sRGB", { 0 } }, { "gAMA", { 0, 0, 0xB1, 0x8F } } });
    _ReadColorSourcePNG (file.data ( ), file.size ( ), cs);
    _ReadColorSourcePNG (still.data ( ), still.size ( ), none);

    Check (cs.has_srgb && _DescribeColorSource (cs) == "sRGB (PNG sRGB chunk)", "PNG sRGB", "described as " + _DescribeColorSource (cs));
    Check (_DescribeColorSource (none) == "Not specified", "still.png", "described as " + _DescribeColorSource (none));
  }

  {
    color_source_s cs;
    const auto     file = png ({ { "gAMA", { 0, 1, 0x86, 0xA0 } } }); // 100000: linear
    _ReadColorSourcePNG (file.data ( ), file.size ( ), cs);
    Check (_DescribeColorSource (cs) == "Gamma 1.00 (PNG gAMA)", "PNG gAMA 1.0", "described as " + _DescribeColorSource (cs));
  }

  // JPEG: Exif (both byte orders) and an ICC profile split across two APP2 segments
  for (bool little : { false, true })
  {
    std::vector <uint8_t> jpeg = { 0xFF, 0xD8 };

    auto segment = [&] (uint8_t marker, const std::vector <uint8_t>& body)
    {
      jpeg.insert (jpeg.end ( ), { 0xFF, marker, static_cast <uint8_t> ((body.size ( ) + 2) >> 8), static_cast <uint8_t> (body.size ( ) + 2) });
      jpeg.insert (jpeg.end ( ), body.begin ( ), body.end ( ));
    };

    const auto exif = MakeExif (little, 65535, "R03");
    std::vector <uint8_t> app1 = { 'E', 'x', 'i', 'f', 0, 0 };
    app1.insert (app1.end ( ), exif.begin ( ), exif.end ( ));

    segment (0xE0, { 'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0 });
    segment (0xE1, app1);

    const size_t half = iccV2.size ( ) / 2;

    for (int part = 0; part < 2; part++)
    {
      std::vector <uint8_t> app2 = { 'I', 'C', 'C', '_', 'P', 'R', 'O', 'F', 'I', 'L', 'E', 0, static_cast <uint8_t> (part + 1), 2 };
      app2.insert (app2.end ( ), iccV2.begin ( ) + (part ? half : 0), part ? iccV2.end ( ) : iccV2.begin ( ) + half);
      segment (0xE2, app2);
    }

    jpeg.insert (jpeg.end ( ), { 0xFF, 0xDA, 0, 2, 0x12, 0x34, 0xFF, 0xD9 });

    const std::string name = little ? "JPEG (Exif II)" : "JPEG (Exif MM)";
    color_source_s    cs;
    _ReadColorSourceJPEG (jpeg.data ( ), jpeg.size ( ), cs);

    Check (cs.exif_color_space == 65535 && strcmp (cs.exif_interop, "R03") == 0, name, "Exif not read");
    Check (cs.icc == iccV2 && _DescribeColorSource (cs) == "ICC: Test Profile", name, "profile not read: " + _DescribeColorSource (cs));

    // Without the profile: the Exif color space
    cs.icc.clear ( );
    Check (_DescribeColorSource (cs) == "Adobe RGB (Exif, R03)", name, "described as " + _DescribeColorSource (cs));

    for (size_t size = 0; size < jpeg.size ( ); size++)
    {
      color_source_s cut;
      _ReadColorSourceJPEG (jpeg.data ( ), size, cut);
      Check (cut.icc.empty ( ) || cut.icc == iccV2, name, "a cut-off profile was kept");
    }
  }

  // WebP: an extended WebP (VP8X) made of a still lossless one, with ICCP and EXIF chunks
  {
    const auto lossless = ReadFile (images / "webp_still_lossless.webp");
    std::vector <uint8_t> body = { 'W', 'E', 'B', 'P' };

    auto chunk = [&] (const char* fourcc, const std::vector <uint8_t>& data)
    {
      body.insert (body.end ( ), fourcc, fourcc + 4);
      const uint32_t size = static_cast <uint32_t> (data.size ( ));
      body.insert (body.end ( ), { static_cast <uint8_t> (size), static_cast <uint8_t> (size >> 8), static_cast <uint8_t> (size >> 16), static_cast <uint8_t> (size >> 24) });
      body.insert (body.end ( ), data.begin ( ), data.end ( ));
      if (size & 1) body.push_back (0);
    };

    chunk ("VP8X", { 0x20 | 0x08, 0, 0, 0, 59, 0, 0, 89, 0, 0 }); // ICCP and EXIF; 60x90
    chunk ("ICCP", iccV4);
    body.insert (body.end ( ), lossless.begin ( ) + 12, lossless.end ( ));   // Its VP8L chunk
    chunk ("EXIF", MakeExif (true, 1, "R98"));

    std::vector <uint8_t> webp = { 'R', 'I', 'F', 'F', 0, 0, 0, 0 };
    webp.insert (webp.end ( ), body.begin ( ), body.end ( ));
    const uint32_t riff = static_cast <uint32_t> (body.size ( ));
    memcpy (&webp [4], &riff, 4);

    color_source_s cs;
    _ReadColorSourceWebP (webp.data ( ), webp.size ( ), cs);

    Check (cs.icc == iccV4 && _DescribeColorSource (cs) == "ICC: Display P3", "WebP ICCP", "profile not read: " + _DescribeColorSource (cs));
    Check (cs.exif_color_space == 1 && strcmp (cs.exif_interop, "R98") == 0, "WebP EXIF", "not read");

    uint32_t width = 0, height = 0; std::vector <uint8_t> rgba;
    Check (_DecodeWebPStill (webp.data ( ), webp.size ( ), width, height, rgba) && width == 60 && height == 90, "WebP ICCP", "the test file does not decode");

    for (size_t size = 0; size < webp.size ( ); size++)
    {
      color_source_s cut;
      _ReadColorSourceWebP (webp.data ( ), size, cut);
    }
  }

  // Linear or not (sRGB): what the viewer decides from them
  {
    const std::vector <uint8_t> identity = { 'c', 'u', 'r', 'v', 0, 0, 0, 0,  0, 0, 0, 0 },
                                gamma10  = { 'c', 'u', 'r', 'v', 0, 0, 0, 0,  0, 0, 0, 1,  1, 0 },
                                gamma22  = { 'c', 'u', 'r', 'v', 0, 0, 0, 0,  0, 0, 0, 1,  2, 0x33 },
                                para10   = { 'p', 'a', 'r', 'a', 0, 0, 0, 0,  0, 0, 0, 0,  0, 1, 0, 0 },
                                para22   = { 'p', 'a', 'r', 'a', 0, 0, 0, 0,  0, 0, 0, 0,  0, 2, 0x33, 0x33 };

    auto linearICC = [&] (const std::vector <uint8_t>& trc)
    {
      color_source_s cs;
      const auto     icc = MakeICC ("Curve", false, trc);
      _SetICC (cs, icc.data ( ), icc.size ( ));
      return ! cs.icc.empty ( ) && _IsLinearColorSource (cs);
    };

    Check (  linearICC (identity), "ICC rTRC identity",  "not linear");
    Check (  linearICC (gamma10),  "ICC rTRC gamma 1.0", "not linear");
    Check (! linearICC (gamma22),  "ICC rTRC gamma 2.2", "linear");
    Check (  linearICC (para10),   "ICC rTRC para 1.0",  "not linear");
    Check (! linearICC (para22),   "ICC rTRC para 2.2",  "linear");
    Check (! linearICC ({ }),      "ICC without rTRC",   "linear");

    auto linearPNG = [&] (const std::vector <std::pair <const char*, std::vector <uint8_t>>>& chunks)
    {
      color_source_s cs;
      const auto     file = png (chunks);
      _ReadColorSourcePNG (file.data ( ), file.size ( ), cs);
      return _IsLinearColorSource (cs);
    };

    Check (  linearPNG ({ { "gAMA", { 0, 1, 0x86, 0xA0 } } }),                        "PNG gAMA 1.0",          "not linear");
    Check (! linearPNG ({ { "gAMA", { 0, 0, 0xB1, 0x8F } } }),                        "PNG gAMA 1/2.2",        "linear");
    Check (! linearPNG ({ { "sRGB", { 0 } }, { "gAMA", { 0, 1, 0x86, 0xA0 } } }),     "PNG sRGB + gAMA 1.0",   "linear");
    Check (  linearPNG ({ { "cICP", { 1, 8, 0, 1 } } }),                              "PNG cICP linear",       "not linear");
    Check (! linearPNG ({ { "cICP", { 1, 13, 0, 1 } }, { "gAMA", { 0, 1, 0x86, 0xA0 } } }), "PNG cICP sRGB + gAMA 1.0", "linear");
    Check (! linearPNG ({ }),                                                         "PNG without chunks",    "linear");

    color_source_s exif;
    exif.exif_color_space = 65535;
    Check (! _IsLinearColorSource (exif), "Exif 65535", "linear");
  }

  // Not images, or garbage
  for (const char* name : { "garbage.png", "garbage.gif", "garbage.webp" })
  {
    const auto     file = ReadFile (images / name);
    color_source_s cs;
    _ReadColorSourcePNG  (file.data ( ), file.size ( ), cs);
    _ReadColorSourceJPEG (file.data ( ), file.size ( ), cs);
    _ReadColorSourceWebP (file.data ( ), file.size ( ), cs);
    Check (cs.icc.empty ( ) && ! cs.has_cicp && cs.exif_color_space == 0, name, "color information found in garbage");
  }
}

#pragma endregion

int
main (int argc, char** argv)
{
  const fs::path folder = (argc > 1) ? fs::path (argv [1]) : fs::path (argv [0]).parent_path ( );
  const fs::path images = folder / "images";

  const auto all = ReadExpected (folder);
  Check (all.size ( ) >= 10, "expected.txt", "missing or incomplete (in " + folder.string ( ) + ")");

  // Animated images: the frames, as the reference decoders see them
  for (const auto& e : all)
  {
    const auto file = ReadFile (images / e.name);
    const auto got  = Decode   (file);

    Check (got.result == static_cast <int> (e.frames.size ( )), e.name, "decoded " + std::to_string (got.result) +
                                                                " frames, expected " + std::to_string (e.frames.size ( )));
    Check (got.width == e.width && got.height == e.height, e.name, "wrong size " + std::to_string (got.width) + "x" + std::to_string (got.height));

    for (size_t i = 0; i < std::min (got.frames.size ( ), e.frames.size ( )); i++)
    {
      Check (got.frames [i].delay == e.frames [i].delay, e.name, "frame " + std::to_string (i) + ": delay " +
                                    std::to_string (got.frames [i].delay) + " ms, expected " + std::to_string (e.frames [i].delay));
      const int difference = MaxDifference (got.frames [i].rgba, e.frames [i].rgba);
      Check (difference <= 1, e.name, "frame " + std::to_string (i) + ": pixels differ (by up to " + std::to_string (difference) + ")");
    }

    Check (IsAnimatedImageFile ((images / e.name).wstring ( )), e.name, "not seen as animated");

    // Stopped by onFrame after two frames: decodes no more
    const auto stopped = Decode (file, 2);
    Check (stopped.frames.size ( ) == 2, e.name, "did not stop after two frames");
  }

  // SKIV only: an APNG with a gAMA chunk (which SKIV's stb_image refuses) decodes as it would without one
  {
    const auto original = ReadFile (images / "apng_basic.png");
    auto       withGamma = original;

    // gAMA (1 / 2.2), right after IHDR (8 bytes of signature, 25 of IHDR)
    std::vector <uint8_t> chunk;
    const uint8_t gamma [4] = { 0x00, 0x00, 0xB1, 0x8F }; // 45455
    _AppendPNGChunk (chunk, "gAMA", gamma, sizeof (gamma));
    withGamma.insert (withGamma.begin ( ) + 33, chunk.begin ( ), chunk.end ( ));

    const auto expected = Decode (original);
    const auto got      = Decode (withGamma);

    Check (got.result > 0 && got.result == expected.result, "apng_basic.png + gAMA", "decoded " + std::to_string (got.result) + " frames");

    for (size_t i = 0; i < std::min (got.frames.size ( ), expected.frames.size ( )); i++)
      Check (got.frames [i].rgba == expected.frames [i].rgba, "apng_basic.png + gAMA", "frame " + std::to_string (i) + ": pixels differ");
  }

  // Not animated: still images, and files that are not images at all
  for (const char* name : { "still.png", "still.gif", "webp_still.webp", "webp_still_lossless.webp",
                            "garbage.png", "garbage.gif", "garbage.webp", "missing.png" })
    Check (! IsAnimatedImageFile ((images / name).wstring ( )), name, "seen as animated");

  // Still WebPs (the viewer's libwebp decoder): the size, and the pixels of a lossless one
  for (const char* name : { "webp_still.webp", "webp_still_lossless.webp" })
  {
    const auto            file = ReadFile (images / name);
    uint32_t              width = 0, height = 0;
    std::vector <uint8_t> rgba;

    Check (_DecodeWebPStill (file.data ( ), file.size ( ), width, height, rgba) && width == 60 && height == 90 &&
           rgba.size ( ) == 60 * 90 * 4, name, "not decoded");

    // The first frame of webp_lossy, losslessly: the background (20, 40, 60) in the top left corner
    if (strstr (name, "lossless") && rgba.size ( ) >= 4)
      Check (rgba [0] == 20 && rgba [1] == 40 && rgba [2] == 60 && rgba [3] == 255, name, "wrong pixels");
  }

  // Malformed: cut off halfway (the frames decoded before the cut, if any, must be right), or not images at all (must
  //   fail). Neither may crash or read outside the file (run under ASAN / the debug heap to see that).
  for (const auto& e : all)
  {
    fs::path cut = images / e.name;
    cut.replace_filename (cut.stem ( ).string ( ) + "_truncated" + cut.extension ( ).string ( ));

    if (! fs::exists (cut))
      continue;

    const auto got = Decode (ReadFile (cut));
    Check (got.frames.size ( ) < e.frames.size ( ), cut.filename ( ).string ( ), "decoded all the frames of a cut-off file");

    // The last frame decoded may be cut off itself, so only the ones before it are compared
    for (size_t i = 0; i + 1 < got.frames.size ( ); i++)
      Check (MaxDifference (got.frames [i].rgba, e.frames [i].rgba) <= 1, cut.filename ( ).string ( ), "frame " + std::to_string (i) + ": pixels differ");

    // And cut off everywhere else, a byte at a time near the start
    const auto full = ReadFile (images / e.name);

    for (size_t size = 0; size < full.size ( ); size += (size < 256) ? 1 : 61)
      Decode (std::vector <uint8_t> (full.begin ( ), full.begin ( ) + size));
  }

  for (const char* name : { "garbage.png", "garbage.gif", "garbage.webp" })
    Check (Decode (ReadFile (images / name)).result <= 0, name, "decoded");

  TestColorSources (images);

  printf ("%s: %d of %d checks passed\n", (failures == 0) ? "PASS" : "FAIL", checks - failures, checks);
  return (failures == 0) ? 0 : 1;
}
