//
// Copyright 2024-2025 Andon "Kaldaien" Coleman
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//

// Photoshop documents (PSD, and PSB: large documents; PDD: PhotoDeluxe): their composite image, the document flattened,
//   which Photoshop stores along with the layers ("Maximize Compatibility"; a document saved without it has none: only
//   its layers, which are not combined here). Included by image_decode.cpp.
//
//   Of every color mode: RGB, Grayscale, Duotone (as grayscale), Indexed, Bitmap, CMYK, Lab and Multichannel (as CMYK, of
//   its first channels), of 1, 8, 16 and 32 bits (32: linear light, RGB and grayscale), raw or RLE (PackBits) compressed.
//   Out (DirectX::ScratchImage):
//     - RGB, Grayscale, Duotone, Indexed, Bitmap, and CMYK / Multichannel without a profile (converted the simple way):
//       RGBA of 8 or 16 bpc, of the image's colors (its ICC profile, if it has one, is converted by the decoder after,
//       as other images' are); sRGB otherwise, as an _SRGB format (8 bpc), or made linear (16 bpc, half floats).
//     - CMYK with its profile, and Lab: converted to scRGB (half floats, image.is_wcg).
//     - 32 bpc: linear RGBA (floats): HDR (image.is_hdr) if brighter than SDR's white anywhere, otherwise linear SDR.
//   Its transparency: the first channel after the color ones, if the document says it is the composite's (an extra channel
//     is otherwise an alpha channel of its own: a saved selection), or the transparent index of an indexed one.
//   Photoshop stores the composite's colors against white where it is transparent: they are unmatted.

#pragma region PSD

struct psd_reader_s {
  const uint8_t* data = nullptr;
  size_t         size = 0;
  size_t         pos  = 0;
  bool           ok   = true;

  bool     has   (size_t n) const { return ok && pos <= size && n <= size - pos; }
  void     skip  (uint64_t n)     { if (n > size - pos) { ok = false; pos = size; } else pos += static_cast <size_t> (n); }
  uint8_t  u8    (void)           { if (! has (1)) { ok = false; return 0; } return data [pos++]; }
  uint16_t u16   (void)           { if (! has (2)) { ok = false; return 0; } const uint16_t v = static_cast <uint16_t> ((data [pos] << 8) | data [pos + 1]); pos += 2; return v; }
  uint32_t u32   (void)           { const uint32_t hi = u16 ( ); return (hi << 16) | u16 ( ); }
  uint64_t u64   (void)           { const uint64_t hi = u32 ( ); return (hi << 32) | u32 ( ); }
  uint64_t len   (bool psb)       { return (psb) ? u64 ( ) : u32 ( ); } // A length field: 8 bytes in PSB, 4 in PSD
};

// PackBits (the PSD's RLE): a row, of rowBytes bytes, out of in (of inSize bytes). False if malformed.
static bool
_PSDUnpackBits (const uint8_t* in, size_t inSize, uint8_t* out, size_t rowBytes)
{
  size_t i = 0, o = 0;

  while (o < rowBytes && i < inSize)
  {
    const int8_t n = static_cast <int8_t> (in [i++]);

    if (n >= 0)          // n + 1 bytes as they are
    {
      const size_t count = static_cast <size_t> (n) + 1;
      if (i + count > inSize || o + count > rowBytes) return false;
      memcpy (out + o, in + i, count);
      i += count; o += count;
    }

    else if (n != -128)  // The next byte, 1 - n times
    {
      const size_t count = static_cast <size_t> (1 - n);
      if (i >= inSize || o + count > rowBytes) return false;
      memset (out + o, in [i++], count);
      o += count;
    }
  }

  // (Rows too short are padded, as other readers do)
  if (o < rowBytes)
    memset (out + o, 0, rowBytes - o);

  return true;
}

// The composite image of a Photoshop document (data: the whole file), into img, and what is known of it into image
//   (its colors, bit depth, channels, dynamic range). failure: why it could not be read.
static bool
_DecodePSD (const uint8_t* data, size_t size, image_s& image, DirectX::ScratchImage& img, std::string& failure)
{
  psd_reader_s r { data, size };

  // Header
  if (! r.has (26) || memcmp (data, "8BPS", 4) != 0)
    return (failure = "not a Photoshop document", false);

  r.pos = 4;
  const uint16_t version  = r.u16 ( );
  r.skip (6);
  const uint16_t channels = r.u16 ( );
  const uint32_t height   = r.u32 ( );
  const uint32_t width    = r.u32 ( );
  const uint16_t depth    = r.u16 ( );
  const uint16_t mode     = r.u16 ( );
  const bool     psb      = (version == 2);

  enum { Bitmap = 0, Grayscale = 1, Indexed = 2, RGB = 3, CMYK = 4, Multichannel = 7, Duotone = 8, Lab = 9 };

  if (version != 1 && version != 2)
    return (failure = "unknown version " + std::to_string (version), false);

  // (Photoshop's limit is 56 channels, yet documents of more exist)
  if (channels < 1 || channels > 128 || width == 0 || height == 0 || width > 300000 || height > 300000 ||
      static_cast <uint64_t> (width) * height > (1ull << 28)) // (Too large to show, at 16 bytes per pixel)
    return (failure = "unsupported size or channel count (" + std::to_string (width) + "x" + std::to_string (height) + ", " + std::to_string (channels) + " channels)", false);

  if (! (depth == 1 && mode == Bitmap) && depth != 8 && depth != 16 && depth != 32)
    return (failure = "unsupported bit depth (" + std::to_string (depth) + ")", false);

  if (mode != Bitmap && mode != Grayscale && mode != Indexed && mode != RGB && mode != CMYK &&
      mode != Multichannel && mode != Duotone && mode != Lab)
    return (failure = "unsupported color mode (" + std::to_string (mode) + ")", false);

  if (depth == 32 && (mode == CMYK || mode == Lab || mode == Multichannel))
    return (failure = "32-bit CMYK, Lab and multichannel documents are not supported", false);

  // Color mode data: the palette of an indexed one (256 red values, then green, then blue)
  const uint32_t colorModeSize = r.u32 ( );
  const uint8_t* palette       = (r.has (colorModeSize)) ? data + r.pos : nullptr;
  r.skip (colorModeSize);

  if (mode == Indexed && (palette == nullptr || colorModeSize < 768))
    return (failure = "indexed, without its palette", false);

  // Image resources: its ICC profile (1039), the transparent index of an indexed one (1047), the identifiers of its alpha
  //   channels (1053; identifier 0: transparency), and whether it has a composite image at all (1057)
  const uint32_t resourcesSize = r.u32 ( );
  const size_t   resourcesEnd  = r.pos + std::min <size_t> (resourcesSize, r.size - r.pos);

  const uint8_t* icc            = nullptr;
  uint32_t       iccSize        = 0;
  int            transparentIdx = -1;
  int            alphaIds       = -1; // -1: none; otherwise whether the first alpha channel's identifier is 0
  bool           hasMerged      = true;

  while (r.ok && r.pos + 12 <= resourcesEnd)
  {
    r.skip (4);                              // Signature ("8BIM", or others)
    const uint16_t id      = r.u16 ( );
    const uint8_t  nameLen = r.u8  ( );
    r.skip ((nameLen + 1) % 2 == 0 ? nameLen : nameLen + 1); // Its name: a Pascal string, padded to an even size
    const uint32_t length  = r.u32 ( );

    if (! r.has (length))
      break;

    const uint8_t* block = data + r.pos;

    if      (id == 1039)                 { icc = block; iccSize = length; }
    else if (id == 1047 && length >= 2)  { transparentIdx = (block [0] << 8) | block [1]; }
    else if (id == 1053 && length >= 4)  { alphaIds = ((block [0] | block [1] | block [2] | block [3]) == 0) ? 1 : 0; }
    else if (id == 1057)                 { hasMerged = (length > 4 && block [4] != 0); }

    r.skip (length + (length & 1));
  }

  r.ok  = true;
  r.pos = resourcesEnd;

  if (! hasMerged)
    return (failure = "it has no composite image (saved without Maximize Compatibility): only its layers, which SKIV does not combine", false);

  // Layer and mask information: only whether the composite has transparency (a negative layer count, or the global
  //   additional information holding Mt16, Mt32 or Mtrn: its transparency for 16 and 32-bit documents)
  bool layersAlpha = false;
  {
    const uint64_t sectionSize = r.len (psb);
    const size_t   sectionEnd  = (sectionSize <= r.size - r.pos) ? r.pos + static_cast <size_t> (sectionSize) : r.size;

    if (sectionSize > 0)
    {
      const size_t   infoStart = r.pos;
      const uint64_t infoSize  = r.len (psb);
      const size_t   infoBody  = r.pos;

      if (infoSize >= 2 && static_cast <int16_t> (r.u16 ( )) < 0)
        layersAlpha = true;

      r.pos = infoBody;
      r.skip (infoSize);

      // Global layer mask information (always a 4-byte length)
      if (r.pos + 4 <= sectionEnd)
        r.skip (r.u32 ( ));

      // Global additional layer information: tagged blocks (8BIM or 8B64, a key, a length padded to 4 bytes). Some
      //   keys' lengths are 8 bytes in PSB.
      while (r.ok && r.pos + 12 <= sectionEnd)
      {
        const bool sig = (memcmp (data + r.pos, "8BIM", 4) == 0 || memcmp (data + r.pos, "8B64", 4) == 0);
        if (! sig)
          break;

        r.skip (4);
        char key [5] = { };
        memcpy (key, data + r.pos, 4);
        r.skip (4);

        static const char* longKeys [] = { "LMsk", "Lr16", "Lr32", "Layr", "Mt16", "Mt32", "Mtrn", "Alph", "FMsk", "lnk2", "FEid", "FXid", "PxSD" };
        bool longLength = false;
        if (psb) for (const char* k : longKeys) if (memcmp (key, k, 4) == 0) longLength = true;

        const uint64_t length = (longLength) ? r.u64 ( ) : r.u32 ( );

        if (memcmp (key, "Mt16", 4) == 0 || memcmp (key, "Mt32", 4) == 0 || memcmp (key, "Mtrn", 4) == 0)
          layersAlpha = true;

        // 16 and 32-bit documents' layers: their layer count, as the layer information's
        if ((memcmp (key, "Lr16", 4) == 0 || memcmp (key, "Lr32", 4) == 0) && length >= 2 && r.has (2) &&
            static_cast <int16_t> ((data [r.pos] << 8) | data [r.pos + 1]) < 0)
          layersAlpha = true;

        r.skip ((length + 3) & ~3ull);
      }

      (void) infoStart;
    }

    r.ok  = true;
    r.pos = sectionEnd;
  }

  // The channels of its colors, and whether the next one is the composite's transparency
  const int colorChannels = (mode == RGB || mode == Lab) ? 3 : (mode == CMYK) ? 4 :
                            (mode == Multichannel) ? std::min <int> (channels, 4) : 1;

  if (channels < colorChannels)
    return (failure = "too few channels (" + std::to_string (channels) + ")", false);

  const bool alpha = (mode != Multichannel && mode != Indexed && mode != Bitmap && channels > colorChannels &&
                      ((alphaIds >= 0) ? (alphaIds == 1) : layersAlpha));

  const int used = colorChannels + (alpha ? 1 : 0);

  // Image data: planar, each channel's rows after the other's; raw, or RLE (a table of each row's compressed size first)
  const uint16_t compression = r.u16 ( );
  const size_t   rowBytes    = (depth == 1) ? (width + 7) / 8 : static_cast <size_t> (width) * (depth / 8);
  const size_t   planeBytes  = rowBytes * height;

  if (! r.ok)
    return (failure = "truncated", false);

  if (compression != 0 && compression != 1)
    return (failure = "unsupported compression of the composite image (" + std::to_string (compression) + ")", false);

  std::vector <std::vector <uint8_t>> planes (used);

  if (compression == 0)
  {
    for (int c = 0; c < used; c++)
    {
      const uint64_t offset = r.pos + static_cast <uint64_t> (c) * planeBytes;

      if (offset + planeBytes > r.size)
        return (failure = "truncated image data", false);

      planes [c].assign (data + offset, data + offset + planeBytes);
    }
  }

  else
  {
    const size_t   countSize  = (psb) ? 4 : 2;
    const uint64_t tableBytes = static_cast <uint64_t> (channels) * height * countSize;

    if (r.pos + tableBytes > r.size)
      return (failure = "truncated RLE table", false);

    const uint8_t* table  = data + r.pos;
    uint64_t       offset = r.pos + tableBytes;

    auto _Count = [&](uint64_t row) -> uint64_t {
      const uint8_t* p = table + row * countSize;
      return (psb) ? (static_cast <uint64_t> (p [0]) << 24 | p [1] << 16 | p [2] << 8 | p [3]) : static_cast <uint64_t> (p [0] << 8 | p [1]);
    };

    for (int c = 0; c < used; c++)
    {
      planes [c].resize (planeBytes);

      for (uint32_t y = 0; y < height; y++)
      {
        const uint64_t count = _Count (static_cast <uint64_t> (c) * height + y);

        if (offset + count > r.size || ! _PSDUnpackBits (data + offset, static_cast <size_t> (count), planes [c].data ( ) + y * rowBytes, rowBytes))
          return (failure = "malformed RLE data", false);

        offset += count;
      }
    }
  }

  // A sample of a channel, as 0-1 (8 and 16 bpc) or as it is (32 bpc: floats)
  auto _Sample = [&](int c, size_t index) -> float
  {
    const uint8_t* p = planes [c].data ( );

    if (depth == 8)  return p [index] / 255.0f;
    if (depth == 16) return ((p [index * 2] << 8) | p [index * 2 + 1]) / 65535.0f;

    const uint32_t bits = static_cast <uint32_t> (p [index * 4]) << 24 | p [index * 4 + 1] << 16 | p [index * 4 + 2] << 8 | p [index * 4 + 3];
    float value; memcpy (&value, &bits, 4);
    return std::isfinite (value) ? value : 0.0f;
  };

  auto _SampleRaw16 = [&](int c, size_t index) -> uint16_t // (8 bpc: widened)
  {
    const uint8_t* p = planes [c].data ( );
    return (depth == 8) ? static_cast <uint16_t> (p [index] * 257) : static_cast <uint16_t> ((p [index * 2] << 8) | p [index * 2 + 1]);
  };

  const size_t pixels = static_cast <size_t> (width) * height;

  image.color_source.origin = "PSD";
  image.bpc                 = depth;
  image.channels            = (alpha || (mode == Indexed && transparentIdx >= 0)) ? 4 : 3;

  // CMYK with its profile, and Lab: converted to scRGB by Little CMS (CMYK as ink amounts: Photoshop stores them inverted)
  const bool cmykProfile = (mode == CMYK && icc != nullptr && iccSize >= 132);

  if (cmykProfile || mode == Lab)
  {
    cmsHPROFILE source = (mode == Lab) ? cmsCreateLab4Profile (nullptr)
                                       : cmsOpenProfileFromMem (icc, iccSize);

    if (source != nullptr && mode == CMYK && cmsGetColorSpace (source) != cmsSigCmykData)
    {
      cmsCloseProfile (source);
      source = nullptr;
    }

    if (source != nullptr)
    {
      // Interleaved, 16 bpc (8 bpc widened), with its alpha (opaque if none)
      std::vector <uint16_t> in (pixels * (colorChannels + 1));

      for (size_t i = 0; i < pixels; i++)
      {
        uint16_t* px = in.data ( ) + i * (colorChannels + 1);

        for (int c = 0; c < colorChannels; c++)
          px [c] = (mode == CMYK) ? static_cast <uint16_t> (65535 - _SampleRaw16 (c, i)) : _SampleRaw16 (c, i);

        px [colorChannels] = (alpha) ? _SampleRaw16 (colorChannels, i) : 65535;
      }

      const cmsUInt32Number inFormat = (mode == CMYK) ? (COLORSPACE_SH (PT_CMYK) | CHANNELS_SH (4) | EXTRA_SH (1) | BYTES_SH (2))
                                                      : (COLORSPACE_SH (PT_Lab)  | CHANNELS_SH (3) | EXTRA_SH (1) | BYTES_SH (2));
      bool converted = false;

      if (SUCCEEDED (img.Initialize2D (DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, 1, 1)))
      {
        converted = _ConvertToScRGB (source, inFormat, reinterpret_cast <const uint8_t*> (in.data ( )), static_cast <size_t> (width) * (colorChannels + 1) * 2,
                                     img.GetPixels ( ), img.GetImage (0, 0, 0)->rowPitch, width, height);
      }

      cmsCloseProfile (source);

      if (converted)
      {
        if (mode == CMYK)
          _SetICC (image.color_source, icc, iccSize);
        else
          image.color_source.format_defined = "CIE L*a*b* (D50)";

        image.is_wcg = true;
        return true;
      }

      img.Release ( );
    }

    if (mode == Lab)
      return (failure = "its Lab colors could not be converted", false);

    // (CMYK: its profile could not be used: converted the simple way, below)
  }

  // The rest: RGBA, of 8 or 16 bpc (32: floats)
  const DXGI_FORMAT format = (depth == 32) ? DXGI_FORMAT_R32G32B32A32_FLOAT :
                             (depth == 16) ? DXGI_FORMAT_R16G16B16A16_UNORM  : DXGI_FORMAT_R8G8B8A8_UNORM;

  if (FAILED (img.Initialize2D (format, width, height, 1, 1)))
    return (failure = "out of memory", false);

  const DirectX::Image* out = img.GetImage (0, 0, 0);

  auto _Put = [&](size_t i, float rgba [4])
  {
    const size_t   x   = i % width, y = i / width;
    uint8_t*       row = out->pixels + y * out->rowPitch;

    if (depth == 32)
      memcpy (row + x * 16, rgba, 16);

    else if (depth == 16)
      for (int c = 0; c < 4; c++)
        reinterpret_cast <uint16_t*> (row) [x * 4 + c] = static_cast <uint16_t> (std::clamp (rgba [c], 0.0f, 1.0f) * 65535.0f + 0.5f);

    else
      for (int c = 0; c < 4; c++)
        row [x * 4 + c] = static_cast <uint8_t> (std::clamp (rgba [c], 0.0f, 1.0f) * 255.0f + 0.5f);
  };

  for (size_t i = 0; i < pixels; i++)
  {
    float rgba [4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    switch (mode)
    {
      case Bitmap: // 1: black
      {
        const size_t x = i % width, y = i / width;
        const bool   black = (planes [0][y * rowBytes + x / 8] >> (7 - (x % 8))) & 1;
        rgba [0] = rgba [1] = rgba [2] = (black) ? 0.0f : 1.0f;
        break;
      }

      case Indexed:
      {
        const uint8_t index = planes [0][i];
        rgba [0] = palette [index]       / 255.0f;
        rgba [1] = palette [256 + index] / 255.0f;
        rgba [2] = palette [512 + index] / 255.0f;
        rgba [3] = (index == transparentIdx) ? 0.0f : 1.0f;
        break;
      }

      case Grayscale:
      case Duotone:   // (Shown as its grayscale: its inks are not combined)
        rgba [0] = rgba [1] = rgba [2] = _Sample (0, i);
        if (alpha) rgba [3] = _Sample (1, i);
        break;

      case RGB:
        for (int c = 0; c < 3; c++) rgba [c] = _Sample (c, i);
        if (alpha) rgba [3] = _Sample (3, i);
        break;

      case CMYK:
      case Multichannel: // (Its first channels, as C, M, Y, K: missing ones as no ink)
      {
        float ink [4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        for (int c = 0; c < colorChannels; c++) ink [c] = 1.0f - _Sample (c, i);
        for (int c = 0; c < 3; c++) rgba [c] = (1.0f - ink [c]) * (1.0f - ink [3]);
        if (alpha) rgba [3] = _Sample (4, i);
        break;
      }
    }

    // Unmatted: Photoshop stores transparent pixels' colors blended with white
    if (alpha && depth != 32 && rgba [3] > 0.0f && rgba [3] < 1.0f)
      for (int c = 0; c < 3; c++)
        rgba [c] = std::clamp ((rgba [c] - (1.0f - rgba [3])) / rgba [3], 0.0f, 1.0f);

    _Put (i, rgba);
  }

  // Its colors: 32 bpc is linear light (see below); the others are of its ICC profile (RGB ones, converted by the decoder after,
  //   as other images' are), or sRGB
  if (icc != nullptr && (mode == RGB || mode == Grayscale || mode == Indexed || mode == Duotone))
    _SetICC (image.color_source, icc, iccSize);

  if (mode == CMYK || mode == Multichannel)
    image.color_source.format_defined = (mode == CMYK) ? "CMYK, converted without its profile" : "Multichannel, shown as CMYK";

  // 32 bpc: HDR if brighter than SDR's white anywhere (above 1.0); otherwise SDR, of linear values, drawn as they are
  if (depth == 32)
  {
    bool brighter = false;

    for (size_t y = 0; y < height && ! brighter; y++)
    {
      const float* row = reinterpret_cast <const float*> (out->pixels + y * out->rowPitch);

      for (size_t x = 0; x < width * 4 && ! brighter; x++)
        brighter = ((x % 4) != 3 && row [x] > 1.0f);
    }

    image.is_hdr           = brighter;
    image.light_info.isHDR = brighter;
    image.color_source.format_defined = (brighter) ? "Linear, HDR (32-bit Photoshop document)" : "Linear (32-bit Photoshop document)";
  }

  return true;
}

#pragma endregion
