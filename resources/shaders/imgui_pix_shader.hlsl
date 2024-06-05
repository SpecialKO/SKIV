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

struct PS_INPUT
{
  float4 pos     : SV_POSITION;
  float2 uv      : TEXCOORD0;
  float4 col     : COLOR0;
  float4 lum     : COLOR1; // constant_buffer->luminance_scale
  float  hdr_img : COLOR2;
};

cbuffer imgui_cbuffer : register (b0)
{
  float4 font_dims;
  uint   hdr_visualization;
  float  hdr_max_luminance;
};

sampler   sampler0 : register (s0);
Texture2D texture0 : register (t0);

//#define FAST_SRGB

float
ApplySRGBCurve (float x)
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
ApplySRGBCurve (float3 x)
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
RemoveSRGBCurve (float x)
{
#ifdef FAST_SRGB
  return x < 0.04045 ? x / 12.92 : -7.43605 * x - 31.24297 * sqrt(-0.53792 * x + 1.279924) + 35.34864;
#else
  // Approximately pow(x, 2.2)
  return (x < 0.04045 ? x / 12.92 :
                  pow( (x + 0.055) / 1.055, 2.4 ));
#endif
}

float3
RemoveSRGBCurve (float3 x)
{
#ifdef FAST_SRGB
  return x < 0.04045 ? x / 12.92 : -7.43605 * x - 31.24297 * sqrt(-0.53792 * x + 1.279924) + 35.34864;
#else
  // Approximately pow(x, 2.2)
  return (x < 0.04045 ? x / 12.92 :
                  pow( (x + 0.055) / 1.055, 2.4 ));
#endif
}

float3
ApplyGammaExp (float3 x, float exp)
{
  return     sign (x) *
         pow (abs (x), 1.0f/exp);
}

float3
RemoveGammaExp (float3 x, float exp)
{
  return     sign (x) *
         pow (abs (x), exp);
}

float3 ApplyREC709Curve (float3 x)
{
  return x < 0.0181 ? 4.5 * x : 1.0993 * pow(x, 0.45) - 0.0993;
}

float Luma (float3 color)
{
  return
    dot (color, float3 (0.299f, 0.587f, 0.114f));
}

float3 ApplyREC2084Curve (float3 L, float maxLuminance)
{
  float m1 = 2610.0 / 4096.0 / 4;
  float m2 = 2523.0 / 4096.0 * 128;
  float c1 = 3424.0 / 4096.0;
  float c2 = 2413.0 / 4096.0 * 32;
  float c3 = 2392.0 / 4096.0 * 32;

  float maxLuminanceScale = maxLuminance / 10000.0f;
  L *= maxLuminanceScale;

  float3 Lp = pow (L, m1);

  return pow ((c1 + c2 * Lp) / (1 + c3 * Lp), m2);
}

float3 RemoveREC2084Curve (float3 N)
{
  float  m1 = 2610.0 / 4096.0 / 4;
  float  m2 = 2523.0 / 4096.0 * 128;
  float  c1 = 3424.0 / 4096.0;
  float  c2 = 2413.0 / 4096.0 * 32;
  float  c3 = 2392.0 / 4096.0 * 32;
  float3 Np = pow (N, 1 / m2);

  return
    pow (max (Np - c1, 0) / (c2 - c3 * Np), 1 / m1);
}

// Apply the ST.2084 curve to normalized linear values and outputs normalized non-linear values
// pq_inverse_eotf
float3 LinearToST2084 (float3 normalizedLinearValue)
{
    return pow((0.8359375f + 18.8515625f * pow(abs(normalizedLinearValue), 0.1593017578f)) / (1.0f + 18.6875f * pow(abs(normalizedLinearValue), 0.1593017578f)), 78.84375f);
}

// ST.2084 to linear, resulting in a linear normalized value
float3 ST2084ToLinear (float3 ST2084)
{
    return pow(max(pow(abs(ST2084), 1.0f / 78.84375f) - 0.8359375f, 0.0f) / (18.8515625f - 18.6875f * pow(abs(ST2084), 1.0f / 78.84375f)), 1.0f / 0.1593017578f);
}

float3 REC709toREC2020 (float3 linearRec709)
{
  static const float3x3 ConvMat =
  {
    0.627401924722236,  0.329291971755002,  0.0433061035227622,
    0.0690954897392608, 0.919544281267395,  0.0113602289933443,
    0.0163937090881632, 0.0880281623979006, 0.895578128513936
  };
  return mul (ConvMat, linearRec709);
}

float3 REC2020toREC709 (float3 linearRec2020)
{
  static const float3x3 ConvMat =
  {
     1.66049621914783,   -0.587656444131135, -0.0728397750166941,
    -0.124547095586012,   1.13289510924730,  -0.00834801366128445,
    -0.0181536813870718, -0.100597371685743,  1.11875105307281
  };
  return mul (ConvMat, linearRec2020);
}

float3 Rec709_to_XYZ (float3 linearRec709)
{
  static const float3x3 ConvMat =
  {
    0.4123907983303070068359375f,    0.3575843274593353271484375f,   0.18048079311847686767578125f,
    0.2126390039920806884765625f,    0.715168654918670654296875f,    0.072192318737506866455078125f,
    0.0193308182060718536376953125f, 0.119194783270359039306640625f, 0.950532138347625732421875f
  };

  return
    mul (ConvMat, linearRec709);
}

// Expand bright saturated colors outside the sRGB (REC.709) gamut to fake wide gamut rendering (BT.2020).
// Inspired by Unreal Engine 4/5 (ACES).
// Input (and output) needs to be in sRGB linear space.
// Calling this with a fExpandGamut value of 0 still results in changes (it's actually an edge case, don't call it).
// Calling this with fExpandGamut values above 1 yields diminishing returns.
float3
expandGamut (float3 vHDRColor, float fExpandGamut = 1.0f)
{
  const float3 AP1_RGB2Y =
    float3 (0.272229, 0.674082, 0.0536895);
  
  const float3x3 AP1_2_sRGB = {
     1.70505, -0.62179, -0.08326,
    -0.13026,  1.14080, -0.01055,
    -0.02400, -0.12897,  1.15297,
  };

  //AP1 with D65 white point instead of the custom white point from ACES which is around 6000K
  const float3x3 sRGB_2_AP1_D65_MAT =
  {
    0.6168509940091290, 0.334062934274955, 0.0490860717159169,
    0.0698663939791712, 0.917416678964894, 0.0127169270559354,
    0.0205490668158849, 0.107642210710817, 0.8718087224732980
  };
  const float3x3 AP1_D65_2_sRGB_MAT =
  {
     1.6926793984921500, -0.606218057156000, -0.08646134133615040,
    -0.1285739800722680,  1.137933633392290, -0.00935965332001697,
    -0.0240224650921189, -0.126211717940702,  1.15023418303282000
  };
  const float3x3 AP1_D65_2_XYZ_MAT =
  {
     0.64729265784680500, 0.13440339917805700, 0.1684710654303190,
     0.26599824508992100, 0.67608982616840700, 0.0579119287416720,
    -0.00544706303938401, 0.00407283027812294, 1.0897972045023700
  };
  // Bizarre matrix but this expands sRGB to between P3 and AP1
  // CIE 1931 chromaticities:   x         y
  //                Red:        0.6965    0.3065
  //                Green:      0.245     0.718
  //                Blue:       0.1302    0.0456
  //                White:      0.3127    0.3291 (=D65)
  const float3x3 Wide_2_AP1_D65_MAT = 
  {
    0.83451690546233900, 0.1602595895494930, 0.00522350498816804,
    0.02554519357785500, 0.9731015318660700, 0.00135327455607548,
    0.00192582885428273, 0.0303727970124423, 0.96770137413327500
  };
  const float3x3
         ExpandMat = mul (Wide_2_AP1_D65_MAT, AP1_D65_2_sRGB_MAT);   
  float3 ColorAP1  = mul (sRGB_2_AP1_D65_MAT, vHDRColor);

  float  LumaAP1   = dot (ColorAP1, AP1_RGB2Y);
  float3 ChromaAP1 =      ColorAP1 / LumaAP1;

  float ChromaDistSqr = dot (ChromaAP1 - 1, ChromaAP1 - 1);
  float ExpandAmount  = (1 - exp2 (-4 * ChromaDistSqr)) * (1 - exp2 (-4 * fExpandGamut * LumaAP1 * LumaAP1));

  float3 ColorExpand =
    mul (ExpandMat, ColorAP1);
  
  ColorAP1 =
    lerp (ColorAP1, ColorExpand, ExpandAmount);

  vHDRColor =
    mul (AP1_2_sRGB, ColorAP1);
  
  return vHDRColor;
}

bool  IsNan (half  x) { return (asuint (x) & 0x7fffffff)  > 0x7f800000; } // Scalar NaN checker
half2 IsNan (half2 v) { return half2 ( IsNan (v.x), IsNan (v.y) );                           }
half3 IsNan (half3 v) { return half3 ( IsNan (v.x), IsNan (v.y), IsNan (v.z) );              }
half4 IsNan (half4 v) { return half4 ( IsNan (v.x), IsNan (v.y), IsNan (v.z), IsNan (v.w) ); }

bool  IsInf (half  x) { return (asuint (x) & 0x7f800000) == 0x7f800000; } // Scalar Infinity checker
half2 IsInf (half2 v) { return half2 ( IsInf (v.x), IsInf (v.y) );                           }
half3 IsInf (half3 v) { return half3 ( IsInf (v.x), IsInf (v.y), IsInf (v.z) );              }
half4 IsInf (half4 v) { return half4 ( IsInf (v.x), IsInf (v.y), IsInf (v.z), IsInf (v.w) ); }

// Vectorized versions
bool AnyIsInf (half  x)    { return        IsInf (x);                                 }
bool AnyIsInf (half2 xy)   { return any ((asuint (xy)   & 0x7f800000) == 0x7f800000); }
bool AnyIsInf (half3 xyz)  { return any ((asuint (xyz)  & 0x7f800000) == 0x7f800000); }
bool AnyIsInf (half4 xyzw) { return any ((asuint (xyzw) & 0x7f800000) == 0x7f800000); }

bool AnyIsNan (half  x)    { return        IsNan (x);                                 }
bool AnyIsNan (half2 xy)   { return any ((asuint (xy)   & 0x7fffffff)  > 0x7f800000); }
bool AnyIsNan (half3 xyz)  { return any ((asuint (xyz)  & 0x7fffffff)  > 0x7f800000); }
bool AnyIsNan (half4 xyzw) { return any ((asuint (xyzw) & 0x7fffffff)  > 0x7f800000); }

// Combined NaN and Infinity check
bool isnormal (half  x)    { return (! (     (asuint (x)    & 0x7fffffff) >= 0x7f800000));  }
bool isnormal (half2 xy)   { return (! (any ((asuint (xy)   & 0x7fffffff) >= 0x7f800000))); }
bool isnormal (half3 xyz)  { return (! (any ((asuint (xyz)  & 0x7fffffff) >= 0x7f800000))); }
bool isnormal (half4 xyzw) { return (! (any ((asuint (xyzw) & 0x7fffffff) >= 0x7f800000))); }

// Remove special floating-point bit patterns, clamping is the
//   final step before output and outputting NaN or Infinity would
//     break color blending!
#define SanitizeFP(c) ((! isnormal ((c))) ? (! IsNan ((c))) * (IsInf ((c)) ? sign ((c)) * float_MAX : (c)) : (c))

#define float_MAX 65504.0 // (2 - 2^-10) * 2^15
#define FP16_MIN 0.00006103515625f                 

float3 Clamp_scRGB (float3 c)
{
  if (any (c < 0.0f) || (! isnormal (c)))
  {
    // Clamp to Rec 2020
    return
      REC2020toREC709 (
        max (REC709toREC2020 (c), 0.0f)
      );
  }

  return c;
}

float Clamp_scRGB (float c, bool strip_nan = false)
{
  // No colorspace clamp here, just keep it away from 0.0
  if (strip_nan)
    c = SanitizeFP (c);

  return clamp (c + sign (c) * FP16_MIN, -float_MAX,
                                          float_MAX);
}

// HDR Color Input is Linear Rec 709 (scRGB)
float4 ApplyHDRVisualization (uint type, float4 hdr_color)
{
  switch (type)
  {
    case SKIV_VISUALIZATION_HEATMAP:
    {
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
        max (Rec709_to_XYZ (hdr_color.rgb).y, 0.0) * 80.0f;

      // 2: Determine which gradient segment will be used.
      // Only one of useSegmentN will be 1 (true) for a given nits value.
      float useSegment0 = sign (nits - STOP0_NITS) - sign (nits - STOP1_NITS);
      float useSegment1 = sign (nits - STOP1_NITS) - sign (nits - STOP2_NITS);
      float useSegment2 = sign (nits - STOP2_NITS) - sign (nits - STOP3_NITS);
      float useSegment3 = sign (nits - STOP3_NITS) - sign (nits - STOP4_NITS);
      float useSegment4 = sign (nits - STOP4_NITS) - sign (nits - STOP5_NITS);
      float useSegment5 = sign (nits - STOP5_NITS) - sign (nits - STOP6_NITS);
      float useSegment6 = sign (nits - STOP6_NITS) - sign (nits - STOP7_NITS);
      float useSegment7 = sign (nits - STOP7_NITS) - sign (nits - STOP8_NITS);

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
        lerp (STOP0_COLOR, STOP1_COLOR, lerpSegment0) * useSegment0 +
        lerp (STOP1_COLOR, STOP2_COLOR, lerpSegment1) * useSegment1 +
        lerp (STOP2_COLOR, STOP3_COLOR, lerpSegment2) * useSegment2 +
        lerp (STOP3_COLOR, STOP4_COLOR, lerpSegment3) * useSegment3 +
        lerp (STOP4_COLOR, STOP5_COLOR, lerpSegment4) * useSegment4 +
        lerp (STOP5_COLOR, STOP6_COLOR, lerpSegment5) * useSegment5 +
        lerp (STOP6_COLOR, STOP7_COLOR, lerpSegment6) * useSegment6 +
        lerp (STOP7_COLOR, STOP8_COLOR, lerpSegment7) * useSegment7;
      hdr_color.a = 1.0f;
    } break;

    case SKIV_VISUALIZATION_NONE:
    default:
      break;
  }

  return hdr_color;
}

float4 main (PS_INPUT input) : SV_Target
{
  float4 input_col = input.col;

  // For HDR image display, ignore ImGui tint color
  if (input.hdr_img)
  {
    input_col = float4 (1.0f, 1.0f, 1.0f, 1.0f);
  }

  // When sampling FP textures, special FP bit patterns like NaN or Infinity
  //   may be returned. The same image represented using UNORM would replace
  //     these special values with 0.0, and that is the behavior we want...
  float4 out_col =
    SanitizeFP (texture0.Sample (sampler0, input.uv));

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
  bool isHDR   = input.lum.y > 0.0; // HDR (10 bpc or 16 bpc)
  bool is10bpc = input.lum.z > 0.0; // 10 bpc
  bool is16bpc = input.lum.w > 0.0; // 16 bpc (scRGB)
  
  // 16 bpc scRGB (SDR/HDR)
  // ColSpace:  DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709
  // Gamma:     1.0
  // Primaries: BT.709 
  if (is16bpc)
  {
    // Clamp_scRGB_StripNaN ( expandGamut
    out_col =
      float4 (  input.hdr_img ?
                RemoveGammaExp  (           input_col.rgb,        2.2f) *
                                              out_col.rgb               :
                RemoveGammaExp  (           input_col.rgb *
                               ApplyGammaExp (out_col.rgb, 2.2f), 2.2f),
                                  saturate (  out_col.a)  *
                                  saturate (input_col.a)
              );

//#def EXPAND
#ifdef EXPAND
    out_col.rgb = Clamp_scRGB_StripNaN (expandGamut (
                                  saturate (out_col.rgb), 0.0333)
              );
#endif

    float hdr_scale = input.lum.x;

    if (! input.hdr_img)
      out_col.rgb = saturate (out_col.rgb) * hdr_scale;
    else
      out_col.a = 1.0f; // Opaque

    if (abs (orig_col.r + orig_col.g + orig_col.b) <= 0.000013)
      out_col.rgb = 0.0f;

    // Manipulate the alpha channel a bit...
  //out_col.a = 1.0f - RemoveSRGBCurve (1.0f - out_col.a); // Sort of perfect alpha transparency handling, but worsens fonts (more haloing), in particular for bright fonts on dark backgrounds
  //out_col.a = out_col.a;                                 // Worse alpha transparency handling, but improves fonts (less haloing)
  //out_col.a = 1.0f - ApplySRGBCurve  (1.0f - out_col.a); // Unusable alpha transparency, and worsens dark fonts on bright backgrounds
    // No perfect solution for various reasons (ImGui not having proper subpixel font rendering or doing linear colors for example)

    out_col.rgb *= out_col.a;

    if (input.hdr_img)
    {
      out_col =
        ApplyHDRVisualization (hdr_visualization, out_col);
    }
  }
  
  // HDR10 (pending potential removal)
  // ColSpace:  DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
  // Gamma:     2084
  // Primaries: BT.2020
  else if (is10bpc && isHDR)
  {
    out_col =
      float4 (  RemoveGammaExp (            input_col.rgb *
                               ApplyGammaExp (out_col.rgb, 2.2f), 2.2f),
                                  saturate (  out_col.a)  *
                                  saturate (input_col.a)
              );
    
    float hdr_scale = (-input.lum.x / 10000.0);
    
    out_col.rgb =
        LinearToST2084 (
          REC709toREC2020 ( saturate (out_col.rgb) ) * hdr_scale);

    // Manipulate the alpha channel a bit... sometimes...
    if (orig_col.a < 0.5f)
      out_col.a = 1.0f - ApplySRGBCurve (1.0f - out_col.a);
    
    out_col.rgb *= out_col.a;
  }

  // 10 bpc SDR
  // ColSpace:  DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709
  // Gamma:     2.2
  // Primaries: BT.709 
  else if (is10bpc)
  {
    out_col =
      float4 (  RemoveGammaExp  (           input_col.rgb *
                               ApplyGammaExp (out_col.rgb, 2.2f), 2.2f),
                                  saturate (  out_col.a)  *
                                  saturate (input_col.a)
              );

    out_col.rgb = ApplySRGBCurve (out_col.rgb);

    out_col.rgb *= out_col.a;
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

    out_col =
      float4 (  RemoveGammaExp (           input_col.rgb *
                              ApplyGammaExp (out_col.rgb, 2.2f), 2.2f),
                                  saturate (  out_col.a)  *
                                  saturate (input_col.a)
              );

    out_col.rgb = ApplySRGBCurve (out_col.rgb);

#endif

    out_col.rgb *= out_col.a;
    
  }

  return out_col;
};

#endif