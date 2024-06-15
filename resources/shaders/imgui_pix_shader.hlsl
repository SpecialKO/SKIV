#pragma warning ( disable : 3571 )

#define SKIF_Shaders

#ifndef SKIF_Shaders

struct PS_INPUT
{
  float4 pos : SV_POSITION;
  float4 col : COLOR0;
  float2 uv  : TEXCOORD0;
};
sampler sampler0;
Texture2D texture0;

float4 main(PS_INPUT input) : SV_Target
{
  float4 out_col = input.col * texture0.Sample(sampler0, input.uv);
  return out_col;
}

#else

#define SKIV_VISUALIZATION_NONE    0
#define SKIV_VISUALIZATION_HEATMAP 1
#define SKIV_VISUALIZATION_GAMUT   2
#define SKIV_VISUALIZATION_SDR     3

#define SKIV_VIZ_FLAG_SDR_CONSIDER_LUMINANCE  0x1
#define SKIV_VIZ_FLAG_SDR_CONSIDER_GAMUT      0x2
#define SKIV_VIZ_FLAG_SDR_CONSIDER_OVERBRIGHT 0x4

#define SKIV_TONEMAP_TYPE_NONE               0x0
#define SKIV_TONEMAP_TYPE_CLIP               0x1
#define SKIV_TONEMAP_TYPE_INFINITE_ROLLOFF   0x2
#define SKIV_TONEMAP_TYPE_NORMALIZE_TO_CLL   0x4
#define SKIV_TONEMAP_TYPE_MAP_CLL_TO_DISPLAY 0x8

struct PS_INPUT
{
  float4 pos : SV_POSITION;
  float2 uv : TEXCOORD0;
  float4 col : COLOR0;
  float4 lum : COLOR1; // constant_buffer->luminance_scale
  float hdr_img : COLOR2;
};

cbuffer imgui_cbuffer : register(b0)
{
  float4 font_dims;

  uint4 hdr_visualization_flags;
  uint hdr_visualization;

  float hdr_max_luminance;
  float sdr_reference_white;
  float display_max_luminance;
  float user_brightness_scale;
  uint tonemap_type;
  float2 content_max_cll;

  float4 rec709_gamut_hue;
  float4 dcip3_gamut_hue;
  float4 rec2020_gamut_hue;
  float4 ap1_gamut_hue;
  float4 ap0_gamut_hue;
  float4 invalid_gamut_hue;
};

sampler sampler0 : register(s0);
Texture2D texture0 : register(t0);

RWTexture2D<float4> texGamutCoverage : register(u1);

//#define FAST_SRGB

float
ApplySRGBCurve(float x)
{
#ifdef FAST_SRGB
  return x < 0.0031308 ? 12.92 * x : 1.13005 * sqrt(x - 0.00228) - 0.13448 * x + 0.005719;
#else
  // Approximately pow(x, 1.0 / 2.2)
  return (x < 0.0031308 ? 12.92 * x :
                          1.055 * pow(x, 1.0 / 2.4) - 0.055);
#endif
}

float3
ApplySRGBCurve(float3 x)
{
#ifdef FAST_SRGB
  return x < 0.0031308 ? 12.92 * x : 1.13005 * sqrt(x - 0.00228) - 0.13448 * x + 0.005719;
#else
  // Approximately pow(x, 1.0 / 2.2)
  return (x < 0.0031308 ? 12.92 * x :
                          1.055 * pow(x, 1.0 / 2.4) - 0.055);
#endif
}

float
RemoveSRGBCurve(float x)
{
#ifdef FAST_SRGB
  return x < 0.04045 ? x / 12.92 : -7.43605 * x - 31.24297 * sqrt(-0.53792 * x + 1.279924) + 35.34864;
#else
  // Approximately pow(x, 2.2)
  return (x < 0.04045 ? x / 12.92 :
                  pow((x + 0.055) / 1.055, 2.4));
#endif
}

float3
RemoveSRGBCurve(float3 x)
{
#ifdef FAST_SRGB
  return x < 0.04045 ? x / 12.92 : -7.43605 * x - 31.24297 * sqrt(-0.53792 * x + 1.279924) + 35.34864;
#else
  // Approximately pow(x, 2.2)
  return (x < 0.04045 ? x / 12.92 :
                  pow((x + 0.055) / 1.055, 2.4));
#endif
}

float3
ApplyGammaExp(float3 x, float exp)
{
  return sign(x) *
         pow(abs(x), 1.0f / exp);
}

float3
RemoveGammaExp(float3 x, float exp)
{
  return sign(x) *
         pow(abs(x), exp);
}

float3 ApplyRec709Curve(float3 x)
{
  return x < 0.0181 ? 4.5 * x : 1.0993 * pow(x, 0.45) - 0.0993;
}

float Luma(float3 color)
{
  return
    dot(color, float3(0.299f, 0.587f, 0.114f));
}

float3 ApplyREC2084Curve(float3 L, float maxLuminance)
{
  float m1 = 2610.0 / 4096.0 / 4;
  float m2 = 2523.0 / 4096.0 * 128;
  float c1 = 3424.0 / 4096.0;
  float c2 = 2413.0 / 4096.0 * 32;
  float c3 = 2392.0 / 4096.0 * 32;

  float maxLuminanceScale = maxLuminance / 10000.0f;
  L *= maxLuminanceScale;

  float3 Lp = pow(L, m1);

  return pow((c1 + c2 * Lp) / (1 + c3 * Lp), m2);
}

float3 RemoveREC2084Curve(float3 N)
{
  float m1 = 2610.0 / 4096.0 / 4;
  float m2 = 2523.0 / 4096.0 * 128;
  float c1 = 3424.0 / 4096.0;
  float c2 = 2413.0 / 4096.0 * 32;
  float c3 = 2392.0 / 4096.0 * 32;
  float3 Np = pow(N, 1 / m2);

  return
    pow(max(Np - c1, 0) / (c2 - c3 * Np), 1 / m1);
}

// Apply the ST.2084 curve to normalized linear values and outputs normalized non-linear values
// pq_inverse_eotf
float3 LinearToST2084(float3 normalizedLinearValue)
{
  return pow((0.8359375f + 18.8515625f * pow(abs(normalizedLinearValue), 0.1593017578f)) / (1.0f + 18.6875f * pow(abs(normalizedLinearValue), 0.1593017578f)), 78.84375f);
}

// ST.2084 to linear, resulting in a linear normalized value
float3 ST2084ToLinear(float3 ST2084)
{
  return pow(max(pow(abs(ST2084), 1.0f / 78.84375f) - 0.8359375f, 0.0f) / (18.8515625f - 18.6875f * pow(abs(ST2084), 1.0f / 78.84375f)), 1.0f / 0.1593017578f);
}

float3 Rec709toRec2020(float3 linearRec709)
{
  static const float3x3 ConvMat =
  {
    0.627403914928436279296875f, 0.3292830288410186767578125f, 0.0433130674064159393310546875f,
    0.069097287952899932861328125f, 0.9195404052734375f, 0.011362315155565738677978515625f,
    0.01639143936336040496826171875f, 0.08801330626010894775390625f, 0.895595252513885498046875f
  };

  return mul(ConvMat, linearRec709);
}

float3 Rec709toDCIP3(float3 linearRec709)
{
  static const float3x3 ConvMat =
  {
    0.82246196269989013671875f, 0.17753803730010986328125f, 0.f,
    0.03319419920444488525390625f, 0.96680581569671630859375f, 0.f,
    0.017082631587982177734375f, 0.0723974406719207763671875f, 0.91051995754241943359375f
  };

  return mul(ConvMat, linearRec709);
}

float3 Rec709toAP1_D65(float3 linearRec709)
{
  static const float3x3 ConvMat =
  {
    0.61702883243560791015625f, 0.333867609500885009765625f, 0.04910354316234588623046875f,
    0.069922320544719696044921875f, 0.91734969615936279296875f, 0.012727967463433742523193359375f,
    0.02054978720843791961669921875f, 0.107552029192447662353515625f, 0.871898174285888671875f
  };

  return mul(ConvMat, linearRec709);
}

float3 Rec709toAP0_D65(float3 linearRec709)
{
  static const float3x3 ConvMat =
  {
    0.4339316189289093017578125f, 0.3762523829936981201171875f, 0.1898159682750701904296875f,
    0.088618390262126922607421875f, 0.809275329113006591796875f, 0.10210628807544708251953125f,
    0.01775003969669342041015625f, 0.109447620809078216552734375f, 0.872802317142486572265625f
  };

  return mul(ConvMat, linearRec709);
}

float3 Rec2020toRec709(float3 linearRec2020)
{
  static const float3x3 ConvMat =
  {
    1.66049621914783, -0.587656444131135, -0.0728397750166941,
    -0.124547095586012, 1.13289510924730, -0.00834801366128445,
    -0.0181536813870718, -0.100597371685743, 1.11875105307281
  };
  return mul(ConvMat, linearRec2020);
}

float3 Rec709toXYZ(float3 linearRec709)
{
  static const float3x3 ConvMat =
  {
    0.4123907983303070068359375f, 0.3575843274593353271484375f, 0.18048079311847686767578125f,
    0.2126390039920806884765625f, 0.715168654918670654296875f, 0.072192318737506866455078125f,
    0.0193308182060718536376953125f, 0.119194783270359039306640625f, 0.950532138347625732421875f
  };

  return
    mul(ConvMat, linearRec709);
}

float3 XYZtoRec709(float3 linearXYZ)
{
  static const float3x3 ConvMat =
  {
    3.240969896316528320312500000f, -1.5373831987380981445312500f, -0.4986107647418975830078125000f,
    -0.969243645668029785156250000f, 1.8759675025939941406250000f, 0.0415550582110881805419921875f,
     0.055630080401897430419921875f, -0.2039769589900970458984375f, 1.0569715499877929687500000000f
  };

  return
    mul(ConvMat, linearXYZ);
}

bool IsNan(float x)
{
  return (asuint(x) & 0x7fffffff) > 0x7f800000;
} // Scalar NaN checker
float2 IsNan(float2 v)
{
  return float2(IsNan(v.x), IsNan(v.y));
}
float3 IsNan(float3 v)
{
  return float3(IsNan(v.x), IsNan(v.y), IsNan(v.z));
}
float4 IsNan(float4 v)
{
  return float4(IsNan(v.x), IsNan(v.y), IsNan(v.z), IsNan(v.w));
}

bool IsInf(float x)
{
  return (asuint(x) & 0x7f8fffff) == 0x7f800000;
} // Scalar Infinity checker
float2 IsInf(float2 v)
{
  return float2(IsInf(v.x), IsInf(v.y));
}
float3 IsInf(float3 v)
{
  return float3(IsInf(v.x), IsInf(v.y), IsInf(v.z));
}
float4 IsInf(float4 v)
{
  return float4(IsInf(v.x), IsInf(v.y), IsInf(v.z), IsInf(v.w));
}

// Vectorized versions
bool AnyIsInf(float x)
{
  return IsInf(x);
}
bool AnyIsInf(float2 xy)
{
  return any((asuint(xy) & 0x7f8fffff) == 0x7f800000);
}
bool AnyIsInf(float3 xyz)
{
  return any((asuint(xyz) & 0x7f8fffff) == 0x7f800000);
}
bool AnyIsInf(float4 xyzw)
{
  return any((asuint(xyzw) & 0x7f8fffff) == 0x7f800000);
}

bool AnyIsNan(float x)
{
  return IsNan(x);
}
bool AnyIsNan(float2 xy)
{
  return any((asuint(xy) & 0x7fffffff) > 0x7f800000);
}
bool AnyIsNan(float3 xyz)
{
  return any((asuint(xyz) & 0x7fffffff) > 0x7f800000);
}
bool AnyIsNan(float4 xyzw)
{
  return any((asuint(xyzw) & 0x7fffffff) > 0x7f800000);
}

// Combined NaN and Infinity check
bool isnormal(float x)
{
  return (!((asuint(x) & 0x7fffffff) >= 0x7f800000));
}
bool isnormal(float2 xy)
{
  return (!(any((asuint(xy) & 0x7fffffff) >= 0x7f800000)));
}
bool isnormal(float3 xyz)
{
  return (!(any((asuint(xyz) & 0x7fffffff) >= 0x7f800000)));
}
bool isnormal(float4 xyzw)
{
  return (!(any((asuint(xyzw) & 0x7fffffff) >= 0x7f800000)));
}

// Remove special floating-point bit patterns, clamping is the
//   final step before output and outputting NaN or Infinity would
//     break color blending!
#define SanitizeFP(c) ((! isnormal ((c))) ? (IsInf ((c)) ? sign ((c)) * float_MAX : (! IsNan ((c))) * (c)) : (c))

#define float_MAX 65504.0 // (2 - 2^-10) * 2^15
#define FP16_MIN  asfloat (0x33C00000)

float3 AP0_D65toRec709(float3 linearAP0)
{
  static const float3x3 ConvMat =
  {
    2.552483081817626953125f, -1.12950992584228515625f, -0.422973215579986572265625f,
    -0.2773441374301910400390625f, 1.3782665729522705078125f, -0.1009224355220794677734375f,
    -0.01713105104863643646240234375f, -0.14986114203929901123046875f, 1.1669921875f
  };

  return mul(ConvMat, linearAP0);
}

float3 Clamp_scRGB(float3 c)
{
  c = SanitizeFP(c);

  c =
    clamp(c, -float_MAX, float_MAX);
  
  return c;
}

float Clamp_scRGB(float c, bool strip_nan = false)
{
  // No colorspace clamp here, just keep it away from 0.0
  if (strip_nan)
    c = SanitizeFP(c);

  return clamp(c + sign(c) * FP16_MIN, -float_MAX,
                                          float_MAX);
}



void UpdateCIE1931(float4 hdr_color)
{
  float3 XYZ = Rec709toXYZ(hdr_color.rgb);
  float xyz = XYZ.x + XYZ.y + XYZ.z;

  float4 normalized_color =
    float4(hdr_color.rgb / xyz, 1.0);

  if (all(Rec709toAP1_D65(hdr_color.rgb) >= 0))
  {
    texGamutCoverage[uint2(1024 * XYZ.x / xyz,
                               1024 - 1024 * XYZ.y / xyz)].rgba =
      float4(normalized_color.rgb * 2.0f, 1.0f);
  }
}

// HDR Color Input is Linear Rec 709 (scRGB)
float4 ApplyHDRVisualization(uint type, float4 hdr_color, bool post_tonemap)
{
  // Ideally this remains constant with changes in luminance, but
  //   let's test that theory :)
  if (post_tonemap && type != SKIV_VISUALIZATION_GAMUT)
    UpdateCIE1931(hdr_color);
  else if ((!post_tonemap) && type == SKIV_VISUALIZATION_GAMUT)
    UpdateCIE1931(hdr_color);

  switch (type)
  {
    case SKIV_VISUALIZATION_SDR:
    {
        if (post_tonemap)
          return hdr_color;
      
        float luminance =
        max(Rec709toXYZ(hdr_color.rgb).y, 0.0);

        bool sdr = true;
        uint viz_flags = hdr_visualization_flags[SKIV_VISUALIZATION_SDR];

        if ((viz_flags & SKIV_VIZ_FLAG_SDR_CONSIDER_LUMINANCE) && luminance > 1.0f /*sdr_reference_white*/)
          sdr = false;
        else if ((viz_flags & SKIV_VIZ_FLAG_SDR_CONSIDER_GAMUT) && any(hdr_color < 0.0))
          sdr = false;
        else if ((viz_flags & SKIV_VIZ_FLAG_SDR_CONSIDER_OVERBRIGHT) && any(hdr_color > 1.0))
          sdr = false;

        return
        sdr ? float4(luminance.xxx, 1.0f)
            : float4(hdr_color.rgb, 1.0f);
      }
      break;

    case SKIV_VISUALIZATION_HEATMAP:
    {
        if (post_tonemap)
          return hdr_color;

      // Taken from https://github.com/microsoft/Windows-universal-samples/blob/main/Samples/D2DAdvancedColorImages/cpp/D2DAdvancedColorImages/LuminanceHeatmapEffect.hlsl in order to match HDR + WCG Image Viewer's Heatmap
      //
      // Define constants based on above behavior: 9 "stops" for a piecewise linear gradient in scRGB space.
#define STOP0_NITS 0.00f
#define STOP1_NITS 3.16f
#define STOP2_NITS 10.0f
#define STOP3_NITS 31.6f
#define STOP4_NITS 100.f
#define STOP5_NITS 316.f
#define STOP6_NITS 1000.f
#define STOP7_NITS 3160.f
#define STOP8_NITS 10000.f

#define STOP0_COLOR float4 (0.0f, 0.0f, 0.0f, 1.0f) // Black
#define STOP1_COLOR float4 (0.0f, 0.0f, 1.0f, 1.0f) // Blue
#define STOP2_COLOR float4 (0.0f, 1.0f, 1.0f, 1.0f) // Cyan
#define STOP3_COLOR float4 (0.0f, 1.0f, 0.0f, 1.0f) // Green
#define STOP4_COLOR float4 (1.0f, 1.0f, 0.0f, 1.0f) // Yellow
#define STOP5_COLOR float4 (1.0f, 0.2f, 0.0f, 1.0f) // Orange
      // Orange isn't a simple combination of primary colors but allows us to have 8 gradient segments,
      // which gives us cleaner definitions for the nits --> color mappings.
#define STOP6_COLOR float4 (1.0f, 0.0f, 0.0f, 1.0f) // Red
#define STOP7_COLOR float4 (1.0f, 0.0f, 1.0f, 1.0f) // Magenta
#define STOP8_COLOR float4 (1.0f, 1.0f, 1.0f, 1.0f) // White

      // 1: Calculate luminance in nits.
      // Input is in scRGB. First convert to Y from CIEXYZ, then scale by whitepoint of 80 nits.
        float nits =
        max(Rec709toXYZ(hdr_color.rgb).y, 0.0) * 80.0f;

      // 2: Determine which gradient segment will be used.
      // Only one of useSegmentN will be 1 (true) for a given nits value.
        float useSegment0 = sign(nits - STOP0_NITS) - sign(nits - STOP1_NITS);
        float useSegment1 = sign(nits - STOP1_NITS) - sign(nits - STOP2_NITS);
        float useSegment2 = sign(nits - STOP2_NITS) - sign(nits - STOP3_NITS);
        float useSegment3 = sign(nits - STOP3_NITS) - sign(nits - STOP4_NITS);
        float useSegment4 = sign(nits - STOP4_NITS) - sign(nits - STOP5_NITS);
        float useSegment5 = sign(nits - STOP5_NITS) - sign(nits - STOP6_NITS);
        float useSegment6 = sign(nits - STOP6_NITS) - sign(nits - STOP7_NITS);
        float useSegment7 = sign(nits - STOP7_NITS) - sign(nits - STOP8_NITS);

      // 3: Calculate the interpolated color.
        float lerpSegment0 = (nits - STOP0_NITS) / (STOP1_NITS - STOP0_NITS);
        float lerpSegment1 = (nits - STOP1_NITS) / (STOP2_NITS - STOP1_NITS);
        float lerpSegment2 = (nits - STOP2_NITS) / (STOP3_NITS - STOP2_NITS);
        float lerpSegment3 = (nits - STOP3_NITS) / (STOP4_NITS - STOP3_NITS);
        float lerpSegment4 = (nits - STOP4_NITS) / (STOP5_NITS - STOP4_NITS);
        float lerpSegment5 = (nits - STOP5_NITS) / (STOP6_NITS - STOP5_NITS);
        float lerpSegment6 = (nits - STOP6_NITS) / (STOP7_NITS - STOP6_NITS);
        float lerpSegment7 = (nits - STOP7_NITS) / (STOP8_NITS - STOP7_NITS);

      //  Only the "active" gradient segment contributes to the output color.
        hdr_color =
        lerp(STOP0_COLOR, STOP1_COLOR, lerpSegment0) * useSegment0 +
        lerp(STOP1_COLOR, STOP2_COLOR, lerpSegment1) * useSegment1 +
        lerp(STOP2_COLOR, STOP3_COLOR, lerpSegment2) * useSegment2 +
        lerp(STOP3_COLOR, STOP4_COLOR, lerpSegment3) * useSegment3 +
        lerp(STOP4_COLOR, STOP5_COLOR, lerpSegment4) * useSegment4 +
        lerp(STOP5_COLOR, STOP6_COLOR, lerpSegment5) * useSegment5 +
        lerp(STOP6_COLOR, STOP7_COLOR, lerpSegment6) * useSegment6 +
        lerp(STOP7_COLOR, STOP8_COLOR, lerpSegment7) * useSegment7;
        hdr_color.a = 1.0f;
        if (nits > 10000.0)
          hdr_color.rgb = 125.0f;
        if (nits < 0.0)
          hdr_color.rgb = 0.0f;
        return hdr_color;
      }
      break;

    case SKIV_VISUALIZATION_GAMUT:
    {
        if (post_tonemap)
          return hdr_color;

#define REC709_HUE  rec709_gamut_hue
#define DCIP3_HUE   dcip3_gamut_hue
#define REC2020_HUE rec2020_gamut_hue
#define AP1_HUE     ap1_gamut_hue
#define AP0_HUE     ap0_gamut_hue
#define INVALID_HUE invalid_gamut_hue

      // Display all colors wider than Rec 709 at a minimum of 12 nits
#define MIN_WIDE_GAMUT_Y 0.15f

        float fLuminance =
        min(125.0f, Rec709toXYZ(hdr_color.rgb).y);

        if (fLuminance < FP16_MIN)
          return (0.0f).xxxx;

        if ((!isnormal(hdr_color.rgb)) || any(Rec709toAP0_D65(hdr_color.rgb) < 0.0))
        {
          return
          max(MIN_WIDE_GAMUT_Y * 8.0f, fLuminance) * INVALID_HUE;
        }

        if (any(Rec709toAP1_D65(hdr_color.rgb) < 0.0))
        {
          return
          max(MIN_WIDE_GAMUT_Y * 4.0f, fLuminance) * AP0_HUE;
        }

        if (any(Rec709toRec2020(hdr_color.rgb) < 0.0))
        {
          return
          max(MIN_WIDE_GAMUT_Y * 3.0f, fLuminance) * AP1_HUE;
        }

        if (any(Rec709toDCIP3(hdr_color.rgb) < 0.0))
        {
          return
          max(MIN_WIDE_GAMUT_Y * 2.0f, fLuminance) * REC2020_HUE;
        }

        if (any(hdr_color.rgb < 0.0))
        {
          return
          max(MIN_WIDE_GAMUT_Y, fLuminance) * DCIP3_HUE;
        }

        return
        fLuminance * REC709_HUE;
      }
      break;

    case SKIV_VISUALIZATION_NONE:
    default:
      return hdr_color;
      break;
  }
}

float TonemapNone(float L)
{
  return L;
}

float TonemapClip(float L, float Ld)
{
  return
    min(L, Ld);
}

float TonemapSDR(float L, float Lc, float Ld)
{
  return
    (L + (1.0f / pow(Lc, 2.0f)) * pow(L, 2.0f)) / (1.0f + L);
}

float TonemapHDR(float L, float Lc, float Ld)
{
  float a = (Ld / pow(Lc, 2.0f));
  float b = (1.0f / Ld);

  return
    L * (1 + a * L) / (1 + b * L);
}

float4 main(PS_INPUT input) : SV_Target
{
  float4 input_col = input.col;

  // For HDR image display, ignore ImGui tint color
  if (input.hdr_img)
  {
    input_col = float4(1.0f, 1.0f, 1.0f, 1.0f);
  }

  float4 out_col =
    texture0.Sample(sampler0, input.uv);


  float prescale_luminance = 1.0f;
  float postscale_luminance = 1.0f;

  if (input.hdr_img)
  {
    if (hdr_visualization == SKIV_VISUALIZATION_HEATMAP)
      prescale_luminance = user_brightness_scale;
    if (hdr_visualization == SKIV_VISUALIZATION_SDR)
      postscale_luminance = user_brightness_scale;

    out_col =
      ApplyHDRVisualization(hdr_visualization, out_col * prescale_luminance, false)
                                                        * postscale_luminance;
  }

  prescale_luminance = 1.0f;
  postscale_luminance = 1.0f;


  // When sampling FP textures, special FP bit patterns like NaN or Infinity
  //   may be returned. The same image represented using UNORM would replace
  //     these special values with 0.0, and that is the behavior we want...
  out_col =
    SanitizeFP(out_col);

  // Input is an alpha-only font texture if these are non-zero
  if (font_dims.x + font_dims.y > 0.0f)
  {
  // Supply constant 1.0 for the color components, we only want alpha
    out_col.rgb = 1.0f;
  }

  // Other way around for everything else, we do not want a texture's alpha
  else
  {
    out_col.a = 1.0f;
  }

  float4 orig_col = out_col;

              // input.lum.x        // Luminance (white point)
  bool isHDR = input.lum.y > 0.0; // HDR (10 bpc or 16 bpc)
  bool is10bpc = input.lum.z > 0.0; // 10 bpc
  bool is16bpc = input.lum.w > 0.0; // 16 bpc (scRGB)
  
  // 16 bpc scRGB (SDR/HDR)
  // ColSpace:  DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709
  // Gamma:     1.0
  // Primaries: BT.709 
  if (is16bpc)
  {
    out_col =
      float4(input.hdr_img ?
                RemoveGammaExp(input_col.rgb, 2.2f) *
                                              out_col.rgb :
                RemoveGammaExp(input_col.rgb *
                               ApplyGammaExp(out_col.rgb, 2.2f), 2.2f),
                                  saturate(out_col.a) *
                                  saturate(input_col.a)
              );

    float hdr_scale = input.lum.x;

    if (!input.hdr_img)
      out_col.rgb = saturate(out_col.rgb) * hdr_scale;
    else
      out_col.a = 1.0f; // Opaque

    if (abs(orig_col.r + orig_col.g + orig_col.b) <= 0.000013)
      out_col.rgb = 0.0f;


    if (input.hdr_img)
    {
      int implied_tonemap_type =
        (hdr_visualization == SKIV_VISUALIZATION_GAMUT) ? SKIV_TONEMAP_TYPE_NONE
                                                        : tonemap_type;

      out_col.rgb *=
        (hdr_visualization != SKIV_VISUALIZATION_NONE) ? 1.0f
                                                       : user_brightness_scale;


      float dML = display_max_luminance;
      float cML = hdr_max_luminance;

      float3 xyz = Rec709toXYZ(out_col.rgb);
      float Y_in = max(xyz.y, 0.0f);
      float Y_out = 1.0f;

      switch (implied_tonemap_type)
      {
        // This tonemap type is not necessary, we always know content range
        //SKIV_TONEMAP_TYPE_INFINITE_ROLLOFF

        default:
        case SKIV_TONEMAP_TYPE_NONE:
          Y_out = TonemapNone(Y_in);
          break;
        case SKIV_TONEMAP_TYPE_CLIP:
          Y_out = TonemapClip(Y_in, dML);
          break;
        case SKIV_TONEMAP_TYPE_NORMALIZE_TO_CLL:
          Y_out = TonemapSDR(Y_in, cML, 1.0f);
          break;
        case SKIV_TONEMAP_TYPE_MAP_CLL_TO_DISPLAY:
          Y_out = TonemapHDR(Y_in, cML, dML);
          break;
      }

      if (Y_out + Y_in > 0.0)
      {
        xyz.xyz *= max((Y_out / Y_in), 0.0f);
      }
      else
        xyz.xyz = (0.0).xxx;

      out_col.rgb = XYZtoRec709(xyz);


      // Scale the input to visualize the heat, then undo the scale so that the
      //   visualization has constant luminance.
      if (hdr_visualization == SKIV_VISUALIZATION_HEATMAP)
      {
        prescale_luminance = user_brightness_scale;
        postscale_luminance = 1.0f / user_brightness_scale;
      }

      out_col =
        ApplyHDRVisualization(hdr_visualization, out_col * prescale_luminance, true)
                                                          * postscale_luminance;
    }

    // Manipulate the alpha channel a bit...
  //out_col.a = 1.0f - RemoveSRGBCurve (1.0f - out_col.a); // Sort of perfect alpha transparency handling, but worsens fonts (more haloing), in particular for bright fonts on dark backgrounds
  //out_col.a = out_col.a;                                 // Worse alpha transparency handling, but improves fonts (less haloing)
  //out_col.a = 1.0f - ApplySRGBCurve  (1.0f - out_col.a); // Unusable alpha transparency, and worsens dark fonts on bright backgrounds
    // No perfect solution for various reasons (ImGui not having proper subpixel font rendering or doing linear colors for example)

    out_col.rgb *= out_col.a;
  }
  
  // HDR10 (pending potential removal)
  // ColSpace:  DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
  // Gamma:     2084
  // Primaries: BT.2020
  else if (is10bpc && isHDR)
  {
    out_col =
      float4(RemoveGammaExp(input_col.rgb *
                               ApplyGammaExp(out_col.rgb, 2.2f), 2.2f),
                                  saturate(out_col.a) *
                                  saturate(input_col.a)
              );
    
    float hdr_scale = (-input.lum.x / 10000.0);
    
    out_col.rgb =
        LinearToST2084(
          Rec709toRec2020(saturate(out_col.rgb)) * hdr_scale);

    // Manipulate the alpha channel a bit... sometimes...
    if (orig_col.a < 0.5f)
      out_col.a = 1.0f - ApplySRGBCurve(1.0f - out_col.a);
    
    out_col.rgb *= out_col.a;
  }

  // 10 bpc SDR
  // ColSpace:  DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709
  // Gamma:     2.2
  // Primaries: BT.709 
  else if (is10bpc)
  {
    if (!input.hdr_img)
    {
      out_col =
        float4(RemoveGammaExp(input_col.rgb *
                                 ApplyGammaExp(out_col.rgb, 2.2f), 2.2f),
                                    saturate(out_col.a) *
                                    saturate(input_col.a)
                );

      out_col.rgb = ApplySRGBCurve(out_col.rgb);
    }
    else
    {
      out_col =
        float4(RemoveGammaExp(input_col.rgb, 2.2f) *
                                                out_col.rgb,
                                    saturate(out_col.a) *
                                    saturate(input_col.a)
                );

      out_col.a = 1.0f; // Opaque

      if (abs(orig_col.r + orig_col.g + orig_col.b) <= 0.000013)
        out_col.rgb = 0.0f;
    }


    if (input.hdr_img)
    {
      int implied_tonemap_type =
        (hdr_visualization == SKIV_VISUALIZATION_GAMUT) ? SKIV_TONEMAP_TYPE_NONE
                                                        : tonemap_type;

      out_col.rgb *=
        (hdr_visualization != SKIV_VISUALIZATION_NONE) ? 1.0f
                                                       : user_brightness_scale * 0.25;


      float dML = display_max_luminance;
      float cML = hdr_max_luminance;

      float3 xyz = Rec709toXYZ(out_col.rgb);
      float Y_in = max(xyz.y, 0.0f);
      float Y_out = 1.0f;

      Y_out = TonemapSDR(Y_in, cML, 2.5375f);

      if (Y_out + Y_in > 0.0)
      {
        xyz.xyz *= max((Y_out / Y_in), 0.0f);
      }
      else
        xyz.xyz = (0.0).xxx;

      out_col.rgb = XYZtoRec709(xyz);


      // Scale the input to visualize the heat, then undo the scale so that the
      //   visualization has constant luminance.
      if (hdr_visualization == SKIV_VISUALIZATION_HEATMAP)
      {
        prescale_luminance = (user_brightness_scale * 0.25f);
        postscale_luminance = 1.0f / (user_brightness_scale * 0.25f);
      }

      out_col =
        ApplyHDRVisualization(hdr_visualization, out_col * prescale_luminance, true)
                                                          * postscale_luminance;

      out_col.rgb =
        ApplySRGBCurve(saturate(out_col.rgb));
    }

    out_col.rgb *= out_col.a;

    // Manipulate the alpha channel a bit...
  //out_col.a = 1.0f - RemoveSRGBCurve (1.0f - out_col.a); // Sort of perfect alpha transparency handling, but worsens fonts (more haloing), in particular for bright fonts on dark backgrounds
  //out_col.a = out_col.a;                                 // Worse alpha transparency handling, but improves fonts (less haloing)
  //out_col.a = 1.0f - ApplySRGBCurve  (1.0f - out_col.a); // Unusable alpha transparency, and worsens dark fonts on bright backgrounds
    // No perfect solution for various reasons (ImGui not having proper subpixel font rendering or doing linear colors for example);
  }

  // 8 bpc SDR (sRGB)
  else
  {

#ifdef _SRGB
    out_col =
      float4 (   (           input_col.rgb) *
                 (             out_col.rgb),
                                  saturate (  out_col.a)  *
                                  saturate (input_col.a)
              );

    out_col.rgb = RemoveSRGBCurve (out_col.rgb);

    // Manipulate the alpha channel a bit...
    out_col.a = 1.0f - RemoveSRGBCurve (1.0f - out_col.a);
#else

    if (!input.hdr_img)
    {
      out_col =
        float4(RemoveGammaExp(input_col.rgb *
                                ApplyGammaExp(out_col.rgb, 2.2f), 2.2f),
                                    saturate(out_col.a) *
                                    saturate(input_col.a)
                );

      out_col.rgb = ApplySRGBCurve(out_col.rgb);
    }
    else
    {
      out_col =
        float4(RemoveGammaExp(input_col.rgb, 2.2f) *
                                                out_col.rgb,
                                    saturate(out_col.a) *
                                    saturate(input_col.a)
                );

      out_col.a = 1.0f; // Opaque

      if (abs(orig_col.r + orig_col.g + orig_col.b) <= 0.000013)
        out_col.rgb = 0.0f;
    }


    if (input.hdr_img)
    {
      int implied_tonemap_type =
        (hdr_visualization == SKIV_VISUALIZATION_GAMUT) ? SKIV_TONEMAP_TYPE_NONE
                                                        : tonemap_type;

      out_col.rgb *=
        (hdr_visualization != SKIV_VISUALIZATION_NONE) ? 1.0f
                                                       : user_brightness_scale * 0.25;


      float dML = display_max_luminance;
      float cML = hdr_max_luminance;

      float3 xyz = Rec709toXYZ(out_col.rgb);
      float Y_in = max(xyz.y, 0.0f);
      float Y_out = 1.0f;

      Y_out = TonemapSDR(Y_in, cML, 2.5375f);

      if (Y_out + Y_in > 0.0)
      {
        xyz.xyz *= max((Y_out / Y_in), 0.0f);
      }
      else
        xyz.xyz = (0.0).xxx;

      out_col.rgb = XYZtoRec709(xyz);


      // Scale the input to visualize the heat, then undo the scale so that the
      //   visualization has constant luminance.
      if (hdr_visualization == SKIV_VISUALIZATION_HEATMAP)
      {
        prescale_luminance = (user_brightness_scale * 0.25f);
        postscale_luminance = 1.0f / (user_brightness_scale * 0.25f);
      }

      out_col =
        ApplyHDRVisualization(hdr_visualization, out_col * prescale_luminance, true)
                                                          * postscale_luminance;

      out_col.rgb =
        ApplySRGBCurve(saturate(out_col.rgb));
    }

#endif

    out_col.rgb *= out_col.a;
  }

  return out_col;
};

#endif