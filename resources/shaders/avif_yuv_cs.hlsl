// The frames of AVIF image sequences (animated images, see src/utility/image_decode.cpp): their YUV planes, as libavif
//   decodes them, made RGBA. The values stay as they are encoded (sRGB, or Rec. 709's curve): the texture written is read
//     as sRGB. On the GPU, as libavif's own conversion takes four times as long as decoding a frame.

Texture2D <float>          planeY       : register (t0);
Texture2D <float>          planeU       : register (t1); // (4:2:0 and 4:2:2: smaller, sampled where each pixel is)
Texture2D <float>          planeV       : register (t2);
Texture2D <float>          planeA       : register (t3);
SamplerState               linearClamp  : register (s0);
RWTexture2D <unorm float4> outputRGBA   : register (u0);

cbuffer constants : register (b0)
{
  float4 range;   // Y' = Y * x + y, Cb and Cr = value * z + w  (the sampled values: their bits, range and offsets)
  float4 coeffs;  // R = Y' + x Cr, G = Y' + y Cb + z Cr, B = Y' + w Cb
  float4 alpha;   // A = value * x + y; z: whether it has alpha, w: whether it is monochrome (no chroma planes)
  uint4  size;    // x, y: the frame's size
};

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

  outputRGBA [id.xy] = saturate (float4 (rgb, a));
}
