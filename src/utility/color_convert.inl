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

#pragma region Convert

// Converts the rows of an image (inFormat: TYPE_RGBA_8, TYPE_BGRA_8 or TYPE_RGBA_16 of lcms2.h) from the source profile into
//   scRGB, as RGBA half floats (TYPE_RGBA_HALF_FLT); alpha is copied. Spread over the CPU's threads, the transform being
//     made without its cache (cmsFLAGS_NOCACHE), which lets them share it. False if it cannot be made.
static bool
_ConvertToScRGB (cmsHPROFILE source, cmsUInt32Number inFormat, const uint8_t* in, size_t inStride,
                                                                     uint8_t* out, size_t outStride, uint32_t width, uint32_t height)
{
  cmsHPROFILE   scrgb     = _CreateScRGBProfile ( );
  cmsHTRANSFORM transform = (scrgb == nullptr) ? nullptr :
    cmsCreateTransform (source, inFormat, scrgb, TYPE_RGBA_HALF_FLT, INTENT_RELATIVE_COLORIMETRIC,
                        cmsFLAGS_COPY_ALPHA | cmsFLAGS_NOCACHE);

  if (scrgb != nullptr)
    cmsCloseProfile (scrgb);

  if (transform == nullptr)
    return false;

  const uint32_t threads = std::clamp (std::thread::hardware_concurrency ( ), 1u, std::max (1u, height / 64));
  std::vector <std::thread> workers;

  auto rows = [&] (uint32_t first, uint32_t last)
  {
    for (uint32_t y = first; y < last; y++)
      cmsDoTransform (transform, in + y * inStride, out + y * outStride, width);
  };

  for (uint32_t t = 1; t < threads; t++)
    workers.emplace_back (rows, height * t / threads, height * (t + 1) / threads);

  rows (0, height / threads);

  for (auto& worker : workers)
    worker.join ( );

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
