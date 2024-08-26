#include "utility/image.h"
#include "utility/registry.h"
#include <cstdint>
#include <string_view>
#include <plog/Log.h>
#include <strsafe.h>
#include <wincodec.h>
#include <ShlObj_core.h>
#include <utility/fsutil.h>
#include <dxgi1_5.h>
#include <dxgi1_6.h>
#include <Shlwapi.h>
#include <windowsx.h>
#include <ImGuiNotify.hpp>
#include <utility/skif_imgui.h>
#include <utility/utility.h>
#include "DirectXTex.h"
#include <utility/DirectXTexEXR.h>

#include <jxl/codestream_header.h>
#include <jxl/encode.h>
#include <jxl/encode_cxx.h>
#include <jxl/resizable_parallel_runner.h>
#include <jxl/resizable_parallel_runner_cxx.h>
#include <jxl/thread_parallel_runner.h>
#include <jxl/thread_parallel_runner_cxx.h>
#include <jxl/types.h>

#include <DirectXPackedVector.h>

skiv_image_desktop_s SKIV_DesktopImage;

extern std::wstring defaultHDRFileExt;
extern std::wstring defaultSDRFileExt;
extern CComPtr <ID3D11Device> SKIF_D3D11_GetDevice (bool bWait = true);

DirectX::XMVECTOR
SKIV_Image_PQToLinear (DirectX::XMVECTOR N, DirectX::XMVECTOR maxPQValue)
{
using namespace DirectX;

  XMVECTOR ret;

  XMVECTOR sign =
    XMVectorSet (XMVectorGetX (N) < 0.0f ? -1.0f : 1.0f,
                 XMVectorGetY (N) < 0.0f ? -1.0f : 1.0f,
                 XMVectorGetZ (N) < 0.0f ? -1.0f : 1.0f, 1.0f);

  ret =
    XMVectorPow (XMVectorAbs (N), XMVectorDivide (g_XMOne, PQ.M));

  XMVECTOR nd;

  nd =
    XMVectorDivide (
      XMVectorMax (XMVectorSubtract (ret, PQ.C1), g_XMZero),
                   XMVectorSubtract (     PQ.C2,
            XMVectorMultiply (PQ.C3, ret)));

  ret =
    XMVectorMultiply (XMVectorMultiply (XMVectorPow (nd, XMVectorDivide (g_XMOne, PQ.N)), maxPQValue), sign);

  return ret;
};

DirectX::XMVECTOR
SKIV_Image_LinearToPQ (DirectX::XMVECTOR N, DirectX::XMVECTOR maxPQValue)
{
  using namespace DirectX;

  XMVECTOR ret;
  XMVECTOR sign =
    XMVectorSet (XMVectorGetX (N) < 0.0f ? -1.0f : 1.0f,
                 XMVectorGetY (N) < 0.0f ? -1.0f : 1.0f,
                 XMVectorGetZ (N) < 0.0f ? -1.0f : 1.0f, 1.0f);

  ret =
    XMVectorPow (XMVectorDivide (XMVectorAbs (N), maxPQValue), PQ.N);

  XMVECTOR nd =
    XMVectorDivide (
       XMVectorAdd (  PQ.C1, XMVectorMultiply (PQ.C2, ret)),
       XMVectorAdd (g_XMOne, XMVectorMultiply (PQ.C3, ret))
    );

  return
    XMVectorMultiply (XMVectorPow (nd, PQ.M), sign);
};

float
SKIV_Image_LinearToPQY (float N)
{
  const float fScaledN =
    fabs (N * 0.008f); // 0.008 = 1/125.0

  float ret =
    pow (fScaledN, 0.1593017578125f);

  float nd =
    fabs ( (0.8359375f + (18.8515625f * ret)) /
           (1.0f       + (18.6875f    * ret)) );

  return
    pow (nd, 78.84375f);
};

DirectX::XMVECTOR
SKIV_Image_Rec709toICtCp (DirectX::XMVECTOR N)
{
  using namespace DirectX;

  XMVECTOR ret = N;

  ret = XMVector3Transform (ret, c_from709toXYZ);
  ret = XMVector3Transform (ret, c_fromXYZtoLMS);

  ret =
    SKIV_Image_LinearToPQ (XMVectorMax (ret, g_XMZero), XMVectorReplicate (125.0f));

  static const DirectX::XMMATRIX ConvMat = // Transposed
  {
    { 0.5000f,  1.6137f,  4.3780f, 0.0f },
    { 0.5000f, -3.3234f, -4.2455f, 0.0f },
    { 0.0000f,  1.7097f, -0.1325f, 0.0f },
    { 0.0f,     0.0f,     0.0f,    1.0f }
  };

  return
    XMVector3Transform (ret, ConvMat);
};

DirectX::XMVECTOR
SKIV_Image_ICtCptoRec709 (DirectX::XMVECTOR N)
{
  using namespace DirectX;

  XMVECTOR ret = N;

#pragma warning( push )
#pragma warning( disable : 4305 )

  static const DirectX::XMMATRIX ConvMat = // Transposed
  {
    { 1.0,                  1.0,                  1.0,                 0.0f },
    { 0.00860514569398152, -0.00860514569398152,  0.56004885956263900, 0.0f },
    { 0.11103560447547328, -0.11103560447547328, -0.32063747023212210, 0.0f },
    { 0.0f,                 0.0f,                 0.0f,                1.0f }
  };

#pragma warning( pop ) 

  ret =
    XMVector3Transform (ret, ConvMat);

  ret = SKIV_Image_PQToLinear (ret, XMVectorReplicate (125.0f));
  ret = XMVector3Transform (ret, c_fromLMStoXYZ);

  return
    XMVector3Transform (ret, c_fromXYZto709);
};

static uint32_t
png_crc32 (const void* typeless_data, size_t offset, size_t len, uint32_t crc)
{
  auto data =
    (const BYTE *)typeless_data;

  uint32_t c;

  static uint32_t
      png_crc_table [256] = { };
  if (png_crc_table [ 0 ] == 0)
  {
    for (auto i = 0 ; i < 256 ; ++i)
    {
      c = i;

      for (auto j = 0 ; j < 8 ; ++j)
      {
        if ((c & 1) == 1)
          c = (0xEDB88320 ^ ((c >> 1) & 0x7FFFFFFF));
        else
          c = ((c >> 1) & 0x7FFFFFFF);
      }

      png_crc_table [i] = c;
    }
  }

  c =
    (crc ^ 0xffffffff);

  for (auto k = offset ; k < (offset + len) ; ++k)
  {
    c =
      png_crc_table [(c ^ data [k]) & 255] ^
                    ((c >> 8)       & 0xFFFFFF);
  }
  
  return
    (c ^ 0xffffffff);
}

/* This is for compression type. PNG 1.0-1.2 only define the single type. */
constexpr uint8_t PNG_COMPRESSION_TYPE_BASE = 0; /* Deflate method 8, 32K window */
#define PNG_COMPRESSION_TYPE_DEFAULT PNG_COMPRESSION_TYPE_BASE

//
// To convert an image passed to an encoder that does not understand HDR,
//   but that we actually fed HDR pixels to... perform the following:
//
//  1. Remove gAMA chunk  (Prevents SKIV from recognizing as HDR)
//  2. Remove sRGB chunk  (Prevents Discord from rendering in HDR)
//
//  3. Add cICP  (The primary way of defining HDR10) 
//  4. Add iCCP  (Required for Discord to render in HDR)
//
//  (5) Add cLLi  [Unnecessary, but probably a good idea]
//  (6) Add cHRM  [Unnecessary, but probably a good idea]
//

#if (defined _M_IX86) || (defined _M_X64)
# define SK_PNG_GetUint32(x)                    _byteswap_ulong (x)
# define SK_PNG_SetUint32(x,y)              x = _byteswap_ulong (y)
# define SK_PNG_DeclareUint32(x,y) uint32_t x = SK_PNG_SetUint32((x),(y))
#else
# define SK_PNG_GetUint32(x)                    (x)
# define SK_PNG_SetUint32(x,y)              x = (y)
# define SK_PNG_DeclareUint32(x,y) uint32_t x = SK_PNG_SetUint32((x),(y))
#endif

struct SK_PNG_HDR_cHRM_Payload
{
  SK_PNG_DeclareUint32 (white_x, 31270);
  SK_PNG_DeclareUint32 (white_y, 32900);
  SK_PNG_DeclareUint32 (red_x,   70800);
  SK_PNG_DeclareUint32 (red_y,   29200);
  SK_PNG_DeclareUint32 (green_x, 17000);
  SK_PNG_DeclareUint32 (green_y, 79700);
  SK_PNG_DeclareUint32 (blue_x,  13100);
  SK_PNG_DeclareUint32 (blue_y,  04600);
};

struct SK_PNG_HDR_sBIT_Payload
{
  uint8_t red_bits   = 10; // 12 if source was scRGB (compression optimization)
  uint8_t green_bits = 10; // 12 if source was scRGB (compression optimization)
  uint8_t blue_bits  = 10; // 12 if source was scRGB (compression optimization)
};

struct SK_PNG_HDR_mDCv_Payload
{
  struct {
    SK_PNG_DeclareUint32 (red_x,   35400); // 0.708 / 0.00002
    SK_PNG_DeclareUint32 (red_y,   14600); // 0.292 / 0.00002
    SK_PNG_DeclareUint32 (green_x,  8500); // 0.17  / 0.00002
    SK_PNG_DeclareUint32 (green_y, 39850); // 0.797 / 0.00002
    SK_PNG_DeclareUint32 (blue_x,   6550); // 0.131 / 0.00002
    SK_PNG_DeclareUint32 (blue_y,   2300); // 0.046 / 0.00002
  } primaries;

  struct {
    SK_PNG_DeclareUint32 (x, 15635); // 0.3127 / 0.00002
    SK_PNG_DeclareUint32 (y, 16450); // 0.3290 / 0.00002
  } white_point;

  // The only real data we need to fill-in
  struct {
    SK_PNG_DeclareUint32 (maximum, 10000000); // 1000.0 cd/m^2
    SK_PNG_DeclareUint32 (minimum, 1);        // 0.0001 cd/m^2
  } luminance;
};

struct SK_PNG_HDR_cLLi_Payload
{
  SK_PNG_DeclareUint32 (max_cll,  10000000); // 1000 / 0.0001
  SK_PNG_DeclareUint32 (max_fall,  2500000); //  250 / 0.0001
};

//
// ICC Profile for tonemapping comes courtesy of ledoge
//
//   https://github.com/ledoge/jxr_to_png
//
struct SK_PNG_HDR_iCCP_Payload
{
  char          profile_name [20]   = "RGB_D65_202_Rel_PeQ";
  uint8_t       compression_type    = PNG_COMPRESSION_TYPE_DEFAULT;

  unsigned char profile_data [2178] = {
	0x78, 0x9C, 0xED, 0x97, 0x79, 0x58, 0x13, 0x67, 0x1E, 0xC7, 0x47, 0x50,
	0x59, 0x95, 0x2A, 0xAC, 0xED, 0xB6, 0x8B, 0xA8, 0x54, 0x20, 0x20, 0x42,
	0xE5, 0xF4, 0x00, 0x51, 0x40, 0x05, 0xAF, 0x6A, 0x04, 0x51, 0x6E, 0x84,
	0x70, 0xAF, 0x20, 0x24, 0xDC, 0x87, 0x0C, 0xA8, 0x88, 0x20, 0x09, 0x90,
	0x04, 0x12, 0x24, 0x24, 0x90, 0x03, 0x82, 0xA0, 0x41, 0x08, 0x24, 0x41,
	0x2E, 0x21, 0x01, 0x12, 0x83, 0x4A, 0x10, 0xA9, 0x56, 0xB7, 0x8A, 0xE0,
	0xAD, 0x21, 0xE0, 0xB1, 0x6B, 0x31, 0x3B, 0x49, 0x74, 0x09, 0x6D, 0xD7,
	0x3E, 0xCF, 0x3E, 0xFD, 0xAF, 0x4E, 0x3E, 0xF3, 0xBC, 0xBF, 0x79, 0xBF,
	0xEF, 0xBC, 0x33, 0x9F, 0xC9, 0xFC, 0x31, 0x2F, 0x00, 0xE8, 0xBC, 0x8D,
	0x4A, 0x3E, 0x62, 0x30, 0xD7, 0x09, 0x00, 0xA2, 0x63, 0xE2, 0x91, 0xEE,
	0x6E, 0x2E, 0x06, 0x7B, 0x82, 0x82, 0x0D, 0xB4, 0x46, 0x01, 0x6D, 0x60,
	0x0E, 0xA0, 0xDC, 0x82, 0x10, 0xA8, 0x58, 0x67, 0x38, 0x7C, 0x8F, 0xEA,
	0xE8, 0x57, 0x1B, 0x34, 0xEA, 0xF5, 0xB0, 0x6A, 0xAC, 0xC4, 0x42, 0x31,
	0xD7, 0xF2, 0x84, 0x9D, 0x68, 0xBD, 0xA9, 0xD6, 0x43, 0xEB, 0x16, 0xE5,
	0xBD, 0xFC, 0xC6, 0xD2, 0xFC, 0xF8, 0xFF, 0x38, 0xEF, 0xE3, 0xB6, 0x30,
	0x24, 0x14, 0x85, 0x80, 0xDA, 0x9F, 0xA1, 0x7D, 0x1B, 0x22, 0x16, 0x19,
	0x0F, 0x4D, 0xE9, 0x04, 0xD5, 0x46, 0x49, 0xF1, 0xB1, 0x8A, 0x3A, 0x04,
	0xAA, 0xBF, 0x44, 0x44, 0x04, 0x41, 0xED, 0x9C, 0x64, 0xA8, 0x36, 0x47,
	0x44, 0x22, 0x62, 0xA1, 0x9A, 0x06, 0xD5, 0xDA, 0x48, 0x2F, 0x6F, 0x1F,
	0xA8, 0x66, 0x29, 0xC6, 0x84, 0xAB, 0xEA, 0x1E, 0x45, 0x1D, 0xAC, 0xAA,
	0x47, 0x14, 0xB5, 0xB3, 0xB5, 0x8B, 0x25, 0x54, 0x3F, 0x03, 0x80, 0xC5,
	0x97, 0x5C, 0xAC, 0x9D, 0xA1, 0x5A, 0xA7, 0x06, 0xEA, 0x87, 0x47, 0x1F,
	0x49, 0x50, 0x5C, 0xF7, 0x83, 0x03, 0xA0, 0x1D, 0x1A, 0xE3, 0xE9, 0x01,
	0xB5, 0x30, 0x68, 0xD7, 0x07, 0xDC, 0x01, 0x37, 0xC0, 0x05, 0x08, 0x04,
	0xB6, 0x01, 0xEB, 0x00, 0x3B, 0xA8, 0xB5, 0x06, 0x2C, 0xA1, 0x3D, 0x10,
	0xEA, 0x0F, 0x05, 0x8E, 0x40, 0x2D, 0x1C, 0x6A, 0xF7, 0x43, 0xCF, 0xEC,
	0xB7, 0xE7, 0x98, 0xAF, 0x9C, 0x63, 0x2B, 0xF4, 0x83, 0xAE, 0x06, 0xDD,
	0x8A, 0x81, 0x6A, 0xC8, 0xCC, 0x73, 0x42, 0x85, 0xD9, 0x58, 0xAB, 0xCE,
	0xD2, 0x86, 0x5C, 0xE7, 0xDD, 0x91, 0xCB, 0x27, 0xCD, 0x00, 0x40, 0xAB,
	0x18, 0x00, 0xA6, 0x0B, 0xE5, 0xF2, 0x77, 0x54, 0xB9, 0x7C, 0x9A, 0x0A,
	0x00, 0x9A, 0xB7, 0x01, 0xA0, 0x33, 0x4B, 0xE5, 0x0B, 0x00, 0x0B, 0x74,
	0x80, 0x39, 0x33, 0x73, 0xD5, 0x45, 0x00, 0x80, 0xDB, 0x51, 0xB9, 0x5C,
	0x9E, 0x3D, 0xD3, 0x67, 0x16, 0x09, 0xF5, 0x8F, 0x42, 0xF3, 0xD4, 0xCF,
	0xF4, 0x19, 0x68, 0x01, 0xC0, 0xA2, 0xF3, 0x00, 0x70, 0x65, 0x69, 0x74,
	0x58, 0xBC, 0x95, 0xA2, 0x47, 0x53, 0x73, 0x81, 0xEA, 0x6E, 0x7F, 0xF1,
	0x2F, 0xFE, 0xEA, 0x78, 0x8E, 0x86, 0xE6, 0xDC, 0x79, 0xF3, 0xB5, 0xFE,
	0xB2, 0x60, 0xE1, 0x22, 0xED, 0x2F, 0x16, 0x2F, 0xD1, 0xD1, 0xFD, 0xEB,
	0xD2, 0x2F, 0xBF, 0xFA, 0xDB, 0xD7, 0xDF, 0xFC, 0x5D, 0x6F, 0x99, 0xFE,
	0xF2, 0x15, 0x2B, 0x0D, 0xBE, 0x5D, 0x65, 0x68, 0x64, 0x0C, 0x33, 0x31,
	0x5D, 0x6D, 0xB6, 0xC6, 0xDC, 0xE2, 0xBB, 0xB5, 0x96, 0x56, 0xD6, 0x36,
	0xB6, 0x76, 0xEB, 0xD6, 0x6F, 0xD8, 0x68, 0xEF, 0xB0, 0xC9, 0x71, 0xF3,
	0x16, 0x27, 0x67, 0x97, 0xAD, 0xDB, 0xB6, 0xBB, 0xBA, 0xED, 0xD8, 0xB9,
	0x6B, 0xF7, 0x9E, 0xEF, 0xF7, 0xEE, 0x83, 0xEF, 0x77, 0xF7, 0x38, 0xE0,
	0x79, 0xF0, 0x10, 0x74, 0x6F, 0xBE, 0x7E, 0xFE, 0x01, 0x81, 0x87, 0x83,
	0x82, 0x11, 0x21, 0xA1, 0x61, 0xE1, 0x11, 0x91, 0x51, 0xFF, 0x38, 0x12,
	0x1D, 0x73, 0x34, 0x36, 0x0E, 0x89, 0x8A, 0x4F, 0x48, 0x4C, 0x4A, 0x4E,
	0x49, 0x4D, 0x4B, 0xCF, 0x38, 0x96, 0x09, 0x66, 0x65, 0x1F, 0x3F, 0x71,
	0x32, 0xE7, 0x54, 0xEE, 0xE9, 0xBC, 0xFC, 0x33, 0x05, 0x68, 0x4C, 0x61,
	0x51, 0x31, 0x16, 0x87, 0x2F, 0x29, 0x25, 0x10, 0xCB, 0xCE, 0x96, 0x93,
	0x2A, 0xC8, 0x94, 0xCA, 0x2A, 0x2A, 0x8D, 0xCE, 0xA8, 0xAE, 0x61, 0xD6,
	0x9E, 0xAB, 0xAB, 0x3F, 0x7F, 0x81, 0xD5, 0x70, 0xB1, 0xB1, 0x89, 0xDD,
	0xDC, 0xC2, 0xE1, 0xF2, 0x5A, 0x2F, 0xB5, 0xB5, 0x77, 0x74, 0x76, 0x5D,
	0xEE, 0xEE, 0xE1, 0x0B, 0x7A, 0xFB, 0xFA, 0x85, 0xA2, 0x2B, 0xE2, 0x81,
	0xAB, 0xD7, 0xAE, 0x0F, 0x4A, 0x86, 0x6E, 0x0C, 0xDF, 0x1C, 0xF9, 0xE1,
	0xD6, 0xED, 0x1F, 0xEF, 0xDC, 0xFD, 0xE7, 0x4F, 0xF7, 0xEE, 0x8F, 0x3E,
	0x18, 0x1B, 0x7F, 0xF8, 0xE8, 0xF1, 0x93, 0xA7, 0xCF, 0x9E, 0xBF, 0x78,
	0x29, 0x9D, 0x90, 0x4D, 0x4E, 0xBD, 0x7A, 0xFD, 0xE6, 0xED, 0xBF, 0xFE,
	0xFD, 0xEE, 0xE7, 0xE9, 0xF7, 0xF2, 0xCF, 0xFE, 0x7F, 0x72, 0x7F, 0x10,
	0x04, 0xB2, 0x32, 0x34, 0x4E, 0x85, 0x2F, 0xAC, 0x70, 0x33, 0x64, 0x1B,
	0xEF, 0xE8, 0x99, 0x1F, 0x7A, 0x41, 0x2F, 0xAE, 0x66, 0x55, 0x22, 0x1D,
	0x36, 0x37, 0x2D, 0x7B, 0x6E, 0x7A, 0xE6, 0xFC, 0xEC, 0xC8, 0xC5, 0xC4,
	0x5D, 0xC6, 0x17, 0x4D, 0x76, 0x76, 0x6B, 0x41, 0x11, 0x52, 0x19, 0xAD,
	0xF0, 0xC1, 0xAE, 0xF4, 0x2D, 0x34, 0x08, 0x4E, 0x35, 0x4E, 0xF3, 0xB6,
	0x25, 0x59, 0xC1, 0xB9, 0xDA, 0x11, 0x75, 0xFA, 0xC8, 0x6A, 0xC3, 0x24,
	0x3A, 0x6C, 0xDF, 0x3A, 0xD6, 0xBE, 0xF5, 0x75, 0x70, 0x07, 0x82, 0xFB,
	0x9E, 0x44, 0xAF, 0xA3, 0x3B, 0x23, 0xCE, 0xEA, 0xC7, 0x51, 0x57, 0x25,
	0xD2, 0x8C, 0x93, 0x69, 0x26, 0xE8, 0x25, 0x62, 0xF4, 0x12, 0x21, 0x5A,
	0xF7, 0x12, 0x46, 0x8F, 0x5C, 0x64, 0x15, 0x87, 0x0F, 0xB1, 0xCF, 0x43,
	0xDB, 0x80, 0xE5, 0xE6, 0x69, 0x95, 0xAB, 0xF9, 0xC0, 0x03, 0x3E, 0x30,
	0xCA, 0x07, 0x7E, 0xE4, 0x03, 0x7D, 0x02, 0x80, 0xD6, 0xAB, 0x17, 0xCD,
	0x8E, 0xD9, 0x57, 0x96, 0xE7, 0x98, 0x43, 0xB4, 0x1C, 0x33, 0x6C, 0x52,
	0xD2, 0x38, 0x66, 0xD8, 0x30, 0x66, 0x54, 0x3B, 0x6E, 0x4A, 0x78, 0x68,
	0x1D, 0xDB, 0x83, 0xF2, 0x26, 0xE7, 0x39, 0x49, 0xF7, 0x13, 0x3F, 0x42,
	0x90, 0xEE, 0x2F, 0x95, 0xBA, 0xE3, 0xA5, 0x07, 0xCE, 0xC8, 0x7C, 0x93,
	0xFA, 0xE2, 0xFD, 0x64, 0xDE, 0xB8, 0x59, 0xF8, 0x60, 0x65, 0x3E, 0x45,
	0x93, 0x7E, 0x79, 0xAF, 0x10, 0x29, 0x1A, 0x27, 0xB2, 0x34, 0x4E, 0x1E,
	0x9B, 0x9B, 0x1F, 0xA1, 0x5D, 0xB9, 0xC3, 0xA8, 0x19, 0xB6, 0x83, 0xAF,
	0xF0, 0x52, 0x29, 0xCF, 0xCB, 0x3C, 0x3E, 0x0F, 0x04, 0xB5, 0x72, 0xA2,
	0x74, 0xCA, 0x77, 0xC3, 0x2E, 0x9A, 0xAA, 0x2B, 0xAF, 0x0C, 0xC4, 0x19,
	0x1C, 0x2E, 0xFA, 0x36, 0x3C, 0x0D, 0x96, 0xE1, 0x63, 0x57, 0x61, 0x05,
	0xE7, 0xA9, 0x29, 0x6F, 0x64, 0xED, 0xB3, 0xAF, 0x87, 0x6F, 0x26, 0xB8,
	0xEF, 0x4D, 0xF2, 0x8E, 0x9D, 0xAD, 0xAC, 0x23, 0x46, 0xEB, 0x0A, 0xD1,
	0x4B, 0xDB, 0x30, 0xCB, 0xC8, 0x45, 0xD6, 0xC8, 0x12, 0xA5, 0x72, 0x56,
	0xB9, 0x79, 0xFA, 0x27, 0x94, 0xCB, 0xFE, 0x78, 0xE5, 0x2F, 0x2A, 0x4E,
	0x2F, 0x26, 0xE7, 0x2C, 0xA9, 0x8A, 0xFD, 0xBA, 0x16, 0xBE, 0x86, 0xBB,
	0x66, 0xB7, 0x60, 0x41, 0x18, 0x6B, 0x19, 0xB2, 0xC6, 0x10, 0xF2, 0xD2,
	0x25, 0xE6, 0xEB, 0x96, 0xE5, 0x2E, 0x2D, 0x47, 0x2E, 0xA3, 0xBB, 0x5B,
	0x34, 0x9B, 0xEF, 0xE1, 0x2F, 0x08, 0xBB, 0xF0, 0x21, 0x32, 0x49, 0x27,
	0x9A, 0xA4, 0x97, 0x98, 0x82, 0xA0, 0xC5, 0x99, 0x80, 0x8D, 0x34, 0x5B,
	0x8F, 0xD6, 0xC5, 0x91, 0xF5, 0xFA, 0x28, 0xA5, 0xB2, 0xFB, 0xF7, 0x17,
	0xDD, 0xF7, 0x5E, 0xF0, 0xD8, 0x5F, 0xE6, 0xE9, 0x97, 0xE2, 0x9B, 0xB4,
	0x3B, 0xAA, 0x62, 0x39, 0x92, 0x66, 0x98, 0x48, 0x83, 0x41, 0xCA, 0x18,
	0xBD, 0x01, 0xCC, 0x32, 0x11, 0x46, 0xBF, 0xAD, 0xD0, 0x88, 0x52, 0xBC,
	0x01, 0x59, 0x12, 0xEE, 0x90, 0x8F, 0x81, 0x94, 0x2D, 0xD4, 0x94, 0xEF,
	0xF0, 0x81, 0x7E, 0xC1, 0x1C, 0x5A, 0xEF, 0xF2, 0x98, 0xE6, 0xA3, 0xF0,
	0xDF, 0x50, 0x36, 0x3E, 0xF7, 0x69, 0xE5, 0x09, 0xCF, 0xDF, 0x57, 0xB6,
	0x6C, 0xAE, 0xB4, 0x6A, 0xAE, 0xB0, 0x6A, 0x39, 0x65, 0xC7, 0x0B, 0x71,
	0xEA, 0xDE, 0x78, 0x48, 0xA4, 0x1B, 0xD5, 0xB0, 0x1C, 0x55, 0x63, 0x94,
	0xC4, 0x80, 0x59, 0x37, 0x56, 0x59, 0x37, 0x91, 0x6D, 0x9A, 0x72, 0xD7,
	0x73, 0x42, 0x9D, 0x2F, 0xDB, 0x7B, 0x09, 0xA1, 0x68, 0x85, 0x2A, 0x72,
	0xA4, 0x32, 0x37, 0x53, 0xE9, 0x9B, 0xE9, 0x18, 0x67, 0xE6, 0x91, 0x5D,
	0x6C, 0x27, 0xFF, 0xCB, 0x5F, 0x45, 0x9F, 0x5F, 0x19, 0x0F, 0x45, 0x74,
	0x58, 0x40, 0x0A, 0x2F, 0x20, 0xA5, 0x25, 0x20, 0xAD, 0xEA, 0x30, 0x08,
	0x86, 0x62, 0xDC, 0x15, 0x2F, 0x0C, 0xC3, 0x18, 0xEA, 0x87, 0x94, 0xB1,
	0x0E, 0xD7, 0xB1, 0x0E, 0x03, 0xD8, 0x4D, 0x5D, 0x38, 0x67, 0x6A, 0x09,
	0x3C, 0xB1, 0x0C, 0xE5, 0x58, 0x50, 0x64, 0x97, 0x4D, 0xB2, 0x48, 0xAF,
	0x5A, 0x2D, 0xD0, 0x18, 0x13, 0x68, 0x3C, 0x10, 0x68, 0xDE, 0xED, 0x9D,
	0x2F, 0xEC, 0x5D, 0x42, 0xEF, 0x33, 0x3D, 0xDA, 0x12, 0x07, 0x2F, 0xCB,
	0xDF, 0x7C, 0x0A, 0x52, 0x36, 0x66, 0x8F, 0x19, 0x37, 0x29, 0xB9, 0x38,
	0x0E, 0x3B, 0x37, 0x6E, 0x46, 0x7C, 0x64, 0xAB, 0x52, 0x76, 0x9E, 0xA5,
	0xEC, 0xFE, 0x51, 0xD9, 0x2F, 0xA9, 0x2F, 0x61, 0xB6, 0xB2, 0xCF, 0x2C,
	0xE5, 0xE0, 0xA1, 0xEE, 0xE0, 0xA1, 0xCE, 0xE0, 0x21, 0x66, 0xC8, 0xF0,
	0x89, 0xC8, 0x5B, 0x9E, 0xF1, 0x23, 0x46, 0x49, 0x6C, 0x58, 0x72, 0xAD,
	0x49, 0x32, 0xC3, 0x04, 0x21, 0xE9, 0x41, 0x48, 0x3A, 0x11, 0x12, 0x66,
	0xE8, 0x8D, 0x93, 0x51, 0x3F, 0x78, 0x26, 0x8C, 0x18, 0xFF, 0x37, 0x8A,
	0x10, 0x09, 0x22, 0x44, 0xDD, 0x11, 0x57, 0xEA, 0xA2, 0xC4, 0xB9, 0x31,
	0x83, 0x5E, 0xC9, 0x83, 0x26, 0x29, 0x8D, 0x26, 0x29, 0x4C, 0x93, 0x14,
	0x86, 0x49, 0x1A, 0xEB, 0x6A, 0x1A, 0x4B, 0x94, 0xD6, 0xD0, 0x9C, 0xDE,
	0x88, 0xCD, 0xE4, 0x86, 0xE4, 0x74, 0x5A, 0x82, 0x75, 0xE6, 0xE9, 0x8C,
	0xD5, 0xA9, 0x74, 0x53, 0x72, 0xE2, 0x6D, 0x72, 0xE2, 0x08, 0x39, 0x51,
	0x48, 0x49, 0xA9, 0xAF, 0x04, 0x41, 0x3A, 0x76, 0x3B, 0x8E, 0x68, 0x9F,
	0x53, 0x61, 0x99, 0x51, 0x65, 0x26, 0x34, 0x7F, 0x2C, 0x34, 0x7F, 0x24,
	0x34, 0xBF, 0x27, 0xFC, 0x4E, 0x2C, 0xB4, 0x65, 0x8A, 0x5C, 0x51, 0xBC,
	0x64, 0x0F, 0x52, 0xC1, 0x96, 0xDC, 0x32, 0xAB, 0x87, 0x6B, 0x5A, 0x3E,
	0xC2, 0x7E, 0x68, 0x7E, 0xFE, 0xE1, 0xDA, 0xB3, 0x8F, 0x37, 0xC4, 0xF1,
	0x13, 0x7C, 0x28, 0xF9, 0xCE, 0x52, 0x0F, 0xA2, 0x1A, 0x04, 0xE9, 0x01,
	0xFC, 0xC4, 0xC1, 0x82, 0x49, 0xFF, 0x64, 0x85, 0xB2, 0x42, 0x53, 0x1D,
	0xAC, 0xCC, 0xB7, 0x68, 0xD2, 0x5F, 0xA1, 0x4C, 0x92, 0x8A, 0x49, 0x52,
	0x11, 0x49, 0x7A, 0x99, 0x24, 0xAD, 0x25, 0x4F, 0x66, 0xD1, 0xDE, 0x6D,
	0xC7, 0x76, 0x6C, 0x3C, 0x59, 0xBF, 0x36, 0xA3, 0xDA, 0x8C, 0xF4, 0x52,
	0x4C, 0x7A, 0x79, 0x85, 0xF4, 0xF2, 0x72, 0x85, 0x32, 0xA2, 0xAB, 0x45,
	0xE4, 0x67, 0x57, 0xC9, 0xCF, 0xC4, 0xE4, 0xE7, 0x3D, 0xE4, 0xE7, 0x75,
	0x95, 0xD2, 0x6C, 0xC6, 0x5B, 0x57, 0x5C, 0xBB, 0xFD, 0xC9, 0x7A, 0x4B,
	0x28, 0x62, 0xDC, 0xBB, 0xC5, 0xB8, 0x37, 0xC2, 0xB8, 0x2F, 0xAE, 0x1E,
	0x6D, 0xAC, 0x19, 0xCF, 0xAD, 0x7B, 0xB1, 0x9B, 0xC8, 0x71, 0xCC, 0xAD,
	0xB5, 0x3A, 0xC6, 0x58, 0xC3, 0x69, 0x93, 0x72, 0xDA, 0x5E, 0x70, 0xDA,
	0x7E, 0xE2, 0xB4, 0x77, 0x73, 0x3B, 0x09, 0xAD, 0x7D, 0xFE, 0xD5, 0x4C,
	0xD7, 0x42, 0xEA, 0xFA, 0x6C, 0xAA, 0x85, 0x04, 0x25, 0x93, 0xA0, 0x26,
	0x24, 0xA8, 0x27, 0x92, 0xF8, 0x9B, 0x92, 0xA4, 0xA6, 0x21, 0x10, 0xEC,
	0xC9, 0xF7, 0xA6, 0x61, 0xB7, 0xE5, 0x97, 0xDB, 0x3E, 0x71, 0xE8, 0x51,
	0xD2, 0xFD, 0x64, 0x53, 0xD7, 0x53, 0x47, 0xCE, 0xD3, 0x2D, 0xD4, 0x67,
	0xAE, 0xA8, 0xFE, 0x34, 0xBF, 0xAA, 0x82, 0xAD, 0x13, 0x07, 0x89, 0x33,
	0x1C, 0x22, 0x4C, 0x1C, 0xC2, 0xCB, 0x7C, 0x0A, 0xA6, 0x0E, 0x27, 0xF7,
	0x27, 0xF9, 0xCB, 0x7C, 0x71, 0xEA, 0x4C, 0xFA, 0x62, 0x27, 0xFD, 0x8A,
	0x26, 0x03, 0xF2, 0x5E, 0x85, 0xA4, 0x70, 0x06, 0x28, 0x9C, 0x01, 0xB2,
	0x92, 0x72, 0xEE, 0x00, 0xAE, 0xF5, 0x6A, 0x66, 0x97, 0xC4, 0xAB, 0x92,
	0xED, 0x92, 0x57, 0x6B, 0xF3, 0xA9, 0x48, 0x4C, 0x51, 0x42, 0xE6, 0x8A,
	0xCB, 0xB9, 0x62, 0x28, 0x02, 0xBB, 0x06, 0xBD, 0x2A, 0x9B, 0xB6, 0x42,
	0x51, 0x6B, 0x3F, 0x45, 0x09, 0xB9, 0x55, 0x48, 0xBA, 0x24, 0xC4, 0xB7,
	0x8B, 0xB2, 0xBA, 0xAF, 0x7A, 0x53, 0x1B, 0xB7, 0xE5, 0x33, 0x6D, 0xBA,
	0x3B, 0xAA, 0xBA, 0x3B, 0x2A, 0x95, 0x90, 0x7B, 0x3A, 0x4B, 0xF9, 0x5D,
	0x27, 0x84, 0x82, 0x80, 0x9A, 0xF3, 0x6E, 0x05, 0xD5, 0x76, 0xC3, 0x8C,
	0xEA, 0x0F, 0x54, 0xD3, 0x87, 0xAB, 0x2B, 0x6E, 0x32, 0x0B, 0x6E, 0xB1,
	0x22, 0x9A, 0x29, 0x70, 0x3C, 0xC5, 0x5E, 0x86, 0x94, 0x2B, 0x99, 0x96,
	0x21, 0xA7, 0x64, 0xA8, 0xBB, 0xB2, 0x44, 0xAE, 0x0C, 0x04, 0x07, 0x73,
	0x83, 0x99, 0xC5, 0x6E, 0x53, 0x41, 0x65, 0x6A, 0x10, 0x5F, 0x05, 0x97,
	0xBE, 0x42, 0x60, 0xDE, 0x44, 0xA6, 0x8A, 0xD3, 0x03, 0x27, 0x03, 0x70,
	0xEA, 0x4C, 0x05, 0x60, 0xA7, 0x02, 0x8B, 0xA6, 0x82, 0xF2, 0x5F, 0x87,
	0xA5, 0xF2, 0x3B, 0x4A, 0x95, 0x94, 0x28, 0xC1, 0x09, 0x3A, 0xD0, 0x7D,
	0x1D, 0x99, 0x03, 0x5D, 0x87, 0xE9, 0x0D, 0xDB, 0xF9, 0xED, 0xA5, 0x0A,
	0x3E, 0x11, 0xB5, 0x97, 0x28, 0x99, 0x89, 0x18, 0x0D, 0xDB, 0x05, 0x6D,
	0xA5, 0x4A, 0x4A, 0x94, 0xE0, 0x7A, 0xDB, 0xD0, 0xFD, 0xED, 0xE0, 0x40,
	0x67, 0x10, 0x83, 0xE5, 0xDA, 0xC7, 0x2B, 0xFD, 0x48, 0x49, 0x3F, 0x0F,
	0xD7, 0xCF, 0xC3, 0x88, 0x5A, 0xB3, 0xAE, 0xB7, 0x05, 0x43, 0xD6, 0xD7,
	0x58, 0xA5, 0x6A, 0xE0, 0xAF, 0xB3, 0x0A, 0x07, 0x1B, 0x8E, 0xDF, 0x6C,
	0x0A, 0xAB, 0xAF, 0xDD, 0x75, 0xFF, 0x2C, 0x41, 0x8D, 0xD2, 0xFB, 0x67,
	0xB1, 0xA3, 0xA4, 0xD3, 0xE3, 0x94, 0x58, 0x1E, 0xC9, 0x63, 0x3A, 0xB9,
	0x56, 0x0D, 0xE6, 0x74, 0x4A, 0xF5, 0x74, 0x2A, 0x65, 0x1A, 0x04, 0x6F,
	0x9C, 0x0A, 0x7D, 0x1D, 0x8E, 0x57, 0x03, 0xA7, 0x20, 0xA2, 0xF8, 0x4D,
	0xE4, 0x99, 0xB7, 0x31, 0x69, 0xFD, 0x5C, 0x9C, 0x1A, 0x58, 0x21, 0xB7,
	0x58, 0xC8, 0x2D, 0xB8, 0xC2, 0x03, 0x07, 0x5B, 0x11, 0xFF, 0x5F, 0x24,
	0xE4, 0xE2, 0xD4, 0x50, 0x44, 0x22, 0x28, 0xE2, 0x82, 0x83, 0x3C, 0x84,
	0x90, 0x83, 0x53, 0x03, 0x2B, 0xE2, 0x14, 0x8B, 0x38, 0x05, 0x62, 0x0E,
	0x28, 0xE1, 0x85, 0x88, 0x9B, 0x70, 0x6A, 0x60, 0xC5, 0xEC, 0xE2, 0x01,
	0x76, 0xC1, 0x35, 0x76, 0xD6, 0x8D, 0x96, 0xD0, 0xA1, 0x3A, 0xDC, 0x6C,
	0x8A, 0x6F, 0xD4, 0xA1, 0x87, 0xEB, 0x8F, 0xDF, 0xBE, 0x10, 0x39, 0x46,
	0xC0, 0xAB, 0x81, 0x1B, 0x23, 0x60, 0xC7, 0x88, 0x85, 0xE3, 0x65, 0xB9,
	0x8F, 0x49, 0xC8, 0xF7, 0xE9, 0x25, 0x6A, 0xE0, 0x95, 0x60, 0xDF, 0x67,
	0xA0, 0xE5, 0xD0, 0x77, 0xC8, 0xE7, 0x4F, 0xD1, 0xCF, 0xFE, 0x7F, 0x66,
	0xFF, 0x68, 0x17, 0x67, 0xE5, 0x7A, 0x56, 0x53, 0x53, 0xB5, 0xA8, 0xFD,
	0xC5, 0x6A, 0x15, 0x88, 0x0D, 0x42, 0x06, 0xA9, 0xAF, 0x5D, 0x7F, 0xEF,
	0xF8, 0x3F, 0x0B, 0x10, 0x3B, 0xD9
};
};

static bool
sk_png_remove_chunk (const char* szName, void* data, size_t& size)
{
  if (szName == nullptr || data == nullptr || size < 12 || strlen (szName) < 4)
  {
    return false;
  }

  size_t   erase_pos = 0;
  uint8_t* erase_ptr = nullptr;

  // Effectively a string search, but ignoring nul-bytes in both
  //   the character array being searched and the pattern...
  std::string_view data_view ((const char *)data, size);
  if (erase_pos  = data_view.find (szName, 0, 4);
      erase_pos == data_view.npos)
  {
    return false;
  }

  erase_pos -= 4; // Rollback to the chunk's length field
  erase_ptr =
    ((uint8_t *)data + erase_pos);

  uint32_t chunk_size = *(uint32_t *)erase_ptr;

// Length is Big Endian, Intel/AMD CPUs are Little Endian
#if (defined _M_IX86) || (defined _M_X64)
  chunk_size = _byteswap_ulong (chunk_size);
#endif

  size_t size_to_erase = (size_t)12 + chunk_size;

  memmove ( erase_ptr,
            erase_ptr             + size_to_erase,
                 size - erase_pos - size_to_erase );

  size -= size_to_erase;

  return true;
}

static SK_PNG_HDR_cLLi_Payload
SKIV_HDR_CalculateContentLightInfo (const DirectX::Image& img)
{
  using namespace DirectX;

  SK_PNG_HDR_cLLi_Payload clli;

  float N          =       0.0f;
  float fLumAccum  =       0.0f;
  float fMaxLum    =       0.0f;
  float fMinLum    = 5240320.0f;

  EvaluateImage ( img,
    [&](const XMVECTOR* pixels, size_t width, size_t y)
    {
      UNREFERENCED_PARAMETER(y);

      float fScanlineLum = 0.0f;

      switch (img.format)
      {
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        {
          for (size_t j = 0; j < width; ++j)
          {
            XMVECTOR v =
              *pixels++;

            v =
              XMVector3Transform (
                SKIV_Image_PQToLinear (XMVectorSaturate (v)), c_from2020toXYZ
              );

            const float fLum =
              XMVectorGetY (v);

            fMaxLum =
              std::max (fMaxLum, fLum);

            fMinLum =
              std::min (fMinLum, fLum);

            fScanlineLum += fLum;
          }
        } break;

        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
        {
          for (size_t j = 0; j < width; ++j)
          {
            XMVECTOR v =
              *pixels++;

            v =
              XMVector3Transform (v, c_from709toXYZ);

            const float fLum =
              XMVectorGetY (v);

            fMaxLum =
              std::max (fMaxLum, fLum);

            fMinLum =
              std::min (fMinLum, fLum);

            fScanlineLum += fLum;
          }
        } break;

        default:
          break;
      }

      fLumAccum +=
        (fScanlineLum / static_cast <float> (width));
      ++N;
    }
  );

  if (N > 0.0)
  {
    // 0 nits - 10k nits (limit imposed by PQ)
    fMinLum = std::clamp (fMinLum, 0.0f,    125.0f);
    fMaxLum = std::clamp (fMaxLum, fMinLum, 125.0f);

    const float fLumRange =
            (fMaxLum - fMinLum);

    auto        luminance_freq = std::make_unique <uint32_t []> (65536);
    ZeroMemory (luminance_freq.get (),     sizeof (uint32_t)  *  65536);

    EvaluateImage ( img,
    [&](const XMVECTOR* pixels, size_t width, size_t y)
    {
      UNREFERENCED_PARAMETER(y);

      for (size_t j = 0; j < width; ++j)
      {
        XMVECTOR v = *pixels++;

        v =
          XMVector3Transform (v, c_from709toXYZ);

        luminance_freq [
          std::clamp ( (int)
            std::roundf (
              (XMVectorGetY (v) - fMinLum)     /
                                    (fLumRange / 65536.0f) ),
                                              0, 65535 ) ]++;
      }
    });

          double percent  = 100.0;
    const double img_size = (double)img.width *
                            (double)img.height;

    for (auto i = 65535; i >= 0; --i)
    {
      percent -=
        100.0 * ((double)luminance_freq [i] / img_size);

      if (percent <= 99.9)
      {
        fMaxLum =
          fMinLum + (fLumRange * ((float)i / 65536.0f));

        break;
      }
    }

    SK_PNG_SetUint32 (clli.max_cll,
      static_cast <uint32_t> ((80.0f * fMaxLum)         / 0.0001f));
    SK_PNG_SetUint32 (clli.max_fall,
      static_cast <uint32_t> ((80.0f * (fLumAccum / N)) / 0.0001f));
  }

  return clli;
}

static bool
SKIV_HDR_ConvertImageToPNG (const DirectX::Image& raw_hdr_img, DirectX::ScratchImage& png_img)
{
  using namespace DirectX;

  if (auto typeless_fmt = DirectX::MakeTypeless (raw_hdr_img.format);
           typeless_fmt == DXGI_FORMAT_R8G8B8A8_TYPELESS     ||
           typeless_fmt == DXGI_FORMAT_R10G10B10A2_TYPELESS  ||
           typeless_fmt == DXGI_FORMAT_R16G16B16A16_TYPELESS ||
           typeless_fmt == DXGI_FORMAT_R32G32B32A32_TYPELESS)
  {
    if (png_img.GetImageCount () == 0)
    {
      // Early SDR exit
      if (typeless_fmt == DXGI_FORMAT_R8G8B8A8_TYPELESS)
        return (SUCCEEDED (png_img.InitializeFromImage (raw_hdr_img)));

      if (FAILED (png_img.Initialize2D (DXGI_FORMAT_R16G16B16A16_UNORM,
              raw_hdr_img.width,
              raw_hdr_img.height, 1, 1)))
      {
        return false;
      }
    }

    if (png_img.GetMetadata ().format != DXGI_FORMAT_R16G16B16A16_UNORM)
      return false;

    uint16_t* rgb16_pixels =
      reinterpret_cast <uint16_t *> (png_img.GetPixels ());

    if (rgb16_pixels == nullptr)
      return false;

    EvaluateImage ( raw_hdr_img,
    [&](const XMVECTOR* pixels, size_t width, size_t y)
    {
      UNREFERENCED_PARAMETER(y);

      static const XMVECTOR pq_range_10bpc = XMVectorReplicate (1023.0f),
                            pq_range_12bpc = XMVectorReplicate (4095.0f),
                            pq_range_16bpc = XMVectorReplicate (65535.0f),
                            pq_range_32bpc = XMVectorReplicate (4294967295.0f);

      auto pq_range_out =
        (typeless_fmt == DXGI_FORMAT_R10G10B10A2_TYPELESS)  ? pq_range_10bpc :
        (typeless_fmt == DXGI_FORMAT_R16G16B16A16_TYPELESS) ? pq_range_12bpc :
                                                              pq_range_12bpc;//pq_range_16bpc;

      pq_range_out = pq_range_16bpc;

      const auto pq_range_in  =
        (typeless_fmt == DXGI_FORMAT_R10G10B10A2_TYPELESS)  ? pq_range_10bpc :
        (typeless_fmt == DXGI_FORMAT_R16G16B16A16_TYPELESS) ? pq_range_16bpc :
                                                              pq_range_32bpc;

      int intermediate_bits = 16;
      int output_bits       = 
        (typeless_fmt == DXGI_FORMAT_R10G10B10A2_TYPELESS)  ? 10 :
        (typeless_fmt == DXGI_FORMAT_R16G16B16A16_TYPELESS) ? 12 :
                                                              12;//16;

      output_bits = 16;

      for (size_t j = 0; j < width; ++j)
      {
        XMVECTOR v =
          *pixels++;

        // Assume scRGB for any FP32 input, though uncommon
        if (typeless_fmt == DXGI_FORMAT_R16G16B16A16_TYPELESS ||
            typeless_fmt == DXGI_FORMAT_R32G32B32A32_TYPELESS)
        {
          v =
            SKIV_Image_LinearToPQ (XMVectorMax (XMVector3Transform (v, c_scRGBtoBt2100), g_XMZero));
        }

        v = // Quantize to 10- or 12-bpc before expanding to 16-bpc in order to improve
          XMVectorRound ( // compression efficiency
            XMVectorMultiply (
              XMVectorSaturate (v), pq_range_out));

        *(rgb16_pixels++) =
          static_cast <uint16_t> (DirectX::XMVectorGetX (v)) << (intermediate_bits - output_bits);
        *(rgb16_pixels++) =
          static_cast <uint16_t> (DirectX::XMVectorGetY (v)) << (intermediate_bits - output_bits);
        *(rgb16_pixels++) =
          static_cast <uint16_t> (DirectX::XMVectorGetZ (v)) << (intermediate_bits - output_bits);
          rgb16_pixels++; // We have an unused alpha channel that needs skipping
      }
    });
  }

  return true;
}

static bool
SKIV_PNG_MakeHDR ( const wchar_t*        wszFilePath,
                   const DirectX::Image& encoded_img,
                   const DirectX::Image& raw_img )
{
  std::ignore = encoded_img;

  static const BYTE _test [] = { 0x49, 0x45, 0x4E, 0x44 };

  if (png_crc32 ((const BYTE *)_test, 0, 4, 0) == 0xae426082)
  {
    PLOG_VERBOSE << "png_crc32 == TRUE";

    FILE*
        fPNG = _wfopen (wszFilePath, L"r+b");
    if (fPNG != nullptr)
    {
      fseek  (fPNG, 0, SEEK_END);
      size_t size = ftell (fPNG);
      rewind (fPNG);

      auto data =
        std::make_unique <uint8_t []> (size);

      if (! data)
      {
        fclose (fPNG);
        return false;
      }

      fread (data.get (), size, 1, fPNG);
      rewind (                     fPNG);

      sk_png_remove_chunk ("sRGB", data.get (), size);
      sk_png_remove_chunk ("gAMA", data.get (), size);

      fwrite (data.get (), size, 1, fPNG);

      // Truncate the file
      _chsize (_fileno (fPNG), static_cast <long> (size));

      size_t         insert_pos = 0;
      const uint8_t* insert_ptr = nullptr;

      // Effectively a string search, but ignoring nul-bytes in both
      //   the character array being searched and the pattern...
      std::string_view  data_view ((const char *)data.get (), size);
      if (insert_pos  = data_view.find ("IDAT", 0, 4);
          insert_pos == data_view.npos)
      {
        fclose (fPNG);
        return false;
      }

      insert_pos -= 4; // Rollback to the chunk's length field
      insert_ptr =
        (data.get () + insert_pos);

      fseek (fPNG, static_cast <long> (insert_pos), SEEK_SET);

      struct SK_PNG_Chunk {
        uint32_t      len;
        unsigned char name [4];
        void*         data;
        uint32_t      crc;
        uint32_t      _native_len;

        void write (FILE* fStream)
        {
          // Length is Big Endian, Intel/AMD CPUs are Little Endian
          if (_native_len == 0)
          {
            _native_len = len;
#if (defined _M_IX86) || (defined _M_X64)
            len         = _byteswap_ulong (_native_len);
#endif
          }

          crc =
            png_crc32 (data, 0, _native_len, png_crc32 (name, 0, 4, 0x0));
#if (defined _M_IX86) || (defined _M_X64)
          crc = _byteswap_ulong (crc);
#endif

          fwrite (&len, 8,           1, fStream);
          fwrite (data, _native_len, 1, fStream);
          fwrite (&crc, 4,           1, fStream);
        };
      };

      uint8_t cicp_data [] = {
        9,  // BT.2020 Color Primaries
        16, // ST.2084 EOTF (PQ)
        0,  // Identity Coefficients
        1,  // Full Range
      };

      // Embedded ICC Profile so that Discord will render in HDR
      SK_PNG_HDR_iCCP_Payload iccp_data;

      SK_PNG_HDR_cHRM_Payload chrm_data; // Rec 2020 chromaticity
      SK_PNG_HDR_sBIT_Payload sbit_data; // Bits in original source (max=12)
      SK_PNG_HDR_mDCv_Payload mdcv_data; // Display capabilities
      SK_PNG_HDR_cLLi_Payload clli_data; // Content light info

      clli_data =
        SKIV_HDR_CalculateContentLightInfo (raw_img);

      sbit_data = {
        static_cast <unsigned char> (DirectX::BitsPerColor (raw_img.format)),
        static_cast <unsigned char> (DirectX::BitsPerColor (raw_img.format)),
        static_cast <unsigned char> (DirectX::BitsPerColor (raw_img.format))
      };

      // If using compression optimization, max bits = 12
      sbit_data.red_bits   = 16;//std::min (sbit_data.red_bits,   12ui8);
      sbit_data.green_bits = 16;//std::min (sbit_data.green_bits, 12ui8);
      sbit_data.blue_bits  = 16;//std::min (sbit_data.blue_bits,  12ui8);

      // We don't actually know the mastering display, but some effort should be made
      //   to read this metadata and preserve it if it exists when SKIV originally
      //     loads HDR images.
# if 0
      auto& rb =
        SK_GetCurrentRenderBackend ();

      auto& active_display =
        rb.displays [rb.active_display];

      SK_PNG_SetUint32 (mdcv_data.luminance.minimum,
        static_cast <uint32_t> (round (active_display.gamut.minY / 0.0001f)));
      SK_PNG_SetUint32 (mdcv_data.luminance.maximum,
        static_cast <uint32_t> (round (active_display.gamut.maxY / 0.0001f)));

      SK_PNG_SetUint32 (mdcv_data.primaries.red_x,
        static_cast <uint32_t> (round (active_display.gamut.xr / 0.00002)));
      SK_PNG_SetUint32 (mdcv_data.primaries.red_y,
        static_cast <uint32_t> (round (active_display.gamut.yr / 0.00002)));

      SK_PNG_SetUint32 (mdcv_data.primaries.green_x,
        static_cast <uint32_t> (round (active_display.gamut.xg / 0.00002)));
      SK_PNG_SetUint32 (mdcv_data.primaries.green_y,
        static_cast <uint32_t> (round (active_display.gamut.yg / 0.00002)));

      SK_PNG_SetUint32 (mdcv_data.primaries.blue_x,
        static_cast <uint32_t> (round (active_display.gamut.xb / 0.00002)));
      SK_PNG_SetUint32 (mdcv_data.primaries.blue_y,
        static_cast <uint32_t> (round (active_display.gamut.yb / 0.00002)));

      SK_PNG_SetUint32 (mdcv_data.white_point.x,
        static_cast <uint32_t> (round (active_display.gamut.Xw / 0.00002)));
      SK_PNG_SetUint32 (mdcv_data.white_point.y,
        static_cast <uint32_t> (round (active_display.gamut.Yw / 0.00002)));
#endif

      SK_PNG_Chunk clli_chunk = { sizeof (clli_data),               { 'c','L','L','i' }, &clli_data };
      SK_PNG_Chunk iccp_chunk = { sizeof (SK_PNG_HDR_iCCP_Payload), { 'i','C','C','P' }, &iccp_data };
      SK_PNG_Chunk cicp_chunk = { sizeof (cicp_data),               { 'c','I','C','P' }, &cicp_data };
      SK_PNG_Chunk sbit_chunk = { sizeof (sbit_data),               { 's','B','I','T' }, &sbit_data };
      SK_PNG_Chunk chrm_chunk = { sizeof (chrm_data),               { 'c','H','R','M' }, &chrm_data };
#if 0
      SK_PNG_Chunk mdcv_chunk = { sizeof (mdcv_data),               { 'm','D','C','v' }, &mdcv_data };
#endif

      iccp_chunk.write (fPNG);
      clli_chunk.write (fPNG);
      cicp_chunk.write (fPNG);
      sbit_chunk.write (fPNG);
      chrm_chunk.write (fPNG);
#if 0
      mdcv_chunk.write (fPNG);
#endif

      // Write the remainder of the original file
      fwrite (insert_ptr, size - insert_pos, 1, fPNG);

      auto final_size =
        ftell (fPNG);

      auto full_png =
        std::make_unique <unsigned char []> (final_size);

      rewind (fPNG);
      fread  (full_png.get (), final_size, 1, fPNG);
      fclose (fPNG);

      //PLOG_VERBOSE << "Applied HDR10 PNG chunks to " << SK_WideCharToUTF8 (wszFilePath).c_str () << ".";
      //PLOG_VERBOSE << " >> MaxCLL: " <<
      //          static_cast <double> (SK_PNG_GetUint32 (clli_data.max_cll))  * 0.0001 << " nits, MaxFALL: " <<
      //          static_cast <double> (SK_PNG_GetUint32 (clli_data.max_fall)) * 0.0001 << " nits";
      //SK_LOGi1 (L" >> Mastering Display Min/Max Luminance: %.6f/%.6f nits",
      //          static_cast <double> (SK_PNG_GetUint32 (mdcv_data.luminance.minimum)) * 0.0001,
      //          static_cast <double> (SK_PNG_GetUint32 (mdcv_data.luminance.maximum)) * 0.0001);

      return true;
    }
  }

  return false;
}

static void
SK_WIC_SetMaximumQuality (IPropertyBag2 *props)
{
  if (props == nullptr)
    return;

  PROPBAG2 opt  = { .pstrName = L"ImageQuality" };
  VARIANT  var  = { VT_R4,0,0,0, { .fltVal = 1.0f } };

  PROPBAG2 opt2 = { .pstrName = L"FilterOption" };
  VARIANT  var2 = { VT_UI1,0,0,0, { .bVal = WICPngFilterAdaptive } };

  props->Write (1, &opt,  &var);
  props->Write (1, &opt2, &var2);
}

static bool
SKIV_HDR_SavePNGToDisk (const wchar_t* wszPNGPath, const DirectX::Image* png_image,
                                                   const DirectX::Image* raw_image,
                           const char* szUtf8MetadataTitle, bool isHDR)
{
  if ( wszPNGPath == nullptr ||
        png_image == nullptr ||
        raw_image == nullptr )
  {
    PLOG_VERBOSE_IF(wszPNGPath == nullptr) << "wszPNGPath == nullptr";
    PLOG_VERBOSE_IF(png_image  == nullptr) << "png_image  == nullptr";
    PLOG_VERBOSE_IF(raw_image  == nullptr) << "raw_image  == nullptr";
    return false;
  }

  std::string metadata_title (
         szUtf8MetadataTitle != nullptr ?
         szUtf8MetadataTitle            :
         "HDR10 PNG" );

  if (SUCCEEDED (
    DirectX::SaveToWICFile (*png_image, DirectX::WIC_FLAGS_NONE,
                           GetWICCodec (DirectX::WIC_CODEC_PNG),
                               wszPNGPath, &GUID_WICPixelFormat48bppRGB,
                                              SK_WIC_SetMaximumQuality/*,
                                            [&](IWICMetadataQueryWriter *pMQW)
                                            {
                                              SK_WIC_SetMetadataTitle (pMQW, metadata_title);
                                            }*/)))
  {
    PLOG_VERBOSE << "DirectX::SaveToWICFile ( ): SUCCEEDED";

    return (isHDR) ? SKIV_PNG_MakeHDR (wszPNGPath, *png_image, *raw_image)
                   : true;
  } else
    PLOG_VERBOSE << "DirectX::SaveToWICFile ( ): FAILED";

  return false;
}

// The parameters are screwy here because currently the only successful way
//   of doing this copy involves passing the path to a file, but the intention
//     is actually to pass raw image data and transfer it using OLE.
static bool
SKIV_PNG_CopyToClipboard (const DirectX::Image& image, const void *pData, size_t data_size)
{
  std::ignore = image;
  std::ignore = data_size; // It's a string, we can compute the size trivially

  int clpSize = sizeof (DROPFILES);

  clpSize += sizeof (wchar_t) * static_cast <int> (wcslen ((wchar_t *)pData) + 1);
  clpSize += sizeof (wchar_t);

  HDROP hdrop =
    (HDROP)GlobalAlloc (GHND, clpSize);

  DROPFILES* df =
    (DROPFILES *)GlobalLock (hdrop);

  df->pFiles = sizeof (DROPFILES);
  df->fWide  = TRUE;

  wcscpy ((wchar_t*)&df [1], (const wchar_t *)pData);

  bool clipboard_open = false;
  for (UINT i = 0 ; i < 5 ; ++i)
  {
    clipboard_open = OpenClipboard (SKIF_ImGui_hWnd);

    if (! clipboard_open)
      Sleep (2);
  }

  if (clipboard_open)
  {
    EmptyClipboard   ();
    SetClipboardData (CF_HDROP, hdrop);
    CloseClipboard   ();
    GlobalUnlock               (hdrop);

    return true;
  }

  GlobalUnlock (hdrop);

  return false;
}

bool SKIV_Image_CopyToClipboard (const DirectX::Image* pImage, bool snipped, bool isHDR)
{
using namespace DirectX;

  if (pImage == nullptr)
    return false;

  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  std::wstring wsPNGPath = _path_cache.skiv_temp;
  wsPNGPath += snipped ? L"SKIV_Snip"
                       : L"SKIV_Clipboard";
  wsPNGPath += L".png";

  PLOG_VERBOSE << wsPNGPath;

  static SKIF_RegistrySettings& _registry =
    SKIF_RegistrySettings::GetInstance ( );

  int snipping_tonemap_mode = _registry._SnippingTonemapsHDR;

  if (_registry._SnippingTonemapsHDR == 2)
  {
    XMVECTOR maxLum = XMVectorZero ();

    EvaluateImage ( *pImage,
    [&](const XMVECTOR* pixels, size_t width, size_t y)
    {
      UNREFERENCED_PARAMETER(y);

      for (size_t j = 0; j < width; ++j)
      {
        XMVECTOR v = *pixels++;

        v =
          XMVector3Transform (v, c_from709toXYZ);

        maxLum =
          XMVectorReplicate (XMVectorGetY (XMVectorMax (v, maxLum)));
      }
    });

    POINT          ptCursor = { };
    GetCursorPos (&ptCursor);

    HMONITOR hMon =
      MonitorFromPoint (ptCursor, MONITOR_DEFAULTTONEAREST);

    float mastering_sdr_nits = SKIF_Util_GetSDRWhiteLevel (hMon);

    if (XMVectorGetY (maxLum) > std::max (1.0f, (mastering_sdr_nits * 1.00333f) / 80.0f))
         snipping_tonemap_mode = 0;
    else snipping_tonemap_mode = 1;
  }

  if (isHDR && (! snipping_tonemap_mode))
  {
    if (SUCCEEDED (SKIV_Image_SaveToDisk_HDR (*pImage, wsPNGPath.c_str())))
    {
      PLOG_VERBOSE << "SKIF_Image_SaveToDisk_HDR ( ): SUCCEEDED";

      if (SKIV_PNG_CopyToClipboard (*pImage, wsPNGPath.c_str(), 0))
      {
        PLOG_VERBOSE << "SKIV_PNG_CopyToClipboard ( ): TRUE";
        return true;
      }
    }

    else
      PLOG_VERBOSE << "SKIF_Image_SaveToDisk_HDR ( ): FAILED";
  }

  else
  {
    DirectX::ScratchImage tonemapped_sdr;
    if (snipping_tonemap_mode && isHDR)
    {
      if (SUCCEEDED (SKIV_Image_TonemapToSDR (*pImage, tonemapped_sdr, SKIV_DesktopImage._max_display_nits, SKIV_DesktopImage._sdr_display_nits)))
      {
        pImage = tonemapped_sdr.GetImage (0,0,0);
      }

      else
        PLOG_INFO << "SKIV_Image_TonemapToSDR ( ): FAILED!";
    }

    const int
        _bpc    =
      (int)(DirectX::BitsPerPixel (pImage->format)),
        _width  =
      (int)(                       pImage->width),
        _height =
      (int)(                       pImage->height);

    DirectX::ScratchImage swizzled_sdr;
    // Swizzle the image and handle gamma if necessary
    if (pImage->format != DXGI_FORMAT_B8G8R8X8_UNORM_SRGB)
    {
      if (SUCCEEDED (DirectX::Convert (*pImage, DXGI_FORMAT_B8G8R8X8_UNORM_SRGB, DirectX::TEX_FILTER_DEFAULT, 0.0f, swizzled_sdr)))
      {
        pImage = swizzled_sdr.GetImage (0,0,0);
      }
    }
    ////SK_ReleaseAssert (pImage->format == DXGI_FORMAT_B8G8R8X8_UNORM ||
    ////                  pImage->format == DXGI_FORMAT_B8G8R8A8_UNORM ||
    ////                  pImage->format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB);

    HBITMAP hBitmapCopy =
       CreateBitmap (
         _width, _height, 1,
           _bpc, pImage->pixels
       );

    BITMAPINFOHEADER
      bmh                 = { };
      bmh.biSize          = sizeof (BITMAPINFOHEADER);
      bmh.biWidth         =   _width;
      bmh.biHeight        =  -_height;
      bmh.biPlanes        =  1;
      bmh.biBitCount      = (WORD)_bpc;
      bmh.biCompression   = BI_RGB;
      bmh.biXPelsPerMeter = 10;
      bmh.biYPelsPerMeter = 10;

    BITMAPINFO
      bmi                 = { };
      bmi.bmiHeader       = bmh;

    HDC hdcDIB =
      CreateCompatibleDC (GetDC (nullptr));

    void* bitplane = nullptr;

    HBITMAP
      hBitmap =
        CreateDIBSection ( hdcDIB, &bmi, DIB_RGB_COLORS,
            &bitplane, nullptr, 0 );
    memcpy ( bitplane,
               pImage->pixels,
        static_cast <size_t> (_bpc / 8) *
        static_cast <size_t> (_width  ) *
        static_cast <size_t> (_height )
           );

    HDC hdcSrc = CreateCompatibleDC (GetDC (nullptr));
    HDC hdcDst = CreateCompatibleDC (GetDC (nullptr));

    if ( hBitmap     != nullptr &&
         hBitmapCopy != nullptr )
    {
      auto hbmpSrc = (HBITMAP)SelectObject (hdcSrc, hBitmap);
      auto hbmpDst = (HBITMAP)SelectObject (hdcDst, hBitmapCopy);

      BitBlt (hdcDst, 0, 0, _width,
                            _height, hdcSrc, 0, 0, SRCCOPY);

      SelectObject     (hdcSrc, hbmpSrc);
      SelectObject     (hdcDst, hbmpDst);

      bool clipboard_open = false;
      for (UINT i = 0 ; i < 5 ; ++i)
      {
        clipboard_open = OpenClipboard (SKIF_ImGui_hWnd);

        if (! clipboard_open)
          Sleep (2);
      }

      if (clipboard_open)
      {
        EmptyClipboard   ();
        SetClipboardData (CF_BITMAP, hBitmapCopy);
        CloseClipboard   ();
      }
    }

    DeleteDC         (hdcSrc);
    DeleteDC         (hdcDst);
    DeleteDC         (hdcDIB);

    if ( hBitmap     != nullptr &&
         hBitmapCopy != nullptr )
    {
      DeleteBitmap   (hBitmap);
      DeleteBitmap   (hBitmapCopy);

      return true;
    }
  }

  return false;
}

class SKIV_ScopedThreadPriority
{
public:
  SKIV_ScopedThreadPriority (int prio = THREAD_PRIORITY_TIME_CRITICAL) {
    orig_prio_ =
      GetThreadPriority (GetCurrentThread ());

    orig_process_class_ =
      GetPriorityClass (GetCurrentProcess ());

    SetPriorityClass  (GetCurrentProcess (), HIGH_PRIORITY_CLASS);
    SetThreadPriority (GetCurrentThread  (), prio);
  };

  ~SKIV_ScopedThreadPriority (void) {
    SetPriorityClass  (GetCurrentProcess (), orig_process_class_);
    SetThreadPriority (GetCurrentThread  (), orig_prio_);
  }

private:
  int orig_prio_;
  int orig_process_class_;
};

#include <iostream>
#include <vector>
#include <cmath>

void
scRGB_to_ICtCp (float r, float g, float b, float& I, float& Ct, float& Cp)
{
  using namespace DirectX;

  XMVECTOR scRGB =
    XMVectorSet (r,g,b,1.0f);

  XMVECTOR ICtCp =
    SKIV_Image_Rec709toICtCp (scRGB);

  I  = XMVectorGetX (ICtCp);
  Ct = XMVectorGetY (ICtCp);
  Cp = XMVectorGetZ (ICtCp);
}

void
ICtCp_to_scRGB (float I, float Ct, float Cp, float& r, float& g, float& b)
{
  using namespace DirectX;

  XMVECTOR ICtCp =
    XMVectorSet (I,Ct,Cp,1.0f);

  XMVECTOR scRGB =
    SKIV_Image_ICtCptoRec709 (ICtCp);

  r = XMVectorGetX (scRGB);
  g = XMVectorGetY (scRGB);
  b = XMVectorGetZ (scRGB);
}

// Function to clamp values between 0 and 63
int clamp(int value, int min, int max) {
    return std::max(min, std::min(value, max));
}

// Example function to look up a value from the LUT
//std::array<float, 3> lookupLUT(const std::vector<std::vector<std::vector<std::array<float, 3>>>>& lut, 
//                               float r, float g, float b) {
//
//}

DirectX::XMVECTOR
SKIV_Image_LookupLUT (std::vector <std::vector <std::vector <float [3]>>>& lut, DirectX::XMVECTOR& v)
{
  using namespace DirectX;

  const int LUT_SIZE = 64;

  // Ensure input values are between 0 and 1
  float r = std::max (0.0f, std::min (1.0f, XMVectorGetX (v)));
  float g = std::max (0.0f, std::min (1.0f, XMVectorGetY (v)));
  float b = std::max (0.0f, std::min (1.0f, XMVectorGetZ (v)));

  // Convert normalized values to indices
  int ri = static_cast<int>(r * (LUT_SIZE - 1));
  int gi = static_cast<int>(g * (LUT_SIZE - 1));
  int bi = static_cast<int>(b * (LUT_SIZE - 1));

  // Clamp indices to be within the LUT bounds
  ri = clamp (ri, 0, LUT_SIZE - 1);
  gi = clamp (gi, 0, LUT_SIZE - 1);
  bi = clamp (bi, 0, LUT_SIZE - 1);

  // Retrieve the value from the LUT
  return
    XMVectorSet (lut [ri][gi][bi][0], lut [ri][gi][bi][1], lut [ri][gi][bi][2], 1.0f);
}

void
SKIV_Image_GenerateLUT_scRGB_to_ICtCp (std::vector <std::vector <std::vector <float [3]>>>& lut)
{
  const int   LUT_SIZE = 64;
//const float MAX_VAL  = 1.0f; // Assuming values are normalized between 0 and 1

  for (int r = 0; r < LUT_SIZE; ++r)
  {
    for (int g = 0; g < LUT_SIZE; ++g)
    {
      for (int b = 0; b < LUT_SIZE; ++b)
      {
        float sr = static_cast <float> (r) / (LUT_SIZE - 1);
        float sg = static_cast <float> (g) / (LUT_SIZE - 1);
        float sb = static_cast <float> (b) / (LUT_SIZE - 1);

        float I, Ct, Cp;
        scRGB_to_ICtCp (sr, sg, sb, I, Ct, Cp);

        lut[r][g][b][0] = I;
        lut[r][g][b][1] = Ct;
        lut[r][g][b][2] = Cp;
      }
    }
  }
}

void
SKIV_Image_GenerateLUT_ICtCp_to_scRGB (std::vector <std::vector <std::vector <float [3]>>>& lut)
{
  const int   LUT_SIZE = 64;
//const float MAX_VAL  = 1.0f; // Assuming values are normalized between 0 and 1

  for (int r = 0; r < LUT_SIZE; ++r)
  {
    for (int g = 0; g < LUT_SIZE; ++g)
    {
      for (int b = 0; b < LUT_SIZE; ++b)
      {
        float I  = static_cast <float> (r) / (LUT_SIZE - 1);
        float Ct = static_cast <float> (g) / (LUT_SIZE - 1);
        float Cp = static_cast <float> (b) / (LUT_SIZE - 1);

        float sr, sg, sb;
        ICtCp_to_scRGB (I, Ct, Cp, sr, sg, sb);

        lut[r][g][b][0] = sr;
        lut[r][g][b][1] = sg;
        lut[r][g][b][2] = sb;
      }
    }
  }
}

HRESULT
SKIV_Image_TonemapToSDR (const DirectX::Image& image, DirectX::ScratchImage& final_sdr, float mastering_max_nits, float mastering_sdr_nits)
{
  SKIV_ScopedThreadPriority _;

  DWORD dwStart = SKIF_Util_timeGetTime1 ();

  PLOG_INFO << "SKIV_Image_TonemapToSDR ( ): Begin";

  using namespace DirectX;

  XMVECTOR maxLum = XMVectorZero          (),
           minLum = XMVectorSplatInfinity ();

  bool is_hdr = false;

  ScratchImage scrgb;

  if (image.format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
      image.format == DXGI_FORMAT_R32G32B32A32_FLOAT)
  {
    is_hdr = true;

    if (FAILED (scrgb.InitializeFromImage (image)))
      return E_INVALIDARG;
  }

  if (is_hdr)
  {
    bool needs_tonemapping = false;

    ScratchImage tonemapped_hdr;
    ScratchImage tonemapped_copy;

    PLOG_INFO << "SKIV_Image_TonemapToSDR ( ): EvaluateImageBegin";

    EvaluateImage ( scrgb.GetImages     (),
                    scrgb.GetImageCount (),
                    scrgb.GetMetadata   (),
    [&](const XMVECTOR* pixels, size_t width, size_t y)
    {
      UNREFERENCED_PARAMETER(y);

      for (size_t j = 0; j < width; ++j)
      {
        XMVECTOR v = *pixels++;

        v =
          XMVector3Transform (v, c_from709toXYZ);

        maxLum =
          XMVectorReplicate (XMVectorGetY (XMVectorMax (v, maxLum)));

        minLum =
          XMVectorReplicate (XMVectorGetY (XMVectorMin (v, minLum))); 
      }
    });

    PLOG_INFO << "SKIV_Image_TonemapToSDR ( ): EvaluateImageEnd";

    // After tonemapping, re-normalize the image to preserve peak white,
    //   this is important in cases where the maximum luminance was < 1000 nits
    XMVECTOR maxTonemappedRGB = g_XMZero;

    // If it's too bright, don't bother trying to tonemap the full range...
    const float _maxNitsToTonemap = (mastering_max_nits != 0.0f ? mastering_max_nits
                                                                : 1000.0f) / 80.0f;

    const float SDR_YInPQ = // Use the user's desktop SDR white level
      SKIV_Image_LinearToPQY (mastering_sdr_nits / 80.0f);

    const float  maxYInPQ =
      std::max (SDR_YInPQ,
        SKIV_Image_LinearToPQY (std::min (_maxNitsToTonemap, XMVectorGetY (maxLum)))
      );

    if (XMVectorGetY (maxLum) > std::max (1.0f, (mastering_sdr_nits * 1.00333f) / 80.0f))
    {
      needs_tonemapping = true;
    }

    const XMVECTOR vLumaRescale =
      XMVectorReplicate (1.0f/std::max (1.0f, (mastering_sdr_nits * 0.99667f) / 80.0f)); // user's SDR white level

    PLOG_INFO << "SKIV_Image_TonemapToSDR ( ): TransformImageBegin";
    TransformImage ( scrgb.GetImages     (),
                     scrgb.GetImageCount (),
                     scrgb.GetMetadata   (),
      [&](XMVECTOR* outPixels, const XMVECTOR* inPixels, size_t width, size_t y)
      {
        UNREFERENCED_PARAMETER(y);

        auto TonemapHDR = [](float L, float Lc, float Ld) -> float
        {
          float a = (  Ld / pow (Lc, 2.0f));
          float b = (1.0f / Ld);

          return
            L * (1 + a * L) / (1 + b * L);
        };

        for (size_t j = 0; j < width; ++j)
        {
          XMVECTOR value = inPixels [j];

          value =
           XMVectorMultiply (value, vLumaRescale);

          if (needs_tonemapping)
          {
#if 1
          XMVECTOR ICtCp =
            SKIV_Image_Rec709toICtCp (value);

          float Y_in  = std::max (XMVectorGetX (ICtCp), 0.0f);
          float Y_out = 1.0f;

          Y_out =
            TonemapHDR (Y_in, maxYInPQ, SDR_YInPQ);

          if (Y_out + Y_in > 0.0f)
          {
            ICtCp.m128_f32 [0] = std::pow (Y_in, 1.18f);

            float I0      = XMVectorGetX (ICtCp);
            float I1      = 0.0f;
            float I_scale = 0.0f;

            ICtCp.m128_f32 [0] *=
              std::max ((Y_out / Y_in), 0.0f);

            I1 = XMVectorGetX (ICtCp);

            if (I0 != 0.0f && I1 != 0.0f)
            {
              I_scale =
                std::min (I0 / I1, I1 / I0);
            }

            ICtCp.m128_f32 [1] *= I_scale;
            ICtCp.m128_f32 [2] *= I_scale;
          }

          value =
            SKIV_Image_ICtCptoRec709 (ICtCp);
#else
          XMVECTOR xyz =
            XMVector3Transform (value, c_from709toXYZ);

          float Y_in  = std::max (XMVectorGetY (xyz), 0.0f);
          float Y_out = 1.0f;

          Y_out =
            TonemapHDR (Y_in, maxY, SDR_Y);

          if (Y_out + Y_in > 0.0f)
          {
            value =
              XMVectorMultiply (XMVector3Normalize (value), XMVectorReplicate (Y_out));
          }
#endif

          maxTonemappedRGB =
            XMVectorMax (maxTonemappedRGB, XMVectorMax (value, g_XMZero));
          }

          outPixels [j] = XMVectorSaturate (value);
        }
      }, tonemapped_hdr
    );
    PLOG_INFO << "SKIV_Image_TonemapToSDR ( ): TransformImageEnd";

#if 1
    float fMaxR = XMVectorGetX (maxTonemappedRGB);
    float fMaxG = XMVectorGetY (maxTonemappedRGB);
    float fMaxB = XMVectorGetZ (maxTonemappedRGB);

    if (( fMaxR <  1.0f ||
          fMaxG <  1.0f ||
          fMaxB <  1.0f ) &&
        ( fMaxR >= 1.0f ||
          fMaxG >= 1.0f ||
          fMaxB >= 1.0f ))
    {
#ifdef GAMUT_MAPPING_WARNING
      SK_LOGi0 (
        L"After tone mapping, maximum RGB was %4.2fR %4.2fG %4.2fB -- "
        L"SDR image will be normalized to min (R|G|B) and clipped.",
          fMaxR, fMaxG, fMaxB
      );
#endif

      float fSmallestComp =
        std::min ({fMaxR, fMaxG, fMaxB});

      float fRescale =
        (1.0f / fSmallestComp);

      XMVECTOR vNormalizationScale =
        XMVectorReplicate (fRescale);

      PLOG_INFO << "SKIV_Image_TonemapToSDR ( ): TransformImageBegin";
      TransformImage (*tonemapped_hdr.GetImages (),
        [&]( _Out_writes_ (width)       XMVECTOR* outPixels,
              _In_reads_  (width) const XMVECTOR* inPixels,
                                        size_t    width,
                                        size_t )
        {
          for (size_t j = 0; j < width; ++j)
          {
            XMVECTOR value =
             inPixels [j];
            outPixels [j] =
              XMVectorSaturate (
                XMVectorMultiply (value, vNormalizationScale)
              );
          }
        }, tonemapped_copy
      );
      PLOG_INFO << "SKIV_Image_TonemapToSDR ( ): TransformImageEnd";

      std::swap (tonemapped_hdr, tonemapped_copy);
    }
#endif

    PLOG_INFO << "SKIV_Image_TonemapToSDR ( ): ConvertBegin";
    if (FAILED (DirectX::Convert (*tonemapped_hdr.GetImages (), DXGI_FORMAT_B8G8R8X8_UNORM_SRGB,
                                  (TEX_FILTER_FLAGS)0x200000FF, 1.0f, final_sdr)))
    {
      return E_UNEXPECTED;
    }
    PLOG_INFO << "SKIV_Image_TonemapToSDR ( ): ConvertEnd";
  }

  DWORD dwEnd = SKIF_Util_timeGetTime1 ();

  PLOG_INFO << "SKIV_Image_TonemapToSDR ( ): End";
  PLOG_INFO << "Operation took " << dwEnd - dwStart << "ms";

  return S_OK;
}

HRESULT
SKIV_Image_SaveToDisk_SDR (const DirectX::Image& image, const wchar_t* wszFileName, const bool force_sRGB)
{
  using namespace DirectX;

  ScratchImage
    scratch_image;
    scratch_image.InitializeFromImage (image);


  TransformImage (image,
                  [&](XMVECTOR* outPixels, const XMVECTOR* inPixels, size_t width, size_t y)
                  {
                    UNREFERENCED_PARAMETER(y);

                    for (size_t j = 0; j < width; ++j)
                    {
                      XMVECTOR value = inPixels [j];
                      outPixels [j]  =
                        image.format == DXGI_FORMAT_R16G16B16A16_FLOAT ?
                          XMVectorSet (XMVectorGetX (value), XMVectorGetY (value), XMVectorGetZ (value), 65504.0f) :
                          XMVectorSet (XMVectorGetX (value), XMVectorGetY (value), XMVectorGetZ (value),     1.0f);
                    }
                  }, scratch_image);

  auto pDevice =
    SKIF_D3D11_GetDevice ();

  if (! pDevice)
    return E_UNEXPECTED;

  CComPtr <IDXGIFactory> pFactory;
  CreateDXGIFactory (IID_IDXGIFactory, (void **)&pFactory.p);

  if (! pFactory)
    return E_NOTIMPL;

  CComPtr <IDXGIAdapter> pAdapter;
  UINT                  uiAdapter = 0;

  RECT                             wnd_rect = {};
  GetWindowRect (SKIF_ImGui_hWnd, &wnd_rect);

  HMONITOR hMonitor =
    MonitorFromWindow (SKIF_ImGui_hWnd, MONITOR_DEFAULTTONEAREST);

  CComPtr <IDXGIOutput> pMonitorOutput;
  DXGI_OUTPUT_DESC      out_desc = {};
  DXGI_OUTPUT_DESC1     out_desc1= {};

  while (SUCCEEDED (pFactory->EnumAdapters (uiAdapter++, &pAdapter.p)))
  {
    CComPtr <IDXGIOutput> pOutput;
    UINT                 uiOutput = 0;

    while (SUCCEEDED (pAdapter->EnumOutputs (uiOutput++, &pOutput)))
    {
      pOutput->GetDesc (&out_desc);

      CComQIPtr <IDXGIOutput6>
          pOutput6 (pOutput);
      if (pOutput6.p != nullptr)
      {
        pOutput6->GetDesc1 (&out_desc1);
      }

      if (out_desc.AttachedToDesktop && out_desc.Monitor == hMonitor)
      {
        pMonitorOutput = pOutput;
        break;
      }

      pOutput = nullptr;
    }

    if (pMonitorOutput != nullptr)
      break;

    pAdapter = nullptr;
  }

    wchar_t* wszExtension =
    PathFindExtensionW (wszFileName);

  wchar_t wszImplicitFileName [MAX_PATH] = { };
  wcscpy (wszImplicitFileName, wszFileName);

  // For silly users who don't give us filenames...
  if (! wszExtension)
  {
    PathAddExtension (wszImplicitFileName, defaultSDRFileExt.c_str ());
    wszExtension =
      PathFindExtensionW (wszImplicitFileName);
  }


  using namespace DirectX;

//float mastering_max_nits = out_desc1.MaxLuminance;
//float mastering_sdr_nits = SKIF_Util_GetSDRWhiteLevel (out_desc1.Monitor);

  const Image* pOutputImage = scratch_image.GetImages ();

  XMVECTOR maxLum   = XMVectorZero          (),
           minLum   = XMVectorSplatInfinity (),
           maxICtCp = XMVectorZero          ();

  bool is_hdr = false;

  ScratchImage scrgb;
  ScratchImage final_sdr;

  if (image.format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
      image.format == DXGI_FORMAT_R32G32B32A32_FLOAT)
  {
    is_hdr = true;

    if (FAILED (scrgb.InitializeFromImage (*scratch_image.GetImages ())))
      return E_INVALIDARG;
  }

  bool bPrefer10bpcAs48bpp = false;
  bool bPrefer10bpcAs32bpp = false;

  GUID      wic_codec;
  WIC_FLAGS wic_flags = WIC_FLAGS_DITHER_DIFFUSION | (force_sRGB ? WIC_FLAGS_FORCE_SRGB : WIC_FLAGS_NONE);

  if (StrStrIW (wszExtension, L"jpg") ||
      StrStrIW (wszExtension, L"jpeg"))
  {
    wic_codec = GetWICCodec (WIC_CODEC_JPEG);

    if (DirectX::BitsPerColor (image.format) == 10 ||
        DirectX::BitsPerColor (image.format) == 16)
    {
      if (! is_hdr)
      {
        if (FAILED (scrgb.InitializeFromImage (*scratch_image.GetImages ())))
          return E_INVALIDARG;

        TransformImage ( *scratch_image.GetImages (),
           [&]( _Out_writes_ (width)       XMVECTOR* outPixels,
                 _In_reads_  (width) const XMVECTOR* inPixels,
                                           size_t    width,
                                           size_t )
          {
            for (size_t j = 0; j < width; ++j)
            {
              XMVECTOR value =
               inPixels [j];
              outPixels [j] =
                XMColorRGBToSRGB (
                  XMVectorSaturate (value)
                );
            }
          }, scrgb
        );

        pOutputImage = scrgb.GetImages ();
      }
    }
  }

  else if (StrStrIW (wszExtension, L"png"))
  {
    wic_codec           = GetWICCodec (WIC_CODEC_PNG);
    bPrefer10bpcAs48bpp = is_hdr;

    wic_flags |= WIC_FLAGS_FORCE_SRGB;
    wic_flags |= WIC_FLAGS_DEFAULT_SRGB;

    if (DirectX::BitsPerColor (image.format) == 10 ||
        DirectX::BitsPerColor (image.format) == 16)
    {
      if (! is_hdr)
      {
        bPrefer10bpcAs48bpp = true;

        if (FAILED (scrgb.InitializeFromImage (*scratch_image.GetImages ())))
          return E_INVALIDARG;

        TransformImage ( *scratch_image.GetImages (),
           [&]( _Out_writes_ (width)       XMVECTOR* outPixels,
                 _In_reads_  (width) const XMVECTOR* inPixels,
                                           size_t    width,
                                           size_t )
          {
            for (size_t j = 0; j < width; ++j)
            {
              XMVECTOR value =
               inPixels [j];
              outPixels [j] =
                XMColorRGBToSRGB (
                  XMVectorSaturate (value)
                );
            }
          }, scrgb
        );

        pOutputImage = scrgb.GetImages ();
      }
    }
  }

  else if (StrStrIW (wszExtension, L"bmp"))
  {
    wic_codec = GetWICCodec (WIC_CODEC_BMP);

    if (DirectX::BitsPerColor (image.format) == 10 ||
        DirectX::BitsPerColor (image.format) == 16)
    {
      if (! is_hdr)
      {
        if (FAILED (Convert (*scratch_image.GetImages (), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DirectX::TEX_FILTER_DEFAULT, 0.0f, scrgb)))
          return E_INVALIDARG;

        pOutputImage = scrgb.GetImages ();
      }
    }
  }

  else if (StrStrIW (wszExtension, L"tiff"))
  {
    wic_codec           = GetWICCodec (WIC_CODEC_TIFF);
    bPrefer10bpcAs48bpp = false; // ?
    bPrefer10bpcAs32bpp = false; // ?

    if (DirectX::BitsPerColor (image.format) == 10 ||
        DirectX::BitsPerColor (image.format) == 16)
    {
      if (! is_hdr)
      {
        if (FAILED (scrgb.InitializeFromImage (*scratch_image.GetImages ())))
          return E_INVALIDARG;

        TransformImage ( *scratch_image.GetImages (),
           [&]( _Out_writes_ (width)       XMVECTOR* outPixels,
                 _In_reads_  (width) const XMVECTOR* inPixels,
                                           size_t    width,
                                           size_t )
          {
            for (size_t j = 0; j < width; ++j)
            {
              XMVECTOR value =
               inPixels [j];
              outPixels [j] =
                value;
            }
          }, scrgb
        );

        pOutputImage = scrgb.GetImages ();
      }
    }
  }

  // Probably ignore this
  else if (StrStrIW (wszExtension, L"hdp") ||
           StrStrIW (wszExtension, L"jxr"))
  {
    wic_codec           = GetWICCodec (WIC_CODEC_WMP);
    bPrefer10bpcAs32bpp = is_hdr;
  }

  // AVIF technically works for SDR... do we want to support it?
  //  If we do, WIC won't help us, however.

  else
  {
    return E_UNEXPECTED;
  }

  if (is_hdr)
  {
    if (bPrefer10bpcAs48bpp ||
        bPrefer10bpcAs32bpp)
    {
      wic_flags |= WIC_FLAGS_FORCE_SRGB;
    }

    ScratchImage tonemapped_hdr;
    ScratchImage tonemapped_copy;

    PLOG_INFO << "SKIV_Image_TonemapToSDR ( ): EvaluateImageBegin";

    EvaluateImage ( scrgb.GetImages     (),
                    scrgb.GetImageCount (),
                    scrgb.GetMetadata   (),
    [&](const XMVECTOR* pixels, size_t width, size_t y)
    {
      UNREFERENCED_PARAMETER(y);

      for (size_t j = 0; j < width; ++j)
      {
        XMVECTOR v = *pixels++;

        v =
          XMVector3Transform (v, c_from709toXYZ);

        maxLum =
          XMVectorReplicate (XMVectorGetY (XMVectorMax (v, maxLum)));

        minLum =
          XMVectorReplicate (XMVectorGetY (XMVectorMin (v, minLum))); 
      }
    });

    auto        luminance_freq = std::make_unique <uint32_t []> (65536);
    ZeroMemory (luminance_freq.get (),     sizeof (uint32_t)  *  65536);

    const float fLumRange =
      XMVectorGetY (maxLum) -
      XMVectorGetY (minLum);

    EvaluateImage ( scrgb.GetImages     (),
                    scrgb.GetImageCount (),
                    scrgb.GetMetadata   (),
    [&](const XMVECTOR* pixels, size_t width, size_t y)
    {
      UNREFERENCED_PARAMETER(y);

      for (size_t j = 0; j < width; ++j)
      {
        XMVECTOR v = *pixels++;

        v =
          XMVector3Transform (v, c_from709toXYZ);

        luminance_freq [
          std::clamp ( (int)
            std::roundf (
              (XMVectorGetY (v) - XMVectorGetY (minLum)) /
                                              (fLumRange / 65536.0f) ),
                                                        0, 65535 ) ]++;
      }
    });

          double percent  = 100.0;
    const double img_size = (double)scrgb.GetMetadata ().width *
                            (double)scrgb.GetMetadata ().height;

    for (auto i = 65535; i >= 0; --i)
    {
      percent -=
        100.0 * ((double)luminance_freq [i] / img_size);

      if (percent <= 99.825)
      {
        PLOG_INFO << "99.825th percentile luminance: " <<
          80.0f * (XMVectorGetY (minLum) + (fLumRange * ((float)i / 65536.0f)))
                                                      << " nits";

        maxLum =
          XMVectorReplicate (
            XMVectorGetY (minLum) + (fLumRange * ((float)i / 65536.0f))
          );

        break;
      }
    }

    PLOG_INFO << "SKIV_Image_TonemapToSDR ( ): EvaluateImageEnd";

    // After tonemapping, re-normalize the image to preserve peak white,
    //   this is important in cases where the maximum luminance was < 1000 nits
    XMVECTOR maxTonemappedRGB = g_XMZero;

    // If it's too bright, don't bother trying to tonemap the full range...
    const float _maxNitsToTonemap = 125.0f;

    const float SDR_YInPQ =
      SKIV_Image_LinearToPQY (1.5f);

    const float  maxYInPQ =
      std::max (SDR_YInPQ,
        SKIV_Image_LinearToPQY (std::min (_maxNitsToTonemap, XMVectorGetY (maxLum)))
      );

    bool needs_tonemapping = false;

    if (XMVectorGetY (maxLum) > 1.01f)
    {
      needs_tonemapping = true;
    }

    TransformImage ( scrgb.GetImages     (),
                     scrgb.GetImageCount (),
                     scrgb.GetMetadata   (),
      [&](XMVECTOR* outPixels, const XMVECTOR* inPixels, size_t width, size_t y)
      {
        UNREFERENCED_PARAMETER(y);

        auto TonemapHDR = [](float L, float Lc, float Ld) -> float
        {
          float a = (  Ld / pow (Lc, 2.0f));
          float b = (1.0f / Ld);

          return
            L * (1 + a * L) / (1 + b * L);
        };

        for (size_t j = 0; j < width; ++j)
        {
          XMVECTOR value = inPixels [j];

          if (needs_tonemapping)
          {
          XMVECTOR ICtCp =
            SKIV_Image_Rec709toICtCp (value);

          float Y_in  = std::max (XMVectorGetX (ICtCp), 0.0f);
          float Y_out = 1.0f;

          Y_out =
            TonemapHDR (Y_in, maxYInPQ, SDR_YInPQ);

          if (Y_out + Y_in > 0.0f)
          {
            ICtCp.m128_f32 [0] =
              std::pow (Y_in, 1.18f);

            float I0      = XMVectorGetX (ICtCp);
            float I1      = 0.0f;
            float I_scale = 0.0f;

            ICtCp.m128_f32 [0] *=
              std::max ((Y_out / Y_in), 0.0f);

            I1 = XMVectorGetX (ICtCp);

            if (I0 != 0.0f && I1 != 0.0f)
            {
              I_scale =
                std::min (I0 / I1, I1 / I0);
            }

            ICtCp.m128_f32 [1] *= I_scale;
            ICtCp.m128_f32 [2] *= I_scale;
          }

          value =
            SKIV_Image_ICtCptoRec709 (ICtCp);

          maxTonemappedRGB =
            XMVectorMax (maxTonemappedRGB, XMVectorMax (value, g_XMZero));
          }

          if (bPrefer10bpcAs48bpp || bPrefer10bpcAs32bpp)
               outPixels [j] = XMVectorSaturate (value);
          else outPixels [j] = XMVectorSaturate (value);
        }
      }, tonemapped_hdr
    );

    float fMaxR = XMVectorGetX (maxTonemappedRGB);
    float fMaxG = XMVectorGetY (maxTonemappedRGB);
    float fMaxB = XMVectorGetZ (maxTonemappedRGB);

    if (false)
        //( fMaxR <  1.0f ||
        //  fMaxG <  1.0f ||
        //  fMaxB <  1.0f ) &&
        //( fMaxR >= 1.0f ||
        //  fMaxG >= 1.0f ||
        //  fMaxB >= 1.0f ))
    {
#ifdef GAMUT_MAPPING_WARNING
      SK_LOGi0 (
        L"After tone mapping, maximum RGB was %4.2fR %4.2fG %4.2fB -- "
        L"SDR image will be normalized to min (R|G|B) and clipped.",
          fMaxR, fMaxG, fMaxB
      );
#endif

      float fSmallestComp =
        std::min ({fMaxR, fMaxG, fMaxB});

      float fRescale =
        (1.0f / fSmallestComp);

      XMVECTOR vNormalizationScale =
        XMVectorReplicate (fRescale);

      PLOG_INFO << "SKIV_Image_TonemapToSDR ( ): TransformImageBegin";
      TransformImage (*tonemapped_hdr.GetImages (),
        [&]( _Out_writes_ (width)       XMVECTOR* outPixels,
              _In_reads_  (width) const XMVECTOR* inPixels,
                                        size_t    width,
                                        size_t )
        {
          for (size_t j = 0; j < width; ++j)
          {
            XMVECTOR value =
             inPixels [j];
            outPixels [j] =
              XMVectorSaturate (
                XMVectorMultiply (value, vNormalizationScale)
              );
          }
        }, tonemapped_copy
      );

      std::swap (tonemapped_hdr, tonemapped_copy);
    }

    if (FAILED (DirectX::Convert (*tonemapped_hdr.GetImages (), bPrefer10bpcAs48bpp ? DXGI_FORMAT_R16G16B16A16_UNORM :
                                                                bPrefer10bpcAs32bpp ? DXGI_FORMAT_R10G10B10A2_UNORM  :
                                                                                      DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                                  (TEX_FILTER_FLAGS)0x200000FF, 1.0f, final_sdr)))
    {
      return E_UNEXPECTED;
    }

    pOutputImage =
      final_sdr.GetImages ();
  }

  ///DirectX::TexMetadata              orig_tex_metadata;
  ///CComPtr <IWICMetadataQueryReader> pQueryReader;
  ///
  ///DirectX::GetMetadataFromWICFile (wszOriginalFile, DirectX::WIC_FLAGS_NONE, orig_tex_metadata, [&](IWICMetadataQueryReader *pMQR){
  ///  pQueryReader = pMQR;
  ///});

  return
    DirectX::SaveToWICFile (*pOutputImage, wic_flags, wic_codec,
                      wszImplicitFileName, bPrefer10bpcAs48bpp ? &GUID_WICPixelFormat48bppRGB       :
                                           bPrefer10bpcAs32bpp ? &GUID_WICPixelFormat32bppBGR101010 :
                                                                 nullptr, SK_WIC_SetMaximumQuality);
}

HRESULT
SKIV_Image_SaveToDisk_HDR (const DirectX::Image& image, const wchar_t* wszFileName)
{
  using namespace DirectX;

  const Image* pOutputImage = &image;

  if (image.format != DXGI_FORMAT_R16G16B16A16_FLOAT &&
      image.format != DXGI_FORMAT_R32G32B32A32_FLOAT)
  {
    // SKIV always uses scRGB internally for HDR, any other format
    //   can't be HDR...
    return E_NOTIMPL;
  }

  wchar_t* wszExtension =
    PathFindExtensionW (wszFileName);

  wchar_t wszImplicitFileName [MAX_PATH] = { };
  wcscpy (wszImplicitFileName, wszFileName);

  // For doofus users who don't give us filenames...
  if (! wszExtension)
  {
    PathAddExtension (wszImplicitFileName, defaultHDRFileExt.c_str ());
    wszExtension =
      PathFindExtensionW (wszImplicitFileName);
  }

  GUID wic_codec;

  if (StrStrIW (wszExtension, L"jxr"))
  {
    wic_codec = GetWICCodec (WIC_CODEC_WMP);
  }

#ifdef _M_X64
  else if (StrStrIW (wszExtension, L"exr"))
  {
    using namespace DirectX;

    if (SUCCEEDED (SaveToEXRFile (image, wszImplicitFileName)))
    {
      return S_OK;
    }
  }
#endif

  else if (StrStrIW (wszExtension, L"hdr"))
  {
    using namespace DirectX;

    if (SUCCEEDED (SaveToHDRFile (image, wszImplicitFileName)))
    {
      return S_OK;
    }
  }

  else if (StrStrIW (wszExtension, L"png"))
  {
    DirectX::ScratchImage                  png_img;
    if (SKIV_HDR_ConvertImageToPNG (image, png_img))
    {
      if (SKIV_HDR_SavePNGToDisk (wszImplicitFileName, png_img.GetImages (), &image, nullptr, true))
      {
        return S_OK;
      }
    }
  }

#ifdef _M_X64
  else if (StrStrIW (wszExtension, L"jxl"))
  {
    extern bool isJXLDecoderAvailable (void);
    if (!       isJXLDecoderAvailable ())
      return E_NOTIMPL;

    static HMODULE hModJXL;
    SK_RunOnce (   hModJXL = LoadLibraryW (L"jxl.dll"));

    static HMODULE hModJXLThreads;
    SK_RunOnce (   hModJXLThreads = LoadLibraryW (L"jxl_threads.dll"));

    using JxlEncoderCreate_pfn                               = JxlEncoder*              (*)(const JxlMemoryManager* memory_manager);
    using JxlEncoderDestroy_pfn                              = void                     (*)(JxlEncoder* enc);
    using JxlEncoderCloseInput_pfn                           = void                     (*)(JxlEncoder* enc);
    using JxlEncoderProcessOutput_pfn                        = JxlEncoderStatus         (*)(JxlEncoder* enc, uint8_t** next_out, size_t* avail_out);
    using JxlEncoderFrameSettingsCreate_pfn                  = JxlEncoderFrameSettings* (*)(JxlEncoder* enc, const JxlEncoderFrameSettings* source);
    using JxlEncoderInitBasicInfo_pfn                        = void                     (*)(JxlBasicInfo* info);
    using JxlEncoderSetBasicInfo_pfn                         = JxlEncoderStatus         (*)(JxlEncoder* enc, const JxlBasicInfo* info);
    using JxlEncoderAddImageFrame_pfn                        = JxlEncoderStatus         (*)(const JxlEncoderFrameSettings* frame_settings, const JxlPixelFormat* pixel_format, const void* buffer, size_t size);
    using JxlEncoderSetColorEncoding_pfn                     = JxlEncoderStatus         (*)(JxlEncoder* enc, const JxlColorEncoding* color);
    using JxlEncoderFrameSettingsSetOption_pfn               = JxlEncoderStatus         (*)(JxlEncoderFrameSettings *frame_settings, JxlEncoderFrameSettingId option, int64_t value);
    using JxlEncoderSetParallelRunner_pfn                    = JxlEncoderStatus         (*)(JxlEncoder* enc, JxlParallelRunner parallel_runner, void* parallel_runner_opaque);

    using JxlThreadParallelRunner_pfn                        = JxlParallelRetCode       (*)(void* runner_opaque, void* jpegxl_opaque, JxlParallelRunInit init, JxlParallelRunFunction func, uint32_t start_range, uint32_t end_range);
    using JxlThreadParallelRunnerCreate_pfn                  = void*                    (*)(const JxlMemoryManager* memory_manager, size_t num_worker_threads);
    using JxlThreadParallelRunnerDestroy_pfn                 = void                     (*)(void* runner_opaque);
    using JxlThreadParallelRunnerDefaultNumWorkerThreads_pfn = size_t                   (*)(void);

    static JxlEncoderCreate_pfn                 jxlEncoderCreate                 = (JxlEncoderCreate_pfn)                GetProcAddress (hModJXL, "JxlEncoderCreate");
    static JxlEncoderDestroy_pfn                jxlEncoderDestroy                = (JxlEncoderDestroy_pfn)               GetProcAddress (hModJXL, "JxlEncoderDestroy");
    static JxlEncoderCloseInput_pfn             jxlEncoderCloseInput             = (JxlEncoderCloseInput_pfn)            GetProcAddress (hModJXL, "JxlEncoderCloseInput");
    static JxlEncoderProcessOutput_pfn          jxlEncoderProcessOutput          = (JxlEncoderProcessOutput_pfn)         GetProcAddress (hModJXL, "JxlEncoderProcessOutput");
    static JxlEncoderFrameSettingsCreate_pfn    jxlEncoderFrameSettingsCreate    = (JxlEncoderFrameSettingsCreate_pfn)   GetProcAddress (hModJXL, "JxlEncoderFrameSettingsCreate");
    static JxlEncoderInitBasicInfo_pfn          jxlEncoderInitBasicInfo          = (JxlEncoderInitBasicInfo_pfn)         GetProcAddress (hModJXL, "JxlEncoderInitBasicInfo");
    static JxlEncoderSetBasicInfo_pfn           jxlEncoderSetBasicInfo           = (JxlEncoderSetBasicInfo_pfn)          GetProcAddress (hModJXL, "JxlEncoderSetBasicInfo");
    static JxlEncoderAddImageFrame_pfn          jxlEncoderAddImageFrame          = (JxlEncoderAddImageFrame_pfn)         GetProcAddress (hModJXL, "JxlEncoderAddImageFrame");
    static JxlEncoderSetColorEncoding_pfn       jxlEncoderSetColorEncoding       = (JxlEncoderSetColorEncoding_pfn)      GetProcAddress (hModJXL, "JxlEncoderSetColorEncoding");
    static JxlEncoderFrameSettingsSetOption_pfn jxlEncoderFrameSettingsSetOption = (JxlEncoderFrameSettingsSetOption_pfn)GetProcAddress (hModJXL, "JxlEncoderFrameSettingsSetOption");
    static JxlEncoderSetParallelRunner_pfn      jxlEncoderSetParallelRunner      = (JxlEncoderSetParallelRunner_pfn)     GetProcAddress (hModJXL, "JxlEncoderSetParallelRunner");

    static JxlThreadParallelRunner_pfn                        jxlThreadParallelRunner                        = (JxlThreadParallelRunner_pfn)                       GetProcAddress (hModJXLThreads, "JxlThreadParallelRunner");
    static JxlThreadParallelRunnerCreate_pfn                  jxlThreadParallelRunnerCreate                  = (JxlThreadParallelRunnerCreate_pfn)                 GetProcAddress (hModJXLThreads, "JxlThreadParallelRunnerCreate");
    static JxlThreadParallelRunnerDestroy_pfn                 jxlThreadParallelRunnerDestroy                 = (JxlThreadParallelRunnerDestroy_pfn)                GetProcAddress (hModJXLThreads, "JxlThreadParallelRunnerDestroy");
    static JxlThreadParallelRunnerDefaultNumWorkerThreads_pfn jxlThreadParallelRunnerDefaultNumWorkerThreads = (JxlThreadParallelRunnerDefaultNumWorkerThreads_pfn)GetProcAddress (hModJXLThreads, "JxlThreadParallelRunnerDefaultNumWorkerThreads");

    using JxlEncoderSetFrameLossless_pfn    = JxlEncoderStatus (*)(JxlEncoderFrameSettings *frame_settings, JXL_BOOL lossless);
    using JxlEncoderSetFrameDistance_pfn    = JxlEncoderStatus (*)(JxlEncoderFrameSettings *frame_settings, float distance);
    using JxlEncoderSetFrameBitDepth_pfn    = JxlEncoderStatus (*)(JxlEncoderFrameSettings *frame_settings, const JxlBitDepth *bit_depth);
    using JxlEncoderDistanceFromQuality_pfn = float            (*)(float quality);

    static JxlEncoderSetFrameLossless_pfn    jxlEncoderSetFrameLossless    = (JxlEncoderSetFrameLossless_pfn)   GetProcAddress (hModJXL, "JxlEncoderSetFrameLossless");  
    static JxlEncoderSetFrameDistance_pfn    jxlEncoderSetFrameDistance    = (JxlEncoderSetFrameDistance_pfn)   GetProcAddress (hModJXL, "JxlEncoderSetFrameDistance");    
    static JxlEncoderSetFrameBitDepth_pfn    jxlEncoderSetFrameBitDepth    = (JxlEncoderSetFrameBitDepth_pfn)   GetProcAddress (hModJXL, "JxlEncoderSetFrameBitDepth");  
    static JxlEncoderDistanceFromQuality_pfn jxlEncoderDistanceFromQuality = (JxlEncoderDistanceFromQuality_pfn)GetProcAddress (hModJXL, "JxlEncoderDistanceFromQuality");

    bool succeeded = false;

    if ( jxlEncoderCreate                               == nullptr ||
         jxlThreadParallelRunnerCreate                  == nullptr ||
         jxlThreadParallelRunnerDefaultNumWorkerThreads == nullptr )
    {
      PLOG_ERROR << "JPEG-XL library unavailable";
      return E_NOINTERFACE;
    }

    auto jxl_encoder = jxlEncoderCreate              (nullptr);
    auto jxl_runner  = jxlThreadParallelRunnerCreate (nullptr,
      jxlThreadParallelRunnerDefaultNumWorkerThreads ());

    for (;;)
    {
      if ( jxl_encoder == nullptr ||
           jxl_runner  == nullptr )
        break;

      if ( JXL_ENC_SUCCESS !=
             jxlEncoderSetParallelRunner ( jxl_encoder,
                                             jxlThreadParallelRunner,
                                               jxl_runner ) )
      {
        PLOG_ERROR << "JxlEncoderSetParallelRunner failed";
        break;
      }

#if 0
      JxlDataType type = JXL_TYPE_FLOAT16;
      size_t      size = sizeof (uint16_t);

      std::vector <uint16_t> fp_pixels (image.width * image.height * 3);

      auto fp_pixel_comp =
        fp_pixels.begin ();

      using namespace DirectX::PackedVector;

      EvaluateImage ( image,
        [&](const XMVECTOR* pixels, size_t width, size_t y)
        {
          UNREFERENCED_PARAMETER(y);

          for (size_t j = 0; j < width; ++j)
          {
            XMHALF4 v (pixels++->m128_f32);

            *fp_pixel_comp++ = v.x;
            *fp_pixel_comp++ = v.y;
            *fp_pixel_comp++ = v.z;
          }
        }
      );
#else
      JxlDataType type = JXL_TYPE_FLOAT;
      size_t      size = sizeof (float);

      std::vector <float> fp_pixels (image.width * image.height * 3);

      auto fp_pixel_comp =
        fp_pixels.begin ();

      EvaluateImage ( image,
        [&](const XMVECTOR* pixels, size_t width, size_t y)
        {
          UNREFERENCED_PARAMETER(y);

          for (size_t j = 0; j < width; ++j)
          {
            XMVECTOR v =
              *pixels++;

            *fp_pixel_comp++ = XMVectorGetX (v);
            *fp_pixel_comp++ = XMVectorGetY (v);
            *fp_pixel_comp++ = XMVectorGetZ (v);
          }
        }
      );
#endif

      JxlPixelFormat pixel_format =
        { 3, type, JXL_NATIVE_ENDIAN, 0 };

      JxlBasicInfo              basic_info = { };
      jxlEncoderInitBasicInfo (&basic_info);

      const bool bLossless = false;

      basic_info.xsize                    = static_cast <uint32_t> (image.width);
      basic_info.ysize                    = static_cast <uint32_t> (image.height);
      basic_info.bits_per_sample          = static_cast <uint32_t> (DirectX::BitsPerColor (image.format));
      basic_info.exponent_bits_per_sample =                         DirectX::BitsPerColor (image.format) == 32 ? 8 : 5;
      basic_info.uses_original_profile    = bLossless ? JXL_TRUE : JXL_FALSE;

      if ( JXL_ENC_SUCCESS !=
             jxlEncoderSetBasicInfo ( jxl_encoder, &basic_info) )
      {
        PLOG_ERROR << "JxlEncoderSetBasicInfo failed";
        break;
      }

      JxlColorEncoding color_encoding = { };

      color_encoding.color_space       = JXL_COLOR_SPACE_RGB;
      color_encoding.white_point       = JXL_WHITE_POINT_D65;
      color_encoding.primaries         = JXL_PRIMARIES_SRGB;
      color_encoding.transfer_function = JXL_TRANSFER_FUNCTION_LINEAR;
      color_encoding.rendering_intent  = JXL_RENDERING_INTENT_PERCEPTUAL;

      if ( JXL_ENC_SUCCESS !=
             jxlEncoderSetColorEncoding (jxl_encoder, &color_encoding) )
      {
        PLOG_ERROR << "JxlEncoderSetColorEncoding failed";
        break;
      }

      JxlEncoderFrameSettings* frame_settings =
        jxlEncoderFrameSettingsCreate (jxl_encoder, nullptr);

      jxlEncoderSetFrameLossless       (frame_settings, bLossless ? JXL_TRUE : JXL_FALSE);
      jxlEncoderSetFrameDistance       (frame_settings, 0.05f);//jxlEncoderDistanceFromQuality (100.0f));
      jxlEncoderFrameSettingsSetOption (frame_settings, JXL_ENC_FRAME_SETTING_EFFORT, 7);

      if ( JXL_ENC_SUCCESS !=
             jxlEncoderAddImageFrame ( frame_settings, &pixel_format,
               static_cast <const void *> (fp_pixels.data ()),
                                    size * fp_pixels.size () ) )
      {
        PLOG_ERROR << "JxlEncoderAddImageFrame failed";
        break;
      }

      jxlEncoderCloseInput (jxl_encoder);

      std::vector <uint8_t> output (64);

      uint8_t* next_out  = output.data ();
      size_t   avail_out = output.size () - (next_out - output.data ());

      JxlEncoderStatus process_result = JXL_ENC_NEED_MORE_OUTPUT;

      while (process_result == JXL_ENC_NEED_MORE_OUTPUT)
      {
        process_result =
          jxlEncoderProcessOutput (jxl_encoder, &next_out, &avail_out);

        if (process_result == JXL_ENC_NEED_MORE_OUTPUT)
        {
          size_t offset = next_out - output.data ();

          output.resize (output.size () * 2);

          next_out  = output.data () + offset;
          avail_out = output.size () - offset;
        }
      }

      output.resize (next_out - output.data ());

      if (JXL_ENC_SUCCESS != process_result)
      {
        PLOG_ERROR << "JxlEncoderProcessOutput failed";
        break;
      }

      FILE* fOutput =
        _wfopen (wszImplicitFileName, L"wb");

      if (fOutput != nullptr)
      {
        fwrite (output.data (), output.size (), 1, fOutput);
        fclose (fOutput);

        PLOG_INFO << "JPEG-XL Encode Finished";

        succeeded = true;
      }

      break;
    }

    if (jxl_encoder != nullptr)
      jxlEncoderDestroy (jxl_encoder);

    if (jxl_runner != nullptr)
      jxlThreadParallelRunnerDestroy (jxl_runner);

    return
      succeeded ? S_OK : E_FAIL;
  }
#endif

  else if (StrStrIW (wszExtension, L"avif"))
  {
    // Not yet, sorry...
    return E_NOTIMPL;
  }

  else
  {
    // What the hell is this?
    return E_UNEXPECTED;
  }

  return
    DirectX::SaveToWICFile (*pOutputImage, DirectX::WIC_FLAGS_NONE, wic_codec,
                      wszImplicitFileName, nullptr, SK_WIC_SetMaximumQuality);
}

HRESULT
SKIV_Image_CaptureDesktop (DirectX::ScratchImage& image, POINT point, int flags)
{
  SKIV_DesktopImage.clear ();

  std::ignore = image;
  std::ignore = flags;

  HRESULT res = E_NOT_VALID_STATE;

  auto pDevice =
    SKIF_D3D11_GetDevice ();

  if (! pDevice)
    return res;

  CComPtr <IDXGIFactory> pFactory;
  CreateDXGIFactory (IID_IDXGIFactory, (void **)&pFactory.p);

  if (! pFactory)
    return E_NOTIMPL;

  CComPtr <IDXGIAdapter> pAdapter;
  UINT                  uiAdapter = 0;

  CComPtr <IDXGIOutput> pCursorOutput;
  DXGI_OUTPUT_DESC      out_desc = {};
  DXGI_OUTPUT_DESC1     out_desc1= {};

  while (SUCCEEDED (pFactory->EnumAdapters (uiAdapter++, &pAdapter.p)))
  {
    CComPtr <IDXGIOutput> pOutput;
    UINT                 uiOutput = 0;

    while (SUCCEEDED (pAdapter->EnumOutputs (uiOutput++, &pOutput)))
    {
      pOutput->GetDesc (&out_desc);

      CComQIPtr <IDXGIOutput6>
          pOutput6 (pOutput);
      if (pOutput6.p != nullptr)
      {
        pOutput6->GetDesc1 (&out_desc1);
      }

      if (out_desc.AttachedToDesktop && PtInRect (&out_desc.DesktopCoordinates, point))
      {
        pCursorOutput = pOutput;
        break;
      }

      pOutput = nullptr;
    }

    if (pCursorOutput != nullptr)
      break;

    pAdapter = nullptr;
  }

  if (! pCursorOutput)
    return E_UNEXPECTED;

  CComQIPtr <IDXGIOutput5> pOutput5 (pCursorOutput);
  CComQIPtr <IDXGIOutput1> pOutput1; // Always DXGI_FORMAT_B8G8R8A8_UNORM

  // Down-level interfaces support duplication, but for HDR we want Output5
  if (! pOutput5 ) //&& ! pOutput1)
  {
    PLOG_VERBOSE << "IDXGIOutput5 is unavailable, falling back to using IDXGIOutput1.";

    pOutput1 = pCursorOutput;

    if (! pOutput1)
      return E_NOTIMPL;
  }

  // The ordering goes from the highest prioritized format to the lowest
  DXGI_FORMAT capture_formats [] = {
    // SDR:
    DXGI_FORMAT_B8G8R8X8_UNORM_SRGB,
    DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, // Prefer SRGB formats as even non-SRGB formats use sRGB gamma
    DXGI_FORMAT_B8G8R8X8_UNORM,
    DXGI_FORMAT_R8G8B8A8_UNORM,

    // HDR:
    DXGI_FORMAT_R10G10B10A2_UNORM,
    DXGI_FORMAT_R16G16B16A16_FLOAT,
    DXGI_FORMAT_R32G32B32A32_FLOAT,

    // The list of supported formats should always contain DXGI_FORMAT_B8G8R8A8_UNORM,
    //   as this is the most common format for the desktop.
    //DXGI_FORMAT_B8G8R8A8_UNORM
  };

  static constexpr int num_sdr_formats = 4;
  static constexpr int num_all_formats = 7;

  HMONITOR hMon = MonitorFromPoint (point, MONITOR_DEFAULTTONEAREST);

  SKIF_Util_UpdateMonitors ();

  extern bool SKIF_Util_IsHDRActive (HMONITOR hMonitor);
  bool bHDR = SKIF_Util_IsHDRActive (hMon);

  CComPtr <IDXGIOutputDuplication> pDuplicator;

  if (pOutput5)
    pOutput5->DuplicateOutput1 (pDevice, 0x0, bHDR ? num_all_formats
                                                   : num_sdr_formats,
                                                     capture_formats, &pDuplicator.p);
  else if (pOutput1)
    pOutput1->DuplicateOutput  (pDevice, &pDuplicator.p);

  if (! pDuplicator)
    return E_NOTIMPL;

  DXGI_OUTDUPL_DESC       dup_desc   = { };
  DXGI_OUTDUPL_FRAME_INFO frame_info = { };

  pDuplicator->GetDesc (&dup_desc);

  CComPtr <IDXGIResource> pDuplicatedResource;
  DWORD timeout = SKIF_Util_timeGetTime1 ( ) + 5000;

  while (true)
  {
    // Capture the next frame
    HRESULT hr = pDuplicator->AcquireNextFrame (150, &frame_info, &pDuplicatedResource.p);

    // DXGI_ERROR_ACCESS_LOST if the desktop duplication interface is invalid.
    // The desktop duplication interface typically becomes invalid when a different
    //   type of image is displayed on the desktop.
    // In this situation, the application must release the IDXGIOutputDuplication interface
    //   and create a new IDXGIOutputDuplication for the new content.
    if (hr == DXGI_ERROR_ACCESS_LOST)
    {
      pDuplicator.Release();

      if (pOutput5)
        pOutput5->DuplicateOutput1 (pDevice, 0x0, bHDR ? num_all_formats
                                                       : num_sdr_formats,
                                                         capture_formats, &pDuplicator.p);
      else if (pOutput1)
        pOutput1->DuplicateOutput  (pDevice, &pDuplicator.p);
    }

    else if (FAILED (hr))
      PLOG_WARNING.printf ("Unexpected failure: %ws (HRESULT=%x)", _com_error(hr).ErrorMessage(), hr);

    // A non-zero value indicates that the desktop image was updated since an application last called the
    //   IDXGIOutputDuplication::AcquireNextFrame method to acquire the next frame of the desktop image.
    if (frame_info.LastPresentTime.QuadPart)
      break;

    // Release the prior captured frame, since it's invalid for our usage
    pDuplicator->ReleaseFrame ();

    // Fail after we have attempted to capture a new frame of the desktop for 5 seconds
    if (SKIF_Util_timeGetTime1 ( ) > timeout)
      break;
  }

  // This means the timeout was reached without a successful capture
  if (! frame_info.LastPresentTime.QuadPart)
    return E_UNEXPECTED;

  if (! pDuplicatedResource)
    return E_UNEXPECTED;

  CComQIPtr <IDXGISurface>    pSurface       (pDuplicatedResource);
  CComQIPtr <ID3D11Texture2D> pDuplicatedTex (pSurface);

  if (! pDuplicatedTex)
    return E_NOTIMPL;

  DXGI_SURFACE_DESC   surfDesc;
  pSurface->GetDesc (&surfDesc);

  CComPtr <ID3D11Texture2D> pStagingTex;
  CComPtr <ID3D11Texture2D> pDesktopImage; // For rendering during snipping

  // DXGI_FORMAT_B8G8R8A8_UNORM (Windows 8.1 fallback code)
  if (surfDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM) // ! pOutput5
      surfDesc.Format  = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

  D3D11_TEXTURE2D_DESC
    texDesc                = { };
    texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    texDesc.Usage          = D3D11_USAGE_STAGING;
    texDesc.ArraySize      = 1;
    texDesc.MipLevels      = 1;
    texDesc.SampleDesc     = { .Count = 1, .Quality = 0 };
    texDesc.Format         = surfDesc.Format;
    texDesc.Width          = surfDesc.Width;
    texDesc.Height         = surfDesc.Height;

  CComPtr <ID3D11DeviceContext>  pDevCtx;
  pDevice->GetImmediateContext (&pDevCtx);

  if (pDevCtx == nullptr)
    return E_UNEXPECTED;

  texDesc.CPUAccessFlags = 0x0;
  texDesc.Usage          = D3D11_USAGE_DEFAULT;
  texDesc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;

  if (FAILED (pDevice->CreateTexture2D (&texDesc, nullptr, &pDesktopImage.p)))
  {
    pDuplicator->ReleaseFrame ();
    return E_UNEXPECTED;
  }

  D3D11_SHADER_RESOURCE_VIEW_DESC
    srvDesc                           = { };
    srvDesc.Format                    = texDesc.Format;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels       = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;

  pDevCtx->CopyResource             (pDesktopImage, pDuplicatedTex);
  pDevice->CreateShaderResourceView (pDesktopImage, &srvDesc, &SKIV_DesktopImage._srv);

  SKIV_DesktopImage._rotation    = dup_desc.Rotation;
  SKIV_DesktopImage._desktop_pos =
    ImVec2 (static_cast <float> (out_desc.DesktopCoordinates.left),
            static_cast <float> (out_desc.DesktopCoordinates.top));
  SKIV_DesktopImage._max_display_nits = out_desc1.MaxLuminance;
  SKIV_DesktopImage._sdr_display_nits = SKIF_Util_GetSDRWhiteLevel (out_desc1.Monitor);

  PLOG_VERBOSE << "Max Display Nits : " << SKIV_DesktopImage._max_display_nits;
  PLOG_VERBOSE << "SDR Display Nits : " << SKIV_DesktopImage._sdr_display_nits;

  pDevCtx->Flush ();

  pDuplicator->ReleaseFrame ();

  SKIV_DesktopImage.process ();

  return S_OK;
}

void
SKIV_Image_CaptureRegion (ImRect capture_area)
{
  HMONITOR hMonCaptured =
    MonitorFromPoint ({ static_cast <long> (capture_area.Min.x),
                        static_cast <long> (capture_area.Min.y) }, MONITOR_DEFAULTTONEAREST);

  MONITORINFO                    minfo = { .cbSize = sizeof (MONITORINFO) };
  GetMonitorInfo (hMonCaptured, &minfo);

  // Fixes snipping rectangles on non-primary (origin != 0,0) displays
  auto _AdjustCaptureAreaRelativeToDisplayOrigin = [&](void)
  {
    capture_area.Min.x -= minfo.rcMonitor.left;
    capture_area.Max.x -= minfo.rcMonitor.left;

    capture_area.Min.y -= minfo.rcMonitor.top;
    capture_area.Max.y -= minfo.rcMonitor.top;
  };

  _AdjustCaptureAreaRelativeToDisplayOrigin ();

  if (SKIV_DesktopImage._rotation == DXGI_MODE_ROTATION_ROTATE90 ||
      SKIV_DesktopImage._rotation == DXGI_MODE_ROTATION_ROTATE270)
  {
    const float height =
      static_cast <float> (minfo.rcMonitor.right  - minfo.rcMonitor.left),
                width  =
      static_cast <float> (minfo.rcMonitor.bottom - minfo.rcMonitor.top);

    std::swap (capture_area.Min.x, capture_area.Min.y);
    std::swap (capture_area.Max.x, capture_area.Max.y);

    const float capture_height =
      static_cast <float> (capture_area.Max.y - capture_area.Min.y),
                capture_width  =
      static_cast <float> (capture_area.Max.x - capture_area.Min.x);

    if (SKIV_DesktopImage._rotation == DXGI_MODE_ROTATION_ROTATE90)
    {
      capture_area.Min.y = height - capture_area.Max.y;
      capture_area.Max.y = height - capture_area.Max.y + capture_height;
    }

    else
    {
      std::ignore = capture_width;
      std::ignore = width;
      //capture_area.Min.x = width - capture_width;
      //capture_area.Max.x = width;
    }
  }

  const size_t
    x      = static_cast <size_t> (std::max (0.0f, capture_area.Min.x)),
    y      = static_cast <size_t> (std::max (0.0f, capture_area.Min.y)),
    width  = static_cast <size_t> (std::max (0.0f, capture_area.GetWidth  ())),
    height = static_cast <size_t> (std::max (0.0f, capture_area.GetHeight ()));

  const DirectX::Rect
    src_rect (x,y, width,height);

  extern CComPtr <ID3D11Device>
    SKIF_D3D11_GetDevice (bool bWait);

  auto pDevice = SKIF_D3D11_GetDevice (false);

  CComPtr <ID3D11DeviceContext>  pDevCtx;
  pDevice->GetImmediateContext (&pDevCtx.p);

  DirectX::ScratchImage captured_img;
  if (SUCCEEDED (DirectX::CaptureTexture (pDevice, pDevCtx, SKIV_DesktopImage._res, captured_img)))
  {
    PLOG_VERBOSE << "DirectX::CaptureTexture    ( ): SUCCEEDED";
    DirectX::ScratchImage subrect;

    if (SUCCEEDED (subrect.Initialize2D   ( captured_img.GetMetadata ().format, width, height, 1, 1)))
    {
      PLOG_VERBOSE << "subrect.Initialize2D       ( ): SUCCEEDED";

      if (SUCCEEDED (DirectX::CopyRectangle (*captured_img.GetImages (), src_rect,
                                                  *subrect.GetImages (), DirectX::TEX_FILTER_DEFAULT, 0, 0)))
      {
        PLOG_VERBOSE << "DirectX::CopyRectangle     ( ): SUCCEEDED";

        DirectX::ScratchImage rotated;
        const DirectX::Image* final = subrect.GetImages ();

        if (SKIV_DesktopImage._rotation > DXGI_MODE_ROTATION_IDENTITY)
        {
          DirectX::TEX_FR_FLAGS rotate_flags = DirectX::TEX_FR_ROTATE0;

          switch (SKIV_DesktopImage._rotation)
          {
            case DXGI_MODE_ROTATION_ROTATE90:
              rotate_flags = DirectX::TEX_FR_ROTATE90;
              break;

            case DXGI_MODE_ROTATION_ROTATE180:
              rotate_flags = DirectX::TEX_FR_ROTATE180;
              break;

            case DXGI_MODE_ROTATION_ROTATE270:
              rotate_flags = DirectX::TEX_FR_ROTATE270;
              break;
          }

          if (SUCCEEDED (DirectX::FlipRotate (*subrect.GetImages (), DirectX::TEX_FR_ROTATE90, rotated)))
          {
            PLOG_VERBOSE << "DirectX::FlipRotate        ( ): SUCCEEDED";

            final = rotated.GetImages ();
          } else
            PLOG_VERBOSE << "DirectX::FlipRotate        ( ): FAILED";
        }

        if (SKIV_Image_CopyToClipboard (final, true, SKIV_DesktopImage._hdr_image))
        {
          PLOG_VERBOSE << "SKIV_Image_CopyToClipboard ( ): SUCCEEDED";

          ImGui::InsertNotification (
            {
              ImGuiToastType::Info,
              3000,
              "Copied image to clipboard", ""
            }
          );
        }

        else {
          ImGui::InsertNotification (
            {
              ImGuiToastType::Error,
              3000,
              "Failed to copy image to clipboard", ""
            }
          );
          PLOG_WARNING << "SKIV_Image_CopyToClipboard ( ): FAILED";
        }
      } else
        PLOG_WARNING << "DirectX::CopyRectangle     ( ): FAILED";
    } else
      PLOG_WARNING << "subrect.Initialize2D       ( ): FAILED";
  } else
    PLOG_WARNING << "DirectX::CaptureTexture    ( ): FAILED";
}