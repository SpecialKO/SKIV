//
// Copyright 2024 Andon "Kaldaien" Coleman
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

static const uint SKIV_TONEMAP_TYPE_NONE               = 0x0;
static const uint SKIV_TONEMAP_TYPE_CLIP               = 0x1;
static const uint SKIV_TONEMAP_TYPE_INFINITE_ROLLOFF   = 0x2;
static const uint SKIV_TONEMAP_TYPE_NORMALIZE_TO_CLL   = 0x4;
static const uint SKIV_TONEMAP_TYPE_MAP_CLL_TO_DISPLAY = 0x8;

// The HDR-to-SDR tone mappers, see SKIV_SdrToneMapper (sdr_tone_map.h)
static const uint SKIV_SDR_TONEMAP_DEFAULT             = 0;
static const uint SKIV_SDR_TONEMAP_PIECEWISE           = 1;
static const uint SKIV_SDR_TONEMAP_AUTO                = 2;

float TonemapNone (float L)
{
  return L;
}

float TonemapClip (float L, float Ld)
{
  return
    min (L, Ld);
}

float TonemapSDR (float L, float Lc, float Ld)
{
  return
    (L + (1.0f / pow (Lc, 2.0f)) * pow (L, 2.0f)) / (1.0f + L);
}

float TonemapHDR (float L, float Lc, float Ld)
{
  float a = (  Ld / pow (Lc, 2.0f));
  float b = (1.0f / Ld);

  return
    L * (1 + a * L) / (1 + b * L);
}

// SKIV Piecewise / Auto Tone: the shoulder of SKIV_SdrTone::ShoulderCurve (sdr_tone_map.h), of PQ in and out. Its curve
//   is made on the CPU: (knee PQ, peak PQ, offset, scale), from the knee (half of SDR white) to the content's peak.
float MapShoulder (float input, float4 curve)
{
  float p = min (input, curve.y);
  float white = LinearToPQY (1.5f);
  float intensity = pow (abs (p), 0.18f) * TonemapHDR (p, curve.y, white);
  return curve.x + (intensity - curve.z) * curve.w;
}

// SKIV Piecewise / Auto Tone, of scRGB (rgb) to linear SDR (1.0 at 203 nits, SDR white). The CPU side of it is
//   SKIV_Image_ApplyShoulderToneMap (image.cpp), for HDR images saved as SDR.
float3 TonemapSDRWithShoulder (float3 rgb, float brightness, uint mode, float4 curve, float3 auto_tone)
{
  const float sdr_white = 203.0f / 80.0f;
  const float shoulder = 0.5f * sdr_white;
  const float knee_pq = curve.x;

  if (mode == SKIV_SDR_TONEMAP_AUTO) // Luminance mapping after the adaptive midtone curve.
  {
    float y = Rec709_to_XYZ (rgb).y;
    if (auto_tone.x > 0.0f)
    {
      float p = LinearToPQY (min (y, DEFAULT_MAX_PQ));
      float weight = p < auto_tone.y ? smoothstep (0.25f, auto_tone.y, p)
                                    : 1.0f - smoothstep (auto_tone.y, auto_tone.z, p);
      brightness *= exp2 (-auto_tone.x * weight);
    }

    rgb *= brightness;
    y *= brightness;
    if (y > shoulder)
    {
      float mapped = MapShoulder (LinearToPQY (min (y, DEFAULT_MAX_PQ)), curve);
      rgb *= PQToLinear (mapped.xxx, DEFAULT_MAX_PQ).x / y;
    }
  }
  else
  {
    // SKIV Piecewise: leave the lower range intact and retain SKIV's ICtCp chroma scaling.
    rgb *= brightness;
    float3 ictcp = Rec709toICtCp (rgb);
    if (ictcp.x > knee_pq)
    {
      float mapped = MapShoulder (ictcp.x, curve);
      ictcp.yz *= min (mapped / ictcp.x, ictcp.x / mapped);
      ictcp.x = mapped;
      rgb = ICtCptoRec709 (ictcp);
    }
  }

  return saturate (rgb / sdr_white);
}
