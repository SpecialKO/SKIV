#pragma warning ( disable : 3571 )

#define SKIF_Shaders

#ifndef SKIF_Shaders

cbuffer vertexBuffer : register(b0) 
{
  float4x4 ProjectionMatrix;
};
struct VS_INPUT
{
  float2 pos : POSITION;
  float4 col : COLOR0;
  float2 uv  : TEXCOORD0;
};

struct PS_INPUT
{
  float4 pos : SV_POSITION;
  float4 col : COLOR0;
  float2 uv  : TEXCOORD0;
};

PS_INPUT main(VS_INPUT input)
{
  PS_INPUT output;
  output.pos = mul( ProjectionMatrix, float4(input.pos.xy, 0.f, 1.f));
  output.col = input.col;
  output.uv  = input.uv;
  return output;
}

#else

cbuffer vertexBuffer : register (b0)
{
  float4x4 ProjectionMatrix;
  float4   Luminance;
};

struct VS_INPUT
{
  float2 pos : POSITION;
  float2 uv  : TEXCOORD0;
  float4 col : COLOR0; // ImU32
  uint   vI  : SV_VERTEXID;
};

struct PS_INPUT
{
  float4 pos     : SV_POSITION;
  float2 uv      : TEXCOORD0;
  float4 col     : COLOR0; // ImU32
  float4 lum     : COLOR1; // constant_buffer->luminance_scale
  float  hdr_img : COLOR2;
};

PS_INPUT main (VS_INPUT input)
{
  PS_INPUT output;
  
  output.pos  = mul ( ProjectionMatrix,
                        float4 (input.pos.xy, 0.f, 1.f) );
  output.lum = Luminance.xyzw;

  // Reserved texcoords for HDR content passthrough
  if (all (input.uv < -1024.0f))
  {
    output.uv      = float2 (input.vI >= 1 &&
                             input.vI <  3,
                             input.vI >> 1);
    output.col     = float4 (1.0f, 1.0f, 1.0f, 1.0f);
    output.hdr_img = 1.0f;
  }

  else
  {
    output.uv      = input.uv; // Texture coordinates
    output.col     = input.col;
    output.hdr_img = 0.0f;
  }

  return output;
}

#endif