// Color sources: how an image file describes its colors (an ICC profile, cICP, the PNG sRGB, gAMA and cHRM chunks, Exif),
//   read from the file itself, so it is known whichever decoder decodes the pixels. Shown in the viewer's details panel.
//     The first step of color management: nothing uses it to convert colors yet.
//
//   Included by viewer.cpp after animated_images.inl (its _ReadBE16 and _ReadBE32), stb_image (stbi_zlib_decode_malloc,
//     for iCCP) and libwebp's demux.h. Also tested on its own by tools\image_tests. No Windows or DirectX types are used here.

#pragma once

struct color_source_s {
  const char*           origin           = nullptr; // What it was read from: "PNG", "JPEG", "WebP", "AVIF", "JPEG XL", "WIC"
  const char*           format_defined   = nullptr; // Formats whose colors are fixed by the format itself (OpenEXR, ...)

  std::vector <uint8_t> icc;                        // ICC profile (decompressed)
  std::string           icc_name;                   // Its description ('desc' tag)
  bool                  icc_has_cicp     = false;   // Its cicp tag (ICC 4.4), as HDR profiles have
  uint8_t               icc_primaries    = 0,
                        icc_transfer     = 0;

  bool                  has_cicp         = false;   // cICP (PNG), nclx (AVIF), or an encoded color space (JPEG XL)
  bool                  cicp_first       = false;   // cICP overrides the ICC profile (PNG: as the PNG specification says)
  uint8_t               primaries        = 0,
                        transfer         = 0,
                        matrix           = 0,
                        full_range       = 0;

  bool                  has_srgb         = false;   // PNG sRGB chunk
  uint8_t               rendering_intent = 0;

  uint32_t              gamma            = 0;       // PNG gAMA (the encoding gamma, times 100000; 45455 = 1 / 2.2), 0 if none
  bool                  has_chrm         = false;   // PNG cHRM: white point, red, green, blue (x, y each)
  float                 chrm [8]         = { };

  uint16_t              exif_color_space = 0;       // Exif ColorSpace: 0 if none, 1 sRGB, 2 Adobe RGB (WIC only), 65535 uncalibrated
  char                  exif_interop [4] = { };     // Exif InteroperabilityIndex: "R98" (sRGB), "R03" (Adobe RGB), "THM"

  bool empty (void) const
  {
    return icc.empty ( ) && ! has_cicp && ! has_srgb && gamma == 0 && ! has_chrm && exif_color_space == 0 && format_defined == nullptr;
  }
};

#pragma region ICC

// The description of an ICC profile ('desc' tag: textDescriptionType in v2 profiles, multiLocalizedUnicodeType in v4)
static std::string
_ReadICCName (const uint8_t* icc, size_t size)
{
  if (size < 132)
    return { };

  const uint32_t tags = _ReadBE32 (icc + 128);

  for (uint32_t i = 0; i < tags && 132 + (i + 1) * 12ull <= size; i++)
  {
    const uint8_t* entry  = icc + 132 + i * 12;
    const uint32_t offset = _ReadBE32 (entry + 4),
                   length = _ReadBE32 (entry + 8);

    if (memcmp (entry, "desc", 4) != 0 || offset > size || length > size - offset || length < 12)
      continue;

    const uint8_t* tag = icc + offset;
    std::string    name;

    // textDescriptionType: an ASCII string (with its terminator)
    if (memcmp (tag, "desc", 4) == 0)
    {
      const uint32_t count = _ReadBE32 (tag + 8);

      for (uint32_t c = 0; c < count && 12ull + c < length && tag [12 + c] != 0; c++)
        name += static_cast <char> (tag [12 + c]);
    }

    // multiLocalizedUnicodeType: UTF-16BE strings, one per language; the first is used
    else if (memcmp (tag, "mluc", 4) == 0 && length >= 28 && _ReadBE32 (tag + 8) > 0)
    {
      const uint32_t bytes = _ReadBE32 (tag + 20),
                     at    = _ReadBE32 (tag + 24);

      if (at <= length && bytes <= length - at)
      {
        for (uint32_t c = 0; c + 1 < bytes; c += 2)
        {
          uint32_t ch = _ReadBE16 (tag + at + c);

          if (ch == 0)
            break;

          // UTF-8 (characters outside the BMP are left out)
          if      (ch < 0x80)                  name +=  static_cast <char> (ch);
          else if (ch < 0x800)               { name +=  static_cast <char> (0xC0 | (ch >> 6));
                                               name +=  static_cast <char> (0x80 | (ch & 0x3F)); }
          else if (ch < 0xD800 || ch > 0xDFFF) { name += static_cast <char> (0xE0 | (ch >> 12));
                                               name +=  static_cast <char> (0x80 | ((ch >> 6) & 0x3F));
                                               name +=  static_cast <char> (0x80 | (ch & 0x3F)); }
        }
      }
    }

    while (! name.empty ( ) && (name.back ( ) == ' ' || name.back ( ) == '\0'))
      name.pop_back ( );

    return name;
  }

  return { };
}

// Whether an ICC profile's tone curve is linear: its red TRC is the identity (a 'curv' of no points, or of gamma 1.0;
//   a 'para' of type 0 with gamma 1.0). Profiles of other curves (and of no rTRC, such as LUT based ones) are not.
static bool
_IsLinearICC (const uint8_t* icc, size_t size)
{
  if (size < 132)
    return false;

  const uint32_t tags = _ReadBE32 (icc + 128);

  for (uint32_t i = 0; i < tags && 132 + (i + 1) * 12ull <= size; i++)
  {
    const uint8_t* entry  = icc + 132 + i * 12;
    const uint32_t offset = _ReadBE32 (entry + 4),
                   length = _ReadBE32 (entry + 8);

    if (memcmp (entry, "rTRC", 4) != 0 || offset > size || length > size - offset || length < 12)
      continue;

    const uint8_t* tag = icc + offset;

    if (memcmp (tag, "curv", 4) == 0)
    {
      const uint32_t count = _ReadBE32 (tag + 8);
      return (count == 0) || (count == 1 && length >= 14 && _ReadBE16 (tag + 12) == 0x0100);
    }

    if (memcmp (tag, "para", 4) == 0 && length >= 16)
      return _ReadBE16 (tag + 8) == 0 && _ReadBE32 (tag + 12) == 0x00010000;

    return false;
  }

  return false;
}

static void
_SetICC (color_source_s& out, const uint8_t* icc, size_t size)
{
  // Not a profile (the header alone is 128 bytes, and its size comes first)
  if (icc == nullptr || size < 132 || _ReadBE32 (icc) > size || memcmp (icc + 36, "acsp", 4) != 0)
    return;

  out.icc.assign (icc, icc + size);
  out.icc_name = _ReadICCName (icc, size);

  // Its cicp tag (ICC 4.4): the signature, 4 reserved bytes, then primaries, transfer, matrix and range
  const uint32_t tags = _ReadBE32 (icc + 128);

  for (uint32_t i = 0; i < tags && 132 + (i + 1) * 12ull <= size; i++)
  {
    const uint8_t* entry  = icc + 132 + i * 12;
    const uint32_t offset = _ReadBE32 (entry + 4),
                   length = _ReadBE32 (entry + 8);

    if (memcmp (entry, "cicp", 4) == 0 && offset <= size && length <= size - offset && length >= 12 &&
        memcmp (icc + offset, "cicp", 4) == 0)
    {
      out.icc_has_cicp  = true;
      out.icc_primaries = icc [offset + 8];
      out.icc_transfer  = icc [offset + 9];
      break;
    }
  }
}

#pragma endregion

#pragma region Exif

// The ColorSpace and InteroperabilityIndex of an Exif block (a TIFF structure: the APP1 segment of a JPEG after
//   "Exif\0\0", or the EXIF chunk of a WebP)
static void
_ReadExifColor (const uint8_t* tiff, size_t size, color_source_s& out)
{
  // Some WebP writers keep the JPEG prefix
  if (size >= 6 && memcmp (tiff, "Exif\0\0", 6) == 0)
    tiff += 6, size -= 6;

  if (size < 8 || size > UINT32_MAX)
    return;

  const bool little = (tiff [0] == 'I' && tiff [1] == 'I');

  if (! little && ! (tiff [0] == 'M' && tiff [1] == 'M'))
    return;

  auto read16 = [&] (size_t at) -> uint32_t { return little ? (tiff [at] | (tiff [at + 1] << 8)) : _ReadBE16 (tiff + at); };
  auto read32 = [&] (size_t at) -> uint32_t { return little ? (tiff [at] | (tiff [at + 1] << 8) | (tiff [at + 2] << 16) | (static_cast <uint32_t> (tiff [at + 3]) << 24))
                                                            : _ReadBE32 (tiff + at); };

  if (read16 (2) != 42)
    return;

  // Calls found (tag, offset of its entry) for each entry of the IFD at offset
  auto walk = [&] (uint32_t offset, const std::function <void (uint32_t, size_t)>& found)
  {
    if (offset < 8 || offset > size - 2)
      return;

    const uint32_t count = read16 (offset);

    for (uint32_t i = 0; i < count && offset + 2 + (i + 1) * 12ull <= size; i++)
      found (read16 (offset + 2 + i * 12), offset + 2 + i * 12);
  };

  uint32_t exifIFD = 0, interopIFD = 0;

  walk (read32 (4), [&] (uint32_t tag, size_t entry) { if (tag == 0x8769) exifIFD = read32 (entry + 8); });

  walk (exifIFD, [&] (uint32_t tag, size_t entry)
  {
    if (tag == 0xA001) out.exif_color_space = static_cast <uint16_t> (read16 (entry + 8)); // SHORT, in the entry itself
    if (tag == 0xA005) interopIFD           = read32 (entry + 8);
  });

  walk (interopIFD, [&] (uint32_t tag, size_t entry)
  {
    // ASCII, 4 bytes ("R03\0"): in the entry itself
    if (tag == 0x0001 && read32 (entry + 4) <= 4)
      for (int c = 0; c < 3; c++)
        out.exif_interop [c] = (tiff [entry + 8 + c] >= 0x20 && tiff [entry + 8 + c] < 0x7F) ? static_cast <char> (tiff [entry + 8 + c]) : '\0';
  });
}

#pragma endregion

#pragma region Files

// PNG: iCCP, cICP, sRGB, gAMA and cHRM, which all come before the image data
static void
_ReadColorSourcePNG (const uint8_t* data, size_t size, color_source_s& out)
{
  static constexpr uint8_t signature [8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };

  if (size < 8 || memcmp (data, signature, 8) != 0)
    return;

  out.origin = "PNG";

  for (size_t pos = 8; pos + 12 <= size; )
  {
    const uint32_t length = _ReadBE32 (data + pos);
    const uint8_t* type   = data + pos + 4;
    const uint8_t* body   = data + pos + 8;

    if (length > size - pos - 12)
      break;

    if (memcmp (type, "IDAT", 4) == 0 || memcmp (type, "IEND", 4) == 0)
      break;

    if (memcmp (type, "iCCP", 4) == 0 && out.icc.empty ( ))
    {
      // The profile's name (1-79 bytes, then a zero), the compression method (0: zlib), and the zlib stream
      const uint8_t* end = static_cast <const uint8_t*> (memchr (body, 0, std::min <size_t> (length, 80)));

      if (end != nullptr && end + 2 <= body + length && end [1] == 0)
      {
        int   inflated = 0;
        char* icc      = stbi_zlib_decode_malloc (reinterpret_cast <const char*> (end + 2), static_cast <int> (body + length - (end + 2)), &inflated);

        if (icc != nullptr && inflated > 0)
          _SetICC (out, reinterpret_cast <const uint8_t*> (icc), static_cast <size_t> (inflated));

        STBI_FREE (icc);
      }
    }

    else if (memcmp (type, "cICP", 4) == 0 && length == 4)
    {
      out.has_cicp   = true;
      out.cicp_first = true;
      out.primaries  = body [0];
      out.transfer   = body [1];
      out.matrix     = body [2];
      out.full_range = body [3];
    }

    else if (memcmp (type, "sRGB", 4) == 0 && length == 1)
    {
      out.has_srgb         = true;
      out.rendering_intent = body [0];
    }

    else if (memcmp (type, "gAMA", 4) == 0 && length == 4)
      out.gamma = _ReadBE32 (body);

    else if (memcmp (type, "cHRM", 4) == 0 && length == 32)
    {
      out.has_chrm = true;

      for (int i = 0; i < 8; i++)
        out.chrm [i] = static_cast <float> (_ReadBE32 (body + i * 4)) / 100000.0f;
    }

    pos += 12ull + length;
  }
}

// JPEG: the ICC profile (APP2 "ICC_PROFILE", split across segments of up to 64 KB) and Exif (APP1), which both come before
//   the image data
static void
_ReadColorSourceJPEG (const uint8_t* data, size_t size, color_source_s& out)
{
  if (size < 4 || data [0] != 0xFF || data [1] != 0xD8)
    return;

  out.origin = "JPEG";

  std::vector <std::vector <uint8_t>> chunks; // ICC_PROFILE segments, by their sequence number (from 1)
  size_t                              count = 0;

  for (size_t pos = 2; pos + 4 <= size; )
  {
    if (data [pos] != 0xFF)
      break;

    const uint8_t marker = data [pos + 1];

    // Fill bytes, and markers without a length
    if (marker == 0xFF)                                                       { pos += 1; continue; }
    if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7))                 { pos += 2; continue; }

    // The image data (start of scan), or the end of the image
    if (marker == 0xDA || marker == 0xD9)
      break;

    const size_t   length = _ReadBE16 (data + pos + 2);
    const uint8_t* body   = data + pos + 4;

    if (length < 2 || length > size - pos - 2)
      break;

    const size_t   bytes  = length - 2;

    if (marker == 0xE2 && bytes > 14 && memcmp (body, "ICC_PROFILE\0", 12) == 0)
    {
      const size_t seq   = body [12],
                   total = body [13];

      if (seq >= 1 && seq <= total && (count == 0 || count == total))
      {
        count = total;
        chunks.resize (total);
        chunks [seq - 1].assign (body + 14, body + bytes);
      }
    }

    else if (marker == 0xE1 && bytes > 6 && memcmp (body, "Exif\0\0", 6) == 0)
      _ReadExifColor (body + 6, bytes - 6, out);

    pos += 2 + length;
  }

  // The profile, if all its segments are there
  if (count > 0 && std::all_of (chunks.begin ( ), chunks.end ( ), [] (const auto& c) { return ! c.empty ( ); }))
  {
    std::vector <uint8_t> icc;

    for (const auto& c : chunks)
      icc.insert (icc.end ( ), c.begin ( ), c.end ( ));

    _SetICC (out, icc.data ( ), icc.size ( ));
  }
}

// WebP: the ICCP and EXIF chunks (extended WebPs, VP8X). data may be the start of the file only: the EXIF chunk, which
//   comes after the image data, is then missed.
static void
_ReadColorSourceWebP (const uint8_t* data, size_t size, color_source_s& out)
{
  if (! _IsWebP (data, size))
    return;

  out.origin = "WebP";

  WebPData      webp  = { data, size };
  WebPDemuxer*  demux = WebPDemuxPartial (&webp, nullptr);

  if (demux == nullptr)
    return;

  const uint32_t   flags = WebPDemuxGetI (demux, WEBP_FF_FORMAT_FLAGS);
  WebPChunkIterator chunk;

  if ((flags & ICCP_FLAG) && WebPDemuxGetChunk (demux, "ICCP", 1, &chunk))
  {
    _SetICC (out, chunk.chunk.bytes, chunk.chunk.size);
    WebPDemuxReleaseChunkIterator (&chunk);
  }

  if ((flags & EXIF_FLAG) && WebPDemuxGetChunk (demux, "EXIF", 1, &chunk))
  {
    _ReadExifColor (chunk.chunk.bytes, chunk.chunk.size, out);
    WebPDemuxReleaseChunkIterator (&chunk);
  }

  WebPDemuxDelete (demux);
}

#pragma endregion

#pragma region Decide

// Whether the file says its colors are linear (not sRGB), by the same precedence as _DescribeColorSource: cICP of the
//   linear transfer, an ICC profile of a linear tone curve, or (without either, or an sRGB chunk) a PNG gAMA of 1.0.
//     Everything else is taken as sRGB, as browsers do; other gammas and primaries are not told apart yet.
static bool
_IsLinearColorSource (const color_source_s& cs)
{
  if (cs.has_cicp && (cs.cicp_first || cs.icc.empty ( )))
    return cs.transfer == 8;

  if (! cs.icc.empty ( ))
    return (cs.icc_has_cicp) ? cs.icc_transfer == 8 : _IsLinearICC (cs.icc.data ( ), cs.icc.size ( ));

  if (cs.has_srgb)
    return false;

  return cs.gamma == 100000;
}

// Whether the file says it is HDR10 (Rec. 2020 primaries, PQ), by the same precedence: its cICP, or its ICC profile's
//   cicp tag (as SKIV and Special K write HDR PNGs), or a profile without one but named for PQ ("...PQ...", "...PeQ",
//     "...2084..."), as profiles of HDR PNGs made before ICC 4.4 are.
static bool
_IsHDR10ColorSource (const color_source_s& cs)
{
  if (cs.has_cicp && (cs.cicp_first || cs.icc.empty ( )))
    return cs.primaries == 9 && cs.transfer == 16;

  if (cs.icc.empty ( ))
    return false;

  if (cs.icc_has_cicp)
    return cs.icc_primaries == 9 && cs.icc_transfer == 16;

  // The name's words (split at anything but letters and digits)
  std::string word;

  for (size_t i = 0; i <= cs.icc_name.size ( ); i++)
  {
    const char c = (i < cs.icc_name.size ( )) ? cs.icc_name [i] : ' ';

    if (isalnum (static_cast <unsigned char> (c)))
      word += c;

    else
    {
      if (word == "PQ" || word == "PeQ" || word.find ("2084") != std::string::npos)
        return true;

      word.clear ( );
    }
  }

  return false;
}

// Whether a JPEG XL image (decoded into scRGB) is HDR, or SDR of colors outside of sRGB (wide gamut): HDR if it says so (a
//   PQ or HLG transfer, by its color source; or an intensity target above SDR's 255 nits, libjxl's default), or if its
//     pixels go above SDR white (aboveOne: a luminance over 1.0); wide gamut if they only go outside of sRGB (belowZero:
//       a channel below 0)
struct dynamic_range_s { bool hdr = false, wcg = false; };

static dynamic_range_s
_JXLDynamicRange (const color_source_s& cs, float intensity_target, bool belowZero, bool aboveOne)
{
  const bool declared =
    (cs.has_cicp     && (cs.transfer     == 16 || cs.transfer     == 18)) ||
    (cs.icc_has_cicp && (cs.icc_transfer == 16 || cs.icc_transfer == 18)) ||
    (intensity_target > 255.0f);

  dynamic_range_s range;
  range.hdr = declared || aboveOne;
  range.wcg = belowZero && ! range.hdr;
  return range;
}

#pragma endregion

#pragma region Describe

static const char*
_CICPPrimariesName (uint8_t primaries)
{
  switch (primaries)
  {
    case  1: return "Rec. 709";
    case  2: return "Unspecified";
    case  4: return "Rec. 470 M";
    case  5: return "Rec. 601 (625)";
    case  6: return "Rec. 601 (525)";
    case  7: return "SMPTE 240M";
    case  8: return "Generic film";
    case  9: return "Rec. 2020";
    case 10: return "CIE XYZ";
    case 11: return "DCI-P3";
    case 12: return "Display P3";
    case 22: return "EBU 3213";
    default: return "Unknown";
  }
}

static const char*
_CICPTransferName (uint8_t transfer)
{
  switch (transfer)
  {
    case  1: return "Rec. 709";
    case  2: return "Unspecified";
    case  4: return "Gamma 2.2";
    case  5: return "Gamma 2.8";
    case  6: return "Rec. 601";
    case  7: return "SMPTE 240M";
    case  8: return "Linear";
    case 13: return "sRGB";
    case 14:
    case 15: return "Rec. 2020";
    case 16: return "PQ";
    case 17: return "SMPTE 428";
    case 18: return "HLG";
    default: return "Unknown";
  }
}

static std::string
_ExifColorName (const color_source_s& cs)
{
  const bool r03 = (strcmp (cs.exif_interop, "R03") == 0);

  return (cs.exif_color_space ==     1) ? "sRGB (Exif)"                   :
         (cs.exif_color_space ==     2) ? "Adobe RGB (Exif)"              :
         (cs.exif_color_space == 65535) ? (r03 ? "Adobe RGB (Exif, R03)" : "Uncalibrated (Exif)") :
                                          "Unknown (Exif " + std::to_string (cs.exif_color_space) + ")";
}

// What an image's colors are said to be, in a few words: by the first of these the file has (cICP first in PNGs, as the
//   PNG specification says)
static std::string
_DescribeColorSource (const color_source_s& cs)
{
  const auto cicp = [&] { return std::string ("cICP: ") + _CICPPrimariesName (cs.primaries) + ", " + _CICPTransferName (cs.transfer); };
  const auto icc  = [&] { return "ICC: " + (cs.icc_name.empty ( ) ? std::string ("Unnamed profile") : cs.icc_name) +
                                 (cs.icc_has_cicp ? std::string (" (") + _CICPPrimariesName (cs.icc_primaries) + ", " + _CICPTransferName (cs.icc_transfer) + ")" : ""); };

  if (cs.has_cicp && (cs.cicp_first || cs.icc.empty ( )))
    return cicp ( );

  if (! cs.icc.empty ( ))
    return icc ( );

  if (cs.has_srgb)
    return "sRGB (PNG sRGB chunk)";

  if (cs.gamma != 0 || cs.has_chrm)
  {
    char text [64] = { };

    if (cs.gamma != 0)
      snprintf (text, sizeof (text), "Gamma %.2f (PNG gAMA%s)", 100000.0 / cs.gamma, cs.has_chrm ? ", cHRM" : "");
    else
      snprintf (text, sizeof (text), "Primaries only (PNG cHRM)");

    return text;
  }

  if (cs.exif_color_space != 0)
    return _ExifColorName (cs);

  if (cs.format_defined != nullptr)
    return cs.format_defined;

  return "Not specified";
}

// Everything the file has (for the details panel's tooltip, and the log), one per line
static std::string
_DescribeColorSourceInDetail (const color_source_s& cs)
{
  std::string text;
  char        line [160] = { };

  if (! cs.icc.empty ( ))
  {
    snprintf (line, sizeof (line), "ICC profile: %s (%zu bytes)\n", cs.icc_name.empty ( ) ? "unnamed" : cs.icc_name.c_str ( ), cs.icc.size ( ));
    text += line;

    if (cs.icc_has_cicp)
    {
      snprintf (line, sizeof (line), "ICC cicp tag: %s, %s\n", _CICPPrimariesName (cs.icc_primaries), _CICPTransferName (cs.icc_transfer));
      text += line;
    }
  }

  if (cs.has_cicp)
  {
    snprintf (line, sizeof (line), "cICP: %s, %s (%u/%u/%u, %s range)\n", _CICPPrimariesName (cs.primaries), _CICPTransferName (cs.transfer),
                                    cs.primaries, cs.transfer, cs.matrix, cs.full_range ? "full" : "limited");
    text += line;
  }

  if (cs.has_srgb)
  {
    snprintf (line, sizeof (line), "sRGB chunk: rendering intent %u\n", cs.rendering_intent);
    text += line;
  }

  if (cs.gamma != 0)
  {
    snprintf (line, sizeof (line), "gAMA: %u (gamma %.2f)\n", cs.gamma, 100000.0 / cs.gamma);
    text += line;
  }

  if (cs.has_chrm)
  {
    snprintf (line, sizeof (line), "cHRM: white %.4f, %.4f; red %.4f, %.4f; green %.4f, %.4f; blue %.4f, %.4f\n",
                                    cs.chrm [0], cs.chrm [1], cs.chrm [2], cs.chrm [3], cs.chrm [4], cs.chrm [5], cs.chrm [6], cs.chrm [7]);
    text += line;
  }

  if (cs.exif_color_space != 0 || cs.exif_interop [0] != '\0')
  {
    snprintf (line, sizeof (line), "Exif: ColorSpace %u, InteroperabilityIndex %s\n", cs.exif_color_space,
                                    cs.exif_interop [0] != '\0' ? cs.exif_interop : "none");
    text += line;
  }

  if (cs.format_defined != nullptr)
    text += std::string (cs.format_defined) + "\n";

  if (text.empty ( ))
    text = "The file says nothing about its colors\n";

  if (cs.origin != nullptr)
    text += std::string ("Read from: ") + cs.origin + "\n";

  text.pop_back ( );
  return text;
}

#pragma endregion
