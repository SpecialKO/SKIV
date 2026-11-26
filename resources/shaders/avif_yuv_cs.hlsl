// The frames of AVIF image sequences (animated images, see src/utility/image_decode.cpp): their YUV planes, as libavif
//   decodes them, made RGBA. The values stay as they are encoded (sRGB, or Rec. 709's curve): the texture written is read
//     as sRGB. On the GPU, as libavif's own conversion takes four times as long as decoding a frame.
//
//   AVIF_YUV_HDR (avif_yuv_hdr_cs.hlsl): HDR frames (PQ, HLG), made scRGB (half floats) instead, as _HDRToScRGB makes
//     them on the CPU: the signal to display light (1.0: 10,000 nits; HLG for a 1,000-nit display, as _HLGToDisplay),
//       then its primaries to scRGB's (toScRGB, which includes the scale of 125: 10,000 nits over 80).

Texture2D <float>          planeY       : register (t0);
Texture2D <float>          planeU       : register (t1); // (4:2:0 and 4:2:2: smaller, sampled where each pixel is)
Texture2D <float>          planeV       : register (t2);
Texture2D <float>          planeA       : register (t3);
SamplerState               linearClamp  : register (s0);
#ifdef AVIF_YUV_HDR
RWTexture2D <float4>       outputRGBA   : register (u0);
#else
RWTexture2D <unorm float4> outputRGBA   : register (u0);
#endif

cbuffer constants : register (b0)
{
  float4 range;   // Y' = Y * x + y, Cb and Cr = value * z + w  (the sampled values: their bits, range and offsets)
  float4 coeffs;  // R = Y' + x Cr, G = Y' + y Cb + z Cr, B = Y' + w Cb
  float4 alpha;   // A = value * x + y; z: whether it has alpha, w: whether it is monochrome (no chroma planes)
  uint4  size;    // x, y: the frame's size
  float4 hdr;     // HDR: x: 1 if HLG (else PQ); yzw: the luminance of its primaries (HLG's OOTF)
  float4 toScRGB [3]; // HDR: the rows of the matrix from its primaries to scRGB's (a row vector times it)
};

#ifdef AVIF_YUV_HDR
// PQ (SMPTE ST 2084) to display light, 1.0 being 10,000 nits (SKIV_Image_PQToLinear)
float3 PQToLinear (float3 n)
{
  const float m1 = 2610.0 / 4096.0 / 4.0,
              m2 = 2523.0 / 4096.0 * 128.0,
              c1 = 3424.0 / 4096.0,
              c2 = 2413.0 / 4096.0 * 32.0,
              c3 = 2392.0 / 4096.0 * 32.0;

  const float3 p = pow (max (n, 0.0f), 1.0f / m2);

  return pow (max (p - c1, 0.0f) / (c2 - c3 * p), 1.0f / m1);
}

// HLG (BT.2100) to display light for a 1,000-nit display, 1.0 being 10,000 nits (_HLGToDisplay)
float3 HLGToDisplay (float3 e)
{
  const float a = 0.17883277f,
              b = 0.28466892f,
              c = 0.55991073f;

  e = max (e, 0.0f);

  const float3 scene = (e <= 0.5f) ? (e * e / 3.0f) : ((exp ((e - c) / a) + b) / 12.0f);
  const float  y     = dot (hdr.yzw, scene);

  return (y > 0.0f) ? scene * (0.1f * pow (y, 0.2f)) : 0.0f;
}
#endif

[numthreads (8, 8, 1)]
void main (uint3 id : SV_DispatchThreadID)
{
  if (id.x >= size.x || id.y >= size.y)
    return;

  const float luma = planeY.Load (int3 (id.xy, 0)) * range.x + range.y;

  float cb = 0.0f,
        cr = 0.0f;

  if (alpha.w == 0.0f)
  {
    const float2 uv = (float2 (id.xy) + 0.5f) / float2 (size.xy);

    cb = planeU.SampleLevel (linearClamp, uv, 0) * range.z + range.w;
    cr = planeV.SampleLevel (linearClamp, uv, 0) * range.z + range.w;
  }

  const float3 rgb = float3 (luma + coeffs.x * cr,
                             luma + coeffs.y * cb + coeffs.z * cr,
                             luma + coeffs.w * cb);

  const float a = (alpha.z != 0.0f) ? planeA.Load (int3 (id.xy, 0)) * alpha.x + alpha.y
                                    : 1.0f;

#ifdef AVIF_YUV_HDR
  const float3 light = (hdr.x != 0.0f) ? HLGToDisplay (saturate (rgb)) : PQToLinear (saturate (rgb));

  outputRGBA [id.xy] = float4 (light.r * toScRGB [0].xyz + light.g * toScRGB [1].xyz + light.b * toScRGB [2].xyz, saturate (a));
#else
  outputRGBA [id.xy] = saturate (float4 (rgb, a));
#endif
}
