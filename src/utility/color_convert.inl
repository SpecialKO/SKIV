// Color conversion: images whose colors are not sRGB (wide gamut photos: Display P3, Adobe RGB, Rec. 2020; ICC profiles of
//   other spaces) are converted with Little CMS (packages_misc\lcms2) into scRGB: linear, Rec. 709 primaries, D65, unclamped,
//     as half floats. Colors outside of sRGB keep their negative (or over 1) values, and are seen as they are on wide gamut
//       and HDR displays. sRGB images (most of them) are left as they are: _CreateSourceProfile gives no profile for them.
//
//   Included by viewer.cpp after color_source.inl, and lcms2.h (with CMS_NO_REGISTER_KEYWORD). Also tested on its own by
//     tools\image_tests. No Windows or DirectX types are used here.

#pragma once

#pragma region Profiles

// The colors of sRGB (and of the cICP code of each set of primaries): their white point, red, green and blue (x, y)
struct color_primaries_s { double wx, wy, rx, ry, gx, gy, bx, by; };

static constexpr color_primaries_s _sRGBPrimaries  = { 0.3127, 0.3290,  0.640, 0.330,  0.300, 0.600,  0.150, 0.060 };
static constexpr color_primaries_s _AdobePrimaries = { 0.3127, 0.3290,  0.640, 0.330,  0.210, 0.710,  0.150, 0.060 };

static bool
_CICPPrimaries (uint8_t code, color_primaries_s& out)
{
  switch (code)
  {
    case  1: out = _sRGBPrimaries;                                                                  return true;
    case  4: out = { 0.3100, 0.3160,  0.670, 0.330,  0.210, 0.710,  0.140, 0.080 };                 return true; // Rec. 470 M
    case  5: out = { 0.3127, 0.3290,  0.640, 0.330,  0.290, 0.600,  0.150, 0.060 };                 return true; // Rec. 601 (625)
    case  6:
    case  7: out = { 0.3127, 0.3290,  0.630, 0.340,  0.310, 0.595,  0.155, 0.070 };                 return true; // Rec. 601 (525)
    case  8: out = { 0.3100, 0.3160,  0.681, 0.319,  0.243, 0.692,  0.145, 0.049 };                 return true; // Generic film
    case  9: out = { 0.3127, 0.3290,  0.708, 0.292,  0.170, 0.797,  0.131, 0.046 };                 return true; // Rec. 2020
    case 11: out = { 0.3140, 0.3510,  0.680, 0.320,  0.265, 0.690,  0.150, 0.060 };                 return true; // DCI-P3
    case 12: out = { 0.3127, 0.3290,  0.680, 0.320,  0.265, 0.690,  0.150, 0.060 };                 return true; // Display P3
    case 22: out = { 0.3127, 0.3290,  0.630, 0.340,  0.295, 0.605,  0.155, 0.077 };                 return true; // EBU 3213
    default:                                                                                        return false;
  }
}

static cmsToneCurve*
_SRGBCurve (void)
{
  static constexpr cmsFloat64Number params [5] = { 2.4, 1.0 / 1.055, 0.055 / 1.055, 1.0 / 12.92, 0.04045 };
  return cmsBuildParametricToneCurve (nullptr, 4, params);
}

// The cICP transfer characteristics SDR images use, as the curve from their values to linear light (Rec. 709 and its
//   siblings: the inverse of their camera curve). nullptr for PQ and HLG (HDR, see _IsHDR10ColorSource) and unknown ones.
static cmsToneCurve*
_CICPTransferCurve (uint8_t code)
{
  static constexpr cmsFloat64Number bt709 [5] = { 1.0 / 0.45, 1.0 / 1.099, 0.099 / 1.099, 1.0 / 4.5, 0.081 };

  switch (code)
  {
    case  1:
    case  6:
    case  7:
    case 14:
    case 15: return cmsBuildParametricToneCurve (nullptr, 4, bt709);
    case  2:
    case 13: return _SRGBCurve ( );
    case  4: return cmsBuildGamma (nullptr, 2.2);
    case  5: return cmsBuildGamma (nullptr, 2.8);
    case  8: return cmsBuildGamma (nullptr, 1.0);
    case 17: return cmsBuildGamma (nullptr, 2.6);
    default: return nullptr;
  }
}

// An RGB profile of the given primaries and curve (which it frees)
static cmsHPROFILE
_CreateRGBProfile (const color_primaries_s& p, cmsToneCurve* curve)
{
  if (curve == nullptr)
    return nullptr;

  const cmsCIExyY       white      = { p.wx, p.wy, 1.0 };
  const cmsCIExyYTRIPLE primaries  = { { p.rx, p.ry, 1.0 }, { p.gx, p.gy, 1.0 }, { p.bx, p.by, 1.0 } };
  cmsToneCurve*         curves [3] = { curve, curve, curve };

  cmsHPROFILE profile = cmsCreateRGBProfile (&white, &primaries, curves);
  cmsFreeToneCurve (curve);
  return profile;
}

// scRGB: what images are converted into
static cmsHPROFILE
_CreateScRGBProfile (void)
{
  return _CreateRGBProfile (_sRGBPrimaries, cmsBuildGamma (nullptr, 1.0));
}

// Whether a profile is sRGB, or close enough to be drawn as such: a matrix/shaper profile of sRGB's colorants (as adapted
//   to D50) and tone curves
static bool
_IsSRGBProfile (cmsHPROFILE profile)
{
  if (! cmsIsMatrixShaper (profile))
    return false;

  static constexpr double colorants [3][3] = { { 0.4360747, 0.2225045, 0.0139322 },
                                               { 0.3850649, 0.7168786, 0.0971045 },
                                               { 0.1430804, 0.0606169, 0.7141733 } };
  static constexpr cmsTagSignature colorantTags [3] = { cmsSigRedColorantTag, cmsSigGreenColorantTag, cmsSigBlueColorantTag },
                                   curveTags    [3] = { cmsSigRedTRCTag,      cmsSigGreenTRCTag,      cmsSigBlueTRCTag      };

  cmsToneCurve* srgb = _SRGBCurve ( );
  bool          same = (srgb != nullptr);

  for (int c = 0; c < 3 && same; c++)
  {
    const auto* xyz   = static_cast <const cmsCIEXYZ*>    (cmsReadTag (profile, colorantTags [c]));
    const auto* curve = static_cast <const cmsToneCurve*> (cmsReadTag (profile, curveTags    [c]));

    same = (xyz != nullptr && curve != nullptr &&
            std::abs (xyz->X - colorants [c][0]) < 0.003 &&
            std::abs (xyz->Y - colorants [c][1]) < 0.003 &&
            std::abs (xyz->Z - colorants [c][2]) < 0.003);

    for (int i = 1; i <= 16 && same; i++)
    {
      const float x = static_cast <float> (i) / 16.0f;
      same = std::abs (cmsEvalToneCurveFloat (curve, x) - cmsEvalToneCurveFloat (srgb, x)) < 0.005f;
    }
  }

  if (srgb != nullptr)
    cmsFreeToneCurve (srgb);

  return same;
}

// A profile of cICP codes: nullptr if they are sRGB (Rec. 709 primaries, or unspecified ones, with an SDR curve), linear
//   Rec. 709 (drawn as it is), HDR, or unknown
static cmsHPROFILE
_CreateCICPProfile (uint8_t primaries, uint8_t transfer)
{
  // Rec. 709 primaries (or unspecified ones): only plain gamma curves are converted
  if (primaries == 1 || primaries == 2)
    return (transfer == 4 || transfer == 5 || transfer == 17) ? _CreateRGBProfile (_sRGBPrimaries, _CICPTransferCurve (transfer))
                                                              : nullptr;

  color_primaries_s p;
  return _CICPPrimaries (primaries, p) ? _CreateRGBProfile (p, _CICPTransferCurve (transfer)) : nullptr;
}

// A profile of the colors the file says it has, by the same precedence as _DescribeColorSource (cICP first in PNGs, then
//   the ICC profile, the PNG sRGB chunk, gAMA and cHRM, and Exif). nullptr if they are sRGB (or linear Rec. 709): images
//     drawn as they are decoded (see _IsLinearColorSource), as well as when the file says nothing (taken as sRGB), or
//       something this cannot convert from (HDR, gray or CMYK profiles, broken ones).
static cmsHPROFILE
_CreateSourceProfile (const color_source_s& cs)
{
  if (cs.has_cicp && (cs.cicp_first || cs.icc.empty ( )))
    return _CreateCICPProfile (cs.primaries, cs.transfer);

  if (! cs.icc.empty ( ))
  {
    if (cs.icc_has_cicp)
      return _CreateCICPProfile (cs.icc_primaries, cs.icc_transfer);

    cmsHPROFILE profile = cmsOpenProfileFromMem (cs.icc.data ( ), static_cast <cmsUInt32Number> (cs.icc.size ( )));

    if (profile == nullptr)
      return nullptr;

    const cmsProfileClassSignature kind = cmsGetDeviceClass (profile);

    if (cmsGetColorSpace (profile) != cmsSigRgbData || kind == cmsSigLinkClass || kind == cmsSigAbstractClass ||
        kind == cmsSigNamedColorClass || _IsSRGBProfile (profile))
    {
      cmsCloseProfile (profile);
      return nullptr;
    }

    return profile;
  }

  if (cs.has_srgb)
    return nullptr;

  if (cs.gamma != 0 || cs.has_chrm)
  {
    color_primaries_s p = _sRGBPrimaries;

    if (cs.has_chrm)
      p = { cs.chrm [0], cs.chrm [1], cs.chrm [2], cs.chrm [3], cs.chrm [4], cs.chrm [5], cs.chrm [6], cs.chrm [7] };

    const bool srgbPrimaries = (! cs.has_chrm) ||
      (std::abs (p.wx - _sRGBPrimaries.wx) < 0.01 && std::abs (p.wy - _sRGBPrimaries.wy) < 0.01 &&
       std::abs (p.rx - _sRGBPrimaries.rx) < 0.01 && std::abs (p.ry - _sRGBPrimaries.ry) < 0.01 &&
       std::abs (p.gx - _sRGBPrimaries.gx) < 0.01 && std::abs (p.gy - _sRGBPrimaries.gy) < 0.01 &&
       std::abs (p.bx - _sRGBPrimaries.bx) < 0.01 && std::abs (p.by - _sRGBPrimaries.by) < 0.01);

    // gAMA of 1/2.2 (or none), or 1.0: as sRGB, or linear Rec. 709
    const bool srgbGamma = (cs.gamma == 0 || (cs.gamma >= 44000 && cs.gamma <= 46000) || cs.gamma == 100000);

    if (srgbPrimaries && srgbGamma)
      return nullptr;

    return _CreateRGBProfile (p, (cs.gamma != 0) ? cmsBuildGamma (nullptr, 100000.0 / cs.gamma) : _SRGBCurve ( ));
  }

  // Adobe RGB (1998): Exif ColorSpace 2 (as WIC reports it), or uncalibrated with the interoperability index R03
  if (cs.exif_color_space == 2 || (cs.exif_color_space == 65535 && strcmp (cs.exif_interop, "R03") == 0))
    return _CreateRGBProfile (_AdobePrimaries, cmsBuildGamma (nullptr, 563.0 / 256.0));

  return nullptr;
}

// A profile of the colors of SDR images that decoders hand over as they are encoded, of more than 8 bpc (JPEG XL, as
//   libjxl does for all but XYB encoded images; AVIF of 10 or 12 bpc): those of their color source, sRGB included (which
//     _CreateSourceProfile leaves out, as 8-bpc sRGB images are drawn as they are). nullptr if they are linear Rec. 709
//       already, HDR (PQ: converted by the viewer itself), or unknown.
static cmsHPROFILE
_CreateSDRSourceProfile (const color_source_s& cs)
{
  // A plain gamma (JPEG XL's own transfer function; cICP has none)
  if (cs.has_cicp && cs.transfer == 2 && cs.gamma != 0)
  {
    color_primaries_s p = _sRGBPrimaries;
    _CICPPrimaries (cs.primaries, p);
    return _CreateRGBProfile (p, cmsBuildGamma (nullptr, 100000.0 / cs.gamma));
  }

  if (cmsHPROFILE profile = _CreateSourceProfile (cs))
    return profile;

  // What is left: Rec. 709 primaries (or ones that are not converted), of their curve
  const uint8_t transfer = (cs.has_cicp && (cs.cicp_first || cs.icc.empty ( ))) ? cs.transfer
                         : (cs.icc_has_cicp)                                    ? cs.icc_transfer
                                                                                : 13;

  return (transfer == 8) ? nullptr : _CreateRGBProfile (_sRGBPrimaries, _CICPTransferCurve (transfer));
}

// Whether an image of this color source is converted (to scRGB, by _ConvertToScRGB)
static bool
_NeedsColorConversion (const color_source_s& cs)
{
  cmsHPROFILE profile = _CreateSourceProfile (cs);

  if (profile != nullptr)
    cmsCloseProfile (profile);

  return (profile != nullptr);
}

#pragma endregion

#pragma region HLG

// HLG (BT.2100) to display light, as a fraction of 10,000 nits (as PQ decodes to, SKIV_Image_PQToLinear): its inverse
//   OETF (to scene light), then the OOTF of a 1,000-nit display (system gamma 1.2) on the luminance of the image's own
//     primaries (weights: their Y, e.g. 0.2627, 0.6780, 0.0593 for Rec. 2020). Its reference white (0.75) is 203 nits.
static void
_HLGToDisplay (const float signal [3], const float weights [3], float out [3])
{
  constexpr float a = 0.17883277f,
                  b = 0.28466892f, // 1 - 4a
                  c = 0.55991073f; // 0.5 - a ln (4a)

  float scene [3];

  for (int i = 0; i < 3; i++)
  {
    const float e = std::max (signal [i], 0.0f);
    scene [i] = (e <= 0.5f) ? (e * e / 3.0f) : ((std::exp ((e - c) / a) + b) / 12.0f);
  }

  const float y    = weights [0] * scene [0] + weights [1] * scene [1] + weights [2] * scene [2];
  const float gain = (y > 0.0f) ? 0.1f * std::pow (y, 0.2f) : 0.0f; // 1,000 nits * Y^(1.2 - 1), over 10,000 nits

  for (int i = 0; i < 3; i++)
    out [i] = gain * scene [i];
}

#pragma endregion

#pragma region Convert

// Calls rows (first, last) for all of height's rows, spread over the CPU's threads (64 rows each at least)
static void
_ForRows (uint32_t height, const std::function <void (uint32_t, uint32_t)>& rows)
{
  const uint32_t threads = std::clamp (std::thread::hardware_concurrency ( ), 1u, std::max (1u, height / 64));
  std::vector <std::thread> workers;

  for (uint32_t t = 1; t < threads; t++)
    workers.emplace_back (rows, height * t / threads, height * (t + 1) / threads);

  rows (0, height / threads);

  for (auto& worker : workers)
    worker.join ( );
}

// A float as a half float, rounded to the nearest (even); out of its range: infinity (NaN stays NaN).
//   Fabian Giesen's float_to_half_fast3_rtne (public domain).
static uint16_t
_FloatToHalf (float value)
{
  uint32_t f;
  memcpy (&f, &value, 4);

  const uint32_t sign = f & 0x80000000u;
  f ^= sign;

  uint32_t half;

  if (f >= 0x47800000u)                 // Too large for a half (65520 and up), infinity, or NaN
    half = (f > 0x7F800000u) ? 0x7E00u : 0x7C00u;

  else if (f < 0x38800000u)             // Below a half's smallest normal number: subnormal, or zero
  {
    float v;
    memcpy (&v, &f, 4);
    v += 0.5f;                          // Shifts the mantissa into place, rounding it
    memcpy (&f, &v, 4);
    half = f - 0x3F000000u;
  }

  else
  {
    const uint32_t odd = (f >> 13) & 1; // Rounds to even
    f   += 0xC8000FFFu + odd;           // Rebiases the exponent (by -112), and rounds
    half = f >> 13;
  }

  return static_cast <uint16_t> (half | (sign >> 16));
}

// A matrix/shaper profile's tone curves and the matrix from its linear colors to scRGB's (relative colorimetric: from
//   its colorants, which are adapted to D50, to scRGB's): what Little CMS does with two matrix/shaper profiles.
//     False if the profile is not one (a LUT based one).
static bool
_ReadMatrixShaper (cmsHPROFILE profile, const cmsToneCurve* curves [3], double toScRGB [3][3])
{
  if (! cmsIsMatrixShaper (profile) || cmsGetColorSpace (profile) != cmsSigRgbData)
    return false;

  static constexpr cmsTagSignature colorantTags [3] = { cmsSigRedColorantTag, cmsSigGreenColorantTag, cmsSigBlueColorantTag },
                                   curveTags    [3] = { cmsSigRedTRCTag,      cmsSigGreenTRCTag,      cmsSigBlueTRCTag      };

  cmsHPROFILE scrgb = _CreateScRGBProfile ( );

  if (scrgb == nullptr)
    return false;

  // Their colorants, as the columns of matrices from their linear RGB to XYZ (D50)
  double src [3][3], dst [3][3];
  bool   ok = true;

  for (int c = 0; c < 3 && ok; c++)
  {
    const auto* s = static_cast <const cmsCIEXYZ*>    (cmsReadTag (profile, colorantTags [c]));
    const auto* d = static_cast <const cmsCIEXYZ*>    (cmsReadTag (scrgb,   colorantTags [c]));
    curves [c]    = static_cast <const cmsToneCurve*> (cmsReadTag (profile, curveTags    [c]));

    ok = (s != nullptr && d != nullptr && curves [c] != nullptr);

    if (ok)
    {
      src [0][c] = s->X; src [1][c] = s->Y; src [2][c] = s->Z;
      dst [0][c] = d->X; dst [1][c] = d->Y; dst [2][c] = d->Z;
    }
  }

  cmsCloseProfile (scrgb);

  if (! ok)
    return false;

  // toScRGB = inverse (dst) * src
  const double (&m) [3][3] = dst;
  const double det = m [0][0] * (m [1][1] * m [2][2] - m [1][2] * m [2][1]) -
                     m [0][1] * (m [1][0] * m [2][2] - m [1][2] * m [2][0]) +
                     m [0][2] * (m [1][0] * m [2][1] - m [1][1] * m [2][0]);

  if (std::abs (det) < 1e-12)
    return false;

  const double inv [3][3] = {
    { (m [1][1] * m [2][2] - m [1][2] * m [2][1]) / det, (m [0][2] * m [2][1] - m [0][1] * m [2][2]) / det, (m [0][1] * m [1][2] - m [0][2] * m [1][1]) / det },
    { (m [1][2] * m [2][0] - m [1][0] * m [2][2]) / det, (m [0][0] * m [2][2] - m [0][2] * m [2][0]) / det, (m [0][2] * m [1][0] - m [0][0] * m [1][2]) / det },
    { (m [1][0] * m [2][1] - m [1][1] * m [2][0]) / det, (m [0][1] * m [2][0] - m [0][0] * m [2][1]) / det, (m [0][0] * m [1][1] - m [0][1] * m [1][0]) / det } };

  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      toScRGB [i][j] = inv [i][0] * src [0][j] + inv [i][1] * src [1][j] + inv [i][2] * src [2][j];

  return true;
}

// The fast way to convert 8 or 16-bpc pixels of a matrix/shaper profile (most of them: cICP, PNG gAMA and cHRM, Adobe
//   RGB, and most ICC profiles) into scRGB half floats: their tone curves as tables, and a matrix. About ten times as
//     fast as Little CMS's float pipeline; the same results, but for float rounding. False if it does not apply.
static bool
_ConvertMatrixShaperToScRGB (cmsHPROFILE source, cmsUInt32Number inFormat, const uint8_t* in, size_t inStride,
                                                                               uint8_t* out, size_t outStride, uint32_t width, uint32_t height)
{
  const bool sixteen = (inFormat == TYPE_RGBA_16),
             bgra    = (inFormat == TYPE_BGRA_8);

  if (! (sixteen || bgra || inFormat == TYPE_RGBA_8))
    return false;

  const cmsToneCurve* curves [3];
  double              matrix [3][3];

  if (! _ReadMatrixShaper (source, curves, matrix))
    return false;

  // Each channel's values, linear
  const uint32_t      levels = sixteen ? 65536 : 256;
  std::vector <float> table (static_cast <size_t> (levels) * 3);

  for (uint32_t c = 0; c < 3; c++)
    for (uint32_t i = 0; i < levels; i++)
      table [c * levels + i] = cmsEvalToneCurveFloat (curves [c], static_cast <float> (i) / static_cast <float> (levels - 1));

  const float* lr = table.data ( );
  const float* lg = lr + levels;
  const float* lb = lg + levels;
  const float  m [3][3] = { { (float) matrix [0][0], (float) matrix [0][1], (float) matrix [0][2] },
                            { (float) matrix [1][0], (float) matrix [1][1], (float) matrix [1][2] },
                            { (float) matrix [2][0], (float) matrix [2][1], (float) matrix [2][2] } };
  const float  alpha = 1.0f / static_cast <float> (levels - 1);

  _ForRows (height, [&] (uint32_t first, uint32_t last)
  {
    for (uint32_t y = first; y < last; y++)
    {
      uint16_t* dst = reinterpret_cast <uint16_t*> (out + y * outStride);

      for (uint32_t x = 0; x < width; x++, dst += 4)
      {
        uint32_t r, g, b, a;

        if (sixteen)
        {
          const uint16_t* p = reinterpret_cast <const uint16_t*> (in + y * inStride) + x * 4;
          r = p [0]; g = p [1]; b = p [2]; a = p [3];
        }

        else
        {
          const uint8_t* p = in + y * inStride + x * 4;
          r = p [bgra ? 2 : 0]; g = p [1]; b = p [bgra ? 0 : 2]; a = p [3];
        }

        const float R = lr [r], G = lg [g], B = lb [b];

        dst [0] = _FloatToHalf (m [0][0] * R + m [0][1] * G + m [0][2] * B);
        dst [1] = _FloatToHalf (m [1][0] * R + m [1][1] * G + m [1][2] * B);
        dst [2] = _FloatToHalf (m [2][0] * R + m [2][1] * G + m [2][2] * B);
        dst [3] = _FloatToHalf (static_cast <float> (a) * alpha);
      }
    }
  });

  return true;
}

// Converts the rows of an image (inFormat: TYPE_RGBA_8, TYPE_BGRA_8, TYPE_RGBA_16 or TYPE_RGBA_FLT of lcms2.h) from the
//   source profile into scRGB, as RGBA half floats (TYPE_RGBA_HALF_FLT) or floats (TYPE_RGBA_FLT: in and out may then be
//     the same); alpha is copied. Matrix/shaper profiles into half floats take the fast way (_ConvertMatrixShaperToScRGB);
//       the others go through Little CMS, spread over the CPU's threads, the transform being made without its cache
//         (cmsFLAGS_NOCACHE), which lets them share it. False if it cannot be made.
static bool
_ConvertToScRGB (cmsHPROFILE source, cmsUInt32Number inFormat, const uint8_t* in, size_t inStride,
                                                                     uint8_t* out, size_t outStride, uint32_t width, uint32_t height,
                 cmsUInt32Number outFormat = TYPE_RGBA_HALF_FLT)
{
  if (outFormat == TYPE_RGBA_HALF_FLT &&
      _ConvertMatrixShaperToScRGB (source, inFormat, in, inStride, out, outStride, width, height))
    return true;

  cmsHPROFILE   scrgb     = _CreateScRGBProfile ( );
  cmsHTRANSFORM transform = (scrgb == nullptr) ? nullptr :
    cmsCreateTransform (source, inFormat, scrgb, outFormat, INTENT_RELATIVE_COLORIMETRIC,
                        cmsFLAGS_COPY_ALPHA | cmsFLAGS_NOCACHE);

  if (scrgb != nullptr)
    cmsCloseProfile (scrgb);

  if (transform == nullptr)
    return false;

  _ForRows (height, [&] (uint32_t first, uint32_t last)
  {
    for (uint32_t y = first; y < last; y++)
      cmsDoTransform (transform, in + y * inStride, out + y * outStride, width);
  });

  cmsDeleteTransform (transform);
  return true;
}

// Converts RGBA pixels (8 bpc) in place from the source profile into sRGB, clipping colors outside of it: for the frames of
//   animated images, which stay 8 bpc (see _LoadAnimatedImage)
static cmsHTRANSFORM
_CreateSRGBTransform (cmsHPROFILE source)
{
  cmsHPROFILE   srgb      = _CreateRGBProfile (_sRGBPrimaries, _SRGBCurve ( ));
  cmsHTRANSFORM transform = (srgb == nullptr) ? nullptr :
    cmsCreateTransform (source, TYPE_RGBA_8, srgb, TYPE_RGBA_8, INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_COPY_ALPHA);

  if (srgb != nullptr)
    cmsCloseProfile (srgb);

  return transform;
}

#pragma endregion
